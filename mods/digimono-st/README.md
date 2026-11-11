# SY MONOMACHINE 0.1

One digital Syntakt machine (type 71), ported from gdeo607's
[Digi Mono](https://github.com/gdeo607/digi1_mods/tree/32fd22a1fd66fa5833b808b1c1c049a0deb82d88/mods/digimono).
This is Digi Mono's clean-room interpretation of the Monomachine, **not**
the original Monomachine DSP or a claim of identical sound. MACRO is excluded.

SYN 1 has eight positions: TUNE and seven engine parameters. Unused positions
are empty. SYN 2 has TYPE; it selects a voice for the **next trigger**, including
a parameter-locked trigger. TUNE and the seven values remain shared across
types: changing TYPE never resets a Sound or its locks. Init Sound is SAW.
Save/load, locks, undo, modulation and page navigation use the existing core.

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

Controls use 0..127; TUNE is signed, centered at 64 (0 semitones), and retains
fractional tuning. ENS/POLY SIN note offsets center at 63, with +/-36 semitones;
POLY SIN EDEP/ESPD center at 64. PW uses the engine's full range (64 = 50%),
including on ENS/CHORDS; the Digitakt UI's half-range remap is not used.
CHRD is the chord selector, leaving TYPE unambiguous on SYN 2.

Useful starting patches (A..H in the table above, `-` means unused):

| TYPE | A | B | C | D | E | F | G | H |
|---|---|---|---|---|---|---|---|---|
| SAW | 64 | 0 | 40 | 0 | - | 0 | 0 | 0 |
| PULSE | 64 | 0 | 40 | 0 | 0 | 64 | 0 | 40 |
| ENS | 64 | 63 | 63 | 63 | 0 | 64 | 0 | 127 |
| VO | 64 | 43 | 113 | 64 | 0 | 0 | 40 | 100 |
| POLY SIN | 64 | 63 | 63 | 63 | 64 | 64 | - | - |
| CHORDS | 64 | 0 | 0 | 0 | 16 | 64 | 0 | 127 |

## Backend

The MIT engine files under `upstream/` are unchanged, pinned by
`upstream/provenance.json`. Thin wrappers namespace their public symbols.
`sine.c` contains an MIT-licensed Plaits sine table (Emilie Gillet); its use
does not include the MACRO engine. The Syntakt adapter is GPL-2.0-or-later.

- Eight independent `mono_voice` instances live in the bank's owned DDR on
  AUDIO CPU; MAIN only presents controls. No firmware patch sites or new SRAM.
- 48 kHz, 32 mono samples per block, Q15 converted exactly to Q31. No resampler.
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

The emulator command needs the EMAC-capable Unicorn (`LIBUNICORN_PATH`).
It executes the linked ColdFire bank and native gain calls, comparing PCM
before native gains against the host engine. See `docs/TESTS.md` for scope.

To combine it with the normal bank, append `mods/digimono-st` to the machine
folders in the documented bank command. It needs no Monomachine/Digitakt OS.
Reserve type 70 for the parallel Machinedrum branch. This first version uses
one shared parameter set, without per-type preset memory or automatic defaults.
Hardware listening, CPU timing and original-Monomachine matching remain owner QA.
The browser catalog configuration lists it as an optional machine; publishing
or regenerating the site catalog is a separate step.
