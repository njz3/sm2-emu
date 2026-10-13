"""A whole race in agent mode with a driver, from a start state: each lap's
time and position, the result.

usage: python eval_race.py START_STATE MAP POLICY [--pics DIR]
  POLICY: pursuit[:gain] | line:PARAMS.json | ppo:CKPT.pt[:PARAMS.json] | q:MODEL.pt
          | visual:MODEL.pt (the picture alone)"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import indy500_telemetry as T  # noqa: E402
import telemetry_any as TA  # noqa: E402
from pursuit import Line, Pursuit  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

HERE = Path(__file__).parent
IMPACTS = {0x55, 0x56, 0x57, 0x65, 0x66, 0x67}


def race(start, mapf, policy, pics=None, game="indy500"):
    line = Line(HERE / "runs" / mapf if not Path(mapf).exists() else mapf)
    kind, _, arg = policy.partition(":")
    if kind == "ppo":
        from policy_ppo import PpoRunner
        ckpt, _, params = arg.partition(":")
        runner = PpoRunner(line, ckpt, params or None)
        env = runner.env_for(start)
        return runner.race(env, pics)
    if kind == "q":
        from policy_q import QDriver, race as q_race
        a = Sm2Agent(game, nvram=HERE / "states" / "nvram")
        assert a.load(str(Path(start).resolve()))
        drv = QDriver(line, arg, image_source=lambda: a.obs(64, 48))
        laps_log = []
        t, impacts = q_race(a, drv, laps_log)
        a.close()
        return {"finished": t.player.lap > t.laps, "position": t.position,
                "progress": t.player.progress, "laps": laps_log, "impacts": impacts}
    if kind == "visual":
        from visual_driver import VisualDriver
        a = Sm2Agent(game, nvram=HERE / "states" / "nvram")
        assert a.load(str(Path(start).resolve()))
        drv = VisualDriver(arg, source=lambda: a.obs(64, 48))
    elif kind == "line":
        from line_opt import LineDriver
        import json
        p = json.loads(Path(arg).read_text())
        drv = LineDriver(line, p["x"], p["knots"])
    else:
        drv = Pursuit(line, gain=float(arg or 3.0))
    if kind != "visual":
        a = Sm2Agent(game, nvram=HERE / "states" / "nvram")
        assert a.load(str(Path(start).resolve()))
    laps_log, last_lap, lap_frame, impacts = [], None, a.frame, 0
    while True:
        t = TA.read(a)
        p = t.player
        if last_lap is not None and p.lap != last_lap:
            laps_log.append(((a.frame - lap_frame) / T.FPS, t.position, t.time_left))
            lap_frame = a.frame
        last_lap = p.lap
        laps = t.laps
        if p.lap > laps or t.time_left <= 0:
            break
        drive = a.step(4, *drv(t))
        impacts += sum(1 for b in drive if b in IMPACTS)
    a.close()
    return {"finished": p.lap > laps, "position": t.position, "progress": p.progress,
            "laps": laps_log, "impacts": impacts}


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("start"); ap.add_argument("map"); ap.add_argument("policy")
    ap.add_argument("--pics")
    ap.add_argument("--game", default="indy500")
    args = ap.parse_args()
    r = race(args.start, args.map, args.policy, args.pics, args.game)
    for k, (lt, pos, tl) in enumerate(r["laps"], 1):
        print(f"lap {k}: {lt:6.2f} s, position {pos}, time left {tl:5.1f}")
    print(f"{'finished' if r['finished'] else 'not finished'}: position {r['position']}, "
          f"progress {r['progress']:.3f}, hard impacts {r['impacts']}")
