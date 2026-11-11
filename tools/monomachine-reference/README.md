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

## Next rounds

First establish how much of the measured gain/colour comes from the neutral
reference chain. A comparable raw reference tap or independent chain
characterisation is needed before changing oscillator gain. Then add SAW/PULSE,
NOISE, ENS/VO, SID and FM+ parameter sweeps, one engine and characteristic at a
time. Preserve named fixtures and measured regressions rather than tuning a
single patch. POLY SIN and CHORDS have no direct original-machine equivalent.
