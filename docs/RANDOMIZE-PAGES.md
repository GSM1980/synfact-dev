# Randomizing the new pages (core 1.4, list item 22)

The page key + YES randomizes the page shown, as on the stock pages. Before
core 1.4 it changed nothing on the bank machines' SYN pages, POLY's TRIG
page, the ARP view and the chorus page. Addresses are OS 1.41 MAIN, measured
in the disassembly and in the analog-emu emulator.

## How the stock randomize works

The parameter view's key adapter (`4003bc74`) calls the view's vtable slot
`0xe0` when YES is pressed with the page key held.

- The parameter pages (`400385dc`, "Randomize Page %s") call `4001f286`:
  the page's layout IDs are captured (`4001f16c`), each ID's model is
  resolved (`4001c422`, by the ID's owner) and the model's method 7
  (`40014954`) draws the ID.
- The SYN view has its own slot (`40044972`), which calls `4001c5da`: the
  track's machine lists its IDs (`400146bc`) and the Sound model draws each.
- `40014954` takes the ID's cached entry (`4007b9b2`) and skips the ID when
  the entry's `std::function` (+52 functor, +60 manager, +64 invoker) is
  empty. Almost every stock ID has the generic pair `400744f0`/`40077ee8`:
  a uniform value over the ID's range (`400a62c8`), in whole steps. The
  value is stored by the model's setter (`40012b6e`).

## Why the new pages did nothing

- `400146bc` scans IDs 0..504, so a bank machine (IDs `0x1f9..0x3f8`) had
  no IDs to draw.
- A bank or module control's cached entry is its alias's; the alias is
  None (`0x13`), which has no randomizer, so `40014954` skipped it.
- The chorus's IDs resolved to the Sound model, and the Kit model's setter
  (`40013516`) does not know the chorus's tagged FX record words.
- The ARP is a module view of its own; YES there toggles a step.

## Core 1.4

| site | what |
|---|---|
| `40044972` | the SYN view's randomize records its view |
| `400146bc` | a bank machine's list: the IDs on that view's current page |
| `4007b9b2` (existing hook) | for the draw (`40014954`): the generic randomizer for a bank control, or for any ID a provider answers query 31 for |
| `400a61fa` | the Kit-owner check asks the owner query, so a module's Kit control resolves to the Kit's model |
| `40013516` | the Kit model's setter sends module IDs to query 24, as the Sound setter does |

The pool sample slot alias (`0x1f2`) is never drawn. Host API 14 adds no
field (queries 24 for the Kit and 31; `host.h`).

## Modules

- Chorus 1.1: answers 31 for its seven settings and stores 24 through its
  own setter. FUNC + AMP + YES on Reverb 2/2 draws them.
- POLY 1.2: answers 31 for NOT2..NOT4 (`0x1e..0x20`, laid out only on its
  page). NOTE stays, as on the stock TRIG page; POLY, VOICES and PLAY are
  never drawn. Registers its provider on host API 14 only.
- ARP 1.6: TRIG + YES on its view draws the page shown before that TRIG
  press. The arp page draws SPEED, RANGE, N.LEN, LEN and the selected step's
  OFS, and MODE among the four modes when the arp is on (OFF stays OFF);
  the chord page draws NOT2..NOT4 over 0..127. Nothing in lock mode.
- LFO3 1.2: leaves `40013516` to core 1.4 and stores its FX values through
  query 24.

## Checks

- `tests/test_st_common_hooks.py` pins the four sites (106).
- analog-emu (opt-in `DTEMU_ST141_MODIFIED=1` profile for this core): SP
  ONE SHOT and a multi-page machine draw only the page shown; chorus, POLY
  and ARP draw as above; the stock TRIG/SYN pages and LFO3's page draw as
  before. The first build was checked by the user on the unit.
- Not changed: SYN + NO / TRIG + NO (reload) on the new pages.
