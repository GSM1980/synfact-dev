# FM TONE for Syntakt

Local source port of Synfact's FM TONE: logical machine **65**, category
**10 (SY)**, four SYN pages and 28 persisted scalar controls. RATIO B combines
B1/B2 on one encoder, including native paired parameter locks and the original
packed pair for page copy/paste/reset. Its sound and lock values stay separate.
The four SP machines can share the same SMZ.

Use **core 0.12.2** with its AUDIO companion and matching bank for complete
machine-selection defaults. Older UI service APIs reject
the FM module initializer before publishing AUDIO. Graphics, formatting, input
semantics and FM DSP belong to the SMZ; core services are machine-independent.

## Build from the imported sources

```powershell
python -m elekloader.sdk.syntakt.smz mods/samplers-st mods/fm-tone-st --out out/new-bank.smz4
```

This compiles C and symbolic assembler, including private imported instruction
source, tables, UI resources and initialized state under `imported/`. It reads
no Digitone SYX, extracted firmware section or previous SMZ. The source hash
report includes nested headers and assembler includes. Never overwrite an old
delivery: choose a new output path.

`tools/import_synfact_fm.py` is a separate, one-time local import from the
existing optimized Synfact reference build and its local extracted UI data.
It emits instruction-by-instruction assembler with symbolic relocations,
constant/data source and a provenance manifest. It does not run an emulator.
`tools/adapt_synfact_fm.py` prepares the authored adapter/UI from that imported
assembly and the Synfact drawing C; it is not part of normal compilation.
These maintenance tools require the local Synfact reference tree. Private
imported firmware-derived sources remain ignored by Git and are not licensed
for redistribution by this port.

## Performance contract

Target compilation uses **`-O3 -mcpu=54415 -mtune=cfv4`**, with the existing
four-iteration full-peel limit. The assembler retains Synfact's optimized
FM_CACHE kernels, indexed per-track state, native SRAM voice templates,
signature-based reinstall after native voice reset, raw-parameter cache,
16-slot HARM bake and its original-morph fallback for every nonzero feedback
value (0.2; the earlier guard covered only zero-based algorithms 6/7). Per-render
context copying remains absent. The initialized workspace is loaded as data;
there is no eight-voice startup initializer in the IRQ.

Core/kernel/constants/waves/workspace preserve their reference address phases
modulo 2 KiB, including constants `5c0`, waves `230` and workspace `080`.
This preserves the cache placement tuning when samplers precede FM in the bank.

**ECO remains locked ON**, matching Synfact's current delivered profile. Page
four position eight stays empty; the x2 kernels/decimator remain in the imported
engine but no track requests x2. This port does not re-enable that control.
The HARM approximation remains for steady controls without feedback. With
feedback the native morph kernel prevents rounding from circulating through
the feedback loop. This does not make the shipped x1 profile identical to
Digitone x2, and no hardware CPU percentage has been measured.

## SYN page drawing cost (MAIN)

The page is drawn on MAIN, and its cost peaks while a trig is held to edit
a lock: with a trig held, the native lock-preview read `GET` (`40039160`)
allocates a callback, walks the trig selection and frees it on every call.
The donor drawing read up to 42 values per frame, most of them twice or not
drawn at all (B1 and B2 both read the one stored RATIO B). It also drew its
bitmaps with one native `PIX` call per pixel, zeros included, over an area it
had just cleared. The port keeps the donor's pixels and:

- reads each stored control once per frame (value and lock flag together),
  and only the controls the page shows;
- draws only the set pixels of a bitmap inside the area `fm_ui_draw` cleared
  (rows 0..51); below it, zeros are still written as the donor did;
- writes colour-1 pixels straight into the native 1-bit plane, as `PIX`
  (`400f7140`) does, with the same clipping: `dot` per pixel, `column` for up
  to 32 rows of a bitmap or glyph column at once. Colour 0 and computed
  colours still call `PIX`. The plane is column-major (`ctx+16`, words per
  column at `ctx+12`, clip size at `ctx+4`/`ctx+8`; bit 31 = first row).

Per frame, from `tests/test_st_fm.py` (averages over its 64 states per page):

| SYN page | native GET | PIX calls |
|---|---:|---:|
| 1 (ALGO … MIX) | 42 → 8 | 3,165 → 38 |
| 2 (ratio offsets) | 32 → 5 | 1,706 → 0 |
| 3 (envelopes) | 40 → 13 | 1,700 → 4 |
| 4 (delays, trigs) | 38 → 8 | 1,713 → 0 |

These are call counts on the host, not MAIN CPU measured on the Syntakt.
The harness draws into a context with the native layout, between zero
guards, and checks `dot`/`column` against its `PIX` double at every position
up to and past the edges (114,034 writes).

## Validation

`tests/test_st_fm.py --bank BANK --synfact PATH --cc I686_CLANG` checks:

- 4,692 imported instructions, every constant/wave/state word and all symbolic
  relocations against the optimized reference, allowing only unreachable
  post-RTS alignment zero words to become valid NOPs.
- All five tuned cache phases, zero ECO request/latch defaults and optimized
  bake source, with only the narrowly allowlisted 0.2 feedback guard change.
- 256 frames from the production UI and the original Synfact drawing C,
  with identical test primitives, values, algorithms and lock overlays,
  drawn over a patterned (not blank) screen. Native toggle bitmaps/page
  indicator are borrowed unchanged and mocked here. The same run counts the
  UI's native GET and PIX calls (see "SYN page drawing cost" below).
- 361 B1/B2 combinations, carries/clamps, creation/deletion of paired locks,
  page-operation pair encoding, all 28 control bounds and eight-track transport.

Additional native C regressions cover UI API 4, module initialization failure,
28-control MAIN/AUDIO snapshots, the project pool, Sample Manager and all four
SP engines. Full core build, linked instruction audits and lint must pass.
These are static/native-host tests, not ColdFire execution. Physical loading,
audio comparison, real encoder/lock/page operations and CPU measurement remain
pending on the Syntakt. No device write or transfer is performed by these tests.

## Install the local candidate

Input uses ordinary SMZ subscriptions to the existing `ev_enc` / `ev_key`
collections with native-view context (`ST_EV_VIEW`). There is no private
machine input registry. Module service API 2 / UI API 4 is required; the core
rejects older FM banks that still require `bind_input`. Native page, lock and
undo routing, UI graphics and the optimized AUDIO payload remain unchanged.

The current combined candidate includes Audio Recorder, digital POLY and System info, requiring core
0.12.2. Install its matching SYX first. From the repository directory, the
existing transfer command for the combined bank is:

```powershell
python -m elekloader.sdk.syntakt.mods put out/core-st-0.12.2-fm-defaults-poly-fix/synfact-machines.smz4 --yes-write-device
```

The CLI checks readback; restart to activate the bank. `/mods` is the single
bank file, so the delivered bank includes FM TONE, the four existing SP machines,
the [recorder](../recorder-st/README.md), [POLY](../poly-st/README.md)
and [System info](../system-info-smz-st/README.md).

Core 0.12.1 fixes native SourcePageView editing on SYN 2/3/4 and moves ENC to
the one common post-virtual resolver. This also reaches RATIO B's existing
paired handler. Selector visibility and page ownership are core decisions,
shared with SP; the FM renderer uses its supplied page. The ratio algorithm
remains Synfact's `2*(19*ID218+ID204)` ordering (361 pairs, raw 120 = 1/1):
the lower physical B ratio advances first, then the upper on carry. Donor legacy
B1/B2 variable names are inverted relative to physical operators; preserve IDs.
The native increment engine handles pressed/accelerated input; both lock halves
are seeded before movement, and native page actions keep the compound codec.
23,826 cases compare the original donor ratio C against the port, in addition
to 256 byte-identical drawing fixtures. Physical encoder/audio checks remain pending.

Core 0.12.2 completes confirmed-selection defaults for every registered control
beyond the native first eight, using the shared descriptor and model protocol.
This initializes FM's envelopes, trigger/reset modes, B2 ratio and neutral
offsets without resetting saved Sounds/projects on load. Reselect FM TONE for
a fresh default Sound. All 28 range/default contracts are checked against
independent Synfact declarations/reference data; FEEDBACK reaches `7f00`, as
in Synfact. Default page-3 envelope graphics match the donor drawing exactly.

Core 0.12.8: LFO and Sound Setup destinations offer the native None again
(slot 0, where RATIO OFFSET C is stored). New FM Sounds load with None; a None
route with depth no longer modulates RATIO OFFSET C, which stays editable but
is not a modulation destination.

## One stored RATIO B (2026-10-27)

Device tests showed the native single/page lock clear removes the lock of the
visible RATIO B half but never the hidden B2 lock, even when B2 alone was
cleared. RATIO B is now ONE stored control in its native slot `15`: the
combined index `19*B1 + B2` (0..360; physical B1 = slow digit, B2 = fast) times
88, so the 361 combinations span the native `0..7f00` range. One lock, native
clear/copy/paste/undo, and modulation of the whole index (B2 cycles, B1 steps
every 19) follow without a paired codec or a key handler; the encoder keeps the
original carry/clamp stepping (23,826 cases against the donor algorithm). The
SYN page and graphics are unchanged (256 frames byte-identical to the donor
drawing for the same logical state). The value popup reads
`RATIO RB1=<B1> RB2=<B2>`; other places show `<B2>/<B1>` as the page does.

The freed slot `45` now holds RATIO OFFSET C (parameter 23): lockable and an
LFO/Sound Setup destination. Slot 0 stays the native None destination with a
hidden placeholder (parameter 24). AUDIO decodes `index = word / 88` and reads
offset C from transport word 15. Sounds, kits and patterns saved with the
previous layout are NOT converted (user decision): re-enter their RATIO B and
ratio offset C, and remove old B2 locks, which now read as offset C locks.

## Digitone Sound import

Digitone Sound import is maintained in [tools/digitone.py](tools/digitone.py),
with the current Elekloader storage layout, one-word RATIO B, all four ratio
offsets and AMP ATK minimum 8. Synfact Transfer uses its vendored copy.
See [the import contract and tests](tools/README.md).

## Offline audio differential (2026-10-28)

`tests/test_st_fm_audio.py` executes the built FM adapter, its extended-control
receiver and the original SHA-pinned Digitone 1.44 DSP with native ColdFire EMAC.
See [the workstation commands](../../docs/SYNTAKT-BUILD.md) for the required
Unicorn library. The clock, parameter row and scheduler calls are explicit
fixtures; the output is raw mono Q31 before Syntakt AMP/filter/FX.

The 48 cases cover all eight algorithms, PHRT 0/1/4, slow operator attacks,
HARM +7.5, FDBK 32, two retriggers and release, on the first voice. Operator
scalar states agree in x1 and x2; the retained x2 mode also agrees sample for
sample with Digitone. x2 is selected in emulator memory only. The shipped ECO
profile remains x1. The different x1 PCM is not proof of an attack-click defect.
This is not an AMP, POLY allocator, full-boot or physical listening test;
[IMP-018](../../improvements.md#imp-018--clics-de-ataque-fm-fm-c) remains open.

`tests/test_st_fm_onset.py` additionally executes the original Syntakt AUDIO
and Digitone MAIN AMP routines. It checks six native attack ramps against each
other and exports fixed-gain 24-bit WAV comparisons of INIT/modulated FM with
AMP ATK 0/32/96. Controls, an already released zero-gain envelope and scheduling
are explicit fixtures; filter/FX, mute, voice allocation, full MAIN/AUDIO timing
and physical capture remain excluded. The routines' different onset block counts
must not be presented as measured device/MIDI latency.

Its long x2 test also distinguishes the weighted-wave HARM bake from the native
morph fallback: the modulated example first differs after 76.7 ms, by at most
388 Q31 units over the 500 ms test, while the fallback remains sample-exact.
The first 6 ms of the baked x2 path must still agree with the native reference.
This does not establish a cause of the reported first-note click.

## Factory AMP comparison (2026-10-28)

The native factory AMP defaults differ: Syntakt ATK is **0**, Digitone 1.44 ATK
is **8** (about 1.875 ms in their identical attack tables). Syntakt also has
HOLD 64 / DEC 127 / SUS 100 / REL 32 with digital AD mode; Digitone has
DEC 32 / SUS 96 / REL 24. Equal INIT labels do not imply equal AMP controls.
Selecting FM keeps existing common Sound controls; no AMP default override
has been added to this machine.

`tests/test_st_fm_factory.py` reads the pinned native MAIN descriptors, checks
the Sound constructor's AD mode selection, and executes FM plus native AMP
for notes 48/60/69/72 after an explicit silent pre-roll. Changing only Syntakt
ATK from 0 to 8 reduces the first nonzero sample's step from silence by over
20 dB in all four fixtures. Try AMP ATK 8 for the reported untouched INIT case;
physical confirmation is still pending. This does not prove the cause of the
older report with already matched ADSR/soft attack. The test excludes full
boot/selection, filters/FX, mute/prior-note lifecycle and physical capture.

## HARM / DTUN / FDBK audit and feedback fix (0.2, 2026-10-29)

`tests/test_st_fm_sweep.py` builds independent SYN inputs from the SHA-pinned
Digitone MAIN descriptors. In 1,104 static cases (8 algorithms, MIDI 48/60/84,
INIT and a modulated fixture, 23 control points), every received word matches.
The x2 native-morph diagnostic is sample exact with original Digitone in every
case. The shipped x1 path differs; selecting x2 is an emulator-only diagnostic.
There is no range/slot correction to HARM, DTUN or FDBK from this result.

The old HARM bake can diverge with feedback outside the previously protected
algorithms, including algorithm 1 at FDBK 127. Version 0.2 keeps the original
morph kernel whenever feedback is nonzero; no-feedback baking remains intact.
`tests/test_st_fm_feedback_guard.py` compares actual before/after banks on all
algorithms, three notes and feedback 0/32/127, with 500ms C4 notes by default.
All 48 feedback cases match native Digitone PCM at x2; all 24 no-feedback cases
preserve the prior x1 and x2 PCM exactly. The full combined bank is separately
checked with the short 64-block matrix and the existing 48-case native test.

The cost is measured only as emulated steady update/render instructions per
voice: unchanged without feedback; up to approximately +17% at x1 and +20% at
x2 with feedback in these fixtures. These are not device cycles or total CPU
percentages. MAIN, SRAM allocation, control IDs and the fixed ECO profile stay
unchanged by this correction. Physical CPU/audio acceptance is pending.

Evidence: `out/fm-sweep-20261029/`, `out/fm-feedback-guard-20261029/` and the full
bank `out/fm-feedback-fix-20261029/`. Listening sweeps use matched native AMP
ADSR 8/32/96/24, algorithm 1, MIDI 60, 250ms per control value, fixed gain 0.2,
no onset fade or normalization, and a disclosed 10ms final-file fade. They
exclude filters/FX, mixer/DAC, polyphonic allocation and full device timing.
