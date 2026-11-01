# Notes for coding agents

Synfact builds custom firmware and mods for the Elektron Syntakt (OS 1.41), on
the user's machine or in their browser, from the stock OS file they supply. It
is built on elekloader's engine and keeps only the Syntakt. Read README.md
first; the rest of the docs start at docs/README.md.

**Building on this workstation** (core-st, SMZ banks): follow
[docs/SYNTAKT-BUILD.md](docs/SYNTAKT-BUILD.md) exactly. It gives the m68k
toolchain location (`C:\Desarrollo\syntakt-dev\toolchains\sgdk211\bin`,
unprefixed GCC 13.2.0), the required `ELEKLOADER_CROSS=''` and
`GCC_EXEC_PREFIX`, the compiler flags and the build, lint, bank and
regression commands. Do not change flags or builders to make a build pass.

**Several agents at once**: each works in its own git worktree, never in a
folder another agent uses; see [docs/PARALLEL-AGENTS.md](docs/PARALLEL-AGENTS.md).

## If you are adapting or writing a mod

**Core first.** Before you add a code site to a mod, check whether the core
(`mods/core-st`) already has an event, a table or a firmware export for it,
and if not, whether another mod could want the same thing. If one could,
build that part in the core's shape, in a file of its own, so that it can
move into the core ([docs/CORE-CONTRIBUTING.md](docs/CORE-CONTRIBUTING.md)),
and tell the user.

Follow [docs/ADAPTING.md](docs/ADAPTING.md): the rules, the Syntakt core's
events and tables, a table from each refusal message to its fix, and a
definition of done. `examples/machine-test-st` and `examples/smz-test-st`
are small starting points; the bank and regression commands are in
SYNTAKT-BUILD.md.

## If you are changing the engine

The engine (`elekloader/`, `js/`) comes from
[elekloader](https://github.com/irpina/elekloader) and still supports every
device it did; only the Syntakt's products are kept here. Change it as
little as possible, and offer the user a pull request on elekloader for any
engine change, so the two stay comparable.

| path | what |
|---|---|
| `elekloader/devices/` | everything device-specific: one profile per product, releases by hash (`syntakt.py` is ours) |
| `elekloader/formats.py`, `syx.py` | the OS file families; `syx.py` reads, writes (MAIN and the explicit AUDIO companion) and verifies the Syntakt's ELE3 file |
| `elekloader/elemod.py`, `link.py` | the mod format and the format-2 linker with its checks |
| `elekloader/sdk/` | the mod SDK; `sdk/syntakt/` builds the core SYX (`build.py`, `audio.py`) and SMZ4 banks (`smz.py`), and talks to the unit (`mods.py`, `data_rpc.py`) |
| `elekloader/sdk/include/syntakt/` | the headers mods build against |
| `elekloader/patch.py`, `gui.py`, `lint.py`, `mkmod.py` | the command line, the window and the mod authors' tools |
| `elekloader/codec/`, `elekloader/isa/` | code from digikit (GPL-2.0-or-later): change it only with a round-trip test |
| `mods/core-st/` | the Syntakt's core: hook bus, SMZ4 banks, storage, sample pool; `mods/core/` holds the two sources it shares with elekloader's core |
| `mods/*-st/` | the Syntakt's machines and modules |
| `js/` | the engine in TypeScript (GPL-3.0-or-later; js/README.md), matching the Python byte for byte; `js/src/syntakt-*.ts` and `smz-*.ts` build the core and the bank in a browser |
| `web/bridge.py` | the Python reference for the TypeScript engine's bridge; elekloader's page itself is not kept |
| `js/src/kit/`, `js/tools/`, `js/config/`, `packaging/build_kit.py` | the kit for websites (docs/INTEGRATING.md) and the tools that prepare the browser's core package and SMZ catalog (docs/SYNFACT-WEB.md) |
| `tools/` | maintenance imports and studies for the Syntakt mods |

Tests (docs/TESTS.md):

```bash
python tests/test_units.py                                   # always
cd js && node --test "test/*.test.ts"                        # the TypeScript engine
python tests/test_st_common_regressions.py <build folder> ...  # the Syntakt's regressions: docs/SYNTAKT-BUILD.md
```

A test whose files are not given is skipped, not passed. Say which ran.

Rules:
- **Never commit firmware.** That means `.syx` and `.smz4` files, extracted
  sections, `.bin` images, built `.elemod` files, or anything derived from a
  stock OS. `.gitignore` covers the usual names; check `git status` anyway.
- **Never weaken a check to make something pass.** `syx.verify` and the
  checks in `elemod`/`link` are the user's protection: the bootloader must
  stay stock, and mods must not collide. If a check refuses something
  legitimate, fix the check narrowly and add a test for both sides.
- **Device-specific facts belong in `devices/`**, not in the code paths.
- **The TypeScript engine (js/) changes with the Python.** A change to what
  the Python accepts, writes or says needs the same change in its js/ port,
  and `node --test "js/test/*.test.ts"` to pass.
- **The kit stays site-agnostic.** Nothing in js/src/kit names a website,
  its pages or its accounts. A change to a call or a reply raises `PROTOCOL`
  (js/src/kit/protocol.ts); a change to the catalog format raises
  `CATALOG_SCHEMA`. docs/INTEGRATING.md changes with them.
- **A change to the file format** needs docs/FORMAT.md, docs/ADAPTING.md and
  tests updated with it.
- **No device access.** Agents never send MIDI or USB, install firmware or
  write the instrument; the user does.

Done means all of these:
- the tests that could run pass;
- the docs match the code;
- `git status` shows no firmware.
