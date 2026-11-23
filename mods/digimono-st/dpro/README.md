# De-Pattern DigiPRO for SY MnM 0.8.0

TYPE 12/13/14 are the Monomachine's DigiPRO machines **DPRO WAVE**,
**DPRO DDRW** and **DPRO DENS**, ported from De-Pattern's reverse-engineered
algorithms in [dn2mk2-firmware](https://github.com/De-Pattern/dn2mk2-firmware),
pinned at `a0249fc55fd188242c92f377b4cbb2748b851a35`, the same commit as FM+.
`provenance.json` records the original paths and hashes. `upstream/` keeps the
unmodified `wave.py`, `dec.py`, `dpro.py`, `ddrw.py`, `dens.py`, their machine
table and PERC's reciprocal, with the full AGPLv3 licence; the DSL and the ROM
reader they share with FM+ stay in `../fmplus/upstream`. The port is
AGPL-3.0-or-later.

**DPRO BBOX is not included.** De-Pattern did not port it (24 drum samples,
about 189 KB, did not fit its firmware), so there is no reference to port from.

## The tables come from your Monomachine OS

The DigiPRO waves, the interpolator, PCH, WID, the bit reducer and the exp table
are Elektron's data, read from the owner's Monomachine SFX-6/60 OS 1.32B
(`Elektron_SFX6-60_OS1.32B.syx`, sha256 `fe13c09f…a1710`). **None of it is in
the repository.** Every checkout makes the header before building:

```bash
python mods/digimono-st/dpro/import_tables.py "$MONOMACHINE_OS"
```

It writes `dpro/tables.h` (git-ignored) and `dpro/tables.json` (committed: the
OS hash and the header's hash). A bank build without the header stops with a
message naming this command. The Digibank stand-in takes about a minute the
first time; it is cached in `dpro/out/` (git-ignored).

**The web catalog cannot ship SY MnM 0.8.0 yet:** its AUDIO object holds these
tables. It needs the donor mechanism FM TONE uses (the site's builder reading
the user's Monomachine OS) before SY MnM goes back into a web release.

## What runs on Syntakt

- **The blocks run at 48 kHz**: two 16-frame blocks a 32-frame render, with
  no converter. Everything that depends on the rate is scaled by 44100/48000 =
  147/160: the increments, DPRO WAVE's sync period (48,000 blocks of 16 a
  second) and its WP sweep, DDRW's fade. Pitch and times stay the
  Monomachine's; only the fixed filters (WAVE's decimator, the interpolator)
  sit 8.8% higher against the audio. `DM_DPRO_44K` builds the Monomachine's own
  44.1 kHz blocks followed by FM+'s 160-phase, 16-tap converter instead; both
  are tested. Syntakt's declick, gain, AMP, FLTR and FX follow, as for every SY
  MnM type.
- The increments are the Monomachine's own integer arithmetic (`exact.py`),
  word for word, before that scaling: the kernel's exp table, DPRO WAVE's phase
  and sync period, DENS's PCH ratios and mutes (in Hz, so unscaled), DDRW's WID
  detune scaled by MIX. They are recomputed only when the pitch or a knob they
  read changes.
- Signals are Q26 integers (three guard bits above the original Q23 words).
  Products run on the EMAC (fractional, truncating). A sum of products is one
  accumulator chain read once: the EMAC adds each product as floor(p / 2^23)
  and reads floor(sum / 2^8), and the split build computes exactly that
  (checked on analog-emu). ACC0, ACC1, their extension words and MACSR are
  restored. No FPU or libgcc arithmetic: 64-bit products are built from 16-bit
  limbs.
- Pitch follows FM+: Syntakt note 60 is 261.6 Hz, common TUNE applied first.
  DPRO WAVE plays an octave up, as De-Pattern does, because on the Monomachine it
  sounds an octave under the other machines at the same pitch.

### DPRO WAVE

One of 32 single-cycle waves (512 12-bit samples) read with linear
interpolation at 4x through a 512..1023-sample window that WP, swept by WPM,
slides into the next wave; WPRS restarts the sweep on a trig. SYNC: OFF, SFRQ
(its own oscillator) or PRCH (the previous track's pitch, when that track is
SY MnM; otherwise De-Pattern's "none", pitch word 0). Then the Monomachine's
polyphase IIR decimator, two inner rounds, as `dec.py`'s register program:
each run of products into one register is an accumulator chain, and a chain the
program doubles (ASL) before adding G y1 sums its products and G/2 y1, then
doubles once. The waves are kept decoded (Q26, 68 KB).

### DPRO DDRW

Two Digibank waves (WAV1, WAV2), each at its own pitch (WID detunes them, scaled
by how mixed they are), each fading from the previous wave on a change (TIME),
bit-reduced (BR1, BR2), read at two mip levels crossfaded over each octave's top
1/16, then MIX ramped over the block. TIME and BR follow De-Pattern's host
mapping (its v0.56, after its owner found the original's TIME inaudible and BR
mostly inaudible): BR 1..127 uses the reducer's 64..127 (9 bits down to 3; 0
stays clean), and TIME 0..127 is a fade from instant to about 2 s.

### DPRO DENS

Four oscillators on one Digibank wave, three at PCH2..4's ratios (equal-tempered
±12 semitones and just intervals; the middle is OFF), each band-limited by mip
level with the partner crossfade, mixed at 0.633 each; an oscillator at 20 kHz
or more, or at the first one's whole Hz, is silent. PCH's table is ENS's,
word for word, so the labels are ENS's. **CHRL/CHRW are hidden in this
release**: DENS plays mono and the bank's Chorus FX serves it. A chorus in the
machine with CHRW's stereo spread needs the shared stereo service's second
pass, which with seven stock tracks did not fit on the unit (99 % AUDIO). The
chorus written for it stays in `mono2k.c` (`dm_ens_chorus`), unused: the
Monomachine's CHRL/CHRW are its chorus after the machine, which De-Pattern does
not model; that one is
MONO2K's ENS chorus (measured on the Monomachine's SWAVE-ENS), with its stereo
spread; for DENS its 0.2 Hz LFO's two delays are computed at each block's first
and last samples and taken linearly between them (they move under 0.2 of a line
sample a block). ENS keeps its own per-sample LFO. That chorus was not measured
on DENS itself.

### The Digibank

DDRW and DENS read the Monomachine's user Digibank (64 waves in its own flash),
which is not in the OS file. As De-Pattern does, they read a stand-in built from
the 32 ROM DigiPRO waves (nine mip levels through a 7-tap halfband chain, each
word a 16-bit sample with its crossfade partner's byte), here precomputed by
`import_tables.py` (256 KB of AUDIO, Q26), with each level's partner sample
precomputed at the level's own positions (another 256 KB). A level read that does
not wrap is interpolated in place. A wave knob selects slot `knob >> 1`; slots
32..63 repeat 0..31.

### Read rate

`DM_DPRO_OS` sets DDRW's and DENS's read rate: **1** (shipped) is De-Pattern's
own trimmed rate, read at the block rate from the next mip level up; **2** is the
Monomachine's 2x read with its 2:1 allpass decimator. DPRO WAVE always runs the
Monomachine's 4x. Both rates are tested against the DSL.

## Checks

```bash
python mods/digimono-st/dpro/import_tables.py "$MONOMACHINE_OS" --check
python tests/test_st_dpro.py --cc "$LLVM_MINGW_BIN/x86_64-w64-mingw32-clang.exe" --os "$MONOMACHINE_OS" [--os-rate 2] [--44k]
python tests/test_st_dpro_cfbench.py --cfbench "$CFBENCH" --out out/dpro-cfbench [--os-rate 2] [--44k]
```

- `test_st_dpro.py`: the header against `tables.json`; pitch words and every
  increment against `exact.py` (about 12,600 combinations; at 48 kHz, with the
  same 147/160 scaling in Python); then 16-frame blocks (notes, each control at
  0..127, fractional controls, sync, wave changes and fades, heavy BR, chords)
  against the pinned float32 DSL run with those increments: 215 cases with
  `--44k`, 206 at 48 kHz (the WPM sweep cases need `--44k`: the DSL cannot take
  a scaled sweep). All at 60 dB or more; measured worst: WAVE 86 dB, DDRW 90 dB,
  DENS 109 dB.
- `test_st_dpro_cfbench.py`: on analog-emu's ColdFire V4e with its EMAC, the
  EMAC build equals the split integer build in PCM, voice state and every EMAC
  register over 480 random renders; then the instructions of 32-frame renders
  at heavy settings.

Instructions per 32-frame render (cfbench, heavy settings, read rate 1), not
counting native gain/mix, AMP, FLTR or FX:

| | first port (44.1 kHz) | 0.8.0 (48 kHz) | `--44k` |
|---|---:|---:|---:|
| DPRO WAVE | 18,700 | 9,000-9,300 | 11,900-12,200 |
| DPRO DDRW | 16,600 | 8,400-8,500 | 11,450-11,500 |
| DPRO DENS | 20,000 | 8,800-9,800 | 11,600-12,700 |

With knobs changed every render (DDRW fading all the time, DENS's four
oscillators at random intervals) the worst seen is DDRW 10,600 and DENS 11,600.
DENS's interpolator now reads the kernel at 256 fixed phases (worst 60.5 dB
against the DSL, level and bands unchanged; `DM_DENS_KERNEL` keeps the kernel):
the heavy DENS render is 7,200-8,200. DENS has no chorus of its own in this
release (see above).
FM+ 0.7.0 is 11,000 to 12,700 by the same linked measure. These are instruction
counts without cache stalls, not AUDIO CPU percentages; how many fit beside
other tracks is for the unit's AUDIO meter. Nothing here was run on a Syntakt.
