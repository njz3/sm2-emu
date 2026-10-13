"""Indy 500's work RAM, as found so far (see AUTOPILOT.md).

Addresses are i960 addresses; the work RAM starts at 0x00500000."""
import numpy as np

WORK_RAM = 0x00500000
CARS = 0x0053C7AC          # the car array: the player first, then the 32 others
CAR_SIZE = 0x1E4
CAR_COUNT = 33

# Offsets in a car's structure.
X, Y, Z = 0x000, 0x004, 0x008      # float, world units
RPM_RAW = 0x010                    # u32, [guess] the engine's revs
SPEED = 0x030                      # float, world units a frame
DISTANCE = 0x040                   # u32, grows with the distance driven


def cars(ram):
    """Per car: x, y, z, speed (units/frame), distance, rpm_raw. `ram` is the
    whole work RAM (bytes) or a slice of it starting at CARS."""
    buf = np.frombuffer(ram, dtype=np.uint8)
    if len(buf) > CAR_SIZE * CAR_COUNT:
        buf = buf[CARS - WORK_RAM:CARS - WORK_RAM + CAR_SIZE * CAR_COUNT]
    s = buf.reshape(CAR_COUNT, CAR_SIZE)
    f = s.view("<f4")
    u = s.view("<u4")
    return {
        "x": f[:, X // 4].astype(np.float64),
        "y": f[:, Y // 4].astype(np.float64),
        "z": f[:, Z // 4].astype(np.float64),
        "speed": f[:, SPEED // 4].astype(np.float64),
        "distance": u[:, DISTANCE // 4].astype(np.int64),
        "rpm_raw": u[:, RPM_RAW // 4].astype(np.int64),
    }
