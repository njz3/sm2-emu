# Widescreen: tracking

Tracks a 16:9 option for the 3D and the 2D of the Model 2 picture, beside the present
stage's aspect setting.

## Where things stand (2026-10-07)

- A plan only: nothing is implemented. This file holds the design, the steps and what
  to check.
- Prior art: Supermodel's `WideScreen` (the 3D viewport widened to 16:9) and
  `WideBackground` (the 2D layers stretched to it); ElSemi's Model 2 Emulator's
  `WideScreenWindow`. Both show the same side effect on some games: things the game
  culled for its own 4:3 view are missing at the edges.

## Goal

Two settings, plus the aspect setting already there:

| Setting | Values | What it does |
|:--|:--|:--|
| 3D render | **4:3** (default) / **16:9 wide** | 16:9 widens the 3D view horizontally: more of the scene on each side, the vertical view unchanged |
| 2D render | **4:3 centred** (default) / **16:9 stretched** | The 2D layers (HUD, text, title screens) kept 4:3 in the middle, or stretched to the wide frame. Only offered while the 3D is 16:9 |
| Aspect (exists) | 4:3 (arcade) / square pixels / stretch | How the finished frame fits the window, see below. Its three values keep their meaning; "4:3 (arcade)" gives 16:9 when the 3D is wide |

The aspect setting, as it is today:

- **4:3 (arcade)**: the arcade monitor's picture. Model 2's 496x384 raster was shown
  on a 4:3 tube, so its pixels are not square: each is 1.032 times as wide as it is
  tall ((4/3) / (496/384)). This keeps that pixel shape.
- **Square pixels**: the raster with square pixels, 496x384, 1.29:1: a little narrower
  than the arcade picture.
- **Stretch**: the picture fills the whole window, whatever its shape. The setting
  for a CRT driven at a super-resolution, 2560x384p for instance: the 384 lines go
  one to one, the width is stretched to fill the 2560 columns, and the tube shows it
  as 4:3.

With the 3D widened to 662x384, each value keeps its meaning: **4:3 (arcade)** keeps
the arcade's pixel shape, so the frame becomes 16:9 (662 x 1.032 / 384 = 1.78); **square
pixels** gives 662x384, 1.72:1; **stretch** still fills the window. Its label could
become "Arcade (4:3, 16:9 when wide)".

The cases that matter:

- **The arcade picture**: 3D 4:3, 2D 4:3, aspect 4:3 (arcade). Today's default,
  unchanged.
- **A 16:9 screen**: 3D 16:9, 2D centred or stretched, aspect 4:3 (arcade), which
  then gives 16:9.
- **CRT super-resolutions** (2560x384p and the like, a 4:3 tube driven at a very wide
  width): 3D 4:3, 2D 4:3, aspect **Stretch**, so the 4:3 picture fills the
  super-wide mode. This already works today: nothing to do but check it with the
  integer scaling method, which must not fight the stretch.

A 2D stretched over a 4:3 3D is not offered: the HUD would no longer line up with the
3D under it. Stretching the whole picture is the aspect setting's job.

## How the picture is built today

- **The geometrizer** ([src/hw/geometrizer.cpp](src/hw/geometrizer.cpp), the MAME
  port) clips each polygon against four planes taken from its window: when a window is
  set (`model2_3d_push`, the code near "left_plane"), each plane is the distance from
  the window's vanishing point (`center`) to the viewport's edge (`viewport[0..3]`).
  Projection then puts a vertex at `crtc_xoffset + center.x + x / z`.
  `screen_scissor()` clamps each polygon's viewport to the 496x384 raster
  (`kRasterWidth`), the scissor the renderers clip to. A window smaller than the
  screen (a mirror, an inset) has its own viewport.
- **The renderers** draw the polygon list into a 496x384 target times the render
  scale: Vulkan [src/render/vk/poly3d_pass.cpp](src/render/vk/poly3d_pass.cpp), OpenGL
  [src/render/gl/gl_poly3d_pass.cpp](src/render/gl/gl_poly3d_pass.cpp), and the
  software renderer [src/hw/model2_softrender.cpp](src/hw/model2_softrender.cpp).
- **The 2D** is the Sega 315-5292 tilemaps ([src/hw/segaic24.h](src/hw/segaic24.h),
  496 wide), drawn by the tilemap passes (`vk/tilemap_pass`, `gl/gl_tilemap_pass`) and
  mixed with the 3D by priority.
- **The present pass** (`vk/present_pass`, `gl/gl_present_pass`) fits the frame into
  the window with the scaling method and `AspectMode` (`FourThree`, `SquarePixel`,
  `Stretch`, [src/core/config.h](src/core/config.h)).
- **The 496 is written in many places**: `kRasterWidth` (geometrizer), `kNativeWidth`
  ([src/render/backend.h](src/render/backend.h)), `kWidth` (poly3d passes, software
  renderer, model2_video), `kVisibleWidth` (model2.h, model2b.h, model2c.h,
  model2_original.h), `kScreenWidth` (segaic24.h), and the texture dumps.

## Design

- **The wide frame**: 496 x (16/9) / (4/3) = 661.3, so **662 x 384**: 83 columns
  added on each side. The original raster sits at x = 83..578.
- **Which windows widen**: only those whose viewport spans the whole raster width
  (left edge at or before 0, right edge at or after 495). Their `viewport[0]` moves 83
  left and `viewport[2]` 83 right, so the left and right clip planes open and the
  scissor follows. The vanishing point and the focal length stay: the picture keeps
  its scale and only gains sides. Smaller windows (mirrors, insets) keep their size
  and position, shifted by the 83 columns like everything else.
- **Projection**: every x gains the 83-column offset, so the 4:3 part lands where it
  was, inside the wide target.
- **The 2D, 4:3 centred**: the tilemap layers drawn at x = 83..578. The two side bands
  get the 3D where there is some, and the backdrop colour (black) elsewhere: a title
  screen in 2D keeps black bars.
- **The 2D, 16:9 stretched**: the tilemap layers scaled by 662/496 horizontally when
  mixed. Nearest sampling keeps the text sharp but uneven; the 2D upscale filters
  (xBR, ScaleFX) run before the stretch.
- **The present stage**: "4:3 (arcade)" computes the frame's shape from the arcade
  pixel shape (1.032:1) and the frame's width, instead of a fixed 4:3: 4:3 at 496
  columns, 16:9 at 662. "Square pixels" becomes 662x384. "Stretch" unchanged.
- **Where it can be turned off**: a per-game attribute in games.xml
  (`widescreen="false"`) for the games it breaks, and always off for the light-gun
  games: their aim is mapped onto the 4:3 picture.
- **Unchanged**: emulation and timing (only presentation moves), save states, the
  network outputs, the drive boards.

## Per-scene control: a Lua script

One setting cannot suit every scene of a game: the HUD of a race reads well stretched
to 16:9, while the attract mode's title screens and the menus are better left 4:3, or
the other way round. Which scene is on screen is in the game's own state, in the i960's
work RAM (1 MB at `0x00500000`, already exposed by the machine as `work_ram()`,
[src/hw/model2_machine_base.h](src/hw/model2_machine_base.h)).

Model 2 Emulator (ElSemi) answers this with a Lua script per game, in its `scripts`
folder: run every frame, it reads the game's RAM and switches the widescreen and the
stretch of the 2D layers [to check against its scripts: the names of its calls, for
instance `Model2_SetWideScreen` and `Model2_SetStretchALow`, and their granularity].
The same here needs:

- **Lua embedded**: the Lua 5.4 interpreter (MIT licence, compatible with BSD-3) in
  `3rdparty`, built like the other vendored libraries. One script per game,
  `scripts/<game>.lua` next to games.xml (or in the config directory), loaded with the
  game. Only the base, string, math and table libraries: no `io`, `os` or `package`,
  so a script cannot touch files or start programs.
- **The calls a script gets**: read the i960's RAM (byte, word, double word at an
  address); the frame number; set the 2D stretch, for all of the 2D or per tilemap
  layer (the 315-5292 has four: two pairs of a "screen" and a "window" layer, each
  drawn before or after the 3D); and possibly the 3D widening itself, for the scenes
  it breaks.
- **When it runs**: once a frame, after the frame's emulation and before the mixing,
  so a switch applies to the very frame that shows the scene.
- **The settings still decide**: a script only refines within what the user chose. A
  2D stretch turned off in the settings stays off whatever the script says.
- **What the scripts must know**: the RAM addresses of each game's state (attract,
  race, menus, results). Model 2 Emulator's scripts hold many and can guide the
  search, but their code and its licence are not ours: the addresses are found and
  checked here, game by game, with a RAM search between scenes.
- **Beyond widescreen** (not this plan's job): a script engine is also what Model 2
  Emulator uses for input patches, per-game fixes and extra outputs; the same engine
  would serve them later.

## What will go wrong, and what to watch

- **Missing geometry at the edges**: the games cull objects against their own 4:3
  view before sending them, so cars, trees and scenery pop in and out in the side
  bands. How badly depends on the game: the main reason for the per-game attribute.
- **HUD and 2D**: placed for 496 columns. Centred, they stay right; stretched, they
  are wider.
- **2D that is part of the scene** (a sky or a backdrop drawn as a tilemap): centred,
  it stops short of the sides.
- **Windows that nearly span the width** (a viewport of 0..494, say) must still count
  as full-width: compare against the raster with a small margin.
- **Render scale**: the target is a third wider, so the device limit that lowers the
  scale (`clamp_scale_to_max_dimension`, through `scaled_width`, in
  [src/render/backend.h](src/render/backend.h)) has to use the wide width.
- **Light guns**: the pointer is mapped onto the frame letterboxed to 4:3 (the comment
  near "letterboxed to 4:3" in [src/osd/input.cpp](src/osd/input.cpp)). Kept 4:3 for
  the gun games, as above; if that ever changes, this mapping has to follow.
- **Screenshots and texture dumps**: at the wide size.

## Plan

0. **Survey**: log the windows each game sets (viewports, vanishing points) in a race
   or a fight: Daytona USA, Sega Rally, Indy 500, Virtua Fighter 2, Sega Touring Car,
   Virtua Cop (gun). Which windows span the width, which are insets.
1. **Settings**: `render3d_wide` (bool), `render2d_stretch` (bool), the GUI in the
   Video tab next to the aspect, the 2D one greyed out while the 3D is 4:3, and the
   aspect's "4:3 (arcade)" label saying it gives 16:9 when wide. No change in
   behaviour yet.
2. **The width as a value**: replace the 496 constants that size the 3D target, the
   scissor and the present stage by one runtime frame width, still 496. Check the
   pictures are bit-identical before and after (screenshots of a few games at fixed
   frames).
3. **The geometrizer**: widen the full-width windows and offset the projection when
   the 3D is wide.
4. **Vulkan**: draw the 3D into the 662-wide target, mix the 2D centred, present at
   the frame's aspect.
5. **The 2D stretch** in the mixing pass.
6. **Test**: the games of step 0 in 16:9, screenshots side by side, the side effects
   listed per game; `widescreen="false"` where it does not hold. The CRT
   super-resolution case: 4:3 and Stretch, with each scaling method.
7. **OpenGL**: the same as step 4 and 5.
8. **The software renderer**: wide too, or kept 4:3 with a log line saying so.
9. **README**, and a `pr/` branch for upstream.
10. **Lua scripts** (after step 6, when the stretch works as a setting): embed Lua,
    give scripts the calls above, then a first script for Daytona USA (the HUD
    stretched in a race, not in attract) and one for Sega Rally, with the RAM
    addresses of their scenes found and noted in this file.

## Open questions

- Does any game draw its 3D across the full width with several side-by-side windows
  rather than one? Then "full-width window" needs another test.
- Do the CRTC offsets (`m_crtc_xoffset`), which some games set for their own centring,
  interact with the added columns?
- 16:10 or 21:9 later: the design takes any width; only the settings would change.
- The script calls: take Model 2 Emulator's names, so its users and its scripts'
  logic carry over, or our own?
- Does stretching only some of the four tilemap layers (the HUD's pair, not the
  backdrop's) hold up in each game, or must it be all or nothing?

## Log

- 2026-10-07: plan written, from a reading of the geometrizer, the render passes and
  the present stage. Nothing built or measured yet.
- 2026-10-07: per-scene control by Lua scripts reading the i960's RAM added to the
  plan, after Model 2 Emulator's scripts.
