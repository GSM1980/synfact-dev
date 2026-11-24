# Live mute (livemute-st)

**Draft, 0.1.0.** Not yet run on a unit.

On the Syntakt, MUTE stops a track's sequencer trigs but not live notes:
a track played from MIDI in (an external sequencer such as an OXI ONE) or
from the trig keys in KEYBOARD mode keeps sounding while muted. With this
module in the bank, a muted track also ignores live notes.

- **What counts as muted** is exactly what the sequencer itself mutes: the
  track mutes, every track but the soloed ones while a solo is on, and the
  pattern/song mutes.
- **Only note-ons are dropped.** Note-offs still reach the track, so a note
  that started before you muted is released normally and nothing hangs. A
  note already sounding when you mute keeps its tail, as with the sequencer.
- **Sequencer trigs** are untouched: the OS already mutes them.
- **The option** is the module itself: include it in the bank to get this
  behaviour, leave it out for the stock one. It has no settings and keeps no
  state.
- ARP on a muted track: its live arps are muted too (their notes are live
  notes); sequence arps were already muted by the OS.

Needs host API 10 (core 0.12.22 or later). Bank order: after COMP (after
Chorus without COMP); see [SYNTAKT-BUILD](../../docs/SYNTAKT-BUILD.md).

## How it is built

One subscription, the core's native bus, with interest in NOTE_ACCEPT only
(host `native_interest`). NOTE_ACCEPT runs in the MAIN IRQ after the OS's
mute and stale filter (`400a421a`) and before the note's effects; its
arguments are `{event, sound, effective time, frame end}`. The event's
words +4 (action: 1 note-on, 2 release), +8 (track) and +12 (kind: 1
sequencer, 2 live) are the ones ARP reads. For a live note-on on audio track
0..11 that `st_muted_tracks()` marks, the handler supplies a nonzero result
and returns 1: the core skips the note's effects and reaches the same native
event release, so the event is freed once, by the OS.

`mute.h` reads the three words the OS's own filter reads (`400a3998..b8`,
`400a421a..2c`): the track mute `80003048`, the solo mask `8000304c` (when
nonzero, the muted tracks are its complement), and the 16-bit pattern/song
mute `43bdd828` that the pattern change (`40019bfa`) and song rows
(`4002332c`) set through `400a30b2`. It is shaped to move into the core: no
module state, readable from the MAIN IRQ; another module wanting "is this
track muted?" could use it.

## Tests

`python tests/test_st_livemute.py [--cc m68k-elf-gcc]`: the handler against
the OS's mute words in Unicorn, see [TESTS](../../docs/TESTS.md).
