# MD MACHINEDRUM for Syntakt (prototype)

Machine type **70**, **MD MACHINEDRUM** (short name **MDRM**), category 10,
rank 15. One machine holds Machinedrum voices, so the Machinedrum costs one
machine type, not one per voice: selecting the machine shows the list of voices,
and the one chosen stays on the track (below, "Pages").

This prototype has 30 voices: every voice mdkit has (13 TRX, 8 EFM, 9 PI). Built
with `MD_ST_EXACT`, all but EFM-BD are mdkit's output word for word; EFM-BD is a
fixed-point port of a model (below, "EFM-BD"). As the bank builds them, most voices
take a few cheaper products that are not (below, "Approximate forms"): their error
against mdkit stays under -85 dBFS.

| TYPE | voice | knobs |
|---|---|---|
| 0 | TRX-BD | PTCH DEC RAMP RDEC STRT NOIS HARM CLIP |
| 1 | TRX-SD | PTCH DEC BUMP BENV SNAP TONE TUNE CLIP |
| 2 | TRX-XT | PTCH DEC RAMP RDEC DAMP DIST DTYP |
| 3 | TRX-CP | CLPY TONE HARD RICH RATE ROOM RSIZ RTUN |
| 4 | TRX-RS | PTCH DEC DIST |
| 5 | TRX-CB | PTCH DEC ENH DAMP TONE BUMP |
| 6 | TRX-CH | GAP DEC HPF LPF MTAL |
| 7 | TRX-OH | GAP DEC HPF LPF MTAL |
| 8 | TRX-CY | RICH DEC TOP TTUN SIZE PEAK |
| 9 | TRX-MA | ATT SUS REV DAMP RATL RTYP TONE HARD |
| 10 | TRX-CL | PTCH DEC DUAL ENH TUNE CLIC |
| 11 | TRX-XC | PTCH DEC RAMP RDEC DAMP DIST DTYP |
| 12 | TRX-B2 | PTCH DEC RAMP HOLD TICK NOIS DIRT DIST |
| 13 | EFM-BD | PTCH DEC RAMP RDEC MOD MFRQ MDEC MFB |
| 14 | EFM-SD | PTCH DEC NOIS NDEC MOD MFRQ MDEC HPF |
| 15 | EFM-XT | PTCH DEC RAMP RDEC MOD MFRQ MDEC CLIC |
| 16 | EFM-CP | PTCH DEC CLPS CDEC MOD MFRQ MDEC HPF |
| 17 | EFM-RS | PTCH DEC MOD HPF SNAR SPTC SDEC SMOD |
| 18 | EFM-CB | PTCH DEC SNAP FB MOD MFRQ MDEC |
| 19 | EFM-HH | PTCH DEC TREM TFRQ MOD MFRQ MDEC FB |
| 20 | EFM-CY | PTCH DEC FB HPF MOD MFRQ MDEC |
| 21 | PI-BD | PTCH DEC HARD HAMR TENS DAMP |
| 22 | PI-SD | PTCH DEC HARD RING TENS RVOL RDEC |
| 23 | PI-MT | PTCH DEC HARD HAMR TUNE DAMP SIZE POS |
| 24 | PI-ML | PTCH DEC HARD TENS |
| 25 | PI-MA | GRNS DEC GLEN SIZE HARD |
| 26 | PI-RS | PTCH DEC HARD RING RVOL RDEC |
| 27 | PI-RC | PTCH DEC HARD RING AG AU BR GRAB |
| 28 | PI-CC | PTCH DEC HARD RING AG AU BR GRAB |
| 29 | PI-HH | PTCH DEC CLSN RING AG AU BR CLOS |

The labels are mdkit's. The user has measured the TRX and EFM voices on a unit (banks
0.6 to 0.13); the PI voices after PI-SD and EFM-BD have not run there yet.

## Credits

The voices are **[mdkit](https://github.com/soejrd/mdkit)** by Soejrd: the
Machinedrum's TRX, EFM and PI machines in standalone C99, dedicated to the
public domain under **CC0 1.0**. `mdkit/` is mdkit's `src/` and `include/` at
commit `96157fe` (2026-10-22), unchanged, with its `LICENSE`, `NOTICE` and
README (`mdkit/UPSTREAM-README.md`). Everything Synfact adds to them is
generated from that copy and marked "Synfact" where it changes a line.

mdkit and this machine are not made, affiliated with or endorsed by Elektron.
*Machinedrum* is Elektron's name, used only to say what the voices model.
mdkit's sources carry words that come from the Machinedrum (the knobs'
control-word tables and some of its ROM curves); they are here as mdkit
publishes them.

## Pages

- **The voice list.** In the machine selector (a SYN double-tap), YES on MD
  MACHINEDRUM shows the list of the 30 voices ("CHOOSE A VOICE") over the selector,
  on the voice the track has: any encoder or UP/DOWN moves the cursor (FUNC+UP/DOWN:
  a page), **YES** loads MD MACHINEDRUM with that voice, **NO** goes back to the
  machine selector (another NO closes it, as always). A Sound with MD and no voice
  shows the same list in place of the knobs (YES loads); such a track plays nothing.
- **The voice's name.** Arriving at an MD track's SYN page (from another page,
  another track or the machine selector), or loading a voice, shows the voice's
  name in a box in the middle of the screen for about a second (30 of the core's 30 Hz TICKs,
  counted by the module; letters of md-st's own, two pixels a dot).
- **SYN 1**: the voice's eight knobs, 0 to 127, under the voice's own names.
  The labels follow the voice of the Sound on screen (`main.c`: the core reads
  a control's label through its pointer each time it draws it, and `idle`
  refills the buffers). A voice has 3 to 8 knobs (the table above); the places
  past its last one are empty (the machine's draw paints them over) and their
  encoders are taken, so no value or popup changes: md.c sends a voice its own
  controls only (`tests/md_st_spare_knobs.c` checks it).

The voice (TYPE in the table above) is a ninth control on no page, so a Sound
keeps it, and it cannot be locked per step from the panel. Its default is "none
chosen" (TYPE 30): the core writes a machine's defaults when another machine is
replaced by it, never when a Sound or a project loads, so saved Sounds keep their
voice. The list over the selector never reopens the selector: YES on MD there is taken
by the module (`md_picker_module`, the global `ST_EV_KEY`/`ST_EV_ENC` before the native,
whatever view takes the keys) while a track's SYN model shows the selector (predicate
`400c3340`) and that view's machine is MD (the selector's preview selects the highlighted
machine); the list is drawn by the module's global `ST_EV_DRAW` over the whole frame (the
selector's machine list is a view drawn after the SYN view; `ST_EV_DRAW|ST_EV_VIEW` only
remembers each track's SYN view). YES
in the list goes on to the native, which confirms MD MACHINEDRUM (with its defaults if
the track had another machine), and the track's next MD page sets the voice by an
explicit edit of the hidden control (native Sound/undo path). NO only takes the list
away. The list on the page follows the sample machines' browser: the machine's draw,
the view's ENC and pre-native KEY. YES with a trig held, or while recording, does
nothing (the edit would be a lock). A Sound's first knob values are the first voice's
defaults.

## How it sounds on the Syntakt (`audio/md.c`)

- **The voice is taken at a note.** A track changes voice when it is triggered.
  With no voice chosen it rests and its notes play nothing. A knob reaches the
  voice through mdkit's own control clock, which applies it at the voice's next
  block.
- **44.1 kHz.** Each voice runs at the Machinedrum's rate, block by block, and
  the track reads it at 147/160 of a voice sample per output, through a 4-point
  Hermite interpolator (Q14 weights, `audio/md-weights.h`). The voice itself is
  unchanged.
- **Always rendered.** A track with a voice chosen renders it on every block from
  the moment it loads, sounding or not, as the stock machines do, so loading a voice
  shows what it costs (`MD_REST 0` in `audio/md.c`, for the measurements). With
  `MD_REST 1`, a voice whose output stays within 2^-21 of zero for 32 of its blocks
  (23 ms) is not rendered until its next note; most PI voices never get that quiet
  after a hit (their tails stay a few units from zero, mdkit's as well).
- **The track chain.** As the sample machines do, the stock TwinShot update sets
  up the track's gain and AMP, and the four stock calls of the TwinShot render
  take the machine's PCM through the track (`mods/samplers-st/audio/sampler.c`).
  The Syntakt's filters, drive and AMP follow as for any machine.
- The voices are mono, at full scale (24-bit words to Q31): their level beside
  the stock machines has not been heard.

## How the voices are built for the Syntakt

The AUDIO CPU is an MCF54415: EMAC, no FPU, no 32x32->64 multiply, no libgcc in
the bank. mdkit models the Machinedrum's DSP56303 in integers (24-bit words, a
56-bit accumulator in a `uint64_t`), with a few functions in double precision.
`tools/md_port.py` writes `audio/voices/` from `mdkit/`, per voice:

1. **The accumulator helpers** (`mid`, `add`, `mpy`, `acc_to_word`...) call
   `audio/voices/md-acc.h`, which keeps the same 56-bit values as a signed
   32-bit high word (bits 24..55) and a 24-bit low word: A1 is a mask and a
   56-bit wrap is a 32-bit one.
2. **The double-precision functions** the recipe names (the sines, the fitted
   curves, the TRX model memory, PI's ROM) read tables: those functions evaluated
   on the host by the voices' own code (`tools/md-tables.c`), identical tables kept
   once (`audio/md-tables.c`). A function that depends on its argument only through
   a key (TRX-CP's room decay, `raw >> 8`) is read at the key, and the host proves
   f(x) = table[key(x)] for every x it can take; a computation written inline (TRX-CL's
   decay, EFM-XT's pitch and decay tables, EFM-HH's and EFM-RS's two pitch
   polynomials) is read at its statement. The TRX voices share one model memory (its
   256 offset words and 8,192 cos/exp words are the same in all of them). A sine is
   kept as a quarter wave and one range of exceptions, and the voices' three sines
   differ only by rounding, so one quarter is kept and the other two are it less the
   bits of a 1 KB bitmap. 119 KB of tables in all.
3. **The render** writes the voice's words as `int32_t`; the float one stays for
   the host.
4. **Native blocks** (`NATIVE` in `md_port.py`, `audio/native/`): the hot
   functions of a voice rewritten in plain integers, each file saying why it is
   the same: the DSP's address registers become fixed indices, "mac a,g then sub
   mpy b,g" one product of g and a - b, three MACs into one state one
   accumulation, a 48-bit phase its two 24-bit halves, a bus the modes add into
   a local array. TRX-BD's block and control clock, EFM-SD's render and clock
   (a control at its target is not computed again, and the coefficients are
   rebuilt only when a control changes), PI-SD's block, modes, delays, modulo
   addressing and control clock; the renders or cores of EFM-XT, CP, RS, CB and HH
   in EFM-SD's scheme, TRX-SD's tone, TRX-CL's scan, the PI voices' modal excitation
   (a ROM read between two words at a 48-bit phase), TRX-SD's and TRX-MA's blocks
   (each loop a word plus products: one read of the EMAC and a limit). TRX-CY, CH and
   OH's metal source goes by runs in which no square changes sign, its six limited adds
   composed into one (limit(limit(x + C) + c) is one limit of x + C + c). The TRX
   metal and CP filters keep in registers, signed, what mdkit's loops read back of
   their own writes (the input window, the last outputs, the history pair). In the PI
   voices `MAC_DIFF` writes "mac a,k then sub mpy p,k" as one product of a - p.
5. **The EMAC** (`audio/voices/md-native.h`). In fractional mode (MACSR 0x20,
   truncating, not saturating), with the words shifted left by 8 and 6, a MAC
   adds floor(x*y / 2^9) and a read gives the product's high bits, floor(x*y /
   2^17); a 32-bit `muls.l` gives its low 32 bits, and the two together are the
   whole product, exactly (the read is short of the true value by less than
   2^17 + 2^9 a product, which the low bits make up). So "multiply and keep the
   high word" is mac, movclr and a shift; a full 48-bit product, a rounded one
   (mpyr) and a sum of two or three products are exact too, and no read can
   overflow. `md_emac_begin`/`md_emac_end` (`md.c`) set the mode and give the
   stock code back MACSR, ACC0 and ACCext01 as they were.
6. **The self-test** (`md.c`). The first update compares every EMAC primitive with
   its C form on fixed and pseudo-random words. If any differs the machine stays
   silent: a unit whose EMAC is not what the emulator models would play wrong
   words. (Up to bank 0.7 a copy of the voices built without the EMAC ran instead;
   it cost as much AUDIO memory as the voices themselves and was dropped once a
   unit had passed: a bank that forced the EMAC without the test measured the same
   CPU as one that tested it.)
7. **The control clock** of TRX and PI is in integers (their floats only hold
   1/128ths); EFM's stays in float32, a few operations every 12 blocks, through
   `audio/md-runtime.c` (IEEE binary32 and the 64-bit multiply by their libgcc
   names).

### EFM-BD

mdkit's EFM-BD is not the Machinedrum's DSP code but a model of it in double precision
(two sine operators, three exponential envelopes, phases in doubles), so it cannot be
generated as the others are. `audio/native/efm_bd.c` is a port of that model written by
hand (`HAND` in `tools/md_port.py`): mdkit's control clock in float32, its own 2^x
polynomial, the envelopes in Q31 (a base times its decay's power, one rounding a block),
the decays as 1 - decay in Q37, phases in Q15.17, the sine from TRX-BD's table.

It is **not word for word**, and cannot be: its FM feedback (MFB) has a loop gain past 1
from MFB about 21, so any difference from mdkit's doubles grows sample by sample there.
`tests/md_st_voice_model.c` checks instead, over 60 random scenarios each: the three
envelopes within 1e-5 of mdkit's (relative, or 1e-6 absolute below 0.1; measured 1.1e-6);
and without feedback the first 2048 samples of a note at 90 dB signal to error or better
(measured 95 dB) and each operator's phase within 1e-5 of mdkit's over a second (measured
8e-7, about 0.001 cent).

### Approximate forms

To fit more tracks, a voice may take forms that drop the last bits of a few
computations (`MD_ST_APPROX` in `audio/voices/md-native.h`; `APPROX` in
`tools/md_port.py` says which voice takes which; `-DMD_ST_EXACT` turns them off):

- `mid`: a rounded product, or the word of a sum of two, from the EMAC's read alone,
  without the 32-bit products that make it exact (short by a unit at most).
- `floor`: md-floor.h's words of a filter likewise.
- `sine`: the sine quarter without its fix word and its minus bit (1 LSB).
- `acc` (`MD_AP_ACC`, `md-acc.h`): a product into an accumulator from the read alone:
  A0 keeps the read's 6 bits below the word (2^-6 of an LSB) plus half a unit, so that
  the error has no mean. Where an accumulator starts a block from a word and only its
  word is read (the PI voices' modes and delay lines, `pi_modal_filter.c`, `pi_modes.c`,
  `pi_delays.c`), it is one 32-bit word, floor(acc / 2^18), stepped by the read and by a
  unit every other sample.
- `fm` (`MD_AP_FM`, EFM-SD, XT, RS, CB and HH): the FM operators' 48-bit phases as one
  32-bit word, the top bits of the 39 the sine reads (`native/efm_fm.h`): the phase
  plus its modulation is one add, the sine's index a shift; a step floor(x y / 2^k)
  comes from the EMAC's read alone; the rounded MAC is the read's; the sines are their
  quarter as it is (as `sine`). Each phase is off by under 2^-31 of a turn a sample.
- `struct` (`MD_AP_STRUCT`, every PI voice but PI-MA; `PI_STRUCT` in `md_port.py`): the
  PI voices' structural forms. Each leaves out the modes that weigh least on its sound
  (`MD_PI_SKIP_MODES`: PI-ML modes 2 and 4; PI-SD and RS modes 4, 6 and 7; PI-MT 2 and 4
  to 7; PI-BD 2 to 7). PI-SD, RS, MT and BD run their three delay lines at half rate
  (`pi_delays.c`, `pi_mt_block.c`, `pi_bd_block.c`); PI-SD and RS limit their bus of modes
  once, after the last mode, and keep the noise filter in one word (`pi_modes.c`); PI-BD's excitation,
  coefficients, modes and hammer filter take `acc` and `mid`; PI-RC, CC and HH's metal
  filter takes its products from the read, its accumulator in one word, and the four
  products before each stored word as one read (`pi_metal.c`); the metal's EMAC operands are
  shifted once, with the words' sign extension in the shift, and its delay loops test the
  limits once a sample and use straight pointers (the same words).
- `MD_PI_HALF` (with `struct`; PI-SD, RS, RC, CC and HH, `PI_STRUCT_DEFINES`): the whole voice
  at 22.05 kHz, 16 samples a block, its 16 words made the block's 32 (each sample and its mean
  with the one before). Each stage takes its two-step form: one-poles 2 g - g^2, the modes' and
  the feedbacks' poles squared, the excitation's phase and window stepping two samples, the
  delays halved, decays squared and steps doubled (`pi_sd_half_*.c`, `pi_rs_half_block.c`,
  `pi_metal_half.c`). The metal's three filters take their rows from
  `native/pi_metal_half_rows.h` (`tools/pi_metal_half_rows.py`: poles and zeros squared, those
  above 11 kHz put at 11 kHz, the power under 11 kHz kept). What is above 11 kHz is gone: PI-SD
  and RS pass the spectrum test; PI-RC, CC and HH do not (their 10 kHz band is 4-17 dB down) and
  were approved by ear, with their own limits (`SPECTRUM_LIMITS`).

Dropping A0 altogether was tried first and broke the filters and phases of most voices
(a floor at every product is a bias the recursions add up). Under `acc` the products that
stay exact take the `_x` forms: a voice's coefficients, made once a block (`ACC_EXACT` in
`md_port.py`: the modes' frequencies and dampings took the approximation at -60 to -90
dBFS), an address made from A0 (PI-RC, CC, HH's `metal_activate`), PI-RC, CC and HH's
fourth-order metal filter (-27 dBFS with it), EFM-SD's phase step, the PI noise filter
(its level feeds a threshold) and the TRX filters' coefficients. They were found product
by product, each approximated alone against mdkit.

Each voice was measured against mdkit over 60 scenarios (`tests/md_st_voice_ser.c`, the
error's RMS level): the forms are kept where the error stays under -85 dBFS, which is why
EFM takes no sine change outside `fm` (its FM feedback amplifies one LSB), EFM-CB and CY no `mid`, and
TRX-CP's `cp_filter6` keeps the exact floor (`MD_CP6_EXACT`; -35 dBFS with it), and PI-BD
takes no `acc` in its modes or its hammer filter (`MD_MODAL_EXACT`: its long low modes
amplify the read's error to -72 dBFS). Voice errors as built: -89 dBFS (TRX-MA) to below
-150 dBFS.

`fm` and `struct` are the exceptions: `fm` was taken so that one instance of each of
these voices fits next to seven SY SWARM (the heaviest stock machine), `struct` so that
one instance of each PI voice fits in 15 % of AUDIO CPU. A left-out mode changes the
waveform outright; with `struct` PI-ML is within 0.78 dB of level and 5.0 dB in every
band, PI-SD 0.29 and 2.3 dB, PI-RS 0.77 and 3.4 dB, PI-MT 0.96 and 3.7 dB, PI-BD 0.03
and 1.2 dB, PI-RC 0.57 and 4.6 dB, PI-CC 0.40 and 2.3 dB, PI-HH unchanged. Under `fm`, with MOD (or feedback) high, an operator's
pitch follows its modulator's words so closely that any change, however small, puts the
waveform elsewhere after a few milliseconds. These voices are compared by what is heard
instead (`tests/md_st_voice_spectrum.c`): each scenario's level within 1.5 dB of mdkit's,
and its third-octave bands (those within 30 dB of the loudest) within 12 dB. As built:
bands within 0.2 dB (EFM-SD, RS), 0.7 (CB), 2.8 (HH), 8.5 (XT, one scenario with MOD and
feedback high); levels within 0.5 dB. A 16-bit half-period sine was tried first: 2 to 5 %
fewer instructions, but 32 KB the AUDIO reserve does not have, and bands up to 10 dB off.

To change a voice or add one: edit the recipe (`VOICES`, `NATIVE` in
`tools/md_port.py`), then

```bash
python mods/md-st/tools/md_port.py           # needs $LLVM_MINGW_BIN (docs/SETUP.md)
python tests/test_md_st.py                   # host steps: $LLVM_MINGW_BIN; 6: $SGDK_BIN + unicorn; 7: also $MD_CFBENCH
```

`tests/test_md_st.py` checks the generated files are current; the runtime against
the host's arithmetic; every `md-acc.h` helper against mdkit's; every voice word
for word against mdkit's own code (60 scenarios, then each knob swept 0 to 127 with
the others at 0, 64 and 127: 8 to 16 million samples a voice), built so that a table
read outside its table fails the test (`MD_ST_DOMAIN_CHECK`); `md.c`
on the host; each voice built without the EMAC in Unicorn; and, with
`MD_CFBENCH` set to analog-emu's `cfbench` (a ColdFire V4e interpreter with the EMAC), each voice and
one track built as the bank builds them, with the EMAC, whose words must hash as
the host's, and the self-test, which must pass. Unicorn is not used for the EMAC:
its integer mode loses the extension bytes and its fractional mode does not
compute what the MCF5441x reference manual says. analog-emu's ColdFire core
models the EMAC on its physical registers, as that manual does.

## Cost

Instructions per Syntakt block (32 frames at 48 kHz) of one sounding track, the
voice with its resampler and glue, knobs `100 40 64 20 90 10 64 30`
(`tests/test_md_st.py`). Instructions, not cycles, and not the unit's CPU: a block
is about 166,000 cycles of the AUDIO CPU, and the stock engine uses 43 % of it in
STOP (docs/MACHINE-REFERENCE.md).

| track | first port | exact, C (Unicorn) | exact, EMAC (analog-emu) |
|---|---|---|---|
| TRX-BD | about 20,000 | 10,949 | 7,768 |
| EFM-SD | 24,858 | 18,675 | 11,497 |
| PI-SD | 123,221 | 71,364 | 32,327 |
| EFM-BD | 19,255 (port) | - | 6,538 |

Every column is mdkit's output word for word, the EMAC track too (its hash is
the host's), except EFM-BD's (a port of a model, above): its first port, at 19,255,
spent a third in float32 rebuilding the coefficients twice a block; they and the decays'
powers are now rebuilt only when the control clock moves. The resampler and the glue are about 1,800 of each; a resting voice
(`MD_REST 1`) costs nothing. An EMAC form of the resampler (four mac.w an output) measured
slower than the C one and was dropped. The hot loops as GCC compiles them are
already close to what this arithmetic needs (PI-SD's modes 47 instructions a
sample for three exact products, EFM-SD's operators 166 for ten, a resonator 25),
so hand-written assembly was estimated at 5 to 15 % more and not written.

**`MD_ST_FAST`** computes TRX-BD's signal products from one 16 x 16 multiply
(each word's top 16 bits) and keeps the products that feed back exact. It is not
mdkit's word for word (worst signal-to-error ratio 78.9 dB over the 60 test
scenarios; the test's floor is 70 dB) and gains nothing once the EMAC is there.
It is off; a test bank turns it on with an `md-fast.h` (`#define MD_ST_FAST 1`)
beside `md-native.h` in a copy of the folder.

## Not done

- **On a unit**: the voices play and their CPU A was read (banks 0.37 to 0.40, below); not
  yet read there: PI-CC and HH at half rate (bank 0.42; PI-RC, which they share their code with, was).
- **The PI voices' cost** (CPU A over all tracks TwinShot, 37 %, one track MD; bank 0.38 read on
  the unit, the rest from the cache model fitted to those readings): TRX-XT 7 points, PI-MA 7,
  TRX-BD and EFM-CP 10, PI-ML ~14, PI-BD ~15, PI-MT ~17, and at half rate (bank 0.42) PI-SD and
  RS ~17, PI-RC, CC and HH ~18 (22 at full rate). A project of seven different stock machines
  takes about 40 points (77-78 %), leaving about 17-18 at 95 %: PI-SD, RS and RC fit there (95 %, no
  break-up). The meter is a mean over a second, not the dearest block. PI-RC, CC and HH keep the
  1,024 words of DSP delay memory they use, not the 65,536 mdkit gives them (WORK.md).
  The 30 voices fill the AUDIO reserve to 3.7 KB (bank 0.17).
- **EFM-BD on a unit**: not heard yet. GND-SN, GND-NS, GND-IM and TRX-S2 are not in mdkit.
- **TYPE's order** is fixed at the first release (a saved Sound keeps its number);
  until then it is the recipe's order.
- **Native blocks for the voices that come after these three**; PI-SD stays the
  heaviest by far (eight resonant modes and three delay lines a sample).
