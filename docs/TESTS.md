[Synfact docs](README.md) › Tests

# Tests

Sample Manager 1.3 / core 2.3: native-host manager/pool tests cover the expanded
63.62 MiB budget, protected slots, mono/stereo sizes, unique page extents,
fragmentation, exact fits and capacity rejection before any AUDIO command.
The compiled UI additionally executes against OS 1.42's real list, font,
QString, bitmap and selection routines (allocation/synchronization, core
services and manager attachment are fixture boundaries):

```bash
python tests/test_st_manager_draw_emu.py --stock "$SYNTAKT_STOCK" \
    --bank out/<build>/synfact-machines.smz4 --out out/<build>/manager-preview
```

It checks the two-line header, six-row scrolling, folder labels, stereo/mono
sizes, ellipsis and the right column's pixel positions, and writes two optional
LCD PNGs plus an AUDIO FULL error capture. It does not access a device or model LCD timing.

Core 2.1.2: the registry regression (`tests/test_core_st_machines.c`) includes
module-only banks (LFO3 metadata with and without a second provider), no-bank
fallback, empty-directory/lane safety and allocation failure/recovery. It uses
production registry/page code and explicit metadata/allocator doubles. The
common regressions run it, or run it alone without firmware from Git Bash:

```bash
mkdir -p out/module-only-fix
"$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe" -O2 -Wl,--large-address-aware \
    -I elekloader/sdk/include tests/test_core_st_machines.c \
    -o out/module-only-fix/registry.exe
out/module-only-fix/registry.exe
```

`tests/test_st_module_only_emu.py` runs actual LFO3/ARP initialization and
native metadata hooks against linked OS 1.42 MAIN, with a zero-machine bank
and an optional previous-core ERR negative control. Its inputs, scope and
results are in [the module-only investigation](LFO3-MODULE-ONLY-INVESTIGATION.md).

Core 2.1.0: `test_st_project_operations.py BUILD --emulate` checks the nine
project worker routes and 324 linked lifecycle/ABI cases, with native workers
and STOP doubled. `test_st_project_idle_cfbench.py --build BUILD --cfbench
CFBENCH --out OUT` executes the real OS 1.42 reduced output/USB/EMAC path,
including no-recorder operation and a missing-clear negative control. See
[Project operations and MAIN CPU](PROJECT-OPERATIONS-CPU.md).

Current desktop builds target OS 1.42. `tests/test_st_os142.py --stock STOCK
--build BUILD --previous-build REFERENCE_141` checks 218 stock preimages and four Playhead CRC contexts, the
actual SYX, the complete bank contract and unchanged core/bank AUDIO. See
[the port notes](OS142-PORT.md) for the direct 1.42 emulator checks and the
historical 1.41/donor fixtures that are not current-release validation.

Core 2.0.2's `test_st_native_catalog_emu.py --core BUILD --bank BANK --out JSON`
retains the complete five-native-subscriber catalog, including real Playhead
initialization, and checks 39 LFO3 inactive/active/neutral cases. Chorus UI/DSP,
allocator, mutex and renderer bodies are explicit boundaries. Add
`--overflow-reference CORE_201` to require the previous four-record core to
reproduce the lost inactive LFO route. The common regressions run the fixed
full-catalog case. The route suite also checks all sixteen registration
records and safe overflow without changing its original dispatch/ABI oracles.
Neither test measures physical CPU percentages or accesses the device.

ARP 1.10.7: `test_st_arp_native_stack_emu.py` checks edits beneath permanent
mute in both opening orders and on both pages, plus track changes before a tick,
cancelled step holds, native navigation and other-window ownership. The same
native-stack cases also run through retained/reentrant view fixtures.
Core 2.0.3: `test_st_chorus_ui_emu.py` targets OS 1.42 and checks selected-model
popup labels for every Chorus control, stock/filter labels and native fallbacks,
in addition to metadata, values, codecs, pages and dials.
The 2.0.3 validation with Unicorn 2.1.4 also found CPU exceptions in the broad
`test_st_arp_view_emu.py` (the long-gate `arp_frame` case) and
`test_st_patchers_emu.py` (`probe_modulation`, native PC `40097b3e`). Both
failures reproduce on the unmodified remote-main build; these two suites are
not counted as passes. The focused ARP/mute/reentrant, Chorus UI, safe-boot,
native ABI/mixer and full-catalog cases run separately.

Each suite takes its input files from environment variables or arguments. A
test whose input files are not given is skipped, not passed: say which ran.
Firmware files never go in this repository.

The full LFO3/MSKIP installation regression executes native ColdFire
MAC-with-load instructions. Set `LIBUNICORN_PATH` to a Unicorn build with
that support; generic 2.1.4 raises an exception at the native MAC instruction.
The stock-stream test itself uses ordinary integer instructions.

## Syntakt

`tests/test_st_stock_stream_emu.py --core BUILD --out BUILD/stock-stream.json`
executes the actual boot replacement, native sender loop, target page/wave
code, task release hook and native CPU2 descriptor parser. It compares all
64 five-second samples byte-for-byte, split-page cases, an optional machine
batch and a disabled pool-service image. It verifies code preservation, exact
CPU2 pointer agreement and scratch release, with poisoned freed memory.
Core 2.0.1 also requires zero boot calls to the waveform builder and no cache
allocation during transfer, then executes lazy disk reads and cache reuse.
Core 2.1.1 also requires the bank file holding the machine batch's AUDIO section
to be freed exactly once, by the same hook, after its pages reached the
receiver. `tests/test_core_st_trial_boot.c` (the regressions' `trial-boot`)
checks the loader's side: MAIN's part in a block of its own, the AUDIO section
intact in the file until the release, and the whole file kept when that block
cannot be allocated.
Core 2.2: `tests/test_st_pool_service.c` (the regressions' `pool-service`) maps the
pool's new addresses and checks that the AUDIO companion installs the service at
`4404ff50`, publishes its descriptors and writes the channel table into the ABI
directory, and that a corrupt or misplaced service publishes nothing.
Disk, heap, semaphore, notification and mailbox delivery are boundaries, not a full
RTOS boot or hardware validation. The host project-pool fixture additionally
covers empty/oversized headers, missing disk, read/allocation failures, exact
stock peaks after release, lazy HZFS CRC refusal/retry, source changes during
partial scans, and HZFS mono/stereo boot/runtime behavior.

`tests/test_st_stock_blob_emu.py --stock OS1.41.syx --stock OS1.42.syx
--out out/sample-memory/reproduction.json` reproduces native sample writes
into the resident core's RAM window. It runs the actual stock constructor and
memory copies with sample-read/heap fixtures and shared DDR alias backing.
Five cases per OS cover 16/17/64 five-second samples and the exact first
overlapping frame. This is an investigation test that expects corruption,
not evidence of a fix, full boot or hardware behavior. See
[the findings and scope](STOCK-SAMPLE-CORE-OVERLAP.md).

`tests/test_st_view_cleanup_emu.py --before old-core/main.bin
--after new-core/main.bin --bank bank.smz4 --out report.json` executes native
close and full deferred cleanup with synchronous visibility/compose/tick.
The same bank with the old core must lose a nested close request and leave
a closed ARP node blocking re-entry; a new cleanup request recovers it. The
new core must preserve the request and return screen/blue keyboard without
a modifier key. It checks digital/analog, POLY, retained owners and removal
of mute while staying on ARP, including a complete-call instruction equality.
For that equality, the negative control must differ only in the cleanup site:
restore the 18 stock bytes at `401010f0` in a copy of the current MAIN and use
it as `--before`, beside the current `verification.json`. An older complete
core can reproduce the behavior, but other code changes affect its total cost.

`tests/test_st_arp_reentrant_view_emu.py --core main.bin --bank candidate.smz4
--before old-1.10.3.smz4 --out report.json` executes native push/visibility,
synchronous compose and the actual core tick, including real base View
construction/destruction. The timed auto-popup model is idle and LCD work
after the tick is a boundary. Normal entry must select the page before the
nested tick, then pass both pages, wrap/reopen, both fallback subpages and
the existing native mute/retained-owner suites through this same flow.
Delivered 1.10.3 must reproduce its blank normal opening. No device access.

`tests/test_st_arp_retained_view_emu.py --core main.bin --bank candidate.smz4
--before old-1.10.2.smz4 --out report.json` reproduces mute/TRIG wrap with a
native shared owner retaining a closed view beyond stack removal. The new
session must paint blue through the actual LED-frame gate/walk without
releasing the old owner or pressing a modifier; delayed destruction must
preserve new settings and held keys. Native manager key traversal, shared
copy/refcounts and removal execute linked code. Model/panel IO, unrelated
LED handlers and base construction are boundaries. No device access.

TRIG/ARP focus regression: `tests/test_st_arp_mute_emu.py` checks native
page forwarding under mute, focus/step-release ownership and before/after
MAIN instruction counts. Give `--core main.bin --bank candidate.smz4
--before baseline.smz4 --out report.json`; the baseline for ARP 1.10.1 is an
unmodified bank from the same current-main revision.

`tests/test_st_arp_native_stack_emu.py --core main.bin --bank candidate.smz4
--out report.json` runs native MainScreen publication, shared ownership,
manager insertion and draw traversal with the contextual core DRAW bus.
It checks both ARP pages, blue LEDs and step taps, fallback opening and
mute-first creation beneath native mute, on digital/analog tracks with/without
POLY. It validates both list directions and native mute removal, restoring
the ARP keyboard without another allocation. `--mute-before` requires the
1.10.1 delivery to reproduce its mute-first blank screen.
Optional repeated `--before` inputs are the earlier blank 1.9.1/1.9.2
deliveries, which must reproduce the inactive ARP state. Both suites need
the linked bank `.work/main.elf`, matching core verification report and the
ColdFire-capable Unicorn library. No device access occurs.

The LFO3 activity fixture models the skipped Chorus initializer's real
PAGE_LAYOUT-only native interest. Otherwise a normal bank's uninitialized
Chorus callback defaults to every operation and fabricates an LFO subscriber;
this reproduces on unmodified main too. The original inactive-route, live
activation and missed-update negative checks remain required.

The Syntakt's suites (`tests/test_st_*`, `tests/test_core_st_*`,
`tests/test_syntakt_secondary.py`) and the regressions that run them all on a
build folder are in [SYNTAKT-BUILD.md](SYNTAKT-BUILD.md): the commands, the
toolchain they need, and the emulator checks.

```bash
python tests/test_units.py        # needs nothing; always run it
python -B tests/test_st_playhead.py --cc "$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe" # Windows x86; no firmware
python tests/test_st_fm_recipe.py --digitone Digitone_and_Digitone_Keys_OS1.44.syx   # FM TONE's recipe
python -B tests/test_st_playhead_emu.py --bank <bank with playhead-st> --stock "$SYNTAKT_STOCK"   # Unicorn
```

`test_st_playhead_emu.py` runs the stock sequencer code at playhead-st's five
sites in Unicorn with the module's real `playhead_seq_set` and stubs: the two
trig pushes (`4008cf00`, `4008cf14`), the mid-pattern trig push (`4008c204`),
a whole tick's UI copy loop (`4008d166..d1d2`) and the accessor of the step
the trig LEDs show (`4008b734`). Forwards equal to stock,
registers, pushed arguments and memory; backwards the step mirrored to
`length - 1 - step` (a step at or past the length as it is), in both length
modes and every track; the sites in with the first backwards track, out with the last;
a changed context refused with nothing written. Then the other modes through
`playhead_step`: pendulum without an end twice, random over every step, walk
one step either way, Anchor alternating step 1 with the remaining steps,
Stutter repeating every fourth visited step twice, Fold retracing its
outside-to-centre traversal, and Walk+ with 60/20/20 forward/stay/backward
probabilities; one step per count for the UI copy, the trig and the
LEDs. Then TRK + UP / DOWN through the real key handler: the selector opens
on the current mode without editing; navigation and repeat do not apply;
YES commits to the captured track/kit and calls native `KitActive::kitChanged`
once, NO cancels, confirming the same mode makes no edit, context changes
cancel and keys/encoders cannot leak into the underlying view. Native Bitmap
construction and masked blit execute on the real OS code: all nine icons,
text regions, centered mask, selection inversion and cached draws are checked.
The real singleton getter, redraw request and compose gate also execute:
each open/navigation/confirm/cancel press must set the embedded controller's
dirty flag without modifying its root owner. Version 1.2 reproduces the
missing request as a negative control. Incremental cursor updates are compared
against full frames over 36 transitions and limited to the two affected rows;
instruction counts include the native blit and exclude underlying native
screen composition, RTOS scheduling and real kit-mirror IO.
Then the saving (`persist.c` through the Sound metadata bus): the
modes in a known kit slot's free tail on KIT_ENCODE and back on KIT_DECODE,
unknown slots never written, kit copies through a buffer, a changed buffer,
NEW, PROJECT_NEW, a damaged record, each pattern's own kit, the sites
following the modes. Then the header icon (`playhead_draw`, DRAW | VIEW, on a fake MainScreen,
the stock tempo split): each mode's pixels inverted, two columns left of
the metronome and centred on rows 56..62, for three tempos, nothing else, and nothing for FORWARD, another view's
layer or a MainScreen not on top.

`test_st_playhead.py` compiles the production step selector for the Windows
x86 host with explicit native RAM and patch-write doubles. It exercises all
nine modes over lengths 1..64, thirteen tracks and both scale modes, repeated
reads, native wraps, mode starts and length changes, including the two-step
cache ambiguity. It needs no stock file. The five original preimages are
provided and their context CRC is deliberately bypassed in this host test;
only the stock Unicorn differential verifies those firmware boundaries.

`test_st_playhead_cfbench.py` executes the real bank's register-ABI step
helpers and UI-copy hook on analog-emu's ColdFire interpreter. It compares
outputs against a bank built from the initial implementation, counts start,
advance, cached and peek instructions, and verifies C callee saves and the
entire EMAC context. The UI fixture supplies the native registers and an RTS
at its continuation; the stock differential independently checks that hook
inside the real native loop. Six tick copies per step exercise the paired
cache. Counts exclude the measured probe overhead and are not hardware cycles.

```bash
python -B tests/test_st_playhead_cfbench.py --reference-bank <reference bank> \
    --bank <optimized bank> --cfbench "$CFBENCH" --out out/<new benchmark folder>
```

This uses the same `SGDK_BIN`, `ELEKLOADER_CROSS` and `GCC_EXEC_PREFIX` as bank
builds, and needs no stock firmware. A baseline with the new modes is required
for a nine-mode comparison.

### Recorder MAIN LR stereo capture

`tests/test_st_recorder_capture_cfbench.py --reference-bank BANK_1_2 --bank BANK_1_3
--cfbench CFBENCH --out OUTPUT_DIR` executes real callbacks and assembly kernels
in analog-emu. It compares PCM, meter/take peaks, state/clock and correction
banks; checks complete physical EMAC preservation, PCM-kernel ABI, PRE/POST,
zero/nonzero gain transitions, 47-frame tails, ARMED/threshold transitions,
2,048 randomized mono/stereo blocks and 512 integer-oracle kernel vectors. Negative controls
must detect corrupted PCM/correction. Callback instruction counts exclude setup
and are not physical CPU percentages. No instrument is accessed.

The common regressions include the post-take owner/UI tests: reopening a
retained take from PLAY issues STOP, retains PCM/trim and reenters reduced audio;
PLAY remains consumed and FUNC+NO restores full processing.
Recorder 1.4 adds 108 early manual STOP cases across all nine lengths,
mono/stereo, transport states and STOP/FUNC+STOP. They check reduced-mode
eligibility before the next UI tick, unchanged PCM/length, one native STOP
only, repeat/release handling and normalization/PLAY gating.
See [RECORDER-CAPTURE-CPU.md](RECORDER-CAPTURE-CPU.md).

## Digi Mono

The original-Monomachine calibration bench is documented in
[`tools/monomachine-reference`](../tools/monomachine-reference/README.md).
It is separate from the port's exactness tests: it needs the owner's Monomachine
OS 1.32B and a built DSP reference. Its numerical analysis tests run without
firmware using `python tests/test_monomachine_compare.py` in the research venv.
`complex_compare.py` adds 88 cases in eight complex engines: unison/subs/PWM,
ENS intervals/chorus, VO vowels/consonants, SID waves/RING/SYNC and all three
FM+ families. `python tests/test_monomachine_complex.py` checks its band-power
metrics, stereo/silence handling, gain-response calculation, transient-preserving
A/B normalization and explicit fixtures without any firmware input. Reanalysis
may reuse hash-verified captures; the linked DSP/native generator sources and
every parameter/event must match.
The ENS extension adds 149 octave/control/chorus/retrigger fixtures and 28
separate shape cuts, with per-note gain anchors. `ens_model.py` checks hashed
original captures and infers duty/blend from same-note harmonic ratios, then
validates 22 independent response cuts, excluding repeated grid settings.
Numerical tests cover width/blend
inference, invalid inputs, the five-octave grid and stereo captures. Original
DIST, the entire chain and the fixture set must also match for capture reuse.

The standalone Digi Mono port test runs all twelve types through the real
adapter/DSP, checking PCM/state against the engine, UI labels and chord names,
TYPE transport, backend re-entry, independent tracks, pitch and AMP sleep.
Only firmware service boundaries are doubles; it does not boot a Syntakt:

```bash
python tests/test_st_digimono.py --cc "$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe"
```

Its upstream source hashes are also verified. No firmware input is needed.
The runner also compiles `test_st_digimono_pages.c` with the real core registry,
page resolver and Digi Mono UI. It checks all twelve engine types, repeated TYPE
switches, two views, every dial/edit ID and page-action layout, TUNE and LEVEL.
This catches the old core's frozen SAW layout, which a descriptor-only UI test
missed: the hidden E position must reappear for every seven-parameter engine.
It also alternates destination queries for two different Sound models without
opening SYN, checks every TYPE's visibility, unchanged runtime flags/storage,
module-provider fallback and rejection of older host APIs.
Version 0.5 adds `test_st_digimono_picker.c`: the real one-page UI and shared
subtype helper, twelve names, hidden TYPE, YES/NO/encoder routing, recording
guards, late defaults, track/model changes, preserved knobs and wrapping ticks.
`test_st_digimono_picker_emu.py BUILD --out OUTPUT` runs the linked ColdFire
picker and actual native Bitmap construction/fill, writing PBM previews of all
twelve name boxes and the selector. Native UI/model/get/edit boundaries are
explicit doubles; a complete native selector/undo/modal lifecycle is not
emulated. The original MD work is imported read-only with snapshot hashes. Core 2.3.2
also runs native SYN reload through its real machine-parameter enumeration for
all twelve subtypes, with boot-created kit/snapshot and final reload-consumer
boundaries doubled: all eight synthesis IDs remain and TYPE is absent.
See [SUBTYPE-PICKER.md](SUBTYPE-PICKER.md).
Core 2.3.1's transport fixture also tests `ST_MACHINE_CONFIG_PARAMETER` for
every extended index 8..23, both packed halves, independent/shared POLY owners,
legacy modulated working values versus raw stored edits, and restoration of
ordinary modulation after clearing the marker. TYPE flags are zero in native
metadata and it is absent from LFO/Sound destination lists; other synthesis
flags and common destinations remain unchanged.

The runner also runs `test_st_mono2k.py`. It verifies pinned MONO2K hashes and
deterministic extraction; the generated tables at 44.1 kHz match upstream and
the committed ones regenerate at 48 kHz. The five MONO2K generators compare
6,912 blocks of PCM/state against the pinned code using native 64-bit maths,
including fractional controls, low/high pitches and changing modulation.
ENS's oracle includes the measured 0.3.1 PW/WAVE laws; its width conversion uses
independent wide arithmetic. The adapter checks PW=0 remains audible at WAVE127.
Independent checks cover wide arithmetic, 440 Hz carrier tuning at 48 kHz
and all five SID waves, ENS L/R and delay/clock continuity across retriggers.
`test_st_digimono_stereo.c` checks both output channels, single ramp advancement,
TYPE/width changes, AMP tails and service fallback with stateful native doubles.
This is not original-Monomachine matching or timing.

`test_st_digimono_destinations_emu.py BUILD_FOLDER` executes the native list
builder and sorter, actual Sound-proxy getter, linked core flags wrapper and
linked bank's destination module. Only the boot objects, empty map header and
heap are fixtures. It checks TYPE changes with the three LFO masks, unique
targets, unchanged common destinations, Sound bytes and runtime flags. It needs
a built core/bank folder and EMAC-capable Unicorn, without external fixtures.

The linked ColdFire test additionally needs Syntakt OS 1.41, a bank with its
sibling `.work/audio.elf`, 64-bit native clang, and the EMAC-capable Unicorn
from [SETUP.md](SETUP.md) (`LIBUNICORN_PATH`). It runs real native update and
gain calls, compares the oscillator PCM to the host engine, checks callee-save
registers and stack balance, and reports instruction counts (not CPU cycles).
Its 2,300 blocks include a full second of stereo ENS with fractional controls,
delay wrap and retrigger, plus 128 blocks of mono fold-down with the service
unavailable. It also checks the real native gain/mix ramp states match between
the two channel calls. A complete bank exercises the shared stereo service;
a standalone bank exercises the missing weak symbol and centered fold-down:

```bash
python tests/test_st_digimono_emu.py --bank out/digimono/synfact-machines.smz4 \
  --stock "$SYNTAKT_STOCK" --host-cc "$LLVM_MINGW_BIN/x86_64-w64-mingw32-clang.exe"
```

This is an offline fixture, without a full boot, scheduler or FLTR/AMP/FX pass.

## The engine

The engine (`elekloader/`, `js/`) comes from elekloader and still knows every
device it supported. Its own suites need other devices' stock files, which this
repository does not use; they stay so the engine can be compared with
elekloader's:

```bash
ELEKLOADER_STOCK=Digitakt_OS1.53.syx ELEKLOADER_MODS=path/to/mods python tests/test_link.py
ELEKLOADER_RELEASES=folder/of/stock/files python tests/test_releases.py   # every known release, and the cores here
ELEKLOADER_STOCK=... ELEKLOADER_BUNDLE=bundle.elemod ELEKLOADER_CTOOL_SYX=its-build.syx python tests/test_patcher.py
ELEKLOADER_STOCK=... ELEKLOADER_STOCK_154=... ELEKLOADER_OT_SYX=... ELEKLOADER_MODS=... python tests/test_gui.py   # the window, hidden (Tk)
```

A change to the engine belongs in elekloader first: propose it there, then
bring it here.

The TypeScript engine has its own tests, which need no firmware, and a
parity run that compares it with the Python on your own files
([js/README.md](../js/README.md)):

```bash
cd js && node --test "test/*.test.ts"
```

The tools that prepare the browser's SMZ catalog have theirs:

```bash
python js/test/test_smz_merge.py
python js/test/test_smz_approve.py
```


### Stereo MAIN port on current main (samplers 1.4.2)

`tests/test_st_stereo_main_current.py --bank <new.smz4> --reference-bank
<current-main.smz4> --cfbench <cfbench> --out <folder>` executes the two banks'
linked callbacks and track-D accessor, including gain ramps, sparse masks,
late/torn publications and restarts. Five 384-block scenarios cover DDR,
SRAM then forced fallback, Chorus present and either SRAM window occupied.
Both banks need their sibling `.work/main.elf` files. It compares every bus
word and checks the callee-saved ABI. It needs SGDK_BIN and analog-emu.

`tests/test_st_stereo_natives_emu.py` also links the port's exact kernel and
resolver source spans for its existing fractional EMAC/model checks, then
runs them from real emulated SRAM, checks migration/fallback and occupied
windows. These are target-instruction checks; full hardware scheduling is
not modeled.

`tools/check_st_stereo_audio.py --bank <new.smz4> --reference-bank
<owner-working.smz4> --core <new.syx> --reference-core <owner-working.syx>`
requires exact AUDIO equality and, when supplied, exact whole-SYX equality.
The rejected pre-port bank is a negative control for this delivery guard.

The 1.4.3 linked differential can use the delivered 1.4.2 bank as its
reference. It additionally counts the actual four callbacks after priming
stable gains: eight stereo tracks and 0/1/2/3/4/8 tracks feeding both FX,
with DDR and with SRAM. These counts exclude the native mixer, FX and other
modules. They are stored in `stable_block_instruction_counts` and must not
be reported as physical CPU percentages.

For 1.4.4, `tests/test_st_stereo_natives_emu.py` additionally executes the
production assembly expansion kernel against an independent C integer
oracle over 4,096 blocks, all masks and shifts 0..16. It checks untouched
inactive tracks, output canaries and EMAC preservation, then repeats cases
with the relocated SRAM kernel and after fallback. The linked differential
can use 1.4.3 as its reference and retains the stable-send count matrix.

For 1.4.5, the linked differential additionally measures first and continued
gain changes and genuinely alternating stereo masks, with 0, 2 and 8 sends.
Use `--require-no-count-regressions` to reject any increase versus the
reference in either the stable or changing complete-block count matrix.
Results include `dynamic_block_instruction_counts`. This catches gains
that hide a setup/check overhead by benchmarking only an unchanged loop.

### LFO3 MAIN callback, engine and task measurements

`tools/bench_st_lfo3_main.py` executes linked before/after bank functions and
real native helpers in analog-emu/cfbench. See `docs/LFO3-MAIN-PERFORMANCE.md`
for the native LFO2 oracle, continuous/boundary checks, exact default-control
bit changes with signed tempo values, pinned TRI helper, all lock
masks, ABI/EMAC comparison, negative control and complete-stage count matrix.
`--require-no-count-regressions` rejects increases in any measured block or
stage. Outputs pin core, bank, fixture and emulator hashes. Counts include
a fixed argument wrapper and exclude preparation; they are not CPU cycles.

`tools/bench_st_lfo3_tasks.py` measures publication, whole-object hashes and
lock setters with the existing explicit native boundary doubles. Those costs
occur per task/event, not necessarily every audio block. It retains the
historical regression scripts' acceptance conditions unchanged.

### ARP MAIN callback and clock measurements

`tools/bench_st_arp_main.py` compares complete linked callbacks across forty
live/sequence scenarios (270 blocks per bank), plus 414 clock-state cases.
It checks runtime/settings bytes, native note outputs, retained templates,
pool ownership, changing/invalid root locks, tempo, ABI and non-increasing
callback instruction counts. Existing native-producer/analog fixtures supply
explicit synthesis boundaries. See `docs/ARP-MAIN-PERFORMANCE.md` for the
counts, reproduction and limits; no physical MAIN percentage is inferred.
For ARP 1.11, `--changing-keys` adds five changing-held-snapshot cases for 45
scenarios / 330 blocks. `tools/bench_st_arp_keyboard.py` checks the linked
assembly entry and C worker in 193 cases, including all bitmap words, random
and 128-note sets, insertion order, invalid list heads, cycles, native-memory
immutability and unchanged selector state on rejection. Every individual
keyboard call also has a non-increasing instruction-count gate and ABI check.

For ARP 1.12, `tools/bench_st_arp_selector.py` checks 42,604 calls with exact
selector state/ABI and per-call instruction gates, including mode/range/held
changes, all bitmap boundaries, falling ranges and invalid lengths. The
unchanged native-oracle assertions in `test_st_arp_selector_emu.py` now build
and execute the authored target assembly and record its source hash. The main
callback benchmark's `--transitions` adds 24 stop/reconfiguration cases.
`tools/bench_st_arp_tracks.py` checks twelve noncontiguous-track scenarios /
60 callbacks, native event order and final balanced pool ownership.

### POLY MAIN synchronization, publication and event measurements

`tools/bench_st_poly_main.py` compares 48 sync cases and 2,068 complete
publication maps with an independent byte oracle, alignment/ABI checks and
per-call instruction gates. `tools/bench_st_poly_events.py` compares eleven
complete configuration/keyboard/sequencer scenarios and shutdown, preserving
state, native notes, pools and refcounts. Accepted-root staging and parent UI
origin are disclosed inputs; sequencer batches enter the real core ENQUEUE
dispatcher. `tools/bench_st_poly_helpers.py` checks fixed-copy canaries at
all byte alignments (the Sound record copy only in banks before POLY 1.7),
exact stack-census reads, and 120 queue admission cases.
`tools/bench_st_poly_keyboard.py` preserves already-correct stock/core analog
master-Sound routing in both banks without changing the historical test's
requirement for a broken-bank negative control. Counts are instructions,
not hardware cycles or MAIN percentages. See
[POLY MAIN performance](POLY-MAIN-PERFORMANCE.md) for commands and limits.

`test_st_poly_copy_events_emu.py` checks that POLY 1.7's UI flush publishes
the master's Sound to each slave and never writes the slaves' own records.
Its shared-lock probe asserts the pinned
LFO3 backend's exported context size and uses its 36-byte event rows, retaining
all exact native/sparse lock values and ownership assertions.

### Unused-module MAIN work

`tools/bench_st_idle_main.py` compares complete integer idle callbacks, native
operation dispatch with callback-entry counters and all-stock parameter
transport across core/bank pairs, with non-increasing instruction gates.
`tools/bench_st_arp_accept.py` compares 26 Sound-setting/acceptance cases,
including otherwise-OFF ARP started by a MODE p-lock, exact state/notes/pools
and non-increasing counts. Native synthesis/publication boundaries are explicit.
The existing cfbench LFO3 and stereo drivers verify actual EMAC and whole-path
count gates separately. See [idle MAIN performance](IDLE-MAIN-PERFORMANCE.md).

The route-bitset prototype in core 1.8.1 is withdrawn. Core 1.8.1.1 uses the
original 1.8.0 native-dispatch tests. `test_st_module_startup_emu.py` now runs
the actual binder after resident copying and invalidation of appended boot
storage, checking full/empty lists, rejected initialization, allocation failure,
list order/publication and ABI. `--broken-core` reproduces the withdrawn
1.8.1 fault at 40347BC2 as a negative control. Initializers/heap are explicit
boundaries; this is not full device boot. The builder and fast-path regression
also reject nonresident runtime startup entries. Common regressions include
the new target test when Unicorn is supplied.

Core 1.8.1.2 replaces that prototype with resident assembly and routes encoded
in the existing interest bytes. `test_st_native_routes_emu.py` checks 256
encoder cases and 11,264 dispatcher cases, exact TYPE/PAGE flags, order,
consumption, duplicate/unknown/long lists, nested setter snapshots, ABI,
canaries and a corrupted-route negative control. It joins common regressions
when the linked core contains the encoder. `bench_st_native_routes.py` gates
all-interested dispatch overhead, including immediate consumption and empty
lists. `bench_st_core_poly_events.py` compares full states/pools across cores,
reports one-time configuration work separately and requires non-increasing
counts for subsequent keys/sequence events. `bench_st_arp_accept.py` accepts
an optional `--before-core` for its unchanged acceptance/count assertions.

Core 1.8.1.3 adds `test_st_native_service_emu.py --before <core-folder> --after
<core-folder> --out <json>`: 2,880 independent linked service cases per core,
checking consumption/results, original replay, zero/uninitialized results,
filtered/full/empty/long lists, all operations and invalid unsigned values,
callee registers/stack, deterministic timer wrap, legacy profiler canaries and
counter wrap. Real callbacks change the profiler registration and make a nested
service call to check entry-snapshot semantics. Every call count must not increase.

`test_st_core_mixer_emu.py` with the same arguments checks 360 linked mixer cases
per core against the stock register/stack/table/callback/EMAC-state oracle,
with zero/one/two subscribers and non-increasing complete-case counts. Only
event-list operands and table addresses are rebound to the test harness.
Its unchanged MAC arithmetic is compared on both sides, not treated as a
physical arithmetic oracle. Both tests join common regressions for the new
wrapper when a compatible previous core is supplied; otherwise self-comparison
checks behavior and the missing performance A/B is reported as skipped.

`test_st_patchers_emu.py` executes the real LFO3 and Modulation Skip
initializers after resident copying and nonresident clearing. It checks
LFO3 SRAM/hook installation, MSKIP ON and 128 zero-depth modulation blocks
(124 skipped, no errors), plus a refused-preimage negative control. Native
allocation/mutex and other module initializers are explicit boundaries;
this is not a complete boot or IRQ. Common regressions run it for banks
containing Modulation Skip. `bench_st_lfo3_events.py` measures the separate
empty-extra-lock event bookkeeping, with native event bodies as boundaries;
these counts are per call, never a claim of calls per audio block.

`test_st_lfo3_events_emu.py --core <folder> --before <previous bank> --after
<candidate bank> --out <json>` executes the old bank's thirteen-lookup
converter as an independent ownership oracle. It checks 1,512 row/owner
cases (overlaps, precedence, TRACK exclusion, legacy/null contexts, unsigned
wrap) and 188 exact event/copy cases with real shared sparse lanes, owned
patterns, PLAY-start static locks, invalid pointers and overlapping ARP
output. Every measured event must use no more instructions than the
reference. Both banks need sibling work/ELF files. Native event bodies,
allocator and mutex are boundaries. Common regressions run it when the
candidate exports the optimized row resolver and a previous LFO3 bank is
supplied. This neither changes the renderer's suspension policy nor measures
hardware CPU percentages.

`test_st_idle_bisect_emu.py --core <folder> --reference <R bank> --catalog
<S bank> --parser <host parser> --out <json>` validates the same-layout
diagnostics generated by `tools/build_st_idle_bisect.py`. It compares all
bytes except explicitly recorded module pointer/checksum words, invokes
the real C parser/mutation checks, and executes the packed directory's
resident binder with real diagnostic/LFO3/ARP/POLY/MSKIP initialization.
It requires exact native-interest masks/slot limits, ABI/list publication,
LFO3 hooks enabled only in R, and MSKIP ON with 124/128 synthetic skips and
zero errors in both. Negative controls skip zero-interest registration
(must expose fail-open dispatch) and corrupt the checksum (must be refused).
Heap/mutex and other initializers are boundary doubles. It does not measure
physical CPU, scheduler/DMA or complete boot. See
[the diagnostic example](../examples/idle-bisect-st/README.md).

The same test accepts repeated `--variant <T/U bank>` for the complementary
groups made by `split_st_idle_bisect.py`. It independently checks every
packed initializer/subscription pointer and event against R, exact disabled
groups, unchanged bytes outside those fields/checksums, real enabled LFO3/
ARP/POLY initialization, native-mask slot limits/routes and MSKIP counters.
Both variants reuse the original hardware-tested R executable/relocations;
the generator first verifies its recorded source/object hashes. They are
not rebuilt banks or normal functional/performance releases.

`split_st_idle_bisect.py --set lfo` adds V (only LFO3 services) and W
(V with only its native subscription disabled, preserving initialization).
The same `--variant` check requires both real LFO3 hook/SRAM installations,
two native-mask slots, no ARP/POLY initialization, the LFO native route only
in V and zero native routes in W. It independently verifies the selected
subscription pointer and that the initializer/tick/draw/data/code remain
unchanged, plus the MSKIP counters and all earlier variants' checks. This
isolates the callback/route, including page operations, not only the renderer.

`test_st_lfo3_activity_emu.py --core <folder> --before <bank> --after <bank>
--out <json>` checks LFO3 1.7's published activity/core interest against its
effective parameters. It runs the real module initializers and 855 public
publication/lock transitions with task/IRQ ABI checks, exact IRQ-masked
bitmap/route writes, fine DEP/FX/static locks, editor/republish, nested busy,
CRC fallback and POLY's retained interest. A suppressed update is a negative
control. It gates inactive/active entry counts and charges both route changes
to a sixteen-idle-block amortization check. Native DSP/event bodies, metadata,
heap/mutex are explicit boundaries; real renderer/EMAC verification remains
in analog-emu. Common regressions include it when the bank exports activity
rows and a previous LFO3 bank is supplied.

The event-copy differential keeps strict non-increase when no activity update
occurs. For explicit DEP/DEST updates it reports the cold operation separately
and limits its added cost to 128 instructions with the host/router absent
(measured max 92); the integration test measures actual route publication.
All byte/state/native-callback/ABI assertions remain exact. The ARP persistence
fixture identifies LFO3's local `host` using its ELF file/unique `ddr_bank`,
preserving the real codec checks when GCC reorders local symbols.

`test_st_idle_audio_cuts_emu.py` validates the X/Y and Z1/Z2 variants generated by
`build_st_idle_audio_cuts.py` from the hardware-tested production build. It
independently checks every initializer/directory/subscription field and the
exact authored RTS target (only known void audio signatures), unchanged bytes
outside selected pointers/checksums, unmodified parser/mutation checks, real
LFO3/ARP/POLY/stereo/recorder/MSKIP init, stereo SRAM placement, identical
native masks and bus bytes, a warmed stock POLY cache and untagged/tagged-empty
callback blocks. MSKIP must retain 124/128 skips with zero errors. Native
metadata/heap/mutex and DSP/event bodies are explicit boundaries; this is not
hardware CPU timing or full boot. These diagnostics preserve UI/tick and
native operations and are not normal functional/performance releases.
The generator's `--set split` produces Z1 (only stereo audio callbacks cut)
and Z2 (only Recorder main-mix cut) from the same source-verified production
bank. The test's independently declared expected cuts check each one; its
original byte/directory/parser/init/mask/SRAM/MSKIP assertions are retained.

`test_st_idle_audio_paths_emu.py --core <folder> --before <bank> --after <bank>
--out <json>` checks the functional samplers 1.4.8 / Recorder 1.1.1 bank
against its predecessor. It runs actual initialization, stereo SRAM placement
and the complete idle audio subscriber set (untagged/tagged-empty). It also
compares 142 Recorder transitions, including all capture states, clock wrap,
MAIN source/mode, mono/stereo audition while closed, invalid source and view
closure; worker state, PCM and preview buses must agree exactly. Empty entry
counts must improve. Native metadata/heap/mutex and original LFO DSP are
boundaries. Stereo DSP is separately compared on analog-emu with the existing
`test_st_stereo_main_current.py --require-no-count-regressions`: exact output,
EMAC/ABI, DDR/SRAM/fallback/Chorus/occupied windows and complete stable/dynamic
block budgets. No hardware CPU percentage is inferred from these counts.

Samplers 1.4.9 commits all four gain rows after MAIN/DIRECT. The linked
stereo differential installs each native gain target immediately before its
own event, retaining all original bus, D, EMAC and ABI comparisons. Its new
negative control suppresses only the empty gain-history commit and must
produce a bus mismatch when stereo activates later. The native model test's
standalone arbitrary-bus add adapter retains its original immediate row
postcondition; production events use the combined commit. SRAM placement
case 4 corrupts the new commit span and requires fallback of its pointer and
all prior code/state. The added 104 bytes must fit the existing bounds; those
bounds and all previous placement/occupied/Chorus checks are unchanged.

The Recorder/idle fixture requires identical closed-entry counts when its
recorded object hash is identical. It retains strict improvement when that
object changes, plus every exact transition/state/PCM/preview comparison.

`tools/bench_st_modulation_skip.py --before <bank> --after <bank> --out <json>
--require-no-regressions` compares full linked 1.2.3/1.3 calls over fourteen
128-block matrix/source/publication/error cases, checking exact SRAM, decisions,
counters and ABI plus sixteen periodic/wrap/mode/pointer dispatch edges. The
native fixtures require `SYNTAKT_WORK`; no physical CPU percentage is inferred.
`tests/test_st_modulation_skip_cfbench.py --bank <bank> --cfbench <executable>
--out <new-folder>` exhausts all 65,536 zero-projection inputs against an
independent model on analog-emu, preserving neighbors, ABI and physical EMAC;
an RTS mutation must fail. The host decision check exhausts 8,192 dirty masks.
The full stock comparison retains all SRAM/heap/mode/error assertions and now
models code-cache invalidation when patching the previously executed writer.
Actual publication must set one dirty row/count two writes; its refresh must
read that row's exact 24 matrix addresses in order, with fresh instrumentation.

MSKIP 1.4's linked comparison uses 1.3 as its baseline with the same complete-call
budgets. The native stock oracle adds 72 mixed-row blocks: dirty captures, clean
active replay, zero-only saturation, all six source groups, overlap fallback,
matrix deactivation/reactivation and the 64-block audit boundary. SRAM and LFO
heap must remain identical to native stock; each selected group and transition
must actually reach partial execution. The same test also runs against 1.3.
