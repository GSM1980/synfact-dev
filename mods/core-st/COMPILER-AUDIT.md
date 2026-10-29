# Compiler and instruction review, 2026-10-26

This closes the interrupted static compiler review for core 0.8.0. It does not
mark the new sampler UI or sound as physically validated.

## Target and optimization

The installed cross compiler is GCC 13.2.0. With the actual device options its
preprocessor defines `__mcf_cpu_54415`, `__mcf_family_54418`, `__mcfisac__`,
`__mcfv4__` and `__mcfhwdiv__`; it does not define `__mcffpu__`.
All target C uses `-mcpu=54415 -mtune=cfv4 -O3` and
`--param=max-completely-peel-times=4`. No fast-math is enabled.

MCF54415 has no hardware FPU. Float/double arithmetic can require software
library helpers; this freestanding build does not automatically supply those
helpers. Such unresolved calls fail linking. Integer/fixed-point DSP avoids
that cost. EMAC is available, but enabling a CPU target does not mean GCC
automatically turns every C multiply/accumulate into an EMAC instruction.

O3 is a speed-oriented starting point, not evidence of minimum possible CPU
usage. The loop-expansion cap was selected from generated code and the fixed
AUDIO window; no O2/O3 cycle comparison or cache benchmark has run on the unit.
Moving code into SMZ frees resident space but alone does not reduce CPU cost.

Primary references:

- [NXP MCF5441x manual](https://www.nxp.com/docs/en/reference-manual/MCF54418RM.pdf),
  section 3.3.2 and table 3-9: ISA_C, EMAC present, FPU absent.
- [NXP ColdFire programmer reference](https://www.nxp.com/docs/en/reference-manual/CFPRM.pdf):
  instruction encodings and allowed operands.
- [GCC m68k target options](https://gcc.gnu.org/onlinedocs/gcc/M680x0-Options.html):
  CPU targeting, tuning and software floating point.

## What the static audit checks

The SDK rejects decoder flags `FPU`, `LINEF` and `ILLEGAL`: floating-point
instructions, unrecognized line-F operations and unsupported encodings/forms.
Examples covered by regression tests include FADD/FMOVE, FSAVE, DBRA, MOVEP
and ILLEGAL. Legal cache operations in line F remain allowed; line F as a whole
is not forbidden. EMAC, MOVEC and recognized privileged instructions remain
allowed because the core uses them. Privilege correctness is context-dependent.

The interrupted review exposed a boundary bug: a final `MOVE.L #imm,D0` opcode
could pass even when its immediate bytes were absent. The shared SDK audit now
checks range alignment and that each instruction fits entirely in the range.
Tests reject every incomplete prefix of several multiword instructions and
retain acceptance of their complete forms. MAIN and AUDIO use the same check.

Linked C function ranges are now checked for the core AUDIO companion and the
generic AUDIO pool service as well as both sides of SMZ banks. Functions are
bounded by ELF symbols so interleaved data is not decoded as instructions.
Assembly objects are checked before linking over explicit code ranges; this
is not a claim of complete linked assembly coverage. The decoder has not been
formally verified against every legal/illegal ISA encoding. This check does not
validate addresses, synchronization, stack use, callback privileges or all
execution paths. Native-host tests execute host code, not ColdFire code.

## Rebuilt artifacts and validation

New output: `out/core-st-0.8.0-resumed-audit/`.

The rebuilt SYX, MAIN image, AUDIO image and sampler SMZ are byte-identical to
`out/core-st-0.8.0-delivery/`. Only build-time checks/reports have changed; there
is no firmware version bump and no need to reinstall for these audit changes.

| Result | Value |
|---|---:|
| Resident RAM used / capacity | 63,468 / 65,536 bytes |
| Resident RAM free | 2,068 bytes |
| MAIN decompression minimum gap | 10,516 bytes |
| Container budget headroom | 26,928 bytes |
| Core MAIN object code ranges audited | 23 |
| Core AUDIO linked C functions audited | 4 |
| Pool AUDIO linked C functions audited | 2 |
| Sampler SMZ linked MAIN / AUDIO C functions audited | 7 / 19 |

Stored sections 1/2/4/5/6/8 and container/version header remain identical to
official OS 1.41. Bootloader, bootstrap and updater are unchanged.

Validation run in this continuation:

- `tests/test_units.py`: 30 passed.
- `tests/test_st_instruction_audit.py`: 4 passed.
- `tests/test_st_mods_cli.py`: 5 passed.
- `tests/test_syntakt_secondary.py`: 3 passed.
- Fresh native 32-bit builds: sampler UI, 8/9/27/28 parameter sender/receiver,
  four-page machine/layout mapping, optional module publication, and parser
  compatibility for the legacy bank, new sampler bank and module-only bank.
- Full source SYX build, sampler SMZ and module-only SMZ builds, and lint passed.

Windows tests mapping target-like addresses above 2 GiB require
`-Wl,--large-address-aware`; the parameter test passed with that host option.

Parameter transport uses the native eight controls plus only the defined extra
values: 27 controls add 19, and 28 add 20. An odd final pair has a zero padding
halfword, not an additional parameter. The current storage ABI supports 28
controls, with 32 positions reserved. The four SP machines each have eight.

Physical checks still pending: TUNE dial travel, PLAY graphics, fractional
encoder feel, pool slots 1..128, SYN2 waveform/markers/playhead and audio/CPU
behavior. SMZ loading stays silent and the existing sample bar remains.
No emulator run, MIDI transfer, flash or device write was performed here.
