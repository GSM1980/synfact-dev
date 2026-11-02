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
```

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
