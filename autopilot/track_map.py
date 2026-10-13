"""Build a track's map from record_laps.py recordings: the AI cars' whole laps,
from one crossing of the line to the next, averaged by arc length.

usage: python track_map.py OUT.npz RECORDING.npz...
OUT holds `line` (N x 2: x, z, a point every 2 m or so, starting at the line),
`sectors` (their count) and `sector_start` (each sector's start, as a fraction
of the lap's length)."""
import sys

import numpy as np

N = 2048            # points a lap
out, recs = sys.argv[1], sys.argv[2:]
laps, starts = [], []
sectors = 0
for rec in recs:
    r = np.load(rec)
    xz, e4, tl, course = r["xz"].astype(np.float64), r["e4"], r["time_left"], r["course"]
    # The race only: once the time is out, the attract mode drives other courses.
    live = (course == course[0]) & (tl > 0)
    lap = (e4 >> 20).astype(int)
    sector = ((e4 >> 16) & 15).astype(int)
    sectors = max(sectors, int(sector[live].max()) + 1)
    for k in range(1, xz.shape[1]):
        cross = np.where(live[1:] & live[:-1] & (lap[1:, k] == lap[:-1, k] + 1))[0] + 1  # 0 to 1: the rolling start, already a lap
        for i0, i1 in zip(cross[:-1], cross[1:]):
            if not live[i0:i1 + 1].all():
                continue
            p = xz[i0:i1 + 1, k]
            seg = np.hypot(*np.diff(p, axis=0).T)
            if seg.max() > 15:          # a jump: not a lap driven
                continue
            s = np.concatenate([[0], np.cumsum(seg)])
            u = np.linspace(0, s[-1], N, endpoint=False)
            laps.append(np.column_stack([np.interp(u, s, p[:, 0]), np.interp(u, s, p[:, 1])]))
            # where each sector starts, as a fraction of this lap
            sec = sector[i0:i1 + 1, k]
            change = np.where(np.diff(sec) != 0)[0] + 1
            starts.append({int(sec[c]): s[c] / s[-1] for c in change})
if not laps:
    sys.exit("no whole lap recorded")
L = np.stack(laps)
line = np.median(L, axis=0)
dev = np.hypot(*(L - line).transpose(2, 0, 1)).mean(axis=1)
keep = dev < np.median(dev) * 3      # drop the odd lap (a crash, the pits)
line = np.median(L[keep], axis=0)
sector_start = np.array([np.median([s[k] for s in starts if k in s]) if any(k in s for s in starts) else k / sectors
                         for k in range(sectors)])
sector_start[0] = 0.0
np.savez(out, line=line, sectors=sectors, sector_start=sector_start)
seg = np.hypot(*np.diff(np.vstack([line, line[:1]]), axis=0).T)
print(f"{len(laps)} laps ({keep.sum()} kept), {sectors} sectors, lap length {seg.sum():.0f} m, "
      f"sectors start at {np.round(sector_start, 3).tolist()}")
