"""Record the work RAM under a known control script, from a save state, to find
the game's variables.

usage: python discover.py GAME STATE OUT.npz SCRIPT [--scratch]
  SCRIPT: comma-separated FRAMES:STEER:ACCEL:BRAKE segments.
Saves ram (T x 1 MB, one snapshot every 2 frames; --scratch: the 256 KB of
scratch RAM at 00200000, where Touring Car keeps its game), the controls and
the drive board bytes of each step."""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from sm2agent import Sm2Agent  # noqa: E402

game, state, out, script = sys.argv[1:5]
scratch = "--scratch" in sys.argv
a = Sm2Agent(game, nvram=Path(__file__).parent / "states" / "nvram")
assert a.load(str(Path(state).resolve())), "load failed"
snaps, controls, drive = [], [], []
for seg in script.split(","):
    frames, steer, accel, brake = seg.split(":")
    for _ in range(int(frames) // 2):
        d = a.step(2, float(steer), float(accel), float(brake))
        raw = a.mem(0x200000, 0x40000) if scratch else a.ram(0, 0x100000)
        snaps.append(np.frombuffer(raw, dtype=np.uint8))
        controls.append((float(steer), float(accel), float(brake)))
        drive.append(d)
a.close()
np.savez_compressed(out, ram=np.stack(snaps), controls=np.array(controls),
                    drive=np.array([" ".join(f"{b:02X}" for b in d) for d in drive]))
print("saved", out, len(snaps), "snapshots")
