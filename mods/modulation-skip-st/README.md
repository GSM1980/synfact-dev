# Skip native modulation per track

The MAIN audio IRQ calls stock modulation `40097afc` for every track on every
block. Its full body takes 6,614 instructions in the ColdFire emulator and
previously measured about 5.7 MAIN CPU points on the device. This module keeps
the output and input dependencies of each track separately. It runs the stock
body when necessary, replays unchanged tracks, or executes a guarded copy of
the native kernel for only the tracks whose inputs changed.

## Decision and exact output

Tracks 0–11 have one velocity group and five further modulation source groups;
track 12 (FX) has only the latter five. Each group contains four `{amount,
destination index}` longwords. For each track, the decision compares only the
source values of groups with a nonzero amount and the initial words of their
unique destinations. The source tables are at `80004670..88`,
`800046b8..ec` and `800046f8..80004760`; matrix entries are at
`working + 0x754 + 0x7c·track`. Destination word `index` is at
`working + 0x1c + 0x8e·track + 2·index`.

The native kernel saturates a zero-amount destination even though it adds no
modulation: `0000..7fff` stays unchanged, `8000..bfff` becomes `7fff`, and
`c000..ffff` becomes zero. The module applies this exact projection to
zero-only destinations. An ordinary FLTR FREQ edit with a zero amount therefore
does not force the matrix to run. A destination that also has an active entry
is always handled by the native kernel or its cached output.

The `400a30c4` Sound writer is hooked to notice matrix changes and record the
published track. That track's 24 longwords are scanned on a write; all 312 are
scanned on the first block, every 64 blocks, or if a writer did not leave a
valid track hint. Only changed track lists are rebuilt. An unobserved matrix
writer disables the optimization. A full native run periodically checks cached
active outputs and the zero-depth projection; any mismatch turns the module OFF
for the session.
The stock RAM reader stays in a separate function so GCC cannot combine its
absolute address with a relocatable cache pointer. The bank builder checks
that every runtime relocation targets its own MAIN image, as the core parser
requires.

All destinations must lie in their own 71-word track row. An out-of-row signed
index may alias another track, so the entire stock routine runs for that
matrix. Dense matrices with more than 100 distinct active and zero-only
destinations also use stock. Twelve or more dirty tracks use stock for that block.
These limits keep the decision cheaper in the sparse cases this module targets.

The guarded kernel is based on the pinned OS 1.41 kernel already used by POLY.
Its guards advance both source cursors and the track cursors when they skip a
track. The second-loop helper is initialized even when all velocity tracks are
skipped, so FX can run alone. Analog POLY selects its own modulation kernel and
does not enter this module; digital POLY uses the native path.

For a dirty track with one active source group among the first five, a second
path calls the stock four-entry helper for that group alone. It is used only
when zero-amount entries in the other groups do not target its active
destinations; the guarded kernel handles all other matrices. Zero-only
destinations still receive the stock saturation projection.

## Modes and display

`40097b04` becomes a jump to the module only when its six stock bytes and the
Sound writer's eight stock bytes match. A refused installation leaves stock
untouched. ON is the normal build. VERIFY always runs stock and checks cached
results. The header `MSKIP ON  H99 S98 E000` shows the mode, percentage of
blocks with no dirty tracks, percentage avoiding the full stock body (including
partial runs), and error count. ON only shows the header after an error or
refusal; VERIFY always shows it.

## Validation and cost

`tests/test_st_modulation_skip.py` checks the decision and fallback logic on a
native host. `tests/test_st_modulation_skip_emu.py` compares the linked bank
against the stock ColdFire kernel after conversion, modulation and LFO calls.
It tests sparse edits, zero-depth FREQ/RES including out-of-range values,
dense fallback, unobserved writers, and 17 kernel masks covering each track
alone, all tracks, no tracks and alternating tracks.

Example steady-block instruction counts from a controlled emulator fixture:

| Active tracks recalculated | Instructions |
|---|---:|
| Full stock body | 6,614 |
| None; all amounts zero | about 515 |
| One track, one active source group | 1,524 |
| Three tracks | about 3,440 |

With a continuous Sound publication on the edited track, the controlled FREQ
fixture with one active amount uses 9,134 instructions in 1.2.1 and 2,497 in
1.2.3. The corresponding zero-amount case drops from 7,722 to 1,453. The
change is chiefly the 312-entry scan becoming a 24-entry scan. The emulated
real writer marks track 2 and the next block reads exactly 24 matrix words.

These include the decision, guarded kernel and result capture in the module
cases. They are instruction counts, not device CPU percentages or timing
guarantees. The first block builds the track lists and is more expensive;
subsequent blocks use the cached lists. On 2026-11-09 the user reported that
1.2.3 greatly improved FLTR FREQ responsiveness in AVE2 during PLAY. This is
a physical usability observation, not a measured CPU-cycle comparison.
