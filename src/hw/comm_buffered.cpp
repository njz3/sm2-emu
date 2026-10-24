// SPDX-License-Identifier: GPL-3.0-or-later
//
// See comm_buffered.h. The handshake and the exchange follow Supermodel's
// CSimNetBoard (Handshake(), RunFrame()'s ready state); the connections its
// TCPSend/TCPReceive (a packet is its length, four bytes, then its bytes; an
// empty one tells the others a cabinet left), on sm2-emu's own sockets
// (core/net.h).

#include "hw/comm_buffered.h"

#include "core/log.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <utility>

namespace sm2::hw {

namespace {

// Checked by the handshake: every cabinet must run the same link. Changed with
// the link's packets, so that a ring never mixes two kinds.
constexpr u64 kGuid = 0x534d32425546'0001ull;  // "SM2BUF", 1

constexpr int kLinkTimeoutMs     = 10000;  // a cabinet silent this long: the link is broken
constexpr int kHandshakeStepMs   = 3000;   // each step of a handshake round, once started
constexpr u64 kHandshakeRetryMs  = 2000;   // the master's GUID sent again when not back
// The frames from the board enabled to taking part in a handshake: MAME's
// board links four seconds after it is enabled (its 0xe8 frames), and the
// games count on it, writing their role and their data's size meanwhile.
constexpr u32 kSettleFrames      = 0xe8;
constexpr int kConnectTimeoutMs  = 100;
constexpr u32 kMaxPacket         = 0x10000;

// Packet types, the payload's first byte.
constexpr u8 kGuidPacket    = 1;  // u64 GUID, u32 round
constexpr u8 kIndexPacket   = 2;  // u32 round, u8 total, u8 playable
constexpr u8 kSegmentPacket = 3;  // u8 sender's id, its data

u64 now_ms()
{
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}

}  // namespace

BufferedLink::BufferedLink(const std::string& local_ip, u16 port, std::string next_ip,
                           u16 next_port)
    : m_next_ip(std::move(next_ip)), m_next_port(next_port)
{
    if (!m_listen.open(local_ip, port)) {
        m_error = m_listen.last_error();
        return;
    }
    m_thread = std::thread([this] { connect_loop(); });
    SM2_INFO("m2comm: buffered link listening on %s:%u, connecting to %s:%u",
             local_ip.empty() ? "*" : local_ip.c_str(), port,
             m_next_ip.empty() ? "(none)" : m_next_ip.c_str(), m_next_port);
}

BufferedLink::~BufferedLink()
{
    if (m_state == State::Ready) {
        SM2_INFO("m2comm: buffered link: leaving; the other cabinets told");
        send_packet({});
    }
    m_state = State::Idle;
    m_quit  = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool BufferedLink::ok() const
{
    return m_listen.valid();
}

// The connections to the next cabinet and from the previous one, made again in
// the background whenever one is missing, so that nothing waits for them. A
// connection in use by the frame's thread is there.
void BufferedLink::connect_loop()
{
    bool logged = false;
    while (!m_quit) {
        bool send_ok = false;
        {
            std::unique_lock lock(m_send_mutex, std::try_to_lock);
            send_ok = !lock.owns_lock() || m_send.valid();
        }
        if (!send_ok) {
            net::TcpStream stream;
            if (stream.connect(m_next_ip, m_next_port, kConnectTimeoutMs)) {
                std::lock_guard lock(m_send_mutex);
                m_send  = std::move(stream);
                send_ok = true;
            }
        }
        bool receive_ok = false;
        {
            std::unique_lock lock(m_receive_mutex, std::try_to_lock);
            receive_ok = !lock.owns_lock() || m_receive.valid();
        }
        if (!receive_ok) {
            net::TcpStream stream = m_listen.accept();
            if (stream.valid()) {
                std::lock_guard lock(m_receive_mutex);
                m_receive  = std::move(stream);
                receive_ok = true;
            }
        }
        const bool connected = send_ok && receive_ok;
        if (connected && !m_connected) {
            SM2_INFO("m2comm: buffered link connected to the next cabinet and from the previous one");
            logged = false;
        } else if (!connected && !logged) {
            SM2_INFO("m2comm: buffered link waiting for %s",
                     send_ok ? "the previous cabinet" : "the next cabinet");
            logged = true;
        }
        m_connected = connected;
        std::this_thread::sleep_for(std::chrono::milliseconds(connected ? 50 : 16));
    }
}

bool BufferedLink::send_packet(std::span<const u8> payload)
{
    std::vector<u8> bytes(4 + payload.size());
    const u32       size = static_cast<u32>(payload.size());
    for (int i = 0; i < 4; ++i) {
        bytes[static_cast<usize>(i)] = static_cast<u8>(size >> (8 * i));
    }
    std::copy(payload.begin(), payload.end(), bytes.begin() + 4);

    std::lock_guard lock(m_send_mutex);
    if (!m_send.valid()) {
        return false;
    }
    if (!m_send.send_all(bytes)) {
        if (m_connected) {
            SM2_WARN("m2comm: buffered link lost the next cabinet (%s:%u): %s", m_next_ip.c_str(),
                     m_next_port, m_send.last_error().c_str());
        }
        m_connected = false;
        return false;
    }
    return true;
}

BufferedLink::Received BufferedLink::receive_packet(int timeout_ms)
{
    m_packet.clear();
    std::lock_guard lock(m_receive_mutex);
    if (!m_receive.valid()) {
        return Received::Lost;
    }
    if (!m_receive.wait_readable(timeout_ms)) {
        return Received::Nothing;
    }
    // The rest of a packet started follows at once; a failed read closes the
    // connection, which the background thread then makes again.
    u8 header[4] = {};
    if (!m_receive.read_exact(header, sizeof header, kHandshakeStepMs)) {
        if (m_connected) {
            SM2_WARN("m2comm: buffered link lost the previous cabinet: %s",
                     m_receive.last_error().c_str());
        }
        m_connected = false;
        return Received::Lost;
    }
    const u32 size = header[0] | (u32{header[1]} << 8) | (u32{header[2]} << 16) | (u32{header[3]} << 24);
    if (size > kMaxPacket) {
        SM2_WARN("m2comm: buffered link out of step (a %u-byte packet); connection dropped", size);
        m_receive.close();
        m_connected = false;
        return Received::Lost;
    }
    if (size == 0) {
        return Received::Alert;
    }
    m_packet.resize(size);
    if (!m_receive.read_exact(m_packet.data(), size, kHandshakeStepMs)) {
        m_packet.clear();
        m_connected = false;
        return Received::Lost;
    }
    return Received::Packet;
}

// What has arrived so far, dropped (stale data of an earlier attempt or link).
void BufferedLink::flush()
{
    for (;;) {
        const auto received = receive_packet(0);
        if (received == Received::Nothing || received == Received::Lost) {
            return;
        }
    }
}

// The exchange's data from the previous cabinet: a segment, anything left of a
// handshake dropped.
BufferedLink::Received BufferedLink::receive_segment(int timeout_ms)
{
    const u64 start = now_ms();
    for (;;) {
        const int  left     = std::max(0, timeout_ms - static_cast<int>(now_ms() - start));
        const auto received = receive_packet(left);
        if (received != Received::Packet || m_packet[0] == kSegmentPacket) {
            return received;
        }
    }
}

// A handshake packet of this type and round from the previous cabinet within
// timeout_ms (0: only what has arrived), anything else dropped.
bool BufferedLink::receive_handshake(u8 type, u32 round, int timeout_ms)
{
    const u64 start = now_ms();
    for (;;) {
        const int  left     = std::max(0, timeout_ms - static_cast<int>(now_ms() - start));
        const auto received = receive_packet(left);
        if (received == Received::Lost || received == Received::Nothing) {
            return false;
        }
        if (received != Received::Packet || m_packet[0] != type) {
            continue;
        }
        u32 packet_round = 0;
        const usize at   = type == kGuidPacket ? 9 : 1;
        if (m_packet.size() < at + 4) {
            continue;
        }
        std::memcpy(&packet_round, m_packet.data() + at, 4);
        if (packet_round == round) {
            return true;
        }
    }
}

bool BufferedLink::send_guid(u64 guid, u32 round)
{
    u8 packet[13] = {kGuidPacket};
    std::memcpy(packet + 1, &guid, 8);
    std::memcpy(packet + 9, &round, 4);
    return send_packet(packet);
}

bool BufferedLink::send_index(u32 round, u8 total, u8 playable)
{
    u8 packet[7] = {kIndexPacket};
    std::memcpy(packet + 1, &round, 4);
    packet[5] = total;
    packet[6] = playable;
    return send_packet(packet);
}

void BufferedLink::broken(const char* reason)
{
    SM2_WARN("m2comm: buffered link broken: %s", reason);
    send_packet({});  // the others told
    m_state = State::Broken;
}

void BufferedLink::start()
{
    m_state          = State::Handshake;
    m_handshake_sent = false;
    m_wait_logged    = false;
    m_settle         = 0;
    m_id = m_count = m_total = 0;
    m_data.clear();
}

void BufferedLink::stop()
{
    // The others are not left waiting for this cabinet's data. Nothing waits
    // here: what is left to read is dropped by the next handshake.
    if (m_state == State::Ready) {
        SM2_INFO("m2comm: buffered link: the board stopped by the game; the other cabinets told");
        send_packet({});
    }
    m_state = State::Idle;
}

/*
 * One attempt at the ring's handshake, Supermodel's. The master sends its GUID
 * round the ring twice (every cabinet must run the same link), then the indices
 * (each cabinet adds one: the total, and the playing ones but for a relay) and
 * the counts. Nothing waits for cabinets that are not there yet: the master
 * sends its GUID again every 2 seconds, in a new round; the others take the
 * latest GUID that has come and drop anything else. Once a round has started,
 * each step waits 3 seconds at most. False while the ring is not complete.
 */
bool BufferedLink::handshake(Role role)
{
    if (!m_connected) {
        return false;
    }
    // The game writes its role and its data's size after enabling the board,
    // and expects the link a few seconds later, as MAME's board has it: until
    // then every cabinet looks like the master, and one linked at once would
    // exchange the size by default. Supermodel waits on the master only, half
    // a second; a slave started after it would link before its game is ready.
    if (m_settle < kSettleFrames) {
        ++m_settle;
        return false;
    }
    const u64 now = now_ms();
    u64 guid = 0;
    if (role == Role::Master) {
        if (!m_handshake_sent || now - m_handshake_start > kHandshakeRetryMs) {
            flush();
            if (!send_guid(kGuid, ++m_round)) {
                return false;
            }
            if (!m_wait_logged) {
                SM2_INFO("m2comm: buffered link: waiting for the other cabinets");
                m_wait_logged = true;
            }
            m_handshake_sent  = true;
            m_handshake_start = now;
        }
        if (!receive_handshake(kGuidPacket, m_round, 0)) {
            return false;  // not come round yet
        }
        m_handshake_sent = false;
        std::memcpy(&guid, m_packet.data() + 1, 8);
        if (guid != kGuid) {
            guid = 0;
        }
        if (!send_guid(guid, m_round)
            || !receive_handshake(kGuidPacket, m_round, kHandshakeStepMs)) {
            return false;
        }
        if (guid != kGuid) {
            SM2_ERROR("m2comm: buffered link: another cabinet runs a different link; "
                      "every cabinet needs the same sm2-emu");
            m_state = State::Broken;
            return false;
        }
        // The master is cabinet 0 of the indices; the counts of the others come
        // back and go round.
        if (!send_index(m_round, 0, 0)
            || !receive_handshake(kIndexPacket, m_round, kHandshakeStepMs)) {
            return false;
        }
        const u8 total    = m_packet[5];
        const u8 playable = m_packet[6];
        if (!send_index(m_round, total, playable)
            || !receive_handshake(kIndexPacket, m_round, kHandshakeStepMs)) {
            return false;
        }
        m_id    = 1;
        m_count = static_cast<u8>(playable + 1);
        m_total = static_cast<u8>(total + 1);
        return true;
    }

    // Slave or relay: the latest GUID from the previous cabinet.
    std::vector<u8> guid_packet;
    for (;;) {
        const auto received = receive_packet(0);
        if (received == Received::Lost) {
            return false;
        }
        if (received == Received::Nothing) {
            break;
        }
        if (received == Received::Packet && m_packet[0] == kGuidPacket && m_packet.size() == 13) {
            guid_packet = m_packet;
        }
    }
    if (guid_packet.empty()) {
        return false;
    }
    u32 round = 0;
    std::memcpy(&guid, guid_packet.data() + 1, 8);
    std::memcpy(&round, guid_packet.data() + 9, 4);
    if (guid != kGuid) {
        guid = 0;
    }
    if (!send_guid(guid, round)) {
        return false;
    }
    // Once more, for a later cabinet's mismatch.
    if (!receive_handshake(kGuidPacket, round, kHandshakeStepMs)) {
        return false;
    }
    std::memcpy(&guid, m_packet.data() + 1, 8);
    if (guid != kGuid) {
        guid = 0;
    }
    if (!send_guid(guid, round)) {
        return false;
    }
    if (guid != kGuid) {
        SM2_ERROR("m2comm: buffered link: another cabinet runs a different link; "
                  "every cabinet needs the same sm2-emu");
        m_state = State::Broken;
        return false;
    }
    // The indices of the previous cabinet, one more, on to the next one.
    if (!receive_handshake(kIndexPacket, round, kHandshakeStepMs)) {
        return false;
    }
    const u8 total    = static_cast<u8>(m_packet[5] + 1);
    const u8 playable = static_cast<u8>(m_packet[6] + (role == Role::Slave ? 1 : 0));
    if (!send_index(round, total, playable)) {
        return false;
    }
    // The counts, forwarded.
    if (!receive_handshake(kIndexPacket, round, kHandshakeStepMs)) {
        return false;
    }
    const u8 total_count    = m_packet[5];
    const u8 playable_count = m_packet[6];
    if (!send_index(round, total_count, playable_count)) {
        return false;
    }
    m_id    = role == Role::Slave ? static_cast<u8>(playable + 1) : u8{0};
    m_count = static_cast<u8>(playable_count + 1);
    m_total = static_cast<u8>(total_count + 1);
    return true;
}

// Every cabinet's data of this frame: this cabinet's first, then, as many times
// as there are cabinets, what the previous one sent, sent on. The last that
// comes is this cabinet's own, back round the ring.
BufferedLink::Step BufferedLink::exchange(std::span<const u8> own)
{
    m_data.resize(usize{m_count} + 1);
    if (m_id != 0) {
        m_data[m_id].assign(own.begin(), own.end());
    }
    std::vector<u8> segment(2 + own.size());
    segment[0] = kSegmentPacket;
    segment[1] = m_id;
    std::copy(own.begin(), own.end(), segment.begin() + 2);
    for (u8 step = 0; step < m_total; ++step) {
        if (!send_packet(segment)) {
            broken("the next cabinet is gone");
            return Step::Broken;
        }
        const auto received = receive_segment(kLinkTimeoutMs);
        if (received != Received::Packet) {
            broken(received == Received::Nothing ? "no data from the previous cabinet for 10 s"
                   : received == Received::Lost  ? "the previous cabinet is gone"
                                                 : "another cabinet left the link");
            return Step::Broken;
        }
        const u8 sender = m_packet.size() >= 2 ? m_packet[1] : 0;
        if (sender >= 1 && sender <= m_count && sender != m_id) {
            m_data[sender].assign(m_packet.begin() + 2, m_packet.end());
        }
        segment = m_packet;
    }
    return Step::Exchanged;
}

BufferedLink::Step BufferedLink::run_frame(Role role, std::span<const u8> own)
{
    switch (m_state) {
    case State::Idle:
    case State::Broken:
        return m_state == State::Broken ? Step::Broken : Step::Waiting;
    case State::Handshake:
        if (!handshake(role)) {
            return m_state == State::Broken ? Step::Broken : Step::Waiting;
        }
        SM2_INFO("m2comm: buffered link: cabinet %u of %u (%s), %zu bytes a cabinet", m_id, m_count,
                 role == Role::Master ? "master" : role == Role::Slave ? "slave" : "relay",
                 own.size());
        m_state = State::Ready;
        return Step::Linked;
    case State::Ready:
        return exchange(own);
    }
    return Step::Waiting;
}

std::span<const u8> BufferedLink::data(u8 id) const
{
    if (id >= m_data.size()) {
        return {};
    }
    return m_data[id];
}

}  // namespace sm2::hw
