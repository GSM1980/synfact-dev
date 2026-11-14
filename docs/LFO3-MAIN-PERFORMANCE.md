# LFO3 MAIN measurement and optimizations

Release versions and development labels: see [the consolidated MAIN release](MAIN-OPTIMIZATION-RELEASE.md). Numbers in the measurements below identify historical development iterations.

## Current variant: LFO3 1.7, inactive native operation removed

Hardware result: the owner measured the complete bank at **84% MAIN**,
against 86% before this change with core 1.8.1.2 and the same stock pattern.
The two points indicated by the V/W diagnostic are recovered in this test.

2026-11-13. The owner's same-layout V/W comparison (84%/82%) identified
the LFO3 native callback/route as a material idle cost. This variant uses
the existing core 1.8.1.2 unchanged. It turns off its LFO operation interest
when every effective DEP/DEST is silent, while retaining page operations
and all global hooks. There is no new per-block guard: the existing core
byte bypasses the callback, overlay and engine entirely.

One DDR activity word is maintained only on changed parameter publication
and DEP/DEST-lock apply/clear, with IRQ-safe first/last transitions. Reset/
CRC fallback rebuild once. Other native subscribers are retained. ARP-only
locks do not wake LFO3; potentially valid locked destinations retain the
existing metadata validation in the overlay. The accepted phase/fade/RANDOM
suspension policy, renderer, SRAM bank and AUDIO are unchanged.

Real-core routing checks: 855 publication/lock/transition cases, including
same-next-block activation, FX/static locks, editor/republish, nested busy,
CRC fallback and POLY coexistence. Entry at an explicit DSP-body boundary:
inactive 137 -> 6 instructions; active/fine-depth 137 -> 137. The separate
analog-emu renderer differential has 80 unchanged matrix counts/results.
Empty apply improves 104 -> 103; clear/restore improve 46/48 -> 45/47.

The event tradeoff is explicit. Changed base rows add 61–66 instructions in
the publication fixture; first/last transitions add 834/826 for the existing
core route rebuild. Two transitions followed by sixteen idle blocks still
save instructions with the renderer excluded: 2,691 -> 2,255. The event-copy
oracle separates bounded cold updates from the original no-increase gate
on unrelated calls. These instruction counts do not predict a physical
percentage; the observed hardware result above applies to that stock pattern.

## Previous variant: LFO3 1.6, suspension without output

2026-11-12. Uses the hardware-tested core 1.8.1.1 unchanged, with all earlier
sampler/ARP/POLY MAIN optimizations. Baseline for this comparison is LFO3 1.5.
The owner selected suspension of any engine without output, accepting changes
to phase, fades, triggers and RANDOM. This is an explicit behavior change:
DEP exactly `4000`, or DEST outside 1..70, suspends that engine completely.
State freezes and triggers are ignored; resuming FREE does not catch up.
Shared RNG draws are omitted, which can also alter active RANDOM sequences.
The effective parameters are read after p-lock overlay, without a cache.
Nonzero fine depth `4001` is not treated as neutral.

The inline assembly guard is part of the existing SRAM copy. With default
neutral depth it takes three instructions to skip an engine; another four
test destination only when needed. The old running-default specialization
and its per-block tempo multiplication are removed. Register allocation also
removes a state pointer/increment, repeated depth preparation, redundant MODE
copies and two stack accesses around non-RANDOM waveform helpers. The
trigger/loop sentinel stays in a callee-saved register. No extra allocation,
activity bitmap, publication work or SRAM reservation is introduced.
Kernel: 1,072 to 822 bytes of the same 1,152-byte window. State/parameters:
944 bytes. Existing CRC checks, DDR fallback and Chorus neighbours remain.

Complete steady callback counts, including guard and argument wrapper:

| Scenario | LFO3 1.5 | LFO3 1.6 | Reduction |
|---|---:|---:|---:|
| All engines at defaults | 878 | 196 | 77.7% |
| Silent nondefaults | 2,314 | 196 | 91.5% |
| 1 active TRI | 986 | 355 | 64.0% |
| 4 active TRI | 1,312 | 834 | 36.4% |
| All 13 active TRI | 2,288 | 2,269 | 0.8% |
| All 13 active RANDOM | 2,340 | 2,321 | 0.8% |
| All tracks locked | 2,967 | 2,948 | 0.6% |

All 80 matrix rows (DDR and SRAM), first and subsequent blocks, pass the
no-count-increase gate separately for callback, selector and kernel. The
seven waves all improve with 13 active engines. These counts are not CPU
MAIN points or hardware cycles; cache/interrupt scheduling latency is absent.

The historical full-state before/after oracle remains unchanged for versions
with identical behavior. It proves the independent loop-register change
against 1.5 before suspension is added. For 1.6, `--idle-policy` selects
`tests/st_lfo3_idle_cfbench.c`: its separate reference is the old linked kernel
with only its uniquely pinned loop count changed from 13 to 1 in an ignored
test image. This reference first has to match the untouched full old kernel
for 280 all-active blocks across waves/modes and every state/work/RNG/physical
EMAC byte. It then calls that original engine, in order, only for eligible
engines. The real new full callback must match that policy reference.
No delivered bank is patched by the test.

Nine checks cover that reference validation, the candidate versus native LFO2,
560 continuous suspend/resume blocks, 280 SRAM/neighbour blocks, 280 phase/fade/
tempo boundaries, 256 control-bit cases, 1,792 destination/depth cases, 512
lock-mask cases and a deliberately corrupted state that must be detected.
The actual callbacks retain the C ABI. Publication/setter tasks keep identical
results and counts. ARP/LFO3 lock integration and 120 POLY/native pipeline
cases (9,600 blocks, including LFO1/2/3) also pass. Common regressions include
startup after clearing nonresident storage; 83 checks pass, with one optional
external FM comparison skipped. Unit tests: 39 pass. Only the LFO3 renderer
object changes relative to the preceding bank; core and bank AUDIO are exact.

Reproduce with the documented compiler flags/environment:

```powershell
python tools/bench_st_lfo3_main.py --core <core-1.8.1.1-build> `
  --before <LFO3-1.5-bank.smz4> --after <LFO3-1.6-bank.smz4> `
  --cfbench $env:CFBENCH --out out/<new-measurement-folder> `
  --idle-policy --require-no-count-regressions
```

The preceding bank SHA256 is
`d7f76be7b03e053a75afd31f5f03318699c20563a99b9ac81a679247ef07a80c`;
this bank is
`e18aa8589beb3db0f80b02ce2210a7f829275a455b4f0959ec388ffb9a7650c7`.
Core SYX remains
`b22737f44a16554ce4c2cd8f3e5080eb75da71af5331953080ea0eb880c77de2`.
Hardware results for this new bank remain pending.

## Historical 1.2 to 1.4 baseline

2026-11-12. Branch `perf/lfo3-main`, base main
`e4af43a3a522846aba224a181d5813e42f5dd827`, with the owner's tested sampler
1.4.5 source changes inherited intact. Core 1.8.0, AUDIO and the other mods
remain unchanged. LFO3 baseline is 1.2; lock preparation is 1.3 and the
additional renderer optimization is 1.4.

## What is measured

`tools/bench_st_lfo3_main.py` relocates the actual two SMZ4 banks and executes
their linked code in analog-emu/cfbench, together with the core's real MAIN
image and relocated resident section. Synthetic inputs supply parameters,
events, tempo and trigs; no device I/O, boot or native scheduling occurs.

It measures the complete LFO3 native callback, the parameter/lock selector,
and the renderer in independent runs. Each measured stage includes a fixed
small C argument wrapper. Preparation, data resets, EMAC setup, relocation
and correctness checks are excluded from those stage counts. The three
counts must not be added: the complete callback already includes the others.

The full matrix contains 40 scenarios in DDR and SRAM (80 rows), four
successive blocks each: defaults with DEST none/zero, silent nondefault
parameters, fine depth `4001`, 1/4/13 active engines across all seven waves,
additional trigger modes, an FX engine, 1/4/13 locked tracks, all eight locked
fields, changing locks/tempo/trigs, and the engine-busy exit. First and later
blocks are retained separately. No hardware latency difference between DDR
and SRAM is modeled, so equal instruction counts are expected.

The correctness fixture checks:

- The reference kernel against the real native LFO2 routine: all seven waves
  and five modes over eight frames, 3,640 individual third-LFO steps. Native
  LFO1 is at defaults; destinations do not target other LFO controls.
- 256 continuous before/after callback blocks with changing parameters,
  locks, activity and tempo, including zero-depth and fine-depth transitions.
  Another 128 blocks execute the relocated SRAM kernel/data while checking
  sentinels throughout the adjacent sampler SRAM windows.
- 280 boundary cases over phases, fade states, tempo signs and mode changes.
  MACSR uses the native MAIN IRQ's `20` mode; arbitrary non-native EMAC modes
  are not a supported caller contract for the firmware's waveform helpers.
- All 256 low lock masks with and without the eight upper ARP bits: 512
  cases, each covering all thirteen tracks.
- Another 256 renderer cases flip every bit of each default control, using
  all thirteen tracks, zero/none destinations, arbitrary phase/fade history
  and signed/overflowing tempo values. Candidate state matches the reference.
  The native TRI table target is checked against the supplied core image.
- Every working, engine-state, RNG, parameter and overlay byte, all physical
  EMAC registers, the callee-saved C ABI and stack. The renderer's state is
  allowed to change as its contract requires; before/after must match.
- A deliberate state corruption must fail the comparison (negative control).

Publication and setter costs use `tools/bench_st_lfo3_tasks.py`, reusing the
existing fixtures. Real bank code handles hashes, ownership, publication,
the native setter and its wrappers. Allocation, native copies, mutexes,
descriptors and model boundaries are explicit doubles. These are costs per
event/task operation, not work done on every audio block.

## Reference and first result

Instruction counts for the complete callback, including its argument wrapper:

| Synthetic case | LFO3 1.2 | LFO3 1.3 | LFO3 1.4 |
|---|---:|---:|---:|
| Default parameters, no locks | 1,087 | 1,087 | 930 |
| Defaults, DEST zero | 1,204 | 1,204 | 1,047 |
| Silent, nondefault parameters, after onset | 2,315 | 2,315 | 2,314 |
| Four active, one locked track, steady | 1,590 | 1,520 | 1,411 |
| Four active, four locked tracks, steady | 1,938 | 1,661 | 1,552 |
| Thirteen active/locked tracks, steady | 3,866 | 2,968 | 2,967 |
| Thirteen tracks, all eight fields locked, steady | 4,153 | 3,163 | 3,162 |
| Changing tempo/trigs, locks on that block | 4,404 | 3,507 | 3,506 |
| Engine busy | 38 | 38 | 38 |

The first optimization only exposes the eight lock fields as constant-offset,
constant-bit assignments instead of a dynamic eight-iteration loop. The
compiler eliminates its counter, variable bit shifts and loop branches.
There is no new cache, persistent state or additional condition per block.
For 1.3, DEST resolution, dirty-row logic and the renderer remain unchanged.
Only `locks_runtime.c` and the module version change within LFO3. Its engine
code remains 1,088 / 1,152 SRAM bytes; its bank remains 944 / 944 bytes.

In the measured thirteen-track examples, the complete callback executes
about 23–24% fewer instructions with locks. With no locks its count is
identical. This is not a percentage of total MAIN CPU and is not a physical
timing measurement. All measured stages/blocks must be no worse than the
reference when `--require-no-count-regressions` is used.

### Renderer 1.4

The exact-default path now shares its tempo-derived phase increment across
the block, using a previously unused word in the existing stack frame.
Adjacent default controls are checked as aligned 32-bit pairs, preserving
every fine-value bit. The private DDR bank is explicitly aligned to four
bytes; compile-time assertions cover both parameter arrays, their stride
and the SRAM address. No alignment guard is added at runtime.

The specialized TRI path calls the same pinned native helper directly,
without loading its known table entry. The thirteen-step loop uses one
backward conditional branch. Its final pointer advances touch no pointed-to
memory and its address registers are restored by the existing ABI epilogue.

There is no persistent cache, additional state, extra per-track condition,
phase freeze or reduced update rate. All engines still advance their phase,
fade, wave and RNG as before; fractional EMAC semantics remain unchanged.
The full nondefault kernel is retained. The shared multiply adds work even
when all thirteen engines take that full path; the loop change offsets it
in instruction counts (one fewer instruction per complete callback). This
does not establish physical cycle savings for that case.

With defaults, the complete callback executes 14.4% fewer instructions than
1.2/1.3. With four active/locked tracks it executes 19.9% fewer than 1.2,
and 6.6% fewer than 1.3. Fully active locked examples retain the first
optimization's 23–24% saving. Code is now 1,062 / 1,152 SRAM bytes, with the
same 944-byte bank and untouched sampler 1.4.5 SRAM windows. The compiled
`main.c` object is identical despite its explicit alignment and assertions;
only the renderer and lock-runtime objects differ from 1.2. Task/publication
counts and all other mod objects remain identical.

Task-side reference counts (unchanged by this candidate):

| Operation | Instructions |
|---|---:|
| Republish 13 live sources | 2,148 |
| Republish owned sources, warm lookup | 2,301 |
| Republish owned sources, cold lookup | 3,454 |
| Side-Kit hash validation, repeated | 150,864 |
| Stale side-Kit hash | 152,303 |
| Native lock edit, no sparse locks | 1,332 |
| New native lane, combined capacity check | 4,836 |

The side-Kit paths need attention for event spikes, but their native object
hashes protect against stale associations. They cannot simply be removed or
charged as a permanent per-block cost. The measurements do not establish how
often each path runs in an owner's real pattern.

## Reproduce

Use the unmodified build flags and environment in SYNTAKT-BUILD.md. Inputs
are build folders, owner-provided firmware and local tools; all generated
images, binaries, source hashes and reports stay in a new ignored `out/`
folder. No workstation path is built into a source file.

```powershell
python tools/bench_st_lfo3_main.py --core <core-build> `
  --before <reference-bank.smz4> --after <candidate-bank.smz4> `
  --cfbench $env:CFBENCH --out out/<new-measurement-folder> `
  --require-no-count-regressions

python tools/bench_st_lfo3_tasks.py --core <core-build> `
  --before <reference-bank.smz4> --after <candidate-bank.smz4> `
  --out out/<new-task-report.json>
```

Omit `--after` to establish a self-comparison baseline. Each bank needs its
matching `.smz4.json` and sibling `.work/main.elf`. Run common regressions and
the LFO3/ARP lock integration check before delivering a candidate. The optional
external FM reference comparison is reported as skipped when not supplied.

For hardware, keep the same core, project, pattern, display and meter
settings. Compare the reference and candidate with LFO3 defaults, an active
modulation pattern and a complex lock pattern. Allow 20 seconds to settle,
observe a minute, repeat three times, then return to the reference. Use CPU
MAIN's range and recurring maxima, not one isolated displayed percentage.
The owner alone installs and tests the banks. Candidate 1.4 physical results
are pending; no global CPU improvement is claimed from this offline work.
