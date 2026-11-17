# Native stock samples overlap the resident core

Investigation of remote main `3c0cdcb` (OS 1.42), with the official 1.41
constructor checked independently. Core 2.0.1 removes the MAIN aggregate and
streams samples instead. The original failure and its reproduction are
recorded below, followed by the fix and validation scope.

## Finding

Individually valid samples in native slots 1..64 can overwrite Synfact's
resident MAIN core. The native aggregate blob starts at `4d84f2c5`, the
uncached alias of physical `4584f2c5` in the 128 MiB DDR model. Both OS
versions use that same address. Synfact places 64 KiB of code/data at
`46000000..46010000`, inside the native blob's possible span.

There are `8064315` bytes (`7.690730 MiB`) between the blob start and the
core. The blob contains 64 four-byte frame counts plus two bytes per PCM
frame. It does **not** retain the 64-byte on-disk SAMP header for each slot.
Missing samples still contribute a four-byte zero frame count.

With mono PCM16 at 48 kHz, the first overlapping frame is aggregate frame
`4032030`, or `84.000625` seconds, including the 256 bytes of frame counts.
The total blob size at that boundary is `8064316` bytes: one byte of core
overlap. The previous frame is safe. This arithmetic assumes the complete
64-slot blob; write timing within the builder depends on slot order.

| Native sample contents | Blob bytes | Overlap of the 64 KiB core window |
|---|---:|---:|
| 16 samples of 5 s, 48 empty slots | 7680256 | none |
| 17 samples of 5 s, 47 empty slots | 8160256 | all 65536 bytes |
| 64 samples of 5 s | 30720256 | all 65536 bytes |

The full legal 64-slot blob is `29.297119 MiB`, ending at physical
`4759b3c5` (exclusive). Thus limiting each slot to five seconds cannot make
the existing core placement safe. This is a collision introduced by the
resident allocation; the reproduction does not establish a stock-OS crash
without Synfact code in that window.

## Why the existing guards do not prevent it

- `high-storage.c:core_sample_write_guard` protects each on-disk 512 KiB
  slot and the following `/mods` area. It accepts a 480064-byte SAMP body
  (64-byte header plus five seconds of PCM), as intended.
- `sample-hooks.s:core_pool_blob` first calls the original native blob
  builder (`400822bc` in 1.41, `40082334` in 1.42). The native builder
  concatenates all 64 samples into the fixed MAIN buffer.
- Only after that returns does the hook call `core_pool_prepare`. Its
  frame/bounds checks parse an **already constructed** blob. The aggregate
  size check (`1fff100`) also exceeds the distance to the resident core.
- Link-time core size accounting checks the 64 KiB allocation itself; it
  does not prove that a native runtime writer will leave that memory alone.

## MAIN to AUDIO loading

The native 1..64 path constructs the complete aggregate in **MAIN RAM
first**. It uses a temporary 512 KiB buffer to read one sample at a time
and copies its frame count and PCM into the retained aggregate. After the
builder, boot starts the sender tasks. The stock sender (`40082f90` in
1.42) copies 2048-byte pages from the MAIN aggregate through FPGA mailbox
`10000800`; AUDIO receives them into its own sample memory. The full MAIN
copy remains available to the waveform code (`core_pool_sample_source`).
This native path is not a direct eMMC-to-AUDIO stream.

HZFS long samples use a different path. `core_pool_prepare` plans extents,
and `core_pool_boot_page` reads eMMC on demand into small MAIN scratch
buffers as the sender requests pages. Those samples are streamed to
AUDIO's separate `42101750..4404f750` PCM region; complete long samples are
not accumulated in MAIN. Runtime HZFS loads likewise stream through the
checked DATA/COMMIT protocol.

## Reproduction and scope

```sh
python tests/test_st_stock_blob_emu.py \
    --stock "$SYNTAKT_STOCK_141" --stock "$SYNTAKT_STOCK" \
    --out out/sample-memory/reproduction.json
```

The test hash-verifies both official inputs. Unicorn executes each OS's
actual constructor, fixed-buffer getter, memcpy and memset. The cached and
uncached buffer windows share host memory, modelling 128 MiB DDR aliases.
Sample-read and malloc/free calls are fixtures. Samples use SDK-generated
mono SAMP metadata and PCM, within the per-slot limit; the native eMMC/file
parser is not executed. A 64 KiB sentinel represents the resident core.

Five cases per OS check 16, 17 and 64 five-second samples, plus the exact
last safe and first overlapping frame. Assertions compare the entire native
output blob and the entire core window to independent expected byte strings.
The report records the real native write PC, originating slot and alias
address of the first write overlapping the core, including writes that
begin just below the boundary.

All ten cases passed. In the 17- and 64-sample cases, the first overlapping
write occurs while copying slot 17: native memcpy PC `40115f94` in 1.41,
`40116158` in 1.42. The four-byte store starts at alias `4dfffffd`
(physical `45fffffd`) and straddles the first core byte. The reproduction
also verifies complete native output even after the core sentinel is lost;
it does not execute the overwritten sentinel as code. The unchanged engine
unit suite passed 39 cases and the storage suite passed 13 cases.

Both reported addresses `460001d0` and `460001d6` lie in the overwritten
window. The report includes their resulting bytes. This confirms they can
be corrupted by PCM; it does **not** identify their original linked
instructions, reproduce an exception at either PC, or prove the reported
formatting symptoms have this cause. There is no full boot, AUDIO execution,
device access or hardware validation in this test.

## Core 2.0.1 fix

The boot hook no longer calls the native aggregate builder. The core reads
one sector per stock header, validates SAMP and at most 240000 mono frames,
and records the same 64 frame counts and CPU2 offsets. Each native sender
page is generated on demand from those headers and PCM sectors. The virtual
concatenation is byte-identical to the old blob, including zero-length slots,
headers split across pages, PCM boundaries and zero-filled final page padding.
Missing or oversize headers become empty slots before any PCM planning.

The original sender loop, page indices, progress queue, FPGA handshake and
completion state remain. Stock, optional machine and HZFS/service pages keep
their existing wire regions. There is no 84-second aggregate limit: all 64
five-second samples occupy 30720256 virtual bytes and 15001 sender pages.

Only 5120 bytes of MAIN PCM scratch are allocated (2048-byte output page and
3072-byte sector-read buffer, shared by stock and HZFS). A task-only hook at
`40083026` frees this allocation after the final memcpy into the FPGA mailbox,
before the completion kick. No further MAIN buffer read is needed by AUDIO:
the mailbox holds its own copy. Release is idempotent and clears the retained
pointer before freeing it. Incomplete transfers keep the small buffer alive;
they never permit premature reuse. Pool metadata stays available for runtime
mapping and HZFS transactions, with no complete PCM copy retained in MAIN.

The initial prototype accumulated stock envelopes during streaming, using the
96-bin cache. The final release defers this work, as described below. Allocation failure
leaves waveform caching optional; failure to allocate the plan/scratch sends
an empty stock table. A failed PCM read sends zeroes for that span, increments
the error counter and keeps that waveform unavailable. The old builder is
never a fallback. This also works without the optional bank or AUDIO pool
service.

The native heap is only 16 MiB (its stock allocator configuration), so a
heap-allocated 29.3 MiB aggregate would not solve the full-capacity case.
This change neither expands that heap nor claims that the old fixed blob
region has been added to it. The original 64 KiB core reserve and compiler
flags are retained. Task-only pool copies use exported native memcpy/memset
instead of replicated inline loops to stay within the resident budget.

### CPU2 descriptor correction

The 1.42 port had changed MAIN's pool descriptor base from `4004f750` to
`4004f7c8`, a 120-byte shift. AUDIO is identical across 1.41/1.42, and its
native receiver (`40000fba`) and parser (`40000d06`) still use `4004f750`.
Core 2.0.1 restores that AUDIO base; native TwinShot and pool references now
name exactly the same stock PCM. The AUDIO firmware itself is unchanged.

### Fix regression

```sh
python tests/test_st_stock_stream_emu.py --core "$BUILD" \
    --out "$BUILD/stock-stream.json"
```

This executes the linked boot replacement through its native task-start
boundary, then the original MAIN sender loop, actual target stock page and
wave code, native memcpy/memset and the task completion hook. The receiver
fixture copies mailbox pages to a separate AUDIO instance using its native
wire mapping, then executes the native CPU2 stock descriptor parser. All
30720256 bytes must match an independent reference, every native and pool
descriptor must agree, and core function bytes must remain intact. The
scratch allocation must be freed exactly once; its memory is poisoned before
post-load waveform use. Additional cases cover split boundaries with machine
pages and an unavailable AUDIO pool service.

Disk IO, native malloc/free, semaphore wakes, UI notifications and GPIO/page
delivery are explicit boundaries. This is not a full RTOS boot, machine/DSP
initialization, USB test, physical validation or boot-time measurement.
`test_st_pool.c` independently runs production host C over all 64 full slots,
exact envelope peaks, retries, invalid/empty headers, read/allocation failures,
post-release consumers and HZFS mono/stereo paths. Existing common regressions
cover safe bank startup, ownership, runtime publication and compatibility.

The initial eager-envelope prototype used **64316 / 65536 resident bytes**,
with **1220 bytes free**. Packaging/lint and the 1.42 release check pass 217 exact preimages,
all protected sections, in-place decompression margin and the complete bank
contract. Core AUDIO is byte-identical to the original 2.0.0 baseline; core
and bank AUDIO also match the preceding prototype. The storage suite passes
13 tests; JavaScript passes 62 with one optional precompiled browser-core
reference comparison skipped. Target streaming passes all three cases above.
The full common regression runner passes **92 checks**, including real
LFO3/MSKIP installation, startup after native clearing, native routes/ABI,
pool runtime paths and the complete stock transfer. Its only skipped check
is the optional historical FM source comparison (`--synfact` not supplied).
The modulation checks use a Unicorn build with ColdFire MAC-with-load support.

## Deferred waveforms in core 2.0.1

The user confirmed that the initial streamed prototype boots with all 64 five-second slots loaded and
that the earlier core crashes with the same contents. They requested the
fastest startup, accepting deferred waveform display.

Core 2.0.1 removes all stock and HZFS envelope begin/feed/finish calls from
the boot path. Boot performs no waveform-cache allocation. HZFS's original
full CRC validation stays in that path; its data-integrity check is independent
of drawing envelopes. The sender's pages, payload bytes, wire indices, ACKs,
completion hook and 5120-byte scratch release are unchanged from that prototype.

When a waveform UI requests `wave_idle`, it reads at most 4096 PCM bytes in
one task slice, accumulates min/max peaks and releases its 5120-byte scratch
allocation before returning. A complete envelope remains in the existing
MAIN cache; subsequent visits need no reads. LOADING WAVE remains visible
until the exact envelope is complete. Runtime HZFS imports can still populate
that cache during their explicit transfer.

The pool retains the original disk extent/CRC metadata for loaded HZFS slots,
including runtime imports and aliases. Lazy HZFS scans verify their SAMP header
and full saved CRC before becoming ready. A changed stock file or a stock
span that failed boot IO is barred from lazy reads, preserving the identity
of the original AUDIO PCM. IO/allocation failures leave a waveform unavailable
and retryable; they do not alter playback. UI reads recheck source identity
after disk IO and do not reset a concurrent runtime transfer's waveform worker.

The same 64-slot target sender/parser regression requires zero waveform
builder calls and no cache allocation during boot, then exercises lazy stock
generation after scratch release and verifies cached read avoidance. Host C
also covers lazy mono/stereo HZFS, CRC mismatch/retry, partial scans interrupted
by stock writes and all full-size stock samples. Hardware boot-time improvement
has not been measured; unchanged wire protocol does not imply identical
effective throughput when MAIN's per-page preparation differs.

The 2.0.1 package passes 92 common regression checks (only the optional
historical FM comparison is skipped), 217 release preimages/protected-section
checks and 13 storage tests. Resident use is **65116 / 65536 bytes**, with
**420 bytes free**. Core and bank AUDIO remain byte-identical to the streamed prototype.
