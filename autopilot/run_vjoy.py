"""The autopilot on its own: sm2-emu in its window, driven through vJoy like a
player's wheel, from the telemetry it serves.

usage: python run_vjoy.py [--course oval|highland|bayside] [--policy SPEC]
                          [--state FILE] [--exe PATH] [--port 9100] [--log DIR] [--bff]

  --policy  pursuit | line:PARAMS.json (line_opt.py's best.json) | ppo:CKPT.pt
            | visual:MODEL.pt (bc_train.py --visual: drives from the window's
            picture alone) | q:MODEL.pt (q_train.py: chooses the planner's
            candidates by learnt scores, from telemetry or the picture)
  --state   start from this save state (states/indy_oval_mt.state: the
            oval's start), else coins, start, course and gearbox are
            chosen through vJoy as a player would
Logs, in --log (runs/vjoy by default): race.csv (a line a frame: the car,
the controls, the drive-board bytes and the force feedback packets of that
frame) and summary.txt.

The emulator runs with its own settings (a copy of build/bin/sm2-emu.ini,
with the background input, the force feedback and the network outputs on)
and its own NVRAM (a copy of states/nvram), never the player's.

Refuses to start while BackForceFeeder runs: vJoy is then BFF's, and the
drive-board commands of --net-outputs would reach the cabinet's motor.

--bff: the other way, on the cabinet. BackForceFeeder runs with
bff/autopilot.lua loaded and keeps vJoy and the motor; the autopilot sends
it the controls over UDP and gets back the drive board bytes and the torque.
The emulator runs with the player's own settings (BFF's network outputs).
The motor then turns the wheel with the game's force feedback while the
autopilot drives. Not tested yet."""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import indy500_telemetry as T  # noqa: E402
from pursuit import Line, Pursuit  # noqa: E402
from vjoy import AXIS_X, AXIS_Y, AXIS_Z, VJoy, bff_running  # noqa: E402

HERE = Path(__file__).parent
REPO = HERE.parent
ROMS = Path(os.environ.get("SM2_ROMS", r"D:\racecab\MAME ROMs (merged)"))
MAPS = {"stcc": "stcc_country_line.npz",           # Touring Car: Country Circuit only, for now
        "srallyc": "srally_desert_line.npz",       # Sega Rally: the Desert only, for now
        "daytona": "daytona_beginner_line.npz",    # Daytona USA: the beginner course only, for now
        "Indy 500": "indy_oval_line.npz", "Highland Raceway": "indy_highland_line.npz",
        "Bay Side Street": "indy_bayside_line.npz"}
COURSE_MOVES = {"oval": 0.0, "bayside": 0.6, "highland": -0.6}
AXES = [AXIS_X, AXIS_Y, AXIS_Z]


def read_ini(path):
    keys = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if line and not line.startswith(("#", ";", "[")) and "=" in line:
            k, v = line.split("=", 1)
            keys[k.strip()] = v.strip()
    return keys


def write_ini(src, dst, overrides):
    out, seen = [], set()
    for line in Path(src).read_text(encoding="utf-8", errors="replace").splitlines():
        key = line.split("=", 1)[0].strip() if "=" in line and not line.lstrip().startswith("#") else None
        if key in overrides:
            out.append(f"{key} = {overrides[key]}")
            seen.add(key)
        else:
            out.append(line)
    out += [f"{k} = {v}" for k, v in overrides.items() if k not in seen]
    Path(dst).write_text("\n".join(out) + "\n", encoding="utf-8")


class Controls:
    """The game's controls on vJoy, as the emulator's settings map them."""

    def __init__(self, vj, ini):
        self.vj = vj
        g = lambda k, d: ini.get(k, d)  # noqa: E731
        self.steer_axis = AXES[int(g("wheel_steer_axis", "0"))]
        self.accel = (AXES[int(g("wheel_accel_axis", "1"))], g("wheel_accel_invert", "false") == "true",
                      g("wheel_accel_half", "false") == "true")
        self.brake = (AXES[int(g("wheel_brake_axis", "2"))], g("wheel_brake_invert", "false") == "true",
                      g("wheel_brake_half", "false") == "true")
        self.buttons = {r: int(g(f"wheel_button_{r}", "-1")) + 1 for r in ("coin", "start", "gear_up", "gear_down")}

    def _pedal(self, spec, value):
        usage, invert, half = spec
        if half:
            frac = 0.5 - 0.5 * value if invert else 0.5 + 0.5 * value
        else:
            frac = 1 - value if invert else value
        self.vj.axis(usage, frac)

    @staticmethod
    def _byte(value, centred=False):
        """The 8-bit value agent mode would give the game, computed as it does
        (the control as text with 4 decimals, then single precision:
        src/agent.cpp), returned as the middle of its interval: what the
        emulator reads off the axis then rounds to that same byte, whatever
        vJoy and SDL do to the last bits."""
        f32 = np.float32
        v = f32(float(f"{float(value):.4f}"))
        fraction = f32(0.5) + f32(0.5) * v if centred else v
        fraction = min(max(fraction, f32(0.0)), f32(1.0))
        byte = int(f32(fraction * f32(255)) + f32(0.5))
        return byte / 255

    def set(self, steer, accel, brake):
        self.vj.axis(self.steer_axis, self._byte(np.clip(steer, -1, 1), centred=True))
        self._pedal(self.accel, self._byte(np.clip(accel, 0, 1)))
        self._pedal(self.brake, self._byte(np.clip(brake, 0, 1)))

    def press(self, role, seconds=0.8):
        b = self.buttons[role]
        if b <= 0:
            raise RuntimeError(f"no wheel button for {role} in the settings")
        self.vj.button(b, True)
        time.sleep(seconds)
        self.vj.button(b, False)


class BffControls:
    """The same controls, sent to BackForceFeeder's bff/autopilot.lua."""

    ROLES = {"coin": 1, "start": 2, "gear_up": 4, "gear_down": 8}

    def __init__(self, port_out=8765, port_in=8766):
        self.out = ("127.0.0.1", port_out)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx.bind(("127.0.0.1", port_in))
        self.rx.setblocking(False)
        self.state, self.mask = (0.0, 0.0, 0.0), 0

    def _send(self):
        s, a, b = self.state
        self.sock.sendto(f"AP {s:.4f} {a:.4f} {b:.4f} {self.mask}".encode(), self.out)

    def set(self, steer, accel, brake):
        self.state = (float(np.clip(steer, -1, 1)), float(np.clip(accel, 0, 1)), float(np.clip(brake, 0, 1)))
        self._send()

    def press(self, role, seconds=0.8):
        self.mask |= self.ROLES[role]
        t0 = time.time()
        while time.time() - t0 < seconds:              # the bridge lets go after 300 ms of silence
            self._send(); time.sleep(0.05)
        self.mask &= ~self.ROLES[role]
        self._send()

    def take(self):
        """What BFF sent back: drive board bytes, and torque lines."""
        drive, torque = [], []
        while True:
            try:
                msg = self.rx.recv(256).decode(errors="replace").split()
            except BlockingIOError:
                break
            if msg and msg[0] == "DRV":
                drive.append(int(msg[1]))
            elif msg and msg[0] == "TQ":
                torque.append(f"torque {msg[1]} wheel {msg[2]}")
        return drive, torque


class Telemetry:
    """sm2-emu's telemetry server: the latest frame, read on a thread. With the
    emulator's --telemetry-sync, each frame waits for ack(); until manual is
    set, the thread acknowledges every frame itself."""

    def __init__(self, port, manual=False):
        self.port, self.latest, self.count = port, None, 0
        self._stop = False
        self.sock, self.manual = None, manual
        threading.Thread(target=self._run, daemon=True).start()

    def ack(self):
        if self.sock is not None:
            try:
                self.sock.sendall(b"go\n")
            except OSError:
                pass

    def _run(self):
        while not self._stop:
            try:
                s = socket.create_connection(("127.0.0.1", self.port), timeout=2)
            except OSError:
                time.sleep(0.5)
                continue
            buf = b""
            s.settimeout(1.0)
            self.sock = s
            while not self._stop:
                try:
                    chunk = s.recv(1 << 16)
                except socket.timeout:
                    continue
                except OSError:
                    break
                if not chunk:
                    break
                buf += chunk
                *lines, buf = buf.split(b"\n")
                if lines:
                    self.latest = json.loads(lines[-1])
                    self.count += len(lines)
                    if not self.manual:
                        self.ack()


class DriveBytes:
    """The drive-board bytes from sm2-emu's network outputs (MAME's format)."""

    def __init__(self, port=8000):
        self.port, self.pending = port, []
        self._lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        while True:
            try:
                s = socket.create_connection(("127.0.0.1", self.port), timeout=2)
            except OSError:
                time.sleep(0.5)
                continue
            buf = b""
            while True:
                try:
                    chunk = s.recv(4096)
                except OSError:
                    break
                if not chunk:
                    break
                buf += chunk
                *lines, buf = buf.replace(b"\r", b"\n").split(b"\n")
                for line in lines:
                    name, _, value = line.decode(errors="replace").partition("=")
                    if name.strip() == "RawDrive":
                        with self._lock:
                            self.pending.append(int(value))

    def take(self):
        with self._lock:
            out, self.pending = self.pending, []
        return out


def stcc_menus(ctl, tel):
    """Touring Car: coins (1 a credit), start, the car with the automatic
    gearbox (the manual starts in 1st and wants the paddles), the name as it
    is, up to the qualifying lap."""
    time.sleep(28)
    for _ in range(2):
        ctl.press("coin")
        time.sleep(0.4)
    time.sleep(1.0)
    ctl.press("start", 1.0)
    time.sleep(2.5)
    ctl.set(0, 1.0, 0.0); time.sleep(0.6)
    ctl.set(0, 0, 0)
    t0 = time.time()
    while time.time() - t0 < 30:
        d = tel.latest
        if d and 0 < (d.get("time_left") or 0) < 40.5 * 64 / 57.5:
            break
        ctl.set(0, 1.0, 0); time.sleep(0.4); ctl.set(0, 0.0, 0); time.sleep(0.4)


def srally_menus(ctl, tel):
    """Sega Rally: 2 coins a credit, start, Championship, the Celica automatic
    (the first of each screen: the gas confirms), up to the Desert's start."""
    time.sleep(28)
    for _ in range(2):
        ctl.press("coin")
        time.sleep(0.4)
    time.sleep(1.0)
    ctl.press("start", 1.0)
    for _ in range(3):                          # SELECT GAME, CAR SELECT, and one more for luck
        time.sleep(2.0)
        ctl.set(0, 1.0, 0); time.sleep(0.6); ctl.set(0, 0, 0)


def daytona_menus(ctl, tel):
    """Daytona USA: coins held a second each, start, the beginner course and the
    automatic (the first of each screen: the gas confirms)."""
    time.sleep(28)
    for _ in range(2):
        ctl.press("coin", 1.2)
        time.sleep(0.5)
    time.sleep(1.0)
    ctl.press("start", 1.2)
    for _ in range(3):                          # CIRCUIT SELECT, MISSION SELECT, and one more
        time.sleep(2.5)
        ctl.set(0, 1.0, 0); time.sleep(0.6); ctl.set(0, 0, 0)


def make_policy(spec, line, pid=None):
    kind, _, arg = spec.partition(":")
    if kind == "visual":
        from visual_driver import VisualDriver
        return VisualDriver(arg, pid=pid)
    if kind == "q":
        from policy_q import QDriver
        from visual_driver import grab, window_of
        win = {}

        def picture():
            win.setdefault("hwnd", window_of(pid))
            return grab(win["hwnd"])
        return QDriver(line, arg, image_source=picture)
    if kind == "pursuit":
        return Pursuit(line, gain=float(arg or 3.0))
    if kind == "line":
        from line_opt import LineDriver
        p = json.loads(Path(arg).read_text())
        return LineDriver(line, p["x"], p["knots"])
    if kind == "ppo":
        from policy_ppo import PpoDriver
        return PpoDriver(line, arg)
    raise SystemExit(f"unknown policy {spec}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--course", default="oval", choices=list(COURSE_MOVES))
    ap.add_argument("--policy", default="pursuit")
    ap.add_argument("--state")
    ap.add_argument("--exe", default=str(REPO / "build" / "bin" / "sm2-emu.exe"))
    ap.add_argument("--port", type=int, default=9100)
    ap.add_argument("--log", default=str(HERE / "runs" / "vjoy"))
    ap.add_argument("--manual", action="store_true", default=True, help="the 6-speed gearbox (default)")
    ap.add_argument("--bff", action="store_true", help="through BackForceFeeder, on the cabinet")
    ap.add_argument("--game", default="indy500", choices=["indy500", "stcc", "srallyc", "daytona"])
    ap.add_argument("--sync", action="store_true",
                    help="lockstep: each frame waits until the controls computed from it are set")
    ap.add_argument("--max-seconds", type=float, default=0.0,
                    help="stop after this long (Touring Car: its qualifying, grid and race; default 330)")
    args = ap.parse_args()
    ROM = ROMS / f"{args.game}.zip"
    if args.game == "stcc" and not args.max_seconds:
        args.max_seconds = 330.0
    if args.game in ("srallyc", "daytona") and not args.max_seconds:
        args.max_seconds = 150.0
    tel_module = __import__({"stcc": "stcc_telemetry", "srallyc": "srally_telemetry",
                             "daytona": "daytona_telemetry"}.get(args.game,
                                                                                         "indy500_telemetry"))

    logdir = Path(args.log).resolve()      # the emulator runs from its own directory
    (logdir / "cfg").mkdir(parents=True, exist_ok=True)
    if args.bff:
        if not bff_running():
            sys.exit("--bff: BackForceFeeder is not running (with bff/autopilot.lua loaded)")
        if args.state:
            sys.exit("--bff runs the player's settings (its sound core): start from the menus, without --state")
        print("--bff: the cabinet's motor will turn the wheel with the game's force feedback", flush=True)
        ctl, vj, drive = BffControls(), None, None
        cmd = [args.exe, "--telemetry", str(args.port), "--game", args.game, str(ROM)]
    else:
        if bff_running():
            sys.exit("BackForceFeeder is running: it owns vJoy and the cabinet's motor. Close it first, "
                     "or use --bff.")
        nvram = logdir / "nvram"
        shutil.rmtree(nvram, ignore_errors=True)
        shutil.copytree(HERE / "states" / "nvram", nvram)
        user_ini = Path(args.exe).parent / "sm2-emu.ini"
        write_ini(user_ini, logdir / "cfg" / "sm2-emu.ini",
                  {"input_background": "true", "wheel_ffb": "true", "outputs_network": "true",
                   "outputs_network_port": "8000",
                   "scsp_core": "mame"})          # the save states of agent mode are on the MAME core
        ini = read_ini(logdir / "cfg" / "sm2-emu.ini")
        vj = VJoy(1)
        vj.acquire()
        vj.log_ffb()
        ctl = Controls(vj, ini)
        drive = DriveBytes(int(ini.get("outputs_network_port", "8000")))
        cmd = [args.exe, "--config", str(logdir / "cfg"), "--nvram", str(nvram), "--telemetry", str(args.port),
               "--net-outputs", "--game", args.game, str(ROM)]
    if args.sync:
        cmd[1:1] = ["--telemetry-sync"]
    for s in (0.3, -0.3, 0.0):                  # moved once: the emulator ignores an axis never moved
        ctl.set(s, 0.5, 0.5)
        time.sleep(0.05)
    ctl.set(0.0, 0.0, 0.0)
    ready = None
    if args.state:
        cmd[1:1] = ["--load-state", str(Path(args.state).resolve())]
        # the driver ready before the first frame (lockstep: no frame waits on it)
        line0 = Line(HERE / "runs" / MAPS[args.game if args.game in MAPS else "Indy 500"])
        ready = (make_policy(args.policy, line0), line0)
    env = dict(os.environ, SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS="1")
    emu = subprocess.Popen(cmd, cwd=str(Path(args.exe).parent), env=env)
    tel = Telemetry(args.port, manual=args.sync and bool(args.state))   # from a state: the race loop acknowledges
    csvf = open(logdir / "race.csv", "w", encoding="utf-8")
    csvf.write("time,frame,lap,progress,speed,position,time_left,steer,accel,brake,inputs,drive,ffb\n")
    summary = []
    try:
        if not args.state and args.game == "stcc":
            stcc_menus(ctl, tel)
        elif not args.state and args.game == "srallyc":
            srally_menus(ctl, tel)
        elif not args.state and args.game == "daytona":
            daytona_menus(ctl, tel)
        elif not args.state:
            time.sleep(28)                                    # boot, attract
            for _ in range(4):
                ctl.press("coin"); time.sleep(0.4)
            time.sleep(1.0)
            ctl.press("start", 1.0)
            time.sleep(3.0)
            target = COURSE_MOVES[args.course]
            for k in range(1, 41):                            # the menus ignore a wheel that jumps
                ctl.set(target * k / 40, 0, 0); time.sleep(1 / 57.5)
            time.sleep(0.4)
            ctl.set(target, 1.0, 0.0); time.sleep(0.6); ctl.set(target, 0.0, 0.0)
            for k in range(39, -1, -1):
                ctl.set(target * k / 40, 0, 0); time.sleep(1 / 57.5)
            if args.manual:
                time.sleep(1.8)
                for k in range(1, 41):
                    ctl.set(0.6 * k / 40, 0, 0); time.sleep(1 / 57.5)
                time.sleep(0.4)
                ctl.set(0.6, 1.0, 0.0); time.sleep(0.6); ctl.set(0.6, 0.0, 0.0)
                for k in range(39, -1, -1):
                    ctl.set(0.6 * k / 40, 0, 0); time.sleep(1 / 57.5)
            # through the intros, pulsing the gas, up to the race
            t0 = time.time()
            while time.time() - t0 < 40:
                d = tel.latest
                if d and d.get("racing") and (d.get("time_left") or 99) < 64:
                    break
                ctl.set(0, 1.0, 0); time.sleep(0.4); ctl.set(0, 0.0, 0); time.sleep(0.4)
        # the race
        tel.manual = args.sync
        policy, line = ready if ready else (None, None)
        last_frame, last_lap, lap_t0 = -1, None, time.time()
        t_start = time.time()
        while emu.poll() is None:
            d = tel.latest
            if d is None or d["frame"] == last_frame:
                time.sleep(0.002)
                continue
            last_frame = d["frame"]
            if line is None:
                mapf = MAPS.get(args.game if args.game in ("stcc", "srallyc", "daytona") else d["track"])
                if mapf is None:
                    continue
                line = Line(HERE / "runs" / mapf)
                policy = make_policy(args.policy, line, emu.pid)
            t = tel_module.from_json(d)
            steer, accel, brake = policy(t)[:3]
            ctl.set(steer, accel, brake)
            if args.sync:
                # vJoy to the driver and SDL, before the emulator reads it: longer the first time
                time.sleep(0.1 if last_lap is None else 0.003)
                tel.ack()
            if args.bff:
                db, fb = ctl.take()
            else:
                db, fb = drive.take(), [txt for _, txt in vj.take_ffb()]
            p = d["player"]
            csvf.write(f"{time.time() - t_start:.3f},{d['frame']},{p['lap']},{p['progress']:.4f},{p['speed']:.2f},"
                       f"{p['position']},{d['time_left']},{steer:.3f},{accel:.2f},{brake:.2f},"
                       f"{' '.join(str(v) for v in d.get('inputs', []))},"
                       f"{' '.join(f'{b:02X}' for b in db)},{' | '.join(fb)}\n")
            if last_lap is not None and p["lap"] != last_lap:
                msg = (f"lap {last_lap} done in {time.time() - lap_t0:.2f} s (wall clock), position {p['position']}, "
                       f"time left {d['time_left']:.1f}")
                print(msg, flush=True); summary.append(msg)
                lap_t0 = time.time()
            last_lap = p["lap"]
            if args.max_seconds:
                if time.time() - t_start > args.max_seconds:
                    summary.append("stopped after --max-seconds")
                    break
            elif d.get("finished") or (not d.get("racing") and time.time() - t_start > 10):
                msg = f"race over: finished={d.get('finished')} position {p['position']}"
                print(msg, flush=True); summary.append(msg)
                time.sleep(5)
                break
    finally:
        ctl.set(0.0, 0.0, 0.0)
        if vj is not None:
            vj.release()
        csvf.close()
        (logdir / "summary.txt").write_text("\n".join(summary) + "\n", encoding="utf-8")
        if emu.poll() is None:
            emu.terminate()


if __name__ == "__main__":
    main()
