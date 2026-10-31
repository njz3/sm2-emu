// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cabinet link as Supermodel's simulated network board has it
// (Src/Network/SimNetBoard.cpp), for the Model 2 communication board: a
// "buffered" link, chosen by link_mode, beside MAME's frame simulation in
// M2Comm.
//
// A ring of TCP connections, each cabinet listening for the previous one and
// connected to the next. A handshake numbers the cabinets in the ring's order
// (the master 1, the next one 2...), then once a frame, after the frame has
// run, each cabinet's data goes round the whole ring at once: as many steps
// as there are cabinets, each sending on what the step before received, each
// waiting for the previous cabinet's. Every cabinet then has the whole ring's
// data of that frame, which the game reads the next, as the real ring is fast
// enough to have it. The wait falls at the end of the frame, in the time the
// frame pacer would sleep anyway, and the cabinets settle into step.
#pragma once

#include "core/net.h"
#include "core/types.h"

#include <atomic>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace sm2::hw {

class BufferedLink {
public:
    /// The game's choice, read from the board each frame.
    enum class Role { Master, Slave, Relay };

    /// What a frame's run_frame() did.
    enum class Step {
        Waiting,    ///< the ring not complete yet (or the board idle)
        Linked,     ///< the handshake just completed: id() and count() are set
        Exchanged,  ///< every cabinet's data of this frame is in data()
        Broken,     ///< the link has failed; the game resets the board
    };

    /// Listen on local_ip:port (empty: every interface) for the previous
    /// cabinet; connect to next_ip:next_port. Both made in the background.
    BufferedLink(const std::string& local_ip, u16 port, std::string next_ip, u16 next_port);
    ~BufferedLink();

    BufferedLink(const BufferedLink&)            = delete;
    BufferedLink& operator=(const BufferedLink&) = delete;

    /// False when the listening socket could not be made (error() says why).
    [[nodiscard]] bool ok() const;
    [[nodiscard]] const std::string& error() const { return m_error; }

    /// The game enabled the board: a new handshake.
    void start();
    /// The game disabled the board: the others told if linked.
    void stop();

    /// Once a frame, after it has run. While the ring is not complete, one
    /// handshake attempt (not waiting for cabinets that are not there); once
    /// linked, the exchange of `own`, this cabinet's data, with every other
    /// cabinet's, waiting for them.
    Step run_frame(Role role, std::span<const u8> own);

    /// 1 the master, then 2... in the ring's order; 0 a relay.
    [[nodiscard]] u8 id() const { return m_id; }
    /// The cabinets that play (relays left out).
    [[nodiscard]] u8 count() const { return m_count; }
    /// Cabinet `id`'s data from the last exchange (1..count), empty before.
    [[nodiscard]] std::span<const u8> data(u8 id) const;

    /// Both connections, to the next cabinet and from the previous one, made.
    [[nodiscard]] bool connected() const { return m_connected; }
    /// Each of them, to tell which one is missing.
    [[nodiscard]] bool connected_to_next() const { return m_to_next; }
    [[nodiscard]] bool connected_from_previous() const { return m_from_previous; }

    /// Where the link listens for the previous cabinet and connects to the next.
    [[nodiscard]] u16 port() const { return m_port; }
    [[nodiscard]] const std::string& next_ip() const { return m_next_ip; }
    [[nodiscard]] u16 next_port() const { return m_next_port; }

private:
    enum class Received { Packet, Nothing, Lost, Alert };
    enum class State { Idle, Handshake, Ready, Broken };

    void connect_loop();
    bool send_packet(std::span<const u8> payload);
    Received receive_packet(int timeout_ms);
    Received receive_segment(int timeout_ms);
    bool receive_handshake(u8 type, u32 round, int timeout_ms);
    void flush();
    void broken(const char* reason);

    bool handshake(Role role);
    bool send_guid(u64 guid, u32 round);
    bool send_index(u32 round, u8 total, u8 playable);
    Step exchange(std::span<const u8> own);

    u16         m_port = 0;
    std::string m_next_ip;
    u16         m_next_port = 0;
    std::string m_error;

    // The connections: the background thread makes a missing one; the frame's
    // thread uses them, each under its mutex.
    net::TcpListener m_listen;
    std::mutex       m_send_mutex;
    net::TcpStream   m_send;
    std::mutex       m_receive_mutex;
    net::TcpStream   m_receive;
    std::thread      m_thread;
    std::atomic_bool   m_quit{false};
    std::atomic_bool   m_connected{false};
    std::atomic_bool   m_to_next{false};
    std::atomic_bool   m_from_previous{false};

    std::vector<u8> m_packet;  ///< The last packet received, its type first.

    State m_state = State::Idle;
    // The handshake (Supermodel's): the master's attempts are numbered (its
    // round), so that what is left of an earlier one is told apart.
    bool m_handshake_sent  = false;
    u64  m_handshake_start = 0;
    u32  m_settle          = 0;  ///< frames since the board was enabled, up to kSettleFrames
    u32  m_round           = 0;
    bool m_wait_logged     = false;
    /// Broken, the board still enabled: the ring is joined again when it comes
    /// back (another link's mismatch excepted).
    bool m_rejoin          = false;

    u8 m_id    = 0;
    u8 m_count = 0;  ///< playing cabinets
    u8 m_total = 0;  ///< every cabinet in the ring, relays too
    std::vector<std::vector<u8>> m_data;  ///< by id, 1..m_count
};

}  // namespace sm2::hw
