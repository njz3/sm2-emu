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
#include "telemetry/sgt24h.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sm2::telemetry {
namespace {

constexpr u32   kWorkRam   = 0x00500000;
constexpr u32   kCars      = 0x00518424;
constexpr u32   kCarSize   = 0x1728;
constexpr int   kCarCount  = 14;
constexpr u32   kState     = 0x00510948;
constexpr u32   kLapTime   = 0x00510F2C;
constexpr u32   kLaps      = 0x00510F6C;
constexpr u32   kTimeLeft  = 0x00510F74;
constexpr u32   kStateRace = 3;
constexpr u32   kStateOver = 4;
constexpr float kLap       = 3000.0f;
constexpr float kGameFps   = 60.0f;
constexpr float kPi        = 3.14159265f;
constexpr float kAngle     = 2 * kPi / 65536;

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

bool Sgt24hTelemetry::decode(const Memory& memory, u64 frame, Frame& out)
{
    const std::span<const u8> ram = memory.work;
    if (ram.size() < kCars + kCarSize * kCarCount - kWorkRam) {
        return false;
    }
    const u32 state = read<u32>(ram, kState);
    out.frame = frame;
    out.game  = "sgt24h";
    switch (state) {
        case 1: out.state = State::Attract; break;
        case 2:
        case 7: out.state = State::Select; break;
        case kStateRace: out.state = State::Race; break;
        case kStateOver: out.state = State::Over; break;
        default: break;
    }
    if (state != kStateRace && state != kStateOver) {
        return true;  // the state alone: the cars are not set up
    }
    out.lap_length = kLap;
    out.laps  = read<u8>(ram, kLaps);
    out.others.resize(kCarCount - 1);
    for (int k = 0; k < kCarCount; ++k) {
        const u32 base = kCars + static_cast<u32>(k) * kCarSize;
        // The player is the last car.
        Vehicle& v = k == kCarCount - 1 ? out.player : out.others[static_cast<usize>(k)];
        v.x        = read<float>(ram, base + 0x0C);
        v.y        = read<float>(ram, base + 0x10);
        v.z        = read<float>(ram, base + 0x14);
        v.heading  = wrap(-static_cast<float>(read<s32>(ram, base + 0x34)) * kAngle);
        v.pitch    = 0;
        v.roll     = 0;
        v.speed    = read<float>(ram, base + 0x84C) * kGameFps;
        v.progress = read<float>(ram, base) / kLap;
        v.lap      = read<u8>(ram, base - 0x04) + 1;
        v.position = read<u8>(ram, base - 0x0C) + 1;
        orient(v);
    }

    // The player: its orientation, gear, revs.
    Vehicle&  p    = out.player;
    const u32 base = kCars + (kCarCount - 1) * kCarSize;
    for (int i = 0; i < 9; ++i) {
        p.rotation[static_cast<usize>(i)] = read<float>(ram, base + 0x764 + static_cast<u32>(4 * i));
    }
    const auto& r = p.rotation;
    p.heading     = std::atan2(r[2], r[8]);
    p.pitch       = std::asin(std::clamp(r[5], -1.0f, 1.0f));
    // The right side's height: lower, leaning right.
    p.roll        = std::atan2(-r[3], r[4]);
    p.gear        = read<u8>(ram, base + 0xC8) + 1;
    p.rpm         = read<u16>(ram, base + 0x220);

    const int lap_time = read<u16>(ram, kLapTime);
    out.time_left      = static_cast<float>(std::max<int>(read<s16>(ram, kTimeLeft), 0)) / kGameFps;
    out.lap_time       = static_cast<float>(lap_time) / kGameFps;
    out.finished       = out.laps > 0 && p.lap > out.laps;
    out.racing         = state == kStateRace && lap_time > 0 && out.time_left > 0 && !out.finished;
    if (state == kStateRace && !out.racing && !out.finished) {
        out.state = lap_time == 0 ? State::Select : State::Over;  // the countdown, or time up
    }
    kinematics(out);
    return true;
}

}  // namespace sm2::telemetry
