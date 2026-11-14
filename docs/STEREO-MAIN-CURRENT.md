# Stereo MAIN optimization port on current main

Release versions and development labels: see [the consolidated MAIN release](MAIN-OPTIMIZATION-RELEASE.md). Numbers in the measurements below identify historical development iterations.

Base: `e4af43a3a522846aba224a181d5813e42f5dd827`, fetched from main on
2026-11-12. The worktree branch is `perf/stereo-main-current`.

## Correct reference

The owner's working deployed bank is `Synfact-mods-d7501be1.smz4`, SHA-256
`d7501be130eaef79c81ea9d2feac178354b8e1887a4f185a7cd5536976ca04f3`.
Its AUDIO is 108,640 bytes, SHA-256
`cb86bae5e932fdd2a1b64b0d295ddeeb6db0677c1c6079c0198389fa8972a585`.
The rebuilt bank must keep that entire section byte-identical.

The working core SYX and the rebuilt main core 1.8.0 are byte-identical:
`985e0186db8069444ed96bc950779cb04046ddf1ced4d14367bbc52aa390b007`.
No core installation is needed for this bank comparison.

The earlier MAIN optimization bank came from an older source base. Its
98,352-byte AUDIO matched that older baseline, but omitted the AUDIO
optimizations already deployed in samplers 1.4. It was therefore the wrong
reference for the owner's test. That delivery is superseded by this port.

## Port

Samplers 1.4.2 changes only MAIN. It keeps current main's PCM runs, stereo
voice tail, right-chain optimizations, quiet-track release and the other
mods (including ARP 1.9 and POLY 1.5). AUDIO sources and the shared stereo
wire format are unchanged.

The full 32-bit fractional EMAC mixer and four-track saturation order are
preserved. Its instruction sequence is moved into a relocatable assembly
span; it replaces the old optimization branch's integer scalar/ramp kernels.
Existing raw MACSR/ACC0/ACCext01 preservation remains around the mixer.

MAIN retains the faster MOVEM copies, active-mask traversal, early rejection
of in-flight publications, lazy fingerprinting and gain endpoint tracking for
all eight tracks. Normal banks omit stereo profiling; test banks may enable
it with `SP_STEREO_TEST`. System info's global CPU meter remains enabled.

Conditional MAIN SRAM placement waits until the bank directory is published
and confirms Chorus is absent, and both windows are zero:

| Window | Payload | Occupied / available |
|---|---|---|
| `800077c0..80008000` | state 1,236 + copy 92 + resolver 556 bytes | 1,884 / 2,112 |
| `8000fc20..8000ff94` | fractional mix4 assembly | 112 / 884 |

The expanded D buffers stay in DDR. The neighboring LFO3 windows are
untouched. A code integrity check every 60 UI ticks moves back to DDR on a
mismatch. Physical exclusivity of the borrowed SRAM retains the limitations
documented in `syntakt/sram.h`; it is not established by the emulator.

## Validation

- All 82 common regression checks passed, including parser/load, safe boot,
  UI/ABI/source provenance, units, instruction audit and identical core/bank
  AUDIO versus the current sampler 1.4 reference. The optional FM comparison
  against the external authored source was skipped; it is not a pass.
- Direct comparison against the owner's deployed files confirms whole-SYX
  equality and whole-bank-AUDIO equality. The delivery guard rejects the
  previous optimization bank as a negative control.
- Linked bank differential in analog-emu: five scenarios of 384 blocks each,
  comparing every bus word and track-D output against an unmodified
  current-main build, including DDR, SRAM, fallback, Chorus and occupied
  windows. Callee-saved ABI and LFO3 neighbor sentinels are checked.
- The target EMAC/model test passes 400 mixed-gain/mask cases plus 2,000 cases
  across SRAM, fallback and rejection scenarios, with no output/model,
  EMAC-state or ramp-endpoint errors. PCM/native AUDIO differentials also
  pass (24,000 PCM blocks), and the chain-end and base-width checks pass.

The local baseline bank and the deployed bank have the same layout and AUDIO,
but 54 MAIN bytes differ in UI string references/tables. The linked callback
comparison uses the local unmodified main build with its matching ELF; it
does not claim whole-bank byte identity with the web build.

No hardware CPU percentage or improvement is claimed from these checks.
The owner's eight-track pattern is the pending hardware validation.

## 1.4.3: sparse FX sends and stable gains

The owner confirmed 1.4.2 sounds clean with eight stereo tracks, two tracks
sending to delay and reverb: MAIN peaks at 88%, AUDIO at 88%. The working web
bank had measured 89% MAIN / 88% AUDIO in that pattern. Those are observed
meter readings, not an isolated timing measurement of this module.

1.4.3 keeps all previous improvements and changes only MAIN again:

- A partial group uses a one-, two- or three-track kernel, instead of running
  four lanes padded with zero. Saturation still occurs at the end of the
  same group. A zero product has no effect on the full-precision sum.
- Stable gains omit the per-sample gain increments. Each group checks all
  four steps once before choosing its loop; ramping gains retain the exact
  previous integer wrap and update order.
- The second fractional MSAC for a track fetches the next track's sample
  in parallel. No prefetch reads beyond a sample buffer.
- Later buses reuse the resolved mask without calling the resolver again.

The movable assembly now occupies 486 / 884 bytes in the same MAIN SRAM
code window; state, copies and resolver remain 1,884 / 2,112 bytes. The new
bank's entire AUDIO is identical both to 1.4.2 and to the owner's web bank.
Every compiled object except sampler MAIN and its mixer assembly is also
identical to 1.4.2.

The linked-bank differential passes all five 384-block scenarios against
1.4.2. Native/model/EMAC/SRAM checks and all 82 common regression checks
pass. The same optional external FM comparison remains skipped.

A controlled stable-gain fixture measures only STEREO's four MAIN callbacks
per 32-frame block: eight active stereo tracks, the chosen tracks feeding
both delay and reverb, main bus active and direct bus silent. First the gains
and sequence are primed; the following block is counted with cfbench:

| Tracks feeding both FX | 1.4.2 instructions | 1.4.3 instructions |
|---|---:|---:|
| 0 | 4,930 | 4,455 |
| 1 | 6,532 | 5,245 |
| 2 | 6,542 | 5,387 |
| 3 | 6,546 | 5,523 |
| 4 | 6,550 | 5,655 |
| 8 | 8,134 | 6,821 |

The two-send fixture reduces this module's instruction count by 17.7%.
Counts are identical with DDR and SRAM because cfbench does not model their
latency difference. They exclude native mixing, native FX, core dispatch,
other modules, caches and complete device scheduling. They are not CPU
percentages or a prediction of the total MAIN reading. Physical validation
of 1.4.3 in the owner's pattern is pending.

## 1.4.4: assembly expansion and two-frame mixer loops

Keeps 1.4.3 and changes only MAIN. The conversion from the block's signed
16-bit mantissas to 32-bit D/2 vectors is now hand-written assembly: eight
samples per iteration, with four-register stores and no access to inactive
tracks. C static assertions guard its private block layout offsets. It does
not touch EMAC. Each fractional mix loop now processes two frames per
iteration, retaining each frame's exact product and saturation order.

Both routines still migrate to the same MAIN SRAM windows: state 1,236 +
copy/expansion 216 + resolver 556 = 2,008 / 2,112 bytes; mix 790 / 884 bytes.
The expanded vectors remain in DDR. The placement and fallback switch the
expansion function together with the other kernels.

The target expansion test checks 4,096 blocks against an independent integer
oracle, all 256 masks and shifts 0..16, inactive-track sentinels, output
canaries and EMAC preservation. Further cases execute it from SRAM and after
fallback. Five linked-bank scenarios of 384 blocks each remain bit-exact
against 1.4.3, and all 82 common regression checks pass. The optional external
FM source comparison remains skipped. The entire bank AUDIO and core SYX
remain identical to the owner's working deployed inputs.

The same controlled eight-track fixture counts:

| Tracks feeding both FX | 1.4.3 instructions | 1.4.4 instructions |
|---|---:|---:|
| 0 | 4,455 | 3,763 |
| 1 | 5,245 | 4,489 |
| 2 | 5,387 | 4,631 |
| 3 | 5,523 | 4,767 |
| 4 | 5,655 | 4,899 |
| 8 | 6,821 | 6,001 |

For two FX sends, this module executes 14.0% fewer instructions than 1.4.3,
and 29.2% fewer than 1.4.2. The same limitations apply: these are module
instruction counts, not physical CPU percentages. Hardware validation of
1.4.4 is pending.

## 1.4.5: local ramp preparation, no additional cache

The owner confirmed 1.4.4 sounds clean and reports MAIN at 86–87% in the
same eight-track/two-send pattern. A cache of prepared gain groups was tried
offline but rejected: despite its steady-state savings, changing stereo
masks added roughly 5% to this module's instruction count. None of that
cache, its comparison routine or its additional SRAM/DDR state is shipped.

The accepted change simply captures each bus's previous-gain pointer and
primed flag once per callback. They are owned by this module and remain
fixed throughout the callback. This prevents redundant reloads inside the
track walk, without adding conditions, persistent state, invalidation or
per-block comparisons. The assembly and SRAM layout are identical to 1.4.4.

The linked differential now measures both stable values and changing values.
`--require-no-count-regressions` rejects an increase in any complete-block
count in that matrix. All 30 measured configurations improve: 12 stable
cases (six send counts in DDR and SRAM) and 18 cases covering first/continued
gain changes and alternating masks with 0, 2 and 8 FX sends. These count
the complete four STEREO callbacks, including setup and control flow:

| Tracks feeding both FX | 1.4.4 stable instructions | 1.4.5 stable instructions |
|---|---:|---:|
| 0 | 3,763 | 3,599 |
| 1 | 4,489 | 4,319 |
| 2 | 4,631 | 4,461 |
| 3 | 4,767 | 4,595 |
| 4 | 4,899 | 4,727 |
| 8 | 6,001 | 5,829 |

The two-send case saves 3.7% of the module's instructions. All 18 changing
cases save between 67 and 172 instructions per block. Five 384-block linked
differentials remain bit-exact, native/model/expansion/EMAC/SRAM checks pass,
and all 82 common regression checks pass (external FM source comparison
skipped). AUDIO and the core remain byte-identical to the owner's files.
Only the sampler MAIN C object differs from 1.4.4. These measurements do not
establish a physical CPU improvement or cover every scheduler/cache state;
hardware validation of 1.4.5 is pending.
