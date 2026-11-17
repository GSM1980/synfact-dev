# Playhead: step order per track (playhead-st)

Desktop OS 1.42 release: **1.0.1**, based on remote main `f48f3ab`.
Use the matching core 2.0 and bank (SMZ4 contract `0x53594605`).

**TRK + UP** / **TRK + DOWN**: the active track goes to the next / the
previous mode of the cycle

| mode | a 8-step track plays | in phase with the pattern |
|---|---|---|
| `FORWARD` | 1 2 3 … 8, stock | yes |
| `REVERSE` | 8 7 6 … 1 | yes: the last step on the pattern's first |
| `PENDULUM` | 1 2 … 8 7 … 2, again from 1; no end twice | no: its cycle is 2 × length − 2 |
| `RANDOM` | any step, chosen at each step | no |
| `WALK` | one step to the left or to the right at random, around the ends | no |

FORWARD is at most two presses away from any mode. TRK + LEFT / RIGHT are
not the module's: they stay native. The other tracks keep their modes. A
popup over the parameter page says `TRK n REVERSE`, `TRK n PENDULUM` …
(`FX TRK …` for the FX track).

Modes belong to the pattern: each pattern has its own thirteen, and changing
patterns changes them with it. They are saved with the pattern's kit and
come back when the project or the pattern is loaded, copied with it and
reset to FORWARD when the kit is initialised ("Saving" below). This needs
LFO3 in the bank (it is in every normal bank); without it the modes last
until a restart.

While the active track is not FORWARD, its mode's icon is in the header,
two pixels left of the metronome: a thin arrow to the left (REVERSE), a thin
arrow both ways (PENDULUM), a die showing five (RANDOM), steps left and right
(WALK). All are one-pixel strokes, centred on the tempo's digits, dark on the
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

## What it costs

Nothing while every track is FORWARD: the sequencer's code is the stock code.
The five sites go in with the first track in another mode and come out with
the last one. While they are in, the MAIN sequencer interrupt does, for each
FORWARD track, about 17 more instructions once per step (the trig) and about
30 once per tick (the UI's copy of the step; 24 ticks a quarter note). A
track in another mode adds a call to `playhead_step` (C, a few dozen
instructions) per step and two per tick. At 120 BPM with every track in a
mode that is under 100 000 instructions a second, well under a tenth of a
percent of MAIN, and nothing in the audio paths. The LEDs' accessor adds a
few instructions per track each time the UI reads it, in the UI task. The key
handler runs only on key events and returns at its first compares for every
key but UP and DOWN. The header icon is drawn on the contextual DRAW after the
MainScreen view, each time it is drawn, in the UI task: with every track FORWARD the handler returns at its first
test, and otherwise it draws at most 26 pixels.

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
popup says `MODES N/A`. The initializer patches nothing, so the core's boot
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
3 RANDOM, 4 WALK), three zero bytes and an FNV-1a hash of those 20 bytes. A
kit with every track FORWARD has no record (24 zero bytes). The stock
firmware never reads the tail, so a project saved here opens on a stock unit,
and a project without the record (or with a damaged one) opens with every
track FORWARD.

`persist.c` hooks nothing. It observes the Sound metadata notifications
that LFO3's object/codec adapter publishes on the core's native bus (host
API 10, `<syntakt/sound-meta.h>`): KIT_ENCODE writes a known slot's record
from the kit's modes, KIT_DECODE gives the kit the slot's modes (FORWARD for
an unknown buffer, a SysEx import), COPY and RELOCATE of a whole kit carry
its modes, NEW initialises them, FREE forgets buffers, PROJECT_NEW clears the
records of the container's 128 slots. A known slot is one by construction, as
LFO3's: the two project containers and the event's own container
(+2e0200h + k × 1600h, k < 128) and the save and reload buffers; nothing is
written past the native record of any other buffer. Kits outside the live
array (undo, clipboard, decode buffers) keep their modes in a 16-entry table
keyed by address and checked against a hash of the kit's bytes. Copies of an
encoded slot (1600h) carry the record with the slot's bytes.

A change of mode must also reach what survives a power cycle without a
project save: the kit's mirror slots. A native edit of a kit ends in
`KitActive::kitChanged` (`40172250`, its vtable +5Ch), which re-encodes the
kit into its mirrors (the job "KitActive::updateSingleMirror", `4001082e`).
After each TRK + UP / DOWN the module calls it for the playing kit, so the
re-encode reaches KIT_ENCODE and the record. The KitActive object is in the
project (`40a38b18` + 378d8h, as `40018ece` reaches it) and is used only
while its vtable (`401c33f4`), its kits (+10h, `41ad5134`) and that vtable
entry are the stock ones; otherwise the change waits for a project save.

The live modes are a table of 128 kits × 13 tracks. The sequencer's pattern
pointers name their kits (`43bd7430` playing with `43bd743c`, `43bd7438`
next with `43bd7444`), so the step sites read the modes of the pattern each
step belongs to. The step sites are in while any kit has a mode, so a loaded
project that uses modes in any pattern installs them.

## Building

Any bank, as a module. Put it before `arp-st` (the ARP's own view takes the
arrows when it is open) and before `modulation-skip-st`, which stays last:

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
