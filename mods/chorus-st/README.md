# Chorus

Desktop OS 1.42 release: **1.2.1**, based on remote main `f48f3ab`.
Use core 2.2 or later and the matching bank (SMZ4 contract `0x53594606`).

Current desktop target: **Syntakt OS 1.42 / core 2.0**. Rebuild this mod in a
new bank; older hardware measurements below refer to the previous OS. See
[the port notes](../../docs/OS142-PORT.md).

Core 2.0.3 fixes the parameter modal showing `Error = value`: the selected-track
model has its own long-label lookup, which now uses the core's existing
parameter provider. All seven controls show their names (Depth, Speed,
High-pass, Width, Delay Send, Reverb Send, Mix Volume); CHO shows Chorus Send.
No Chorus DSP or parameter-storage change is needed. The OS 1.42 UI emulator
suite checks these names, native fallbacks and the existing page/value paths.

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
| 1.2 | the per-block path in hand-written assembly, the wet fused with VOL's return: about a third fewer instructions, bit for bit the same buses as 1.1 |

## 1.2: the same chorus, a third cheaper

The DSP, its coefficients, the modulation, the mono send, the ramps, the
sleep and the stored settings are 1.1's: the delay, reverb and FX-track
buses equal 1.1's **bit for bit**, checked on analog-emu's ColdFire core
(whose EMAC is the reference) over 1,200-block runs of changing settings,
sends, mutes, silences, full-scale signals and depth jumps, with both SRAM
windows free, either one occupied, or both (everything from DDR), and in
Unicorn by `tests/test_st_chorus_emu.py --reference`. What changed is how
the work is done:

- **All of the per-block path is assembly** (`hot.s`, `kernels.s`), on one
  state block (`state.inc`; `chorus.h` checks every offset). 1.1's `hot.c`
  was 1,788 bytes of `-Os` C reloading the state between products.
- **The delay** no longer recomputes each tap's index and weights from its
  position every sample (shifts, masks, a subtraction): the fraction and
  `1 - frac` step by the tap's step, and a carry out of the fraction moves
  the read pointer. The same weights on the same samples, for taps moving
  less than a sample a sample, which the modulation never reaches (a depth
  jump from 0 to full moves about 0.2); faster taps take 1.1's loop, kept
  unchanged. The ring keeps a mirror of its first 48 entries, so no read
  needs a mask. 815 instructions a block become 428.
- **The returns** start the EMAC at the bus value: one truncated product,
  one saturation (1.1's product then saturating add: the same result), four
  samples per pass with MOVEM.
- **VOL alone** (DEL and REV at 0, VOL not moving: the default) is returned
  at `ev_main_mix` by the reconstructor itself, each output straight into
  the FX bus: the 256-byte wet is neither written nor read. With nothing
  returning at all, the reconstructor does not run.
- **The mix** keeps up to four tracks in registers for the whole block and
  enters their code at the first live slot; more tracks go four at a time
  through records rebuilt only when a gain, send or mute changes (1.1 rebuilt
  its list every block).
- **The decimator and the reconstructor** compute two outputs a pass, sharing
  their loads (the decimator keeps 1.1's two passes of eight: its partial
  sums round as before). Their coefficients are immediates
  (`tools/gen_kernels.py` writes `coeffs.inc` from `design.json`).
- **The settings** are compared word by word (eight words, as 1.1) and
  decoded only on a change; the return targets and the HPF coefficient are
  computed then, not every block.
- Rare paths (gain ramps, more than one record's mix rebuild, waking, tails,
  depth jumps, the settings decode) stay in the bank's DDR.

Per 32-frame block (`tools/bench_st_chorus.py`, instructions; the core's
event stubs not counted), 1.1 -> 1.2:

| case | 1.1 | 1.2 | |
|---|---:|---:|---:|
| no send, asleep | 238 | 146 | -39% |
| one send, VOL | 3,255 | 2,184 | -33% |
| four sends, VOL | 3,654 | 2,361 | -35% |
| twelve sends, VOL | 4,722 | 3,054 | -35% |
| three sends, DEL+REV+VOL | 4,457 | 3,051 | -32% |
| twelve sends, DEL+REV+VOL | 5,658 | 3,803 | -33% |
| three sends, returns moving | 5,235 | 4,037 | -23% |
| twelve sends raised, silent (asleep) | 1,476 | 1,109 | -25% |

Distinct 16-byte lines of the bank image touched per block with one send and
VOL: code 14 -> 5, data 73 -> 55 (the wet is not touched). These are
emulator counts, not cycles or CPU percentages; the unit decides.

MAIN SRAM (`<syntakt/sram.h>`, the same two windows as 1.1, nothing new):
the first holds the state block (468 bytes) and `hot.s`'s code (1,640
bytes: sends, the per-block control, modulation, decimator, HPF, returns;
4 bytes free of 2,112), the second `kernels.s`'s span (874 bytes: mix,
delay, reconstructor; 10 free of 884). The block's buffers stay in the
bank's DDR, as 1.1's. The entries read the state's address from
`chorus_ch`, so the code runs on the SRAM state or, when its window is not
free at init, on the DDR one; the kernels reach the state in A6, and each
other through the state's kernel table. Each window must be all zero at
init, else its part stays in DDR; a tick check moves a copy back to DDR if it
ever changes.

## What changed from V56

- The audio runs on core 1.2's mixer events (`ev_track_send`, `ev_delay_in`,
  `ev_reverb_in`) and `ev_main_mix`, not on sites of its own
  ([ADAPTING.md](../../docs/ADAPTING.md), core 1.2).
- MAIN SRAM, which never misses in cache: the state and the per-block code
  (1.2 above). On the unit, MAIN pays for code and data fetched from DDR in
  every audio block more than for instructions: the first port (all in DDR,
  unrolled) took MAIN from 88% to 100% with one send, and its sound broke up
  from the overload. Per block it touched 5.4 KB of code and 2.0 KB of data
  in DDR. V56 had its state at `80007340`, where LFO3 now runs.
- The settings' codec runs on the core's FX record notifications (queries
  28/29), which LFO3 shares.
- The modulation shape (32 entries) and the HPF coefficients are the
  Digitone's. This repository holds a recipe (`digitone.json`), and
  `tools/import_digitone.py` rebuilds them from your own Digitone OS 1.44 file,
  as for SY FM DTONE. The filters are ours: `tools/gen_kernels.py` writes
  `coeffs.inc` from `design.json`.

- The send is mono: one EMAC pass sums every track into four accumulators
  and saturates once per sample (V56 saturated track by track to keep the
  Digitone's order, which with twelve sends was half its cost). The
  decimator, the HPF and the delay ring run once; two taps of the ring make
  the stereo wet.
- The modulation runs once a block (V56: four times): each tap moves
  linearly over the block, and the pole's step is c^4 with its input weighted
  1+c+c^2+c^3, so its gain and settling match V56's.

`tests/test_st_chorus_emu.py` checks the core's stubs; each kernel against a
numeric oracle (the delay also against 1.1's loop, with small and large
steps; the fused return against the wet's); the whole chain against V56
(compiled from its pinned sources) fed the same signal on both sides: level
0.00 dB, correlation 1.000, delay trajectory within 0.11 samples, and both
sides equal at WDTH 0; that both SRAM spans keep every PC-relative reference
inside themselves and fit their windows (and, in a full bank, that LFO3's
kernel still fits its window); and with `--reference <bank>`, that the
bank's buses match another bank's bit for bit over 1,200 blocks of changing
settings, sends, mutes and silences.

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

After changing `design.json`, run `python mods/chorus-st/tools/gen_kernels.py`
(it rewrites `coeffs.inc`). The cost per block, and the buses against
another bank's (both banks with their sibling `.work/` folders):

```bash
python tools/bench_st_chorus.py --bank <bank.smz4> --reference <other bank.smz4>
```
