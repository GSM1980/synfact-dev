# Digitone ARP → Syntakt: experimental source port

## Pattern locks (2026-11-01)

ARP now uses the shared pattern-lock service hosted by `lfo3-st`; both modules
must be included in the bank, with LFO3 before ARP. Neither engine needs the
other engine enabled. This bank requires the expanded service; do not mix its
ARP object with an older LFO3 object.

In the ARP view, enable **REC**, hold a sequencer trig and turn encoders A..D
to lock MODE, SPEED, RANGE and note LEN. OFS, the arp sequence length (F) and
YES are not locked: with trigs held they do nothing. Multiple held trigs
receive relative edits from their respective values. The edit context is the
native lock editor's: the app's 64-step selection of held trigs within the
track length (400c34d6, the test of 40038e42 and the lock preview), including
later sequencer pages; an edit marks the selection as the native editor does
(400c2d34), so that releasing the trigs does not toggle them. The view shows no
step label: p-locked values are inverted on their encoder bars. Releasing the trigs restores the
Sound's base values in the display; turning knobs without held trigs edits
those base values. Empty pattern steps acquire a native lock trig; writes
outside the track length are refused. Native step/track/pattern clear
operations also clear these locks.

Until 2026-11-02 the view also required live REC (400c2fb6, `+16Ah`, set by
REC+PLAY) and the edit mark (`+160h`) before the first edit, so REC alone never
entered lock editing on the instrument. The next bank entered it but wrote
nothing: it took the pattern model from the parameter view's `+68h`, which is
not the application, so LFO3's store lookup failed. The pattern and track
models now come from the application (4016e804), as the native editor's do;
the view fixture's model doubles reject any other argument.

An encoder push (A..D) with trigs held clears that lock on every held step.
The arp started by a trig keeps that trig's ARP locks for as long as it
plays (the root is consumed before the native effects, so the track's
published locks never carry them); a trigless lock trig passing a running
arp adds its ARP locks to it without restarting or stopping it. A new rate
keeps the phase of the last emitted beat.

A sequence root's outputs are native kind 1 (sequencer), live input's kind 2:
the native stale test (400a4236) drops an event whose kind is below the
track's last started note (45510fc8), so kind 2 outputs hid every following
trig and its locks until the arp ended. The runtime remembers its queued
outputs (pointer and note) so the root adapter neither recaptures them nor
plays one a newer root has superseded.

Slots 0x60..0x63 hold the four locks; 0x64..0x67 stay reserved in the shared
service and are not read. They are distinct from LFO3's 0x58..0x5f. The
existing shared maximum of 79 created lanes still applies; the native stored
block has 80 lanes. The compact private bank occupies 11,072 bytes (16,384
after allocator rounding), including both families' index.

### The arp steps on the trig keys

Outside REC, with the ARP view open, the sixteen trig keys are the arp's
sixteen steps on every track: steps up to LEN are lit, active ones in one
colour (palette 9) and muted ones in red (palette 3, the stock note-trig
colour); keys past LEN are dark. A press selects the step (as LEFT/RIGHT do);
turning any of A..F while steps are held edits each held step's note (OFS);
releasing a key without an encoder edit toggles that step. With REC (grid or
live), or with FUNC, key 2 or key 12h held, the keys and their LEDs stay the
sequencer's, and a release goes where its press went.

The LEDs use the stock LED frame (400094f4): the view stack (40100856) offers
the key LEDs from the top down to each view that is a `LedHandler`, found by
`dynamic_cast`, and the first setter of an LED wins (40008220). The view is
therefore a View followed by a LedHandler at `+50h`, described by its own
`__vmi_class_type_info` as TrackSelect's (401d42ec) describes its own. Palette
9 for the active steps is a guess from the stock track keys that needs the
instrument: change `ARP_LED_ACTIVE` in `view.c` if it is not green.

Checks: `tests/test_st_arp_locks_emu.py` executes compiled storage, LFO3/ARP
isolation, clone/codec, capacity/reuse, event capture and ARP LEN acceptance.
The view fixture executes the native selection test and 64-bit step membership,
checks held-trig edits with explicit pattern-model doubles, runs the firmware's
`__dynamic_cast` on the view, and checks the step keys and their LEDs with the
LED setter as a double.
No physical instrument or full scheduler test is implied by these fixtures.

This is an **experimental SMZ module**, OFF at startup, for twelve independent
audio tracks (eight digital, four analog), fed by sequence roots or held keyboard notes. Double tap on the TRIG parameter button opens a native
IView with the Sound settings; a new Sound keeps MODE OFF. The selector,
native event generator, clock and view are connected. This is an experimental hardware-test candidate: native sequencer chord lanes
remain incomplete. Sound persistence uses the shared lifecycle
adapter and needs a physical save/reload/reboot check. The ARP is monophonic and works without POLY;
it never reserves other tracks. Each audio track has one ARP.

## Controls and view

Two distinct TRIG presses (key 0x13), on the same selected track, within 525 MAIN
blocks / 350 ms. First press/release retain native behavior. The second pair
opens the page. FUNC, repeats and modal views do not open it. Failed opening
on an unsupported track preserves the native action and release. The adapter
qualifies MainScreenView before borrowing its selected-track model.

TEST02 corrects TEST01's key binding: the original MainScreen key switch
`40033ee0` routes key 19/0x13 to parameter page 0 (TRIG), and key 20/0x14 to
page 1 (SYN). TEST01 wrongly used key 18/0x12, so physical TRIG never armed the
gesture. The new view regression executes that native switch/getters/flags,
then the real global core KEY dispatcher to the ARP handler. It rejects TEST01
as a negative control and opens on TEST02. Parameter-page selection and the
manager's view dispatch remain explicit test boundaries. The core firmware is
unchanged; updating the SMZ bank and rebooting is sufficient.

Current eligibility is tracks 1..12 outside MIDI/POLY groups. A..F edit
MODE/SPEED/RANGE/LEN/OFS/sequence length. Left/right, or a trig key outside REC,
select a step, up/down edit its offset, YES toggles it, NO closes without disabling, STOP passes to native
transport. The page stays bound to its opening track. Opening another track
selects that track's settings while the others keep playing. MODE OFF stops only
the displayed track's generator.
Encoder D/LEN changes following output gates without dropping a held root.
Its temporary value label uses the original length spelling and fractions.
Settings follow the Sound through native copies, Kit/project and pool codecs,
Kit save/reload buffers and +Drive Sound records. Missing metadata restores OFF.

`ui.c` reproduces Digitone 1.44 `40051390..40051928`: labels, positions, step
selection, rests, offsets, adaptive scale, length marker and rail. `view.c`
connects the native stack lifetime using the same plain IView ownership as
Recorder; complete-view drawing keeps later system overlays above it. It uses
the original small-label framed-text renderer, whose alignment differs from
plain text. Native fonts/maps/pixels and rail image/mask are identical to the
Digitone resources, verified against SHA-pinned local MAIN inputs. No donor
firmware bytes are shipped. The real font height is five pixels: title y=59.
The earlier laboratory height double had placed it two pixels too low.

The canvas consumes strings synchronously. `length_bitmap` is adapter-owned;
0xffffffff is the rail resource ID, not a native pointer. Native framebuffer
equality and physical UI operation remain unverified.

## Selector, native generation and clock

`selector.c` is authored integer C matching Digitone `40097e46..40098130`.
The caller owns held notes, insertion order and traversal state (180 bytes per
instance). `arp_next` returns a raw note, rest (-1) or no chord/stale generation
(-2). Settings occupy 24 bytes: MODE 0..4, SPEED 0..17, RANGE 0..3, note length
0..127, sequence length 0..15, 16-bit enabled mask and sixteen signed offsets.
Native transpose/scale policy remains separate.

Core 0.12.20 introduced `ST_NATIVE_NOTE_ACCEPT=20`, after mute/stale filtering
and Sound resolution, before effects. Its borrowed arguments are event, Sound,
effective musical time and frame end. Consumption still reaches the original
native cleanup, which frees the intercepted event exactly once. Core 0.12.21 /
host API 9 appends `native_dispatch` to query the existing OWNERS operation
if POLY is present; no provider means independent tracks. The old host prefix and 36-word profiler ABI remain intact.
No private code patches or additional dispatcher exist in ARP.

`runtime.c` retains one native template with balanced lock references and copies
one output when due. Outputs use native live-note kind 2 and strip retrigger
state. The first output can append to the original bucket. Only ordinary full
pitch/gate sequence/live trigs are supported; temporary pooled Sounds, other flags
and initial pool exhaustion fall through to native behavior. A superseded
infinite template is released before processing its replacement.

TEST03 also accepts the full factory flags (10781h), including bit 7 for the
native FX/velocity handling. The old mask incorrectly excluded bit 7. Partial
and legato predicates remain constrained; output flags retain source behavior
apart from the existing retrigger removal.

The queue adapter checks for a free bucket or matching timestamp before calling
the native allocator (which otherwise spins on exhaustion). Later allocation or
queue failures roll back references and recover at the next division without a
burst. IRQ uses bounded native pools; it does not allocate from the general heap,
wait, perform IO or draw. The module currently supplies a root plus octave/step
offsets for sequence roots. Held keyboard chords are connected independently;
native sequencer chord lanes are not wired.
Output gates use `length.c` arithmetic matching the original Digitone producer,
clipped to a finite parent's remaining duration. A native-zero infinite parent
keeps the sequence active; each output has its own LEN gate. Zero/-1 parent
sentinels are accepted. Stopped/held-step previews keep their native route.
Changing LEN/steps/range retains the root; mode/speed/track/context changes
retain the current stop/rearm policy. Complete Digitone input/scheduler lifecycle
equivalence is not established by the gate differential alone.

## Standalone monophonic keyboard input

Holding a chord feeds the selector; notes play successively on the configured
track. POLY is neither required nor modified. Eligibility still refuses its
groups so another voice allocator cannot expand or retarget these notes.

OS 1.41 `40119684` maintains a 128-note bitmap and insertion list at
`4553f6e0 + track*2e18h` before applying native mono note priority. `keyboard.c`
reads that state under native exclusion, validates list pointers and bounds
traversal to 128 nodes. Unchanged bitmaps take a short path; updates preserve
TRUE's insertion order and the clock phase. This observes releases that produce
no audible event. It never writes the native held-note set.

The existing IMMEDIATE event supplies the first live template. Consumed pooled
input is freed once. Outputs use timed ENQUEUE and cannot become keyboard input
again. The normal native internal clock also runs with transport stopped.
Releasing the last key, STOP, OFF or pattern/kit replacement releases gate and
template. While keys are held, sequence roots on that track yield to the chord.
LEN edits retain it. Template allocation failure keeps native input; output
pressure drops that division and recovers without a burst. No latch is added.
Enabling while keys are already held is not separately validated; a new native
key event starts the template.

`test_st_arp_keyboard_emu.py` executes original held-key/priority instructions,
native pools/queue/cleanup, the real core dispatcher and the linked module.
Only ARP is subscribed, without POLY or an owner-map double. Its 16 cases cover
order, chord edits, suppressed releases, all eight independent digital tracks,
all 128 note bits, stopped internal clock, LEN edits, mono sequence priority,
OFF/STOP/contexts, changed malformed lists and pool pressure. All pools recover.
Sound lookup/publication, smoothing and lock application are explicit boundaries;
synthesis, physical keyboard/UI, external MIDI and full scheduling are not run.

Current results: `out/core-st-0.12.21-arp-keyboard-checked/` (`keyboard.json`,
`generator.json`, `view.json`, lint and 62 common checks, zero optional skips).
Bank MAIN: 265,176 bytes. Core MAIN/AUDIO are identical to the LEN milestone,
with 24 resident bytes free; bank AUDIO is also identical. No new core hook,
API field or reserve is needed for keys. Isolated callbacks: held keyboard not
due 140 instructions, due 508; sequence OFF 7, not due 89, due 585. These are
fixture instruction counts, not cycles or device CPU percentages.

```powershell
$env:LIBUNICORN_PATH='C:/Desarrollo/syntakt-dev/syntakt-work/analysis/unicorn-2.1.4-oneshot-build/Release'
& C:/Desarrollo/syntakt-dev/syntakt-work/sp-one-shot/.venv/Scripts/python.exe -B tests/test_st_arp_keyboard_emu.py `
  --core out/core-st-0.12.21-arp-keyboard-checked/main.bin `
  --bank out/core-st-0.12.21-arp-keyboard-checked/synfact-machines.smz4 `
  --out out/core-st-0.12.21-arp-keyboard-checked/keyboard.json
```

`clock.c` uses native musical time: 21,600,000 per quarter / 86,400,000 per whole
note. It receives actual frame intervals, never UI ticks or an accumulated BPM
estimate. It handles wrap, an exclusive deadline and stale generations, emitting
at most once per frame. STOP, pattern/kit pointer replacement, header load/reset,
disable and ownership changes stop the template. External-clock/device scheduling
is not yet verified.

## Offline verification

### TEST04: eight independent digital tracks

Each track owns its settings, runtime/selector, phase, generation, live flag,
context and preview Sound snapshot. The page selection never retargets another
track's engine. Only running bits are visited in MAIN; OFF returns immediately.
One output per running track per frame remains the bound. Native pools, API,
firmware, AUDIO and compiler flags/reserves are unchanged. MAIN bank: 270,364 B.

`test_st_arp_multi_emu.py` runs eight simultaneous native-producer chords,
independent disable, different rates/gates, stopping all templates, and eight UI
pages retaining their settings. Native pools balance; original keyboard,
generator, native-producer and view tests plus 62 common checks pass offline.
Physical TEST03 pad/keyboard behavior was confirmed by the user; simultaneous
TEST04 load has not yet been measured on the instrument.

Isolated callback instruction counts, with unchanged held sets:

| Running tracks | No emission due | All emissions due |
|---|---:|---:|
| 0 | 8 | 8 |
| 1 | 173 | 570 |
| 2 | 323 | 1,112 |
| 4 | 623 | 2,195 |
| 8 | 1,223 | 4,363 |

These exclude complete synthesis/device scheduling and are not CPU percentages.
The user's unchanged CPU display supports only a small visible cost in that
single-instance test; it does not measure eight instances.

Persistence is still **not connected**. The agreed ownership is the Sound,
including Sound copy/load, +Drive and kit/project save/reload. The storage audit
and the required integration tests are not in the repository yet.

### TEST03: native UI producer and borrowed preview Sound

Physical TEST02 opened but played ordinary pad/keyboard notes. Its live fixture
had supplied 10601h and a null preview Sound, unlike the native UI producer.
The original factory-default initializer `400b3574` writes flags 0780h. Note-on
`40104c02`, with UI origin 64, produces 10781h and borrows the current preview
Sound through global `444842d0`. Note-off `4010502c` produces flags 0, and native
priority may resend a held note after the release. TEST02 rejected those inputs.

TEST03 accepts the full flags and that known preview source. The first live
template captures the flat 123h-byte Sound into private instance storage, keeping
native +2ch publication semantics. It does not retain mutable UI storage or
substitute a different kit Sound. Native event copy/free own lock +44h only,
not Sound +2ch. Generated outputs are due and consumed in their current MAIN
frame under native exclusion before UI replacement of the template. Unrelated
temporary Sound sources remain unsupported. A zero-flag release updates the
held set without discarding other held keys.

`test_st_arp_live_producer_emu.py` runs factory initialization, real note-on/off
producers, mono priority, core/pools/queue and pitch/gate. It covers eight tracks,
single-note octave arpeggios, TRUE chord order, preview-copy independence,
native zero-flag release/resend, full-flag sequence roots and OFF. It rejects
TEST02 as a negative control. Thirteen cases pass alongside the original sixteen
keyboard cases, view/key and generator regressions, and 62 common checks without
optional skips. MIDI bookkeeping, record-message/position, Sound publication,
smoothing and lock application remain explicit boundaries; no synthesis or
physical input/rendering is claimed by those fixtures.

Historical TEST03 candidate: `out/core-st-0.12.21-arp-native-live-r1/`. Bank MAIN is
265,580 bytes. The core firmware and AUDIO are unchanged, as is bank AUDIO.
Only the bank must be replaced for TEST03, then the instrument rebooted.
TEST03 was subsequently confirmed on hardware. TEST04 added eight digital
instances. TEST05 adds Sound persistence; see the current behavior above.

Use [SYNTAKT-BUILD.md](../../docs/SYNTAKT-BUILD.md) flags/toolchain unchanged.
All outputs go under ignored `out/`; no MIDI/USB/device access.

- `test_st_arp_selector_emu.py`: 360 configurations / 11,520 selector calls
  against untouched Digitone instructions; twelve independent selector states,
  held-note invariants, MODE OFF opening and nine gesture cases. Also 2,772 target
  clock polls across 54 division/tempo configurations. Stack/register checks,
  unchanged instruction audit and negative control.
- `test_st_arp_ui_emu.py`: 192 original/authored drawing streams, corrected
  native font height, negative control. Model/font/bitmap resolution are doubles;
  this test alone is not a pixel comparison.
- `test_st_note_accept_emu.py`: 384 stock/core acceptance cases and four native
  cleanup cases. Condition/dispatcher are doubles; host tests run real dispatch.
- `test_st_arp_generator_emu.py`: linked module, real core dispatch, native
  pools/refcounts/queue, pitch/gate writes and cleanup. Covers finite/infinite
  notes, pool exhaustion, rollback/recovery, replacement, STOP and next native
  notes. Publication, smoothing, lock application and owner-map provider are
  explicit doubles/boundaries; no synthesis/audio assertion. The bank contains
  LFO3/POLY, but this fixture does not initialize their active engines.
- `test_st_arp_length_emu.py`: all 128 gates and displayed labels match the
  SHA-pinned original. Executes its gate producer/store and 21,504 DN/ST
  stock/core countdown comparisons at 30/120/300 BPM. The 128 settings also
  reach native output gates through the linked module, retaining a held root
  across edits. Checks the note-off flag boundary, not synthesis or audio.
- `test_st_arp_view_emu.py`: linked double tap, MODE OFF, A..F limits, steps,
  modal priority, STOP, close/dispose/reopen and failure passthrough. Stack
  ownership, allocation, base view ctor/dtor and text rasterization are doubles;
  the native rail constructor executes. Font metrics/maps/pixels (453 and 599
  glyph bytes) and 27x27 rail image/mask compare byte-for-byte to Digitone.
- Host clock/runtime tests cover tempo changes, wrap, rests, chord order,
  references and failed allocation/queue. They run in common regressions when
  the bank contains ARP. General core/bank regression retains optional reference
  and prior AUDIO comparisons; unavailable inputs are not reported as passes.

LEN milestone outputs (current results are in the standalone keyboard section):
`out/arp-selector-native-clock-final/result.json`,
`out/arp-ui-native-fonts-r1/result.json`, and
`out/core-st-0.12.21-arp-len-checked/` (verification, lint, combined bank,
regressions, `generator.json`, `view.json`, `note-accept.json`).
The laboratory ELF RWX warning describes its emulator section, not a device bank.

Generator-only callback counts in the fixture are 7 instructions OFF, 88 active
not due and 524 due including native copy/queue. These are not cycles, CPU
percentages or active LFO3/POLY composition measurements. Core resident memory is
65,512/65,536 bytes (24 free), with the agreed flags and reserve unchanged.

Example emulator environment and connected checks:

```powershell
$env:LIBUNICORN_PATH='C:/Desarrollo/syntakt-dev/syntakt-work/analysis/unicorn-2.1.4-oneshot-build/Release'
& C:/Desarrollo/syntakt-dev/syntakt-work/sp-one-shot/.venv/Scripts/python.exe -B tests/test_st_arp_generator_emu.py `
  --core out/core-st-0.12.21-arp-len-checked/main.bin `
  --bank out/core-st-0.12.21-arp-len-checked/synfact-machines.smz4 `
  --out out/core-st-0.12.21-arp-len-checked/generator.json
& C:/Desarrollo/syntakt-dev/syntakt-work/sp-one-shot/.venv/Scripts/python.exe -B tests/test_st_arp_view_emu.py `
  --core out/core-st-0.12.21-arp-len-checked/main.bin `
  --bank out/core-st-0.12.21-arp-len-checked/synfact-machines.smz4 `
  --digitone-main C:/Desarrollo/synfact/build/smz-1/work/analysis/digitone-1.44-sections/section_3_MAIN_OS.bin `
  --out out/core-st-0.12.21-arp-len-checked/view.json
```

The selector/UI differential scripts take `--digitone-main`, `--toolchain` and
`--out` (a new output directory). The SHA-pinned donor remains a read-only local
input. No firmware or generated assets belong in Git.

## LEN source-port detail

`length.c` is authored C translated from the observed native instructions and
validated by executing them. The regular table is expressed as six arithmetic
ranges. Labels are formatted without shipping the firmware string table,
including its `1.5` spelling and `1/64` through `1/2` aliases.

The DN 1.44 producer adds one table entry to the displayed setting. At LEN 127
(label `128`) it reads the following positive word, 2,136,268,810. The original
consumer stores it unchanged; the signed native countdown treats it as finite.
The compatibility case retains that observed value, not an infinity sentinel.
A finite parent clips it; STOP/disable release it through the native track
release path. This does not assert parity for other Digitone OS versions.

Current LEN differential: `out/arp-length-r5/result.json`. Run with the Unicorn
environment above and a new output directory:

```powershell
& C:/Desarrollo/syntakt-dev/syntakt-work/sp-one-shot/.venv/Scripts/python.exe -B tests/test_st_arp_length_emu.py `
  --digitone-main C:/Desarrollo/synfact/build/smz-1/work/analysis/digitone-1.44-sections/section_3_MAIN_OS.bin `
  --stock-main out/arp-study/stock-main.bin `
  --core out/core-st-0.12.21-arp-len-checked/main.bin `
  --bank out/core-st-0.12.21-arp-len-checked/synfact-machines.smz4 `
  --toolchain C:/Desarrollo/syntakt-dev/toolchains/sgdk211/bin `
  --out out/arp-length-NEW
```


## TEST05 hardware candidate

`out/core-st-0.12.22-arp-sound-final/` contains the verified core and bank.
Install both, then reboot. The existing TEST04 firmware lacks host API 10 and
cannot initialize the persistence module. Artifacts for the user are packaged
separately under `out/Syntakt_ARP_Digital_TEST05_20261030/`.

`tests/test_st_arp_persist_emu.py` covers the shared lifecycle bus, actual
Sound/Kit/pattern codecs, native project serializer for Kits/pool, cleared-RAM
reload, snapshots, copy/free/defaults, nondefault LFO3 coexistence, short export
canaries, +Drive wire records and a held chord reset by loading OFF.
The displayed Sound settings stay separate from another Sound selected by a
sequence Sound lock. All eight voices and existing timing paths remain active.
The final physical save/reload/reboot and +Drive IO checks remain for hardware.


## TEST06: analog tracks

`out/core-st-0.12.22-arp-analog-r1/` extends the same generator to all twelve
audio tracks. The core SYX is byte-identical to TEST05; only the SMZ changes.
Four runtime states are added. The Sound metadata format already stores all
12 Kit Sounds and needs no change. FX and MIDI tracks remain excluded, as do
POLY masters/slaves. The parent workspace and POLY sources were not edited.

The original native consumer resolves the Sound, applies transpose/clamp,
writes the shared pitch/gate arrays and sets the existing trigger masks. Its
native track-to-voice map maps tracks 9..12 to analog voices 0..3. ARP emits
ordinary native notes through that path; it adds no analog hardware sender.

`test_st_arp_analog_emu.py` executes native UI producers and effects for all
four analog tracks, repeated notes, short-gate expiry, retrigger during a long
gate, transpose/clamping, sequence roots and OFF passthrough. It also checks
12 simultaneous chords, balanced pools/STOP, owner/MIDI/FX refusal and T9..T12
pages. Its CPU differential compares the frame callback with TEST05; note-event
admission is measured separately. No SPI/DAC, machine control translation,
physical pitch/audio or physical CPU percentage is asserted.

The bank MAIN image grows by 2,228 bytes, to 441,780. The existing per-block
counts for 0/1/4/8 active live ARPs are unchanged in the fixture; 12 active ARPs
cost 1,823 instructions on a non-note block and 6,531 on a due-note block.
These are instruction counts, not cycles. Files for hardware testing are under
`out/Syntakt_ARP_AllTracks_TEST06_20261030/`.
