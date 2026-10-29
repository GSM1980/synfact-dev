# POLY for Syntakt

Source port of Synfact's POLY, including its optimized scalar
publication and shared native modulation/LFO guards. Requires Syntakt OS 1.41,
**core 0.12.2 / UI API 7 / module API 3 / host API 2**, and the matching AUDIO
companion. Code and state live in the optional SMZ, alongside the other mods.
No LFO3 or chorus is included. With host API 5 (core 0.12.11+) POLY narrows
its native subscription: without a group only TRIG, IMMEDIATE, the pattern
header and the POLY page operations reach its handler; the voice-routing
operations (TYPE, ENQUEUE, PUBLISH, OWNERS, MODULATION, LFO...) are added while
a group exists; from core 0.12.12 TYPE is dispatched only for slave
digital tracks. With host API 6 (core 0.12.13+) its page notifications are
limited to the TRIG ParameterView.

## Operation

On a digital track, open the third TRIG page. It preserves Synfact's layout:
**A: POLY, B: VOICES, E: NOTE, F/G/H: NOT2/NOT3/NOT4**, with its keyboard graphic.
OFF hides/disables the remaining controls. VOICES selects 2, 3 or 4 contiguous
digital tracks. The first track is the master; remaining tracks are slaves.
Groups cannot overlap or cross the digital/analog boundary between tracks 8/9.
Track 9 also has the third TRIG page (2 or 3 voices: 9–10 or 9–10–11);
track 10 has 2 voices (10–11). Tracks 11/12 cannot be masters. Track 12 is
never a voice (its machine class is CY/HH only). Either track-9 group conflicts
with track 10 ON. Analog NOTE uses native ID `14h` and range 0..84; chord
notes outside this range are omitted. Digital NOTE stays `15h`. Analog slaves
have the same TRACK IN / SLAVE MODE display and edit/Sound/machine guards.

Keyboard and MIDI note input allocate one voice per held note, rotate free
voices, steal the oldest occupied voice and absorb releases of stolen notes.
Track trigs and held-step preview play the chord. Sequencer chords retain
native condition/probability decisions, event ownership, cancellation and
first-step preparation. Live recording writes NOTE and relative note lanes.

Slaves show TRACK IN / SLAVE MODE and block parameter edits, page mutations,
machine selection and Sound writes according to the reference. Their saved
Sounds and sequences are retained. Leaving a group releases pending voices and
restores the track's own Sound. A group containing a MIDI-mode track is
suspended while its saved topology remains available for audio mode.

Topology uses Synfact's seven-byte PQ record in PatternSettings `21h..27h`,
with CRC16 (initial `ffffh`, polynomial `1021h`, bytes 0..4). Byte 0 is `50h`;
byte 1 is `51h` without analog groups, otherwise `58h | flags`: bit 2 enables
9, bit 1 selects its third voice, bit 0 enables 10. Thus active flags are
`5ch` (9×2), `5eh` (9×3), `59h` (10×2); `58h` is also valid (none).
`5ah/5bh` lack the required track-9 ON bit; `5dh/5fh` overlap and are rejected,
even with a correct CRC. Bytes 2..4 retain the eight digital three-bit states;
bytes 5..6 retain the big-endian CRC. Old Synfact analog records load directly.
Invalid/missing records load without POLY. Analog OFF voice counts are not
encoded (as in Synfact); without an analog group the bytes match the old digital
format exactly. Pattern copy/reset/undo/save use the native backing and notifications.
This record does not overlap the project's sample-pool record.

## Integration and performance

Six subscriptions use `ev_render_in`, contextual `ev_draw` / `ev_enc`, consuming
pre-native `ev_key`, `ev_tick`, and the shared `ev_native` transaction collection.
The module manifest contains no firmware sites or runtime patch installer.
Core owns native replay, site validation and ABI. See
[the hook audit](../core-st/HOOK-AUDIT.md) for the new operations.

Existing page/action adapters support TRIG. Encoder context comes once after
the actual native editor virtual resolver, covering Source/TRIG/base views.
Selector and native page/slave guards run before any module encoder, so FM's
consuming ratio handler cannot bypass slave ownership. The module does not
repeat that guard. Metadata/base navigation no longer dispatch encoders.
Contextual DRAW runs after each complete
native view, below later overlays. It replaces base-view emission and the
recorder's private draw bridge. KEY pre/post subscribers share the same list.
All lists and assembly operands publish in one existing IRQ exclusion, so a
render callback cannot observe an unbound native transaction list.

Publication copies the master's effective scalar parameters while retaining
independent voice synthesis state. Hidden slave modulation/LFO work is skipped;
their LFO states pause and resume when released. Only masters/independent tracks
consume random-stream steps, matching the optimized Synfact reference. LFO1/2
remain present; adding LFO3 is separate work.

The current generic machine transport supplies effective type and 28-parameter
snapshots for FM TONE and SP machines. The obsolete FM-only mailbox is excluded.
Target flags remain **`-O3 -mcpu=54415 -mtune=cfv4`** with the bounded loop-peel
policy. No filesystem IO or general heap allocation is introduced in render;
note expansion uses the bounded native event pools and rollback on exhaustion.
Physical CPU cost and timing remain to be measured. See the instruction
comparison and physical procedure below.

## Source and build

[provenance.json](provenance.json) pins authored donor files. `tools/adapt_synfact_poly.py --synfact PATH` is the original bootstrap import;
it overwrites local adaptations and must not be rerun to maintain this port.
Neither it nor the kernel importer imports legacy builders. Local authored
source hashes are pinned separately from the unchanged donor hashes. Native optimized kernels are prepared explicitly by
`tools/import_poly_kernels.py --stock Syntakt_OS1.41.syx --synfact PATH`, using
the pinned official Syntakt image and authored guards. Generated private
assembler/provenance remain ignored in `imported/`. Normal bank compilation
uses local C/assembler and does not read donor firmware or old SMZ files.

```sh
python -m elekloader.sdk.syntakt --stock Syntakt_OS1.41.syx --eft /path/to/elektron-firmware-tool --system-info --out out/poly-core
python -m elekloader.sdk.syntakt.smz mods/samplers-st mods/fm-tone-st --module mods/recorder-st --module mods/poly-st --module mods/system-info-smz-st --out out/poly-core/synfact-machines.smz4
```

System info's UI/collectors move to an SMZ module, freeing resident space. The
core keeps its generic clock accounting; `--system-info` retains the AUDIO
meter. The full candidate includes that module automatically in its bank.
Older machine banks and ordinary subscriptions remain readable. The recorder
must be rebuilt for the new complete-view DRAW/host API; the older strict
host-version initializer declines cleanly. New native/pre-KEY subscriptions are
rejected by older cores before execution. Use the matching SYX and SMZ together.

## Validation

`tests/test_st_poly.c` runs production C with explicit native doubles on a
32-bit host: 108 digital topologies plus analog 9×2/9×3/10×2, 2/3/4 voices, key/chord/sequencer paths,
stealing/note-off, event rollback, MIDI suspension, CRC, UI controls, slave
guards, live recording and scalar publication. `test_st_parameters.c` checks
effective owner/type/count transport and invalid-owner fallback.

`tests/test_st_native.c` checks actual core list merging, native fallback and
pre/post KEY isolation. `tests/test_st_poly.py` checks source hashes, subscriptions,
host pointers, native entry masks/replays and imported optimized guards. The
common regression runner includes FM/SP, recorder, pool/storage, units, linked
instruction audits and lint. These are host/static checks, not a full firmware
execution. Physical audio, UI, persistence and CPU tests remain pending.

The 0.12.2 bank fixes three guard jumps whose sizes GAS had relaxed inside
the imported native kernels. Explicit six-byte encodings retain every original
relative branch offset. The linked audit compares the complete kernel bytes
with the preserved stock bodies, accounting only for the guards/external
bridges, and rejects shortened layouts. It also rejects the prior delivered
0.12.1 bank, providing a regression witness for the activation exception
reported in `IMG_5182.jpeg`. Physical confirmation of this fix is pending.

The 2026-10-27 performance investigation adds an OFF fast path: native TRIG
still synchronizes a newly loaded topology first, then declines directly to
the core's original replay when no groups exist. ENQUEUE also declines, and
PUBLISH skips the projection entirely. The stable frame cache remains necessary
to recognize saved group changes; it creates no queue command when unchanged.
Host tests check 1,500 OFF frames without event allocation, immediate commands,
commits or projections, plus first-trig recognition of a newly loaded PQ record.
This removes unnecessary work but does not establish the cause of the reported
persistent 100% MAIN load. Optional [MAIN timing](../main-profile-st) uses the
existing events to obtain physical measurements without adding native sites.

## Analog parameter layout evidence (OS 1.41)

Checked before changing publication against the official OS SHA-256
`8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e`:

- `400a31a2`: `400a31e6..400a3206` copies 142 bytes from Sound+`14h` to
  `800021cch + t*8eh`, with no digital-only branch. `400a31dc..400a31e2`
  updates `800030c0h + 4*t`, the Sound pointer cache. The shared publisher
  calls `400a30c4`, whose explicit bound at `400a30da` permits t=0..11;
  `400a30e2..400a30ec` publishes the machine byte to `80002f50h[t]`.
  `400a30f0..400a3108` forms `124*t`, and the later copies fill the auxiliary
  fields, preserving the gaps at offsets 101..103 and byte 123.
- `40097952`: conversion covers the unified working table. `400979b2..400979c0`
  copies `64ch = 13*7ch` auxiliary bytes from working+`754h` to `80006a9c`.
  `400979f6..40097a04` advances by `8eh` to `80006a14`; the explicit analog
  correction loop at `40097a06..40097a34` starts at type cursor `80002f58`
  and uses the same `8eh` stride for tracks 9..11.
- `40097afc`: `40097b04..40097b1a` initializes working+`1ch`, auxiliary+`754h`,
  and level cursor `80004670`. The first loop advances `8eh`/`7ch` and ends
  at `80004688` (12 entries); the second advances the same strides through
  index 12 inclusive (26 level bytes), hence the guards must retain `<12`.
- At publication site `400a4ade`, the following native voice loop advances
  auxiliary by `7ch` (`400a4b08`) and terminates at 12 (`400a4b3a`). The later
  effective-type loop `400a4bce..400a4c00` stops at `80002f58`: analog tracks
  read the original type table directly. The event Sound at +`28h` reaches
  `400a4638 -> 400a31a2`. No analog TYPE interception is needed.

This establishes the same scalar field layout and alternating two-byte working
alignment for analog voices. It does not emulate the analog synth hardware.
On commit, each analog slave receives its master's kit Sound. A machine-byte
change is checked only in `ev_tick`; a private IMMEDIATE command rechecks current
owners/states and publishes under the existing native IRQ exclusion. Inactive
analog polling is one byte test. No machine scan runs in `poly_sync`/PUBLISH.

## Instruction budget against main (`dbf6e12`)

`tests/test_st_poly_cost_emu.py` executes the linked banks with the documented
ColdFire flags, EMAC mode `20h`, explicit zero-depth modulation/LFO parameters,
reset LFO state, tempo 14400, and no trig flags. Native kernels/helpers execute
from the SHA-pinned stock image. Queue/native TRIG replay is a boundary double;
we count POLY routing, not the stock event builder. Counts are instructions,
not cycles, percentages, cache misses, contention or device measurements.

The block column includes unchanged frame sync (78 instructions), POLY native
PUBLISH/OWNERS/MODULATION/LFO callbacks, filtered TYPE callbacks twice per digital
slave, and both complete native kernels. It excludes the core's dispatcher and
unrelated modules; physical measurements must include those. OFF is subscription
gated: no publication or guard kernel executes. The stock kernels still run.
Analog rows compare with main's identical digital subset, because main has no
analog topology. The measured analog rows include the inherent parameter copy.

| Topology | Publication main → new | Modulation main → new | LFO main → new | Block subtotal main → new | TRIG t1 main → new |
|---|---:|---:|---:|---:|---:|
| None | 0 → 0 | 6614 → 6614 | 4819 → 4819 | 11511 → 11511 | 119 → 116 |
| 1×2 | 193 → 141 | 6410 → 6402 | 4586 → 4582 | 11464 → 11394 | 278 → 178 |
| 7×2 | 193 → 141 | 6410 → 6402 | 4586 → 4582 | 11475 → 11394 | 267 → 169 |
| 1×4 + 5×4 | 760 → 742 | 3860 → 3852 | 2766 → 2762 | 8031 → 7995 | 278 → 178 |
| 9×2 | 0 → 141 | 6614 → 6402 | 4819 → 4582 | 11511 → 11320 | 119 → 169 |
| 9×3 | 0 → 258 | 6614 → 5892 | 4819 → 4218 | 11511 → 10563 | 119 → 169 |
| 10×2 | 0 → 141 | 6614 → 6402 | 4819 → 4582 | 11511 → 11320 | 119 → 169 |
| 1×4 + 9×3 | 417 → 661 | 5390 → 4362 | 3858 → 3126 | 10088 → 8566 | 278 → 178 |

TRIG costs with analog groups newly active include the necessary grouped route;
main previously declined every analog event. Digital behavior is unchanged.
The duplicate synchronization in the grouped TRIG adapter is removed; direct
`poly_trig` callers still synchronize. The byte `poly_has_groups` replaces the
two owner-word comparisons, and covers all 12 tracks.

All 108 digital layouts are checked for non-increasing publication instructions;
all 432 digital/analog combinations are checked for exact copied bytes, untouched
gaps, callee-saved registers and SP. A single slave costs 141 instructions at
any position (including 9–11); a second same-master slave adds 117. The copy
body is identical for digital/analog voices and uses word accesses for the
142-byte block. The end bound is loaded once into a saved register. First/last slave bounds
and a slave bitmap are computed only
at commit, alongside owners, under the native exclusion. Starting at the first
slave removes the otherwise-added scan over independent preceding tracks.

Each extended guard retains `cmpi.l #12`; index 12 goes straight to stock.
An IRQ-published slave bitmap replaces the owner lookup, saving two instructions
for each digital entry and avoiding an aggregate analog-identity overhead.
For the 1×2 case the isolated guard totals (first / second / LFO) are
149/146/154 → 145/142/150; with 9×3 they are 148/144/157 → 146/144/147.
Unchanged native bodies remain covered by the linked byte-for-byte audit.

Reproduce using the Unicorn interpreter/environment in `docs/SYNTAKT-BUILD.md`:

```powershell
$env:LIBUNICORN_PATH='C:/Desarrollo/syntakt-dev/syntakt-work/analysis/unicorn-2.1.4-oneshot-build/Release'
& C:/Desarrollo/syntakt-dev/syntakt-work/sp-one-shot/.venv/Scripts/python.exe -B tests/test_st_poly_cost_emu.py `
  --before out/poly-analog-20261029-evidence/baseline.smz4 `
  --after out/core-st-0.12.19-poly-analog-ready/synfact-machines.smz4 `
  --stock C:/Desarrollo/syntakt-dev/elektron-firmware-tool/Syntakt_OS1.41.syx `
  --out out/core-st-0.12.19-poly-analog-ready/poly-cost.json
```

The baseline was built with the same builder/flags before editing, from main
`dbf6e12`. Both `.work/main.elf` files are required. Their bank hashes and the
complete counts are recorded in the output JSON. Workstation stock/EFT inputs
currently live under `C:/Desarrollo/syntakt-dev/elektron-firmware-tool` (the build
guide's former `C:/Desarrollo/elektron-firmware-tool` location is absent).

## Physical validation — still required

Use matching 0.12.19 SYX plus the normal bank, then its diagnostic counterpart
`synfact-machines-profile-r4.smz4` in
`out/core-st-0.12.19-poly-analog-ready/`. Build the diagnostic with the exact normal
bank command in `docs/SYNTAKT-BUILD.md`, adding `--module mods/main-profile-st`
first and `--module mods/main-profile-end-st` last. Do not ship profiler modules
in the normal bank. Follow [MAIN profiling](../main-profile-st/README.md).

On main and this branch, use the same pattern, machines, note rate, modulation,
locks, outputs and tempo. Record two settled windows at boot, PLAY and STOP for
(1) no groups, (2) digital-only groups, (3) 9×2, 9×3 and 10×2 (compare equivalent
voice workloads; main cannot load these topologies). Capture MAIN/AUDIO and
I/S/P/D/C/E, then repeat with the normal bank to disclose profiler overhead.
Check activation, slave editing guards, master machine changes while stopped
and during PLAY, MIDI suspension/resumption, chord notes at 84/85, saved-pattern
reload and slave Sound restoration. Never transfer/install from an agent.
Until these physical readings exist, the zero-CPU requirement is **not closed**.


Delivery validation uses `out/core-st-0.12.19-poly-analog-ready/`: core/SYX
verification and lint, normal and profiler SMZ banks, host suites and the linked
POLY instruction counter. The common regression's previous-build input is
`out/fm-feedback-fix-20261029`, whose core and FM/SP AUDIO match both the current
build and the main bank built before this task. The older LFO3-publication
folder predates main's FM feedback changes and is not an AUDIO baseline for
this task. No comparison check was relaxed.


## 0.2.1: analog keyboard/MIDI slave Sound fallback

Physical report after 0.2: track 9 with 2/3 voices alternates one different
sounding note as voice allocation rotates. Track 10 with 2 voices and digital
groups were reported unaffected. Confirmed native defect: the live constructor
`40119684` clears event Sound+`28h` at `401196e2`. POLY previously retargeted
only track/note identity. In the consumer, `400a4610` sees the null Sound and
`400a464c..400a466a` chooses the destination track's saved kit Sound, invoking
`400a31a2/400a30c4` and replacing its machine byte. The preceding commit-time
publication cannot prevent a later note from undoing it. Digital effective TYPE
resolution can mask that mismatch. The audible outcome depends on the saved
slave Sounds; a group that sounds correct does not rule out the fallback.

`poly_voice_target` now binds the master's borrowed kit Sound only for analog
slave events with neither an explicit nor a pooled/locked Sound. It is shared
by keyboard/MIDI, chord, preview/release and first-step preparation. It retains
existing explicit Sound references and leaves master/digital events unchanged.
This runs on events, with no added per-block work. The instruction counter
reproduces the previous block/guard/publication counts exactly (432 topologies).

The earlier host test's sink stopped before native Sound selection; therefore
it verified routing while missing this failure. `tests/test_st_poly_keyboard_emu.py`
now executes the real live constructor, compiled POLY immediate handler and
native Sound selection/publication up to its smoothing tail, against stock MAIN
and the loaded 0.12.19 resident core. All 12 keyboard/MIDI cases (9×2, 9×3,
10×2) fail with the previous bank and select the master Sound with 0.2.1.
This checks firmware data/control flow, not analog audio electronics. Host tests
also cover chord/preview/sequencer/statics and retaining explicit/pooled Sounds.

The corrected full normal bank is
`out/poly-analog-keyboard-20261029/synfact-machines.smz4`, with the same SP,
FM TONE, Recorder, LFO3, POLY and System info combination. Keep the already
installed core 0.12.19 SYX; only replace the bank. The folder's core images and
SYX are identical copies of the preceding verified delivery, not a changed core.
The user confirmed the alternating-note correction works on the Syntakt on
2026-10-29. Physical CPU/timing measurements remain pending.

```powershell
$env:LIBUNICORN_PATH='C:/Desarrollo/syntakt-dev/syntakt-work/analysis/unicorn-2.1.4-oneshot-build/Release'
& C:/Desarrollo/syntakt-dev/syntakt-work/sp-one-shot/.venv/Scripts/python.exe -B tests/test_st_poly_keyboard_emu.py `
  --before out/core-st-0.12.19-poly-analog-ready/synfact-machines.smz4 `
  --after out/poly-analog-keyboard-20261029/synfact-machines.smz4 `
  --stock C:/Desarrollo/syntakt-dev/elektron-firmware-tool/Syntakt_OS1.41.syx `
  --core out/poly-analog-keyboard-20261029/main.bin `
  --out out/poly-analog-keyboard-20261029/keyboard.json
```

The 0.2.1 delivery passes the common regression (58 checks; 0 optional
comparisons skipped), a separate profiler-order/timer test, fresh core lint,
the 12 native keyboard/MIDI cases and the 432-topology instruction/copy test.
The recorded block/guard/publication counts equal 0.2 exactly. AUDIO and the
core SYX remain byte-identical. The user physically verified this corrected
bank on 2026-10-29 and approved merging it into main. This functional
confirmation does not supply the still-pending CPU/timing readings.
