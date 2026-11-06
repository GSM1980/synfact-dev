# Project lifecycle audit (issue 06)

## Reproduction evidence

The user supplied an `AVE2.stprj` export after observing slow loading when
switching from another project and a slower than usual save. A reboot directly
into AVE2 loaded normally. The export was inspected locally; it is not stored in
this repository.

- The Transfer package, object CRC and native 0x3e6604-byte project container
  validate. The project pool record CRC validates and references no long sample.
- Pattern A01 is the only pattern with substantial changes from the otherwise
  empty pattern slots. The LFO3 pool section has 128 valid checks; all its pool
  records have default values.
- All 128 APC1 ARP carriers validate, as do their 1,664 Sound-record bindings.
  Only one of those ARP rows differs from the default settings. There is no
  evidence of a malformed project or a loop caused by bad sample data.

The native project serializer visits the whole fixed-size container, even for a
single used pattern. The old ARP writer additionally hashed 12 Sound records
and sealed the 320-byte carrier for every Kit. It created valid carriers for
default settings, so project load also checked all 1,664 bindings. The firmware
boot path and the live project-switch path do not use the same decode sequence;
this is consistent with the user's different timings, but their wall-clock
times still require a device measurement.

## Change and offline verification

`arp-st` now seals a Kit carrier once after its twelve rows. When all thirteen
rows (the twelve Kit Sounds and the corresponding pool Sound) have default ARP
settings, it leaves the carrier absent. An absent carrier already means defaults
in the existing decoder; the APC1 layout and project size do not change. Old
projects with full APC1 carriers remain readable. Reverting an edited Sound to
defaults removes the carrier once its other rows are defaults.

With the same core, bank selection and emulated native serializer fixture,
the first full Kit/pool serialization took 24,782,579 emulated MAIN instructions
before this change and 4,772,223 afterwards (81% fewer). The ARP/LFO3 decode
callback for one default Kit from AVE2 took 83,524 instructions with its old
carrier and 10,288 with an absent default carrier. These counts are workload
proxies, not device CPU percentages or promises of equivalent wall-clock gains.

`tests/test_st_arp_persist_emu.py` verifies Sound and Kit save/reload, old-style
carrier decode, default-carrier removal, A/B copy, cleared-RAM restore, LFO3
coexistence, ARP MODE/SPEED/RANGE/LEN pattern locks, and the native NOT3 chord
lane on step 17. The test uses the original target codecs under emulation. It
does not exercise eMMC, a complete scheduler, or a physical save/reboot.

## Remaining checks

1. On the Syntakt, load AVE2 from another project and time the loading spinner;
   repeat after saving it with the optimized bank. Compare a direct reboot.
2. Save/reboot/load a project with changed ARP Sound parameters, LFO3 settings,
   POLY state, and ARP p-locks; verify each in the UI and playback.
3. Check a new project and an A/B copy for clean defaults and retained edits.
4. If a save or load still hangs, capture the exact screen, elapsed time,
   source/destination project slots and installed core/bank versions. A project
   export alone cannot expose the native eMMC timing or the spinner task state.

Other audit candidates remain separate from the AVE2 slowness: the sample-pool
save hook accepts slot 128 before the native saver rejects it, and the project
load hook retains the previous pool record if its extra raw-sector read fails.
Neither explains a valid AVE2 export with no long sample references. They need
their own bounded failure-path tests before a core firmware change.
