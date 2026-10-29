# Shared-event audit: core 0.10.0

Implemented for the local Syntakt OS 1.41 candidate. Core 0.9.2 had a separate
machine input registry (`bind_input`) alongside the existing event bus. That
registry and both private dispatch functions are now removed.

## Rule

Reuse existing core events/exports before introducing another hook. Missing
context belongs in shared core adapters. One collection owns each operation;
modules use its subscriptions. A new site requires demonstrating which required
behavior cannot be supplied through the existing services.

## Delivered routes

| Work | Existing collection | Context and consumer |
|---|---|---|
| FM compound ratio encoder | `ev_enc` | FM module subscribes with `ST_EV_VIEW`; receives the native resolved view, normalized event and parameter ID |
| FM paired lock clear | `ev_key` | FM module subscribes with `ST_EV_VIEW`; notified after native parameter-view key processing |
| FM and sampler page graphics | `ev_draw` | Core machine drawer subscribes with the view marker; draws at its native layer before higher overlays |
| Sampler waveform scan/playhead | `ev_tick` | Core subscriber traverses the live visible-view list; calls the bank's idle callback only for an eligible active parameter view |
| Sample Manager audition refill | `ev_tick` | Ordinary core subscriber; private idle override removed, native idle/lifetime methods retained |
| System info | `ev_tick`, `ev_draw`, `ev_settings` | Ordinary global subscriptions keep their existing ABI/order |
| Sample Manager FUNC interception | `ev_key` | Existing ordinary subscription keeps its pre-dispatch behavior |

There are still eleven event collections. `ST_EV_VIEW` selects contextual
subscribers in those same lists; it does not introduce a second collection or
machine input registry. Runtime list entries have a core-owned high-bit marker.
Global dispatch skips contextual entries; native adapters call only contextual
entries. Raw/global subscribers do not run twice. ENC stops on consumption;
DRAW/KEY contextual notifications do not replace native drawing/key handling.

## Deleted and retained sites

- **Deleted:** encoder policy/input patch at `4003b96c`. Its six bytes are stock
  again. Policy lookup and native encoder context reuse the already-owned
  cached-parameter entry `4007b9b2`, gated by native return PC `4003b972`.
- **Deleted:** machine input callback array, bind/dispatch implementation and
  `core_machine_encoder_policy`. The old API field only refuses registration
  so old FM banks reject cleanly instead of calling a null pointer.
- **Deleted:** all five private idle-vtable profiles/clones, their allocations,
  vptr replacement/restoration, and the private idle trampoline. `ev_tick` owns
  the periodic work. Native view pointers are not retained between ticks.
- **Added (0.12.18):** lock preview selection walk `400391a8` (8 bytes inside
  `40039160`, after its trig test) to `core_preview_selected`: the same walk
  without operator new/delete (README, "Version 0.12.18"). It sits past
  LFO3's entry patch, so both share it.
- **Retained:** SYN-view draw adapter `4003a244`. It supplies the actual view
  and clipping/layer position to `ev_draw`; the global frame-end callback alone
  cannot paint underneath an already drawn modal. Both phases share one list.
- **Retained:** native parameter key adapter `4003bc74`. It synchronizes pages
  and supplies the post-native context to `ev_key`. Moving this notification
  before native clear would change the existing paired-lock operation order.
- **Retained:** generic parameter/dial/range/storage/label adapters. They serve
  different native operations and already share the machine descriptor/query
  services. The four destination-caption sites are distinct inline stock table
  readers; all call the same short-label getter.

The native Brain encoder path still performs normalization and native view
routing. The shared cache entry observes its resolved context instead of
estimating a target from a cached pointer or a topmost-view guess. Native edit,
lock, undo and stock fallback routes remain in place. Encoder policies are
selected only for the native editor caller; other cache/format callers retain
the descriptor alias.

## Digitakt reference

Reviewed [machine-pages](../machine-pages/README.md), its
[periodic work](../machine-pages/pages.c),
[native adapters](../machine-pages/pages.s), and the DT1 core subscription.
DT1 uses `ev_tick` for periodic work and shared native adapters for page-local
metadata/drawing. Its layout adapter recognizes the native view caller by
return PC instead of adding another hook at the view getter. The Syntakt
consolidation follows that ownership/context pattern. No DT1 behavior changes:
new shared-assembly filtering is conditional on `ST_CONTEXT_EVENTS`, defined
only by the Syntakt core.

## Compatibility and evidence

- UI API 4 / module service API 2. Ordinary event signatures are unchanged.
  Ordinary-event initializers receive API 1; native-view event initializers
  receive API 2, preserving legacy modules that check for exactly version 1.
- Existing sampler/ordinary banks remain supported. Rebuild/install the matching
  FM bank; the old FM initializer requires the retired input registration.
- Serialized contextual IDs are only `101h`, `102h`, `103h`; old cores reject
  them. All other flags remain invalid. Relocated callback-pointer validation
  remains unchanged. Disk ABI, feature bits and descriptor sizes are unchanged.
- **81 distinct, non-overlapping MAIN sites**, down from 82; **11 collections**.
- MAIN reserve **64,912 / 65,536 bytes**, 624 free (500 fewer bytes than 0.9.2).
  Target flags remain `-O3 -mcpu=54415 -mtune=cfv4`.
- Full build/lint and native-host/static regressions passed. Tests cover raw/view
  isolation, consumption/order, live visibility/removal and stationary waves,
  FM's 361 ratios/paired locks/four pages, sampler UI/audio, parameter transport,
  project pools, Sample Manager, legacy/invalid SMZs and all destination labels.
- Final SYX assertions verify native frame/return-PC gates, restored encoder
  bytes, native draw/key order and the one shared table per event. AUDIO is
  byte-identical to 0.9.2; imported FM instructions/cache phases/ECO and 256 UI
  frames also pass comparison against the optimized reference.

Candidate: `out/core-st-0.10.0-shared-ui/`, including matching SYX/SMZ,
`verification.json`, `validation.json`, `lint.json` and `regressions.log`.
These checks do not execute ColdFire or write to the device. Physical UI/audio
and CPU measurements remain to be verified on the Syntakt.

## Core 0.11.0: recorder uses the existing bus

The source-built [recorder](../recorder-st/README.md) subscribes to KEY, ENC,
TICK, DRAW-with-view-context, MAIN-mix, FS-write and FS-written. Double SOUND is
detected on raw KEY; input uses raw ENC/KEY with native focus checks. Periodic
jobs use TICK. An owned plain IView's draw bridge emits through the existing
contextual DRAW list beneath native modals. Its native key/encoder/idle methods
remain unchanged. No extra collection or firmware site is introduced.

UI API 5's appended host pointer exposes existing generic services rather than
duplicating storage validation, pool synchronization or event dispatch. The
machine drawer checks the native parameter-view class before reading extended
view fields, so plain owned views can share DRAW safely. A high-zone descriptor
completion capture enables the stock close routine's existing FS-written path;
the capture callback itself is a no-op. Recorder save/upload exclusion uses
those same FS events.

The complete candidate retains **81 MAIN sites / 11 collections**, using
**65,040 / 65,536 resident bytes** (496 free), with `-O3` and unchanged AUDIO
relative to 0.10.0. Source/ABI audits, 16 native-host fixtures, linked hook tests,
FM reference comparisons, units, instruction audit and lint pass. Validation
and logs are in `out/core-st-0.11.0-recorder-complete/`. These are host/static
checks; physical UI/audio/timing tests remain pending.

## Core 0.12.0: POLY and complete-view contexts

The [POLY module](../poly-st/README.md) uses existing RENDER-in, TICK, ENC,
DRAW and KEY collections. ENC emission moves from the metadata getter to the
already-owned control resolver (`40037d9e`), with exact editor return-PC and
TRIG-wrapper checks. This reaches virtual controls before the native zero-ID
exit. There remains one contextual encoder dispatch, and `4003b96c` stays stock.

The existing key adapter adds a consuming pre-native phase (`302h`) to the same
KEY list; old post-native `102h` callbacks keep their void contract. Page mutation
guards use the existing five action adapters. TRIG metadata and page-vector
support reuse existing navigation/layout services.

One new view-layer site at `40100830` emits DRAW after a complete view. It
replaces emission inside `4003a244` and removes the recorder's owned draw bridge;
native TRIG's additional drawing cannot overwrite the module page afterward.
Native key/encoder/idle methods are retained. Higher modals keep their order.

The new generic `ev_native` collection supplies distinct native transactions:

| Native sites | Operation |
|---|---|
| `4008b882`, `4008cfc4`, `4011a734` | Trig preparation, sequencer enqueue, immediate event processing under native exclusion |
| `400a79cc`, `400b36ce` | Pattern header load/reset and validated extension preservation |
| `400a4ade` | Effective scalar publication before voice/frame packing |
| `400a4bce`, `400a4c96` | Two native consumers of effective machine type; one shared operation |
| `40013304`, `40025594` | Sound writes and machine selection guards |
| `40097afc`, `40119266` | Optional shared modulation/native-LFO kernel entry, exact stock fallback without groups |

These contexts are absent from raw UI/tick/render-boundary events. Core owns
all preimages, stock replays and ABI; modules subscribe to one shared native
list and never patch firmware. The existing parameter snapshot transport reads
effective owner/type for FM/SP rather than installing a second FM transport.

There are **94 MAIN sites and 12 collections**: 12 missing native transaction
sites and one complete-view DRAW site added to 0.11. System info UI/collectors
move to the SMZ; generic core accounting remains. API 6/3/2 negotiates the new
semantics. C list references and assembly operands publish in a single existing
IRQ exclusion, preventing a partially activated render/native pair.

Final resident usage: **63,360 / 65,536 bytes**, **2,176 free**. The candidate
`out/core-st-0.12.0-poly-complete/` passes the combined native/static suite and
lint, with firmware AUDIO and the FM/SP AUDIO section identical to 0.11.0.
These checks do not execute the full firmware or replace device validation.

## Core 0.12.1: close the SourcePageView gap

The earlier return-PC gate on base navigation missed SourcePageView's virtual
override (`40044c54`), which reads source layouts directly. The existing source
getter now recognizes its two call sites and returns the selected view's shared
layout. Other callers retain descriptor/action/native fallback behavior.

ENC context is relocated to the single virtual call inside the native editor:
`4003b8c6`, replacing `movea.l 188(a0),a0; jsr (a0)`. The adapter forwards the
original view/knob arguments, runs the actual virtual method, then emits the
existing ENC context exactly once. Consumption unwinds the original 72-byte
native editor frame. Base navigation and cached metadata emit no input events.
TRIG virtual controls, ordinary parameter controls and SourcePageView now share
the same encoder phase. The retired policy site `4003b96c` remains stock.

The shared SYN ownership gate reads native selector predicate `400c3294`, with
its byte-bool ABI, before custom draw/sync/idle. FM no longer qualifies its own
view or independently selects a page. All machines use the same core decision.
This adds one common editor adapter to 0.12.0: **95 sites / 12 collections**.
It adds no per-machine hook or callback registry. Tests pin both native source
getter calls, virtual argument forwarding and the one ENC emission.
The sole editor gate runs selector/native page guards before event subscribers,
so FM cannot bypass a POLY slave block by consuming RATIO B first. The redundant
POLY encoder guard is removed. UI API 7 marks this contract; module/host remain
3/2 with unchanged prefixes.

## 0.12.2: defaults through the existing lookup adapter

No additional hook or collection. `core_machine_slot_to_engine` recognizes
only native machine initialization's final lookup (return `40024e04`, slot
`1a`) and calls the shared descriptor-driven default service for controls
9..28. A2 supplies the native model, D5.b its quiet flag. Model notifications
follow the full write; normal lookups and saved Sound/project loads remain
pure. The matching POLY bank pins imported JMP sizes to preserve relative
branch offsets; this also introduces no runtime hook.

## 0.12.3: skip unused dispatch without removing hooks

All 95 sites / 12 collections remain. Native adapters use the actual native
list's availability to reach their existing stock replays without C when empty.
The flag is initialized from resident subscriptions and atomically published
with optional module lists. No second dispatch list or module-specific gate is
added. Metadata adapters short-circuit native IDs/inactive machine banks while
retaining their extended queries. The extended sender skips FPGA writes for
stock-only blocks, preserving masks/checksums and immediate extension restart.


## Core 0.12.19 / POLY analog ownership

No site, ABI, native body or core callback contract changes. The optional POLY
bank now uses all 12 entries of the existing OWNERS map; digital TYPE cursor
bits remain limited to 0..7 and an analog-only group does not request TYPE.
The bank publishes its owners, grouped byte, first/end publication bounds and
slave bitmap in the same existing native IMMEDIATE exclusion. Analog machine
changes use that operation from TICK, never an added audio-block check. See
[POLY evidence and budgets](../poly-st/README.md) for the stock layout audit,
linked checks and pending physical profiling.
