# LFO3 port to elekloader (working design, 2026-10-28)

The consolidated release is **LFO3 1.3**, one step from main's 1.2. Numbers
1.4 through 1.7 below identify development measurements, not public releases.
See [the version mapping and inactive-engine policy](../../docs/MAIN-OPTIMIZATION-RELEASE.md).

## Disable the inactive native route (1.7, 2026-11-13)

The owner's same-layout diagnostics measured V (LFO3 only) at 84% and W
(same initialization/global hooks, native subscription disabled) at 82%.
The production module now removes only its LFO operation interest when no
prepared/locked DEP/DEST row can produce output. Page sync/navigation/layout
interest remains enabled. It uses the existing host API/core interest byte;
there is no new per-block check or alteration to the renderer's algorithm.

`lfo3_output_rows` is one DDR word, updated by `prepare_track` after base
parameter publication and by active DEP/DEST-lock apply/clear. Its read/
modify/write and the first/last-row route update share a bounded IRQ-masked
section; there is no allocation or mutex inside it. Other subscribers' bits
remain owned by the core. A locked destination that might fail metadata
validation is conservatively enabled; the existing overlay still resolves
it before rendering. ARP-only locks cannot enable LFO3. Exact neutral DEP
4000 and DEST outside 1..70 remain suspended under the accepted 1.6 policy.

Parameter edits, Sound/Kit publication/republish, both queued and static
PLAY-start locks reactivate the next eligible block. Removing a lock derives
eligibility from the base bank again. Pool/bank reset recomputes all rows
once; CRC fallback also invalidates the old overlay before re-evaluating
the reset bank with its active locks. SRAM bank/kernel sizes stay 944/822.
Generated event edits live in `shared_locks.event_hot_paths` for regeneration.

The real-core transition test covers 855 cases, task/IRQ contexts, fine depth,
FX, invalid destinations, editor refresh, task republish, nested publication,
CRC fallback with/without locks and another POLY subscriber. A dropped-update
negative control must fail. At an explicit DSP-body boundary, inactive native
entry drops 137 -> 6 instructions, while active entry remains 137. Eighty
analog-emu matrices keep exact renderer/state/EMAC results and counts.

Bookkeeping moves to events: changed base publication adds about 61–66
instructions without a route change in the measured fixture; first/last
transitions add 834/826. Charging both transitions still wins after sixteen
idle blocks even with the renderer excluded (2,691 -> 2,255 instructions).
The separate event-copy differential keeps its no-increase gate for the
172 calls without an output update; 16 DEP/DEST updates have an explicit
128-instruction additional budget with no host/router, measured max 92.
The actual host route cost is separately measured by the integration test.
These are instructions, not hardware CPU points or call-frequency claims.

## Shared event bookkeeping (1.6.1, 2026-11-13)

Conversion resolves the row's owner and track in one traversal, replacing
up to thirteen complete `lfo3_owned_lookup` calls. The lowest track still
wins; equal-track ties keep live/owned-list/legacy-temporary precedence.
TRACK clipboard objects remain excluded. Static PLAY-start lock blocks,
unsigned candidate subtraction, null contexts and legacy-only mode retain
their previous behavior. There is no new cache, allocation, IRQ exclusion,
SRAM reservation or writer-side activity manager.

Apply and ARP output copies expose their fixed halfword assignments to the
compiler. They still copy every payload word, including with mask zero,
and preserve access order for overlapping/halfword-aligned output. Busy
publication wrappers and LFO3 1.6's suspension policy are unchanged.
`gen_locks.py` applies these changes through `shared_locks.event_hot_paths`;
regeneration from the pinned donor reproduces both generated C files.

An independent old-bank differential checks 1,512 owner/row cases and 188
event cases (real sparse lanes, owned patterns, static first-step locks,
active masks, invalid locks and overlapping ARP copies). All event cases
retain exact bytes/returns and do not increase instructions. Native event
bodies and allocator/mutex remain explicit boundaries. Empty conversion
is 121 -> 109 instructions at track 1 and 1,573 -> 196 at FX; application
126 -> 104; ARP read 67 -> 53. These are per call, not physical CPU points.

## Suspend engines without output (1.6, 2026-11-12)

The owner explicitly selected suspension of every engine without output,
accepting changed phase, fades, triggers and RANDOM sequences. The renderer
now skips the whole engine for exact neutral DEP `4000`, or a destination
outside native slots 1..70 (including zero and `7f` NONE). It reads the
effective parameter bank after the existing p-lock overlay, so locks can
activate or suspend an engine in the same block. Fine DEP `4001` remains active.
Skipped engines retain state and make no RNG or working-buffer writes.
Their triggers are ignored. RNG is shared, so active RANDOM timelines can
also change when another engine stops drawing numbers.

The guard is inside the existing SRAM renderer, not a separate manager.
The old exact-default running path is removed. A register holds trigger bits
and the loop sentinel; another holds scaled depth until destination output.
The state waveform uses the existing state pointer, removing its redundant
pointer/increment. MODE stays in one register and non-RANDOM helpers preserve
their saved argument in a register rather than two stack accesses.
The kernel shrinks from 1,072 to 822 bytes in the same 1,152-byte window;
the bank stays 944 bytes. SRAM refusal/CRC fallback and Chorus windows remain
unchanged, and the same guard works in DDR.

The full callback including its guard falls from 878 to 196 instructions
with all engines inactive. Steady TRI cases: 1 active 986 to 355, 4 active
1,312 to 834, all 13 active 2,288 to 2,269. All 80 measured DDR/SRAM rows
pass the no-count-increase gate for callback, selector and renderer. These
are emulator instruction counts, not physical cycles or MAIN meter points.
See [LFO3 MAIN performance](../../docs/LFO3-MAIN-PERFORMANCE.md) for the
separate suspension oracle and exact behavior contract.

## Exact-default TRI fold (1.5, 2026-11-12)

The default path folds TRI with integer instructions instead of pushing the
phase and calling its native helper. All oscillator/working/RNG/EMAC bytes
match the former path in cfbench; phase still advances on all thirteen engines.
The complete default callback falls from 930 to 878 instructions, with no new
guard or cached state. See [idle MAIN performance](../../docs/IDLE-MAIN-PERFORMANCE.md).

## MAIN measurement, lock preparation and renderer (1.4, 2026-11-12)

The current bank's callback, engine, parameter selector, publication and
setter paths now have a reproducible measurement baseline. The first change
unrolls the eight lock-field assignments into constant bit tests/offsets,
without adding a cache or changing engine state (1.3). Version 1.4 shares
the default tempo increment, packs exact default comparisons using explicitly
aligned private banks, calls the same native TRI helper directly and simplifies
the loop branch. All phase/fade/RNG/EMAC state is preserved. Default callback
cost falls from 1,087 to 930 instructions; code occupies 1,062 of the same
1,152 SRAM bytes and data remains 944 bytes. See
[LFO3 MAIN performance](../../docs/LFO3-MAIN-PERFORMANCE.md) for the measured
scope, correctness checks, reproduction and hardware validation procedure.

Reference: Synfact `src/features/lfo3` (option B, universal side storage) and
`src/research-runtime/lfo3-coherence-20261021/*_proto`. Synfact applied ~50 SAFE
patches from its file. Here the core owns some of those sites, so LFO3 is an
SMZ module plus a few generic core services; the core resident has ~364 bytes
free (0.12.13), so logic must live in the module.

## Held-trig lock display (2026-11-02)

LFO3 takes the entry of the lock-aware getter 40039160 (view, id, locked*)
for its IDs. It returned the Sound's value and cleared `locked*`, so held
trigs never showed LFO3 locks, although the editor wrote them and playback
applied them. `lfo3_ui_preview` now does what the native/core preview
(400391a8) does: with trigs held (selection +160h, 400c34d6), the first
selected step within the active track's length whose lock the view reads
(vt +90h, 40037942) is returned and marked locked; otherwise the Sound's
value. `lfo3_ui_get`, used for base values elsewhere, is unchanged. Found on
the instrument: the per-step getter returned the lock while 40039160 made no
lock query at all. Confirmed on the instrument.

## Page key lock LEDs (2026-11-02)

Holding trigs lights the page keys whose parameters hold a lock at those
steps. The native collector (40031de6, per held step, called through
400c3670 from the main view's LED code) checks the parameter IDs of each
page's fixed layouts; LFO3's page and ARP's lanes are not among them.
`lfo3_page_locks` replays the collector, then marks LFO (page 4) for an LFO3
lane (58h..5Fh) and TRIG (page 0) for an ARP lane (60h..67h) at the same
step of the active pattern's lock model. Confirmed on the instrument.

## First step when PLAY starts (2026-11-02)

PLAY fires each track's first step (4008c0b0) with a static event whose lock
block is that track's static block (444e3884 + 4 * track), outside the pool
of 64 that `queued()` maps. Capture, convert, apply and `synfact_extra_locks`
therefore found nothing, and LFO3 and ARP locks on that step applied only
from the second pass. `shared_locks.py` now gives `queued()` thirteen
`start_locks` entries for those blocks (pool first, then the table).
`tests/test_st_arp_locks_emu.py` checks both sides; the previous bank fails it.

## Shared ARP lanes: temporary slots (2026-11-02)

`shared_locks.py` widens parameter loops from eight to sixteen for ARP's
lanes 0x60..0x67. Its first version also widened the loops over the eight
temporary pattern slots (`temporary[8]` in `pattern.c` and `owned.c`): the
pattern context's initialiser wrote 96 bytes past its storage into the
owned-lock context, and LFO3 lock writes failed. Lines naming `temporary` are
now left as generated.

## Native lock editing: lazy lane census (2026-10-28)

`gen_locks.py` adapts the pinned pattern setter before generating `locks/pattern.c`.
Sparse writes retain the original census and capacity backend. Native writes
count the 13 x 71 used-lane flags only if sparse lanes exist and the target
native lane was unused, when the combined-capacity decision needs the count.
Existing-lane edits and native edits with no sparse lanes avoid that scan.
The writer mutex, owner lookup, native setter, notifications, rejection threshold
and stored data format remain unchanged. No persistent counter or new hook is added.

`tests/test_st_lfo3_setter_emu.py` compares the old and new compiled bank setters
against stock MAIN and core 0.12.18. It checks results, matrix/store bytes, busy
state, callback arguments/order, registers and SP across 584 cases, including
capacity boundaries, sparse controls, deletion and invalid arguments. Mutex,
ownership and sparse-backend bodies are explicit boundary doubles. In the
one-native-lane fixture, zero sparse lanes, instructions fall from 6,020 to 1,332;
flag reads fall from 923 to zero. These are isolated instruction counts, not
physical CPU percentages or a full boot test. Full common regressions also pass.

Candidate: `out/lfo3-lock-edit-20261028/synfact-machines.smz4`, unchanged core
0.12.18 and AUDIO. The sibling `baseline/` bank is byte-identical to fm-fb.
See the workstation build guide for the emulator environment and commands.

## Task-side publication lookup memo

Candidate: `out/lfo3-publication-20261028-final/synfact-machines.smz4`.
It retains the lazy-census change above, all modules and unchanged core 0.12.18.

Publication still visits each source and compares all eight parameter words,
validity and native machine type. The task-side republish path memoizes only
the address of a heap-owned record, per track/source (108 bytes in bank DDR).
It acquires the recursive writer mutex lazily and retains it across consecutive
owned lookups, so a warm pass over 13 owned sources needs one mutex entry and
no owner queries. Native/IRQ publication and ordinary UI queries do not use
these pointer memos. Live Kit/pool sources retain their direct lookup path.

`gen_locks.py` inserts invalidation before owner allocation/release, record-cell
create/delete and relocation in generated `locks/owners.c`. Callers hold the
writer mutex; the relocation service now acquires it explicitly. Cache hits
are dereferenced only under that same mutex. Values are always read from the
current record; they are not cached. No null results are memoized.

Side entries retain their whole-object hash verification. An owner-lookup miss
releases the mutex before those hashes; it is not held across a long Kit hash.
Skipping a republish merely because a memcpy size is unrecognized is unsafe:
a small copy may stale a side Kit's hash and the next unrelated large copy must
still detect it. Global source revalidation therefore remains present.

`tests/test_st_lfo3_publication_emu.py` executes real compiled shadow, owner and
publication code: 35 comparisons on stock plus 35 on the core image, and 584
additional setter cases preserving the previous optimization. It covers live,
pool, owned and side sources, values/types, copies, reused/overlapping owners,
cell deletion/recreation, poisoned freed metadata, relocation and IRQ bypass.
It checks mutex protection of metadata and release before side hashes. A
disabled-invalidator negative control is detected. Native allocator, mutex,
copy and destination descriptors are explicit boundary doubles; scheduler
contention, full boot and physical CPU are not modeled.

Instruction examples, excluding real native mutex bodies: owned warm republish
3,543 -> 2,301; unrelated row copy with those sources 3,581 -> 2,339. Live-only
republish 2,080 -> 2,148; side-Kit hashes dominate at roughly 151k instructions
and are preserved. These are conditional savings, not a percentage of MAIN or
all LFO3 work. Physical performance remains to be measured.

## Phases

1. **1a engine (done, emulator):** `render.s` (Synfact throughput kernel, PIC)
   from MAIN SRAM, state/RNG/params bank in SRAM, run from the core LFO
   operation (core 0.12.14 passes `{working,tempo,trigs}`). Bit-exact to the
   native LFO algorithm on the same words. The original renderer used about
   1,026 instructions/block idle; current callback counts are recorded above.
2. **1b page:** third page on the LFO key, eight controls, wave drawing,
   destination picker; values per track in RAM (not saved yet).
3. **2 storage:** Sound/Kit/project/new-project/copy (Synfact shadows +
   container tail), Sound pool.
4. **3 p-locks:** Synfact lock banks/lanes/editor.

## IDs

Synfact used engine IDs 0x245..0x24e. In elekloader those collide with SMZ
machine IDs (`0x1f9 + (type-64)*32`, types 64..79 → 0x1f9..0x3f8). LFO3 first
took the type-79 blocks (engine 0x3d9.., UI 0x4c0..). Since core 1.2 it uses
the core's module range after the machines (`ST_MODULE_ID` 0x3f9..0x402, UI
`ST_MODULE_UI` 0x4e0..0x4e9), so type 79 is free for machines again. Its
locks store lock slots (0x58..), not IDs, so the 11-bit IDs pack nowhere.
Projects saved with the old IDs are not translated.

## Site map for 1b (Synfact → here)

Core-owned (need a core service, not a module patch):
`37d9e` control id (PAGE_NAV event exists), `3bc74` page op, `7b9d0` layout,
`a62c8` range, `12b24`/`11a82` labels, `a636a`/`a634e` engine↔UI,
`a5c4a` local slot, `7b9b2` cached parameter, `7bb6a` value format,
`a666a` modulation flags, `13304` Sound store.
→ core 0.12.14: a parameter provider called by `core_machine_query` when no
machine claims an ID (host API 7 registration), plus what PAGE_* already offer.

Module-owned (self-installed, preimage-checked, like Synfact init / FAST MAIN):
`f38f2` LfoPageView constructor (page vector 2→3), `f3706/f370c` wave widget,
`37f9a/37e50` editor, `1306e`, `38124`, `14c48`, `14e32` relative edits,
`39160` value entry, `1c422` resolver (LFO3 ID → LFO2 ID for the model),
`3b8d4/3baae/3b6be/3b11a` encoder/popup metadata,
`14ad8/14cc2/14eac` destination validation.
