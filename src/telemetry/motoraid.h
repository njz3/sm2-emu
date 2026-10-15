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
// Motor Raid's telemetry, from its work RAM (TELEMETRY.md tells how it was
// found). The game counts 60 frames a second.
//
//   0057B8E4  10 riders of 0x71C bytes, the player first:
//             +00 x, +04 y, +08 z (floats, y up)
//             +0C pitch (s32, 65536 a turn, + nose up)
//             +28 heading (s32, 65536 a turn, -atan2(dx, dz))
//             +4C speed (float, units a frame; x 216 the speedometer's km/h)
//             +284 lean (s32, 65536 a turn, negated: the bike leans into a
//                  turn, + right; about 40 degrees at most)
//   00517BD0  time left (u16, frames; 1200 in the menus and the attract mode)
//   00517CB0  time since the start's countdown (u16, frames; still once the
//             race is over)
#pragma once

#include "telemetry/telemetry.h"

namespace sm2::telemetry {

class MotorRaidTelemetry final : public GameTelemetry {
public:
    [[nodiscard]] const char* name() const override { return "Motor Raid"; }
    bool decode(const Memory& ram, u64 frame, Frame& out) override;

private:
    u16 m_race_time  = 0;
    u64 m_race_frame = 0;
};

}  // namespace sm2::telemetry
