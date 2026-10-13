"""Daytona USA's telemetry, decoded from its work RAM (Model 2).

  00505100  40 entries of 0x300 bytes: the player's first, then the AI cars
            +9C x, +A0 y, +A4 z (floats; the four wheels from +178)
            +C8 heading (i32, a turn is 65536, negated: heading = -v x 2pi/65536,
                atan2(dx, dz) of the nose)
            +EC the place in the race, from 0
            +F0 laps done
            +1AC progress along the course (16.16, a segment every 8.3 units;
                 196 a lap of the beginner course, back to 0 at the line)
The clock is not found yet; speed from successive positions."""
from dataclasses import dataclass, field

import numpy as np

WORK_RAM = 0x00500000
CARS, CAR_SIZE, CAR_COUNT = 0x00505100, 0x300, 40
FPS = 57.5
SEGMENTS = 196               # a lap of the beginner course
WINDOW = (CARS - WORK_RAM, CAR_SIZE * CAR_COUNT)


@dataclass
class Car:
    x: float
    y: float
    z: float
    speed: float = 0.0
    lap: int = 1
    progress: float = 0.0     # laps, fractional (the beginner course's 196 segments a lap)
    heading: float = 0.0


@dataclass
class Telemetry:
    frame: int
    time_left: float
    heading: float
    player: Car
    others: list = field(default_factory=list)
    position: int = 0
    course: int = 0
    laps: int = 8             # the beginner course's race


_last = {}


def decode(buf: bytes, frame: int = 0) -> Telemetry:
    s = np.frombuffer(buf, dtype=np.uint8)[:CAR_SIZE * CAR_COUNT].reshape(CAR_COUNT, CAR_SIZE)
    f, i = s.view("<f4"), s.view("<i4")
    cars = []
    for k in range(CAR_COUNT):
        x, y, z = float(f[k, 0x9C // 4]), float(f[k, 0xA0 // 4]), float(f[k, 0xA4 // 4])
        heading = float((-int(i[k, 0xC8 // 4]) * 2 * np.pi / 65536 + np.pi) % (2 * np.pi) - np.pi)
        speed = 0.0
        last = _last.get(k)
        if last is not None and 0 < frame - last[2] <= 30:
            speed = float(np.hypot(x - last[0], z - last[1])) * FPS / (frame - last[2])
        _last[k] = (x, z, frame)
        done = int(s[k, 0xF0])
        cars.append(Car(x, y, z, speed, done + 1, done + int(i[k, 0x1AC // 4]) / 65536.0 / SEGMENTS, heading))
    return Telemetry(frame, 99.0, cars[0].heading, cars[0], cars[1:], int(s[0, 0xEC]) + 1)


def read(agent) -> Telemetry:
    off, length = WINDOW
    return decode(agent.ram(off, length), agent.frame)


def from_json(d: dict) -> Telemetry:
    """The same, from sm2-emu's telemetry server (src/telemetry/daytona.cpp)."""
    def car(v):
        return Car(v["x"], v["y"], v["z"], v["speed"], v["lap"], v["progress"], v["heading"])
    p = d["player"]
    return Telemetry(d["frame"], 99.0, p["heading"], car(p), [car(o) for o in d["others"]], p["position"])
