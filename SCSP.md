# SCSP port: tracking

Tracks the port of the SCSP (Yamaha YMF292-F) model from Mednafen to sm2-emu, keeping
the current implementation (derived from MAME) as a selectable reference.

- Current reference ("mame"): [src/hw/scsp.cpp](src/hw/scsp.cpp),
  [src/hw/scsp.h](src/hw/scsp.h), [src/hw/scsp_dsp.cpp](src/hw/scsp_dsp.cpp),
  [src/hw/scsp_dsp.h](src/hw/scsp_dsp.h).
- Studied source ("mednafen"): Mednafen 1.32.1, `src/ss/scsp.h`, `src/ss/scsp.inc`,
  `src/ss/sound.cpp` (local copy: `E:\Arcade\emulateurs\mednafen\src\mednafen-1.32.1\src\ss`).

All the comparisons in this file were made on **audio captures**, measured by scripts;
none of them comes from listening. See "Comparison method: audio captures".

## Where things stand (2026-10-10)

- `dev/network_output_scsp` has upstream main 3c9e79a merged in (34437e4): v0.9.43, with
  the translated interface, PR #12 (the vibrato depth at PLFOS 3) and PR #13 (gun aim).
  Both cores build. The mame core gives the same WAVs as upstream's own build on the 16
  A/B games (tag `m11dev` against `m11up`, 1,500 frames each, all 16 bit-identical), and
  the same as before (`m10dev`): none of them uses PLFOS 3 in that time.
- Upstream now has the sound 68000's two fixed wait states (our PR #5) and the vibrato and
  FM changes of PR #9 and #12 in the mame core: see "Upstream's PR #9: changes to the mame
  core".
- Steps 1 to 10 are done; step 0 (licence) and step 11 (per-game validation, default
  core) are not.

Next, in this order:
1. The tempo error of Sega Rally, Dynamite Cop and House of the Dead, +0.4% to +0.65% with
   the two fixed wait cycles. Virtual On keeps the cabinet's tempo within 0.05% (see
   "Check against a hardware recording: Virtual On"), so the error is not common to every
   game: find what the three fast games' sound programs use that von's does not, the SCSP
   timers first (their settings in dynamcop), then the interrupt latency (see "Tempo and
   the fixed wait states").
2. The vibrato depth: upstream's table (PR #9, #12) no longer follows the manual's table
   4.17, and the manual and the hardware model still disagree on it. Its author tuned it
   on House of the Dead in play: chapters 2 and 3 and the boss music (FM with PLFOS 3) and
   the Magician's theme (PLFOS 1 and 2). Those tracks are the material: cabinet
   recordings of them, against both cores captured in play (a save state or a scripted
   run, since the attract does not reach them). Von's attract hardly uses a vibrato and
   could not settle it. Compare on both cores the manual's depth, the hardware model's
   and upstream's table, with and without upstream's FM blend. The mednafen core does not
   take upstream's changes meanwhile (see "Not carried over to the mednafen core"). Then
   decide what to report upstream, with the output filter that is never applied and the
   save-state layout it changed.
3. Step 11: per-game validation on both cores, then the choice of the default core.
4. Step 0: the licence decision.

For a new session: since v0.9.43 the build needs two more submodules, harfbuzz and
SheenBidi (`git submodule update --init --depth 1`). The measurement scripts are in
`tools/`, which git ignores (see "Tools"). The reference recordings are in `build/scsp_ab/ref_*.wav`, also outside git:
`ref_hotd_hw.wav`, `ref_dynamcop_hw.wav` and `ref_von_hw.wav` (cabinets),
`ref_power_games_ost.wav` (Sega Rally) and `ref_daytona_ost.wav`. The captures quoted below
are in `build/scsp_ab` under their tags: `m11dev`, `m11up`, `m10dev`, `m10up`, `m9dev`, `m9up`, `m9_hotd`,
`m9_dynamcop`, `vib_*` (von), `hotdfw0`..`hotdfw4`, `fw*`.

## References

### Official Sega documentation (online)

Sega's Saturn developer manuals, in the English HTML translation hosted by infochunk.com
(originals © Sega Enterprises, 1997). The register tables were read from the pages
themselves in a browser, not from a summary.

**SCSP User's Manual** — the sound chip, Yamaha YMF292-F
([index](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/index.htm)). Pages used:

| Section | Page |
|---|---|
| 2 Overview of SCSP: LSI specifications | [p02_10](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p02_10.htm) |
| 3 SCSP function: CPU interfaces, memory access control | [p03_10](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p03_10.htm) |
| 4.1 Register map: slot registers (table 4.2) | [p04_11](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_11.htm) |
| 4.1 Register map: common control registers (table 4.3) | [p04_12](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_12.htm) |
| 4.1 Register map: sound data stack (table 4.4) | [p04_13](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_13.htm) |
| 4.1 Register map: DSP registers, microprogram, internal buffers (tables 4.5–4.7) | [p04_14](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_14.htm) |
| 4.2 Sound source registers: overview | [p04_20](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_20.htm) |
| 4.2.1 Loop control: KYONEX/KYONB, SBCTL, SSCTL, SA/LSA/LEA, PCM8B, LPCTL | [p04_21](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_21.htm) |
| 4.2.2 EG: rates, EGHOLD, DL, KRS, LPSLNK | [p04_22](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_22.htm) |
| 4.2.3 FM modulation control: SOUS, MDL (table 4.8), MDXSL/MDYSL, STWINH | [p04_23](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_23.htm) |
| FM sound source method, 1: slot structure, loops, averaging unit | [p04_fm1](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_fm1.htm) |
| FM sound source method, 2: slot pipeline, sound stack timing, MDXSL/MDYSL rules | [p04_fm2](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_fm2.htm) |
| FM sound source method, 3: MDXSL/MDYSL table (4.16), displacement (table 4.10), clipping | [p04_fm3](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_fm3.htm) |
| 4.2.4 Volume: TL, SDIR | [p04_24](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_24.htm) |
| 4.2.5 Pitch: OCT, FNS | [p04_25](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_25.htm) |
| 4.2.6 LFO: LFORE, LFOF (table 4.14), waveforms, ALFOS/PLFOS (table 4.17) | [p04_26](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_26.htm) |
| 4.2.7 Mixer: IMXL, ISEL, DISDL, DIPAN, EFSDL, EFPAN, MVOL, DAC18B | [p04_27](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_27.htm) |
| 4.2.8 Slot status: MSLC, CA | [p04_28](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_28.htm) |
| 4.2.9 Sound memory configuration: MEM4MB | [p04_29](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_29.htm) |
| 4.2.10 MIDI | [p04_2a](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2a.htm) |
| 4.2.11 Timers | [p04_2b](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2b.htm) |
| 4.2.12 Interrupt control (table 4.31) | [p04_2c](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2c.htm) |
| 4.2.13 DMA transfer | [p04_2d](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2d.htm) |
| 5 DSP operations: DSP RAM | [p05_10](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p05_10.htm) |

**SCSP / DSP Assembler User's Manual** (dAsms 2.0) — the SCSP's effects DSP, through its
assembly language ([index](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/index.htm)).
Pages used:

| Chapter | Page |
|---|---|
| 4 Program description: commands, internal registers and RAM, memory access, YREGH/YREGL, store options, ADREG/FREG | [p04_10](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p04_10.htm) |
| 5 Executable file format: coefficient and address encoding | [p05_10](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p05_10.htm) |
| 6 Programming guide: DEC, TEMP as a ring, example delay program | [p06_10](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p06_10.htm) |
| 7 Error and warning messages: EFREG stores | [p07_10](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p07_10.htm) |

Found but not used: the *SCSP / DSP Parameter Editor User's Manual*
([p01_10](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/para/hon/p01_10.htm)), and Sega
Retro's [Saturn official documentation files](https://segaretro.org/Category:Saturn_official_documentation_files),
which lists the original documents.

### Emulator sources

- **Mednafen 1.32.1**, `src/ss/scsp.h` and `src/ss/scsp.inc` ([mednafen.github.io](https://mednafen.github.io/);
  local copy in `E:\Arcade\emulateurs\mednafen\src\mednafen-1.32.1\src\ss`): the hardware
  model the new core follows, read as a description of the chip and not copied (see the
  licence section).
- **MAME**, `src/devices/sound/scsp.cpp` and `scspdsp.cpp` ([github.com/mamedev/mame](https://github.com/mamedev/mame)):
  the origin of the mame core ([scsp.cpp](src/hw/scsp.cpp), [scsp_dsp.cpp](src/hw/scsp_dsp.cpp)).
  MAME 0.282 was also run for the Sega Rally captures.

### Audio references

- Sega Rally Championship, "Power Games" (Saturn soundtrack, said to be identical to the
  arcade), from [this YouTube playlist](https://www.youtube.com/watch?v=-OJo965t4RM&list=PLRtaAHG_k8HAGFmns9WruFpFW23dYb0JF&index=1),
  supplied as a FLAC file.
- Daytona USA, "Let's Go Away (Advertisement)", supplied as a FLAC file.
- Dynamite Cop / Dynamite Deka 2, attract mode recorded on **real hardware** (Game Nexus,
  [YouTube](https://www.youtube.com/watch?v=FfdDF_Bu85Y)), supplied as the video's AAC audio
  (118 s, about 128 kbit/s).
- The House of the Dead, attract mode recorded on **real hardware** ("Model 2 Hardware
  Capture", [YouTube](https://www.youtube.com/watch?v=5O9s1V9RoPs)), supplied as the video's
  AAC audio (62 s, 128 kbit/s, 44.1 kHz), decoded to WAV with Windows' own AAC decoder.
- Cyber Troopers Virtual On, attract mode recorded on **real hardware** (Game Nexus, "Arcade
  and Attract Mode Intros", 720p60), supplied as the video's AAC audio (303 s, 128 kbit/s,
  44.1 kHz), decoded the same way.

## Goal

Two interchangeable SCSP cores:

- **mame**: the current core, which stays the default until the other one is validated;
- **mednafen**: the new core, closer to the hardware (EG, LFO, FM, DSP, timers,
  interrupts, MIDI).

Switching from one to the other is a setting, so the output can be compared game by
game without touching the rest of the sound board (68000, timing, DSB, per-game balance).

## ⚠️ Licence

| Project | Licence |
|---|---|
| sm2-emu | BSD-3-Clause + a "no commercial use without the author's permission" clause |
| MAME (base of the current core) | BSD-3-Clause, compatible |
| Mednafen | **GPL-2.0-or-later** |

The GPL forbids adding restrictions, and sm2-emu's non-commercial clause is one. Copying
or translating `scsp.inc` line by line would therefore produce a binary that cannot be
distributed legally. Options:

1. **Independent reimplementation (recommended)**: write the core from a description of
   the hardware's behaviour (this file, the Yamaha/Sega documentation, tests), using
   Mednafen as documentation rather than as source to copy.
2. **Ask the Mednafen team for permission** for this part.
3. **Optional GPL backend** (`SM2_SCSP_MEDNAFEN=ON`, off by default): acceptable for local
   experiments only; a binary that includes it could not be distributed.

To be settled with the upstream author (dmanlfc). This is an opinion, not legal advice.

- [x] Working hypothesis since step 3: **option 1**. [scsp_mdfn.cpp](src/hw/scsp_mdfn.cpp)
      is written for sm2-emu, in its style and structure, and does not translate
      `scsp.inc`. It is not a strict clean room: `scsp.inc` was read for the initial
      comparison. `SM2_SCSP_MEDNAFEN` is therefore **on** by default.
- [ ] Decision confirmed with the upstream author: ______

## Switching architecture

### Single integration point

Only [Model2Sound](src/hw/model2_sound.h) instantiates the SCSP (`m_scsp`). `main.cpp`
also reads `scsp().stats()` for the headless report. What the sound board uses, and so
what [ScspCore](src/hw/scsp_core.h) declares:

| Call | Role |
|---|---|
| constructor `(ScspMemory&, clock)`, `reset()` | construction, reset |
| `read(offset, mem_mask)`, `write(offset, data, mem_mask)` | registers, word address (byte access through the mask) |
| `generate(out, 1)` | one stereo sample at 44,100 Hz, called every 256 cycles of the 68000 |
| `midi_in(byte)`, `set_midi_out_handler` | serial link with the CPU board |
| `set_irq_handler(level, assert)` | interrupts to the 68000 |
| `set_slot_gains(gains[32])` | per-game volume balance |
| `sample_rate()`, `active_slots()` | sample rate, and the voice count behind the bus contention (`Model2Sound::bus_wait` in [model2_sound.cpp](src/hw/model2_sound.cpp)) |
| `stats()`, `serialize(Archive&)` | headless test, save states |

### Layout

```
src/hw/scsp_core.h        ScspCore interface (the calls above, virtual)           ✅ step 1
src/hw/scsp.h/.cpp        ScspMame: the current core, function bodies unchanged   ✅ step 1
src/hw/scsp_dsp.h/.cpp    the mame core's DSP, unchanged
src/hw/scsp_mdfn.h/.cpp   ScspMednafen: the new core                              ✅ steps 3–8: control, slots, envelopes, levels, LFO, FM, DSP
```

Built only with `SM2_SCSP_MEDNAFEN=ON` (the default; CMake then defines
`SM2_HAVE_SCSP_MEDNAFEN` for `sm2_hw`). With `OFF`, `scsp_core = mednafen` falls back to
`mame` with a warning. The CMake configuration summary lists the cores built.

- `Model2Sound` holds a `std::unique_ptr<ScspCore>` ([model2_sound.h](src/hw/model2_sound.h)),
  built by `make_scsp()` in [model2_sound.cpp](src/hw/model2_sound.cpp): that is where a
  new core is added. One virtual call per sample costs nothing measurable (44,100 calls/s).
- `Model2Sound::set_scsp_core(kind)` replaces the core and keeps its wiring (68000
  interrupts, MIDI out, per-game gains, all remembered by `Model2Sound`); `load_game()` in
  `main.cpp` calls it before the reset, so the game starts on the right core. A core left
  out of the build leaves `mame` in place, with a warning in the log.
- `ScspCore::Stats` and the callback types (`IrqHandler`, `MainIrqHandler`,
  `MidiOutHandler`) live in the interface; `write()` no longer has a default mask
  (`Model2Sound` always passes one).
- The current core keeps its names and structure, to stay comparable with MAME.
- **The switch applies when a game is loaded (or reset)**, not live: one core's internal
  state does not carry over to the other.

### Selection

- [x] Key `scsp_core = mame | mednafen` in `sm2-emu.ini`, `mame` by default (`struct Config`,
      read and written in [config.cpp](src/core/config.cpp), copied into `options.config`
      in `main.cpp`). An unknown value is reported and ignored.
- [x] Command-line option `--scsp-core <mame|mednafen>`, which takes precedence over the
      ini; an unknown value stops the launch.
- [x] Drop-down at the top of the Audio tab ("Mednafen (in progress)" while the core was
      missing).
- [x] The headless report (`--boot-test`) prints `scsp core : <name>`.
- [x] CMake option `SM2_SCSP_MEDNAFEN` to leave the new core out of the build (added in
      step 3; on by default, see the licence section).

### Save states

- [x] `Model2Sound::serialize` writes the `SCSPCORE` marker and then the core's id
      (`ScspCoreKind`, one byte) before the SCSP's state. A state made by another core is
      refused with a message saying which `scsp_core` to pick, and the machine is put back
      as it was (`Archive::mark_failed()`, the loader's rollback).
- [x] Save-state format raised to 2 ([archive.h](src/core/archive.h)). Format 1 stays
      readable: `read_header` accepts 1 to 2, `Archive::format_version()` says what is being
      read, and a format 1 state is read as coming from the mame core, the only one that
      existed then.
- [x] `--savestate-test` covers it: a state whose core id has been altered must be refused
      without touching the machine.
- [x] Format 3 since the merge of upstream's PR #9 (2026-10-06): `ScspMame` saves its
      output filter's state from version 3 on ([scsp.cpp:299](src/hw/scsp.cpp:299)), and
      versions 1 and 2 still load, the filter as reset. A version 2 state made with the
      previous build was loaded to check it. Upstream added that state without a new
      version, so its own older states are read out of step there.
- The new core's own layout changes from step to step while it is being written (step 7
  added the stack delay, step 8 the DSP's latches): states made with an earlier step of
  the mednafen core do not load in a later one.

### Adaptations on the new core's side

- [x] **Memory**: the 512 KB RAM belongs to `Model2Sound` and is shared with the 68000.
  DMA, slot reads and the DSP's ring buffer go through `ScspMemory`, which also maps the
  sample ROM at 0x80000–0xFFFFF ([model2_sound.cpp](src/hw/model2_sound.cpp)).
- [x] **Registers**: `read(word_offset, mem_mask)` and `write(word_offset, data, mem_mask)`.
  `read()` now takes the mask of the bytes read (`ScspCore` interface): `Model2Sound::read8`
  passes it, so that reading the MIDI flags byte does not take a byte out of the FIFO.
  `ScspMame` ignores it, as MAME does.
- [x] **Interrupts**: the level towards the 68000 is the highest SCILV level among the
  pending and enabled sources (sources 8 to 10 use source 7's bits). On every change,
  `irq(0, false)` then `irq(level, true)`. MCIEB/MCIPD are kept and call `main_irq`,
  which is not wired on Model 2.
- [x] **MIDI**: 4-byte FIFOs in each direction, MIEMP/MIFULL/MIOVF/MOEMP/MOFULL flags at
  0x404, a transmitter clocked bit by bit (31,250 baud), interrupt 9 when the output FIFO
  empties. A byte received with the FIFO full is lost and counted (`midi_in_dropped`,
  shown by `--boot-test` when there are any).
- [x] **Output**: the Saturn's 27/32 scaling (`sound.cpp`, outside the core) is not taken
  over. Every Model 2 game tested writes 0x030F to 0x400 (MVOL 15, DAC18B and MEM4MB set):
  DAC18B only selects the DAC's 18-bit word format, at the same full scale (the mame core
  normalises its 18-bit sum to the same 16-bit scale), so the 16-bit output is the top
  16 bits and nothing changes with it.
- [x] **Model 2 specifics**: EXTS at 0; writes to 0x7C0–0x7FF ignored (DoA); per-slot
  gains kept (on the direct output and the DSP send, as in the mame core); `Stats` counters.
- [x] **`active_slots()`**: since step 5, the voices still reading their waveform, from
  key-on until the end of a one-shot sample or until the envelope passes 0x3C0 (the
  hardware then stops reading memory). This is mame's notion (a voice lives until the
  end of its release or of its sample). Step 3's first version counted every keyed slot
  (32 on vf2); see the note under "Step 3 findings" about what that did and did not change.

## Comparison method: audio captures

Every comparison below was made on recorded audio and measured with scripts. No A/B
listening test has been done yet.

**Captures**
- **sm2-emu**: headless runs (`--boot-test N` frames, a fresh NVRAM each time) recorded
  with `--dump-audio` to 16-bit stereo WAV at 44,100 Hz, one file per core and per game.
  The mame core's noise generator has a fixed seed, so two identical runs give the same
  WAV. `--dump-audio` also works in an interactive session (the WAV is written on exit,
  before the volume setting), for music that only plays in game.
- **Real MAME**: MAME 0.282 with `-wavwrite` (48 kHz), for Sega Rally.
- **Reference recordings**: OST excerpts supplied as FLAC and converted to 44.1 kHz WAV
  with VLC: Sega Rally "Power Games" (Saturn version, said to be identical to the arcade)
  and Daytona USA "Let's Go Away (Advertisement)".

**Measurements** (Python + numpy)
- bit-exact identity (SHA-256 of the WAVs), for the steps that must not change the sound;
- RMS level, the average spectrum folded into semitones (55 Hz to 7 kHz) and the
  semitone shift that best aligns two spectra (0: notes at the right pitch; ±12: an octave
  error) — [tools/scsp_compare.py](tools/scsp_compare.py);
- position, tempo and pitch against a reference: 8 s chroma pieces of the reference
  placed in the capture at 10 ms resolution, a straight-line fit giving the speed ratio,
  and the residuals showing tempo irregularities; pitch to 5 cents from the long-term
  spectrum — [tools/find_pieces.py](tools/find_pieces.py),
  [tools/tempo_fit.py](tools/tempo_fit.py), [tools/ost_compare.py](tools/ost_compare.py);
  the same over a chosen stretch from a given start, [tools/hw_fit.py](tools/hw_fit.py)
  (see the method note under "Tempo and the fixed wait states"), and pitch to a cent,
  [tools/fine_pitch.py](tools/fine_pitch.py);
- reverb indicators: left/right correlation over 50 ms windows, side/mid energy ratio,
  and the depth of the loudness envelope's dips (reverb decorrelates the channels and
  fills the dips) — [tools/wetness.py](tools/wetness.py), `ost_compare.py`;
- share of clipped samples — [tools/clipping.py](tools/clipping.py).

**Beside the audio**, the emulator's headless report gives the sound program's counters
(key-ons, MIDI bytes, register accesses, timer interrupts, faults), which show whether the
sound program behaves the same way on both cores. A few internal questions (is FM used,
is a DSP program loaded, when is a timer reloaded) were answered with temporary traces in
the code, removed afterwards; the findings below say so where it applies.

**Tools**

[tools/scsp_ab.ps1](tools/scsp_ab.ps1) captures a list of games (fresh NVRAM, a separate
`--config` so the user's `sm2-emu.ini` is not rewritten, SHA-256 of the WAVs in
`build/scsp_ab/<tag>.hashes`) and compares two tags:

```
pwsh tools/scsp_ab.ps1 -Tag before -Roms <ROM folder>
pwsh tools/scsp_ab.ps1 -Tag after  -Roms <ROM folder> -Against before
pwsh tools/scsp_ab.ps1 -Tag mdfn   -Roms <ROM folder> -Extra '--scsp-core,mednafen'
```

(With `pwsh -File`, a list is written as one comma-separated string.)

```
python tools/scsp_compare.py build/scsp_ab after mdfn vf2 hotd bel
python tools/wetness.py build/scsp_ab after,mdfn vf2,dynamcop
python tools/tempo_fit.py reference.wav name=capture.wav
python tools/clipping.py 35 build/scsp_ab/<tag>_hotd.wav
```

`tools/` is ignored by the project's `.gitignore`: `git add -f tools/scsp_ab.ps1
tools/scsp_compare.py tools/scsp_eg_timing.py tools/ost_compare.py tools/tempo_fit.py
tools/find_pieces.py tools/wetness.py tools/clipping.py` to version the scripts.

The default list has 16 games whose sound program runs within the first 1,500 frames
(26 s): vf2, hotd, stcc, vstriker, dynamcop, skytargt, fvipers, zerogun, von, gunblade,
vcop2, sgt24h, topskatr, dynabb97, bel, motoraid. Indy 500, Last Bronx, Sega Water Ski and
Planet Harriers stay silent over that span. Some music starts later (hotd's attract music
at about 30 s), so longer captures are used where noted.

- [x] Bit-exact comparison (hashes), for the steps meant to be inaudible.
- [x] Measured comparison (RMS level, spectrum, pitch): `tools/scsp_compare.py`.
- [x] Comparison with reference recordings (tempo, pitch, reverb): `tools/tempo_fit.py`,
      `tools/ost_compare.py`.
- [ ] Alternating listening of the two cores.
- [x] Step 1 validated: WAVs **bit-identical** before and after going through the
      `ScspCore` interface.

## Plan

| # | Step | Status | Notes |
|---|---|---|---|
| 0 | Licence decision | to do | see above |
| 1 | `ScspCore` interface; the current core becomes `ScspMame` | ✅ done | 20 WAVs bit-identical, save states OK |
| 2 | Selection (ini, CLI, GUI) + core id in save states | ✅ done | CMake option moved to step 3 |
| 3 | New core skeleton: registers, DMA, timers, interrupts, MIDI; CMake option `SM2_SCSP_MEDNAFEN` | ✅ done | the sound program runs on 16/16 games; see "Step 3 findings" |
| 4 | Slot playback: phase, interpolation, loops, 8/16-bit, LFSR noise, SBCTL | ✅ done | provisional mix without EG; see "Step 4 findings" |
| 5 | EG + TL + ALFO in the attenuation domain, logarithmic MVOL, pan/SDL | ✅ done | ALFO wired to 0 until step 6; see "Step 5 findings" |
| 6 | LFO: shared counter, exact period, LFORE | ✅ done | frequencies match the documentation; only von uses one in 26 s |
| 7 | FM: sound stack with the 4-slot delay | ✅ done | none of the 16 A/B games uses it in 25 s; see "Step 7 findings" |
| 8 | DSP: all 128 steps always run, memory pipeline, EFREG | ✅ done | levels and reverb on a par with mame on 16 games + doa; see "Step 8 findings" |
| 9 | MSLC monitor, per-sample interrupt (0x400), MIDI flags and 4-byte FIFO | ✅ done | review against the model and the official manual; see "Step 9 findings" and "Check against the official manual" |
| 10 | Model 2 settings: per-game balance, bus contention | ✅ done | nothing to change: levels within +0.3 dB and bus load within 3% of mame on 24 games; see "Step 10 findings" |
| 11 | Per-game validation, then choice of the default core | to do | |

## Step 3 findings

The 16 games of the A/B list, 1,500 headless frames on each core:

| Game | key-ons mame / mednafen | MIDI received mame / mednafen | Note |
|---|---|---|---|
| hotd, stcc, vstriker, dynamcop, skytargt, fvipers, zerogun, gunblade, vcop2, sgt24h, topskatr, dynabb97, motoraid | identical | identical | |
| vf2 | 397 / 164 | 42 / 42 | see "key-ons" |
| von | 198 / 105 | 88 / 88 | see "key-ons" |
| bel | 569 / 44 | 27 / 22 | 5 bytes lost, see "bel" |

No fault, no access off the board, no byte sent back to the host, on either core.

- **bel, lost MIDI bytes.** The host sends 9 bytes (`a0 00 01` three times) between 1.8 s
  and 2.6 s; the sound program, on both cores, only reads its FIFO from 2.64 s on
  (SCIEB = 0x1c8, MIDI at level 3, pending but not served). mame's 32-byte FIFO keeps
  everything; the hardware's 4-byte one loses 5, and the truncated command (`a0` without
  its operands) throws what follows out of step. That the host repeats the same command
  suggests it expects losses on the hardware, but this is to be confirmed. Leads: the
  sound program's start-up time (bus contention? 68000 speed?), or a deeper FIFO than
  believed.
- **key-ons (vf2, von, bel).** ~~Put down to the key-on model.~~ **Gone in step 4**, where
  the key-ons became identical to mame on vf2 (397), von (198) and bel (569). At the time
  this was put down to `active_slots()`, which counted the 32 keyed slots and was thought
  to slow the 68000 through the bus contention.
  **Revisited after the 68000 fix (see "Tempo")**: until then, wait states had no effect in
  MSVC builds, so `active_slots()` could not slow the 68000 and that explanation does not
  hold. The change that mattered in step 4 was more likely something else, such as voices
  actually ending or the monitor's CA field, which a sound program can poll. Not
  re-investigated; the step 3 "with 0" experiment is moot either way.
- **bel (continued).** The 5 lost MIDI bytes change nothing over these 26 s: key-ons and
  level identical to mame (the host repeats its command).
- **Timers.** 5 to 25% more timer events: the timers run continuously, as on the
  hardware, including those a program has not re-armed; mame only restarts them on a
  write. No visible effect on the sound programs tested.

## Step 4 findings

`tools/scsp_compare.py`, mame (tag `step3final`) against the new core (tag `step4mdfn`),
1,500 frames:

| Game | RMS mame | RMS mednafen | Spectrum similarity | Shift |
|---|---|---|---|---|
| vf2 | −25.6 dB | −26.8 dB | 0.999 | 0 |
| hotd | −28.3 dB | −28.3 dB | 1.000 | 0 |
| vstriker | −32.1 dB | −32.1 dB | 1.000 | 0 |
| dynamcop | −17.4 dB | −23.8 dB | 0.992 | 0 |
| skytargt | −19.4 dB | −19.7 dB | 1.000 | 0 |
| fvipers | −33.5 dB | −33.7 dB | 1.000 | 0 |
| zerogun | −28.2 dB | −30.2 dB | 0.996 | 0 |
| von | −21.8 dB | −22.5 dB | 1.000 | 0 |
| gunblade | −26.0 dB | −26.2 dB | 1.000 | 0 |
| vcop2 | −37.2 dB | −39.1 dB | 1.000 | 0 |
| sgt24h | −20.8 dB | −20.8 dB | 1.000 | 0 |
| topskatr | −14.5 dB | −14.5 dB | 1.000 | 0 |
| dynabb97 | −29.5 dB | −30.0 dB | 1.000 | 0 |
| bel | −24.4 dB | −24.4 dB | 1.000 | 0 |
| motoraid | −21.9 dB | −23.6 dB | 1.000 | 0 |

(stcc: music on the DSB, SCSP silent over this span on both cores.)

- **Pitch**: shift 0 everywhere; the notes are at the right pitch and in the right octaves.
- **Timeline**: on bel and hotd, the two cores' half-second level curves overlap
  (correlation 1.000, against 0.2 for the same curve shifted by 5 s), but the
  sample-by-sample difference stays 3 to 6 dB below the signal: same notes, same
  instants, waveforms that differ in detail (phase, interpolation, envelopes).
- **Level**: within 0–2 dB of mame, except dynamcop (−6 dB). These games seem to play
  mostly simple envelopes (instant attack, sustain), hence the small gap without EG. The
  missing dB probably come from the effect returns: the DSP does not run yet (step 8) and
  EFSDL/EFPAN are not mixed. **Confirmed in step 8.**
- **Provisional**: TL, DISDL/DIPAN and MVOL as plain gains (TL 0.375 dB per step, MVOL
  3 dB per step, hardware-style pan), without envelope: a note stops dead at key-off.
  Replaced in step 5.
- **Not implemented**: bit 15 of slot word 8 ("short wave" in Mednafen, added in step 7),
  and bit 10 of FNS (unused per the documentation; Mednafen takes it into account). FM
  (step 7) and the PLFO (step 6) do not act on the position yet.

## Step 5 findings

**What is in place** in `ScspMednafen`:
- a four-phase envelope (exponential attack, decays 1 and 2, release);
- timing derived from the sample counter;
- key scaling KRS + octave, DL, EGHOLD, LPSLNK, and the "EG bypass" bit (bit 15 of word 5);
- instant attack when AR + key scaling ≥ 32;
- memory reads stop from 0x3C0 on;
- one attenuation, envelope + TL×4 + ALFO (0 for now), 6 dB per 64 steps, bypassed by SDIR;
- direct and effect sends (DISDL/DIPAN, EFSDL/EFPAN) as integer gains;
- effect returns EFREG 0–15 and EXTS on slots 0–17;
- logarithmic MVOL;
- the full MSLC monitor (CA, SGC, EG).

**Envelope rates** ([tools/scsp_eg_timing.py](tools/scsp_eg_timing.py) replays the model in
Python, without key scaling, and compares it with the documented durations that
`scsp.cpp` uses):

| Rate | Attack (new / doc) | Full decay (new / doc) |
|---|---|---|
| 8 | 760.5 / 760 ms | 11,871 / 11,100 ms |
| 16 | 47.6 / 47 ms | 742 / 690 ms |
| 22 | 5.96 / 6.0 ms | 92.8 / 85 ms |
| 28 | 0.84 / 0.85 ms | 11.6 / 11 ms |
| 30 | 0.39 / 0.40 ms | 5.8 / 5.4 ms |
| 31 | 0.39 / 0 ms | 5.8 / 3.6 ms |

- The attacks match the documentation.
- The decays are all about 7% longer, a constant ratio of 1023/960. So the documentation
  measures down to 0x3C0 (≈ −90 dB, where reading stops), and the script down to silence.
- Rate 31: this model treats it like rate 30 (a decay) rather than as an instant attack
  without key scaling; the documentation differs. To be checked on hardware if a game
  depends on it.

**Comparison with mame** (16 games, 1,500 frames):
- **Sound program**: key-ons identical on all 16 games, register accesses within 0.3%.
- **Pitch**: still right (shift 0).
- **Levels**: closer than in step 4. skytargt, fvipers, hotd, topskatr and bel are within
  ±0.2 dB, and vf2 goes from −1.2 to −0.4 dB. Remaining: dynamcop −6.1 dB, zerogun
  −1.8 dB, motoraid −1.5 dB and vcop2 −1.2 dB, probably effect returns (DSP, step 8).
  **Closed in step 8.**
- **Sample-by-sample difference**: unchanged (−21 to −35 dB depending on the game). It is
  dominated by phase and interpolation, not by the envelopes.

## Step 6 findings

**What is in place**: one LFO per slot, running continuously (a key-on leaves it alone),
with:
- an 8-bit counter shared by the PLFO and the ALFO;
- the wait between two steps derived from LFOF, ((8 − f&3) × 128 >> (f>>2)) − 4 samples;
- LFORE holding the counter at 0;
- four waveforms: saw, square, triangle, and noise (the slots' LFSR).

The ALFO (0 to 254, i.e. up to 23.8 dB at ALFOS 7) adds to the attenuation. The PLFO
(−128 to 126, shifted by PLFOS; doubled since step 9 to follow the manual, see "Vibrato
depth") is scaled by the top six bits of FNS (64/64 to 127/64)
and added to the pitch mantissa, so a vibrato keeps the same interval from one octave to
the next. As on the chip, the PLFO is read before the counter and the LFSR advance for
the sample, the ALFO after.

**Frequencies**: the 32 LFOF values give 0.169 to 172.27 Hz, within ±0.3% of the
documented table (`LFOFreq` in `scsp.cpp`) above 1 Hz. The larger gaps below (up to 2%)
come from the table being rounded to two decimals.

**Games**: of the 16 in the A/B list, only von uses an LFO in its first 26 seconds; the
other 15 WAVs are bit-identical to step 5's. For von, nothing measurable at the scale of
level or spectrum: the use is discreet. Key-ons identical to mame, `--savestate-test`
PASS (vf2, hotd, von), mame still bit-exact.

## Step 7 findings

**What is in place**:
- each slot writes its output (after envelope, TL and ALFO, before the sends) into the
  sound stack, four slot periods later, at the entry of the period it came from, unless
  that slot has STWINH;
- the stack has 64 entries, two samples of 32 slots;
- a slot's FM input adds the two entries MDXSL and MDYSL name, relative to the current
  time, and scales them by MDL; MDL ≤ 4 does not modulate;
- the whole part of that input moves the read position (11 bits, signed) and the
  fractional part adds to the interpolation's;
- as on the chip, the interpolation's second sample is placed with the next slot's
  fraction, which only shows under FM;
- bit 15 of word 8 ("short wave", undocumented): the address wraps within the lowest
  power of two set in LEA's bits 10..7.

Without FM, playback is identical to step 6. On the 16 A/B games (25 s), the captures are
bit-identical to step 6's: none modulates or uses the short wave over that span. A
temporary counter in the code saw no FM either in 90 s of attract mode on hotd, vf2,
dynamcop, vcop2, von, zerogun, motoraid, skytargt and Sega Rally.

**Not validated yet**: FM itself. hotd uses it (upstream commit def088c), but not in its
attract mode. To be recorded in play on hotd with `scsp_core = mednafen`.

## Tempo: the sound 68000's bus contention was lost under MSVC

Found by comparing a capture of Sega Rally's attract mode with the OST ("Power Games",
Saturn version, said to be identical to the arcade), with
[tools/ost_compare.py](tools/ost_compare.py) and [tools/tempo_fit.py](tools/tempo_fit.py):
- the pitch is right (±5 cents) on both cores;
- before the fix, the piece played 2.5% too fast, with irregularities of ±40 ms per 8 s
  piece;
- a capture from MAME 0.282 seems to be ahead by as much, but it aligns less well with
  the reference.

Sega Rally's sequencer is clocked by timer B, prescaler 8 and reload 192, i.e. 512 samples
(from a temporary trace of the timer writes). The timer's period does not change; what
changes the tempo is the 68000's speed: when it is slow, ticks get lost.

The cause was in [m68000.cpp](src/cpu/m68000/m68000.cpp), in `run()`:
`m68k_execute(cycles) + m_stalled`. The operands of `+` are unsequenced, and MSVC reads
`m_stalled` (still 0) before the call that fills it. Every wait cycle shortened the
timeslice without being counted: `kBusWaitCycles` and `kContentionCycles` (upstream commit
def088c, fitted to hardware captures of hotd, probably with GCC or Clang) had no effect in
Windows builds.

Proof: before the fix, 2 or 5,000 wait cycles per access gave the same number of SCSP
writes and a bit-identical WAV. After the fix, the original settings slow the 68000 down
by about 10%.

Effect on Sega Rally after the fix: +0.49% instead of +2.5%, irregularities of 6 ms
instead of 40 ms, chroma correlation 0.894 instead of 0.866. Both cores give the same
result within 0.01%. The fix affects both cores, since it is the 68000 that changes;
upstream took it in 8e1e7c3 (2026-10-01).

Consequences on the 16 A/B games (25 s):
- 15 captures change on each core (stcc is silent);
- levels and pitch between the cores: as in step 6;
- key-ons still identical between the cores, except bel (mame 554, mednafen 557): the
  slower 68000 reads its MIDI FIFO later, and the 4-byte FIFO loses 5 bytes (22 received
  against 27);
- `--savestate-test 900` PASS on vf2, hotd and bel, on both cores.

+0.5% remains. It may be the OST (Saturn version), the contention fitted on hotd, or the
contention model itself. A recording from a cabinet would settle it. **Since then**: more
waiting barely moves it (+0.37% at four fixed cycles instead of two), and Dynamite Cop's
cabinet recording shows a similar error that does not depend on the 68000 at all; see
"Tempo and the fixed wait states".

**Reverb.** On the same passage, the captures are drier than the OST:
- side/mid ratio −13.0 dB against −10.8 dB;
- left/right correlation 0.963 against 0.953;
- envelope dips 15.1 dB against 13.4 dB.

The mame core, which has a DSP, gave the same figures as the mednafen core, which did
not have one yet. Step 8 explained why: Sega Rally loads no DSP program in attract mode
(see "Step 8 findings").

## Step 8 findings

**What is in place** in `ScspMednafen::run_dsp()`, run every sample before the slots:
- all 128 steps of the program, always, without a computed "last step";
- INPUTS as a latch: MEMS, MIXS << 4 or EXTS << 8 as IRA selects; IRA 0x32 and up leave
  it unchanged;
- Y chosen among FRC_REG, COEF, Y_REG[23:11] and Y_REG[15:4] before YRL reloads Y_REG;
- the shifter working on the previous step's accumulator (26 bits): ×2 with SHFT0 or
  SHFT1 alone, saturation to 24 bits without SHFT1;
- FRCL and ADRL taking the high or low end depending on SHFT0 and SHFT1;
- EFREG replaced (not accumulated), TEMP and MEMS;
- a memory access carried out on the step after the one that asks for it, a read before
  a write;
- addresses, in words: MADRS + NXADR + ADRS_REG as a signed 12-bit value, then MDEC_CT
  and the ring mask outside TABLE, then RBP;
- the memory's 16-bit float format, or plain integers with NOFL.

The float ↔ integer conversions were checked against the model's formulas over all
65,536 floats and 370,000 integers: no difference. The slots feed MIXS (ISEL, IMXL) with
their attenuated output, through the per-game gain as in the mame core, and MIXS is
cleared after each pass of the DSP. The EFREG returns go through the EFSDL/EFPAN of
slots 0 to 15.

**Comparison with mame** (16 games, 25 s, after the 68000 fix):
- mame still bit-exact;
- 12 games change on the new core. hotd, stcc, vstriker and bel stay identical: nothing
  goes through the DSP in their first 25 seconds;
- hotd over 3 minutes (a temporary trace of the DSP's state, then captures on both cores):
  - it loads its DSP program from 5 s on (112 steps, 57 coefficients, like dynamcop);
  - its slots only send to the DSP from about 30 s on, when the attract music starts; until
    then it plays a single key-on;
  - mame and mednafen have the same key-ons (2,950), levels within 0.1 dB (−16.6 against
    −16.5 dB from 35 s on), the same reverb (L/R 0.940 against 0.939, side/mid −15.6 dB on
    both), and the same rare clipping (0.023% against 0.027% of samples clipped);
- levels: the last gaps close, and the 15 games with sound are within ±0.3 dB of mame;

  | Game | mame | mednafen step 7 | mednafen step 8 |
  |---|---|---|---|
  | dynamcop | −17.4 dB | −23.5 dB | −17.2 dB |
  | zerogun | −28.6 dB | −30.3 dB | −28.4 dB |
  | motoraid | −21.9 dB | −23.4 dB | −21.7 dB |
  | vcop2 | −37.1 dB | −38.3 dB | −36.9 dB |
  | vf2 | −25.6 dB | −26.1 dB | −25.4 dB |

- reverb: the left/right correlation and the side/mid ratio come back to mame's;

  | Game | L/R mame / no DSP / DSP | side/mid mame / no DSP / DSP |
  |---|---|---|
  | vf2 | 0.731 / 0.934 / 0.743 | −8.4 / −14.1 / −8.6 dB |
  | dynamcop | 0.770 / 0.985 / 0.773 | −9.7 / −27.6 / −9.7 dB |
  | motoraid | 0.977 / 1.000 / 0.977 | −18.9 / −45.8 / −18.7 dB |
  | doa | 0.910 / — / 0.916 | −12.1 / — / −12.2 dB |

- doa (50 s, the reference game for the DSP): 693 key-ons on both, −26.2 against
  −26.5 dB, identical spectrum;
- key-ons unchanged (the DSP does not touch the sound program); `--savestate-test 900`
  PASS on vf2, hotd, bel and dynamcop;
- cost: 25 s of dynamcop run headless in 7.5 s against 6.2 s on mame, about 5% of a CPU
  core in real time.

**Sega Rally does not use the DSP** in attract mode (from a temporary trace of the
register writes). It writes only zeros over the whole 0x700–0xBFF area (at 0.4 s, then at
1.8 s), and no slot has an IMXL. The reverb heard on the OST therefore does not come from
the SCSP in this mode, on either core. It may be the Saturn version's mix, or a DSP
program loaded only during a race. The captures with and without the DSP are
bit-identical.

**Daytona USA** (original Model 2) has no SCSP: it uses the Model 1 sound board, with a
YM3438 and two MultiPCMs. Its OST ("Let's Go Away", advertisement version) aligns with
the capture at −0.10% tempo, 1 ms residual and 0.97 correlation: the measuring method is
reliable, and the Model 1 board runs at the right tempo.

## Step 9 findings

A review of the control side and the slot pipeline against the hardware model, which found
five differences, now fixed:
- **Two passes per sample**, as on the chip: envelopes, KYONEX and the loops for all 32
  slots first, then each slot's waveform and output. A slot's FM read then sees the next
  slot already keyed on or turned round (only matters under FM).
- **The loops run for every slot**, reading or not (no audible difference: a slot that
  has stopped does not move).
- **Turning round mirrors the fraction too.** At the reverse loop's entry and at the
  alternating loop's turns, the position was mirrored but the fraction kept, which put the
  read point up to one sample off at every turn. A Python simulation of the chip's
  complemented counter against sm2's position/fraction, over 20,000 random loops in the four
  modes: no difference now (the previous code differed on 5,000/5,000 reverse and
  4,984/5,000 alternating loops).
- **The monitor (0x408) is latched** as its slot runs: CA from where the slot reads before
  that sample's step, 0 once it has stopped reading, with SGC and EG. It used to be worked
  out when the register was read.
- **FNS bit 10**, left unused by the manual: the chip takes FNS as 11 bits, and bit 10
  cancels the implicit 1 (in the pitch and in the vibrato's scaling).
- **The MIDI bit clock** is the chip's clock over 720 (32 bits every 45 samples, 31,360 baud)
  instead of exactly 31,250.

What the games use (a temporary trace over 60 to 90 s of attract on 20 games): all of them
write 0x030F to 0x400 (DAC18B and MEM4MB set, see "Output" above); none uses reverse or
alternating loops, FNS bit 10 or the short wave. Those fixes are therefore checked by the
simulation, not by the captures.

Results (16 A/B games, 25 s):
- mame still bit-exact;
- 11 captures change on the new core: those games poll the monitor, and the latched value
  shifts some of their timing by a sample. Levels within 0.1 dB of step 8 and spectrum
  1.000 on all 11; key-ons unchanged (bel still 554/557 through the MIDI FIFO);
- Sega Rally over 3 minutes: the waveform decorrelates from 17.5 s (notes start a sample
  apart), but tempo (+0.49%), alignment with the OST (0.894, 6 ms), level (−38.1 dB),
  spectrum and stereo are unchanged;
- `--savestate-test 900` PASS on vf2, hotd, bel and dynamcop.

## Step 10 findings

The two Model 2 settings the sound board applies around the SCSP were checked on the new
core, on every set that has a per-game gain and loads here: 90 s headless on each core,
from a fresh NVRAM, with a temporary trace of `active_slots()` per sample (removed since).

- **Per-game balance** (`kFlatGain` in [model2_sound.cpp](src/hw/model2_sound.cpp)): a flat
  gain per set, chosen to level the library against Daytona by ear, not a hardware figure.
  It applies to both cores the same way (direct output and DSP send).
- **Bus contention** (`Model2Sound::bus_wait`): the sound 68000's wait states grow with
  `active_slots()`, with constants fitted upstream on the mame core. The two cores define
  an active slot slightly differently: mame until the release ends, the new core until it
  stops reading memory (0x3C0 or a one-shot's end). Upstream later dropped the fixed part
  of the wait states (88d5557); this branch keeps it (see "Tempo and the fixed wait
  states").

| Game | RMS mame | RMS mednafen | Difference | Spectrum | Clipped mame / mednafen | Bus load mame / mednafen | Key-ons mame / mednafen |
|---|---|---|---|---|---|---|---|
| bel | −23.2 dB | −23.1 dB | +0.2 | 1.000 | 0 / 0 % | 14.20 / 13.88 | 2,273 / 2,280 |
| doa | −24.1 dB | −23.9 dB | +0.2 | 1.000 | 0 / 0 % | 9.77 / 9.70 | 1,840 / 1,843 |
| dynabb | −17.3 dB | −17.0 dB | +0.3 | 1.000 | 0.004 / 0.005 % | 2.85 / 2.87 | 312 / 312 |
| dynabb97 | −18.5 dB | −18.3 dB | +0.2 | 1.000 | 0 / 0 % | 9.09 / 9.15 | 1,245 / 1,245 |
| dynamcop | −16.7 dB | −16.5 dB | +0.2 | 1.000 | 0.004 / 0.005 % | 7.74 / 7.80 | 1,621 / 1,621 |
| dyndeka2 | −17.0 dB | −16.8 dB | +0.2 | 1.000 | 0.003 / 0.005 % | 6.68 / 6.72 | 1,394 / 1,394 |
| fvipers | −30.7 dB | −30.6 dB | +0.1 | 1.000 | 0 / 0 % | 5.35 / 5.29 | 1,012 / 1,012 |
| gunblade | −20.0 dB | −19.9 dB | +0.2 | 1.000 | 0 / 0 % | 9.20 / 8.92 | 1,605 / 1,610 |
| hotd | −17.3 dB | −17.2 dB | +0.1 | 1.000 | 0.014 / 0.017 % | 11.86 / 11.82 | 1,433 / 1,433 |
| motoraid | −19.2 dB | −19.1 dB | +0.1 | 1.000 | 0 / 0 % | 7.92 / 7.78 | 1,959 / 1,959 |
| overrev | −22.5 dB | −22.2 dB | +0.3 | 1.000 | 0 / 0 % | 3.04 / 3.04 | 427 / 427 |
| rchase2 | −26.2 dB | −26.2 dB | +0.0 | 1.000 | 0 / 0 % | 13.60 / 13.60 | 3,604 / 3,604 |
| schamp | −28.9 dB | −28.8 dB | +0.1 | 1.000 | 0 / 0 % | 15.37 / 15.47 | 3,575 / 3,575 |
| sfight | −29.2 dB | −29.1 dB | +0.1 | 1.000 | 0 / 0 % | 14.18 / 14.27 | 3,312 / 3,312 |
| sgt24h | −16.1 dB | −15.8 dB | +0.3 | 1.000 | 0.002 / 0.003 % | 20.21 / 20.19 | 1,120 / 1,120 |
| skytargt | −14.5 dB | −14.3 dB | +0.2 | 1.000 | 0.013 / 0.016 % | 9.69 / 9.71 | 1,870 / 1,874 |
| srallyc | −38.3 dB | −38.1 dB | +0.2 | 1.000 | 0 / 0 % | 5.86 / 5.88 | 2,386 / 2,386 |
| stcc | −20.7 dB | −20.7 dB | +0.0 | 1.000 | 0 / 0 % | 0 / 0 | 0 / 0 |
| topskatr | −12.1 dB | −12.1 dB | +0.0 | 1.000 | 0.016 / 0.016 % | 2.05 / 2.07 | 132 / 132 |
| vcop2 | −29.5 dB | −29.4 dB | +0.1 | 1.000 | 0 / 0 % | 15.53 / 15.60 | 3,502 / 3,502 |
| vf2 | −22.6 dB | −22.5 dB | +0.1 | 1.000 | 0 / 0 % | 9.34 / 9.36 | 1,692 / 1,692 |
| von | −20.0 dB | −19.8 dB | +0.2 | 1.000 | 0 / 0 % | 4.26 / 4.19 | 1,163 / 1,163 |
| vstriker | −24.1 dB | −23.9 dB | +0.2 | 1.000 | 0 / 0 % | 7.06 / 7.08 | 1,530 / 1,530 |
| zerogun | −24.2 dB | −23.9 dB | +0.3 | 1.000 | 0 / 0 % | 22.98 / 22.97 | 2,868 / 2,869 |

(RMS from 5 s on; stcc's music is on the DSB. desert and vcop have no SCSP, Model 1 sound
board, and are identical. Silent over 90 s of attract: airwlkrs, indy500, lastbrnx, manxtt,
pltkids, segawski, skisuprg, waverunr. hpyagu98 could not be loaded: backup ROM missing.)

These levels predate upstream's new per-game gains (cb57c0a, merged on 2026-10-01): the
absolute figures are out of date, the differences between the cores are not, since a gain
applies to both cores alike.

**Conclusions: nothing to change.**
- The new core is 0.0 to 0.3 dB louder than mame on every game, never more: well under
  what the gains are chosen to (by ear), so `kFlatGain` stays as it is for both cores.
- Clipping is the same on both cores (at most 0.017% of samples).
- The mean bus load differs by 3% at most (bel, gunblade) and usually under 1%, so the
  sound 68000 sees the same contention on both cores and the fitted constants hold. The
  key-ons, which depend on that timing, are identical or within 0.3%.
- A contention model closer to the manual (its 128 memory cycles per sample shared
  between refresh, 2 per slot fetching memory, the DSP's accesses, DMA and the CPUs) would
  count only slots that fetch from memory, and the DSP's accesses. It is left aside: the
  upstream constants were fitted on hotd, whose DSP load they therefore already absorb, and
  there is no hardware recording to fit a new model on.

## Check against a hardware recording: Dynamite Cop

The attract mode of a real Dynamite Cop / Dynamite Deka 2 cabinet (see "Audio references")
against 3 minutes of attract captured on each core, after the merge with upstream's
main-CPU wait states. The recording lines up with both `dynamcop` and `dyndeka2`
(chroma match 0.93–0.98); the stretch used is 20–116 s of the recording, its first 8 s
being silent. Scripts: [tools/hw_fit.py](tools/hw_fit.py) (tempo),
[tools/fine_pitch.py](tools/fine_pitch.py) (pitch on a 1-cent grid),
[tools/hw_wetness.py](tools/hw_wetness.py) (reverb indicators, octave-band balance).

| | Hardware | mame core | mednafen core |
|---|---|---|---|
| Tempo (`dynamcop` / `dyndeka2`) | — | +0.65% / +0.59% | +0.64% / +0.59% |
| Pitch | — | +0.1 cent | +0.0 cent |
| L/R correlation | 0.774 | 0.777 | 0.777 |
| side/mid | −10.1 dB | −10.4 dB | −10.4 dB |
| Envelope dips | 9.7 dB | 9.0 dB | 9.0 dB |
| 2–4 kHz / 4–8 kHz / 8–16 kHz, against 500 Hz–1 kHz | −4.1 / −10.4 / −17.7 dB | −3.2 / −8.1 / −12.8 dB | −3.2 / −8.5 / −14.1 dB |

- **Reverb: on a par with the hardware.** Stereo width, left/right correlation and the
  envelope's dips match within the measurement's precision on both cores. This is the
  first check of the effects DSP (step 8) against a cabinet rather than against mame, on
  the game where the DSP matters most.
- **Pitch: exact.** Within a cent, so the SCSP's sample clock is right.
- **Tempo: 0.6% fast on both cores.** Pitch being exact, this is timing rather than the
  audio clock, the same way as Sega Rally against its soundtrack: its pitch is also
  exact (+0.3 cent with the same method) and its tempo +0.49%. ~~Two references, one of
  them a cabinet, now point to a sound 68000 still slightly too fast after the MSVC fix,
  i.e. the bus contention fitted on hotd being a little short.~~ **Disproved on
  2026-10-03**: Dynamite Cop's tempo does not move with the sound 68000's wait states
  (+0.66% to +0.63% from zero to four fixed cycles), so recalibrating the contention cannot
  fix it; see "Tempo and the fixed wait states".
- **Tone: the cabinet is darker above 2 kHz** (1–5 dB depending on the band); mednafen is a
  little closer than mame at the top. It may be the board's output stage, the cabinet's
  amplifier, or the recording chain; a single recording cannot tell them apart.
- Daytona, for the record, measured the same way against its soundtrack: pitch +7.5 cents
  (+0.43%) with a tempo of −0.10%. That is the Model 1 sound board (MultiPCM), not the SCSP.

## Check against a hardware recording: Virtual On

The attract mode of a real Virtual On cabinet (see "Audio references") against 6 minutes of
attract captured from a fresh NVRAM (2026-10-08, at efe0731), each core twice: the mame core
with upstream's PR #9 and without it (its vibrato depth and FM fraction as before), the
mednafen core with the manual's vibrato depth and with the hardware model's, half of it.
The two variants of each core came from one build through a temporary environment
variable, removed since.

The attract runs its scenes in a different order on the cabinet, so the recording lines up
with the capture in stretches rather than as a whole: A (ref 20–64 s, capture 14.7 s on),
B (ref 92–136 s, capture 88.6 s on) and C (ref 176–196 s, capture 246.1 s on).

| | Hardware | mame core | mednafen core |
|---|---|---|---|
| Tempo, A / B / C | — | −0.04% / −0.05% / −0.05% | −0.01% / −0.02% / −0.03% |
| Pitch, A / B | — | 0.0 / +0.1 cent | 0.0 / +0.1 cent |
| L/R correlation, A / B | 0.989 / 0.990 | 0.982 / 0.984 | 0.983 / 0.986 |
| side/mid, A / B | −21.0 / −22.2 dB | −19.9 / −21.1 dB | −20.2 / −21.5 dB |
| Envelope dips, A / B | 9.9 / 9.8 dB | 9.4 / 9.7 dB | 9.4 / 10.1 dB |
| 2 / 4 / 8 kHz octaves against 500 Hz, A | +3.9 / +4.0 / −1.4 dB | +3.6 / +4.3 / +1.5 dB | +3.6 / +4.4 / +1.0 dB |
| 2 / 4 / 8 kHz octaves against 500 Hz, B | +4.0 / +4.4 / −0.9 dB | +2.9 / +3.2 / +0.5 dB | +3.0 / +3.2 / −0.0 dB |

- **Vibrato: not settled.** Von's attract hardly uses one. The mame core's two variants are
  bit-identical over the 6 minutes, and the mednafen core's differ on 0.01% of the samples,
  at −81.5 dB. Neither PR #9's depth nor the manual against the model can be judged on it.
- **Tempo: exact, on both cores.** Within 0.05% of the cabinet on all three stretches, with
  residuals of 1 to 2 ms. Unlike Sega Rally, Dynamite Cop and House of the Dead, 0.4% to
  0.65% fast, von's music is not, so their error is not one every sound program shares.
- **Pitch: exact.** Within 0.1 cent, so the recording's clock is right.
- **Reverb: close.** The emulation is a little wider (side/mid 1 dB higher, L/R correlation
  0.005 lower); the mednafen core is slightly the closer.
- **Tone: the cabinet is darker at the top**, 1 to 3 dB in the 8 kHz octave, as Dynamite
  Cop's is above 2 kHz. PR #9's output filter, 8 kHz and never applied, aims at the same.

## Tempo and the fixed wait states

Upstream's 88d5557 (2026-10-01, "improve hotd sound fidelity") dropped the fixed two wait
cycles of `Model2Sound::bus_wait`, keeping only the contention term. Measured on
2026-10-03 after merging it (013bfff): 3 minutes of attract on each core from a fresh
NVRAM, with the fixed part set from 0 to 4 cycles per access by a temporary environment
variable (removed since). House of the Dead was added the same day (at 8e94b36), against
a cabinet recording of its attract mode (see "Audio references"), the same way.

| Fixed wait cycles | Sega Rally against the OST | Dynamite Cop against the cabinet | House of the Dead against the cabinet |
|---|---|---|---|
| 0 (upstream from 88d5557 to PR #5) | +1.45% (17 ms) | +0.66% (56 ms) | +1.34% / +1.33% (35 ms) |
| 1 | +0.78% (10 ms) | — | +0.91% / +0.97% (30 / 25 ms) |
| 2 (restored here, upstream again since PR #5) | +0.48% (6 ms) | +0.65% (44 ms) | +0.48% / +0.57% (28 / 19 ms) |
| 3 | +0.45% (6 ms) | — | +0.07% / +0.21% (26 / 20 ms) |
| 4 | +0.37% (6 ms) | +0.63% (42 ms) | −0.28% / −0.22% (17 / 14 ms) |

Tempo against the reference, with the fit's residual rms in brackets, on the mame core;
the mednafen core gives the same figures within 0.01% (checked at 0 and 2 cycles on Sega
Rally, at 0 on Dynamite Cop). Sega Rally was fitted on two passes of its attract loop,
which agree within 0.01%. House of the Dead's attract music passes twice in the capture,
at 27.8 and 135.1 s; the two figures are those passes, which agree within 0.15%.

- **Sega Rally depends on the sound 68000's speed**: steeply up to two fixed cycles, then
  hardly. Without them its music is three times further from the soundtrack. Its tempo
  stays steady at every setting.
- **Dynamite Cop does not**: 0.03% across the whole range. Its 0.65% is therefore not the
  68000's speed, which disproves the conclusion first drawn from the cabinet recording.
- **House of the Dead depends on it all the way**: about 0.4% per cycle from 0 to 4, with
  no knee. Its pitch matches the cabinet's at every setting (+1.0 cent, with
  `fine_pitch.py`), so the recording's clock is right and the gap in tempo is the
  emulation's. Without the fixed cycles its music is 1.3% fast against the cabinet, with
  two 0.5%, with three 0.1–0.2%.
- **The fixed two cycles are restored on this branch** (`kBusWaitCycles = 2` in
  [model2_sound.cpp](src/hw/model2_sound.cpp)). Captures with it are bit-identical to the
  sweep's two-cycle ones. Upstream made the change for House of the Dead, yet House of the
  Dead's own cabinet recording is for the fixed cycles: without them its music goes from
  +0.5% to +1.3%. These figures went upstream with PR #5, merged on 2026-10-04 (51310b2):
  upstream main has the two fixed cycles again.
- **What remains** (+0.4% on Sega Rally, +0.65% on Dynamite Cop, pitch exact on both) is
  not the 68000's speed. Next candidates: the period of the SCSP timers the sound programs
  run on (their settings can be traced, on dynamcop first), the interrupt latency, or for
  Sega Rally the Saturn soundtrack itself.
- **Three cycles rather than two?** At two, the three games are all 0.5–0.65% fast. Three
  would bring House of the Dead within 0.2% and move the other two by 0.03% or less. But
  Sega Rally and Dynamite Cop no longer follow the 68000 there, so their common 0.5% has
  another cause, which three cycles might only be masking on House of the Dead. Two stays,
  the value upstream fitted the contention with (def088c), until that cause is looked at.

**Method note.** `tools/tempo_fit.py` finds each pass of the reference from its first piece
alone, and "Power Games" repeats sections. At 0 and 1 fixed cycle it latched onto a repeat
16 s later and reported tempos from −0.4% to +4.3% with residuals of 100 to 230 ms, a false
irregular tempo.
The figures above come from `tools/hw_fit.py`, with the start imposed:
`python tools/hw_fit.py tools <OST> 4 48 24.3 name=capture.wav` (the pass at 28.3 s; 77.6
for the next one). Before trusting `tempo_fit.py` on Sega Rally, check that it reports the
passes at about 28.3, 81.6 and 134.7 s.
House of the Dead: `python tools/hw_fit.py tools <recording> 0 62 27.8 name=capture.wav`
(135.07 for the second pass), and for its pitch
`python tools/fine_pitch.py <recording> 2 58 name=capture.wav@29.83`.

## Upstream's PR #9: changes to the mame core

Upstream merged PR #9 from pnixon-afk ("Update SCSP implementation with latest
adjustments", e1aafb7, merged as 2e056f2) on 2026-10-06. It changes the MAME-derived core
in three places:
- **Vibrato depth.** `UpdateSlot` now multiplies the pitch LFO's deviation by a new
  `PLFO_DEPTH_TABLE` ([scsp.cpp:1274](src/hw/scsp.cpp:1274)), 2/16 to 50/16 for PLFOS 1
  to 7. But that deviation already carries table 4.17's depth (`PSCALE`, applied in
  `PLFO_Step`), so the depth is scaled twice and no longer follows the table: ±0.9 cents
  at PLFOS 1 instead of ±7, ±10 at 3 instead of ±27, ±126 at 5 instead of ±112, and
  +1227/−2588 cents at 7 instead of ±494. Its comment says the values were tuned to match
  real hardware; that was not checked here. Upstream's PR #12 (ec9c3ac, 2026-10-08, same
  author) raised PLFOS 3 from 6/16 to 8/16: ±13.5 cents, which is exactly the hardware
  model's depth there, half the manual's ±27.
- **FM interpolation.** Under FM, only 1/16 of the change the modulation makes to the
  fractional position reaches the interpolation weight (`FM_FRAC_BLEND`,
  [scsp.cpp:1317](src/hw/scsp.cpp:1317)); the integer part still moves in full. MAME and
  Mednafen both interpolate at the modulated position.
- **Output filter.** A 2-pole Butterworth low-pass at 8 kHz, "tuned against a hardware
  recording", is set up in `init()` ([scsp.cpp:801](src/hw/scsp.cpp:801)) and its state is
  saved, but nothing applies it: it does not change the output.

**What its author says** (pnixon-afk, in a comment passed on by the user, 2026-10-10): he
targeted House of the Dead's chapter 2, chapter 3 and boss tracks. Before his change the
FM modulation did not seem to apply correctly: its "buzz" sat idly underneath the track
instead of following it as on hardware captures, and without the table those FM tracks
played at a higher pitch than the hardware. From his research, House of the Dead uses
PLFOS 1 and 2 only in the Magician's theme, and PLFOS 3 in its other FM tracks. He suggests
tuning the PLFOS value a von track uses the same way. Neither the double scaling nor the
higher pitch was measured here yet: a vibrato does not move a note's mean pitch by itself,
so the sharpness he heard may come from the FM rather than the depth.

**Not carried over to the mednafen core** (decided 2026-10-10). Each change is a few lines
there, but each answers a fault of the mame core that the mednafen core does not have:
- **The depth table.** It scales a depth the mame core already applies, so it follows
  neither the manual nor the hardware model, except at PLFOS 3: ±0.9 cents at PLFOS 1
  (model ±3.5, manual ±7), ±13.5 at 3 (model ±13.5, manual ±27), +1227/−2588 at 7 (model
  ±247, manual ±494). Its PLFOS 3, found by ear, is the hardware model's depth, which the
  mednafen core had before step 9 doubled it to follow the manual
  ([scsp_mdfn.cpp:1158](src/hw/scsp_mdfn.cpp:1158)). That points at going back to the
  model's depth, not at taking the table, whose low values are far below both.
- **The FM blend.** The "buzz" left under the track most likely comes from what the mame
  core lacks: the sound stack's four-slot delay (its `SCSP_FM_DELAY` code is dead,
  [scsp.cpp:1474](src/hw/scsp.cpp:1474), row 8 of "Where the mame core departs from the
  official documentation"), so for the slots just before a carrier the FM reads another
  generation of samples. The blend damps the symptom. The mednafen core has the delay
  ([scsp_mdfn.cpp:1468](src/hw/scsp_mdfn.cpp:1468)) and interpolates at the modulated
  position, placing the second sample with the next slot's fraction as the hardware model
  does ([scsp_mdfn.cpp:1050](src/hw/scsp_mdfn.cpp:1050)); the blend would likely make it
  worse.
- **The output filter.** Applied nowhere, upstream included. A filter like it makes sense,
  since both cabinet recordings are 1 to 3 dB darker around 8 kHz, but after both cores, in
  `Model2Sound`, fitted to those recordings, bearing in mind that the recording chain may
  account for part of it.

The depth and the blend are to be settled by measurement on the tracks upstream's author
used (next steps, item 2): cabinet recordings of House of the Dead's chapters 2 and 3, its
boss and the Magician's theme, and the same tracks captured on both cores, from save states
in those chapters or the sound test of the service menu if there is one.

Merged into `ScspMame` (cbc820d), PR #12 too (34437e4); the one conflict was `UpdateSlot`'s
signature. The filter's state raised the save-state format to 3 (see "Save states").

Checks on 2026-10-06:
- The mame core is bit-identical to upstream's build on the 16 A/B games (`m9dev`, `m9up`).
- House of the Dead, 3 minutes of attract: bit-identical to the capture made before the
  merge (`m9_hotd` against `hotdfw2`), so PR #9 does not touch it there.
- Dynamite Cop, 3 minutes of attract (`m9_dynamcop` against `fw2_mame_dynamcop`): the
  first difference comes at 176.8 s, the last 11 s of the capture, at −39 dB from the
  signal. Tempo (+0.65%) and pitch (+0.1 cent) against the cabinet are unchanged.
- The mednafen core is not touched; captures of it before the merge still stand.
- The mame core's earlier captures stand for games that use neither FM nor the pitch LFO.
  Von is the one A/B game seen using the pitch LFO (step 6) and was not measured again.

## Check against the official manual

Source: Sega's *SCSP User's Manual* (Sega Enterprises, 1997), read in its English HTML
version at [infochunk.com](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/index.htm)
(register maps read from the pages' tables directly). Every chapter was checked against
`ScspMednafen`:

| Area (manual section) | Manual | New core | Status |
|---|---|---|---|
| Specifications ([2.2](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p02_10.htm)) | 32 slots, 44.1 kHz, 22.5792 MHz, 128-step DSP, 13-bit COEF, 24-bit TEMP/MEMS, 26-bit accumulator | same | ✅ |
| Slot register map ([4.1, table 4.2](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_11.htm)) | every field and bit position | same | ✅ |
| Common registers ([table 4.3](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_12.htm)) | every field and bit position | same | ✅ |
| Sound stack ([table 4.4](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_13.htm)) | 64 words, two generations | same | ✅ |
| DSP registers ([tables 4.5–4.7](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_14.htm)) | COEF in bits 15..3, MADRS, MPRO as 4 words, TEMP/MEMS 8+16 bits, MIXS 4+16 bits, EFREG | same | ✅ |
| KYONEX/KYONB ([4.2.1](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_21.htm)) | KYONEX in any slot keys all slots | same | ✅ |
| SBCTL ([4.2.1](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_21.htm)) | bit 0 inverts all but the sign, bit 1 the sign | same | ✅ |
| SSCTL ([4.2.1](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_21.htm)) | 0 memory, 1 noise (the LFO's noise), 2 zero, 3 not available | 3 taken as 2 | ✅ |
| Loops ([4.2.1](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_21.htm), [FM method 1](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_fm1.htm)) | off, normal, reverse, alternate; reading stops at the end of a one-shot or at full attenuation | same (cut at 0x3C0, as measured on hardware) | ✅ |
| EG ([4.2.2](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_22.htm)) | 4 states, EGHOLD, DL on the top 5 bits, KRS 0xF = off, LPSLNK (attack ends at LSA, held at full level until then) | same | ✅ |
| FM ([4.2.3](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_23.htm), FM method [1](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_fm1.htm), [2](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_fm2.htm), [3](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_fm3.htm)) | MDL 0–4 none, then doubling to ±32,768 samples at MDL F; X and Y averaged; MDXSL/MDYSL generations (tables 4.9, 4.16); 5-slot lag before a slot's output can be read; displacement clipped to ±1K words | same, table by table | ✅ |
| TL, SDIR ([4.2.4](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_24.htm)) | 0.375 dB steps, −95.7 dB at 0xFF; SDIR skips EG, TL and ALFO | same | ✅ |
| Pitch ([4.2.5](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_25.htm)) | frequency ∝ (1024 + FNS) × 2^OCT | same | ✅ |
| LFO ([4.2.6](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_26.htm)) | LFOF table 0.17–172.3 Hz, LFORE, 4 waveforms, noise ignores LFORE and LFOF, ALFOS up to 24 dB | same | ✅ |
| **PLFOS ([table 4.17](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_26.htm))** | ±7, 13.5, 27, 55, 112, 230, 494 cents for PLFOS 1–7 | follows the manual since 2026-09-29 (−6.8, −13.6, −27.3, −55, −112, −231, −498 cents); the hardware model has half that | ✅ manual (see below) |
| Mixer ([4.2.7](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_27.htm)) | IMXL, DISDL, EFSDL 6 dB steps from −36 to 0 dB; pan 3 dB steps, −∞ at 0xF, bit 4 selects the side; EFSDL/EFPAN of slots 0–15 for EFREG, 16–17 for EXTS | same | ✅ |
| MVOL, DAC18B, MEM4MB ([4.2.7](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_27.htm), [4.2.9](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_29.htm)) | write-only; 16- or 18-bit DAC; must be 1 (4 Mbit) | same; see "Output" | ✅ |
| Slot status ([4.2.8](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_28.htm)) | MSLC write-only; CA in 4K-sample units | same, plus SGC and EG from the hardware model | ✅ |
| MIDI ([4.2.10](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2a.htm)) | 4-byte FIFOs, MIOVF/MIFULL/MOFULL/MOEMP, 31.25 kbit/s | same flags (plus MIEMP from the model); 31.36 kbit/s from the clock | ✅ (MIDI out unused on Model 2) |
| Timers ([4.2.11](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2b.htm)) | 8-bit up counters, prescaler 2^n, interrupt at 0xFF, (255 − TIM) × period | same | ✅ |
| Interrupts ([4.2.12](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2c.htm)) | 11 sources; SCIPD/MCIPD bit 5 writable; SCILV codes, sources 7–10 share bit 7's column; MIDI in/out interrupts clear themselves | same | ✅ |
| DMA ([4.2.13](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2d.htm)) | word transfers, DGATE zero-fill, DDIR, DEXE back to 0 when done, no DMA onto its own registers | same (instant) | ✅ |
| DMA timing ([3.2](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p03_10.htm), [4.2.13](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2d.htm)) | slows both CPUs down while running | not modelled (nor in the mame core) | — |
| VER, 0x400 bits 7..4 ([table 4.3](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_12.htm)) | version number | reads 0, as in the hardware model (the mame core reads back what was written) | — |
| DSP ([chapter 5](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p05_10.htm)) | only lists the memories; TEMP's pointer goes down by one every sample | same | ✅ |

Not in this manual: the DSP's instruction set (a separate Sega document), and the bits
the hardware model uses although the manual marks them reserved (EG bypass in word 5,
short wave and FNS bit 10 in word 8, MIEMP, SGC/EG in the monitor, the TEST register,
EXTS).

**Vibrato depth: the manual is followed.** The manual gives twice the depth of the hardware
model at every PLFOS step: the wave reaches ±2^(PLFOS+1)/1024 of the pitch, 768/1024 at
PLFOS 7, i.e. −498 cents against the manual's ±494. `pitch_lfo()` in
[scsp_mdfn.cpp](src/hw/scsp_mdfn.cpp) now doubles the model's wave, with a comment giving the
table. On the A/B captures only von, vcop2 and bel use a vibrato, briefly (0.01% of samples
differ, levels and spectra unchanged); mame stays bit-exact. A recording of a game with a
clear vibrato would confirm it against the hardware.

## Check against the DSP assembler manual

Source: Sega's *SCSP / DSP Assembler User's Manual* (dAsms 2.0, 1997), English HTML at
[infochunk.com](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/index.htm). It
describes the DSP through its assembly language rather than the instruction bits, which
still come from the hardware model. What it settles, all matching `run_dsp()`:

| Point (chapter) | Manual | New core |
|---|---|---|
| Y from YREG ([4](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p04_10.htm)) | YREGH = YREG bits 23..11; YREGL = 0 then bits 15..4 | same (YSEL 2 and 3, YSEL 3 never negative) |
| Store options ([4](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p04_10.htm)) | none: ×1 saturated; S1: ×2 saturated; S2: ×2 not saturated; S3: ×1 not saturated, store mode B | SHFT0/SHFT1 = 0, 1, 2, 3 |
| ADREG ([4](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p04_10.htm)) | mode A: INPUTS bits 23..16 sign-extended to 12 bits; mode B: shifter bits 23..12; storing to ADREG needs S3 | same |
| FREG ([4](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p04_10.htm)) | mode A: shifter bits 23..11; mode B: 0 then shifter bits 11..0 | same |
| Multiplier inputs ([4](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p04_10.htm)) | X is INPUTS or TEMP, B is the last result or TEMP; one TEMP read per step | same |
| Memory access ([4](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p04_10.htm), [6](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p06_10.htm)) | address = user address (MADRS) + DEC + ADREG + 1, NF for integer data | MADRS + ring offset + ADRS_REG + NXADR, NOFL |
| DEC ([6](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p06_10.htm)) | decremented by one every sample; makes the delay buffer a ring | `ring_offset` |
| TEMP ([6](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p06_10.htm)) | a ring too: what goes to TEMP00 is TEMP01 at the next sample | indexed from the ring offset |
| Coefficients ([5](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p05_10.htm)) | 13-bit fractions, 0x0800 = 0.5 | Y × X >> 12 |
| EFREG ([7](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p07_10.htm)) | "Multiple STR to EFREGxx": only the last store is valid | replaced, not accumulated |
| Pipeline ([6](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p06_10.htm)) | the assembler hides it from the programmer | the memory access one step late, from the hardware model |

## Where the mame core departs from the official documentation

The mame core ([scsp.cpp](src/hw/scsp.cpp), [scsp_dsp.cpp](src/hw/scsp_dsp.cpp)) is MAME's
SCSP. Checked against the two Sega manuals above, it departs from what they state in these
places (each checked in the code):

| # | Manual says | mame core does | Where |
|---|---|---|---|
| 1 | The DSP runs 128 steps per sample ([SCSP manual 2.2](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p02_10.htm)) | does not run at all until the program writes 0xBF0 (the first word of step 126); from then on it stops after the last non-zero step, as it stood at that write | [scsp.cpp:1111](src/hw/scsp.cpp:1111), [scsp_dsp.cpp:113](src/hw/scsp_dsp.cpp:113), [scsp_dsp.cpp:319](src/hw/scsp_dsp.cpp:319) |
| 2 | DEC goes down by one every sample ([assembler manual 6](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p06_10.htm), [SCSP manual 5.2](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p05_10.htm)) | an IRA above 0x31 abandons the program (`return`), skipping the DEC decrement and the MIXS reset for that sample | [scsp_dsp.cpp:189](src/hw/scsp_dsp.cpp:189) |
| 3 | Only the last store to an EFREG counts ([assembler manual 7](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p07_10.htm)) | EFREG is cleared, then every store is added (`+=`) | [scsp_dsp.cpp:303](src/hw/scsp_dsp.cpp:303) |
| 4 | ADREG in store mode A is INPUTS' top byte sign-extended ([assembler manual 4](https://www.infochunk.com/saturn/segahtml_en/sndt/tool/dasm/hon/p04_10.htm)) | added as an unsigned 12-bit value (`& 0x0FFF`), so a negative modulation lands 4,096 words away | [scsp_dsp.cpp:269](src/hw/scsp_dsp.cpp:269) |
| 5 | SBCTL inverts bits of the source waveform data ([SCSP manual 4.2.1](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_21.htm)) | inverts the sample after interpolation, which is not the same once two samples are blended | [scsp.cpp:1299](src/hw/scsp.cpp:1299) |
| 6 | A slot's noise is the output of its LFO's noise oscillator; with the noise waveform, LFOF and LFORE have no effect ([SCSP manual 4.2.1](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_21.htm), [4.2.6](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_26.htm)) | slot noise from a separate random generator ("unknown algorithm"); the LFO's noise is a random table stepped at the LFOF rate | [scsp.cpp:1295](src/hw/scsp.cpp:1295), [scsp.cpp:1726](src/hw/scsp.cpp:1726) |
| 7 | LFORE resets the LFO, which runs again once cleared ([SCSP manual 4.2.6](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_26.htm)) | LFORE is decoded but never used | [scsp.cpp:79](src/hw/scsp.cpp:79) |
| 8 | A slot's output is written to the sound stack several cycles after it is computed: when slot C reads its modulation, the newest output there is slot C−5's; MDXSL/MDYSL select the latest or the previous generation accordingly ([SCSP manual 4.2.3](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_23.htm), FM method [2](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_fm2.htm) and [3](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_fm3.htm), tables 4.9 and 4.16) | no delay: the `SCSP_FM_DELAY` code is dead, so for the four slots just before the carrier FM reads another generation than the tables give | [scsp.cpp:1412](src/hw/scsp.cpp:1412) |
| 9 | The MIDI input FIFO is 4 bytes, with MIFULL and MIOVF ([SCSP manual 4.2.10](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2a.htm)) | a 32-byte FIFO, nothing lost | [scsp.h:216](src/hw/scsp.h:216), [scsp.cpp:540](src/hw/scsp.cpp:540) |
| 10 | Register 0x404 shows MIFULL, MIOVF, MOEMP and MOFULL ([SCSP manual table 4.3](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_12.htm), [4.2.10](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2a.htm)) | the flag byte is never updated | [scsp.cpp:1017](src/hw/scsp.cpp:1017) |
| 11 | Interrupt sources 9 (MIDI output empty) and 10 (every sample) exist ([SCSP manual table 4.31](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_2c.htm)) | neither is ever raised (the sample interrupt is commented out) | [scsp.cpp:1057](src/hw/scsp.cpp:1057) |
| 12 | The vibrato's depth by PLFOS is ±7 to ±494 cents ([SCSP manual table 4.17](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_26.htm)) | since upstream's PR #9 and #12, the table's deviation is scaled again by 2/16 to 50/16: ±0.9 cents at PLFOS 1, ±13.5 at 3 (the hardware model's), +1227/−2588 at 7 | [scsp.cpp:1274](src/hw/scsp.cpp:1274) |

The vibrato depth (PLFOS, [SCSP manual table 4.17](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_26.htm)) is where the mame core
agreed with the manuals and the hardware model did not, and the new core was made to follow
the table as well. Since upstream's PR #9 the mame core no longer does (row 12).

Differences the manuals do not settle, left to the hardware model: memory accesses allowed
on odd DSP steps only and carried out at once (the SCSP manual's budget of 64 DSP accesses
per sample fits either); the DSP's ACC, FRC, Y and ADRS registers reset every sample; which
source wins when several interrupts are pending (the mame core takes a fixed order over five
sources, the model the highest SCILV level); one-shot timers; the attack's curve; MVOL's
curve; the key-scaling formula.

## Differences found between the two cores

Found in the initial comparison of 2026-09-29. "sm2" means the current mame core.

### Envelope (EG) and volume
- sm2: rates taken from tables in milliseconds; attack **linear** in amplitude
  ([scsp.cpp:1376](src/hw/scsp.cpp:1376)); key scaling = octave + 2×KRS + FNS bit 9
  ([scsp.cpp:559](src/hw/scsp.cpp:559)); EG, TL and ALFO multiplied separately; slot
  cut at the end of the release.
- Mednafen (`scsp.inc` RunEG, l. 1006): EG clocked by the global counter; exponential
  attack; KRS + octave clamped to 0–0xF; EG, TL and ALFO **added** and then capped at
  0x3FF; memory reads cut beyond 0x3C0; all 32 slots always run.
- MVOL: linear in sm2 (`MVOL/15`, [scsp.cpp:843](src/hw/scsp.cpp:843)), logarithmic in
  Mednafen (about 3 dB per step).

### FM
- sm2: no pipeline delay ([scsp.cpp:1255](src/hw/scsp.cpp:1255)); the `SCSP_FM_DELAY` code
  is dead (`m_DELAYBUF` does not exist).
- Mednafen: a slot's output is written into the stack 4 slots later (`SoundStackDelayer`);
  the interpolation's next sample uses the next slot's phase.
- SBCTL applied after interpolation (sm2) against before (Mednafen); xorshift noise (sm2)
  against the hardware's 17-bit LFSR (Mednafen).
- Since upstream's PR #9, sm2 lets only 1/16 of the modulation's fractional change into
  the interpolation weight ([scsp.cpp:1317](src/hw/scsp.cpp:1317)); Mednafen interpolates
  at the modulated position.

### LFO
- sm2: frequency in Hz, separate PLFO/ALFO phases, only advances for active slots,
  **LFORE ignored** ([scsp.cpp:79](src/hw/scsp.cpp:79)).
- Mednafen: shared 8-bit counter, exact period, LFORE handled, vibrato depth depending on
  FNS.

### DSP
- sm2: only starts after a write to 0xBF0 and stops at `LastStep`, computed once
  ([scsp_dsp.cpp:319](src/hw/scsp_dsp.cpp:319)).
- sm2: `return` in the middle of the program if IRA > 0x31
  ([scsp_dsp.cpp:190](src/hw/scsp_dsp.cpp:190)), which skips the DEC decrement and the
  MIXS reset.
- sm2: memory accesses allowed only on odd steps (a workaround for DoA, l. 278) and
  immediate; Mednafen models the read and write delay.
- sm2: ACC, FRC, Y_REG and ADRS_REG reset to 0 every sample; EFREG cleared and then
  accumulated (`+=`). Mednafen keeps the registers and replaces EFREG.
- Despite these differences, the measured levels and reverb indicators of the two cores
  agree on every game tested (see "Step 8 findings").

### Timers, interrupts, MIDI
- One-shot timers in sm2 ([scsp.cpp:454](src/hw/scsp.cpp:454)); free-running and aligned
  to the global counter in Mednafen.
- Interrupt level: MAME's priority cascade in sm2 ([scsp.cpp:359](src/hw/scsp.cpp:359)),
  exact SCILV encoding and a per-sample interrupt (0x400) in Mednafen.
- MIDI: 32-byte FIFO in sm2 (4 on the hardware); **register 0x404's flags never updated**
  ([scsp.cpp:1017](src/hw/scsp.cpp:1017)).
- DMA: in sm2, the address registers are restored in one direction only
  ([scsp.cpp:1520](src/hw/scsp.cpp:1520)); Mednafen never modifies them.

### Already on a par (or ahead) in sm2
- One sample every 256 cycles of the 68000, synchronised to the sample (as in Mednafen).
- Wait states and RAM/SCSP bus contention for the 68000: absent from Mednafen (a TODO
  there). Without effect in MSVC builds until the 2026-09-29 fix (see "Tempo").
- The Model 2's own sample ROM window.

## Per-game validation

Legend: ✅ correct, ⚠️ difference measured or heard, ❌ broken, — not tested.

| Game | Feature | mame | mednafen | Notes |
|---|---|---|---|---|
| hotd | FM, tempo (contention fitted on it), DSP from 30 s of attract | ⚠️ | — | tempo +0.5% against a cabinet recording with the fixed wait states (+1.33% without), pitch within a cent; cores equivalent over 3 min of attract; PR #9 leaves its attract bit-identical; FM to be recorded in play |
| vf2 | DSP reverb | — | — | |
| dynamcop | DSP reverb (strongest use) | ⚠️ | ⚠️ | against a cabinet recording: reverb and pitch match, tempo 0.65% fast on both cores, whatever the sound 68000's wait states; PR #9 changes the mame core's output from 176.8 s of attract only |
| von | pitch LFO (the one A/B game using it in 26 s) | ✅ | ✅ | against a cabinet recording: tempo within 0.05%, pitch within 0.1 cent, reverb close, 1–3 dB brighter at 8 kHz, on both cores; its attract hardly uses the vibrato, so PR #9's depth is still to judge |
| doa | DSP, MADRS | — | — | |
| daytona | original Model 2 | — | — | Model 1 sound board, no SCSP; aligns with its OST at −0.10% tempo, pitch +7.5 cents |
| srallyc | MSLC (end-of-music beeps) | ⚠️ | ⚠️ | tempo +0.48% against the OST with the fixed wait states (+1.45% without, +2.5% before the 68000 fix); drier than the OST, but no DSP program in attract |
| stcc | DSB (MPEG music) | — | — | only the effects go through the SCSP |
| indy500 | | — | — | |
| vcop | original Model 2 | — | — | |
| vstriker | MSLC | — | — | |
| lastbrnx | | — | — | |

## Log

- **2026-09-29**: file created. Initial comparison of the two cores, choice of a
  `ScspCore` interface switched when a game is loaded, licence issue raised.
- **2026-09-29**: step 1. New interface [scsp_core.h](src/hw/scsp_core.h); `Scsp` renamed
  `ScspMame` and derived from `ScspCore` (only the declarations and the `ScspMame::` prefix
  change, the function bodies stay MAME's); `Model2Sound` goes through a
  `std::unique_ptr<ScspCore>`. Validation: 20 games captured over 1,500 frames before and
  after, WAVs bit-identical (16 with sound, 4 silent), and `--savestate-test 900` PASS on
  vf2 and hotd. Save-state format unchanged. Added [tools/scsp_ab.ps1](tools/scsp_ab.ps1).
- **2026-09-29**: step 2. `ScspCoreKind` (`mame` = 0, `mednafen` = 1) and its names in
  [scsp_core.h](src/hw/scsp_core.h); `scsp_core` setting, `--scsp-core` option, menu in the
  Audio tab; `Model2Sound::set_scsp_core()` called by `load_game()` before the reset; save
  states at format 2 with the `SCSPCORE` marker + id, format 1 still read. Validation: 16
  games bit-identical to the original; `--savestate-test 900` PASS on vf2 and hotd,
  including the new "wrong-scsp-core" refusal; a format 1 state (hotd) loads and the sound
  carries on; `--scsp-core mednafen` falls back to mame with a warning; `--scsp-core foo`
  is refused.
- **2026-09-29**: step 3. [scsp_mdfn.h](src/hw/scsp_mdfn.h) / [scsp_mdfn.cpp](src/hw/scsp_mdfn.cpp):
  the control side of the new core (slot registers, common control, sound stack, DSP
  memories, DMA, three free-running timers, SCILV interrupt controller, MIDI with 4-byte
  FIFOs), silent. CMake option `SM2_SCSP_MEDNAFEN` (ON by default). `ScspCore::read()` takes
  the byte mask; `Stats::midi_in_dropped`. Validation: mame still bit-exact (16 games); on
  the new core, the sound program runs on all 16 games (see "Step 3 findings");
  `--savestate-test 900` PASS on vf2, hotd and bel; build and fallback checked with
  `SM2_SCSP_MEDNAFEN=OFF`.
- **2026-09-29**: step 4. Slot playback in `ScspMednafen`: pitch step (FNS/OCT, 14 bits of
  fraction), 6-bit linear interpolation, the four loop modes, 8/16-bit PCM through
  `ScspMemory`, noise (17-bit LFSR clocked every slot), SBCTL before interpolation, the
  monitor's CA field. Provisional direct mix (TL, DISDL/DIPAN, MVOL, per-game gains),
  without EG. `active_slots()` = voices reading. `tools/scsp_ab.ps1` accepts "a,b" lists
  through `pwsh -File`; added `tools/scsp_compare.py`. Validation: mame bit-exact (16
  games); new core at the right pitch on the 15 games with sound and within 0–2 dB of
  mame (dynamcop −6 dB); sound program counters identical to mame on bel, vf2, von, hotd;
  `--savestate-test 900` PASS on vf2, hotd, bel.
- **2026-09-29**: step 5. The hardware's envelopes and level arithmetic in `ScspMednafen`
  (see "Step 5 findings"); step 4's provisional mix is gone; `active_slots()` follows the
  memory reads. Added `tools/scsp_eg_timing.py`. Validation: mame bit-exact (16 games);
  envelope rates matching the documentation (exact attacks, decays within 1023/960);
  key-ons identical to mame on all 16 games; levels within ±0.2 dB on 5 games;
  `--savestate-test 900` PASS on vf2, hotd, bel.
- **2026-09-29**: step 6. One LFO per slot in `ScspMednafen` (see "Step 6 findings"); the
  LFSR now advances in `generate()`, between the PLFO read and the ALFO read. Validation:
  frequencies matching the documented table; 15 WAVs out of 16 identical to step 5 (von
  changes); key-ons identical to mame; `--savestate-test 900` PASS on vf2, hotd, von; mame
  bit-exact.
- **2026-09-29**: step 7. Sound stack fed by the slots with the 4-period delay and STWINH,
  FM input (MDL, MDXSL, MDYSL) in playback, the next slot's fraction for the second
  sample, short wave (see "Step 7 findings"). `m_stack_delay` goes into the new core's save
  states. Validation: mame bit-exact, new core identical to step 6 on the 16 games (no FM
  in attract).
- **2026-09-29**: Sega Rally compared with the OST "Power Games": tempo 2.5% too fast.
  Cause: the sound 68000's wait states were lost under MSVC (`m68k_execute(cycles) +
  m_stalled` unsequenced in [m68000.cpp](src/cpu/m68000/m68000.cpp)). After the fix: +0.49%
  and a steady tempo, on both cores. Added `tools/ost_compare.py`, `tools/tempo_fit.py` and
  `tools/find_pieces.py`. See "Tempo: the sound 68000's bus contention was lost under MSVC".
- **2026-09-29**: step 8. The new core's effects DSP: `run_dsp()` (128 steps, latches,
  shifter, memory accesses one step late, 16-bit float format), `send_to_dsp()` (MIXS from
  ISEL/IMXL), the `Dsp` state in the new core's save states. Validation: mame bit-exact;
  levels and reverb on a par with mame on the 15 games with sound and doa; key-ons
  unchanged; `--savestate-test 900` PASS (vf2, hotd, bel, dynamcop). Findings: Sega Rally
  loads no DSP program in attract mode; Daytona has no SCSP and aligns with its OST at
  −0.10%.
- **2026-09-29**: file translated into English. The comparison method now states that
  every comparison was made on audio captures; added `tools/wetness.py` and
  `tools/clipping.py`; the step 3 explanation of the key-on gap is marked as not holding
  since the 68000 fix.
- **2026-09-29**: step 9. Two passes per sample, loops followed for every slot, fraction
  mirrored when a loop turns round, monitor latched as its slot runs, FNS bit 10, MIDI bit
  clock from the chip's clock (see "Step 9 findings"). The new core was also checked
  chapter by chapter against Sega's *SCSP User's Manual* (see "Check against the official
  manual"): everything matches except the vibrato depth (PLFOS), left open. Validation:
  mame bit-exact; 11 captures change through the monitor with levels within 0.1 dB and
  identical spectra; key-ons unchanged; `--savestate-test 900` PASS (vf2, hotd, bel,
  dynamcop); loop turns checked by simulation.
- **2026-09-29**: vibrato depth (PLFOS) set to the manual's table 4.17, twice the hardware
  model's (comment in `pitch_lfo()`); von, vcop2 and bel change marginally, mame
  bit-exact. The DSP checked against Sega's *SCSP / DSP Assembler User's Manual*: everything
  it describes matches. Added "Where the mame core departs from the official
  documentation": 11 points, each located in the code.
- **2026-09-29**: "References" section added: every page of the two Sega manuals that was
  read, the emulator sources and the audio references, with their links; the check tables
  now link each row to its manual page.
- **2026-09-29**: step 10. The per-game balance and the bus contention checked on the new
  core over 90 s on the 24 sets with a gain that play: levels 0.0 to +0.3 dB from mame,
  same clipping, bus load within 3%, key-ons within 0.3%. Nothing to change (see "Step 10
  findings").
- **2026-09-30**: checked against a hardware recording of Dynamite Cop's attract mode:
  reverb and pitch match the cabinet on both cores; tempo 0.6% fast on both, like Sega
  Rally against its soundtrack, pointing at the sound 68000's bus contention. Added
  `tools/hw_fit.py`, `tools/fine_pitch.py` and `tools/hw_wetness.py`.
- **2026-10-01**: merged upstream main (8da7bbd): upstream's take of the 68000 fix
  (8e1e7c3), new per-game gains (cb57c0a; step 10's absolute levels are now out of date)
  and the fixed wait states dropped (88d5557).
- **2026-10-03**: tempo measured again after merging upstream once more (013bfff): Sega
  Rally +1.45% without the fixed wait states and +0.48% with them, Dynamite Cop +0.65%
  either way. The fixed two cycles are restored (`kBusWaitCycles`), the conclusion drawn
  from the Dynamite Cop recording is corrected, and a method note warns about
  `tempo_fit.py` on repeated sections. See "Tempo and the fixed wait states".
- **2026-10-03**: House of the Dead checked against a cabinet recording of its attract
  mode ([YouTube](https://www.youtube.com/watch?v=5O9s1V9RoPs)): +1.33% without the fixed
  wait states, +0.5% with two, +0.1–0.2% with three, pitch within a cent. Its tempo follows
  the 68000's speed all the way, unlike Sega Rally's beyond two cycles. Two cycles kept. See
  "Tempo and the fixed wait states".
- **2026-10-04**: upstream merged PR #5 (51310b2): the sound 68000's two fixed wait states
  are back in upstream main, on the strength of the figures above.
- **2026-10-06**: merged upstream main up to 2e056f2 (cbc820d): upstream's PR #9 changes the
  mame core's vibrato depth and FM interpolation and adds an output filter that is never
  applied; save-state format 3. The mame core matches upstream's build on the 16 A/B games;
  House of the Dead's attract is unchanged by it and Dynamite Cop's changes only in its last
  11 s, tempo and pitch unchanged. See "Upstream's PR #9: changes to the mame core". Added
  "Where things stand" at the top, and the reference recordings now sit in
  `build/scsp_ab/ref_*.wav`.
- **2026-10-08**: merged upstream main up to d7e1cab (efe0731): the Model 2B/2C coprocessor
  speed-up (PR #10) and the low-resolution overlay (PR #11). The mame core still matches
  upstream's build on the 16 A/B games, bit-identical to before. Checked against a cabinet
  recording of Virtual On's attract: tempo and pitch exact on both cores, reverb close, the
  cabinet darker at 8 kHz; the attract hardly uses the vibrato, so that question stays open.
  See "Check against a hardware recording: Virtual On".
- **2026-10-10**: merged upstream main up to 3c9e79a (34437e4): v0.9.43's translated
  interface (with the harfbuzz and SheenBidi submodules; our interface strings now go
  through `tr()`, `tr_id()` and `tooltip()` as upstream's do), PR #12 (PLFOS 3 at 8/16,
  the hardware model's depth) and PR #13 (gun aim). The mame core still matches upstream's
  build on the 16 A/B games, bit-identical to before. PR #9's author explained what he
  tuned and on which tracks; see "Upstream's PR #9: changes to the mame core".
- **2026-10-10**: PR #9's and #12's changes are not carried over to the mednafen core: the
  depth table scales twice and only meets the hardware model at PLFOS 3, the FM blend works
  around the stack delay the mame core lacks and the mednafen core has, and the output
  filter belongs after both cores. Both to be measured on House of the Dead's in-game
  tracks. See "Not carried over to the mednafen core".
