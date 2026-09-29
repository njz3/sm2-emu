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
// Cabinet outputs over the network: the lamps and the drive-board commands,
// published the way MAME's "network" output provider publishes them, so that
// tools written for MAME, Supermodel or Flycast (BackForceFeeder, MameHooker,
// DOFLinx, ...) can drive real lamps and a real drive board.
//
// The protocol is MAME's: a TCP server, port 8000 by default. A client first
// reads "mame_start = <romset>", then the current value of every output, then one
// "<name> = <value>" line per change, in decimal, each line ended by '\r'.
// "mame_stop = 1" says the game has gone. Whatever a client sends is read and
// ignored.
#pragma once

#include "core/net.h"
#include "core/types.h"
#include "rom/game.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sm2::osd {

class NetOutputs {
public:
    NetOutputs() = default;
    ~NetOutputs();

    NetOutputs(const NetOutputs&)            = delete;
    NetOutputs& operator=(const NetOutputs&) = delete;

    /// Serve `game` (its romset name) on bind_ip:port, or nothing when `game` is
    /// empty. A change to any of the three stops the old server first, telling
    /// its clients the game has gone. Cheap enough to call every frame; a listen
    /// that failed is not retried until one of the three changes.
    void sync(const std::string& game, const std::string& bind_ip, u16 port);

    /// Tell every client the game has gone, and close everything.
    void stop();

    /// Listening for clients, and talking to any that connected.
    [[nodiscard]] bool active() const { return m_listener.valid(); }

    [[nodiscard]] usize client_count() const { return m_clients.size(); }

    /// Why the last listen failed; empty when it did not.
    [[nodiscard]] const std::string& error() const { return m_error; }

    /// Set an output. Queued for every client only when the value changes.
    void set(std::string_view name, s32 value);

    /// Accept new clients, drop the ones that went away, and send what set()
    /// queued. Once per frame; never blocks.
    void service();

private:
    struct Client {
        net::TcpConnection connection;
        std::string        unsent;  ///< What the socket has not taken yet.
    };

    static void queue(Client& client, std::string_view name, std::string_view value);

    /// Send what the client has queued. False when the client is gone or has
    /// stopped reading, either of which drops it.
    [[nodiscard]] static bool flush(Client& client);

    net::TcpListener    m_listener;
    std::vector<Client> m_clients;

    /// Every output set so far, in the order first set, so a client that
    /// connects late is sent the whole current state.
    std::vector<std::pair<std::string, s32>> m_values;

    std::string m_game;
    std::string m_bind_ip;
    u16         m_port = 0;
    std::string m_error;
};

/// Publish one frame of a cabinet's outputs: the lamp port under the names this
/// game's cabinet gives its bits, and each drive-board byte in the order written.
void publish_cabinet_outputs(NetOutputs& outputs, const rom::GameSpec& game,
                             std::optional<u8> lamps, std::span<const u8> drive_writes);

}  // namespace sm2::osd
