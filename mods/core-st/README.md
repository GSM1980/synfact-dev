# Syntakt core 0.12.22

Version 0.12.19 adds host API 8 `native_query`, a pointer to the existing
synchronous native bus. It adds no hook site or operation number. OWNERS with
null arguments still returns the Sound owner map; `{1}` requests the LFO-state
owner map. POLY and LFO3 use it to share Sound configuration while running
independent voice oscillators. Extended parameter slots 17/24 use the Sound
owner's base value and the LFO state's own output. Module/UI APIs and the host API's earlier prefix remain
unchanged. Banks using independent POLY require this core and host API 8.

Core for Syntakt OS 1.41. Its sources and `mod.json` use the normal elekloader
SDK, like the other cores. Python build/packaging tools live in the SDK.

Ordinary mods use the hook bus and share **64 KiB** at
`46000000..46010000`. They work whether or not `/mods` contains a valid
code bank. An SMZ can contain machines and optional MAIN modules; core storage,
transfers and sample loading remain functional without that file.

Core 0.8 delegates machine-specific formatting, graphics and 1..4 SYN page
layouts to code in the SMZ. The sampler bank uses two pages with the same eight
controls, including a waveform page. Generic wave caches and asynchronous
playhead transport remain core services. Optional SMZ modules join the existing
event lists once at boot; there is no additional dispatcher on each event.
See [SMZ4.md](SMZ4.md) and [the module example](../../examples/smz-test-st).

## Project pool and sample machines

Core 0.6 adds the generic 128-position project pool, Sample Manager in SOUND
and SETTINGS, and long PCM loading through the native sample progress bar.
The pool and transport work without an SMZ bank. Build the four optional SP
machines from `mods/samplers-st` with `python -m elekloader.sdk.syntakt.smz
--preset samplers --out out/samplers.smz4`; no firmware input is needed.
See [SAMPLE-POOL.md](SAMPLE-POOL.md) and [samplers-st](../samplers-st/README.md).

Version 0.6.1 restores the native 2048-byte page shift before returning from
the pool mapping hook to the stock sample sender. This also applies when no
machine bank is present; 0.6 incorrectly reused the stock page count as the
shift and could send incorrect stock sample data to AUDIO.

Version 0.7 replaces the initial overlay with an owned native three-pane sample
manager, multi-selection, batch add/remove and streaming audition on FUNC+YES.
It also exposes a read-only machine-loader snapshot:

```sh
python -m elekloader.sdk.syntakt.mods status
```

This distinguishes missing metadata, bank rejection, heap/IO/init failures and
missing AUDIO services. Boot registration and selector visibility of the four
machines are confirmed on a device. No invalid file bypass exists.

## Long-sample storage

The core implements HZFS v1 in the high eMMC zone, with automatic creation
on first use only when both catalogue windows are blank. Named samples can
be uploaded, read back, listed, verified and deleted with the SDK tools in
`elekloader/sdk/syntakt/`. This works without an SMZ4 file. Stock-slot uploads
are cropped to five seconds by the PC tool and independently bounded in the
core before stream creation, protecting `/mods` after slot 64.
See [HZFS.md](HZFS.md) for the disk format, native API and transfer commands.

## Optional machine bank

At the application startup site `400826a6`, eMMC is ready and neither the
saved project nor the sample blob has been loaded:

1. Ensure/mount the high-zone sample filesystem, then read `/mods` metadata and silently validate the complete SMZ4 bank.
2. Missing, incompatible, corrupt or unsupported bank: skip all machine
   activation, then continue the original project/sample startup.
3. Valid bank: allocate private native heap memory and silently read the bank
   in chunks. No display or LCD buffer changes occur during bank loading.
4. Revalidate the RAM copy, relocate MAIN, initialize private machine state
   and publish the generic machine registry.
5. Continue the original project load and original 64-sample loading path.

MAIN/AUDIO transfer uses the native boot page protocol. The machine AUDIO
section is already in the validated MAIN buffer before sample loading starts.
Its delivery to CPU2 happens during the native transfer, after the sample pages,
using page indices `4000+k`. CPU2 validates and installs the complete bank
before its native voice initialization. No separate protocol or emulator is used.

The format reserves **32 controls / four SYN pages**, eight knobs per page.
This first storage implementation maps **28** controls to existing Sound words;
positions 29..32 are reserved and banks requiring them are rejected. A 27-control
machine fits. See [SMZ4.md](SMZ4.md) for slots, transport, limits and future work.
The fourth page is present when a machine has 25..28 controls; unused knobs are
empty. No new sound engine is embedded in this firmware.

## Build

Core MAIN alone, with the same SDK as the other cores:

```sh
python -m elekloader.sdk.build mods/core-st --stock Syntakt_OS1.41.syx
python -m elekloader.lint mods/core-st/out/core-0.12.13.elemod --stock Syntakt_OS1.41.syx
```

The optional CPU2 machine services require an explicit companion build:

```sh
python -m elekloader.sdk.syntakt --stock Syntakt_OS1.41.syx --eft /path/to/elektron-firmware-tool
```

Add `--system-info` for the AUDIO telemetry companion (include
`--module mods/system-info-smz-st` in the bank for its UI), or `--mod path/to/mod`
for another ordinary SDK mod. These ordinary mods remain in the 64 KiB area.
The explicit builder also compiles `audio-pool.c` from source into its core AUDIO
service image, and links the normal `core` and `machines-st` `.elemod`
modules, compiles the core's AUDIO sources, and verifies both expected images.
The `machines-st` capability declaration belongs with that companion; do not
install it through the MAIN-only patch CLI or GUI. A core built without that
declaration keeps the machine loader disabled and retains ordinary mods.

Host tools live in `elekloader/sdk/syntakt/`: the firmware builder (`python -m
elekloader.sdk.syntakt`), `smz`, `mods_object`, `long_samples`, `data_rpc` and
the source AUDIO builder (`audio.py`). The ColdFire compiler prefix is selected by
`ELEKLOADER_CROSS`, as with the normal SDK. The compact compressor is a local
explicit dependency. Build outputs must stay in ignored `out/`. Firmware and
SMZ builders are offline; the separate `long_samples` CLI opens MIDI only for
an explicitly requested transfer/storage command.

### Toolchain and flags (Windows host)

The compiler is GCC 13.2.0 `m68k-elf` (crosstool-NG) from SGDK 2.11, in
`C:\Desarrollo\syntakt-dev\toolchains\sgdk211\bin`. Its tools carry no prefix
(`gcc.exe`, `as.exe`, `ld.exe`) and `gcc` finds `cc1.exe` only through `PATH`,
so put that directory on `PATH` **and** give its full path as the prefix:

```sh
export PATH="/c/Desarrollo/syntakt-dev/toolchains/sgdk211/bin:$PATH"
export ELEKLOADER_CROSS='C:/Desarrollo/syntakt-dev/toolchains/sgdk211/bin/'
```

Every MAIN/AUDIO C source of the core and of SMZ banks compiles with the
Syntakt profile's flags (`elekloader/devices/syntakt.py`), never typed by hand:
`-mcpu=54415 -mtune=cfv4 -O3 --param=max-completely-peel-times=4
-ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie -fomit-frame-pointer
-Wall` (no FPU, no relaxed math). Each build records them: `cflags` in the
core's `verification.json` and in the bank's `synfact-machines.smz4.json`;
compare them with the previous build. `--eft` takes the executable
(`elektron-firmware-tool.exe`), not its folder. Host tests compile with the
i686 clang of llvm-mingw (`--cc`). Full sequence:

```sh
python -m elekloader.sdk.syntakt --stock Syntakt_OS1.41.syx --eft elektron-firmware-tool.exe --system-info --out out/core-st-<ver>
python -m elekloader.sdk.syntakt.smz mods/samplers-st mods/fm-tone-st --module mods/recorder-st --module mods/lfo3-st --module mods/poly-st --module mods/system-info-smz-st --stock Syntakt_OS1.41.syx --out out/core-st-<ver>/synfact-machines.smz4
python -m elekloader.lint mods/core-st/out/core-<ver>.elemod --stock Syntakt_OS1.41.syx --with mods/machines-st/out/machines-st-0.1.elemod --json > out/core-st-<ver>/lint.json
python tests/test_st_common_regressions.py out/core-st-<ver> --cc i686-w64-mingw32-clang.exe --synfact <synfact>
```

## Core services

| Event | Handler | Context |
|---|---|---|
| `ev_tick` | `void f(void *ctrl)` | UI; set byte `ctrl+20h` to request redraw |
| `ev_draw` | `void f(void *bmp, void *ctrl)` | After the native frame |
| `ev_key`, `ev_enc` | `int f(void *brain, void *event)` | Nonzero consumes the event |
| `ev_settings` | `void f(void *menu)` | Add rows with `core_additem` |
| `ev_render_in`, `ev_render_out` | `void f(void)` | MAIN audio IRQ, no allocation or IO |
| `ev_fs_route`, `ev_fs_write`, `ev_fs_written` | See STORAGE.md | Native Data RPC tasks |

From 0.10, optional subscriptions may select the native-view context of the
existing DRAW/KEY/ENC collections with `ST_EV_VIEW`. Ordinary subscriptions
keep their signatures and run only at the global site. Contextual DRAW runs
after its native view, before higher overlays; KEY after native view processing;
ENC with the normalized event and selected parameter before native editing.
The same collection stores both kinds of subscriber, marked in core-owned RAM;
there is no separate input registry. See [HOOK-AUDIT.md](HOOK-AUDIT.md).

`core_clock_snapshot(out[3])` returns cumulative native timer, parked idle
and MAIN audio-IRQ-in-idle counters. The stock timer remains running as configured.
`fw_*` exports cover native UI, eMMC, allocation and the required view helpers.
Machine lookups, SYN pages, Sound/lock mapping and parameter transport are generic
core source; their descriptors and specific MAIN/AUDIO code come only from SMZ4.

The key ID is a 32-bit word at `event+12`, flags at `event+16`. YES=0Ch,
NO=0Dh, FUNC=1, PAGE=18, trigs=24..39, encoder pushes=40..47, LEVEL push=48.
Hardware reports press=1, repeat=9, release=10h. Encoder turn IDs are 1..8
and LEVEL=9, with a signed delta at `event+16`.

## Placement and verification

The shared `../core/core.s` copier moves `.run` and clears the mod's own `.bss`
in the 64 KiB reserve before continuing natively. Native copy/clear routines,
load destinations and vectors retain their stock behavior. `NO_DTIM0` preserves
the native timer configuration.

Stock SHA256:
`8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e`.
The explicit companion only changes MAIN (3) and bounded AUDIO (7) application
sites. Sections **1/2/4/5/6/8**, their stored bytes, all load destinations and
the version/container header are checked against the official file. Bootstrap,
updater and boot stub remain identical. Require at least 4096 bytes of MAIN
in-place decompression margin and retain the conservative stock container budget.

Build/link/lint, native host tests of C logic, static instruction decoding and
container checks do not establish hardware acceptance. The new machine loader,
machine UI, extended parameter transport and native UI actions require physical
confirmation. No emulator run, flash, eMMC write or MIDI transfer is performed.


## MAIN sample audition event

`ev_main_mix`: `void f(void)`, MAIN audio interrupt, at application site
`4008fb04` before native output gain/mix. The core owns the site, preserves
D0/D1/A0/A1/A2 and replays the exact two displaced pushes. Subscribers must not
allocate, wait or read storage. The sample manager subscribes its bounded
32-frame stereo mixer on `ST_PREVIEW_BUS` (`8000dfd0`, `<syntakt/mix.h>`):
`8000dad0` went through the FX track on the device; the x8 stage there is
matched by x5/4 to keep the level. Its ordinary UI `ev_tick` subscription refills the
private PCM ring and stops audition when the manager loses foreground.


## Mixer FX-send events and the FX codec (1.2)

`ev_track_send`, `ev_delay_in` and `ev_reverb_in` (`fx-send.s`) run in the
MAIN audio interrupt at `4008f3a0`, `4008f828` and `4008f952`: a track's send
gains once per track and block, then the delay's and the reverb's complete
input bus before the native effect renders it. The track stub fills a row of
`core_track_sends` per track and calls once a block, at track 11 (a call per
track cost the chorus about 600 instructions a block). The stubs replay the displaced
instructions exactly (two MSACs at the track site; the two sites' JSR tail
into the stock delay and reverb), so with no subscriber the unit runs as stock.
Arguments, contexts and limits are in [ADAPTING.md](../../docs/ADAPTING.md)
(core 1.2) and `<syntakt/mix.h>`. `fx-codec.s` owns the Kit's FX record
codec entries `400a6d96`/`400a6d2e` and notifies the parameter providers
(queries 28/29, `<syntakt/host.h>`). `tests/test_st_chorus_emu.py` runs the
three stubs against the stock instructions in Unicorn.


## Machine registration API

`core_machine_bind(descriptors, count)` is the core registration entry point.
The boot loader calls it after validating, relocating and retaining the SMZ
image. Each `core_machine_def` may provide an optional `on_load` initializer;
it can initialize private state and returns zero on success. The core then
publishes the whole directory and supplies the selector/parameter lookups.
Registration is an API, not an event subscription or an SMZ firmware hook.
`mods status` separates boot registration from subsequent selector queries.


Version 0.7.2 reports native sample progress on percentage changes, including
short batches. The first message switches the native title, subsequent messages
update its bar, and 100 closes it. This does not remove the stock sample builder's
synchronous preparation work or prove interactive boot UI responsiveness.
The read-only snapshot also records availability acceptance, final selector vector,
row count and name/category query masks. This distinguishes registration, native
filtering, vector construction and visible label lookup without changing the list.

Version 0.7.3 removes the early machine-bank dialog. SMZ validation and loading
are silent; the existing native sample progress bar is retained unchanged.

Version 0.7.4 accepts sample category 11 (TwinShot) in machine descriptors.
The SP bank 0.2 uses that category, retaining machine types 66..69. Category 5
remains accepted for existing banks and the generic SYN example. Updating the
category requires uploading the rebuilt bank; firmware alone does not rewrite it.

Version 0.7.5 accepts the complete native OS 1.41 menu-category range, 1..14.
Each machine's SMZ descriptor chooses its category. Zero and out-of-range values
are rejected before activation. Existing banks, including SP bank 0.2, remain valid.

## Compilation and CPU work (0.8)

The exact target is MCF54415, ColdFire V4 with EMAC and no FPU. All C uses the
same speed-oriented profile: `-mcpu=54415 -mtune=cfv4 -O3`, with
`--param=max-completely-peel-times=4`. This bounds large loop expansion while
retaining other O3 optimizations; it is not a per-function size optimization.
No fast-math is enabled. Static instruction audits complement compiler targeting;
they do not prove every runtime path or measure on-device cycle counts.
The SDK rejects odd code ranges and incomplete final instructions. Core AUDIO
and pool AUDIO C functions are checked again after linking, as are the bank's C
functions. See [COMPILER-AUDIT.md](COMPILER-AUDIT.md) for the resumed review,
coverage, reproducible build results and remaining physical checks.

CRC processing uses a shared byte table with the same outputs. Machine lookup
uses a direct type directory. Extended control snapshots process only the
controls defined by each machine: eight use the native frame; 27 use that frame
plus 19 extra values. Idle waveform redraw occurs only after a visible change.
Wave caches, private view tables and page layouts use owned native heap, with
allocation failure handled, instead of occupying the fixed code reserve.

The 0.8.0 delivery with System info occupied **63,468 / 65,536 bytes** of the
resident reserve, leaving **2,068 bytes (2.02 KiB)**. The build report records
the actual layout for each selected mod set. Machine code/UI lives separately
in the SMZ allocation; these figures do not include native-heap working data.

Version 0.8.1 chains the pool value formatter into the generic machine formatter
before stock fallback. The format entry now has one declared hook; previously
the later pool hook displaced the machine hook, so popup values stayed numeric.
UI service API 2 distinguishes popup values from direct control text. Sampler
bank 0.4 uses this to keep SAMP fixed and show the selected sample name in its
popup; PLAY and WERP MODE show the playback mode name. Existing disk descriptors
and UI callbacks retain their layouts.

Version 0.8.2 initializes missing project pools with stock samples 1..64 in
positions 1..64 and positions 65..128 empty. Existing valid records are preserved
without migration. Sample Manager adds long files to the first free position
65..128 and protects positions 1..64 from removal, in single and batch actions.
Native TwinShot's original stock selection/direction is unchanged.

Version 0.8.3 fixes the actual NEW PROJECT path at `4001d16a`. The older hook
at `40018ab0` constructs cached projects and did not cover the live initializer;
a valid pool from the preceding project could survive NEW PROJECT. The new
wrapper preserves both native arguments and the return value, runs the native
body first, then resets the working pool to stock 1..64 and synchronizes AUDIO.
Cached-project construction only writes its own defaults and no longer syncs
the unrelated working pool. Loading an existing valid pool still preserves it.

Version 0.9.0 appends UI API 3 services for explicit native parameter edits,
paired clear operations and machine-owned input callbacks. A registered compound
value codec applies only within the existing native page transactions, preserving
separate Sound/lock scalars outside that scope. The file owns all value encoding;
the core owns ABI preservation and native dispatch. Failed module initialization
clears the callback registry. No new machine-specific firmware site is added.
See [FM TONE](../fm-tone-st/README.md) for the first consumer.

The local core 0.9.0 + System info candidate occupies 65,156 / 65,536 bytes,
leaving 380 bytes in the resident reserve. FM code/data use the separate SMZ
allocation. All target C still uses `-O3 -mcpu=54415 -mtune=cfv4`.

Version 0.9.1 routes four inline short-label lookups through the machine
registry: LFO destination widget `4007c272`, Sound modulation widget `4007c3c0`,
selected Sound Setup row `400dffda` and group/name line `400de5e0`.
Extended IDs now use each descriptor's short label instead of native entry 0
(`ERR`). Native IDs replay each exact stock block and continuation, including
the selected row's label at `48(SP)` and depth register `D5=7f00`.
This generic core fix covers every valid machine parameter without changing
SMZ descriptors, values, locks or saved data. The previous FM + SP bank works
unchanged. The new System info build uses 65,428 / 65,536 bytes (108 free);
AUDIO is byte-identical to the 0.9.0 candidate. Target flags remain unchanged.

Validation: `tests/test_st_destination_labels.c` tests all 60 current FM/SP
short labels, native/invalid/unused IDs and bank absence. The final-image test
`tests/test_st_destination_hooks.py BUILD_DIR` checks all four installed jumps,
exact stock replays/internal branches, registered label calls, stack arguments
and row results in the delivered SYX. Native C and static instruction tests do
not execute ColdFire or the full views; physical display verification remains
pending. Unit tests, instruction audits, NEW PROJECT regression and lint pass.

Version 0.9.2 preserves the already selected engine ID in native editor resolver
`40038dae`. At its SYN-classification call (return `40038dc2`), a registered
machine parameter returns zero so the resolver returns the explicit input ID.
Previously it entered the native SYN translation, which read the machine's
first-page source layout and replaced SYN 2/3/4 parameters with SYN 1 controls.
The same path covers ordinary edits and explicit B1/B2 edits. Other callers
retain SYN classification; stock/unregistered IDs keep the exact native fallback.
The existing page selector, native lock/undo routing and page actions remain
in use. The fix is generic to every loaded machine and needs no SMZ change.

`tests/test_st_syn_pages.c` uses the production registry/page selector and real
FM/SP UI descriptors to check all four FM pages, offsets, envelopes, empty
controls, both SP pages, LEVEL and stock fallback. The final-image check
`tests/test_st_syn_page_hook.py BUILD_DIR` verifies registration before bypass,
the exact caller scope, stack/register restoration, native identity return and
classification fallback in the delivered SYX. Physical editing/locks still need
verification on the unit. The 0.9.2 + System info build uses 65,412 / 65,536 bytes
(124 free), with the same `-O3` profile and byte-identical AUDIO as 0.9.1.

## Version 0.10.0: reuse the core events

FM's paired encoder and key handling now subscribes to `ev_enc` / `ev_key`.
The `bind_input` registry and its dispatchers are removed. UI API 4 keeps that
old field at its ABI offset as a refusal stub, so old FM banks fail init safely;
rebuild/install the matching FM bank. Module service API 2 documents the opt-in
native-view subscription contexts. Modules requesting only ordinary events
still receive module API version 1, preserving older strict initializers.
Modules requesting `ST_EV_VIEW` receive version 2. Old ordinary module
subscriptions and sampler banks remain readable.

The extra encoder patch at `4003b96c` is removed; those six firmware bytes are
stock again. The already-owned cached-parameter entry supplies the resolved
view/event to the encoder collection and selects the increment policy only for
the native editor caller. Other cached-parameter and formatting callers retain
their original alias. The native SYN draw and key adapters feed the existing
DRAW/KEY collections at their required layer/order, rather than calling private
machine input dispatchers. These context adapters remain necessary for native
modal layering and post-clear notifications.

Machine waveform scanning/playhead updates run from `ev_tick`. No native idle
vptr/table is cloned or replaced, and no view pointer is retained between ticks.
The live native visible list supplies ownership and visibility. This follows
Digitakt's `machine-pages` separation of periodic work on the existing tick and
shared native adapters for view-specific metadata/drawing.
Sample Manager audition refill also moves to `ev_tick`; its private idle
override is removed, retaining the stock idle/lifetime methods of its owned view.

The build has **81 MAIN patch sites, 11 event collections**, and occupies
**64,912 / 65,536 bytes** (624 free), 500 fewer resident bytes than 0.9.2.
It uses the same `-O3 -mcpu=54415 -mtune=cfv4` profile. The AUDIO companion is
byte-identical to 0.9.2. Native-host tests cover event phase isolation,
consumption, visibility/removal, stationary waveform behavior, all FM ratios,
locks/pages and sampler regressions. Linked SYX checks cover native frames,
callback order, restored encoder bytes and the one event table per mechanism.
Physical UI/audio/CPU validation remains necessary on the unit.

## Version 0.11.0: source-built recorder module

[Audio Recorder](../recorder-st/README.md) joins the SMZ bank using seven
existing events, including double-SOUND detection on `ev_key`. No firmware
sites or event collections are added: the build retains **81 MAIN sites and
11 collections**. UI API 5 appends a versioned host-service pointer at offset
40 for existing storage/CRC/pool/preview/context-dispatch services. Earlier API
prefixes remain unchanged. The generic machine drawer validates parameter-view
identity before reading extended fields, allowing an owned plain native view
to emit contextual DRAW safely beneath higher modals.

High-zone descriptors carry a native completion capture with a no-op callback.
This enables the already-installed `ev_fs_written` path, which the stock close
routine skips when no capture exists. The recorder uses existing write/written
events to serialize saves with high-zone uploads; no second filesystem hook
or validator is introduced.

The matching core + System info occupies **65,040 / 65,536 bytes** (496 free).
All target code retains the speed-oriented `-O3` profile; AUDIO is byte-identical
to 0.10.0. The candidate in `out/core-st-0.11.0-recorder-complete/` includes the
matching SYX, FM/SP/recorder SMZ, validation/lint reports and regression log.
Native-host and linked-image checks pass, including 60-second capture,
normalization, transactional save, assignment and previous machine regressions.
Physical UI/audio/CPU verification remains pending.

## Version 0.12.0: digital POLY and resident space

[Digital POLY](../poly-st/README.md) is an optional source-built SMZ module.
The core adds a shared native transaction collection for bounded sequencer,
pattern, publication/type, Sound/machine guards and native modulation/LFO
operations. Sites/replays belong to the core; the bank supplies no patch plan.
Existing page/action adapters gain TRIG support. Input stays on KEY/ENC, with
one consuming pre-KEY phase and one resolved encoder context. Contextual DRAW
moves to the end of each complete view, replacing base-view emission and the
recorder's owned draw bridge. The snapshot transport uses effective owners for
all machine types, including FM's extended controls.

UI API 6 / module API 3 / host API 2 preserve previous prefixes. The added host
fields borrow existing telemetry services and explicit native stock replays.
Ordinary initializers still receive API 1; old view-only initializers receive
API 2; pre-KEY/native subscribers receive API 3. All C event lists and assembly
operands publish in the same existing short IRQ exclusion.

[System info](../system-info-smz-st/README.md) moves to the SMZ to make room.
Its collectors/UI remain the same; the core retains generic clock accounting.
`--system-info` still builds AUDIO telemetry, with the matching UI module in
the combined bank. No bootloader/startup reservation is enlarged.

Install the matching rebuilt combined bank: the old recorder initializer
requires host API 1 exactly and declines host API 2. Machine-only banks and
ordinary legacy subscriptions retain their earlier contracts.

The final core + AUDIO telemetry build uses **63,360 / 65,536 resident bytes**,
leaving **2,176 bytes**, compared with 496 free before moving System info.
The candidate is `out/core-st-0.12.0-poly-complete/`, with its combined bank and
validation/lint reports. Eighteen native-host fixtures, source/linked ABI audits,
FM reference checks, units and instruction checks pass. Firmware AUDIO and the
FM/SP AUDIO bank remain byte-identical to 0.11.0. Target flags retain `-O3`.
Physical boot, UI, audio, persistence and CPU validation remain pending.

## Version 0.12.1: shared selector and SourcePageView handling

The complete-view DRAW phase exposed a missing ownership condition: native
MachineParameterPageView renders its selector instead of controls while model
predicate `400c3294` is true. FM's full-page artwork could cover that selector;
SP's first page retained native artwork and did not show the same symptom.
One shared SYN-view gate now governs drawing, synchronization and idle work.
Highlighting a machine neither paints custom controls nor resizes its pages.
The FM renderer accepts the page supplied by the core, without its own view
qualification/page selection.

SourcePageView overrides the base virtual control resolver and queries source
layouts directly at `40044cc6` / `40044ce6`. Those existing getter paths now
use `core_machine_view_layout`, the same service as base navigation and page
actions, for every SYN page including page zero. The new common editor adapter
at `4003b8c6` emits ENC after the actual virtual resolver, before the native
zero-ID exit. Base navigation no longer emits ENC. This covers Source, TRIG
and ordinary parameter views uniformly, including FM's paired RATIO B handler.
There is one contextual encoder dispatch and no separate machine input registry.
Selector ownership and native page guards run once before module encoders,
including the POLY slave guard before FM's consuming ratio handler. The bank's
POLY encoder no longer repeats that guard. UI API 7 advertises this protected
post-virtual context; earlier fields, module API 3 and host API 2 are unchanged.

The fix has **95 MAIN sites / 12 collections**. No event ABI or storage format
changes. Host tests cover selector highlight/confirmation for all five machines,
all page/control IDs and the real SourcePageView route. FM tests compare 256
rendered frames and 23,826 ratio transitions against original Synfact C, including
pressed encoder and trig/page lock seeding. Physical verification remains pending.

Final candidate: `out/core-st-0.12.1-ui-ratio-fixes/`, with matching SYX/SMZ,
**63,264 / 65,536 resident bytes** (2,272 free), `-O3`, and byte-identical
firmware AUDIO / FM-SP AUDIO bank relative to 0.12.0. The combined suite passes
19 native-host fixtures, linked/source audits, units and lint. Install both
matching files: the new POLY bank requires the guarded UI API 7 context.

## Version 0.12.2: confirmed machine defaults and POLY kernel layout

The existing `slot_to_engine` adapter now completes native `40024db2` machine
defaults only at its final lookup (`return 40024e04`, slot `1a`, A2=model,
D5.b=quiet). Stock initializes the first eight controls. Core copies defaults
for controls 9..28 from the registered descriptor into their persisted Sound
slots, then sends the native change notifications unless quiet. Thus FM's
envelopes, trigger/reset modes, B2 ratio and four neutral offsets initialize
when the machine is confirmed. Ordinary parameter lookups, selector preview,
and loading saved Sounds/projects do not run this initialization. No new
site, subscription, machine-specific callback or default table is introduced.

The matching bank fixes a separate POLY error reported on the physical unit
(`IMG_5182.jpeg`, V04, PC read as `44535366`). GAS relaxed three local JMPs
inside the imported native modulation/LFO kernels to four-byte PC-relative
jumps. Their replacement blocks became 2/4 bytes shorter, while copied native
branch displacements retained their original offsets. Emit explicit absolute
six-byte JMP encodings as in Synfact's builder. The final linked kernels now
match every preserved stock byte at the original offsets, apart from the
declared guards and external PC-relative bridges. Negative layout fixtures
and the prior delivered bank demonstrate that the new check rejects this
specific defect. This is offline evidence; physical acceptance remains pending.

FM's 28 range/default contracts are compared with independent Synfact source
declarations and pinned reference descriptors, without importing its builders.
Its FEEDBACK maximum is corrected to Synfact's `7f00` (the port used `7800`).
All four page drawings, including the default envelopes on page 3, match
Synfact's C reference pixel for pixel in 256 native-host frames.

Candidate: `out/core-st-0.12.2-fm-defaults-poly-fix/`; install its matching
SYX and SMZ. Resident RAM is **63,520 / 65,536 bytes**, with 2,016 free.
The 95 sites / 12 collections, API 7/3/2, `-O3`, firmware AUDIO and FM/SP
AUDIO bank are unchanged. Existing saved parameter values are preserved;
reselect FM TONE to initialize a fresh default Sound.

## Version 0.12.3: stock fast paths with all features retained

Physical baseline with core 0.12.2 and only System info in the SMZ: MAIN
81% at boot, 91% PLAY and 91% after STOP on the user's stock pattern. These
readings justify avoiding unused extension work; they do not identify every
source of the high fixed load. No physical saving is claimed for this candidate
before repeating the same comparison.

All 95 sites, 12 collections, UI/module/host APIs 7/3/2, machine types and
modules remain supported. No native processing is stopped or simplified.

- The twelve native adapters replay their original instructions immediately
  when the shared native list has no subscribers. The flag derives from the
  actual list, including ordinary resident subscribers. It fails open before
  application initialization, and publishes atomically with SMZ event lists.
- Twenty-three machine metadata entries skip C when no machine bank is active.
  Native machine/parameter IDs also bypass C while a full bank is installed.
  UI IDs keep their own `2e0` boundary, engine IDs `1f9`, machine types 64,
  and label getters use their actual second argument. The SourcePageView
  getter retains its contextual resolver whenever machines are active.
- The generic machine drawer exits before reading view fields without a bank.
- MAIN's extended-parameter sender performs zero FPGA mailbox writes when
  no track needs extra controls. It publishes tag zero instead. Active rows
  retain generation/checksum/double-bank semantics; a resumed extended machine
  publishes immediately. The committed mask excludes untouched inactive rows.

Candidate: `out/core-st-0.12.3-stock-fast-paths-final/`. RAM: **64,352 / 65,536
bytes** (1,184 free), flags `-O3 -mcpu=54415 -mtune=cfv4`. Firmware AUDIO and
FM/SP machine AUDIO remain byte-identical. The user's existing System-info-only
SMZ is compatible and should remain unchanged for the first CPU comparison.
The full FM/SP/Recorder/POLY/System-info bank is also built and checked.

Linked audits pin each fast branch and native replay. Native-host tests cover
empty/ordinary/resident/SMZ subscriptions, failed binding, atomic publication,
zero stock-only mailbox writes and extension restart, plus the full machine,
UI, recorder, POLY, storage, unit and instruction regressions. These are offline
checks; real CPU percentages, latency and complete device execution are pending.

## Version 0.12.4: extended-control transport v3

Field reports with eight FM TONE tracks: on some boots every FM track was silent
while MAIN stayed near 100%; another boot of the same project played normally.
One mechanism is established from the code. AUDIO offered its 8x24 extended
rows from installation onward, zeros included. FM TONE copies them into its
ratio offsets, and its oscillator computes ratio = table + (offset - 0x4000)
<< 12, saturated at zero: a never-received row silences the carrier. The
receiver accepted only the bank of the frame's exact generation, read at the
end of the AUDIO block, while MAIN rewrote a bank every block. Whether a given
boot's MAIN/AUDIO phase made every packet miss is a hypothesis consistent with
the reports, not a measurement. Synfact keeps its previous values on rejection.

- AUDIO withholds a row until one was accepted for the machine type now on
  that voice, so FM keeps its neutral defaults instead of zeros.
- MAIN still reads every effective value every block, but compares it with a
  shadow (in the existing bind allocation) and publishes a new generation only
  on change, writing only the rows that bank holds stale. Stable values: no FPGA
  writes, no checksum, and no AUDIO bank reads.
- Byte `1a9` names the newest complete generation, which is never overwritten
  before a newer one exists: AUDIO can retry it on any later block. A lagging
  frame uses the other bank's next generation; rows are rechecked against the
  header (seqlock); checksum/torn reads keep the previous row and retry.
- The AUDIO companion stays within the device's declared window
  `40040860..40041050`: ABI entries and glue moved after the ABI directories
  (198 bytes to `40041026`); the main reserve holds 1,428 of 1,440 bytes.

All hooks, sites, collections, APIs 7/3/2, machines and modules are retained;
the SMZ bank is byte-identical. Candidate: `out/core-st-0.12.4-transport-v3/`.
RAM: **64,600 / 65,536 bytes** (936 free), `-O3 -mcpu=54415 -mtune=cfv4`.
`tests/test_st_parameters.c` covers never-received rows, eight-FM boot, stable
blocks without FPGA access, LFO slots and clamps, locks and their removal,
stale-row-only rewrites, checksum retry, torn headers, lagging frames, a
600-generation wrap, mask/type/owner changes, the ready gate, receiver restart
and stock/FM transitions; nine source mutations are each detected. These are
native-host checks: silence, CPU and timing on the Syntakt are not yet measured.

## Version 0.12.5: cheaper extended-control comparison

0.12.4 removed FPGA writes for stable values, but MAIN still evaluated each
extended control through two cross-file slot lookups and a generic value
function: about 20 calls per FM track per block. The sender now reads the
core's fixed layout directly (indices 8..23 are the contiguous Sound slots
36..45, then 0, 11, 18 and 46), compares whole packed pairs against the
shadow without branches, and only evaluates mirror/LFO arithmetic for slots
11 and 18. Semantics, block resolution, owner/POLY projection, clamps and
the transport protocol are unchanged; the OWNERS query keeps its position (the
optional MAIN profiler's D/C boundary). Code: `core_machine_snapshot` 998
bytes (was 1,080) with no per-value calls.

The registry test pins the layout against `core_machine_slot`.
`tests/test_st_parameters.c` adds 25/26-control machines, both LFOs on one
slot, owner LFO outputs and 20,000 randomized blocks against an independent
oracle; eight further source mutations are each detected. Firmware AUDIO and
the SMZ bank are byte-identical to 0.12.4. Candidate:
`out/core-st-0.12.5-fast-emitter/`, RAM **64,520 / 65,536** (1,016 free).
It also contains `synfact-machines-profile-r4.smz4`, the unchanged diagnostic
bank (FM, profile begin, Recorder, POLY, System info, profile end). No CPU
saving is claimed before physical measurement.

## Version 0.12.6: sender telemetry (host API 3)

Field profile, 8 FM + 4 analog stock, PLAY: `I90 S00 P21 D07 C51 E11` with FM
against `I87 ... C48` with stock machines; FM's remaining cost lies only in
C, where the sender runs. To split it, `core_machine_snapshot` now accumulates
`{blocks, MAIN timer ticks inside, publications, rows written}` (two timer reads
per block; the work itself is a separate unchanged function). Host API 3 appends
`machine_telemetry` to the existing service table; modules requiring API 2 are
unaffected. The optional profiler shows a third header page `Xd.d Unnn Rd.d`:
% of wall time in the sender, % of blocks publishing, rows per publication.
Firmware AUDIO and the normal SMZ are unchanged. Candidate:
`out/core-st-0.12.6-sender-telemetry/`, RAM **64,616 / 65,536** (920 free).

## Version 0.12.7: word-granular publication

0.12.6 telemetry on the user's 8 FM + 4 analog pattern: compare-only 1.2% of
wall time at boot; 3.1% in PLAY/STOP with 60-71% of blocks publishing about two
rows. FPGA writes dominate. A publication now writes only the words each bank
holds stale (per track and bank: type bit and one bit per packed pair) plus
the row sum, and the header costs two writes (invalidate, commit generation);
magic/reserved words are written once per bank and the mask only when it
differs. AUDIO, protocol and semantics are unchanged. Telemetry adds the last
publication's changed tracks and word bits, shown by the profiler as
`T`/`W`. Candidate: `out/core-st-0.12.7-word-writes/`, RAM **64,900 / 65,536**
(636 free; the sender grew to 1,276 bytes).

## Version 0.12.8: None stays None for FM TONE

The native parameter table maps engine `0x13` ("None", short `---`, flags
`0xe00`) to Sound slot 0: LFO and Sound Setup destination lists, their
defaults (DEST 0) and the native LFO engine (`working[dest] += ...`) treat slot
0 as None. FM TONE stores RATIO OFFSET C in slot 0, so its lists showed `C` in
place of None, and any LFO or modulation left on None with non-zero depth
modulated the carrier's ratio offset (0.12.7 telemetry: `T05 W200`).

`slot_to_engine` now never reports slot 0 as a machine control: lists, labels
and defaults show the native None again. The sender reads index 24 from the
Sound mirror instead of the modulated working projection. Saved Sounds are
unchanged; RATIO OFFSET C remains editable but is no longer a modulation target
(it shared the None slot). Firmware AUDIO and both SMZ banks are unchanged.
Candidate: `out/core-st-0.12.8-fm-none/`, RAM **64,948 / 65,536** (588 free).

## Version 0.12.9: compare cadence

Field, no profiler, PLAY, same pattern: 8 FM 93% on FILTER and SYN views, the
same tracks as stock machines 90%; with the profiler the sender compare cost
2.8% against 0.6% for stock (no publication once None was fixed). FM's cost is
inside the audio IRQ, mostly the per-block compare, likely cache refills.

Every 4th block compares all tracks and refreshes the active and LFO-routed
masks; other blocks compare only tracks whose trigger bit is set in this block
(D3 at the existing frame-tail hook, passed as the sender's argument) so locks
reach AUDIO with their note, and tracks with a native LFO routed to an extended
slot (`11`, `18`, `36..46`) with depth other than `4000`. Edits, slides and
aftertouch on extended controls arrive within four blocks (2.7 ms). The OWNERS
query still runs every block (POLY owners, the profiler's D/C boundary).

The word-granular writes of 0.12.7 are reverted to whole stale rows (no
measured gain, ~200 bytes of resident code). Telemetry word 5 is now the OR of
the changed rows' XOR differences. Firmware AUDIO and both banks are unchanged.
Candidate: `out/core-st-0.12.9-cadence/`, RAM **64,972 / 65,536** (564 free).

## Version 0.12.10: optional native-operation profile (host API 4)

Field bisect: p-lock editing reaches 100% with POLY's native subscription
alone. To find the operation, `core_native` (the single C entry of all native
operations) accumulates MAIN timer ticks and calls per operation into a buffer
that a diagnostic module registers through host API 4 (`native_profile`).
Without a registered buffer the cost is one null test per call; `core_native`
is no longer inlined into `core_native_view`, so the dispatcher code shrank.
The optional profiler shows a fourth header page with the two most expensive
operations. Firmware AUDIO and the normal bank are unchanged. Candidate:
`out/core-st-0.12.10-native-profile/`, RAM **64,944 / 65,536** (592 free).

## Version 0.12.11: per-operation native interest (host API 5)

The 0.12.10 profile showed POLY's subscription costing TYPE 1.5% (audio IRQ)
and SYNC 1.2% (UI) with no POLY group active. The single subscribed flag is
replaced by `core_native_interest`, one byte per `ST_NATIVE_*` operation: each
of the 12 assembly adapters tests its own byte (same 6-byte `tst.b`, registers
untouched) and replays stock at once when it is zero. The bytes are the union
of the ev_native subscribers' masks; a subscriber that never calls host API 5
(`native_interest(callback,mask)`) keeps every operation, so the default is
the previous behaviour. Bind publishes the bytes (5 words) inside the same
atomic patch as the native list; startup before application initialization
stays fail open (all ones). POLY declares TRIG, IMMEDIATE (its private
SYNC/CFG1 commands), HEADER LOAD/RESET and the page operations always, plus the
voice-routing operations only while a group exists (recomputed in
`poly_commit`). The optional profiler declares PUBLISH and OWNERS.
`core_native_original` is no longer inlined (only declined dispatches reach it
from C). Firmware AUDIO and the bank AUDIO are unchanged. Candidate:
`out/core-st-0.12.11-native-interest/`, RAM **65,328 / 65,536** (208 free).

## Version 0.12.12: no repeated SYNC, TYPE by track

Device, normal bank, p-lock editing: 96/98% (STOP/PLAY) without POLY, 100%
with lag with a 3-voice group; profiler top operations SYNC 1.0–1.9 (UI) and,
with a group, TYPE 1.8 (audio IRQ). Two exact reductions:

- The per-knob navigation path sent PAGE_NAV and then PAGE_SYNC for the same
  view. PAGE_NAV subscribers now synchronize the view themselves (POLY's
  `poly_ui_control` already did), so `core_machine_nav` no longer sends the
  second PAGE_SYNC. Other PAGE_SYNC sites are unchanged.
- The two padding bytes of `core_native_interest` filter TYPE: byte 18 holds
  the digital tracks whose type cursor is dispatched, byte 19 "every cursor".
  Host API 5 mask bits 24..31 narrow a TYPE subscriber (0 or 0xff: every
  cursor, the default). Both TYPE adapters test them in assembly (d0 only,
  which stock and active paths overwrite). POLY passes its slave tracks, the
  only cursors it overrides. Published atomically within the same 5 words.

Firmware AUDIO and the bank AUDIO are unchanged. Candidate:
`out/core-st-0.12.12-sync-type/`, RAM **65,296 / 65,536** (240 free).

## Version 0.12.13: page notifications by view class (host API 6)

Device 0.12.12, normal bank, p-lock editing: 95–97% without POLY, 98–100%
without lag with a 3-voice group; profiler: SYNC 1.1–1.6 in both cases, TYPE
gone. The edits happen on SYN pages, yet every ParameterView sync went through
the dispatcher while POLY's PAGE_SYNC/PAGE_NAV handlers act only on the TRIG
ParameterView (vtable `401c97fc`).

Host API 6 `native_page_view(callback,vtable)` declares that class.
`core_native_page_view` is nonzero only when every PAGE_SYNC/PAGE_NAV
subscriber declared the same class; then `core_native_view` replays stock for
those two operations on any other view (SYNC 0, NAV none) without dispatch.
BEGIN/END/GUARD are not filtered (POLY tracks the active view there). The
word is published in the same atomic bind patch as the native list and
interest bytes; 0 (every view) is the default. Subscriber slots are 4. The
cold mask code no longer unrolls, so resident code shrank. Firmware AUDIO and
the bank AUDIO are unchanged. Candidate: `out/core-st-0.12.13-page-view/`,
RAM **65,172 / 65,536** (364 free).

## Version 0.12.17: preview outside the FX track

Sample-pool preview (and the recorder's audition/monitor, through the same
`<syntakt/mix.h>`) mixes into `8000dfd0` instead of `8000dad0`: on the device
the old bus went through the FX track. `8000dfd0` is the bus the OS adds
digitally to USB MAIN (`8 x (ADC sum + 8000dfd0)`); its `4008eeca` stage is x8
instead of x10, so the sample is scaled x5/4 to keep the previous digital
level. Only `sample-manager.c` changes in the resident core. Physical routing
and level still need confirmation on the Syntakt.

## Version 0.12.18: lock preview without the heap

Device, core 0.12.17 with the FM drawing fix, PLAY, one FM track: CPU M 93%
with a trig held, 97% turning a control, 100% turning fast. With a trig
held, the native lock preview `40039160` (every page's value read: SYN
pages, FM drawing, the stock renderers, LFO3's fallback) built a 28-byte
`std::function` with operator new, walked the selection through `400c3670`
and `40037a34`, and freed it, on every read.

One site, `400391a8` (8 bytes, after its trig test), enters
`core_preview_selected` (`preview.s`, 184 bytes), which does the same walk
on the native frame: track length, selection test, both selection words per
position, the lock query and, for pool IDs other than 0x52, the pool query,
stopping at the first lock or pool sound; then the native tail `4003921e`.
No allocation, nothing cached between reads. LFO3's entry patch at
`40039160` is untouched: its native fallback reaches the site.

`tests/test_st_preview_emu.py` runs the real `40039160` of 0.12.17 and
0.12.18 in Unicorn with the same stubs (4,000 reads: lengths 0..64,
selection words changing during the walk, locks, pool, ID 0x52, held/active
tested by byte): same value, lock flag, callee-saved registers, SP and call
sequence; 0.12.17 allocates and frees once per held-trig read, 0.12.18 never.
It catches an off-by-one length, a wrong 0x52 exception and swapped
selection words. Firmware AUDIO and the bank AUDIO are unchanged.
Candidate: `out/core-st-0.12.18-preview-noheap/`, RAM **65,400 / 65,536**
(136 free). CPU on the Syntakt not measured yet.

## Core 0.12.20: shared note acceptance (experimental, not an ARP release)

Host API 8 adds `ST_NATIVE_NOTE_ACCEPT=20` to the existing native collection.
`note-events.s` owns `400a423e`, after track mute/stale checks and Sound selection,
before note effects. Borrowed arguments are `{event, sound, effective_time,
frame_end}`. Observers return zero. A handler supplying nonzero result bypasses
the effects and reaches the original `400a471e` cleanup; it must not free or
retain the event itself. This covers audio-track events, including releases;
it is not a notification that every event is a note-on. Effects/synthesis and
complete lock/Sound lifetime validation remain separate.

Bytes 18/19 retain the TYPE filter ABI. The old `ST_NATIVE_OPERATION_COUNT=18`
and 36-word API 4 profiler buffers remain unchanged; new operations are outside
that legacy profile. `ST_NATIVE_OPERATION_LIMIT` sizes dispatch storage.
The pointer layout of host API 7 is unchanged. Existing narrowed POLY/LFO3
subscriptions do not opt into NOTE_ACCEPT. No additional collection is added.

The experiment `out/core-st-0.12.20-note-accept-r4/` uses 65,512/65,536 resident
bytes (24 free), with source/audit flags unchanged and protected firmware
sections verified. `tests/test_st_note_accept_emu.py` compares 384 acceptance
slices with stock, tests observer/consume paths and executes native release once
in four consumed cases. The condition predicate and C dispatcher are doubles;
host tests separately run the real native dispatcher. Off costs four extra
instructions per accepted event in this slice, not a measured CPU percentage.
This is groundwork for ARP; no generator is connected or ready for device use.

## Core 0.12.21: shared native dispatch export

Host API 9 appends `native_dispatch` (offset 84, total 88 bytes) to the existing
host service record. It calls the existing `core_native` collection, unlike
`native_original`, which bypasses mods. Context restrictions remain the same;
OWNERS is MAIN IRQ-only. The previous host record prefix and API 4 profiler
buffer size remain unchanged. This lets ARP inspect POLY ownership through the
shared bus instead of linking to POLY internals or inventing another registry.

The experimental ARP module now uses NOTE_ACCEPT and native pools/queue; its
UI uses the existing KEY/ENC/complete-view DRAW events. This does not make the
full Digitone port device-ready. See [arp-st](../arp-st/README.md) for unsupported
paths and explicit emulator doubles. Core resident use remains 65,512/65,536
bytes with unchanged build flags/reserve. Current offline results are under
`out/core-st-0.12.21-arp-view-checked/`; protected sections are stock and both core
and bank AUDIO retain the prior delivery's bytes.


Host API 10 (core 0.12.22) adds logical `ST_NATIVE_SOUND_META` notifications on
`native_dispatch`; see `syntakt/sound-meta.h`. The shared object/codec adapter
publishes them, without another firmware site or pointer field. The host is
still 88 bytes and API 4 profiling remains 36 words. Sound codec observers see
final extended machine words. ARP uses this lifecycle for Sound persistence.


Main integration retains independent/shared POLY and its API 8 `native_query`
name. API 10's `native_dispatch` is an alias in the same 4-byte slot at offset
84; both call `core_native`, and the host remains 88 bytes. The Sound codec
notification helper is shared out of line; its extended controls plus final
word are copied as one contiguous 34-byte range before notification. This
keeps the combined core within its unchanged 64 KiB reserve and build flags.
