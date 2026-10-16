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
// Manx TT Superbike's telemetry, from its work RAM (TELEMETRY.md tells how it
// was found): the player's bike.
//
//   00578A20  +00 x, +04 y, +08 z (floats, decimetres, y up)
//             +0C pitch, +10 heading (negated), +14 lean (floats, radians; +
//                 nose up, + leaning right: into a right turn)
//   00578B64  gear (u8, 1 first)
//   005009D0  the speedometer (u16, km/h)
//   005333E0  the distance along the course (float)
//   005334F0  time left (u16; about 56 to a second shown; still on the
//             menus' screens, counting during a race)
#pragma once

#include "telemetry/telemetry.h"

namespace sm2::telemetry {

class ManxTtTelemetry final : public GameTelemetry {
public:
    [[nodiscard]] const char* name() const override { return "Manx TT Superbike"; }
    bool decode(const Memory& ram, u64 frame, Frame& out) override;

private:
    u16 m_time_left  = 0;
    u64 m_time_frame = 0;
};

}  // namespace sm2::telemetry
