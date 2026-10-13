"""The picture-only driver: bc_train.py --visual's network, fed from the
emulator's window (a capture of its client area), for run_vjoy.py
--policy visual:MODEL.pt and for agent mode (eval_visual)."""
import ctypes
from ctypes import wintypes

import numpy as np
import torch
from PIL import Image, ImageGrab

from bc_train import VisualPolicy

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.SetProcessDPIAware()


def window_of(pid):
    """The biggest visible window of a process."""
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        p = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd):
            r = wintypes.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(r))
            found.append(((r.right - r.left) * (r.bottom - r.top), hwnd))
        return True

    user32.EnumWindows(cb, 0)
    return max(found)[1] if found else None


def grab(hwnd):
    r = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    pt = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    box = (pt.x, pt.y, pt.x + r.right, pt.y + r.bottom)
    return ImageGrab.grab(bbox=box, all_screens=True)


def to_input(img):
    """A picture (PIL or H x W x 3) as the network's grey 64 x 48, 0..1: the
    mean of R, G and B over boxes, as the agent mode's pictures it learnt on."""
    if isinstance(img, Image.Image):
        img = np.asarray(img.convert("RGB").resize((64, 48), Image.BOX))
    elif img.shape[:2] != (48, 64):
        img = np.asarray(Image.fromarray(img).resize((64, 48), Image.BOX))
    return img.astype(np.float32).mean(axis=2) / 255.0


class VisualDriver:
    def __init__(self, model, pid=None, source=None):
        self.net = VisualPolicy()
        self.net.load_state_dict(torch.load(model, map_location="cpu")["visual"])
        self.net.eval()
        self.pid, self.hwnd, self.source = pid, None, source
        self.prev = None

    def frame(self):
        if self.source is not None:            # agent mode: the emulator's own picture
            return to_input(self.source())
        if self.hwnd is None:
            self.hwnd = window_of(self.pid)
        return to_input(grab(self.hwnd))

    def __call__(self, t):
        cur = self.frame()
        prev = self.prev if self.prev is not None else cur
        self.prev = cur
        x = torch.as_tensor(np.stack([prev, cur])[None])
        speed = torch.tensor([[t.player.speed / 100.0 if t is not None else 0.0]], dtype=torch.float32)
        with torch.no_grad():
            steer, pedal = self.net(x, speed)[0].numpy()
        return float(steer), float(max(pedal, 0.0)), float(max(-pedal, 0.0))
