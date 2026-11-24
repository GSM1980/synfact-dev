# kitload-st: a kit bank in a saved project

**Draft, 0.2.2.** 0.2.0 and 0.2.1 were built and tried in review; 0.2.2
(the stock RELOAD FROM PROJ page while KIT BANK is off) has not. The same
feature has been used on OS 1.42 as a binary patch outside this project;
this module is its rewrite on core events. Needs core 2.7 (host API
18, SMZ events 16/17).

## What it does

**Off until you turn it on.** SETTINGS > **KIT BANK** reads `OFF`. YES turns
it on with **P128** offered; LEFT/RIGHT pick any project, P1..P128 (and turn
it on); YES again turns it off. Installing the module never makes an
existing project a bank. The choice lasts until power-off: a module has no
place to keep it yet (see Open questions).

While it is off, RELOAD FROM PROJ is the stock page: the module's rows are
built at boot but kept out of the menu until the bank is on. While the bank
project is not saved, the page shows one row, `BANK P128 NOT SAVED`; YES on
it looks again (after you save that project) and brings the list.

With the bank on:

- **List.** SETTINGS > PATTERN > RELOAD FROM PROJ shows, after WHOLE PATTERN /
  KIT DATA / SEQUENCE DATA, a MODE row and 128 rows `A01 NAME` ... `H16 NAME`,
  one per kit of the bank project. The names are read again each time the
  page opens; a name that cannot be read shows `A01 ?` and is retried on the
  next opening.
- **Load.** In `MODE: LOAD KIT`, YES on a row shows `LOAD A06? YES again`
  (popup and row, as for SAVE); a
  second YES on the same row replaces the current pattern's kit with that
  bank kit, whole, and shows `LOADED A06 NAME`. The bank is only read.
- **Save.** YES on the MODE row switches to `MODE: SAVE KIT` (every row then
  reads `SAVE A01 ...`). YES on a row shows `SAVE A09 IN P128? YES`, as a
  popup and in place of the row until it is answered; a second
  YES writes the current pattern's kit into that position of the bank with
  the OS's own Kit encoder and slot writer, then shows `SAVED A09 NAME`. The
  project in memory and its "kit changed" flags are untouched.
- **Cancel.** A pending confirmation is cancelled by any other key (the
  arrows included), by any encoder event, by YES on another row or on MODE,
  and by leaving or reopening the page. Every move of the selection is an
  arrow key or an encoder event, so moving away and back between two ticks
  still cancels.
- **Shortcut.** A long press of **SETTINGS** opens the page from anywhere,
  instead of the LED INTENSITY quick view (LED intensity stays in the
  SETTINGS menu). The page stays open; NO closes it.
- **Not saved.** If the bank project is not a saved project in the current
  format, row 0 reads `BANK P128 NOT SAVED`, rows 1 and 2 the slot status
  (13 = ok) and the storage state (2 = ready); YES on row 0 tries again.
  Nothing is read or written.

**What LOAD restores.** Everything the Kit holds: the 12 audio tracks' and
the FX track's sounds and settings, the kit's per-track blocks and name, and
every module's per-kit state (LFO3, COMP, Chorus, Playhead's saved modes).
The record is decoded by the native decoder into a temporary Kit, so the
core's DECODE notifications give each module its record from the Kit tail;
one `0x1618`-byte copy through the core's COPY path (`core_meta_copy`) then
replaces the live Kit, the way the OS copies kits, and the modules move their
state with it. The pattern and its sequencer data (trigs, notes, parameter
locks, microtiming, conditions, lengths) live outside the Kit and are not
touched. Playhead: its saved per-track modes travel with the kit; playback
position follows Playhead's own load behaviour.

## Before you use it

- **The bank project is written.** Pick a project you made for the purpose.
  Every write needs SAVE mode and two YES, and the prompt names the project.
- **Samples are per project.** A bank kit's sampler tracks, and pool
  positions 65..128, play the *current* project's samples, not the bank's.
- **Machines are per bank.** A kit that uses SMZ4 machines missing from the
  loaded bank falls back like any project would.

## How it is built

Events only; the module owns no code site.

| event | use |
|---|---|
| `ST_EV_RELOAD_MENU` (core 2.7) | appends the MODE row and the 128 kit rows with the OS's MenuItem constructor and `Menu::addItem` |
| `ST_EV_PATTERN_MENU` (core 2.7) | keeps PatternMenuView, which owns the page (`shared_ptr` at +472) |
| `ST_EV_SETTINGS` | the KIT BANK row (`additem`: label, YES, LEFT/RIGHT) |
| `ST_EV_KEY`, global | a long press of SETTINGS (KeyEvent flags bit 5) pushes the page on the global view manager and holds SETTINGS until released; other keys cancel a confirmation |
| `ST_EV_ENC`, global | any encoder event cancels a pending confirmation (never consumed) |
| `ST_EV_TICK` | page entry and exit (the page's view on the global view stack, `std::list` at manager +20), and the selected row (Menu vtable +36) against the armed row |

The OS routines it calls (never patches) are in `kitload.h`. LOAD and SAVE
call the native Kit decoder `400a78a0` and encoder `400a78f4`, the entries the
core hooks, so the core's ENCODE/DECODE notifications run. SAVE writes the
whole `0x1600`-byte record, module records in the encoded tail
(`[14e0,15f8)`) included.

Construction touches no storage: the OS builds the SETTINGS menus once at
boot, before the saved-project table exists. The bank is read when the page
is drawn after it opens (128 reads of 512 bytes).

## Open questions

- **Where KIT BANK is kept.** It resets to OFF at power-off. A module has no
  settings store of its own; a core place for small module settings would
  let it persist.
- **View-stack reading.** Page entry and exit read the global view manager's
  `std::list` (layout from `40100d86`), a static reading not yet observed on
  a unit.

## Tests

`python tests/test_st_kitload.py [--cc m68k-elf-gcc]` compiles `main.c` with
the configured ColdFire compiler and runs the handlers in Unicorn against
stubs of the OS routines, at their real addresses:

- KIT BANK: off by default, YES toggles, LEFT/RIGHT pick P1..P128; off reads
  and writes nothing;
- the 129 rows are built with no storage access, and the labels read the bank;
- the menu shows only its own three rows while KIT BANK is off, one row while
  the bank project is not saved and all 129 when it is, with the same items
  put back and the selection clamped when rows go;
- LOAD and SAVE need two YES on the same row, and the row shows the question
  while it waits; another key, an encoder
  event, a selection change, leaving the page cancel, including a move away
  and back with no tick between;
- the names are read again on each entry (SETTINGS > PATTERN or the
  shortcut), and a failed read shows `?` and is retried on the next entry;
- LOAD replaces the whole Kit in one COPY of `0x1618` bytes, with what the
  decoder produced, and notifies the engine;
- SAVE writes the whole record, and a LOAD of that position hands the same
  bytes, Kit tail `[14e0,15f8)` included, to the native decoder;
- a long press of SETTINGS opens the page once, holds SETTINGS until its
  release, and leaves other event classes alone.

`python tests/test_st_kitload_restore_emu.py --core ... --bank ...` runs LOAD's
restore with the real native codecs and the real COMP, Chorus, LFO3, POLY and
Playhead providers on a built core and bank: the destination Kit must equal a
native decode of the record, with the source's COMP, Chorus and Playhead
settings, and the project memory before the Kits and the other Kits must not
change.

## Licence

GPL-2.0-or-later.
