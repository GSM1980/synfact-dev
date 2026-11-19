# De-Pattern FM+ for SY MnM 0.6.0

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

- Native 44.1 kHz, 16-frame control interval, 2x operator/carrier rendering,
  original sine/ratio/TONE/envelope tables and two-pair allpass decimation.
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
  are asserted by the importer; oversampling, taps and precision are retained.
- Common Syntakt TUNE is applied once, then quantized to the original pitch
  table. Enum ratios use the original `(6 * knob + 3) >> 5` boundaries on both
  the sound and readout. Fractional scalar controls remain Q8; original table
  selections and decay-law knob positions use the original whole-value steps.

No core firmware change is required: retain core 2.3.2. No concurrent-voice
cap or automatic reduction in quality is added. Counts from emulation are
not hardware CPU percentages; final safe voice counts require the unit's
AUDIO meter under sustained notes, retriggers, modulation and effects.

In the same 768-block linked A/B suite, optimization retains identical PCM
and complete voice state. The 2,300-block full adapter suite also compares
against the host backend. These are measured instruction counts for a
32-frame render callback, including native gain/mix and excluding update,
FLTR/AMP/FX, scheduler and cache stalls; they are not cycle budgets or CPU
percentages. See the delivery report for the exact bank hashes and maxima.

| Engine | First port | Optimized | Reduction |
|---|---:|---:|---:|
| FM+STAT | 46,383 | 16,500 | 64.4% |
| FM+PAR | 64,739 | 21,999 | 66.0% |
| FM+DYN | 46,500 | 16,564 | 64.4% |

These maxima are observed across the 768-block A/B suite's defaults, zero,
maximum and changing fractional controls, notes 24/48/60/96 and retriggers.
They do not establish a global worst-case bound or guarantee eight instances.

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
python tests/test_st_fmplus.py --cc "$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe" --os "$MONOMACHINE_OS"
```

The independent oracle executes the pinned upstream FM code and float32 DSL;
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
