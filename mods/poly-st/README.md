# Digital POLY for Syntakt

Source port of Synfact's digital POLY, including its optimized scalar
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
Groups cannot overlap or cross track 8. Analog tracks have no POLY page.

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
with CRC16 and validation. Invalid/missing/retired analog records load without
POLY. Pattern copy/reset/undo/save use the native backing and notifications.
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
Physical CPU cost and timing remain to be measured.

## Source and build

[provenance.json](provenance.json) pins authored donor files. The maintenance
import is `tools/adapt_synfact_poly.py --synfact PATH`; it never imports legacy
builders. Native optimized kernels are prepared explicitly by
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
32-bit host: 108 group topologies, 2/3/4 voices, key/chord/sequencer paths,
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
