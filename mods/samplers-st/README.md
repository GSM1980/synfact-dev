# SP sample machines 1.3

Four source-built sample machines for Syntakt and its explicit AUDIO companion.
Banks 0.9 and later require the existing UI API 7 and module API 3 for guarded native-view
encoder events and consuming pre-key events. Banks 0.6..0.8 used module API 2;
older bank/core requirements below are historical.
Their MAIN descriptors, graphics and AUDIO engines live in an SMZ4
bank. The core contains only generic dispatch/services for them.

```sh
python -m elekloader.sdk.syntakt.smz --preset samplers --out out/samplers.smz4
python -m elekloader.sdk.syntakt.mods put out/samplers.smz4 --yes-write-device
```

The second command replaces `/mods` over MIDI and verifies its readback. It
requires `python-rtmidi` and a device running the core. Restart afterwards to
load the bank. `mods_object pack` remains available for other Data RPC clients.

Run from the repository root with the ColdFire cross compiler configured as
for the SDK (`ELEKLOADER_CROSS`). **No SYX is required to compile this bank**;
there is no Digitakt firmware input, hidden precompiled motor or binary patch.
The optional `--stock` verifies Syntakt target identity only. Firmware building
still requires the official Syntakt OS, separately from machine compilation.
Outputs and compiler intermediates must remain in ignored `out/` directories.

The manifest declares several descriptors sharing a set of AUDIO sources.
To compile a custom selection, pass source directories instead of `--preset`.
The generator compiles C/assembly, links MAIN relocations and fixed-base AUDIO,
audits executable instructions, creates checksums and writes a source/hash report.
It uses the device toolchain profile, without extracting code or tables from an OS.

| Machine | Type | Controls E/F/G |
|---|---:|---|
| SP ONE SHOT | 66 | START, LENGTH, LOOP POSITION |
| SP WERP | 67 | SEGMENT SIZE, SEGMENT PLAY MODE, BARS |
| SP REPITCH | 68 | START, LENGTH, BARS |
| SP SLICE | 69 | SLICE SELECT, ADDITIONAL SLICES, GRID |

All four appear in the native sample-machine category **11**, shared with
TwinShot. Types and control slots retain their earlier values. Install core
0.8 before uploading bank 0.3: older cores reject its `machine-ui-v1` feature.
The new core still accepts older banks.

Shared controls A/B/C/D/H are TUNE, PLAY, BIT REDUCTION, PROJECT POOL SLOT
and OVERDRIVE. REPITCH leaves A empty on both SYN pages: pitch is set by tempo,
window and BARS, so there is no TUNE knob to edit. LEVEL remains the native track level. PLAY values 0..3 are
reverse, reverse loop, forward loop and forward. The SAMP control selects
one of the 128 **project pool** positions; it does not select a disk file id.

`audio/sampler.c` owns the integer PCM engine, interpolation, bounded crossfade,
bit reduction and per-track state. The four separate machine sources define
their window/tempo/slice logic. `main.c` contains descriptors, labels, ranges
and defaults; `ui.c` owns graphics, formatting and encoder policies. From 0.6,
`input.c` joins the existing guarded view KEY/ENC collections for playback-mode
stepping; it installs no application hook. Native Syntakt services
provide pitch step, filtering, overdrive, envelopes and output mixing. The generic
core API lends immutable PCM descriptors to these engines.

ONE SHOT uses absolute 0..120 Q8 start/length/loop positions. REPITCH computes
the tempo-derived step from the selected window and BARS, with a four-times
rate cap and overflow checks. SLICE uses a 256-point bounded zero-crossing
search, 4..64 slices, and note-based selection when SLCE is zero. WERP advances
a separate position and segment clock and retriggers its windows on segment
boundaries. Frame/fraction cursors support long PCM without a signed-Q12
duration limit. Actual duration is limited by the core's available PCM RAM.

This is an experimental C implementation of those behaviors. It is not a
bit-exact reproduction of another device's DSP. Native-host tests cover PCM
boundaries, forward/reverse/loop modes, fractional cursors, long scaling,
REPITCH, SLICE and WERP decisions; static audits cover target instructions.
Physical sound, CPU cost, UI and boot remain to be validated on a Syntakt.
No emulator, device transfer or flashing is part of this build workflow.

The generic Sample Manager, filesystem, project record and PCM transfer live
in [the core](../core-st/SAMPLE-POOL.md). They remain available without an SMZ.

## 1.4: lighter PCM and stereo engine (2026-11-10)

The same sound for less CPU, word for word on AUDIO (instruction counts in
docs/MACHINE-REFERENCE.md, "Stereo tracks"):

- `sp_pcm` renders the stretches of a block where the cursor needs no
  crossfade, no wrap and no end in runs: interpolation alone, one loop per
  case (channels, BR/OVER, direction). The per-sample model does every other
  sample. On the unit the plain runs (no BR, no OVER; mono, or both channels
  of a stereo sample) are hand-written loops with the same integer
  operations. A track's PCM, mono or stereo, costs about a quarter to a
  third of 1.3's. `tests/test_st_sampler_fast.c` compares the runs with the
  model on random windows, loops, steps and modes on the host (the C loops),
  and `tests/st_sampler_pcm_check.c` does the same on the target in
  analog-emu (the hand-written ones).
- A stereo track runs its voice tail (render filter and gain ramps) on L and
  R in one loop each (`audio/voice-stereo.c`), with the native routines' own
  EMAC operations.
- The engine's chain end exchanges the control state once and copies it back
  once (1.3: three copies), four words at a time, packs D with fewer
  instructions, and with OVERDRIVE off on every claimed track runs the drive's
  dry MAC itself instead of the native drive on all eight R buffers.
- The R multimode skips its coefficient part (R's targets are L's) and
  enters the native routine where its filter part starts.
- A track is stereo on the signal, not while its sample plays: the block a
  stereo sample is triggered in, while its output sounds, and the block its
  PCM starts after silence. In STOP a loop under a closed envelope no longer
  keeps the R pass running. A track whose D stays under -96 dBFS is mixed
  mono.
- A tail stays stereo while L or R is at or above -96 dBFS
  (`ST_STEREO_QUIET`), not while it is nonzero: the native multimode's LP
  types never reach 0 after silence, which kept a track stereo, and its R
  pass running, after it had played a stereo sample. With no stereo track
  the engine sends one empty block and then does nothing.
- MAIN's gR × D runs on EMAC, four tracks a pass, with D expanded once a
  block for the four buses: about half the instructions, and the gain's full
  32 bits instead of 16. A clipping bus saturates after each four tracks
  instead of after each track; nothing else in its output changes beyond
  the products' truncation.

`tests/test_st_stereo_natives_emu.py` runs the replacements of native calls
against the natives, MAIN's mix against a 64-bit model and the PCM loops
against the per-sample model in analog-emu; `tests/test_st_stereo_engine_emu.py` runs the
chain end in Unicorn (docs/SYNTAKT-BUILD.md).

## 1.3: stereo tracks, transport only (2026-11-10)

`stereo/` is the stereo-track engine (docs/MACHINE-REFERENCE.md, "Stereo
tracks"), written in the core's shape so that it can move into the core:
`stereo/audio.c` runs the native chain on R at the core 1.8 chain end and
sends D = R − L; `stereo/main.c` (module `STEREO`) adds gR × D to the right
buses. `sp_render` renders R for a claimed track with the same four native
calls as L.

A track is stereo while it plays a stereo sample (core 1.8: the reader takes
L and R from the interleaved PCM, zero crossings on L) and until its chain's
tail has ended on both sides; a mono sample keeps the track mono and costs
nothing. With only mono samples the engine is never installed, and the
module reads one window word a block. A transport test build adds a file `stereo-test.h`
(`#define SP_STEREO_TEST 1`) to a copy of this folder: every sampler track is
then stereo while it plays and while its chain still sounds on either side
(a silent track costs nothing),
with R read from the same sample 480 frames (10 ms) behind L. It
costs what reading a stereo sample's second channel will cost, and a working
transport sounds wider.

Needs core 1.8 for stereo; on an older core the engine stays off and every
sample plays mono.

The sample list shown while turning SAMP covers the whole page right of the
track rail, the SYN controls included, so long names fit; the right end of
each row is cleared before the scroll bar (the native text call does not clip).
On core 1.8 (UI API 8) it tags a stereo slot `ST` at the row's right end, and SYN page 2 draws a stereo sample as two
bands, L above R; a mono sample keeps the single full-height wave.

## Controls and SYN pages

TUNE spans **-60.00..+24.00**. Its visual dial uses the entire travel on each
side of zero; it no longer reaches the minimum at -24. PLAY and WERP MODE use
the native four playback graphics (reverse, reverse loop, forward loop, forward).
SAMP displays **1..128**, selecting the project pool; its label stays **SAMP**
while the popup shows **SAMP = selected sample name**, or NO SAMPLE for an empty
slot. PLAY's popup shows REVERSE, REVERSE LOOP, FORWARD LOOP or FORWARD;
WERP MODE uses the same names. Bank 0.4 needs core 0.8.1 for the corrected
popup route and separate control/popup context. BR stays the discrete
**0..127** control. OVER shapes the sampler's PCM before the native
filter/amp/mixer path.

In 1.1, BR quantizes positive and negative PCM symmetrically, preserving
silence. Its 0..127 curve removes bits faster in the lower half of the knob;
zero remains a bypass and 127 keeps the existing two-bit endpoint. This curve
is an approximation because the Digitakt mk1 DSP mapping is unpublished and
no matching device recording is available. Stock waveforms get a whole-sample
preview on the first UI visit and later refine to exact peaks.

In 1.2, OVER uses a bounded, symmetric soft clip on the sampler PCM. Zero is
an exact bypass and does not change the track level. The common AUDIO word
previously used for OVER is an output gain: at zero it silenced the sample.
It is fixed at its unity value while OVER shapes the signal before the native
filter, amp and mixer. This curve is source-built and is not claimed to match
TwinShot's internal overdrive exactly. Hardware listening and intermittent
waveform reproduction are still needed to confirm the user-facing behavior.

ONE SHOT START/LEN/LOOP and REPITCH START/LEN use **0.00..120.00**, with the
native fractional encoder policy and Q8 display (for example 101.94). WERP SEG
uses the three segment glyphs and 1/32, 1/16, 1/8; BARS displays 1/2/4/8.
SLICE SELECT displays NOTE at zero and slices 1..64 otherwise. Its LEN is an
integer number of additional slices, displayed 1..64, and GRID uses the five
4/8/16/32/64 slice graphics. These discrete controls retain integer knob behavior.

The second SYN page repeats the same controls (A stays empty for REPITCH), showing the selected
waveform with START/end/LOOP markers or segment/slice grids, plus an active
playhead. LEVEL and the native header remain in place. Wave data comes from
the core cache; drawing does no storage IO. A stationary waveform causes no
extra idle redraw. All four machines have eight parameters, so they need no
extended MAIN-to-AUDIO parameter traffic.

Every C source uses the shared MCF54415/O3 profile documented in
[SMZ4.md](../core-st/SMZ4.md). Host checks exercise formatting, dial travel,
pool bounds and the native PLAY graphic selection. Physical rendering, knob
feel and sound of this revision still require validation on the unit.

## 0.5: inactive REPITCH TUNE (2026-10-28)

The existing empty-layout ABI hides encoder A and its native page-action entry
on both REPITCH pages. The waveform page also omits its TUNE label. The eight
stored parameter positions, IDs, ranges/defaults and AUDIO sources are unchanged;
legacy TUNE data remains readable without migrating Sounds or projects. Existing
explicit parameter references are retained for compatibility; this is a SYN-page
UI change, not removal of the stored parameter or a modulation-format change.
ONE SHOT, WERP and SLICE keep their TUNE controls. No new core service or LFO3
change is required. Rebuild the sampler sources into the desired complete bank;
uploading a samplers-only bank would replace any other machines/modules in `/mods`.

## 0.6: four encoder steps per playback mode (2026-10-28)

PLAY on all four SP machines and WERP MODE now require four normalized encoder
steps per mode, on either SYN page and with the encoder pressed or released.
Small movements accumulate locally; batched movements count their steps without
acceleration. Reversing direction, changing view/track/page/control/value/lock
context, pressing/releasing the encoder or receiving a view key event clears
the partial movement. Range endpoints clamp without building movement debt.

The `SP PLAY INPUT` module uses the existing guarded ENC and post-native KEY
events and UI API `edit`, retaining native Sound, lock preview, trig/page lock
seeding and undo routing. It adds no core hook, audio callback, allocation or IO.
Stored mode values, defaults, layouts and DSP are unchanged, including 0.5's
blank REPITCH A. The host UI regression checks all five mode controls on both
pages, four-step thresholds, reversal, batches, limits, context reset and lock
seeding. ColdFire builds compare AUDIO byte for byte against 0.5. Physical knob
feel, key gestures and persistence still need confirmation on the instrument.

## 0.7: four steps for further discrete controls (2026-10-28)

The same threshold now covers WERP SEG/MODE/BARS, REPITCH BARS, and SLICE
SLCE/GRID, alongside PLAY on all four SP machines. The module is displayed as
`SP DISCRETE INPUT`; its SDK symbol and subscriptions stay the same.

Limits come from the machine's existing descriptor, without changing stored
values: SEG has three choices (1/32, 1/16, 1/8); MODE has four; BARS has four
(1, 2, 4, 8); SLCE includes NOTE and 1..64; GRID has five (4, 8, 16, 32, 64).
Each adjacent choice takes four normalized encoder steps in either direction.
Batched events can cover the complete SLCE range. Other controls, including
SLICE LEN, retain their native policy. Host regressions exercise all ten
selected machine/control pairs on both pages, pressed/released, full-range
stepping, bounds, context resets and native lock seeding. Bank 0.7 keeps the
0.6 descriptor contracts and layouts, with byte-identical AUDIO; physical validation is pending.

## 0.8: FUNC TUNE across -60..+24 (2026-10-28)

ONE SHOT, WERP and SLICE now snap across -60, -48, -36, -24, -12, 0, +12 and
+24 semitones with FUNC, on both SYN pages. Ordinary and pressed fine turning
keep the native TUNE encoder policy. REPITCH A stays empty.

In 0.8, the same sampler input module handles FUNC through native increment processing
and the core's `edit` service, preserving lock previews, trig/page lock seeding
and undo routing. It retains native floor-to-semitone behavior for off-grid Q8
values before choosing the strictly next/previous octave. Bounds remain -60..+24.

Those three TUNE descriptors borrow scalar cache alias `0x13` (without a fixed
FUNC quantizer), while encoder-only policy still borrows native `0x1f1`. This
prevents the native Sound/lock editor from reapplying its five-note -24..+24
list after our edit. Only these three cached aliases change; IDs, stored values,
ranges, defaults, page layouts and AUDIO remain unchanged. Native shared caches
and firmware hooks are untouched. The existing module symbol/subscriptions stay
the same, so no additional module or new core build is needed.

Checks include 129,030 host range vectors, both pages/pressed input/locks,
production cached-policy dispatch, linked instruction audits and byte-identical
AUDIO against 0.7. Narrow Unicorn fixtures run the stock list lookup (98 cases)
and both original Sound/lock relative-edit bodies with explicit context doubles
(48 cases, FUNC set), verifying no second fixed quantizer runs. These do not
constitute a full boot or physical encoder/undo/persistence test.

The FUNC increment processing above is superseded by the fixed travel in 0.10.

## 0.9: sample list while turning SAMP (2026-10-28)

Turning SAMP opens a seven-row list on the right, with pool position and cached
sample name, an inverted selected row and a scroll indicator for all 128 slots.
The first column of controls and native track/level rail remain visible, following
the supplied Digitakt-style reference. The header reads `Press NO to revert`.
Long names are clipped by the native bounded font renderer; unavailable positions
show `NO SAMPLE`. Both SYN pages and all four SP machines use the same browser.
On SYN2, the list temporarily replaces the waveform while keeping the native
first-column controls; the waveform returns on dismissal.

SAMP stepping itself stays native. NO restores the value at the start of that
browsing session; YES accepts it. Other keys/controls and an inactivity timeout
accept the current selection and dismiss the list. Partial sessions never cross
track, page, machine or Sound/lock edit context. Native edit/clear services restore
an existing lock or remove a newly created lock, respectively. During live
recording SAMP keeps its native edit behavior and does not open this cancellable
browser, since a single captured value cannot revert recorded event lanes.

`browser.inc` is part of the sampler UI and shares the existing
`sp_sampler_input_module`; there is no additional module or native view object.
The module now subscribes to guarded ENC, post-native KEY and pre-native KEY.
Machine idle/draw callbacks supply timeout and painting at the original view's
layer, below later native overlays. The timeout is 240 visible UI idle visits;
its real duration and key feel need physical confirmation. Names come only from
the loaded pool cache: painting allocates nothing and performs no sample IO.

The new host fixture covers four machines, two pages, every pool position,
selection/scroll bounds and name clipping, native left-area preservation, NO/YES
press/release, existing/new locks, dismissal, timeout, and the real file-owned
ENC/KEY integration. It is included in the common regression runner. Module
audits distinguish the legacy two-subscription profile from the browser's exact
three-subscription profile using pinned sources. Cross builds preserve all
descriptor contracts, stored values and layouts, with byte-identical AUDIO to
0.8. The local preview renders the production panel with a fixture background;
it is not a photo or a complete native UI/hardware validation.

## 0.10: slower discrete choices and FUNC octave jumps (2026-10-28)

Hardware feedback on 0.9 confirmed the sample list and hidden REPITCH TUNE,
but four steps per discrete choice still felt too sensitive. The octave list
was correct, but FUNC+TUNE also changed too readily.

All ten discrete machine/control pairs listed in 0.7 now require **16 raw
normalized encoder steps per adjacent choice**. FUNC+TUNE uses the same fixed
distance per octave in ONE SHOT, WERP and SLICE. Both use native event delta
extraction with multipliers 1/1; neither uses native acceleration, pressed
scaling or a one-jump-per-event rule. A batch counts its full distance, so
slow single steps and the same travel in larger events give the same result.
Ordinary/fine TUNE and sample selection keep their native behavior.

The shared accumulator resets on reversal, view/track/page/machine/parameter,
value/lock, pressed/FUNC changes and view key events. After an edit it records
the actual resulting value/lock; reaching an endpoint discards leftover travel.
Native Sound/lock/undo editing and off-grid octave rounding are retained.
No new module, core API, hook, DSP or stored-format change is required.

Stock routine `0x400f68d2` accumulates movement with parameter-dependent
thresholds and acceleration; it does not specify a fixed mechanical angle.
The new threshold is four times the 0.9 discrete threshold. **16 raw steps
must not be described as a measured quarter turn**: the mechanical distance
and preferred feel still need to be checked on the instrument. Host fixtures
cover the new thresholds, grouped/slow movement, direction/context changes,
locks and signed extremes, alongside the existing 129,030 octave range vectors.

## 0.11: native FUNC snap with an extended octave list (2026-10-28)

Further hardware feedback rejected the fixed-step FUNC snap in 0.10. TUNE now
uses the stock native increment (`0x400f68d2`) with its borrowed TUNE policy,
then the actual native FUNC-entry/repetition gate (`0x400f6ad4`, `0x400f6b02`).
The original view already owns periodic decay of that state. Entering FUNC
arms the gate just as the native SourcePageView does; every accepted snap
re-arms it, including a snap at a range endpoint. We use the native shared
FUNC-transition flag at the same point in the sequence as the original editor.
There is no custom travel threshold or repeated-octave loop for FUNC+TUNE.

The stock ordered-list lookup (`0x4007b97e`) chooses the next/previous value
from a private immutable list: -60, -48, -36, -24, -12, 0, +12, +24. Its input
uses the same floor-to-semitone Q8 conversion as native TUNE. A large accepted
increment selects one next/previous octave, as stock does; native cadence and
gate state decide when a further snap is accepted. The native five-value list
and shared parameter caches are not changed. The scalar cached alias `0x13`
still prevents a second stock -24..+24 quantizer during Sound/lock editing.

Ordinary/fine TUNE, sample selection and the 16-step discrete controls retain
their existing policies. This is an SMZ-only change, using existing guarded
events and native routines; no new core API/hook or stored-format change.

Host tests cover native-call ordering, entry/repetition gating, limits, locks
and all 129,030 octave vectors. `tests/test_st_sampler_native_snap.py` also
executes the linked target encoder and real stock increment, gate, periodic
decay and lookup in Unicorn against a separate reference state. Its UI/model,
clock, runtime policy object and edit service are explicit fixtures; this is
not a complete boot or a hardware validation. Physical knob feel remains to
be confirmed with the new bank.

## 0.12: allocate SLICE locks to placed trigs (2026-10-28)

On either SYN page of **SP SLICE**, press **YES** to open **ALLOCATE SLICE**.
Use **UP/DOWN** to select **CREATE LINEAR LOCKS** or **CREATE RANDOM LOCKS**,
then press **YES** to apply and **close the menu**. **NO** closes without
changing locks. YES reopens it, remembering the previous choice, so another
YES can generate a fresh random allocation. This close-after-apply behavior
is the user's explicit preference, overriding the supplied Digitakt excerpt.

Only placed steps in the active track's current length (up to 64) receive a
native **SLCE** p-lock. Both sound trigs and native trigless locks count as
placed steps. LINEAR starts at slice 1 on the first placed step, increments
over placed steps only, and wraps to 1 after GRID. RANDOM assigns each step
a fresh value in **1..GRID** (4/8/16/32/64); it never assigns NOTE (0).
The current base GRID value supplies the range. All other p-lock parameters,
empty steps, trig data, tracks and patterns are preserved. Existing SLCE
locks on the affected steps are replaced. The base Sound SLCE is not edited.

The menu declines machine selectors, held-trig/selected-step edit contexts,
foreign views and other machines. A track, pattern, page or machine change
dismisses it. Its YES/NO/UP/DOWN edges are consumed in the existing pre-KEY
subscription; encoders are consumed while the menu is open. A sample-browser
YES accepts the browser first and does not also open allocation. Other keys
close the menu and retain their normal native behavior.

Native per-step view/proxy get/set/clear functions operate on the original
pattern matrix; the reusable borrowed services are declared in SDK
`syntakt/pattern.h`. Every write is read back. On refusal, the affected SLCE
locks are restored to their old values or their previous absence, and a
native notification reports the failure. No custom lock persistence, undo
buffer, sequencer, firmware hook or additional module is introduced. Normal
native lock saving/copying/clearing applies; no separate allocation-undo action
is claimed. The new header adds no resident API or core rebuild requirement.

Host tests cover 5,120 linear length/grid/page/track combinations, random
range/repeated generation, existing/absent locks, write refusal and rollback,
empty patterns, key edges, context guards and menu drawing. A separate
ColdFire fixture runs the linked production menu, installed core metadata
adapters and the actual native trig predicates and lock get/set/clear paths
against synthetic native matrices. App/model ownership getters and event
queues are fixtures; this is not a complete boot or physical validation.
Source contracts/layouts and AUDIO remain unchanged; the full bank retains
SP, FM TONE, Recorder, LFO3, POLY and System info.
