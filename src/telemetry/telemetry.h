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
//
// Telemetry: what a racing game knows about the cars and the race, read out of
// its work RAM and served to TCP clients (a motion simulator, an autopilot, a
// dashboard), one JSON line a frame. Each game has its own decoder, a class
// derived from GameTelemetry; make_telemetry() picks it by the ROM set.
#pragma once

#include "core/net.h"
#include "core/types.h"

#include <array>
#include <cmath>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sm2::rom {
struct GameSpec;
}

namespace sm2::telemetry {

/// One car. Units: metres, seconds, radians; a NaN or -1 is unknown.
struct Vehicle {
    float x = 0, y = 0, z = 0;                 ///< World position, y up.
    float heading = 0;                         ///< Yaw: atan2(dx, dz) of the car's nose.
    float pitch   = 0;                         ///< Radians, + nose up.
    float roll    = 0;                         ///< Radians, + leaning right.
    /// World from car, row-major: yaw about y, then pitch about the car's x,
    /// then roll about its z (forward).
    std::array<float, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
    float speed = 0;                           ///< m/s, the game's own.
    float vx = 0, vy = 0, vz = 0;              ///< m/s, from successive positions.
    float ax = 0, ay = 0, az = 0;              ///< m/s^2, the same.
    float ground = NAN;                        ///< Height above the ground.
    int   gear   = -1;                         ///< 1, 2, ...; -1 unknown.
    float rpm    = NAN;
    int   lap    = 0;                          ///< 1 on the first lap.
    int   sector = 0;
    float progress = 0;                        ///< Laps driven, fractional.
    int   position = 0;                        ///< In the race, 1 leading.
};

struct Frame {
    u64         frame = 0;
    std::string game;
    std::string track;
    int         sectors   = 0;
    int         laps      = 0;                 ///< The race's length; 0 unknown.
    std::string transmission;                  ///< "AT", "MT" or empty.
    bool        racing    = false;
    bool        finished  = false;             ///< The player has done the race's laps.
    float       time_left = NAN;               ///< Seconds before the time is up.
    float       lap_time  = NAN;               ///< Seconds into the current lap.
    Vehicle              player;
    std::vector<Vehicle> others;
    /// The player's camera, world from camera, row-major (a chase camera lags
    /// the car in the turns); identity when unknown.
    std::array<float, 9> camera{1, 0, 0, 0, 1, 0, 0, 0, 1};

    /// The controls the game read in the last frame run, as it got them: the
    /// first analogue channels (steering, accelerator, brake...) and the IN0
    /// and IN1 ports. Filled by the caller, not the decoder.
    std::array<u8, 4> analog{};
    u8                in0 = 0xff, in1 = 0xff;
};

/// The host CPU's RAMs a decoder reads.
struct Memory {
    std::span<const u8> work;     ///< 0x00500000, 1 MB
    std::span<const u8> scratch;  ///< 0x00200000, 128 or 256 KB
};

class GameTelemetry {
public:
    virtual ~GameTelemetry() = default;
    [[nodiscard]] virtual const char* name() const = 0;

    /// Read one frame from the host CPU's RAMs. False when the game is in no
    /// state to say anything.
    virtual bool decode(const Memory& ram, u64 frame, Frame& out) = 0;

protected:
    /// Velocities and accelerations from the positions of frame to frame.
    void kinematics(Frame& out);

    /// Vehicle::rotation from its heading, pitch and roll.
    static void orient(Vehicle& v);

private:
    struct Last {
        float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
        u64   frame = 0;
        bool  valid = false;
    };
    std::vector<Last> m_last;
};

/// The decoder for this ROM set, or nullptr for a game nobody has decoded.
std::unique_ptr<GameTelemetry> make_telemetry(const rom::GameSpec& game);

/// One frame as a JSON object, on one line.
std::string to_json(const Frame& frame);

/// Serves frames to every connected client, one JSON line each.
class Server {
public:
    bool open(u16 port);
    void publish(const Frame& frame);
    void poll();

    /// Sends what is queued and waits, at most timeout_ms, for a client's line:
    /// the --telemetry-sync handshake (the client has set its controls).
    bool wait_ack(int timeout_ms);

    /// Waits, at most timeout_ms, for a client to connect (--telemetry-sync:
    /// no frame runs before the driver is there).
    bool wait_client(int timeout_ms);
    [[nodiscard]] bool valid() const { return m_server.valid(); }
    [[nodiscard]] usize clients() const { return m_server.client_count(); }

private:
    net::TcpServer m_server;
    bool           m_started = false;
};

}  // namespace sm2::telemetry
