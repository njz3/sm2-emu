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
// Over Rev's telemetry, from its work RAM (AUTOPILOT.md tells how it was
// found). Distances in half-metres; the game counts 60 frames a second.
//
//   0059E6A8  7 AI cars of 0x2A8 bytes:
//             +050 laps done (u8)
//             +0A4 heading (s32, 65536 a turn, pi/2 - atan2(dx, dz))
//             +0E0 speed (float, half-metres a frame)
//             +100 x, +104 y, +108 z (floats, y up)
//             +1F0 distance from the start line (float, negative on the grid)
//   00590DE4  the player's roll, negated (+ leaning right: out of a turn);
//             its pitch at +10 (+ nose up), its heading at +14, negated
//             (s32, 65536 a turn)
//   00595A5C  the player's gear (u8, 1 first)
//   00595A7C  its revs (float)
//   00595AA4  its distance into the lap (float)
//   00595AB8  its speed (float, m/s)
//   00595C04  its x, y, z (floats)
//   00595C9C  its laps from the start line (s32, -1 on the grid, which is
//             more than half a lap behind it)
//   00595CAC  race time, 00595CB0 lap time (u16, frames)
//   00595CE0  seconds left (u8)
//   00595D18  the race's laps (u8)
//   0051D33C  the lap's length (float)
//
// The game keeps no race position but the one its display slides to; it is
// counted here from the cars' progress, which gives the same.
#pragma once

#include "telemetry/telemetry.h"

namespace sm2::telemetry {

class OverRevTelemetry final : public GameTelemetry {
public:
    [[nodiscard]] const char* name() const override { return "Over Rev"; }
    bool decode(const Memory& ram, u64 frame, Frame& out) override;
};

}  // namespace sm2::telemetry
