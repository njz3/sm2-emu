"""Learn to drive from the planner's races (plan_race.py demo.npz files):
behaviour cloning, PyTorch.

usage: python bc_train.py OUT.pt DEMO_DIR... [--visual] [--epochs 60]

Without --visual: the PPO policy's network (ppo_model.Policy) learns the
planner's actions from the telemetry features; OUT.pt is a checkpoint
train_ppo.py --resume and policy_ppo.py read.
--visual: a network that sees only the picture (64 x 48, grey, the last two
frames) and the speed, and gives the steering and the pedals themselves
(-1..1 steering, -1..1 brake..accelerator), for the games or setups without
telemetry; OUT.pt holds it for visual_driver.py."""
import argparse
import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

sys.path.insert(0, str(Path(__file__).parent))
from env_indy import OBS_DIM  # noqa: E402
from ppo_model import Policy  # noqa: E402


class VisualPolicy(nn.Module):
    """Two grey frames, 64 x 48, and the speed: the steering, and the pedals
    (1 full accelerator, -1 full brake)."""

    def __init__(self):
        super().__init__()
        self.cnn = nn.Sequential(nn.Conv2d(2, 32, 5, 2), nn.ReLU(), nn.Conv2d(32, 64, 5, 2), nn.ReLU(),
                                 nn.Conv2d(64, 64, 3, 2), nn.ReLU(), nn.Flatten())
        with torch.no_grad():
            n = self.cnn(torch.zeros(1, 2, 48, 64)).shape[1]
        self.head = nn.Sequential(nn.Linear(n + 1, 256), nn.ReLU(), nn.Linear(256, 2))

    def forward(self, img, speed):
        return torch.tanh(self.head(torch.cat([self.cnn(img), speed], dim=1)))


def load(dirs, visual):
    vec, act, img = [], [], []
    for d in dirs:
        f = Path(d) / "demo.npz"
        if not f.exists() or (visual and "ctl" not in np.load(f)):
            continue
        r = np.load(f)
        vec.append(r["vec"])
        if visual:
            c = r["ctl"]                                       # steer, accel, brake
            act.append(np.stack([c[:, 0], c[:, 1] - c[:, 2]], axis=1).astype(np.float32))
        else:
            act.append(r["act"])
        if visual:
            im = r["img"].astype(np.float32) / 255.0
            prev = np.concatenate([im[:1], im[:-1]])          # the frame before (the first: itself)
            img.append(np.stack([prev, im], axis=1))
    vec, act = np.concatenate(vec), np.concatenate(act)
    return vec, act, (np.concatenate(img) if visual else None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out"); ap.add_argument("demos", nargs="+")
    ap.add_argument("--visual", action="store_true")
    ap.add_argument("--epochs", type=int, default=60)
    args = ap.parse_args()
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    vec, act, img = load(args.demos, args.visual)
    n = len(act)
    print(f"{n} decisions from {len(args.demos)} races", flush=True)
    rng = np.random.default_rng(0)
    idx = rng.permutation(n)
    val, tr = idx[: n // 10], idx[n // 10:]
    V = torch.as_tensor(vec, device=dev); A = torch.as_tensor(act, device=dev)
    I = torch.as_tensor(img, device=dev) if args.visual else None
    if args.visual:
        net = VisualPolicy().to(dev)
        speed = V[:, :1]                                      # the features' first: speed / 100
        fwd = lambda b: net(I[b], speed[b])                   # noqa: E731
    else:
        net = Policy(OBS_DIM).to(dev)
        fwd = lambda b: net(V[b])[0]                          # noqa: E731
    opt = torch.optim.Adam(net.parameters(), lr=1e-3)
    for epoch in range(args.epochs):
        net.train()
        perm = torch.as_tensor(rng.permutation(tr), device=dev)
        for s in range(0, len(perm), 256):
            b = perm[s:s + 256]
            loss = ((fwd(b) - A[b]) ** 2).mean()
            opt.zero_grad(); loss.backward(); opt.step()
        if epoch % 10 == 9 or epoch == args.epochs - 1:
            net.eval()
            with torch.no_grad():
                vb = torch.as_tensor(val, device=dev)
                vl = ((fwd(vb) - A[vb]) ** 2).mean().item()
                tl = ((fwd(torch.as_tensor(tr[:5000], device=dev)) - A[torch.as_tensor(tr[:5000], device=dev)]) ** 2).mean().item()
            print(f"epoch {epoch + 1}: train {tl:.4f}  validation {vl:.4f}", flush=True)
    if args.visual:
        torch.save({"visual": net.state_dict()}, args.out)
    else:
        # as a PPO checkpoint, its exploration where PPO starts it
        torch.save({"net": net.state_dict(), "update": 0}, args.out)
    print("saved", args.out)


if __name__ == "__main__":
    main()
