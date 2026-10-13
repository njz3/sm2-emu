"""A pure-pursuit driver on a track map: the baseline the learning starts from,
and the teacher of the picture-based policy."""
import numpy as np


class Line:
    def __init__(self, path, smooth=9):
        m = np.load(path)
        line = m["line"].astype(np.float64)
        n = len(line)
        k = np.ones(smooth) / smooth
        ext = np.vstack([line[-smooth:], line, line[:smooth]])
        line = np.column_stack([np.convolve(ext[:, i], k, mode="same")[smooth:smooth + n] for i in range(2)])
        self.p = line
        seg = np.hypot(*np.diff(np.vstack([line, line[:1]]), axis=0).T)
        self.s = np.concatenate([[0], np.cumsum(seg)[:-1]])
        self.length = seg.sum()

    def nearest(self, x, z):
        return int(np.argmin(np.hypot(self.p[:, 0] - x, self.p[:, 1] - z)))

    def ahead(self, i, dist):
        """The line's point `dist` metres ahead of point i."""
        target = (self.s[i] + dist) % self.length
        j = int(np.searchsorted(self.s, target)) % len(self.p)
        return self.p[j]

    def lateral(self, x, z):
        """Signed distance from the line (positive: to the right of it), and the
        line's direction there."""
        i = self.nearest(x, z)
        a, b = self.p[i], self.p[(i + 1) % len(self.p)]
        d = b - a
        d /= np.linalg.norm(d) + 1e-9
        r = np.array([x, z]) - a
        cross = d[0] * r[1] - d[1] * r[0]
        return -cross, np.arctan2(d[0], d[1]), i


def wrap(a):
    return (a + np.pi) % (2 * np.pi) - np.pi


class Pursuit:
    def __init__(self, line, gain=1.6, look=18.0, look_per_mps=0.45):
        self.line, self.gain, self.look, self.look_per_mps = line, gain, look, look_per_mps

    def __call__(self, t):
        p = t.player
        i = self.line.nearest(p.x, p.z)
        tx, tz = self.line.ahead(i, self.look + self.look_per_mps * p.speed)
        alpha = wrap(np.arctan2(tx - p.x, tz - p.z) - t.heading)
        steer = float(np.clip(self.gain * alpha, -1.0, 1.0))
        return steer, 1.0, 0.0


class Driver:
    """Drives a line at an offset (metres, positive to the right) from it:
    the steering is the line's curvature ahead (feed-forward) plus the lateral
    and heading errors (feedback), limited to the grip at speed; the throttle
    lifts and brakes where the curvature ahead asks for less speed than the
    car has (a speed plan from a lateral and a braking acceleration).

    Measured on the oval at 110 m/s (turn_probe.py): yaw rate = speed *
    steer / 60 m up to steer 0.35 (7 g); past 0.5 the front tyres saturate
    and the car scrubs 30 m/s."""

    def __init__(self, line, c=60.0, k_lat=0.05, k_head=1.0, k_slip=0.5, preview=0.25, smax=0.4,
                 a_lat=60.0, a_brake=15.0, horizon=250.0):
        self.line, self.c, self.k_lat, self.k_head, self.k_slip = line, c, k_lat, k_head, k_slip
        self.prev = None
        self.preview, self.smax, self.a_lat, self.a_brake, self.horizon = preview, smax, a_lat, a_brake, horizon
        p = line.p
        n = len(p)
        d = np.arctan2(*(np.roll(p, -1, 0) - p).T)
        seg = np.hypot(*(np.roll(p, -1, 0) - p).T)
        k = max(1, int(round(15.0 / max(seg.mean(), 1e-6))))     # over 30 m
        dd = np.array([wrap(d[(i + k) % n] - d[(i - k) % n]) for i in range(n)])
        ds = np.array([seg[[(i - k + j) % n for j in range(2 * k)]].sum() for i in range(n)])
        self.kappa = dd / ds                       # signed: + turns right
        self.dir = d
        self.step = seg.mean()
        # the speed the curvature allows at each point
        self.v_curve = np.sqrt(a_lat / np.maximum(np.abs(self.kappa), 1e-5))
        self.offset = 0.0

    def _at(self, i, dist):
        return (i + int(round(dist / self.step))) % len(self.line.p)

    def target_speed(self, i):
        """The highest speed now from which the curves ahead can still be made."""
        n_ahead = int(self.horizon / self.step)
        idx = (i + np.arange(n_ahead)) % len(self.line.p)
        dist = np.arange(n_ahead) * self.step
        return float(np.min(np.sqrt(self.v_curve[idx] ** 2 + 2 * self.a_brake * dist)))

    def __call__(self, t, offset=None):
        if offset is not None:
            self.offset = offset
        p = t.player
        v = max(p.speed, 1.0)
        lat, ldir, i = self.line.lateral(p.x, p.z)
        # the direction of travel, from the last position: the body slides in the turns
        travel = t.heading
        if self.prev is not None and 0 < t.frame - self.prev[2] <= 8:
            dx, dz = p.x - self.prev[0], p.z - self.prev[1]
            if dx * dx + dz * dz > 0.25:
                travel = float(np.arctan2(dx, dz))
        self.prev = (p.x, p.z, t.frame)
        # curvature ahead, over the preview's span
        a, b = self._at(i, v * 0.5 * self.preview), self._at(i, v * 1.5 * self.preview)
        span = np.arange(a, b + 1) % len(self.line.p) if b >= a else np.r_[a:len(self.line.p), 0:b + 1]
        kappa = float(self.kappa[span].mean())
        e_lat = lat - self.offset
        e_head = wrap(travel - self.dir[i])
        slip = wrap(travel - t.heading)          # + : the nose points left of the travel
        steer = self.c * kappa - self.k_lat * e_lat - self.k_head * e_head + self.k_slip * slip
        smax = float(np.clip(self.smax + (90.0 - v) * 0.02, self.smax, 1.0))
        steer = float(np.clip(steer, -smax, smax))
        vt = self.target_speed(i)
        if v < vt:
            accel, brake = 1.0, 0.0
        elif v < vt + 5:
            accel, brake = 0.3, 0.0
        else:
            accel, brake = 0.0, float(np.clip((v - vt - 5) / 10, 0, 1))
        return steer, accel, brake
