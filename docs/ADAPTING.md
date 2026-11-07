# How to adapt your mod to the Syntakt

This guide is for people and for coding agents. Every step has a command
and the output that means it worked; the last sections map every refusal to
its fix and give a definition of done. The format itself is
[FORMAT.md](FORMAT.md). Addresses are Syntakt OS 1.41's.

Most Syntakt mods are machines or modules in the core's SMZ4 bank: the
"Syntakt core" sections below describe its services, and
[SYNTAKT-BUILD.md](SYNTAKT-BUILD.md) builds and checks a bank. Sections 3 and 4
describe elekloader's general mod paths (linkable mods and whole builds),
which the core itself uses; their examples come from elekloader, whose other
devices this repository no longer keeps.

## 0. Pick a path

| you have | do | result |
|---|---|---|
| code that should run next to other mods | **Path B**: a linkable mod (format 2), built with the SDK | combines with any other mod that does not conflict |
| a byte or two to change (a constant, a limit) | **Path B** with `bytes` sites and no sources | the same; no compiler needed |
| a custom firmware you already build as one piece (one `.syx`) | **Path A**: a whole-build mod (format 1), made with `mkmod` | quick, but it cannot be combined with anything |

Path B is the one to aim for. Path A is a stopgap for an existing
monolithic build; section 4 says how to go from A to B.

## 1. What you need

- Python 3.9 or newer, and this repository (no dependencies).
- The Syntakt OS 1.41 stock file, exactly as Elektron publishes it: the
  engine knows it by hash (`elekloader/devices/syntakt.py`).
- For code: the m68k cross toolchain, with the location, environment and
  flags in [SYNTAKT-BUILD.md](SYNTAKT-BUILD.md).
- The Syntakt's **core** (`mods/core-st`): it owns the shared hook sites,
  copies every linkable mod's code into RAM at boot, turns the shared sites
  into events, and loads the SMZ4 bank. Build it as SYNTAKT-BUILD.md says.

## 2. The rules

The loader checks all of these before it builds anything; each rule says
what you see when it is broken (section 5 has the fixes).

1. **Only the main OS changes.** A mod has no way to touch the bootloader
   or any other section, so the stock OS file always recovers the device.
2. **Ship no firmware bytes.** Call firmware routines by address; do not
   copy stock code into your sources. When your bytes do repeat stock bytes
   (the instruction a hook displaced, say), runs of 8 bytes or more are
   stored as references and filled in from the user's own file.
3. **Patch whole instructions.** A code site covers whole stock
   instructions, and so do its new bytes: typically a 6-byte `jsr`/`jmp`
   over one or more instructions, and the routine it calls does their work
   itself. *"ends mid-instruction"*, *"sweeps land on the start"*.
4. **Do not patch the shared sites: subscribe.** The core owns the sites in
   `mods/core-st/mod.json`. Subscribe to their events instead of hooking
   them. *"... overlap"*.
5. **One owner per byte.** No two mods may patch overlapping bytes, or bytes
   another mod's table entries claim. *"... overlap"*.
6. **Let the linker place you.** Your code and data go in sections; never
   hard-code the address of your own code or data. Budgets are shared by
   every mod and come from the Syntakt's profile
   (`elekloader/devices/syntakt.py`); the core build prints what is left
   (`MAIN RAM: ... FREE`, SYNTAKT-BUILD.md). *"the mods need RAM to ..."*.
7. **Only relocatable references.** The linker relocates 32-bit absolute and
   32/16-bit PC-relative references. Use the device's compiler flags (the
   SDK does) and 32-bit addressing for your own symbols. Firmware addresses
   are plain numbers and need no relocation. *"relocation type N"*.
8. **One namespace.** Every global symbol is visible to every mod: prefix
   yours with your mod's id (`hello_draw`), and make internals `static`.
   *"both export ..."*.
9. **Claim what you use.** List named resources in `resources.names`: SysEx
   device ids (`sysex:0x7d`), SETTINGS rows (`settings:MY ROW`), +Drive
   paths (`drive:/cfw/mine.bin`), key combinations (`ui:SRC page:hold YES`).
   Memory you use outside the linker's areas goes in `resources.regions`.
   *"both claim ..."*.
10. **Inside another mod's copied block, be absolute.** FAST AUDIO copies the
    render block 0x400716c0-0x4007629a to SRAM at run time. A site of yours
    in that block runs from the copy too, so it may not be PC-relative and
    may not touch the bytes the copy's fix-ups rewrite. *"PC-relative,
    inside ... copied block"*.
11. **Say what you need.** `requires` names the mods yours depends on (always
    `core` for format 2); `conflicts` names the ones it cannot live with.
    *"X requires Y"*.
12. **One file, one release.** A mod targets one stock release by hash.
    Porting to another OS version means finding every address again.

### The hook bus (core's events)

Handlers are C functions (arguments on the stack; d0-d1/a0-a1 free,
everything else kept; the result in d0). `order` sorts handlers of one
event (lower first; the shipped mods use 10-90).

| event | when | C prototype | notes |
|---|---|---|---|
| `ev_tick` | 30 times a second, UI task | `void f(void *ctrl)` | set `*((unsigned char *)ctrl + 0x20) = 1` to redraw |
| `ev_draw` | after each frame is drawn | `void f(void *bmp, void *ctrl)` | draw over the frame; y = 0 is the bottom row |
| `ev_key` | each key event | `int f(void *brain, void *ev)` | return nonzero to take it: nothing later sees it. `ev+12` key id, `ev+16` flags (1 pressed, 8 repeat, 2 FUNC held) |
| `ev_enc` | each encoder turn | `int f(void *brain, void *ev)` | `ev+12` encoder 1-8 (A-H), `ev+16` delta |
| `ev_settings` | the SETTINGS menu is built | `void f(void *menu)` | add a row with `core_additem(menu, row)`; `row` is four callbacks (label, select, draw, change) in the firmware's MenuItem conventions, which this guide does not document yet. |
| `ev_render_in` | audio render entry, 1500 times a second | `void f(void)` | interrupt level: keep it short |
| `ev_render_out` | audio render exit | `void f(void)` | the same |

These are the events the Syntakt's core shares with elekloader's cores
(`mods/core/core.s`). It adds its own (`ev_native`, `ev_main_mix`, `ev_track_send`, `ev_delay_in`, `ev_reverb_in`, `ev_fs_route`,
`ev_fs_write`, `ev_fs_written`) and the services of the sections below; its
sites, which no mod may patch, are in `mods/core-st/mod.json` and
[mods/core-st/README.md](../mods/core-st/README.md). A firmware routine your
mod calls is named by its OS 1.41 address.

### Syntakt core 1.1: the mod list in System info

The SETTINGS row SYSTEM INFO opens a list: METERS ON/OFF, then the core and every
mod, "ID VERSION". The core's mods come from the table `core_mod_info`, which the
linker fills from each mod's `id` and `version` (mod.json). The bank's mods come from
its mod-info directory (feature bit 3, [SMZ4](../mods/core-st/SMZ4.md)), which
`elekloader.sdk.syntakt.smz` writes from each mod's `mod.json`. A bank without that
directory lists only the core. Host API 12 gives modules `core_info` and `bank_info`.
In the list: ENC A or the arrows move, YES on METERS toggles the meters, NO closes it.
The list is in [system-info-st](../mods/system-info-st/system_info.c); its C test is
`tests/test_st_system_info.py`. Its display on the unit is not verified here.

### Syntakt core 0.10: machine UI and optional modules

Core 0.12's [digital POLY](../mods/poly-st/README.md) uses existing UI events
and a core-owned native transaction collection for contexts not present in
raw UI/tick events: sequencer events, pattern headers, effective publication,
type and native edit guards. Reuse these operations rather than patching from
an SMZ. [`native.h`](../elekloader/sdk/include/syntakt/native.h) defines the
borrowed synchronous arguments/results and stock replay service.

Module API 3 adds consuming pre-native KEY (`302h`) alongside the old post-native
notification. DRAW now follows each complete view; ENC uses the already-owned
control resolver, including TRIG's wrapper and virtual controls. UI API 6 / host
API 2 exposes existing telemetry services. System info's UI is now in
[`system-info-smz-st`](../mods/system-info-smz-st/README.md); `--system-info`
retains the AUDIO companion. Native replay must use its original task/IRQ
context. All event lists/operands publish atomically before callbacks activate.

Core 0.11.0 adds UI API 5's host pointer after the unchanged earlier prefix.
Its versioned table borrows existing core HZFS/IO, CRC, pool, preview and event
services. Check UI API 5 before accessing it; storage remains task-only.
[Audio Recorder](../mods/recorder-st/README.md) demonstrates a source-built
module with seven existing subscriptions, a double-SOUND shortcut on `ev_key`,
periodic jobs on `ev_tick` and contextual painting below native modals. Its
manifest contains no firmware sites. Coordinate storage with existing FS
events; do not duplicate the core's catalogue validator or pool transport.

Core 0.10.0 routes machine input through the existing `ev_enc` and `ev_key`
subscriptions, contextual machine painting through `ev_draw`, and waveform
background work through `ev_tick`. Do not use the retired `bind_input` API.
Module API 2 / UI API 4 supports `ST_EV_VIEW` on DRAW/KEY/ENC subscriptions;
ordinary subscriptions keep their existing behavior. Native context adapters
retain view/page ownership, event normalization and modal layering. The extra
encoder patch and idle-vptr clones are removed. This follows the DT1
`machine-pages` model of existing periodic events and shared native adapters.
See [the audit](../mods/core-st/HOOK-AUDIT.md) and
[phase contracts](../mods/core-st/SMZ4.md). Rebuild old FM banks for 0.10;
older ordinary modules and sampler descriptors remain supported.

Core 0.9.2 preserves a registered machine's explicit engine ID in native edit
resolver `40038dae`, preventing a second SYN-1 source-layout translation from
redirecting controls on later SYN pages. The classifier override is restricted
to that native call site; normal SYN metadata and stock fallback are retained.
Existing SMZ banks remain compatible.

Core 0.9.1 resolves LFO and Sound Setup modulation destination captions through
the registered short-label getter for every file-owned machine parameter. The
four inline stock lookups are core-owned; machines provide only descriptor
labels. Native lookups are replayed exactly. No SMZ format or bank change is
required for the correction.

Core 0.9.0 appends UI API 3 native edit/clear, input-registration and compound
page-value codec services. All control semantics remain machine-owned; native
storage, locks, undo and transaction dispatch stay in the core. A machine can
list inline module symbols under `smz.modules` alongside its descriptors.
[FM TONE](../mods/fm-tone-st/README.md) demonstrates these services with four
pages and a paired ratio encoder; its source build needs no Digitone SYX.

Core 0.8.1's UI service API 2 appends `format_context`, distinguishing popup
value text from direct control text without changing disk descriptors. Read
the appended field only when `api->version >= 2`; see
[SMZ4.md](../mods/core-st/SMZ4.md). The same machine-owned formatter can keep
SAMP fixed on the control and supply a sample name to its modal.

Machine sources may provide `st_machine_ui` descriptors and request
`machine-ui-v1`. The core manages 1..4 SYN pages and dispatches draw, idle,
format, dial and encoder-policy callbacks to code in the bank. Layouts select
defined controls and may repeat them for waveform pages. The current native
storage mapping supports 28 controls, with 32 IDs/page positions reserved.
Only defined extended parameters are sent to AUDIO; eight-control machines
use the native parameter frame without an extension.

Use `--module DIR` with the SMZ builder for an optional MAIN module. It can be
combined with `--preset samplers`, or used alone. See
[examples/smz-test-st](../examples/smz-test-st) and the
[format and callback contracts](../mods/core-st/SMZ4.md). Core services remain
functional without SMZ. Uploading an SMZ does not change running code; reboot
to activate it. Core 1.7 banks require an exact core contract and use
`/mods-stage`; restart to try the bank. The core confirms it after AUDIO/sample
startup and five seconds of normal UI ticks. A crash or restart before that
point selects the previous bank or stock machines. See
[the safe startup design](SAFE-BOOT-SMZ4-07-16.md).
LFO3 and MODULATION SKIP patch stock code directly. Their source preimages
generate the core's read-only boot gate; a new direct-code patcher must be
added to that gate and its initializer must run after ordinary modules.

The Syntakt profile selects the exact MCF54415 with `-O3`, ColdFire V4 tuning
and a four-iteration full-peel limit for bounded AUDIO code. No FPU or fast-math
instructions are enabled. Compilation, linked instruction checks and protected
SYX verification do not substitute for physical UI/audio validation.

### Syntakt core 0.7: sample engines and project pool

Compile the four SP engines with `python -m elekloader.sdk.syntakt.smz --preset
samplers --out out/samplers.smz4`. This path compiles C/assembler directly and
requires no SYX. The manifest supports several descriptors sharing an AUDIO
source set and can request the generic `sample-pool-v1` core service.
The core provides a 128-position pool in the existing project header, Sample
Manager entries in SOUND/SETTINGS, stock-preserving boot transfer with the
native sample bar, and a bounded runtime PCM receiver. See
[SAMPLE-POOL](../mods/core-st/SAMPLE-POOL.md) and [samplers](../mods/samplers-st/README.md).
The explicit firmware builder compiles the core AUDIO service from source,
counts its image in the 64 KiB reserve and reports used/free bytes every build.

### Syntakt core 0.5: long samples and stock-slot bounds

The core mounts/initializes HZFS v1 in the high eMMC zone, independent of SMZ4.
It exports catalogue enumeration, streamed put/get and deletion, with two
CRC-checked catalogues and checked native Data windows. Only blank catalogue
windows may be initialized. The SDK crops stock uploads to five seconds;
the core rejects overlong stock-slot writes before creating a native writer,
protecting `/mods` immediately after slot 64. See [HZFS](../mods/core-st/HZFS.md).
All Syntakt Python tools are in `elekloader/sdk/syntakt/`; mods remain in `mods/`.

### Syntakt core 0.4: optional machine banks

The core loads validated SMZ4 banks from `/mods`, before native
project/sample loading, with a `loading mods` progress dialog. Missing or invalid
banks skip activation and the dialog. Ordinary SDK mods remain in the 64 KiB
reserve and work independently of a bank. The contract reserves 32 controls in
four SYN pages; 28 have a native Sound/lock storage mapping in this revision.
See [SMZ4](../mods/core-st/SMZ4.md) and the [core build](../mods/core-st/README.md).
Host build tools live in `elekloader/sdk`, not in the core source directory.

### Syntakt core 0.3: Data RPC and `/mods` (OS 1.41)

Adds `ev_fs_route`, `ev_fs_write` and `ev_fs_written` for native Data RPC
extensions, and uses them for one opaque `/mods` file, maximum 16 MiB,
immediately after the 64 native sample slots in eMMC. Stock samples retain
their original paths, table and limits. All writes are bounded before stream
creation. A separate sector records the committed length; interrupted files
are not published. This does not define or execute SMZ4 content.

See [the source and ABI contract](../mods/core-st/STORAGE.md). Build the core
with the normal SDK; all implementation is SDK source and a normal manifest.
The offline `python -m elekloader.sdk.syntakt.mods_object` helper wraps raw bytes in native Data transport.
Hardware transfer/readback remains to be confirmed; no emulator is used.

### Syntakt core 0.2 (OS 1.41)

`mods/core-st` supplies `ev_tick`, `ev_draw`, `ev_key`, `ev_enc`,
`ev_settings`, `ev_render_in` and `ev_render_out`. The render events cover
the MAIN audio interrupt; CPU2 runs a different program. `core_additem`
uses the shared SETTINGS implementation, with a relocatable inline capture
manager. `core_clock_snapshot(out[3])` provides coherent cumulative
timer/idle/MAIN-render-in-idle counters. Native UI services are exported as
`fw_*`; see [core-st](../mods/core-st).

[System info](../mods/system-info-st) demonstrates a Syntakt SETTINGS row,
native icon, temporary toggle, header overlay and the clock snapshot.
It subscribes to the core and owns no MAIN patch sites.
Its explicit build also installs a read-only AUDIO telemetry companion.
The generic patch command still accepts MAIN mods only; a `.elemod` does
not silently authorize replacement of another firmware section.

## 3. A Syntakt mod: a machine or a module in the bank

A Syntakt mod is a machine or a module in the core's SMZ4 bank, built from
C and assembler with the SDK (`elekloader.sdk.syntakt.smz`). Start from
`examples/machine-test-st` or `examples/smz-test-st`, or from a mod in
`mods/*-st`, and use the core's services from the sections above.
[SYNTAKT-BUILD.md](SYNTAKT-BUILD.md) has the bank command, the order the
modules must keep, the parser check and the regressions; `lint` reports the
sites a mod hooks itself under `core_candidates` (section 2's "core first").

### Machine types: taken and free

A machine's type is the first field of its `core_machine_def`. A bank takes
types **64 to 79** (16 machines at most), each once: the bank builder and the
core's parser refuse anything else. The type is also what a Sound stores, and
the core derives the machine's parameter ids from it (engine
`0x1f9 + (type-64)*32`, UI `0x2e0 + (type-64)*32`). So a released machine
keeps its type for good, and a new machine takes a free one, never a retired
one: Sounds saved with the old machine would open as the new one.

| type | machine | mod |
|---|---|---|
| 64 | LOADER TEST | `examples/machine-test-st` (test only, never in a release) |
| 65 | SY FM DTONE | `mods/fm-tone-st` |
| 66 | SP ONE SHOT | `mods/samplers-st` |
| 67 | SP WERP | `mods/samplers-st` |
| 68 | SP REPITCH | `mods/samplers-st` |
| 69 | SP SLICE | `mods/samplers-st` |

**Free: 70 to 79.** Take the lowest free type, add your machine to this
table in the same change, and keep it if the machine is ever renamed.
`tests/test_units.py` checks that this table names every type the mods
and examples use.

## 4. elekloader's general paths

Linkable mods linked into MAIN (format 2, how the core itself is built) and
whole builds (format 1, `mkmod`) are elekloader's general paths. The file
format is in [FORMAT.md](FORMAT.md); the step-by-step guide, with its
examples for elekloader's other devices, is elekloader's own
[ADAPTING.md](https://github.com/irpina/elekloader/blob/main/docs/ADAPTING.md).

## 5. When the loader refuses: message Ã¢â€ â€™ fix

| message contains | means | fix |
|---|---|---|
| `made for a firmware elekloader does not know` | the target or stock file is not a supported release | use the exact stock file; check its hash against `devices/` |
| `is made for <device> 1.53; the stock file is <device> 1.54` / `mod.json is for ... 1.53; the stock file is ... 1.54` | the mod is for another OS version | build it for that OS: a port (3.10) |
| `the stock bytes are ..., not ...` (SDK) / `not the ones it expects` | the address is wrong, or the OS version differs | re-read the stock bytes at that address |
| `ends mid-instruction`, `sweeps land on the start`, `does not decode` | the site does not cover whole instructions | move or widen the site to instruction boundaries (disassemble the stock main OS) |
| `... overlap (0x...-0x...)` | another mod patches or claims those bytes | subscribe to an event instead, agree with that mod's author, or propose the site as a core event ([CORE-CONTRIBUTING.md](CORE-CONTRIBUTING.md)) |
| `X requires Y` / `core is not enabled` | a dependency is missing | add Y (with lint: `--with Y.elemod`) |
| `X needs core 3.0 or newer, and this build has core 2.1` | the mod's `resources.core` asks for a newer core | build with that core (the app and the web page take it when you tick the mod); an older loader says `imports fw_..., which no given mod exports` instead |
| `imports cm_ui_v31, which no given mod exports` | the page names `cm_ui_v31` (a knob that draws itself, `CM_DRAW`), and no mod given exports it: machine-pages before 1.1 does not | use machine-pages 1.1 or newer |
| `CM_KNOB: draw= needs flags=CM_DRAW` or `CM_KNOB: with CM_DRAW, +36 is draw` (assembly, `core3.inc`) | a knob's `draw=` without `flags=CM_DRAW`, or its `gfx=` with it | give the knob `flags=CM_DRAW` with `draw=`, or `draw=` in place of `gfx=`; the page's `CM_UI` needs `abi=cm_ui_v31` |
| `imports N, which no given mod exports` | a missing dependency, or a typo | add the mod that exports N, fix the name, or list N under `weak` |
| `adds to table T, which no given mod declares` | the table's owner is missing, or the event name is wrong | add core (or the owner); check the event name |
| `both export S` | two mods define the same global | prefix your globals with your mod id; make internals `static` |
| `relocation type N` | a reference the linker cannot relocate (e.g. 16-bit absolute) | use 32-bit addressing for your own symbols; build with the SDK's flags |
| `section X is not one elekloader places` | code or data in a section the linker does not know | C: default sections; asm: `.section .run, "ax"`, `.fast` or `.bss` |
| `the mods need RAM to ...` / `.fast code needs SRAM to ...` | over budget | shrink tables and buffers; keep `.fast` for the hottest code only |
| `has .fast code, which needs ...` | `.fast` is copied in by FAST AUDIO | require `fast-audio`, or use `.run` |
| `PC-relative, inside ... copied block` | a site in FAST AUDIO's block branches relatively | use an absolute `jmp`/`jsr` |
| `both claim N` | two mods use the same named resource | pick another SysEx id, row name or path |
| `is a whole build: it cannot be combined` | a format-1 mod was mixed with format-2 mods | use one or the other; see section 4 |
| `has DSP code for payload A, which needs a mod that frees DSP memory there` | nothing frees P memory for DSP code | add the mod that claims the name the message gives |
| `the DSP code for payload A needs P words to ...` | the DSP code and tables do not fit in the freed words | shrink your DSP code |
| `holds an address the linker cannot place` (SDK) | a label used as a short or packed operand | use a form that takes the address as a whole word (`>`); branches are relative |
| `is a DSP reference to a ColdFire address` (or the reverse) | a `dsp24` relocation names a ColdFire symbol, or a ColdFire one a DSP label | name the right symbol; DSP labels are word addresses on the DSP |
| `the label N is part of a longer word here` (SDK) | dsp_asm would put N's address inside that word | rename the label |

## 6. Definition of done

- [ ] `python -m elekloader.sdk.build <dir> --stock <stock.syx>` prints `BUILT`.
- [ ] `python -m elekloader.lint <mod> --stock <stock.syx> --with <core> [--with <others>]`
      exits 0, with every mod users are likely to combine yours with.
- [ ] `python -m elekloader.patch ... --out test.syx --version XXXX` prints `WROTE`.
- [ ] `mod.json` has `description`, `category`, `license`, `requires`, and every named
      resource you use under `resources.names`.
- [ ] Your sources hold no copied firmware code, and no firmware file is
      committed anywhere.
- [ ] The build ran in an emulator or on a device you can recover.

## Syntakt: independent voice modulation (core 0.12.19)

Host API 8 appends `native_query` to the API 7 table. Modules can query the
existing `ST_NATIVE_OWNERS` operation from MAIN IRQ with null arguments for
Sound owners or `{1}` for LFO-state owners. Do not infer oscillator ownership
from Sound ownership: a POLY slave can use its master's configuration and its
own LFO state. Preserve the returned map only for the synchronous transaction.
Older hosts keep their existing behavior; test the host version before reading
the new pointer. See [the native contract](../elekloader/sdk/include/syntakt/native.h)
and [POLY's implementation/tests](../mods/poly-st/README.md). No new native hook
or serialized SMZ feature bit is needed. Pattern-owned PLAY uses the existing
PQ/PR record described in [FORMAT](FORMAT.md). POLY 0.3 only exposes POLY/M.LFO;
its retired mono codes decode as M.LFO. Native LEGATO and NOTE PRIORITY remain
stock Sound settings and are never written by this module.


POLY 0.8 combines the digital PLAY record and analog topology in the same seven
PatternSettings bytes. Preserve legacy PQ/analog-flag and PR readers; use PS
for a ranked digital topology with analog voices, as [FORMAT](FORMAT.md)
specifies. Analog masters keep their native shared modulation and saved Sounds.
Only analog slaves enter the copied native kernels' skip bitmap. Digital
independent groups must keep advancing when analog groups coexist.


Host API 11 appends `const struct st_sample_pool_api *pool` at byte 88,
retaining every earlier offset. The pool service version is 1. It supplies
validation/sync/cached names, preview start/refill/release and one optional
menu/progress UI provider. Native hooks, storage, PCM streaming/mixing and
project references remain in core. A bank-owned Sample Manager uses existing
KEY/TICK subscriptions for its input/foreground work. Binding is task-only;
callbacks live in the loaded bank and the core clears them on bind reset or
failure. Read this field only when host version >= 11; see
`syntakt/sample-pool.h` and `mods/sample-manager-st`.

## Syntakt: Sound metadata and note generation (core 0.12.22)

Host API 10 retains API 8's native query pointer at offset 84, also exposed as
`native_dispatch`. Both names address the existing native bus. Check the host
version before subscribing to NOTE_ACCEPT (20) or SOUND_META (21), and follow
`syntakt/native.h` and `syntakt/sound-meta.h` for context and borrowed lifetimes.
NOTE_ACCEPT consumption reaches the native event cleanup once; observers must
not free the accepted event. LFO3's established object/codec adapters publish
Sound lifecycle events, avoiding competing hooks. ARP uses these services for
a monophonic generator on all twelve audio tracks and Sound persistence.
Build it with `--module mods/lfo3-st --module mods/arp-st --module mods/poly-st`
in that order, alongside the ordinary recorder and System info modules.
See [ARP](../mods/arp-st/README.md) for tests, native boundaries and persistence.
Project APC1 carriers may be absent when all thirteen associated ARP Sound rows
are defaults; readers must restore those defaults without requiring a carrier.

## Syntakt: mixer events, the FX codec and several providers (core 1.2)

Three events of the MAIN audio interrupt (`mods/core-st/fx-send.s`) let a
module add an effect on the mixer's FX sends:

| event | when | C prototype |
|---|---|---|
| `ev_track_send` | once per block, when every track's level x pan gains are computed | `void f(const struct st_track_sends *t)` |
| `ev_delay_in` | the delay's input bus is complete, before the delay renders it | `void f(void)` |
| `ev_reverb_in` | the reverb's input bus is complete, before the reverb renders it | `void f(void)` |

The table holds each track's left/right gains (native inverted Q31) and its
142-byte mixer record (s16: +106 the dormant Digitone chorus send, +118 DEL,
+120 REV, +122 PAN, +124 LEV), then the native send-mute bits (`muted`, bit
t set: track t's sends are muted). The core fills a row per track and calls
once, at track 11. At `ev_track_send` the EMAC
accumulators hold the mixer's live products: use no EMAC instruction and keep
MACSR. The buses and the tracks' dry PCM are named in `<syntakt/mix.h>`
(`ST_TRACK_PCM`, `ST_FX_SEND_BUS`, `ST_FX_TRACK_BUS`). With no subscriber each
site costs a LEA, a TST and a branch. A bank that subscribes to these events
(`ST_EV_TRACK_SEND`, `ST_EV_DELAY_IN`, `ST_EV_REVERB_IN`) is refused by an
older core's bank parser.

Host API 13 keeps up to four parameter providers: queries go to each in
registration order until one answers, notifications (25..29) reach all of
them. The core now owns the Kit's FX record codec (`400a6d96` encode,
`400a6d2e` decode, `fx-codec.s`) and tells the providers after every call:
28 `{record, fx, ok}`, 29 `{fx, record, ok}`. LFO3 1.1 uses them instead of
hooking those two entries itself, so it needs core 1.2 with a rebuilt bank
(on an older core it hooks them as before). `ST_NATIVE_PAGE_LAYOUT` also asks
for page 25, the dormant chorus page. The [chorus](../mods/chorus-st/README.md)
uses all of it.

Query 30 asks for a dial no machine claims (`struct st_machine_dial`; nonzero
means drawn), and query 17 also gives such an ID's direct value text.
Queries 16, 17, 18 and 20 also come for parameter 72, the Sound's dormant
Digitone chorus send, which has no stock labels or formatter.

A module's parameter IDs come from their own range, after the machines'
blocks: `ST_MODULE_ID` (engine `0x3f9`) and `ST_MODULE_UI` (UI `0x4e0`), 128
of each (`<syntakt/machines.h>`). The core sends them to the providers,
lets the native relative edit and range check reach them (its bound is now
`0x478`) and passes their Sound stores to query 24. Taken: LFO3 +0..+9, the
chorus +10..+16; take the next free ones and list yours there. Every
machine type 64-79 stays free for machines. The core's lookups run only
while a bank's machines are bound, so a provider's IDs need a bank with
machines.

MAIN SRAM never misses in cache, which an audio-IRQ module pays for in DDR.
`<syntakt/sram.h>` names the stock-zero windows and their owners (LFO3's
kernel and bank, the chorus's state, per-block C code and kernels); each owner checks only its
own window at init and falls back to DDR, so bank order does not matter.

## Syntakt: page randomize for new pages (core 1.4)

The page key + YES randomizes the page shown (list item 22). Core 1.4 makes
the native randomize reach the bank's and the modules' controls
([RANDOMIZE-PAGES.md](RANDOMIZE-PAGES.md)):

- A bank machine's SYN page needs nothing: SYN + YES draws the controls of
  the page shown, each uniformly over its range, except a control whose
  alias is `0x1f2` (the pool sample slot).
- A module's controls on a native page are drawn when its provider answers
  query 31 for the ID; a stock ID it lays out (POLY's chord lanes) works the
  same way. A module ID on the Kit's model (the chorus) is stored through
  query 24, which now also comes for the Kit model's setter.
- A module's own view (the ARP) handles page key + YES itself.

Host API 14 adds no field. Before host API 13 there is one provider slot, so
a module that needs a provider only for query 31 registers it on 14. LFO3 1.2
leaves the Kit setter `40013516` to core 1.4 and stores through query 24, as
it does for the Sound; on an older core it hooks it as before.
