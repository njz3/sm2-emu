"""A trained PPO policy at the wheel: PpoDriver for the vJoy runner (the TCP
telemetry, any frame rate), PpoRunner for whole races in agent mode."""
from pathlib import Path

import numpy as np
import torch

import indy500_telemetry as T
from env_indy import FRAMES, IMPACTS, OBS_DIM, IndyEnv, Pilot
from ppo_model import Policy

HERE = Path(__file__).parent


def load_policy(ckpt):
    state = torch.load(ckpt, map_location="cpu")["net"]
    img = (64, 48) if any(k.startswith("cnn.") for k in state) else None
    net = Policy(OBS_DIM, img)
    net.load_state_dict(state)
    net.eval()
    return net, img


class PpoDriver:
    """Decides every FRAMES frames, as in training; the base driver steers at
    every frame in between. No picture: a policy trained with --image needs
    the emulator's (PpoRunner)."""

    def __init__(self, line, ckpt, params=None):
        self.net, img = load_policy(ckpt)
        if img:
            raise SystemExit("this policy needs the picture: race it with eval_race.py")
        self.pilot = Pilot(line, params or (HERE / "runs" / "opt_oval" / "best_gen30.json"))
        self.last, self.thr = None, 1.0

    def __call__(self, t):
        if self.last is None or t.frame - self.last >= FRAMES or t.frame < self.last:
            vec = torch.as_tensor(self.pilot.features(t))[None]
            with torch.no_grad():
                mu, _ = self.net(vec)
            a = mu[0].numpy()
            self.last, self.thr = t.frame, float(a[1])
            return self.pilot.control(t, a)
        return self.pilot.base(t, extra=self.pilot.offset * 7.0, thr=self.thr)


class PpoRunner:
    def __init__(self, line, ckpt, params=None):
        self.line, self.params = line, params or str(HERE / "runs" / "opt_oval" / "best_gen30.json")
        self.net, self.img = load_policy(ckpt)

    def env_for(self, start):
        return IndyEnv(self.line, [start], HERE / "states" / "nvram", params=self.params, seconds=600,
                       image=self.img)

    def race(self, env, pics=None):
        o = env.reset()
        laps_log, last_lap, lap_frame, impacts = [], env.t.player.lap, env.agent.frame, 0
        while True:
            vec = torch.as_tensor(o["vec"])[None]
            img = torch.as_tensor(o["img"])[None] if self.img else None
            with torch.no_grad():
                mu, _ = self.net(vec, img)
            env.act_send(mu[0].numpy())
            o, r, done, info = env.act_wait()
            impacts += info["impacts"]
            t = env.t
            if t.player.lap != last_lap:
                laps_log.append(((env.agent.frame - lap_frame) / T.FPS, t.position, t.time_left))
                lap_frame, last_lap = env.agent.frame, t.player.lap
            if done:
                break
        env.close()
        return {"finished": info["finished"], "position": t.position, "progress": t.player.progress,
                "laps": laps_log, "impacts": impacts}
