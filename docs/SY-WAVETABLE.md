[Synfact docs](README.md) › SY WAVETABLE (design)

# SY WAVETABLE: a dual wavetable machine (design)

Status: **design agreed 2026-11-23; core 2.8.0 (section 5), the import tools
(section 6) and the machine 0.1.0 (section 3, `mods/wavetable-st`) are
written.** Nothing here has run on a unit.

SY WAVETABLE is a Syntakt machine that does what the Tonverk's Wavefinder SRC
machine (Tonverk OS 1.3.0 and later) does: two independent wavetable
oscillators, each with its own internal modulator (ANIM), mixed into one track.
It reads the Tonverk's wavetable files without conversion by the user.

Source of the behaviour: the Tonverk User Manual OS 1.4.1 (Elektron,
2026-09-23), appendix A.2.5 (the machine) and sections 5.2.5 and 5.2.6 (the
files and the pools). The manual gives no numeric ranges; the ranges below are
ours.

## 1. What the Tonverk machine does

Two pages, one per oscillator, eight knobs each:

| Page 1 (osc 1) | Page 2 (osc 2) | Behaviour |
|---|---|---|
| TUNE | DTUN | TUNE: bipolar pitch of osc 1. DTUN: detune of osc 2 against osc 1 |
| LEV | LEV | the oscillator's level |
| POS | POS | the wave played; moving it crossfades between neighbouring waves |
| SLOT | SLOT | the wavetable, from the project's wavetable pool; p-lockable |
| SPD | SPD | the time of one ANIM cycle; looping shapes snap to beat divisions of the tempo |
| A.LEV | A.LEV | ANIM's depth on LEV |
| A.POS | A.POS | ANIM's depth on POS, bipolar (0: none, negative: subtracted) |
| ANIM | ANIM | the modulator's shape (below) |

ANIM shapes, in the Tonverk's order: Exp Down, Ramp Down, Tri, Ramp Up, Exp Up
(one shot); Ramp Down, Tri, Square, Ramp Up (looping); Random; Sample and
Hold; Wavetable 1/2 (looping).

Wavetable files (section 5.2.5):

- an ordinary audio file, in any format a sample may use;
- up to 64 waves; a file with more waves gives 64 evenly spaced ones, so other
  tools' tables, and even plain samples, load as they are;
- wave sizes 64, 128, 256, 512, 1024, 2048 or 4096 samples; 2048 unless the
  file name ends in `_wt<size>` (`pad_wt1024.wav`);
- an `r` after the size (`strings_wt128r.wav`) loads it "without
  interpolation".

## 2. Decisions

| # | Question | Decision |
|---|---|---|
| 1 | A separate wavetable pool? | **No.** Wavetables use the core's project pool (positions 65 to 128) like samples. The project record does not change |
| 2 | Where files live | An HZFS v2 root folder **`wavetables`**, created by the tools when absent |
| 3 | Where conversion happens | **At import**, on the PC (SDK) and in the browser (Transfer), into one canonical layout with band-limited levels (section 4). AUDIO does no FFT |
| 4 | How AUDIO knows a slot is a wavetable | A **core change first**: an HZFS entry flag and a pool slot kind (section 5) |
| 5 | What `r` means | Nearest-neighbour upsampling of a short wave to 2048 (a stepped, lo-fi wave), applied at import. Crossfading between waves stays on. *To confirm against a Tonverk* |
| 6 | "Wavetable 1/2 (looping)" | ANIM's shape is the oscillator's own current wave (osc 1 for page 1, osc 2 for page 2), read as a bipolar LFO |
| 7 | Pages 3 and 4 | **None in 1.0** (faithful). Later candidates: phase reset on trig, ANIM retrig mode, osc 1 / osc 2 pan on stereo tracks |
| 8 | Wavetables on SP machines | Not refused. The slot is marked; an SP machine plays its PCM as a mono sample |
| 10 | Plain samples on SLOT (as the Tonverk) | **Yes, as they are** (owner, 2026-11-25): any pool sample plays as a wavetable, 2048-frame waves, at most 64 evenly spaced, a stereo sample's L; no conversion on the PC or the web, so no band-limited levels (high notes alias) |
| 9 | Name, type | **SY WAVETABLE**, short name `WTBL`, type **72**, mod `mods/wavetable-st` |

## 3. The machine

Descriptor: `{72, 17, "SY WAVETABLE", "WTBL", "SYN", controls, ST_MACHINE_NO_INIT, 44, 10, 17}`,
with a UI descriptor (`machine-ui-v1`) of two pages. Controls 0 to 7 are the
first page and 8 to 15 the second, so page 2 comes from `st_machine_extended`
(MACHINE-REFERENCE section 7: keep our own defaults while it is null).
Control 16 (`TMPO`) is hidden, with alias `ST_MACHINE_TEMPO_PARAMETER`: the
core sends the tempo in it (section 5).

| Ctl | Name | Range shown | Default | Meaning |
|---|---|---|---|---|
| 0 | TUNE | -48.00 to +48.00 | 0 | semitones on both oscillators, fine steps |
| 1 | LEV | 0 to 127 | 100 | osc 1 level |
| 2 | POS | 0 to 127 | 0 | osc 1 wave, 0 = first, 127 = last, fine steps between |
| 3 | SLOT | 1 to 128 | 65 | osc 1 pool position; shows the sample name |
| 4 | SPD | 0 to 127 | 64 | ANIM 1 time (below) |
| 5 | A.LEV | -64 to +63 | 0 | ANIM 1 depth on LEV |
| 6 | A.POS | -64 to +63 | 0 | ANIM 1 depth on POS |
| 7 | ANIM | 12 names | EXP D | ANIM 1 shape (pop-up names: EXP D, RAMP D, TRI, RAMP U, EXP U, L RAMP D, L TRI, L SQUARE, L RAMP U, RANDOM, S&H, WAVE) |
| 8 | DTUN | -64 to +63 | 0 | osc 2 against osc 1; ±64 = ±1 semitone |
| 9 to 15 | LEV … ANIM | as 1 to 7 | LEV 0 | the same for osc 2 |
| 16 | TMPO | hidden | 14400 | the tempo, BPM x 120, filled in by the core |

Sound:

- osc 1 pitch = note + TUNE; osc 2 pitch = osc 1 pitch + DTUN.
- Each oscillator crossfades two neighbouring waves at POS + A.POS·m (clamped
  to the table), with linear interpolation inside each wave.
- Level = LEV + A.LEV·m, clamped to 0..127. Output = (osc 1 + osc 2) / 2, so
  one oscillator at 127 sits at -6 dB; to match against SP ONE SHOT on the unit.
- SLOT plays a canonical wavetable (nine band-limited levels), or a plain
  sample as it is: waves of 2048 frames, at most 64 evenly spaced as the
  Tonverk takes a sample (`wt_sample_starts`), one level, a stereo sample's
  L. An empty slot or a sample shorter than 2048 frames is silent for that
  oscillator, and the page says which.
- m is ANIM's output, 0..1 for the ramp, tri, square and exponential shapes,
  -1..1 for Random (smoothed), Sample and Hold (stepped) and Wavetable.
- One-shot shapes restart on every trig and hold their last value. Looping
  shapes restart on every trig too (1.0), and run from SPD. Every trig also
  restarts both oscillators' phases.
- SPD: one-shot shapes, an exponential time from 2 ms to 30 s; looping
  shapes (and Random, Sample and Hold, Wavetable), one of 17 tempo divisions,
  1/16 beat (a 1/64 note) to 64 beats (16 bars), SPD x 17 / 128: 1/16, 1/8,
  3/16, 1/4, 3/8, 1/2, 3/4, 1, 3/2, 2, 3, 4, 6, 8, 16, 32, 64 beats.
- A.POS and A.LEV: depth x 2 x m added to POS and LEV, so +-63 sweeps the whole
  range.
- The band-limited level is chosen once a block from each oscillator's phase
  step: the richest level whose top harmonic stays under 28 kHz (aliases land
  above 20 kHz). No crossfade between levels in 1.0.

State: per track, in the bank's AUDIO memory (as SY MnM), with a signature at
`+0x6f8` (MACHINE-REFERENCE section 6) to notice the stock reset. The machine
does not call the stock filter at `0x400068ea`; it uses the native update for
declick and gains, then its own render, as SY MnM does.

Cost, measured on analog-emu's cfbench (instructions, not cycles): about
3,100 a block per track with both oscillators crossfading at a fixed position
(a table or a plain sample), about 3,800 with ANIM on position, against 9k to 13k for MD KIT voices.
Cache misses on the tables are not counted; the unit's CPU reading is to come.

## 3b. The screen

The SYN pages are drawn as the Tonverk's Wavefinder SRC page (Elektron's
video "Wavefinder", 2026; the manual's page 94 shows the same page): one
page per oscillator, its eight controls around the wave. On the Syntakt the
native track rail (x < 19), the header (y > 51) and the page indicator
(x > 124) stay native; the page indicator plays the Tonverk's W1/W2.

| Where | Tonverk | SY WAVETABLE (`page.c`) |
|---|---|---|
| top row | TUNE as `..0..` in a box, LEV over its bar, `[POS]`, `W` + the slot inverted | the same; page 2 shows DTUN's steps in the box |
| middle | the wave at the position | the wave at the position, crossfaded between neighbours, 102 columns from level 5 (64 samples a wave) |
| under it | a dotted track with the position's marker | the same; the marker follows AUDIO's position while the oscillator sounds, else POS |
| bottom row | SPD's scale, A.LEV and A.POS as bipolar bars, ANIM's shape | the same |

- The wave is read from eMMC once per slot (core 2.8, UI API 9
  `wavetable_level`), not every frame.
- The moving position comes from AUDIO: osc 1 in the track's cursor record,
  osc 2 in record 8 + track (core 2.8's 16 records), in frames (waves x 4088),
  lent only for the slot's own PCM.
- A p-locked control's cell is inverted, as FM TONE marks its locks.
- Value pop-ups stay native (the format callback, as before).
- SLOT opens the pool list as the SP machines' SAMP does (names, `WT`/`ST`
  tags, NO reverts, p-locks included): `mods/wavetable-st/slot-list.c`, the
  SP list made machine-neutral so that both can share it later.
- `tests/test_wavetable_st_page_emu.py` runs the bank's draw on the stock
  1.42 routines and writes PNGs; its text is recorded and drawn by the test,
  since the native fonts need the boot that the fixture does not run.

## 4. The canonical wavetable

The import tools (SDK and Transfer, byte for byte the same) turn any accepted
file into this layout:

1. Read the audio, mixed to mono (the channels' mean), at its own rate (no
   resampling to 48 kHz: a wave is a cycle, not a time).
2. Wave size from `_wt<size>` at the end of the name (64 to 4096, any case),
   else 2048; `r` after the size marks nearest-neighbour upsampling. Another
   size is refused, and so is a file shorter than one wave. Samples after the
   last whole wave are left out, with a note, so plain samples load too, as on
   the Tonverk.
3. More than 64 waves (N): keep 64 evenly spaced, wave
   `(i * (N - 1) + 31) // 63` for i = 0 to 63, so the first and the last stay.
   W = the count kept, 1 to 64.
4. Each wave to 2048 samples: one FFT per wave; a shorter wave is first
   repeated sample by sample when `r` is set; a 4096 wave loses its top octave.
5. Levels k = 0 to 8: wave length 2048 >> k (2048 down to 8), harmonics up to
   (1024 >> k) - 1, by inverse FFT of the truncated spectrum. DC removed.
6. One gain for the whole table, so its loudest sample is full scale (a
   silent table stays zero); `floor(x + 0.5)`, clamped, to PCM16 big endian.

The FFT is a radix-2 one whose roots come from half angles (only +, -, *, /
and sqrt), so the Python and the TypeScript give the same bytes;
`js/test/wavetable-vectors.json` holds the Python's results.

Body (after the 64-byte `SAMP` header), level-major, so the two waves a
crossfade reads are next to each other:

    offset(k, w) = W * (4096 - (4096 >> k)) + w * (2048 >> k)   (samples)
    frames       = W * 4088

A table of 64 waves is 523,264 bytes of PCM (about 0.5 MiB); the pool's
63.62 MiB holds about 120 of them.

Entry: mono, flag `ST_HZ_WAVETABLE`, name = the file name without extension and
suffix (1 to 32 UTF-8 bytes, as for a sample; `--name` gives another), in the
root folder `wavetables`, which needs the v2 catalogue (folders enabled). `get` writes the stored level
0 waves back as a 2048-sample table (`<name>.wav`), not the original file.

## 5. Core change (core 2.8.0)

Any future wavetable machine could want this, so it is in the core; the
layout and its rule are a header of their own, `wavetable.h`, which
`storage.h` and `machines.h` include.

- `wavetable.h`: `ST_HZ_WAVETABLE 2u`. Valid only when bit 0 is clear,
  `frames % 4088 == 0` and 1 <= frames / 4088 <= 64; otherwise the entry reads
  as a plain mono sample and `fsck` reports it. HZFS already preserves
  unknown flag bits (HZFS.md, "Stereo samples").
- Pool transport: COMMIT and ALIAS carry the kind in bit 9 of the slot (bit 8
  stays stereo). The AUDIO service writes `0x81` in `CHANNELS[slot]` for a
  wavetable. Old readers test `== 2`, so they see mono.
- `machines.h`: `st_machine_sample_wavetable(slot)` returns W, or 0 for
  anything else.
- MAIN: `core_pool_sample_wavetable(slot)` beside `core_pool_sample_channels`;
  the Sample Manager (1.3.1) shows `WT ` before a wavetable's name, as it
  shows `ST `.
- Python: `elekloader/sdk/syntakt/wavetable.py` holds the same constants;
  `long_samples ls` marks wavetables `wt` and `fsck` reports a wrong count.
- The tempo: alias `ST_MACHINE_TEMPO_PARAMETER` (`0xfffffffd`, machines.h) on
  an extended control 8..23. The MAIN sender puts BPM x 120 there instead of
  the Sound value; no modulation, no randomize; the SMZ parser accepts it at
  8..23 only. Tests: `test_st_parameters.c`, `test_st_smz_extensions.c`.

## 6. Import tools

- `elekloader/sdk/syntakt/wavetable.py`: the conversion (section 4), no device
  access. `long_samples put --wavetable` (implied by a `_wt` suffix) stores it
  in `wavetables`, created when absent, or in `--folder`; `get` writes the
  level 0 waves. Tests: `tests/test_st_wavetable.py` (in the regressions).
- `js/src/wavetable.ts` (exported as `wavetable`): the same conversion, byte
  for byte; `js/tools/wavetable_vectors.py` writes the vectors that
  `js/test/wavetable.test.ts` checks.
- Synfact site, Transfer: a drop into `wavetables` converts (site repository,
  after the engine).

## 7. Open, to settle during development

- (Settled in core 2.8: a hidden extended control with alias
  `ST_MACHINE_TEMPO_PARAMETER`, filled in by the MAIN sender.)
- `r`'s meaning (decision 5) and the Random shape's smoothing, against a
  Tonverk if one is at hand.
- Output level and the LEV curve, by ear against stock machines on the unit.
- (Done: the SYN pages are drawn as the Tonverk's, section 3b.)

## 8. Order of work

1. Core: flag, slot kind, accessor, `WT ` row; tests on both sides; version bump. **Done: core 2.8.0.**
2. `wavetable.py` + `long_samples` + tests; the js port + parity test. **Done.**
3. `mods/wavetable-st`: descriptor, UI, AUDIO engine; host test against a
   Python reference; cfbench cost; the type row in ADAPTING.md section 3. **Done: 0.1.0.**
4. A test bank for the user's unit; then the site's Transfer and the catalog.
