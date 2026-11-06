# Chorus

Synfact's chorus on the Syntakt's MAIN mixer. Each track sends through its
CHO control (the Sound's dormant Digitone chorus send, parameter 72); the
sends sum to one mono signal, the chorus renders it to a stereo wet and
returns it to the delay (DEL), the reverb (REV) and the FX bus (VOL). Seven
settings: DPTH, SPD, HPF, WDTH, DEL, REV, VOL.

Version 0.2 is V56's DSP ("chorus-fast", 2026-10-20, the last of Synfact's
chorus line) on a mono send, as on Roland's Juno: the tracks' sends ignore
their pan, and WDTH alone spreads the wet. On the unit the first port (0.1,
stereo as V56) took MAIN from 88% to 100% with one send and broke up from
the overload; its DSP was R2's to 1 LSB, the version heard clean on the
unit, so the overload was the cause.

| version | what it added |
|---|---|
| V22-V27 | the Digitone's chorus DSP ported at 48 kHz |
| chorus24 (V46/V50) | a new design: stereo wet path at 24 kHz, modulation at 6 kHz, shorter filters |
| R1 | the fractional delay's parity bug fixed; only live tracks mixed |
| R2 | the controls read and write the Kit's FX record |
| V55 | the settings stored as values in the Kit's FX record, so they survive power-off |
| V56 | POLY voices mixed in EMAC groups of four, no 12-track rebuild on sparse ramps, cached settings, precomputed widgets |
| 0.1 | V56 on core 1.2's events, controls on the core's providers |
| 0.2 | mono send, one saturation per sample, modulation once a block, kernels, state and per-block C code in MAIN SRAM |

Measured in the emulator (instructions per 32-frame block: sends, DSP and
the VOL return; the core's stub not counted): 206 idle, 3,232 with one
send, 3,634 with four, 4,714 with twelve (0.1: 322, 4,194, 5,165, 7,753);
the per-block C code in SRAM adds about 20 for its calls through `chorus_e`.
Code fetched from DDR per active block: 0.2 KB (with that code in DDR, as
chorus test d: 1.9 KB; 0.1 first build: 5.4 KB). Bank data in DDR with DEL,
REV and VOL returning: 1.6 KB (test d: 0.96 KB), so 1.9 KB of DDR in all
against test d's 2.9 KB. These are emulator counts, not CPU percentages.

## What changed from V56

- The audio runs on core 1.2's mixer events (`ev_track_send`, `ev_delay_in`,
  `ev_reverb_in`) and `ev_main_mix`, not on sites of its own
  ([ADAPTING.md](../../docs/ADAPTING.md), core 1.2).
- MAIN SRAM, which never misses in cache (`<syntakt/sram.h>`): in the window
  after LFO3's kernel, the hot state (208 bytes) and after it the per-block C
  code (`hot.c`, 1,788 bytes: sends, modulation, returns); in the window
  after LFO3's bank, the five kernels every active block runs (mix,
  decimator, delay, reconstructor, returns: 882 bytes). The block's buffers
  stay in the bank's DDR: written before they are read in each block, they
  cost less there than the code would. Each window must be all zero at init,
  else that part stays in DDR; a tick check moves the code back to DDR if an
  SRAM copy ever changes. `mod.json` links `hot.c` between `hot_begin.s` and
  `hot_end.s`; the emulator test checks that no PC-relative reference leaves
  that span. V56 had its state at `80007340`, where LFO3 now runs.
- The decimator, delay and reconstructor are rolled loops and the DSP's
  modulation is not unrolled: the same arithmetic in the same order, in
  less code. On the unit, MAIN pays for code and data fetched from DDR in
  every audio block more than for instructions: the first port (all in DDR,
  unrolled) took MAIN from 88% to 100% with one send, and its sound broke up
  from the overload. Per block it touched 5.4 KB of code and 2.1 KB of data
  in DDR; now 0.2 KB and 1.6 KB, for 4% more instructions.
- The settings' codec runs on the core's FX record notifications (queries
  28/29), which LFO3 shares.
- The modulation shape (32 entries) and the HPF coefficients are the
  Digitone's. This repository holds a recipe (`digitone.json`), and
  `tools/import_digitone.py` rebuilds them from your own Digitone OS 1.44 file,
  as for SY FM DTONE. The kernels are ours: `tools/gen_kernels.py` writes
  `kernels.s` from `design.json`.

- The send is mono: one EMAC pass sums every track into four accumulators
  and saturates once per sample (V56 saturated track by track to keep the
  Digitone's order, which with twelve sends was half its cost). The
  decimator, the HPF and the delay ring run once; two taps of the ring make
  the stereo wet.
- The modulation runs once a block (V56: four times): each tap moves
  linearly over the block, and the pole's step is c^4 with its input weighted
  1+c+c^2+c^3, so its gain and settling match V56's.

`tests/test_st_chorus_emu.py` checks the core's stubs, each kernel against a
numeric oracle, and the whole chain against V56 (compiled from its pinned
sources) fed the same signal on both sides: level 0.00 dB, correlation 1.000,
delay trajectory within 0.11 samples, and both sides equal at WDTH 0. It
also checks that `hot.c`'s span keeps every PC-relative reference inside
itself (and, in a full bank, that LFO3's kernel still fits its window), and
with `--reference <bank>`, that the bank's buses match another bank's bit for
bit over 1,200 blocks of changing settings, sends, mutes and silences (how
the per-block code's move to SRAM was checked against chorus test d).

## The controls

As on V56:

- **REVERB** gains a second page: DPTH, SPD, HPF, WDTH on A-D, DEL, REV,
  VOL on F-H (E is empty). DPTH, SPD, DEL and REV show two decimals; HPF
  and WDTH draw V56's widgets, DEL and REV the native send knobs.
- **AMP** gets CHO, the track's chorus send, on knob E, with V56's cone
  widget; DEL, REV and PAN move to F-H, and VOL moves to knob E of AMP's
  second page.

How they reach the firmware (`ui.c`, `sites.s`, `install.c`):

- Labels, ranges, formats, cached records and the dials answer core 1.2's
  parameter provider queries, including the CHO send (parameter 72, which
  has no stock labels); page 25's layout is `ST_NATIVE_PAGE_LAYOUT`.
- The settings take core 1.2's module IDs after LFO3's (engine
  `0x403..0x409`, UI `0x4ea..0x4f0`); V56's Digitone IDs now belong to
  machine type 66. The values are stored by FX record word, so no project
  holds an ID.
- Five entries only the chorus uses are hooked at init, all or none, after
  checking their stock bytes: the global value provider (`4000d870`,
  `4000d94a`, `4000da32`), REVERB's draw (`40042ce0`) and AMP's send drawing
  (`40041e7c`). They are core candidates (a global parameter provider and a
  view draw override).
- The core's lookups only run with machines in the bank, so the controls
  need a bank with machines, as LFO3's do; the audio does not.

`tests/test_st_chorus_ui_emu.py` runs all of it on the real core image.

## Build

```bash
python mods/chorus-st/tools/import_digitone.py <Digitone OS 1.44 .syx>
```

Then add `--module mods/chorus-st` to the bank, before
`--module mods/modulation-skip-st` ([SYNTAKT-BUILD.md](../../docs/SYNTAKT-BUILD.md)).
It needs core 1.2.
