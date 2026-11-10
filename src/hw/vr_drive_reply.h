//  ____  __  __  ____         _____ __  __ _   _
// / ___||  \/  ||___ \       | ____|  \/  | | | |
// \___ \| |\/| |  __) |_____ |  _| | |\/| | | | |
//  ___) | |  | | / __/|_____|| |___| |  | | |_| |
// |____/|_|  |_||_____|      |_____|_|  |_|\___/
//
// A Sega Model 1 & 2 arcade emulator.
// Copyright (c) 2025+ Daniel Martin (dmanlfc)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This header must not be removed. sm2-emu is licensed under the GNU General
// Public License, version 3 or later, with additional terms under section 7 of
// that licence. See LICENSE and NOTICE.
//
// The answer of the EPR-15111 drive board (Virtua Racing) on the I/O board's
// port D.
#pragma once

#include "core/types.h"

namespace sm2::hw {

struct VrDriveReply {
    /// Both DIP banks: everything OFF but switch 4, which skips the calibration.
    static constexpr u8 kDip = 0xf7;

    u8 command = 0x00;

    /// The board's answer, where `wheel` is the steering pot's value: the
    /// test menu's questions 81 to 83, else the byte echoed.
    [[nodiscard]] u8 read(u8 wheel) const
    {
        switch (command) {
            case 0x81:
            case 0x82: return kDip;
            case 0x83: return wheel;
            default:   return command;
        }
    }
};

}  // namespace sm2::hw
