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
// Sega Touring Car Championship's telemetry. The game keeps its race in the
// Model 2C's scratch RAM (from 0x00200000), not in the work RAM
// (AUTOPILOT.md tells how it was found):
//
//   0020A900  8 cars of 0x58 bytes, the player first:
//             +00 x, +04 y, +08 z (floats)
//             +0C, +10, +14 velocity (floats, units a frame)
//             +1C heading (float, radians, -atan2(dx, dz))
//             +50 the track segment the car is on (u16, 0 at the line)
//             +52 segments driven since the line (i16, negative on the grid)
//   002022EC  time left (u32, frames; the screen shows it divided by 64)
//
// Country Circuit has 300 segments a lap; on the other courses the count is
// the highest segment seen, plus one.
#pragma once

#include "telemetry/telemetry.h"

namespace sm2::telemetry {

class StccTelemetry final : public GameTelemetry {
public:
    [[nodiscard]] const char* name() const override { return "Sega Touring Car Championship"; }
    bool decode(const Memory& ram, u64 frame, Frame& out) override;

private:
    int m_segments = 300;  ///< Segments a lap: Country Circuit's, or the highest seen plus one.
};

}  // namespace sm2::telemetry
