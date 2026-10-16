# Telemetry of the driving games: tracking

The goal (the owner, 15 October 2026): a 6-axis motion simulator and a
dashboard on the wheel, for every driving game, with the same data for the
autopilot. Everything is computed in the emulator, configurable by the client,
in the most standard format; damage and the gaps to the other cars where they
can be read. Supermodel sends the same thing for the Model 3 games, from its
own code (GPL v3 against this project's licence): its tracking file is
`Telemetry.md` in njz3/Supermodel (in French).

## Where it stands (2026-10-15)

- **Built** in `src/telemetry`: the agreed keys, what is worked out from them,
  the client commands, the Forza Data Out datagrams, the paused state; the
  agent's `tel` follows every frame. README, "Telemetry".
- **Ten games decoded**: Daytona USA, Sega Rally, Indy 500, Touring Car, Over
  Rev, Super GT 24h, Motor Raid, Wave Runner, Desert Tank and Manx TT.
- **Checked** in agent mode on all ten (a race from a state or from
  power-up, the keys printed), and end to end in the window (TCP commands,
  311-byte datagrams). Not yet with a commercial program (SimHub).
- **The clones** (each from power-up, driven 20 s): all give the parent's
  keys. daytonas, daytonase, daytonam, srallycb, srallycdx, overrevb,
  overrevba and manxttc read as their parents; daytona93, srallycc, srallycdxa, stcca, stccb,
  stcco, indy500to and indy500d have their own addresses (below). manxttdx
  runs a race but its RAM differs (no telemetry yet); motoraiddx stops at
  "DRIVEBOARD NOT RESPONSE ERROR!!".
- **Left**: motoraiddx (it wants the DX cabinet's motion board), manxttdx's
  addresses, daytona93's AI cars, indy500d's lap time; what the table below
  marks unknown.

## The keys (agreed with the Supermodel session)

One JSON line a frame (`--telemetry PORT`, the agent's `tel`); SI units, y up,
heading `atan2(dx, dz)`; a value the game does not give is `null`.

- Per car (`player`, each of `others`): `x y z`, `heading` (+ turning right),
  `pitch` (+ nose up), `roll` (+ leaning right, the right side lower),
  `rotation[9]` (world from car, row-major: columns right, up, forward),
  `speed` (the game's own: its speedometer), `vx vy vz`, `ax ay az` (world,
  from the positions), `along alat avert` (m/s^2 in the car's frame:
  speeding up, towards its right, up), `yaw_rate pitch_rate roll_rate`
  (rad/s, the same senses), `ground`, `airborne`, `impact` (a collision's
  delta-v, m/s), `wheels[4]` (FL, FR, RL, RR: `susp` > 0 compressed,
  `contact`, `surface`, `slip`, `spin`), `gear` (-1 reverse, 0 neutral, 1..N),
  `gears`, `rpm rpm_max rpm_idle`, `lap sector progress position`, `damage`,
  `gap_ahead gap_behind` (s) `dist_ahead dist_behind` (m along the track),
  `throttle brake clutch handbrake` (0..1) `steer` (+1 left).
- The race: `frame game track sectors laps transmission racing finished`,
  `state` (`attract select race pause over replay unknown`), `time_left
  lap_time last_lap best_lap race_time` (the game's clocks as shown),
  `checkpoint stage car difficulty`, `camera[9]`, `inputs`.
- Commands (a client's lines; each answered with one JSON line): `fields
  all|PATH,...` (dotted, `player.wheels.0.susp`: the wheels not asked for
  stay as `{}` so that the indices hold), `others N|all` (the nearest along
  the track, else in a straight line), `every N`, `udp forza HOST:PORT`,
  `udp off`, `list`. An empty line, or `go`, acknowledges a frame
  (`--telemetry-sync`).
- Forza Motorsport 7 "Car Dash" (311 bytes, UDP; `telemetry_forza` or
  `--telemetry-forza HOST:PORT`): `IsRaceOn` while the state is race;
  acceleration, velocity and angular velocity in the car's frame (X right, Y
  up, Z forward; X pitch, Y yaw, Z roll); yaw/pitch/roll; normalised
  suspension `0.5 + susp/0.2` (0 unknown); slip as both slip ratio and
  combined slip; position; speed; fuel 1; distance `progress x lap length`;
  best/last/current lap and race time; `LapNumber` laps done; place; pedals
  0..255; gear 0 reverse, 1..10, 11 neutral or unknown; steer -127 full left.

### What is worked out

| Key | How |
|:--|:--|
| `vx..az` | From the positions frame to frame. A step faster than 150 m/s is a jump (a crash put back): the speed holds, no acceleration. A car that did not move holds the last values for up to two frames (Desert Tank moves its tank every other frame) |
| `along alat avert` | The acceleration on the rotation's columns, right = up x forward |
| `*_rate` | From the angles frame to frame, unless the game gives them; more than 0.5 rad in a frame is a jump: 0 |
| `impact` | `|a| dt` when `|a|` passes 80 m/s^2 (8 g: a fast turn reaches 3 to 4) |
| `airborne` | `ground` above 0.25 m |
| `gap_*`, `dist_*` | The progress (laps) times the lap's length, to the nearest car ahead and behind; the gap at this car's speed |
| `throttle brake steer` | The analogue channels the game read, by its controls' wiring (games.xml) |
| `state` | The game's own where known, else race while racing |

Roll was checked on every game: Daytona's and Indy 500's banked left ovals
lean left (negative); Over Rev's steady circles lean out of the turn; Motor
Raid's bike leans into it; Sega Rally's horizon; Super GT 24h's matrix (its
column 0 is the car's right, so its roll is `atan2(-m10, m11)`; it was the
other way before). `orient()` built its matrix leaning the other way too
(Rz(roll) where Rz(-roll) leans right): fixed.

## What each game gives

| Game | Position, speed | Angles | Gear, rpm | Clocks | State | Others | Lap length |
|:--|:--|:--|:--|:--|:--|:--|:--|
| Daytona USA | yes, speedometer | pitch, heading, roll | 1..4, rpm, red 8000 | time left, race, lap, last, best (64 a second) | yes | 39 | 1569 (beginner) |
| Sega Rally | yes, speedometer | pitch, heading, roll | 1..4, rpm, red 8200 | time left, total (1/60 s) | yes (mode + clocks) | 7 | unknown |
| Indy 500 | yes | pitch, heading, roll | 1..6 (6 MT), rpm | time left, lap | race only | 32 / 27 | 4053 / 4610 / 4910 |
| Touring Car | yes | heading | 0..6, rpm (probable) | time left (64 a second, fixed), lap | race only | 7 | 2047 (Country) |
| Super GT 24h | yes | matrix | gear, rpm | time left, lap | yes | 13 | 3000 |
| Over Rev | yes | pitch, heading, roll | gear, rpm | time left, race, lap | race only | 7 | from the game |
| Motor Raid | yes, speedometer | pitch, heading, lean | | time left, race | race / over | 9 | |
| Wave Runner | yes, speedometer | heading (attitude unknown: level) | | race | race / over | | |
| Desert Tank | yes | heading, pitch, roll from its corners | | mission timer | race | | |
| Manx TT | yes, speedometer | pitch, heading, lean | gear | time left (56 a second) | race (its clock running) | | |

Units: the games' own, about metres (Daytona's is 1.11 m by its
speedometer, Sega Rally's 1.03 m; Indy 500's oval measures 4053 against
Indianapolis's 4023 m).

## The RAM, game by game (15 October)

Found in agent mode (scratchpad tools: `ram_race.py` and `explore_game.py`
record whole sessions from power-up; `ram_find.py` matches HUD values read
off the pictures; `needle_find.py` reads a tachometer's needle; the vehicle
by the steering weave, its travel and its speed). The decoders' headers
(`src/telemetry/*.h`) hold the addresses; the new ones:

- **Daytona USA**: car `+98` speedometer, `+A8/+AC` pitch and roll (s16),
  `+1CC` gear, `+1D4` rpm; `005010A0` state, `005010CC` race time,
  `005010D0` time left, `005010E0/EC` laps, `00502A00` lap times. Daytona
  USA 2 keeps the same car structure (x at `+9C`, heading at `+C8`).
- **Sega Rally**: player `+38/+3C/+40` pitch, heading, roll (radians),
  `+44..+4C` rates, `+78` gear, `+7C` rpm, `+80` speed; scratch `00202098`
  mode, `002020B8` total time, `0020B0B0` time left.
- **Touring Car** (scratch): `002022D0` lap time, `0020462C` gear,
  `00208914` rpm (probable); no orientation found (no matrix, no angle by the
  heading).
- **Motor Raid**: 10 riders of `0x71C` from `0057B8E4`: `+0C` pitch, `+28`
  heading, `+4C` speed, `+284` lean; `00517BD0` time left, `00517CB0` race.
- **Wave Runner**: `0051DF1C` x/y/z, `+30` heading, `+48` speed;
  `005AD874` race time.
- **Desert Tank**: `005003D4` centre, `00500404` four corners; `00521040`
  mission timer.
- **Manx TT**: the bike at `00578A20` (x/y/z in decimetres, `+0C` pitch,
  `+10` heading, `+14` lean, floats), `00578B64` gear, `005009D0`
  speedometer, `005334F0` time left. Revision D's EEPROM ships set to the DX
  cabinet, whose motion slider is not emulated, and its self-test waits on
  the slider forever; `Model2::load_nvram` turns it to Twin (word 5, word 13,
  the CRC-16 in word 0), which is then revision C's Twin image byte for byte
  (manxtt and manxttc only: the DX-only set plays on without its slider).
- **The clones** (`kLayouts` in each decoder, picked by the set's name):
  daytona93 keeps the player at `00504D80` (from `+1A8` on, 4 bytes lower;
  the AI cars not found); srallycc its time left at `0020B0D0`, srallycdxa
  its mode, total time and time left at `00202088`, `002020A4`, `0020C990`;
  stcca, stccb and stcco their cars at `0020A830`, gear `002045BC`, rpm
  `00208864`, stcco its clocks 0x10 higher; indy500to everything 0x10 to 0x30
  lower; indy500d its cars at `0054C714` of `0x1EC` bytes (angles `+14`,
  progress `+E8`), time left `0054C2D2`, course `0054C2F0`, gear `0054C4A4`,
  rpm `0054C600`, camera `00554C80`. The two Indy clones keep their gearbox
  byte four before the gear, not three (where they hold a 3); read in
  automatic only so far.

## Next

1. A commercial program on the Forza output: SimHub's free edition (gear,
   rpm, speed on its dash; the motion axes' signs, especially the angular
   velocities', which Forza does not document).
2. What the table leaves: Sega Rally's place and stage, Touring Car's
   orientation and state, Indy 500's and Over Rev's states outside the race,
   the last and best laps beyond Daytona, the wheels everywhere.
3. motoraiddx once its motion board answers; manxttdx's addresses; Sega
   Ski Super G and Top Skater (riding games) if wanted.

## Log

- 2026-10-15: the schema agreed with the Supermodel session; Daytona's gear,
  rpm, angles, clocks and state, Sega Rally's gear, rpm, angles, rates,
  clocks and mode, Touring Car's gear and (probably) rpm found.
- 2026-10-15: built: the keys, the derived values, the commands, Forza Data
  Out, the pause; the agent's telemetry every frame; roll made + right
  everywhere (orient() and Super GT 24h were the other way); Touring Car's
  clock fixed (64 a second); Motor Raid, Wave Runner and Desert Tank
  decoded. Supermodel's implementation follows the same rules (the jump,
  angle and pause guards from here; its wheels' `{}` placeholders here).
- 2026-10-15: Manx TT runs (revision D's EEPROM turned to Twin) and is
  decoded; the clones checked, their addresses per set (daytona93, srallycc,
  srallycdxa, stcca, stccb, stcco, indy500to, indy500d); Touring Car's time
  left read signed (it went to 4 billion at the time up).
