"""Replay a race made of planned parts (each a plan.json with candidate
indices; the pilot starts afresh at each part, as the planner that made it
did), from the start state, in one emulator: checks the result, saves a
picture every second (and after the flag), the drive board bytes, the
whole race's demonstration, and each decision (its features, picture and
candidate) in decisions.npz.

usage: python replay_win.py START MAP OUT_DIR PLAN_DIR... [--commit 1] [--continuous]
  --continuous: one pilot through all the parts (a part planned from the
  pilot's state the one before left: plan_beam.py --pilot)."""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
import plan_race as P  # noqa: E402
import telemetry_any as TA  # noqa: E402
from env_indy import FRAMES, Pilot  # noqa: E402
from pursuit import Line  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

HERE = Path(__file__).parent

ap = argparse.ArgumentParser()
ap.add_argument("start"); ap.add_argument("map"); ap.add_argument("out"); ap.add_argument("plans", nargs="+")
ap.add_argument("--params", default=str(HERE / "runs" / "opt_oval" / "best_gen30.json"))
ap.add_argument("--commit", type=float, default=1.0)
ap.add_argument("--game", default="indy500")
ap.add_argument("--continuous", action="store_true")
args = ap.parse_args()
out = Path(args.out); (out / "pics").mkdir(parents=True, exist_ok=True)
line = Line(HERE / "runs" / args.map)
a = Sm2Agent(args.game, nvram=HERE / "states" / "nvram")
assert a.load(str(Path(args.start).resolve()))
steps = int(round(args.commit * 57.5 / FRAMES))
demo, drive_log, laps, decisions, n = [], [], [], [], 0
pilot = None
t = TA.read(a)
last_lap, lap_frame = t.player.lap, a.frame
for part in args.plans:
    plan = json.loads((Path(part) / "plan.json").read_text())
    if pilot is None or not args.continuous:
        pilot = Pilot(line, args.params)
    for e in plan["log"]:
        cand = (P.CANDIDATES_PASS if e.get("set") == "pass" else P.CANDIDATES)[e["index"]]
        for k in range(steps):
            vec = pilot.features(t)
            img = a.obs(64, 48).mean(axis=2).astype(np.uint8)
            if k == 0:
                decisions.append((vec, img, e["index"]))
            action = cand(t, pilot, k) if callable(cand) else cand
            ctl = pilot.control(t, action)
            demo.append({"vec": vec, "act": np.asarray(action, dtype=np.float32),
                         "ctl": np.asarray(ctl[:3], dtype=np.float32), "img": img})
            d = a.step(FRAMES, *ctl)
            drive_log.append(" ".join(f"{b:02X}" for b in d))
            t = TA.read(a)
            if t.player.lap != last_lap:
                laps.append(((a.frame - lap_frame) / 57.5, t.position, round(t.time_left, 1)))
                last_lap, lap_frame = t.player.lap, a.frame
            if P.over(t):
                break
        n += 1
        Image.fromarray(a.obs(248, 192)).save(out / "pics" / f"{n:04d}.png")
        if abs(t.player.progress - e["progress"]) > 0.0006 or t.position != e["position"]:
            print(f"diverged at {part} step {e['step']}: {t.player.progress:.4f}/{t.position} "
                  f"against {e['progress']}/{e['position']}", flush=True)
        if P.over(t):
            break
result = {"finished": t.player.lap > t.laps, "position": t.position, "progress": round(t.player.progress, 4),
          "time_left": round(t.time_left, 1), "laps": laps}
print(json.dumps(result), flush=True)
for k in range(12):                                   # the screens after the flag
    a.step(57)
    Image.fromarray(a.obs(248, 192)).save(out / "pics" / f"end{k:02d}.png")
a.save(str((out / "finish.state").resolve()))
a.close()
P.save(out, demo, [], [], (64, 48), t, result["finished"])
(out / "result.json").write_text(json.dumps(result))
(out / "drive.txt").write_text("\n".join(drive_log))
np.savez_compressed(out / "decisions.npz", vec=np.stack([d[0] for d in decisions]),
                    img=np.stack([d[1] for d in decisions]), index=np.array([d[2] for d in decisions]))
