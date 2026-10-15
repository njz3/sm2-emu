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
// Desert Tank's telemetry, from its work RAM (TELEMETRY.md tells how it was
// found): the player's tank. The game moves it every other frame.
//
//   005003D4  its centre: x, y, z (floats, y up)
//   00500404  its four corners, x, y, z each (floats): rear right, rear left,
//             front right, front left; the heading, pitch and roll come from
//             them (the ground under them is nearly flat: the dunes are
//             drawn, not driven)
//   00521040  mission timer (u16; the screen shows it divided by 32)
#pragma once

#include "telemetry/telemetry.h"

namespace sm2::telemetry {

class DesertTankTelemetry final : public GameTelemetry {
public:
    [[nodiscard]] const char* name() const override { return "Desert Tank"; }
    bool decode(const Memory& ram, u64 frame, Frame& out) override;
};

}  // namespace sm2::telemetry
