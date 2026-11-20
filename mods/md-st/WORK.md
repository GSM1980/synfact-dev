# MD MACHINEDRUM: work log and what is left

A handoff for whoever continues this machine (person or agent). Read
`README.md` (this folder) first: it says how the voices are built and tested.
Keep this file current: what was done, what was measured, what is next.

## Where the work is

- Branch **`feat/md-machine`**, in its own git worktree (`synfact-dev-md`, a
  sibling folder of the main checkout; docs/PARALLEL-AGENTS.md). Cut from `main`
  at `ccd74b7`. **Nothing is committed yet**: the work is the uncommitted files
  `git status` shows (all of `mods/md-st/`, `tests/md_st_*.c`,
  `tests/test_md_st.py`, `js/test/coldfire.test.ts`, and the small edits in
  `docs/ADAPTING.md`, `docs/TESTS.md`, `elekloader/isa/coldfire.py`,
  `js/src/isa/coldfire.ts`, `tests/test_units.py`: the `extb.l` decoder fix and
  machine type 70). Commit only when the user asks.
- Ignored, in the worktree: `out/md-st-*` (banks), `out/md-st-tools/` (the
  profiling tools below, and `LOCAL.md` with this machine's tool paths).
- The user's rules for this machine: mdkit's code lives here (`mdkit/`, vendored,
  unchanged; credit in README), never used from the outside repository; one machine
  type (70) with TYPE choosing the voice; exact (word for word with mdkit) is the
  default, approximations only as clearly labelled options.

## State (2026-11-11)

21 voices, all word for word with mdkit on the host (60 scenarios plus every knob
swept, `MD_ST_DOMAIN_CHECK` on): TRX-BD, SD, XT, CP, RS, CB, CH, OH, CY, MA, CL,
XC, B2; EFM-SD, XT, CP, RS, CB, HH, CY; PI-SD. Bank 0.8 (`out/md-st-0.8/`) has them
all: AUDIO 498 KB of the 723 KB reserve. The C copies of the voices (fallback when
the EMAC self-test fails) were removed with the user's agreement: a failed test now
leaves the machine silent.

Measured on the unit (CPU A; STOP is 43 %):

| voice | per track | tracks |
|---|---|---|
| TRX-BD | 4.6 % | 8 (79-83 %) |
| EFM-SD | 11.5 % | 4 (89 %), 5 saturates |
| PI-SD | 37 % | 1 (80 %), 2 saturate |

What the measurements taught (keep these in mind when optimizing):

1. **`muls.l` is cheap** (~3 cycles): a probe bank with every exact low product
   replaced by `eor.l` saved only 2 % CPU. The EMAC exact-product scheme is fine.
2. **Cache misses are the other half of the cost.** The MCF54415 has 8 KB of
   instruction cache and 8 KB of data cache (datasheet). A rough model that fits all
   three measurements: cycles ~ 0.85 x instructions + ~40 x cache misses. Code and
   state are cold at the start of each block (the stock engine and the other tracks
   run between). Several tracks of the same voice share its code and tables, so a
   voice with a small code footprint scales well (TRX-BD x 8).
3. So for a voice, measure **instructions and code/data touched per block**, not
   instructions alone (tools below). `-Os` did not shrink PI-SD's footprint much and
   cost 60 % more instructions; the footprint is many different pieces of code, not
   unrolled loops.
4. The AUDIO SRAM has no free room (docs/MACHINE-REFERENCE.md); state stays in DDR.

Instructions per Syntakt block of one sounding track (analog-emu, voice + resampler +
glue, knobs 100 40 64 20 90 10 64 30, 2 warm-up blocks, 16 measured), and the
estimate of tracks that fit (55 % of CPU A free, factor per family from the table above):

| voice | instr/block | est. per track | est. tracks |
|---|---|---|---|
| TRX-BD | 6,290 (native) | 4.6 % measured | 8 |
| TRX-XT / XC | 5,600 | 4-5 % | 8 |
| TRX-CL | 12,657 | 9-11 % | 4-6 |
| TRX-RS / B2 | 14,000 | 10-12 % | 4-5 |
| TRX-SD | 17,540 | 13-16 % | 3-4 |
| TRX-CB / CH / OH | 19,900 | 15-18 % | 3 |
| TRX-CP | 20,763 | 15-19 % | 2-3 |
| TRX-MA | 24,058 | 18-21 % | 2-3 |
| TRX-CY | 31,154 | 23-28 % | 1-2 |
| EFM-SD | 12,880 (native) | 11.5 % measured | 4 |
| EFM-XT / CP | 13,400 | ~12 % | 4 |
| EFM-RS / CB / HH | 17,500 | ~16 % | 3 |
| EFM-CY | 23,894 | ~21 % | 2 |
| PI-SD | 32,547 (native) | 37 % measured | 1 |

The 18 new voices have not run on the unit. Their analog-emu check (step 7 of
`tests/test_md_st.py`) passed for all 21 voices: words equal to the host's with the
EMAC, self-test passed (2026-11-11, before the metal work below).

## Optimization log

- **Metal stages** (`audio/native/trx_metal.c`, NATIVE entries `metal(...)` in
  `md_port.py`): `metal_source`, `metal_resonant_lowpass` (CY, CH, OH) and
  `metal_difference_cascade` (CY, CB) are the same code in those voices, written once.
  The square sources go by runs of one sign with two products a source a block; the
  filters keep no 56-bit accumulator: each is read only as acc_to_word(asl(acc,1)) =
  limit(floor(S / 2^22) + d), computed with the EMAC read and the 32-bit products
  (`md_metal_floor22`). Host word for word (quick run). Instructions per block:
  TRX-CY 31,154 -> 21,873; TRX-CB 19,926 -> 14,700; TRX-CH/OH 19,800 -> 14,600 (with
  part 3, `metal_difference_filter`).
- **CP filters** (parts 4 and 5 of `trx_metal.c`): `cp_filter6` is the same code in
  TRX-MA and TRX-CP (its coefficient index walks {square, 1/2, -1, 1/2} and returns to
  its start every sample, so a sample is the cascade's work); `cp_filter4` (CP) is the
  resonant low-pass loop. TRX-MA 24,058 -> 17,373; TRX-CP 20,793 -> 15,441.
- Full test run 2 (sweeps, Unicorn, analog-emu step 7) passed with parts 0-3; its
  analog-emu track step already had parts 4-5 (EMAC self-test passed, words equal to the
  host). Track instructions then (voice + resampler, analog-emu): TRX-BD 7,742, SD 17,281,
  XT 5,441, CP 14,927, RS 13,223, CB 15,057, CH 14,312, OH 14,209, CY 21,326, MA 16,129,
  CL 12,557, XC 5,438, B2 13,451, EFM-SD 12,737, XT 13,252, CP 13,246, RS 17,516.
- Full test run 3 on the final sources (parts 0-5) passed: all 21 voices word for word on
  the host (scenarios + sweeps), in Unicorn and in analog-emu with the EMAC (self-test
  passed); unit tests 40/40. Track instructions as in run 2 above, plus EFM-CB 17,328,
  HH 16,931, CY 23,566, PI-SD 32,327.
- **Bank 0.9** (`out/md-st-0.9/`, AUDIO 499,792 bytes, parser PASS): given to the user to
  measure CPU A with TRX-CY, MA, CP, CB, CH/OH (how many tracks before it saturates).
  Record the numbers here. User (2026-11-11): the tracks that fit are "more or less" the
  estimates (2-3 CY, 3-4 MA/CP/CB/CH/OH): the cost model holds.
- Tried and dropped: rewriting acc_to_word(mpy(a,b)) as one EMAC read (GCC already
  drops the unused low bits: no gain); the metal sources sample by sample with the six
  stages inside (arrays left the registers: slower).
- `md-acc.h` gained `md_acc_limit(t)` (acc_to_word's limit, one compare when in range).
- **The EFM sine whole again** (recipe `whole=True` in `efm()`): the minus bitmap cost the
  FM operators a load and a test per read (EFM-SD's track went 11,497 -> 12,737 with it).
  The EFM sine keeps its own quarter (+32 KB, tables 150 KB); TRX-B2/XC/XT and EFM-XT
  still use the bitmap. Voice instructions per block: EFM-SD 12,880 -> 11,846, CP 13,475
  -> 13,008, RS 17,730 -> 16,444, CB 17,589 -> 16,245, HH 17,163 -> 15,822, CY 23,894 ->
  21,750 (EFM-XT unchanged, 13,386). Host quick run passed; full run pending.
- Full test run 4 (whole EFM sine) passed, EMAC self-test included. Track instructions:
  EFM-SD 11,719, CP 12,792, RS 16,250, CB 16,007, HH 15,637.
- **`audio/native/md-floor.h`** now holds the floor22 helpers (`md_floor22`, `md_floor22_2`,
  `md_floor_word`), shared by `trx_metal.c` and **`trx_sd_noise.c`** (TRX-SD's noise filter,
  same scheme, NATIVE `trx_sd`). TRX-SD 17,604 -> 16,211. Host quick run passed; full run 5
  launched after this (record it; then build bank 0.10).
- Full test run 5 (TRX-SD noise native) passed.
- **TRX-SD's clock in integers** (`audio/native/trx_sd_clock.c`: seed, `sd_boundary`,
  `sd_controls`, the clock's only users in the voice; no float routine left in the voice):
  TRX-SD 16,211 -> 13,351. Full run 6 passed (all steps, EMAC self-test). Track
  instructions (voice + resampler, analog-emu): TRX-SD 13,001, EFM-CY 21,463, the rest as
  in run 2/4 above. **Bank 0.10** (`out/md-st-0.10/`, AUDIO 530,512 bytes with the whole
  EFM sine, parser PASS, unit tests 40/40) for the user to measure.
  **Measured (2026-11-16): the float clock costs little in most voices**, because they
  call their `controls` only on a trigger or a publish, not every block (TRX-SD did every
  block). Soft-float instructions per block (prof_cf, top functions): TRX-XT/XC 520,
  CP/MA/CL 365, RS 421, CB/CH/OH/CY ~600, EFM 325-550 (2-4 % of each voice); **TRX-B2 2,915
  (22 %)**: its `b2_controls` runs every block.
- **TRX-B2's clock in integers** (`audio/native/trx_b2_clock.c`: seed, `b2_controls`,
  `b2_boundary`): TRX-B2 14,031 -> 10,375. Full run 7 passed (all steps, EMAC self-test;
  TRX-B2 track 10,175). **Bank 0.11** (`out/md-st-0.11/`, AUDIO 530,512 bytes, parser
  PASS, unit tests 40/40) for the user to measure.
- **EFM voices (2026-11-16)**: a native EFM-CY render (32-bit phase and feedback, md_mpyr
  words, the resonator as one rounded sum) was exact but saved only ~260 of 21,750: GCC
  already compiled the generic code close to that. Dropped. The cost per EFM sample is the
  sine lookups and md_mpyr (~17-31 instructions each), near the floor of this arithmetic.
  What did pay: `md_acc_mpy_su` (signed x unsigned) was still the C 12-bit-halves form; it
  is now md_acc_product's EMAC product (the same doubled value; |u| < 2^24): EFM-HH 15,822 ->
  14,790, EFM-CY 21,750 -> 20,937, the others unchanged. Host quick run passed (md-acc check
  included). Full run 8 passed (all steps, EMAC self-test; tracks EFM-HH 14,642, EFM-CY
  20,666). **Bank 0.12** (`out/md-st-0.12/`, AUDIO 530,512 bytes, parser PASS, unit tests
  40/40).
  Further EFM gains would need hand-written assembly or a lighter sine lookup. A whole
  32768-word sine (128 KB) would fit in the bank's AUDIO reserve (DDR, ~192 KB free at bank
  0.11) but not in AUDIO SRAM (64 KB, all taken), and it spreads the reads over 4x the cache
  lines of the quarter (8 KB data cache): it saves the fold (a few instructions a read) but
  may lose that in misses. Untested; it would also eat most of the room the PI voices need.
- **Round of 2026-11-16 (user's points 1-3):**
  1. The resampler and glue (`md.c`): ~1,500 instructions a track (Hermite ~1,200 =
     32 outputs x ~37 with the phase step, `md_pull` ~270, update ~100). The loop compiles
     to ~30 instructions an output for four muls.w, the limit and the phase: 200-300 a
     track left at most (2-4 %). Left as it is.
  2. TRX-CY: its filters are in the floor22 form and its gain loops already one EMAC read
     each (GCC drops the unused low bits): near the floor of this arithmetic (21,300).
  3. **TRX-CL's filter native** (`audio/native/trx_cl_filter.c`, the cascade's scheme):
     12,749 -> 10,248. **TRX-RS's filter** (`trx_rs_filter.c`): its accumulators persist and
     are stored whole, so they stay md_acc; only its MAC runs are joined (`md_acc_mac2`, a
     product with 2a for "mac f,a" twice, `md_acc_add_halves`, both back in md-acc.h):
     14,041 -> 13,005. Host quick run passed (md-acc check included); full run 9 launched
     (record it, then build bank 0.13).
  Full run 9 passed (tracks TRX-RS 12,238, TRX-CL 10,195).
  4. **The float clocks of the other TRX voices, by the generator** (`port_clock`,
     `INT_CLOCK`, `CLOCK_RULES` in `md_port.py`): eight textual rules rewrite the seed, the
     1/128 smoothing, the targets, `(uint32_t)(128.0f * x)`, `(unsigned)k[X]` and the local
     `k[i]=(float)(raw>>7)` arrays into the integer storage (`MD_CLK`); copies stay. The
     Unicorn step now fails if a voice with an integer clock calls any `__*sf*` routine.
     None of the ten does. Voice instructions: TRX-XT/XC 5,621 -> 4,671, CP 15,441 ->
     14,662, RS 13,005 -> 12,164, CB 14,700 -> 13,605, CH/OH 14,600 -> 13,615, CY 21,873 ->
     20,869, MA 17,373 -> 16,614, CL 10,248 -> 9,487. Full run 10 passed (all steps, the
     float check, EMAC self-test). Track instructions (voice + resampler, analog-emu):
     TRX-BD 7,742, SD 13,001, XT 4,729, CP 14,418, RS 11,655, CB 14,146, CH 13,497, OH
     13,392, CY 20,509, MA 15,656, CL 9,690, XC 4,728, B2 10,175; EFM-SD 11,719, XT 13,252,
     CP 12,792, RS 16,250, CB 16,007, HH 14,642, CY 20,666; PI-SD 32,327.
     **Bank 0.13** (`out/md-st-0.13/`, AUDIO 528,464 bytes, parser PASS, unit tests 40/40).
  **Points 1-4 done. Next (user): the missing engines** (PI family, EFM-BD): done, all 30.

## The PI family (2026-11-16, in progress)

- **Ported, word for word on the host (quick run): PI-BD, PI-MT, PI-ML, PI-MA, PI-RS**, in
  the Machinedrum's order after PI-BD: TYPE 20 PI-BD, 21 PI-SD, 22 PI-MT, 23 PI-ML,
  24 PI-MA, 25 PI-RS (TYPE's order is not released yet). Recipes: each voice's
  `*_sample` (its ROM range: BD 1,480 words, MT 1,676, ML 2,682, RS 587) and
  `pi_decay(v)` (`*_damp`/`*_env`, 512 words each, the same in every PI voice).
- **`share_arrays`** (md_port.py, point 6 of its docstring): mdkit's integer tables that
  repeat (the PI word0/word5/word7 sets, some TRX control words) are kept once in
  `md-tables.c`, narrowed when the values fit; PI-SD and PI-RS went from ~57 KB each to
  ~12 KB. A test bank with 26 voices: AUDIO 665,680 of 723,120 bytes (57 KB free).
  PI-MA keeps its own `gain[16384]` (64 KB, used by it alone).
- The PI voices' clocks are in `INT_CLOCK` (the seed rule now takes any array name).
- **`pi_wrap.c`** (PI-SD's native modulo addressing) is the same function in every PI
  voice and now serves all six. Voice instructions per block: PI-BD 80,474 -> 54,716,
  MT 69,598 -> 56,324, ML 55,921 -> 42,437, RS 77,942 -> 63,524, MA 7,732, SD 32,547.
  Still heavy (PI-SD measures 37 % of CPU A at 32,000): next is each voice's modal
  filter natively, as `pi_modes.c` / `pi_delays.c` / `pi_sd_block.c` did for PI-SD
  (profile with prof_cf.py + outer.py: PI-BD's hot lines were the modal MAC loop).
- **`pi_modal_filter.c`**: `mdkit_pi_<v>_modal_filter` is the same code in every PI voice
  (its coefficient pointer cycles over three fixed words a sample; a sample's three MACs are
  one `md_acc_mac3`; the accumulator keeps its A0, so it stays md_acc). Wired for BD, MT,
  ML, MA, RS: PI-BD 54,716 -> 38,464, MT 56,324 -> 40,337, ML 42,437 -> 26,284 (host quick
  run exact). PI-RS's `drum_modes`/`drum_delays` are PI-SD's code under other names:
  `pi_modes.c` and `pi_delays.c` now take the voice's types by macro (`MD_PI_T`,
  `MD_PI_MODAL_T`, PI-SD's by default; `RS_TYPES` in md_port.py): PI-RS 63,524 -> 36,381.
  PI voices now: BD 38,464, SD 32,547, MT 40,337, ML 26,284, MA 7,732, RS 36,381 (one
  track each, like PI-SD; PI-MA several).
- Full run 11 **failed on PI-MA** (scenario 35, 1 LSB; the quick run's 12 scenarios do not
  reach it). Cause: mdkit's `ma_sub_round` rounds on the raw uint64 accumulator and clears
  bit 24 (A1's lowest) for round-to-even; in the two-word encoding that bit is t's bit 0, so
  the line did nothing. `port_helpers` now rewrites `x &= ~(UINT64_C(1) << 24);` as clearing
  t's bit 0 and **refuses any other raw `UINT64_C(1) << n` (n >= 24) outside the helpers**
  (none in any other voice; the raw `+ 2^23` and `& ~0xffffff` act on A0 and are right).
  PI-MA: 60 scenarios + 1,920 sweeps word for word.
- **PI-MA's gain** (`gain[16384]`, 64 KB) is exactly floor(row[a] col[b] / gain[0]) =
  floor(row[a] C[b] / 2^23), C[b] = ceil(col[b] 2^23 / gain[0]): two 128-word tables
  (recipe `only=True` tables, a text recipe with one `md_mul_shift_exact`, and a
  `host_check` that proves all 16,384 words when the tables are made). PI-MA 72 KB -> 5.7 KB.
  Full run 12 launched (record it, then bank 0.14).
- Full run 12 passed (26 voices). **Bank 0.14** (`out/md-st-0.14/`, 26 voices, AUDIO 594,000
  bytes, 129 KB free, parser PASS, unit tests 40/40).
- **PI-RC, PI-CC, PI-HH ported** (TYPE 26, 27, 28), word for word on the host (quick run):
  - `pi_metal(v)` recipe: damp/env, the two filter-curve banks (the same in all three), and
    the ROM's sine of period 32,767 (32,768 words; not antisymmetric word for word). It is
    kept as its difference from TRX-BD's sine, -841..1611, in runs of 64: a 16-bit base a
    run and an 8-bit offset a word (recipe `minus`; 33 KB, shared by the three).
  - Their DSP delay memory is 65,536 words (256 KB a track); they use the 1,024 at
    0x135600 (see md_port.py's docstring, point 7, `EDITS`): the port keeps those.
    MD_STATE_BYTES 270,952 -> 12,904.
  - `pi_wrap.c`, `pi_modal_filter.c`, the integer clock as for the other PI voices.
  - Voice instructions per block: RC 63,113, CC 63,541, HH 63,492 (about two of PI-SD:
    on the unit probably over 60 % of CPU A for one track; not native yet beyond the
    shared parts). Profile them next.
  - A test bank with all 29 voices: AUDIO 715,248 of 723,120 bytes (7.9 KB free). Room to
    win back if needed: the EFM sine kept whole (32 KB; `whole=True`), code at -Os for cold
    functions.
- Full run 13 passed (29 voices; tracks PI-RC 61,933, CC 62,353, HH 62,362). **Bank 0.15**
  (`out/md-st-0.15/`, 29 voices, AUDIO 715,248 bytes, 7.9 KB free, parser PASS, unit tests
  40/40). README's voice table and mod.json updated.
- **PI-RC/CC/HH natively** (the same code in the three): `pi_metal_advance.c` (their modulo
  register, as pi_wrap.c) and `pi_metal.c` (MD_PM_PART 0 `metal_filter`: two exact sums a
  sample; 1 `metal_delay_read`: md_acc_mac_diff; 2 `metal_delay_write`: fresh accumulators
  read as words, md_floor22_2). Voice instructions 63,500 -> 45,800 (advance) -> ~38,700
  (RC 38,283, CC 38,714, HH 38,767), host quick run exact; the bank still fits (715,248).
  Full run 14 passed (all steps, EMAC self-test; tracks PI-RC 37,566, CC 37,989, HH 38,080).
  **Bank 0.16** (`out/md-st-0.16/`, 29 voices, AUDIO 715,248 bytes, parser PASS, unit tests
  40/40) for the user to try the PI voices on the unit.
- **EFM-BD (done, 2026-11-16)**: mdkit's EFM-BD is a double model, not DSP code, so it is a
  hand-written fixed-point port: `audio/native/efm_bd.c` + `efm_bd.h` (md_port.py `HAND`,
  TYPE 13; the voices after it moved up one: PI is now 21-29). mdkit's float control
  clock as is; Q24 exponents through mdkit's own exp2 polynomial; envelopes = base x the
  block's powers (rounded once a block), decays held as 1 - decay in Q37 and the 32nd
  power made by squaring (a Q31 decay drifted 1e-5 a second); ptch Q37 / y1 Q34 (at PTCH 0
  MFRQ 1 op1's phase step had 14 bits: 4.5e-5 frequency error); phases Q15.17; sine =
  TRX-BD's quarter, folded once per sine. **Its FM feedback (mod = 32768 MFB s1) has loop
  gain 2 pi MFB cos > 1 for MFB >~ 21: chaotic, no arithmetic but mdkit's doubles follows
  it sample for sample**, so its test is `tests/md_st_voice_model.c` (test_md_st.py, HAND
  voices): envelopes within 1e-5 (relative, or 1e-6 absolute below 0.1) - measured 1.1e-6;
  without feedback, attacks (first 2048 samples) >= 90 dB - measured 95.0 dB; each
  operator's phase error summed over a one-second note within 1e-5 of its advance -
  measured 8.2e-7. Speed: first port 19,255 instructions a track block (a third in
  `__mulsf3`: coefficients from the float clock twice a block); now coefficients and powers
  are rebuilt only when `clock.current` moves (`md_efm_bd_refresh`), products in the
  sample loop are floors where the bias does not add up (op1's phase: md_q30r, rebase:
  md_q31r): **6,538** (TRX-BD's is 6,290). **Bank 0.17** (`out/md-st-0.17/`, 30 voices,
  AUDIO 719,344 of 723,120 bytes: 3.7 KB free, parser PASS, unit tests 40/40).
  Full run: 117 steps passed, none skipped (sweeps, Unicorn, analog-emu with the EMAC
  self-test; EFM-BD track 6,527). **Next: the user tries bank 0.17 on the unit.**
  **2026-11-17: on main 3c0cdcb (OS 1.42, core 2.0.0)**: md-st's `os` is 1.42; its one MAIN
  call (`0x40039160`, the Sound's value) is where the samplers keep it in 1.42, and its AUDIO
  addresses sit in section 7, identical in both OS files. Bank 0.19 (`out/md-st-0.19-core-2.0.0/`,
  with its core SYX): 30 voices, AUDIO as 0.17, parser PASS; no tests run (user's call).
  **Then (user's plan, 2026-11-16): a fixed cap on tracks per TYPE**, like the real MD's
  four RAM machines: the user measures on the unit how many tracks of each TYPE fit, and
  md-st refuses a TYPE beyond its cap. No run-time CPU governor (the user turned it down).
- **To do before a bank: the full test run** (sweeps, Unicorn, analog-emu step 7).
  The EFM voices' clocks (`efm_clock_*`, `k[]` in their build functions) are not done:
  their float use is ~325-550 a block; the same rules may need a few more patterns.
- (older note) The other voices' clocks: a 2-4 % gain each; do them only after the larger items. The
  template is `trx_sd_clock.c` / `trx_b2_clock.c`.
  The functions that read or write each voice's float clock (all of them must go native
  together, since the storage changes meaning), from mdkit's sources:
  - TRX-CB: `md_control_clock_seed`, `md_trx_events_boundary`, `trx_cb_from_knobs`,
    `cb_convert`, `cb_controls`; TRX-CY the same names with `cy`; TRX-CH/OH:
    `md_control_clock_seed`, `trx_ch_from_knobs`, `ch_convert`, `hat_controls`,
    `hat_boundary` (OH: `oh`); TRX-CL: seed, `md_trx_events_boundary`, `trx_cl_packet`,
    `cl_controls`; TRX-RS: seed, `md_trx_events_boundary`, `trx_rs_packet`, `rs_controls`;
    TRX-CP and TRX-MA: seed, `cp_controls`/`ma_controls`, `cp_boundary`/`ma_boundary`;
    TRX-XC/XT: `xc_control_clock_seed`, `xc_trx_events_boundary`, `trx_xc_packet`,
    `xc_build_controls` (`xt` likewise); TRX-B2: `b2_control_clock_seed`, `b2_controls`,
    `b2_boundary`.
  - EFM voices: `efm_clock_seed`, `efm_clock_target`, `efm_clock_boundary` and the
    `*_build_controls` / `*_build_slots` that read `k[]` (EFM-SD keeps its float clock with
    a native boundary, `efm_clock.c`: check what is left of float there first).
  Check each with `nm -u` (no `__mulsf3`, `__addsf3`, `__floatunsisf`, `__fixunssfsi` left).
- **Big lever left: the control clocks in float32.** Each TRX voice but BD (and the EFM
  ones) smooths its knobs in soft float32 every block (TRX-SD: ~1,400 instructions a block
  in `md-runtime.c`). TRX-BD's `trx_bd_clock.c` shows the integer form (every value is a
  whole number of 1/128). The clocks are not the same code in every voice
  (`*_md_trx_events_boundary` differs; `*_md_control_clock_seed` is the same in SD, CP,
  CB, CH, OH, CY, MA, CL), so each voice's boundary and its readers of `clock.current`
  need converting.
- Next in line: EFM-CY (21,750: its FM feedback loops and wave-bank scans, lines ~425-470
  of the generated voice), TRX-SD (17,604), EFM-RS/CB/HH (~17,500), then a new
  profile of CY (21,873). The recipe that keeps paying: find an accumulator that is only
  read as acc_to_word (after an asl or not), and compute that word directly as
  limit(floor(S / 2^k) + d) from the EMAC (`md_metal_floor22` and friends).

## Survey: approximations for more tracks (2026-11-17)

The user asked for each voice what could be cheaper if it is allowed to leave mdkit's words
"without losing much", to fit the most tracks of each TYPE. First unit reading with a mix
(OS 1.42, core 2.0.0, bank 0.19): TRX-BD + TRX-SD + TRX-CH + EFM-HH + 4 SP ONE SHOT, all
sounding = 83 % (estimate 81 %).

Profile (analog-emu, prof_all.py in out/md-st-tools: one compile, every TYPE, 40 blocks):
where a track's instructions go, from its top 25 source lines. The resampler and glue
(`md_render`) are 1,772 a track in every voice. The rest is mostly the DSP's arithmetic kept
word for word: sign-extending 24-bit words (`md_s`, `md_acc_s24`: 9-19 %), the two-word
56-bit accumulator (`md_acc_add`: up to 14 %), the limit on every store (`acc_to_word`:
up to 16 %), and the exact products (`md_emac_read` + `md_low32` + the floor helpers'
recombination: 10-23 %).

Levers (A-D leave mdkit's words; E is exact):
- **A, the arithmetic at 32 bits.** An accumulator keeps its high word only (bits 24..55;
  A0 dropped), a product is one EMAC read without the low 32 bits, words stay signed
  int32 (no md_s). Phases, LFSRs and address arithmetic stay exact. Error ~1 LSB of 24
  bits a product (about -130 dB); in long recursions it adds up (to check: SER against
  mdkit with tests/md_st_voice_ser.c). Estimated -20 to -35 % of a voice.
- **B, the sines without their corrections** (fix_at word, the minus bitmap): 1 LSB.
  -3 to -9 % in the voices that read sines often (EFM, TRX-XT/XC/CL/B2).
- **C, a linear resampler for the low voices** (kicks, toms): about -400 a track; images
  of content above ~10 kHz at -25 to -35 dB, so only where there is none.
- **D, structural** (last resort, audible): fewer PI modes/taps, half-rate voices.
- **E, exact natives still missing**: TRX-SD's tone (2,565), TRX-CL's scan, PI-MT's and
  PI-RS's ROM reads (2,833, 3,457), PI-RC/CC/HH's memory map (metal_read/metal_write
  decode an address every access: lines 458-469 about 2,700) and metal_delay_read (8,255),
  the generic EFM-XT/CP/RS/CB/HH renders (EFM-SD's native showed the way).

Estimate per voice after E + A (+ B, C where they apply): see the table given to the
user (2026-11-17). TRX and EFM mostly reach 5-8 tracks; PI stays at 1-2 (PI-MA 8).
Plan proposed: prototype A on TRX-SD, EFM-SD and PI-SD, measure instructions and SER,
then roll out.

### Done (2026-11-17): the safe approximations, two natives, bank 0.20

- **A as estimated does not hold.** Dropping A0 (products into md_acc with A0 = 0:
  `md_acc_product`, `md_acc_mac2/3`) saved 15-37 % but broke most voices: errors of
  -10 to -30 dBFS, growing through the note (first 1024 samples at 27-48 dB SER): A0
  carries the precision of the filters and of phases built from products. Removed.
- **Kept (per voice, `APPROX` in md_port.py; `MD_ST_EXACT` off):** `mid` (rounded and
  two-product words from the EMAC read: md_mpyr, md_mid_mpy2[_first]), `floor`
  (md-floor.h), `sine` (no fix word/minus bit). Measured error, RMS dBFS over 60 scenarios:
  all <= -89 (TRX-MA), most < -110. EFM: no sine (FM feedback: -15 to -36 dBFS with it);
  EFM-CB/CY no mid (-45/-24 with it); TRX-CP no floor (-30 with it; its cp_filter6).
  md.c's self-test also checks the reads the forms use (md_ar*, against md_ar*_c).
- **Natives (exact, E):** `trx_sd_tone.c` (sd_tone), `trx_cl_scan.c` (cl_scan).
- **Tests:** test_md_st.py step 4 is word for word with -DMD_ST_EXACT, plus an
  "as built" step per APPROX voice (md_st_voice_ser.c with an error ceiling, -85 dBFS).
  Quick run: all steps pass (the "as built" steps after a fix of the step's flags,
  checked with the same program over 60 scenarios); full run not done.
- Track instructions (analog-emu, quick run): TRX-BD 7,404, SD 11,611, XT 4,338, CP 14,385,
  RS 11,532, CB 11,192, CH 11,345, OH 11,262, CY 15,600, MA 13,098, CL 8,428, XC 4,333, B2
  9,760; EFM-BD 6,411, SD 10,170, XT 12,560, CP 11,991, RS 15,124, CB 15,959, HH 13,527, CY
  20,555; PI-BD 38,011, SD 29,914, MT 39,495, ML 25,781, MA 7,693, RS 34,980, RC 36,350,
  CC 36,768, HH 36,721.
- **Bank 0.20** (`out/md-st-0.20-core-2.0.0/`, with the core 2.0.0 SYX of 0.19): AUDIO
  713,200 bytes, parser PASS.
- Left (E, exact, larger): native renders for EFM-XT/CP/RS/CB/HH/CY (as efm_sd.c), for
  PI-BD/MT/ML/RS/RC/CC/HH (as pi_sd_block.c: their cost is spread over the whole DSP
  program, no one hot function), PI-MT/RS ROM decode order (small).

### Done (2026-11-17, later): exact natives for EFM, a PI rewrite rule, bank 0.21

- **EFM natives (word for word, MD_ST_EXACT; as built with their APPROX forms under
  -85 dBFS):** `efm_cp.c` (render), `efm_xt.c` (xt_core, xt_tail), `efm_rs.c` (render),
  `efm_cb.c` (cb_core), `efm_hh.c` (hh_resonance, hh_core), each with efm_clock.c's
  boundary; the renders rebuild their coefficients only when the clock moves. EFM-SD's
  operator scheme (48-bit phases as 24-bit halves, md_mul_floor48 steps) throughout;
  a rounded resonance (macr) is `md_round_sum2_less` (md-native.h, exact always).
  EFM-CY left generic (an exact native measured ~260 fewer, see above).
- **PI: `MAC_DIFF`** (md_port.py): x = mac(x, a, k); x = sub(x, mpy(p, k)) becomes
  md_acc_mac_diff (one product of a - p), exact; 2-5 sites in PI-BD, SD, MT, ML, RS.
  A "cheaper sum" form of pi_modal_filter.c (limit of w + a's high word) measured
  slower and was reverted. PI-RC/CC/HH: their heavy parts are native already.
- Quick run: 149 steps, all passed. Track instructions (analog-emu): EFM-XT 10,501,
  CP 9,170, RS 11,523, CB 13,003, HH 11,453; PI-BD 36,285, MT 36,780, ML 25,426; the
  others as in the bank 0.20 list above.
- **Bank 0.21** (`out/md-st-0.21-core-2.0.0/`, with the core 2.0.0 SYX): parser PASS.

### Done (2026-11-17, bank 0.22): accumulator products from the read, PI states in 32 bits

The user resampled a pattern of several machines from bank 0.21 against the exact build and
heard no difference: squeeze more. What was done (README, "Approximate forms"):

- **`acc` (MD_AP_ACC, md-acc.h):** a product into an accumulator from the EMAC's read alone,
  A0 = the read's 6 bits below the word plus half a unit (no mean error). Dropping A0 had
  broken most voices (a floor bias that recursions add up); this does not, except where a
  product must be exact. Those were found with a **site probe** (`out/md-st-tools/
  siteprobe.py VOICE`): it expands a generated voice and its natives, numbers every product
  call, and measures the error with each site approximated alone (host, md_st_voice_ser.c),
  then with all the good ones. What must stay exact: the coefficients made once a block
  (`*_modal_coefficients`, `drum_coefficients`, PI-MT's interpolated `complement` ones:
  -60 to -90 dBFS), an address made from A0 (`metal_activate`: -14 dBFS), PI-RC/CC/HH's
  fourth-order `metal_filter` (-27 dBFS), EFM-SD's phase step (-14 dBFS), the TRX filters'
  coefficients, PI's noise filter (feeds a threshold: -52 dBFS), and in PI-BD its modes and
  hammer filter (scenario 59 of 60, long low modes: -72 dBFS; `MD_MODAL_EXACT`). Exact
  forms: `md_acc_*_x`; `ACC_EXACT_FUNCS`/`ACC_EXACT_TEXT` in md_port.py rewrite those calls.
  Measure with 60 scenarios: 12 or 30 missed PI-BD's case. The probe's baseline must be
  the natives' exact forms too (it drops MD_AP_ACC from the voice).
- **PI modes and delays in 32 bits** (under MD_AP_ACC): an accumulator that starts a block
  from a word and is only read as a word is one int32, floor(acc / 2^18), stepped by the
  read plus a unit every other sample; the modes' loops two samples a turn with the EMAC's
  operands shifted once (`md_ar3s`, in md.c's self-test). pi_modal_filter.c, pi_modes.c,
  pi_delays.c (not the noise filter).
- **Exact:** TRX metal_source by runs with its six limited adds composed into one
  (trx_metal.c part 0; CH/OH -10 %, CY -8 %); PI modal excitation native
  (`pi_excitation.c`, BD/MT/ML); TRX-CP takes `floor` with its cp_filter6 exact
  (`MD_CP6_EXACT`): -19.5 %.
- **Tried and dropped:** `acc` where it saves nothing (TRX-BD, SD, CB, MA, CL, XT, XC, B2,
  EFM: an approximate product costs as many instructions as the exact one when its A0 is
  not used, GCC drops the low half); a branch-free sine fold (slower); rewriting
  acc_to_word(mpy) as the read's word (slower: the exact form already compiles to it).
  EFM's sines and FM loops take nothing (chaotic feedback: one LSB diverges).
- Track instructions (bench_all.py, bank 0.21 -> 0.22): TRX-CP 14,663 -> 11,797, RS
  11,969 -> 10,357, CH/OH 11,479 -> 10,280, CY 15,916 -> 14,681; PI-BD 36,700 -> 36,274,
  SD 30,359 -> 22,483, MT 37,561 -> 29,259, ML 25,922 -> 17,397, MA 7,844 -> 6,943, RS
  34,793 -> 26,574; the others unchanged (PI-RC/CC/HH 37.2-37.6k: the metal filter stays
  exact). Errors as built, 60 scenarios: all <= -89.3 dBFS (TRX-MA), most < -100.
- **Bank 0.22** (`out/md-st-0.22-core-2.0.0/`, with the core 2.0.0 SYX).

### Done (2026-11-17, bank 0.23): exact work for the voices short of 8 tracks

- **Resampler** (md.c): the weight row and the window stepped, not recomputed; the B
  buffer cleared four words a turn: -164 a track, every voice. The words are unchanged.
- **TRX filters in registers** (trx_metal.c parts 1-5): what mdkit's loops read back of
  their own writes (input window, last two outputs, the history pair) is kept signed in
  registers; only the outputs and the last history pair are stored. Word for word.
- **Natives** `trx_sd_block.c` (TRX-SD -17 %) and `trx_ma_block.c` (TRX-MA): each loop is
  a word plus products, one EMAC read and a limit; TRX-MA's rattle reads A0 only for its
  "value <= floor" test with equal high words.
- Track instructions (quick suite, bank 0.22 -> 0.23): TRX-SD 11,611 -> 9,818, CP 11,584
  -> 10,321, CB 11,192 -> 10,358, CH 10,123 -> 9,425, OH 10,041 -> 9,343, CY 14,346 ->
  12,463, MA 13,098 -> 10,172, RS 9,955 -> 9,791; the others about -165 (the resampler).
  Quick run 149 steps passed; errors at 60 scenarios all <= -89.3 dBFS (TRX-MA).
- Estimated tracks (factors as before): every TRX at 8 but CY (7, at the edge); EFM as
  in 0.22; PI-SD and ML 2, the other PI 1, PI-MA 6.
- **Bank 0.23** (`out/md-st-0.23-core-2.0.0/`, with the core 2.0.0 SYX): parser PASS.
- Left: EFM-CP is about 5 % from 6 tracks, EFM-SD/XT/RS/HH 3-10 % from one more; only
  exact work applies (FM feedback).
- **Tried and dropped: a compact EFM sine.** The quarter (32 KB) rebuilt exactly from 129
  quadratic segments of 64 words (a + (k1 r + k2 r^2 + 2^12) >> 13) plus a 2-bit correction
  a word (-1..1 measured): 3 KB, word for word. It cost +15 to +40 % instructions (EFM-SD
  9,518 -> 12,335, EFM-CY 20,273 -> 27,306) and, in the cache model (cycles ~ 0.85 instr +
  40 misses, trace.py + cache.py), saved nothing: cold, each read touches two tables (more
  misses); warm, the sine misses fall (EFM-CB 117 -> 64) but the instructions eat it. The
  same model gives TRX-BD and EFM-SD alike cold misses (~400), so the sine does not explain
  EFM's ~1.7 cycles an instruction on the unit; what does is not known (the unit's
  measurement of bank 0.23 first).

### Done (2026-11-18, bank 0.24): the voice picker in place of TYPE

The user asked for no TYPE control: selecting the machine shows a box with the voices,
the one chosen loads and its knobs show, without TYPE. Done in md-st alone, no core change:
- TYPE stays the ninth control (the Sound keeps it) but on no page (one SYN page), with
  the default 30, "none chosen". `core_machine_defaults` (mods/core-st/machines.c) writes
  the descriptor's defaults of controls 9.. when a machine is selected, never on a Sound
  or project load.
- `main.c`: the machine's draw paints the list while the Sound on screen has none; its
  module `md_picker_module` takes the SYN view's ENC (cursor) and pre-native KEY (YES =
  12: `api->edit` of the hidden control, its release swallowed), as the samplers' browser
  does (mods/samplers-st/browser.inc, input.c). YES with a trig held or recording does
  nothing. `audio/md.c`: TYPE >= 30 rests and plays nothing.
- Tests: tests/md_st_picker_check.c (main.c on the host, firmware services faked) and the
  glue check's "no voice chosen" case; both in tests/test_md_st.py step 5.
- Not seen on a unit: the list's drawing (positions copied from the browser's), the
  encoder's step per detent, and whether re-selecting MD on a track that has it opens the
  list again (it should: the defaults run at every selection).
- Core first: a list picker for a machine could be a core service (the samplers' browser
  is the same shape); not moved yet.
- Bank 0.25 (user's first unit test of 0.24): UP/DOWN (keys 14/15, FUNC: a page) move the
  cursor; no "YES: LOAD" line (7 rows); NO closes the list and opens the machine selector
  with the native's opener `400311a8(view)` (message 20's path: `400c2d84`, set selecting
  `400c3334(model, 1)`, `40030f06`); selecting MD again on a track that has it opens the
  list (its defaults do not run then): YES in the selector (predicate `400c3340`, the
  model's byte +1c8) arms the track. The picker ignores the view while it shows the
  selector (0.24 could have taken the selector's YES on a Sound with no voice).
- Bank 0.26: the voice's name in a box over the knobs for a second (the user chose it
  from three mock-ups: the name in place of the pattern's, this box, a tag). The module
  counts `ST_EV_TICK` (30 Hz); idle starts the box when the view, track or voice
  changes or after a gap of more than two TICKs (another page, the selector), and asks
  for the redraw that ends it. Letters are md-st's 5x5 glyphs at two pixels a dot (the
  firmware's small font only). Not seen on a unit: whether TICK and idle run at the same
  30 Hz there (the box's length), and the box's place over the first row of knobs.
- Bank 0.27: the box in the middle of the screen (rows 22..41). Re-selecting MD after a
  SYN double-tap did not open the list in 0.26: the YES there is not a key of the SYN
  view (that path reads its model at +0x98). Now the module subscribes to ST_EV_NATIVE,
  narrowed to MACHINE_ALLOWED (host API 5 `native_interest`): the core raises it at the
  entry of the native select `40025594` for every selection; inside, the native
  compares the old type (Sound +0xa2) and skips the defaults when equal. The picker is
  armed when that track's SYN model (remembered from its views, +0x6c) shows the
  selector; Sound/kit loads (also through 40025594) do not.
- Bank 0.28: 0.27's MACHINE_ALLOWED arming reopened the list after NO (NO opens the
  selector, whose preview of the highlighted machine runs the native select). Dropped.
  Now the module's global `ST_EV_KEY` (before the native, any view) arms the track whose
  remembered SYN model shows the selector when YES is pressed (not a repeat). Cancelling
  the selector (NO) arms nothing.
- Bank 0.29, the flow the user described: SYN double-tap -> machine selector; YES on MD
  -> voice list; there YES loads MD with that voice, NO -> machine selector; NO there
  closes it; YES on MD -> list again. 0.25-0.28 opened the selector again from NO with
  `400311a8(view)` alone, which left the native's selector state half set (two NOs, the
  list reopening). Now the selector is never reopened: YES on MD in it is taken (global
  KEY, the view's machine MD: the preview selects the highlighted machine) and the list
  is drawn over the selector (DRAW|VIEW of that SYN view; keys and encoders by the global
  KEY/ENC, SYSTEM INFO's way; redraws by TICK ctrl+0x20). YES in the list passes to the
  native, which confirms MD; the voice is set at the next MD idle of that track
  (`md_pend`, after the defaults). NO only hides the list. The 0.25-0.28 code (`again`,
  `400311a8`) is gone. Not seen on a unit: that the view's machine is the highlighted one
  while the selector is open (else YES on MD from another machine confirms natively and
  the page list follows the defaults, and YES on another machine from MD opens the list).
- Bank 0.30: the user confirmed 0.29's flow works on the unit (the preview assumption
  holds), but the list was drawn under the selector's machine list (a view drawn after
  the SYN view). The list now draws in the global `ST_EV_DRAW` (over the frame);
  DRAW|VIEW only remembers the SYN views.
- Bank 0.31: 0.30 works on the unit. The user asked about knobs without labels (15 voices
  have 3 to 7 controls). They reach nothing: md.c sends a voice its own controls, and
  the TwinShot update (AUDIO 400097dc) reads native words 12-17 and 19 only (not 1a,
  knob G). tests/md_st_spare_knobs.c renders each such voice with only those knobs
  moved: word for word the same. The core copies layouts at bind, so a per-voice layout
  needs the core; instead the machine's draw paints the spare places black and the
  view ENC takes their encoders.
- Bank 0.32: from a track with another machine, the selector's list then the page list
  (numbered) both showed. While the native confirms, the YES release goes through the
  SYN view that still shows the old machine, and 0.29-0.31 dropped the pending voice on
  seeing another machine; MD's defaults (no voice) then opened the page list. The
  pending voice now lives two seconds (MD_PEND_TICKS, then dropped so a later Sound load
  keeps its own voice) and is set only on an MD view.
- Bank 0.33: 0.32 still showed the numbered page list from another machine. Likely the
  native writes MD's defaults (no voice) after the page has shown MD and the voice was
  set: the pending voice is now held for its two seconds and set again whenever the
  track loses it. The selector's track is now the SYN view last drawn in selector mode
  (`md_sel_view`; several tracks' views may share a model, so the old scan of remembered
  models could name another track), and a remembered view must be one of the core's
  machine parameter view classes (vtables). Not seen on a unit.

### Done (2026-11-18, bank 0.34): one instance next to seven SY SWARM, EFM first

The user's measurements (CPU A, track 1 MIDI): seven SP ONE SHOT mono 53 %, seven SY
DTONE 77 %, seven SY SWARM 86 %: SWARM is the heaviest, and leaves about 9 % for one MD
track. EFM-CP (about 9 %) sounds right next to it, so its instructions are the target:
bench 9,174. The user chose to change the sound a little where needed, EFM first, and
wants a fixed table of instances allowed per voice, taken from that worst case.

- `fm` (README, "Approximate forms"; `native/efm_fm.h`): the operators' phases in 32 bits,
  steps from the read, the rounded MAC from the read, the sine's quarter as it is.
  EFM-RS, CB and HH have their operator loops rewritten under `MD_AP_FM`; EFM-SD and XT
  take the `md_mul_floor48` and `md_round_sum2_less` forms only.
- Bench (bench_all, exact before -> as built): EFM-SD 10,160 -> 9,173, XT 10,499 -> 8,778,
  RS 11,541 -> 7,970, CB 13,058 -> 7,856, HH 11,475 -> 7,759. All at or under EFM-CP.
- These voices are no longer mdkit's word for word at high MOD: the test compares their
  level and third-octave spectrum (`tests/md_st_voice_spectrum.c`). A/B renders (mdkit,
  then the bank's) for the user in `out/md-st-ab/` (not committed).
- A 16-bit half-period sine (`md_sine16`) was tried first: 32 KB over the AUDIO reserve,
  and SD bands 10 dB off at high MOD. The quarter is 1 LSB.
- Bank 0.34 (`out/md-st-0.34-core-2.0.3/`, with the 2.0.3 SYX): AUDIO 711,152 bytes,
  parser PASS.
- Next: EFM-CY (bench 20,779), then the PI voices (all but PI-MA need about -53 to -79 %),
  then the instance table.

### Done (2026-11-19, on main 121e965, core 2.2.0): EFM-CY, the whole EFM sine

The worktree moved to main 121e965 (core 2.2.0: the bank's AUDIO window is 4 MiB,
bank contract 0x53594606; docs/AUDIO-MEMORY-MAP.md). The quick suite passed there
unchanged. The user wants no test bank until everything is done.

- `FULL_SINES` (md_port.py): with the room, the EFM sine (efm_cb_sine, every EFM
  voice's but XT's) is also kept whole, 32,768 signed words (+128 KB): md_sine_word
  reads it with one load, the same words; `md_fm_sine` (efm_fm.h) too, so the `fm`
  forms read the exact sine. EFM-SD and RS as built are back under -85 dBFS (the
  folded quarter's 1 LSB was what made them drift at high MOD).
- EFM-CY native (`native/efm_cy.c`, exact; NATIVE): the FM phase steps by the rate
  modulo 2^15 and the feedback only offsets the index (mdkit adds it before the mask
  and takes it off after). Then `mid`, `fm` (APPROX): one gain a sample for MOD x the
  shared envelope, the decays and the resonators from the EMAC read, and the two
  small products (scan gain <= 0xca80, FB <= 12954) as 32-bit multiplies.
  Spectrum check: levels within 0.21 dB, bands within 2.1 dB.
- Bench (bench_all, before -> now): EFM-CY 20,779 -> ~8,990; EFM-CP 9,174 -> 8,288,
  SD 9,173 -> 8,862, RS 7,970 -> 7,294, CB 7,856 -> 6,750, HH 7,759 -> 7,022. Every
  EFM voice is at or under the old EFM-CP cost (the one next to 7 SY SWARM).
- PI: table unpacking does not pay (their sines are not in the profile); their cost
  is the 48/56-bit accumulator helpers, conversions and limits. PI's budget beside 7
  SY SWARM is about 7,900 a track (1.14 % CPU a 1,000 instructions).

### In progress (2026-11-19): the PI voices to 15 % of AUDIO CPU each

The user relaxed the goal: one instance of each voice within 15 % (PI about 13,100
instructions a track, EFM 15,000, TRX 25,200; every TRX and EFM already fits). Per
voice limits are set at the end.

- Exact natives, word for word (60 scenarios): `native/pi_bd_block.c` (bd_block:
  PI-BD voice ~34,500 -> 28,868) and `native/pi_ml_block.c` (ml_block: PI-ML voice
  15,148 -> 14,739; track ~16,600). PI-BD with the `acc` modal filter (no
  MD_MODAL_EXACT) measured 20,560 with identical spectrum: not applied yet.
- Tried and reverted: two modes a pass in pi_modal_filter.c's acc path (register
  spills: 15,568 against 15,148).
- Done, shared (exact unless said): every voice's `*_s24` is md_s (port_helpers); the PI
  render copy tests the trigger once a block (port_render); pi_excitation.c reads the
  voice's sample table directly (PI_SAMPLES) and skips the per-sample limit when the
  block's 32 steps cannot leave the 24 bits, its interpolation `mid` where the voice
  has it (MD_EXC_EXACT for PI-BD); native/pi_coefficients.c for the nine modal voices
  (MD_COEF_EXACT for PI-BD); PI-ML's one-poles as int32 under `acc`.
- Tried and reverted in the modal filter's acc path: the bus limited once (-16 dBFS:
  PI-ML clips its partial sums), the bus as Q31 with sats adding the limited mode word
  (-46 dBFS: its modes pass full scale on their own). Both noted in the file.
- Bench now (bench_all; ~CPU at 1.14 % a 1,000): PI-MA 6,511 (7.5 %), ML 14,029
  (16.1 %), SD 22,061 (25.3 %), RS 26,228 (30 %), MT 26,301 (30.1 %), BD 29,248
  (33.5 %), RC/CC/HH ~36,800 (42 %). TRX 2-3 % cheaper too (md_s).
- PI-RC: pi_metal.c (the 5-tap metal filter, exact: `acc` took -27 dBFS) is 19,300 of
  its 36,300; pi_metal_advance 3,800; metal_read/write 4,700.
- Profile by caller: out/md-st-tools/prof_cf.py WORK TYPE, then a callers script
  (llvm-addr2line -i over WORK/pcs.txt, helper lines charged to their caller).
- The `struct` form (APPROX, `MD_AP_STRUCT`; md_port.py `PI_STRUCT`): the voices that take
  it are judged by level and third-octave spectrum (SPECTRUM: tests/md_st_voice_spectrum.c,
  1.5 dB and 12 dB), and by the user on A/B renders (`out/md-st-ab/<voice>-ab.wav`:
  mdkit, then the build, the same hits; not committed). Each voice leaves out the modes
  that weigh least on its sound (`MD_PI_SKIP_MODES`, a mask in the voice's preamble: the
  modal filter, drum_modes and both coefficient files skip them). Measured one mode at a
  time (bands, worst scenario):
  - PI-ML: 0 5.9, 1 8.2, **2 3.0**, 3 3.9, 4 4.5, 5 6.6, 6 7.7, 7 9.9 dB; mode 2 left out.
  - PI-SD (with the bus once and the noise word: 2.1 dB): 0 8.9, 1 13.0, 2 13.0, 3 17.7,
    **4 2.2**, 5 6.4, **6 2.0**, **7 2.1**; 4, 6 and 7 together 2.2 dB.
  - PI-RS (2.4 dB with its block native): 0 9.8, 1 14.5, 2 15.0, 3 7.3, **4 1.8**,
    5 4.6, **6 1.3**, **7 0.5**; 4, 6 and 7 left out, as PI-SD.
  Under `struct` drum_modes also limits the bus once, after the last mode (PI-SD: -1,310
  instructions, bands 2.1 dB), and drum_delays keep the noise filter in one word (-353).
- Exact, every form: `native/pi_drum_coefficients.c` (PI-SD, PI-RS) makes modes 4 to 7's
  coefficients again only when PTCH, DEC, the eighth parameter or the variant change
  (`coef_key` in the state, EDITS PI_COEF_KEY): they do not follow the tension's pitch
  (PI-SD -838, PI-RS -2,669 a block). `native/pi_rs_block.c`: rs_block natively, as
  pi_sd_block.c (PI-RS with `struct`: 23,575 -> 18,644, voice alone).
- Tried and dropped: the mode feedback unlimited (PI-SD's modes pass full scale: level
  1.97 dB, bands 13.7 dB); drum_modes and drum_delays not inlined (+160); the EMAC
  asm not volatile (+105: GCC schedules it worse); md_sat(a >> 6) branchless with two
  sats.l (+100 to +500 instructions: the compiler's compares are shorter in the common
  case).
- `native/pi_mt_block.c` (exact; under `acc` the same words as before): the one-poles as
  one word each, the delay mix's shared product, the feedback loop in integers (it adds
  the old feedback into the source, so it is not dead). PI-MT 24,400 -> 18,886 (voice).
  PI-MT also leaves out modes 4 to 7 (0.96, 0.41, 0.42, 0.35 dB alone; together 0.18 dB
  of level, 0.9 dB in bands): 15,595.
- PI-BD under `struct`: its excitation, coefficients and modes take `acc` and `mid`
  (PI_BD_EXACT, md_port.py: exact outside `struct`), as WORK measured (identical
  spectrum); modes 2 to 7 left out (0 34.4, 1 28.5, 2 0.25, 3 1.2, 4 0.25, 5 0.32 dB
  alone; 2 to 7 together 0.03 dB of level, 1.17 dB in bands); its branch one-poles as
  one word under `acc` (the same words), the hammer filter too under `struct`.
  27,841 -> 13,088 (voice).
- PI-RC, CC and HH take `acc` with `struct` (no modes left out yet): the metal filter's
  products from the read (pi_metal.c, MD_PM_MAC2/3): PI-RC 35,000 -> 30,535, level
  0.54 dB, bands 4.7 dB. Their cost is now the metal filter (7,700, its accumulator kept
  56-bit), metal_coupling's delay reads (3,800) and writes (2,200), metal_advance (3,800)
  and metal_read/metal_write's address decoding (3,300): next, natively.
- Metal natively (pi_metal.c; PI-RC voice alone, as each step went): the delay reads and
  writes index s->delay directly when their 256-word page is inside it (md_pm_page:
  metal_read/metal_write's address decoding was 3,300), and metal_advance has a closed
  form for modulo 2^k - 1 with 0 < |delta| <= modulo (every call): 30,535 -> 24,175;
  the metal filter under `struct` as one int32 in 2^-6 of a word with its half-unit
  parity h (same words as the 56-bit `acc` form over the 60 scenarios; an int64 version
  gave the same words at 27,641): 21,830; index paths in the delay loops: 20,030;
  metal_coupling (its mix under `acc` one read of delay_gain (x + y) + excitation_gain z),
  metal_amplitude (every product one exact read) and metal_output (its loop in integers)
  natively, parts 3 to 5: 19,002; the filter's window in registers and its four products
  before the stored word as one read (md_ar4, one unit for the two half units): 17,986.
  Spectrum: RC 0.57 / 4.6 dB, CC 0.40 / 2.3 dB, HH 0.00 / 0.00. Exact without `struct`.
  RC's code a block 7,984 B (was 9,392), misses 480 + 247.
- Then, exact unless said (voice alone, vcheck's 60 blocks): drum_delays under `acc` as
  separate loops over each ring's runs, the rattle's product once (PI-SD -90, PI-RS -320);
  PI-SD's block feedback as one word with its half-unit parity (-290; PI-RS's leaks 0.1 a
  sample and outgrows that form: kept 56-bit, 1.6 dB off when tried); PI-BD's branches over
  their ring's runs and its writes by index, with the source's product shared under
  `struct` (13,088 -> 12,147); PI-MT's lines over their runs (-240) and its POS/TUNE mode
  tables kept until a knob moves (`table_key`, `tables` in the state, EDITS PI_MT_TABLES;
  their ROM reads were 2,200): 13,124; the coefficients' damping word read from the
  voice's table (MD_COEF_DAMP); the reads past each excitation sample's end, where the ROM
  is empty (PI_SAMPLE_GAP; PI-SD and PI-RS in their blocks): PI-RS 18,322 -> 15,643,
  PI-BD -880, PI-SD -190. Tried and dropped: metal_excitation natively (no gain).
- These past-the-end reads come after the hit's sample: bench_all's 16 blocks after a hit
  (the worst block) see them only for PI-RS, whose sample is short.
- Audible cuts (the user, 2026-11-19), under `struct`:
  - PI-SD and PI-RS's three delay lines at half rate (pi_delays.c: 16 words a block a
    line, delays 122.5 / 128.5 / 165.5 from two taps, one-pole gains 2 g - g^2, the words
    between as means; the noise, rattle and modes stay at full rate): SD 15,270 -> 14,095
    (bands 2.27 dB, was 2.23), RS 15,643 -> 14,532 (3.4 dB, was 2.4).
  - PI-MT also leaves out mode 2 (modes 2, 4-7: level 0.95 dB, bands 4.0 dB): 12,132.
    Modes 2-7 measured 11,332 at 1.42 dB / 5.4 dB, too near the level limit.
  - More modes in PI-SD (5: bands 11 dB) or PI-RS (5, 3: 8 dB): not taken.
  - Tried and dropped: the metal coupling's four lines at half rate (16 dB of level off,
    and no fewer instructions through metal_read/metal_write); the metal filter's A0
    stores left out (-1,235 and a closer spectrum, but metal_excitation reads Y 32 as its
    first noise word every block, which would then stay fixed: a tone at the block rate).
- Then ("hasta el objetivo"):
  - PI-MT's three lines at half rate too (pi_mt_block.c; an odd SIZE length from two taps):
    12,132 -> 11,024, bands 3.7 dB. PI-BD's (pi_bd_block.c, whole words 123/129/166):
    11,253 -> 9,782, bands 1.2 dB. PI-SD's half delays in whole words (MD_PD_WHOLE,
    PI_STRUCT_DEFINES): 14,095 -> 13,480, bands 2.3 dB (PI-RS took it at 1.41 dB of level:
    kept two taps). PI-ML leaves out modes 2 and 4: 11,225 -> 10,400 (0.78 / 5.0 dB).
  - Tried and dropped: PI-SD/RS's modes at half rate (poles squared, Y' = 2 Y - Y- + 4 a Y +
    b Y- + C (x1 + A x0 - B x-1), on the odd samples, the even ones as means): the sound
    held (bands 2.3 dB) but it saved only 70 (the per-block setup and the bus expansion
    eat the halved loop); more modes out in PI-SD (5: 11 dB) or PI-RS (fails).
  - The knob glide heard in the A/B renders' first hit is mdkit's control clock (each 12
    blocks a quarter of the way to the target), in both halves: the renders set the knobs
    right after init.
- Bench now (bench_all, a track, 16 blocks after a hit; ~13,100 = 15 % at the old factor):
  PI-BD 11,643, PI-ML 12,386, PI-MT 12,955 (under), PI-SD 15,546, PI-RS 16,572,
  PI-RC/CC/HH ~20,200 (from ~36,800), PI-MA 6,511. PI-SD after a hit: delays 5,300
  (half lanes 1,400, mix 1,200, rattle 1,000, noise filter 700, excitation filter 700),
  modes 3,500, block 3,350, glue 1,700. What is left is the snare's own sound (the noise
  and rattle are its top end above 11 kHz) or a few hundred each.
- **Bank 0.35** (`out/md-st-0.35-core-2.2.0/`, with the 2.2.0 SYX, byte for byte the one on
  the unit): every PI voice as above, for the user's CPU reading of one track each. AUDIO
  840,176 bytes. Parser, smz-boot and trial-boot PASS; the regressions stop at the recorder's
  module-list check (st_bank_modules.py lists the normal banks; md_picker_module is not in
  them), as for every MD bank. A/B renders now start the hits after half a second for the
  knobs to reach their values (the control clock's glide).
- Then (exact, the same words: hashes RC 013e7887, CC fc79ce41, HH 26576e67 before and after,
  vcheck's 60 blocks): the metal's EMAC operands shifted once (md_ar1s, md_ar2s, md_ar4s in
  md-native.h: a pair's first operand by 8, its second by 6; the product is the same whichever
  takes which, so a word goes in as w << 8 or (w << 8) >> 2 with no separate md_s; about 3,200
  instructions a block were sign extensions). metal_filter under `struct` two samples a turn
  (h1, h0 constant) and its limit tested once: 17,986 -> 16,664 -> 15,028 with the next two;
  metal_delay_read's old words kept as values: -926; metal_delay_write's sums and differences
  of the shifted words (negative: -(x0 - x1), -(x0 + x1)): -115; both delay loops' limits tested
  once for the two words and straight pointers when neither page wraps in the eight: -305;
  the coupling's mix: -58. That gave PI-RC 14,970 instructions but, in the cache model
  (cold, cycles ~ 0.85 instr + 40 misses), no gain: the copies the block runs had grown the
  code from 7,984 to 8,960 B (+62 I misses), 46,312 -> 46,134 cycles. So, weighed by cycles:
  metal_delay_write as one loop, negative applied to x0 by a mask (45,413); metal_filter under
  `struct` noinline, one copy for its three calls instead of three inlined (44,288, code
  8,048 B); a rolled filter loop was worse (45,878: +1,000 instructions). Now PI-RC 15,380,
  CC 15,515, HH 15,293 (vcheck); after a hit (bench_all) RC 17,469, CC 17,560, HH 17,328 (from
  ~20,200); cache model RC 17,234 instr, 484 + 257 misses, 44,288 cycles (was 19,896, 477 + 258,
  46,312); CC 44,801, HH 44,449. Spectra unchanged. Weigh any further cut by the model's
  cycles, not by instructions: a copy of code the block runs costs about 47 instructions a
  16-byte line. **Bank 0.36** (`out/md-st-0.36-core-2.2.0/`, the same 2.2.0 SYX, sha 8faaf194):
  this metal, everything else as 0.35; AUDIO 844,272 bytes. The 2.2.0 parser
  (`out/md-st-0.35-core-2.2.0/smz-parser.exe`), smz-boot and trial-boot PASS. The regression
  runner stopped at common-events, for 0.35 too, because it was run with the 64-bit host
  compiler: that host test keeps pointers in 32-bit words; docs/TESTS.md gives the i686 one,
  with which it passes. A/B renders of RC, CC and HH redone.
- **Rendered always** (the user, 2026-11-20: "que consuma siempre, al menos ahora para medir"):
  `MD_REST 0` in md.c, a chosen voice renders on every block from its load, as the stock
  machines do; `MD_REST 1` keeps the old rule (tests/md_st_glue_check.c is built both ways).
  Measured first (host, a minute after one hit, the rule's +-4 words): PI-BD, MT and RC never
  rested at any setting, SD, RS and CC at about half, ML, MA and HH within 0.2-3.8 s; mdkit's
  own PI-BD, MT and RC never rest either (tails of 6-386 words). So the rule saved little for
  PI. A later rule could rest on the 16-bit output (words within -128..127 round to 0).
- **Instance limits: the measurement plan** (2026-11-20). CPU A's base depends on the other
  tracks' machines (the user: all MIDI 68 %, all ONE SHOT 46 %, all TWINSHOT 37 %); the base is
  all TwinShot, whose stock chain the MD track runs too, so a track changed to MD adds only the
  voice, the resampler and the glue. The user measures six voices (TRX-XT, TRX-BD, EFM-CP,
  PI-MA, PI-SD, PI-RC) loaded and not played, PI-SD and PI-RC also played; the cache model below
  (trace.py + cache.py, cold, one track after a hit, cycles ~ 0.85 instr + 40 misses) is
  calibrated on them and gives the other 24, and the voices near a limit are checked on the unit:

  | voice | code B | instr | I + D misses | cycles |
  |---|---:|---:|---:|---:|
  | TRX-BD | 3264 | 7,108 | 189 + 219 | 22,361 |
  | TRX-SD | 3776 | 9,600 | 226 + 230 | 26,400 |
  | TRX-XT | 2176 | 4,077 | 134 + 167 | 15,505 |
  | TRX-CP | 4624 | 10,115 | 271 + 172 | 26,317 |
  | TRX-RS | 3280 | 9,868 | 197 + 206 | 24,507 |
  | TRX-CB | 3360 | 10,202 | 202 + 233 | 26,071 |
  | TRX-CH | 4032 | 9,229 | 246 + 173 | 24,604 |
  | TRX-OH | 4112 | 9,183 | 247 + 174 | 24,645 |
  | TRX-CY | 5152 | 12,290 | 306 + 186 | 30,126 |
  | TRX-MA | 4528 | 9,963 | 274 + 179 | 26,588 |
  | TRX-CL | 3040 | 7,905 | 182 + 220 | 22,799 |
  | TRX-XC | 2112 | 4,043 | 131 + 168 | 15,396 |
  | TRX-B2 | 3520 | 9,570 | 210 + 176 | 23,574 |
  | EFM-BD | 2160 | 6,065 | 145 + 224 | 19,915 |
  | EFM-SD | 2800 | 8,659 | 173 + 227 | 23,360 |
  | EFM-XT | 2960 | 8,658 | 181 + 201 | 22,639 |
  | EFM-CP | 2528 | 8,085 | 157 + 200 | 21,152 |
  | EFM-RS | 2896 | 7,091 | 179 + 263 | 23,707 |
  | EFM-CB | 3088 | 6,545 | 189 + 263 | 23,643 |
  | EFM-HH | 3120 | 6,800 | 191 + 231 | 22,660 |
  | EFM-CY | 2256 | 8,786 | 142 + 431 | 30,388 |
  | PI-BD | 5088 | 11,412 | 298 + 269 | 32,380 |
  | PI-SD | 6960 | 15,285 | 420 + 294 | 41,552 |
  | PI-MT | 5296 | 12,752 | 319 + 285 | 34,999 |
  | PI-ML | 4416 | 12,152 | 266 + 218 | 29,689 |
  | PI-MA | 2384 | 6,267 | 147 + 139 | 16,766 |
  | PI-RS | 6768 | 16,360 | 407 + 283 | 41,506 |
  | PI-RC | 8080 | 17,226 | 487 + 257 | 44,402 |
  | PI-CC | 8160 | 17,311 | 495 + 260 | 44,914 |
  | PI-HH | 8048 | 17,085 | 489 + 262 | 44,562 |

  The unit (bank 0.37, core 2.2.0, CPU A, all tracks TwinShot 37 %, one track MD, the voice
  loaded; played with 16 trigs at 120 BPM the same): TRX-XT 44, TRX-BD 47, EFM-CP 47, PI-MA 44,
  PI-SD 57, PI-RC 61 %, so 7, 10, 10, 7, 20 and 24 points. Fit: points = 0.564 x kcycles - 2.2
  (within 1.2 of each reading, the meter's step is 1). Predicted: TRX-XC/XT 6.5, PI-MA 7.3,
  EFM-BD 9.0, the other TRX and EFM 9.7-12.8, PI-ML 14.5, TRX-CY 14.8, EFM-CY 14.9, PI-BD 16.1,
  PI-MT 17.5, PI-RS and SD 21.2, PI-RC, HH and CC 22.8-23.1. Next: whether instances add
  (several tracks of one voice share its code), all-SWARM on this core, and the user's choice of
  rule (an MD-only budget or room next to the heaviest stock machines).
- **The rule chosen (the user, 2026-11-20): a budget over the project's real machines**, the
  selectors offering only what fits. Analog tracks do not use AUDIO: no limit there. The user
  measures the 16 non-MD digital machines (one track with it, the others TwinShot). The table
  would travel in the SMZ4 bank (read only when the bank loaded; without it the menu is stock's).
  Found in OS 1.42's MAIN (main.bin at 0x40000400; addresses as run):
  - the machine menu is built by the function around 0x400264ca..0x400265be: a loop over the
    machine types calls the availability function 0x400a64f2 (type, track) at 0x40026528 and
    appends the available ones (0x40179692); the core already hooks this builder (alloc
    0x400264ca, capacity 0x40026508, bound 0x40026554, finish 0x400265b6) and the availability
    function itself (core_machine_availability, machine-hooks.s: stock types skip to stock);
  - the native select 0x40025594 also calls availability (0x400255ce) and refuses an
    unavailable type; it runs for Sound/kit loads and for the selector's preview of the
    highlighted row too, so a 'does not fit' answer must be given only while the builder runs;
  - a track's machine type is its Sound's byte +0xa2 (0x400249ce reads it).
  So hiding what does not fit is the builder's availability under a flag; showing it disabled
  and skipping it needs the list's row names (name hooks, stock types also) and its cursor moves
  (0x4002f2b2 / 0x4002fbb2 are already core sites), not yet studied. Loads and p-locked TYPE
  bypass any selector: an over-budget MD voice would rest (silent, shown) until it fits.
  The user chose hiding (2026-11-20).
- The user's readings (bank 0.37, one track with the machine, the other seven TwinShot, base 37 %),
  points over TwinShot: BD MODERN 6, SD BASIC 5, SD VINTAGE 7, PC CARBON 4, CP VINTAGE 9, CY ALLOY 4, SY BITS 6, SY CHORD 6, SY TONE 4, SY TOY 5, SY SWARM 7, SY FM DTONE 10, SP ONE SHOT 3, SP ONE SHOT stereo 15, SP WARP 3, SP WARP stereo 18, SP REPITCH 4, SP REPITCH stereo 19, SP SLICE 2, SP SLICE stereo 18.
  They do not add up per track: all eight ONE SHOT read 46 % (9 points, 1.1 a track) against 3
  for one; eight stereo samplers read 99 % (MACHINE-REFERENCE, 2026-11-10: about 7.8 a track)
  against 15-19 for one. The first track of a kind pays its code into the caches, the next ones
  much less: a fixed weight a track would allow three stereo samplers where seven play. The
  budget needs a first-track and a next-track weight a kind (an eight-track reading each).
- Eight-track readings (the user, bank 0.37): 8 SP stereo 73 % (the user: take it for the mono SP
  too), 8 SY FM DTONE 75 %, 8 SY SWARM 88 %, 3 TRX-BD + 5 TwinShot 64 %, 2 PI-RC + 6 TwinShot
  86 %. Next-track weights (reading - 37 - first) / (n - 1): SP 2.6 (first 18 stereo), FM DTONE
  4.0 (first 10), SWARM 6.3 (first 7), TRX-BD 8.5 (first 10), PI-RC 25 (first 24: an MD voice
  shares nothing). Model: a kind costs first + (n - 1) next; the budget is 95 - 37 = 58 points
  over the eight digital tracks (analog tracks do not use AUDIO). Unmeasured stock kinds: next =
  first (safe); MD voices: next = first.
- **The rule is dropped** (the user: too complex). The question is now how far the dearest
  voices must come down to play next to a real project. The user's: PI-RC + 3 SY SWARM +
  ONE SHOT + SY TOY + SY CHORD + SY TONE read 99 % and broke up; the weights give 37 + 24 +
  19.6 + 3 + 5 + 6 + 4 = 98.6 %, so they hold for a mixed project too. Its seven other
  tracks take 37.6 points: at 95 % that leaves about 20 for one MD voice.
- **Cache-footprint cuts** (cycles in the cold model; words unchanged, hashes RC 013e7887,
  CC fc79ce41, HH 26576e67, SD cbe8a71d; RS exact and its spectrum unchanged). A byte of code
  or data a block touches costs about 2.5 cycles, so code run once a block is weighed by its
  bytes:
  - the metal's metal_read, metal_write and metal_product noinline (md_port EDITS: one copy
    instead of one at each call): RC 44,402 -> 42,993 -> 41,868;
  - metal_delay_read as one loop with the index's wrap (its straight-pointer copy also ran most
    blocks): 40,212; the same in metal_delay_write was worse (its wrap copy seldom runs): kept;
  - md-weights.h stored in the order a block reads it (row k is phase 147 k mod 160, 32 rows
    repeated at the end; MD_WEIGHT(p) for phase p): a block reads 32 rows in a row, 256 B
    instead of 768 B: every MD voice gains (TRX-BD 22,361 -> 21,044, RC 39,654);
  - PI-SD and RS: md_pd_half noinline, the WHOLE form in one loop with the wrap; sd_rom noinline
    (a prototype with the attribute, md_port PI_SD_EDITS): SD 41,552 -> 39,110, RS 41,506 ->
    39,747;
  - tried and dropped: metal_advance noinline (44,099: it runs in loops), the metal's
    once-a-block functions under optimize("Os") (40,022: their helpers went out of line).
  Model now (fit 0.564 x - 2.2): TRX-XT 5.8, PI-MA 6.5, EFM-CP 9.0, TRX-BD 9.7, PI-ML 14.0,
  PI-BD 15.4, PI-MT 16.6, PI-SD 19.9, PI-RS 20.2, PI-RC 20.3, CC and HH 20.6 points (RC read
  1.2 over the fit before, SD 1.2 under). **Bank 0.38** (`out/md-st-0.38-core-2.2.0/`, the same
  2.2.0 SYX): AUDIO 823,792 bytes; parser, smz-boot and trial-boot PASS.
- The unit on bank 0.38: PI-RC alone 59 % (22 points, from 24). PI-RS with 2 SWARM, CY ALLOY,
  ONE SHOT, SY TOY, SY CHORD and SY TONE, and with 1 SWARM, SY BITS and the same five: 99 %,
  breaking up. The same two projects with TwinShot in the MD track: 78 % and 77 %, so those
  seven tracks take 41 and 40 points where their single-track weights sum to 35.3 and 35: in
  a mix each machine's code evicts the others', and the weights read beside seven TwinShot
  (one small shared code) undercount by about 15 %. The room such a project leaves at 95 % is
  17-18 points: PI-ML (14), BD (15.4) and MT (16.6) fit; SD (~19-20), RS (~21), RC (22), CC and
  HH (~21) need 15-23 % more.
- **Probe bank 0.39** (`out/md-st-0.39-core-2.2.0/`, the same 2.2.0 SYX): 0.38 with AUDIO's
  instruction cache half-locked on the MD's code (md.c, MD_ICACHE_LOCK, only when MD_STOCK is not
  defined: never in the host or emulator builds). AUDIO's boot writes CACR a50ce100 as MAIN's
  does (audio.bin, its only movec), so icache-lock-st's values: every 4,096 md_render calls,
  a40ce100 before one track's render and a408e800 after it, the render's first two lines a set
  in ways 0-1 (no INTOUCH table: the invalid ways fill first). Interrupts are not masked. If it
  does not pay (the user, 2026-11-20): audible cuts with A/B instead. The readings to take:
  PI-RC alone (0.38: 59 %), the two PI-RS projects (0.38: 99 %, breaking up), the same with PI-ML.
  The unit: readings all but the same as 0.38 (the user, 2026-11-20): nothing gained; the probe
  is removed from md.c.
- **Audible cuts: the whole of PI-SD at 22.05 kHz** (the user chose it, 2026-11-20; MD_PI_HALF in
  PI_STRUCT_DEFINES, only under MD_AP_STRUCT, so the exact build is still mdkit's word for word).
  The audible cuts one at a time (half-rate rattle, noise and excitation filters, a mode out, a
  linear resampler) summed to about 1.3 of the 3 points PI-SD needed. native/pi_sd_half_block.c,
  pi_sd_half_delays.c and pi_sd_half_modes.c take 16 samples a block: the excitation's phase steps
  two samples; one-poles 2 g - g^2; the feedback's pole 0.3 squared (0.91 of the old word); the
  noise eight pairs a block, its leaky sum y += 1.2 n - 0.51 y; the lines as before without the
  words between; the modes' poles squared, c1' = 2 c1 + c1^2 + 2 b, b' = -b^2, the gain 2 g a
  (g a left the body bands 6 dB low); the bus's 16 words make the 32 output words (each sample
  and its mean with the one before, X 0x50 keeping the last). Spectrum (12 scenarios): level 0.80
  dB, bands 10.38 dB, the worst always the 12.8 kHz band (6-10 dB down: the top octave is gone);
  the next worst 6-7 dB. Instructions (vcheck) 13,813 -> 9,322; cache model 39,110 -> 34,235
  cycles, about 17 points (16 if the unit stays 1.2 under the fit as before). A/B rendered.
  **Bank 0.40** (`out/md-st-0.40-core-2.2.0/`, the same 2.2.0 SYX): 0.38 with this PI-SD.
  The unit: the user's project (PI-SD, 2 SWARM, CY ALLOY, ONE SHOT, SY TOY, SY CHORD, SY TONE)
  95 %, no break-up; it sounds right to the user.
- **PI-RS at 22.05 kHz too** (native/pi_rs_half_block.c; its drum_delays and drum_modes are
  pi_sd_half_*'s): the feedback's leak 0.1 (a pole of 0.9) squared, 0.19 (0x1851ec), kept an
  md_acc; its lines from two taps (md_pd_q without MD_PD_WHOLE). The noise's leaky sum now takes
  1.7 n (its DC gain kept): with 1.2 PI-RS read 1.50 dB of level, now 1.31 (bands 6.11 dB), PI-SD
  0.61 (bands 10.19). Instructions (vcheck) PI-RS 14,715 -> 9,858; cache model 39,747 -> 33,981
  cycles, about 17 points. **Bank 0.41** (`out/md-st-0.41-core-2.2.0/`): PI-SD and PI-RS at half
  rate; A/B of both rendered again.
- **PI-RC, CC and HH at 22.05 kHz** (the user chose it knowing it might not reach 17; native/
  pi_metal_half.c and pi_<voice>_half_block.c in place of block, MD_PI_HALF): noise half the pairs,
  the window's cursor stride doubled, the excitation's decay squared and step doubled, the
  amplitude's attack step doubled and decay squared, the coupling in two passes of eight with its
  strides halved and its one-pole 2 g - g^2, the output's pulse halved, its 16 words made 32.
  The three filters take their rows from audio/native/pi_metal_half_rows.h
  (tools/pi_metal_half_rows.py, from md-tables.c's md_pi_cc_curve_a/b): poles and zeros squared,
  those above 11 kHz put at 11 kHz, the gain matching |H|'s power under 11 kHz. Converting at run
  time first (the modes' 2 g a rule) put the level 12 dB high: these filters are broad and many of
  their poles lie above 11 kHz (a < 0), which squaring folds down. Now: level 3.3-4.7 dB, bands
  17.9-22.0 dB, both worst in scenario 8; elsewhere the 10 kHz band (the cymbals' strongest) is 4-17
  dB down and the mid bands 3-10 dB up. Instructions (vcheck) RC 16,467 -> 9,807; cache model
  RC 39,814 -> 32,746, CC 33,639, HH 33,755 cycles: about 18 points. The spectrum test fails:
  only the user's ear (A/B rendered) decides whether to keep it.
  The user, 2026-11-20: the A/B sounds "increíblemente fiel" (checked: the second half of each
  setting is the half-rate voice, 5-11 dB less above 9 kHz and, with every knob at 127, 5-6 dB
  quieter overall). Kept, with its own spectrum limits (md_port SPECTRUM_LIMITS, just over the 60
  scenarios' readings: RC 4.83 / 18.12, CC 4.68 / 20.70, HH 3.29 / 25.76 dB). **Bank 0.42**
  (`out/md-st-0.42-core-2.2.0/`, the same 2.2.0 SYX): PI-SD, RS, RC, CC and HH at half rate;
  AUDIO 836,080 bytes; parser, smz-boot and trial-boot PASS.
  The unit on 0.42: the user's project (2 SWARM, CY ALLOY, ONE SHOT, SY TOY, SY CHORD, SY TONE)
  with PI-RC 95 %, with PI-RS 95 %, neither breaking up: every PI voice fits there now.
- Every PI voice fits beside seven stock machines now. Not done: the level of the metal at
  high knob settings (5-6 dB low). PI-SD's cost is
  spread (no line over 570 a block; its modes already take shifted operands): nothing as large
  left there without touching the sound. Cache locking on AUDIO: not now (the user, 2026-11-20);
  the cold-cache model gave about a third fewer cycles with half of each cache held by the
  voice, without what the stock engine would lose.
- **The 1.14 % a 1,000 instructions overstates the PI voices now.** It was measured on
  PI-SD at 32,547, whose code did not fit the 8 KB I-cache. The cache model (trace.py +
  cache.py, cold) now gives PI-SD 17,503 instructions, code 6.4 KB and data 5.2 KB a
  block, 390 I + 313 D misses; PI-ML 13,051, 4.5 KB + 3.7 KB, 270 + 222; TRX-BD
  (4.6 % measured) 7,116, 3.2 KB + 3.8 KB, 187 + 218. At cycles ~ 0.85 instr + 40
  misses PI-SD is ~43,000 against TRX-BD's ~22,000. Only the unit's CPU reading of the
  current voices says how far each one is from 15 %.
- Cache model now (cold; instructions, code + data a block, I + D misses; cycles at 0.85
  instr + 40 misses; % scaled on TRX-BD's 4.6 %): PI-BD 13,952, 5.0 + 4.5 KB, 288 + 278,
  34,500, ~7 %; PI-SD 17,503, 6.4 + 4.9 KB, 387 + 302, 42,400, ~9 %; PI-MT 17,311, 5.5 +
  4.8 KB, 330 + 288, 39,400, ~8 %; PI-ML 13,051, 4.4 + 3.5 KB, 269 + 209, 30,200, ~6 %;
  PI-RS 20,005, 6.5 + 4.7 KB, 392 + 290, 44,300, ~9 %; PI-RC 32,701, 9.4 + 4.2 KB (code
  over the I-cache), 567 + 250, 60,500, ~12.5 %. The same scaling gives the old PI-SD
  far under its measured 37 %, so the model misses something PI does (EFM's 1.7 cycles an
  instruction is unexplained too): the truth is between the two estimates.
- Left: the unit's reading decides what else is needed. Candidates: PI-SD/RS's delay
  lanes and final mix (drum_delays, ~7,000), the metal delay loops (~5,900), structural
  forms that remove whole stages.

## Next: optimize the new voices (exact)

User's order (2026-11-16): 1. the resampler and glue (every track pays them), 2. TRX-CY,
3. TRX-RS and TRX-CL natively, 4. the remaining float clocks; then the missing engines
(PI family, EFM-BD).

Do what was done for TRX-BD, EFM-SD and PI-SD (README "How the voices are built",
items 4, 5 and 7), voice by voice, most expensive and most useful first:
TRX-CY, TRX-MA, TRX-CP, TRX-CB/CH/OH (CH and OH share code), TRX-SD, EFM-CY, then
EFM-RS/CB/HH, TRX-RS/B2/CL.

For each voice:

1. Profile it: `out/md-st-tools/prof_cf.py` (per function and per source line, in
   analog-emu) and `trace.py` + `cache.py` + `codeby2.py` (code and data touched per
   block, misses with an 8 KB 4-way cache, by source line). Note the numbers here.
2. Write the hot functions natively in `audio/native/<voice>*.c` and list them in
   `NATIVE` (`tools/md_port.py`); each file says why it computes the same words. The
   patterns that paid off: fixed indices for DSP address registers; one product of
   g and (a - b) for "mac a,g / mac -b,g"; several MACs into one accumulation
   (`md_acc_mac3`, `md_acc_word2`, `md_mid_mpy2_first`); a 48-bit phase as its two
   24-bit halves; local arrays for a bus; signed locals.
3. The control clock in integers where mdkit's floats only hold 1/128ths (TRX-BD's
   and PI-SD's `*_clock.c`, `pi_clock.c` with `MD_CLOCK_PART`); a control at its
   target not computed again; coefficients rebuilt only when a control changed
   (EFM-SD). The new TRX voices still call float32 (`__mulsf3`) from their render.
4. Keep the code footprint small (hot code per block well under the 8 KB I-cache).
5. `python mods/md-st/tools/md_port.py`, then `python tests/test_md_st.py` with
   `MD_CFBENCH` set (every step must pass, step 7 included), then `bench_all.py`
   for the new instruction counts; update README's Cost table and this file.
6. Build a bank (`docs/SYNTAKT-BUILD.md`, "SMZ4 bank", with `mods/md-st` after
   `mods/fm-tone-st`), check it with the core's `smz-parser`, and ask the user to
   measure CPU A. One version step per delivery (md-st is 0.1 until it reaches main).

## Later

- The PI voices after PI-SD (BD, CC, HH, MA, ML, MT, RC, RS): share their big tables
  first (PI-SD and PI-RS hold the same word5/word7 set four times, 40 KB each; PI-MA a
  64 KB gain table; PI-CC/HH/RC a sine of period 32,767). Each costs CPU like PI-SD.
- TYPE's order is fixed at the first release (now the recipe's order, above).
- The web catalog. (The Monomachine machine is out of scope here: the user, 2026-11-16.)

## Tools (`out/md-st-tools/`, not committed)

- `cftrace/` (Rust, depends on analog-emu as a path crate): runs functions like
  analog-emu's `cfbench` and writes every instruction and memory access of the
  steps marked `t`. `trace.py WORK TYPE` builds one track as the bank does and
  traces 16 blocks; `cache.py WORK` simulates the caches; `mix.py WORK` the
  instruction mix; `codeby2.py WORK` code touched per source line (build with
  `XFLAGS=-g`).
- `prof_cf.py WORK TYPE` (with `cfprof-main.rs`, the same crate without the
  trace): instructions per function and per source line.
- `bench_all.py WORK`: instructions per block of every TYPE.
- `domains.py VOICE...`: the arguments each double-precision function of an mdkit
  voice receives (host, scenarios + sweeps) - how the table sizes were found;
  `dfuncs.py`, `survey.py`: which functions still need doubles.
- The scripts hold the paths of this machine; `LOCAL.md` there lists them.

### Instance limits and the shared picker (2026-11-20)

- The user: limits on the MD tracks only; the voices that do not fit shown disabled in the
  subtype list, not selectable. One list only: double-tap SYN, the machine list, YES on MD
  MACHINEDRUM, the list of voices, YES loads (a track with a voice or without), NO back to the
  machine list. The page's numbered list ("01 TRX-BD") and TYPE's "none chosen" are gone.
- feat/md-machine: the MD work committed (4975b42) and origin/feat/digimono-st merged (ade9bcf):
  core 2.3.2, where TYPE is a configuration control (ST_MACHINE_CONFIG_PARAMETER, host API 17:
  not modulated, not a destination, kept by FUNC+NO's SYN reload, AUDIO given the Sound's
  mirror), and <syntakt/subtype-picker.h>, the MD prototype's selector made shared (SY MnM uses
  it). The ColdFire decoder took that branch's EXTB fix; both branches' tests of it stay.
- The picker takes two optional parts (docs/SUBTYPE-PICKER.md): STP_ENABLED(track, index), a
  subtype that cannot be chosen there drawn dimmed (every other column), passed by the cursor,
  refused by YES, the list opening on the nearest that can; STP_VIEW_ENCODER, a view ENC
  subscription of the machine's own (MD's spare knobs). SY MnM defines neither: no change there.
- main.c on the picker: TYPE 0..29 as configuration, the first voice by default; labels, spare
  knobs and the name box as before.
- The budget (5.7, the user's choice): a voice's weight is its cost over an average stock track
  of a mixed project, 5.7 points; the MD tracks' weights stay within 12.3 (95 % - 37 % - 8 x 5.7).
  Costs from the cache model of every voice as built (bank 0.42) fitted to the unit's readings,
  the metal with the 1.7 the unit read over the fit (tools/md_port.py VOICE_POINTS, generated into
  md-voices.h as md_voice_weight and MD_BUDGET). A weight is capped at the budget: PI-CC and HH
  (18.5 in the model, not read on the unit) fit alone as PI-RC does. MAIN reads each digital
  track's machine (0x80002f50 + t) and TYPE (the Sound mirror, slot 0x36), as the core's sender.
  Alone: TRX-XT, XC, PI-MA 8; EFM-BD 4; TRX-BD, EFM-XT, CP, HH 3; most TRX and EFM 2; TRX-SD, CP,
  CY, MA, EFM-CY and every PI voice 1.
- AUDIO (md.c): a voice a project brings past the budget stays silent and unrendered until it
  fits; the track first in the block's order keeps its voice. A TYPE past the last voice (an
  older Sound's "none") is the first voice.
- Tests: tests/md_st_picker_check.c rewritten on the picker (YES/NO/encoders, a voice loaded or
  not, dimmed and refused voices, spare knobs, labels, format), tests/md_st_glue_check.c's
  budget and old-none cases; tests/st_bank_modules.py accepts MD (type 70 after FM, its
  md_picker_module "MD TYPE SELECTOR" with six subscriptions and main.c pinned).
- A note plays its knobs whole (the unit, 2026-11-20: a Sound lock from TRX-BD to TRX-CP played
  its first note with other settings, the second right; a p-locked PTCH slid). mdkit's control
  clock moves raw a quarter of the way to the knob every 12 blocks, and a trigger publishes raw:
  a note right after a p-lock, or after a voice is loaded (its defaults), plays the glide.
  md.c's md_snap, at a note, sets the voice's knobs in the clock: target the knob, raw, pending
  and current the same (integers: 128 times it; EFM's floats: the float, MD_FLOAT_CLOCKS from
  md_port's FLOAT_CLOCK), pending published at the next block so the voice rebuilds its
  coefficients. A knob turned between notes still glides. tests/md_st_glue_check.c's
  reference voice starts its clock at the knobs; its case 7 (a p-lock, a turn, a Sound lock to
  TRX-CP, EFM-SD) fails without md_snap.
