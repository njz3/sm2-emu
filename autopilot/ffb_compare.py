"""Compare, in a run_vjoy.py log (race.csv), what the game asked of the drive
board with the force feedback sm2-emu sent to the wheel (vJoy).

usage: python ffb_compare.py RACE.csv [--base 3] [--dip 0]

For each effect command of the EPR-18261 board (Indy 500, Touring Car): how
many frames it was in force, the strength the real board gives it
((2n + base) * 2 + DIP, out of 63: DRIVEBOARD.md), and the constant force
sm2-emu sent in those frames (vJoy's -10000..10000, as a percent of the
wheel's maximum): mean of its size, 95th percentile, and its side."""
import argparse
import csv
from collections import defaultdict

import numpy as np

EFFECTS = {0x1: "stop", 0x2: "brake/friction", 0x3: "spring", 0x4: "anti-spring", 0x5: "push left",
           0x6: "push right"}


def board_strength(cmd, base, dip):
    n = cmd & 7
    hi = cmd >> 4
    if hi == 0x3 and cmd & 8:                  # 38-3F: the soft spring, half as stiff
        return ((2 * n + base) * 2 + dip) / 2
    if hi in (0x3, 0x4, 0x5, 0x6):
        return (2 * n + base) * 2 + dip
    return 0


def rally(rows):
    """Sega Rally (EPR-17891): 80-9F a torque to the right, C0-DF to the left,
    power (cmd & 1F) + 1 out of 32; 15 chops it (rough ground); 00 none."""
    torque, chop, force = None, False, 0
    per = defaultdict(list)
    for r in rows:
        for b in r["drive"].split():
            v = int(b, 16)
            if v >= 0x80 and (v & 0x60) in (0x00, 0x40):
                torque = v
            elif v == 0x00:
                torque = 0
            elif v == 0x15:
                chop = True
            elif v == 0x10:
                chop = False
        for p in r["ffb"].split(" | "):
            if p.startswith("constant"):
                m = int(p.split("magnitude=")[1])
                force = m - 0x10000 if m > 0x7FFF else m
        if torque is not None:
            side = "none" if torque == 0 else ("right" if torque < 0xC0 else "left")
            power = 0 if torque == 0 else (torque & 0x1F) + 1
            per[(side, (power + 3) // 4 * 4, chop)].append(force / 100.0)
    print(f"{len(rows)} frames. Board power out of 32 (in 4s), sm2-emu's force in % of the wheel's maximum, + right")
    print(f"{'side':<6} {'power':>6} {'chop':>5} {'frames':>7} {'board %':>8} {'emu mean F':>11} {'emu |F| p95':>12}")
    for key in sorted(per, key=lambda k: (k[0], k[1], k[2])):
        f = np.array(per[key])
        side, power, chop = key
        print(f"{side:<6} {power:>6} {str(chop):>5} {len(f):>7} {100 * power / 32:>7.0f}% {f.mean():>+10.0f}% "
              f"{np.percentile(np.abs(f), 95):>11.0f}%")


def daytona(rows, dip):
    """Daytona USA (EPR-16488A): pushes 5n (one way) and 6n (the other) and the
    anti-spring 4n at 2n + 10 + base out of 63, the base holding the boost
    of 7X (((x & E) >> 1) + 2) and the breakaway (not counted here); springs
    3n up to 2n + base; 2n a power with no way."""
    effect, boost, force = None, 0, 0
    per = defaultdict(list)
    for r in rows:
        for b in r["drive"].split():
            v = int(b, 16)
            if 0x70 <= v <= 0x7F:
                boost = ((v & 0x0E) >> 1) + 2
            elif 0x20 <= v < 0x70 and (v & 0x08) == 0 or 0x38 <= v <= 0x3F:
                effect = v
            elif v in (0x00, 0x01, 0x02, 0x03, 0x04, 0x08, 0x09, 0x0B, 0x0C):
                effect, boost = None, 0
        for p in r["ffb"].split(" | "):
            if p.startswith("constant"):
                m = int(p.split("magnitude=")[1])
                force = m - 0x10000 if m > 0x7FFF else m
        if effect is not None:
            per[(effect, boost)].append(force / 100.0)
    names = {2: "no way (brake?)", 3: "spring", 4: "anti-spring", 5: "push", 6: "push, other way"}
    print(f"{len(rows)} frames. Board power out of 63 (DIP {dip}, breakaway not counted); "
          f"sm2-emu's force in % of the wheel's maximum, + right")
    print(f"{'command':>8} {'effect':<16} {'boost':>5} {'frames':>7} {'board':>6} {'board %':>8} {'emu mean F':>11} "
          f"{'emu |F| p95':>12}")
    for (cmd, boost) in sorted(per):
        f = np.array(per[(cmd, boost)])
        n, hi = cmd & 7, cmd >> 4
        board = 2 * n + dip + boost + (10 if hi in (4, 5, 6) else 0)
        print(f"{cmd:>8X} {names.get(hi, '?'):<16} {boost:>5} {len(f):>7} {board:>6} {100 * board / 63:>7.0f}% "
              f"{f.mean():>+10.0f}% {np.percentile(np.abs(f), 95):>11.0f}%")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--board", default="indy", choices=["indy", "rally", "daytona"])
    ap.add_argument("--base", type=int, default=3, help="the strength base, set by 7X (Indy 500: 73 -> 3)")
    ap.add_argument("--dip", type=int, default=0)
    args = ap.parse_args()
    rows = list(csv.DictReader(open(args.csv, encoding="utf-8")))
    if args.board == "rally":
        rally(rows)
        return
    if args.board == "daytona":
        daytona(rows, args.dip)
        return
    effect, force = None, 0
    per = defaultdict(list)
    for r in rows:
        for b in r["drive"].split():
            v = int(b, 16)
            if 0x10 <= v < 0x70:               # an effect command: the board's state until the next
                effect = v
        for p in r["ffb"].split(" | "):
            if p.startswith("constant"):
                m = int(p.split("magnitude=")[1])
                force = m - 0x10000 if m > 0x7FFF else m
        if effect is not None:
            per[effect].append(force / 100.0)
    print(f"{len(rows)} frames. Board strength out of 63 (base {args.base}, DIP {args.dip}); "
          f"sm2-emu's force in % of the wheel's maximum, + to the right")
    print(f"{'command':>8} {'effect':<15} {'frames':>7} {'board':>6} {'board %':>8} {'emu |F| mean':>13} "
          f"{'emu |F| p95':>12} {'emu mean F':>11}")
    for cmd in sorted(per):
        f = np.array(per[cmd])
        s = board_strength(cmd, args.base, args.dip)
        print(f"{cmd:>8X} {EFFECTS.get(cmd >> 4, '?'):<15} {len(f):>7} {s:>6.1f} {100 * s / 63:>7.0f}% "
              f"{np.abs(f).mean():>12.0f}% {np.percentile(np.abs(f), 95):>11.0f}% {f.mean():>+10.0f}%")


if __name__ == "__main__":
    main()
