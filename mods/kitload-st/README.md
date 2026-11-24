# kitload-st: a kit bank in a saved project

**Draft, 0.2.4.** 0.2.0 and 0.2.1 were built and tried in review; 0.2.2
(the stock page while KIT BANK is off, the row confirmation and its repaint)
was tried on a unit; 0.2.3 (a project named KITBANK) and 0.2.4 (the OS's
confirmation box) have not. The same
feature has been used on OS 1.42 as a binary patch outside this project;
this module is its rewrite on core events. Needs core 2.7 (host API
18, SMZ events 16/17).

## What it does

**Off until you turn it on.** SETTINGS > **KIT BANK** reads `OFF`. YES turns
it on with **P128** offered; LEFT/RIGHT pick any project, P1..P128 (and turn
it on); YES again turns it off. Installing the module never makes an
existing project a bank. A choice made here lasts until power-off.

**Kept across power-off: name the bank project `KITBANK`** (or `KIT BANK`, any
case). After power-on, while KIT BANK has not been changed by hand, the
lowest saved project with that name is the bank, and KIT BANK reads its
number. Nothing is written anywhere: the name is the setting. Rename or
delete the project and the bank is off again. Changing KIT BANK by hand
(YES or LEFT/RIGHT) takes over until the next power-on. The name is read
from the OS project manager's slot cache; the first lookup after power-on
reads the header of each slot whose entry is not filled yet (512 bytes each,
once).

While it is off, RELOAD FROM PROJ is the stock page: the module's rows are
built at boot but kept out of the menu until the bank is on. The bank must be
a saved project: the module saves kits into it, it cannot create one. Until
there is one, the page shows one row: `BANK P128 EMPTY` (nothing saved in
that slot) or `BANK P128 FORMAT n` (a project whose status is not 13, the
only one the OS loads). Save a project there (PROJECT > SAVE AS, P128), then
YES on that row looks again and brings the list.

With the bank on:

- **List.** SETTINGS > PATTERN > RELOAD FROM PROJ shows, after WHOLE PATTERN /
  KIT DATA / SEQUENCE DATA, a MODE row and 128 rows `A01 NAME` ... `H16 NAME`,
  one per kit of the bank project. The names are read again each time the
  page opens; a name that cannot be read shows `A01 ?` and is retried on the
  next opening.
- **Load.** In `MODE: LOAD KIT`, YES on a row opens the Syntakt's own
  confirmation box, `LOAD KIT A06?` / `CONFIRM Y/N` (the box CLEAR TRACK
  SOUND? uses). YES in the box replaces the current pattern's kit with that
  bank kit, whole; NO closes it. The bank is only read.
- **Save.** YES on the MODE row switches to `MODE: SAVE KIT` (every row then
  reads `SAVE A01 ...`). YES on a row opens the box, `SAVE TO A09?`; YES
  there writes the current pattern's kit into that position of the bank with
  the OS's own Kit encoder and slot writer, and the row shows the new name.
  The project in memory and its "kit changed" flags are untouched.
- **Shortcut.** A long press of **SETTINGS** opens the page from anywhere,
  instead of the LED INTENSITY quick view (LED intensity stays in the
  SETTINGS menu). The page stays open; NO closes it.
- **No bank project.** If the bank slot holds no loadable project, row 0
  reads `BANK P128 EMPTY` or `BANK P128 FORMAT n` (the slot status; 13 is
  the one the OS loads); YES on row 0 rereads the slot header and tries
  again. Nothing is read or written.

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
  Every write needs SAVE mode and a YES in the confirmation box.
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
| `ST_EV_KEY`, global | a long press of SETTINGS (KeyEvent flags bit 5) pushes the page on the global view manager and holds SETTINGS until released |
| `ST_EV_TICK` | page entry (the page's view on the global view stack, `std::list` at manager +20): names reread, rows shown; one redraw after a label changes |

**The confirmation box** is the OS's ConfirmWindow, built as CLEAR TRACK
SOUND? (`400653ae`) builds it: `operator new(0x7c)`, the constructor
`400f6156(this, question, "CONFIRM Y/N", std::function<void(bool)>, 0)`, a
shared_ptr control block `{401c88b0, 1, 1, box}`, pushed on the global view
manager (`40100d86`, as the shortcut pushes the page). YES or NO closes the
box, then calls the function with 1 or 0 (`400f6304`/`400f6330` → `40184a4e`).
It is drawn over the menu by the OS; the message popup (`400c347c`) is drawn
behind it, which is why the earlier confirmation could not use it.

The OS routines it calls (never patches) are in `kitload.h`. LOAD and SAVE
call the native Kit decoder `400a78a0` and encoder `400a78f4`, the entries the
core hooks, so the core's ENCODE/DECODE notifications run. SAVE writes the
whole `0x1600`-byte record, module records in the encoded tail
(`[14e0,15f8)`) included.

Construction touches no storage: the OS builds the SETTINGS menus once at
boot, before the saved-project table exists. The bank is read when the page
is drawn after it opens (128 reads of 512 bytes).

## Open questions

- **Where KIT BANK is kept.** A module has no settings store of its own, so
  0.2.3 uses the project's name (`KITBANK`) instead of a stored setting. A
  core place for small module settings would let a chosen number persist too.
- **The slot cache at power-on.** Whether the OS fills the project
  manager's slot cache for every slot at boot was not established statically;
  the module completes it once, so either way works.
- **View-stack reading.** Page entry and exit read the global view manager's
  `std::list` (layout from `40100d86`), a static reading not yet observed on
  a unit.

## Tests

`python tests/test_st_kitload.py [--cc m68k-elf-gcc]` compiles `main.c` with
the configured ColdFire compiler and runs the handlers in Unicorn against
stubs of the OS routines, at their real addresses:

- KIT BANK: off by default, YES toggles, LEFT/RIGHT pick P1..P128; off reads
  and writes nothing;
- a saved project named `KITBANK` / `kit bank  ` is the bank (the lowest),
  `KITBANKS`, `KIT BANK 2`, a status other than 13 or an empty entry are not;
  renaming turns it off; a change by hand takes over; the slot cache is
  completed once, only for entries not filled, with no kit reads;
- the 129 rows are built with no storage access, and the labels read the bank;
- the menu shows only its own three rows while KIT BANK is off, one row while
  the bank slot holds no loadable project (empty or another status) and all
  129 when it does, with the same items
  put back and the selection clamped when rows go;
- YES on a kit row opens the confirmation box with `LOAD KIT A06?` /
  `SAVE TO A09?` and `CONFIRM Y/N`, its function carrying the row and mode,
  its shared_ptr and push as the OS's own; the box's NO does nothing, its YES
  loads or saves; a box answered after KIT BANK went off does nothing; an
  answered SAVE and MODE invalidate the view and ask the tick for one redraw;
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
