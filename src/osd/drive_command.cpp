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
#include "osd/drive_command.h"

namespace sm2::osd {
namespace {

using Effect = DriveCommand::Effect;

DriveCommand make(Effect effect, int steps, int full_steps, bool held = false)
{
    return {effect, steps * kDriveFull / full_steps, held};
}

// Daytona, Indy 500, Touring Car, Over Rev and Super GT: effect in the high
// nibble, strength in the low one. Indy 500's board, which the last four share,
// is IndyBoard for a force-feedback wheel; this reading is only what pads and
// plain rumble get of it.
//   0x1x  spring       0..7
//   0x2x  friction     0..7
//   0x3x  centring     0..12
//   0x4x  vibration    0..7
//   0x5x  push left    0 releases, up to 7
//   0x6x  push right   0 releases, up to 7
// 0x0x and 0x7x are the boot handshake. Indy 500 follows each effect with two
// parameter bytes, 0xbx then 0xax, not yet understood.
DriveCommand decode_daytona(u8 value)
{
    constexpr DriveCommand kOther{Effect::Other};
    const int low = value & 0x0f;
    switch (value & 0xf0) {
        case 0x10: return low <= 7  ? make(Effect::Spring, low + 1, 8) : kOther;
        case 0x20: return low <= 7  ? make(Effect::Friction, low + 1, 8) : kOther;
        case 0x30: return low <= 12 ? make(Effect::Spring, low + 1, 13) : kOther;
        case 0x40: return low <= 7  ? make(Effect::Vibrate, low + 1, 8) : kOther;
        case 0x50: return low <= 7  ? make(Effect::PushLeft, low, 7) : kOther;
        case 0x60: return low <= 7  ? make(Effect::PushRight, low, 7) : kOther;
        default:   return kOther;
    }
}

// Sega Rally streams a torque every frame, strength in the low five bits:
//   0x80..0x9f  push right  1..32
//   0xc0..0xdf  push left   1..32
// Holding the wheel over sends a push back towards centre. 0x00 releases.
// 0x10..0x17 set the board's torque chopping (0x10 ends it) without touching
// the torque, which the game uses as 0x15 on shocks and rough ground. Every
// other byte leaves the torque as it is: the board replays its last torque
// command (0x40..0x5f, a power with no direction, is not sent by the game).
// Read in the board's EPROM, EPR-17891.
DriveCommand decode_rally(u8 value)
{
    const int low = value & 0x1f;
    switch (value & 0xe0) {
        case 0x80: return make(Effect::PushRight, low + 1, 32, true);
        case 0xc0: return make(Effect::PushLeft, low + 1, 32, true);
        default:   break;
    }
    if (value == 0x00) {
        return DriveCommand{};
    }
    if ((value & 0xf8) == 0x10) {
        return {Effect::Chop, 0, false, static_cast<u8>(value & 0x07)};
    }
    return DriveCommand{Effect::Other};
}

}  // namespace

DriveCommand decode_drive_command(rom::DriveProtocol protocol, u8 value)
{
    switch (protocol) {
        case rom::DriveProtocol::Indy:  return decode_daytona(value);
        case rom::DriveProtocol::Rally: return decode_rally(value);
        case rom::DriveProtocol::Daytona:
        default: return decode_daytona(value);
    }
}

}  // namespace sm2::osd
