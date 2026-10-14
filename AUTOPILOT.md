# Autopilot, telemetry and force feedback: progress log

The goal: a driving autopilot for the Model 2 racing games, trained with
machine learning on the games' telemetry and pictures, that wins a race; used
to drive the games through vJoy while the drive-board commands and the force
feedback the emulator sends are logged, to compare sm2-emu's force feedback
with the real boards'. Indy 500 first, then Touring Car, Sega Rally, Daytona.

Everything here is on the `dev/ffbedition` branch of the user's fork,
njz3/sm2-emu (named `dev/network_output_scsp` until 13 October 2026), pushed.

## Where it stands (2026-10-14, evening)

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
  (`tel`).
- **vJoy autopilot**, standalone, for the four games, logging the drive board's
  bytes and the force feedback sent to vJoy; a BackForceFeeder script for the
  cabinet (untested).
- **FFB compared** for the four boards (tables below): Sega Rally linear and
  faithful; Indy 500's family (Touring Car, Over Rev, Super GT) was much
  steeper than the board (a torque curve of mine): **corrected**, the torque
  now in proportion to the board's power, the pushes exactly the board's;
  Daytona's springs and pushes stronger than the board's formula.
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
- **Published**: committed and pushed on `dev/ffbedition`; a Windows release
  zip made like the CI's, `build\bin\sm2-emu-v0.9.45-67-g826c72c-njz3-windows-x86_64.zip`
  (SHA-256 `427B3A8881EC1446699D15CA56B8860BB515F259A937359F95CBCDCBB46302A8`;
  it predates the blank backup RAM fix).

What is left:

- the user's own test of Indy 500 and Touring Car on the wheel;
- the PR left for dmanlfc: `pr/wheel-panel-spring` (`6e45bcd`, the panel
  spring and the DIP strength settings, its PR message written);
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
| Python client of the agent mode | `autopilot/sm2agent.py` | done |
| Telemetry: per-game RAM decoding, TCP server | `src/telemetry/`, `--telemetry PORT` | done for Indy 500 |
| Indy 500 RAM map | below | done for what driving needs |
| Track maps (the AI cars' laps, averaged) | `autopilot/record_laps.py`, `track_map.py` | the three Indy 500 courses |
| Race start states (coins, course, gearbox) | `autopilot/to_race.py` | the three courses, AT or 6MT |
| Drivers: pure pursuit, curvature driver, optimised line | `autopilot/pursuit.py`, `line_opt.py` | pursuit laps the oval; CMA-ES running |
| Reinforcement learning (PPO, PyTorch, CUDA) | `autopilot/train_ppo.py`, `env_indy.py`, `ppo_model.py` | two runs, no progress in places (see the log) |
| Planner: look-ahead with the emulator itself | `autopilot/plan_race.py`, `plan_beam.py` | **wins the oval from 33rd**; its races are the learning's data |
| Learning from the planner: cloning, candidate scores | `autopilot/bc_train.py`, `q_train.py`, `policy_q.py` | written; data being made |
| Picture-only driver | `autopilot/visual_driver.py`, `bc_train.py --visual`, `q_train.py --visual` | written |
| Autopilot through vJoy, standalone | `autopilot/run_vjoy.py`, `vjoy.py` | done: drives a race, logs drive bytes and FFB |
| Autopilot through BackForceFeeder | `autopilot/bff/autopilot.lua`, `run_vjoy.py --bff` | written, not tested (it needs BFF on the cabinet) |
| Touring Car telemetry | `src/telemetry/stcc.cpp`, `autopilot/stcc_telemetry.py` | cars, speed, heading, segment, time left; course and laps to find |
| Touring Car start, map | `autopilot/stcc_to_race.py`, `runs/stcc_country_line.npz` | automatic (6MT wants shifts), qualifying, race start states; Country Circuit mapped; planner 4th from 8th |
| Sega Rally telemetry, map, vJoy | `src/telemetry/srally.cpp`, `autopilot/srally_telemetry.py` | positions, headings, progress; the Desert mapped; FFB compared |
| Daytona telemetry, map, vJoy | `src/telemetry/daytona.cpp`, `autopilot/daytona_telemetry.py` | positions, headings, progress; the beginner course mapped; FFB compared |
| FFB comparison, all four boards | `autopilot/ffb_compare.py` | done: the tables below |
| Super GT 24h telemetry | `src/telemetry/sgt24h.cpp` | all 14 cars, the player's orientation, gear, revs, positions, laps, clocks; checked against the screen |
| Over Rev telemetry | `src/telemetry/overrev.cpp` | all 8 cars, the player's angles, gear, revs, laps, clocks, positions counted from progress; checked against the screen, the Model 2B sets too |

## Running it

Python 3 with numpy, Pillow, PyTorch (CUDA when there is one) and cma; vJoy
2.1.9 for the cabinet's controls. The tools run from `autopilot/`; the
emulator they drive is `autopilot/bin/sm2-emu.exe` when there is one (a copy,
so that `build\bin` can be relinked), else `build\bin\sm2-emu.exe`.

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

`run_autopilot.bat [indy500|stcc|srallyc|daytona]` does the same with each
game's best driver (Indy 500: the learnt network `runs/q_win1.pt` from the
race's start, in lockstep: the winning race in the emulator's window).

On the cabinet, through BackForceFeeder: put `bff/autopilot.lua` in BFF's
Scripts folder and load it (see its header); it starts
`run_autopilot.bat indy500 --bff` itself, which launches the emulator and
drives it through the script, BFF keeping vJoy and the motor; unloading the
script stops it. Not tested yet.

The Indy 500 win, again (agent mode, headless; from `autopilot/`):

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
`autopilot/daytona_telemetry.py`. The clock is still to find. The game: coins held a second, CIRCUIT SELECT, MISSION SELECT
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
`autopilot/srally_telemetry.py`; the pure pursuit drives it (60 s on the
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
parallel run at about real time each. A scaled picture costs 4 ms, the
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
changing anything.

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
