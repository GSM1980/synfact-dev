[Synfact docs](README.md) › Tests

# Tests

Each suite takes its input files from environment variables or arguments. A
test whose input files are not given is skipped, not passed: say which ran.
Firmware files never go in this repository.

## Syntakt

The Syntakt's suites (`tests/test_st_*`, `tests/test_core_st_*`,
`tests/test_syntakt_secondary.py`) and the regressions that run them all on a
build folder are in [SYNTAKT-BUILD.md](SYNTAKT-BUILD.md): the commands, the
toolchain they need, and the emulator checks.

```bash
python tests/test_units.py        # needs nothing; always run it
python tests/test_st_fm_recipe.py --digitone Digitone_and_Digitone_Keys_OS1.44.syx   # FM TONE's recipe
```

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
all byte alignments, exact stack-census reads, and 120 queue admission cases.
`tools/bench_st_poly_keyboard.py` preserves already-correct stock/core analog
master-Sound routing in both banks without changing the historical test's
requirement for a broken-bank negative control. Counts are instructions,
not hardware cycles or MAIN percentages. See
[POLY MAIN performance](POLY-MAIN-PERFORMANCE.md) for commands and limits.

The shared-lock probe in `test_st_poly_copy_events_emu.py` asserts the pinned
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
