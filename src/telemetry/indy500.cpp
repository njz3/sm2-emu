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
#include "telemetry/indy500.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace sm2::telemetry {
namespace {

constexpr u32   kWorkRam      = 0x00500000;
constexpr int   kCarCount     = 33;
constexpr float kFps          = 57.5f;
constexpr float kPi           = 3.14159265f;
constexpr float kAngle        = 2 * kPi / 65536;

/// A course: its name, the race's laps and where each sector starts along
/// the lap (a fraction of its length, from the maps of autopilot/track_map.py).
struct Course {
    const char*  name;
    int          laps;
    int          cars;
    int          sectors;
    float        start[8];
    float        length;  ///< Metres, the autopilot's map of the racing line.
};

constexpr Course kCourses[] = {
    {"Bay Side Street", 3, 28, 8, {0.0f, 0.131f, 0.264f, 0.396f, 0.528f, 0.661f, 0.795f, 0.927f}, 4909.6f},
    {"Indy 500", 4, 33, 7, {0.0f, 0.16f, 0.321f, 0.482f, 0.643f, 0.802f, 0.963f}, 4052.6f},
    {"Highland Raceway", 3, 28, 8, {0.0f, 0.141f, 0.281f, 0.423f, 0.564f, 0.704f, 0.843f, 0.985f}, 4609.6f},
};

/// Where a version keeps the race.
struct Layout {
    const char* set;
    u32         cars, car_size, angles, progress;
    u32         time_left, course, transmission, gear, rpm, camera;
    u32         lap_time;  ///< 0: not found.
};

constexpr Layout kLayouts[] = {
    {"indy500", 0x0053C7AC, 0x1E4, 0x00C, 0x0E4,
     0x0053C332, 0x0053C350, 0x0053C5B9, 0x0053C5BC, 0x0053C700, 0x005449A0, 0x005441E4},
    {"indy500to", 0x0053C77C, 0x1E4, 0x00C, 0x0E4,
     0x0053C322, 0x0053C340, 0x0053C594, 0x0053C598, 0x0053C6E0, 0x00544980, 0x005441C4},
    {"indy500d", 0x0054C714, 0x1EC, 0x014, 0x0E8,
     0x0054C2D2, 0x0054C2F0, 0x0054C4A0, 0x0054C4A4, 0x0054C600, 0x00554C80, 0},
};

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

Indy500Telemetry::Indy500Telemetry(const std::string& set)
{
    for (usize i = 0; i < std::size(kLayouts); ++i) {
        if (set == kLayouts[i].set) {
            m_layout = i;
        }
    }
}

bool Indy500Telemetry::decode(const Memory& memory, u64 frame, Frame& out)
{
    const std::span<const u8> ram = memory.work;
    if (ram.size() < 0x60000) {
        return false;
    }
    const Layout& l         = kLayouts[m_layout];
    const u8      course_id = read<u8>(ram, l.course);
    const Course* course    = course_id < std::size(kCourses) ? &kCourses[course_id] : nullptr;
    out.frame        = frame;
    out.game         = "indy500";
    out.track        = course != nullptr ? course->name : "";
    out.laps         = course != nullptr ? course->laps : 0;
    out.lap_length   = course != nullptr ? course->length : NAN;
    out.transmission = read<u8>(ram, l.transmission) == 0 ? "MT" : "AT";
    const int cars = course != nullptr ? course->cars : kCarCount;
    out.others.resize(static_cast<usize>(cars - 1));

    int max_sector = 0;
    for (int k = 0; k < cars; ++k) {
        const u32 base = l.cars + static_cast<u32>(k) * l.car_size;
        Vehicle&  v    = k == 0 ? out.player : out.others[static_cast<usize>(k - 1)];
        v.x            = read<float>(ram, base + 0x000);
        v.y            = read<float>(ram, base + 0x004);
        v.z            = read<float>(ram, base + 0x008);
        v.roll         = static_cast<float>(read<s32>(ram, base + l.angles)) * kAngle;
        v.heading      = wrap(-static_cast<float>(read<s32>(ram, base + l.angles + 4)) * kAngle);
        v.pitch        = static_cast<float>(read<s32>(ram, base + l.angles + 8)) * kAngle;
        v.speed        = read<float>(ram, base + 0x030) * kFps;
        const u32 e4   = read<u32>(ram, base + l.progress);
        v.lap          = static_cast<int>(e4 >> 20);
        v.sector       = static_cast<int>((e4 >> 16) & 15);
        max_sector     = std::max(max_sector, v.sector);
        v.progress     = static_cast<float>(e4 & 0xffff) / 65536.0f;  // the sector's fraction, for now
        orient(v);
    }
    m_sectors   = std::max(m_sectors, max_sector + 1);
    out.sectors = course != nullptr ? course->sectors : m_sectors;
    for (int k = 0; k < cars; ++k) {
        Vehicle&    v = k == 0 ? out.player : out.others[static_cast<usize>(k - 1)];
        const float f = v.progress;
        float       in_lap;
        if (course != nullptr && v.sector < course->sectors) {
            // The sectors' lengths differ: the lap's fraction by distance.
            const float a = course->start[v.sector];
            const float b = v.sector + 1 < course->sectors ? course->start[v.sector + 1] : 1.0f;
            in_lap        = a + f * (b - a);
        } else {
            in_lap = (static_cast<float>(v.sector) + f) / static_cast<float>(m_sectors);
        }
        v.progress = static_cast<float>(v.lap - 1) + in_lap;
    }

    // The player: the camera, gear, revs, times.
    Vehicle& p = out.player;
    for (int i = 0; i < 9; ++i) {
        out.camera[static_cast<usize>(i)] = read<float>(ram, l.camera + static_cast<u32>(4 * i));
    }
    p.gear        = static_cast<int>(read<u8>(ram, l.gear)) + 1;
    p.gears       = out.transmission == "MT" ? 6 : kUnknown;
    p.rpm         = read<float>(ram, l.rpm);
    out.time_left = static_cast<float>(read<u16>(ram, l.time_left)) / kFps;
    if (l.lap_time != 0) {
        out.lap_time = static_cast<float>(read<u32>(ram, l.lap_time)) / kFps;
    }
    out.finished  = out.laps > 0 && p.lap > out.laps;
    out.racing    = p.lap >= 1 && !out.finished;

    p.position = 1;
    for (const Vehicle& o : out.others) {
        if (o.progress > p.progress) {
            ++p.position;
        }
    }
    kinematics(out);
    return p.lap >= 1;
}

}  // namespace sm2::telemetry
