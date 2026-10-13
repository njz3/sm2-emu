"""Record every car's position and lap counter every 2 frames during a race, to
map the track (track_map.py).

usage: python record_laps.py STATE SECONDS OUT.npz [MAP.npz]
The player's car drives meanwhile, to keep the race (and its clock) going: a
pursuit of MAP's line when there is one, else of the path the other cars
drove (no map yet). OUT holds `xz` (T x 33 x 2), `e4` (T x 33, the lap word:
lap << 20 | sector << 16 | fraction), `time_left` and `course` (T)."""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import indy500_telemetry as T  # noqa: E402
from pursuit import Line, Pursuit, wrap  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

COURSE = 0x0053C350 - T.WORK_RAM   # u8: 0 Bay Side Street, 1 the oval, 2 Highland Raceway
STEP = 2

state, seconds, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
drv = Pursuit(Line(sys.argv[4]), gain=3.0) if len(sys.argv) > 4 else None
a = Sm2Agent("indy500", nvram=Path(__file__).parent / "states" / "nvram")
assert a.load(str(Path(state).resolve()))
xz, e4, tl, course = [], [], [], []
trail = []          # the other cars' recent positions: the path to follow without a map
steer = 0.0
for _ in range(int(seconds * T.FPS / STEP)):
    ram = a.ram(0, 0x100000)
    buf = np.frombuffer(ram, dtype=np.uint8)
    s = buf[T.CARS - T.WORK_RAM:T.CARS - T.WORK_RAM + T.CAR_SIZE * T.CAR_COUNT].reshape(T.CAR_COUNT, T.CAR_SIZE)
    f, u = s.view("<f4"), s.view("<u4")
    xz.append(np.column_stack([f[:, 0], f[:, 2]]).astype(np.float32))
    e4.append(u[:, 0xE4 // 4].copy())
    t = T.decode(ram, a.frame)
    tl.append(t.time_left)
    course.append(int(buf[COURSE]))
    if drv is not None:
        steer = drv(t)[0]
    else:
        p = t.player
        trail.extend(xz[-1][1:].tolist())
        trail = trail[-20000:]
        P = np.array(trail) - (p.x, p.z)
        dist = np.hypot(P[:, 0], P[:, 1])
        ang = np.array([wrap(v) for v in np.arctan2(P[:, 0], P[:, 1]) - t.heading])
        look = 15 + 0.4 * p.speed
        sel = (np.abs(dist - look) < 6) & (np.abs(ang) < 1.0)
        if sel.any():
            steer = float(np.clip(2.0 * np.median(ang[sel]), -0.45, 0.45))
    a.step(STEP, steer, 1.0, 0.0)
a.close()
np.savez_compressed(out, xz=np.stack(xz), e4=np.stack(e4), time_left=np.array(tl), course=np.array(course))
print("saved", out, len(xz), "samples")
