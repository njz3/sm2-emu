"""Save states all along a race, for the training's episodes to start from:
a pursuit with some noise on its line drives from the start, a state every
few seconds.

usage: python make_pool.py START_STATE MAP OUT_DIR [--races 3] [--every 5]"""
import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import indy500_telemetry as T  # noqa: E402
import telemetry_any as TA  # noqa: E402
from pursuit import Line, Pursuit, wrap  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("start"); ap.add_argument("map"); ap.add_argument("out")
ap.add_argument("--races", type=int, default=3)
ap.add_argument("--every", type=float, default=5.0)
args = ap.parse_args()
out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
line = Line(args.map)
STEP = 4
agents = [Sm2Agent("indy500", nvram=Path(__file__).parent / "states" / "nvram") for _ in range(args.races)]
for a in agents:
    assert a.load(str(Path(args.start).resolve()))
rng = np.random.default_rng(0)
drivers = [Pursuit(line, gain=3.0) for _ in agents]
offset = np.zeros(len(agents))
done = [False] * len(agents)
k = 0
while not all(done):
    tels = [TA.read(a) for a in agents]
    for j, (a, d, t) in enumerate(zip(agents, drivers, tels)):
        if done[j]:
            continue
        # a slow random walk of the line's offset, -5..5 m: other traffic, other states
        offset[j] = np.clip(offset[j] + rng.normal(0, 0.4), -5, 5)
        p = t.player
        i = line.nearest(p.x, p.z)
        tx, tz = line.ahead(i, d.look + d.look_per_mps * p.speed)
        j2 = line.nearest(tx, tz)
        dvec = line.p[(j2 + 1) % len(line.p)] - line.p[j2]
        dvec /= np.linalg.norm(dvec) + 1e-9
        tx, tz = np.array([tx, tz]) - np.array([-dvec[1], dvec[0]]) * offset[j]
        steer = float(np.clip(3.0 * wrap(np.arctan2(tx - p.x, tz - p.z) - t.heading), -1, 1))
        a.step_send(STEP, steer, 1.0, 0.0)
    for j, (a, t) in enumerate(zip(agents, tels)):
        if done[j]:
            continue
        a.step_wait()
        if k % int(args.every * T.FPS / STEP) == 0 and t.player.lap >= 1 and t.time_left > 3:
            name = out / f"r{j}_{t.player.progress:05.2f}.state"
            a.save(str(name.resolve()))
        if t.time_left <= 0 or t.player.lap > t.laps:
            done[j] = True
            print(f"race {j}: over at progress {t.player.progress:.2f}, position {t.position}", flush=True)
    k += 1
for a in agents:
    a.close()
print(len(list(out.glob("*.state"))), "states in", out)
