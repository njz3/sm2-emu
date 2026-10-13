"""vJoy from Python (vJoyInterface.dll, ctypes): a device's axes and buttons, and
the force feedback a game sends to it, logged.

Never while BackForceFeeder runs: it owns vJoy then and drives the cabinet's
motor with what the game sends. bff_running() says whether it does."""
import ctypes
import subprocess
import threading
import time
from ctypes import wintypes

DLL = r"C:\Program Files\vJoy\x64\vJoyInterface.dll"

AXIS_X, AXIS_Y, AXIS_Z = 0x30, 0x31, 0x32
AXIS_MAX = 0x8000
STAT = {0: "owned", 1: "free", 2: "busy", 3: "missing", 4: "unknown"}

# FFB packet types and effect types (vJoy SDK, public.h)
PT = {0x01: "effect", 0x02: "envelope", 0x03: "condition", 0x04: "periodic", 0x05: "constant",
      0x06: "ramp", 0x07: "custom", 0x08: "sample", 0x0A: "operation", 0x0B: "free", 0x0C: "control",
      0x0D: "gain", 0x0E: "custom-set", 0x11: "new", 0x12: "block-load", 0x13: "pool"}
ET = {0: "none", 1: "constant", 2: "ramp", 3: "square", 4: "sine", 5: "triangle", 6: "sawtooth-up",
      7: "sawtooth-down", 8: "spring", 9: "damper", 10: "inertia", 11: "friction", 12: "custom"}
OP = {1: "start", 2: "solo", 3: "stop"}


class EffConstant(ctypes.Structure):
    _fields_ = [("EffectBlockIndex", ctypes.c_ubyte), ("Magnitude", ctypes.c_long)]


class EffReport(ctypes.Structure):
    _fields_ = [("EffectBlockIndex", ctypes.c_ubyte), ("EffectType", ctypes.c_int), ("Duration", ctypes.c_ushort),
                ("TrigerRpt", ctypes.c_ushort), ("SamplePrd", ctypes.c_ushort), ("Gain", ctypes.c_ubyte),
                ("TrigerBtn", ctypes.c_ubyte), ("Polar", wintypes.BOOL), ("Direction", ctypes.c_ubyte),
                ("DirY", ctypes.c_ubyte)]


class EffOp(ctypes.Structure):
    _fields_ = [("EffectBlockIndex", ctypes.c_ubyte), ("EffectOp", ctypes.c_int), ("LoopCount", ctypes.c_ubyte)]


class EffCond(ctypes.Structure):
    _fields_ = [("EffectBlockIndex", ctypes.c_ubyte), ("isY", wintypes.BOOL), ("CenterPointOffset", ctypes.c_long),
                ("PosCoeff", ctypes.c_long), ("NegCoeff", ctypes.c_long), ("PosSatur", ctypes.c_ulong),
                ("NegSatur", ctypes.c_ulong), ("DeadBand", ctypes.c_long)]


class EffPeriod(ctypes.Structure):
    _fields_ = [("EffectBlockIndex", ctypes.c_ubyte), ("Magnitude", ctypes.c_ulong), ("Offset", ctypes.c_long),
                ("Phase", ctypes.c_ulong), ("Period", ctypes.c_ulong)]


FFB_CB = ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)


def bff_running():
    out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True).stdout
    return any(line.lower().startswith('"backforcefeeder') for line in out.splitlines())


class VJoy:
    def __init__(self, device=1):
        self.dll = ctypes.WinDLL(DLL)
        self.dev = device
        self.ffb = []                      # (time, text) for each FFB packet, from vJoy's thread
        self._lock = threading.Lock()
        self._cb = None

    def status(self):
        return STAT.get(self.dll.GetVJDStatus(self.dev), "?")

    def acquire(self):
        if bff_running():
            raise RuntimeError("BackForceFeeder is running: vJoy is its own (and the cabinet's motor)")
        if self.status() not in ("free", "owned"):
            raise RuntimeError(f"vJoy device {self.dev} is {self.status()}")
        if not self.dll.AcquireVJD(self.dev):
            raise RuntimeError(f"cannot acquire vJoy device {self.dev}")
        self.dll.ResetVJD(self.dev)

    def release(self):
        self.dll.ResetVJD(self.dev)
        self.dll.RelinquishVJD(self.dev)

    def axis(self, usage, fraction):
        """fraction 0..1 of the axis' travel."""
        v = int(round(1 + max(0.0, min(1.0, fraction)) * (AXIS_MAX - 1)))
        self.dll.SetAxis(v, self.dev, usage)

    def button(self, number, pressed):
        """number: vJoy's, from 1."""
        self.dll.SetBtn(bool(pressed), self.dev, number)

    # -- force feedback ------------------------------------------------------
    def log_ffb(self):
        """Record every FFB packet the game sends to the device (self.ffb)."""
        dll = self.dll

        def cb(data, _user):
            try:
                t = ctypes.c_int()
                if dll.Ffb_h_Type(ctypes.c_void_p(data), ctypes.byref(t)) != 0:
                    return
                kind = PT.get(t.value, hex(t.value))
                text = kind
                if t.value == 0x05:
                    e = EffConstant()
                    if dll.Ffb_h_Eff_Constant(ctypes.c_void_p(data), ctypes.byref(e)) == 0:
                        m = e.Magnitude & 0xFFFF            # the report's 16 bits, signed: -10000..10000
                        text = f"constant ebi={e.EffectBlockIndex} magnitude={m - 0x10000 if m & 0x8000 else m}"
                elif t.value == 0x01:
                    e = EffReport()
                    if dll.Ffb_h_Eff_Report(ctypes.c_void_p(data), ctypes.byref(e)) == 0:
                        text = (f"effect ebi={e.EffectBlockIndex} type={ET.get(e.EffectType, e.EffectType)} "
                                f"duration={e.Duration} gain={e.Gain} dir={e.Direction}")
                elif t.value == 0x0A:
                    e = EffOp()
                    if dll.Ffb_h_EffOp(ctypes.c_void_p(data), ctypes.byref(e)) == 0:
                        text = f"operation ebi={e.EffectBlockIndex} {OP.get(e.EffectOp, e.EffectOp)} loops={e.LoopCount}"
                elif t.value == 0x03:
                    e = EffCond()
                    if dll.Ffb_h_Eff_Cond(ctypes.c_void_p(data), ctypes.byref(e)) == 0:
                        text = (f"condition ebi={e.EffectBlockIndex} y={int(bool(e.isY))} centre={e.CenterPointOffset} "
                                f"pos={e.PosCoeff} neg={e.NegCoeff} sat={e.PosSatur}/{e.NegSatur} dead={e.DeadBand}")
                elif t.value == 0x04:
                    e = EffPeriod()
                    if dll.Ffb_h_Eff_Period(ctypes.c_void_p(data), ctypes.byref(e)) == 0:
                        text = (f"periodic ebi={e.EffectBlockIndex} magnitude={e.Magnitude} offset={e.Offset} "
                                f"period={e.Period}")
                elif t.value == 0x0D:
                    g = ctypes.c_ubyte()
                    if dll.Ffb_h_DevGain(ctypes.c_void_p(data), ctypes.byref(g)) == 0:
                        text = f"gain {g.value}"
                with self._lock:
                    self.ffb.append((time.time(), text))
            except Exception as exc:          # never let an exception into vJoy's thread
                with self._lock:
                    self.ffb.append((time.time(), f"error {exc}"))

        self._cb = FFB_CB(cb)
        self.dll.FfbRegisterGenCB(self._cb, None)

    def take_ffb(self):
        with self._lock:
            out, self.ffb = self.ffb, []
        return out
