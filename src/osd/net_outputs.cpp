//  ____  __  __  ____         _____ __  __ _   _
// / ___||  \/  ||___ \       | ____|  \/  | | | |
// \___ \| |\/| |  __) |_____ |  _| | |\/| | | | |
//  ___) | |  | | / __/|_____|| |___| |  | | |_| |
// |____/|_|  |_||_____|      |_____|_|  |_|\___/
//
// A Sega Model 2 arcade emulator.
// Copyright (c) 2025+ Daniel Martin (dmanlfc)
// SPDX-License-Identifier: BSD-3-Clause
//
// This header must not be removed. The source files in this project may not be
// used to contribute to commercial projects or for monetary gain without the
// express written permission of the author.
//
// See net_outputs.h.

#include "osd/net_outputs.h"

#include "core/log.h"

#include <algorithm>
#include <array>

namespace sm2::osd {

namespace {

/// A client that has stopped reading is dropped once this much is waiting for
/// it. A second of outputs is a few kilobytes at most, so it is not just slow.
constexpr usize kMaxUnsent = 64 * 1024;

/// How often the UDP announcement goes out while nobody is connected.
/// BackForceFeeder listens for it a second at a time between its TCP attempts,
/// so a single datagram can fall in the gap.
constexpr std::chrono::seconds kAnnounceInterval{2};

[[nodiscard]] bool is_loopback(const std::string& ip)
{
    return ip.starts_with("127.");
}

/// A lamp-port bit that a cabinet gives a name, on top of the lampN every bit
/// is published as.
struct NamedBit {
    u8          bit;
    const char* name;
};

// Daytona USA, from MAME's daytona_output_w. These are also the names and the bit
// positions Supermodel uses for its racing cabinets, which is what tools key on.
constexpr NamedBit kDaytona[] = {
    {2, "LampStart"}, {3, "LampView1"}, {4, "LampView2"},
    {5, "LampView3"}, {6, "LampView4"}, {7, "LampLeader"},
};

// Desert Tank, from MAME's desert_output_w. Its view lamps run the other way
// from Daytona's, so they are not called LampViewN: a tool that files LampViewN
// under Daytona's bit positions would then disagree with lampN.
constexpr NamedBit kDesertTank[] = {
    {2, "LampStart"}, {3, "LampVR3"},         {4, "LampVR2"},
    {5, "LampVR1"},   {6, "MachineGunMotor"}, {7, "CannonMotor"},
};

// Virtua Cop, from MAME's vcop_output_w: the two start lamps, lit together.
constexpr NamedBit kVirtuaCop[] = {
    {2, "LampStart"}, {3, "LampStart2"},
};

constexpr std::array<const char*, 8> kLampBits = {
    "lamp0", "lamp1", "lamp2", "lamp3", "lamp4", "lamp5", "lamp6", "lamp7",
};

[[nodiscard]] std::span<const NamedBit> named_bits(const rom::GameSpec& game)
{
    const std::string& family = game.parent.empty() ? game.name : game.parent;
    if (family == "daytona") return kDaytona;
    if (family == "desert") return kDesertTank;
    if (family == "vcop") return kVirtuaCop;
    return {};
}

}  // namespace

NetOutputs::~NetOutputs()
{
    stop();
}

void NetOutputs::sync(const std::string& game, const std::string& bind_ip, u16 port,
                      u16 announce_port)
{
    if (game == m_game && bind_ip == m_bind_ip && port == m_port) {
        if (announce_port != m_announce_port) {
            // service() announces straight away on the new port.
            m_announce_port   = announce_port;
            m_announce_failed = false;
            m_next_announce   = {};
        }
        return;
    }

    stop();
    m_game          = game;
    m_bind_ip       = bind_ip;
    m_port          = port;
    m_announce_port = announce_port;
    if (game.empty()) {
        return;
    }

    if (!m_listener.open(bind_ip, port)) {
        if (m_listener.address_in_use()) {
            m_error = "port " + std::to_string(port) + " is already in use";
            SM2_WARN("cabinet outputs disabled: port %u is already in use on %s. Something "
                     "else listens there (another emulator's outputs, or a second "
                     "sm2-emu); close it, or set net_outputs_port to a free port.",
                     static_cast<unsigned>(port), bind_ip.empty() ? "*" : bind_ip.c_str());
        } else {
            m_error = m_listener.last_error();
            SM2_WARN("cabinet outputs disabled: %s", m_error.c_str());
        }
        return;
    }
    SM2_INFO("cabinet outputs: serving '%s' on %s:%u", game.c_str(),
             bind_ip.empty() ? "*" : bind_ip.c_str(), static_cast<unsigned>(port));
}

void NetOutputs::stop()
{
    if (m_listener.valid()) {
        for (Client& client : m_clients) {
            queue(client, "mame_stop", "1");
            (void)flush(client);
            client.connection.close();
        }
        SM2_INFO("cabinet outputs: stopped serving '%s'", m_game.c_str());
    }
    m_clients.clear();
    m_listener.close();
    m_announcer.close();
    m_values.clear();
    m_game.clear();
    m_bind_ip.clear();
    m_port            = 0;
    m_announce_port   = 0;
    m_announce_failed = false;
    m_next_announce   = {};
    m_error.clear();
}

void NetOutputs::set(std::string_view name, s32 value)
{
    if (!m_listener.valid()) {
        return;
    }

    const auto known = std::find_if(m_values.begin(), m_values.end(),
                                    [name](const auto& entry) { return entry.first == name; });
    if (known == m_values.end()) {
        m_values.emplace_back(std::string(name), value);
    } else if (known->second == value) {
        return;
    } else {
        known->second = value;
    }

    const std::string text = std::to_string(value);
    for (Client& client : m_clients) {
        queue(client, name, text);
    }
}

void NetOutputs::service()
{
    if (!m_listener.valid()) {
        return;
    }

    for (;;) {
        net::TcpConnection connection = m_listener.accept();
        if (!connection.valid()) {
            break;
        }
        Client& client = m_clients.emplace_back(Client{std::move(connection), {}});
        queue(client, "mame_start", m_game);
        for (const auto& [name, value] : m_values) {
            queue(client, name, std::to_string(value));
        }
        SM2_INFO("cabinet outputs: client connected (%zu connected)", m_clients.size());
    }

    const usize before = m_clients.size();
    std::erase_if(m_clients, [](Client& client) {
        return !client.connection.drain() || !flush(client);
    });
    if (m_clients.size() != before) {
        SM2_INFO("cabinet outputs: client disconnected (%zu connected)", m_clients.size());
    }

    if (m_clients.empty() && m_announce_port != 0 && !m_announce_failed
        && std::chrono::steady_clock::now() >= m_next_announce) {
        announce();
    }
}

void NetOutputs::announce()
{
    // A server on the loopback cannot be reached from another machine, so it
    // is not advertised to the LAN either.
    const bool        loopback    = is_loopback(m_bind_ip);
    const std::string destination = loopback ? "127.0.0.1" : "255.255.255.255";

    if (!m_announcer.valid()) {
        const bool opened = m_announcer.open(loopback ? "127.0.0.1" : m_bind_ip, 0)
                         && (loopback || m_announcer.set_broadcast(true));
        if (!opened) {
            m_announce_failed = true;
            SM2_WARN("cabinet outputs: no UDP announcement: %s",
                     m_announcer.last_error().c_str());
            m_announcer.close();
            return;
        }
        SM2_INFO("cabinet outputs: announcing '%s' to %s:%u (UDP) until a tool connects",
                 m_game.c_str(), destination.c_str(), static_cast<unsigned>(m_announce_port));
    }

    // Supermodel's datagram, two lines: the game, then where its server listens.
    const std::string text = "mame_start = " + m_game + "\rtcp = " + std::to_string(m_port) + "\r";
    const auto* bytes      = reinterpret_cast<const u8*>(text.data());
    if (!m_announcer.send_to({bytes, text.size()}, destination, m_announce_port)) {
        SM2_TRACE("cabinet outputs: UDP announcement not sent: %s",
                  m_announcer.last_error().c_str());
    }
    m_next_announce = std::chrono::steady_clock::now() + kAnnounceInterval;
}

void NetOutputs::queue(Client& client, std::string_view name, std::string_view value)
{
    client.unsent.append(name);
    client.unsent.append(" = ");
    client.unsent.append(value);
    client.unsent.push_back('\r');  // MAME's line ending, which its clients expect
}

bool NetOutputs::flush(Client& client)
{
    if (client.unsent.empty()) {
        return true;
    }
    const std::optional<usize> sent = client.connection.send(client.unsent);
    if (!sent.has_value()) {
        return false;
    }
    client.unsent.erase(0, *sent);
    return client.unsent.size() <= kMaxUnsent;
}

void publish_cabinet_outputs(NetOutputs& outputs, const rom::GameSpec& game,
                             std::optional<u8> lamps, std::span<const u8> drive_writes)
{
    if (!outputs.active()) {
        return;
    }

    if (lamps.has_value()) {
        // Every bit as lampN and the whole byte as RawLamps, whatever the cabinet:
        // which bit lights what is only known for a few of them, and a tool can
        // map the rest itself. The known ones get their names on top.
        const u8 value = *lamps;
        for (usize bit = 0; bit < kLampBits.size(); ++bit) {
            outputs.set(kLampBits[bit], (value >> bit) & 1);
        }
        for (const NamedBit& named : named_bits(game)) {
            outputs.set(named.name, (value >> named.bit) & 1);
        }
        outputs.set("RawLamps", value);
    }

    // One line per byte, in the order the game wrote them, since a frame can hold
    // an effect and its parameters. Like Supermodel's RawDrive, a byte equal to
    // the one before is not sent again.
    for (const u8 value : drive_writes) {
        outputs.set("RawDrive", value);
    }
}

}  // namespace sm2::osd
