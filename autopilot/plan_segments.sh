#!/bin/bash
# Plans 25-second segments from random states of the pool, one after the
# other, for the score network's data (q_train.py). usage: plan_segments.sh N
cd "$(dirname "$0")"
for i in $(seq 1 "${1:-10}"); do
  state=$(ls states/indy_oval_mt_pool/*.state | shuf -n 1)
  out=runs/seg/$(date +%H%M%S)_$(basename "$state" .state)
  python plan_race.py "$state" indy_oval_line.npz "$out" --images --seconds 25 > "$out.out" 2>&1 || mkdir -p "$out"
  tail -1 "$out.out"
done
