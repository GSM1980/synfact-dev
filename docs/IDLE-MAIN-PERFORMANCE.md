# MAIN work with unused optional modules

Release versions and development labels: see [the consolidated MAIN release](MAIN-OPTIMIZATION-RELEASE.md). Numbers in the measurements below identify historical development iterations.

## Hardware Z1=83%, Z2=84%: sampler 1.4.9 gain commit

2026-11-13: the owner reports Z1=83%, Z2=84%, against the exact complete
bank at 84%. Removing stereo audio callbacks accounts for the visible point
in this split; removing Recorder audio does not change the meter. This is
specific to this stock pattern and does not mean Recorder costs zero.

The functional sampler 1.4.9 candidate keeps every module, core 1.8.1.2 and
identical AUDIO. It commits all four native right-gain rows together after
MAIN/DIRECT correction, instead of copying each row in its own callback.
All first-block primed flags commit there too. The current block continues
to use its previous ends; empty delay/reverb entries no longer copy or write
gain history. No type/activity scan or cache, decimation, extra per-track
guard or suspension policy is added. Late/torn/stale block pairing stays.

The empty MAIN commit is a 104-byte MOVEM assembly span copied after the
existing resolver in the already conditional Chorus SRAM window. State
1,236 + copy/widen 216 + resolver 556 + commit 104 = 2,112 bytes, exactly
the existing 0x800077c0..0x80008000 window. Bounds/zero/bank-directory checks
remain; no verified SRAM boundary is extended. Chorus, occupied windows or
insufficient space retain DDR. Integrity checking includes the added span;
corruption restores every pointer/state to DDR under the existing IRQ lock.
The active worker inlines the same commit in its saved frame.

The linked complete idle callback set measures 155 -> 125 untagged and
207 -> 177 tagged-empty instructions, including all new work. Recorder's
object is unchanged and its 142 transition comparisons stay exact, including
its 12-instruction disabled entry. The fixture requires equal Recorder counts
when its object hash is equal, retaining strict idle improvement for the
previous Recorder change. All state/PCM/bus comparisons remain exact.

Analog-emu compares five 384-block callback sequences, including 64 initial
empty blocks, first stereo entry, late/torn masks, dynamic gains and
DDR/SRAM/fallback/Chorus/occupied windows. Gain targets are now installed
immediately before their corresponding mix callback, modeling the existing
native kernel/event order instead of installing every row before render-in.
Every original bus/D/EMAC/ABI comparison is retained. Suppressing only the
empty history commit deliberately fails on subsequent stereo activation.
All complete stable/dynamic instruction budgets are non-increasing; the
typical eight-track active cases save another 15 instructions per block.

The standalone arbitrary-bus EMAC oracle retains add's original immediate
row postcondition through an inlined internal adapter, which is unused and
eliminated in the bank. SRAM tests additionally corrupt the new gain span
and require complete fallback. Hardware CPU improvement remains pending;
instruction counts are not cycles or CPU percentages.

Delivery validation passes: 87 common checks, including 39 unit tests;
142 Recorder transitions; the five analog-emu callback scenarios and missing
commit negative control; 400 independent EMAC/model cases; 4,096 expansion
blocks; all five SRAM placement/corruption cases; unchanged native AUDIO
comparisons and 24,000 sampler PCM blocks. The optional external FM source
comparison is skipped. Current source/object and delivery hashes match;
core MAIN/AUDIO/SYX and bank AUDIO remain byte-identical to 1.4.8.

## Hardware samplers 1.4.8 / Recorder 1.1.1 complete=84%: split Y

2026-11-13: the owner reports **84%** for the functional idle-audio candidate,
unchanged at the meter's resolution. The linked instruction reduction is
confirmed offline; no additional hardware CPU saving is demonstrated. The
combined Y cut previously measured 83%, but it used the preceding binary.

The next pair derives from the exact newly tested 84% bank, without relinking:

- **Z1:** cuts only STEREO's four void audio callbacks. Recorder remains live.
- **Z2:** cuts only Recorder's main-mix callback. STEREO remains live.

`build_st_idle_audio_cuts.py --set split` changes only those callback pointers
to the existing authored RTS and updates checksums. Source/object hashes and
the exact reference bank hash are checked first. Initializers, native masks,
UI/tick, executable bytes, positions, relocations, catalog and AUDIO stay
identical. These are stock-pattern diagnostic banks, not functional releases
for using the cut features. Keep core 1.8.1.2 and compare with the newly tested
complete bank at 84%, using the same pattern/tempo/meter screen after restart.

The independent test retains every directory/subscription/unchanged-byte and
parser mutation assertion, actual initialization/SRAM placement and MSKIP
checks. If neither isolated cut changes the meter, their individual costs may
still be below its resolution; the old combined result is not an exact sum
or proof of a single dominant routine. Hardware Z1/Z2 readings are pending.

Offline validation passes for the reference and both split banks: untagged /
tagged-empty callback counts are 155/207, Z1 62/62 and Z2 144/196. These
counts exclude dispatch/native DSP timing and predict no CPU percentage.
Native masks and stock bus bytes agree; stereo SRAM placement succeeds;
each bank skips 124/128 synthetic MSKIP blocks with zero errors. The unchanged
parser/mutation checks and 39 unit tests pass. Delivery copies match the
validated bank hashes; no core, production source or AUDIO changes.

## Hardware X=84%, Y=83%: retain gain history with smaller idle frames

2026-11-13: the owner reports X=84%, Y=83%, against the complete LFO3 1.7
bank at 84%. Cutting POLY's frame sync is not visible at the meter's
resolution. Cutting stereo and Recorder audio callbacks together saves about
one point in this pattern; this does not split that point between them.

The functional candidate keeps all modules, core 1.8.1.2 and identical AUDIO.
Samplers 1.4.8 outlines the specialized nonempty stereo workers, keeping their
large EMAC register/stack frames out of the empty path. It still resolves a
late AUDIO publication at the first mix callback and remembers all four gain
rows on every block, preserving the first stereo ramp. MAIN/DIRECT share the
same already-resolved mask. The active workers copy each eight-word gain row
with two MOVEMs using their saved registers; the empty leaf copy keeps its
d0/d1-only form. No per-track activity cache, gain comparison or SRAM claim
is added. Existing Chorus/occupied-window fallback and EMAC ownership stay.

Recorder 1.1.1 performs its existing disabled-view clock/phase maintenance in
the small main callback and enters the capture worker only when enabled.
Audition still runs independently of the view and only enters its worker when
the published playback length is nonzero. Capture/normalization/monitor/UI
workers, including the inherited capture source, are unchanged.

The linked callback fixture measures 187 -> 155 untagged and 240 -> 207
tagged-empty instructions for the full audio subscriber set. Recorder alone
is 30 -> 12 when closed without audition. It checks 142 exact Recorder
transitions including clock wrap, mono/stereo audition while closed,
MAIN capture/threshold/ready/normalizing, invalid source and synchronous view
closure. Native metadata, heap/mutex and original LFO DSP are boundaries.

Analog-emu compares 384 blocks in each DDR/SRAM/fallback/Chorus/occupied-window
scenario, including late/torn/empty masks and exact buses, D, EMAC and ABI.
All measured complete stable/dynamic stereo blocks are non-increasing; the
eight-track active cases save 9 instructions per complete four-callback
block. Individual delay/reverb entries add 2/3 instructions there, outweighed
by MAIN/DIRECT savings. These are instruction counts, not physical cycles or
a promise to recover Y's whole CPU point. The owner reports 84%, unchanged
at the meter's resolution, as recorded above.

Delivery validation: 87 common checks pass, including 39 unit tests and the
real startup/native routes/LFO3/MSKIP/event/activity checks. The optional
external FM source comparison is skipped. The separate linked Recorder/idle
and analog-emu stereo differentials pass. Source/object hashes match; core
MAIN/AUDIO/SYX and bank AUDIO match the preceding hardware-tested release.

## Hardware LFO3 1.7 complete=84%: two points recovered

2026-11-13: the owner reports **84%** with the complete LFO3 1.7 bank,
down from 86% with the preceding complete bank on the same core/pattern.
This recovers the two points indicated by the V/W diagnostic. The minimal
A reference remains 81%, so about three points remain overall; the earlier
all-services-disabled S reading of 82% retained catalog/framework/list costs.
The group/context effects remain approximate rather than additive cycles.

The next diagnostics are derived from the exact hardware-tested production
bank, not a newly linked reference. `build_st_idle_audio_cuts.py` verifies
its source/object hashes and ELF and reuses System Info's authored empty
void callback (`si_change`, exactly one RTS). It changes only registered
void audio callback pointers and established checksums. Executable bytes,
offsets, relocations, catalog/AUDIO, all initializers, UI/tick subscriptions
and native-operation masks remain identical. No new stub or callback is
added; the existing no-op is safe only for these known void signatures.

- **X:** replaces POLY's render-in frame synchronization callback only.
  Native TRIG/header/configuration/UI paths are retained.
- **Y:** replaces stereo receive/delay/reverb/main audio callbacks and
  Recorder's main-mix callback. Their initializers, tick/UI and SRAM
  placement remain enabled.

These are stock-pattern diagnostics. Their feature pages still appear;
they are not functional substitutes for exercising POLY/stereo/recording.
Keep core 1.8.1.2, the same pattern/tempo/meter screen and restart each time.
Compare each to the current complete-bank 84% reading. This isolates these
per-block callbacks, not every operation belonging to the modules; ARP,
other native/UI work and the retained catalog are not removed.

The independent check compares every directory/subscription field against
the reference and executes actual LFO3/ARP/POLY/stereo/recorder/MSKIP init,
real stereo SRAM placement, a warmed stock POLY sequence/Kit cache and
four untagged/tagged-empty callback blocks per bank. Native masks match
exactly; stock bus bytes remain unchanged; each bank still skips 124/128
synthetic modulation blocks with zero errors. Callback counts in the
fixture are reference 187/240, X 150/203, Y 51/51 (untagged/tagged-empty).
These are not cycles or hardware CPU predictions. Heap/mutex/metadata and
original DSP/event bodies are explicit boundaries. Hardware readings are
X=84%, Y=83%, as recorded above.

## Hardware V=84%, W=82%: LFO3 1.7 production change

2026-11-13. The owner measured V=84%, W=82%. This puts approximately two
points in the LFO3 native route/callback in that stock pattern; W retaining
global hooks matches S=82% at the meter's resolution. It does not identify
oscillator arithmetic alone, because W also removes dispatch/overlay and
native page callbacks. U=85% versus V=84% puts approximately one point in
adding ARP in that context. The earlier groups remain nonadditive.

The new complete production bank keeps all modules and uses core 1.8.1.2
and byte-identical AUDIO. LFO3 1.7 publishes zero LFO-operation interest
when no effective DEP/DEST row can produce output; page callbacks/global
hooks remain available. One DDR word is maintained at changed base parameter
publication and DEP/DEST lock apply/clear. Its update and first/last route
publication are IRQ-safe through existing APIs. It adds no per-block check
and changes neither SRAM sizes nor the accepted suspension policy.

The target test covers 855 transition cases including FX/static first-step
locks, fine depth, edit/republish, nested busy, reset/CRC fallback and POLY
interest coexistence, with a missed-activation negative control. At explicit
DSP boundaries the inactive native entry is 137 -> 6 instructions; active
entry stays 137. The real analog-emu renderer/EMAC matrix stays exact with
80 unchanged count rows. No-output event paths do not get more expensive.

The moved event work is separately charged: 61–66 extra instructions for
a changed base publication without a route transition in the fixture;
834/826 for first/last output transitions. Both transitions plus sixteen
idle blocks cost 2,691 -> 2,255 even with renderer execution excluded. The
event-copy test retains strict non-increase outside the 16 explicit cold
DEP/DEST updates (max extra 92, budget 128 with no host/router); the real
core route costs are measured separately. This is an amortized idle change,
not an assertion that every event becomes cheaper.

Validation: 87 common checks pass (optional external FM comparison omitted),
39 units, linked event/owner oracle, real-core activity test, analog-emu
renderer, shared ARP locks/persistence and POLY copies. The ARP persistence
fixture now locates LFO3's local host by its ELF file/unique DDR bank rather
than unstable symbol ordering; its actual codec assertions remain intact.
Hardware CPU improvement of this new complete bank remains pending.

## Hardware T=84%, U=85%: isolate LFO3's periodic path

2026-11-13: the owner reports **T 84%, U 85%**, alongside R=86%, S=82%,
minimal A=81%. LFO3/ARP alone adds about three points over S; the other
services alone add about two. With everything enabled the total adds four.
Integer rounding/reading variability and runtime interactions can account
for the one-point nonadditivity; this is not evidence for an exact cycle
sum. Removing LFO3/ARP from the complete bank saves two observed points;
removing the other services saves one. The LFO3/ARP group is the next
priority, but its exact function/frequency cost is still unknown.

The `--set lfo` option of `split_st_idle_bisect.py` produces another pair
from the same original, source/object-verified R without rebuilding code:

- **V:** keeps LFO3 plus System Info/Modulation Skip; ARP and the other
  optional services are disabled. Comparing V to U isolates adding ARP
  while retaining the shared LFO3 lock/metadata backend.
- **W:** same as V, but replaces only LFO3's `ST_EV_NATIVE` callback with
  the existing diagnostic no-op. LFO3 initialization, global native hooks,
  parameter provider, shared lock backend, SRAM bank/kernel and UI tick/draw
  subscriptions remain present. The no-op's registered zero interest means
  no native LFO3 operations (including LFO/page sync/navigation/layout) are
  dispatched. Comparing W to V isolates that callback/route work, including
  its once-per-block renderer/overlay, not just oscillator arithmetic.

If W still exceeds S substantially, LFO3 global hooks/services remain the
priority; if W drops towards S, its native callback/periodic path becomes
the priority. This is a stock-pattern diagnostic: W cannot supply normal
LFO3 modulation or its native page callback behavior. It is not a functional
performance release. Neither result should be predicted from emulator
instructions alone.

The diagnostic transformer can now disable a selected subscription while
preserving its initializer. The independent target check requires that
exact initializer and every unchanged subscription/data byte, plus real
installed LFO3 hooks/SRAM in both V/W, two native-mask slots, the LFO route
present only in V, all native routes zero in W, ARP/POLY off and MSKIP ON.
Each still has 124 skipped/128 synthetic modulation blocks with no errors.
All earlier R/S/T/U checks and negative controls continue to pass; V/W
retain the same code/relocations/layout/AUDIO. Hardware readings are pending.

## Hardware R=86%, S=82%: split the remaining services

2026-11-13: the owner measured **R 86%, S 82%**, with the same core/pattern.
Compared with the minimal A=81% reference, approximately four of the five
points disappear when the optional initializers/services are disabled;
approximately one remains in the retained catalog/framework/list/AUDIO
combination. Integer meter rounding and runtime memory effects still apply.
This controls executable-layout/AUDIO differences between R and S. It does
not yet identify a particular function or prove that all four points can be
removed while preserving enabled features.

The absence of LFO3/ARP/POLY pages in S is expected: their initialization
and callbacks are disabled. Their code, module records and file layout
remain present for the comparison. S is not a functional complete bank.

The next complementary split is generated from **that exact, already
hardware-tested R image**, after verifying its source/object hashes and ELF;
no code is recompiled or moved:

| Variant | LFO3/ARP services | Other optional services |
|---|---|---|
| R (86%) | On | On |
| S (82%) | Off | Off |
| T | Off | On, including POLY |
| U | On | Off, including POLY/stereo/recorder/manager |

System Info and Modulation Skip remain on in all variants. Compare T/U
against S to isolate each group's cost on the same catalog/list/AUDIO base;
compare to R to detect interactions. Do not assume their differences add
linearly. The tool is `tools/split_st_idle_bisect.py`; these are diagnostic
banks, not new optimizations.

The existing target test accepts optional `--variant` paths. For T/U it
independently checks every directory field, subscription event and callback
against R, allowing only the known disabled initializer/callback pointers
and checksums to differ. It executes actual packed-directory startup with
the enabled LFO3/ARP/POLY initializers, confirms two/three native-mask slots
respectively, the correct LFO/ARP routes, no inactive modulation replacement,
and MSKIP ON with 124 skips/128 synthetic blocks and zero errors. R/S and
their negative controls still pass. Native heap/mutex/other initializers
remain boundaries; no full boot/hardware timing is claimed.

## Five hardware points remain: interpretation and controlled diagnostic

2026-11-13: the owner reports **86% again** with LFO3 1.6.1/samplers 1.4.7.
The instruction savings are real in their fixtures but did not produce a
visible decrease for this pattern. A=81%, B=84%, complete=86% remains an
empirical grouping, not a measured sum of per-module cycle costs. Removing
modules changes layout and, for A, AUDIO; the earlier grouping cannot rule
out cache/layout or catalog/framework effects.

Five sustained percentage points mean about 50 ms extra busy time per
second. If distributed over all 1,500 32-sample blocks at 48 kHz, that is
33.3 microseconds per block (two points 13.3 us, three 20 us). These are
equivalent budgets, not proof that the work occurs once per audio block.
The isolated integer counts omit actual call frequency, DDR/cache/MMIO
latency, complete native bodies and scheduler/UI work. They do not yet
account quantitatively for the hardware gap.

An outstanding candidate is LFO3's global memcpy wrapper: every unmasked
task copy of at least 142 bytes runs `lfo3_after_copy` and `lfo3_republish`,
even when the copy is unrelated to an active LFO. Republish revisits up to
13 source records and may inspect owned/hash associations. Actual frequency
is unknown. The machine catalog also sets `core_machine_active`, changing
query adapters and transport/UI guards even with all track types stock.
Neither hypothesis is an attributed CPU measurement.

`tools/build_st_idle_bisect.py` builds two **diagnostic-only** banks from
the current local sources. R is complete; S keeps the same catalog/AUDIO
and code but disables optional initializers and routes their callbacks to
an authored C-ABI no-op. System Info/Modulation Skip remain enabled. Both
have identical executable bytes, MAIN code/data offsets, relocation table,
file size, directory/list lengths and mod-info directory. Only initializer
and callback pointers in existing metadata and the established checksums
differ. Runtime initialization/allocation/cache behavior can still differ.

S intentionally retains the cost of calling empty callbacks; it measures
the remaining catalog/framework/list overhead together. The native no-op
registers zero interest so it cannot turn on fail-open dispatch. A tiny
diagnostic initializer reserves the same slot in R and S; real initialization
confirms R fits all four slots with LFO3/ARP/POLY. No new firmware hook,
core, SRAM, timer instrumentation or periodic callback is added. S is only
for stock patterns: the optional features are disabled despite remaining
listed in System Info.

First verify R still gives the complete-bank reading, then measure S after
a restart with the same core/pattern/tempo/meter screen. A drop toward 81%
would put most overhead in the disabled hooks/services; a substantial
remainder would point toward the retained catalog/framework/list/AUDIO
and its timing effects. Neither outcome attributes a single function.
The next split/profile should follow that result.

Validation uses the unmodified C parser (including invalid-metadata
mutations), actual packed module directory, resident binder after native
nonresident clearing, actual diagnostic/LFO3/ARP/POLY/MSKIP initializers,
native routing/slot checks and 128 zero-depth modulation blocks per bank
(124 skips, no errors). Negative controls expose a missing zero-interest
registration and reject a corrupt checksum. Heap/mutex and the remaining
initializers are explicit boundaries; full boot and device timing remain
unmeasured. Production composition/source checks are not weakened to
accept these special diagnostic variants. Unit tests: 39 passed.

## Same-core hardware comparison and next bank (2026-11-13)

The owner now reports the same stock pattern/core 1.8.1.2 at **A 81%, B 84%,
complete C 86%**. This puts approximately two points in the LFO3/ARP group
and three in the other additions. Group removal also changes bank layout
and, for A, the AUDIO catalog; this is not a per-function attribution or a
guarantee that savings add linearly. Core 1.8.1.2 showed no visible decrease
relative to the preceding core in this pattern.

The next complete bank changes MAIN only: LFO3 1.6.1 and samplers 1.4.7.
It uses that exact same core, and its bank AUDIO is byte-identical to C.
LFO3's converter now resolves the first matching row in one owner traversal
instead of up to thirteen complete lookups. Track and owner precedence,
legacy-only mode, wrapping candidate subtraction and static PLAY-start
locks remain exact. Fixed 16/8-halfword copies remove loop bookkeeping;
zero-mask payloads are still copied. No activity manager, new cached state,
SRAM space or per-block test is introduced. The generator reproduces the
changed sources from the pinned donor. Empty per-call costs are:

| Operation | Previous | Next |
|---|---:|---:|
| Convert track 1 | 121 | 109 |
| Convert track 5 | 605 | 196 |
| Convert track 9 | 1,089 | 196 |
| Convert FX | 1,573 | 196 |
| Apply empty extra locks | 126 | 104 |
| Read ARP extra locks | 67 | 53 |

The new differential executes the previous bank as the ownership oracle:
1,512 owner cases and 188 complete event/copy cases, including real active
sparse lanes, allocator-owned patterns, static locks and aliasing. All
measured event cases are exact and non-increasing. Native event bodies,
allocator and mutex are explicit boundaries, not a complete IRQ model.

The stereo mixer specializes its four constant bus/gain/row call sites.
Inlining removes the generic call frame and repeated argument/index work,
without another guard. The empty, untagged integer fixture's stereo stages
fall from 223 to 164 instructions per block (receive unchanged at 41).
Analog-emu's physical-EMAC interpreter checks 384 callback blocks in each
of DDR, SRAM/fallback, Chorus, occupied-state and occupied-code scenarios.
Every measured stable
and dynamic count decreases; active eight-track stable mixing falls from
5,764 to 5,683 instructions in the full callback fixture. It preserves all
four gain histories and exact first ramps. Bank growth is about 4 KiB;
SRAM kernel/state sizes are unchanged. Instructions are not hardware
cycles or a prediction of the owner's MAIN percentage.

Validation: common regressions include the new linked event differential;
shared ARP locks/persistence and POLY native/shared-lock copies are checked
separately. Hardware CPU/behavior of the next bank remains for the owner.

## Remaining gap: audit and controlled banks

2026-11-12. The owner still sees a substantial difference between a stock
pattern with the complete bank (86% before the latest core test) and the
earlier System Info/Modulation Skip bank (81–82%). Those readings are not
yet a same-core attribution. CPU M measures all non-idle MAIN work, including
UI/task work and the audio IRQ; an oscillator-only instruction benchmark
cannot account for that complete difference.

Additional confirmed work is in LFO3's shared lock backend, which also serves
ARP. These hooks remain installed with neutral DEP/NONE destinations and
are entered by ordinary native events. `lfo3_events_convert` tries up to
13 candidate matrix bases through `lfo3_owned_lookup` to recover the track.
It does that even when the extra-lock store is empty. Applying an empty
extra-lock row still copies 16 halfwords; ARP's reader copies eight even
when its returned mask is zero. Native restoration/clear wrappers also
maintain the publication busy counter and active masks.

New `bench_st_lfo3_events.py` measures the real compiled bookkeeping with
constant native-body boundaries and the actual empty store/queue:

| Operation | Instructions per call |
|---|---:|
| Clear active extra locks | 46 |
| Restore track | 48 |
| Restore FX | 35 |
| Apply empty extra-lock row | 126 |
| Read empty ARP extra locks | 67 |
| Convert empty row, track 1 | 121 |
| Convert empty row, track 5 | 605 |
| Convert empty row, track 9 | 1,089 |
| Convert empty row, FX | 1,573 |

These are per event/call, not 1,500 calls per second by assumption. The
native bodies are doubles, not part of the measured DSP. This proves extra
work and the repeated lookup; it does not assign hardware MAIN points to it.
Priority candidates are deriving the row owner/track once, constant-size
copies without loop bookkeeping, and consolidating the privilege/size
filters on LFO3's global memcpy entry. No new state cache is required for
those candidates. Lifetime, shared ARP locks, native first-step static lock
blocks and busy-counter semantics must be retained.

Existing per-block measurements also remain nonzero: stereo receive and its
four buses cost 41 + 67 + 38 + 77 = 223 instructions in the untagged/empty
fixture, LFO3's suspended callback 196, POLY frame sync 38, ARP frame 8 and
recorder mix 30. These are separate stages, with explicit fixture overhead;
do not add overlapping LFO callback/dispatch/renderer measurements. Stereo
keeps previous gains for the exact first ramp when stereo returns. An empty
assembly path can be investigated while preserving that history. Periodic
UI callbacks, LFO3's global allocator/free/copy hooks and native parameter
lookups need their own attribution. The attempted broader synthetic IRQ
probe stopped at missing native boot/model state: it is not a completed IRQ
measurement or evidence of the owner's real call frequencies.

The owner sees no MSKIP warning. Independently, `test_st_patchers_emu.py`
now executes the real LFO3 and Modulation Skip initializers after resident
copy/nonresident clearing. With the full bank, LFO3's native hooks and SRAM
kernel install successfully, then MSKIP remains ON: 128 synthetic zero-depth
modulation blocks, 124 skips, no errors. The same results hold for both
diagnostic banks. A bad-preimage negative control must refuse without
partially patching the Sound writer. Native allocation/mutex and the other
module initializers are boundaries. This narrows the installation-conflict
hypothesis; it does not prove MSKIP's state for an arbitrary real pattern.
The test joins common regressions when Modulation Skip is present.

Two diagnostic banks isolate the gap using one fixed core:

- **A:** System Info 1.1 and Modulation Skip 1.2.3, no machine catalog.
- **B:** the full bank except LFO3 and ARP. Keeps samplers 1.4.6, FM Tone
  1.1, recorder 1.1, POLY 1.6, Sample Manager 1.2, System Info and Modulation
  Skip. Its AUDIO section is byte-identical to the complete bank.
- **C:** the already tested complete LFO3 1.6 bank, unchanged.

Restart after each bank so the preceding bank's RAM hooks are gone. Keep
the same core, stock pattern, tempo, playback and System Info screen; allow
the meter/startup notices to settle and observe a range. Compare B with C
first. Their difference localizes the LFO3/ARP service cluster; compare A
with B to investigate the remaining catalog/other modules. Removal also
changes linking/cache layout, so this is group attribution, not an exact
sum of per-function hardware costs. Do not assume the remaining meter gap
is entirely LFO3, stereo, UI, or a disabled Modulation Skip before that A/B.

## Current core candidate: 1.8.1.2 resident native routes

The owner measured the LFO3 1.6 full bank at 86% MAIN on the same stock
pattern, about another point below 1.5. The next change keeps that bank
byte-identical and changes only MAIN in the core. AUDIO remains exact.

`native-routes.s` encodes ordered callback routes in the existing 24 interest
bytes. Bits 0..6 select the first seven list entries; bit 7 requests the
original full-list traversal. Unknown subscribers keep all operations, and
interested subscribers beyond index 6 use that fallback. Full routes are
canonicalized to the fallback byte during preparation so they need no
per-callback filtering. Reserved TYPE bytes 18/19 retain their original
track/all-track meaning; PAGE view restrictions, order and consume semantics
are unchanged. The list, bytes and view filter still publish in one native
IRQ exclusion. Dispatch snapshots its list and route before callbacks, so
nested mask changes affect the next dispatch without losing current entries.

Routes are prepared at initialization, binding and changed masks/views.
Unchanged mask/view writes return immediately. No extra route table, heap
allocation, SRAM reservation, ABI field, operation, API version or bank
format is introduced. All runtime functions, including the binder and route
encoder, stay resident. Build profile and 65,536-byte limit are unchanged:
65,468 bytes used, 68 free. The dispatcher retains the original 28-byte
frame: a larger frame would add work to POLY's parent-stack origin census.

Complete integer paths compared with core 1.8.1.1 and the exact same bank:

| Path | Before | 1.8.1.2 |
|---|---:|---:|
| LFO native dispatch, disabled renderer boundary | 165 | 96 |
| NOTE_ACCEPT, idle ARP | 463 | 418 |
| TRIG preparation fixture | 305 | 297 |
| All-interested dispatch, 3 non-consuming boundary callbacks | 46 | 37 |
| First boundary callback consumes | 21 | 21 |
| Empty direct dispatcher | 13 | 13 |

The first row explicitly excludes LFO3's renderer; its separately measured
196-instruction idle callback is unchanged. Counts include routing overhead,
not physical CPU percentages or cycles. All 24 all-interested/consume/empty/
long-list dispatch scenarios have non-increasing counts. Other idle module
stages and the all-stock sender are unchanged.

The cold tradeoff is measured separately: POLY group configuration takes
181..189 additional instructions in the single-command scenarios, or 354
for independent-mode setup with two commands, to prepare the routes once.
All 165 subsequent key/sequence operations across 11 configurations are
non-increasing and preserve complete states/notes/pools. This is not extra
work every audio block. Core dispatch ARP Sound/acceptance differential:
26 cases, exact results and no count increases.

Target route tests cover 256 encoder cases and 11,264 dispatcher cases,
including every operation, reserved TYPE bytes, unknowns, duplicates, empty/
long collections, full routes, consumption, exact PAGE filtering, native
setter refresh, an executing nested-update callback, ABI and canaries.
A deliberately corrupted route must be detected. Host tests retain atomic
publication/profiling/stock-replay checks and add exact encoded-route cases.
Startup tests execute the actual resident binder after clearing appended
storage, including the old 40347BC2 failure as a negative control. Common
regressions: 84 pass, one optional external FM comparison skipped; units:
39 pass. Native/POLY PLAY differential: 120 cases, 9,600 blocks and 72
late-copy negative controls pass on the release.

Reproduce the core-specific comparisons with the documented environment:

```powershell
python tools/bench_st_idle_main.py --before-core <core-1.8.1.1-build> `
  --after-core <core-1.8.1.2-build> --before-bank <LFO3-1.6-bank.smz4> `
  --after-bank <same-bank.smz4> --out out/<new-idle-report.json>
python tools/bench_st_native_routes.py --before <core-1.8.1.1-build> `
  --after <core-1.8.1.2-build> --out out/<new-full-route-report.json>
python tools/bench_st_core_poly_events.py --before-core <old/main.bin> `
  --after-core <new/main.bin> --bank <LFO3-1.6-bank.smz4> `
  --out out/<new-POLY-report.json>
python tools/bench_st_arp_accept.py --before-core <old/main.bin> `
  --core <new/main.bin> --before <LFO3-1.6-bank.smz4> `
  --after <same-bank.smz4> --out out/<new-ARP-report.json>
```

Core SYX SHA256:
`686c44565937a417e3c083ac3a69db66ef3a8cd7bc32f227e97afe9a28a7b1be`.
The existing bank remains
`e18aa8589beb3db0f80b02ce2210a7f829275a455b4f0959ec388ffb9a7650c7`.
Hardware validation of core 1.8.1.2 is pending; no further MAIN points are
claimed from instruction counts.

## Current bank: LFO3 1.6 suspension

The owner confirmed core 1.8.1.1 boots and plays correctly; the reference
stock pattern now reads 87–88% MAIN, roughly one point below its preceding
full bank. The next bank keeps that exact core, AUDIO and all other mod
objects. Only the LFO3 renderer changes (1.6), under the owner's explicit
acceptance of freezing engines without output, including phase/fades/triggers
and RANDOM changes. Default full LFO3 callback: 878 to 196 instructions.
All 13 active TRI engines: 2,288 to 2,269, including the guard. The guard
runs inside the existing SRAM renderer, which shrinks to 822 bytes. See
[LFO3 MAIN performance](LFO3-MAIN-PERFORMANCE.md) for policy, full validation
and reproduction. The owner now reports 86% MAIN for this bank. The earlier
route-filter prototypes remain withdrawn; 1.8.1.2 above is a separate resident
implementation. No runtime code is placed in nonresident boot/load storage.

## Current correction: core 1.8.1.1

**Core 1.8.1 is withdrawn after a hardware startup exception.** Use the
1.8.1.1 correction or the known working 1.8.0 reference. The prototype
description below is historical and does not describe the corrected dispatch.

The device reported V04 at **40347BC2** after loading core and bank and
restarting. That is exactly the nonresident `core_modules_bind` entry in
1.8.1. Its shown return, 46006410, is immediately after the call from
`core_machine_load`. Moving this runtime function to appended `.boot` was
incorrect: native startup clears/reuses this storage after copying the core.
Its opcode audit and host test did not model that destruction; previous
target fixtures seeded event lists directly instead of calling the binder.

1.8.1.1 restores the original 1.8.0 binder and native dispatcher entirely in
resident RAM. The route-bitset optimization is withdrawn. The all-stock
transport exit remains, and the same samplers 1.4.6 / LFO3 1.5 / ARP 1.13 /
POLY 1.6 bank is retained byte for byte. No compiler limit or flag changes;
68 bytes remain in the same 64 KiB resident reserve. AUDIO is unchanged.

The builder and linked fast-path test now reject nonresident application
startup entries before packaging. `tests/test_st_module_startup_emu.py`
copies resident code, clears appended storage to model native startup, then
executes the actual binder/input reset/list construction/atomic publisher.
Heap and module initializers are explicit boundaries. It reproduces the bad
1.8.1 exception at **40347BC2** and verifies the corrected core with no modules,
the full delivered descriptors, initialization rejection and allocation failure,
including exact event lists/order and saved registers/stack. The new check
is part of common regressions. This still is not a complete device boot.

The 930→878 LFO3, stock transport 194→45, STEREO gain-copy and ARP acceptance
improvements below remain. The prototype's 165→105 dispatch and 621→421
NOTE_ACCEPT results are **not claims for 1.8.1.1**: it uses legacy dispatch.
`tools/bench_st_idle_main.py` records the supplied core's actual dispatch
implementation and checks its expected callback membership.

For the retry, use the explicitly named 1.8.1.1 core and re-send the complete
bank. The failed bank may be marked TRYING; do not assume it will be retried
automatically. Check the versions in System info after restart. Hardware
validation of the corrected core remains pending until the owner reports it.

## Withdrawn 1.8.1 prototype: historical implementation and measurements

2026-11-12. Core 1.8.1, samplers 1.4.6, LFO3 1.5 and ARP 1.13, compared
with the tested core 1.8.0 / samplers 1.4.5 / LFO3 1.4 / ARP 1.12 / POLY 1.6
delivery. POLY and the other bank components are unchanged.

The owner measured a stock pattern at 81–82% MAIN with a minimal bank and
88% with the complete bank. These changes reduce confirmed idle work; they
do not assign those six to seven hardware points to individual mods or claim
to recover them. Hardware A/B remains necessary.

## Retained changes

**Native dispatch, core 1.8.1.** The core previously published a union of
interests, then called the common subscriber list for an enabled operation.
It now prepares one ordered subscriber bitset per operation when the list
or a mask changes. The hot path reads that word and visits only its set bits,
preserving order and consume semantics. It does not scan descriptors or
look up masks per event. An unchanged mask/view needs no rebuild. A dispatch
snapshots its route word, so nested mask changes affect subsequent calls
without changing its current remaining subscribers.

The 24 route words cost 96 bytes in the bind allocation. A resident default
table preserves fail-open behavior before initialization and for resident
subscriptions. Unknown callbacks remain subscribed to everything; lists
beyond 31 subscribers use the original list traversal. The route pointer,
union masks, page view and native list publish inside the same existing IRQ
exclusion. No API, event number, mask bit or bank format changes.

**Stock machine transport, core 1.8.1.** Two aligned SRAM words test whether
any digital track type has bit 6 set. All stock digital types have that bit
clear; registered additional machines occupy 64..79. An all-stock block
follows the existing empty/reset path before owner queries or track walks.
Other high types can harmlessly enter the original path. It reads live types
every block: switching to an additional machine has no cache delay. Leaving
the last additional machine clears the published tag/state. The readiness
gate and complete MAIN/AUDIO parameter protocol remain unchanged.

The original core had 100 resident bytes free. Its one-time module binder
now resides with MAIN's appended boot code and remains callable after boot.
All retained state stays resident. The build audits the C `.boot` range as
well as resident code: no compiler flag, linker limit or instruction check
is relaxed. There are 756 bytes free in the same 65,536-byte resident reserve.

**ARP 1.13.** Compare Sound settings as six aligned alias-safe words instead
of a 24-byte loop. Compile-time layout assertions and explicit alignment
establish the contract. A changed record still copies every field and keeps
the original stop/rebind behavior. NOTE_ACCEPT reads published settings
directly without supported root locks; locked cases keep the scratch/effective
calculation. OFF is checked after lock capture, so a MODE p-lock can still
start an otherwise disabled ARP on that step. Clock/selector/persistence stay.

**LFO3 1.5.** The existing exact-default path folds TRI directly with integer
instructions instead of pushing a phase and calling the tiny native helper.
It keeps phase normalization, every state write, existing EMAC arithmetic,
rounding and RNG. All thirteen motors keep advancing. No new per-track guard,
cache or oscillator suspension is added. Physical EMAC/state/ABI are compared
with cfbench; the current native helper remains the arithmetic oracle.

**STEREO, samplers 1.4.6.** Remember the four buses' last-block gains with
an inline fixed MOVEM copy, eliminating helper-call/argument/frame traffic.
The zero-mask path still remembers gains for the exact first ramp when stereo
returns. Active mixing, gain order and EMAC save/restore remain unchanged.
No new SRAM is allocated; existing placement/fallback/Chorus rules remain.

## Measurements

Guest instructions, including each check and its stated fixture wrapper:

| Complete measured stage | Before | After |
|---|---:|---:|
| LFO3 callback, defaults, no locks | 930 | 878 |
| Stock transport, quiet blocks, four-block mean | 194 | 45 |
| Stock transport, all trigs, four-block mean | 352.25 | 45 |
| Integer LFO native dispatch, renderer disabled in fixture | 165 | 105 |
| Stock NOTE_ACCEPT, three native subscribers, warm | 621 | 421 |
| STEREO delay callback, no stereo tracks, steady | 74 | 67 |
| STEREO reverb callback, no stereo tracks, steady | 45 | 38 |
| STEREO main/direct callback, no stereo tracks, steady | 91 | 77 |
| ARP Sound/queue acceptance, first ordinary OFF case | 818 | 660 |
| Eight stereo tracks, two tracks to both sends, four callbacks | 4,461 | 4,398 |

These are different fixtures: do not add them as a full-interrupt CPU budget.
The native-dispatch fixture disables LFO3 rendering; the separate 930→878
cfbench row executes the real renderer/EMAC. TRIG preparation, Sound
publication, smoothing and lock application have explicit fixture boundaries.

Counts are **not cycles or MAIN percentages**. MOVEM, bus latency, caches,
scheduling and the actual pattern determine hardware results. Unused mods
still maintain phase, sync, routing and gain history for exact transitions.
No feature is disabled, updates decimated or future activation approximated.

## Verification

- `tools/bench_st_idle_main.py`: eight sequences of complete integer idle
  callbacks, warm native operations with callback-entry counters, no-trig and
  all-trig stock transport. Every stage must be non-increasing. LFO dispatch
  enters only LFO3; NOTE_ACCEPT enters only ARP.
- `tests/test_st_native.c`: route membership, order/consume, duplicates,
  unknown/long-list fallback, nested mask updates, snapshot semantics, atomic
  publication and legacy profiling bounds. Resident/zero-module tests remain.
- `tests/test_st_parameters.c`: 20,000 randomized blocks against the existing
  oracle, owner/type changes, readiness, retirement, restarts, cadence,
  modulation and stock↔FM transitions.
- LFO3 cfbench: native LFO2 oracle, continuous/boundary/control-bit cases,
  512 lock-mask cases, physical EMAC/ABI, corruption negative control,
  relocated SRAM and adjacent sampler sentinels. Complete-stage count gates.
- STEREO cfbench: 384 callback blocks per DDR/SRAM/fallback/Chorus/occupied
  scenario; empty/torn/late masks, dynamic gains and exact buses/EMAC/ABI;
  stable/dynamic complete-block count gates.
- `tools/bench_st_arp_accept.py`: 26 digital/analog Sound-change and MODE-lock
  activation cases, exact state/notes/pools and no count increases.
- Existing ARP callbacks/transitions/locks/recording/analog/view tests; POLY
  events, keyboard/MIDI, Sound/lock copies, PLAY (120 cases / 9,600 blocks),
  and native pipeline. Full common regressions and unit tests. Optional
  comparison with external FM sources is skipped when unavailable.

Fixtures directly replacing the core's list now call `core_native_initialize`
afterward, as the real binder publishes new routes. Reasserting an unchanged
mask is not list publication. Original note/refusal assertions remain intact
in the analog owner-refusal and owner-transition fixtures.

## Reproduction and hardware A/B

Use the unchanged documented compiler environment/profile. Both banks need
their sibling `.work/main.elf`; core folders need `main.bin` and
`verification.json`. Output directories must be new.

```bash
"$PY" tools/bench_st_idle_main.py --before-core "$OLD_CORE" \
  --after-core "$NEW_CORE" --before-bank "$OLD_BANK" --after-bank "$NEW_BANK" \
  --out "$REPORTS/idle.json"
"$PY" tools/bench_st_arp_accept.py --core "$NEW_CORE/main.bin" \
  --before "$OLD_BANK" --after "$NEW_BANK" --out "$REPORTS/arp-accept.json"
"$PY" tools/bench_st_lfo3_main.py --core "$NEW_CORE" --before "$OLD_BANK" \
  --after "$NEW_BANK" --cfbench "$CFBENCH" --out "$REPORTS/lfo" \
  --require-no-count-regressions
"$PY" tests/test_st_stereo_main_current.py --bank "$NEW_BANK" \
  --reference-bank "$OLD_BANK" --cfbench "$CFBENCH" --out "$REPORTS/stereo" \
  --require-no-count-regressions
```

Core AUDIO remains byte-identical to the working 1.8.0 companion. Bank AUDIO
also equals the owner's working reference. Only sampler stereo MAIN, the
LFO3 renderer and ARP module/persistence objects change in the bank. POLY,
FM, recorder, System info, Sample Manager and modulation skip objects match.
All earlier sampler/LFO3/ARP/POLY optimizations remain included.

Historical prototype instructions (superseded by 1.8.1.1 above): the bank
can run on 1.8.0, but core dispatch/transport savings then do not apply.
Compare the same stock pattern/project, tempo, sends and System info screen.
Then exercise additional machines/stereo/LFO activation/p-locks/ARP MODE
locks/POLY groups and kit/pattern/machine transitions. MAIN, AUDIO and sound
are separate checks. The experimental 1.8.2 core is not involved.
