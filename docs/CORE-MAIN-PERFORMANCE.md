# Core MAIN performance: 1.8.1.3

Release versions and development labels: see [the consolidated MAIN release](MAIN-OPTIMIZATION-RELEASE.md). Numbers in the measurements below identify historical development iterations.

The baseline is core 1.8.1.2 with the complete MSKIP 1.4 bank. This work reduces
recurring shared-service overhead, preserving all preceding machine/module
optimizations. It is not a claim that the remaining idle CPU gap is explained.

## Retained changes

The native-service C wrapper formerly constructed a frame and saved registers
even when an operation had no interested callback. The target wrapper is now
resident assembly. Uninterested/invalid calls tail-call the unchanged original
service with the caller's arguments. Interested calls allocate only outgoing
arguments and an initialized result word before entering the same dispatcher.
Order, consumption, route snapshots and stock replay remain unchanged.

The profiler path retains its entry pointer and timer snapshot across arbitrary
callbacks, including recursive calls and callbacks replacing the profile buffer.
Its 36-word legacy layout, wrap arithmetic and operation bounds remain exact.
Invalid operations still sample the entry timer but never index the buffer.
The existing pointer is volatile so C setter writes remain visible to assembly;
no new persistent state or host API is introduced. The host C implementation
remains the semantic reference.

The track-send bridge groups three existing stores (left/right gain and mixer
record pointer) in a MOVEM. It preserves the previous scratch pointer and every
stored word, including the untouched fourth word. It saves one instruction per
captured track, twelve per normal full mixer block. Callback order, mute bits
and live EMAC state are unchanged. No extra branch or cache is added.

## Rejected prototype

Visiting only selected parameter tracks with bit scans was also compiled. The
compiler enlarged the transport enough to exceed the unchanged resident limit
by 108 bytes. That prototype is discarded; both original eight-track loops,
all four-block comparisons, trigger/LFO updates and mailbox commits remain.
No compiler flags, memory bounds or validation checks were relaxed.

## Complete-call instruction counts

Controlled linked integer fixture with the same full bank on both cores:

| Native service/callback stage | 1.8.1.2 | 1.8.1.3 |
|---|---:|---:|
| Publication with no interested callback | 45 | 28 |
| Ownership query with no interested callback | 41 | 24 |
| Modulation query with no interested callback | 41 | 24 |
| LFO dispatch, including its selected callback | 96 | 81 |
| Trigger dispatch, including its selected callback | 297 | 272 |
| Note acceptance, including its selected callback | 418 | 403 |

The all-stock parameter sender stays at 45 instructions with zero/all trigger
bits. The seven measured idle bank callbacks are unchanged. The mixer bridge's
empty route is unchanged; subscriber routes save one instruction per track.

These are guest instructions, not cycles or whole-MAIN percentages. Adapter
sites already bypass uninterested operations in several paths; a direct-query
saving only applies where the service is actually called. Native DSP bodies,
heap/metadata boundaries, cache/DDR latency and the hardware pattern affect
the total. Hardware MAIN CPU acceptance is pending.

## Memory and compatibility

The unchanged 65,536-byte resident reserve holds 65,516 bytes, leaving 20 bytes.
No runtime entry is moved into boot/load storage, no external SRAM owner is
borrowed, and no EMAC arithmetic is introduced. AUDIO remains byte-identical
to the working companion. Protected stock sections 1/2/4/5/6/8 are verified.

The complete bank is byte-identical to the MSKIP 1.4 delivery, retaining samplers
1.4.9, LFO3 1.7, ARP 1.13, POLY 1.6, Recorder 1.1.1, FM and the existing UI
modules. Only the core SYX needs changing for this comparison.

## Validation and reproduction

Use the unchanged documented toolchain/profile and fresh ignored output folders.

```sh
python tests/test_st_native_service_emu.py --before OLD_CORE --after NEW_CORE --out out/service.json
python tests/test_st_core_mixer_emu.py --before OLD_CORE --after NEW_CORE --out out/mixer.json
python tools/bench_st_idle_main.py --before-core OLD_CORE --after-core NEW_CORE \
  --before-bank BANK --after-bank BANK --out out/idle.json
python tools/bench_st_core_poly_events.py --before-core OLD_CORE/main.bin \
  --after-core NEW_CORE/main.bin --bank BANK --out out/poly-events.json
```

Delivery passes 89 common checks including 39 unit tests, 256 route encodings/
11,264 dispatcher cases, 2,880 service cases and four profiler snapshot/recursive
transitions per core, 360 linked mixer cases per core, safe-boot writes and
actual resident startup after native clearing. All original tests remain.
Real LFO3/MSKIP initialization, refusal, output activity, lock/event ownership
and native route transitions pass. The optional external FM-source comparison
is skipped.

Cross-core POLY passes eleven configurations and 165 warm operations with exact
state and balanced pools. ARP passes 26 acceptance/Sound-edit/locked-activation
cases with exact state and non-increasing counts. The six MAIN pipeline modes
(one/three FM voices; OFF/shared/independent POLY) retain their invariants.
Complete idle/native-service/mixer comparisons all meet non-increase gates.
The changed assembly and native C objects pass ColdFire instruction audits.
No device access or firmware transfer is performed.
