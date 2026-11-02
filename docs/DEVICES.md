# The Syntakt's profile

Everything the engine knows about a product is in one profile,
`elekloader/devices/<device>.py`, registered in `devices/__init__.py`
(`_all()`). The Syntakt's is `syntakt.py`. The engine comes from elekloader
and still holds its other devices' profiles; how they were found, and how a
new device is added, is in elekloader's own
[DEVICES.md](https://github.com/irpina/elekloader/blob/main/docs/DEVICES.md).
The table's last column says how each field was found for the Digitakt mk1,
the first device elekloader supported.

| field | what it is | how it was found for the Digitakt mk1 |
|---|---|---|
| `releases` | each supported stock OS: its `.syx` sha256, and its main OS section's sha256 and length | hash the file, and the section depacked |
| `sysex_id` | the device byte of its OS messages | the framing messages (`0x0A`) |
| `main_section`, `main_load` | which container section is the main OS, and where it runs | the section table (id 3, `0x40000400`) |
| `stage` | where the bootloader stages the main OS before unpacking it in place | the bootstrap's loader (`0x800005fe`: read to `0x40200000`, unpack to `0x40000400`) |
| `flash_at`, `flash_limit` | where the container sits in flash and must end | the bootstrap (`0x80000` .. `0x380000`) |
| `trailer` | how the file is sealed: `None`, or `'hmac'` | mk1 has none |
| `hmac_key_from` | for `'hmac'`: the section and seed string the key is derived from (never the key itself) | none on the mk1 |
| `isa` | the main CPU's instruction decoder, for the boundary checks | ColdFire (`isa/coldfire.py`) |
| `areas` | memory that is free at run time: where a mod's regions may lie | the research on what the OS never touches |
| `ddr`, `sram_code`, `fast_table` | where the linker puts mods' code and data, fast code, and the table that copies it | the areas above |
| `core_ddr` | from a core's version on, the linker's area in place of `ddr` | core 4.0: `0x47BC0000-0x47C00000`, the gap between the sample pool's end and the audio input ring |
| `recovery` | how to get back to stock, shown to the user | FUNC at power-on |
| `container`, `version_len` | the file family (`'ele3'`, `'elek'`) and the version field's length | the container header |
| `terminal_zero_packet` | include a full zero packet when preamble + container is exactly 101-aligned; enforce that padding when reading back | Syntakt 1.41's byte-exact stock transport |
| `secondary` | explicitly requested companion sections: original hash, fixed length, load address and allowed patch windows; no boot sections | Syntakt AUDIO telemetry only |
| `protected` | main OS ranges no mod may change, with the reason | none on the mk1 |
| `relocatable` | data inside a protected range that the OS reaches only through the listed 4-byte operands and never writes, so a mod may serve its own copy through them (sdk.octabam does, for a poke there) | none on the mk1 |
| `blob_max` | a cap on the appended blob, if the DDR areas do not give one | none on the mk1 |
| `image_free` | zero runs inside the main OS that `fixed` code may take (sdk.build) | none on the mk1 |
| `reserve` | (unit, {name: (a, b)}): for a core that sizes its RAM reserve to the mods, the symbols the linker defines from n, the units the mods' RAM fills: a + b·n each | none on the mk1 (its `ddr` is free RAM) |
| `dsp_payloads`, `dsp_areas` | DSP code the main OS uploads at boot (tag: address, length), and the P words of a payload a mod frees by claiming a name, where the linker places DSP code (docs/FORMAT.md, "DSP code") | none on the mk1: its OS has no DSP code |
| `toolchain` | the compiler, assembler and flags the SDK uses | the CFW's build |

## The Syntakt (1.41)

The experimental profile `devices/syntakt.py` and `mods/core-st` use the
shared UI hook bus and SETTINGS implementation. Core 0.2 adds MAIN render
events and idle accounting. `NO_DTIM0` leaves MAIN's stock timer alone.
Core 0.3 adds native Data RPC extension events and a single opaque `/mods`
file (16 MiB maximum) in eMMC at `02400000`, followed by a separate length
record sector. [Storage contract and sources](../mods/core-st/STORAGE.md).
The explicit [System info build](../mods/system-info-st) adds a small,
read-only telemetry companion on AUDIO; the ordinary patch CLI stays MAIN-only.

- **Pinned input.** Official OS SHA256
  `8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e`.
  MAIN SHA256
  `8cf2b4b4ea80d58953da46ca453328454fb3fca5cb5f844a2f7bad4b72241f53`,
  3,438,480 bytes at `0x40000400`.
- **Files.** Device byte `0x16`, ELE3 order 5, 2, 1, 3, 4, 6, 7, 8,
  no trailer. Ordinary builds change section 3 only. The System info build
  explicitly requests section 7 as well. Sections 1, 2, 4, 5, 6 and 8
  must stay byte-identical. The version is optional: omitting it preserves
  the official header's `1.41`.
- **Transport.** Preamble + stock container fills 20,088 chunks of 101
  bytes, followed by a complete zero chunk. `terminal_zero_packet` models
  this convention explicitly. The writer must reproduce the whole stock
  SYX byte for byte; the verifier rejects missing, nonzero or extra padding.
  Other devices retain their previous padding policy.
- **Memory.** Native SRAM copy sources end at `0x40347b90`; native BSS
  clearing ends at `0x45565490`, below the stack at `0x48000000`. The mod
  window is 64 KiB at `0x46000000`, following SyntHack-OS's documented
  `mods/modarea` (reference revision
  `b536fa598cc2587d718905144e4c597947a5770c`). The stock copy and clear
  routines are unchanged; elekloader's normal boot copier runs before
  them and places the core in that window.
- **Staging.** Bootstrap and updater stage MAIN at `0x40200000`.
  In-place decompression must not catch unread input. Package size and
  decompression margin are separate constraints.
- **Packaging budget.** Container at flash `0x80000`. For this first
  integration, `flash_limit` is deliberately `0x80000 + 2,028,880`: no
  package larger than the known official container. This is a build
  policy, not a measured flash or receive limit. The built-in compressor
  makes a larger container and verification refuses it. Compact packages
  produced with elektron-firmware-tool have loaded on a unit; see the
  external packaging step in [core-st](../mods/core-st). Integrating a
  compact compressor is separate work.
- **Event IDs.** Native `KeyEvent` IDs differ from button-name indices.
  YES=12, NO=13 and FUNC=1; the full mapping relevant to mods is in
  [core-st](../mods/core-st). Hardware reports flags `01` (press), `09`
  (held repeat), `10` (release); all nine encoder turns have worked in the
  diagnostic mod. A package loading does not validate the entire update
  path or every byte of the reserved RAM window.

## A new OS version of a supported device

A new Syntakt OS would be added as elekloader added Digitakt mk1 1.54 and
Digitone mk1 1.44 (2026-10-01):

1. **Compare the file with the last release's.** Every section but the main
   OS (and section 5, the version string) must be the same bytes: the same
   bootstrap and updater, so the same staging and flash layout. For both
   releases that held. If the bootstrap changes, check `stage`,
   `flash_limit` and the recovery path again.
2. **Add the release** to the profile: the file's sha256, and the main OS's
   sha256 and length, depacked. The writer must reproduce the file from its
   own main OS stream, and its main OS must depack in place from `stage`
   (`tests/test_releases.py` checks both for every release given).
3. **Check the memory facts** the profile states: here, the same SRAM
   operands in the code, and no operand near the DDR area mods run from.
   Both releases moved their RAM past the image by 0x1000 and changed
   nothing else there.
4. **Port the core**, under `ports` in its `mod.json` (elekloader's ADAPTING.md 3.10),
   and every mod that names a firmware address:
   every `defsym` and site found again in the new image, and each site's
   stock bytes read from it. `tests/test_releases.py` builds and lints the
   core for every release it names.
5. **Run a build with the core in the emulator** against the new stock
   file. Digitakt mk1 1.54: core 2.1 passes, with every screen identical
   and the audio identical apart from the recording's silent end, 1 ms
   off, exactly as core 2.1 does on 1.53. Digitone mk1 1.44: not yet,
   since the emulator does not boot stock 1.44 to a settled screen.
6. **Tell mod authors** to build their mods for the new OS. A mod for the
   old one is refused with the new stock file.
