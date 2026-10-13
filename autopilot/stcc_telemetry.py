"""Sega Touring Car Championship's telemetry, decoded from the Model 2C's
scratch RAM (00200000: the game keeps its race there, not in the work RAM).

  0020A900  8 cars of 0x58 bytes, the player first:
            +00 x, +04 y, +08 z (floats)
            +0C, +10, +14 velocity (floats, units a frame)
            +1C heading (float, radians, -atan2(dx, dz))
            +50 the track segment the car is on (u16, 0 at the line)
            +52 segments driven since the line (i16, negative on the grid)
  002022EC  time left (u32, frames; the screen shows it / 64)
  0020462C  the gear (u8, 1 first; also 00208934)"""
from dataclasses import dataclass, field

import numpy as np

SCRATCH = 0x00200000
CARS, CAR_SIZE, CAR_COUNT = 0x0020A900, 0x58, 8
TIME_LEFT = 0x002022EC
GEAR = 0x0020462C
FPS = 57.5
SEGMENTS = {}                 # segments a lap by course, from track maps (stcc_map.py)
WINDOW = (TIME_LEFT - SCRATCH) & ~0xFF, CARS + CAR_SIZE * CAR_COUNT - ((TIME_LEFT - SCRATCH) & ~0xFF) - SCRATCH


@dataclass
class Car:
    x: float
    y: float
    z: float
    speed: float              # units/s (about metres)
    lap: int
    segment: int
    progress: float           # laps, fractional
    heading: float = 0.0      # radians, atan2(dx, dz) of the nose


@dataclass
class Telemetry:
    frame: int
    time_left: float
    heading: float
    player: Car
    others: list = field(default_factory=list)
    position: int = 0
    course: int = 0           # Country Circuit (Round 1) only, for now
    laps: int = 2             # Round 1's
    gear: int = 0             # 1 first; 0 unknown
    manual: bool = False      # set by the caller that chose the manual gearbox


def decode(buf: bytes, frame: int = 0, base: int = SCRATCH, segments: int = 300) -> Telemetry:
    """buf: memory from `base` (the scratch RAM's start, or WINDOW's)."""
    b = np.frombuffer(buf, dtype=np.uint8)
    o = CARS - base
    s = b[o:o + CAR_SIZE * CAR_COUNT].reshape(CAR_COUNT, CAR_SIZE)
    f = s.view("<f4"); u16 = s.view("<u2"); i16 = s.view("<i2")
    cars = []
    for k in range(CAR_COUNT):
        driven = int(i16[k, 0x52 // 2])
        lap = driven // segments + 1
        speed = float(np.sqrt(f[k, 3] ** 2 + f[k, 4] ** 2 + f[k, 5] ** 2)) * FPS
        cars.append(Car(float(f[k, 0]), float(f[k, 1]), float(f[k, 2]), speed, lap,
                        int(u16[k, 0x50 // 2]), driven / segments, -float(f[k, 7])))
    t = TIME_LEFT - base
    time_left = int(b[t:t + 4].view("<u4")[0]) / FPS
    player = cars[0]
    position = 1 + sum(1 for c in cars[1:] if c.progress > player.progress)
    g = GEAR - base
    gear = int(b[g]) if 0 <= g < len(b) else 0
    return Telemetry(frame, time_left, player.heading, player, cars[1:], position, gear=gear)


def read(agent) -> Telemetry:
    off, length = WINDOW
    return decode(agent.mem(SCRATCH + off, length), agent.frame, SCRATCH + off)


def from_json(d: dict) -> Telemetry:
    """The same, from sm2-emu's telemetry server (src/telemetry/stcc.cpp)."""
    def car(v):
        return Car(v["x"], v["y"], v["z"], v["speed"], v["lap"], v["sector"], v["progress"], v["heading"])
    p = d["player"]
    return Telemetry(d["frame"], d["time_left"] or 0.0, p["heading"], car(p), [car(o) for o in d["others"]],
                     p["position"])
