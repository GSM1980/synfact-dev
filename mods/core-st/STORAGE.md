# `/mods` and safe bank staging, Syntakt core 1.6

Core 1.6 adds two independent 8 MiB slots inside the original 16 MiB
reservation. `/mods-stage` writes the inactive slot, validates the complete
SMZ4 file and its exact core contract, then records it as `CANDIDATE`.
The first boot changes it durably to `TRYING` before running bank code.
After checking sound, playback and controls, the user confirms it through
`mods confirm` or Transfer. An unconfirmed `TRYING` record is skipped on the
next boot; the previous `GOOD` bank, or stock machines, start instead.

The slot metadata sectors are at `02bffe00` and `033ffe00`; the 16-byte
confirmation command is at `02bffc00`. Each metadata sector contains a magic,
format version, generation, state, exact length, length complement, stamp and
CRC32. The slot payload capacity is 8 MiB minus 1024 bytes. `/mod-status`
reports contract, trial slot/generation and rollback in words 22..26.
`/mods` is read-only. Legacy `/mods` metadata at `03400000` is still read
until the first new bank is staged; that first candidate goes to slot 1 so
the legacy bytes remain intact. A core downgrade must be treated separately:
an older core does not understand the new slot metadata.

## Legacy layout and transport details

OS 1.41 only. Extends native Data RPC with the exact object path `/mods`.
Before core 1.6 it was one file, replaced by each accepted upload, with a **16 MiB maximum**
(16,777,216 payload bytes, excluding the native Data envelope). Empty and
odd-sized files retain their exact length. There is no `SAMP` header or padding
inside the stored file. The storage endpoint treats every payload as opaque bytes. From core 0.4,
the separate optional machine loader validates SMZ4 at boot; see [SMZ4.md](SMZ4.md).

This is an endpoint for a Data RPC client. It does not create a directory or
add an entry to the native root listing, and it does not teach Elektron Transfer
to recognize a new extension. `/mods/name` and `/mods/` are not aliases.

## Implementation

Everything is built as an ordinary format-2 core by the SDK:

- `files.s`: three native extension points and their event tables.
- `files.c`: `/mods` descriptor, write guard and native completion callbacks.
- `storage.c`, `storage.h`: bounded eMMC transport metadata and IO helpers.
- `elekloader.sdk.syntakt.mods_object`: optional offline native Data envelope pack/unpack helper.

The storage implementation uses the native OS 1.41 descriptor/stream ABI,
with explicit source, bounds and completion checks. It does not change the
sample-slot limit or use firmware-derived code blobs.

| Event | Native site | Handler, task context |
|---|---|---|
| `ev_fs_route` | `400b8088`, displaced 8-byte prologue | `int f(router, const std::string *path, Result<FileInfo> *out)` |
| `ev_fs_write` | `400b618c`, displaced 8-byte prologue | `int f(router, const FileInfo *info, Result<Writer> *out)` |
| `ev_fs_written` | `4010b8da`, native completion callback call | `void f(const capture *fn, const BackupHeader *header, Status *status)` |

The first two return nonzero only when they construct the complete result.
Zero declines to the next handler and ultimately the original prologue/body.
The third runs after the original callback, only in successful DataWriteClose;
it may downgrade success to a storage error (`status+0` byte, owned string at
`status+4`). No subscriber means the exact native operations are replayed.
The core subscribes its own `/mods` handlers at order 50. Other native paths
are declined without metadata IO.

`FileInfo` is the native 124-byte descriptor: type +0, source kind +4,
permissions u16 +8, byte address +12, length +16, id +20, tag +24; six native
16-byte function objects from +28. `/mods` uses type 8 and raw-eMMC kind 2.
It allows write (4), and read (2) only with a committed metadata record.
Move, copy, rename and clear are not enabled. All captures are created here
and cloned by the native constructor through our inline manager; no borrowed
firmware `std::function` or string representation is copied.

## eMMC reservation

| Byte interval, exclusive end | Purpose |
|---|---|
| `00400000..02400000` | Original 64 sample slots, individually bounded by core 0.5 |
| `02400000..03400000` | Up to 16 MiB of `/mods` file bytes |
| `03400000..03400200` | One sector of transport metadata |

This is immediately after `/samples/1..64` in the native low eMMC gap.
It is well below the native MRAM
copies at `0f400000`/`0f800000` and the project area. The research covers stock
1.41's known accesses; it does not prove the device contains no historical
data there or make the region safe for unrelated firmware versions.

Record (big-endian): +0 `ELMODS01` (8 bytes), +8 record version 1,
+12 exact file length, +16 one's complement of length, +20 standard CRC32
over the first 20 bytes, remainder zero. This is private transport metadata
outside the file, not an SMZ4 format definition. Missing/bad records are absent
files. A read/ready/capacity failure is an error, not an absent record.

## Write ordering and limits

1. Native DataWriteOpen resolves `/mods` and starts its normal single write job.
2. The first DataWritePartial copies the transport header length into FileInfo.
3. `ev_fs_write` checks the exact destination, type and unsigned `length <= 16 MiB`
   before any native stream allocation/body write. It checks card readiness and
   capacity through the metadata sector, then invalidates the old record.
4. The existing bounded SafeMmcStreamWriter receives the bytes, with the native
   64 KiB buffer and the native transport header/footer/checksum handling.
5. Only after successful native close and header/footer validation does the
   completion callback write the matching length record and read it back.
   A metadata commit failure becomes a failed DataWriteClose response.

Readback uses the original MmcStreamReader at the same byte address and exact
recorded length. This endpoint introduces no 16 MiB RAM allocation or automatic execution.
The independent high-zone filesystem added in core 0.5 may create blank
catalogues at boot; see [HZFS.md](HZFS.md). All IO is task context; native block primitives take
their own RTOS controller mutex. A shared descriptor `prepare` callback is
deliberately not used for invalidation because native READ also invokes it.

Replacement is destructive: an interrupted transfer leaves `/mods` unavailable
until the next successful upload, rather than publishing a partial file. No
backup, rollback or automatic recovery copy is created.

## Build and offline transport

To upload a raw SMZ4 bank from the repository root, with `python-rtmidi`
installed and the device running a working core:

```sh
python -m elekloader.sdk.syntakt.mods put out/samplers.smz4 --yes-write-device
python -m elekloader.sdk.syntakt.mods get --out out/readback.smz4
```

The upload command replaces the single `/mods` file and reads it back for
byte-for-byte verification. Restart the device after a successful upload.
Use `--port "name"` before `put`/`get` if the default `Syntakt` match is
ambiguous. Pass the raw `.smz4` file; the command creates the transport
envelope itself. Importing the module or using `--help` opens no MIDI ports.

```sh
python -m elekloader.sdk.build mods/core-st --stock Syntakt_OS1.41.syx
python -m elekloader.lint mods/core-st/out/core-0.5.elemod --stock Syntakt_OS1.41.syx
python -m elekloader.sdk.syntakt.mods_object pack code.smz4 out/code.obj
python -m elekloader.sdk.syntakt.mods_object unpack out/readback.obj out/code-readback.smz4
```

A Data RPC client sends the prepared object to `/mods` with DataWriteOpen,
DataWritePartial and DataWriteClose, and obtains readback with DataRead*.
The normal 31-byte object header has family/type 8; its 12-byte footer contains
the native body checksum, length and footer marker. Those 43 transport bytes
are not stored in the payload area. The helper has no MIDI or USB functions.

## Verification and scope

`tests/test_core_st_files.c` runs the production C route/storage logic on a
32-bit native host with explicit block-IO/constructor doubles. It covers exact
and oversized lengths, other paths/descriptors, persistence, interruption,
record corruption, empty files, short/unknown cards and IO/commit failures.
It does not run the ColdFire assembly or native stream implementation.
`tests/test_core_st_objects.py` checks the offline envelope and corrupt inputs.
Firmware builds also check stock preimages, link compatibility and the emitted
instruction encodings. No instruction emulation or physical disk write is
part of these checks. Real upload/readback and power interruption still need
hardware confirmation.

MAIN sources are compiled and linked by elekloader. A compact SYX can be
packaged as in the core README. Storage alone needs MAIN only and retains AUDIO.
The explicit machine companion additionally supplies and verifies its bounded
AUDIO application image. Sections 1/2/4/5/6/8 and all destinations stay stock. Bootstrap, updater and boot stub
are unchanged. Generated firmware and reports belong only in ignored `out/`.

Core 0.5 also supplies [long-sample storage and stock upload guards](HZFS.md).
The `/mods` handler remains at order 50; the stock/high-zone write guard runs
at order 40 and the high-zone route handler at order 60.


## Read-only loader diagnostics (core 0.7)

`python -m elekloader.sdk.syntakt.mods status` reads `/mod-status`, an exact
read-only native memory object (type 8, kind 1, id 1, 64 bytes). It performs no
eMMC write, reload or activation. `/mods` remains the single opaque stored file.
The snapshot contains 16 big-endian words: `ELS1`, version 1, boot loader state,
file length, first file word, parser rejection source line, active flag, machine
count, AUDIO page count, eMMC ready flag, card sectors, registry type mask, selector query mask, last selector track,
selector enumeration limit and loading-mods paint count. The final five words
were zero in core 0.7 and are supplied from 0.7.1.
State 0 means loaded; 1 missing/uncommitted file; 2 validation rejection; 3 file
read failure; 4 heap failure; 5 initializer failure; 6 missing AUDIO capability;
7 missing sample-pool capability; 8 metadata/card IO failure; 9 startup hook not
reached. Parser line numbers refer to `smz.c` of the matching core source.
The route exposes this fixed snapshot only, with no caller-controlled address.


Core 0.7.2 emits snapshot version 2 with 128 bytes (32 big-endian words).
Words 0..15 retain their meanings. Words 16..21 are accepted availability mask,
final native selector vector type mask, final vector row count, vector build
count, machine-name lookup mask and category lookup mask. Remaining words are
zero. The SDK reads versions 1/64 bytes and 2/128 bytes. The observer runs after
native vector sorting and never modifies the vector or its entries.

From core 0.7.3, bank loading is silent. Word 15 (the retained loading-mods
paint counter) is always zero; the snapshot layout remains compatible.
