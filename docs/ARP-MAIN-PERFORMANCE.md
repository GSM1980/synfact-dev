# ARP MAIN performance

Release versions and development labels: see [the consolidated MAIN release](MAIN-OPTIMIZATION-RELEASE.md). Numbers in the measurements below identify historical development iterations.

2026-11-12. Continue in the existing private `perf/lfo3-main` worktree based
on main `e4af43a3a522846aba224a181d5813e42f5dd827`. The reference contains
ARP 1.9, the owner's tested samplers 1.4.5 and LFO3 1.4. The candidate is
ARP 1.10 for the first pass, 1.11 for the keyboard/clock pass and 1.12 for
the selector/track scan below.
All non-ARP objects, bank AUDIO and the complete core SYX stay
identical. No Chorus or core change is introduced.

## Changes

The running-track callback previously called `arp_clock_arm` on a temporary
twenty-byte clock every block, then used only its period. That helper also
divided `86400000` by one of eighteen fixed denominators on each invocation.
The candidate stores the exact integer periods in 72 bytes of constant data.
It reads only the desired period in the callback and retains the existing
phase adjustment when speed changes. Clock starts use the same table and
keep their original range/generation guards and failure-state writes.

When no supported root lock is present, the render callback now reads its
published base settings directly. All writers already use native exclusion,
and runtime/selector functions only read those settings synchronously. They
retain no settings pointer. A nonzero supported lock mask still produces the
same private settings copy and validates every locked value. Upper unused
mask bits alone do not require a copy. This one test replaces the copy and
the individual lock-field tests in the common unlocked path; the complete
callback, including this test, is measured with and without locks.

This first pass adds no persistent cache, invalidation machinery, frequency
reduction, EMAC change or SRAM reservation. It eliminates integer operations
directly; the additional assembly work is described below.

## Measurement and limits

`tools/bench_st_arp_main.py` imports the existing native-producer and analog
fixtures without altering their historical acceptance conditions. Both real
linked banks execute with the same core, original UI note producers, native
held-note set, event/lock pools, queue and note effects. Publication, smoothing,
mutex and native lock application remain explicit fixture boundaries. LFO3
and POLY active engines, full scheduler, DSP, DMA and cache timing are not
executed by this fixture. Instructions are not cycles or CPU percentages.

The measured region is the entire `arp_frame` callback. Preparation, keyboard
input and queue draining are outside its instruction count; their resulting
events and state are compared. Forty scenarios cover 0/1/4/8/12 tracks, live
and sequenced roots, speeds 0/9/17, no emission, emissions, and changing
tempo/locks. Each ordinary scenario has five blocks and each changing scenario
twelve: 270 complete callbacks per bank. Sequence roots enter through native
acceptance when the queue first drains, so their first callback can be idle.

Changing scenarios cover each individual lock, all fields, unused upper bits,
invalid locked values and transitions back to the base settings. Modes span
TRUE/UP/DOWN/CYCL and ranges span rising/falling settings. Comparisons include
every runtime/selector/clock/settings byte, root locks, generations, retained
template payloads, queued ownership, emitted notes, native analog note effects,
lock calls and final balanced pool counts. Only bank-relative API and captured
live-Sound pointers are normalized by symbol identity because linking moves
them. Each callback checks SP and callee-saved registers via the existing
fixture. Another 414 arm/poll comparisons cover all eighteen speeds, invalid
speeds, zero/stale generations, exclusive frame bounds, wrap and late recovery.
Every measured candidate callback must execute no more instructions than its
reference. The historical analog test's equality to a previous digital-only
delivery describes that port, not this intentional optimization; it is not
used as this delivery's performance comparison and has not been weakened.

Representative unlocked callback counts (these rows use the same input mode
mix and native-producer fixture, and must not be compared to unrelated old
fixtures):

| Case | ARP 1.9 | ARP 1.10 |
|---|---:|---:|
| No ARP running | 8 | 8 |
| One live track, not due | 244 | 197 |
| Four live tracks, not due | 910 | 719 |
| Eight live tracks, not due | 1,798 | 1,415 |
| Twelve live tracks, not due | 2,686 | 2,111 |
| Eight live tracks, emitted notes | 4,965 | 4,582 |
| Twelve live tracks, emitted notes | 7,469 | 6,894 |
| Eight sequenced tracks, not due | 1,374 | 991 |

The eight-track live examples save 21.3% of callback instructions with no
emission and 7.7% during the shown emission block. Removing a division can
also change cycles per instruction, which this count does not quantify.
Hardware MAIN improvement remains to be measured with identical project,
pattern, core, display and meter settings. No device I/O is performed here.

## Additional pass: ARP 1.11

`hot.s` implements the unchanged-held-set exit as a leaf C-ABI function. It
compares the same four native bitmap words with the private selector, returns
the held count, and touches no stack or callee-saved register. When a word
differs it tail-jumps to the checked C list traversal/compaction worker.
Native exclusion keeps the bitmap stable throughout both halves. Static
assertions pin the two structure offsets used by assembly. The host C entry
retains the same semantics for host regression builds.

The worker no longer copies the old bitmap to a second stack array: its
private selector bitmap stays unchanged until the final commit, so it can
read those same old words directly. This also removes a three-instruction
regression seen in some changed-set cases in the first prototype. In the
accepted candidate none of the 193 direct keyboard comparisons increases.
The unchanged path goes from 48 instructions to 16; changed/invalid cases
range from 50..9,743 before to 49..9,116 after. These are ranges over different
cases, not one uniform percentage. Reordered lists with identical masks retain
the original fast-exit behavior, including not reading an invalid head.

The clock-poll body now has one shared inline implementation in `clock.h`.
Its exported standalone wrapper compiles identically, while `arp_runtime_frame`
folds the poll into its own code instead of setting up a five-argument call
on every block. Generation checks, signed wrap arithmetic, late recovery,
exclusive end and clock writes remain exact. Finally, the eight checks for
reused queued-event pointers use fixed offsets instead of a pointer/end loop;
all eight occurrences are still checked and cleared before remembering output.

| Case | ARP 1.10 | ARP 1.11 |
|---|---:|---:|
| No ARP running | 8 | 8 |
| One live track, not due | 197 | 151 |
| Eight live tracks, not due | 1,415 | 1,047 |
| Twelve live tracks, not due | 2,111 | 1,559 |
| Eight live tracks, emitted notes | 4,582 | 4,054 |
| Twelve live tracks, emitted notes | 6,894 | 6,102 |
| Eight sequenced tracks, not due | 991 | 879 |
| Eight sequenced tracks, emitted notes | 4,730 | 4,458 |

The shown eight-track live callback saves an additional 26.0% without emission
and 11.5% when emitting, relative to 1.10. No physical MAIN percentage is
inferred. Inlining/unrolling increases renderer code size (runtime frame:
554 to 738 bytes); instruction improvements do not quantify instruction-cache
effects on the device. The new keyboard assembly is 56 bytes.

With `--changing-keys`, the main differential adds five scenarios in which a
changed native held snapshot reaches the callback while locks/tempo change:
45 scenarios and 330 callbacks per bank, all non-increasing. Other scenarios
still use the original UI producers. `tools/bench_st_arp_keyboard.py` separately
compares 193 state/ABI cases across all bitmap-word boundaries, random held
sets, all 128 notes, insertion-order changes and malformed/cyclic lists. It
checks native memory remains read-only, rejection leaves selector bytes intact,
and each candidate call executes no more instructions than its reference.
A deliberately inconsistent bitmap/list is rejected as its negative control.

Only `runtime.c`, `keyboard.c` and the new assembly object change among compiled
objects relative to 1.10. The full common regression and linked generator,
keyboard, multi-track, locks, recording, view and persistence checks pass.
The optional external FM-source comparison remains skipped. AUDIO, core and
the compiled sampler/LFO3/other-mod objects stay byte-identical.

### EMAC and SRAM review

This work consists of comparisons, bitmaps, clocks and event ownership, with
no multiply-accumulate chain to fuse. Track-index arithmetic already uses
integer hardware multiplication; introducing fractional EMAC would require
managing its live context without removing an accumulation loop. The accepted
code adds no EMAC instruction and preserves its state by construction.

No additional SRAM is claimed. The documented MAIN windows belong to LFO3
and Chorus, with the tested sampler optimization borrowing Chorus's ranges
only when Chorus is absent. Padding inside an owner's reservation is not a
separate free allocation. Moving this 56-byte leaf there would require a
new placement/ownership contract and physical cache/timing evidence; the
present candidate keeps the validated sampler/LFO3 placement intact.

## Selector and running-track scan: ARP 1.12

The new `selector.s` is authored integer code for the existing private selector
ABI, not an extracted firmware routine. `selector.c` retains reset/hold/release
and the portable C selection implementation for host builds; the target uses
the assembly entry. Compile-time assertions pin every private state offset
used by assembly. The target selector, including its octave helper, occupies
562 bytes instead of the previous C function's 1,808 bytes (68.9% smaller).

The common path advances the step and rejects rests/empty/stale cases before
creating a saved-register frame. UP/DOWN share a four-stage bitmap scan;
the fixed stages remove the sparse one-note regression found in the initial
loop-based prototype. CYCL retains the bounded eight-word search and single
retry that avoids repeating an endpoint. Direction, cursor, octave, last note,
remaining bitmap and all other selector bytes remain exact, including changes
of mode and range while running. ColdFire register shifts by 32 produce the
same zero required by the portable C shift helpers.

ISA_C `BITREV` followed by `FF1` finds the least significant set bit directly.
It replaces isolation/leading-count/subtraction in both ascending note choice
and the running-track scan, preserving ascending track order. It needs no
new condition: both callers already establish that the bitmap is nonzero.
No EMAC instruction, new SRAM range or persistent state is introduced.

An alternative that hoisted the frame-end calculation saved steady work but
added two instructions to some Kit/pattern/transport transitions; it was
rejected. A local runtime-pointer refactor alone compiles identically to 1.11.
The accepted track improvement comes from the ISA_C bit scan.

| Case | ARP 1.11 | ARP 1.12 |
|---|---:|---:|
| No ARP running | 8 | 8 |
| One live track, not due | 151 | 148 |
| Eight live tracks, not due | 1,047 | 1,023 |
| Twelve live tracks, not due | 1,559 | 1,523 |
| Eight live tracks, emitted notes | 4,054 | 3,926 |
| Twelve live tracks, emitted notes | 6,102 | 5,901 |

These examples save an additional 2.3% / 3.2% of the eight-track callback,
not of total MAIN CPU. Code-size reduction can affect physical cache behavior,
which the instruction counter does not model. Hardware validation is pending.

`tools/bench_st_arp_selector.py` checks the actual linked before/after functions
over 1,260 configurations: five modes, seven rising/falling ranges, six held
sets including all 128 notes and every bitmap boundary, three enabled masks,
two lengths and randomized offsets. Each has 32 steps and a stale-generation
call; another 1,024 steps change modes, ranges, held notes, rests, lengths
(including invalid lengths) and offsets without resetting history. All 42,604
calls compare the returned note, every selector byte, immutable settings and
the C ABI. Every individual call must execute no more instructions than 1.11.
A deliberate remaining-bitmap corruption is detected by the state comparison.

The existing Digitone oracle builds the production assembly with the same
toolchain flags and keeps all its original acceptance checks: 360 configs /
11,520 output comparisons plus 2,772 clock polls. It covers native ascending
ranges; falling-range extensions are checked against 1.11's complete state.
Source hashes now include the assembly actually executed.

Whole-callback checks retain 45 scenarios / 330 blocks and add 24 transitions
with `--transitions`: 1/8/12 tracks at unsupported tempos, pattern/Kit pointer
changes, MIDI/group ownership, empty held sets and stopped sequence transport.
`tools/bench_st_arp_tracks.py` adds twelve live/sequence scenarios / 60 blocks
with noncontiguous tracks. All 414 total callback/transition comparisons
retain complete state, event order, native note effects and balanced pools,
and have no instruction increases. The 414 standalone clock comparisons
are separate and are not included in that callback count.

Only ARP's module/selector C objects change and its selector assembly is added.
Every other mod object, AUDIO and core remain byte-identical. The new assembly
uses distinct internal labels to avoid collisions with UI symbols in tools
that inspect local ELF names; the UI regression remains unchanged.

## Reproduce

Use the unchanged toolchain/flags from SYNTAKT-BUILD.md and build both banks
with their matching sibling `.work/main.elf` and `.smz4.json` files. The core
folder must contain `main.bin` and `verification.json`. Set `LIBUNICORN_PATH`
as documented there, then run:

```powershell
python tools/bench_st_arp_main.py --core <core-folder>/main.bin `
  --before <reference-bank.smz4> --after <candidate-bank.smz4> `
  --out out/<new-report.json>

# ARP 1.11: also exercise changed native held snapshots at callback entry.
python tools/bench_st_arp_main.py --core <core-folder>/main.bin `
  --before <reference-bank.smz4> --after <candidate-bank.smz4> `
  --changing-keys --out out/<new-extended-report.json>

python tools/bench_st_arp_keyboard.py --core <core-folder>/main.bin `
  --before <reference-bank.smz4> --after <candidate-bank.smz4> `
  --out out/<new-keyboard-report.json>

# ARP 1.12: selector histories and callback stop/reconfiguration transitions.
python tools/bench_st_arp_selector.py --core <core-folder>/main.bin `
  --before <reference-bank.smz4> --after <candidate-bank.smz4> `
  --out out/<new-selector-report.json>

python tools/bench_st_arp_main.py --core <core-folder>/main.bin `
  --before <reference-bank.smz4> --after <candidate-bank.smz4> `
  --changing-keys --transitions --out out/<new-callback-report.json>

python tools/bench_st_arp_tracks.py --core <core-folder>/main.bin `
  --before <reference-bank.smz4> --after <candidate-bank.smz4> `
  --out out/<new-sparse-track-report.json>
```

Also run common bank regressions, the linked ARP generator, keyboard,
multi-track, view/key, lock integration, recording and persistence checks.
Keep the optional external FM-source comparison explicitly reported as
skipped when its reference is unavailable. Verify the user's known-good
bank AUDIO and complete core SYX with `tools/check_st_stereo_audio.py`.
Generated firmware, execution images and reports stay in ignored `out/`.
