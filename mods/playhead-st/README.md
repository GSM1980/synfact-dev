# Playhead: step order per track (playhead-st)

Desktop OS 1.42 release: **1.3.1**, core **2.9 / host API 20**.
The builder includes the internal `state-st` service automatically. Modes
persist through native Kit mirrors without loading the third LFO or saving
the project; the core resolves the actual application-owned KitActive and
recognizes both full arrays and individual Kit slots.

**TRK + UP** / **TRK + DOWN** opens a centered mode selector for the active
track. All nine modes are visible in two columns, each with its icon and name.
The highlight starts on the current mode. Release the opening arrow, then
use **UP / DOWN** (including repeats) to move through the options, wrapping at
the ends. **YES** applies the highlighted mode to that track and closes the
selector. **NO** closes it without changing anything. Browsing does not change
playback or save a kit edit; confirming the current mode also avoids an edit.

The left column contains FORWARD through WALK, the right ANCHOR through
WALK+. **LEFT / RIGHT** (1.3) move to the other column on the same row; RIGHT
from WALK, which has no neighbour, goes to WALK+. The header names the track
(`TRK 1`..`TRK 12`, or `FX TRK`).

**TRK + TRIG** (1.3) changes the active track as stock does while the selector
is open: the selector stays, now for that track, with the highlight on its
mode and nothing applied. Changing the playing kit (pattern) or the opaque
native view dismisses the selector without applying it. Apart from TRK and
TRK + TRIG, keys and encoders are captured while it is open, so the
underlying page cannot be edited (a trig key without TRK does nothing).
Releases of consumed presses are also consumed after closing; a TRK press
begun before opening keeps its native release.

| mode | a 8-step track plays | in phase with the pattern |
|---|---|---|
| `FORWARD` | 1 2 3 … 8, stock | yes |
| `REVERSE` | 8 7 6 … 1 | yes: the last step on the pattern's first |
| `PENDULUM` | 1 2 … 8 7 … 2, again from 1; no end twice | no: its cycle is 2 × length − 2 |
| `RANDOM` | any step, chosen at each step | no |
| `WALK` | one step to the left or to the right at random, around the ends | no |
| `ANCHOR` | 1 2 1 3 1 4 1 5 1 6 1 7 1 8, repeat | no: its cycle is 2 × (length − 1) |
| `STUTTER` | 1 2 3 4 4 5 6 7 8 8, repeat | no: every fourth visited step plays twice |
| `FOLD` | 1 8 2 7 3 6 4 5 4 6 3 7 2 8, repeat | no: its cycle is 2 × length − 2 |
| `WALK+` | 60% one step forwards, 20% stay, 20% one step backwards; wrap at the ends | no |

TRK + LEFT / RIGHT stay native while the selector is closed. The other tracks
keep their modes. A refused native patch leaves the old mode and keeps the
selector open with `MODES N/A`; NO still closes it.

Modes belong to the pattern: each pattern has its own thirteen, and changing
patterns changes them with it. They are saved with the pattern's kit and
come back when the project or the pattern is loaded, copied with it and
reset to FORWARD when the kit is initialised ("Saving" below). The builder
includes the internal `state-st` service automatically; LFO3 is optional.

While the active track is not FORWARD, its mode's icon is in the header,
two pixels left of the metronome: a thin arrow to the left (REVERSE), a thin
arrow both ways (PENDULUM), a die showing five (RANDOM), steps left and right
(WALK), an anchor (ANCHOR), repeated bars and a play tip (STUTTER), a fold
from the edges to the centre and back (FOLD), and steps with a right arrow
(WALK+). All are one-pixel strokes, centred on the tempo's digits, dark on the
lit header, with no box. Popups and modals cover it. FORWARD shows
nothing, so the header is stock.

The trigs play as they are written, with their locks, conditions and micro
timing; only the order of the steps changes. The trig LEDs' playhead, the
screen and live recording read the step the track plays. Both length modes
work: the pattern's length, or each track's own (scale per track).

REVERSE plays the mirror of the stock count: at the pattern's step k a track
of length L plays its step L + 1 − k. It starts on its last step whenever the
pattern starts (play, a pattern change, a restart), and switching a track to
REVERSE while it plays makes it jump to that step (on step 5 of 16 going
forwards, the next step is 11).

PENDULUM, RANDOM and WALK choose their next step once per step. They start
from the step the stock count is on when they are chosen or when the pattern
starts again (PENDULUM going right; RANDOM at a random step). A count that
does not follow the one before (a start, a resync, a pattern change) starts
them again from it. RANDOM and WALK draw from a xorshift generator mixed with
the native clock at each start.

ANCHOR always starts at step 1, then alternates it with steps 2 through the
track's length. FOLD starts at step 1, alternates the remaining outermost
steps until the centre, then retraces that exact path; neither turnaround
endpoint is played twice. Both restart that traversal on a resync or a mode
change, including when selected mid-pattern. A one-step track stays on 1.

STUTTER starts at the current stock count and repeats every fourth visited
step once before advancing: two plays total. The interval and repeat count
are fixed. Repetitions occupy normal sequencer steps, retaining the native
tempo and microtiming; they are not subdivisions within a step. Its phase
continues across the native loop, so a seven-step track plays
`1 2 3 4 4 5 6 7 1 1 2 …`. WALK+ uses the same nearest-neighbour wrap as
WALK, adding a forward bias and the possibility of staying. Its average
movement is +0.4 steps per tick; individual random runs can still move
backwards or stay for several ticks. It does not impose a forced advance.

Every stateful mode resets its phase and cached steps if the track length
changes. Repeated reads of one native count keep the same chosen step;
two-step tracks can advance when the previous count is also the next count.

## What it costs

Nothing while every track is FORWARD: the sequencer's code is the stock code.
The five sites go in with the first track in another mode and come out with
the last one. All work stays on MAIN; nothing runs in an audio render path.

The register-ABI helpers in `seq-direction.s` handle cached steps, FORWARD
and REVERSE directly, with no C call or kit division. The UI copy validates
the metadata once for both indices on ticks without an advance. A new count
calls `playhead_advance` with the already-resolved state, avoiding a second
kit lookup. The canonical `playhead_step` handles cold state, kit/mode/length
changes and resyncs. Sixteen-byte track state needs a shift rather than a
stride multiply. Wrapped neighbours, the next native count and the stutter
phase use comparisons instead of division. WALK+ uses exact integer PRNG
thresholds matching the original 60/20/20 buckets, without multiplication.
No EMAC instruction is needed or used; its complete context is preserved.

Measured by analog-emu's `cfbench` interpreter, comparing the initial C
implementation of these nine modes with the optimized bank, 16-step track:

| path | reference instructions | optimized instructions |
|---|---:|---:|
| UI copy, cached tick, ANCHOR / STUTTER / FOLD / WALK+ | about 186 | about 57 |
| UI copy, FORWARD after initialization | 112 | 32 |
| UI copy, REVERSE after initialization | 144 | 53 |
| Cached step helper, stateful mode | 81 | 44 |
| ANCHOR advance helper, average | 133 | 108 |
| STUTTER advance helper, average | 142 | 120 |
| FOLD advance helper, average | 150 | 127 |
| WALK+ advance helper, average | 153 | 123 |

Probe overhead is subtracted. Cold starts cost more (for example ANCHOR's
step helper: 119 to 140 instructions), while initializing the reusable cache.
The comparison covers all modes, lengths 2/16/64, pattern/track scales and
the FX track, with identical outputs and preserved C ABI/EMAC context.
These are instruction counts, not hardware cycles or a measured device CPU
percentage. The benchmark command is in [docs/TESTS.md](../../docs/TESTS.md).

The LEDs' accessor only reads the cached step in the UI task. The key
handler runs only on key events. The header icon is drawn on the contextual DRAW after the
MainScreen view, each time it is drawn, in the UI task: with every track FORWARD the handler returns at its first
test, and otherwise it draws at most 26 pixels.

The selector never runs in the sequencer or audio paths. Its closed DRAW/ENC
callbacks return at the first state test. It rasterizes its 128x64 masked
bitmap initially, when its track title changes or on an error. Cursor moves
only invert the old and new eight-pixel rows; reopening the same track can
reuse the cached bitmap. It uses one native
masked blit per UI frame. There is no heap allocation or periodic job. Its
two 1 KiB pixel/mask arrays live in the SMZ bank's MAIN allocation. Native
Bitmap construction, clipping and masked blit are checked against OS 1.42
in Unicorn; unchanged draws make zero writes to the cached pixels.

UI interactions request composition through `40100966(root + 0x40)`.
`4016e044()` returns the root owner; the native `ViewController` is embedded
at +0x40 and its dirty byte is +0x20 from there. Writing `root[0x20]` as 1.2
did leaves the real compose gate unchanged, so a moved cursor or closed
selector can wait for an unrelated redraw. The regression now executes the
native singleton getter and `4010095c` compose gate, preserving a sentinel
in the unrelated root field. On the interpreter, a cursor redraw takes
7,113 instructions versus about 49,853 in 1.2; that includes the native blit,
not the underlying screen's composition or scheduling time. It is not a
physical latency measurement. Applying and persisting the mode keep their
existing synchronous semantics.

## How

The sequencer keeps counting each track's step forwards, exactly as stock:
every place that starts, resyncs or hands over a pattern (several places) sets
that count from the pattern's position. The step a track plays is changed
where the count is used (`seq-direction.s`), to what `playhead_step`
(`seq-direction.c`) makes of it:

| site | what uses the step |
|---|---|
| `4008cf00`, `4008cf14` | the trig preparation at a step's tick 0 (`4008b882`, `4008bcec`) |
| `4008c204` | the trig of a pattern started mid-way |
| `4008d1b8` | each tick's copy of the step and the step before for the UI (`444e237d`, `444e2370`): screen, live recording |
| `4008b746` | the accessor `4008b734(t)`, the step a track plays (`43bd7416 + t`): the trig LEDs' playhead (`400c20ee`) |

Each site is a `jmp abs.l` over whole stock instructions; its stub does what
they did, with the track's step, and jumps back. A FORWARD track's stub never
calls `playhead_step`. The length is the one the stock advance uses (the
track's or the pattern's); a count at or past the length (a length made
shorter while playing) stays as it is.
[docs/MACHINE-REFERENCE.md](../../docs/MACHINE-REFERENCE.md), section 9, has
the sequencer's addresses.

REVERSE is a function of the count. The other modes keep, per track, the last
two counts and the steps chosen for them: the first ask for a new count (the
tick's UI copy right after the stock advance, or the trig at the step's tick
0, both in the sequencer interrupt) chooses its step, and every later ask
(the trig, the LEDs, live recording) gets the same one. The LEDs' accessor
runs in the UI task and only reads. Changing a track's mode clears its state
with interrupts masked.

The sites are written from the UI task with interrupts masked, with the
core's cache sequence. The sequencer runs in an interrupt, so the UI task
never finds it in the middle of a site. Before the first write the module
checks each site's stock words and the CRC of the code around them (the
registers the stubs rely on); if they differ nothing is ever written and the
selector says `MODES N/A`. The initializer patches nothing, so the core's boot
gate for stock-code patchers (`mods/core-st/boot-preimages.c`, which LFO3 and
MODULATION SKIP need because they patch at init) is not involved, and no core
change is needed.

`seq-direction.c`/`.s` are written in the core's shape (their sites, one
table, their own files): a sequencer step-order service another module could
use too, and that could move into the core. A new mode is a case in
`playhead_step` and a name in `main.c`.

The header icon: the MainScreen's header (`40032b0e..40032b72`) draws the
tempo `%d.%d` right-aligned to x 125 in the font `443da1f0` and the metronome
at x 117 − the tempo's width, on rows 55..62. The module draws its icon (7 or
9 columns by 7 rows) on rows 56..62, its right column two pixels left of the
metronome, inverting (XOR) the pixels it covers,
in the MainScreen's own layer: the contextual DRAW (`ST_EV_DRAW | ST_EV_VIEW`)
right after the MainScreen view (`40100830`), only while it is the highest
opaque view. The views drawn after it, popups and modals among them, cover
the icon. The
width is the firmware's: `4013273e(the tempo 402d7010, 120, 1, 1)` gives the
whole BPM and the font's fixed width (or `400f92be`, for a font without one)
the width of `<BPM>.0`. A long page title that reaches that far loses the
columns under the icon.

With TRK held the module takes UP's or DOWN's press, its repeats and its
release (and the release of a press it took, even after TRK is let go). Key
IDs: TRK 2, UP 14, DOWN 15 (`mods/core-st/README.md`).

## Saving

Each pattern's kit is encoded into a 1600h-byte slot of the project (and of
the native save and reload buffers). The native codec uses [0, 14d2h) of the
slot and LFO3 [14e0h, 15d0h); the modes take 24 bytes of the free tail at
15e0h: `PLH1`, the thirteen modes (0 FORWARD, 1 REVERSE, 2 PENDULUM,
3 RANDOM, 4 WALK, 5 ANCHOR, 6 STUTTER, 7 FOLD, 8 WALK+), three zero bytes
and an FNV-1a hash of those 20 bytes. The record layout and original mode IDs
are unchanged; old records load as before. Older Playhead versions reject
records containing the new IDs and fall back to FORWARD. A
kit with every track FORWARD has no record (24 zero bytes). The stock
firmware never reads the tail, so a project saved here opens on a stock unit,
and a project without the record (or with a damaged one) opens with every
track FORWARD.

`persist.c` hooks nothing. It observes the Sound metadata notifications
that the internal state service's object/codec adapter publishes on the core's native bus (host
API 10, `<syntakt/sound-meta.h>`): KIT_ENCODE writes a known slot's record
from the kit's modes, KIT_DECODE gives the kit the slot's modes (FORWARD for
an unknown buffer, a SysEx import), COPY and RELOCATE of a whole kit carry
its modes, NEW initialises them, FREE forgets buffers, PROJECT_NEW clears the
records of the container's 128 slots. A known slot is one by construction, as
the state service's: the fixed project containers, registered native mirrors and the event's own container
(+2e0200h + k × 1600h, k < 128) and the save and reload buffers; nothing is
written past the native record of any other buffer. Kits outside the live
array (undo, clipboard, decode buffers) keep their modes in a 16-entry table
keyed by address and checked against a hash of the kit's bytes. Copies of an
encoded slot (1600h) carry the record with the slot's bytes.

A change of mode must also reach what survives a power cycle without a
project save: the kit's mirror slots. A native edit of a kit ends in
`KitActive::kitChanged` (`40172414`, its vtable +5Ch), which re-encodes the
kit into its mirrors (the job "KitActive::updateSingleMirror", `4001082e`).
After YES confirms a different mode the module calls it for the captured
playing kit, so the
re-encode reaches KIT_ENCODE and the record. Host API 20 resolves KitActive
from the application singleton at `444e23f4` plus `130h`, checking the
application vtable (`401c5708`), KitActive vtable (`401c3694`), live kits
(`41ad6134`) and changed entry. Its registered mirror arrays also authorize
their full project container. Unknown buffers never acquire tail capacity.

The live modes are a table of 128 kits × 13 tracks. The sequencer's pattern
pointers name their kits (`43bd7430` playing with `43bd743c`, `43bd7438`
next with `43bd7444`), so the step sites read the modes of the pattern each
step belongs to. The step sites are in while any kit has a mode, so a loaded
project that uses modes in any pattern installs them.

## Building

Any bank, as a module. Put it before the other optional UI modules so its
modal consumes KEY/ENC before ARP or the recorder can edit a page behind it.
Machine-owned view subscribers can retain their prefix. If selected, keep
LFO3 before POLY, and `modulation-skip-st` last, as in the normal catalog:

```bash
"$PY" -m elekloader.sdk.syntakt.smz mods/samplers-st mods/fm-tone-st \
    --module mods/playhead-st --module mods/recorder-st --module mods/lfo3-st \
    --module mods/arp-st --module mods/poly-st --module mods/system-info-smz-st \
    --module mods/sample-manager-st --module mods/chorus-st --module mods/modulation-skip-st \
    --out out/<new folder>/synfact-machines.smz4
"$PY" -B tests/test_st_playhead_emu.py --bank out/<new folder>/synfact-machines.smz4 --stock "$SYNTAKT_STOCK"
```

It needs no core change and no host API: any bank for the current core
(1.8.1) can carry it.

## Status

- 1.2.1: fixes the actual native redraw request for opening, moving and
  closing the selector. The test reproduces 1.2's unchanged native gate as a
  negative control. Thirty-six incremental highlight transitions match full
  frames, with at most two rows touched per move. Sequencer and persistence
  sources remain unchanged.
- 1.2: centered nine-option selector, icons and text, deferred YES commit and
  NO cancel, captured track/kit, cached native bitmap, and input priority over
  the other optional UI modules. Sequencer sources and object code are
  unchanged from 1.1. UI behavior, all nine icons, clipping, native blit,
  cache reuse, context changes, FX track and refusal are checked in Unicorn.
- 1.1: ANCHOR, STUTTER, FOLD and WALK+, four matching pixel icons, assembly
  cache paths and a paired UI copy. Host test: 194,688,000 selections across
  lengths 1..64, thirteen tracks and both scales. Stock Unicorn differential:
  2,968 original-site cases plus all modes, all three trig sites, repeated
  native tick copies, LEDs, saved records, keys and 24 header cases.
  analog-emu: 3,888 identical outputs against the initial C implementation,
  27 cost cases, callee-save and full EMAC preservation, broken-hook negative
  control. Complete bank built and parsed on OS 1.42/core 2.0.3; native/common
  regressions pass. Full-catalog initialization and 39 LFO activity cases
  pass separately. No physical-device validation or transfer performed.
- Build 2 (reverse, four sites) on the unit: backwards from the last step,
  the sound right, but the trig LEDs' playhead went forwards: they read the
  accessor `4008b734`.
- Build 3 added that accessor as a fifth site.
- Build 4: PENDULUM, RANDOM and WALK, the modes on TRK + UP / DOWN, TRK +
  LEFT / RIGHT left native.
- Build 5: the mode's icon in the header. On the unit it was drawn over the
  mode's popup, lit on a cleared box (so in negative on the lit header), and
  RANDOM had a frame.
- Build 6: the icon in the MainScreen's layer, below popups; its pixels
  inverted, no box; RANDOM as five dots. On the unit: right colours, but the
  arrows too heavy with cut tips, WALK not centred, RANDOM wanted its frame.
- Build 8: the mod renamed playhead-st (it was reverse-st).
- Build 9: modes per pattern, saved with its kit (`persist.c`). Checked in
  Unicorn (`tests/test_st_playhead_emu.py`: encode, decode, copies, NEW,
  PROJECT_NEW, damaged records, per-pattern kits); not yet tried on the unit:
  save and reload a project, change patterns, copy, clear and reload a
  pattern.
- Build 9 on the unit: modes came back after a project save and a power
  cycle, but not after a power cycle without a save: a mode change did not
  re-encode the kit's mirrors.
- Build 10: after each mode change the native `KitActive::kitChanged` re-encodes
  the playing kit's mirrors, as after a native kit edit. Checked in Unicorn
  that it is called for the playing kit (the native job itself is not run
  there); not yet tried on the unit.
- Build 7: one-pixel icons centred on the tempo's digits: thin arrows with
  diagonal heads (PENDULUM 9 columns), the die with rounded corners, WALK as
  2 × 1 steps. Checked in Unicorn
  (`tests/test_st_playhead_emu.py`, the header's position with a fixed-width
  font); not yet tried on the unit. Whether the header's font is fixed width
  is not recorded; the code handles both. That the popup is a view drawn
  after the MainScreen (and so above the icon) is not checked here.
