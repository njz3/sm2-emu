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
#include "telemetry/stcc.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sm2::telemetry {
namespace {

constexpr u32   kScratch  = 0x00200000;
constexpr u32   kCars     = 0x0020A900;
constexpr u32   kCarSize  = 0x58;
constexpr int   kCarCount = 8;
constexpr u32   kTimeLeft = 0x002022EC;
constexpr float kFps      = 57.5f;

template <typename T>
T read(std::span<const u8> ram, u32 address)
{
    T v{};
    std::memcpy(&v, ram.data() + (address - kScratch), sizeof(T));
    return v;
}

}  // namespace

bool StccTelemetry::decode(const Memory& memory, u64 frame, Frame& out)
{
    const std::span<const u8> ram = memory.scratch;
    if (ram.size() < kCars + kCarSize * kCarCount - kScratch) {
        return false;
    }
    out.frame = frame;
    out.game  = "stcc";
    out.others.resize(kCarCount - 1);
    for (int k = 0; k < kCarCount; ++k) {
        const u32 base = kCars + static_cast<u32>(k) * kCarSize;
        m_segments     = std::max(m_segments, static_cast<int>(read<u16>(ram, base + 0x50)) + 1);
    }
    for (int k = 0; k < kCarCount; ++k) {
        const u32 base   = kCars + static_cast<u32>(k) * kCarSize;
        Vehicle&  v      = k == 0 ? out.player : out.others[static_cast<usize>(k - 1)];
        v.x              = read<float>(ram, base + 0x00);
        v.y              = read<float>(ram, base + 0x04);
        v.z              = read<float>(ram, base + 0x08);
        const float vx   = read<float>(ram, base + 0x0C);
        const float vy   = read<float>(ram, base + 0x10);
        const float vz   = read<float>(ram, base + 0x14);
        v.speed          = std::sqrt(vx * vx + vy * vy + vz * vz) * kFps;
        v.heading        = -read<float>(ram, base + 0x1C);
        v.pitch          = 0;
        v.roll           = 0;
        const int driven = read<s16>(ram, base + 0x52);
        v.sector         = read<u16>(ram, base + 0x50);
        v.progress       = static_cast<float>(driven) / static_cast<float>(m_segments);
        v.lap            = static_cast<int>(std::floor(v.progress)) + 1;
        orient(v);
    }
    out.sectors   = m_segments;
    out.time_left = static_cast<float>(read<u32>(ram, kTimeLeft)) / kFps;
    out.racing    = out.time_left > 0;
    Vehicle& p    = out.player;
    p.position    = 1;
    for (const Vehicle& o : out.others) {
        if (o.progress > p.progress) {
            ++p.position;
        }
    }
    kinematics(out);
    return true;
}

}  // namespace sm2::telemetry
