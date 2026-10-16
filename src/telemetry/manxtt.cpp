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
#include "telemetry/manxtt.h"

#include <cmath>
#include <cstring>

namespace sm2::telemetry {
namespace {

constexpr u32   kWorkRam  = 0x00500000;
constexpr u32   kBike     = 0x00578A20;
constexpr u32   kGear     = 0x00578B64;
constexpr u32   kSpeed    = 0x005009D0;
constexpr u32   kTimeLeft = 0x005334F0;
constexpr float kMetre    = 0.1f;   ///< A unit.
constexpr float kClock    = 56.0f;  ///< Timer steps a second, as shown.
constexpr float kPi       = 3.14159265f;

template <typename T>
T read(std::span<const u8> ram, u32 address)
{
    T v{};
    std::memcpy(&v, ram.data() + (address - kWorkRam), sizeof(T));
    return v;
}

float wrap(float a)
{
    a = std::fmod(a + kPi, 2 * kPi);
    return (a < 0 ? a + 2 * kPi : a) - kPi;
}

}  // namespace

bool ManxTtTelemetry::decode(const Memory& memory, u64 frame, Frame& out)
{
    const std::span<const u8> ram = memory.work;
    if (ram.size() < kGear + 1 - kWorkRam) {
        return false;
    }
    out.frame  = frame;
    out.game   = "manxtt";
    Vehicle& p = out.player;
    p.x        = read<float>(ram, kBike + 0x00) * kMetre;
    p.y        = read<float>(ram, kBike + 0x04) * kMetre;
    p.z        = read<float>(ram, kBike + 0x08) * kMetre;
    p.pitch    = wrap(read<float>(ram, kBike + 0x0C));
    p.heading  = wrap(-read<float>(ram, kBike + 0x10));
    p.roll     = wrap(read<float>(ram, kBike + 0x14));
    orient(p);
    p.gear = read<u8>(ram, kGear);

    // A race is on while its clock runs; the menus' screens hold it still.
    const u16 left = read<u16>(ram, kTimeLeft);
    if (left != m_time_left) {
        m_time_left  = left;
        m_time_frame = frame;
    }
    out.time_left = static_cast<float>(left) / kClock;
    out.racing    = left > 0 && frame - m_time_frame < 6;
    kinematics(out);
    p.speed = static_cast<float>(read<u16>(ram, kSpeed)) / 3.6f;
    return true;
}

}  // namespace sm2::telemetry
