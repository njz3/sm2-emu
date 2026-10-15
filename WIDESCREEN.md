# Widescreen: tracking

Tracks a 16:9 picture for Model 2: the plan written here on 7 October 2026, set
against what upstream built since, and what is left.

## Where things stand (2026-10-15)

- **Upstream built it**: an experimental 16:9 in v0.9.47 (`338ce10`, Daniel Martin,
  14 October 2026, "add an experimental 16:9 widescreen aspect mode"). Its own
  design, close to the plan's core but not the same in its settings and its 2D.
- **Left**: the 2D stretch, the Lua scripts per scene, the software renderer, the
  edge culling outside Model 2B, a game-by-game check, and the README. See
  [What is left](#what-is-left).
- Prior art, still worth a look: Supermodel's `WideScreen` and `WideBackground`;
  ElSemi's Model 2 Emulator's `WideScreenWindow` and its per-game Lua scripts.

## What upstream built (v0.9.47)

| The plan's piece | Upstream | |
|:--|:--|:--|
| A 3D setting, 4:3 or 16:9 | A fourth value of the aspect setting, **16:9 (widescreen)**, marked experimental, applied live | different: one setting, not two |
| A 2D setting, centred or stretched | The 2D is never stretched. The layers in front of the 3D (the HUD) stay 4:3 in the middle; the layers behind it fill the sides (below) | different, and the stretch not built |
| The aspect "4:3 (arcade)" giving 16:9 when wide | Not needed: 16:9 is its own aspect value | different |
| The wide frame, 662 x 384 | `render::kWideColumns` = 83 a side, `frame_width()` = 662 | **done** |
| The geometrizer: full-width windows widened, the projection offset | `Geometrizer::set_widescreen()`: a window whose edges are within 2 pixels of the raster's (the CRTC offset included) grows by 83 a side; x moves by 83; the scissor clamps to the wider raster | **done** |
| Windows that nearly span the width | The 2-pixel margin | **done** |
| The render scale's device limit at the wide width | `clamp_scale_to_max_dimension` uses `frame_width(kWideColumns)` | **done** |
| Vulkan | 3D into the wide target, the 2D composed with the side fill, presented at 16:9 | **done** |
| OpenGL | The same (`gl_tilemap_pass`, `gl_present_pass`, `gl_poly3d_pass`) | **done** |
| The software renderer | Not wide: 16:9 shows 4:3 there ("GPU renderers only") | not done |
| A per-game `widescreen="false"` | A per-game `side_fill`: `mirror` (default), `black`, `menu`, `pillarbox` (the whole game 4:3, black side bars) | different, see below |
| Light guns kept 4:3 | Guns shoot into the sides: each gun's X travel widened (`rom::widened_axis`), the Sinden border framing the wide picture, Behind Enemy Lines recalibrated and kept 4:3 (`pillarbox`) | different: better |
| Edge culling (scenery missing at the sides) | Model 2B: the AM2 library's outcode test in the SHARC program (`lda -248.0 ... lda 248.0`) found by its shape and its limits rewritten to the wide ones | **done on Model 2B**, not elsewhere |
| Emulation, timing, save states unchanged | The polygon count the game reads back is still the 4:3 screen's (`narrow_count`), so its load balancing does not change | **done**, more than planned |
| Lua scripts per scene | None | not done |
| A survey and a test of each game | Not in the commit | not done here |
| README | No mention of 16:9 | not done |

## How it is built (upstream)

- **The setting**: `AspectMode::Widescreen` ([src/core/config.h](src/core/config.h)),
  "16:9 (widescreen)" in the Video tab. main.cpp shows 4:3 when the renderer cannot
  widen (`backend->capabilities().widescreen`, false for the software renderer) and
  otherwise passes 83 columns to the machine (`set_widescreen`), the input layer and
  the backend.
- **The 3D**: the geometrizer ([src/hw/geometrizer.cpp](src/hw/geometrizer.cpp))
  widens a full-width window's viewport by 83 a side and offsets every projected x by
  83. It keeps the window's planes as the game set them (`narrow_plane`) to count the
  polygons the 4:3 screen would have kept (`narrow_count`), which is what the game
  reads back. A polygon ending exactly on both 4:3 edges (a letterbox bar the game
  sized to its screen) is stretched to the wide frame (`widen_screen_overlay`).
- **The culling, Model 2B**: the games cull objects whose projected points all fall
  off one side of the 4:3 screen; [src/hw/model2b.cpp](src/hw/model2b.cpp) finds that
  test in the uploaded SHARC program by its shape (a loaded state may already hold
  the widened values) and rewrites its two immediates.
- **The 2D**: the compose shader
  ([shaders/tilemap_compose.comp](shaders/tilemap_compose.comp)) draws the layers in
  front of the 3D (the HUD) on the 4:3 screen only, and the layers behind it across
  the sides, from, in order:
  - a game's own **360-degree panorama** when the machine knows one
    (`Model2MachineBase::sky_panorama()`): Daytona USA's sky, which a routine streams
    into layer 2 a column at a time from eight 32-cell pages, found by that
    routine's code ([src/hw/model2_original.cpp](src/hw/model2_original.cpp));
  - a **panorama cache** ([src/render/panorama.cpp](src/render/panorama.cpp),
    `PanoramaTracker`): every on-screen pixel of a plainly scrolling layer recorded
    by world column, so the sides show what the camera has already seen; a new
    epoch empties it when the layer's content changes under it;
  - **map rows that loop** (a row scrolled a long way without a cell changing),
    read directly;
  - else the game's `side_fill`: **mirror** the screen edge (the default),
    **black** (Pilot Kids, Rail Chase 2), black for **menu** panels only (a
    row-scroll layer's foreground tiles: Sonic the Fighters), or **pillarbox**, the
    game kept 4:3 (Behind Enemy Lines, a 2D cockpit frame around the 3D).
- **The guns**: the X axis's travel grows by the side columns' share
  (`rom::widened_axis`, [src/rom/game.h](src/rom/game.h)), so an aim beside the 4:3
  screen reads as a position past its edge; the input layer lets an aim reach
  1 + 83/496 of the screen on each side.

## What is left

1. **The game-by-game check** (the plan's steps 0 and 6): a race or a fight of each
   game in 16:9, screenshots side by side, and per game what pops in at the sides,
   what the side fill shows, and whether `side_fill` needs setting: Daytona USA, Sega
   Rally, Indy 500, Touring Car, Over Rev, Super GT 24h, Virtua Fighter 2, the gun
   games. Upstream set it for four games only.
2. **The edge culling outside Model 2B**: the original Model 2 (TGP: Daytona USA,
   Virtua Fighter 2...), 2A and 2C games have no culling patch, so scenery may be
   missing at the sides. Their culling tests have to be found in their own programs,
   as upstream did for the SHARC's.
3. **The 2D stretch**, if wanted: the HUD stretched to the wide frame (the plan's
   second setting). Upstream's way, the HUD in the middle and the backdrop filling
   the sides, may be enough; to be judged on the games of step 1.
4. **Per-scene control by Lua scripts** (the plan's step 10, kept as it was below):
   a script per game reading the i960's RAM to switch the widescreen, the side fill
   or the stretch per scene (race, attract, menus). Now that 16:9 exists, its first
   use would be the scenes that look wrong in 16:9 (menus, title screens).
5. **The software renderer**: wide too, or kept 4:3 (as now, silently).
6. **The CRT super-resolution case** (2560x384p and the like): 4:3 and Stretch with
   each scaling method, unaffected by the new value but never checked.
7. **README**: the 16:9 value, what it does, and its limits.
8. **Our WIDESCREEN.md for upstream**: the check of step 1 may be worth a PR of
   `side_fill` values, or an issue per game, to dmanlfc.

## Per-scene control: a Lua script (not built)

One setting cannot suit every scene of a game: the HUD of a race reads well in 16:9,
while the attract mode's title screens and the menus may be better left 4:3. Which
scene is on screen is in the game's own state, in the i960's work RAM (1 MB at
`0x00500000`, exposed by the machine as `work_ram()`,
[src/hw/model2_machine_base.h](src/hw/model2_machine_base.h)).

Model 2 Emulator (ElSemi) answers this with a Lua script per game, in its `scripts`
folder: run every frame, it reads the game's RAM and switches the widescreen and the
stretch of the 2D layers [to check against its scripts: the names of its calls, for
instance `Model2_SetWideScreen` and `Model2_SetStretchALow`, and their granularity].
The same here would need:

- **Lua embedded**: the Lua 5.4 interpreter (MIT licence, compatible with BSD-3) in
  `3rdparty`, built like the other vendored libraries. One script per game,
  `scripts/<game>.lua` next to games.xml (or in the config directory), loaded with the
  game. Only the base, string, math and table libraries: no `io`, `os` or `package`.
- **The calls a script gets**: read the i960's RAM (byte, word, double word at an
  address); the frame number; set the widescreen (the 83 columns or none, through
  `set_widescreen`), the side fill, and the 2D stretch if it is built, for all of the
  2D or per tilemap layer.
- **When it runs**: once a frame, after the frame's emulation and before the
  composing, so a switch applies to the very frame that shows the scene.
- **The settings still decide**: a script only refines within what the user chose.
- **What the scripts must know**: the RAM addresses of each game's state (attract,
  race, menus, results), found and checked here game by game with a RAM search
  between scenes (Model 2 Emulator's scripts can guide the search; their code and its
  licence are not ours). The telemetry's work has some already: Super GT 24h's game
  state at `0x00510948` (AUTOPILOT.md).
- **Beyond widescreen**: the same engine would serve input patches, per-game fixes and
  extra outputs, as in Model 2 Emulator.

## Open questions

- Does any game draw its 3D across the full width with several side-by-side windows
  rather than one? Then upstream's full-width test misses them.
- The CRTC offsets (`m_crtc_xoffset`): upstream counts them in the full-width test;
  to check on a game that sets one for its own centring.
- 16:10 or 21:9: `kWideColumns` is a constant; the design takes any width.
- The script calls: Model 2 Emulator's names, so its users and its scripts' logic
  carry over, or our own?

## Log

- 2026-10-07: plan written, from a reading of the geometrizer, the render passes and
  the present stage. Nothing built or measured yet.
- 2026-10-07: per-scene control by Lua scripts reading the i960's RAM added to the
  plan, after Model 2 Emulator's scripts.
- 2026-10-15: upstream's v0.9.47 has an experimental 16:9 (`338ce10`, 14 October):
  the plan's frame, geometrizer and renderers, with its own setting, side fill
  (panorama cache, looping rows, Daytona's sky, mirror or black per game), guns
  aiming into the sides and Model 2B's culling widened. This file now tracks what is
  left beside it.
