"""Run a driver in agent mode from a save state and report the race.

usage: python run_driver.py STATE MAP SECONDS [pursuit gain look look_per_mps]"""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import indy500_telemetry as T  # noqa: E402
from pursuit import Line, Pursuit  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

state, mappath, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
params = [float(v) for v in sys.argv[4:7]] if len(sys.argv) > 6 else []
line = Line(mappath)
drv = Pursuit(line, *params) if params else Pursuit(line)
a = Sm2Agent("indy500", nvram=Path(__file__).parent / "states" / "nvram")
assert a.load(str(Path(state).resolve()))
STEP = 2
last_lap, lap_start, log = None, 0, []
for i in range(int(seconds * T.FPS / STEP)):
    t = T.decode(a.ram(0, 0x100000), a.frame)
    steer, accel, brake = drv(t)
    a.step(STEP, steer, accel, brake)
    p = t.player
    if last_lap is None:
        last_lap, lap_start = p.lap, a.frame
    elif p.lap != last_lap:
        log.append(f"lap {last_lap} done in {(a.frame - lap_start) / T.FPS:.2f} s, position {t.position}")
        last_lap, lap_start = p.lap, a.frame
    if i % int(10 * T.FPS / STEP) == 0:
        lat, _, _ = line.lateral(p.x, p.z)
        print(f"t={a.frame / T.FPS:6.1f} s  lap {p.lap} prog {p.progress:6.3f}  speed {p.speed:5.1f} m/s  "
              f"off-line {lat:+5.1f} m  pos {t.position:2d}/33  time left {t.time_left:5.1f}")
        if t.time_left <= 0:
            pass
a.close()
print("\n".join(log))
