# Mix fast (`mix-fast-st`) 0.1

Redirects the three stock MAIN mix kernels (8-, 9- and 10-track) to versions
that skip the per-frame gain-ramp additions and the gain store-back when every
ramp increment is zero, which is the normal steady state. It changes no sound:
when the gains are ramping, or for any input it cannot prove stable, it runs
the stock kernel. No UI, no pages, no settings.

Like the modulation skip, it ships in **every** bank and is not an optional
choice: `"always": true` in `js/config/syntakt-normal.json`. It is a MAIN CPU
optimization, not a feature, so it is not offered as a module on the website.

## Why

The stock mixer (`4008f242`) calls these kernels once or more per audio block.
Each one, for 32 frames, accumulates N tracks' samples times N gains with EMAC,
and after each frame adds a per-track ramp increment to every gain, then stores
the gains back. When no gain is ramping, those increments are zero: the adds and
the store-back do nothing. The three kernels are about 14 % of MAIN at the stock
reference ([docs/STOCK-MAIN-CPU-PROFILE.md](../../docs/STOCK-MAIN-CPU-PROFILE.md)).

Instructions per call, stock vs fast path (emulator, exact):

| kernel | tracks | stock | fast | 
|---|---|---:|---:|
| `4008ef8c` | 10 | 812 | 469 |
| `4008f024` | 9 | 750 | 422 |
| `4008f0b6` | 8 | 686 | 437 |

On the stock reference pattern the mixer calls the 8-track kernel for the two
main buses and the 9/10-track kernels for the FX buses, several times a block.
The modelled saving is roughly 1–1.5 points of MAIN; the device decides. When a
track's level or pan is moving (a ramp), that kernel runs stock for that block.

## How

`kernels.s` holds `mixfast_k8/k9/k10`. Each is `f(out, in, gains, increments)`.
The entry ORs the 8/9/10 increment words; if any is non-zero it rebuilds the
stock prologue and jumps into the stock kernel body (`4008f0be`, `4008f02c`,
`4008ef92`), so ramps and EMAC saturation are the stock code exactly. Otherwise
it runs the same MSAC sequence, in the same order and with the same operand
encodings as stock (checked byte-for-byte), without the increment adds or the
store-back, and returns the stock result.

`main.c` patches the three kernel entries at init with a `jmp` to the fast
versions (entry word written last, one cache invalidate), after verifying the
stock bytes it is about to overwrite. A mismatch leaves every kernel stock. It
is an init-only module (no tick, no events); the patch is permanent for the
boot, like LFO3's and Chorus's site patches.

## Validation

`tools/stock-profile`'s exact differential (analog-emu, `XDIFF`) ran each kernel
against the stock kernel from live mixer state during playback: 400 real calls
each, 0 differences, comparing all registers, the four EMAC accumulators,
MACSR, all of SRAM and DDR and the I/O. A fuzz pass of 600 cases each, with
extreme gains and samples (the Q31 limits) and both zero and non-zero ramp
increments, also found 0 differences, exercising saturation and the stock
tail-jump. The per-kernel instruction counts above come from the same runs.

A broader scan confirmed it: during 50 M instructions of playback the three
kernels were called 212,625 times in all, every call byte-exact against stock
(registers, the four accumulators, MACSR, SRAM and I/O).

A full-playback audio comparison in the emulator is **not** a valid end-to-end
check here, and this is worth recording. Redirecting a kernel adds one `jmp`
to its call, and the emulator is bounded by instruction count with idle-skip,
so that single extra instruction per call shifts the instruction/interrupt
interleaving and, after about 0.4 s, nudges a sequencer event and the music
diverges. This is not a computation difference: a **verbatim** copy of each
kernel (identical bytes, identical instruction count) relocated and redirected
the same way diverges at the same point, while rewriting a kernel's entry in
place with its own bytes (no extra instruction) leaves the audio bit-identical.
On hardware the engine runs at a fixed sample rate, so one extra `jmp` per
audio block changes neither event timing nor the rendered audio; it only costs
cycles. Correctness rests on the per-call exactness above and the verified
patch bytes; the CPU saving is pending the owner's A/B test.

## Measuring it on the unit

Same core, same pattern, tempo and System Info page, restart between:

1. the bank without this module: CPU M reading;
2. the same bank with `--module mods/mix-fast-st` before
   `mods/modulation-skip-st`: CPU M after a few seconds.

Move a track's level or pan to confirm the sound is unchanged while a ramp runs
(the kernel falls back to stock during the ramp). Listen for clicks: there
should be none, as the computation is identical.
