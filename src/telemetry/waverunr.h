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
// Wave Runner's telemetry, from its work RAM (TELEMETRY.md tells how it was
// found): the player's jet ski only, its attitude on the waves not found.
//
//   0051DF1C  the player: +00 x, +04 y, +08 z (floats, y up: the water near 0)
//             +30 heading (s32, 65536 a turn, -atan2(dx, dz))
//             +48 speed (float, units a frame; x 155.7 the speedometer's km/h)
//   005AD874  race time (u16, frames at 60 a second; 0 before the race, still
//             once it is over)
#pragma once

#include "telemetry/telemetry.h"

namespace sm2::telemetry {

class WaveRunnerTelemetry final : public GameTelemetry {
public:
    [[nodiscard]] const char* name() const override { return "Wave Runner"; }
    bool decode(const Memory& ram, u64 frame, Frame& out) override;

private:
    u16 m_race_time = 0;
    u64 m_race_frame = 0;
};

}  // namespace sm2::telemetry
