"""Reinforcement learning over the planner's candidates (PPO, PyTorch): every
second the policy picks one of plan_race.py's candidates (where to put the
car, slipstreams and passes), which the base line and pursuit drive for that
second. About 160 decisions a race instead of 2300: the credit for a place
won at the flag reaches back to the choices that made it.

usage: python train_options.py RUN_DIR [--envs 12] [--init Q.pt] [--updates N]

The policy starts from a score network (q_train.py / dagger.py: its scores
as logits). Episodes: SECONDS from a state of the pool, or the whole race
from the start (a third of them). Rewards: the metres along the lap /10, 1 a
place gained, -0.5 a hard impact; at the flag 2 a place ahead of the last
and 50 for a win; -10 when the time runs out. Every EVAL updates, a whole
race with the policy's best choices: RUN_DIR/log.csv, last.pt, winner.pt."""
import argparse
import csv
import random
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

sys.path.insert(0, str(Path(__file__).parent))
import plan_race as P  # noqa: E402
import telemetry_any as TA  # noqa: E402
from env_indy import FRAMES, IMPACTS, OBS_DIM, Pilot  # noqa: E402
from pursuit import Line  # noqa: E402
from q_train import QNet  # noqa: E402
from sm2agent import Sm2Agent  # noqa: E402

HERE = Path(__file__).parent
STEPS = int(round(57.5 / FRAMES))          # one option: a second


class OptionEnv:
    def __init__(self, line, params, states, start, game, seconds, seed, endgame=()):
        self.line, self.states, self.start, self.seconds = line, states, start, seconds
        self.endgame = list(endgame)
        self.rng = random.Random(seed)
        self.agent = Sm2Agent(game, nvram=HERE / "states" / "nvram")
        self.pilot = Pilot(line, params)
        self.cands = P.CANDIDATES_PASS

    def reset(self, state=None):
        if state is None:
            u = self.rng.random()
            if self.endgame and u < 0.375:
                state = self.rng.choice(self.endgame)       # the last laps, behind the leaders: to the flag
            elif u < 0.625:
                state = self.start
            else:
                state = self.rng.choice(self.states)
        self.full = state == self.start or state in self.endgame
        assert self.agent.load(str(Path(state).resolve()))
        self.pilot.reset()
        self.t = TA.read(self.agent)
        self.t0 = self.agent.frame
        self.k = 0
        return self.pilot.features(self.t)

    # an option runs STEPS sub-steps; the envs run side by side, one sub-step at a time
    def begin(self, option):
        self.option, self.k, self.acc = self.cands[option], 0, {"prog": self.t.player.progress,
                                                               "pos": self.t.position, "imp": 0}

    def send(self):
        if self.k > 0:
            self.pilot.features(self.t)       # its direction of travel, as the planner does
        c = self.option
        action = c(self.t, self.pilot, self.k) if callable(c) else c
        self.agent.step_send(FRAMES, *self.pilot.control(self.t, action))

    def wait(self):
        self.acc["imp"] += sum(1 for b in self.agent.step_wait() if b in IMPACTS)
        self.t = TA.read(self.agent)
        self.k += 1
        return P.over(self.t)

    def end(self):
        t, p = self.t, self.t.player
        gained = (p.progress - self.acc["prog"]) * self.line.length
        r = gained / 10.0 + (self.acc["pos"] - t.position) - 0.5 * self.acc["imp"]
        finished = p.lap > t.laps
        if finished:
            r += 2.0 * (34 - t.position) + (50.0 if t.position == 1 else 0.0)
        out = t.time_left <= 0 and not finished
        if out:
            r -= 10.0
        timeout = not self.full and (self.agent.frame - self.t0) / 57.5 >= self.seconds
        done = finished or out or timeout
        info = {"position": t.position, "finished": finished, "truncated": timeout and not (finished or out),
                "progress": p.progress}
        obs = self.pilot.features(t)
        return obs, r, done, info


class ActorCritic(nn.Module):
    def __init__(self, n):
        super().__init__()
        self.pi = QNet(n)
        self.v = nn.Sequential(nn.Linear(OBS_DIM, 256), nn.Tanh(), nn.Linear(256, 1))

    def forward(self, x):
        return self.pi(x), self.v(x).squeeze(-1)


def run_options(envs, actions):
    for e, a in zip(envs, actions):
        e.begin(int(a))
    live = list(range(len(envs)))
    for _ in range(STEPS):
        for j in live:
            envs[j].send()
        for j in list(live):
            if envs[j].wait():
                live.remove(j)
        if not live:
            break
    return [e.end() for e in envs]


def evaluate(net, env, dev):
    obs = env.reset(env.start)
    while True:
        with torch.no_grad():
            logits, _ = net(torch.as_tensor(obs, device=dev)[None])
        obs, r, done, info = run_options([env], [int(logits.argmax(1)[0])])[0]
        if done:
            return info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--envs", type=int, default=12)
    ap.add_argument("--init")
    ap.add_argument("--resume")
    ap.add_argument("--updates", type=int, default=400)
    ap.add_argument("--steps", type=int, default=32, help="options per env per update")
    ap.add_argument("--seconds", type=float, default=40.0)
    ap.add_argument("--eval", type=int, default=5)
    ap.add_argument("--lr", type=float, default=1e-4)
    ap.add_argument("--map", default="indy_oval_line.npz")
    ap.add_argument("--params", default=str(HERE / "runs" / "opt_oval" / "best_gen30.json"))
    ap.add_argument("--pool", default=str(HERE / "states" / "indy_oval_mt_pool"))
    ap.add_argument("--start", default=str(HERE / "states" / "indy_oval_mt.state"))
    ap.add_argument("--game", default="indy500")
    ap.add_argument("--endgame", help="states of the race's end (run to the flag), a third of the episodes")
    args = ap.parse_args()
    run = Path(args.run); run.mkdir(parents=True, exist_ok=True)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    line = Line(HERE / "runs" / args.map)
    states = sorted(str(p) for p in Path(args.pool).glob("*.state"))
    endgame = sorted(str(p) for p in Path(args.endgame).glob("*.state")) if args.endgame else []
    envs = [OptionEnv(line, args.params, states, args.start, args.game, args.seconds, k, endgame)
            for k in range(args.envs)]
    n = len(P.CANDIDATES_PASS)
    net = ActorCritic(n).to(dev)
    if args.init:
        net.pi.load_state_dict(torch.load(args.init, map_location=dev)["q"])
    start_update = 0
    if args.resume:
        ck = torch.load(args.resume, map_location=dev)
        net.load_state_dict(ck["net"]); start_update = ck["update"]
    opt = torch.optim.Adam(net.parameters(), lr=args.lr)
    evaluator = OptionEnv(line, args.params, states, args.start, args.game, 600, 999)
    logf = open(run / "log.csv", "a", newline="")
    log = csv.writer(logf)
    obs = [e.reset() for e in envs]
    gamma, lam, clip = 0.98, 0.95, 0.2
    ret_ep, done_ret, done_pos = np.zeros(len(envs)), [], []
    for update in range(start_update, args.updates):
        t0 = time.time()
        B = {"obs": [], "act": [], "logp": [], "val": [], "rew": [], "done": []}
        for _ in range(args.steps):
            x = torch.as_tensor(np.stack(obs), device=dev)
            with torch.no_grad():
                logits, val = net(x)
                dist = torch.distributions.Categorical(logits=logits)
                act = dist.sample()
            results = run_options(envs, act.cpu().numpy())
            rews, dones = [], []
            for k, (o, r, d, info) in enumerate(results):
                if d and info["truncated"]:
                    with torch.no_grad():
                        r += gamma * float(net(torch.as_tensor(o, device=dev)[None])[1][0])
                ret_ep[k] += r
                if d:
                    done_ret.append(ret_ep[k]); done_pos.append(info["position"]); ret_ep[k] = 0
                    o = envs[k].reset()
                obs[k] = o
                rews.append(r); dones.append(float(d))
            B["obs"].append(x); B["act"].append(act); B["logp"].append(dist.log_prob(act)); B["val"].append(val)
            B["rew"].append(torch.tensor(rews, device=dev, dtype=torch.float32))
            B["done"].append(torch.tensor(dones, device=dev))
        with torch.no_grad():
            _, last = net(torch.as_tensor(np.stack(obs), device=dev))
        vals, rews, dones = torch.stack(B["val"]), torch.stack(B["rew"]), torch.stack(B["done"])
        adv = torch.zeros_like(rews); gae = torch.zeros(len(envs), device=dev)
        for t in reversed(range(args.steps)):
            nxt = last if t == args.steps - 1 else vals[t + 1]
            delta = rews[t] + gamma * nxt * (1 - dones[t]) - vals[t]
            gae = delta + gamma * lam * (1 - dones[t]) * gae
            adv[t] = gae
        ret = (adv + vals).flatten()
        O, A, LP = torch.cat(B["obs"]), torch.cat(B["act"]), torch.cat(B["logp"])
        ADV = adv.flatten(); ADV = (ADV - ADV.mean()) / (ADV.std() + 1e-8)
        for _ in range(4):
            perm = torch.randperm(len(A), device=dev)
            for s in range(0, len(A), 128):
                b = perm[s:s + 128]
                logits, v = net(O[b])
                dist = torch.distributions.Categorical(logits=logits)
                ratio = (dist.log_prob(A[b]) - LP[b]).exp()
                pg = -torch.min(ratio * ADV[b], ratio.clamp(1 - clip, 1 + clip) * ADV[b]).mean()
                loss = pg + 0.5 * ((v - ret[b]) ** 2).mean() - 0.01 * dist.entropy().mean()
                opt.zero_grad(); loss.backward(); nn.utils.clip_grad_norm_(net.parameters(), 0.5); opt.step()
        row = [update, round(time.time() - t0, 1),
               round(float(np.mean(done_ret)), 1) if done_ret else "", len(done_ret),
               round(float(np.mean(done_pos)), 1) if done_pos else ""]
        done_ret, done_pos = [], []
        if (update + 1) % args.eval == 0:
            info = evaluate(net, evaluator, dev)
            row += [int(info["finished"]), info["position"], round(info["progress"], 3)]
            torch.save({"net": net.state_dict(), "update": update + 1}, run / "last.pt")
            torch.save({"q": net.pi.state_dict(), "n_actions": n, "visual": False, "set": "pass"}, run / "policy.pt")
            if info["finished"] and info["position"] == 1:
                torch.save({"q": net.pi.state_dict(), "n_actions": n, "visual": False, "set": "pass"},
                           run / "winner.pt")
                print("WON THE RACE at update", update, flush=True)
        log.writerow(row); logf.flush()
        print(" ".join(str(x) for x in row), flush=True)


if __name__ == "__main__":
    main()
