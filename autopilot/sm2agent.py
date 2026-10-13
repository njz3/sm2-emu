"""Python side of sm2-emu's --agent mode (see src/agent.cpp for the protocol)."""
import os
import subprocess
from pathlib import Path

import numpy as np

# The repository this folder is in.
REPO = str(Path(__file__).resolve().parent.parent)
# A copy in autopilot/bin (or SM2_AGENT_EXE) when there is one, so that the
# build's exe can be relinked while a training runs.
_COPY = Path(__file__).parent / "bin" / "sm2-emu.exe"
EXE = os.environ.get("SM2_AGENT_EXE") or str(_COPY if _COPY.exists() else Path(REPO) / "build" / "bin" / "sm2-emu.exe")
# The ROM sets (SM2_ROMS to put them elsewhere).
ROMS = Path(os.environ.get("SM2_ROMS", r"D:\racecab\MAME ROMs (merged)"))
COIN, START = 0x01, 0x10  # IN0 bits, active low


class Sm2Agent:
    def __init__(self, game, archive=None, nvram=None, log_level="warning"):
        archive = archive or game
        args = [EXE, "--agent", "--log-level", log_level, "--game", game, str(ROMS / f"{archive}.zip")]
        if nvram:
            args[1:1] = ["--nvram", str(nvram)]
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, cwd=REPO, bufsize=0)
        self.game = game
        self.frame = 0

    def _send(self, line):
        self.proc.stdin.write((line + "\n").encode())

    def _line(self):
        out = bytearray()
        while True:
            c = self.proc.stdout.read(1)
            if not c:
                raise EOFError("sm2-emu closed")
            if c == b"\n":
                return out.decode()
            out += c

    def _read(self, n):
        buf = bytearray()
        while len(buf) < n:
            chunk = self.proc.stdout.read(n - len(buf))
            if not chunk:
                raise EOFError("sm2-emu closed")
            buf += chunk
        return bytes(buf)

    def step(self, n=1, steer=0.0, accel=0.0, brake=0.0, in0=0xFF, in1=0xFF):
        """Run n frames with these controls; the bytes the game wrote to the drive board."""
        self.step_send(n, steer, accel, brake, in0, in1)
        return self.step_wait()

    def step_send(self, n=1, steer=0.0, accel=0.0, brake=0.0, in0=0xFF, in1=0xFF):
        """Start a step and return at once: several emulators then run together."""
        self._send(f"step {n} {steer:.4f} {accel:.4f} {brake:.4f} {in0:02x} {in1:02x}")

    def step_wait(self):
        parts = self._line().split()
        assert parts[0] == "ok", parts
        self.frame = int(parts[1])
        return [int(b, 16) for b in parts[2:]]

    def obs(self, w=96, h=72):
        self._send(f"obs {w} {h}")
        head = self._line().split()
        assert head[0] == "obs", head
        w, h = int(head[1]), int(head[2])
        return np.frombuffer(self._read(w * h * 3), dtype=np.uint8).reshape(h, w, 3)

    def ram(self, offset=0, length=0x100000):
        self._send(f"ram {offset:x} {length:x}")
        head = self._line().split()
        assert head[0] == "ram", head
        return self._read(int(head[1]))

    def mem(self, address, length):
        """By the i960's address: the work RAM (00500000) or the scratch RAM (00200000)."""
        self._send(f"mem {address:x} {length:x}")
        head = self._line().split()
        assert head[0] == "mem", head
        return self._read(int(head[1]))

    def save(self, path):
        self._send(f"save {path}")
        return self._line() == "ok"

    def load(self, path):
        self._send(f"load {path}")
        return self._line() == "ok"

    def press(self, bit, frames=30):
        """Hold an IN0 button (COIN, START) for some frames, then release it."""
        self.step(frames, in0=0xFF & ~bit)
        self.step(10)

    def close(self):
        try:
            self._send("quit")
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()
