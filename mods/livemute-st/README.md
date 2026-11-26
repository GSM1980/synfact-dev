# Live mute (livemute-st)

**0.3.1.** 0.2.0 was tried on a unit (OS 1.42, core 2.7.0, an OXI ONE mk2 sequencing over MIDI): muted tracks go quiet, LIVE MUTE OFF restores the stock behaviour. 0.3.0 (the setting kept in the core 2.9 preferences) was tried on a unit too. 0.3.1 fixes the row's LEFT and RIGHT arrows (the OS passes the step as the third argument of the change callback) and was tried on a unit.

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
- **SETTINGS > LIVE MUTE** turns it on and off: YES toggles, LEFT/RIGHT
  set OFF/ON. It is ON until you change it (a bank with the module asks for
  it). With core 2.9 (host API 20) the choice is kept across power-off in
  the core's preferences, key `ST_PREF_LIVE_MUTE` (3); on older cores it
  lasts until power-off. OFF is the stock behaviour. Leaving the module out of the bank is OFF too.
- ARP on a muted track: its live arps are muted too (their notes are live
  notes); sequence arps were already muted by the OS.

Needs host API 10 (core 0.12.22 or later). Bank order: after COMP (after
Chorus without COMP); see [SYNTAKT-BUILD](../../docs/SYNTAKT-BUILD.md).

## How it is built

Two subscriptions: SETTINGS (the LIVE MUTE row, four MenuItem callbacks
through the host's `additem`, as KIT BANK's; select and change redraw the
menu with `400ffe22(view + 0x38)`), and the core's native bus, with interest
in NOTE_ACCEPT only (host `native_interest`); while LIVE MUTE is OFF the
handler returns at once. NOTE_ACCEPT runs in the MAIN IRQ after the OS's
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
