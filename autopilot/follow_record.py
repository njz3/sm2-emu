"""Drive the player's car by following the trajectory of the cars ahead, in agent
mode, and record the work RAM, to find the rest of the game's variables and to
map the track.

usage: python follow_record.py STATE SECONDS OUT.npz
The path: the positions the other cars drove through (their racing line), kept
as they go; the car steers towards the point of that path some distance ahead
of it (pure pursuit), full throttle."""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import indy500_ram as R  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

state, seconds, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
a = Sm2Agent("indy500", nvram=Path(__file__).parent / "states" / "nvram")
assert a.load(str(Path(state).resolve()))
path = []          # racing line points (x, z), from the other cars
snaps, ctl = [], []
prev = None
steer = 0.0
frames = int(seconds * 57.5)
step = 2
for i in range(0, frames, step):
    ram = a.ram(0, 0x100000)
    c = R.cars(ram)
    px, pz = c["x"][0], c["z"][0]
    for k in range(1, R.CAR_COUNT):
        path.append((c["x"][k], c["z"][k]))
    if len(path) > 40000:
        path = path[-40000:]
    if prev is not None and np.hypot(px - prev[0], pz - prev[1]) > 0.05:
        heading = np.arctan2(px - prev[0], pz - prev[1])
        P = np.array(path)
        rel = P - (px, pz)
        dist = np.hypot(rel[:, 0], rel[:, 1])
        ang = np.arctan2(rel[:, 0], rel[:, 1]) - heading
        ang = (ang + np.pi) % (2 * np.pi) - np.pi
        look = 25 + 12 * c["speed"][0]
        sel = (np.abs(dist - look) < 6) & (np.abs(ang) < 1.0)
        if sel.any():
            err = float(np.median(ang[sel]))
            steer = float(np.clip(-2.0 * err, -1, 1))  # sign to check: + is right
    prev = (px, pz)
    a.step(step, steer, 1.0, 0.0)
    if i % 10 == 0:
        snaps.append(np.frombuffer(ram, dtype=np.uint8).copy())
        ctl.append((steer, 1.0, 0.0))
a.close()
np.savez_compressed(out, ram=np.stack(snaps), controls=np.array(ctl))
print("saved", out, len(snaps))
