# System info for Syntakt OS 1.41

Adds an info icon and `SYSTEM INFO: OFF/ON` to SETTINGS. Press YES on the
row to toggle. OFF is the power-on default; the setting is temporary.
The header alternates every two seconds between:

```
CPU M xx% CPU A xx%
RAM M xx% RAM A xx%
```

Both labels fit in the title area using a 3x5 font. Values update about
once per second; `--%` means unavailable, warming up, or rejected by the
collector's checks. The left track/level area is retained.

## Current placement (core 0.12)

The standard combined build now uses [system-info-smz-st](../system-info-smz-st)
for this same UI/collector implementation. The core retains clock accounting;
`--system-info` builds the AUDIO meter. Include the SMZ module with the bank.
This frees resident space for generic POLY services. The resident variant below
is retained for older/smaller configurations and must still fit the linker's
64 KiB budget. The current full POLY core uses its SMZ placement; do not enable
both variants together.

## Legacy resident build

The MAIN mod uses the normal SDK, subscriptions and linker. Requires
`core-st`. The complete SDK build composes the AUDIO meter in
`mods/core-st/audio-meter.s` with the machine services, then requests that exact image
from the profile-bounded secondary section API. This explicit step is
needed because the ordinary patch CLI still changes MAIN alone.

From the repository root, with the profile's ColdFire toolchain on PATH:

```sh
python -m elekloader.sdk.syntakt --system-info --mod mods/system-info-st --stock Syntakt_OS1.41.syx --eft /path/to/elektron-firmware-tool
```

`ELEKLOADER_CROSS` selects the compiler prefix, as for the normal SDK.
The compact compressor is an explicit local dependency; the build downloads
nothing and sends nothing to a device. Outputs are ignored:

- `mods/core-st/out/core-0.4.elemod`
- `mods/system-info-st/out/system-info-st-0.1.elemod`
- `out/core-st-machines/Syntakt_OS1.41_Elekloader_Core0.4_SystemInfo_EXPERIMENTAL.syx`
- `out/core-st-machines/verification.json` and `packaging.log`

Building the MAIN `.elemod` alone is supported, but AUDIO readings stay
unavailable without its companion. The generic GUI/CLI does not install it.

## Measurement contract

MAIN accounting identifies the two parked idle tasks and attributes the audio
interrupt separately. The telemetry layer adds no machines or project fields.

| Reading | Meaning |
|---|---|
| CPU M | 100% minus parked idle time; time in the MAIN audio interrupt during idle counts as busy. Other interrupts are not separately attributed. |
| CPU A | Time in AUDIO's instrumented block interrupt divided by elapsed AUDIO timer ticks; work outside that path is excluded. |
| RAM M | Native committed DDR reservations minus validated free buddy blocks, counted once; the heap is inspected under its own native mutex. |
| RAM A | Static AUDIO reservation plus the stack depth observed at the meter entry; not a full deepest-stack scan or sample occupancy. |

RAM uses 128 MiB per processor. Native reservations make a high idle value
normal (MAIN roughly 85% minimum, AUDIO roughly 50%). RAM is committed memory,
not a count of active sounds or samples. Neither RAM collector claims unused
addresses are safe for allocation.

MAIN sends only read-only CMD4 queries via the post-boot FPGA page window
(`MAIN 10000800`, `AUDIO 00000800`). Each query clears the extension, commits
the sequence last and has a bounded timeout. AUDIO validates the magic,
command, zero destination/length and checksum, publishes a generation-tagged
snapshot and acknowledges it. It implements no DATA, POKE or RETIRE command.
There is no wait loop in the UI thread. OFF stops queries/publication.

On the first enabled query, AUDIO enables only the DTIM0 counter clock:
native mode `00c1` becomes `00c3`; capture/IRQ settings, timer period and
vectors are preserved. Unexpected modes invalidate the reading. OFF does
not rewrite the timer configuration.

## Placement and integrity

MAIN core and mod share the linker's 64 KiB reserve at `46000000`.
AUDIO code and state use the bounded application reserve
`40040860..40041050`. The SDK composes meter and machine services into one
image; they never overwrite each other. MAIN and AUDIO are separate CPUs.
Stock hashes, preimages, lengths and the exact permitted ranges are checked.
Runtime timing, mailbox behavior and physical ownership need hardware QA.

The final reader checks MAIN decompression, a minimum 4096-byte in-place
margin, the conservative stock-size container budget, both expected images,
framing/checksums, unchanged header/version, and every untouched stored section.
Bootstrap (2), updater (4), boot stub (6), sections 1/5/8 and their destinations
remain byte-identical. No startup copy/clear routine is patched.

Validation here is build/link, static instruction decoding and container
verification. No emulator or device run is performed; timing, physical mailbox
operation and interaction with the native UI require hardware confirmation.

## Credits and licences

GPL-2.0-or-later (repository LICENSE). Source-level provenance and notices are
retained in the implementation files.
The accounting was inspired by irpina/digihealth, reference commit
`b2c3cf16eaaf0ed638214858a487c955b5efd5cc`.

The 9x9 info icon is a hand-rasterized adaptation of Google
Material Symbols “info” (outlined), Apache-2.0; see `LICENSE-icon.txt`.
This port removes the additional peak/FM/version pages, replaces the native
text renderer with a compact font, uses elekloader subscriptions, and reduces
the AUDIO bridge to a read-only meter. Firmware-derived outputs are not source
files and must never be committed.
