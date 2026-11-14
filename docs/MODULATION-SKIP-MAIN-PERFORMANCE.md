# Modulation Skip 1.3 MAIN optimization

Release versions and development labels: see [the consolidated MAIN release](MAIN-OPTIMIZATION-RELEASE.md). Numbers in the measurements below identify historical development iterations.

The candidate retains core 1.8.1.2, samplers 1.4.9, LFO3 1.7, ARP 1.13,
POLY 1.6 and Recorder 1.1.1. Only Modulation Skip changes from 1.2.3 to 1.3.
Core MAIN/AUDIO/SYX, other modules' compiled objects and bank AUDIO must remain
identical. No device transfer, compiler-profile change or new SRAM claim.

## Preserved behavior

- Every source read, stock modulation/zero-depth arithmetic and per-track
  ordering is retained. Clean words are not rewritten by the projection.
- The 64-block matrix scan and stock-output audits, writer hints, first-block
  setup, OFF/VERIFY/refusal/error transitions and counters remain intact.
- Out-of-row indices and dense matrices retain full-stock fallback. The
  dirty threshold remains twelve tracks, with thirteen possible track bits.
- Sparse source selection and zero-group overlap eligibility remain exact.

## Implementation

Flatten the zero-only destinations only on changed matrix publication. Keep
row offsets and an aggregate sparse-eligibility mask. An aligned leaf assembly
loop performs the existing unsigned-16 zero projection on the published list.
Partial projections iterate selected row bits in ascending order. Addresses
remain in DDR; existing SRAM ownership and Chorus behavior are unaffected.

Read native matrix rows through an opaque address barrier once per row, removing
24 helper calls without removing any matrix read. The bank's existing relocation
and instruction audits remain mandatory. The bit test for at most one missing
track replaces a population loop for the bounded thirteen-bit dirty mask.

An assembly dispatcher uses only volatile ABI registers. It directly handles
validated all-zero blocks and intervening full-stock fallback blocks; all
writer/periodic/invalid/VERIFY cases rejoin the general decision before altering
state. Both timer/count checks and every guard are charged to complete-call
instruction counts. Unsigned wrap and mode/invalid-state decisions are compared
directly with 1.2.3.

Publication selects a volatile entry pointer from current matrix membership.
Ordinary active matrices directly enter the general decision, avoiding the
leaf guards. The equivalent trampoline branches combine the return-code zero/
one tests, preserve the stock-path register value, and offset the indirect
call's extra instruction. This avoids a steady overlap-case regression found
in the prototype, while retaining all selected decisions and complete-call
budgets. The pointer defaults to the general decision before initial refresh.

## Validation and reproduction

Use the normal build flags and a fresh complete-bank output. All outputs remain
under ignored `out/`. `SYNTAKT_WORK` supplies the owner's read-only native
conversion/modulation fixtures; it is required for these linked comparisons.

```sh
python tools/bench_st_modulation_skip.py --before OLD_BANK --after NEW_BANK \
  --out out/mskip/performance.json --require-no-regressions
python tests/test_st_modulation_skip_emu.py --bank NEW_BANK --blocks 512
python tests/test_st_modulation_skip_cfbench.py --bank NEW_BANK \
  --cfbench CFBENCH --out out/mskip/cfbench
```

The benchmark runs fourteen cases for 128 blocks each, retaining all periodic
and first-block work in its totals. It compares SRAM plus modes, block/hit/skip/
partial/error/run-mask/writer counters after every call, and checks callee saves
and stack. Sixteen direct-dispatch cases additionally compare internal cadence,
cache validity, machine generation and capture/check masks at periodic/wrap/
invalid-mode/pointer boundaries. Native writers execute; publication benchmark
matrix words are explicit fixture inputs supplied before conversion.

The stock oracle compares SRAM and LFO heap after each native conversion,
modulation and LFO call in OFF/VERIFY/ON, with independent overlap, dense,
single-group, all 17 kernel-mask and unknown-writer refusal checks.

The previous fixture patched a writer that initialization had already executed
without invalidating its translated block. A real subsequent publish counted
zero writes, versus the expected two after cache invalidation; both 1.2.3 and
the prototype failed the old 400-block test at block 226. Fixture installation
now models the real cache synchronization. The writer-hint test uses the actual
model publication and still requires exactly one dirty row and its exact 24
matrix word addresses, with fresh instrumentation. Native group calculation
reads are counted separately from the scan. All original output/failure/mode
assertions remain.

The physical arithmetic check exhausts 65,536 values, with independent expected
results, neighboring-word canaries, empty and duplicate lists, preserved ABI and
EMAC registers. Replacing the projection by RTS is a negative control. The host
test exhausts 8,192 masks against an independent population count.

Hardware acceptance remains pending. Guest instructions do not measure DDR/cache
latency, cycles or MAIN percentages; do not apply their ratios to the full CPU.

## Measured complete-call results

Steady sample at fixture block 10, including dispatcher and all selected work:

| Scenario | 1.2.3 | 1.3 |
|---|---:|---:|
| All amounts zero | 465 | 164 |
| Zero-only destinations, including out-of-range inputs | 886 | 419 |
| One active track | 1,531 | 1,345 |
| Three changing tracks | 2,187 | 1,993 |
| Eleven changing tracks | 4,822 | 4,590 |
| FX-only active source | 1,530 | 1,341 |
| Zero/active destination overlap | 1,935 | 1,935 |
| Dense full-stock fallback | 6,663 | 6,645 |
| Unsafe index full-stock fallback | 6,660 | 6,645 |
| Continuous publication, zero matrix | 1,437 | 838 |
| Continuous publication, active matrix | 1,589 | 967 |

All fourteen scenarios pass both the 128-block total and stated steady-sample
non-increase gates, including first setup, cache rebuilding and periodic audits.
The zero case totals 109,281 -> 58,303 instructions over 128 blocks; the overlap
case totals 293,254 -> 279,505 while its steady sample stays equal. The complete
A/B comparison covers 1,792 blocks plus sixteen dispatch-boundary cases.

The delivered bank has core and AUDIO identical to the previous complete bank;
all other module objects are unchanged. The only changed existing object is
Modulation Skip's main C object; its guarded stock kernel object is identical,
and the only added object is the exact-projection assembly helper.

Delivery validation passes 87 common checks (including 39 unit tests and the
real core/LFO3/MSKIP startup, native routing, shared events and activity checks),
all fourteen 128-block A/B cases and sixteen dispatch edges, the 65,536-value
physical projection check/negative control and all 8,192 host dirty masks.
The native stock oracle passes OFF/VERIFY at 128 blocks each and ON at 512
blocks/28 input events with zero errors, comparing SRAM/LFO heap after each
conversion, modulation and LFO call. It also passes the ten zero-depth edits,
five sparse source groups/overlap fallback, real writer's 24-address scan,
dense fallback, seventeen kernel masks/221 rows and unknown-writer refusal
after 56 blocks. The optional external FM-source comparison is skipped.
Source/object and delivered-bank hashes match; hardware acceptance is pending.

## 1.4: partial preparation and sparse track traversal

Capturing dirty destinations and restoring clean destinations now share one
active-list traversal. A second traversal combines zero-only capture with
clean zero projection. Accepted addresses cannot escape their own track's
142-byte row, and active/zero-only destination sets are disjoint. Reordering
these independent row operations leaves native source-group ordering intact;
the unsafe-address and overlapping-group fallbacks are unchanged.

The sparse runner visits selected track bits in ascending order and finds its
single active source group with ColdFire bit-search instructions. A prototype
cached that group's index at publication, but its extra cold rebuild work
regressed publication-only cases. The final implementation has no extra cache,
allocation, eligibility check, SRAM use or EMAC operation. Existing eligibility
already guarantees a nonempty single group in the permitted source range.

Steady block 10 of the same complete-call fixture, including all checks:

| Scenario | 1.3 | 1.4 |
|---|---:|---:|
| All amounts zero | 164 | 164 |
| Zero-only destinations with saturation | 419 | 419 |
| One changing active track | 1,345 | 1,060 |
| Three changing tracks | 1,993 | 1,722 |
| Eleven changing tracks | 4,590 | 4,375 |
| Zero/active destination overlap | 1,935 | 1,791 |
| Dense full-stock fallback | 6,645 | 6,645 |
| Continuous publication, zero matrix | 838 | 838 |
| Continuous publication, active matrix | 967 | 967 |

First-block instruction counts in these cases also remain unchanged. These are
guest instructions, not hardware cycles or MAIN percentage estimates. Inactive
matrices retain 1.3's cost; this change reduces work when tracks recalculate.

The full native stock oracle adds 72 targeted mixed-row blocks. Individual
sources change while other active rows replay and zero-only destinations cross
all unsigned saturation ranges. The matrix cycles through the six source
groups, overlap fallback, deactivation and reactivation, crossing a 64-block
audit. SRAM and LFO heap must match the full native stock call on every block;
the same test also checks the previous 1.3 bank.

The core remains 1.8.1.2. Core MAIN, core AUDIO, bank AUDIO and all other module
objects are byte-identical to the 1.3 delivery. Only MSKIP's main C object
changes; its assembly helper and guarded native kernel stay identical. The
complete bank is 400 bytes smaller. No firmware is installed by these tests.

All fourteen 128-block A/B cases pass exact SRAM/counter/decision/ABI comparison
and strict non-increase gates for both the total and the steady sample. The
one-track total is 206,354 -> 171,014; three tracks 290,042 -> 256,438; eleven
tracks 625,078 -> 598,418; overlap 279,505 -> 261,649. Zero-only, publication,
dense/unsafe and unknown-writer totals remain unchanged. All sixteen dispatch
edges pass, including periodic boundaries, counter wraps and invalid modes.

The complete bank passes 87 common checks including 39 unit tests, and the
65,536-value physical projection/ABI/EMAC check with its missing-helper negative
control. The optional external FM-source comparison is skipped. The mixed-row
oracle explicitly supplies identical working FREQ/RES words after conversion:
mirror RES edits can otherwise also change converted FREQ. This isolates one
dirty source while requiring the other active rows to replay cleanly; exact
dirty masks and every native stock output are checked on both 1.3 and 1.4.

The long native oracle passes OFF/VERIFY at 128 blocks each and ON at 512
blocks/28 events with zero errors, comparing SRAM and LFO heap after every
conversion, modulation and LFO call. It retains the ten zero-depth edits,
five sparse source groups/overlap refusal, the real writer's exact 24-word
scan, dense fallback, seventeen kernel masks/221 rows and unknown-writer
shutdown after 56 blocks. Both the mixed mirror-edit case and the isolated
working-input case pass; the latter also verifies exact single-track masks.
Hardware CPU acceptance remains pending.
