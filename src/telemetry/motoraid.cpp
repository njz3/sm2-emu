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
#include "telemetry/motoraid.h"

#include <cmath>
#include <cstring>

namespace sm2::telemetry {
namespace {

constexpr u32   kWorkRam    = 0x00500000;
constexpr u32   kRiders     = 0x0057B8E4;
constexpr u32   kRiderSize  = 0x71C;
constexpr int   kRiderCount = 10;
constexpr u32   kTimeLeft   = 0x00517BD0;
constexpr u32   kRaceTime   = 0x00517CB0;
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

}  // namespace

bool MotorRaidTelemetry::decode(const Memory& memory, u64 frame, Frame& out)
{
    const std::span<const u8> ram = memory.work;
    if (ram.size() < kRiders + kRiderSize * kRiderCount - kWorkRam) {
        return false;
    }
    out.frame = frame;
    out.game  = "motoraid";
    out.others.resize(kRiderCount - 1);
    for (int k = 0; k < kRiderCount; ++k) {
        const u32 base = kRiders + static_cast<u32>(k) * kRiderSize;
        Vehicle&  v    = k == 0 ? out.player : out.others[static_cast<usize>(k - 1)];
        v.x            = read<float>(ram, base + 0x00);
        v.y            = read<float>(ram, base + 0x04);
        v.z            = read<float>(ram, base + 0x08);
        v.pitch        = wrap(static_cast<float>(read<s32>(ram, base + 0x0C)) * kAngle);
        v.heading      = wrap(-static_cast<float>(read<s32>(ram, base + 0x28)) * kAngle);
        v.roll         = wrap(-static_cast<float>(read<s32>(ram, base + 0x284)) * kAngle);
        v.speed        = read<float>(ram, base + 0x4C) * kGameFps;
        orient(v);
    }
    const u16 left = read<u16>(ram, kTimeLeft);
    const u16 race = read<u16>(ram, kRaceTime);
    out.time_left  = static_cast<float>(left) / kGameFps;
    out.race_time  = static_cast<float>(race) / kGameFps;
    // A race is on while its clock runs.
    if (race != m_race_time) {
        m_race_time  = race;
        m_race_frame = frame;
    }
    out.racing = left > 0 && race > 0 && frame - m_race_frame < 4;
    out.state  = out.racing ? State::Race : race > 0 && left == 0 ? State::Over : State::Unknown;
    kinematics(out);
    return true;
}

}  // namespace sm2::telemetry
