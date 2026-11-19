# The AUDIO CPU's memory (OS 1.42)

What the stock AUDIO image (section 7 of the OS file, loaded at `40000400`)
puts where, traced from its code, and what the core adds (core 2.2 layout).
Every stock fact
below names the instruction that shows it. `tools/study_st_audio_memory.py`
checks each of them against the stock file and repeats the trace:

```sh
python tools/study_st_audio_memory.py --stock "$SYNTAKT_STOCK" --out out/audio-memory.json
```

The trace starts from the entry, the vectors, every code pointer an instruction
takes, the image's pointer tables and the function prologues. Any start whose path
meets a word that is not a ColdFire ISA_C/EMAC instruction is dropped. It reaches
11,477 instructions, 38,898 of the 39,704 bytes from `40000404` to `40009f1c`.
The rest of the image is data. It then lists every absolute address that the
reached code names. The linear disassembly decodes the image's float tables as
code, and its "addresses" above `4404f980` come from those tables. The six
stretches it does not reach (806 bytes: the serial-flash read at `40000b92` and
its byte routine at `40000406`, `40001708`, `40004534`, `40004608`, `400048c6`)
name SRAM, `4004f6ec`, the sample store and peripherals only.

## The map

| Address (AUDIO) | Size | Use | Shown by |
|---|---|---|---|
| `00000000`–`00000fff` | 4 KiB | The shared window with MAIN (FlexBus CS0), [MACHINE-REFERENCE.md](MACHINE-REFERENCE.md), "The shared window". AUDIO reaches it through the pointer at `4004f6e0`, which stays zero. **Core:** stereo tracks at `5b0`–`7ef`; after boot the pool and System info mailbox at `800`, extended parameters at `c00` and `e00` | `40000e4c` |
| `40000000`–`400003ff` | 1 KiB | The vector table (VBR = `40000000`, from `4004005c`). Boot points all 256 vectors at an `rte` (`40000404`), then vector 96 at `40000442`, and at the end of boot at `40000934`, the block interrupt | `40000dbe`, `40001064` |
| `40000400` | 4 bytes | The image's first word: its entry, `40001070` | |
| `40000404`–`40009f1c` | 39.7 KiB | Code | the trace |
| `40009f1c`–`40040850` | 218 KiB | Tables (wavetables, float tables, the machines' update/render pointer tables such as `40014900`), and a few words the code reads or writes: `40040054`, `40040058` (the entry's argument, `40001074`), `4004005c` (the VBR) | the trace |
| `40040860`–`40041050` | 2 KiB | Zeros in the stock image. **Core: its AUDIO services** (the companion in section 7: `elekloader/sdk/syntakt/audio.py`, `CAVE`, `TABLES`, `UPPER`) | |
| `40041060`–`40041e67` | 3.5 KiB | A packed stream (3,583 bytes after an 8-byte header, the depacker at `400005a2`) | `40000e00` |
| `40042000`–`4004f588` | 53.4 KiB | Zeros in the image; at boot the stream above is unpacked here (54,664 bytes), then sent out over GPIO by `40000706`. It is probably the FPGA's configuration | `40000dfa`, `40000e6a` |
| `4004f6e0`–`4005df10` | 58 KiB | The image's tail: SRAM's initial contents, copied at boot (`4004f6e0`–`40057670` to `80000000`, `40057670`–`4005df10` to `80008000`; the rest of each SRAM half is cleared). This range is then cleared with the rest of the zeroed data | `400004bc`–`40000510` |
| `4004f6e0`–`4404f980` | 64 MiB + 672 bytes | **The zeroed data**, cleared at boot by `40000512` (16 bytes at a time). Nothing in the image is placed above it | `4000051a`, `40000520` |
| `4004f6e0`–`4004f750` | 112 bytes | Variables: `f6e0` window pointer (0), `f6e4` boot state, `f6e8` DMA in flight, `f6ec`, `f6f0` | |
| `4004f750`–`4404f750` | 64 MiB | **The sample store** (cleared again by `40000d7e`, 64 MiB). Each 2 KiB page from MAIN goes to `4004f750 + index × 800`. The core divides it as the three rows below | `40000fa8`–`40000fc2` |
| `4004f750`–`4204f750` | 32 MiB | Stock samples: MAIN's aggregate of slots 1 to 64, page index `0` to `3fff` at most (64 five-second slots: 29.3 MiB, [STOCK-SAMPLE-CORE-OVERLAP.md](STOCK-SAMPLE-CORE-OVERLAP.md)) | |
| `4204f750`–`4244f750` | 4 MiB | **Core: the SMZ4 bank's AUDIO section**, the machines' and modules' AUDIO code, page index `4000` to `47ff`. A 16-byte header `{SMA3, count, bytes, Adler32}`, 16 bytes a machine `{type, update, render, 0}`, 384 bytes of extended parameter state, then the linked code, data and state, padded to 2 KiB pages ([mods/core-st/SMZ4.md](../mods/core-st/SMZ4.md)). The bank builder refuses more (`elekloader/sdk/syntakt/smz.py`, `AUDIO_MAX`); 723,120 bytes before core 2.2 | |
| `4244f750`–`4404f750` | 28 MiB | Free (page index `4800` to `7fff`). The pool's PCM was here, at `42101750`–`4404f750`, before core 2.2 | |
| `4404f750` | 4 bytes | Cleared by the vector-96 handler `40000442` | `4000044e` |
| `4404f754`–`4404f954` | 512 bytes | The sample table: 64 entries `{length, address}`, built by `40000d06` by walking the store's headers | `40000d2a`, `40000d30` |
| `4404f954`–`4404f980` | 44 bytes | Variables (`f954`, `f958`, `f95c`) | `4000254a`, `4000197a` |
| `4404f980`–`4404ff50` | 1,488 bytes | Unused: the rest of page `8000`, which no page may be sent to | |
| `4404ff50`–`44050f50` | 4 KiB | **Core: the pool service**, its code, channel table (`+b80`) and 128 PCM descriptors (`+c00`), page index `8001`–`8002` ([mods/core-st/SAMPLE-POOL.md](../mods/core-st/SAMPLE-POOL.md)) | |
| `44050f50`–`47feff50` | 63.62 MiB | **Core: the pool's PCM**, the long samples, page index `8003` to `ff40` (66,711,552 bytes) | |
| `47feff50`–`48000000` | 64 KiB | Left to the AUDIO stack | |
| down from `48000000` | about 3 KiB | The stack (SP = `48000000`, set by the entry). After boot the main context idles (`bra .` at `4000106e`), and everything runs in the block interrupt: 3,140 bytes at most along its calls, counting each indirect call as reaching any routine of the pointer tables, and every push as made once | `4000107a` |
| `80000000`–`8000ffff` | 64 KiB | AUDIO's SRAM, [MACHINE-REFERENCE.md](MACHINE-REFERENCE.md), "AUDIO's SRAM". **Core:** FM TONE's scratch `8000e8a0`–`8000f397`, the stereo tracks' `8000f398`–`8000ffff` | |

ACR0 is `4007e020` (`400010d4`): `40000000` to `47ffffff` are cached, so the
AUDIO CPU's DDR is 128 MiB, as MAIN's is. The DDR controller is set up before
this image runs, by the bootloader, so the size here comes from the cache window
and the stack, not from the controller.

Rows marked **Core** are Synfact's, from the core's own pages; the rest are the
stock image's, from the trace. The core's code also patches a few stock sites in
place (the base-width filter, `audio.py`'s `BW_SITES`) without using new memory.

Above `4404f980` the stock image places nothing: no reached instruction names an
address there, no pointer in the image points there, and there is no allocator
(the zeroed data's end, `4404f980`, is named once, by the clear). Core 2.2 puts
the pool there (`tools/probes/high-memory-probe-st` checks it on the unit).

## The page receiver

MAIN fills the sample store page by page. The stock sender (`40082f90` in MAIN)
writes a page's index as a 16-bit word at window `0x00c` and the 2 KiB page at
`0x800`. AUDIO copies the page to `4004f750 + index × 800` (`40000fa8`–`40000fc2`)
without bounding the index. Index `0` to `7fff` is the 64 MiB store. Index
`8000` is the page that holds AUDIO's own variables and sample table
(`4404f750`–`4404ff50`), and must never be sent. Index `8001` to `ffff` reaches
`4404ff50` to `4804f750`. The stock sender sends only pages `0` to
`ceil(aggregate / 2048) - 1`.

So the 63.7 MiB above the zeroed data can be filled with the stock receiver as it
is. A page there must end below the stack. Keeping the top 64 KiB for the stack,
that is page indices `8001` to `ff40` (32,576 pages, 63.6 MiB): core 2.2's pool
service and PCM.

## Not established

- **That nothing writes there at run time.** The trace covers the static
  references. A pointer that the code computes from data it is given could still
  land there: the sample table, built from the store's headers, or anything that
  MAIN's frame carries. A corrupt header could make the sample table point past
  the store.
- **That the memory is there, and the stack's depth.** The stack at its top and
  the cache window say that AUDIO has 128 MiB; the bound above is static, and the
  core's and the bank's AUDIO code run in the same interrupt, on the same stack.
  `tools/probes/high-memory-probe-st` (with core 2.1.1, before the pool moved)
  fills `4404ff50`–`47ff0000` with a pattern and checks it during normal use, and
  measures the deepest stack word reached below `48000000`.
- **OS 1.41.** Only 1.42 is traced; the tool refuses another image whose facts
  differ.
