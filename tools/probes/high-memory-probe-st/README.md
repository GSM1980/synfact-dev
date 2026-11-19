# High memory probe (test only)

A machine that checks, on the unit, two things docs/AUDIO-MEMORY-MAP.md says
from the stock AUDIO code alone, before the sample pool moves there:

| Area | Where | What it shows |
|---|---|---|
| The high zone | AUDIO `4404ff50..47ff0000` (sample store pages `8001..ff40`, 63.6 MiB) | that nothing else writes it |
| The stack reserve | AUDIO `47ff0000..48000000` (64 KiB) | how deep the AUDIO stack really goes |

It is type 70, `HIGH MEM PROBE`, and never belongs in a release bank: it makes
no sound and fills the high zone with a pattern. Run it with core 2.1.1, whose
pool is still at `42101750..4404f750` (not with a core that has moved the pool).
Type 70 is also the MD machine's: build it in a bank without MD.

## What it does

The AUDIO side (`audio.c`) runs once its track has been triggered (one trig is
enough; after it the render runs every block).

- At its first render it paints the stack reserve from `47ff0000` up to just
  below its own stack pointer. Every block it reads 512 of those words: the
  lowest one found changed is the deepest any AUDIO code (stock, core, bank,
  interrupts) has reached. `STACK PEAK` is that depth from `48000000`.
- It fills the high zone with a pattern, 8,192 words a block (about 1.4 s in
  all, `FILLING n%`), then checks 2,048 words a block, over and over: a whole
  sweep takes about 5.4 s (`SWEEPS n`). Each word something else changed is
  counted (`HIGH BAD`), the first address kept, and the pattern restored.

Results go to the shared window `7f0..7ff` (MAIN `100007f0`), past the stereo
tracks' `5b0..7ef`, with a check word. The SYN page (`main.c`) shows them:

```
HIGH MEM PROBE
BLOCKS n
FILLING n%  /  SWEEPS n
HIGH BAD n @addr
STACK PEAK n B
```

`NO DATA YET`: the track has not been triggered yet, or the record failed its check.

## Building and testing

```bash
"$PY" -m elekloader.sdk.syntakt.smz mods/samplers-st mods/fm-tone-st tools/probes/high-memory-probe-st \
    --module mods/playhead-st --module mods/recorder-st --module mods/lfo3-st --module mods/arp-st --module mods/poly-st \
    --module mods/system-info-smz-st --module mods/sample-manager-st --module mods/chorus-st \
    --module mods/mix-fast-st --module mods/icache-lock-st --module mods/modulation-skip-st \
    --out out/probe-high/synfact-machines-high-probe.smz4
```

The owner installs the bank with core 2.1.1, puts `HIGH MEM PROBE` on one track
and gives it a trig. The other tracks play as usual for several minutes:
samplers (stock and long samples, stereo too), FM TONE, POLY, ARP, the chorus,
sample preview, the recorder, project loads and saves. `HIGH BAD` should stay at
zero while `SWEEPS` grows; `STACK PEAK` should stay well under 65,536 bytes.
