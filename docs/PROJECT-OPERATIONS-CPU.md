# Project operations: reduced MAIN audio

Core **2.1.0**, Syntakt OS **1.42**.

Starting an actual project operation stops a running sequencer with the same
native STOP operation used by the recorder. While the worker runs, MAIN uses
the recorder's reduced audio path: it skips the heavy musical mixer, FX,
modulation/LFO bodies and render-in observers. At the next audio block after
the worker returns, the complete calculation resumes. The sequencer stays
stopped until the user starts it again.

This works with the recorder closed, with no retained take, and without the
recorder module or any SMZ bank. Project silence belongs to the core. Existing
OS 1.42 banks need no rebuild for this feature.

The native IRQ, DMA, CPU2 control, sample-pool transfers, output/USB tail and
render-out accounting continue. Six MAIN buses are cleared; project operations
do not audition recorder PCM or call its preview clock. AUDIO synthesis and
per-track USB retain the existing behavior of recorder idle mode. Native FX
tails are frozen and can continue when normal processing resumes. This change
does not alter the eMMC's physical speed.

## Covered operations

The hooks redirect the native worker callback literals after menu selection
and confirmation. Opening a menu or cancelling its confirmation leaves audio
and transport alone. No callback body, file format or project data is replaced.

| Operation | Callback literal instruction | Original worker |
|---|---|---|
| New project | `4005d83a` | `40059f9e` |
| Rename | `4005a486` | `4005a6f8` |
| Copy | `4005a558` | `4005a788` |
| Paste | `4005a606` | `4005a7aa` |
| Clear | `4005a6a6` | `4005a7d0` |
| Clear selected projects | `4005d20e` | `4005a828` |
| Delete selected projects | `4005d3b6` | `4005a8a2` |
| Save, including save before load | `4005b662` | `4005ba50` |
| Load | `4005d9dc` | `4005bafa` |

These OS 1.42 addresses and exact preimages were checked against the supported
stock MAIN hash in the device profile. This covers the native project UI
workers, including their synchronous pool synchronization. It does not wrap
unrelated background autosaves, arbitrary filesystem traffic or project SysEx
transfers.

## Lifetime and audio ownership

`mods/core-st/project-operations.s` wraps each one-argument worker, preserving
its capture pointer, scalar result, stack and callee-saved registers. A resident
depth counter supports nesting. Every ordinary return, including an IO error,
releases its scope; no delayed UI cleanup is needed to restore processing.
Only the short counter updates are shared with the IRQ; no storage operation
holds interrupts masked.

At render-in, `audio-idle.s` snapshots the choice for the entire block: 0 is
normal, 1 is recorder preview, 2 is project silence. The mixer uses that same
choice even if the worker returns between render-in and mixing. Project
silence takes precedence over an eligible recorder owner and does not change
its descriptor. If a recorder still owns a completed take afterward, its
existing preview policy resumes; otherwise processing is complete again.

As with recorder idle mode, an external START fails open to normal calculation
while transport is running. This change stops transport at worker entry and
does not hook MIDI START or automatically restart playback.

The host API and SMZ contract are unchanged. The core uses **65,360 / 65,536
bytes**, leaving **176 bytes**. Keeping a single foreground mailbox `send`
implementation in `sample-pool.c` removes GCC's duplicate constant-argument
clone, making room without changing the compiler flags or RAM reserve. The
retry/timeout protocol itself is unchanged.

## Offline validation

- `test_st_project_operations.py BUILD --emulate`: nine literal routes and
  324 linked ColdFire lifecycle cases covering running/stopped/uninitialized
  transport, absent/present recorder, nested scopes, success/error returns,
  capture/stack/register ABI, actual CCR branches and block-snapshot routing.
  Native STOP and IO worker bodies are explicit doubles in these cases.
- `test_st_project_idle_cfbench.py --build BUILD --cfbench CFBENCH --out OUT`:
  real OS 1.42 output tail and physical EMAC model, six cleared buses, silent
  USB MAIN, all 256 codec words compared with the full native mixer, callee
  saves, completion during a block and a missing-clear negative control.
- In the same silent fixture, the isolated mixer call takes **33,049**
  instructions normally and **7,799** during a project operation. These are
  instruction counts for this call, not whole-IRQ timing, hardware CPU
  percentages or measured save/load times.

The instrument has not been accessed. On hardware, check the covered commands
with the recorder closed and sequencer playing, an IO failure, then playback
and CPU MAIN after completion. Compare save/load durations with the same project.
