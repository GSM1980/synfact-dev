# Monomachine reference comparator

An offline calibration bench for SY MONOMACHINE. Its first suite measures GND-SIN
against the original Monomachine DSP, loaded from the owner's OS 1.32B. It uses
[Monomodule](https://github.com/shnolk/monomodule), pinned in `provenance.json`.
No firmware, reference recordings, extracted images or generated JIT code is
included in this directory. Nothing sends MIDI/USB or accesses an instrument.

## What the first comparison means

- **Reference:** the original DSP plus its original track chain. AMP is held
  open, filters opened with zero resonance/envelope, EQ neutral, SRR and delay
  send off. Level is 100, VOL/PAN 64, master tuning 440 Hz, tempo 120. The
  reference's host/parameter timing is Monomodule's model, not the original
  main processor. Physical Monomachine matching is a separate validation.
- **Our side:** actual bank oscillator source, rendered on the host at 48 kHz,
  before native Syntakt declick/gain/FLTR/AMP/FX. The existing ColdFire port
  differential checks that the bank matches this host generator.
- Absolute RMS/peak differences are reported, but are **not a calibrated
  oscillator gain error**: they include unmatched gain/pan/filter stages.
  Frequency and steady harmonic ratios are estimated separately at each native
  sample rate. Harmonic fits cover H2..H8: JSON `thd_db` is their summed ratio
  to H1, not a full-band THD+N figure; residual noise/other components are
  reported separately. No global gain correction is applied to measurements.
- A/B listening files alone are RMS matched and converted to a common rate;
  source WAVs remain at their original gain and rate. Phase-sensitive errors
  are not used as a single global similarity score.

The suite includes eight notes (MIDI 24, 36, 48, 60, 69, 72, 84, 96), four fine
tuning values and a same-pitch retrigger sequence. Original TUNE is interpreted
as `(value-64)/32` semitones; our common TUNE is converted to the same physical
pitch rather than copying the knob value. Scheduled retriggers at 0.32/0.64 s
are exact block boundaries at both rates; the original host can latch them at
its 48-sample frame boundary. The report does not claim sample-exact trigger
timing between the two platforms. The retrigger case repeats note-on events
with sustained AMP and no intervening note-off; it does not cover recovery
after a silent voice, different gate lengths or native Syntakt declick.

## Set up

The research tools use Python 3.12, NumPy/SciPy/Matplotlib, CMake/Ninja, Git and a
64-bit C++17 compiler. This is independent of the Syntakt firmware toolchain.
The tested Windows compiler is llvm-mingw. Set these paths in your shell only:

- `MONOMACHINE_OS`: the owner's `Elektron_SFX6-60_OS1.32B.syx`.
- `MONOMODULE_SRC`: a separate clone of Monomodule at the pinned commit.
- `MONO_REFERENCE_BUILD`: a scratch build directory outside tracked source.
- `LLVM_MINGW_BIN`: the host compiler directory.

In PowerShell, from the repository root:

```powershell
python -m venv .venv
.venv/Scripts/python -m pip install -r tools/monomachine-reference/requirements.txt
$env:PATH="$PWD/.venv/Scripts;$env:LLVM_MINGW_BIN;$env:PATH"
git clone https://github.com/shnolk/monomodule.git $env:MONOMODULE_SRC
git -C $env:MONOMODULE_SRC checkout 23e541021b92d1a5978f36c0a552ac443376df1a
python tools/monomachine-reference/build.py --source $env:MONOMODULE_SRC `
  --build $env:MONO_REFERENCE_BUILD `
  --cc "$env:LLVM_MINGW_BIN/clang.exe" --cxx "$env:LLVM_MINGW_BIN/clang++.exe"
```

`build.py` refuses a different Monomodule commit. CMake fetches its pinned
DSP56300/AsmJit dependencies and applies Monomodule's own patches. Our MinGW
compatibility adds a missing `<algorithm>` include, Win10 API declarations and
clang SEH support. `mingw.patch` selects the Win64 calling convention and shadow
space on MinGW as on MSVC; it does not change DSP opcode semantics. Without that
patch the JIT chooses SysV argument registers and crashes on Windows. The
external checkout is modified only by these recorded patches. On other
platforms the upstream platform configuration applies.

For an already built reference, `build.py --dsp-source PATH` can reuse its
Monomodule-patched DSP56300 checkout without the FetchContent update/reset;
the helper checks its pinned revision. The caller supplies the patched tree,
as with upstream's `MNM_DSP56300_DIR` option. Our ABI patch is idempotent.

The reference driver links against AGPL-3.0-only Monomodule and is marked
AGPL-3.0-only. Its emulator dependencies retain their own notices/licences.
The host generator adapter and Python analysis are GPL-3.0-only. Source OS files
and derived data stay local and must never be committed.

## Run

```powershell
$env:OPENBLAS_NUM_THREADS='1'
$env:OMP_NUM_THREADS='1'
python tools/monomachine-reference/compare.py --os $env:MONOMACHINE_OS `
  --reference "$env:MONO_REFERENCE_BUILD/synfact-mnm-reference.exe" `
  --cc "$env:LLVM_MINGW_BIN/x86_64-w64-mingw32-clang.exe" `
  --out out/monomachine-sin-baseline
python tests/test_monomachine_compare.py
python tests/test_units.py
```

Use a fresh `--out` directory for each run. Open `report.html` for graphs and
per-case A/B players, or `report.md`/`results.json` for measurements, settings,
input/source/executable hashes, fresh-instance determinism and retrigger phase
observations. WAVs, images, DLLs and reports are ignored under `out/`.

The numerical tests verify frequency/level/DC/harmonics against generated
signals, equivalent measurement across sample rates, phase-continuous versus
reset oscillators, WAV scaling and rejection of silence. They need no OS file.

## Complex-engine suite

`complex_compare.py` adds 88 explicit fixtures for SAW, PULSE, ENS, VO,
SID-6581, FM+STAT, FM+PAR and FM+DYN. The bank/OS machine IDs and the seven
raw SYN values are recorded in `complex_cases.py`; TUNE is matched in physical
pitch. No control remapping hides a difference between the two machines.

```powershell
python tools/monomachine-reference/complex_compare.py --os $env:MONOMACHINE_OS `
  --reference "$env:MONO_REFERENCE_BUILD/synfact-mnm-reference.exe" `
  --cc "$env:LLVM_MINGW_BIN/x86_64-w64-mingw32-clang.exe" `
  --out out/monomachine-complex
python tests/test_monomachine_complex.py
```

The output includes per-case stereo A/B, unmodified reference/raw WAVs,
overview/family plots, early and steady-window metrics, and an HTML report.
SAW/PULSE/VO use Digi Mono; ENS/SID/FM+ use the current MONO2K port. ENS raw
captures retain both channels; the other seven families use mono generators.
Their native source hashes and the original OS/
reference executable hashes are retained. No firmware changes in this suite.

- Timbre uses normalized energy in 1/12-octave bands (20..20480 Hz), with a
  square-root Jensen-Shannon distance between 0 and 1. It is not a percentage
  of fidelity or a direct error in the oscillator: the reference still includes
  its neutral track chain. Welch PSD uses up to 32768 samples per segment.
- Channel **powers** are averaged for the spectral comparison, so antiphase
  stereo does not disappear. Correlation and side/mid ratio are separate.
- Gain response compares each control case's change against its own engine's
  base case. A constant gain difference between chains cancels; frequency-
  dependent filtering, nonlinear behaviour and modulation still affect it.
- The steady window starts at 0.25 s and ends 0.1 s before the clip ends.
  Early windows at 0.02..0.12, 0.2..0.35, 0.7..0.9 and 1.4..1.8 s describe
  transitions. Fast events before 20 ms are not independently scored.
- A -90 dBFS AC threshold suppresses unreliable quiet-window spectra. A case
  can have an audible consonant and a silent steady window. A/B normalization
  then uses full-clip AC RMS for both sounds, preserving the transient rather
  than amplifying its tail or dropping it. Source WAVs are never normalized.
- Most cases use two seconds at MIDI 48 (VO at MIDI 60); ENS chorus uses eight
  seconds. This is not a full keyboard/parameter-grid validation. No external
  LFOs, parameter locks, note-off, portamento or intertrack PRCH are tested.

To change analysis without re-rendering the DSP, pass `--captures OLD_FOLDER`
and a fresh `--out`. Reuse checks the OS and reference hashes, original/native
engine source hashes, every fixture's controls/events/duration and each WAV's
hash. Only the analysis may change. A manifest hash records the reused input.

The metric tests cover gain/phase invariance, cross-rate tone comparison,
added harmonics, antiphase stereo, DC/silence rejection, relative gain response,
preservation of an original short transient, raw stereo-channel capture and
fixture identities/timing.

The ENS 0.3 round uses the same 13 fixtures at MIDI 48 as its 0.2 baseline.
All spectral distances improve, with PCH2=96 falling 0.604 to 0.047 and
WAVE=127/PW=32 falling 0.749 to 0.160. Chorus width 127 gives L/R correlation
0.782 against reference 0.785. All 75 non-ENS raw WAV hashes remain identical.
These limited cuts do not validate the entire keyboard or every parameter
combination; WAVE/PW colour and relative gain response still differ.

## Further rounds

The ENS control-law round adds `--suite ens` (149 fixtures at MIDI
36/48/60/72/84) and `--suite ens-shape` (28 additional cuts, including 22
independent response checks). Each note has its own gain anchor. The original
driver's optional `--dist` and comparator's `--reference-dist` default to 64;
48 provides more internal headroom. All other chain controls remain recorded.
Capture reuse also requires identical chain settings and fixture sets.

```powershell
python tools/monomachine-reference/complex_compare.py --suite ens --reference-dist 48 `
  --os $env:MONOMACHINE_OS --reference "$env:MONO_REFERENCE_BUILD/synfact-mnm-reference.exe" `
  --cc "$env:LLVM_MINGW_BIN/x86_64-w64-mingw32-clang.exe" --out out/ens-grid
python tools/monomachine-reference/complex_compare.py --suite ens-shape --reference-dist 48 `
  --os $env:MONOMACHINE_OS --reference "$env:MONO_REFERENCE_BUILD/synfact-mnm-reference.exe" `
  --cc "$env:LLVM_MINGW_BIN/x86_64-w64-mingw32-clang.exe" --out out/ens-shape
python tools/monomachine-reference/ens_model.py out/ens-grid `
  --validation-captures out/ens-shape --out out/ens-model
```

`ens_model.py` fits 12 harmonics at each capture's native rate, using verified
original WAVs only. Same-note, same-harmonic ratios cancel a shared linear track
response. PW uses the odd harmonics of the half-period pulse as its anchor;
WAVE uses the same PW and the pulse-end saw blend. It records assumptions,
input/tool/WAV hashes, fit errors and independent validation. Training settings
are excluded from the validation even when rendered again. Magnitudes infer
the smaller duty of a complementary pair; the existing monotonic PW direction
is retained. Very narrow pulses
are less well conditioned, and this method does not infer absolute gain.

The measured laws are duty `0.01 + 0.98*PW/128` and saw weights `1-.30*x`,
`1-1.70*x`, `x=WAVE/127`. They replace MONO2K's zero-width endpoint and .63
pulse weight in Digi Mono 0.3.1. Mean harmonic-response RMS error on the 22
validation cuts falls 0.943 to 0.006 dB. The uncorrected raw spectral diagnostic
can move in either direction: the original track chain and raw OSC remain
different stages. No compensating filter or global gain is added to the engine.

First establish how much of the measured gain/colour comes from the neutral
reference chain. A comparable raw reference tap or independent chain
characterisation is needed before changing oscillator gain. Extend the complex
suite across pitches, denser control sweeps, faster transients, GND-NOIS and
realistic gates/locks, one engine and characteristic at a time. Preserve named
fixtures and measured regressions rather than tuning a single patch. POLY SIN
and CHORDS have no direct original-machine equivalent.
