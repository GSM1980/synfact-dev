[elekloader docs](README.md) › Syntakt builds (core-st, SMZ banks)

# Syntakt builds (core-st, SMZ banks)

The exact toolchain, flags and commands used for the Syntakt OS 1.41 core
(`mods/core-st`) and its SMZ4 machine/module banks. Every command below is run
from the repository root in Git Bash, with the environment variables set as
[SETUP.md](SETUP.md) section 2 says. If a step fails, fix the environment;
do not change flags or the builders to make it pass.

## Toolchain

| What | Where / value |
|---|---|
| Cross compiler | `$SGDK_BIN`: the folder with SGDK 2.11's m68k toolchain (GCC 13.2.0, `m68k-elf`) |
| Executable names | `gcc`, `as`, `ld`, `objcopy`, `objdump`, `nm`. Unprefixed in the SGDK 2.11 build used for testing; the engine's default prefix is `m68k-elf-` |
| Python | `$PY` (3.9 or newer). `elekloader` is imported from the repository (run from its root) |
| Stock OS | `$SYNTAKT_STOCK`: your Syntakt OS 1.41 `.syx` from Elektron |
| Elektron firmware tool | `$SYNTAKT_EFT`: the `elektron-firmware-tool` executable |
| Host compiler for regressions | `$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe` (llvm-mingw, UCRT x86_64) |
| Digitone OS (optional) | `$DIGITONE_OS`: your Digitone OS 1.44 `.syx`, for FM TONE and the chorus |

Every build needs these settings. Unprefixed binaries need the first two;
prefixed binaries need neither:

```bash
export PATH="$SGDK_BIN:$PATH"
export ELEKLOADER_CROSS=''                  # unprefixed binaries only
export GCC_EXEC_PREFIX="$SGDK_BIN/"         # unprefixed binaries only
```

- `ELEKLOADER_CROSS=''` makes the builders call `gcc`, `as`, `ld`… without
  a prefix. Without it: `m68k-elf-gcc is not installed`.
- `GCC_EXEC_PREFIX` lets `gcc` find its own `cc1`/`as` subprograms.

## Flags (do not change them per build)

Source of truth: `elekloader/devices/syntakt.py`, `toolchain`.

| Input | Flags |
|---|---|
| C | `-mcpu=54415 -mtune=cfv4 -O3 --param=max-completely-peel-times=4 -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie -fomit-frame-pointer -Wall` |
| C in SMZ banks | the above **plus `-fno-jump-tables`** (added by `elekloader/sdk/syntakt/smz.py`: no absolute jump tables in relocatable bank code), `-I elekloader/sdk/include`, `-I <mod dir>`, `-MMD` |
| Assembler | `-mcpu=54415` |
| SMZ link | `ld --emit-relocs` with a generated `main.ld` (one `.image` section at 0), then `objcopy -O binary -j .image`; the builder turns the `R_68K_32` relocations into the bank's relocation list |

- CPU: MCF54415 (ColdFire V4, ISA_C + EMAC + hardware divide, **no FPU**).
  With these flags GCC defines `__mcfv4__`, `__mcfisac__`, `__mcfhwdiv__`,
  not `__mcffpu__`. Float code would need library helpers that a
  freestanding build does not have; use integer/fixed point.
- `-O3` is the agreed speed profile; `max-completely-peel-times=4` stops
  known-count loops from becoming long chains of absolute loads/branches.
- The bank builder audits every executable section and the linked image
  (`audit`/`audit_functions` in `elekloader/sdk/syntakt/audio.py`): only
  instruction forms the MCF54415 accepts.
- Rationale and review: [mods/core-st/COMPILER-AUDIT.md](../mods/core-st/COMPILER-AUDIT.md).

## Commands

Every output goes to a **new** folder under `out/` (builders refuse an
existing output; earlier deliveries are never overwritten). `out/` is not
committed: no firmware or derived binaries in Git.

### Core SYX (`mods/core-st`)

Bump `"version"` in `mods/core-st/mod.json` first (it names the files).

```bash
"$PY" -m elekloader.sdk.syntakt --stock "$SYNTAKT_STOCK" \
    --eft "$SYNTAKT_EFT" --system-info \
    --out out/core-st-X.Y.Z-name
```

It must print `VERIFIED …syx`, `MAIN RAM: … FREE: N bytes` (the 64 KiB core
resident; N must stay ≥ 0) and `Bootloader/bootstrap/updater sections and
destinations unchanged.` Check `verification.json` →
`protected_sections` 1, 2, 4, 5, 6, 8 all `"stock": true` (only sections 3
MAIN and 7 AUDIO change).

### Lint report (needed by the regressions)

```bash
"$PY" -m elekloader.lint mods/core-st/out/core-X.Y.Z.elemod \
    --stock "$SYNTAKT_STOCK" --json > out/core-st-X.Y.Z-name/lint.json
```

### SMZ4 bank (machines + modules)

FM TONE needs its Digitone parts first (once per checkout, or after a change
to `mods/fm-tone-st/digitone.json`; [its README](../mods/fm-tone-st/README.md)):

```bash
"$PY" mods/fm-tone-st/tools/import_digitone.py "$DIGITONE_OS"
```

```bash
"$PY" -m elekloader.sdk.syntakt.smz mods/samplers-st mods/fm-tone-st \
    --module mods/recorder-st --module mods/lfo3-st --module mods/arp-st --module mods/poly-st --module mods/system-info-smz-st --module mods/sample-manager-st \
    --module mods/modulation-skip-st \
    --out out/core-st-X.Y.Z-name/synfact-machines.smz4
```

- **Module order matters**: `lfo3-st` must come **before** `poly-st` (both
  use the core's native LFO operation; LFO3 is non-consuming and runs first).
- Modulation skip ([mods/modulation-skip-st/README.md](../mods/modulation-skip-st/README.md))
  goes in every bank, last; it is not a choice (`"always": true` in
  `js/config/syntakt-normal.json`). The regressions run its host test; with
  a bank built without it as the reference, the full checks are:
  `"$PY" tests/test_st_modulation_skip.py --cc "$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe" --bank <bank> --reference
  <normal bank> --stock "$SYNTAKT_STOCK" --core <main.bin>` and, with the Unicorn
  environment below, `tests/test_st_modulation_skip_emu.py --bank <bank>`
  (that one needs the owner's fixtures in `SYNTAKT_WORK`; without them it is skipped).
- `ValueError: machine sources changed during compilation`: a source file
  changed while building (another agent editing). Build again.
- Editing any included header (for example `elekloader/sdk/include/syntakt/host.h`)
  changes the recorded source hashes: rebuild the bank before the regressions.
- Validate the bank with the core's parser built by the regressions:
  `out/core-st-X.Y.Z-name/smz-parser.exe <bank>.smz4` → `PASS: …`.

### Regressions (run once per delivery)

The folder must contain the SYX build, `lint.json` and `synfact-machines.smz4`.

```bash
"$PY" tests/test_st_common_regressions.py out/core-st-X.Y.Z-name \
    --cc "$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe" \
    --previous-build out/<previous core folder> \
    --unicorn-python "$PY"
# add  --synfact "$SYNFACT_SRC"  when you have the Synfact source tree
```

`--unicorn-python` runs the linked core's safe-boot record writes in Unicorn
(`tests/test_st_safe_boot_emu.py`; see SETUP.md for Unicorn). Expected last line: `ALL PASS; 0 optional comparisons skipped; no device access`.
A comparison that needs an input you did not give is printed as skipped; it is
not a pass. `SYNFACT_SRC`, when you use it, is read only.

### Generated LFO3 lock sources

`mods/lfo3-st/locks/*` are generated from the pinned Synfact sources (read
only) by `"$PY" -B mods/lfo3-st/gen_locks.py`, which needs `SYNFACT_SRC`;
provenance hashes are in `mods/lfo3-st/locks/PROVENANCE.txt`. Do not edit the generated files.
The generator applies the local lazy-census optimization to the pinned pattern
setter; regeneration preserves it without modifying the Synfact input.
It also inserts publication-pointer invalidation at the owner metadata mutators
in `locks/owners.c`; do not remove these guards when changing that backend.

## The device (user only)

Agents never send MIDI/USB, install firmware or write the instrument. The
user sends the SYX with FUNC+POWER and writes the bank with:

```bash
"$PY" -m elekloader.sdk.syntakt.mods put out/core-st-X.Y.Z-name/synfact-machines.smz4 --yes-write-device
```

Recovery after a bank that hangs at boot: FUNC+POWER with
`out/core-st-0.12.9-recovery-no-bank/Syntakt_OS1.41_Elekloader_Core0.12.9_RECOVERY_NO_BANK.syx`
(a core that never loads `/mods`), write a good bank, then install the normal
core SYX again.

## Emulator checks

The emulator tests run in Python with `unicorn` (see SETUP.md, "Unicorn").
They read the same stock file and build folders as the commands above. Some
also need fixtures that the repository does not include (`SYNTAKT_WORK`); the
test says so and skips when it is not set.

```bash
"$PY" tests/test_st_preview_emu.py \
    --native out/core-st-0.12.17-preview-direct/main.bin --patched out/core-st-X.Y.Z-name/main.bin \
    --object mods/core-st/out/core.work/preview.s.o
```

The core's lock-preview site (0.12.18+) has an in-repository differential
that runs the real `40039160` of two MAIN images with the same interpreter.
Test against the stock MAIN **and** the installed core's image
(`out/core-st-<ver>/main.bin`): the core patches stock routines, so code
that checks or patches stock bytes must be verified against both.

The LFO3 native-lock setter differential runs before/after bank code against
both stock MAIN and the delivered core. It requires the banks' sibling `.work/`
directories with `main.elf`; its model/mutex/sparse-backend doubles are explicit.
These need the owner's earlier build folders (`out/lfo3-lock-edit-20261028/`
and so on); a new build can use its own folders in the same places:

```bash
"$PY" -B tests/test_st_lfo3_setter_emu.py \
  --before out/lfo3-lock-edit-20261028/baseline/synfact-machines.smz4 \
  --after out/lfo3-lock-edit-20261028/synfact-machines.smz4 \
  --stock "$SYNTAKT_STOCK" \
  --core out/lfo3-lock-edit-20261028/main.bin \
  --out out/lfo3-lock-edit-20261028/setter-differential.json
```

The publication differential executes both banks' real shadow/owner backends,
including owner lifetime invalidation. Native allocation/mutex/copy/descriptor
boundaries are doubles; it also reruns 584 setter cases and a broken-invalidator
negative control. It does not simulate scheduler contention or a full boot.

```bash
"$PY" -B tests/test_st_lfo3_publication_emu.py \
  --before out/lfo3-lock-edit-20261028/synfact-machines.smz4 \
  --after out/lfo3-publication-20261028-final/synfact-machines.smz4 \
  --stock "$SYNTAKT_STOCK" \
  --core out/lfo3-publication-20261028-final/main.bin \
  --out out/lfo3-publication-20261028-final/publication-differential.json
```

The FM audio differential needs the original Digitone AUDIO section as a
read-only, SHA-pinned test input. It executes x1 and the retained x2 path in
emulator memory without changing the delivered bank. It does not exercise
Syntakt AMP/filter/FX, the POLY allocator or a full boot. The section is an
extract of the Digitone OS (`DIGITONE_SECTIONS`, not included)
(`DIGITONE_SECTIONS`, owner's input):

```bash
"$PY" tests/test_st_fm_audio.py \
  --bank out/fm-fb-20261028/synfact-machines.smz4 \
  --stock "$SYNTAKT_STOCK" \
  --digitone-audio "$DIGITONE_SECTIONS/section_7_blob.raw" \
  --out out/fm-clicks-20261028/audio-regression.json
```

To compare first-note FM plus the original native AMP routines, with fixed-gain
24-bit WAV exports, also supply the pinned Digitone MAIN section. Keep the same
environment:

```bash
"$PY" tests/test_st_fm_onset.py \
  --bank out/fm-fb-20261028/synfact-machines.smz4 \
  --stock "$SYNTAKT_STOCK" \
  --digitone-audio "$DIGITONE_SECTIONS/section_7_blob.raw" \
  --digitone-main "$DIGITONE_SECTIONS/section_3_MAIN_OS.bin" \
  --out out/fm-onset-20261028-comparison
```

The envelope starts in an explicit released zero-gain fixture; filters/FX,
mute/selection, polyphony and complete device scheduling are not modeled.
These files are offline comparisons, not physical recordings of either unit.

For factory AMP defaults (including native ST ATK 0 versus DN ATK 8), use the
same arguments and replace `tests/test_st_fm_onset.py` with
`tests/test_st_fm_factory.py`, and `--out` with
`out/fm-factory-20261028/results.json`. This reads the pinned MAIN descriptor
defaults and measures the first sample's step from silence at four pitches.
Add `--wav-dir out/fm-factory-ab-20261028` to export the native-default A/B
(Syntakt → Digitone twice), individual 48 kHz/24-bit WAVs and unprocessed Q31.
WAVs use a common gain of 0.2 and a disclosed final 10ms file fade; the onset
is untouched and checked. This is FM + AMP only, not a final-output oracle.
To change only Syntakt AMP ATK to 8 in that listening comparison, add
`--syntakt-attack 8` and use a separate output directory, for example
`--out out/fm-atk8-ab-20261029/results.json --wav-dir out/fm-atk8-ab-20261029`.
Digitone retains its native defaults; the other Syntakt AMP defaults also stay
unchanged. Equal attack values do not imply identical complete envelopes.
Use `--match-digitone-amp` instead to export Syntakt with ADSR mode,
ATK 8/DEC 32/SUS 96/REL 24, HOLD/DELAY 0 and RESET ON. Digitone is unchanged.
For example use `--out out/fm-matched-adsr-ab-20261029/results.json` and
`--wav-dir out/fm-matched-adsr-ab-20261029`. A constant-input native AMP check
also verifies the two envelopes sample for sample before note-off after
aligning routine onset. That alignment is not a device latency measurement.

For HARM/DTUN/FDBK sweeps use `tests/test_st_fm_sweep.py` with the same firmware
arguments, `--out out/fm-sweep-20261029 --listen`. Defaults cover all 8 algorithms,
notes 48/60/84, INIT/modulated fixtures and 1,104 static cases of 64 blocks.
Listening sweeps use matched AMP ADSR 8/32/96/24. Only emulator data requests x2.

For the FM 0.2 feedback guard, run `tests/test_st_fm_feedback_guard.py` with
`--before <old.smz4> --after <new.smz4>`, the same three firmware arguments,
and `--out <comparison.json>`. The 72-case test includes 500ms C4 notes;
`--short` uses 64 blocks per case for a second linked bank. Instruction counts
are per-voice update/render after installing a code hook and flushing its JIT
cache, not device cycles or CPU percentages. The shipped ECO x1 stays unchanged.


Core 0.13+: include `--module mods/sample-manager-st` in the complete SMZ bank.
The optional module supplies Sample Manager UI; HZFS/pool/preview infrastructure
stays in core. Install the matching core 0.13 SYX before enabling v2 folders.
Never enable folders solely because a legacy `v6`/`v7` transfer profile connects.

Core 0.13.1 combines folders and ARP with host API 11. In complete banks,
LFO3 precedes ARP, which precedes POLY; Sample Manager may follow System info.

Core 1.2 adds the mixer events and the FX record codec notifications (host
API 13); LFO3 1.1 needs it. The optional chorus takes its Digitone tables from
the owner's OS file once per checkout, then goes in the bank before
modulation skip ([its README](../mods/chorus-st/README.md)):

```bash
"$PY" mods/chorus-st/tools/import_digitone.py "$DIGITONE_OS"
```

```bash
"$PY" -m elekloader.sdk.syntakt.smz mods/samplers-st mods/fm-tone-st \
    --module mods/recorder-st --module mods/lfo3-st --module mods/arp-st --module mods/poly-st \
    --module mods/system-info-smz-st --module mods/sample-manager-st --module mods/chorus-st \
    --module mods/modulation-skip-st --out out/core-st-X.Y.Z-name/synfact-machines.smz4
```

The core's three mixer stubs against the stock instructions, and the chorus
against Synfact's V56 compiled from its pinned sources. V56 is a build in the
`SYNTAKT_WORK` folder, which the repository does not include; without it the test is skipped:

```bash
"$PY" -B tests/test_st_chorus_emu.py \
    --bank out/core-st-X.Y.Z-name/synfact-machines.smz4 \
    --digitone "$DIGITONE_OS"
```

Add `--reference <other bank>` when a change must not alter the chorus's
sound (code moved, not arithmetic): its buses must equal that bank's bit for
bit (test E). Both banks need their sibling `.work/main.elf`.

The chorus's controls on the delivered core image (labels, formats, values,
codec, page 25, REVERB's pages, AMP's branch, the dials), same environment:

```bash
"$PY" -B tests/test_st_chorus_ui_emu.py \
    --core out/core-st-X.Y.Z-name/main.bin --bank out/core-st-X.Y.Z-name/synfact-machines.smz4
```
