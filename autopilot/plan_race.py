"""Search a race with the emulator itself: from the current state, each of K
emulators drives one candidate (where to put the car across the track, and
the throttle) for a few seconds; the best one, by distance, places and hard
impacts at the horizon, is kept for its first COMMIT seconds; and again from
there, to the flag. The emulator is deterministic and a state loads in a few
milliseconds, so this is a look-ahead with the true game, AI cars included.

The race found is a demonstration: every decision's observation (the same
features as the PPO policy's, and optionally the picture), action and the
controls it gave (steering, accelerator, brake) are saved, for the networks
to learn from (bc_train.py).

usage: python plan_race.py START MAP OUT_DIR [--params BEST.json] [--horizon 6]
                           [--commit 1] [--images] [--seconds S]"""
import argparse
import json
import shutil
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import indy500_telemetry as T  # noqa: E402
import telemetry_any as TA  # noqa: E402
from env_indy import FRAMES, IMPACTS, MAX_OFFSET, Pilot  # noqa: E402
from pursuit import Line  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

HERE = Path(__file__).parent


class Follow:
    """Behind the nearest car ahead (its slipstream), then, after `after`
    seconds, `side` (-1..1 of 7 m) beside it: to pass."""

    def __init__(self, side=0.0, after=2.5):
        self.side, self.after = side, after

    def __call__(self, t, pilot, k):
        line, p = pilot.line, t.player
        lat, _, i = line.lateral(p.x, p.z)
        best = None
        for c in t.others:
            if c.lap < 1:
                continue
            clat, _, ci = line.lateral(c.x, c.z)
            ds = (line.s[ci] - line.s[i]) % line.length
            if 5 < ds < 200 and (best is None or ds < best[0]):
                best = (ds, clat)
        if best is None:
            return (pilot.offset, 1.0)                     # nobody ahead: hold the line
        target = (best[1] - pilot.base.offset_at(line.s[i])) / MAX_OFFSET
        if k * FRAMES / T.FPS >= self.after:
            target += self.side
        return (float(np.clip(target, -1, 1)), 1.0)

    def __repr__(self):
        return f"follow{self.side:+.2f}@{self.after:g}"


# (offset of the line, -1..1 for -7..7 m; throttle, 1 base, below 0 lifts), or
# a callable (telemetry, pilot, step) -> that pair
CANDIDATES = [(-0.85, 1), (-0.6, 1), (-0.4, 1), (-0.2, 1), (0.0, 1), (0.2, 1), (0.4, 1), (0.6, 1),
              (-0.3, -0.5), (0.2, -0.5), Follow(0.0), Follow(-0.5), Follow(0.5)]
# To pass a file of cars: in the slipstream, then out of it sooner or later, either side.
CANDIDATES_PASS = [(-0.85, 1), (-0.5, 1), (-0.2, 1), (0.1, 1), (0.4, 1), (0.7, 1),
                   (-0.3, -0.5), Follow(0.0), Follow(-0.55, 1.0), Follow(0.55, 1.0), Follow(-0.55, 2.5),
                   Follow(0.55, 2.5), Follow(-0.8, 4.0)]


def over(t):
    return t.player.lap > t.laps or t.time_left <= 0


def run_all(agents, pilots, actions, seconds, records=None, image=None):
    """Each emulator holds its action `seconds`, all side by side; for each,
    the telemetry at the end, the distance, places and hard impacts made."""
    n = len(agents)
    tels = [TA.read(a) for a in agents]
    start = [(t.player.progress, t.position) for t in tels]
    impacts = [0] * n
    live = [not over(t) for t in tels]
    for k in range(int(round(seconds * T.FPS / FRAMES))):
        if not any(live):
            break
        for j in range(n):
            if not live[j]:
                continue
            t = tels[j]
            vec = pilots[j].features(t)            # also its direction of travel, as in training
            action = actions[j](t, pilots[j], k) if callable(actions[j]) else actions[j]
            ctl = pilots[j].control(t, action)
            if records is not None:
                obs = {"vec": vec, "act": np.asarray(action, dtype=np.float32),
                       "ctl": np.asarray(ctl, dtype=np.float32)}
                if image:
                    obs["img"] = agents[j].obs(*image).mean(axis=2).astype(np.uint8)
                records[j].append(obs)
            agents[j].step_send(FRAMES, *ctl)
        for j in range(n):
            if not live[j]:
                continue
            impacts[j] += sum(1 for b in agents[j].step_wait() if b in IMPACTS)
            tels[j] = TA.read(agents[j])
            live[j] = not over(tels[j])
    return [(tels[j], tels[j].player.progress - start[j][0], start[j][1] - tels[j].position, impacts[j])
            for j in range(n)]


POSITION_M = 25.0                                  # a place is worth this many metres


def score(t, dprog, dpos, impacts, length):
    laps = t.laps
    s = dprog * length + POSITION_M * dpos - 15.0 * impacts
    if t.player.lap > laps:
        s += 10000.0 - 300.0 * t.position          # finished: the place counts most (then the distance)
    elif t.time_left <= 0:
        s -= 10000.0
    return s


def copy_retry(src, dst, tries=50):
    """A copy that waits out a file held a moment (an antivirus, an emulator closing it)."""
    for k in range(tries):
        try:
            shutil.copyfile(src, dst)
            return
        except PermissionError:
            time.sleep(0.1 * (k + 1))
    shutil.copyfile(src, dst)


def save(out, demo, qdata, log, image, t, finished):
    """The demonstration, the candidates' scores and the log so far (again at each call)."""
    if demo:
        np.savez_compressed(out / "demo.npz", vec=np.stack([d["vec"] for d in demo]),
                            act=np.stack([d["act"] for d in demo]), ctl=np.stack([d["ctl"] for d in demo]),
                            **({"img": np.stack([d["img"] for d in demo])} if image else {}))
    if qdata:
        np.savez_compressed(out / "qdata.npz", vec=np.stack([q["vec"] for q in qdata]),
                            scores=np.stack([q["scores"] for q in qdata]),
                            **({"img": np.stack([q["img"] for q in qdata])} if image else {}))
    (out / "plan.json").write_text(json.dumps({"finished": finished, "position": t.position, "log": log}))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("start"); ap.add_argument("map"); ap.add_argument("out")
    ap.add_argument("--params", default=str(HERE / "runs" / "opt_oval" / "best_gen30.json"))
    ap.add_argument("--horizon", type=float, default=6.0)
    ap.add_argument("--commit", type=float, default=1.0)
    ap.add_argument("--images", action="store_true")
    ap.add_argument("--game", default="indy500")
    ap.add_argument("--seconds", type=float, default=0.0, help="stop after this much race (0: to the flag)")
    ap.add_argument("--pass-set", action="store_true", help="the candidates made to pass a file of cars")
    ap.add_argument("--position-m", type=float, default=25.0)
    args = ap.parse_args()
    global CANDIDATES, POSITION_M, IMPACTS
    if args.pass_set:
        CANDIDATES = CANDIDATES_PASS
    POSITION_M = args.position_m
    if args.game.startswith("stcc"):
        IMPACTS = set()        # Touring Car's 55-57/65-67 are its steering's feel, not knocks
    out = Path(args.out); (out / "states").mkdir(parents=True, exist_ok=True)
    line = Line(HERE / "runs" / args.map)
    image = (64, 48) if args.images else None
    k = len(CANDIDATES)
    agents = [Sm2Agent(args.game, nvram=HERE / "states" / "nvram") for _ in range(k)]
    pilots = [Pilot(line, args.params) for _ in range(k)]
    current = str(Path(args.start).resolve())
    pilot_state = (0.0, np.zeros(2, dtype=np.float32), None)   # the chosen pilot's offset, last action, position
    demo, qdata, log, step, t0 = [], [], [], 0, time.time()
    while True:
        # each candidate: load, drive COMMIT (recording), save, drive on to the horizon
        records = [[] for _ in range(k)]
        for a, p in zip(agents, pilots):
            assert a.load(current)
            p.offset, p.prev_action, p.prev_pos = pilot_state[0], pilot_state[1].copy(), pilot_state[2]
        first = run_all(agents, pilots, CANDIDATES, args.commit, records, image)
        snaps = []
        for j, (a, p) in enumerate(zip(agents, pilots)):
            a.save(str((out / "states" / f"c{j}.state").resolve()))
            snaps.append((p.offset, p.prev_action.copy(), p.prev_pos))
        second = run_all(agents, pilots, CANDIDATES, args.horizon - args.commit)
        results = []
        for j in range(k):
            t1, d1, q1, i1 = first[j]
            t2, d2, q2, i2 = second[j]
            results.append((score(t2, d1 + d2, q1 + q2, i1 + i2, line.length), t1, snaps[j], over(t1)))
        scores = np.array([r[0] for r in results], dtype=np.float32)
        if records[0]:                                  # the decision's observation and every candidate's score
            qdata.append({"vec": records[0][0]["vec"], "scores": scores,
                          **({"img": records[0][0]["img"]} if image else {})})
        best = int(np.argmax(scores))
        sc, t1, snap, done = results[best]
        demo += records[best]
        current = str((out / "states" / f"step{step % 2}.state").resolve())
        copy_retry(out / "states" / f"c{best}.state", current)
        if step % 5 == 0:                               # kept, to plan again from there
            shutil.copy(current, out / "states" / f"p{t1.player.progress:05.2f}.state")
        pilot_state = snap
        log.append({"step": step, "progress": round(t1.player.progress, 4), "position": t1.position,
                    "time_left": round(t1.time_left, 2), "choice": repr(CANDIDATES[best]), "index": best,
                    "set": "pass" if CANDIDATES is CANDIDATES_PASS else "base", "score": round(sc, 1)})
        print(f"{step:4d} prog {t1.player.progress:6.3f} pos {t1.position:2d} time {t1.time_left:5.1f} "
              f"choice {CANDIDATES[best]} score {sc:8.1f}  ({time.time() - t0:.0f} s)", flush=True)
        step += 1
        if step % 10 == 0:
            save(out, demo, qdata, log, image, t1, finished=None)
        if done or (args.seconds and step * args.commit >= args.seconds):
            break
    for a in agents:
        a.close()
    finished = t1.player.lap > t1.laps
    print(f"{'FINISHED' if finished else 'out of time'}: position {t1.position}", flush=True)
    save(out, demo, qdata, log, image, t1, finished)


if __name__ == "__main__":
    main()
