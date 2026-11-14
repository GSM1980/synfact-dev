# Consolidated MAIN optimization release

This release includes the complete MAIN optimization work in one version step
per changed component, relative to main. Intermediate bank/core numbers were
local development labels; they do not represent additional public releases.

| Component | Previous main | Consolidated release | Final development label |
|---|---|---|---|
| Core | 1.8.0 | **1.8.1** | 1.8.1.3 |
| SP samplers | 1.4 | **1.5** | 1.4.9 |
| LFO3 | 1.2 | **1.3** | 1.7 |
| ARP | 1.9 | **1.10** | 1.13 |
| POLY | 1.5 | **1.6** | 1.6 |
| Recorder | 1.1 | **1.2** | 1.1.1 |
| Modulation Skip | 1.2.3 | **1.2.4** | 1.4 |

Machines, FM Tone, Chorus, System Info and Sample Manager keep their existing
versions. No AUDIO DSP changes are included. Chorus remains optional; its
SRAM ownership still takes precedence over the stereo fallback placement.

## Retained behavior and improvements

- Stereo MAIN uses bounded integer/EMAC assembly kernels, shared gain-history
  commits, fixed bus specializations and guarded SRAM placement/fallback.
  [Measurements and ownership](IDLE-MAIN-PERFORMANCE.md).
- LFO3 avoids work when no output can be produced and removes its inactive
  native LFO subscription. Per the accepted behavior change, inactive engines
  suspend phase, fades, triggers and RANDOM progression; active rendering and
  event/lock behavior retain their checked paths.
  [LFO3 measurements](LFO3-MAIN-PERFORMANCE.md).
- ARP uses exact clock-period tables, direct unchanged settings, integer
  selector/keyboard assembly and selected-track traversal without changing
  notes, timing, locks or pool ownership. [ARP measurements](ARP-MAIN-PERFORMANCE.md).
- POLY uses faster integer synchronization, aligned publication/copies and
  bounded origin scanning, preserving topology, events and native synthesis.
  [POLY measurements](POLY-MAIN-PERFORMANCE.md).
- Recorder's closed-view callback maintains the existing clock/phase with a
  smaller frame and only enters audition mixing when playback is present.
- Modulation Skip reduces decision, refresh, projection and partial-run work,
  preserving the native output, source order, cadence, audits and fallback.
  [Modulation Skip measurements](MODULATION-SKIP-MAIN-PERFORMANCE.md).
- The core routes native operations only to interested callbacks and uses a
  resident assembly service wrapper, with unchanged legacy profiling and
  grouped mixer capture. [Core measurements](CORE-MAIN-PERFORMANCE.md).

## Historical labels and validation

The performance/design records retain their development numbers so that their
before/after tables and local artifacts remain traceable. Their prototype
numbers, pending-at-the-time comments and diagnostic banks are historical.
The withdrawn development core called 1.8.1 used a nonresident binder and is
not this release: final startup code remains resident and is tested after
native memory clearing. No failed prototype or diagnostic callback cut is
part of the functional release.

The owner reports MAIN at **83%** with the final development core/bank pair
on the stock reference pattern. This is a hardware observation with rounded
telemetry, not a cycle-accurate universal saving. Instruction comparisons in
the linked tests remain separate from device CPU percentages.

The consolidated build uses the same compiler profile and memory bounds.
Generated changelogs record only the consolidated release versions. Firmware
and locally generated performance artifacts remain outside Git.

The renamed build passes 89 common checks and all 63 TypeScript tests, including
byte-for-byte browser/desktop core generation. The resident-startup guard is
enforced on both builders. Every compiled bank object matches the hardware-tested
development bank, and core AUDIO is identical. Stereo's complete DDR/SRAM/Chorus/
occupied-window differential and the exhaustive 65,536-value zero projection pass
with unchanged outputs and instruction counts. LFO3's pinned sources reproduce
all twelve generated lock files exactly.
