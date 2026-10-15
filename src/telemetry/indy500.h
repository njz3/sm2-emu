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
// Indy 500's telemetry, found in its work RAM (AUTOPILOT.md tells how):
//
//   0053C7AC  33 cars of 0x1E4 bytes, the player first (28 raced on Bay Side
//             Street and Highland Raceway):
//             +000 x, +004 y, +008 z (floats, metres)
//             +00C roll (+ leaning right: negative in the oval's banked
//                  left turns), +010 yaw (negated), +014 pitch (+ nose up,
//                  with the slope): i32, a turn is 65536
//             +030 speed (float, metres a frame)
//             +0E4 progress: lap << 20 | sector << 16 | the sector's fraction
//                  (lap 0: the grid and the rolling start; laps + 1: done)
//   0053C332  time left (u16, frames)
//   0053C350  the course (u8): 0 Bay Side Street, 1 Indy 500 (the oval),
//             2 Highland Raceway
//   0053C5B9  the gearbox (u8): 0 manual (6 speeds), 1 automatic
//   0053C5BC  the player's gear (u8, 0 = first)
//   0053C700  the player's engine revs (float, rpm)
//   005441E4  the player's time in the lap (u32, frames)
//   005449A0  the player's camera: 3x3 floats, world from camera, then its
//             position (a chase camera, late in the turns)
#pragma once

#include "telemetry/telemetry.h"

namespace sm2::telemetry {

class Indy500Telemetry final : public GameTelemetry {
public:
    [[nodiscard]] const char* name() const override { return "Indy 500"; }
    bool decode(const Memory& ram, u64 frame, Frame& out) override;

private:
    int m_sectors = 1;  ///< On a course not in the tables: the highest sector seen, plus one.
};

}  // namespace sm2::telemetry
