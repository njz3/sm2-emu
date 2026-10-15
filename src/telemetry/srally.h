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
// Sega Rally Championship's telemetry, from its work and scratch RAMs
// (AUTOPILOT.md and TELEMETRY.md tell how it was found; the position is not
// yet):
//
//   00500000  16 entries of 0x100 bytes: the player's, then seven slots for
//             the AI cars near the player
//             +14 x, +18 y, +1C z (floats, about metres)
//             player: +38 pitch, +3C heading (atan2(-dx, dz)), +40 roll
//                         (floats, radians; + nose up, + right)
//                     +78 gear (1..4), +7C rpm (float), +80 speed (float,
//                         x 64.875 km/h)
//                     +84 progress (16.16, a segment every 4.8 units)
//             AI:     +A4 heading (float, radians, atan2(dz, dx))
//                     +88 progress (16.16, a segment every 10.3 units; 300 a
//                     lap of the Desert)
//   00202098  game mode: 2 attract, 3 from the coin to the game over
//   002020B8  total time, 0020B0B0 time left (u16, 1/60 s)
//
// The AI cars' speeds come from successive positions (kinematics).
#pragma once

#include "telemetry/telemetry.h"

namespace sm2::telemetry {

class SrallyTelemetry final : public GameTelemetry {
public:
    [[nodiscard]] const char* name() const override { return "Sega Rally Championship"; }
    bool decode(const Memory& ram, u64 frame, Frame& out) override;
};

}  // namespace sm2::telemetry
