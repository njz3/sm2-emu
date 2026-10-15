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
#include "telemetry/waverunr.h"

#include <cmath>
#include <cstring>

namespace sm2::telemetry {
namespace {

constexpr u32   kWorkRam  = 0x00500000;
constexpr u32   kPlayer   = 0x0051DF1C;
constexpr u32   kRaceTime = 0x005AD874;
constexpr float kGameFps  = 60.0f;
constexpr float kKmh      = 155.7f;  ///< The speed's unit, in km/h.
constexpr float kPi       = 3.14159265f;
constexpr float kAngle    = 2 * kPi / 65536;

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

bool WaveRunnerTelemetry::decode(const Memory& memory, u64 frame, Frame& out)
{
    const std::span<const u8> ram = memory.work;
    if (ram.size() < kRaceTime + 2 - kWorkRam) {
        return false;
    }
    out.frame  = frame;
    out.game   = "waverunr";
    Vehicle& p = out.player;
    p.x        = read<float>(ram, kPlayer + 0x00);
    p.y        = read<float>(ram, kPlayer + 0x04);
    p.z        = read<float>(ram, kPlayer + 0x08);
    p.heading  = wrap(-static_cast<float>(read<s32>(ram, kPlayer + 0x30)) * kAngle);
    p.pitch    = 0;  // the attitude on the waves is not known: level
    p.roll     = 0;
    orient(p);

    // The race clock runs only during a race: still a few frames, it is over.
    const u16 race = read<u16>(ram, kRaceTime);
    if (race != m_race_time) {
        m_race_time  = race;
        m_race_frame = frame;
    }
    out.race_time = static_cast<float>(race) / kGameFps;
    out.racing    = race > 0 && frame - m_race_frame < 4;
    out.state     = out.racing ? State::Race : race > 0 ? State::Over : State::Unknown;
    kinematics(out);
    p.speed = read<float>(ram, kPlayer + 0x48) * kKmh / 3.6f;
    return true;
}

}  // namespace sm2::telemetry
