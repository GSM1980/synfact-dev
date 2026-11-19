# I-cache lock (`icache-lock-st`) 0.1, experimental

Keeps 4 KB of the stock audio block's hottest code permanently in MAIN's
instruction cache. It changes no code and no behavior, only how often MAIN
fetches code from DDR. It has no sites, no patches, no pages and no settings.

## Why

The stock audio block executes about 22 KB of code on every block, and MAIN
has an 8 KB instruction cache (4 ways, 128 sets of 16-byte lines). Almost every
hot line is evicted before it comes round again, so it misses once per block.
That is about 1,470 misses per block, roughly a third of MAIN at the stock
reference ([docs/STOCK-MAIN-CPU-PROFILE.md](../../docs/STOCK-MAIN-CPU-PROFILE.md)).

The MCF5441x can lock half of the cache (RM 6.4.5, `CACR[IHLCK]`). With the
cache invalidated, the first two lines of a set fill ways 0 and 1. With
IHLCK set, new lines only go to ways 2 and 3, so the lines in ways 0 and 1
stay. `lines.c` names two stock lines per set: those that missed most often in
an exact emulator profile. In the emulator's cache model this saves
about 220 misses per block: MAIN at the stock reference goes from 77.0 % to
71.7 % of 250 MHz. That is a model, not a meter reading.

## How

- `lock.s`: masks interrupts, writes the native CACR value with the
  instruction and branch cache invalidate (`a40ce100`, the same value the
  mods' code-change sequences write), touches the 256 lines with `INTOUCH`
  (encoded as `.word 0xf428`; the SGDK assembler has no mnemonic for it),
  writes the native value with IHLCK (`a408e800`), and restores SR. About
  2,300 instructions with interrupts masked; each touch is one line fill.
- `main.c`: applies the lock every 30 UI ticks (once a second). Every code
  change in the core and the mods (LFO3, Chorus, Playhead, Modulation Skip,
  the stereo placement, the bank loader) ends with that invalidate write,
  which also unlocks the cache. The lock then comes back within a second.
  A locked line therefore never holds code older than the latest
  invalidate, the same guarantee the cache gives without locking.
- Only stock MAIN addresses are locked (`0x40000400..0x40348b90`, OS 1.42).
  A wrong or moved address only costs performance: `INTOUCH` fills a line
  and changes nothing that runs.

## Measuring it on the unit

Same core, same pattern, same tempo and the same System Info page, and a
restart between the two:

1. the bank without this module: CPU M reading;
2. the same bank with `--module mods/icache-lock-st` before
   `mods/modulation-skip-st`: CPU M reading after a few seconds.

The model predicts about five points less on the stock reference. The bank's
own code also competes for the remaining two ways, so the real figure may be
lower. Listen for clicks or dropouts: interrupts stay masked for about
2,300 instructions once a second.

## Regenerating `lines.c`

`lines.c` is generated from a profile taken with
`tools/stock-profile/analog-emu-xprof.patch` and `XPROF_CACHE=1`:

```sh
python tools/stock-profile/gen_lock_table.py prof.txt mods/icache-lock-st/lines.c "scenario"
```

Regenerate it when the stock OS changes. The table is only valid for the OS
the profile was taken on.
