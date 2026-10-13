"""plan_race.py with a beam: the B best states are kept at each step instead of
one, each tried with every candidate; the children are ranked by where they
stand at the horizon (distance driven and place, not their gain), so that
branches from different parents compare. For the end of a race, where one
pass decides the result.

usage: python plan_beam.py START MAP OUT_DIR [--beam 2] [--horizon 8] [--commit 1]
                           [--position-m 40] [--images]"""
import argparse
import json
import shutil
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import indy500_telemetry as T  # noqa: E402
import plan_race as P  # noqa: E402
from env_indy import Pilot  # noqa: E402
from pursuit import Line  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

HERE = Path(__file__).parent


def standing(t, impacts, length, position_m):
    """Where a child stands at the horizon: metres driven, places, impacts."""
    laps = t.laps
    s = t.player.progress * length - position_m * t.position - 15.0 * impacts
    if t.player.lap > laps:
        s += 1e6 - 3e4 * t.position
    elif t.time_left <= 0:
        s -= 1e6
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("start"); ap.add_argument("map"); ap.add_argument("out")
    ap.add_argument("--params", default=str(HERE / "runs" / "opt_oval" / "best_gen30.json"))
    ap.add_argument("--beam", type=int, default=2)
    ap.add_argument("--horizon", type=float, default=8.0)
    ap.add_argument("--commit", type=float, default=1.0)
    ap.add_argument("--position-m", type=float, default=40.0)
    ap.add_argument("--images", action="store_true")
    ap.add_argument("--pilot", help="the pilot's state to start with (replay_log.py's pilot.json)")
    ap.add_argument("--game", default="indy500")
    args = ap.parse_args()
    out = Path(args.out); (out / "states").mkdir(parents=True, exist_ok=True)
    if args.game.startswith("stcc"):
        P.IMPACTS = set()          # Touring Car's 55-57/65-67 are its steering's feel, not knocks
    line = Line(HERE / "runs" / args.map)
    image = (64, 48) if args.images else None
    cands = P.CANDIDATES_PASS
    k = len(cands)
    agents = [Sm2Agent(args.game, nvram=HERE / "states" / "nvram") for _ in range(k)]
    pilots = [Pilot(line, args.params) for _ in range(k)]
    start = out / "states" / "beam0_0.state"
    P.copy_retry(args.start, start)
    pilot0 = (0.0, np.zeros(2, dtype=np.float32), None)
    if args.pilot:
        pj = json.loads(Path(args.pilot).read_text())
        pilot0 = (pj["offset"], np.array(pj["prev_action"], dtype=np.float32),
                  tuple(pj["prev_pos"]) if pj["prev_pos"] else None)
    beam = [{"state": str(start.resolve()), "pilot": pilot0,
             "demo": [], "log": [], "done": False, "t": None}]
    step, t0, qdata = 0, time.time(), []
    while not all(n["done"] for n in beam):
        children = []
        for b, node in enumerate(beam):
            if node["done"]:                         # over: it competes with what it stood at
                children.append((node["score"], node))
                continue
            records = [[] for _ in range(k)]
            for a, p in zip(agents, pilots):
                assert a.load(node["state"])
                p.offset, p.prev_action, p.prev_pos = node["pilot"][0], node["pilot"][1].copy(), node["pilot"][2]
            first = P.run_all(agents, pilots, cands, args.commit, records, image)
            snaps = []
            for j, (a, p) in enumerate(zip(agents, pilots)):
                a.save(str((out / "states" / f"cand{b}_{j}.state").resolve()))
                snaps.append((p.offset, p.prev_action.copy(), p.prev_pos))
            second = P.run_all(agents, pilots, cands, args.horizon - args.commit)
            if records[0]:                           # every candidate's standing: rows for q_train.py
                qdata.append({"vec": records[0][0]["vec"],
                              "scores": np.array([standing(second[j][0], first[j][3] + second[j][3], line.length,
                                                           args.position_m) for j in range(k)], dtype=np.float32),
                              **({"img": records[0][0]["img"]} if image else {})})
            for j in range(k):
                t1 = first[j][0]
                t2, _, _, imp = second[j]
                sc = standing(t2, first[j][3] + imp, line.length, args.position_m)
                children.append((sc, {"state": str((out / "states" / f"cand{b}_{j}.state").resolve()),
                                      "pilot": snaps[j], "demo": node["demo"] + records[j],
                                      "log": node["log"] + [{"step": step, "choice": repr(cands[j]),
                                                             "index": j, "set": "pass",
                                                             "progress": round(t1.player.progress, 4),
                                                             "position": t1.position}],
                                      "done": P.over(t1), "t": t1,
                                      "score": standing(t1, first[j][3], line.length, args.position_m)}))
        children.sort(key=lambda c: -c[0])
        # the best B, from as many different states as possible
        new, seen = [], set()
        for sc, c in children:
            key = (round(c["t"].player.progress, 3), c["t"].position) if c["t"] else id(c)
            if key in seen:
                continue
            seen.add(key)
            new.append(c)
            if len(new) == args.beam:
                break
        # keep the chosen states under stable names (the candidates' are overwritten next step)
        for b, c in enumerate(new):
            dst = out / "states" / f"beam{(step + 1) % 2}_{b}.state"
            if Path(c["state"]) != dst.resolve():
                P.copy_retry(c["state"], dst)
            c["state"] = str(dst.resolve())
        beam = new
        best = beam[0]
        if step % 5 == 0 and not best["done"]:          # kept, to search again from there
            P.copy_retry(best["state"], out / "states" / f"p{best['t'].player.progress:05.2f}_{best['t'].position}.state")
        print(f"{step:4d} best: prog {best['t'].player.progress:6.3f} pos {best['t'].position:2d} "
              f"time {best['t'].time_left:5.1f} | " + " | ".join(
                  f"{c['t'].player.progress:.3f}/{c['t'].position}" for c in beam) + f"  ({time.time() - t0:.0f} s)",
              flush=True)
        step += 1
    for a in agents:
        a.close()
    best = max(beam, key=lambda c: (c["t"].player.lap > c["t"].laps, -c["t"].position,
                                     c["t"].player.progress))
    t = best["t"]
    finished = t.player.lap > t.laps
    print(f"{'FINISHED' if finished else 'out of time'}: position {t.position}", flush=True)
    demo = best["demo"]
    np.savez_compressed(out / "demo.npz", vec=np.stack([d["vec"] for d in demo]),
                        act=np.stack([d["act"] for d in demo]), ctl=np.stack([d["ctl"] for d in demo]),
                        **({"img": np.stack([d["img"] for d in demo])} if image else {}))
    (out / "plan.json").write_text(json.dumps({"finished": finished, "position": t.position, "log": best["log"]}))
    if qdata:
        np.savez_compressed(out / "qdata.npz", vec=np.stack([q["vec"] for q in qdata]),
                            scores=np.stack([q["scores"] for q in qdata]),
                            **({"img": np.stack([q["img"] for q in qdata])} if image else {}))


if __name__ == "__main__":
    main()
