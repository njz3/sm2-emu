"""A driver that chooses among the planner's candidates with the scores a
network learnt (q_train.py): every second, the best scored candidate; the
base line and pursuit (env_indy.Pilot) drive it in between.

QDriver(t) gives the steering and pedals for any frame (run_vjoy.py
--policy q:MODEL.pt); QRunner races it in agent mode (eval_race.py)."""
from pathlib import Path

import numpy as np
import torch

import indy500_telemetry as T
import telemetry_any as TA  # noqa: E402
from env_indy import FRAMES, IMPACTS, Pilot
from plan_race import CANDIDATES, CANDIDATES_PASS
from q_train import QNet

HERE = Path(__file__).parent
DECIDE = int(round(1.0 * T.FPS))          # frames between choices, about the planner's commit
STEPS = int(round(1.0 * T.FPS / FRAMES))  # exactly: the planner's commit, in steps of FRAMES


class QDriver:
    def __init__(self, line, model, params=None, image_source=None):
        ck = torch.load(model, map_location="cpu")
        self.net = QNet(ck["n_actions"], ck["visual"])
        self.net.load_state_dict(ck["q"])
        self.net.eval()
        self.visual, self.image_source = ck["visual"], image_source
        self.cands = CANDIDATES_PASS if ck.get("set", "pass") == "pass" else CANDIDATES
        self.pilot = Pilot(line, params or (HERE / "runs" / "opt_oval" / "best_gen30.json"))
        self.choice, self.chosen_at, self.k, self.last = None, None, STEPS, None
        self.action = (0.0, 1.0)

    def _choose(self, t, vec):
        v = torch.as_tensor(vec)[None]
        img = None
        if self.visual:
            from visual_driver import to_input
            img = torch.as_tensor(to_input(self.image_source()))[None, None]
        with torch.no_grad():
            q = self.net(v, img)[0].numpy()
        return int(np.argmax(q))

    def __call__(self, t):
        if self.last is not None and 0 <= t.frame - self.last < FRAMES:
            return self.held                  # held FRAMES frames, as the training's steps
        self.last = t.frame
        vec = self.pilot.features(t)
        if self.k >= STEPS or self.chosen_at is None or not 0 <= t.frame - self.chosen_at < 2 * DECIDE:
            self.choice, self.chosen_at, self.k = self._choose(t, vec), t.frame, 0
        cand = self.cands[self.choice]
        self.action = cand(t, self.pilot, self.k) if callable(cand) else cand
        self.k += 1
        self.held = self.pilot.control(t, self.action)
        return self.held


class QChooser:
    """The network as a candidate for plan_race.run_all: every second, the
    candidate it scores best (from the features the pilot just computed),
    then that candidate's action. To roll the network out after a first
    choice (dagger.py --rollout)."""

    def __init__(self, net, cands):
        self.net, self.cands = net, cands
        self.choice, self.k0 = None, 0

    def __call__(self, t, pilot, k):
        if self.choice is None or k - self.k0 >= STEPS:
            with torch.no_grad():
                q = self.net(torch.as_tensor(pilot.last_vec)[None])[0].numpy()
            self.choice, self.k0 = int(np.argmax(q)), k
        cand = self.cands[self.choice]
        return cand(t, pilot, k - self.k0) if callable(cand) else cand


def load_net(model):
    ck = torch.load(model, map_location="cpu")
    net = QNet(ck["n_actions"], ck["visual"])
    net.load_state_dict(ck["q"])
    net.eval()
    return net, (CANDIDATES_PASS if ck.get("set", "pass") == "pass" else CANDIDATES)


def race(agent, driver, laps_log):
    """A whole race in agent mode from the agent's current state."""
    t = TA.read(agent)
    last_lap, lap_frame, impacts = t.player.lap, agent.frame, 0
    laps = t.laps
    while t.player.lap <= laps and t.time_left > 0:
        drive = agent.step(FRAMES, *driver(t))
        impacts += sum(1 for b in drive if b in IMPACTS)
        t = TA.read(agent)
        if t.player.lap != last_lap:
            laps_log.append(((agent.frame - lap_frame) / T.FPS, t.position, t.time_left))
            last_lap, lap_frame = t.player.lap, agent.frame
    return t, impacts
