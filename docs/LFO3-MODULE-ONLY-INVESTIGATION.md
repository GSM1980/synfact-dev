# LFO3 controls in a module-only bank

Investigated on 2026-11-19, then reproduced and fixed on remote main
`2b37c65` (core 2.1.1, Syntakt OS 1.42). The fix is core **2.1.2**.

## Cause and correction

LFO3 and ARP are modules. A bank containing either or both can have zero
machine descriptors, a composition the SMZ builder and parser support.

`core_machine_load()` calls `core_machine_bind(..., again.count)` before
initializing the modules. The latter cleared `core_machine_active` and
returned success immediately for a zero machine count. It skipped the empty
directory, lane table and final activation.

The LFO3 initializer could install its page hooks and register its parameter
provider. PAGE_NAV also dispatches before the machine gate in
`core_machine_nav()`, so the third page appeared. However, `STOCK_FIRST` and
`STOCK_FIRST_SEND` in `machine-hooks.s`, and `core_machine_query()` in C,
bypassed the provider while `core_machine_active` was zero. LFO3's IDs then
reached native fallbacks: ERR labels, missing ranges and missing editor
metadata aliases. Relative bounds in `machine-boot.s` and provider-backed
formatting/dials use the same activation flag.

Core 2.1.2 removes the zero-count early return from `core_machine_bind()`.
An empty registry follows the normal initialization path and becomes active
only after valid directory/lane storage exists. This uses 700 bytes of native
heap, outside the core's resident reserve. Setting the flag alone would leave
custom-type lookups and lane paths with invalid pointers.

The machine loops have zero iterations. Startup without a bank does not call
the binder and remains inactive. Allocation failure returns failure before
activation. No hook, public API, SMZ4 contract, file format or AUDIO change is
needed. Existing OS 1.42 banks can use the corrected core.

## Regression evidence

`tests/test_core_st_machines.c` compiles production registry/page C on a
32-bit host. Its added regression fails on remote main at the empty registry's
activation assertion and passes with the fix. It covers all ten LFO3 metadata
IDs, with and without another provider, native fallbacks, absence of all
sixteen custom machine types, initialized lanes, no-bank behavior and
allocation failure/recovery. Metadata and allocator services are explicit
doubles. The normal common regressions include this test.

`tests/test_st_module_only_emu.py` goes further: it executes the linked
empty-registry binder, module binder and actual LFO3/ARP initialization in
Unicorn against OS 1.42 MAIN and two source-built module-only banks. Those
banks include the usual always-included optimization modules.

| Core | LFO3 bank | LFO3 + ARP bank |
|---|---|---|
| 2.1.1, unmodified remote main | Provider registered, first label ERR | Provider registered, first label ERR |
| 2.1.2 | Ten labels, ranges, ID/cache routes and record edits pass | Same checks pass, with real ARP initialization |

The emulator checks actual native short-label hooks against LFO2's labels,
non-ERR long labels, engine/UI ID round trips, cached metadata aliases,
provider ranges against native LFO2 ranges and ten record write/read pairs
per bank. It also checks inactive no-bank fallbacks and refused allocation.
The previous core reproduces the reported ERR as a required negative control.

Run the focused emulator regression after building the current core and the
two banks (with their sibling `.work` directories):

```bash
python -B tests/test_st_module_only_emu.py \
    --core out/core-st-2.1.2-lfo3-module-only \
    --bank out/module-only-fix/lfo3.smz4 \
    --bank out/module-only-fix/lfo3-arp.smz4 \
    --reference-core out/module-only-fix/remote-main-core \
    --out out/module-only-fix/module-only-emu.json
```

Allocation, mutex, other module initializers and the proxy's native getter
are explicit fixture boundaries. This is not a full RTOS boot, renderer,
physical encoder or hardware test.

`tools/diagnose_st_module_only.c` retains the small dispatch-only host probe
from the investigation. With the old core it reports 0/50 answered metadata
queries and 0/30 aliases for either empty registry; with the corrected core
both become 50/50 and 30/30, matching the one-machine control. It uses a
metadata double and accepts `REGISTRY_SOURCE` to compare a saved old source.

## Build and delivery

The core was built with the documented SGDK 2.11 flags and the supplied
official OS 1.42 input. SYX verification passes, all protected sections remain
stock, AUDIO is byte-identical to a freshly built remote-main core, and
11,616 bytes remain free in the 64 KiB resident reserve. The full machine/module
bank and both module-only banks build successfully. Unit tests: 39 passed.

All executed host/static common-regression checks pass, including the parser,
registry, module lists, sampler/recorder/ARP controls, hooks and instruction
audit. Linked project lifecycle, stock streaming, safe boot, module startup,
native routes/services/mixer and the 39-case full native catalog also pass.
The production parser accepts both module-only banks.

The complete common runner stops at `test_st_patchers_emu.py`:
`probe_modulation`, native PC `40097b3e`, raises a Unicorn CPU exception.
The same command, same bank and a freshly built unmodified remote-main core
reproduce the identical failure. This is the pre-existing fixture limitation
already recorded in TESTS.md, not a passing check. Historical FM comparison and
LFO3 before/after-bank performance differentials were not run; no previous-bank
fixture or pinned historical source was supplied. No check was weakened.

The release artifact is the corrected core SYX. Rebuilding only an SMZ bank
leaves the installed core's gates unchanged.

The final A/B package also contains a literal LFO3-only bank: one module, zero
machines and no optimization modules. Its parser and linked metadata/edit
checks pass, including the previous-core ERR negative control. The user tested
the original and corrected cores with that bank and confirmed the correction
on the instrument. Device installation and testing were performed by the user.
