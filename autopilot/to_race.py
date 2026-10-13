"""Boot a game in agent mode up to the start of a race and save the state there.

usage: python to_race.py GAME COURSE OUT_STATE [PICS_DIR] [--mt]
  COURSE: the menu moves, comma-separated: right, left, gas (Indy 500: none for
  the oval, "right" Bay Side Street, "left" Highland Raceway). The wheel is
  held where the moves leave it while the gas confirms.
  --mt: the manual gearbox (Indy 500: 6 speeds, at its menu's right).
Pictures every second go to PICS_DIR to check where it got."""
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from sm2agent import COIN, Sm2Agent  # noqa: E402

START = {"indy500": 0x40, "stcc": 0x40, "srallyc": 0x40, "daytona": 0x10}

manual = "--mt" in sys.argv
argv = [v for v in sys.argv if v != "--mt"]
game, course, out = argv[1], argv[2], argv[3]
pics = Path(argv[4]) if len(argv) > 4 else None
if pics:
    pics.mkdir(parents=True, exist_ok=True)
a = Sm2Agent(game, nvram=Path(__file__).parent / "states" / "nvram")
second = [0]


wheel = [0.0]


def turn(target):
    """Turn the wheel gradually: the menus pick what the wheel points at, and
    ignore a wheel that jumps in one frame."""
    start = wheel[0]
    for k in range(1, 41):
        a.step(1, steer=start + (target - start) * k / 40)
    wheel[0] = target
    a.step(20, steer=target)


def run(seconds, **controls):
    for _ in range(int(seconds)):
        a.step(57, **controls)
        second[0] += 1
        if pics:
            Image.fromarray(a.obs(248, 192)).save(pics / f"{second[0]:04d}.png")


run(26)
for _ in range(4):
    a.press(COIN, 60 if game == "daytona" else 30)   # Daytona wants a coin held about a second
run(1)
a.press(START[game], 40)
run(3)
for move in [m for m in course.split(",") if m]:
    if move == "gas":
        a.step(35, accel=1.0)
    else:
        turn(max(-1.0, min(1.0, wheel[0] + (0.6 if move == "right" else -0.6))))
a.step(35, steer=wheel[0], accel=1.0)  # confirm the course, the wheel still on it
turn(0.0)
if manual:
    a.step(40)
    turn(0.6)
    a.step(35, steer=0.6, accel=1.0)
    turn(0.0)

clock = [None]


def started():
    """Indy 500: the player's car is on its first lap and the race clock runs
    (the clock's word also counts the menus down)."""
    if game != "indy500":
        return False
    ram = a.ram(0x3C332, 0x3C894 - 0x3C332)
    t = int.from_bytes(ram[0:2], "little")
    lap = int.from_bytes(ram[0x3C892 - 0x3C332:0x3C894 - 0x3C332], "little") // 16
    running = lap >= 1 and clock[0] is not None and 0 < t < clock[0]
    clock[0] = t if lap >= 1 else None
    return running


for _ in range(8):  # through the intros, up to the green flag
    for _ in range(4):
        a.step(25, accel=0.0)
        if started():
            break
        a.step(25, accel=1.0)
        if started():
            break
    if started():
        break
    if pics:
        second[0] += 1
        Image.fromarray(a.obs(248, 192)).save(pics / f"{second[0]:04d}.png")
print("saved" if a.save(str(Path(out).resolve())) else "save failed", out, "frame", a.frame)
a.close()
