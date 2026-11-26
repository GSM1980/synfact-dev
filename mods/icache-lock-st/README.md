# I-cache lock (`icache-lock-st`) 0.1.1

Locks half of MAIN's 8 KiB instruction cache and preloads hot stock audio code
to reduce instruction fetches from DDR. It runs from the UI tick and has no
pages or settings.

Like the modulation skip, it ships in **every** bank and is not an optional
choice: `"always": true` in `js/config/syntakt-normal.json`. It is a MAIN CPU
optimization, not a feature, so it is not offered as a module on the website.

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

The UI tick schedules a refill every 30 ticks, about once a second. Before
invalidating the instruction cache, the routine checks the codec's transmit
DMA cursor and whether a MAIN render interrupt is pending. It defers to the
next UI tick during the first frame or last four frames of either 32-frame
half-buffer, when the cursor is outside the ring, or when the current
interrupt priority already blocks the MAIN render handler.

A refill invalidates the instruction and branch caches, touches the same 256
stock lines with `INTOUCH`, then enables `CACR[IHLCK]`. Audio interrupts stay
enabled throughout. If an interrupt preempts the refill, it can change which
valid lines are retained. Code changes continue to invalidate and unlock the
cache through the existing CACR write, and a later UI tick can reapply the lock.

In 0.1, the refill masked the audio interrupt across roughly 2,300 instructions
and 256 cache fills. The codec DMA continued to advance. In emulation, a
delayed interrupt could select the half of the receive buffer that was still
filling. That boundary crossing is a plausible explanation for the hardware
clicks.

## Syntakt hardware test

On 2026-11-26 the owner tested OS 1.42 with Synfact core 2.9.0 and the same
19-module bank and pattern. Track 1 played a sustained SY BITS tone routed
through the analog FX track with a digital FX send, monitored through the
instrument's physical outputs.

| Bank | Owner observation |
| --- | --- |
| Original, I-cache lock 0.1 | Intermittent clicks. |
| Same bank with only the cache refill callback bypassed | Five minutes of playback without a click. |
| Same bank rebuilt with `0.1.1-irq-test` | Click-free playback and the same MAIN CPU saving indicated by System Info as with 0.1. |

The tested bank SHA-256 was
`756202c8b4c85610c26102c58c2f60bebfa70649f9e15111a85858e50b20fb4e`.
Version 0.1.1 uses the tested refill routine and the same line table.
Offline checks exercised all 512 aligned DMA positions, a pending render,
invalid cursors, interrupt priorities and the tick retry after a deferral.
The hardware CPU result is a meter observation for this pattern.

## Regenerating `lines.c`

`lines.c` is generated from a profile taken with
`tools/stock-profile/analog-emu-xprof.patch` and `XPROF_CACHE=1`:

```sh
python tools/stock-profile/gen_lock_table.py prof.txt mods/icache-lock-st/lines.c "scenario"
```

Regenerate it when the stock OS changes. The table is only valid for the OS
the profile was taken on.
