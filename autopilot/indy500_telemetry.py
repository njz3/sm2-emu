"""Indy 500's telemetry, decoded from the work RAM (see AUTOPILOT.md for how
each field was found)."""
from dataclasses import dataclass, field

import numpy as np

WORK_RAM = 0x00500000
CARS, CAR_SIZE, CAR_COUNT = 0x0053C7AC, 0x1E4, 33
TIME_LEFT = 0x0053C332        # u16, frames
COURSE = 0x0053C350           # u8: 0 Bay Side Street, 1 the oval (Indy 500), 2 Highland Raceway
GEARBOX = 0x0053C5B9          # u8: 0 the 6-speed manual, 1 automatic
GEAR = 0x0053C5BC             # u8: 0 first
CAMERA = 0x005449A0           # camera to world: 3x3 rotation, then x, y, z
FPS = 57.5
ANGLE = 2 * np.pi / 65536     # the cars' angles (+0x0C roll, +0x10 yaw, +0x14 pitch): i32, a turn is 65536
COURSES = {0: "Bay Side Street", 1: "Indy 500", 2: "Highland Raceway"}
LAPS = {0: 3, 1: 4, 2: 3}     # the race's laps, from the lap counter on screen
CARS_RACED = {0: 28, 1: 33, 2: 28}


@dataclass
class Car:
    x: float
    y: float
    z: float
    speed: float              # m/s
    lap: int                  # 1 on the first lap
    sector: int
    sector_fraction: float
    progress: float           # laps done, fractional: (lap - 1) + position in the lap
    heading: float = 0.0      # radians, the body's yaw, as atan2(dx, dz) of its nose
    pitch: float = 0.0        # [guess] radians
    roll: float = 0.0         # radians, + leaning right: the oval's banked left turns read < 0


@dataclass
class Telemetry:
    frame: int
    time_left: float          # seconds
    heading: float            # radians, the player's body yaw, atan2(dx, dz) of its nose
    rotation: np.ndarray      # the camera's 3x3 (a chase camera: lags the car in the turns)
    player: Car
    others: list = field(default_factory=list)
    position: int = 0         # 1 = leading
    course: int = 1           # see COURSES
    gear: int = 0             # 1 first; 0 unknown
    manual: bool = False      # the 6-speed manual gearbox

    @property
    def laps(self):
        """The race's laps."""
        return LAPS.get(self.course, 4)


# Where each sector starts along the lap (a fraction of its length), from the
# maps (track_map.py): the oval's last sector is short.
SECTOR_STARTS = {
    0: [0.0, 0.131, 0.264, 0.396, 0.528, 0.661, 0.795, 0.927],
    1: [0.0, 0.16, 0.321, 0.482, 0.643, 0.802, 0.963],
    2: [0.0, 0.141, 0.281, 0.423, 0.564, 0.704, 0.843, 0.985],
}


def decode(ram: bytes, frame: int = 0, sectors: int = 7) -> Telemetry:
    buf = np.frombuffer(ram, dtype=np.uint8)
    base = CARS - WORK_RAM
    s = buf[base:base + CAR_SIZE * CAR_COUNT].reshape(CAR_COUNT, CAR_SIZE)
    f, u, i = s.view("<f4"), s.view("<u4"), s.view("<i4")
    course = int(buf[COURSE - WORK_RAM])
    starts = SECTOR_STARTS.get(course)
    cars = []
    for k in range(CAR_COUNT):
        e4 = int(u[k, 0xE4 // 4])
        hi, lo = e4 >> 16, e4 & 0xFFFF
        lap, sector = hi // 16, hi % 16
        frac = lo / 65536.0
        if starts is not None and sector < len(starts):
            # the sectors' lengths differ: the lap's fraction by distance
            end = starts[sector + 1] if sector + 1 < len(starts) else 1.0
            in_lap = starts[sector] + frac * (end - starts[sector])
        else:
            in_lap = (sector + frac) / sectors
        yaw = -int(i[k, 0x10 // 4]) * ANGLE
        cars.append(Car(float(f[k, 0]), float(f[k, 1]), float(f[k, 2]), float(f[k, 0x30 // 4]) * FPS,
                        lap, sector, frac, (lap - 1) + in_lap,
                        float((yaw + np.pi) % (2 * np.pi) - np.pi), int(i[k, 0x14 // 4]) * ANGLE,
                        int(i[k, 0x0C // 4]) * ANGLE))
    rot = buf[CAMERA - WORK_RAM:CAMERA - WORK_RAM + 36].view("<f4").reshape(3, 3).astype(np.float64)
    time_left = int(buf[TIME_LEFT - WORK_RAM:TIME_LEFT - WORK_RAM + 2].view("<u2")[0]) / FPS
    cars = cars[:CARS_RACED.get(course, CAR_COUNT)]
    player = cars[0]
    position = 1 + sum(1 for c in cars[1:] if c.progress > player.progress)
    return Telemetry(frame, time_left, player.heading, rot, player, cars[1:], position, course,
                     int(buf[GEAR - WORK_RAM]) + 1, int(buf[GEARBOX - WORK_RAM]) == 0)


def from_json(d: dict, sectors: int = 7) -> Telemetry:
    """The same, from sm2-emu's telemetry server (src/telemetry)."""
    def car(v):
        return Car(v["x"], v["y"], v["z"], v["speed"], v["lap"], v["sector"], 0.0, v["progress"],
                   v["heading"], v.get("pitch") or 0.0, v.get("roll") or 0.0)
    p = d["player"]
    rot = np.array(d.get("camera") or p["rotation"], dtype=np.float64).reshape(3, 3)
    course = {v: k for k, v in COURSES.items()}.get(d.get("track"), 1)
    return Telemetry(d["frame"], d["time_left"] or 0.0, p["heading"], rot, car(p),
                     [car(o) for o in d["others"]], p["position"], course)


# The part of the work RAM decode() reads: the race's variables, the cars, the camera.
WINDOW = (TIME_LEFT - WORK_RAM) & ~0xFF, (CAMERA + 48 - WORK_RAM) - ((TIME_LEFT - WORK_RAM) & ~0xFF)


def read(agent, sectors: int = 7) -> Telemetry:
    """decode() from an Sm2Agent, reading only WINDOW (34 KB, not the whole 1 MB)."""
    off, length = WINDOW
    buf = bytearray(off + length)
    buf[off:] = agent.ram(off, length)
    return decode(bytes(buf), agent.frame, sectors)
