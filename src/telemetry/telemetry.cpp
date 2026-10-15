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
#include "telemetry/telemetry.h"

#include "core/log.h"
#include "rom/game.h"
#include "telemetry/daytona.h"
#include "telemetry/desert.h"
#include "telemetry/indy500.h"
#include "telemetry/motoraid.h"
#include "telemetry/overrev.h"
#include "telemetry/sgt24h.h"
#include "telemetry/srally.h"
#include "telemetry/stcc.h"
#include "telemetry/waverunr.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <set>
#include <thread>

namespace sm2::telemetry {
namespace {

constexpr float kFps = 57.5245f;
constexpr float kPi  = 3.14159265f;

/// A collision: more than this acceleration in one frame (8 g; a fast turn
/// reaches 3 to 4)...
constexpr float kImpactAccel = 80.0f;
/// ...but less than this change of speed, more being a jump to elsewhere.
constexpr float kImpactMax = 50.0f;
/// Higher above the ground than this is in the air.
constexpr float kAirborne = 0.25f;
/// Faster than this from one frame to the next is a jump to elsewhere (a
/// crash put back on the track, a new race), not a speed.
constexpr float kTeleport = 150.0f;
/// An angle turning more than this in a frame jumped (a car turned round
/// by the game, a heading undefined at a standstill): no rate.
constexpr float kAngleJump = 0.5f;
/// A game moving its cars every other frame (Desert Tank) leaves them where
/// they were for a frame: what was worked out holds until they move, over at
/// most this many frames (more is a car standing still).
constexpr u64 kHoldFrames = 2;

float wrap(float a)
{
    a = std::fmod(a + kPi, 2 * kPi);
    return (a < 0 ? a + 2 * kPi : a) - kPi;
}

bool starts_with(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

/// Writes a frame as JSON, or lists the keys it knows, keeping the keys a
/// selection asks for: a key is written when a selected path is it, one of its
/// parents or one of its children.
class Writer {
public:
    Writer(std::string& out, const Selection& selection, std::set<std::string>* known = nullptr)
        : m_out(out), m_fields(selection.fields), m_known(known)
    {
        for (const std::string& f : m_fields) {
            if (f == "all") {
                m_fields.clear();
                break;
            }
        }
    }

    [[nodiscard]] bool wanted(const std::string& path) const
    {
        if (m_fields.empty()) {
            return true;
        }
        for (const std::string& f : m_fields) {
            if (f == path || starts_with(path, f + ".") || starts_with(f, path + ".")) {
                return true;
            }
        }
        return false;
    }

    void number(const std::string& prefix, const char* key, float v)
    {
        const std::string path = prefix + key;
        if (!wanted(path)) {
            return;
        }
        if (m_known != nullptr) {
            if (std::isfinite(v)) {
                m_known->insert(path);
            }
            return;
        }
        char buf[64];
        if (std::isfinite(v)) {
            std::snprintf(buf, sizeof(buf), "\"%s\":%.7g,", key, static_cast<double>(v));
        } else {
            std::snprintf(buf, sizeof(buf), "\"%s\":null,", key);
        }
        m_out += buf;
    }

    void integer(const std::string& prefix, const char* key, long long v)
    {
        const std::string path = prefix + key;
        if (!wanted(path)) {
            return;
        }
        if (m_known != nullptr) {
            if (v != kUnknown) {
                m_known->insert(path);
            }
            return;
        }
        char buf[64];
        if (v == kUnknown) {
            std::snprintf(buf, sizeof(buf), "\"%s\":null,", key);
        } else {
            std::snprintf(buf, sizeof(buf), "\"%s\":%lld,", key, v);
        }
        m_out += buf;
    }

    void boolean(const std::string& prefix, const char* key, bool v)
    {
        const std::string path = prefix + key;
        if (!wanted(path)) {
            return;
        }
        if (m_known != nullptr) {
            m_known->insert(path);
            return;
        }
        m_out += '"';
        m_out += key;
        m_out += v ? "\":true," : "\":false,";
    }

    void text(const std::string& prefix, const char* key, const std::string& v, bool known = true)
    {
        const std::string path = prefix + key;
        if (!wanted(path)) {
            return;
        }
        if (m_known != nullptr) {
            if (known) {
                m_known->insert(path);
            }
            return;
        }
        m_out += '"';
        m_out += key;
        m_out += "\":\"";
        for (const char c : v) {
            if (c == '"' || c == '\\') {
                m_out += '\\';
            }
            if (static_cast<unsigned char>(c) >= 0x20) {
                m_out += c;
            }
        }
        m_out += "\",";
    }

    void matrix(const std::string& prefix, const char* key, const std::array<float, 9>& m)
    {
        const std::string path = prefix + key;
        if (!wanted(path)) {
            return;
        }
        if (m_known != nullptr) {
            m_known->insert(path);
            return;
        }
        m_out += '"';
        m_out += key;
        m_out += "\":[";
        for (usize i = 0; i < m.size(); ++i) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), i + 1 < m.size() ? "%.7g," : "%.7g", static_cast<double>(m[i]));
            m_out += buf;
        }
        m_out += "],";
    }

    /// A value already in JSON (an array of numbers), always known.
    void raw(const std::string& prefix, const char* key, const std::string& json)
    {
        const std::string path = prefix + key;
        if (!wanted(path)) {
            return;
        }
        if (m_known != nullptr) {
            m_known->insert(path);
            return;
        }
        m_out += '"';
        m_out += key;
        m_out += "\":";
        m_out += json;
        m_out += ',';
    }

    /// Opens an object or array under key; false (nothing written) when no
    /// key below it is wanted.
    bool open(const std::string& path, const char* key, char bracket)
    {
        if (!wanted(path)) {
            return false;
        }
        if (m_known == nullptr) {
            if (key != nullptr) {
                m_out += '"';
                m_out += key;
                m_out += "\":";
            }
            m_out += bracket;
        }
        return true;
    }

    /// Closes it, the last comma replaced, then a comma for the next key.
    void close(char bracket, bool comma = true)
    {
        if (m_known != nullptr) {
            return;
        }
        if (m_out.back() == ',') {
            m_out.back() = bracket;
        } else {
            m_out += bracket;
        }
        if (comma) {
            m_out += ',';
        }
    }

    /// An empty object, holding an array's place for an element not wanted.
    void empty()
    {
        if (m_known == nullptr) {
            m_out += "{},";
        }
    }

    [[nodiscard]] bool listing() const { return m_known != nullptr; }

private:
    std::string&             m_out;
    std::vector<std::string> m_fields;
    std::set<std::string>*   m_known;
};

void wheel(Writer& w, const std::string& prefix, const Wheel& wh)
{
    w.number(prefix, "susp", wh.susp);
    w.integer(prefix, "contact", wh.contact);
    w.text(prefix, "surface", wh.surface.empty() ? "unknown" : wh.surface, !wh.surface.empty());
    w.number(prefix, "slip", wh.slip);
    w.number(prefix, "spin", wh.spin);
}

void vehicle(Writer& w, const std::string& prefix, const Vehicle& v)
{
    w.number(prefix, "x", v.x); w.number(prefix, "y", v.y); w.number(prefix, "z", v.z);
    w.number(prefix, "heading", v.heading);
    w.number(prefix, "pitch", v.pitch);
    w.number(prefix, "roll", v.roll);
    w.matrix(prefix, "rotation", v.rotation);
    w.number(prefix, "speed", v.speed);
    w.number(prefix, "vx", v.vx); w.number(prefix, "vy", v.vy); w.number(prefix, "vz", v.vz);
    w.number(prefix, "ax", v.ax); w.number(prefix, "ay", v.ay); w.number(prefix, "az", v.az);
    w.number(prefix, "along", v.along); w.number(prefix, "alat", v.alat); w.number(prefix, "avert", v.avert);
    w.number(prefix, "yaw_rate", v.yaw_rate);
    w.number(prefix, "pitch_rate", v.pitch_rate);
    w.number(prefix, "roll_rate", v.roll_rate);
    w.number(prefix, "ground", v.ground);
    if (v.airborne == kUnknown) {
        w.integer(prefix, "airborne", kUnknown);
    } else {
        w.boolean(prefix, "airborne", v.airborne != 0);
    }
    w.number(prefix, "impact", v.impact);
    const std::string wheels = prefix + "wheels";
    if (w.open(wheels, "wheels", '[')) {
        // All four, the ones not wanted empty, so that the indices hold.
        for (usize i = 0; i < v.wheels.size(); ++i) {
            const std::string path = wheels + "." + std::to_string(i);
            if (w.open(path, nullptr, '{')) {
                wheel(w, path + ".", v.wheels[i]);
                w.close('}');
            } else {
                w.empty();
            }
        }
        w.close(']');
    }
    w.integer(prefix, "gear", v.gear);
    w.integer(prefix, "gears", v.gears);
    w.number(prefix, "rpm", v.rpm);
    w.number(prefix, "rpm_max", v.rpm_max);
    w.number(prefix, "rpm_idle", v.rpm_idle);
    w.integer(prefix, "lap", v.lap);
    w.integer(prefix, "sector", v.sector);
    w.number(prefix, "progress", v.progress);
    w.integer(prefix, "position", v.position);
    w.number(prefix, "damage", v.damage);
    w.number(prefix, "gap_ahead", v.gap_ahead);
    w.number(prefix, "gap_behind", v.gap_behind);
    w.number(prefix, "dist_ahead", v.dist_ahead);
    w.number(prefix, "dist_behind", v.dist_behind);
    w.number(prefix, "throttle", v.throttle);
    w.number(prefix, "brake", v.brake);
    w.number(prefix, "clutch", v.clutch);
    w.number(prefix, "handbrake", v.handbrake);
    w.number(prefix, "steer", v.steer);
}

/// The other cars to send: all, or the N nearest along the track (in a
/// straight line when the track's length is unknown).
std::vector<const Vehicle*> nearest(const Frame& f, int count)
{
    std::vector<const Vehicle*> out;
    out.reserve(f.others.size());
    for (const Vehicle& v : f.others) {
        out.push_back(&v);
    }
    if (count < 0 || static_cast<usize>(count) >= out.size()) {
        return out;
    }
    const Vehicle& p     = f.player;
    const bool     track = std::isfinite(f.lap_length);
    const auto     away  = [&](const Vehicle* v) {
        if (track) {
            return std::fabs(v->progress - p.progress) * f.lap_length;
        }
        return std::hypot(v->x - p.x, v->z - p.z);
    };
    std::stable_sort(out.begin(), out.end(), [&](const Vehicle* a, const Vehicle* b) { return away(a) < away(b); });
    out.resize(static_cast<usize>(count));
    return out;
}

void write(Writer& w, const Frame& f, int others)
{
    const std::string root;
    w.integer(root, "frame", static_cast<long long>(f.frame));
    w.text(root, "game", f.game);
    w.text(root, "track", f.track, !f.track.empty());
    w.integer(root, "sectors", f.sectors);
    w.integer(root, "laps", f.laps);
    w.text(root, "transmission", f.transmission, !f.transmission.empty());
    w.boolean(root, "racing", f.racing);
    w.boolean(root, "finished", f.finished);
    w.text(root, "state", state_name(f.state), f.state != State::Unknown);
    w.number(root, "time_left", f.time_left);
    w.number(root, "lap_time", f.lap_time);
    w.number(root, "last_lap", f.last_lap);
    w.number(root, "best_lap", f.best_lap);
    w.number(root, "race_time", f.race_time);
    w.integer(root, "checkpoint", f.checkpoint);
    w.integer(root, "stage", f.stage);
    w.text(root, "car", f.car, !f.car.empty());
    w.text(root, "difficulty", f.difficulty, !f.difficulty.empty());
    w.matrix(root, "camera", f.camera);
    char inputs[96];
    std::snprintf(inputs, sizeof(inputs), "[%u,%u,%u,%u,%u,%u]", f.analog[0], f.analog[1], f.analog[2],
                  f.analog[3], f.in0, f.in1);
    w.raw(root, "inputs", inputs);
    if (w.open("player", "player", '{')) {
        vehicle(w, "player.", f.player);
        w.close('}');
    }
    if (w.open("others", "others", '[')) {
        for (const Vehicle* v : nearest(f, others)) {
            if (w.open("others", nullptr, '{')) {
                vehicle(w, "others.", *v);
                w.close('}');
            }
        }
        w.close(']');
    }
}

void put_f32(u8* p, float v)
{
    u32 bits = 0;
    std::memcpy(&bits, &v, 4);
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<u8>(bits >> (8 * i));
    }
}

void put_u32(u8* p, u32 v)
{
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<u8>(v >> (8 * i));
    }
}

float or_zero(float v)
{
    return std::isfinite(v) ? v : 0.0f;
}

u8 unit_byte(float v)
{
    return std::isfinite(v) ? static_cast<u8>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f) : 0;
}

}  // namespace

const char* state_name(State state)
{
    switch (state) {
        case State::Attract: return "attract";
        case State::Select:  return "select";
        case State::Race:    return "race";
        case State::Pause:   return "pause";
        case State::Over:    return "over";
        case State::Replay:  return "replay";
        case State::Unknown: break;
    }
    return "unknown";
}

void GameTelemetry::kinematics(Frame& out)
{
    const usize n = 1 + out.others.size();
    if (m_last.size() != n) {
        m_last.assign(n, Last{});
    }
    for (usize i = 0; i < n; ++i) {
        Vehicle& v = i == 0 ? out.player : out.others[i - 1];
        Last&    l = m_last[i];
        const bool still = l.valid && v.x == l.x && v.y == l.y && v.z == l.z && out.frame - l.frame <= kHoldFrames;
        if (l.valid && (out.frame == l.frame || still)) {
            v.vx = l.vx; v.vy = l.vy; v.vz = l.vz;
            v.ax = l.ax; v.ay = l.ay; v.az = l.az;
            continue;
        }
        if (l.valid && out.frame > l.frame && out.frame - l.frame < 30) {
            const float dt = static_cast<float>(out.frame - l.frame) / kFps;
            const float vx = (v.x - l.x) / dt, vy = (v.y - l.y) / dt, vz = (v.z - l.z) / dt;
            if (std::sqrt(vx * vx + vy * vy + vz * vz) < kTeleport) {
                v.vx = vx;
                v.vy = vy;
                v.vz = vz;
                v.ax = (v.vx - l.vx) / dt;
                v.ay = (v.vy - l.vy) / dt;
                v.az = (v.vz - l.vz) / dt;
            } else {
                v.vx = l.vx;  // a jump: the speed as it was, no acceleration
                v.vy = l.vy;
                v.vz = l.vz;
            }
        }
        l.x = v.x; l.y = v.y; l.z = v.z;
        l.vx = v.vx; l.vy = v.vy; l.vz = v.vz;
        l.ax = v.ax; l.ay = v.ay; l.az = v.az;
        l.frame = out.frame;
        l.valid = true;
    }
}

void GameTelemetry::orient(Vehicle& v)
{
    const float cy = std::cos(v.heading), sy = std::sin(v.heading);
    const float cp = std::cos(v.pitch), sp = std::sin(v.pitch);
    const float cr = std::cos(v.roll), sr = std::sin(v.roll);
    // Ry(heading) * Rx(-pitch) * Rz(-roll), in the car's right, up, forward:
    // forward (0, 0, 1) goes to (sin h cos p, sin p, cos h cos p), and a roll
    // to the right lowers the right side (1, 0, 0).
    v.rotation = {cy * cr + sy * sp * sr,  cy * sr - sy * sp * cr, sy * cp,
                  -cp * sr,                cp * cr,                sp,
                  -sy * cr + cy * sp * sr, -sy * sr - cy * sp * cr, cy * cp};
}

void GameTelemetry::set_controls(const rom::GameSpec& game)
{
    m_steer = m_throttle = m_brake = Control{};
    for (usize i = 0; i < game.analog.size() && i < Frame{}.analog.size(); ++i) {
        const rom::AnalogChannel& c = game.analog[i];
        const Control control{static_cast<int>(i), c.minimum, c.maximum, c.reverse};
        switch (c.control) {
            case rom::AnalogControl::Steer:
            case rom::AnalogControl::Handle:
            case rom::AnalogControl::Bank:
                if (m_steer.channel < 0) {
                    m_steer = control;
                }
                break;
            case rom::AnalogControl::Accel:
            case rom::AnalogControl::Throttle:
                if (m_throttle.channel < 0) {
                    m_throttle = control;
                }
                break;
            case rom::AnalogControl::Brake:
                if (m_brake.channel < 0) {
                    m_brake = control;
                }
                break;
            default:
                break;
        }
    }
}

bool GameTelemetry::update(const Memory& ram, u64 frame, std::span<const u8> analog, u8 in0, u8 in1, Frame& out)
{
    // Each frame from nothing: what a decoder leaves is unknown.
    out = Frame{};
    if (!decode(ram, frame, out)) {
        return false;
    }
    for (usize i = 0; i < out.analog.size(); ++i) {
        out.analog[i] = i < analog.size() ? analog[i] : 0;
    }
    out.in0 = in0;
    out.in1 = in1;
    derive(out);
    return true;
}

void GameTelemetry::derive(Frame& out)
{
    if (out.state == State::Unknown && out.racing) {
        out.state = State::Race;
    }

    const usize n = 1 + out.others.size();
    if (m_angles.size() != n) {
        m_angles.assign(n, Last{});
    }
    for (usize i = 0; i < n; ++i) {
        Vehicle&    v = i == 0 ? out.player : out.others[i - 1];
        const auto& r = v.rotation;
        // The car's axes in the world: forward and up are the matrix's last
        // two columns, right is up x forward (a heading grows to the right).
        const float fx = r[2], fy = r[5], fz = r[8];
        const float ux = r[1], uy = r[4], uz = r[7];
        const float rx = uy * fz - uz * fy, ry = uz * fx - ux * fz, rz = ux * fy - uy * fx;
        v.along = v.ax * fx + v.ay * fy + v.az * fz;
        v.alat  = v.ax * rx + v.ay * ry + v.az * rz;
        v.avert = v.ax * ux + v.ay * uy + v.az * uz;

        Last&      l     = m_angles[i];
        const bool still = l.valid && v.heading == l.heading && v.pitch == l.pitch && v.roll == l.roll
                        && out.frame - l.frame <= kHoldFrames && out.frame != l.frame;
        if (still) {
            v.yaw_rate   = std::isfinite(v.yaw_rate) ? v.yaw_rate : l.yaw_rate;
            v.pitch_rate = std::isfinite(v.pitch_rate) ? v.pitch_rate : l.pitch_rate;
            v.roll_rate  = std::isfinite(v.roll_rate) ? v.roll_rate : l.roll_rate;
            v.impact     = std::isfinite(v.impact) ? v.impact : 0.0f;
        } else if (l.valid && out.frame == l.frame) {
            v.yaw_rate   = std::isfinite(v.yaw_rate) ? v.yaw_rate : l.yaw_rate;
            v.pitch_rate = std::isfinite(v.pitch_rate) ? v.pitch_rate : l.pitch_rate;
            v.roll_rate  = std::isfinite(v.roll_rate) ? v.roll_rate : l.roll_rate;
            v.impact     = std::isfinite(v.impact) ? v.impact : l.impact;
        } else {
            if (l.valid && out.frame > l.frame && out.frame - l.frame < 30) {
                const float dt    = static_cast<float>(out.frame - l.frame) / kFps;
                const float steps = static_cast<float>(out.frame - l.frame);
                const auto  rate  = [dt, steps](float now, float before) {
                    const float d = wrap(now - before);
                    return std::fabs(d) < kAngleJump * steps ? d / dt : 0.0f;
                };
                if (!std::isfinite(v.yaw_rate)) {
                    v.yaw_rate = rate(v.heading, l.heading);
                }
                if (!std::isfinite(v.pitch_rate)) {
                    v.pitch_rate = rate(v.pitch, l.pitch);
                }
                if (!std::isfinite(v.roll_rate)) {
                    v.roll_rate = rate(v.roll, l.roll);
                }
                if (!std::isfinite(v.impact)) {
                    const float a  = std::sqrt(v.ax * v.ax + v.ay * v.ay + v.az * v.az);
                    const float dv = a * dt;
                    v.impact       = a > kImpactAccel && dv < kImpactMax ? dv : 0.0f;
                }
            }
            l.heading    = v.heading;
            l.pitch      = v.pitch;
            l.roll       = v.roll;
            l.yaw_rate   = v.yaw_rate;
            l.pitch_rate = v.pitch_rate;
            l.roll_rate  = v.roll_rate;
            l.impact     = v.impact;
            l.frame      = out.frame;
            l.valid      = true;
        }

        if (v.airborne == kUnknown && std::isfinite(v.ground)) {
            v.airborne = v.ground > kAirborne ? 1 : 0;
        }
    }

    // The player's controls, as the game read them.
    const auto fraction = [&out](const Control& c) {
        if (c.channel < 0) {
            return NAN;
        }
        const float span = static_cast<float>(std::max(1, c.maximum - c.minimum));
        float       f    = (static_cast<float>(out.analog[static_cast<usize>(c.channel)]) - c.minimum) / span;
        f                = std::clamp(c.reverse ? 1.0f - f : f, 0.0f, 1.0f);
        return f;
    };
    Vehicle& p = out.player;
    if (!std::isfinite(p.throttle)) {
        p.throttle = fraction(m_throttle);
    }
    if (!std::isfinite(p.brake)) {
        p.brake = fraction(m_brake);
    }
    if (!std::isfinite(p.steer)) {
        // The channel's low end is the wheel's left: +1 left.
        const float s = fraction(m_steer);
        p.steer       = std::isfinite(s) ? 1.0f - 2.0f * s : NAN;
    }

    // Along the track: each car's distance to the nearest ahead and behind.
    if (std::isfinite(out.lap_length) && out.lap_length > 0) {
        for (usize i = 0; i < n; ++i) {
            Vehicle& v      = i == 0 ? out.player : out.others[i - 1];
            float    ahead  = INFINITY;
            float    behind = INFINITY;
            for (usize j = 0; j < n; ++j) {
                if (j == i) {
                    continue;
                }
                const Vehicle& o = j == 0 ? out.player : out.others[j - 1];
                const float    d = (o.progress - v.progress) * out.lap_length;
                if (d >= 0 && d < ahead) {
                    ahead = d;
                } else if (d < 0 && -d < behind) {
                    behind = -d;
                }
            }
            const float speed = std::max(v.speed, 1.0f);
            if (std::isfinite(ahead)) {
                v.dist_ahead = ahead;
                v.gap_ahead  = ahead / speed;
            }
            if (std::isfinite(behind)) {
                v.dist_behind = behind;
                v.gap_behind  = behind / speed;
            }
        }
    }
}

std::unique_ptr<GameTelemetry> make_telemetry(const rom::GameSpec& game)
{
    const std::string& family = game.parent.empty() ? game.name : game.parent;
    std::unique_ptr<GameTelemetry> t;
    if (family == "indy500") {
        t = std::make_unique<Indy500Telemetry>();
    } else if (family == "stcc") {
        t = std::make_unique<StccTelemetry>();
    } else if (family == "srallyc") {
        t = std::make_unique<SrallyTelemetry>();
    } else if (family == "daytona") {
        t = std::make_unique<DaytonaTelemetry>();
    } else if (family == "sgt24h") {
        t = std::make_unique<Sgt24hTelemetry>();
    } else if (family == "overrev") {
        t = std::make_unique<OverRevTelemetry>();
    } else if (family == "motoraid") {
        t = std::make_unique<MotorRaidTelemetry>();
    } else if (family == "waverunr") {
        t = std::make_unique<WaveRunnerTelemetry>();
    } else if (family == "desert") {
        t = std::make_unique<DesertTankTelemetry>();
    }
    if (t) {
        t->set_controls(game);
    }
    return t;
}

std::string to_json(const Frame& f, const Selection& selection)
{
    std::string out;
    out.reserve(512 + 1100 * (1 + f.others.size()));
    Writer w(out, selection);
    out += '{';
    write(w, f, selection.others);
    w.close('}', false);
    out += '\n';
    return out;
}

std::string known_keys(const Frame& f)
{
    std::set<std::string> known;
    std::string           unused;
    Writer                w(unused, {}, &known);
    write(w, f, -1);
    std::string out = "[";
    for (const std::string& k : known) {
        if (out.size() > 1) {
            out += ',';
        }
        out += '"' + k + '"';
    }
    out += ']';
    return out;
}

std::array<u8, kForzaDashSize> forza_dash(const Frame& f)
{
    std::array<u8, kForzaDashSize> b{};
    const Vehicle& p = f.player;
    const auto&    r = p.rotation;
    // The car's axes: Forza's X right, Y up, Z forward.
    const float fx = r[2], fy = r[5], fz = r[8];
    const float ux = r[1], uy = r[4], uz = r[7];
    const float rx = uy * fz - uz * fy, ry = uz * fx - ux * fz, rz = ux * fy - uy * fx;
    usize       o  = 0;
    const auto  f32 = [&](float v) { put_f32(&b[o], or_zero(v)); o += 4; };
    const auto  u32v = [&](u32 v) { put_u32(&b[o], v); o += 4; };

    u32v(f.state == State::Race ? 1u : 0u);                                   // IsRaceOn
    u32v(static_cast<u32>(static_cast<double>(f.frame) * 1000.0 / kFps));    // TimestampMS
    f32(p.rpm_max); f32(p.rpm_idle); f32(p.rpm);
    f32(p.alat); f32(p.avert); f32(p.along);                                  // Acceleration
    f32(p.vx * rx + p.vy * ry + p.vz * rz);                                   // Velocity
    f32(p.vx * ux + p.vy * uy + p.vz * uz);
    f32(p.vx * fx + p.vy * fy + p.vz * fz);
    f32(p.pitch_rate); f32(p.yaw_rate); f32(p.roll_rate);                     // AngularVelocity
    f32(p.heading); f32(p.pitch); f32(p.roll);
    for (const Wheel& w : p.wheels) {                                         // NormalizedSuspensionTravel
        f32(std::isfinite(w.susp) ? std::clamp(0.5f + w.susp / 0.2f, 0.0f, 1.0f) : 0.0f);
    }
    for (const Wheel& w : p.wheels) { f32(w.slip); }                          // TireSlipRatio
    for (const Wheel& w : p.wheels) { f32(w.spin); }                          // WheelRotationSpeed
    o += 16;                                                                  // WheelOnRumbleStrip
    o += 16;                                                                  // WheelInPuddleDepth
    o += 16;                                                                  // SurfaceRumble
    o += 16;                                                                  // TireSlipAngle
    for (const Wheel& w : p.wheels) { f32(w.slip); }                          // TireCombinedSlip
    for (const Wheel& w : p.wheels) { f32(w.susp); }                          // SuspensionTravelMeters
    o += 20;                                                                  // CarOrdinal..NumCylinders
    f32(p.x); f32(p.y); f32(p.z);                                             // Position
    f32(p.speed);
    o += 8;                                                                   // Power, Torque
    o += 16;                                                                  // TireTemp
    f32(0.0f);                                                                // Boost
    f32(1.0f);                                                                // Fuel
    f32(std::isfinite(f.lap_length) ? p.progress * f.lap_length : 0.0f);      // DistanceTraveled
    f32(f.best_lap); f32(f.last_lap); f32(f.lap_time); f32(f.race_time);
    const u16 lap = static_cast<u16>(std::max(p.lap - 1, 0));
    b[o++]        = static_cast<u8>(lap);
    b[o++]        = static_cast<u8>(lap >> 8);
    b[o++]        = static_cast<u8>(std::clamp(p.position, 0, 255));
    b[o++]        = unit_byte(p.throttle);
    b[o++]        = unit_byte(p.brake);
    b[o++]        = unit_byte(p.clutch);
    b[o++]        = unit_byte(p.handbrake);
    // Gear: 0 reverse, 1..10, 11 neutral (and unknown).
    b[o++] = p.gear == -1 ? 0 : p.gear >= 1 && p.gear <= 10 ? static_cast<u8>(p.gear) : 11;
    // Steer: -127 full left.
    b[o++] = static_cast<u8>(static_cast<s8>(std::isfinite(p.steer)
                                                 ? std::clamp(-p.steer * 127.0f, -127.0f, 127.0f)
                                                 : 0.0f));
    o += 2;                                                                   // NormalizedDrivingLine, AIBrakeDifference
    return b;
}

bool Server::open(u16 port)
{
    m_started = net::startup();
    if (!m_started || !m_server.open(port)) {
        SM2_WARN("telemetry: cannot listen on TCP port %u: %s", port, m_server.last_error().c_str());
        return false;
    }
    SM2_INFO("telemetry: serving on TCP port %u", port);
    return true;
}

bool Server::set_forza(const std::string& host_port)
{
    const usize colon = host_port.rfind(':');
    if (colon == std::string::npos || colon == 0) {
        return false;
    }
    const unsigned long port = std::strtoul(host_port.c_str() + colon + 1, nullptr, 10);
    if (port == 0 || port > 65535) {
        return false;
    }
    if (!m_started) {
        m_started = net::startup();
    }
    if (!m_udp.valid() && !m_udp.open("", 0)) {
        SM2_WARN("telemetry: no UDP socket for Forza Data Out: %s", m_udp.last_error().c_str());
        return false;
    }
    m_forza_host = host_port.substr(0, colon);
    if (m_forza_host == "localhost") {
        m_forza_host = "127.0.0.1";
    }
    m_forza_port = static_cast<u16>(port);
    SM2_INFO("telemetry: Forza Data Out to %s:%u", m_forza_host.c_str(), m_forza_port);
    return true;
}

void Server::stop_forza()
{
    m_forza_port = 0;
}

void Server::publish(const Frame& frame)
{
    m_last      = frame;
    m_have_last = true;
    if (m_forza_port != 0) {
        const auto packet = forza_dash(frame);
        static_cast<void>(m_udp.send_to(packet, m_forza_host, m_forza_port));
    }
    if (m_server.client_count() == 0) {
        return;
    }
    // Clients asking for the same thing share one line.
    std::map<std::string, std::string> lines;
    for (auto& [id, client] : m_clients) {
        if (client.every > 1 && frame.frame % client.every != 0) {
            continue;
        }
        std::string key = std::to_string(client.selection.others);
        for (const std::string& f : client.selection.fields) {
            key += '|' + f;
        }
        auto it = lines.find(key);
        if (it == lines.end()) {
            it = lines.emplace(key, to_json(frame, client.selection)).first;
        }
        m_server.send(id, it->second);
    }
}

void Server::poll()
{
    m_server.poll([this](u64 id) { m_clients[id] = Client{}; });
    static_cast<void>(handle_lines());
}

usize Server::handle_lines()
{
    usize acks = 0;
    for (const net::TcpServer::Line& line : m_server.take_lines()) {
        std::string text = line.text;
        while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) {
            text.pop_back();
        }
        if (text.empty() || text == "go") {  // "go": the autopilot's older acknowledgement
            ++acks;
            continue;
        }
        m_server.send(line.client, command(line.client, text) + "\n");
    }
    // Forget the clients that left.
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        it = m_server.has_client(it->first) ? std::next(it) : m_clients.erase(it);
    }
    return acks;
}

std::string Server::command(u64 id, const std::string& line)
{
    Client&     client = m_clients[id];
    const usize space  = line.find(' ');
    const std::string verb = line.substr(0, space);
    std::string arg = space == std::string::npos ? "" : line.substr(space + 1);
    while (!arg.empty() && arg.front() == ' ') {
        arg.erase(arg.begin());
    }
    if (verb == "fields") {
        client.selection.fields.clear();
        if (arg != "all" && !arg.empty()) {
            usize start = 0;
            while (start <= arg.size()) {
                const usize comma = arg.find(',', start);
                std::string f     = arg.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!f.empty()) {
                    client.selection.fields.push_back(f);
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
        }
        return R"({"ok":"fields"})";
    }
    if (verb == "others") {
        if (arg == "all") {
            client.selection.others = -1;
            return R"({"ok":"others"})";
        }
        char*      end = nullptr;
        const long n   = std::strtol(arg.c_str(), &end, 10);
        if (arg.empty() || *end != '\0' || n < 0) {
            return R"({"error":"others N|all"})";
        }
        client.selection.others = static_cast<int>(n);
        return R"({"ok":"others"})";
    }
    if (verb == "every") {
        char*               end = nullptr;
        const unsigned long n   = std::strtoul(arg.c_str(), &end, 10);
        if (arg.empty() || *end != '\0' || n == 0) {
            return R"({"error":"every N, N at least 1"})";
        }
        client.every = static_cast<u32>(n);
        return R"({"ok":"every"})";
    }
    if (verb == "udp") {
        if (arg == "off") {
            stop_forza();
            return R"({"ok":"udp"})";
        }
        if (starts_with(arg, "forza ") && set_forza(arg.substr(6))) {
            return R"({"ok":"udp"})";
        }
        return R"({"error":"udp forza HOST:PORT|off"})";
    }
    if (verb == "list") {
        return std::string(R"({"ok":"list","keys":)") + (m_have_last ? known_keys(m_last) : "[]") + "}";
    }
    return R"({"error":"unknown command: fields, others, every, udp, list"})";
}

bool Server::wait_client(int timeout_ms)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (m_server.client_count() == 0 && std::chrono::steady_clock::now() < until) {
        poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return m_server.client_count() != 0;
}

bool Server::wait_ack(int timeout_ms)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        m_server.poll([this](u64 id) { m_clients[id] = Client{}; });
        if (handle_lines() != 0) {
            return true;
        }
        if (m_server.client_count() == 0 || std::chrono::steady_clock::now() >= until) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

}  // namespace sm2::telemetry
