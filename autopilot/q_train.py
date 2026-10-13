"""Learn to score the planner's candidates (plan_race.py qdata.npz): at each
decision the planner tried every candidate with the emulator and scored it;
a network learns those scores from what the car sees, and then chooses alone
(policy_q.py), without the emulator's look-ahead.

usage: python q_train.py OUT.pt PLAN_DIR... [--visual] [--epochs 200]
  without --visual: from the telemetry features (env_indy.Pilot)
  --visual: from the picture (64 x 48, grey) and the speed only
The network learns the planner's preferences: a softmax of each decision's
scores, TEMPERATURE points apart (cross-entropy); its largest output is its
choice."""
import argparse
import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

sys.path.insert(0, str(Path(__file__).parent))
from env_indy import OBS_DIM  # noqa: E402

SCALE = 100.0
TEMPERATURE = 25.0            # points: a place is worth 40, a metre 1


class QNet(nn.Module):
    def __init__(self, n_actions, visual=False):
        super().__init__()
        self.visual = visual
        if visual:
            self.cnn = nn.Sequential(nn.Conv2d(1, 32, 5, 2), nn.ReLU(), nn.Conv2d(32, 64, 5, 2), nn.ReLU(),
                                     nn.Conv2d(64, 64, 3, 2), nn.ReLU(), nn.Flatten())
            with torch.no_grad():
                n = self.cnn(torch.zeros(1, 1, 48, 64)).shape[1]
            self.head = nn.Sequential(nn.Linear(n + 1, 256), nn.ReLU(), nn.Linear(256, n_actions))
        else:
            self.head = nn.Sequential(nn.Linear(OBS_DIM, 256), nn.ReLU(), nn.Linear(256, 256), nn.ReLU(),
                                      nn.Linear(256, n_actions))

    def forward(self, vec, img=None):
        if self.visual:
            return self.head(torch.cat([self.cnn(img), vec[:, :1]], dim=1))   # the picture and the speed
        return self.head(vec)


def load(dirs, visual):
    vec, sc, img = [], [], []
    for d in dirs:
        f = Path(d) / "qdata.npz"
        if not f.exists():
            continue
        r = np.load(f)
        if visual and "img" not in r:
            continue
        s = r["scores"].astype(np.float32)
        rel = s - s.max(axis=1, keepdims=True)        # each decision's scores from its best, in points
        vec.append(r["vec"]); sc.append(np.clip(rel, -3000, 0) / SCALE)
        if visual:
            img.append(r["img"][:, None].astype(np.float32) / 255.0)
    return np.concatenate(vec), np.concatenate(sc), (np.concatenate(img) if visual else None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out"); ap.add_argument("plans", nargs="+")
    ap.add_argument("--visual", action="store_true")
    ap.add_argument("--epochs", type=int, default=200)
    ap.add_argument("--set", default="pass", choices=["pass", "base"], help="plan_race.py's candidates scored")
    ap.add_argument("--path", help="a race's decisions to reproduce (replay_win.py's decisions.npz)")
    ap.add_argument("--path-weight", type=float, default=20.0)
    args = ap.parse_args()
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    vec, sc, img = load(args.plans, args.visual)
    n, k = sc.shape
    print(f"{n} decisions, {k} candidates", flush=True)
    rng = np.random.default_rng(0)
    idx = rng.permutation(n)
    val, tr = idx[: max(1, n // 10)], idx[max(1, n // 10):]
    if args.path:                                     # the race to reproduce: its choices, hard
        d = np.load(args.path)
        k_path = len(d["index"])
        hard = np.full((k_path, k), -3000.0 / SCALE, dtype=np.float32)
        hard[np.arange(k_path), d["index"]] = 0.0
        vec = np.concatenate([vec, d["vec"]]); sc = np.concatenate([sc, hard])
        if args.visual:
            img = np.concatenate([img, d["img"][:, None].astype(np.float32) / 255.0])
        path_rows = np.arange(n, n + k_path)
        tr = np.concatenate([tr] + [path_rows] * int(args.path_weight))    # seen path_weight times an epoch
        n += k_path
        print(f"+ {k_path} decisions of the race to reproduce, weight {args.path_weight:g}", flush=True)
    else:
        path_rows = None
    V, S = torch.as_tensor(vec, device=dev), torch.as_tensor(sc, device=dev)
    I = torch.as_tensor(img, device=dev) if args.visual else None
    net = QNet(k, args.visual).to(dev)
    opt = torch.optim.Adam(net.parameters(), lr=1e-3, weight_decay=1e-4)

    def fwd(b):
        return net(V[b], I[b] if args.visual else None)

    def objective(logits, rel):
        """The planner's preferences: a softmax of its scores (TEMPERATURE points
        apart is e times likelier), against the network's."""
        target = torch.softmax(rel * SCALE / TEMPERATURE, dim=1)
        return -(target * torch.log_softmax(logits, dim=1)).sum(1).mean()

    def agree(b):
        """How often the network's choice is the planner's, and what its choice scores against the best."""
        with torch.no_grad():
            q = fwd(b)
            pick = q.argmax(1)
            best = S[b].argmax(1)
            regret = (S[b].max(1).values - S[b].gather(1, pick[:, None])[:, 0]).mean().item() * SCALE
        return (pick == best).float().mean().item(), regret

    for epoch in range(args.epochs):
        net.train()
        perm = torch.as_tensor(rng.permutation(tr), device=dev)
        for s in range(0, len(perm), 128):
            b = perm[s:s + 128]
            loss = objective(fwd(b), S[b])
            opt.zero_grad(); loss.backward(); opt.step()
        if epoch % 25 == 24 or epoch == args.epochs - 1:
            net.eval()
            vb, tb = torch.as_tensor(val, device=dev), torch.as_tensor(tr, device=dev)
            with torch.no_grad():
                vl = objective(fwd(vb), S[vb]).item()
            a_tr, r_tr = agree(tb)
            a_va, r_va = agree(vb)
            print(f"epoch {epoch + 1}: validation cross-entropy {vl:.3f}; same choice as the planner {a_tr:.0%} "
                  f"(validation {a_va:.0%}), regret {r_tr:.0f} ({r_va:.0f}) points", flush=True)
            if path_rows is not None:
                a_p, _ = agree(torch.as_tensor(path_rows, device=dev))
                print(f"   the race to reproduce: {a_p:.1%} of its decisions", flush=True)
    torch.save({"q": net.state_dict(), "n_actions": k, "visual": args.visual, "set": args.set}, args.out)
    print("saved", args.out)


if __name__ == "__main__":
    main()
