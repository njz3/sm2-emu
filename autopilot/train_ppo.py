"""PPO on Indy 500 in sm2-emu's agent mode, PyTorch.

usage: python train_ppo.py RUN_DIR [--envs 12] [--image] [--resume CKPT]
                          [--params BEST.json] [--pool DIR] [--start STATE]
Writes RUN_DIR/log.csv, checkpoints, and evaluation races (a whole race from
the start, the policy's mean action: the final position). --params: the base
line (line_opt.py's best.json); --pool: the states the episodes start from."""
import argparse
import csv
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

from env_indy import OBS_DIM, IndyEnv
from ppo_model import Policy
from pursuit import Line

HERE = Path(__file__).parent
ap = argparse.ArgumentParser()
ap.add_argument("run")
ap.add_argument("--envs", type=int, default=12)
ap.add_argument("--image", action="store_true")
ap.add_argument("--resume")
ap.add_argument("--steps", type=int, default=128)
ap.add_argument("--updates", type=int, default=100000)
ap.add_argument("--eval-every", type=int, default=20)
ap.add_argument("--params")
ap.add_argument("--pool", default=str(Path(__file__).parent / "states" / "indy_oval_mt_pool"))
ap.add_argument("--start", default=str(Path(__file__).parent / "states" / "indy_oval_mt.state"))
ap.add_argument("--map", default="indy_oval_line.npz")
ap.add_argument("--seconds", type=float, default=40.0)
ap.add_argument("--lr", type=float, default=3e-4)
args = ap.parse_args()
run = Path(args.run)
run.mkdir(parents=True, exist_ok=True)
dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
IMG = (64, 48) if args.image else None


def batch(obs_list):
    vec = torch.as_tensor(np.stack([o["vec"] for o in obs_list]), device=dev)
    img = torch.as_tensor(np.stack([o["img"] for o in obs_list]), device=dev) if IMG else None
    return vec, img


net = Policy(OBS_DIM, IMG).to(dev)
opt = torch.optim.Adam(net.parameters(), lr=args.lr)
start_update = 0
if args.resume:
    ck = torch.load(args.resume, map_location=dev)
    net.load_state_dict(ck["net"]); start_update = ck["update"]
    if "opt" in ck:
        opt.load_state_dict(ck["opt"])

line = Line(HERE / "runs" / args.map)
pool = sorted(str(p) for p in Path(args.pool).glob("*.state"))
pool += [args.start] * max(1, len(pool) // 8)           # the start, more often: the race is won from there
nvram = HERE / "states" / "nvram"
envs = [IndyEnv(line, pool, nvram, params=args.params, seconds=args.seconds, image=IMG, seed=k)
        for k in range(args.envs)]
obs = [e.reset() for e in envs]


def evaluate(update):
    """A whole race from the start with the mean action: the final position."""
    env = IndyEnv(line, [args.start], nvram, params=args.params, seconds=400, image=IMG)
    o = env.reset()
    last = None
    impacts = 0
    while True:
        with torch.no_grad():
            v, i = batch([o])
            mu, _ = net(v, i)
        env.act_send(mu[0].cpu().numpy())
        o, r, done, info = env.act_wait()
        impacts += info["impacts"]
        if info["lap"] != 0:
            last = info
        if done:
            break
    env.close()
    finished = info.get("finished", False)
    return {"update": update, "finished": int(finished), "final_position": last["position"] if last else 33,
            "laps": last["lap"] if last else 0, "impacts": impacts}


logf = open(run / "log.csv", "a", newline="")
log = csv.writer(logf)
if start_update == 0:
    log.writerow(["update", "steps", "fps", "mean_return", "episodes", "impacts_per_ep", "mean_position",
                  "eval_finished", "eval_position", "eval_laps", "eval_impacts"])
ep_ret = np.zeros(args.envs); ep_imp = np.zeros(args.envs)
done_returns, done_imp, done_pos = [], [], []
total = 0
gamma, lam, clip = 0.995, 0.95, 0.2
for update in range(start_update, args.updates):
    t0 = time.time()
    B = {"vec": [], "img": [], "act": [], "logp": [], "val": [], "rew": [], "done": []}
    for _ in range(args.steps):
        with torch.no_grad():
            v, i = batch(obs)
            mu, val = net(v, i)
            std = net.logstd.exp()
            act = mu + std * torch.randn_like(mu)
            logp = (-0.5 * ((act - mu) / std) ** 2 - net.logstd - 0.5 * np.log(2 * np.pi)).sum(-1)
        a = act.cpu().numpy()
        for e, ak in zip(envs, a):
            e.act_send(ak)
        rews, dones = [], []
        for k, e in enumerate(envs):
            o, r, d, info = e.act_wait()
            ep_ret[k] += r; ep_imp[k] += info["impacts"]
            if d and info.get("truncated"):
                # cut short, not over: the race goes on, its value with it
                with torch.no_grad():
                    vb, ib = batch([o])
                    r += gamma * float(net(vb, ib)[1][0])
            if d:
                done_returns.append(ep_ret[k]); done_imp.append(ep_imp[k]); done_pos.append(info["position"])
                ep_ret[k] = 0; ep_imp[k] = 0
                o = e.reset()
            obs[k] = o
            rews.append(r); dones.append(d)
        B["vec"].append(v); B["img"].append(i); B["act"].append(act); B["logp"].append(logp); B["val"].append(val)
        B["rew"].append(torch.tensor(rews, device=dev, dtype=torch.float32))
        B["done"].append(torch.tensor(dones, device=dev, dtype=torch.float32))
    with torch.no_grad():
        v, i = batch(obs)
        _, last_val = net(v, i)
    # GAE
    vals = torch.stack(B["val"]); rews = torch.stack(B["rew"]); dones = torch.stack(B["done"])
    adv = torch.zeros_like(rews); gae = torch.zeros(args.envs, device=dev)
    for t in reversed(range(args.steps)):
        nxt = last_val if t == args.steps - 1 else vals[t + 1]
        delta = rews[t] + gamma * nxt * (1 - dones[t]) - vals[t]
        gae = delta + gamma * lam * (1 - dones[t]) * gae
        adv[t] = gae
    ret = adv + vals
    V = torch.cat(B["vec"]); A = torch.cat(B["act"]); LP = torch.cat(B["logp"])
    I = torch.cat(B["img"]) if IMG else None
    ADV = adv.flatten(); RET = ret.flatten()
    ADV = (ADV - ADV.mean()) / (ADV.std() + 1e-8)
    n = V.shape[0]
    for _ in range(4):
        perm = torch.randperm(n, device=dev)
        for s in range(0, n, 512):
            idx = perm[s:s + 512]
            mu, val = net(V[idx], I[idx] if IMG else None)
            std = net.logstd.exp()
            logp = (-0.5 * ((A[idx] - mu) / std) ** 2 - net.logstd - 0.5 * np.log(2 * np.pi)).sum(-1)
            ratio = (logp - LP[idx]).exp()
            pg = -torch.min(ratio * ADV[idx], ratio.clamp(1 - clip, 1 + clip) * ADV[idx]).mean()
            vl = 0.5 * ((val - RET[idx]) ** 2).mean()
            ent = net.logstd.sum()
            loss = pg + 0.5 * vl - 0.001 * ent
            opt.zero_grad(); loss.backward(); nn.utils.clip_grad_norm_(net.parameters(), 0.5); opt.step()
    total += n
    fps = n / (time.time() - t0)
    row = [update, total, round(fps, 1),
           round(float(np.mean(done_returns)), 2) if done_returns else "", len(done_returns),
           round(float(np.mean(done_imp)), 2) if done_imp else "", round(float(np.mean(done_pos)), 1) if done_pos else ""]
    done_returns, done_imp, done_pos = [], [], []
    if (update + 1) % args.eval_every == 0:
        ev = evaluate(update)
        row += [ev["finished"], ev["final_position"], ev["laps"], ev["impacts"]]
        torch.save({"net": net.state_dict(), "opt": opt.state_dict(), "update": update + 1}, run / "last.pt")
        if ev["finished"] and ev["final_position"] == 1:
            torch.save({"net": net.state_dict(), "update": update + 1}, run / "winner.pt")
            print("WON THE RACE at update", update, flush=True)
    log.writerow(row); logf.flush()
    print(" ".join(str(x) for x in row), flush=True)
