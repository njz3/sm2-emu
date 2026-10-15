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
// Daytona USA's telemetry, from its work RAM (AUTOPILOT.md and TELEMETRY.md
// tell how it was found):
//
//   00505100  40 entries of 0x300 bytes: the player's first, then the AI cars
//             +98 the speedometer, km/h (u16)
//             +9C x, +A0 y, +A4 z (floats; a unit is about 1.11 m by the
//                 speedometer)
//             +A8 pitch, +AC roll (s16, a turn is 65536; + nose up, + right)
//             +C8 heading (i32, a turn is 65536, negated)
//             +EC the place in the race, from 0
//             +F0 laps done
//             +1AC progress along the course (16.16, a segment every 8.3
//                  units; 196 a lap of the beginner course)
//             +1CC gear, 0..3; +1D4 rpm (u32)
//   005010A0  game state: 2-3 attract, 17-19 coin and menus, 22 the race, 26
//             time up
//   005010CC  race time, 005010D0 time left (frames; the clock's second is 64)
//   005010E0  laps done, 005010EC the race's laps
//   00502A00  the player's lap times (u32 each, frames)
//
// The AI cars' speeds come from successive positions (kinematics).
#pragma once

#include "telemetry/telemetry.h"

namespace sm2::telemetry {

class DaytonaTelemetry final : public GameTelemetry {
public:
    [[nodiscard]] const char* name() const override { return "Daytona USA"; }
    bool decode(const Memory& ram, u64 frame, Frame& out) override;
};

}  // namespace sm2::telemetry
