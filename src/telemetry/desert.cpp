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
#include "telemetry/desert.h"

#include <cmath>
#include <cstring>

namespace sm2::telemetry {
namespace {

constexpr u32   kWorkRam  = 0x00500000;
constexpr u32   kCentre   = 0x005003D4;
constexpr u32   kCorners  = 0x00500404;
constexpr u32   kTimer    = 0x00521040;
constexpr float kTimerHz  = 32.0f;  ///< Timer steps a second, as shown.

template <typename T>
T read(std::span<const u8> ram, u32 address)
{
    T v{};
    std::memcpy(&v, ram.data() + (address - kWorkRam), sizeof(T));
    return v;
}

struct Point {
    float x, y, z;
};

Point point(std::span<const u8> ram, u32 address)
{
    return {read<float>(ram, address), read<float>(ram, address + 4), read<float>(ram, address + 8)};
}

}  // namespace

bool DesertTankTelemetry::decode(const Memory& memory, u64 frame, Frame& out)
{
    const std::span<const u8> ram = memory.work;
    if (ram.size() < kTimer + 2 - kWorkRam) {
        return false;
    }
    out.frame          = frame;
    out.game           = "desert";
    Vehicle&    p      = out.player;
    const Point centre = point(ram, kCentre);
    p.x                = centre.x;
    p.y                = centre.y;
    p.z                = centre.z;

    // Rear right, rear left, front right, front left.
    const Point rr = point(ram, kCorners), rl = point(ram, kCorners + 12);
    const Point fr = point(ram, kCorners + 24), fl = point(ram, kCorners + 36);
    const float dx = (fr.x + fl.x - rr.x - rl.x) / 2, dy = (fr.y + fl.y - rr.y - rl.y) / 2;
    const float dz = (fr.z + fl.z - rr.z - rl.z) / 2;
    const float sx = (fl.x + rl.x - fr.x - rr.x) / 2, sy = (fl.y + rl.y - fr.y - rr.y) / 2;
    const float sz = (fl.z + rl.z - fr.z - rr.z) / 2;
    p.heading = std::atan2(dx, dz);
    p.pitch   = std::atan2(dy, std::hypot(dx, dz));
    p.roll    = std::atan2(sy, std::hypot(sx, sz));  // the left side higher: leaning right
    orient(p);

    const u16 timer = read<u16>(ram, kTimer);
    out.time_left   = static_cast<float>(timer) / kTimerHz;
    out.racing      = timer > 0 && std::hypot(dx, dz) > 0.5f;
    kinematics(out);
    p.speed = std::sqrt(p.vx * p.vx + p.vz * p.vz);
    return true;
}

}  // namespace sm2::telemetry
