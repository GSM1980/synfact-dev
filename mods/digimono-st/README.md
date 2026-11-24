# SY MnM 0.8.2

## The one-track rule follows the tracks (0.8.2)

The list strikes the heavy TYPEs through while another track's SY MnM plays
one. It read each track's machine and TYPE from the copies AUDIO was last
sent (`80002f50`, `800021cc`), which the firmware writes only when a track
plays: after a pattern change, or with a track cleared or given another
machine, the list was wrong until every digital track had played. It now reads
the playing Kit (`<syntakt/kit.h>`), as MD KIT does, so it is right at once.

## Subtype list during a held trig or live REC (0.8.1)

The subtype list's confirmation and its deferred TYPE edit refuse while a
trig is held or live REC runs. Their getters were swapped
(`<syntakt/subtype-picker.h>`): a held trig still refused, by luck, but live
REC never did. They are now `400c3582` (trigs held within the track length)
and `400c3062` (live REC); see the SP machines 1.6.1 notes and
`tests/test_st_sampler_natives_emu.py`, which pins them.

One digital Syntakt machine (type 71), ported from gdeo607's
[Digi Mono](https://github.com/gdeo607/digi1_mods/tree/32fd22a1fd66fa5833b808b1c1c049a0deb82d88/mods/digimono).
ENS and SID-6581 come from Inigo Hernandez's
[MONO2K](https://github.com/InigoHernandez/mono2k/tree/95d5763495d961f3ed225f5e4201328934ca4247).
FM+STAT, FM+PAR and FM+DYN, and the DigiPRO machines DPRO WAVE, DPRO DDRW and
DPRO DENS, use the reverse-engineered algorithms from
[De-Pattern](https://github.com/De-Pattern/dn2mk2-firmware/tree/a0249fc55fd188242c92f377b4cbb2748b851a35/src/dnfw/mnm).

Version 0.8.0 adds the DigiPRO family as TYPE 12, 13 and 14 (all earlier IDs
and saved Sounds unchanged). **Building it needs your Monomachine OS 1.32B:**
the DigiPRO tables are Elektron's data, made by
`python mods/digimono-st/dpro/import_tables.py "$MONOMACHINE_OS"` into a
git-ignored header, never committed. DPRO BBOX is not included: De-Pattern has
no port of it. DDRW's TIME and BR follow De-Pattern's host mapping. In this
release DENS has no chorus of its own: its CHRL/CHRW are hidden, and the bank's
Chorus FX serves it (a stereo chorus in the machine cost the shared stereo
service's second pass). ENS, FM+STAT, FM+PAR, FM+DYN and the three DigiPRO
TYPEs, the heaviest, play on one track at a time between them: while one track
plays any of them, a second is silent and the list strikes them through. The
web catalog leaves 0.8.0 out until it reads the Monomachine OS as a donor. See [dpro/README.md](dpro/README.md).

FM+ is ported to fixed point with original FM tables. Version 0.7.0 ships a balanced
hybrid backend: self-FM stays at 2x; ramped operators and carrier run at 1x.
This is a reconstruction, not
a claim of identical whole-instrument output. MACRO is excluded.

Version 0.6.0 replaces the three FM+ engines in place (TYPE 9/10/11). Existing
Sounds keep their IDs and values but intentionally sound different. FM+ runs
at 44.1 kHz / 16 frames. The retained HQ reference uses 2x operators and its original allpass decimator,
then a 160-phase, 16-tap converter feeds Syntakt at 48 kHz. Oscillator phases
continue on retrigger; the machine's internal envelopes restart. Native Syntakt
AMP/FLTR/FX remain downstream. See [fmplus/README.md](fmplus/README.md).

Version 0.6.1 optimizes the same FM+ arithmetic and memory accesses. A linked
768-block comparison against 0.6.0 retains exact PCM and complete voice state;
observed render maxima fall another 9.3–12.3%. Oversampling, conversion taps,
tables, control timing and truncation points are retained. DYN's standalone
trigger now also saves/sets/restores the EMAC mode explicitly.

Version 0.7.0 trades some high-frequency/strong-modulation fidelity for a
larger CPU reduction. The sensitive feedback operator retains every original
2x sample, including both samples in the carrier's phase integration. Tables,
pitch/ratio laws, 16-frame envelope timing, two filter substeps and the 16-tap
44.1-to-48 kHz converter remain. Ramped operators use the mean input slope;
the carrier is sampled at 1x and no longer uses the original 2x allpass
decimator. This backend is intentionally not bit exact with 0.6.1. The
`DM_FM_HQ` compile-time reference retains that full-quality implementation for
comparison; the previous 0.6.1 bank also remains usable without a core change.

SYN has one page with eight positions: TUNE and seven engine parameters.
Unused positions are empty. TYPE stays in its existing Sound slot but has no
dial. Double-tap SYN, highlight **SY MnM**, then press **YES** to open
**CHOOSE A TYPE**. Encoders or UP/DOWN select a subtype; YES loads the machine
with that subtype, NO returns to the native machine list. FUNC+UP/DOWN jumps
seven rows. The fifteen TYPE values and all existing saved Sounds keep their IDs.
Right of the names the list shows a small drawing of a Monomachine of our own
(no Elektron artwork or logo): `mnm-icon.h`, written by
`python mods/digimono-st/tools/mnm_icon.py` (`--preview out.png` also writes it
eight times larger). It is 52 x 32 at x 72: the DPRO names reach x 68.

Arriving at a Digi Mono SYN page, switching tracks or loading a subtype shows
its name in a bordered box for about one second. Old views left on SYN 2 are
clamped to the single current page. TUNE and the seven values remain shared
across types: the subtype helper changes only TYPE. Init Sound is still SAW.
Replacing a different machine follows the core's ordinary defaults first.
Save/load, locks, undo, modulation and page navigation use the existing core.
Audio still changes generator on the next trigger; the selector never creates
a TYPE lock while a trig is held or live recording is active. TYPE is no longer
available in LFO or Sound Setup destinations. Old routes/locks aimed at TYPE
are ignored by the new core's AUDIO projection; the saved ID and data remain
intact. TUNE and the parameters actually used by each subtype are modulable.

**Requires OS 1.42 and core 2.3.2 for TYPE-preserving SYN reload.**
FUNC+YES randomizes the page as before. FUNC+NO reloads the synthesis
controls while preserving TYPE, including when the saved Sound uses another
subtype. Explicit machine selection and Sound/project loading still load TYPE.
Version 0.4.0 integrates remote main's OS/memory contract and preserves the
0.3.1 AUDIO generators. Rebuild the bank and core together; older 1.41 banks
are not compatible with the current core contract. The stopped-transport
preview service remains at its API 15 ABI offset; destination query 36 (32
before core 2.6.0) needs API 17, so a core without it refuses the destination
module.
The integration baseline is remote main `ce91a3b`. Its OS 1.42 build passes
227 exact stock preimages and 95 common checks; all four Digi Mono AUDIO
objects remain byte-identical to 0.3.1. Physical validation is still separate.

The original core 1.8.1 fix removes the bind-time layout snapshot:
SAW's empty E now becomes PULSE's SUB2, ENS/CHORDS' WAVE or VO's VOIC when TYPE
changes. Unused slots disappear completely, including their dial and edit ID.
Core 1.8.0 leaves SAW's initial seven-position layout fixed even though labels
change. TUNE is always A; the seven engine parameters occupy B..H when used.

Version 0.1.2 also removes unused engine controls from native LFO 1/2/3 and
Sound-modulation destination lists. TUNE and the common targets stay
available. The list reads TYPE from its own Sound model, so changing tracks
without opening SYN first works too. Saved destinations, parameter IDs and
runtime capability flags of the synthesis controls are unchanged. TYPE is the
exception in 0.5: a saved configuration value, never a modulation destination.
The bank's UI-only **MONO TYPE SELECTOR** module combines the destination
provider with five UI subscriptions. It adds no AUDIO oscillator work or new
hook. Its symbol remains `dm_destination_module`, keeping the normal bank's
module count and order. Install core 2.3.1 as well: its generic configuration
marker blocks old TYPE modulation routes without altering other parameters.
The implementation comes from the unfinished Machinedrum worktree; only its
MAIN selection/name-box mechanism is adapted, with exact snapshot hashes in
`picker-provenance.json`. The other agent's worktree is untouched. See
[`SUBTYPE-PICKER.md`](../../docs/SUBTYPE-PICKER.md) for reuse and test scope.

| TYPE | A | B | C | D | E | F | G | H |
|---|---|---|---|---|---|---|---|---|
| SIN | TUNE | | | | | | | |
| NOISE | TUNE | ST | RED | STON | | | | |
| SAW | TUNE | UNIL | UNIW | UNIX | | SUBX | SUB1 | SUB2 |
| PULSE | TUNE | UNIL | UNIW | SUB1 | SUB2 | PW | PWAD | PWRS |
| ENS | TUNE | PCH2 | PCH3 | PCH4 | WAVE | PW | CHRL | CHRW |
| VO | TUNE | VOC1 | VOC2 | V-SW | VOIC | CONS | CLEN | CVOL |
| POLY SIN | TUNE | NOT1 | NOT2 | NOT3 | EDEP | ESPD | | |
| CHORDS | TUNE | CHRD | INV | SPRD | WAVE | PW | CHRL | CHRW |
| SID-6581 | TUNE | PW | PWAD | PWRS | WAVE | MOD | MSRC | MFRQ |
| FM+STAT | TUNE | 1FRQ | 1FIN | 1ENV | 1FB | 2FRQ | 2VOL | TONE |
| FM+PAR | TUNE | 1FRQ | 1ENV | 2FRQ | 2ENV | 3FRQ | 3ENV | TONE |
| FM+DYN | TUNE | 1FRQ | 1FEN | 1VOL | 1VEN | 2FRQ | 2ENV | 2FB |
| DPRO WAVE | TUNE | WAVE | WP | WPM | WPRS | SYNC | SFRQ | |
| DPRO DDRW | TUNE | WAV1 | MIX | WAV2 | TIME | BR1 | WID | BR2 |
| DPRO DENS | TUNE | PCH2 | PCH3 | PCH4 | WAVE | | | |

The three DigiPRO TYPE values are appended (12..14) in 0.8.0. DPRO WAVE's WAVE
shows the wave 0..31, WPRS ON/OFF, SYNC OFF/SFRQ/PRCH; DDRW's WAV1/WAV2 and
DENS's WAVE the Digibank slot 0..63; DENS's PCH2..4 ENS's interval names.

The four SID/FM+ TYPE values are appended (8..11); the original 0..7 identities
and all stored control positions remain stable. Version 0.3 replaces ENS's
generator; the other eleven types retain their PCM output. SID/FM+ use all
eight SYN 1 positions and expose all seven engine controls as LFO destinations.
SID's WAVE/MOD/MSRC/PWRS show choice names; STAT/PAR ratios show their actual
24-step ratios. Other new engine values display 0..127.

Controls use 0..127; TUNE is signed, centered at 64 (0 semitones), and retains
fractional tuning. ENS PCH2/3/4 follow the Monomachine's interval table: OFF
at 61..63, equal-tempered steps within +/-12 semitones and just 3:2, 4:3,
5:4 and 6:5 ratios, with matching labels. POLY SIN retains +/-36 semitones;
POLY SIN EDEP/ESPD center at 64. PW uses the engine's full range (64 = 50%),
including on ENS/CHORDS; the Digitakt UI's half-range remap is not used.
CHRD remains the chord selector; TYPE is chosen through the machine list.

Useful starting patches (A..H in the table above, `-` means unused):

| TYPE | A | B | C | D | E | F | G | H |
|---|---|---|---|---|---|---|---|---|
| SAW | 64 | 0 | 40 | 0 | - | 0 | 0 | 0 |
| PULSE | 64 | 0 | 40 | 0 | 0 | 64 | 0 | 40 |
| ENS | 64 | 63 | 63 | 63 | 0 | 64 | 0 | 127 |
| VO | 64 | 43 | 113 | 64 | 0 | 0 | 40 | 100 |
| POLY SIN | 64 | 63 | 63 | 63 | 64 | 64 | - | - |
| CHORDS | 64 | 0 | 0 | 0 | 16 | 64 | 0 | 127 |
| SID-6581 | 64 | 0 | 0 | 127 | 0 | 0 | 0 | 64 |
| FM+STAT | 64 | 61 | 64 | 80 | 32 | 82 | 64 | 64 |
| FM+PAR | 64 | 61 | 64 | 82 | 64 | 104 | 80 | 64 |
| FM+DYN | 64 | 64 | 64 | 64 | 0 | 74 | 80 | 32 |
| DPRO WAVE | 64 | 0 | 0 | 0 | 96 | 0 | 0 | - |
| DPRO DDRW | 64 | 0 | 64 | 0 | 0 | 0 | 0 | 0 |
| DPRO DENS | 64 | 63 | 63 | 63 | 0 | - | 0 | 0 |

SID's PW/PWAD/PWRS affect the pulse sweep; PW=64 silences the pulse. With
MOD=RING the generator uses two triangles, so WAVE/PW do not alter that mode.
MSRC=PRCH uses the previous digital SY MnM track's last pitch, with
T1 wrapping to T8. When that track runs a different machine, it falls back to
MFRQ: there is no public AUDIO pitch service for other machine backends.
Native AMP continues to control the note's amplitude; FM+'s ENV/FEN/VEN are
the generator's internal modulation envelopes, not duplicate AMP controls.

## Backend

The MIT engine files under `upstream/` are unchanged, pinned by
`upstream/provenance.json`. Thin wrappers namespace their public symbols.
`sine.c` contains an MIT-licensed Plaits sine table (Emilie Gillet); its use
does not include the MACRO engine.

`mono2k/upstream/` pins MONO2K's GPL-3.0-only source and its full licence.
`mono2k/generate.py` verifies hashes, extracts just the ENS/SID generators and
regenerates their mathematical tables at 48 kHz. No sequencer, UI, filters,
bus effects or Elektron executable code is imported by that generator.
The FM+ port is AGPL-3.0-or-later; the combined machine is distributed under
AGPLv3 with the GPL/MIT notices retained. FM tables are included as arrays
at the owner's request, with their extraction recipe and hashes.

- ENS uses four paired BLEP saw voices, the calibrated WAVE/PW blend, and a
  half-rate chorus delay with two 0.2 Hz modulated taps. CHRL sets the blend;
  CHRW separates the taps across L/R. The four voices restart on a trigger;
  the chorus clock and delay line continue. A dedicated 2 KB line per track
  avoids clearing chorus memory when triggering the FM engines.
- The complete bank's existing stereo service runs R through its own native
  drive/FLTR/AMP states and passes both channels to MAIN. Gain/declick ramps
  advance once per block. Include `mods/samplers-st` for that shared service;
  a standalone bank folds ENS to centered mono. No core firmware change.
  Stereo is claimed when CHRL and CHRW are nonzero, then retained for 128
  blocks (about 85 ms) after the spread stops, or until the track and filter
  tails are silent (0.8.0: it used to last while the voice sounded, also after
  a TYPE change).
- Independent oscillator/filter/envelope state per digital track in bank-owned
  DDR. No core hooks or new SRAM.
- Two 16-sample control blocks per native 32-frame render; envelope timing,
  SID PWM speed and pitch are adjusted to 48 kHz. FM+ retains its native clock.
- Full Q8 ENS/SID/FM+ controls preserve fractional modulation; the other seven
  Digi Mono engines keep their original 7-bit behavior and common TUNE.
- Native TUNE remains common (-64..+63 semitones), rather than MONO2K's +/-2
  semitone fine-tune. It is applied once before either engine.
- Portable 16-bit-limb multiplication replaces wide products on ColdFire,
  preserving signed/unsigned results without libgcc, FPU or EMAC side effects.
  Hot TONE/phase-depth products split at their fixed-point boundary and use
  two 32-bit multiplies, retaining the original PCM exactly.
- High self-feedback can diverge on one-bit numerical differences. The new FM+
  comparison separates these cases from clean signal/error checks. SID MIX
  retains its prior limitations. Hardware CPU/listening remain owner QA.

- Eight independent `mono_voice` instances live in the bank's owned DDR on
  AUDIO CPU; MAIN only presents controls. No firmware patch sites or new SRAM.
- 48 kHz, 32 frames per block, Q15 converted exactly to Q31. ENS can be stereo;
  the other types remain mono. Only FM+ uses its 44.1-to-48 kHz resampler.
- Pitch arrives as MIDI Q16; TUNE is Q8, engine pitch is 1/128 semitone.
- Native TwinShot update prepares declick/gain. Its PCM pointers/second reader
  are disabled. Render runs declick and the two native gain stages on our PCM;
  TwinShot's PCM anti-imaging filter is omitted. Native drive/FLTR/AMP/FX follow.
- A signature at voice `+0x6f8` detects stock resets/backend changes. This word
  is never used as render-filter state. Re-entry clears the private oscillator
  and chorus state. TYPE changes reinitialize only on a trigger; tracks do not
  share state. There is no generated sound before the first trigger.
- Digi Mono's UI starvation guard and load shedding are **not ported**. ECO is
  always zero. After 32 blocks with the native AMP idle at zero, oscillator work
  stops until the next trigger. This is envelope-based sleep, not degradation.

## Build and verification

Use the toolchain/environment in `docs/SYNTAKT-BUILD.md`, unchanged:

```sh
python -m elekloader.sdk.syntakt.smz mods/digimono-st \
  --module mods/modulation-skip-st --out out/digimono/synfact-machines.smz4
python tests/test_st_digimono.py --cc "$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe"
python tests/test_st_digimono_emu.py --bank out/digimono/synfact-machines.smz4 \
  --stock "$SYNTAKT_STOCK" --host-cc "$LLVM_MINGW_BIN/x86_64-w64-mingw32-clang.exe"
python tests/test_units.py
```

The host runner also tests stereo ramp state, TYPE/width transitions and AMP
tails, then runs `tests/test_st_mono2k.py`: 3,456 blocks against
pinned MONO2K with native 64-bit arithmetic, deterministic extraction and
44.1/48 kHz tables, wide multiply checks, an independent 440 Hz tuning check,
all five SID waveforms, ENS L/R and chorus continuity across retriggers.
This is a port comparison, not original-MNM matching.
ENS's oracle applies the measured PW/WAVE corrections to the pinned source;
the phase mapping uses independent native-wide arithmetic in the oracle.
Regenerate committed source with `python mods/digimono-st/mono2k/generate.py`.

The emulator command needs the EMAC-capable Unicorn (`LIBUNICORN_PATH`).
It executes the linked ColdFire bank and native gain calls, comparing PCM
before native gains against the host engine, including both ENS channels and
the no-service fold-down. See `docs/TESTS.md` for scope.

Version 0.3.0's offline OS 1.32B comparison improves all 13 ENS fixtures at MIDI
48. Spectral distance falls from 0.604 to 0.047 for PCH2=96, and 0.749 to 0.160
for WAVE=127/PW=32. At CHRL/CHRW=127, L/R correlation is 0.782 versus 0.785 in
the reference. This diagnostic is not a fidelity percentage: original neutral
track output and our raw oscillator are different stages. Remaining WAVE/PW
colour, level response and wider keyboard/gate behaviour need further rounds.
The same run retains byte-identical raw WAVs for all 75 non-ENS fixtures.
ENS's saved values keep their positions but intentionally sound different:
PCH=63 now disables that added voice, and 96 selects +6 rather than +18.

Version 0.3.1 measures 149 ENS cases across MIDI 36/48/60/72/84 and 28 additional
WAVE/PW cuts. Same-note harmonic ratios cancel the common linear track response:
PW's duty is `0.01 + 0.98 * PW / 128`; WAVE blends the paired saws with weights
`1 - 0.30*x` and `1 - 1.70*x`, where `x=WAVE/127`. PW=0 retains a narrow pulse,
PW=64 stays exactly half a period, and the pulse-end blend has weight 0.70.
Fractional Q8 controls remain supported, using integer arithmetic on ColdFire.
The 22 independent harmonic-response checks reduce mean RMS error from 0.943
to 0.006 dB. This is a control-law measurement, not whole-instrument fidelity.
Repeating the original captures at DIST=48 instead of 64 barely changes their
normalized spectra; global gain is still not calibrated. Raw spectral distances
have mixed changes because that diagnostic also includes the unmatched track
chain. Chorus, intervals, UI/storage and the other eleven generators are unchanged
from 0.3.0. Native Syntakt listening and CPU timing remain owner QA.

To combine it with the normal bank, append `mods/digimono-st` to the machine
folders in the documented bank command. It needs no Monomachine/Digitakt OS.
Reserve type 70 for the parallel Machinedrum branch. This first version uses
one shared parameter set, without per-type preset memory or automatic defaults.
Hardware listening, CPU timing and original-Monomachine matching remain owner QA.
The owner reported CPU below 60% with eight CHORDS tracks on version 0.1;
the patch/settings were not recorded. This does not benchmark the new SID/FM+
types. Version 0.1.1 changes UI/core layout
lookup only; the AUDIO engine is unchanged.
Since 0.8.0 the browser catalog configuration (`js/config/syntakt-normal.json`)
leaves it out: its objects would carry the DigiPRO tables, and the catalog
tools strip only a module whose smz config names a donor recipe. It returns
to the list with a Monomachine OS donor recipe.
