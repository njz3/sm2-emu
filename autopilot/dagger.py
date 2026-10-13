"""Train the score network on the races it drives itself (DAgger): each round,
the network races alone from the start, its state saved every second; at each
of those states the planner's candidates are tried with the emulator and
scored (one planner step each); those rows join the data and the network
learns again. Every round's race is also its evaluation.

usage: python dagger.py OUT_DIR --data PLAN_DIR... [--rounds 6] [--visual]
                        [--horizon 8] [--position-m 40] [--every 2]
OUT_DIR/roundN: the race's states, its rows (qdata.npz), the network after
it (q.pt); OUT_DIR/log.csv: each round's result."""
import argparse
import csv
import shutil
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import plan_race as P  # noqa: E402
import telemetry_any as TA  # noqa: E402
from env_indy import FRAMES, IMPACTS, Pilot  # noqa: E402
from policy_q import DECIDE, QDriver  # noqa: E402
from pursuit import Line  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

HERE = Path(__file__).parent


def race_and_record(args, line, model, outdir, latency=0):
    """The network races from the start; a state, the pilot and the picture every
    --every seconds. latency: frames its controls arrive late (0 to latency,
    random: as through vJoy)."""
    import random
    rng = random.Random(len(list(outdir.parent.glob("round*"))))
    a = Sm2Agent(args.game, nvram=HERE / "states" / "nvram")
    assert a.load(str(Path(args.start).resolve()))
    drv = QDriver(line, model, args.params, image_source=lambda: a.obs(64, 48))
    points = []
    t = TA.read(a)
    impacts, k = 0, 0
    prev = (0.0, 0.0, 0.0)
    while t.player.lap <= t.laps and t.time_left > 0:
        if k % int(args.every * DECIDE / FRAMES) == 0:
            name = outdir / f"d{len(points):04d}.state"
            a.save(str(name.resolve()))
            pl = drv.pilot
            points.append({"state": str(name.resolve()),
                           "pilot": (pl.offset, pl.prev_action.copy(), pl.prev_pos),
                           "img": a.obs(64, 48).mean(axis=2).astype(np.uint8)})
        ctl = drv(t)
        late = rng.randint(0, latency) if latency else 0
        drive = a.step(late, *prev) if late else []
        drive += a.step(FRAMES - late, *ctl)
        prev = ctl
        impacts += sum(1 for b in drive if b in IMPACTS)
        t = TA.read(a)
        k += 1
    a.close()
    return points, {"finished": t.player.lap > t.laps, "position": t.position,
                    "progress": round(t.player.progress, 3), "impacts": impacts}


def label(args, line, points, agents, pilots, model=None):
    """Every candidate's score at each point, as the planner scores them; with
    --rollout, each candidate for COMMIT seconds and then the network itself
    (`model`) to the horizon."""
    vecs, scores, imgs = [], [], []
    cands = P.CANDIDATES
    for n, pt in enumerate(points):
        records = [[] for _ in agents]
        for a, p in zip(agents, pilots):
            assert a.load(pt["state"])
            p.offset, p.prev_action, p.prev_pos = pt["pilot"][0], pt["pilot"][1].copy(), pt["pilot"][2]
        first = P.run_all(agents, pilots, cands, args.commit, records)
        if args.rollout and model is not None:
            from policy_q import QChooser, load_net
            net, net_cands = load_net(model)
            then = [QChooser(net, net_cands) for _ in agents]
        else:
            then = cands
        second = P.run_all(agents, pilots, then, args.horizon - args.commit)
        if not records[0]:
            continue
        sc = [P.score(second[j][0], first[j][1] + second[j][1], first[j][2] + second[j][2],
                      first[j][3] + second[j][3], line.length) for j in range(len(cands))]
        vecs.append(records[0][0]["vec"]); scores.append(np.array(sc, dtype=np.float32)); imgs.append(pt["img"])
        if n % 20 == 0:
            print(f"   labelled {n + 1}/{len(points)}", flush=True)
    return np.stack(vecs), np.stack(scores), np.stack(imgs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--data", nargs="+", required=True, help="planner runs (their qdata.npz) to start from")
    ap.add_argument("--rounds", type=int, default=6)
    ap.add_argument("--visual", action="store_true")
    ap.add_argument("--start", default=str(HERE / "states" / "indy_oval_mt.state"))
    ap.add_argument("--map", default="indy_oval_line.npz")
    ap.add_argument("--params", default=str(HERE / "runs" / "opt_oval" / "best_gen30.json"))
    ap.add_argument("--game", default="indy500")
    ap.add_argument("--horizon", type=float, default=8.0)
    ap.add_argument("--commit", type=float, default=1.0)
    ap.add_argument("--position-m", type=float, default=40.0)
    ap.add_argument("--epochs", type=int, default=150)
    ap.add_argument("--every", type=float, default=2.0, help="seconds between the points labelled")
    ap.add_argument("--rollout", action="store_true", help="after the candidate's first second, the network drives")
    ap.add_argument("--init", help="a network to start from instead of training one on --data")
    ap.add_argument("--latency", type=int, default=0, help="frames the network's controls may arrive late (0..N)")
    ap.add_argument("--path", help="a race's decisions to keep reproducing (q_train.py --path)")
    args = ap.parse_args()
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    P.CANDIDATES = P.CANDIDATES_PASS
    P.POSITION_M = args.position_m
    line = Line(HERE / "runs" / args.map)
    data = list(args.data)
    model = out / "q0.pt"
    train = [sys.executable, str(HERE / "q_train.py")]
    flags = (["--visual"] if args.visual else []) + (["--path", args.path] if args.path else [])
    if args.init:
        shutil.copy(args.init, model)
    else:
        subprocess.run(train + [str(model)] + data + flags + ["--epochs", str(args.epochs)], check=True)
    logf = open(out / "log.csv", "a", newline="")
    log = csv.writer(logf)
    for r in range(1, args.rounds + 1):
        rd = out / f"round{r}"
        rd.mkdir(exist_ok=True)
        t0 = time.time()
        points, result = race_and_record(args, line, str(model), rd, args.latency)
        print(f"round {r}: the network alone: {result} ({len(points)} points, {time.time() - t0:.0f} s)", flush=True)
        log.writerow([r, result["finished"], result["position"], result["progress"], result["impacts"]]); logf.flush()
        if result["finished"] and result["position"] == 1:
            shutil.copy(model, out / "winner.pt")
            print(f"WON THE RACE with {model}", flush=True)
        k = len(P.CANDIDATES)
        agents = [Sm2Agent(args.game, nvram=HERE / "states" / "nvram") for _ in range(k)]
        pilots = [Pilot(line, args.params) for _ in range(k)]
        vec, sc, img = label(args, line, points, agents, pilots, str(model))
        for a in agents:
            a.close()
        np.savez_compressed(rd / "qdata.npz", vec=vec, scores=sc, img=img)
        for f in rd.glob("d*.state"):
            f.unlink()
        data.append(str(rd))
        model = out / f"q{r}.pt"
        subprocess.run(train + [str(model)] + data + flags + ["--epochs", str(args.epochs)], check=True)


if __name__ == "__main__":
    main()
