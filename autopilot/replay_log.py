"""Replay a planner's run from its printed log (plan_race.py's output lines),
for a run whose plan.json was lost: each step's printed choice is driven
again (the emulator is deterministic); where a printed name fits several
candidates, each is tried and the one that lands on the printed progress
and place is kept. Saves the demonstration (as plan_race.py) and the state
at the end.

usage: python replay_log.py START MAP PLAN.out OUT_DIR [--pass-set] [--images]"""
import argparse
import re
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import plan_race as P  # noqa: E402
import telemetry_any as TA  # noqa: E402
from env_indy import FRAMES, Pilot  # noqa: E402
from pursuit import Line  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

HERE = Path(__file__).parent
LINE = re.compile(r"^\s*(\d+) prog\s+([\d.]+) pos\s+(\d+) time\s+([\d.]+) choice (.+?) score")


def old_name(c):
    """How the run printed a candidate (Follow's former repr had no delay)."""
    if callable(c):
        return f"follow{c.side:+.1f}"
    return repr(c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("start"); ap.add_argument("map"); ap.add_argument("log"); ap.add_argument("out")
    ap.add_argument("--params", default=str(HERE / "runs" / "opt_oval" / "best_gen30.json"))
    ap.add_argument("--pass-set", action="store_true")
    ap.add_argument("--images", action="store_true")
    ap.add_argument("--commit", type=float, default=1.0)
    ap.add_argument("--game", default="indy500")
    args = ap.parse_args()
    cands = P.CANDIDATES_PASS if args.pass_set else P.CANDIDATES
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    line = Line(HERE / "runs" / args.map)
    image = (64, 48) if args.images else None
    steps = [m.groups() for m in map(LINE.match, Path(args.log).read_text(errors="replace").splitlines()) if m]
    a = Sm2Agent(args.game, nvram=HERE / "states" / "nvram")
    assert a.load(str(Path(args.start).resolve()))
    pilot = Pilot(line, args.params)
    tmp = str((out / "tmp.state").resolve())
    demo, log = [], []
    for step, prog, pos, _, name in steps:
        options = [j for j, c in enumerate(cands) if old_name(c) == name.strip()]
        if not options:
            sys.exit(f"step {step}: no candidate named {name}")
        a.save(tmp)
        snap = (pilot.offset, pilot.prev_action.copy(), pilot.prev_pos)
        chosen = None
        for j in options:
            if chosen is not None:
                break
            a.load(tmp)
            pilot.offset, pilot.prev_action, pilot.prev_pos = snap[0], snap[1].copy(), snap[2]
            records = [[]]
            res = P.run_all([a], [pilot], [cands[j]], args.commit, records, image)[0]
            t = res[0]
            if abs(t.player.progress - float(prog)) < 0.0006 and t.position == int(pos):
                chosen = j
                demo += records[0]
        if chosen is None:
            sys.exit(f"step {step}: none of {[old_name(cands[j]) for j in options]} lands on {prog}/{pos}")
        log.append({"step": int(step), "choice": repr(cands[chosen]), "index": chosen,
                    "set": "pass" if args.pass_set else "base", "progress": float(prog), "position": int(pos)})
        if int(step) % 20 == 0:
            print(f"step {step}: {old_name(cands[chosen])} (index {chosen}) -> {prog} {pos}", flush=True)
    a.save(str((out / "end.state").resolve()))
    import json as _json
    (out / "pilot.json").write_text(_json.dumps({"offset": pilot.offset, "prev_action": pilot.prev_action.tolist(),
                                                 "prev_pos": list(pilot.prev_pos) if pilot.prev_pos else None}))
    a.close()
    P.save(out, demo, [], log, image, t, None)
    print("replayed", len(steps), "steps; demo", len(demo), "decisions")


if __name__ == "__main__":
    main()
