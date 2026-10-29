# Project sample pool and Sample Manager (core 0.8.3)

The core owns a 128-position pool, its project record, an on-device Sample
Manager and the MAIN/AUDIO transport. These services do not come from SMZ4.
Machine-specific controls, graphics and engines live in the optional bank.
The core still supplies its sample services when no machine file is installed.

The explicit Syntakt firmware builder compiles `audio-pool.c` into a 4 KiB
core service image and includes it through the existing AUDIO companion
capability module. Its bytes count toward the same 64 KiB resident reserve.
It is compiled from C during every build; no precompiled input is supplied.
The MAIN-only generic SDK build lacks the AUDIO service image and leaves
the pool transport disabled. Use the explicit builder for this feature:

```sh
python -m elekloader.sdk.syntakt --stock Syntakt_OS1.41.syx --eft /path/to/elektron-firmware-tool --system-info
```

Machine banks are compiled independently, without a firmware file:

```sh
python -m elekloader.sdk.syntakt.smz --preset samplers --out out/samplers.smz4
```

## Project record

The native project header's `f0..200` interval contains a 272-byte record.
It does not change the project size or the geometry of native sample slots.

| Offset relative to record | Field, big endian |
|---|---|
| `00` | `POOL` |
| `04` | u16 version 1 |
| `06` | u16 position count 128 |
| `08` | CRC32 of the 256 position bytes, seed `ffffffff` |
| `0c` | reserved, zero |
| `10..110` | 128 u16 references |

Reference 0 is empty; 1..64 identifies a native stock sample; `8000 | id`
identifies an HZFS file. HZFS ids 1..32767 are representable in this record.
The filesystem can contain larger ids, but those cannot be assigned here.
An old project without a valid record maps positions 1..64 to stock samples
1..64 and leaves 65..128 empty. From core 0.8.3 the live NEW PROJECT command
also installs these defaults, replacing any valid record left by the previous
working project, then synchronizes AUDIO. The command calls `4001d16a`, whose
native body runs first with its original arguments and return value preserved.
The separate `40018ab0` cached-project constructor initializes only its own
record. Version 0.8.2 covered that constructor but missed live NEW PROJECT.
Successful native validation/conversion of a project without
a pool materializes this record; project load and boot also create it in working
RAM when absent. The user saves normally to retain it on disk.

Existing valid pool records retain their previous assignments, including empty
or remapped positions in 1..64; there is no automatic migration or relocation.
Future additions always use 65..128 and removal never edits 1..64. Thus old
projects are preserved while newly initialized pools keep the stock identity map.
Native TwinShot keeps its original 64 stock sources independently of this pool:
sample +1 uses stock sample/slot 1 forward and -1 the same sample in reverse,
through +64/-64. Both native selectors use raw bytes 64..127 for forward
samples 1..64 and bytes 63..0 for reverse samples 1..64. Its update/render code
and stock descriptor table accesses are unchanged; long PCM never replaces them.

The core preserves the record across native snapshot, validation/conversion,
project load and project save. Before save it copies the current record into
the native cache through the pure cache lookup; it does not clear/reinitialize
the cache. A failed validation does not restore a record into a failed project.
After a successful project load the pool is read from that project's first
sector and synchronized to AUDIO. The user saves the project normally after
editing the pool. No autosave or backup is added.

## Boot and the native progress bar

1. The optional SMZ bank is validated and loaded silently before project loading.
2. Native project/state loading runs, then the **original stock sample-blob
   builder** loads its 64 samples unchanged.
3. The core reads the project pool and HZFS catalogue, plans unique long-sample
   extents that fit in PCM RAM, and maps stock references to the native blob.
4. The **same native sample sender and progress bar** cover stock PCM, the
   optional machine batch, long PCM and the core service pages. Long files
   are streamed from eMMC while the native sender requests each page.
5. Every long file's complete SAMP header/body CRC is verified. Its descriptor
   is published in the service's final pages only after successful readback.
   A missing, corrupt, unreadable or oversized sample has an empty descriptor.
6. AUDIO parses its native stock descriptors, verifies and installs the core
   service, then installs any valid machine bank before voice initialization.

There is no separate boot progress dialog for long samples. The native page
count includes the additional pages, so the original bar measures the whole
transfer. An invalid/absent SMZ is skipped; stock and pool loading continue.
Samples do not need an SMZ to be loaded into the core pool.

The PCM region is **`42101750..4404f750` on AUDIO**, about 31.30 MiB. The first
4 KiB at `42100750` hold the generic service code and 128 PCM descriptors.
The SMZ machine region ends at `42100000`, below these allocations. Native
stock PCM/descriptor regions remain independent. Files are deduplicated by
id within a loaded pool and are not silently cropped. A file must fit in
available contiguous PCM RAM; the disk filesystem itself has a larger capacity.

Pages use the native sample-store mapping. The service begins at wire index
`4162`, with PCM from `4164`. These map exactly to `42100750` and `42101750`.
The service's metadata/code pages are sent last, after sample CRC validation.
No bootloader, bootstrap, updater, vector or native copy/clear routine changes.

## Sample Manager

`SAMPLE MANAGER`, with a waveform icon, appears in SOUND and SETTINGS.
Core 0.7 constructs an independently owned native three-pane list view, using
native focus, scrolling, fonts, checkbox rendering, encoder handling and close.
Its callback captures and complete vtables are owned by the core; borrowed
firmware function/string objects are not copied or modified.

- It opens on the eMMC library. The left pane switches `VIEW POOL` / `VIEW eMMC`.
- YES toggles rows for multiple selection. Left/right moves between the list
  and the side panes. UP/DOWN moves one row; FUNC+UP/DOWN moves eight.
- The right pane applies `ADD TO POOL` or `REMOVE FROM POOL` to the selection.
  Adding chooses the first free project position in **65..128** and avoids
  duplicates. If those 64 positions are full, it reports POOL FULL even when
  an existing legacy pool has empty positions in 1..64.
  Removing positions **1..64** is refused with STOCK SLOT(S) PROTECTED. Batch
  removal skips these positions and removes only selected positions 65..128.
- Hold FUNC+YES to audition the current sample; releasing YES stops audition.
  A two-window ring streams PCM from the UI `ev_tick` subscription; the MAIN
  mix event consumes it. The manager's native idle method remains untouched.
  Preview has its own RAM and never replaces project/AUDIO sample descriptors.
- A library without usable entries contains an owned `NO SAMPLES` row, with
  logical sample count zero. Native list maintenance never sees an empty vector.
- NO uses native back/close. Save the project normally to retain assignments.
  Removing from the pool does not delete files; disk deletion remains in the SDK.

The manager and audition work without SMZ. It lists valid HZFS files in eMMC;
existing stock references remain visible and playable in the project pool.

Runtime loads show byte progress while the task streams PCM; no disk IO or
allocation happens in the audio interrupt. Insufficient space and transfer
failures are reported. The previous project record is restored on failure;
an uncertain receiver timeout quarantines further reuse until reboot.

## Runtime protocol and public machine API

The fixed AUDIO directory at `40040f50` is `{454c5001, getter, 128, 0}`.
`st_machine_sample(slot)` from `syntakt/machines.h` returns a borrowed
`{frames, pcm16be_pointer}` descriptor, or null before installation. A zero
length denotes unavailable PCM. Engines check a latched descriptor on update
and before render, and stop if it is retired/replaced.

The task uses the existing `800..bff` FPGA window when it owns the pool-busy
flag. System info defers its read-only query during that time; extended control
banks at `c00..fff` remain separate. Messages have a sequence, command, checked
destination/length, payload sum and explicit status ACK. DATA is at most 992
bytes, sequential and validated before copying. BEGIN permits only unpublished
extents in pool PCM RAM; COMMIT publishes the descriptor only after the entire
extent arrives. RETIRE removes references before their memory can be reused.
Replayed sequences return the same ACK without replaying writes. Runtime
requests cannot write machine/core code, scheduler tables or native descriptors.

MAIN uses temporary heap buffers for the boot plan, PCM page/read scratch,
catalogues and UI state; full long samples are never retained in MAIN RAM.
The 64 KiB reserve contains code/data, including the source-built service image.
The explicit builder prints resident bytes used and **bytes/KiB remaining**
and writes the layout into `verification.json` for every iteration.

## Verification scope

Native-host C fixtures exercise real source algorithms with explicit RAM/sector
buffers: project codec/migration, snapshot and validation retention, preservation
of stock PCM, boot page mapping and CRC failures, staged runtime publication,
duplicate ACKs, retirement, live-extent guards and PCM frame bounds. They do
not execute target instructions or firmware routines. Generated ColdFire code
is statically audited, SDK linking/lint run normally, and each packaged image
is re-read with protected sections/destinations verified. Emulator execution
and physical verification of the new three-pane UI remains pending. The previous
0.6.1 build's stock audio and SMZ upload/readback were confirmed on a device;
core 0.7 reports all four machines registered on a device, and the corrected
sample category was confirmed visible. Core 0.8 machine UI and playheads still
need physical validation.


From core 0.7.1 the core also queues the native sample bar's start and final
page. The unchanged stock sender reports every 128 pages, so a short batch
otherwise need not produce any intermediate update. These notifications use
the existing boot UI message queue; the App's interactive UI starts afterwards.

From 0.7.2 the same native queue also receives percentage changes for short
batches, not just start/end. Physical startup-screen responsiveness remains
unconfirmed: native sample preparation is still synchronous.

## Cached waveforms and AUDIO playheads (0.8)

The generic `st_machine_ui_api` lends a 96-bin min/max envelope for each selected
sample. HZFS PCM is accumulated while the existing loader reads it and becomes
ready only after complete CRC verification. Stock PCM is scanned in bounded UI
idle slices (2048 frames in the sampler bank), using its existing MAIN copy.
The cache is an owned native-heap allocation, approximately 50 KiB for 128
entries, outside the 64 KiB code reserve. Allocation failure leaves no waveform;
it does not prevent audio playback.

Machines can publish a bounded playhead snapshot through the core AUDIO API
at `42100770` (`454c4301`, function pointer). EPS command 6 reads eight
`{PCM pointer, frames, position, active}` records, with sequence/checksum and ACK.
UI polling never waits. A core ownership flag arbitrates the shared window with
System info and sample transfers. The core lends a cursor only if its PCM
identity and frame count match the selected project slot. No machine-specific
graphics or storage reads occur in this service.
