# kitload-st: a kit bank in a saved project

**Draft, 0.3.0.** 0.2.0 to 0.2.5 were tried in review and on a unit; 0.3.0
(the naming box on SAVE, LOAD without confirmation, the SETTINGS icon) has
not.

## What it does

**Off by default, unless a bank project is named `KITBANK` or `KIT BANK`
(see below).** SETTINGS > **KIT BANK** reads `OFF` when no named bank is
found. YES turns it on with **P128** offered; LEFT/RIGHT pick any project,
P1..P128 (and turn it on); YES again turns it off. A manual choice made here
is saved globally by core 2.9 / host API 20 (kitload-st 0.2.4), including an
explicit OFF. Earlier cores retain the temporary choice behavior.

**Default before a manual choice: name the bank project `KITBANK`** (or `KIT BANK`, any
case). After power-on, while KIT BANK has not been changed by hand, the
lowest saved project with that name is the bank, and KIT BANK reads its
number. Nothing is written anywhere: the name is the setting. Rename or
delete the project and the bank is off again. Changing KIT BANK by hand
(YES or LEFT/RIGHT) takes over and persists across power-on on API 20. The name is read
from the OS project manager's slot cache; the first lookup after power-on
reads the header of each slot whose entry is not filled yet (512 bytes each,
once).

While it is off, RELOAD FROM PROJ is the stock page: the module's rows are
built at boot but kept out of the menu until the bank is on. The bank is a
saved project. If its slot is empty, the page shows one row, `CREATE KITBANK
IN P128`: YES, then YES in the confirmation box, makes the bank from the
current project as last saved, with PROJECT's own COPY, PASTE and RENAME
(as one job, like PROJECT runs them), names it `KITBANK` (so it is found
after every power-on) and shows the list. The current project stays current
and is not changed; the project clipboard is replaced, as COPY PROJECT
replaces it. It needs the current project to have been saved, and is never
offered over an existing project: a slot holding a project the OS cannot
load reads `BANK P128 FORMAT n`.

With the bank on:

- **List.** SETTINGS > PATTERN > RELOAD FROM PROJ shows, after WHOLE PATTERN /
  KIT DATA / SEQUENCE DATA, a MODE row and 128 rows `A01 NAME` ... `H16 NAME`,
  one per kit of the bank project. The names are read again each time the
  page opens; a name that cannot be read shows `A01 ?` and is retried on the
  next opening.
- **Load.** In `MODE: LOAD KIT`, YES on a row replaces the current
  pattern's kit with that bank kit, whole, at once: no confirmation, so
  unsaved changes to the current kit are replaced (the project's own saved
  kit is untouched: RELOAD FROM PROJ > KIT DATA brings it back). The bank is only read.
- **Save.** YES on the MODE row switches to `MODE: SAVE KIT` (every row then
  reads `SAVE A01 ...`). YES on a row opens the OS's naming box, `SAVE TO
  A09`, offering the current pattern's name (or the slot's own name when the
  pattern has none). Its YES writes the current pattern's kit into that
  position of the bank under the typed name (an empty name saves as `KIT`),
  with the OS's own Kit encoder and slot writer, and the row shows the new
  name; NO cancels. The project in memory and its "kit changed" flags are
  untouched.
- **Icon.** The KIT BANK row in SETTINGS has its own icon (four pads), drawn
  as Sample Manager draws its own.
- **Shortcut.** A long press of **SETTINGS** opens the page from anywhere,
  instead of the LED INTENSITY quick view (LED intensity stays in the
  SETTINGS menu). The page stays open; NO closes it.
- **No bank project.** An empty bank slot offers `CREATE KITBANK IN P128`
  (above). A slot holding something the OS does not load reads
  `BANK P128 FORMAT n` (the slot status; 13 is the one the OS loads); YES on
  it rereads the slot header and tries again. No kit data is read or written.
- **Messages** (`LOADED A06 NAME`, `SAVED A09 NAME`, `KITBANK CREATED IN
  P128`, ...) appear in the OS's text box, the one PROJECT's COPY PROJECT
  shows, drawn over the menu.

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
  Saving a kit needs SAVE mode and a YES in the confirmation box. Creating
  a bank also writes a project, after its own confirmation.
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

**Creating the bank** runs PROJECT's own routines in one job posted on the
OS's job queue (`400bdc48(4016ec18(), name, work, done)`, as COPY/PASTE/RENAME
PROJECT post theirs at `4005a4d8`/`4005a5ac`/`4005a430`): `400c1e54(store,
current slot)` copies the saved project to the clipboard, `400c1ffa(store,
bank slot, 0)` pastes it, and the header `400c1b64(store, slot)` gets the
name `KITBANK` at +8 and is written by `400c1cbc(store, slot, empty
function)`; `store` is `4016f15a()`, the current slot `40018c2c(project)`.
The done function rereads the slot header (SREFR) and shows the result.

**The naming box** (SAVE) is the one PROJECT NAME (`4005e0a6`) and SAVE
PATTERN (`401836be`) open: a make_shared block of `0xb0` bytes, control
`{401cbb0c, 1, 1, box}` with the box at +16, constructed by `400fca64(box,
title, name, 0, 16, 0, std::function<void(std::string &, int)>)` as SAVE
PATTERN passes the arguments, pushed on the global view manager. YES trims
trailing spaces and calls the function with the name (`400fd40c`); NO closes
it without a call. The name replaces the 16 bytes at +4 of the encoded
record before the slot write.

**The icon** is a 12x11 bitmap in the OS's column format (bit 31 the top
row) with its mask, a non-owning descriptor (`400f72be`) blitted by
`400f8f70` at the row's x/y from the row's draw callback, as Sample
Manager's.

**The confirmation box** (CREATE KITBANK) is the OS's ConfirmWindow, built as CLEAR TRACK
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
- an empty bank slot offers CREATE KITBANK; the box asks; its YES posts one
  job that copies the current saved slot, pastes it into the bank slot and
  renames it KITBANK (NUL-padded), then rereads the slot and reports; the
  module writes nothing itself; no second job while one runs; a never-saved
  current project, a failed copy (no rename), a slot holding a project (not
  offered, and checked again at the answer) are refused;
- messages go to the OS's text box (`400fdfc8`), over the menu;
- YES on a kit row in LOAD mode loads at once (no box); nothing loads while
  KIT BANK is off; a LOAD, a named SAVE and MODE invalidate the view and ask
  the tick for one redraw; the CREATE question's box carries its function,
  shared_ptr and push as the OS's own;
- in SAVE mode YES opens the naming box (`SAVE TO A09`, the slot's own name
  offered when the pattern has none, arguments 0/16/0, make_shared control
  block with the box at +16); its YES saves under the typed name (written at
  +4 of the record), an empty name saves as `KIT`, nothing after KIT BANK
  went off;
- the KIT BANK icon: a 12x11 column bitmap with its mask, blitted at the
  row's x/y and released;
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
