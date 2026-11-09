# Optional machine banks: SMZ4 ABI 0x454c0001

SMZ4 is an optional code file at the native Data path `/mods`. From core 0.8,
it can contain machines, optional MAIN modules, or both. Ordinary firmware mods
can still use `.elemod` and the core's 64 KiB reserve. A machine bank has
1..16 distinct digital machine types **64..79**, AUDIO backends **12..27**.
An optional module uses bounded event subscriptions, never a file-supplied
firmware patch plan. Transfers, storage and project/sample loading work without
an SMZ. Missing or invalid files are skipped silently.

## Source and builds

A machine uses C/assembler SDK sources and a `mod.json`, plus an `smz` description:

```json
"smz": {"descriptor": "my_definition", "audio": {
  "sources": ["audio.c"], "update": "my_update", "render": "my_render"
}}
```

MAIN exports a `const struct core_machine_def`; the SDK header is
`elekloader/sdk/include/syntakt/machines.h`. Build sources, never a previous
bank or an opaque precompiled patch:

```sh
python -m elekloader.sdk.syntakt.smz examples/machine-test-st --out out/machine-test.smz4
python -m elekloader.sdk.syntakt.mods_object pack out/machine-test.smz4 out/machine-test.obj
```

Multiple machine directories may be passed to `smz`. MAIN uses the SDK's device
toolchain profile, then a relocatable image with a contiguous descriptor directory.
AUDIO is compiled for its final CPU2 address. The supplied example is silent
and has 28 controls, exercising all four pages; it is separate from firmware.
The Data envelope is uploaded to `/mods-stage`; no `SAMP` header is stored.
Uploading a candidate does not unload live code. Restart to try it. The core
keeps it automatically after AUDIO/sample startup and five seconds of normal
UI ticks. A restart before that point restores the previous bank.
`/mods` remains a read-only view of the selected stored bank.
Core 1.7 checks the generated preimages of LFO3 and MODULATION SKIP against
resident OS 1.41 code before relocating the bank or running any initializer.
Mismatch status 11 leaves the previous bank or stock machines active.

## On-disk structure

All words are 32-bit big endian. Address intervals exclude their upper end.

**Envelope, 48 bytes**:

| Offset | Value |
|---|---|
| 0 / 4 / 8 | `SAFE` / version 2 / ABI `454c0001` |
| 12 / 16 | Exact file length / Adler32 of bytes 20..EOF |
| 20 | MAIN zone offset, exactly 48 |
| 24 | Feature mask: bit 0 sample-pool v1; bit 1 machine-ui-v1; bit 2 modules-v1; bit 3 mod-info; bit 4 safe core contract; other bits rejected |
| 28 | UI directory offset within MAIN image when bit 1 is set; otherwise zero |
| 32 / 36 | Module directory offset within MAIN image / count 1..16 when bit 2 is set; otherwise zero |
| 40 / 44 | Descriptor directory offset within MAIN image / machine count |

**MAIN zone, 32 bytes**, starting at file offset 48:

| Offset | Value |
|---|---|
| 0 / 4 / 8 | `SMZ4` / MAIN image bytes / relocation count |
| 12 / 16 | Mod-info directory offset within MAIN image when bit 3 is set, otherwise zero / exact core contract (`53594603`) when bit 4 is set, otherwise `ffffffff` |
| 20 | Sum of BE longwords from zone+32 through MAIN image end, modulo 2^32 |
| 24 / 28 | AUDIO offset relative to zone / unpadded AUDIO length |

The zone is followed by sorted non-overlapping relocation offsets, then the
MAIN image. Relocations point at even offsets holding four-byte image-relative
pointers; every target stays in the image. Directory numeric fields, parameter
numeric fields and terminated strings cannot overlap a relocation. These checks
precede relocation and execution. Only the private heap image is modified.

**MAIN directory**: `count` contiguous 40-byte descriptors:
`{type, parameter_count, name*, short_name*, group*, parameters*, on_load*, borrow,
category, rank}`. Strings and pointer fields use explicit MAIN relocations.
`on_load=ffffffff` means absent; otherwise `int on_load(const definition*)` may
initialize only private machine state and must return zero. All initializers
must succeed before publication. An init failure publishes no machine and pins
its private allocation until reboot to avoid dangling references.

Each parameter record is 24 bytes:
`{short_label*, long_label*, raw_min, raw_max, raw_default, native_formatter_alias}`.
Labels are bounded ASCII, min/default/max are unsigned 16-bit raw values in order,
and the alias is an existing native engine descriptor ID (0..1f8h). Values are
not assumed to be whole Q8 units. The initial generic SYN presentation borrows
template type 44. From core 0.7.5, the descriptor may select any native OS 1.41
menu category **1..14**; zero and values above 14 are rejected before activation.
The native icon dispatcher at `4002eed0` supports all 14 categories, including
category 13, which is unused by the stock machine table at `4024d5bd`.
The SP bank uses category 11 (samples, shared with TwinShot) and retains types
66..69. Core 0.7.4 accepted 5/11 only; older cores accepted 5 only.
Machine-specific presentation uses the optional UI directory below.

**AUDIO section**: starts at a file offset aligned to 16 bytes, then:
`{SMA3, count, exact_section_bytes, Adler32(section[16:])}`;
`count` entries `{type, absolute_update, absolute_render, 0}` (16 bytes each);
384 zero-initialized bytes for core-owned extended parameter state;
then linked machine code/data/state. Update/render must be even and inside the
code portion, with the same type set as MAIN. The complete section is padded
to 2048-byte pages with zeros; the final page ends exactly at EOF.

The AUDIO base is **4204f750h**, reserved through **42100000h**. This is inside
the native 64 MiB sample store, beyond the stock first-half sample maximum;
the 64 sample descriptors do not own this extension. MAIN allocation (envelope,
relocations and image) is capped at **2 MiB**; the AUDIO section is capped at
**723120 bytes**. The storage endpoint still accepts 16 MiB, but unsupported
banks are skipped. Allocation uses native malloc/free, retaining a 512 KiB
reserve during setup for the native sample builder. Allocation failure skips
the bank. No new RAM is inferred from zeros or missing references.

## Four SYN pages and parameter storage

The contract reserves **32 controls**, four pages of eight. Each machine gets
32 engine IDs at `1f9h+(type-64)*32` and 32 UI IDs at `2e0h+(type-64)*32`.
The first implementation can activate **1..28** controls. Positions 29..32 have
IDs and page positions reserved, but no storage mapping; declaring them rejects
the bank until a storage extension is implemented. Four pages do not imply that
four additional native Sound words are available.

| Parameter index (zero based) | Sound slot |
|---|---|
| 0..7 | `12,13,14,15,16,17,1a,19` |
| 8..23 | `36..45` |
| 24..27 | `0,11,18,46`, scoped to loaded machine types |
| 28..31 | Reserved; no allocation or native write |

Only a SYN view's owned page vector is resized; all pages retain native ID 6.
Knob lookup and page actions use a scoped layout; LEVEL remains native. Empty
knobs return zero. Native Sound codecs run first; added words are then copied
within the unchanged 172-byte record. Extended storage IDs and lock lanes use
the original native geometry. These application paths still need hardware QA.

## MAIN/AUDIO transport

The complete bank is validated and resident in MAIN before native project and
sample loading. The boot sender keeps sample pages unchanged, then appends
machine pages `4000+k`. CPU2 copies them to the fixed AUDIO base, runs its native
sample parser, validates the complete machine section/checksum, pushes its data
cache and publishes update/render entries before native voice initialization.

At run time the first eight native controls use the native frame. Only a
machine's defined extended values use two 512-byte banks at MAIN `10000c00/10000e00`
(AUDIO `c00/e00`), with generation, type and checksum validation. The unchanged
432-byte native frame uses byte `1a9` to name the newest complete generation.
No bank is written until native AUDIO-ready at `43bdc824` is set; clearing it
marks both banks stale. Telemetry uses `800..bff` and cannot overlap these
banks. IRQ paths perform no allocation, waits or IO.

Transport v3 (core 0.12.4). MAIN reads every effective value every 32-frame
block (LFO slots `11`/`18`, locks and ramps keep block resolution) and compares
it with a shadow held in the bind allocation. Only a changed value, type or
active mask creates generation `n` (1..254), written to bank `n&1`: header
words 0/1 are cleared first, then only the rows that bank holds stale, then
mask, generation and magic `MP33` (0.12.7's word-granular writes were reverted
in 0.12.9). A bank is never rewritten before a newer
generation is complete, and stable values cause no FPGA access at all. AUDIO
returns at once while the frame names the generation it accepted. Otherwise it
takes the frame's bank, or the other bank holding the next generation when the
frame lags; it rechecks the header after every row, and a checksum or torn read
keeps the previous row and retries on a later block. A track with eight or
fewer controls publishes no extension; stock-only blocks write nothing and tag
zero. For 27 controls, 19 extra values are packed into ten longwords; only the
unused half of the last word is zero padding. Record word 0 contains
`{extended_count:u16, type:u16}`; words 1..12 have capacity for 24 values and
word 13 is the sum of track index, metadata and used payload words.

The fixed AUDIO service directory at `40040f40` contains `{ABI, extended_getter,
32, 24}`. AUDIO machine code can call `st_machine_extended(params)` to obtain
the 24 received extended words, indexed from parameter 8; null means unavailable.
From 0.12.4 a row stays null until one was accepted for the machine type now on
that voice: a machine keeps its own defaults instead of reading zeros. (FM TONE
derives ratio = table + (offset - 0x4000) << 12, saturated at zero; offsets of
zero silence the carrier.) The AUDIO companion uses the device's declared
runtime window `40040860..40041050`: code to `40040e00`, scheduler tables,
the two ABI directories, then directory entries and glue from `40040f60`.
That window is not unused on the stock OS: it is the zero tail of the
base-width filter's pole table (`40040060`). From core 1.7.2 the filter's
low-pass lookup is bounded below the window (core README, "Core 1.7.2").
Native update ABI: pitch, voice pointer, params pointer on the C stack. Render:
output pointer, voice pointer. Block: 32 frames at 48 kHz. Machine authors own
their reset/retrigger/release state and must preserve the native voice geometry.

## Validation scope

The parser checks structure, checksums, bounds, alignment, duplicate types,
cross-CPU entry points, relocations and metadata before executing code. Native
host tests exercise actual parser/boot C and generic lookups/pages/codecs with
explicit IO/UI/callback doubles. They execute no target instructions. Static
ColdFire decoding, SDK lint and package verification cover source-built images.
No emulator, hardware upload, physical UI check or eMMC write is performed.


## Core 0.6 sample service requirement

Envelope word `+24` is a feature mask. Zero retains compatibility with the
original ABI; bit 0 requires the generic core sample-pool v1 service. Core 0.8
also accepts bits 1 and 2, with their directories described below. Older cores
reject features they do not implement. No runtime patch plan is allowed.
Core 0.4/0.5 reject a bank requesting bit 0. Core 0.6 rejects that bank when its
AUDIO service image was not provided by the explicit companion build.
The parser validates structure without executing anything; activation checks
service availability before allocating/running machine code.

A manifest may declare `smz.machines` as a list of `{descriptor, update, render}`
entries sharing `sources` and `smz.audio.sources`. The existing single
`descriptor`/`audio.update`/`audio.render` form remains accepted. The optional
`smz.features: ["sample-pool-v1"]` requests bit 0. Compile without firmware:

```sh
python -m elekloader.sdk.syntakt.smz --preset samplers --out out/samplers.smz4
```

The generator uses the Syntakt OS 1.41 device/toolchain profile directly. It
compiles and audits all source objects, links MAIN with relocations, links
AUDIO to its owned bank region, checks source hashes and emits an output hash
report. There is no firmware extraction in this path. Optional `--stock` only
verifies a Syntakt target input; it is not needed and no donor OS is used.
The separate firmware builder continues to require the official Syntakt SYX.

`st_machine_sample(slot)` calls the fixed core AUDIO service at `40040f50`;
this returns a borrowed `{frames, pcm}` entry for a 128-position project pool.
The [pool, project persistence and transfer](SAMPLE-POOL.md) live in the core,
outside SMZ4. There is no machine-specific patch in this file.

## Machine UI directory (core 0.8)

With bit 1 set, the directory contains one 32-byte `st_machine_ui` per machine,
in the same order: `{type, pages, layout*, draw*, idle*, dial*, format*, policy*}`.
`pages` is 1..4. Layout contains eight zero-based parameter indices per page;
`ffffffff` denotes an empty control. Pages may repeat controls. The core owns
navigation, native storage and dispatch; all machine-specific painting and
formatting live in the SMZ. Optional callback pointers use `ffffffff` for absent.
Pointers must be explicitly relocated, inside the image and even; layouts and
parameter arrays are aligned to four bytes. SDK descriptor types enforce their
alignment. Numeric fields/layout entries cannot be relocation targets.

Callbacks and the borrowed service API are declared in
`syntakt/machines.h`. The API exposes cached sample names, 96-bin waveforms,
bounded idle scanning and asynchronous AUDIO playhead snapshots. Draw never
reads storage or waits. Idle returns nonzero only when it needs a redraw.
From core 0.10, idle callbacks run through `ev_tick`, traversing the native live
visible list. No native idle vptr is replaced or cloned; no view pointer is
retained between ticks. UI callbacks cannot push/pop views while iterating it.

For UI-task operations on existing per-step locks, `syntakt/pattern.h` declares
borrowed OS 1.41 track/model and native view/proxy get/set/clear services. They
use the same resident machine-ID adapters as ordinary p-lock editing. They do
not add a resident API, hook, lock matrix or undo transaction; callers must
guard the view/pattern context, bound step indices and check write results.
The sampler's SLICE allocation menu uses them from the existing pre-KEY event.

Core 0.8.1 supplies service API version 2, preserving the version-1 prefix and
appending `format_context`. Callbacks may read it only when `version >= 2`:
`ST_UI_FORMAT_POPUP` selects the modal value, `ST_UI_FORMAT_CONTROL` the direct
text at the control. This allows machine-owned formatting to keep a short
label fixed while resolving a sample name for the modal. Other callbacks receive
the popup/default context. The on-disk UI descriptor and SMZ feature bits do
not change. Callbacks using only version-1 fields remain usable.

Core 0.9.0 supplies service API 3, appending `edit`, `clear`, `bind_input` and
`bind_codec`. Machine-owned input callbacks run only from that machine's active
native SYN view. Synchronous explicit edits and clears retain native Sound,
trig/page lock, undo and preview routing. `st_machine_action_codec` can encode
a compound scalar for page transactions; its get/set/range callbacks apply only
while the corresponding parameter appears in the transaction's page layout.
Outside that scope every control retains its ordinary scalar range/value.
Module initialization failure clears registrations before any AUDIO publication.
The disk format and feature bits remain unchanged. A consumer must check API
version before reading the appended fields and reject an older core in init.

Core 0.10 supplies UI API 4. `edit`, `clear` and `bind_codec` retain their API-3
offsets and native behavior. The former `bind_input` field always returns zero;
its registry/dispatchers are removed. This safely rejects an older FM bank that
requires that registration. Rebuild it to use the existing event subscriptions
below; existing sampler UI descriptors require no migration.

## Optional modules (core 0.8)

Bit 2 enables a directory of 1..16 16-byte `st_smz_module` records:
`{name*, init*, subscriptions*, count}`. Each has up to 16 subscriptions
`{event, callback*}`. Names are bounded ASCII (32 bytes including terminator);
callbacks and initializers are validated relocated pointers. `init=ffffffff`
means absent; a zero-count subscription pointer must be `ffffffff`.
Initializers return zero on success and initialize private state only.

The eleven event IDs are defined in `syntakt/modules.h`, in order: tick, draw,
key, encoder, SETTINGS, render-in, render-out, FS-route, FS-write, FS-written,
MAIN-mix. Callback signatures match the resident hook bus. IRQ callbacks must
not allocate, wait or perform storage IO. After all initializers succeed, the
core merges subscriptions into existing event lists once at boot. Core-owned
RAM operands select those lists, avoiding an additional dispatcher per event.
No arbitrary firmware address or patch destination comes from the file.

Core 0.10's module service API 2 permits **`event | ST_EV_VIEW`** (`100h`) for
DRAW, KEY and ENC only (serialized IDs `101h`, `102h`, `103h`). These select a
context of the existing event, not another collection:

| Subscription | Signature | Native phase |
|---|---|---|
| `ST_EV_DRAW | ST_EV_VIEW` | `void f(view, bitmap, zero)` | After this view, before higher overlays |
| `ST_EV_KEY | ST_EV_VIEW` | `void f(view, event, zero)` | After this native parameter view handles the key |
| `ST_EV_ENC | ST_EV_VIEW` | `int f(view, normalized_event, parameter_id)` | After focus/page resolution, before native editing; nonzero consumes |

Initializers requesting only ordinary event IDs receive module API version 1,
preserving strict older initializers. Those explicitly requesting `ST_EV_VIEW`
receive version 2. Both receive the current UI service API via its versioned
pointer; no ordinary callback signature changes.

Native-view events are scoped to the native SYN/parameter-view adapters; they
are not global input notifications. A module filters its machine, page and
parameter. The core preserves native overlays, lock/undo routing and event
normalization. Ordinary event IDs/signatures are unchanged and their callbacks
do not run again in the view phase. Modules need service API 2 / UI API 4 before
depending on these contexts. Older cores reject the new event IDs during file
validation. All other flag combinations remain invalid.

At boot a contextual callback receives a high-bit marker in the core-owned
existing event list. Global dispatch skips marked entries; native context
adapters select marked entries and remove the marker before calling. This
marker is never supplied as a callback pointer in the file: its relocated,
even, in-image pointer validation remains unchanged. The envelope, feature bits
and disk ABI are unchanged. No second module scan or machine input registry
is introduced.

A module-only file has machine count zero and a minimal SMA3 section containing
its header and 384 zero bytes; no machine AUDIO pages are published. Existing
banks remain accepted. Older cores reject new feature bits before execution.
Build the example separately or combine it with machines:

```sh
python -m elekloader.sdk.syntakt.smz --module examples/smz-test-st --out out/module.smz4
python -m elekloader.sdk.syntakt.smz --preset samplers --module examples/smz-test-st --out out/mixed.smz4
```

An SMZ module uses normal `sources` and `smz_module: {"descriptor": "symbol"}`
in its manifest. Resident `sites`, `subscribe` and patch descriptions are not
accepted by this builder. The source descriptor supplies the bounded event IDs.

A machine manifest can also list `smz.modules: ["module_symbol"]`. Those module
descriptors share its compiled MAIN sources and use the same validated directory
as `--module` inputs; no duplicate source compilation is needed.

## Compiler profile

Core 0.12.9 compares all tracks every 4th block and trigged or LFO-routed
tracks in between (rows rewritten whole again). Core 0.12.8 keeps slot 0 as
the native None destination (FM RATIO OFFSET C is
read from the Sound mirror). Core 0.12.7 writes only stale words and a
two-write header. Core 0.12.6 adds
host API 3 (sender telemetry, appended). Core 0.12.5 compares
extended controls without per-value lookups (same
protocol and semantics). Core 0.12.4 keeps API 7/3/2, all hooks and SMZ compatibility; it changes the
extended-control transport (above) on both CPUs. Existing banks are unchanged.

Core 0.12.3 keeps API 7/3/2 and all hooks. Empty native lists bypass C through
the same stock replays; availability publishes atomically with module lists
and includes resident subscribers. Native IDs bypass machine metadata C even
with a full bank, and stock-only frames perform no extended mailbox writes.
Existing machine/module banks remain compatible, including System info alone.

Core 0.12.2 retains API 7/3/2. Confirmed machine selection now completes the
native default transaction for controls 9..28 from each machine descriptor,
through the existing slot lookup adapter. Saved Sounds/projects are unaffected.
Use the matching 0.12.2 bank, which fixes imported POLY guard jump sizes.

Core 0.12.1 supplies UI/module/host API 7/3/2, preserving all earlier fields.
Contextual ENC now follows the actual
native editor virtual resolver once, covering SourcePageView as well as base
parameter/TRIG views. Source getter calls use the same selected-view layout as
navigation/actions. The shared draw/sync/idle ownership gate defers to the native
selector predicate; render callbacks receive an already-qualified view/page.
Machine renderers should use that supplied page rather than select one again.
The shared editor gate applies selector ownership and native page guards before
any ENC subscriber. POLY relies on this ordering to block slave edits before
FM can consume its paired ratio. Its initializer requires UI API 7; earlier
cores decline the matching new bank cleanly rather than expose unguarded input.

Core 0.12 provides UI API 6, module API 3 and host API 2. A native-view DRAW
subscription now runs after each complete view, below later overlays, using
the existing DRAW list. Base ParameterView emission and owned recorder bridges
are removed. ENC context moves to the existing control resolver, scoped to the
native editor and TRIG wrapper, allowing virtual controls with native ID zero.

Serialized KEY ID `302h` (`KEY | ST_EV_VIEW | ST_EV_PRE`) selects a consuming
pre-native callback `int f(view, event, 1)`. Existing KEY ID `102h` stays a void
post-native notification. Core-owned tagged entries use low bit 1 only for
pre-KEY; serialized callback pointers must still be even and within the image.
Other PRE flag combinations remain invalid. The same KEY list owns both phases.

Ordinary event ID **11** is the new shared native transaction collection:
`int f(operation, const u32 *arguments, u32 *result)`. Nonzero supplies the
result; zero continues to the next subscriber, then the core's exact stock
replay. Fixed operation IDs/contexts are defined in
[`native.h`](../../elekloader/sdk/include/syntakt/native.h). They supply native
sequencer/pattern/publication/model contexts unavailable in the old events.
No firmware address, patch destination or runtime patch plan enters the file.
Handlers inherit the native caller's task/IRQ context and borrowed arguments.

Host API 2 appends clock snapshot, settings row creation, pool-busy/mailbox-owner
references and explicit native-original replays to its unchanged 44-byte prefix.
The full table is 64 bytes. Storage/telemetry remain task-only; a native replay
requires its original task/IRQ context. Initializers using PRE/native receive
module API 3; view-only modules receive 2 and ordinary-only modules receive 1.
All new event IDs are rejected by older cores before activation. Disk descriptor
sizes, features and the ABI identifier are unchanged. Event lists and assembly
operands publish atomically in one existing core IRQ exclusion.

Core 0.11.0 provides **UI API 5**. Its unchanged UI API 4 prefix is followed
at offset 40 by a pointer to `st_machine_host_api` (version 1). This borrowed
table exposes existing HZFS validation/IO, CRC, pool read/write/apply/readiness,
preview stop and contextual event emission. See
[`host.h`](../../elekloader/sdk/include/syntakt/host.h). Storage services are
task-only; callers serialize transactions with native transfers using existing
FS events. Do not read the appended pointer on an older UI API.

An owned native view can bridge its drawing into contextual `ev_draw` through
the host's `view_event`, retaining native modal ordering. This uses the same
marked DRAW list as parameter-view adapters. It does not introduce another
collection or replace the native input/idle methods. The
[recorder](../recorder-st/README.md) demonstrates this with seven existing
subscriptions and no firmware patch plan. It declines initialization on UI API
versions below 5. Disk geometry, features and ABI identifier are unchanged.

All Syntakt C sources use `-mcpu=54415 -mtune=cfv4 -O3` with
`--param=max-completely-peel-times=4`. The latter keeps long bounded loops from
expanding into expensive chains of absolute accesses and preserves the fixed
AUDIO service budget. This is a speed-oriented policy, without relaxed math.
The MCF54415 has EMAC and no FPU; use integer/fixed-point DSP. Objects and linked
C function ranges in the bank are decoded to reject unsupported instructions.
Static checks do not prove every runtime path or measure CPU cycles on hardware.

## Mod-info directory (feature bit 3, core 1.1)

Bit 3 adds the versions of the bank's mods, so System info can list them. The
zone word at file offset 60 (zone+12) is then the directory's offset within the
MAIN image; it is zero when the bit is clear. The directory is zero-terminated
and has no count: each record is 40 bytes, the mod's `id` in 24 bytes and its
`version` in 16, printable ASCII and NUL-padded. A record whose first byte is
zero ends it, and at most 16 mods fit. Its bytes are plain data, so no relocation
may touch them. `smz.py` writes one record per mod folder (machine and module
folders alike), taking `id` and `version` from its `mod.json`. Core 1.1 checks the
directory during validation and publishes the loaded bank's list to modules as
`bank_info` (host API 12); the core's own mods are `core_info`. Cores before 1.0
reject feature bit 3 before execution, and a bank without it lists only the core.
