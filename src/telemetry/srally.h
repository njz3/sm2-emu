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
// Sega Rally Championship's telemetry, from its work RAM (AUTOPILOT.md tells
// how it was found; the position and the laps are not yet):
//
//   00500000  16 entries of 0x100 bytes: the player's, then seven slots for
//             the AI cars near the player
//             +14 x, +18 y, +1C z (floats)
//             player: +3C heading (float, radians, atan2(-dx, dz) of the travel)
//                     +84 progress (16.16, a segment every 4.8 units)
//             AI:     +A4 heading (float, radians, atan2(dz, dx))
//                     +88 progress (16.16, a segment every 10.3 units; 300 a
//                     lap of the Desert)
//   005229E6  seconds left (BCD byte)
//
// Speeds come from successive positions (GameTelemetry::kinematics).
#pragma once

#include "telemetry/telemetry.h"

namespace sm2::telemetry {

class SrallyTelemetry final : public GameTelemetry {
public:
    [[nodiscard]] const char* name() const override { return "Sega Rally Championship"; }
    bool decode(const Memory& ram, u64 frame, Frame& out) override;
};

}  // namespace sm2::telemetry
