# POLY 1.6 MAIN optimization

Release versions and development labels: see [the consolidated MAIN release](MAIN-OPTIMIZATION-RELEASE.md). Numbers in the measurements below identify historical development iterations.

Compared with POLY 1.5 in the ARP 1.12 / LFO3 1.4 / SP 1.4.5 bank.
The compiler profile, native synthesis kernels, queue implementation, AUDIO,
core 1.8.0, and other modules are unchanged. No device access is used.

## Retained changes

- An integer assembly `poly_sync` reads the existing sequence/MIDI/record
  caches. Its unchanged path has no stack frame. Changes still enter the
  original private SYNC command under native immediate exclusion; only the
  original locked C path writes the caches. No additional cached state.
- Publication uses six-register MOVEM bursts for aligned parameter/auxiliary
  rows. The 142-byte rows retain halfword accesses whenever either endpoint
  is not four-byte aligned. Padding bytes 101..103 and 123 remain untouched.
- The 291-byte raw setup Sound copy uses post-increment byte batches, safe
  for every source/destination byte alignment. UI Sound edits retain native
  memcpy and the LFO3 metadata backend.
- Aligned 80-byte prepared-event statics use a fixed twenty-long copy.
  These are native statics, with no change to pooled-event ownership.
- The keyboard-origin census is an integer leaf scanning the same 1,024
  overlapping halfword pairs from the captured parent cursor. It reads no
  further than the original census, writes no stack data, and preserves
  first-match press/release semantics.
- Pure local counters/scalars no longer force volatile stack traffic in
  synchronization, group publication, chord-note calculation and preparation.
  Firmware/shared accesses remain volatile. Signed-byte widening uses the
  compiler's audited integer forms. An instruction-free address barrier keeps
  fixed firmware addresses separate from bank-relative relocations.
- Held-note presence stops scanning at the first held voice.

The remaining control/copy paths contain no multiply-accumulate chain for
EMAC. These changes leave its mode/accumulators alone. All documented MAIN
SRAM windows already belong to LFO3/Chorus; samplers borrow Chorus windows
when it is absent. No undocumented free region or padding is allocated.

## Measurements

Same inputs and actual entry points in both linked banks:

| Operation | POLY 1.5 | POLY 1.6 |
|---|---:|---:|
| Unchanged sync | 79 | 37 |
| Publication, one shared slave | 130 | 118 |
| Publication, three-voice shared group | 246 | 212 |
| Publication, four-voice shared group | 372 | 326 |
| Publication, two four-voice shared groups | 733 | 641 |
| First key press, two-voice digital group | 6,017 | 5,097 |
| First release, same fixture | 783 | 734 |
| Configure same group | 11,578 | 8,525 |
| Two-voice sequencer chord admission | 667 | 667 |
| Three-FM M.LFO pipeline fixture | 12,129 | 12,095 |

Units are guest instructions, **not cycles or MAIN percentages**. MOVEM has
different memory/transfer costs from MOVE; publication's executed opcode bytes
increase (for example 304 to 324 for one slave) even as instruction count falls.
The final bank grows 752 bytes. Hardware/cache/bus effects require an A/B test.
The pipeline fixture excludes the separate frame/sync callback and must not
be added to it to claim a whole-instrument load figure. Independent POLY keeps
its native per-track modulation cost; M.LFO still publishes shared parameters.

## Rejected experiments

Skipping the active queue search when a free bucket exists looked cheaper
in isolation. Its first C form increased exhausted-pool paths. An assembly
version improved all 80 direct admission calls but lost C inlining in the
sequencer: a complete two-voice chord rose from 667 to 677 instructions.
A revised C form still raised a four-voice chord from 1,169 to 1,171.
Neither is retained. The original queue implementation remains intact, with
120 direct edge cases and complete sequencer events compared before/after.

## Verification and reproduction

Set up the documented compiler and Unicorn environment in SETUP.md. Define
`BEFORE` as the previous complete bank, `AFTER` as this bank, `CORE` as its
unchanged extracted MAIN, and `REPORTS` as an existing output directory.
Both banks need their sibling `.work/main.elf`; CORE needs its sibling
`verification.json` for the native core fixtures.

```bash
"$PY" tools/bench_st_poly_main.py --before "$BEFORE" --after "$AFTER" \
  --core "$CORE" --out "$REPORTS/main.json"
"$PY" tools/bench_st_poly_events.py --before "$BEFORE" --after "$AFTER" \
  --core "$CORE" --out "$REPORTS/events.json"
"$PY" tools/bench_st_poly_helpers.py --before "$BEFORE" --after "$AFTER" \
  --out "$REPORTS/helpers.json"
"$PY" tools/bench_st_poly_keyboard.py --before "$BEFORE" --after "$AFTER" \
  --core "$CORE" --stock "$SYNTAKT_STOCK" --out "$REPORTS/keyboard.json"
```

- 48 sync cases: unchanged, null pointers, sequence/MIDI mismatch and every
  record-byte mismatch, across four sequence alignments. Real locked sync
  executes; native immediate exclusion is an explicit boundary.
- 2,068 publication topology/PLAY maps: independent whole-buffer oracle,
  padding, long-access alignment, ABI, and per-call non-increasing counts.
  The historical publication test also runs 6,204 randomized oracle cases.
- Eleven native-event scenarios: 11 configurations, 66 presses, 55 releases,
  44 sequencer chords and group shutdown. Complete state, notes, pools,
  refcounts and kit bytes agree; every measured operation is non-increasing.
  Sequencer accepted-root staging is a fixture input; the real core ENQUEUE
  dispatcher and batch/native queue consumers execute. Parent UI origin,
  Sound publication, smoothing and lock application are explicit boundaries.
- Seventeen fixed-copy/alignment/canary/ABI cases; 31 stack-census cases
  assert exact halfword reads, first-match ordering and no writes; 120 queue
  edge cases preserve acceptance/results and instruction counts.
- Twelve stock/core keyboard/MIDI constructor and analog master-Sound cases.
- Existing analog integration, native modulation pipeline, Sound-copy and
  lock tests, and PLAY regression (120 cases / 9,600 blocks, with negative
  controls). Unicorn's native EMAC arithmetic is not a hardware sound oracle;
  the integer optimizations and unchanged native kernels are checked separately.
- The Sound/lock fixture now uses the pinned LFO3 backend's actual 36-byte
  event row (16 halfwords plus mask), asserting its exported context size.
  Its old 20-byte assumption failed on both banks. All value/refcount checks
  remain in place, and the corrected test passes both banks.
- Full common regressions, unit tests, provenance and instruction audit.
  The optional external Synfact-source comparison is skipped when unavailable.

Only POLY MAIN objects `main5-0.o`, `main5-2.o` and the two added assembly
objects differ from the previous bank. All other objects are byte-identical.
The working reference's complete core SYX SHA-256 remains
`985e0186db8069444ed96bc950779cb04046ddf1ced4d14367bbc52aa390b007`;
bank AUDIO remains 108,640 bytes with SHA-256
`cb86bae5e932fdd2a1b64b0d295ddeeb6db0677c1c6079c0198389fa8972a585`.

Hardware A/B: use the same core, project, pattern, sends, PLAY mode and
System info page. Compare OFF, independent POLY and M.LFO; then note-stealing,
keyboard/MIDI, p-locks, start/stop and group/kit/pattern changes. Keep listening
and MAIN/AUDIO readings separate from the emulator instruction counts.
