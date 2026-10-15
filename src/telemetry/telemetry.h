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
// its RAMs and served to TCP clients (a motion simulator, an autopilot, a
// dashboard), one JSON line a frame, and as Forza Motorsport 7's "Data Out"
// datagrams. Each game has its own decoder, a class derived from
// GameTelemetry; make_telemetry() picks it by the ROM set. The keys are those
// Supermodel serves for the Model 3 games (TELEMETRY.md).
#pragma once

#include "core/net.h"
#include "core/types.h"

#include <array>
#include <climits>
#include <cmath>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sm2::rom {
struct GameSpec;
}

namespace sm2::telemetry {

/// An integer the game does not say: null in the JSON.
inline constexpr int kUnknown = INT_MIN;

/// One wheel: front left, front right, rear left, rear right.
struct Wheel {
    float       susp    = NAN;       ///< Suspension travel, m, > 0 compressed.
    int         contact = kUnknown;  ///< 1 on the ground.
    std::string surface;             ///< "asphalt", "dirt"...; empty unknown.
    float       slip    = NAN;       ///< 0 gripping, 1 sliding.
    float       spin    = NAN;       ///< rad/s.
};

/// One car. Units: metres, seconds, radians; a NaN or kUnknown is unknown.
/// The car's frame: forward, right and up as the driver sees them.
struct Vehicle {
    float x = 0, y = 0, z = 0;                 ///< World position, y up.
    float heading = 0;                         ///< Yaw: atan2(dx, dz) of the car's nose; + turning right.
    float pitch   = 0;                         ///< Radians, + nose up.
    float roll    = 0;                         ///< Radians, + leaning right.
    /// World from car, row-major: yaw about y, then pitch about the car's x,
    /// then roll about its z (forward).
    std::array<float, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
    float speed = 0;                           ///< m/s, the game's own.
    float vx = 0, vy = 0, vz = 0;              ///< m/s, from successive positions.
    float ax = 0, ay = 0, az = 0;              ///< m/s^2, the same.
    /// m/s^2 in the car's frame: speeding up, towards its right, up.
    float along = NAN, alat = NAN, avert = NAN;
    /// rad/s: turning right, nose rising, rolling right.
    float yaw_rate = NAN, pitch_rate = NAN, roll_rate = NAN;
    float ground   = NAN;                      ///< Height above the ground.
    int   airborne = kUnknown;
    float impact   = NAN;                      ///< A collision's delta-v this frame, m/s; else 0.
    std::array<Wheel, 4> wheels{};
    int   gear  = kUnknown;                    ///< -1 reverse, 0 neutral, 1, 2...
    int   gears = kUnknown;                    ///< Forward gears.
    float rpm = NAN, rpm_max = NAN, rpm_idle = NAN;  ///< rpm_max: the red line.
    int   lap    = 0;                          ///< 1 on the first lap.
    int   sector = 0;
    float progress = 0;                        ///< Laps driven, fractional.
    int   position = 0;                        ///< In the race, 1 leading.
    float damage   = NAN;                      ///< 0..1.
    /// To the nearest car ahead and behind: seconds, metres along the track.
    float gap_ahead = NAN, gap_behind = NAN, dist_ahead = NAN, dist_behind = NAN;
    /// The controls as the game reads them: pedals 0..1, steer -1..1, +1 left.
    float throttle = NAN, brake = NAN, clutch = NAN, handbrake = NAN, steer = NAN;
};

/// What the game is doing. A motion platform parks unless it is Race.
enum class State : u8 { Unknown, Attract, Select, Race, Pause, Over, Replay };

[[nodiscard]] const char* state_name(State state);

struct Frame {
    u64         frame = 0;
    std::string game;
    std::string track;
    int         sectors   = 0;
    int         laps      = 0;                 ///< The race's length; 0 unknown.
    std::string transmission;                  ///< "AT", "MT" or empty.
    bool        racing    = false;
    bool        finished  = false;             ///< The player has done the race's laps.
    State       state     = State::Unknown;    ///< Racing gives Race when the game says no more.
    /// Clocks as the game shows them (its seconds can be longer than real ones).
    float       time_left = NAN;               ///< Seconds before the time is up.
    float       lap_time  = NAN;               ///< Seconds into the current lap.
    float       last_lap  = NAN, best_lap = NAN, race_time = NAN;
    int         checkpoint = kUnknown;
    int         stage      = kUnknown;
    std::string car, difficulty;
    /// Metres a lap, for the distances between the cars; NaN unknown. Not sent.
    float       lap_length = NAN;
    Vehicle              player;
    std::vector<Vehicle> others;
    /// The player's camera, world from camera, row-major (a chase camera lags
    /// the car in the turns); identity when unknown.
    std::array<float, 9> camera{1, 0, 0, 0, 1, 0, 0, 0, 1};

    /// The controls the game read in the last frame run, as it got them: the
    /// first analogue channels (steering, accelerator, brake...) and the IN0
    /// and IN1 ports. Filled by update(), not the decoder.
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

    /// One frame: the game's decode(), the controls the game read (`analog`,
    /// `in0`, `in1`), then what is computed from them all (the car's frame,
    /// the rates, the impacts, the gaps). False when the game is in no state
    /// to say anything.
    bool update(const Memory& ram, u64 frame, std::span<const u8> analog, u8 in0, u8 in1, Frame& out);

    /// Where the game's controls are, for throttle, brake and steer.
    void set_controls(const rom::GameSpec& game);

protected:
    /// Read one frame from the host CPU's RAMs.
    virtual bool decode(const Memory& ram, u64 frame, Frame& out) = 0;

    /// Velocities and accelerations from the positions of frame to frame.
    void kinematics(Frame& out);

    /// Vehicle::rotation from its heading, pitch and roll.
    static void orient(Vehicle& v);

private:
    void derive(Frame& out);

    /// A car's last frame, and what was worked out from it (given again when
    /// the same frame is asked for twice).
    struct Last {
        float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, ax = 0, ay = 0, az = 0;
        float heading = 0, pitch = 0, roll = 0;
        float yaw_rate = NAN, pitch_rate = NAN, roll_rate = NAN, impact = NAN;
        u64   frame = 0;
        bool  valid = false;
    };
    std::vector<Last> m_last;
    std::vector<Last> m_angles;

    struct Control {
        int  channel = -1;
        u8   minimum = 0, maximum = 0xff;
        bool reverse = false;
    };
    Control m_steer, m_throttle, m_brake;
};

/// The decoder for this ROM set, or nullptr for a game nobody has decoded.
std::unique_ptr<GameTelemetry> make_telemetry(const rom::GameSpec& game);

/// What a client asks for: the keys (dotted paths, "player.speed",
/// "others.x", "player.wheels.0.susp"; empty for all) and the other cars (the
/// nearest along the track, -1 all).
struct Selection {
    std::vector<std::string> fields;
    int                      others = -1;
};

/// One frame as a JSON object, on one line.
std::string to_json(const Frame& frame, const Selection& selection = {});

/// The keys this frame fills (its values known), as a JSON array.
std::string known_keys(const Frame& frame);

/// One frame as Forza Motorsport 7's "Car Dash" datagram (311 bytes).
inline constexpr usize kForzaDashSize = 311;
std::array<u8, kForzaDashSize> forza_dash(const Frame& frame);

/// Serves frames to every connected client, one JSON line each, and to a
/// Forza Data Out listener. A client's lines: an empty one (or "go")
/// acknowledges a frame (--telemetry-sync), the others are commands, each
/// answered with one JSON line:
///   fields all | fields PATH,PATH...   the keys sent to this client
///   others N | others all              the other cars, the nearest first
///   every N                            one frame in N
///   udp forza HOST:PORT | udp off      the Forza datagrams, for every client
///   list                               the keys the game fills
class Server {
public:
    bool open(u16 port);

    /// Forza Data Out to HOST:PORT; false if it does not parse.
    bool set_forza(const std::string& host_port);
    void stop_forza();

    void publish(const Frame& frame);
    void poll();

    /// Sends what is queued and waits, at most timeout_ms, for a client's
    /// empty line: the --telemetry-sync handshake (the client has set its
    /// controls). Commands meanwhile are answered.
    bool wait_ack(int timeout_ms);

    /// Waits, at most timeout_ms, for a client to connect (--telemetry-sync:
    /// no frame runs before the driver is there).
    bool wait_client(int timeout_ms);

    /// Anyone listening: a TCP client or the Forza datagrams.
    [[nodiscard]] bool wanted() const { return m_server.client_count() != 0 || m_forza_port != 0; }
    [[nodiscard]] bool valid() const { return m_server.valid(); }
    [[nodiscard]] usize clients() const { return m_server.client_count(); }

private:
    struct Client {
        Selection selection;
        u32       every = 1;
    };

    /// Answers the commands received; the number of acknowledgements.
    usize handle_lines();
    std::string command(u64 client, const std::string& line);

    net::TcpServer         m_server;
    net::UdpSocket         m_udp;
    std::string            m_forza_host;
    u16                    m_forza_port = 0;
    std::map<u64, Client>  m_clients;
    Frame                  m_last;
    bool                   m_have_last = false;
    bool                   m_started   = false;
};

}  // namespace sm2::telemetry
