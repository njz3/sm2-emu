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
#include "telemetry/srally.h"

#include <cmath>
#include <cstring>

namespace sm2::telemetry {
namespace {

constexpr u32 kWorkRam   = 0x00500000;
constexpr u32 kEntrySize = 0x100;
constexpr int kCars      = 8;

template <typename T>
T read(std::span<const u8> ram, u32 address)
{
    T v{};
    std::memcpy(&v, ram.data() + (address - kWorkRam), sizeof(T));
    return v;
}

}  // namespace

bool SrallyTelemetry::decode(const Memory& memory, u64 frame, Frame& out)
{
    const std::span<const u8> ram = memory.work;
    if (ram.size() < kEntrySize * kCars) {
        return false;
    }
    out.frame = frame;
    out.game  = "srallyc";
    out.track = "";
    out.others.resize(kCars - 1);
    for (int k = 0; k < kCars; ++k) {
        const u32 base = kWorkRam + static_cast<u32>(k) * kEntrySize;
        Vehicle&  v    = k == 0 ? out.player : out.others[static_cast<usize>(k - 1)];
        v.x            = read<float>(ram, base + 0x14);
        v.y            = read<float>(ram, base + 0x18);
        v.z            = read<float>(ram, base + 0x1C);
        if (k == 0) {
            v.heading  = -read<float>(ram, base + 0x3C);
            v.progress = static_cast<float>(read<u32>(ram, base + 0x84)) / 65536.0f;
        } else {
            const float a = read<float>(ram, base + 0xA4);   // atan2(dz, dx)
            v.heading     = std::atan2(std::cos(a), std::sin(a));
            v.progress    = static_cast<float>(read<u32>(ram, base + 0x88)) / 65536.0f;
        }
        v.lap = 1;
        orient(v);
    }
    const u8 clock = read<u8>(ram, 0x005229E6);
    out.time_left  = static_cast<float>((clock >> 4) * 10 + (clock & 15));
    out.racing     = out.time_left > 0;
    kinematics(out);
    for (usize i = 0; i <= out.others.size(); ++i) {
        Vehicle& v = i == 0 ? out.player : out.others[i - 1];
        v.speed    = std::sqrt(v.vx * v.vx + v.vz * v.vz);
    }
    return true;
}

}  // namespace sm2::telemetry
