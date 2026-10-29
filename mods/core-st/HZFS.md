# Long samples in the core (0.5)

HZFS v1 stores named long samples in the high eMMC zone. It is implemented
in `hzfs.c`, `high-storage.c` and `high-storage.s`, built with the ordinary
core manifest. It works without a machine bank. No filesystem code, catalogue
or sample bytes are added to SMZ4; that optional file carries code and metadata.

The code occupies the core's existing 64 KiB reserve. Temporary catalogue and
IO buffers use the native MAIN heap and are freed after each operation.
There is no AUDIO change for this storage feature. Core 0.6 supplies playback/pool assignment as a separate generic consumer;
see [SAMPLE-POOL.md](SAMPLE-POOL.md).

## Regions and bounds

| eMMC bytes, exclusive end | Owner |
|---|---|
| `00400000..02400000` | 64 native sample slots, 512 KiB each |
| `02400000..03400200` | `/mods` payload and its length record |
| `0f400000..10000000` | Native saved/temporary state |
| `10000000..30000000` | Native projects |
| `30000000..30080000` | HZFS catalogue A |
| `30080000..30100000` | HZFS catalogue B |
| `30100000..card end` | Contiguous long-sample extents |

All HZFS IO checks the actual card sector count. Only complete 512 KiB
windows are allocatable, and byte addressing is limited to the first 4 GiB.
A 1,864 MiB card has 1,095 MiB for sample extents after the two catalogues.
There is no partition table or FAT formatting operation.

At the application startup hook, before machine-bank and native project/sample
loading, the core opens the catalogues. If neither is valid, it scans both
**entire** catalogue windows. Only a zone consisting entirely of `00`/`FF`
is initialized. Unknown data, corruption, allocation failure and read failure
never authorize formatting. A single valid copy is enough to mount; conflicting
valid copies with the same sequence, or CRC-valid inconsistent extents, are errors.
Boot continues normally if storage initialization fails; `core_hzfs_status`
reports the result to other mods. It does not show a new boot dialog.

## Stock sample protection

The stock upload command converts audio to 48 kHz mono PCM16BE, then crops to
**240,000 frames / 5 seconds / 480,000 PCM bytes**. It rebuilds the 64-byte
SAMP header, native Data length and CRC **after** cropping. Longer samples
must use `put` instead of a stock slot.

An independent `ev_fs_write` guard rejects native stock-slot writes whose
body exceeds 480,064 bytes, or whose destination is not a slot boundary,
**before the native stream is created or sample sectors are written**.
The check covers every stock slot, including slot 64 at `02380000`.
Even a raw 512 KiB object is rejected there. No script option disables the
limit. This prevents a stock upload from spilling into `/mods` at `02400000`.

## Disk format

All integers are big endian. A block is a 512 KiB window; A/B use blocks
`600`/`601` and extents begin at block `602`. Each catalogue begins with:

| Offset | Field |
|---|---|
| `00` | `HZFS` |
| `04` | version 1 |
| `08` | sequence, starts at 1 |
| `0c` | number of entries |
| `10` | block bytes `80000` |
| `14` | first data block `602` |
| `18` | complete card windows |
| `1c` | next monotonically increasing file id |
| `20` | CRC32 with the CRC field zeroed |
| `24` | logical catalogue bytes, `140 + count*40` |
| `40..140` | 64 reserved association ids; retained on updates, cleared on deletion |
| `140..` | 64-byte entries |

An entry contains eight u32 words (`id`, first block, block count, logical
bytes, frames, body CRC32, creation time, flags), then a 32-byte UTF-8 name.
CRC uses `zlib.crc32(bytes, 0xffffffff) & 0xffffffff`, including sample bodies.
The catalogue supports up to 8,187 entries. Each file is a stock-layout
64-byte `SAMP` header followed by PCM16BE mono at 48 kHz; it has no stock
five-second duration limit. The native transport envelope is not stored.

A put allocates extents free in **both valid catalogues**, writes the sample,
reads it back, then writes and verifies the older/invalid catalogue with the
next sequence. Until publication the selected catalogue remains intact.
Deletion removes the entry and its reserved associations, then commits the
catalogue. PCM is left in place. An extent still referenced by the other
valid copy remains reserved until that copy is replaced by a later commit.

## Native Data transport and SDK

Raw high-zone windows use `/samples/(8185+k)`, for `600 <= k < 2000`.
The core computes and checks the address directly; it does not extend the
stock slot table or depend on integer overflow. A read returns at most one
window; a write may cover multiple windows but cannot leave the high zone
or pass the card boundary. Only native read/write permissions are enabled.
The extension does not add files to Elektron Transfer's native directory list.

The PC tools are in `elekloader/sdk/syntakt/`, alongside the machine and firmware
builders. Live commands require `python-rtmidi`; importing the tools opens no
ports. Close another application using the MIDI port before explicit transfers.

```sh
python -m elekloader.sdk.syntakt.long_samples ls
python -m elekloader.sdk.syntakt.long_samples put pad.wav --yes-write-device
python -m elekloader.sdk.syntakt.long_samples get pad --out pad-back.wav
python -m elekloader.sdk.syntakt.long_samples rm pad --yes-write-device
python -m elekloader.sdk.syntakt.long_samples fsck --deep
python -m elekloader.sdk.syntakt.long_samples stock hit.wav --slot 64 --yes-write-device
```

`init --yes-write-device` is idempotent and has the same blank-zone restriction
as the core; it cannot force-format damaged or occupied metadata. `put` also
ensures an empty filesystem exists. Names must be 1..32 UTF-8 bytes; duplicate
names are rejected. `get` checks SAMP metadata and CRC before creating the WAV.
WAV conversion accepts PCM8/16/24/32, float32 and extensible WAV, mixes channels
to mono, and resamples with Hermite interpolation. Sources above 48 kHz should
be prepared with a proper anti-aliasing resampler before transfer.

The Data client checks destinations before DataWriteOpen, sends the native
12-byte footer in its own partial, checks acknowledgments and requires successful
DataWriteClose. It does not retry writes or publish a catalogue after a failed
transfer. Commands use only Data RPC `54..59`, including deletion via catalogue
updates. No firmware-upgrade or native project operations are exposed here.

## API and verification

`elekloader/sdk/include/syntakt/storage.h` exports open/close, enumeration,
ensure, streamed put/get and remove. Pass `&core_hzfs_native_io` on MAIN.
Operations are task-only and must be serialized with Data RPC transfers.
The API stores bodies in bounded 4 KiB chunks; catalogues can temporarily take
about 1 MiB of heap plus validation/transaction scratch space.

Offline checks use the actual C on a native 32-bit host with explicit block-IO
doubles, and Python protocol fixtures. They cover CRC compatibility, long
file readback, first-use initialization, corruption, failure before publication,
two-copy allocation, deletion and stock slot/card bounds. These fixtures do
not execute ColdFire instructions or the native firmware stream. The builder
audits the generated instructions and verifies protected firmware sections.
Hardware transfer/boot behavior of this port remains to be confirmed.

## Recorder coordination (core 0.11.0)

The SMZ recorder borrows the existing validator, IO and CRC through UI API 5's
host table. It reserves extents against both validated catalogues, writes and
verifies PCM in bounded chunks, then publishes the older catalogue. Pool
application uses the core's existing AUDIO synchronization/rollback service.

Native high-zone descriptors now have a completion capture. Its callback is
a no-op, but its presence makes stock DataWriteClose enter the existing
FS-written event path. There is no additional close hook. Raw IDs use the shared
`ST_HZ_RAW_FIRST=8185` base, with the first high-zone ID at `8185+600h`.

The recorder's existing FS-write/FS-written subscriptions coordinate an atomic
save lease with native high-zone uploads. A save rejects a competing upload;
an active upload defers a save. An aborted upload remains conservatively busy
until a successful high-zone transfer completes or the device reboots. Lease
publication alone excludes interrupts; block IO, allocation and normalization
run with interrupts restored. Physical transfer/concurrency testing is pending.
