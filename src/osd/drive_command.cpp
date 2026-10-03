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

// Daytona: effect in the high nibble, strength in the low one, decoded the
// way the drive board's program (epr-16488) does.
//   0x1x  motor off
//   0x2x  friction     0..7
//   0x3x  centring     0..7; 8..15 are the same strengths with a wider deadzone
//   0x4x  kick away from the centre  0..7
//   0x5x  push left    0..7
//   0x6x  push right   0..7
// All strengths share the board's scale (0..63), including its gain setting
// of 2. A push or kick is 12 + 2n, so even step 0 is a firm push; only 0x1x
// releases the wheel. A spring is 2n + 2, reached by a ramp of two a pot step
// from just inside its deadzone. Steps 8..15 of the other families match
// nothing on the board and also stop the motor. 0x0x and 0x7x are the boot
// handshake and the gain setting.
DriveCommand decode_daytona(u8 value)
{
    constexpr DriveCommand kOther{Effect::Other};
    constexpr DriveCommand kOff{};
    constexpr int kPotStep = 256;  // one step of the board's 8-bit pot, in axis units
    const int low = value & 0x0f;
    const auto push = [low](Effect effect) { return make(effect, 6 + low, 13); };
    switch (value & 0xf0) {
        case 0x10: return kOff;
        case 0x20: return low <= 7 ? make(Effect::Friction, low + 1, 8) : kOff;
        case 0x30: {
            DriveCommand spring = make(Effect::Spring, (low & 7) + 1, 13);
            spring.deadzone     = (low < 8 ? 3 : 8) * kPotStep;
            spring.ramp_from    = (low < 8 ? 2 : 7) * kPotStep;
            spring.full_at      = (low < 8 ? 21 : 24) * kPotStep;
            return spring;
        }
        case 0x40: return low <= 7 ? push(Effect::Kick) : kOff;
        case 0x50: return low <= 7 ? push(Effect::PushLeft) : kOff;
        case 0x60: return low <= 7 ? push(Effect::PushRight) : kOff;
        default:   return kOther;
    }
}

// Indy 500, Touring Car, OverRev and Super GT 24h share a later board program
// (epr-18261) that keeps Daytona's layout but reads the low nibble differently:
//   0x1x  motor off
//   0x2x  friction     0..7
//   0x3x  centring     0..7; 8..15 repeat them
//   0x5x  push left    0..7, where 0 is no push
//   0x6x  push right   0..7, where 0 is no push
// A push fades to nothing at step 0 (Indy 500 streams 0x54, 0x53 .. 0x50 as a
// jolt dies away), so strengths run from zero. 0x4x is not a command here.
// 0x0x is game state, 0x7x the motor strength, and 0xax/0xbx follow each
// effect in Indy 500 as parameters not yet understood.
DriveCommand decode_indy(u8 value)
{
    constexpr DriveCommand kOther{Effect::Other};
    constexpr int kPotStep = 256;
    const int low = value & 0x0f;
    const auto push = [low](Effect effect) {
        return low == 0 ? DriveCommand{} : make(effect, low, 7);
    };
    switch (value & 0xf0) {
        case 0x10: return DriveCommand{};
        case 0x20: return make(Effect::Friction, (low & 7) + 1, 8);
        case 0x30: {
            DriveCommand spring = make(Effect::Spring, (low & 7) + 1, 13);
            spring.deadzone     = 3 * kPotStep;
            spring.ramp_from    = 2 * kPotStep;
            spring.full_at      = 21 * kPotStep;
            return spring;
        }
        case 0x50: return push(Effect::PushLeft);
        case 0x60: return push(Effect::PushRight);
        default:   return kOther;
    }
}

// Touring Car streams its pushes every frame as a centring torque worked out
// from the car, which lags the wheel: swinging from one turn into the next, it
// still pushes the old way and throws the wheel. So only its strength is used,
// as a spring about the wheel's own position. It idles at 1, so strength runs
// from 1 (none) to 7 (full).
DriveCommand decode_stcc(u8 value)
{
    DriveCommand command = decode_indy(value);
    const int high = value & 0xf0;
    if (high == 0x50 || high == 0x60) {
        const int low    = value & 0x0f;
        command.effect   = Effect::Spring;
        command.strength = low <= 1 ? 0 : (low - 1) * kDriveFull / 6;
        command.held     = true;
    }
    return command;
}

// Sega Rally streams a torque every frame, strength in the low five bits,
// decoded the way its board's program (EPR-17891) does:
//   0x80..0x9f  push right  1..32
//   0xc0..0xdf  push left   1..32
//   0x40..0x5f  brake       1..32 (a power with no direction; not sent by the game)
//   0x10..0x17  chop the torque on and off, 0x10 stops; the game sends 0x15
//               on shocks and rough ground
// Holding the wheel off-centre makes the game push it back. 0x00 releases.
// Every other byte leaves the torque as it is: the board replays its last one.
DriveCommand decode_rally(u8 value)
{
    const int low = value & 0x1f;
    switch (value & 0xe0) {
        case 0x00:
            if (value == 0x00) {
                return DriveCommand{};
            }
            if ((value & 0xf8) == 0x10) {
                DriveCommand chop{Effect::Other};
                chop.pulse = value & 7;
                return chop;
            }
            return DriveCommand{Effect::Other};
        case 0x40: return make(Effect::Friction, low + 1, 32);
        case 0x80: return make(Effect::PushRight, low + 1, 32, true);
        case 0xc0: return make(Effect::PushLeft, low + 1, 32, true);
        default:   return DriveCommand{Effect::Other};
    }
}

}  // namespace

DriveCommand decode_drive_command(rom::DriveProtocol protocol, u8 value)
{
    switch (protocol) {
        case rom::DriveProtocol::Indy:  return decode_indy(value);
        case rom::DriveProtocol::Stcc:  return decode_stcc(value);
        case rom::DriveProtocol::Rally: return decode_rally(value);
        case rom::DriveProtocol::Daytona:
        default: return decode_daytona(value);
    }
}

}  // namespace sm2::osd
