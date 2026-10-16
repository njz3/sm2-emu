# Autopilot, telemetry and force feedback: progress log

The goal: a driving autopilot for the Model 2 racing games, trained with
machine learning on the games' telemetry and pictures, that wins a race; used
to drive the games through vJoy while the drive-board commands and the force
feedback the emulator sends are logged, to compare sm2-emu's force feedback
with the real boards'. Indy 500 first, then Touring Car, Sega Rally, Daytona.

Everything here is on the `dev/ffbedition` branch of the user's fork,
njz3/sm2-emu (named `dev/network_output_scsp` until 13 October 2026), pushed.
Since 14 October that branch is upstream's main (v0.9.46, `cec4404`) and only
its own commits on top, rebuilt from the merged branch (whose last state is
kept locally as `backup/dev-ffbedition-e9d9d76`).

Since 15 October the autopilot's scripts, with their data, are in
BackForceFeeder (`BackForceFeeder_master`, `autopilot/sm2emu/`), beside
Supermodel's (`autopilot/supermodel/`) and the force feedback analysis both
emulators share (`autopilot/analyze_ffb.py`). sm2-emu keeps the agent mode
and the telemetry (`src/agent.cpp`, `src/telemetry/`) and this log. Below,
`sm2emu/` is that folder of BackForceFeeder.

## Where it stands (2026-10-16)

- **Every course of Daytona USA, Sega Rally and Indy 500 finished** by the
  autopilot in agent mode, frame by frame, with the manual gearbox
  (BackForceFeeder's `autopilot/sm2emu/rl/`, the section "Reinforcement
  learning, again" below): **all three Indy 500 courses won**, **all three
  of Daytona's courses won** (beginner, advanced, expert), **Sega Rally's
  championship in 3'28"53, the best time** (first of the records table,
  ahead of 3'30"00; the race 3rd), **Touring Car's round 1 won** (its
  qualifying lap and its race planned in one go, on an optimised line).
  The driver that does it is a planner with the emulator itself over a base
  driver; reinforcement learning alone (PPO) did not get there in the time,
  nor did a network learning the planner's choices. Each race replays frame
  by frame with its force feedback recorded, and the force feedback was set
  against the races' events (hits, off the road, airborne).
- **Indy 500 is won** by the autopilot: from 33rd on the grid of the oval, 1st
  at the flag, 2'31"069, best lap 36"518 (the cabinet's record 37"248). The
  driver: a racing line optimised by CMA-ES, a pure pursuit, and a planner that
  tries 13 candidates (where to be across the track, slipstreams and passes)
  with the emulator itself 8 s ahead, a beam of 3 for the last lap. Replayed
  from the green flag in one emulator (`runs/win1`).
- **The learnt network wins too**, alone: trained on the planner's scores and
  on the winning race's 157 decisions, it chooses the candidates itself, from
  the telemetry (`runs/q_win1.pt`) or **from the picture alone**
  (`runs/q_win1_visual.pt`), and finishes 1st from 33rd, in agent mode and
  **through vJoy in the emulator's window** (`run_vjoy.py --state
  states/indy_oval_mt.state --policy q:runs/q_win1.pt --sync`: the emulator's
  `--telemetry-sync` waits for each frame's controls, so the game gets them on
  the frame they were computed for, byte for byte as in training). It wins
  from that start because it reproduces that race: from other situations it
  drives like the networks that only generalise (10th to 17th); through vJoy
  without the lockstep (the controls a frame or two late) it was 23rd after
  three laps.
- **Telemetry** for Indy 500, Touring Car, Sega Rally, Daytona, Super GT 24h
  and Over Rev, served over TCP by sm2-emu (`--telemetry`) and to agent mode
  (`tel`); the last two checked against the screen in a race of each. Since
  15 October the keys Supermodel shares, for motion simulators and
  dashboards (Forza Data Out too), and Motor Raid, Wave Runner and Desert
  Tank: TELEMETRY.md.
- **Agent mode for force feedback benches**, in the protocol of Supermodel's
  agent mode, so one client drives both emulators: inputs held by name
  (`set Steering 1` fully left, trigonometrically, as a real wheel and
  BackForceFeeder), the force feedback's commands frame by frame (`ffb`, a
  constant > 0 pushing left), the drive board model's ports at every
  interrupt (`servo`), a virtual wheel or, with `--agent-device`, vJoy read
  in the very next frame (300 in 300) and given the force (its packets match
  the records).
- **vJoy autopilot**, standalone, for the four games, logging the drive board's
  bytes and the force feedback sent to vJoy; a BackForceFeeder script for the
  cabinet (untested).
- **FFB compared** for the four boards (tables below): Sega Rally linear and
  faithful; Indy 500's family (Touring Car, Over Rev, Super GT) was much
  steeper than the board (a torque curve of mine): **corrected**, the torque
  now in proportion to the board's power, the pushes exactly the board's;
  Daytona's springs and pushes stronger than the board's formula.
- **A force feedback bench in agent mode** (15 October, below): each game
  from power-up to its race, the autopilot driving three ways, every link
  logged and checked (`ffb_campaign.py`, `analyze_ffb.py`, in the files of
  Supermodel's bench). In 12 races: no command lost, the force sent is the
  board model's output replayed (25 046 records in 25 046), the Indy
  family's pushes are the board's formula to the unit and its power out of
  63, Sega Rally's linear. Daytona's model follows its formula; Daytona and
  Sega Rally have the same board and the same panel (an AC motor and a power
  clutch, the same motor byte), and sm2-emu gives both the same scale, a
  power of 32 the wheel's maximum (Sega Rally's strongest torque). The 1.5 to
  2 times of the vJoy comparison was that formula read out of 63.
- Touring Car: the autopilot finishes 4th from 8th (the three leaders lap at
  its pace). Sega Rally and Daytona: driven around their first course.
- **Force feedback of Indy 500's family, done in the code**: the torque in
  proportion to the board's two powers (`wheel_indy_deadband` 0,
  `wheel_indy_full_power` 63), the brake `2x` a damper, the board's answer on
  port D (the test menus work), the DIP strength setting, the board in the
  save states, an EEPROM centre of `$FF` taken as the factory's `$80` (in the
  board's answer, upstream's `indy_board_centre`, since 14 October). The
  real board, EPR-18261 on the cabinet Racecab1 (measured by the
  BackForceFeeder session on 13 October), confirmed the model's 500 Hz NMI and
  the brake as a damper. Not yet tried on the user's wheel (Sega Rally and
  Daytona were, found faithful at 100 %).
- **Over Rev** drives its board only once its HARDWARE TYPE (test menu) is a
  Sega cabinet (README). **Super GT 24h** drives it with I/O TYPE A, the
  default, or B, the same commands, and not with C; each type reads the
  pedals its own way round, which sm2-emu follows since 14 October (read in
  the game's program, checked in a race with all three), upstream too (PR
  #23). A blank backup RAM leaves Super GT 24h's I/O TYPE at `FFFF`, no type
  (pedals garbage, no board): sm2-emu sets it to A. From a blank NVRAM, Super
  GT 24h races with its force feedback; Over Rev sends nothing until its
  HARDWARE TYPE is set.
- **Published**: committed and pushed on `dev/ffbedition`; the last Windows
  release zip made like the CI's, `build\bin\sm2-emu-v0.9.47-32-ga568878-njz3-windows-x86_64.zip`
  (SHA-256 `8AA40FE8EA35C27E892F7581B5FA77A1D593C9EE48FF13948229F1B3B4B15057`),
  upstream's v0.9.47 and this branch's work.

What is left:

- the user's own test of Indy 500, Touring Car, Over Rev and Super GT 24h on
  the wheel;
- the bench: Super GT 24h and Over Rev in it (their lines and drivers are in
  the scratchpad tools), a model of Sega Rally's board (EPR-17891) for its
  outputs, the wheel's position at each interrupt in the models, the
  analysis shared with Supermodel's;
- the PR left for dmanlfc: `pr/wheel-panel-spring` (the panel spring and the
  DIP strength settings, its PR message written), one commit rebased on
  v0.9.47 (`19d413b`, pushed), to open;
- the autopilot: a win in Touring Car, races in Sega Rally and Daytona.

## Pieces

| Piece | Where | State |
|:--|:--|:--|
| Drive board answers on port D / IN2 | `src/hw/model2*.cpp`, `src/hw/indy_drive_reply.h` | done: Indy family, Sega Rally, Daytona |
| Drive board DIP strength setting | Wheel tab, `wheel_drive_dip` | done |
| Indy family motor response | `wheel_indy_deadband`, `wheel_indy_full_power` | done: in proportion to the power (0 and 63 by default) |
| Drive board model in the save states | the save state's host section | done |
| Indy 500 EEPROM centre | `indy_board_centre`, `src/hw/indy_drive_reply.h` | done: `$FF` read as `$80` (upstream's, in place of the EEPROM repair) |
| Over Rev / Super GT 24h board settings | README, `data/games.xml` | documented |
| Super GT 24h's pedals by its I/O TYPE | `Model2B::analog_value`, `repair_sgt24h_io_type` | done, upstream too (PR #23); a blank backup RAM's I/O TYPE becomes A |
| `--agent` mode: headless, driven over stdin/stdout | `src/agent.cpp` | done; `tel` answers the telemetry; named holds (`set`/`get`), the force feedback's commands (`ffb`) and the board's ports (`servo`) as in Supermodel's agent mode; `--agent-device` reads vJoy and drives its FFB |
| Python client of the agent mode | `sm2emu/sm2agent.py` | done |
| Telemetry: per-game RAM decoding, TCP server | `src/telemetry/`, `--telemetry PORT` | done for Indy 500 |
| Indy 500 RAM map | below | done for what driving needs |
| Track maps (the AI cars' laps, averaged) | `sm2emu/record_laps.py`, `track_map.py` | the three Indy 500 courses |
| Race start states (coins, course, gearbox) | `sm2emu/to_race.py` | the three courses, AT or 6MT |
| Drivers: pure pursuit, curvature driver, optimised line | `sm2emu/pursuit.py`, `line_opt.py` | pursuit laps the oval; CMA-ES running |
| Reinforcement learning (PPO, PyTorch, CUDA) | `sm2emu/train_ppo.py`, `env_indy.py`, `ppo_model.py` | two runs, no progress in places (see the log) |
| Planner: look-ahead with the emulator itself | `sm2emu/plan_race.py`, `plan_beam.py` | **wins the oval from 33rd**; its races are the learning's data |
| Learning from the planner: cloning, candidate scores | `sm2emu/bc_train.py`, `q_train.py`, `policy_q.py` | written; data being made |
| Picture-only driver | `sm2emu/visual_driver.py`, `bc_train.py --visual`, `q_train.py --visual` | written |
| Autopilot through vJoy, standalone | `sm2emu/run_vjoy.py`, `vjoy.py` | done: drives a race, logs drive bytes and FFB |
| Autopilot through BackForceFeeder | `sm2emu/bff/autopilot.lua`, `run_vjoy.py --bff` | written, not tested (it needs BFF on the cabinet) |
| Touring Car telemetry | `src/telemetry/stcc.cpp`, `sm2emu/stcc_telemetry.py` | cars, speed, heading, segment, time left; course and laps to find |
| Touring Car start, map | `sm2emu/stcc_to_race.py`, `runs/stcc_country_line.npz` | automatic (6MT wants shifts), qualifying, race start states; Country Circuit mapped; planner 4th from 8th |
| Sega Rally telemetry, map, vJoy | `src/telemetry/srally.cpp`, `sm2emu/srally_telemetry.py` | positions, headings, progress; the Desert mapped; FFB compared |
| Daytona telemetry, map, vJoy | `src/telemetry/daytona.cpp`, `sm2emu/daytona_telemetry.py` | positions, headings, progress; the beginner course mapped; FFB compared |
| FFB comparison, all four boards | `sm2emu/ffb_compare.py` | done: the tables below |
| FFB bench in agent mode: power-up to race, three ways of driving, the whole chain checked | `sm2emu/ffb_race.py`, `ffb_campaign.py`, `analyze_ffb.py` | done for the four boards: below |
| Super GT 24h telemetry | `src/telemetry/sgt24h.cpp` | all 14 cars, the player's orientation, gear, revs, positions, laps, clocks; checked against the screen |
| Over Rev telemetry | `src/telemetry/overrev.cpp` | all 8 cars, the player's angles, gear, revs, laps, clocks, positions counted from progress; checked against the screen, the Model 2B sets too |

## Running it

Python 3 with numpy, Pillow, PyTorch (CUDA when there is one) and cma; vJoy
2.1.9 for the cabinet's controls. The tools run from `sm2emu/`; the
emulator they drive is `sm2emu/bin/sm2-emu.exe` when there is one (a copy,
so that `build\bin` can be relinked), else sm2-emu's `build\bin\sm2-emu.exe`,
in the checkout `SM2_EMU_REPO` names (by default
`Racecab\emulateurs\git_sm2-emu_njz3`, beside BackForceFeeder).

The autopilot on its own, in the emulator's window, through vJoy, logging
the drive board's bytes and the force feedback sent to vJoy (BackForceFeeder
must not run):

```
python run_vjoy.py --policy pursuit                       # the plain pursuit
python run_vjoy.py --policy q:runs/dagger1/q8.pt          # the learnt network
python run_vjoy.py --policy visual:runs/visual.pt         # from the picture alone
python run_vjoy.py --state states/indy_oval_mt.state      # straight to the race
python run_vjoy.py --game stcc --policy pursuit           # Touring Car
```

Logs in `runs/vjoy/`: `race.csv` (each frame: the car, the controls, the
drive bytes, the FFB packets), `summary.txt`. `ffb_compare.py
runs/vjoy/race.csv` sets the board's commands against the forces sent.

The force feedback bench, headless in agent mode with its virtual wheel
(vJoy and BackForceFeeder left alone; several at once), the emulator
sm2-emu's `build\bin\sm2-emu.exe` unless `SM2_AGENT_EXE` names another:

```
python ffb_campaign.py runs/bench --jobs 6       # 4 games x 3 races, then analysis.md
python ffb_race.py daytona runs/bench/day1 --weave 0.35 3
python ../analyze_ffb.py runs/bench              # the analysis shared with Supermodel's bench
```

Each race: `drive.tsv`, `servo.tsv`, `ffb.tsv`, `telemetry.tsv`, `summary.txt`,
`bench.json` (the settings): the race format both emulators' benches write
(BackForceFeeder's `autopilot/README.md`).

`run_autopilot.bat [indy500|stcc|srallyc|daytona]` does the same with each
game's best driver (Indy 500: the learnt network `runs/q_win1.pt` from the
race's start, in lockstep: the winning race in the emulator's window).

On the cabinet, through BackForceFeeder: put `bff/autopilot.lua` in BFF's
Scripts folder and load it (see its header); it starts
`run_autopilot.bat indy500 --bff` itself, which launches the emulator and
drives it through the script, BFF keeping vJoy and the motor; unloading the
script stops it. Not tested yet.

The Indy 500 win, again (agent mode, headless; from `sm2emu/`):

```
python eval_race.py states/indy_oval_mt.state indy_oval_line.npz q:runs/q_win1.pt
python eval_race.py states/indy_oval_mt.state indy_oval_line.npz q:runs/q_win1_visual.pt
python replay_win.py states/indy_oval_mt.state indy_oval_line.npz runs/win2 runs/plan4_replay runs/beam3 --continuous
```

The first two race the learnt networks alone (from the telemetry, from the
picture); the last replays the planner's race and saves its pictures, the
drive board's bytes and its decisions. The start state was made by
`to_race.py indy500 "" states/indy_oval_mt.state --mt`; do not make it again
over the old one: the NVRAM has changed since (the lap record among others)
and the race from a new state would not be the same. Making it from nothing:
`line_opt.py` (the line, about an hour), `plan_race.py --pass-set --horizon 8
--position-m 40` (the race, about 25 minutes), `plan_beam.py --beam 3 --pilot`
for the last lap (15 minutes), `q_train.py --path` (a minute on the GPU).

In agent mode, headless: `eval_race.py` (a whole race, any driver),
`plan_race.py`/`plan_beam.py` (the planner), `dagger.py` (the network's
training), `replay_win.py` (replays a planned race and checks it).

## The drive boards' DIP switches

From the three EPROMs' programs (BackForceFeeder `docs/EPR-*`), the switches
that set the motor's strength. A switch ON reads 0. On EPR-18261's board,
measured on the cabinet Racecab1 (13 October 2026), switch n is bit 8 - n:
its strength switches are 1-1 to 1-3, not 1-6 to 1-8 as read before.

| Board | Switches | Effect | Maximum |
|:--|:--|:--|:--|
| Sega Rally (EPR-17891) | 1-3 | 0 to 7 added to every torque's power (1..32) | all three ON: +7 |
| Daytona USA (EPR-16488A) | 1-3 | (0 to 7) x 4 added to every power | all three ON: +28 |
| Indy 500 family (EPR-18261) | 1-1, 1-2, 1-3 (bits 7, 6, 5: +4, +2, +1) | 0 to 7 added to every strength `(2n + base) * 2` | all three ON: +7 |

sm2-emu's `wheel_drive_dip` (0..7, the Wheel tab's "Drive board DIP strength")
is that number: 7 is the maximum on every board. Other switches the programs
read: Sega Rally 4 and Daytona 4 skip the power-up calibration; Indy 2-1 turns
the damping off; 1-7/1-8 (Indy) and 7-8 (Rally) select test modes.

## Indy 500: the work RAM

i960 addresses (the work RAM starts at `00500000`). Found by recording the RAM
while driving (`discover.py`, `follow_record.py`) and by differences between
states (`to_race.py` states of each course and gearbox).

| Address | Type | What |
|:--|:--|:--|
| `0053C7AC` | 33 x `0x1E4` | the cars, the player first (28 raced on Bay Side Street and Highland Raceway) |
| car `+000/+004/+008` | float | x, y, z, metres |
| car `+00C` | i32 | roll, a turn = 65536 (the oval's banked left turns read < 0) |
| car `+010` | i32 | yaw, negated: heading = -v x 2pi/65536, atan2(dx, dz) of the nose |
| car `+014` | i32 | pitch [guess], same unit |
| car `+030` | float | speed, metres a frame (x 57.5: m/s) |
| car `+0E4` | u32 | lap << 20, sector << 16, the sector's fraction (lap 0: the grid and the rolling start; race laps + 1: finished) |
| `0053C332` | u16 | time left, frames (also counts the menus down) |
| `0053C350` | u8 | course: 0 Bay Side Street, 1 Indy 500 (the oval), 2 Highland Raceway |
| `0053C360` | u8 | steering, raw ADC |
| `0053C5B9` | u8 | gearbox: 0 6-speed manual, 1 automatic |
| `0053C5BC` | u8 | gear, 0 = first |
| `0053C700` | float | engine revs |
| `005441E4` | u32 | the player's time in the lap, frames |
| `00544108` | u8 | steering as the game uses it (calibrated, inverted) |
| `005449A0` | 3x3 float + xyz | the camera, world from camera: a chase camera, up to 0.3 rad late in the turns |

No velocity vector in the car structure: the direction of travel comes from
successive positions. The sectors are not of equal length: the telemetry
turns sector + fraction into a fraction of the lap's length with each
course's table (from the maps).

| Course | Laps | Cars | Sectors | Lap (map) | Record lap (EEPROM) | Starting time |
|:--|:--|:--|:--|:--|:--|:--|
| Indy 500 (oval) | 4 | 33 | 7 | 4052 m | 37.248 s | 65 s, about +33 s a lap |
| Highland Raceway | 3 | 28 | 8 | 4607 m | 50.613 s | 57 s |
| Bay Side Street | 3 | 28 | 8 | 4906 m | 1'02.509 | 66 s |

## Touring Car: the scratch RAM

The Model 2C game keeps its race in the scratch RAM at `00200000` (256 KB):
its work RAM at `00500000` stays all zero. The agent's `mem ADDRESS LENGTH`
reads either; the telemetry decoders get both (`telemetry::Memory`).

| Address | Type | What |
|:--|:--|:--|
| `0020A900` | 8 x `0x58` | the cars, the player first |
| car `+00/+04/+08` | float | x, y, z |
| car `+0C/+10/+14` | float | velocity, units a frame (not the speed: its x is what first looked like it) |
| car `+1C` | float | heading, radians, = -atan2(dx, dz) |
| car `+50` | u16 | the track segment (Country Circuit: 300 a lap, about 8 units each) |
| car `+52` | i16 | segments driven since the line (negative on the grid) |
| `002022EC` | u32 | time left, frames; the screen shows it divided by 64 |
| `00208880` | object | the player's car (position at +08, speed at +BC); `00200B00` a list of 0x40-byte nodes pointing at it |

The game: 1 coin a credit; the car chosen with the wheel, the manual gearbox
by holding the brake while the gas confirms; a one-lap qualifying (40 s), then
Round 1 on Country Circuit, 2 laps, 8 cars. A qualifying not finished in time
is not the end: "DISQUALIFIED", the race starts last.

## Daytona USA: the work RAM

40 entries of 0x300 bytes from `00505100`, the player's first (it alone moves
with our throttle; a structure at `00501580` follows it: the camera), then
the AI cars: position +9C/+A0/+A4, the four wheels from +178, heading +C8
(i32, a turn is 65536, negated), the place in the race +EC (from 0), laps
done +F0 (also `005010E0` for the player), progress +1AC (16.16, a segment
every 8.3 units, 196 a lap of the beginner course, back to 0 at the line). Map:
`runs/daytona_beginner_line.npz` (1572 units). Decoders: `src/telemetry/daytona.cpp`,
`sm2emu/daytona_telemetry.py`. The clock is still to find. The game: coins held a second, CIRCUIT SELECT, MISSION SELECT
(automatic, manual 4), 40 cars, 8 laps of the beginner course from 40th.

## Sega Rally: first look

2 coins a credit; SELECT GAME (Championship, Practice), CAR SELECT (Celica,
Delta; automatic or manual), then the Desert stage, 15th of 15, 60 s. The
work RAM (Model 2A) holds 16 entries of 0x100 bytes from `00500000`: position
at +14/+18/+1C (x, y, z); entries 1 to 7 move with the cars, their heading at
+A4 = atan2(dz, dx); entry 0 is the player's (it speeds up with the gas),
laid out otherwise: heading at +3C = atan2(-dx, dz); 8 to 15 stay still: seven
slots for the AI cars near the player. Progress along the course: AI, a 16.16
number at +88 (also +E4), a segment every 10.3 units; the player, a 16.16 at
+84, a segment every 4.8 units. The clock and the position are still to find
(no counter going down a frame or a second in u16/u32/float/BCD in 17 s of
recording). Map of the Desert: `runs/srally_desert_line.npz` (a loop of 300
AI segments, 3145 units); decoders `src/telemetry/srally.cpp`,
`sm2emu/srally_telemetry.py`; the pure pursuit drives it (60 s on the
clock: the first checkpoint is not reached yet).

## Super GT 24h: the work RAM

Found on 14 October 2026 by recording the work RAM while driving (from a
save state at the start of a race) and matching it with the screen: the
speedometer, the clocks, the position, the lap, the gear and the tachometer.
Metres; the game counts 60 frames a second (its 2400 frames are 40 s), so its
speeds are the speedometer's and 4 % above those from successive positions.

| Address | Type | What |
|:--|:--|:--|
| `00518424` | 14 x `0x1728` | the cars, the player last (`0052B12C`) |
| car `-0C` | u8 | race position, 0 leading |
| car `-04` | u8 | laps done |
| car `+00` | float | distance along the course, 3000 a lap; the grid is just past the line (the player 14th at 46) |
| car `+0C/+10/+14` | float | x, y (up), z |
| car `+34` | s32 | heading, 65536 a turn, = -atan2(dx, dz) (also +40) |
| car `+84C` | float | speed, metres a frame |
| player `+C8` | u8 | gear, 0 first (also `005111A8`) |
| player `+220` | u16 | revs (848 idle, about 8500 at the shifts) |
| player `+764` | 9 floats | orientation, world from car, row-major (the AI cars leave it zero) |
| player `+168..+194` | floats | the four wheels' x, y, z |
| `0051119C` | u16 | the speedometer, km/h |
| `00510948` | u32 | game state: 0 boot, 1 attract mode, 7 coin, 2 car select, 3 race and its countdown, 4 game over |
| `00510F2C` | u16 | lap time, frames |
| `00510F64` | u8 | the player's laps done; `00510F6C` the race's laps (5); `00510F7C` its position, 0 leading |
| `00510F74` | s16 | time left, frames (negative after the game over); checkpoints at about 1750 and the line |

The AI cars run up to about 480 km/h by their own speed (+84C matches their
motion): the game keeps them with the player. Line from the AI cars, driven by
a pure pursuit (scratchpad `sgt_pilot.py`): a lap in 3360 frames, 14th to
12th.

## Over Rev: the work RAM

Found the same way (Challenge Cup, round 1: 8 cars, 3 laps, finish 5th or
better). Half-metres, 60 frames a second. The Model 2B sets (`overrevb`,
`overrevba`) give the same values at the same addresses.

| Address | Type | What |
|:--|:--|:--|
| `0059E6A8` | 7 x `0x2A8` | the AI cars |
| AI `+050` | u8 | laps done |
| AI `+0A4` | s32 | heading, 65536 a turn: heading = pi/2 - value |
| AI `+0E0` | float | speed, half-metres a frame |
| AI `+100/+104/+108` | float | x, y (up), z; +10C.. the frame before |
| AI `+1EC` | float | distance, from its own origin |
| AI `+1F0` | float | distance from the start line, negative on the grid; 2964 a lap |
| `00590DE4` | s32 | the player's roll (orient()'s roll is minus it); +10 pitch, +14 heading = -value (65536 a turn) |
| `00595A5C` | u8 | the player's gear, 1 first |
| `00595A7C` | float | its revs (1200 idle, about 7000 at the shifts) |
| `00595AA4` | float | its distance into the lap, back to 0 at the line |
| `00595AB8` | float | its speed, m/s |
| `00595B24..` | floats | its wheels: x at +00, y at +10, z at +20, four each |
| `00595C04/08/0C` | float | its x, y, z |
| `00595C9C` | s32 | its laps from the start line: -1 on the grid, which is 1647 behind it |
| `00595CAC` / `00595CB0` | u16 | race time / lap time, frames |
| `00595CE0` | u8 | seconds left; `00595CE4` laps done |
| `00595CE8` | float | the position the display shows, sliding from one to the next |
| `00595D18` | u8 | the race's laps (3) |
| `0051D33C` | float | the lap's length, 2963.8 |

No integer race position: the decoder counts it from the cars' progress
(the player's laps from the line plus its distance into the lap over the
length; the AI's +1F0 over the length), which matched the display on all
689 settled samples of a race. Attract mode and menus: the clocks at zero
(the decoder answers nothing then). Pursuit on the AI line, by its nearest
point (scratchpad `ovr_pilot.py`): 3 laps, 8th to 6th.

## The telemetry server

`sm2-emu --telemetry 9100` (or `telemetry_port` in the ini) serves one JSON
line a frame to every TCP client, while a race runs. Per car: position,
heading, pitch, roll, a rotation matrix from them, speed, velocity and
acceleration (from successive positions), lap, sector, progress (laps,
fractional, by distance), position in the race; for the player also gear and
revs. Per race: course, laps, gearbox, racing/finished, time left, lap time,
the camera's matrix. `sm2-emu --agent` answers the same line to `tel`.
Tested: 1791 frames in 75 s of the attract demo on Bay Side Street.

## What the game's menus and car need

- The menus pick what the wheel points at, but ignore a wheel that jumps
  there in one frame: turn it gradually (40 frames) and hold it while
  pressing the gas. Highland Raceway is left, Bay Side Street right; the 6MT
  gearbox right.
- 6MT: the car starts and stays in 6th on the oval, 110 m/s with a car to
  draft, 103.4 alone. Automatic: drops to 2nd-3rd in the turns, 107 at best.
- The steering, measured on the straight at 97 m/s with short pulses: linear
  up to about 0.6 (0.93 rad/s, 9 g), no slip, no speed lost. In a sustained
  turn the car slides outward (nose 0.2 rad inside the travel), loses 103 to
  91 m/s and, when it reaches the wall, 15 more.
- The AI leader keeps the same distance ahead whatever the player does: its
  laps followed ours (37.6 to 42.4 s) over a race (rubber band).

## Measurements

### Agent mode speed

Indy 500, headless, one instance: about 71 frames a second (1.2 x real time)
at boot, about 2.2 x real time in a race. 16 logical CPUs: 12 instances in
parallel run at about real time each. No vsync and no pacing in agent mode:
a step runs as fast as the host goes (the bench, with the telemetry and the
steering read every frame: Daytona 132 frames a second alone, 2.3 x). A scaled picture costs 4 ms, the
telemetry window (34 KB of RAM) 0.2 ms.

### Indy 500, drive board commands in a race (autopilot, 15 minutes, 7 games, 3 courses)

`01`, `03`, `07`, `71`, `73` (idle, start, base); `30`, `35` (springs); `40` with
`B7 A3` (anti-spring and vibration, the grass); `50`-`57`, `60`-`64` (pushes:
the steering's feel, `55`-`57` on hard impacts); `10`.

### Indy 500 through vJoy (run_vjoy.py, a whole race on the oval, pursuit)

Finished 22nd (the emulator at 0.65 x real time, the machine busy with the
optimiser). Drive bytes: `35` 2235 times (the spring, every frame or so),
`A0`/`B0` 2227, `40` 14 with `A3`/`B7` 15, pushes `50`-`54` 22 to 38 times
each, `55`-`57` 11, `60`-`64` 28. Force feedback to vJoy: 2099 constant force
updates, magnitude -10000..10000 (the whole range), |m| 4619 on average, 8461
at the 95th percentile; one sine effect created, never given a magnitude.

### Force feedback: what the game asked against what sm2-emu sent (Indy 500)

`ffb_compare.py runs/vjoy_test/race.csv`, the vJoy race above (the motor
curve at its defaults: `wheel_indy_deadband` 6, `wheel_indy_full_power` 32;
DIP 0, strength 100 %). The board's strength from its program, (2n + base) x 2
out of 63 with base 3 (`73`); sm2-emu's constant force in % of the wheel's
maximum while each command is in force.

| Command | Effect | Frames | Board | sm2-emu, mean size | sm2-emu, 95th pct. |
|:--|:--|--:|--:|--:|--:|
| `35` | spring | 8885 | 41 % | 33 % | 77 % |
| `40` (+ `B7 A3`) | anti-spring, vibration | 53 | 10 % | 51 % | 81 % |
| `50` | push | 39 | 10 % | 1 % | 0 % |
| `51` | push | 44 | 16 % | 8 % | 8 % |
| `52` | push | 47 | 22 % | 23 % | 23 % |
| `53` | push | 72 | 29 % | 38 % | 38 % |
| `54` | push | 140 | 35 % | 54 % | 54 % |
| `55` | push (hard impact) | 9 | 41 % | 68 % | 69 % |
| `56` | push (hard impact) | 8 | 48 % | 85 % | 85 % |
| `57` | push (hard impact) | 2 | 54 % | 100 % | 100 % |
| `60`-`64` | push, other side | 52 | 10-35 % | 0-54 % | |

sm2-emu's curve is much steeper than a motor fed the board's power: the
weak pushes come out weaker (`50`: 1 %), the strong ones twice as strong
(`57`: 100 %), and the anti-spring of the grass five times the board's. That
fits the player's report against M2PAC (the grass too strong). `5X` and `6X`
come out with opposite signs, as they should; which is physically left
depends on the wheel's driver.

#### Corrected (2026-10-13): the torque in proportion to the board's power

The board model (`src/osd/indy_board.cpp`) follows the program: strength
(2n + base) x 2 + DIP, sine springs with damping, the vibration. The
difference was after it: the curve `wheel_indy_deadband` 6 /
`wheel_indy_full_power` 32 (nothing up to 6, full from 32), added earlier for
the player's report against M2PAC while two real errors pushed the same way
(the board's centre at `FF` for want of an answer to `80`, and no DIP strength:
the cabinet is at the maximum, +7 on every force). Both are emulated now. The
board sends its two powers in 6 bits each (CW 0-5, CCW 0-5) to the motor drive
board 838-11966: the torque goes in proportion, 63 full. The defaults are now 0
and 63 (the code before the curve did that). The same race again
(`runs/vjoy_ffb_linear`):

| Command | Board (base 2) | sm2-emu, mean |
|:--|--:|--:|
| `50`-`56` | 6, 13, 19, 25, 32, 38, 44 % | 6, 13, 19, 25, 32, 38, 44 % |
| `60`-`63` | 6, 13, 19, 25 % | 6, 13, 19, 25 % |
| `35` spring | up to 38 % + damping | 24 % (95th pct. 44 %) |
| `40` + `B7 A3` | 6 % + the vibration (8 or more) | 14 % (95th pct. 22 %) |

Base 2, not 3: the race starts from a save state, after the game's `73`, and
the board model is not in save states; in play from power-up it gets the
`73`. To feel it as on a cabinet whose DIP switches are at the maximum:
"Drive board DIP strength" 7 in the Wheel tab (`wheel_drive_dip = 7`).

### Force feedback, Touring Car (`ffb_compare.py runs/vjoy_stcc/race.csv --base 1`)

Through vJoy (`run_vjoy.py --game stcc --policy line:runs/opt_stcc/best.json`):
coins, start, automatic, name, the qualifying, Round 1 (finished 6th, the
optimised line without the planner), Round 2 begun. Touring Car never sends
`7X`: the base stays 1.

| Command | Effect | Frames | Board | sm2-emu, mean size | sm2-emu, 95th pct. |
|:--|:--|--:|--:|--:|--:|
| `30` | spring, n = 0 (menus) | 277 | 3 % | 1 % | 0 % |
| `34` | spring, n = 4 (course screen, time's up) | 3258 | 29 % | 46 % | 46 % |
| `38` | soft spring, held through the race | 2212 | 2 % | 14 % | 77 % |
| `51`-`57` | the steering's feel, pushes | 676-1785 | 10-48 % | 5-85 % | 33-92 % |
| `61`-`67` | the same, other side | 49-478 | 10-48 % | 3-89 % | 15-92 % |

The race's `38` is nearly nothing on the board ((2 x 0 + 1) x 2, halved: 2 %),
and sm2-emu's board model does the same (`src/osd/indy_board.cpp`: the
quartered sine capped at the strength, plus the damping); the 77 % comes from
the attribution: Touring Car interleaves its pushes of one to three frames
with `38`, and the network outputs only report changes, so frames counted
under `38` carry the pushes' force. The pushes have
the same steep curve as Indy 500's (`57`, held at full lock, 48 % on the
board, 85 % here).

### Force feedback, Sega Rally (`ffb_compare.py runs/vjoy_srally/race.csv --board rally`)

Through vJoy (`run_vjoy.py --game srallyc --policy pursuit:2.0`): coins,
Championship, the Celica automatic, two minutes of the Desert. The board
gives a torque of power (cmd & 1F) + 1 out of 32, `80`-`9F` one way,
`C0`-`DF` the other; sm2-emu's constant force, by power (groups of 4):

| Power | Board | sm2-emu, mean (one side / the other) |
|--:|--:|:--|
| 4 | 12 % | 8 % / 8 % |
| 8 | 25 % | 26 % / 24 % |
| 12 | 38 % | 37 % / 35 % |
| 16 | 50 % | 53 % / 51 % |
| 20 | 62 % | 69 % / 62 % |
| 24 | 75 % | 83 % |
| 32 | 100 % | 99 % / 100 % |

Linear and faithful (the player found Sega Rally faithful at strength 100);
the chopping `15` (rough ground) lowers the mean as on the board. Indy 500's
and Touring Car's curves above are the ones that are not.

### Force feedback, Daytona USA (`ffb_compare.py runs/vjoy_daytona/race.csv --board daytona`)

Through vJoy (`run_vjoy.py --game daytona --policy pursuit:2.5`): coins,
start, the beginner course, the automatic, two and a half minutes of racing.
The board's powers out of 63 from its program (pushes and anti-spring
2n + 10 + base, springs up to 2n + base; the base here only the boost of
`71`, 2: the breakaway the real board measures at power-up is not counted).

| Command | Effect | Frames | Board | sm2-emu, mean | sm2-emu, 95th pct. |
|:--|:--|--:|--:|--:|--:|
| `38`-`3C` | spring, wide dead zone | 103-2961 | 3-16 % | 10-26 % | 12-76 % |
| `42` | anti-spring | 4 | 25 % | 1 % | 53 % |
| `52`, `57` | push | 18, 271 | 25 %, 41 % | 46 %, 22 % | 53 %, 86 % |
| `60`, `62` | push, the other way | 39, 18 | 19 %, 25 % | 78 %, 94 % | 82 %, 95 % |

The springs come out 1.5 to 2 times the board's formula (whose breakaway
would add to it on the cabinet), the short pushes much stronger; pushes of
a few frames carry the ramps and springs around them in this count. The
player found Daytona faithful at 100 %: to measure on the cabinet before
changing anything. (15 October: the board's percentages here are out of 63,
but Daytona's board and panel are Sega Rally's and sm2-emu gives both the
wheel's maximum at a power of 32; the bench below.)

### Force feedback bench in agent mode (`ffb_campaign.py`, 15 October, `runs/bench1`)

Each game from power-up with a fresh NVRAM (the board gets the game's
start-up commands: Indy 500's base 3 from its `73`), its menus passed, then
150 s of racing or up to the game over (Sega Rally: about 70 s), three times:
the pursuit alone, a weave over it (0.35 of full lock, 3 s: the springs
through their travel), hard turns at random (slides, walls). Agent mode's
virtual wheel: strength 100 %, DIP 0, panel spring 20 %, a 270-degree wheel
for a lock of 240. 12 races, 26 minutes, six at once. The whole tables:
`runs/bench1/analysis.md`.

| Game | Commands written / lost | Frames with 2+ writes | Force records = the model's outputs replayed |
|:--|--:|--:|--:|
| Indy 500 | 40 218 / 0 | 3 (each a repeat) | 9 891 / 9 891 |
| Touring Car | 34 509 / 0 | 0 | 8 113 / 8 113 |
| Sega Rally | 20 477 / 0 | 0 | (no model) |
| Daytona | 34 911 / 0 | 3 (each a repeat) | 7 042 / 7 042 |

The games write one byte a frame: the model takes each, in order, so
nothing needs queueing as Supermodel's Z80 does (it runs after the frame).
The replay works the model's last outputs of each frame into a force as
`src/osd/input.cpp` does (the clutch's braking and the panel spring
included) and finds every record sent, value and frame.

- **EPR-18261** (Indy 500 base 3; Touring Car base 1: it sends `00`, an idle
  centring, and never `7x`): every push is the formula's power to the unit
  (Touring Car `51`-`57` 6 to 30, Indy 500 `50`-`54` 6 to 22), sent as that
  power out of 63. The springs: Indy 500's `35` (its cap 26) gives 3 % at the
  centre (0-3 counts), 11 % at 4-7, 26 % at 8-15, 35 to 39 % from 16 (15 degrees of
  the wheel); Touring Car's `34` (cap 18) 29 % from 16 counts on, its soft
  `38` 2 to 13 %.
- **Daytona**: the model's power is the formula plus its distance term
  (`52` 16 -> 16.7, `57` 26 -> 30.5, `62` 16 -> 17.4), the breakaway 0 at the
  panel spring's 20 %. Its board and panel are Sega Rally's (the BackForceFeeder
  analyses of EPR-16488A and EPR-17891: the same motor byte on port `46`, an
  AC motor and a power clutch), so it has Sega Rally's scale, a power of 32
  the wheel's maximum: the pushes give 81 to 94 % of that rate (`57` 81 %,
  `62` 62 %, `52` 50 % sent), the springs about 116 %, the light panel spring
  adding to or taking from them by the wheel's side: `38`-`3F` nothing within
  7 counts, then 19 % at 8-15, 32 % at 16-31, 44 % at 32-63, 63 % at 64-127.
- **Sega Rally**: linear, the command's power out of 32 (4: 10 %, 16: 53 %,
  32: 97 %); the chopping lowers the mean.

The force over the races' time (|constant force|, every frame):

| Game | Median | 95th percentile | Time above 50 % |
|:--|--:|--:|--:|
| Indy 500 | 29 % | 43 % | 0.5 % |
| Touring Car | 29 % | 48 % | 0.3 % |
| Sega Rally | 20 % | 67 % | 12.2 % |
| Daytona | 27 % | 67 % | 16.0 % |

The chain from the game to the wheel is exact. Daytona and Sega Rally, one
board and one panel, have one scale and come out alike: 16 % and 12 % of
their races' time above half the wheel's force, where the Indy family's
direct-drive panel, its own board and scale, stays below 1 %. The player found
both faithful at 100 %.

### Clean laps on the oval (no traffic: `indy_oval_solo.state`, the pack 10 s ahead)

| Driver | Lap |
|:--|:--|
| pure pursuit, gain 1.6 | 42.19 s |
| pure pursuit, gain 3 | 41.77 s |
| pure pursuit, gain 4 | 41.43 s |
| curvature driver (feed-forward, feedback, speed plan) | 41.6 to 48 s |
| CMA-ES on line, throttle and gains (12 knots), generation 22 | 40.83 s |
| the game's record | 37.25 s |

### Whole races from the start (agent mode, `eval_race.py`, `plan_race.py`)

| Driver | Laps | Result |
|:--|:--|:--|
| pure pursuit, gain 3 | 39.0, 39.9, 40.8 | out of time, 23rd |
| optimised line (CMA-ES, generation 30) | 39.2, 39.1, 39.9, 40.4 | finished 14th, 5.5 s left |
| PPO v2 (evaluations at updates 9 to 39) | | finished 16th, 26th, 27th, 28th |
| planner (12 candidates, 5 s ahead, 1.5 s kept) | about 36, 37.5, 37.5, 39 | **finished 5th**, 10.6 s left; the four ahead 5 to 141 m in front |
| cloned from that one planned race (2206 decisions) | 44.2, 47.6 | out of time, 22nd |
| picture alone (bc_train.py --visual, one planned race) | 68.3 | out of time, 33rd (laps alone) |
| planner + slipstream candidates (6 s ahead, 1 s kept) | | finished 5th again: stuck behind four cars in file |
| planner, passing candidates, 8 s ahead, from 6th at 2.49 laps | | **2nd** |
| same from the start (crashed at 3.09 laps, 5th, on a locked file; replayed from its log) + beam search of 2 for the end | | **2nd, 2 m behind the winner** |
| the same + beam search of **3** for the last 0.9 lap | 36.94, 38.26, 38.47, 40.07 | **WON: 1st of 33**, 2'31"069, best lap 36"518 (the record: 37"248), 10.3 s left |

The win, replayed from the green flag in one emulator (`replay_win.py
states/indy_oval_mt.state indy_oval_line.npz runs/win1 runs/plan4_replay
runs/beam2`): 20th after one lap, 10th after two, 5th after three, first at
3.33 laps; the game's own screen: "FINISH 1st". Pictures in `runs/win1/pics`,
the drive board bytes in `runs/win1/drive.txt`, the demonstration (2219
decisions with pictures) in `runs/win1/demo.npz`.

### The learnt network (DAgger on the planner's scores, `dagger.py`, `runs/dagger1`)

Each round: the network races alone from the start; at a state every 2 s the
13 candidates are tried with the emulator (8 s ahead) and scored; the rows
join the data; the network learns the planner's preferences again
(cross-entropy on a softmax of the scores, 25 points apart).

| Round | Data (decisions) | The network alone |
|:--|--:|:--|
| 1 | 174 (planner runs) | out of time at 2.97 laps, 25th |
| 2 | 239 | **finished, 17th** |
| 3 | 319 | finished, 13th |
| 4 | 398 | finished, 13th |
| 5 | 477 | finished, 21st |

Not steady: with a few hundred decisions the network learns its data (59 %
the planner's choice) but not new situations (17 %). The second run with
network roll-outs in its labels did worse (22nd, out of time: two kinds of
labels mixed) and was stopped.

### From the picture (`q_train.py --visual`, `runs/q_visual1.pt`)

The same choice among the candidates, made from the picture alone (64 x 48,
grey, from the emulator's frame) and the speed, trained on the 695 scored
decisions that have their picture: the whole race from the start, **finished
16th** (laps 38.2, 39.7, 40.1, 42.0). The steering under the choice is still
the pursuit of the map. Steering itself from the picture (`bc_train.py
--visual`, one race of data): a lap in 68 s, out of time.

### The network that wins (`q_train.py --path`, `runs/q_win1.pt`, `runs/q_win1_visual.pt`)

The winning race made again with one pilot all along (`plan_beam.py --pilot`
from the state and pilot the first part left, `replay_win.py --continuous`:
1st, 11.2 s left), its 157 decisions saved (features, picture, candidate).
The network learns them (weight 20) with the 1001 scored decisions; it
reproduces 100 % of the race's decisions. Alone, in agent mode, from the start:

| Network | Laps | Result |
|:--|:--|:--|
| from the telemetry (`q_win1.pt`) | 36.94, 38.26, 38.47, 39.23 | **1st of 33** |
| from the picture and the speed (`q_win1_visual.pt`) | 36.94, 38.26, 38.47, 39.23 | **1st of 33** |
| `q_win1.pt` through vJoy, in the emulator's window (`run_vjoy.py --state states/indy_oval_mt.state --policy q:runs/q_win1.pt`) | (wall clock 60, 63, 56 s) | 31st, 29th, 23rd after three laps, the run stopped |
| the same, `--sync` (the emulator's lockstep) | | **1st of 33**, 11.2 s left: the same race, frame for frame |

Through vJoy the controls arrive a frame or two late and the race leaves the
learnt path at once; from there the network is as good as its generalisation.
The lockstep closes the gap. What it took, each found by comparing the
game's inputs frame by frame with agent mode's (the telemetry now carries
them: `"inputs"`): the emulator waits for its driver before the first frame
and for an acknowledgement after each telemetry frame (250 ms at most, 5 s
the first), then pumps the window messages, handles the joystick events (an
axis counts once it has moved) and reads the inputs again; the runner builds
its driver before the emulator starts, holds the controls four frames as in
training, writes the telemetry's floats with 7 digits (4 rounded positions to
0.1 m), and sets each axis to the middle of the byte agent mode would give
(the control with 4 decimals, single precision), so vJoy's and SDL's last
bits cannot change it. Identical: 317 frames, then 1641, then the whole race.

### The winning network made robust to a late control (`dagger.py --latency 2 --path`, `runs/dagger3`)

From `q_win1.pt`, each round's race with the controls 0 to 2 frames late at
random (as through vJoy without the lockstep), labelled by the planner, the
winning race kept in the data: 9th, 13th, 7th, 9th, out of time, 6th, 11th,
20th. A little better than the networks that only generalise, nothing
decisive: the lockstep is what makes the win hold through vJoy.

### Reinforcement learning over the candidates (`train_options.py`, `runs/opt_ppo1`)

PPO where the action is one of the planner's 13 candidates, held a second
(about 160 decisions a race), from the network of DAgger round 4 (13th).
Episodes of 40 s from the pool, or the whole race (one in three). Each
update: 12 emulators, 32 decisions each, 47 s.

| Update | Whole race, the policy's best choices |
|--:|:--|
| 5 | finished 10th |
| 10 | finished 13th |
| 15 | finished 17th |
| 20, 25, 30, 35 | 13th, 16th, 15th, 20th: no progress; stopped |

Second run (`runs/opt_ppo2`): from the same network, a third of the episodes
starting in the last laps behind the leaders (22 states of the planner's
races, laps 2.5 to 4, places 1 to 6) and run to the flag, so the result
itself is rewarded: 15th at update 5, out of time at update 10; stopped (its
value function, learnt from nothing, pushes the policy about). Second run
(`runs/dagger2`): from round 4's network, all those rows, a point every 1.5 s,
and labels where, after the candidate's first second, the network itself
drives on to the horizon (`--rollout`: each choice valued by what the
network makes of it afterwards, a policy iteration).

### Touring Car (Country Circuit, automatic)

| What | Result |
|:--|:--|
| pure pursuit, qualifying | lap done, 8.9 s left of 40 |
| CMA-ES on line and throttle (6 a generation, 60 generations), qualifying from the state | 40.21 s to 31.10 s (countdown and standing start included) |
| the grid | 8th of 8 all the same (the AI qualify in 24.5 to 26.5 s) |
| speed decoded wrongly at first (+0C is the velocity's x): the look-ahead went negative; all redone | |
| CMA-ES again, the right speed | 31.23 s from the start, 30.61 s after 60 generations |
| the optimised line alone, the race | finished 6th from 8th (laps 26.8, 26.6 s) |
| the planner (passing candidates, 8 s ahead) | **finished 4th from 8th**; the three ahead in file, 90 to 150 units in front from 0.75 lap |
| beam search of 3 from 0.75 lap | 4th again: the three leaders lap at our pace; winning would take a faster car |
| the manual gearbox (gear at `0020462C`, paddles IN1 `10`/`20`; limits 20, 34, 42, 55, 71, 85 units/s), shifted at 96 % of each | the same qualifying lap to the hundredth as the automatic (which shifts sooner): no gain |

## Reinforcement learning, again (16 October): every course, telemetry and picture

The user's request (16 October): the reinforcement learning again, on the
new telemetry and a reduced picture, the autopilot driving frame by frame,
until it wins (first place or best time) at Daytona USA, Sega Rally, Indy 500
and Touring Car, and finishes every course of Sega Rally, Daytona and Indy
500; the manual gearbox, and Daytona's 4-2-4 (down two gears and back at
once) to throw the car into the turns. In BackForceFeeder's
`autopilot/sm2emu/rl/`, its own copy of the emulator in `rl/bin`:

| Piece | File |
|:--|:--|
| Start states, the manual gearbox, from power-up (Daytona's three courses, Sega Rally's Celica or Delta) | `menus.py` |
| The courses' lines: every car recorded (`tel`), binned by the game's own progress, the median; Rally's laps found by their period | `record_map.py`, `build_line.py`, `build_line_laps.py` |
| The base driver: a pure pursuit of the line, a speed plan from its curvature, the gears by the revs (H shifter `set Gear`, paddles) | `driver.py` |
| The environment and its two levels | `env.py` |
| PPO, the emulators in worker processes, one policy for several courses | `train.py`, `model.py` |
| A whole race, frame by frame, the policy's likeliest choices | `evaluate.py` |
| Sega Rally's championship, each stage on its own map | `rally_run.py`, `next_stage.py` |
| The planner: candidates tried from the saved race, the best driven | `plan_drive.py` |
| A planned race again, frame by frame, the force feedback recorded; set against the events | `replay.py`, `ffb_events.py` |
| The network learns the planner's choices | `plan_dataset.py`, `imitate.py` |
| The base driver's settings searched (cross-entropy) | `tune_base.py` |
| Pools of start states, cleaned of states off the course | `run_base.py --pool`, `clean_pool.py` |

The policy decides every 8 frames: where to be across the road (five lanes,
a fraction of the half-width either side of the line), how hard to go (0.85,
1, 1.2 times the speed plan), and the 4-2-4. The base driver turns that into
the steering, the pedals and the gear every 2 frames. It sees the telemetry
against the line (speed, slip, rates, gear and revs, the line ahead in the
car's frame, its width, the six nearest cars, place, laps, clock) and the
picture: 80 x 60, grey, the last two. Rewarded by the distance along the
line, the places, the finish (more for a win).

Two first tries were dropped: corrections to the steering and the pedals
every 2 frames (Gaussian, PPO) drifted (a steady left bias, the brake
cancelled: the noise on a pedal clipped at 0 brakes at random, and the mean
learnt against it), and a penalty on the telemetry's impacts made it slow
(Daytona's fire all the time). The discrete choices explore whole manoeuvres
and race by their likeliest value.

The agent mode got two commands for it: `tel PATH,...` (only those keys: 3 KB
a frame instead of 36) and `bands N` (the picture drawn on one thread: 12
agents at once, 721 frames a second against 669). A picture costs as much as
running the frames: 1270 frames a second for 12 agents without, about 700 with
one every 8 frames.

Found on the way: Daytona's expert lap is 990 segments (7527 units: past the
start straight a long loop), not the 600 the first recording reached; Sega
Rally's stage ends when its clock stops with the player's progress past
17 900, and the next stage's time depends on how much was left.

| Course | Base driver alone | Learnt (PPO) | Planner (`plan_drive.py`) |
|:--|:--|:--|:--|
| Daytona beginner (8 laps, from 40th) | out of time at lap 7, 28th (19-20 s laps, crashes in traffic) | finished 10th (update 80), 15th (update 100) | 4th (leading laps 7 and 8, then a crash 5 s from the line); the last 10 s planned again, 6 s ahead, a decision every 0.5 s: **WON, 1st**, 8.1 s left |
| Daytona advanced (4 laps, from 20th) | out of time at lap 3 | no better than the base | 3rd, 2.0 s left; with the tuned base driver and an optimised line 1st into the last lap, 8th at the line (the last hairpin); with the hairpin's ceiling, from lap 3 planned again (`--prefix`, 6 s ahead, every 0.5 s, a place worth 60 units): **WON, 1st** ("POSITION 1/20"), 10.6 s left, laps 55"85, 46"98, 45"93, 45"46 |
| Daytona expert (2 laps, from 30th) | out of time at lap 2 (lap 1: 128 s) | | 2nd (the leader 1 s ahead for the last 90 s); its last 55 s planned again unbroken (`--prefix`, 6 s ahead, every 0.5 s, a place worth 60 units): **WON, 1st**, 14.1 s left |
| Sega Rally Desert | cleared, 63.7 s | 0.4 s more left than the base | cleared, 0'58"38 |
| Sega Rally Forest | cleared, 90.9 s, 1.9 s left | as the base | cleared, 1'18"52 |
| Sega Rally Mountain | out of time (17 s at its start) | | **cleared**, 1'13"37: the championship 3rd, 3'30"27 (the table's best: 3'30"00); with the Forest's optimised line, unbroken: 0'58"48, 1'17"20, 1'12"85, **3'28"53, first of the records table** ("CHAMPIONSHIP TOP 16"), the race 3rd |
| Indy 500 oval (4 laps, from 33rd) | finished 25th | as the base | **WON, 1st**, 12.7 s left |
| Indy 500 Bay Side Street (3 laps) | finished 9th, 3.4 s left (`a_lat` 90) | | **WON, 1st**, 15.1 s left |
| Indy 500 Highland Raceway (3 laps) | out of time at lap 3 | | **WON, 1st**, 13.6 s left |
| Touring Car, Country Circuit (round 1, from 8th) | 6th (laps 26-27 s on the new line) | | 4th; the lap record all the same (0'24"384 against the table's 0'40"000) |
| Touring Car, the qualifying lap then round 1 (one race) | | | with the optimised line (the round 1.5 s quicker): **WON, "RESULT OF ROUND 1: 1st WINNER"**, 0'46"863 (the second 0'47"019) |

PPO over the two levels, with 6 to 12 emulators each, three games at once
(the machine full: about 1300 frames a second in all), did not get far in a
few hours: the reward per second flat on Daytona and Sega Rally, slowly up on
Indy 500. What did it: **a planner over the base driver**, with the emulator
itself. Every second the race is saved; 13 workers each load it, drive one
candidate (a lane, a pace, the 4-2-4) for a second and the plain choice for
three more, and score the distance along the line, the places (30 units
each) and the finish; the best is driven. The workers start from the race's
controller state (the line's smoothing, the gearbox's wait, the held gear),
so the race goes as the winning try did; a race takes 2.5 times its length.
Its controls are recorded frame by frame: `replay.py` drives the same race
again (checked: Daytona advanced, 3rd again) and records the force feedback
as the bench does (`--ffb`); `ffb_events.py` sets it against the race's
events. Its decisions with every candidate's score are the network's data
(`plan_dataset.py`, `imitate.py`).

On the way: Daytona's expert line was missing its last 2650 units (a long
loop past the start straight; 990 segments a lap, not 600); Touring Car's old
line went out and back over its first 200 units (its lap is 2047 units, not
2434: the decoder's constant corrected); Sega Rally's Mountain line ended
before the finish (the road turns back in a hairpin and runs past the start
to the finish: closed from a run that bounced through it). A Sega Rally
stage starts with the time left at the last one plus about 18 s. Touring
Car's round ends with its clock stopping (its lap counter goes back to 0
and the next round starts on another course).

**A save state is not the race.** From one state, 300 frames at once and
150, a save, 150 more do not end in the same place, nor does a load in a
fresh emulator: something outside the save goes on from frame to frame. A
race driven from its start in one emulator is exact (every replay above
came out to the place); a race picked up from a loaded state is another
race. So the planner's tries, each from a loaded state, foresee the race a
little wrong (the Daytona beginner crash nobody saw coming), and an ending
planned again from a state does not join the race before it: the planner's
`--prefix PLAN --until FRAME` drives the earlier plan's controls first, in
the same emulator, and plans on from there, the race unbroken (the beginner
win). The Sega Rally championship's two parts (the Mountain planned from a
state) replay as two races. Narrowed down: it is the first load in an
emulator that differs. Loaded again in an emulator that has run, a state goes
on exactly as the race did; run a frame from the same state in a fresh
emulator and in one that has run, and the next saves differ in 2705 bytes, the
i960's registers first: something outside the save acts within the first
frame. Not found yet; not the drive board's interrupt phase (agent mode's,
tried), not the link board's or the I/O board's memory (saved). Supermodel's
save states, the Supermodel session checked, are exact.

**The way round it: warm the emulator.** Load any state and run 60 frames or
more (`common.Agent.warm`: 120); from then on that emulator's loads are exact,
whatever ran before: from a warmed emulator's save in mid-race, a load in
another warmed emulator (warmed on another course even) goes on to the same
place to the hundredth as the race that never stopped. Every emulator of
`rl/` is now warmed (the environment does it), the planner records it with
its race (`"warm"`), and `replay.py` warms before replaying such a race. The
planner's tries now foresee the race exactly. (A warm-up of 1 to 10 frames
from power-up, or a load and one frame, is not enough.)

Two more things outside a save, on the agent's side, both now reset at a
load: the gear held on the H shifter (`set Gear`: a race loaded after another
started in the gear the last one ended in; `rl/` releases the holds at every
load), and the telemetry's history (the first frame after a load took its
speed from the race before, held as a jump's: the base driver's speed plan
read it; the agent now calls `GameTelemetry::forget()` at a load, and the
planner gives its tries the race's speed for their first step).

The racing lines (`line_opt.py`, CMA-ES, 24 offsets across the road and the
speed plan's scale; `compare_lines.py` sets them against the mapped line in a
warmed emulator): the Forest's finishes its stage from its start in 87.4 s,
where the mapped line has 130 units still to go at 88 s (`courses.py` takes
it: `driver=dict(offsets=...)`); the Desert's first optimisation gained
nothing once measured fairly.

Daytona's advanced course, won (16 October, afternoon). The base driver's
settings searched (`tune_base.py`: a_lat 97, a_brake 17, gain 2.68, look-ahead
8.1 + 0.48 v, up at 7431 rpm: 1 % more distance), then its line
(`line_opt.py`, 32 knots, from a flying lap 3: 10 % more distance on that
lap, 9 % from another state, 2 % over the race). `line_opt.py` saved the
pace as searched, not as driven: its workers clip the speed plan's scale to
0.8..1.4, the file kept the unclipped value (Daytona advanced's 0.15, the car
then crawled; Sega Rally Forest's 1.64, driven so in the championship, where
the search had driven 1.4). Fixed: the file now holds the clipped pace.

The race planned on that line led into the last lap and finished 8th: the
hairpin before the line (3200 to 3520 along the lap), every lap the same.
The car does not brake there, runs wide by 16 to 20 units with the wheel
full over, and hits the wall at the exit, where the road narrows; the cars
behind go by on the straight. The AI's speeds there (`ai_speed.py`: every
car's speed along the line, its 90th percentile) are 62 to 69 units a
second; a ceiling at the AI's speed is not enough (the car runs wide at 255
km/h; it makes it at 240): the base driver takes ceilings over stretches of
the line (`caps`), the hairpin's at 60. With it, the race planned again from
lap 3 (the first part driven as it was, `--prefix`) takes the lead back and
keeps it, the 4-2-4 at the last hairpin. Replayed frame by frame from the
start: 1st.

### Contacts with the other cars: the force feedback (`contact_bench.py`)

The replays now record the nearest car (`replay.py`: its distance, ahead and
to the right); `ffb_events.py` sorts a hit with a car within 5 units apart
from a hit on the scenery, and the cars rubbing. In the won races, a car
hit (Daytona, three nose-into-the-car-ahead) gives one jolt: the force steps
1.1 to 1.4 (of -1..1) from one update to the next, turning round, where on a
clean road its largest step in 0.8 s is 0.13 to 0.23 (above 1.0 in 1 % of
the windows); rubbing side by side makes the force change 2 to 3 times as
much per update (0.26 to 0.29 against 0.10 to 0.16).

Made on purpose: from the planned races' states, the car driven into the
car ahead (rear), alongside then into it (side), or across the road (wall),
the same state driven by the base driver alone as the baseline; a touch is
the cars within 5.5 units and their closing speed falling 15 km/h at once.
The force sm2-emu sends in the 0.5 s after the touch:

| Game (board) | Rear | Side | Wall |
|:--|:--|:--|:--|
| Daytona (EPR-16488A), 22 touches, 3 courses | the push `52` for 0.3 to 0.5 s; a jolt of 1.0 to 1.3 (expert), 0.4 to 0.55 (beginner) | a jolt every time: 1.0 to 1.65, turning round 1 to 6 times (the baseline 0.04 to 0.2); `52`, `62`, `42` among the usual `3A`-`3C` | `53` every time (and `39`), a jolt of 0.15 to 1.2 |
| Indy 500 (EPR-18261), 13 | a shake rather than a jolt: the force turning round 1 to 10 times in 0.5 s (0 to 2), `54`/`55`/`56` and `62` among `35`, `A0`, `B0`; steps 0.4 to 0.7 | jolts of 0.7 to 2.0, `56`, `61`, `62`, `64` | `54`, `30`: weak (0.2 to 0.7) |
| Touring Car (EPR-18261), 1 | a jolt of 1.5, `57`, `53`, `55`, `51` | | |
| Sega Rally (EPR-17891), 1 | | | the strongest torques `D5`, `9F`, a jolt of 0.86 |

The games give the cars' contacts: Daytona a jolt (side) or its push (rear),
Indy 500 a shake. Touring Car and Sega Rally have too few touches yet (the
saved states seldom have a car within reach; Sega Rally races one car).

### A won race's force feedback in the cabinet's wheel (`replay.py --device`)

The won races are records (`replay.py PLAN --record DIR`, in the form shared
with Supermodel's `ap_replay.py`: controls.npz with frame, steer +1 left,
accel, brake, gear, shift, step; result.json; the start state beside them):
Daytona's three courses, Indy 500's three, Touring Car's round 1 and Sega
Rally's championship record, each replayed from its record to the same end.
`replay.py RECORD --device` runs the agent with the controllers
(`--agent-device`): every control of the race (steering, pedals, gear,
buttons) held over the wheel and the pedals, the game's force feedback sent
to the wheel (vJoy, BackForceFeeder), in real time (`--live`: the client keeps
57.52 frames a second; a 216 s race took 222 s here with another emulator
running). The record's SCSP core is imposed (`--scsp-core`: a state does not
load under the other); `--exe`, `--roms`, `--config` take a cabinet's
emulator, ROM folder and ini. Checked with the cabinet's build (v0.9.47-32)
and an ini on the other core: the race comes out the same. Not tried with
a device here (BackForceFeeder and vJoy are the owner's). The game does not
know where the wheel is: its centring follows the recorded steering.

`--agent-show` (sm2-emu) shows the agent's game: the same emulation and
protocol, every frame drawn by the soft renderer (the one `obs` uses) into a
window (each pixel twice; any other window size stretched 4:3, through the
window's surface: SDL's 2D renderer is not built here), the sound board's
samples played, the frames held to 57.5245 a second by the frame pacer
(`--no-throttle`: as fast as they go), a picture skipped while the frames run
late. `replay.py RECORD --show` watches a won race; with `--device` the race
is seen and heard with its torques in the wheel. Measured beside six
Supermodel autopilots (the CPU at 80 %): drawing a race's frame costs 1.1 ms
to compose, 8.5 ms to render on four threads, 2.1 ms to show; the Daytona
advanced win (216 s) replayed in 217 s at normal priority (240 s below
normal), 1st as recorded: the window changes nothing in the race.

### What the force feedback does at the race's events (`ffb_events.py`)

The planned races driven again with `replay.py --ffb`: the frames sorted into
on the road, off it (outside the line's half-width), a hit (the speed down
40 km/h in 0.25 s) and light (the height on a parabola of about -g: a crest
or a jump). The board's commands most written, the force sm2-emu sent
(|constant|, and how often it turns round: a shake), against the road:

| Game | Off the road | A hit | Light, airborne |
|:--|:--|:--|:--|
| Daytona (EPR-16488A) | pushes `50`-`53`/`60`, the wobble `32`/`42`, the brake `27`: the force up 30-45 %, turning round 1.8 times a second (0.5 on the road) | the strongest pushes `57`, `53`, `63`, `67`, the brake `27`: p90 0.86-0.95 | nothing of its own |
| Indy 500 (EPR-18261) | the vibration (`A3` half period 3, `B7` amplitude 7), the anti-spring `40`, pushes `54`/`64`: the force turning round 3 to 6 times a second (1 on the road) | the same vibration: 6 to 8.5 turns a second | nothing of its own |
| Sega Rally (EPR-17891) | stronger torques to the left `C7`-`CC` | `00`, then the strongest torque `9F` | **`00`, no torque**: the wheel goes light (mean 0.10 against 0.38) |
| Touring Car (EPR-18261) | the strong push `57`: the force 0.40 against 0.24 | nothing of its own (the spring `34`) | the soft spring `38`: the wheel light (0.08) |

sm2-emu sends no vibrate effect for these boards: the Indy board's
vibration reaches the wheel as the constant force turning round. Sega Rally
has one more: the torque chopped (`15`, until `10`), 2 to 4 s at a time at
full speed, 26 times over the championship's three stages, on the road (the
rough stretches, the ground; the telemetry does not say the surface): the
force sm2-emu sends then turns round 3.1 times a second, against 1.1
elsewhere (its once-a-frame update smooths the board's chopping).

## Log

- 2026-10-13, night: agent mode, DIP setting, motor curve built. Visual
  autopilot (road edges in the picture) laps the Indy 500 oval; chains games.
- First PPO run (12 emulators, automatic gearbox, pursuit gain 1.6 under the
  policy's offset and throttle): 60 000 steps, 31st to 33rd, no progress.
  Stopped: the base lost 25 to 30 m/s in every turn.
- Found the 6MT gearbox (the menu wants a gradual wheel), the cars' yaw,
  pitch and roll, the course and gearbox bytes; maps rebuilt from whole AI
  laps (the old ones had kinks of 3 m radius at the line); telemetry server
  completed and tested; vJoy runner written and tested on a whole race.
- CMA-ES on the oval's line and throttle: 41.53 to 40.70 s in 30
  generations (`runs/opt_oval/best_gen30.json`), stopped for PPO.
- PPO v2 (6MT, the CMA-ES line as base, 99 states over three races, bonus for
  places at the finish and for a win): 75 000 steps, no better than its start.
- Planner (`plan_race.py`): from the current state each of 12 emulators drives
  a candidate (an offset across the track, the throttle) 5 s; the best is kept
  1.5 s; again to the flag. 5th. The leaders hold their distance (rubber band):
  the last places have to be taken in traffic and in the slipstream.
- What decides the race: from 2.5 laps on, the four leaders run in file, 40 m
  apart at 103.1 to 103.5 m/s, our top speed without slipstream. Passing them
  takes their slipstream, then pulling out at the right moment
  (`Follow(side, after)` candidates) and seeing 8 s ahead. The planner then
  goes from 5th to 2nd in the last lap, 2 m short of the win.
- 2026-10-13, morning: **the planner wins** (beam of 3 for the last lap). An
  automatic 5th/6th shift tried on clean laps: 0.35 s lost on one, 1 s won on
  the next; left off.
- The winning network: the planner's win made again with one continuous
  pilot, the network trained on its decisions and on the scores: it wins
  alone in agent mode, from the telemetry and from the picture; then
  **through vJoy in the emulator's window** with the new lockstep
  (`--telemetry-sync`, `run_vjoy.py --sync`).
- Score network (`q_train.py`): the planner scores every candidate at each
  decision; a network learns those scores (13 labels a decision) from the
  telemetry, or from the picture, and chooses alone (`policy_q.py`).
- 2026-10-13, afternoon: the force feedback work committed, each piece on its
  board's PR branch and on this branch (`ef3b80f` the answers on port D,
  `6b8d83d` the Indy family's torque in proportion, `7144055` the DIP setting,
  `c124909` the board in the save states, `edd3993` the EEPROM centre), and
  pushed; upstream merged (PR #14, `91dbef1`); the telemetry (`3cba294`), the
  agent mode (`bb3dd04`) and the autopilot with this log (`3b7e965`)
  committed.
- EPR-18261 measured on the cabinet Racecab1 by the BackForceFeeder session
  (`utils/DriveboardProbe`, BFF `docs/EPR-18261-Indy500-DriveBoard`): the NMI
  at 500 Hz, the brake `2x` a damper, the reply complemented, a switch ON reads
  0 and switch n is bit 8 - n.
- Virtua Racing (Model 1) left out of sm2-emu: its board's EPROM, EPR-15111,
  dumped by the owner of a Twin, read and run in a MAME built with only `vr`
  (njz3/mame, branch `vr-driveboard`); BFF `docs/EPR-15111-VirtuaRacing-DriveBoard`.
  Both sides wait for both calibrations: the BackForceFeeder now does the
  board's handshake (01, then 07, each echoed on Rx; BFF `5340a9f`).
- 2026-10-13, evening: the Over Rev and Super GT 24h note (`f8f1b8f`);
  `pr/merged-romset-clones` merged (`2393fc0`); the release zip built and
  checked (`--help`, `--list-games`); the branch renamed `dev/ffbedition`.
- 2026-10-14: upstream merged the drive board PRs (#15-#22: Sega Rally's,
  Indy 500's and Daytona's boards, the H shifter, the half-axis pedals,
  background input, merged romsets), and dev merged upstream (`73fe0ec`,
  `318e42a`). Super GT 24h's pedals: its I/O TYPE, the backup RAM's halfword
  at 0x0A (A 1, B 3, C 0), picks P = raw (C) or 256 - raw (A, B), full
  throttle and braking at the lowest P; sm2-emu fed them reversed always, so
  A and B drove alone and a full press read as released. Fixed (`5c8a2d5`,
  and `pr/sgt24h-pedals` for upstream); the game writes the drive board for
  A and B only. Agent mode reported every drive board byte as 00 (a dangling
  temporary): fixed (`ce7509b`). The Indy family's strength switches named
  1-1..1-3 everywhere (they were 1-6..1-8).
- 2026-10-14, afternoon: release zip `v0.9.45-67-g826c72c`. The FFB tests run
  again from an NVRAM that does not exist yet: Super GT 24h's blank backup RAM
  (all ones, as in MAME) leaves its I/O TYPE at `FFFF`, since the game's set-up
  (0x67920) writes only the header; the pedals read garbage and the board gets
  nothing. Such a value becomes A (`ea74922`); then a race through vJoy from a
  blank NVRAM: 259 km/h, the board's commands and 61 FFB packets. Over Rev from
  a blank NVRAM: HARDWARE TYPE NORMAL, no board commands, the pedals right.
  `pr/outputs-network-tab` and `pr/outputs-udp-announce` deleted (too
  specific to BackForceFeeder; their code stays on this branch).
- 2026-10-14, evening: dmanlfc's tidy-up (`e0eced8`); `pr/sgt24h-pedals` and
  `pr/wheel-panel-spring` rebased on it, one commit each, comments short in
  his style (`83c6690`, `6e45bcd`). He merged the first as PR #23
  (`653c851`); dev merged upstream (`575dc93`): upstream's code and comments,
  dev keeps the `wheel_panel_spring` and `wheel_drive_dip` settings, the SCSP
  core choice and `scratch_ram()`; checked with Super GT 24h from a blank
  NVRAM (a race, the board's commands), pushed.
- 2026-10-14, night: telemetry for Super GT 24h and Over Rev
  (`src/telemetry/sgt24h.cpp`, `overrev.cpp`), their RAM found by recording it
  while a pure pursuit drove on lines made from the AI cars, and matching it
  with the screen; checked against the screen (time, position, lap, lap time,
  speed, gear, revs), the attract mode (nothing), the speed from successive
  positions, and Over Rev's Model 2B sets.
- 2026-10-14, night: the agent mode for force feedback benches, in Supermodel's
  protocol (its session wrote the same for Model 3): inputs held by name
  (`set Steering 0.3`, `setraw`, `release`, `get`, `inputs`), `step [N]`, the
  force feedback's commands per frame (`ffb`: `FRAME Steering EFFECT FORCE`),
  the drive board model's ports (`servo`: `FRAME NMI PORT VALUE`, 28/2A/2E
  on EPR-18261, 27/46 on Daytona's), `frame`, `shot`, `ready`/`bye`. Without a
  device a virtual wheel runs the force feedback; with `--agent-device` the
  controllers are read through SDL (vJoy: the axis reaches the game the next
  frame) and the force goes to the wheel (vJoy got the recorded values). All in
  `src/agent.cpp`; `Input` only has hooks (`AgentHooks`).
- Then aligned with Supermodel, at the user's request: the trigonometric way
  of a real wheel and BackForceFeeder (an axis +1 at its left end, a constant
  force > 0 pushing left; the positional step's STEER stays -1 left for the
  autopilot), and the board's ports at every interrupt (the agent steps the
  models one at a time: 2A/2E 8.69 times a frame on EPR-18261, 46 17.38 times
  on Daytona's). With a device, DirectInput rather than Raw Input: 94 axis
  changes in 100 read in the very next frame (50 in 60 before); with a
  `settle` wait before a step, measured with BackForceFeeder off: 1 ms and
  more, 100 in 100, and 300 in 300 at the default 2 ms. Through vJoy the
  records have its sign: +1.0 gave +10000, -0.127 gave -1269.
- 2026-10-15: the force feedback bench in agent mode (`ffb_race.py`,
  `ffb_campaign.py`, `analyze_ffb.py`): four games from power-up, three races
  each, the files of Supermodel's bench. No command lost, the force sent the
  model's outputs to the record; the Indy family and Sega Rally linear,
  Daytona on Sega Rally's scale (the same board and panel: a power of 32 the
  wheel's maximum).
- 2026-10-15: the autopilot's scripts and data moved to BackForceFeeder's
  `autopilot/sm2emu/`, at the user's decision: one folder for both
  emulators' autopilots and benches, and one analysis of their races
  (`autopilot/analyze_ffb.py`, merged by the Supermodel session). The scripts
  find sm2-emu's checkout by `SM2_EMU_REPO`.
- 2026-10-16: the reinforcement learning started again for every course
  (section above): start states, lines and pools for Daytona's three
  courses, Sega Rally's three stages, Indy 500's three courses and Touring
  Car; the base driver; two levels of control; the first learnt race
  finished (Daytona beginner, 10th).
- 2026-10-16: the planner (`plan_drive.py`) finishes every course of Daytona,
  Sega Rally and Indy 500 that the base driver could not: Daytona advanced
  3rd and expert 2nd, Sega Rally's championship to the end (3rd, 3'30"27),
  and **wins Indy 500's Highland Raceway**.
- 2026-10-16: **all three Indy 500 courses won** (the oval from 33rd, Bay
  Side Street, Highland); **Daytona beginner won** (the last lap planned
  again 6 s ahead, `--prefix`); Sega Rally's championship 3rd twice
  (3'30"27, 3'30"30; the table's best 3'30"00); Touring Car 4th. The races
  replayed frame by frame with the force feedback recorded; save states
  found inexact. **Daytona expert won** the same way (its end planned
  again, unbroken). Sega Rally planned finer (a decision every 0.5 s, 6 s
  ahead): slower (about 3'33"); Touring Car the same, 4th (the leaders 1.5
  to 2.5 s ahead and drawing away). The racing lines next (`line_opt.py`,
  CMA-ES). The machine shared with Supermodel's autopilot: 6 emulators at
  most, below normal priority.
- 2026-10-16: save states made exact (warm the emulator first; the agent's
  held gear and the telemetry's history reset at a load); the Forest's racing
  line optimised; **Sega Rally's championship in 3'28"53, the records table's
  first** (the race unbroken, replayed frame by frame with its force
  feedback).
- 2026-10-16: Touring Car's qualifying lap and race planned in one go (the
  planner's `--stages` takes any courses now), on its optimised line: **round
  1 won**, 0'46"863. Every goal reached: Daytona, Sega Rally (the best time),
  Indy 500 and Touring Car won, every course of Daytona, Sega Rally and Indy
  500 finished.
