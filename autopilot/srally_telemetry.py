"""Sega Rally Championship's telemetry, decoded from its work RAM (Model 2A).

  00500000  16 entries of 0x100 bytes: the player's first, then the AI cars
            (seven slots, the cars near the player; the rest still)
            +14 x, +18 y, +1C z (floats)
            player: +3C heading (float, radians, atan2(-dx, dz) of the travel);
                    +84 progress along the course (16.16, a segment every 4.8 units)
            AI:     +A4 heading (float, radians, atan2(dz, dx));
                    +88 progress along the course (16.16, a segment every 10.3 units)
  005229E6  seconds left (BCD byte)
The position and the laps are not found yet; speed from successive
positions."""
from dataclasses import dataclass, field

import numpy as np

WORK_RAM = 0x00500000
CARS, CAR_SIZE, CAR_COUNT = 0x00500000, 0x100, 8
FPS = 57.5
WINDOW = (0, CAR_SIZE * CAR_COUNT)
CLOCK = 0x005229E6


@dataclass
class Car:
    x: float
    y: float
    z: float
    speed: float = 0.0        # units/s, from the last position (0 on the first read)
    lap: int = 1
    progress: float = 0.0     # the course's segments (not laps: their count is unknown)
    heading: float = 0.0      # radians, atan2(dx, dz) of the travel


@dataclass
class Telemetry:
    frame: int
    time_left: float
    heading: float
    player: Car
    others: list = field(default_factory=list)
    position: int = 0
    course: int = 0
    laps: int = 99            # unknown yet: a race never "finishes" here


_last = {}


def decode(buf: bytes, frame: int = 0) -> Telemetry:
    b = np.frombuffer(buf, dtype=np.uint8)
    s = b[:CAR_SIZE * CAR_COUNT].reshape(CAR_COUNT, CAR_SIZE)
    f, u = s.view("<f4"), s.view("<u4")
    cars = []
    for k in range(CAR_COUNT):
        x, y, z = float(f[k, 0x14 // 4]), float(f[k, 0x18 // 4]), float(f[k, 0x1C // 4])
        if k == 0:
            heading = float(f[k, 0x3C // 4])                  # atan2(-dx, dz): the sign flipped below
            heading = -heading
            prog = int(u[k, 0x84 // 4]) / 65536.0
        else:
            a = float(f[k, 0xA4 // 4])                       # atan2(dz, dx)
            heading = float(np.arctan2(np.cos(a), np.sin(a)))  # to atan2(dx, dz)
            prog = int(u[k, 0x88 // 4]) / 65536.0
        speed = 0.0
        last = _last.get(k)
        if last is not None and 0 < frame - last[3] <= 30:
            speed = float(np.hypot(x - last[0], z - last[2])) * FPS / (frame - last[3])
        _last[k] = (x, y, z, frame)
        cars.append(Car(x, y, z, speed, 1, prog, heading))
    player = cars[0]
    return Telemetry(frame, 99.0, player.heading, player, cars[1:], 0)


def read(agent) -> Telemetry:
    off, length = WINDOW
    t = decode(agent.ram(off, length), agent.frame)
    c = agent.ram(CLOCK - WORK_RAM, 1)[0]
    t.time_left = float((c >> 4) * 10 + (c & 15))
    return t


def from_json(d: dict) -> Telemetry:
    """The same, from sm2-emu's telemetry server (src/telemetry/srally.cpp)."""
    def car(v):
        return Car(v["x"], v["y"], v["z"], v["speed"], v["lap"], v["progress"], v["heading"])
    p = d["player"]
    return Telemetry(d["frame"], 99.0, p["heading"], car(p), [car(o) for o in d["others"]], 0)
