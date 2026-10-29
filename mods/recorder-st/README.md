# Audio Recorder for Syntakt

Source port of Synfact's recorder, UI, trim, save job, optimized capture and
normalizer. Requires Syntakt OS 1.41 and **core 0.12.0 / UI API 6** with its
AUDIO companion. The recorder is an optional MAIN module inside the same SMZ4
bank as FM TONE and the four SP machines. It does not require a donor SYX.

## Controls

Double-tap **SOUND** within 350 ms. The first press keeps its native action;
the recorder opens on the second release, after the native release processing.

| Control | Action |
|---|---|
| A before recording | Toggle POST FX |
| E / F / G / H | Recording length / threshold / source / input monitor |
| YES on an empty take | Arm threshold recording |
| FUNC+YES on an empty take | Start immediately |
| YES while recording | Stop, then normalize |
| FUNC+YES with a take | Audition |
| A / C with an editable take | Trim start / end |
| B / D with an editable take | Zoom and loupe |
| YES with a take | Confirm trim; YES again opens native naming and optional track assignment |
| NO after confirming trim | Undo trim |
| FUNC+NO before confirming trim | Discard take |
| NO outside confirmed trim | Close; the take remains in RAM |

Capture is mono PCM16 at 48 kHz, up to 60 seconds. Sources are IN L/R/L+R,
MAIN L/R/L+R, and TRK 1..12. Monitor affects external inputs only. PLAY/STOP
continue through the native transport; a take that fills its set number of
steps stops a playing sequencer (native STOP `4008c288`) before normalizing.
Audition and monitor use the core's preview bus `8000dfd0`, outside the FX
track (`<syntakt/mix.h>`). Saving uses a native 16-character name;
NO in track selection skips assignment and still saves the sample.

The nondestructive selection is written to HZFS with explicit PCM16 big-endian
serialization and readback verification. Project-pool placement starts at the
first free slot **65..128**; stock positions 1..64 stay intact. Optional digital
track assignment selects SP ONE (66), then its SAMP parameter `23ch` through
the native Sound setter. A full/busy pool does not delete the saved HZFS file.

## Existing events and core services

Seven subscriptions use the current collections: raw `ev_key`, raw `ev_enc`,
`ev_tick`, contextual `ev_draw`, `ev_main_mix`, `ev_fs_write`, `ev_fs_written`.
There are **no new firmware sites or event collections**. The owned native
view keeps its stock key/encoder/idle methods. The shared complete-view adapter emits into
the existing DRAW collection below naming/track modals; its stock draw method
is retained.

UI API 5 introduced a borrowed, versioned host table for the core's HZFS validator,
block IO, CRC, pool synchronization, preview stop and contextual event dispatch.
The module rejects older services before activation. Native high-zone file
descriptors now carry a completion capture, allowing the existing FS-written
event to report normal upload completion without an additional hook.

A short atomic lease serializes recording saves with native high-zone uploads.
No filesystem IO, allocation, normalization or UI operation runs inside an audio
IRQ or the lease's interrupt exclusion. An interrupted upload conservatively
keeps saving busy until a successful high-zone transfer completes (or reboot).
This avoids publishing a competing catalogue while a native transfer is unresolved.

## Speed profile and source provenance

Target compilation uses **`-O3 -mcpu=54415 -mtune=cfv4`** and the existing bounded
loop-peel policy, without relaxed math or FPU instructions. Capture retains the
optimized full 32-frame path, partial-tail behavior, 60 Hz empty-state metering
and inactive-state skips. The source normalizer retains ColdFire MULS.L/SWAP,
exact -1 dBFS fixed-point scaling and combined waveform-envelope updates.

Normalization runs in 3,072-frame quotas, at most 32 per UI tick. Save writes
and verification use bounded 4 KiB steps. Each job checks the audio clock
between steps against a soft 4 ms budget; a native storage call is not a hard
real-time deadline. Dirty recording paints are capped at 15 Hz. Closing the
view cancels an unfinished save, releases scratch/lease state and retains the
take; closed views do no background normalization work.

[provenance.json](provenance.json) pins the five authored Synfact source files.
`tools/adapt_synfact_recorder.py --synfact PATH` is an explicit maintenance
import, never part of normal builds. The port retains the source UI/draw/trim
and optimized capture/normalization arithmetic. PRE/POST follows that reference:
shared delay/reverb returns can include other tracks and tails; it does not
fully model the analog chain or an active chorus path. Physical routing, sound,
CPU load and native modal behavior still need device verification.

## Build and delivery

Build the core with the existing offline builder and combine all optional mods:

```sh
python -m elekloader.sdk.syntakt --stock Syntakt_OS1.41.syx --eft /path/to/elektron-firmware-tool --system-info --out out/recorder-core
python -m elekloader.sdk.syntakt.smz mods/samplers-st mods/fm-tone-st --module mods/recorder-st --module mods/poly-st --module mods/system-info-smz-st --out out/recorder-core/synfact-machines.smz4
```

The local candidate is `out/core-st-0.12.1-ui-ratio-fixes/`. Install its
matching core SYX and combined bank; installing an SMZ alone does not upgrade
the resident core. The explicit bank transfer command is:

```sh
python -m elekloader.sdk.syntakt.mods put out/core-st-0.12.1-ui-ratio-fixes/synfact-machines.smz4 --yes-write-device
```

The CLI checks readback; restart to activate the bank. No device transfer is
performed by a build or test.

## Validation

`tests/test_st_recorder.c` executes the production C with explicit native UI/IO
doubles and the real core HZFS/CRC code on a 32-bit host. It covers double-tap
edges/wrap, focus, raw encoders, trim/zoom, 72 capture comparisons, 56 exact
normalization cases, a full 60-second take, PCM readback/CRC, slot 65 assignment,
pool/AUDIO failures, competing transfers and closing during save.

`tests/test_st_recorder.py BUILD_DIR --synfact PATH` checks source provenance,
linked event/host ABI, target flags, IRQ helper instructions and retained donor
draw/trim/optimized arithmetic. The complete regression runner also checks FM,
SP, project pools, Sample Manager, native hook replays, instruction legality,
unit tests and lint. Final 0.12.0 AUDIO remains byte-identical to 0.11.0. These checks
do not execute ColdFire or replace physical tests on the Syntakt.
