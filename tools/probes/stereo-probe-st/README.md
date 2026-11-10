# Stereo probe (test only)

A machine that shows the stereo transport's counters (docs/MACHINE-REFERENCE.md,
"Stereo tracks"). It is type 70, `ST PROBE`, makes no sound and never belongs in
a release bank. It needs `mods/samplers-st` in the same bank: it reads the MAIN
module's counters directly.

Its SYN page:

```
ST PROBE A ON M FF   A ON: the AUDIO engine has written; M: tracks with D now
MAIN RD n MIX n      MAIN's cost, permille of its interrupt period: reading D, mixing it
DROP n LATE n        blocks with no D matching their L; blocks that used the second read
TORN n AGAIN n       reads caught mid-write; the same L and D mixed again
S. D. M. A. X. R.    permille of the block: state copies, drive+width, multimode,
                     AMP, restore+send, sampler renders (whole stereo renders)
```

The profiles count only while System info measures (SETTINGS > SYSTEM INFO):
they use the same timers.

As with any bank machine, the page counts only once its track has been
triggered (one trig is enough).

## Building the transport test bank

Copy `mods/samplers-st` to a folder outside the repository, add `stereo-test.h`
there with `#define SP_STEREO_TEST 1`, and build the bank from that copy
([docs/SYNTAKT-BUILD.md](../../../docs/SYNTAKT-BUILD.md) for the environment):

```bash
"$PY" -m elekloader.sdk.syntakt.smz <copy>/samplers-st mods/fm-tone-st tools/probes/stereo-probe-st \
    --module mods/recorder-st --module mods/lfo3-st --module mods/arp-st --module mods/poly-st \
    --module mods/system-info-smz-st --module mods/sample-manager-st --module mods/chorus-st \
    --module mods/modulation-skip-st --out out/stereo-test/synfact-machines-stereo-test.smz4
```

It needs core 1.8 on the unit. With the probe on a track, `A ON` and `DROP`
near zero mean the transport works.
