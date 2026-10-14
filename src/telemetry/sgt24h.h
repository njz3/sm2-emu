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
// Super GT 24h's telemetry, from its work RAM (AUTOPILOT.md tells how it was
// found). Metres; the game counts 60 frames a second.
//
//   00518424  14 cars of 0x1728 bytes, the player last; from a car's base:
//             -0C race position (u8, 0 leading)
//             -04 laps done (u8)
//             +00 distance along the course (float, 3000 a lap; the grid is
//                 just past the line)
//             +0C x, +10 y, +14 z (floats, y up)
//             +34 heading (s32, 65536 a turn, -atan2(dx, dz))
//             +84C speed (float, metres a frame)
//             the player only: +C8 gear (u8, 0 first), +220 revs (u16),
//             +764 its orientation (3x3 floats, world from car, row-major)
//   00510948  game state (u32): 1 attract mode, 7 coin, 2 car select, 3 race
//             (its countdown too), 4 game over
//   00510F2C  lap time (u16, frames)
//   00510F6C  the race's laps (u8)
//   00510F74  time left (s16, frames)
#pragma once

#include "telemetry/telemetry.h"

namespace sm2::telemetry {

class Sgt24hTelemetry final : public GameTelemetry {
public:
    [[nodiscard]] const char* name() const override { return "Super GT 24h"; }
    bool decode(const Memory& ram, u64 frame, Frame& out) override;
};

}  // namespace sm2::telemetry
