"""Indy 500 as a reinforcement learning environment, on sm2-emu's agent mode.

The base driver is the line CMA-ES optimised (line_opt.py: an offset from
the map's line and a throttle along the lap, a pure pursuit steering). The
policy moves that line sideways (to draft, to pass, to stay clear: -1..1 for
-7..7 m, at most 0.6 m a decision) and may lift or brake, 14 times a second.

Observations: the car against the line (speed, offset, direction of travel,
slip), the curves ahead, the race (time, laps, position) and the 10 nearest
cars along the track (distance, side, speed difference); optionally the
picture.

Rewards: the metres gained along the lap /10, 1 a place gained, -0.5 a hard
impact (drive board pushes 55-57, 65-67); at the end of the race 2 a place
ahead of the last, 50 more for a win; -10 when the time runs out."""
import json
import random
from pathlib import Path

import numpy as np

import indy500_telemetry as T
import telemetry_any as TA  # noqa: E402
from line_opt import LineDriver
from pursuit import Line, wrap
from sm2agent import Sm2Agent

FRAMES = 4                 # per decision
MAX_OFFSET = 7.0           # metres
N_CARS = 10
OBS_DIM = 18 + 4 * N_CARS
IMPACTS = {0x55, 0x56, 0x57, 0x65, 0x66, 0x67}


def base_driver(line, params):
    p = json.loads(Path(params).read_text()) if params else None
    if p is None:                                      # the plain pursuit: gain 3, the map's line
        knots = 12
        x = np.zeros(2 * knots + 4); x[knots:2 * knots] = -0.2
        return LineDriver(line, x, knots)
    return LineDriver(line, p["x"], p["knots"])


class Gearbox:
    """A manual gearbox shifted by speed. Indy 500's 6-speed on the oval: 5th
    below 93 m/s (it pulls harder out of the turns), 6th above 96.5 (5th's
    limit is 98). Touring Car's (TOPS): up at 96 % of the gear's limit, down
    below 80 % of the one beneath. A shift is a press of the paddle (IN1 0x20
    down, 0x10 up) for one decision, then a release."""

    DOWN, UP = 93.0, 96.5
    TOPS = {1: 20.2, 2: 33.6, 3: 42.1, 4: 55.3, 5: 70.9, 6: 90.0}   # Touring Car, units/s

    def __init__(self, tops=None):
        self.pressed = False
        self.tops = tops

    def __call__(self, t):
        if not getattr(t, "manual", False) or not getattr(t, "gear", 0) or self.pressed:
            self.pressed = False
            return 0xFF
        v, g = t.player.speed, t.gear
        if self.tops:
            if g < max(self.tops) and v > 0.96 * self.tops[g]:
                self.pressed = True
                return 0xFF & ~0x10
            if g > 1 and v < 0.8 * self.tops[g - 1]:
                self.pressed = True
                return 0xFF & ~0x20
            return 0xFF
        if g >= 6 and v < self.DOWN:
            self.pressed = True
            return 0xFF & ~0x20
        if g == 5 and v > self.UP:
            self.pressed = True
            return 0xFF & ~0x10
        return 0xFF


class Pilot:
    """What the policy sees and how its actions drive, from the telemetry
    alone: the same in training (agent mode) and on vJoy (the TCP telemetry).
    With shift=True, control() also gives the IN0 and IN1 bytes (the gear
    paddles), for Sm2Agent.step's in0 and in1."""

    def __init__(self, line, params=None, shift=False, tops=None):
        self.line = line
        self.gearbox = Gearbox(tops) if shift else None
        self.base = base_driver(line, params)
        self.kappa = self._curvature(line)
        self.reset()

    def reset(self):
        self.offset = 0.0
        self.prev_action = np.zeros(2, dtype=np.float32)
        self.prev_pos = None
        if self.gearbox is not None:
            self.gearbox.pressed = False

    @staticmethod
    def _curvature(line):
        p = line.p
        n = len(p)
        d = np.arctan2(*(np.roll(p, -1, 0) - p).T)
        seg = np.hypot(*(np.roll(p, -1, 0) - p).T)
        k = max(1, int(round(15.0 / seg.mean())))
        return np.array([wrap(d[(i + k) % n] - d[(i - k) % n]) for i in range(n)]) / (2 * k * seg.mean())

    def features(self, t):
        line, p = self.line, t.player
        lat, ldir, i = line.lateral(p.x, p.z)
        travel = t.heading
        if self.prev_pos is not None:
            dx, dz = p.x - self.prev_pos[0], p.z - self.prev_pos[1]
            if dx * dx + dz * dz > 0.25:
                travel = float(np.arctan2(dx, dz))
        self.prev_pos = (p.x, p.z)
        step = line.length / len(line.p)
        curv = [float(self.kappa[(i + int(a / step)) % len(line.p)]) * 100 for a in (25, 50, 100, 150, 200)]
        laps = t.laps
        f = [p.speed / 100.0, lat / MAX_OFFSET, wrap(travel - ldir), wrap(travel - t.heading), *curv,
             self.base.offset_at(line.s[i]) / MAX_OFFSET, self.base.offset_at(line.s[i] + 100) / MAX_OFFSET,
             self.offset, t.time_left / 60.0, (laps - p.progress) / laps, t.position / 33.0,
             1.0 if p.progress > laps - 1 else 0.0, *self.prev_action]
        # the nearest cars along the track, by their arc length relative to ours
        s0 = line.s[i]
        rows = []
        for c in t.others:
            if c.lap < 1:
                continue
            clat, _, ci = line.lateral(c.x, c.z)
            ds = (line.s[ci] - s0 + line.length / 2) % line.length - line.length / 2
            if abs(ds) < 250:
                rows.append((abs(ds), ds / 250.0, (clat - lat) / MAX_OFFSET, (c.speed - p.speed) / 20.0))
        rows.sort()
        for k in range(N_CARS):
            f += [rows[k][1], rows[k][2], rows[k][3], 1.0] if k < len(rows) else [0.0, 0.0, 0.0, 0.0]
        self.last_vec = np.array(f, dtype=np.float32)
        return self.last_vec

    def control(self, t, action):
        """The policy's action (-1..1: the line's offset, the throttle) as steering, accelerator, brake."""
        a = np.clip(np.asarray(action, dtype=np.float64), -1, 1)
        self.offset += float(np.clip(a[0] - self.offset, -0.6 / MAX_OFFSET, 0.6 / MAX_OFFSET))
        self.prev_action = a.astype(np.float32)
        steer, accel, brake = self.base(t, extra=self.offset * MAX_OFFSET, thr=float(a[1]))
        if self.gearbox is not None:
            return steer, accel, brake, 0xFF, self.gearbox(t)
        return steer, accel, brake


class IndyEnv:
    def __init__(self, line, states, nvram, params=None, seconds=40.0, image=None, seed=0):
        self.line, self.states, self.seconds, self.image = line, list(states), seconds, image
        self.rng = random.Random(seed)
        self.agent = Sm2Agent("indy500", nvram=nvram)
        self.pilot = Pilot(line, params)
        self.t = None

    def observe(self):
        self.t = TA.read(self.agent)
        obs = {"vec": self.pilot.features(self.t)}
        if self.image:
            w, h = self.image
            obs["img"] = (self.agent.obs(w, h).astype(np.float32).mean(axis=2) / 255.0)[None]
        return obs

    # -- an episode ------------------------------------------------------------
    def reset(self, state=None):
        state = state or self.rng.choice(self.states)
        assert self.agent.load(str(Path(state).resolve())), state
        self.pilot.reset()
        self.start_frame = self.agent.frame
        obs = self.observe()
        self.prev_prog, self.prev_pos_race = self.t.player.progress, self.t.position
        return obs

    def act_send(self, action):
        """Start the step; the emulators of several environments then run together."""
        self.agent.step_send(FRAMES, *self.pilot.control(self.t, action))

    def act_wait(self):
        drive = self.agent.step_wait()
        obs = self.observe()
        t, p, prev = self.t, self.t.player, self.prev_prog
        laps = t.laps
        gained = (p.progress - prev) * self.line.length if abs(p.progress - prev) < 0.2 else 0.0
        reward = gained / 10.0 + 1.0 * (self.prev_pos_race - t.position)
        impacts = sum(1 for b in drive if b in IMPACTS)
        reward -= 0.5 * impacts
        self.prev_prog, self.prev_pos_race = p.progress, t.position
        finished = p.lap > laps
        if finished:
            reward += 2.0 * (34 - t.position) + (50.0 if t.position == 1 else 0.0)
        out_of_time = t.time_left <= 0 and not finished
        if out_of_time:
            reward -= 10.0
        timeout = (self.agent.frame - self.start_frame) / T.FPS >= self.seconds
        info = {"impacts": impacts, "position": t.position, "lap": p.lap, "speed": p.speed,
                "finished": finished, "progress": p.progress,
                "truncated": timeout and not (finished or out_of_time)}
        return obs, reward, finished or out_of_time or timeout, info

    def close(self):
        self.agent.close()
