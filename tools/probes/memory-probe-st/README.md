# Memory probe (test only)

A machine for checking, on the unit, that nothing else writes two memory areas
that no code is known to use. It is type 70, `MEM PROBE`, and never belongs in a
release bank: it makes no sound, and it overwrites both areas with a pattern.
Result on the unit (2026-11-10): both areas free. The stereo tracks use them
since; do not run this probe with a stereo test bank.

| Area | Where | Why it matters |
|---|---|---|
| Shared window `0x5b0..0x7df` | AUDIO `0x5b0`, MAIN `0x100005b0` | Free space between the track audio (`0x1b0..0x5af`) and the boot pages (`0x800`). See "The shared window between the two CPUs" in [docs/MACHINE-REFERENCE.md](../../../docs/MACHINE-REFERENCE.md) |
| AUDIO SRAM `0x8000f398..0x8000ffff` | AUDIO only | The SRAM tail after FM TONE's scratch (`0x8000e8a0..0x8000f397`). The stock boot leaves it zero |

## What it does

On its first render, the AUDIO side (`audio.c`) counts the SRAM words that are
not zero, then writes a pattern into both areas. Every block it checks the
whole window and 128 SRAM words (the whole tail every seven blocks). Each word
that differs is counted, its first address kept, and the pattern restored. The
results go to window `0x7e0..0x7ff` with a magic word and a checksum.

The SYN page (`main.c`) shows them:

```
MEM PROBE
BLOCKS n          blocks the probe has run
WIN BAD n @addr   window words something else changed, and the first address
SRAM BAD n @addr  SRAM words something else changed, and the first address
SRAM START n      SRAM words that were not zero before the probe began
```

`NO DATA YET` means the AUDIO side has not run, or its record failed its checksum.
The AUDIO side runs only once its track has been triggered: until then the
scheduler keeps the track's previous backend (docs/MACHINE-REFERENCE.md,
"Scheduler tables"). One trig is enough; after it the render runs every block.

## Building and testing

Build it into a complete bank, after the machines it should live with
([docs/SYNTAKT-BUILD.md](../../../docs/SYNTAKT-BUILD.md) for the environment):

```bash
"$PY" -m elekloader.sdk.syntakt.smz mods/samplers-st mods/fm-tone-st tools/probes/memory-probe-st \
    --module mods/recorder-st --module mods/lfo3-st --module mods/arp-st --module mods/poly-st \
    --module mods/system-info-smz-st --module mods/sample-manager-st --module mods/chorus-st \
    --module mods/modulation-skip-st --out out/probe/synfact-machines-probe.smz4
```

The owner then installs the bank, puts `MEM PROBE` on one track and gives that
track a trig (one is enough). The other
tracks play as usual: samplers, FM TONE, POLY, the chorus, sample preview, project
loads, the recorder. All three counters should stay at zero while `BLOCKS` grows.
A nonzero count, with its address, shows which area is in use.
