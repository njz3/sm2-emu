"""Replay a planned race (plan_race.py's plan.json choices) in one emulator:
the emulator being deterministic, the same race again, now with a look at
the cars ahead each second (gaps, speeds), or saving states on the way.

usage: python replay_plan.py START MAP PLAN_DIR [--save-every S --save-dir DIR]"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import indy500_telemetry as T  # noqa: E402
import telemetry_any as TA  # noqa: E402
from env_indy import FRAMES, Pilot  # noqa: E402
from plan_race import CANDIDATES, CANDIDATES_PASS  # noqa: E402
from pursuit import Line  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

HERE = Path(__file__).parent

ap = argparse.ArgumentParser()
ap.add_argument("start"); ap.add_argument("map"); ap.add_argument("plan")
ap.add_argument("--params", default=str(HERE / "runs" / "opt_oval" / "best_gen30.json"))
ap.add_argument("--commit", type=float, default=1.0)
ap.add_argument("--save-every", type=float, default=0.0)
ap.add_argument("--save-dir")
args = ap.parse_args()
line = Line(HERE / "runs" / args.map)
plan = json.loads((Path(args.plan) / "plan.json").read_text())
names = {repr(c): c for c in CANDIDATES}
a = Sm2Agent("indy500", nvram=HERE / "states" / "nvram")
assert a.load(str(Path(args.start).resolve()))
pilot = Pilot(line, args.params)
steps = int(round(args.commit * T.FPS / FRAMES))
if args.save_dir:
    Path(args.save_dir).mkdir(parents=True, exist_ok=True)
for e in plan["log"]:
    if "index" in e:
        cand = (CANDIDATES_PASS if e.get("set") == "pass" else CANDIDATES)[e["index"]]
    else:                                   # older plans: by name, the base set's
        choice = e["choice"]
        cand = names[choice] if isinstance(choice, str) else tuple(choice)
    t = TA.read(a)
    for k in range(steps):
        pilot.features(t)
        action = cand(t, pilot, k) if callable(cand) else cand
        a.step(FRAMES, *pilot.control(t, action))
        t = TA.read(a)
    ahead = sorted((c for c in t.others if c.progress > t.player.progress), key=lambda c: -c.progress)[:4]
    gaps = " ".join(f"{(c.progress - t.player.progress) * line.length:5.0f}m/{c.speed:5.1f}" for c in ahead)
    print(f"{e['step']:4d} prog {t.player.progress:6.3f} (plan {e['progress']:6.3f}) pos {t.position:2d} "
          f"v {t.player.speed:5.1f} | ahead: {gaps}", flush=True)
    if args.save_every and e["step"] % int(args.save_every / args.commit) == 0:
        a.save(str((Path(args.save_dir) / f"p{t.player.progress:05.2f}.state").resolve()))
a.close()
