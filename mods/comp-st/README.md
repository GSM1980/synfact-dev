# COMP (comp-st 0.2.0)

Compact stereo-linked compressor on the **Syntakt FX track bus only**, for
OS 1.42 and core 2.4.0 / host API 16. It scales the two native FX VCA controls
at `400905b6`, before the original clamp and stores. Direct track outputs
and the MAIN stereo mix are not processed. The fixed detector reads the FX
ADC pair `80003c50/80003cd0`; no SCS or SCF is implemented.
The detector/envelope are digital; gain acts through the left/right FX AMP
VCAs, after DRIVE/filter. This is not a PCM sample-multiplication loop.

Select the FX track, open SYN, then its second page, **COMP** (DRIVE stays
first). Encoder positions follow Digitakt:

| A | B | C | D | E | F | G | H |
|---|---|---|---|---|---|---|---|
| THR | ATK | REL | MUP | RAT | | | MIX |

The six labels, defaults, ranges, dial animations/masks and MUP/RAT value
strings are extracted from the owner's original **Digitakt OS 1.54** SysEx.
RAT has 1.50, 2.00, 3.00, 4.00, 6.00, 8.00, 16.00 and 20.00; MUP displays
0.0..24.0. MIX defaults to zero, so existing projects start with COMP off.
Turn H up to enable it. SCS/SCF positions F/G remain empty. These settings
are Kit-wide; this version does not add lock lanes or LFO destinations.

The **GR meter** follows Digitakt 1.54's native draw (`40038f80`): half-dB
steps, 50 pixels over 31.5 dB, growing down from LCD y=50. Its original x=126
would overlap Syntakt's page indicators (x=125..127), so COMP draws a two-pixel
strip at **x=122..123, y=1..50**. MUP/MIX graphics end at x=119, with empty
gutters on both sides of the meter. The existing smoothed DSP publication
drives it (immediate rise, about 20 dB/s release). The UI tick invalidates only
a visible COMP view whose pixel height changed; audio never requests a global
redraw. The native view paints the six controls once, then COMP overlays the
bar. MIX=0 displays no reduction. Only the bank changes from 0.1.1; the
accompanying core 2.4.0 SYX is identical.

The DSP is adapted from Angelo-politek's MIT
[Synthack-OS master-comp](https://github.com/Angelo-politek/Synthack-OS/tree/c12cf23ad88e68d3f85422a7d09c2a911659997f/mods/master-comp).
`upstream/` keeps its exact `comp.S`, fixed-point reference model and MIT
license. The adaptation retains its block-peak/log/envelope math, uses bank
settings and OS 1.42 services, and adds parallel gain:

`gain = (1 - MIX) + MIX * compressor_gain`.

This needs no additional PCM loop: both branches carry the same bus and
latency. The native FX VCA applies the combined gain. Task-side control edits
publish only affected prepared coefficients under short IRQ exclusion. Audio
caches Kit selection, uses a compact assembly ABI, scans 64 contiguous ADC
words, skips log/envelope/exp when state and a conservative threshold permit
it, and uses bounded 16-bit products for MIX/VCA. Disabled steady state needs
no stack frame or MAC access. The running DSP math/quality are preserved.

`config-tables.h` is generated from the MIT formulas, including all 16,384
attack/release pairs. It preserves real ColdFire FI=1/RT=0 product truncation
before accumulation, which differs by rare Q31 LSBs from the upstream ideal
64-bit `frac2`. No firmware data is in this table. Its roughly 66 KiB are cold
task-side data; audio reads a prepared 40-byte row.

## SRAM and cache

The current linked LFO3 renderer is **822 bytes**, not the older 1,088-byte
size recorded in historical notes. Its old 1,152-byte window had **330 unused
bytes**. LFO3 1.3.3 narrows its checked window to `80007680`, preserving its
code and ten alignment bytes. COMP uses the next 320 bytes:

| Range | Use | Bytes |
|---|---|---:|
| `80007680..80007780` | peak/log/curve/VCA code, eight-sample peak unroll | 256 |
| `80007780..800077b8` | prepared config, Kit, envelope, GR | 56 |
| `800077b8..800077c0` | state guards | 8 |

Both copies require an empty owned window and verified copy. Either can use
DDR independently when occupied. Periodic code/guard checks switch back to
DDR under IRQ exclusion, preserving envelope/config history. The cache
invalidation happens after the executable copy at initialization, never per
audio block. Both LFO3/COMP init orders are tested. Chorus keeps all its code
and state in its original windows (4 and 10 spare bytes); LFO3's 944-byte data
bank is unchanged. Nothing uses unverified memory above `8000ff94`.

`icache-lock-st` still locks two of four I-cache ways with profiled stock
lines (4 KiB); COMP takes no locked slots. SRAM removes its hot code from
that competition. Six peak unroll sizes were compared before placement:
four samples had the lowest cold-cache proxy; eight fit the final SRAM copy
along with curve and VCA. Larger unrolls consume more cache/SRAM for small
instruction savings. No other module is evicted to place COMP.

## Build

Use the environment and exact flags in [SYNTAKT-BUILD](../../docs/SYNTAKT-BUILD.md).
Import the private controls once per checkout:

```sh
python -B mods/comp-st/tools/import_digitakt.py "$DIGITAKT_OS"
```

The importer requires the pinned original 1.54 SHA-256 and writes only ignored
`imported/` files plus private provenance. No firmware assets are checked in.
Build core 2.4.0, add `--module mods/comp-st` after Chorus and before the three
always-included optimizations in the normal bank command. Matching Chorus
1.2.2 and LFO3 1.3.3 reuse the core's shared sites; both retain older-core
fallback behavior. A core without event 15 rejects this bank.

## Storage and verification

Six full-Q8 values occupy encoded Kit bytes `[15d0,15e0)`: tag `4351`, six
big-endian words, and a folded hash. LFO3 ends at `15d0`; Playhead starts at
`15e0`. Missing, out-of-range or corrupt records restore Digitakt defaults
with MIX off. Native short SysEx Kit buffers are not extended. This version
does not transport these extra settings in native Kit SysEx.

Core query 34 notifies Kit copy/new/encode/decode; query 35 preserves LFO3's
owning-object memcpy path. Kit edits call the native mirror update, as other
Kit controls do. No native Sound/FX field is reused for COMP values.
The audited slots include both working containers, the native project image
and save/reload buffers. Temporary Kit associations validate the native
bytes' hash, so reuse of a clipboard/decode buffer cannot revive old settings.

```sh
python -B tests/test_st_comp_emu.py --core out/<build>/main.bin \
  --bank out/<build>/synfact-machines.smz4 --preview out/<build>/comp.png
python -B tools/bench_st_comp.py --core out/<build>/main.bin \
  --bank out/<build>/synfact-machines.smz4 --sram --out out/<build>/comp-cost.json
python -B tests/test_st_comp_meter_emu.py --core out/<build>/main.bin \
  --bank out/<build>/synfact-machines.smz4 --digitakt "$DIGITAKT_OS" \
  --sram --out out/<build>/gr-preview
python -B mods/comp-st/tools/generate_config_tables.py --check
python -B tests/test_st_comp_perf_emu.py --core out/<build>/main.bin \
  --bank out/<build>/synfact-machines.smz4 --previous <0.1.1-bank> --sram
python -B tests/test_st_comp_sram_emu.py --core out/<build>/main.bin \
  --bank out/<build>/synfact-machines.smz4
```

Offline checks execute the actual core hooks, assembly, native page drawing,
Source/base navigation, Kit constructor/codecs/copy, corrupt-record rejection
and comparison with the upstream independent fixed-point model. Application
selection, allocator and some UI state are explicit fixture boundaries.
The GR test executes the original Digitakt meter calculation and compares 36
native Syntakt LCD renders, checking every pixel outside the strip, control
extremes, unchanged page dots, bar erasure, bypass, audio-driven rise/release
and redraw publication. Its isolated allocator cannot overlap LCD storage.

Measured extra guest instructions per 32 stereo frames, including shared
dispatch and VCA: **43 off, 377..379 below threshold, 484..509 compressing,
485..510 while controls move**, versus 187 / 657..699 / 1017..1061 in 0.1.1.
The instruction reduction is about 27% active and 77% off, with further cache
benefit. One steady compression scene touches 65/10 cached instruction/bank
data lines before, 50/7 optimized in DDR, and 35/4 with SRAM (16 bytes/line).
The cache tool also charges task setters separately. These are instruction
counts and footprint bounds, not measured cycles or a physical CPU percentage.
Quantitative hardware timing and audio characterization remain pending.
See [the cost study](../../docs/FX-BUS-COMPRESSOR-FEASIBILITY.md).

The owner reported correct operation of 0.2.0 on the instrument on
2026-11-20. No physical CPU measurement or bit-exact comparison with the
original Digitakt or an original Synthack-OS binary was supplied. The exact
release comparison above is against comp-st 0.1.1, not those binaries.
