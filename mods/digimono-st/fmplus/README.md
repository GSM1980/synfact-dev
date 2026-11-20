# De-Pattern FM+ for SY MnM 0.7.0

TYPE 9/10/11 now select De-Pattern's FM+STAT, FM+PAR and FM+DYN algorithms,
replacing the previous MONO2K FM engines in place. Their names, control order,
IDs and saved values remain. Old patches intentionally sound different.
The original MONO2K FM source remains only in its historical upstream snapshot;
its generated machine cases and tables are no longer in the bank.

The source is pinned at `a0249fc55fd188242c92f377b4cbb2748b851a35` of
[dn2mk2-firmware](https://github.com/De-Pattern/dn2mk2-firmware). `provenance.json`
records original paths and hashes. `upstream/` retains the unmodified FM DSL,
its helpers and full AGPLv3 licence. The port is AGPL-3.0-or-later, combined
with the existing GPLv3/MIT parts under AGPLv3 with their notices retained.

## What runs on Syntakt

- Native 44.1 kHz, 16-frame control interval and original sine/ratio/TONE/
  envelope tables. The shipped balanced backend evaluates ramped operators
  and carrier at 1x, retaining self-FM at the original 2x. The compile-time
  `DM_FM_HQ` reference retains all 2x samples and two-pair allpass decimation.
- Integer Q29 signals with six guard bits above the original Q23 words;
  original block-end lowpass/envelope truncations are retained. Coefficients
  and gains use their explicit Q23/Q27 domains. No FPU or libgcc arithmetic.
- The 32-bit phases and differentiated/lowpassed operators follow De-Pattern;
  this is not a claim of bit identity with the original 48-bit DSP.
- A trigger restarts internal envelopes while oscillator/filter phases
  continue. Backend changes initialize the whole private voice. AMP-idle sleep
  retains the existing adapter policy; this differs from a continuously
  running original oscillator after a long silent interval.
- A causal 160-phase, 16-tap windowed-sinc converter feeds 48 kHz / 32 frames
  to the existing Syntakt declick/gain/AMP/FLTR/FX chain. Its delay is about
  seven source samples; controls/triggers can wait for the remaining part of
  the previously rendered 16-frame source block. Q17 history and Q15 taps keep
  the resampler's hot products within signed 32 bits, with two guard bits
  above the final Q15 PCM. Eight tracks have separate state.
- On ColdFire the fractional EMAC evaluates exact bounded products and the
  resampler dot product. Prealigned taps and a local doubled ring remove
  shifts and wrapping from that dot product. All accumulator/extension words
  and MACSR are restored, including nonzero incoming guard bits. Table bounds
  are asserted by the importer; integer precision and all resampler taps remain.
- Common Syntakt TUNE is applied once, then quantized to the original pitch
  table. Enum ratios use the original `(6 * knob + 3) >> 5` boundaries on both
  the sound and readout. Fractional scalar controls remain Q8; original table
  selections and decay-law knob positions use the original whole-value steps.

No core firmware change is required: retain core 2.3.2. No concurrent-voice
cap or automatic quality switching is added. Counts from emulation are
not hardware CPU percentages; final safe voice counts require the unit's
AUDIO meter under sustained notes, retriggers, modulation and effects.

For 0.6.0 to 0.6.1, the 768-block linked A/B suite retained identical PCM
and complete voice state. The 0.7.0 comparison below allows its intentional
DSP change; the 2,300-block full adapter suite still compares exactly against
the corresponding host backend. These are measured instruction counts for a
32-frame render callback, including native gain/mix and excluding update,
FLTR/AMP/FX, scheduler and cache stalls; they are not cycle budgets or CPU
percentages. See the delivery report for the exact bank hashes and maxima.

### 0.7.0: a fixed balanced compromise

Hardware feedback showed that one STAT, PAR and DYN plus five mono ONE SHOTs
still reached 99% AUDIO CPU with 0.6.1. This release intentionally changes the
DSP evaluation rate. It is not bit exact with the full-quality reference.

Ramped operators evaluate 16 sine samples instead of 32, use their mean input
slope, and keep both original phase increments/filter substeps. Carrier phase
also integrates two substeps, using both distinct self-FM samples in STAT/DYN;
only its even sample is synthesized. The original decimator is omitted, with
its DC gain retained explicitly. Envelope/parameter laws and native block
clock remain, followed by the unchanged 16-tap output converter. Strong/high
register modulation can alias more, and feedback spectra can differ. There is
no knob-dependent switch between quality modes.

In the 768-block linked comparison (32-frame render callbacks, including
native gain/mix but excluding update and FLTR/AMP/FX), the observed maxima are:

| Engine | 0.6.1 HQ | 0.7.0 balanced | Reduction |
|---|---:|---:|---:|
| FM+STAT | 14,734 | 11,298 | 23.3% |
| FM+PAR | 19,955 | 13,437 | 32.7% |
| FM+DYN | 14,548 | 11,058 | 24.0% |

Aggregate render instructions across those fixtures fall 25.4%. This is not
a hardware CPU percentage or proof that the owner's three-FM pattern fits.

A fully 1x prototype changed high-feedback STAT substantially and was rejected.
The shipped hybrid keeps that sensitive operator and its one-subsample delay
at 2x. The 27 original-DSP fixtures compare the tradeoff directly; their mean
spectral distances are diagnostics, not percentages of fidelity. A separate
48-case HQ/balanced bench checks tuning, level and low-depth spectra, and
reports high-register/feedback spectra without claiming sample identity:

```sh
python tests/test_st_fmplus_quality.py --cc "$LLVM_MINGW_BIN/x86_64-w64-mingw32-clang.exe" \
  --out out/fmplus-quality
```

It needs the reference bench's numpy/scipy environment and writes three WAVs
containing HQ, a pause, then balanced, with a shared gain. The linked cost test
is `test_st_fmplus_cost_emu.py --before BANK_0_6_1 --after BANK --stock OS
--out OUTPUT`. PCM correctness of the shipped backend is independently tested
against its portable arithmetic by the 2,300-block adapter and physical EMAC
benches. The strict source DSL sweep now explicitly tests `--backend hq`,
with its previous 60 dB/level thresholds unchanged. The retained HQ also
compares exactly against 0.6.1 over 12,288 changing-control/state blocks.

The earlier instruction tables below describe the full-quality backend.

| Engine | First port | 0.6.0 | Reduction |
|---|---:|---:|---:|
| FM+STAT | 46,383 | 16,500 | 64.4% |
| FM+PAR | 64,739 | 21,999 | 66.0% |
| FM+DYN | 46,500 | 16,564 | 64.4% |

These maxima are observed across the 768-block A/B suite's defaults, zero,
maximum and changing fractional controls, notes 24/48/60/96 and retriggers.
They do not establish a global worst-case bound or guarantee eight instances.

### 0.6.1: exact arithmetic and fewer memory accesses

The same linked A/B suite against the shipped 0.6.0 bank retains every PCM
sample and every byte of voice state, including fractional controls, pitch
changes, retriggers and buffered source blocks:

| Engine | 0.6.0 | 0.6.1 | Further reduction |
|---|---:|---:|---:|
| FM+STAT | 16,500 | 14,752 | 10.6% |
| FM+PAR | 21,999 | 19,955 | 9.3% |
| FM+DYN | 16,564 | 14,523 | 12.3% |

The port keeps operator state in locals until the end of each source block,
allows legal address-register EMAC operands, uses bounded exact Q31 products
for levels/envelopes, and stores the original sine words already scaled to
Q29 on target. The original and target sine arrays are alternatives: the
unused one is removed by the compiler. The resampler combines each MAC with
the next history load. Only the small feedback loop is unrolled four times;
compiler flags are unchanged. No quality or control-rate reduction is used.

DYN's standalone trigger previously depended on an incoming fractional EMAC
mode. It now owns an explicit guard too; the native-chain A/B remains exact.
The linked guard test covers trigger/half/render in 144 calls. A separate
physical-register EMAC model compares 768 target/split-integer blocks across
eight voices and four incoming MACSR modes, checking PCM, full voice state,
all four accumulators, extension words and MACSR. This independently covers
the EMAC arithmetic that patched Unicorn cannot generally model correctly.

```sh
python tests/test_st_fmplus_cfbench.py --cfbench "$CFBENCH" \
  --sgdk-bin "$SGDK_BIN" --out out/fmplus-cfbench
```

In an additional 16-block sustained maximum-control fixture, writes to DDR
addresses fall 31–37%. This counts emulated accesses, including stack and
native update/gain/mix; it does not model cache/writeback or physical bus
traffic. Neither metric establishes the safe concurrent voice count on hardware.

## Tables and reproducibility

At the owner's explicit request the FM table arrays are committed in
`tables.h`, with OS/input and generated-output hashes in `tables.json`.
No firmware image or executable Elektron routine is included. The source
recipe reproduces the arrays from the owner's OS 1.32B:

```sh
python mods/digimono-st/fmplus/import_tables.py "$MONOMACHINE_OS"
```

The importer uses the pinned upstream decoder, checks the exact OS hash,
compressed-block checksums and sine-builder opcode positions. The waveform
table is reconstructed by the original 48-bit recursion. Pitch increments
use the upstream integer model. Resampler coefficients are generated from
their documented mathematical formula. Building a bank needs no OS extraction.

## Validation and limits

```sh
python tests/test_st_fmplus.py --cc "$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe" --os "$MONOMACHINE_OS" --backend hq
```

For the retained HQ backend, the independent oracle executes the pinned upstream FM code and float32 DSL;
AST loading selects its complete functions, avoiding unrelated MD table
construction. The opcode semantics and FM bodies are unmodified. 120 cases
cover three engines, notes 24/48/60/69/96 and each control at 0/32/64/96/127.
Each clean case must reach 60 dB signal/error or an absolute RMS error below
2^-20. Chaotic self-feedback at 32 and above is checked by level (2.5 dB),
not sample identity. This bounded 512-sample sweep does not establish a full
keyboard/long-duration/feedback spectral match. Separately, 12,288 changing
control/retrigger/resampling blocks compare ColdFire's split multiply with
native wide arithmetic, exact PCM/state, independent voices and buffer guards.

The original-Monomachine comparison is a distinct bench:

```sh
python tools/monomachine-reference/complex_compare.py --suite fmplus \
  --os "$MONOMACHINE_OS" --reference "$MONO_REFERENCE_BUILD/synfact-mnm-reference.exe" \
  --cc "$LLVM_MINGW_BIN/x86_64-w64-mingw32-clang.exe" --out out/fmplus-original
```

It renders 27 original neutral-chain versus raw bank cases, with stereo A/B
and spectra. Those stages differ in filtering/gain, so spectral distance is
a diagnostic, not a percentage of fidelity. Large self-feedback remains
numerically sensitive. Hardware listening and complete CPU load remain owner QA.
