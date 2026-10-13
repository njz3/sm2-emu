"""Touring Car from the boot to its qualifying lap, and from there to the race:
saves a state at each.

usage: python stcc_to_race.py OUT_QUALIFY.state [OUT_RACE.state] [--map MAP] [--pics DIR] [--mt]

Coins (1 a credit), start, the Alfa Romeo, automatic (--mt: the manual
gearbox, the brake held while the gas confirms; it starts in 1st and wants
the paddles), the name as it is. The qualifying lap: driven
by the pure pursuit on MAP when given (a better place on the grid), else let
time out (disqualified: last of 8). Then the grid screens, and the race's
state is saved at its rolling start."""
import argparse
import struct
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
import stcc_telemetry as S  # noqa: E402
from pursuit import Line, Pursuit  # noqa: E402
from sm2agent import COIN, Sm2Agent  # noqa: E402

START = 0x40
ap = argparse.ArgumentParser()
ap.add_argument("qualify"); ap.add_argument("race", nargs="?")
ap.add_argument("--map"); ap.add_argument("--pics")
ap.add_argument("--mt", action="store_true")
args = ap.parse_args()
pics = Path(args.pics) if args.pics else None
if pics:
    pics.mkdir(parents=True, exist_ok=True)
a = Sm2Agent("stcc", nvram=Path(__file__).parent / "states" / "nvram")
n = [0]


def run(seconds, **controls):
    for _ in range(int(seconds)):
        a.step(57, **controls)
        n[0] += 1
        if pics:
            Image.fromarray(a.obs(248, 192)).save(pics / f"{n[0]:04d}.png")


def time_left():
    return struct.unpack("<I", a.mem(S.TIME_LEFT, 4))[0]


def driven():
    return struct.unpack("<h", a.mem(S.CARS + 0x52, 2))[0]


run(26)
for _ in range(2):
    a.press(COIN, 30)
run(1)
a.press(START, 40)
run(2)
if args.mt:
    a.step(30, brake=1.0)                   # the brake held...
    a.step(35, accel=1.0, brake=1.0)        # ...while the gas confirms: manual gearbox
else:
    a.step(35, accel=1.0)                   # automatic
a.step(20)
for _ in range(10):                         # the name entry, through the intros, up to the qualifying
    run(1)
    if 0 < time_left() < 41 * 57.5:
        break
    a.step(25, accel=1.0)
print("qualifying:", "saved" if a.save(str(Path(args.qualify).resolve())) else "save failed", args.qualify)
if args.race:
    if args.map:
        line = Line(args.map)
        drv = Pursuit(line, gain=2.0)
        t = S.read(a)
        while time_left() > 0 and t.player.progress < 1.0:
            t = S.read(a)
            a.step(2, *drv(t))
        print("qualifying lap: progress %.3f, time left %.1f" % (t.player.progress, time_left() / 57.5))
    # the grid screens, up to the race's rolling start (the cars behind the line again)
    for _ in range(90):
        run(1)
        if driven() < 0 and time_left() > 30 * 57.5:
            break
    print("race:", "saved" if a.save(str(Path(args.race).resolve())) else "save failed", args.race)
a.close()
