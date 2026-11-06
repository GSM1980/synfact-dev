# LFO3 port to elekloader (working design, 2026-10-28)

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
   native LFO1 on the same words; ~1,026 instructions/block idle.
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
