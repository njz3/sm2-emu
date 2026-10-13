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
#include "telemetry/indy500.h"
#include "telemetry/srally.h"
#include "telemetry/stcc.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace sm2::telemetry {
namespace {

constexpr float kFps = 57.5f;

void number(std::string& out, const char* key, float v)
{
    char buf[64];
    if (std::isfinite(v)) {
        std::snprintf(buf, sizeof(buf), "\"%s\":%.7g,", key, static_cast<double>(v));
    } else {
        std::snprintf(buf, sizeof(buf), "\"%s\":null,", key);
    }
    out += buf;
}

void integer(std::string& out, const char* key, long long v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "\"%s\":%lld,", key, v);
    out += buf;
}

void matrix(std::string& out, const char* key, const std::array<float, 9>& m)
{
    out += '"';
    out += key;
    out += "\":[";
    for (usize i = 0; i < m.size(); ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), i + 1 < m.size() ? "%.7g," : "%.7g", static_cast<double>(m[i]));
        out += buf;
    }
    out += "],";
}

void vehicle(std::string& out, const Vehicle& v)
{
    out += '{';
    number(out, "x", v.x); number(out, "y", v.y); number(out, "z", v.z);
    number(out, "heading", v.heading);
    number(out, "pitch", v.pitch);
    number(out, "roll", v.roll);
    matrix(out, "rotation", v.rotation);
    number(out, "speed", v.speed);
    number(out, "vx", v.vx); number(out, "vy", v.vy); number(out, "vz", v.vz);
    number(out, "ax", v.ax); number(out, "ay", v.ay); number(out, "az", v.az);
    number(out, "ground", v.ground);
    integer(out, "gear", v.gear);
    number(out, "rpm", v.rpm);
    integer(out, "lap", v.lap);
    integer(out, "sector", v.sector);
    number(out, "progress", v.progress);
    integer(out, "position", v.position);
    out.back() = '}';
}

}  // namespace

void GameTelemetry::kinematics(Frame& out)
{
    const usize n = 1 + out.others.size();
    if (m_last.size() != n) {
        m_last.assign(n, Last{});
    }
    for (usize i = 0; i < n; ++i) {
        Vehicle& v = i == 0 ? out.player : out.others[i - 1];
        Last&    l = m_last[i];
        if (l.valid && out.frame > l.frame && out.frame - l.frame < 30) {
            const float dt = static_cast<float>(out.frame - l.frame) / kFps;
            v.vx           = (v.x - l.x) / dt;
            v.vy           = (v.y - l.y) / dt;
            v.vz           = (v.z - l.z) / dt;
            v.ax           = (v.vx - l.vx) / dt;
            v.ay           = (v.vy - l.vy) / dt;
            v.az           = (v.vz - l.vz) / dt;
        }
        l = {v.x, v.y, v.z, v.vx, v.vy, v.vz, out.frame, true};
    }
}

void GameTelemetry::orient(Vehicle& v)
{
    const float cy = std::cos(v.heading), sy = std::sin(v.heading);
    const float cp = std::cos(v.pitch), sp = std::sin(v.pitch);
    const float cr = std::cos(v.roll), sr = std::sin(v.roll);
    // Ry(heading) * Rx(-pitch) * Rz(roll): forward (0, 0, 1) goes to
    // (sin h cos p, sin p, cos h cos p).
    v.rotation = {cy * cr - sy * sp * sr,  -cy * sr - sy * sp * cr, sy * cp,
                  cp * sr,                 cp * cr,                 sp,
                  -sy * cr - cy * sp * sr, sy * sr - cy * sp * cr,  cy * cp};
}

std::unique_ptr<GameTelemetry> make_telemetry(const rom::GameSpec& game)
{
    const std::string& family = game.parent.empty() ? game.name : game.parent;
    if (family == "indy500") {
        return std::make_unique<Indy500Telemetry>();
    }
    if (family == "stcc") {
        return std::make_unique<StccTelemetry>();
    }
    if (family == "srallyc") {
        return std::make_unique<SrallyTelemetry>();
    }
    if (family == "daytona") {
        return std::make_unique<DaytonaTelemetry>();
    }
    return nullptr;
}

std::string to_json(const Frame& f)
{
    std::string out;
    out.reserve(256 + 600 * (1 + f.others.size()));
    out += '{';
    integer(out, "frame", static_cast<long long>(f.frame));
    out += "\"game\":\"" + f.game + "\",\"track\":\"" + f.track + "\",";
    integer(out, "sectors", f.sectors);
    integer(out, "laps", f.laps);
    out += "\"transmission\":\"" + f.transmission + "\",";
    out += f.racing ? "\"racing\":true," : "\"racing\":false,";
    out += f.finished ? "\"finished\":true," : "\"finished\":false,";
    number(out, "time_left", f.time_left);
    number(out, "lap_time", f.lap_time);
    matrix(out, "camera", f.camera);
    char inputs[96];
    std::snprintf(inputs, sizeof(inputs), "\"inputs\":[%u,%u,%u,%u,%u,%u],", f.analog[0], f.analog[1],
                  f.analog[2], f.analog[3], f.in0, f.in1);
    out += inputs;
    out += "\"player\":";
    vehicle(out, f.player);
    out += ",\"others\":[";
    for (usize i = 0; i < f.others.size(); ++i) {
        if (i != 0) {
            out += ',';
        }
        vehicle(out, f.others[i]);
    }
    out += "]}\n";
    return out;
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

void Server::publish(const Frame& frame)
{
    if (m_server.client_count() != 0) {
        m_server.broadcast(to_json(frame));
    }
}

void Server::poll()
{
    m_server.poll([](u64) {});
}

bool Server::wait_client(int timeout_ms)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (m_server.client_count() == 0 && std::chrono::steady_clock::now() < until) {
        m_server.poll([](u64) {});
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return m_server.client_count() != 0;
}

bool Server::wait_ack(int timeout_ms)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        m_server.poll([](u64) {});
        if (m_server.take_lines() != 0) {
            return true;
        }
        if (m_server.client_count() == 0 || std::chrono::steady_clock::now() >= until) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

}  // namespace sm2::telemetry
