# kitload-st: a kit bank in a saved project

**Draft. Not yet built with SGDK 2.11 or run on a unit through Synfact.**
The same feature has been used on OS 1.42 as a binary patch outside this
project; this module is its rewrite on core events. Needs core 2.7 (host API
18, SMZ events 16/17).

## What it does

One saved project, **P128**, is a bank of 128 kits.

- **List.** SETTINGS > PATTERN > RELOAD FROM PROJ shows, after WHOLE PATTERN /
  KIT DATA / SEQUENCE DATA, a MODE row and 128 rows `A01 NAME` ... `H16 NAME`,
  one per kit of P128.
- **Load.** In `MODE: LOAD KIT`, YES on a row shows `LOAD A06? YES again`; a
  second YES on the same row copies that kit's sounds into the current
  pattern's kit and shows `LOADED A06 NAME`. P128 is only read.
- **Save.** YES on the MODE row switches to `MODE: SAVE KIT` (every row then
  reads `SAVE A01 ...`). YES on a row shows `SAVE A09 IN P128? YES`; a second
  YES writes the current pattern's kit into that position of P128 with the
  OS's own Kit encoder and slot writer, then shows `SAVED A09 NAME`. The
  project in memory and its "kit changed" flags are untouched.
- **Cancel.** Any other key, another row or the MODE row cancels a pending
  confirmation; opening the page with the long press does too.
- **Shortcut.** A long press of **SETTINGS** opens the page from anywhere,
  instead of the LED INTENSITY quick view (LED intensity stays in the
  SETTINGS menu). The page stays open; NO closes it.
- **No bank.** If P128 is not a saved project in the current format, row 0
  reads `BANK P128 NOT SAVED`, rows 1 and 2 the slot status (13 = ok) and the
  storage state (2 = ready); YES on row 0 tries again. Nothing is read or
  written.

Only the sounds move on LOAD: the 12 audio tracks' 291-byte sound records
and the FX track's 71 parameter words, through the core's COPY path
(`core_meta_copy`), as the stock track-sound reload does. The kit's per-track
blocks and the pattern's settings stay the pattern's own.

## Before you use it

- **P128 is written.** Save into a project you made for the purpose. If
  P128 holds a real project, SAVE replaces its kits. Every write needs SAVE
  mode and two YES, and the prompt names the project. The bank is fixed at
  build time (`-DKL_BANK=n`, 1..128). A runtime setting that turns the bank on
  or picks the slot is not there yet.
- **Samples are per project.** A bank kit's sampler tracks, and pool
  positions 65..128, play the *current* project's samples, not P128's.
- **Machines are per bank.** A kit that uses SMZ4 machines missing from the
  loaded bank falls back like any project would.

## How it is built

Events only; the module owns no code site.

| event | use |
|---|---|
| `ST_EV_RELOAD_MENU` (core 2.7) | appends the MODE row and the 128 kit rows with the OS's MenuItem constructor and `Menu::addItem` |
| `ST_EV_PATTERN_MENU` (core 2.7) | keeps PatternMenuView, which owns the page (`shared_ptr` at +472) |
| `ST_EV_KEY`, global | a long press of SETTINGS (KeyEvent flags bit 5) pushes the page on the global view manager and holds SETTINGS until released; other keys cancel a confirmation |

The OS routines it calls (never patches) are in `kitload.h`. LOAD and SAVE
call the native Kit decoder `400a78a0` and encoder `400a78f4`, the entries the
core hooks, so the core's ENCODE/DECODE notifications run. SAVE writes the
whole `0x1600`-byte record, so module records in the encoded Kit tail
(`[14e0,15f8)`: LFO3, COMP, Playhead) are stored with it, and LOAD hands the
same bytes to the decoder.

Construction touches no storage: the OS builds the SETTINGS menus once at
boot, before the saved-project table exists. The bank is read when a row is
drawn or chosen (128 reads of 512 bytes, once per page opening).

## Open question

On LOAD the record (tail included) is decoded into a temporary Kit, and
only sound records are then copied into the live kit. Per-sound module state
that follows the core's COPY notifications comes along; per-kit records
(COMP's FX settings, Playhead) stay the live kit's own. Whether LOAD should
bring those too, and through which audited path, is for the maintainers.

## Tests

`python tests/test_st_kitload.py [--cc m68k-elf-gcc]` compiles `main.c` with
the configured ColdFire compiler and runs the handlers in Unicorn against
stubs of the OS routines, at their real addresses:

- the 129 rows are built with no storage access, and the labels read the bank;
- LOAD and SAVE need two YES on the same row; another key cancels;
- LOAD copies only the sounds, through the COPY path, and notifies the engine;
- SAVE writes the whole record, and a LOAD of that position hands the same
  bytes, Kit tail `[14e0,15f8)` included, to the native decoder;
- with no bank, nothing is read or written;
- a long press of SETTINGS opens the page once, holds SETTINGS until its
  release, and leaves other event classes alone.

It does not run the real OS, the core's codec hooks or a bank build.

## Licence

GPL-2.0-or-later.
