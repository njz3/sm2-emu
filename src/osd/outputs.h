// ██╗  ██╗███████╗██╗███████╗██╗  ██╗ ██████╗
// ██║ ██╔╝██╔════╝██║██╔════╝██║  ██║██╔═══██╗
// █████╔╝ █████╗  ██║███████╗███████║██║   ██║
// ██╔═██╗ ██╔══╝  ██║╚════██║██╔══██║██║   ██║
// ██║  ██╗███████╗██║███████║██║  ██║╚██████╔╝
// ╚═╝  ╚═╝╚══════╝╚═╝╚══════╝╚═╝  ╚═╝ ╚═════╝
//            継 承  ( k e i s h ō )
//
// A Sega Model 1, 2 & 3 arcade emulator.
// Copyright (c) 2025+ Daniel Martin (dmanlfc)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This header must not be removed. Keisho is licensed under the GNU General
// Public License, version 3 or later, with additional terms under section 7 of
// that licence. See LICENSE and NOTICE.
//
// Cabinet outputs: lamps and drive-board bytes, published for MAMEHooker,
// DOFLinx and similar tools in MAME's two formats.
//
// Network: a TCP server (port 8000 by default) sending "name = value\r" lines,
// with "mame_start = <set>", "mame_stop = 1" and "pause = 0/1". When a game
// starts, it is also announced once the way Supermodel does,
// "mame_start = <set>\rtcp = <port>\r" over UDP to 127.0.0.1:8001:
// BackForceFeeder waits for that before it connects.
//
// Windows: a hidden "MAMEOutput" window answering MAME's registered messages.
//
// Every game publishes lamp0..lamp5 (MAME's names for the lamp latch's bits
// 2-7), RawLamps and RawDrive, plus LampStart/LampView1-4/LampLeader where the
// bit meanings are known. Values are sent only when they change.
//
// A game with a drive board also sends its bytes as "m2ffb = XX" (hex), the
// name BackForceFeeder answers to in M2PAC with the real board's reply,
// "m2ffb_rx = XX": that client's last reply is then what the game reads from
// its drive board (drive_reply()), until it disconnects.
//
// A title can add outputs read from work RAM in games.xml (rom::OutputSpec). The
// gun games use DemulShooter's names (P1_CtmRecoil, P1_Ammo, ...) so those
// tools' stock game files work.

#pragma once

#include "core/types.h"
#include "rom/game.h"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sm2::osd {

class Outputs {
public:
    Outputs();
    ~Outputs();

    Outputs(const Outputs&)            = delete;
    Outputs& operator=(const Outputs&) = delete;

    /// Enable or disable each backend. Cheap to call every frame. announce_port
    /// is the UDP port for the start announcement, 0 for none.
    void configure(bool network, u16 port, u16 announce_port, bool windows);

    /// The set being run, or an empty string for none. A change sends
    /// mame_stop for the old set and mame_start for the new one. `game_outputs`
    /// is the set's own list from games.xml.
    void set_game(const std::string& name, const std::string& parent,
                  std::span<const rom::OutputSpec> game_outputs = {}, bool drive_board = false,
                  bool billboard = false);

    void set_paused(bool paused);

    /// The RAM a set's own outputs read: Model 2's i960 work RAM at
    /// 0x00500000 (little-endian), Model 3's PowerPC RAM at 0 (big-endian).
    struct RamView {
        std::span<const u8> bytes;
        u32                 base       = 0x00500000;
        bool                big_endian = false;
    };

    /// This frame's lamp latch and drive-board bytes, the RAM the set's own
    /// outputs are read from, and the input ports IN0-IN2 (active low).
    void update(u8 lamp_latch, std::span<const u8> drive_writes, RamView ram,
                std::array<u8, 3> ports = {0xff, 0xff, 0xff});

    /// A Model 3 billboard's digits and lamps, published as BillDigit1-5.
    /// Call after update().
    void update_billboard(std::span<const u8> bill);

    /// Accept clients and flush queued messages. Call once per frame.
    void poll();

    /// Where the network outputs stand, for the settings.
    struct NetworkStatus {
        bool        enabled   = false;
        bool        listening = false;
        usize       clients   = 0;
        std::string error;  ///< Why it is not listening; empty when it is.
    };
    [[nodiscard]] NetworkStatus network_status() const;

    /// A real drive board's answer, the last "m2ffb_rx" a network client sent,
    /// while that client stays connected and the game the same; none otherwise.
    [[nodiscard]] std::optional<u8> drive_reply() const;

    /// One row of what the running game publishes, for the settings to list,
    /// in the overlay's language.
    struct Listed {
        std::string name;
        std::string type;
        std::string meaning;
        std::string value;  ///< The last one sent; "-" before any, or with the outputs off.
    };

    /// What the running game publishes: its latch's lamps with what each is
    /// for in this game, the drive board's bytes when it has one (and
    /// m2ffb_rx, the reply a tool sends back), the set's own outputs, each with
    /// its last value. Empty with no game.
    [[nodiscard]] std::vector<Listed> listing() const;

    /// One published output, exposed for the backends.
    struct Item {
        std::string name;
        s32         value = 0;
        bool        known = false;  ///< False until the first value is set.
        bool        hex   = false;  ///< Sent as two hex digits (m2ffb).
    };

    class Backend;

private:
    void set(usize index, s32 value);
    void build_items(const std::string& parent);
    void update_game_outputs(u8 lamp_latch, const RamView& ram, const std::array<u8, 3>& ports);

    std::string m_game;
    std::string m_family;               ///< The set's family, for its lamps' names.
    bool        m_drive_board = false;  ///< It latches drive-board commands.
    bool        m_paused      = false;

    std::vector<Item> m_items;

    /// Which lamp-latch bit feeds each item, or -1 for RawDrive.
    std::vector<int> m_bits;

    /// The first BillDigit item, added by set_game(); zero when there is none.
    usize m_first_bill_item = 0;

    /// One of the set's own outputs as it stood this frame and the one before.
    struct GameOutput {
        rom::OutputSpec spec;
        s32             value      = 0;
        s32             previous   = 0;
        bool            live       = false;  ///< Its condition held this frame.
        bool            was_live   = false;
        bool            pending    = false;  ///< Pulse: an edge waiting its turn.
        u32             on_frames  = 0;      ///< Pulse: frames left at 1.
        u32             off_frames = 0;      ///< Pulse: frames left at 0 after it.
    };

    /// The set's own outputs. Their items follow the lamp ones in m_items,
    /// starting at m_first_game_item.
    std::vector<GameOutput> m_game_outputs;
    usize                   m_first_game_item = 0;

    std::unique_ptr<Backend> m_network;
    std::unique_ptr<Backend> m_windows;
    u16                      m_port          = 0;
    u16                      m_announce_port = 0;
};

}  // namespace sm2::osd
