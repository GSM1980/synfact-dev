[Synfact docs](README.md) › Tests

# Tests

Each suite takes its input files from environment variables or arguments. A
test whose input files are not given is skipped, not passed: say which ran.
Firmware files never go in this repository.

## Syntakt

The Syntakt's suites (`tests/test_st_*`, `tests/test_core_st_*`,
`tests/test_syntakt_secondary.py`) and the regressions that run them all on a
build folder are in [SYNTAKT-BUILD.md](SYNTAKT-BUILD.md): the commands, the
toolchain they need, and the emulator checks.

```bash
python tests/test_units.py        # needs nothing; always run it
python tests/test_st_fm_recipe.py --digitone Digitone_and_Digitone_Keys_OS1.44.syx   # FM TONE's recipe
```

## Digi Mono

The standalone Digi Mono port test runs all eight types through the real
adapter/DSP, checking PCM/state against the engine, UI labels and chord names,
TYPE transport, backend re-entry, independent tracks, pitch and AMP sleep.
Only firmware service boundaries are doubles; it does not boot a Syntakt:

```bash
python tests/test_st_digimono.py --cc "$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe"
```

Its upstream source hashes are also verified. No firmware input is needed.
The runner also compiles `test_st_digimono_pages.c` with the real core registry,
page resolver and Digi Mono UI. It checks all eight engine types, repeated TYPE
switches, two views, every dial/edit ID and page-action layout, TUNE and LEVEL.
This catches the old core's frozen SAW layout, which a descriptor-only UI test
missed: the hidden E position must reappear for every seven-parameter engine.

The linked ColdFire test additionally needs Syntakt OS 1.41, a bank with its
sibling `.work/audio.elf`, 64-bit native clang, and the EMAC-capable Unicorn
from [SETUP.md](SETUP.md) (`LIBUNICORN_PATH`). It runs real native update and
gain calls, compares the oscillator PCM to the host engine, checks callee-save
registers and stack balance, and reports instruction counts (not CPU cycles):

```bash
python tests/test_st_digimono_emu.py --bank out/digimono/synfact-machines.smz4 \
  --stock "$SYNTAKT_STOCK" --host-cc "$LLVM_MINGW_BIN/x86_64-w64-mingw32-clang.exe"
```

This is an offline fixture, without a full boot, scheduler or FLTR/AMP/FX pass.

## The engine

The engine (`elekloader/`, `js/`) comes from elekloader and still knows every
device it supported. Its own suites need other devices' stock files, which this
repository does not use; they stay so the engine can be compared with
elekloader's:

```bash
ELEKLOADER_STOCK=Digitakt_OS1.53.syx ELEKLOADER_MODS=path/to/mods python tests/test_link.py
ELEKLOADER_RELEASES=folder/of/stock/files python tests/test_releases.py   # every known release, and the cores here
ELEKLOADER_STOCK=... ELEKLOADER_BUNDLE=bundle.elemod ELEKLOADER_CTOOL_SYX=its-build.syx python tests/test_patcher.py
ELEKLOADER_STOCK=... ELEKLOADER_STOCK_154=... ELEKLOADER_OT_SYX=... ELEKLOADER_MODS=... python tests/test_gui.py   # the window, hidden (Tk)
```

A change to the engine belongs in elekloader first: propose it there, then
bring it here.

The TypeScript engine has its own tests, which need no firmware, and a
parity run that compares it with the Python on your own files
([js/README.md](../js/README.md)):

```bash
cd js && node --test "test/*.test.ts"
```

The tools that prepare the browser's SMZ catalog have theirs:

```bash
python js/test/test_smz_merge.py
python js/test/test_smz_approve.py
```
