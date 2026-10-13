"""Optimise a racing line and a throttle profile with CMA-ES on clean laps.

usage: python line_opt.py STATE MAP OUT_DIR [--p0 0.3] [--knots 12] [--pop 12] [--gens 120] [--init PARAMS.json]

The driver (LineDriver): a pure pursuit of the map's line moved sideways by
an offset, and a throttle, both interpolated between KNOTS points evenly
spread along the lap; plus the pursuit's gain, look-ahead and steering limit.
A candidate's score: the time of the lap from progress P0 to P0 + 1, from
STATE (a state with no traffic ahead: indy_oval_solo.state). Each generation's
candidates race side by side, one emulator each. OUT_DIR gets log.csv and
best.json (the best parameters so far)."""
import argparse
import csv
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import indy500_telemetry as T  # noqa: E402
import telemetry_any as TA  # noqa: E402
from pursuit import Line, wrap  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

HERE = Path(__file__).parent
MAX_OFFSET = 7.0
STEP = 4


class LineDriver:
    """Parameters, all in -1..1 (CMA-ES's space): K offsets, K throttles, then
    gain, look-ahead, look-ahead per m/s, steering limit."""

    def __init__(self, line, x, knots):
        x = np.clip(np.asarray(x, dtype=np.float64), -1, 1)
        self.line, self.k = line, knots
        self.off = x[:knots] * MAX_OFFSET
        self.lift = x[knots:2 * knots]                  # <= 0 full gas; 0..0.7 lifts; past 0.7 brakes
        g, look, lpm, smax = x[2 * knots:2 * knots + 4]
        self.gain = 3.0 + 1.5 * g                       # 1.5..4.5
        self.look = 15.0 + 8.0 * look                   # 7..23 m
        self.lpm = 0.35 + 0.2 * lpm                     # 0.15..0.55 s
        self.smax = 0.55 + 0.35 * smax                  # 0.2..0.9

    def _interp(self, values, s):
        u = (s / self.line.length) * self.k
        i = int(np.floor(u)) % self.k
        f = u - np.floor(u)
        return values[i] * (1 - f) + values[(i + 1) % self.k] * f

    def offset_at(self, s):
        """The line's offset (metres, + right) at arc length s."""
        return self._interp(self.off, s % self.line.length)

    def __call__(self, t, extra=0.0, thr=None):
        """extra: metres added to the line's offset (a learnt policy's move);
        thr, -1..1: under 0 lifts the throttle in proportion, under -0.7 brakes."""
        p, line = t.player, self.line
        i = line.nearest(p.x, p.z)
        look = self.look + self.lpm * p.speed
        s_target = (line.s[i] + look) % line.length
        j = int(np.searchsorted(line.s, s_target)) % len(line.p)
        a, b = line.p[j], line.p[(j + 1) % len(line.p)]
        d = (b - a) / (np.linalg.norm(b - a) + 1e-9)
        normal = np.array([-d[1], d[0]])               # the right of the line is - normal (see Line.lateral)
        tx, tz = line.p[j] - normal * (self._interp(self.off, s_target) + extra)
        alpha = wrap(np.arctan2(tx - p.x, tz - p.z) - t.heading)
        steer = float(np.clip(self.gain * alpha, -self.smax, self.smax))
        u = self._interp(self.lift, line.s[i])
        accel = float(np.clip(1 - u / 0.7, 0, 1))
        brake = float(np.clip((u - 0.7) / 0.3, 0, 1))
        if thr is not None and thr < 0:
            accel *= max(0.0, 1 + thr)
            brake = max(brake, float(np.clip((-thr - 0.7) / 0.3, 0, 1)))
        return steer, accel, brake


def evaluate(agents, line, xs, knots, state, p0, limit=75.0):
    """Lap times (from P0 to P0 + 1) of the candidates, side by side; a lap not
    done counts 100 s plus the part left."""
    drivers = [LineDriver(line, x, knots) for x in xs]
    for a in agents[:len(xs)]:
        assert a.load(state)
    t0 = [None] * len(xs)
    score = [None] * len(xs)
    prog = [0.0] * len(xs)
    live = list(range(len(xs)))
    start = agents[0].frame
    while live:
        tels = {j: TA.read(agents[j]) for j in live}
        for j in live:
            agents[j].step_send(STEP, *drivers[j](tels[j]))
        for j in list(live):
            agents[j].step_wait()
            t = tels[j]
            prog[j] = t.player.progress
            if t0[j] is None and prog[j] >= p0:
                t0[j] = agents[j].frame
            if t0[j] is not None and prog[j] >= p0 + 1:
                score[j] = (agents[j].frame - t0[j]) / T.FPS
                live.remove(j)
            elif t.time_left <= 0 or (agents[j].frame - start) / T.FPS > limit:
                score[j] = 100.0 + (p0 + 1 - prog[j]) * 50
                live.remove(j)
    return score


def main():
    import cma
    ap = argparse.ArgumentParser()
    ap.add_argument("state"); ap.add_argument("map"); ap.add_argument("out")
    ap.add_argument("--p0", type=float, default=0.3)
    ap.add_argument("--knots", type=int, default=12)
    ap.add_argument("--pop", type=int, default=12)
    ap.add_argument("--gens", type=int, default=120)
    ap.add_argument("--sigma", type=float, default=0.1)
    ap.add_argument("--init")
    ap.add_argument("--game", default="indy500")
    args = ap.parse_args()
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    line = Line(HERE / "runs" / args.map)
    state = str((HERE / "states" / args.state).resolve())
    n = 2 * args.knots + 4
    x0 = np.zeros(n)
    x0[args.knots:2 * args.knots] = -0.2                # full throttle everywhere
    if args.init:
        x0 = np.array(json.loads(Path(args.init).read_text())["x"])
    agents = [Sm2Agent(args.game, nvram=HERE / "states" / "nvram") for _ in range(args.pop)]
    base = evaluate(agents, line, [x0], args.knots, state, args.p0)[0]
    print("start: lap %.2f s" % base, flush=True)
    es = cma.CMAEvolutionStrategy(x0, args.sigma, {"popsize": args.pop, "bounds": [-1, 1], "seed": 1, "verbose": -9})
    best = (base, x0.tolist())
    logf = open(out / "log.csv", "a", newline="")
    log = csv.writer(logf)
    for g in range(args.gens):
        xs = es.ask()
        scores = evaluate(agents, line, xs, args.knots, state, args.p0)
        es.tell(xs, scores)
        k = int(np.argmin(scores))
        if scores[k] < best[0]:
            best = (scores[k], list(map(float, xs[k])))
            (out / "best.json").write_text(json.dumps({"lap": best[0], "x": best[1], "knots": args.knots,
                                                        "map": args.map, "state": args.state, "p0": args.p0}))
        log.writerow([g, round(min(scores), 3), round(float(np.median(scores)), 3), round(best[0], 3), round(es.sigma, 4)])
        logf.flush()
        print(f"gen {g}: best {min(scores):.2f}  median {np.median(scores):.2f}  best ever {best[0]:.2f}  sigma {es.sigma:.3f}", flush=True)
    for a in agents:
        a.close()


if __name__ == "__main__":
    main()
