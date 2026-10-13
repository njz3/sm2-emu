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
#include "telemetry/daytona.h"

#include <cmath>
#include <cstring>

namespace sm2::telemetry {
namespace {

constexpr u32   kWorkRam  = 0x00500000;
constexpr u32   kCars     = 0x00505100;
constexpr u32   kCarSize  = 0x300;
constexpr int   kCarCount = 40;
constexpr float kPi       = 3.14159265f;
constexpr float kSegments = 196.0f;  ///< A lap of the beginner course.

template <typename T>
T read(std::span<const u8> ram, u32 address)
{
    T v{};
    std::memcpy(&v, ram.data() + (address - kWorkRam), sizeof(T));
    return v;
}

}  // namespace

bool DaytonaTelemetry::decode(const Memory& memory, u64 frame, Frame& out)
{
    const std::span<const u8> ram = memory.work;
    if (ram.size() < kCars + kCarSize * kCarCount - kWorkRam) {
        return false;
    }
    out.frame = frame;
    out.game  = "daytona";
    out.track = "";
    out.others.resize(kCarCount - 1);
    for (int k = 0; k < kCarCount; ++k) {
        const u32 base = kCars + static_cast<u32>(k) * kCarSize;
        Vehicle&  v    = k == 0 ? out.player : out.others[static_cast<usize>(k - 1)];
        v.x            = read<float>(ram, base + 0x9C);
        v.y            = read<float>(ram, base + 0xA0);
        v.z            = read<float>(ram, base + 0xA4);
        float h        = -static_cast<float>(read<s32>(ram, base + 0xC8)) * 2 * kPi / 65536.0f;
        h              = std::fmod(h + kPi, 2 * kPi);
        v.heading      = (h < 0 ? h + 2 * kPi : h) - kPi;
        const int done = read<u8>(ram, base + 0xF0);
        v.lap          = done + 1;
        v.progress     = static_cast<float>(done)
                   + static_cast<float>(read<s32>(ram, base + 0x1AC)) / 65536.0f / kSegments;
        v.position     = read<u8>(ram, base + 0xEC) + 1;
        orient(v);
    }
    out.laps     = 8;
    out.racing   = out.player.lap >= 1 && out.player.lap <= out.laps;
    out.finished = out.player.lap > out.laps;
    kinematics(out);
    for (usize i = 0; i <= out.others.size(); ++i) {
        Vehicle& v = i == 0 ? out.player : out.others[i - 1];
        v.speed    = std::sqrt(v.vx * v.vx + v.vz * v.vz);
    }
    return true;
}

}  // namespace sm2::telemetry
