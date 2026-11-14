# Skip native modulation per track

The consolidated release is **1.2.4**, one step from main's 1.2.3. Sections
labelled 1.3 and 1.4 below are development iterations included together in
this release. See [the mapping](../../docs/MAIN-OPTIMIZATION-RELEASE.md).

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
The stock RAM row address crosses an opaque compiler barrier once per row,
so GCC cannot combine its absolute address with a relocatable cache pointer.
The bank builder checks
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

## 1.3: lower decision, projection and matrix-refresh overhead

The stock kernel, 64-block audits, modes, thresholds, zero-depth saturation,
source order, writer hints and failure behavior stay unchanged. Optimizations:

- A contiguous derived zero-only address list and row offsets are rebuilt only
  when matrix membership changes, preserving track/entry order. The common
  all-track projection uses one bounded assembly loop; partial projections
  visit only selected rows. In-range words are read but never rewritten.
- The RAM address barrier is applied once per row instead of calling a helper
  for each of its 24 words. First/full/hinted scan sizes remain 312/312/24.
- A leaf assembly dispatcher handles validated zero-only blocks and steady
  dense/unsafe stock fallback without the general decision's large frame.
  Matrix publication selects its entry pointer; ordinary active matrices
  directly enter the general decision instead of paying the zero-path guards.
  The trampoline's equivalent return-code branches offset the indirect call.
  Every writer, periodic check, unready/invalid state and VERIFY case rejoins
  the general path before changing state. OFF/refusal immediately runs stock.
- The 12-of-13 dirty-track threshold uses the number of missing bits. Sparse
  eligibility is aggregated at matrix publication instead of rescanning rows
  on each decision. Dirty masks remain restricted to the same 13 track bits.
- The hot address list and assembly entries are aligned. No SRAM is borrowed
  and no EMAC arithmetic is introduced or changed; the added cache lives in DDR.

`tools/bench_st_modulation_skip.py` compares complete linked calls, SRAM,
decisions and counters with 1.2.3 over zero/active/changing/FX/overlap/dense/
unsafe/publication/error cases. It includes exact 64-block boundaries, unsigned
counter wraps, invalid pointers/modes and cache invalidation cases. Counts
include every new dispatcher check, cache rebuild and periodic scan.

`tests/test_st_modulation_skip_cfbench.py` exhausts all 65,536 unsigned
16-bit values on analog-emu, checking exact projection, untouched neighbors,
empty/duplicate lists and preserved ABI/physical EMAC state. A missing
projection must fail. The host test additionally exhausts all 8,192 dirty masks.

The native stock fixture now invalidates the writer's old translation when
installing the patch, matching the real install's cache sync. Previously a
later native publish could bypass the hook: this also reproduces with 1.2.3.
The real publication must still produce exactly two writer counts and a single
dirty row. Its scan test observes the exact 24 source addresses during refresh,
separately from any native source-group reads, with instrumentation installed
in freshly translated blocks. No original SRAM/heap, failure or mode assertion
is removed.

See [the MAIN performance record](../../docs/MODULATION-SKIP-MAIN-PERFORMANCE.md)
for the delivered bank's results and limits. Instruction counts are not
physical cycles or whole-MAIN CPU percentages.

## 1.4: less work preparing partial blocks

Partial blocks walk the active destination lists once, capturing dirty rows
and restoring clean rows during that traversal. The zero-only lists similarly
combine dirty capture and clean projection. Validated destinations stay inside
their own track row, and zero-only destinations cannot alias active ones;
unsafe and overlapping source groups retain their original fallback/order.

Sparse execution visits the selected track bits in ascending order instead of
testing all thirteen tracks. ColdFire bit-search instructions locate the track
and its single active source group, replacing the group-search loop. No extra
cache, SRAM allocation, EMAC use or per-block guard is needed. Full checks,
writer scans, thresholds and the complete stock kernel are unchanged.

The native oracle additionally mixes clean and dirty active rows with changing
zero-only words across all saturation ranges. It changes all six source groups,
forces overlap fallback, disables/re-enables the matrix, and crosses a periodic
audit boundary. Every result is compared with the full native stock call.
