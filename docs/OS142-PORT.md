# Syntakt OS 1.42: core and mods

Core **2.0.0** and all source-built machines/modules now target official
Syntakt **1.42**. This is a desktop SDK port. The browser core package, SMZ
catalogue and website integration are deferred; recognizing the new hash in
the common device profile does not enable the web builder for it.

The current source baseline is remote main `f48f3abc4eb598351311c1f6970f6651b8f4bdcb`
(core 1.9.0), integrating all 17 commits after the initial port baseline
`40e397f`. This includes ARP 1.10.4 blue LEDs and lifecycle corrections,
Playhead, Recorder 1.4, Chorus 1.2, sampler/stereo and MAIN optimizations.
The user reported that the initial local port worked on the instrument.
The complete latest-main build was labelled 2.1.0 during local evaluation;
the published release is 2.0.0. Hardware validation of this final build remains
pending; its SMZ4 contract is `53594605`.

## Pinned inputs and memory

| Fact | 1.41 reference | 1.42 target |
|---|---|---|
| Internal build identifier | 0094 | 0097 |
| MAIN decoded bytes | 3,438,480 | 3,442,576 |
| Container bytes | 2,028,880 | 2,029,392 |
| MAIN SRAM copy source end | `40347b90` | `40348b90` |
| Native BSS clear end | `45565490` | `45566490` |
| Boot stack | `48000000` | `48000000` |

1.42 stock SYX SHA-256:
`ded3c59fdb909eb9e2c3307b30a486452c8fa6947b12c14e0ce35a187b7f37c1`.
MAIN SHA-256:
`1a9723b19fb54d0566c1f690379d2e6dfe43227f77e170ad731dd029430eb3e7`.
The device profile retains recognition of 1.41 for reference tools, while the
current core, bank builder and manifests require 1.42.

Sections 1, 2, 4, 6, 7 and 8 are byte-identical between the two official files,
including bootstrap, updater and AUDIO. Section 5 and the container version/build
header change. The conservative flash budget follows the new official container
(512 bytes larger); this is not a newly measured receiver/flash limit. Stock
1.42 round-trips byte-for-byte, including its 94 transport-padding bytes.

The 64 KiB core reserve, AUDIO companion windows, sample pool, bank allocation
and MAIN SRAM ownership windows are retained. The completed build uses 65,524
resident bytes, leaving 12 bytes, exactly as the latest-main 1.9 reference. No compiler flags or size checks are relaxed.

## What was retargeted

All 108 resident patch sites, firmware exports, fixed native calls, globals, native
view identities, return addresses and inline stock replays are audited against
the new image. There is no single global code offset: observed code/table moves
include `+78`, `+ac`, `+1c4`, `+29c`, `+2a0`, `+320` and `+354` (hex). The native
DDR image/BSS moves by `1000`.

The initial address inventory used unique surrounding binary contexts, masking
embedded pointer literals to find moved code. It was checked against exact
stock hook bytes, matching referenced operands, compatible local code/table
contexts, and the linked replay/ABI regressions. It was not used as an unchecked
range-wide replacement rule. Addresses inside shared AUDIO/owned windows are
kept separate from MAIN addresses with the same numeric value.

The complete bank includes all four SP sampler engines and stereo support,
SY FM DTONE, Recorder, LFO3, ARP, POLY, System info, Sample Manager, Chorus, Playhead and
Modulation Skip. Their native MAIN boundaries and SDK headers are updated.
POLY's native kernels are extracted again from pinned 1.42, keeping relative
branch offsets and assembling external bridges. Digitone donor data is unchanged.

LFO3 lock files remain generated from the historical, hash-pinned Synfact source.
`gen_locks.py` applies the explicit MAIN literal map in
`elekloader/devices/syntakt142.py` after its existing adaptations, so regeneration
does not restore 1.41 calls. That map is a maintenance input, not runtime address
detection. `tools/generate_boot_preimages.py` regenerates the resident preflight
from the current module declarations.

## Bank compatibility

The SAFE/SMZ4 layout and checksums are unchanged. Core 2.0 requires feature 16 and
exact contract **`53594605`** at offset 64. It refuses older contract IDs and
untagged banks before relocation or any initializer. New banks are also refused
by older cores' exact-contract check. Rebuild and replace the bank alongside the
core; its native MAIN calls are OS-specific even though AUDIO is identical.

Source module versions are incremented to identify these new banks. Machine
types and parameter IDs are retained. DSP is byte-identical to the latest-main
1.9 reference build; the sampler/Chorus changes from that baseline are included.
Contract 05 distinguishes the updated host API and core from the initial
local 1.42 contract 04 delivery; replace both the core and bank. Those local
evaluation labels do not define separate published releases.

## Reproduce the local checks

Follow [SYNTAKT-BUILD.md](SYNTAKT-BUILD.md), with `SYNTAKT_STOCK` pointing to
1.42. Generate POLY, FM and Chorus imports in the new checkout first:

```sh
python tools/import_poly_kernels.py --stock "$SYNTAKT_STOCK"
python mods/fm-tone-st/tools/import_digitone.py "$DIGITONE_OS"
python mods/chorus-st/tools/import_digitone.py "$DIGITONE_OS"
python tools/generate_boot_preimages.py --check
```

Build the full bank including `--module mods/chorus-st --module mods/playhead-st`, plus the core and lint
report. Run the common regressions and these release checks:

```sh
python tests/test_st_os142.py --stock "$SYNTAKT_STOCK" --build "$BUILD" \
    --previous-build "$REFERENCE_141_BUILD"
python tests/test_st_safe_boot_emu.py "$BUILD"
python tests/test_st_bw_filter_emu.py --stock "$SYNTAKT_STOCK" "$BUILD"
python tests/test_st_note_accept_emu.py --stock "$SYNTAKT_STOCK" \
    --core "$BUILD/main.bin" --out "$BUILD/note-accept.json"
```

`test_st_os142.py` checks 216 exact resident/runtime preimages and four Playhead context CRCs, the actual output
SYX's protected sections and in-place decompression margin, and the complete
bank/contract. With a matching complete 1.41 reference build, it also checks the
core AUDIO and the bank AUDIO byte-for-byte. The parser tests include validly
checksummed old-contract and untagged-bank negative controls.

The earlier cost/donor differentials that explicitly pin 1.41 and archived banks
remain historical fixtures. They are not evidence of a 1.42 pass. The legacy FM
source comparison additionally needs its older generated provenance file, which
the current recipe importer does not produce. DSP parity against the complete
local 1.41 reference bank is checked independently.

## Validation and remaining limits

The updated build passes 216 stock preimages, all four Playhead CRC contexts,
SYX protected-section identity and exact core/bank AUDIO parity against the
complete latest-main 1.9 reference. Playhead passes 2,968 native sequencing
comparisons plus direction, keyboard, persistence and header-icon cases. The
ARP view and deferred-cleanup regressions check palette 14 (blue), retained
owners, digital/analog tracks and POLY; a stock cleanup negative control
reproduces the old failure with otherwise identical current core code.

The engine unit suite passes 39 cases; TypeScript passes 62 tests with one
optional precompiled browser-core comparison skipped. The common regressions
include host logic, linked startup after BSS clearing, safe-boot writes, native
routing/ABI, mixer, actual LFO3/MSKIP installation and event ownership. The
optional legacy FM source comparison needs an older generated provenance file
and is skipped; independent AUDIO parity remains checked.

The isolated LFO3 activity/cost differential excludes Playhead's metadata
subscription. The inherited core has four native-interest narrowing slots;
a fifth subscriber uses its documented fail-open dispatch. Full-bank startup,
route correctness and Playhead behavior are tested separately. These isolated
activity regression uses the current bank on both sides as a functional
comparison, with activation-loss negative controls. Those instruction results
do not establish an idle CPU improvement for the complete
five-subscriber bank or full-system timing. No compiler flags, memory reserves
or production checks were relaxed.

The user must validate installation, boot with/without the new bank, project and
pattern load/save/reload, every machine/module, stereo recording/playback,
LFO/ARP locks and POLY on digital/analog tracks, and normal USB/Outbox 8 audio.
The local checks do not establish full-system timing, USB behavior or real
project compatibility. The reported exceptions and downgrade diagnosis are a
separate, deferred investigation; this port does not claim to fix their cause.

## Current validation recipe

The release regression runner uses `--previous-build "$BUILD"` for same-build
native-route, mixer and LFO3 functional checks: these are not performance A/B
claims against the initial port. The older-bank event differential was also
run separately. Use the complete latest-main 1.41 reference with
`test_st_os142.py --previous-build` for the independent AUDIO parity check.
The optional legacy FM comparison and precompiled web-core reference remain
skipped. The web builder is deferred.
