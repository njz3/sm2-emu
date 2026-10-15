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

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sm2::telemetry {
namespace {

constexpr u32   kWorkRam   = 0x00500000;
constexpr u32   kCars      = 0x00505100;
constexpr u32   kCarSize   = 0x300;
constexpr int   kCarCount  = 40;
constexpr u32   kState     = 0x005010A0;
constexpr u32   kRaceTime  = 0x005010CC;
constexpr u32   kTimeLeft  = 0x005010D0;
constexpr u32   kLapsDone  = 0x005010E0;
constexpr u32   kLaps      = 0x005010EC;
constexpr u32   kLapTimes  = 0x00502A00;
constexpr float kPi        = 3.14159265f;
constexpr float kAngle16   = 2 * kPi / 65536;
constexpr float kSegments  = 196.0f;    ///< A lap of the beginner course.
constexpr float kLapLength = 1569.4f;   ///< The beginner course, in units (autopilot's map).
constexpr float kClock     = 64.0f;     ///< Frames a second, on the game's clocks.

State state_of(u8 s)
{
    if (s >= 1 && s <= 3) {
        return State::Attract;
    }
    if (s >= 17 && s <= 21) {
        return State::Select;
    }
    if (s == 22) {
        return State::Race;
    }
    if (s == 26) {
        return State::Over;
    }
    return State::Unknown;
}

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
        v.pitch        = static_cast<float>(read<s16>(ram, base + 0xA8)) * kAngle16;
        v.roll         = static_cast<float>(read<s16>(ram, base + 0xAC)) * kAngle16;
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
    Vehicle&  p    = out.player;
    const u32 base = kCars;
    p.gear         = read<u8>(ram, base + 0x1CC) + 1;
    p.gears        = 4;
    p.rpm          = static_cast<float>(read<u32>(ram, base + 0x1D4));
    p.rpm_max      = 8000.0f;  // the dial's red zone

    out.laps       = read<u8>(ram, kLaps);
    out.lap_length = kLapLength;
    out.state      = state_of(read<u8>(ram, kState));
    out.finished   = out.laps > 0 && p.lap > out.laps;
    out.racing     = out.state == State::Race && p.lap >= 1 && !out.finished;
    const u32 race = read<u32>(ram, kRaceTime);
    out.time_left  = static_cast<float>(read<u16>(ram, kTimeLeft)) / kClock;
    out.race_time  = static_cast<float>(race) / kClock;
    // The laps done are in the table; the current one is the rest of the race.
    const int done = std::min<int>(read<u8>(ram, kLapsDone), 16);
    u32       sum  = 0;
    u32       best = 0;
    for (int i = 0; i < done; ++i) {
        const u32 t = read<u32>(ram, kLapTimes + static_cast<u32>(4 * i));
        sum += t;
        best = best == 0 ? t : std::min(best, t);
    }
    if (done > 0) {
        out.last_lap = static_cast<float>(read<u32>(ram, kLapTimes + static_cast<u32>(4 * (done - 1)))) / kClock;
        out.best_lap = static_cast<float>(best) / kClock;
    }
    if (race >= sum) {
        out.lap_time = static_cast<float>(race - sum) / kClock;
    }
    kinematics(out);
    for (usize i = 0; i <= out.others.size(); ++i) {
        Vehicle& v = i == 0 ? out.player : out.others[i - 1];
        v.speed    = std::sqrt(v.vx * v.vx + v.vz * v.vz);
    }
    p.speed = static_cast<float>(read<u16>(ram, base + 0x98)) / 3.6f;
    return true;
}

}  // namespace sm2::telemetry
