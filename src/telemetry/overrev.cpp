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
#include "telemetry/overrev.h"

#include <cmath>
#include <cstring>

namespace sm2::telemetry {
namespace {

constexpr u32   kWorkRam    = 0x00500000;
constexpr u32   kCars       = 0x0059E6A8;
constexpr u32   kCarSize    = 0x2A8;
constexpr int   kAiCount    = 7;
constexpr u32   kAngles     = 0x00590DE4;
constexpr u32   kGear       = 0x00595A5C;
constexpr u32   kRpm        = 0x00595A7C;
constexpr u32   kInLap      = 0x00595AA4;
constexpr u32   kSpeed      = 0x00595AB8;
constexpr u32   kPosition   = 0x00595C04;
constexpr u32   kLapsDone   = 0x00595C9C;
constexpr u32   kRaceTime   = 0x00595CAC;
constexpr u32   kLapTime    = 0x00595CB0;
constexpr u32   kTimeLeft   = 0x00595CE0;
constexpr u32   kLaps       = 0x00595D18;
constexpr u32   kLapLength  = 0x0051D33C;
constexpr float kDefaultLap = 2963.8f;  ///< Round 1's course.
constexpr float kMetre      = 0.5f;     ///< A unit.
constexpr float kGameFps    = 60.0f;
constexpr float kPi         = 3.14159265f;
constexpr float kAngle      = 2 * kPi / 65536;

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

float angle(std::span<const u8> ram, u32 address)
{
    return static_cast<float>(read<s32>(ram, address)) * kAngle;
}

}  // namespace

bool OverRevTelemetry::decode(const Memory& memory, u64 frame, Frame& out)
{
    const std::span<const u8> ram = memory.work;
    if (ram.size() < kCars + kCarSize * kAiCount - kWorkRam) {
        return false;
    }
    const int race_time = read<u16>(ram, kRaceTime);
    const int time_left = read<u8>(ram, kTimeLeft);
    if (race_time == 0 && time_left == 0) {
        return false;  // the attract mode and the menus
    }
    float lap = read<float>(ram, kLapLength);
    if (!(lap > 500.0f && lap < 50000.0f)) {
        lap = kDefaultLap;
    }
    out.frame = frame;
    out.game  = "overrev";
    out.laps  = read<u8>(ram, kLaps);
    out.others.resize(kAiCount);

    Vehicle& p = out.player;
    p.x        = read<float>(ram, kPosition) * kMetre;
    p.y        = read<float>(ram, kPosition + 4) * kMetre;
    p.z        = read<float>(ram, kPosition + 8) * kMetre;
    p.roll     = -angle(ram, kAngles);
    p.pitch    = angle(ram, kAngles + 0x10);
    p.heading  = wrap(-angle(ram, kAngles + 0x14));
    p.speed    = read<float>(ram, kSpeed);
    p.gear     = read<u8>(ram, kGear);
    p.rpm      = read<float>(ram, kRpm);
    const s32 laps_done = read<s32>(ram, kLapsDone);
    p.progress = static_cast<float>(laps_done) + read<float>(ram, kInLap) / lap;
    p.lap      = (laps_done < 0 ? 0 : laps_done) + 1;
    orient(p);

    for (int k = 0; k < kAiCount; ++k) {
        const u32 base = kCars + static_cast<u32>(k) * kCarSize;
        Vehicle&  v    = out.others[static_cast<usize>(k)];
        v.x            = read<float>(ram, base + 0x100) * kMetre;
        v.y            = read<float>(ram, base + 0x104) * kMetre;
        v.z            = read<float>(ram, base + 0x108) * kMetre;
        v.heading      = wrap(kPi / 2 - angle(ram, base + 0xA4));
        v.pitch        = 0;
        v.roll         = 0;
        v.speed        = read<float>(ram, base + 0xE0) * kMetre * kGameFps;
        v.progress     = read<float>(ram, base + 0x1F0) / lap;
        v.lap          = read<u8>(ram, base + 0x50) + 1;
        orient(v);
    }
    // Positions from the progress, as the game's display has them.
    for (int k = 0; k <= kAiCount; ++k) {
        Vehicle& v = k == 0 ? p : out.others[static_cast<usize>(k - 1)];
        v.position = 1;
        for (int j = 0; j <= kAiCount; ++j) {
            const Vehicle& o = j == 0 ? p : out.others[static_cast<usize>(j - 1)];
            if (j != k && o.progress > v.progress) {
                ++v.position;
            }
        }
    }

    out.time_left = static_cast<float>(time_left);
    out.lap_time  = static_cast<float>(read<u16>(ram, kLapTime)) / kGameFps;
    out.finished  = out.laps > 0 && p.lap > out.laps;
    out.racing    = race_time > 0 && time_left > 0 && !out.finished;
    kinematics(out);
    return true;
}

}  // namespace sm2::telemetry
