# System info in the Syntakt SMZ

Uses the same [System info implementation](../system-info-st/README.md), font,
icon, CPU/RAM collectors, cadence and AUDIO mailbox. Only its placement changes:
UI/collector code and state live in the SMZ. Requires core 0.12 / UI API 6 /
host API 2. The core retains clock accounting and exposes its existing add-item,
pool-busy and mailbox-owner services through the borrowed host table.

Subscriptions remain SETTINGS, TICK and global DRAW. No firmware site is added.
Core builder `--system-info` enables the AUDIO meter; include this module with
`--module mods/system-info-smz-st` when building the SMZ. The full POLY bank
includes it, FM TONE, the four SP machines and the recorder. Do not also install
the legacy resident System info module in the same configuration.

The ordinary `mods/system-info-st` sources are retained for older/smaller
configurations. Their resident placement still has to pass the 64 KiB linker
budget; the current full POLY core uses the SMZ placement.

Version 0.2 requests a redraw only when the displayed text changes or the user
toggles System info. Measurement windows (30 UI ticks), CPU/RAM page changes
(60 ticks), per-tick AUDIO response polling and native drawing remain intact.
Formatting runs only at those windows/page transitions or the first enabled
tick. Turning OFF requests one paint to remove the header. Other native redraw
requests are never cleared, and the header still paints on every requested draw.
The shared resident implementation uses the same optimization.

`tests/test_st_system_info.py` compiles the actual production C with explicit
native collector boundaries. It verifies 1,800 ticks / 60 measurement windows /
30 redraw requests for stable readings, first paint, page changes, OFF cleanup,
100% formatting, AUDIO polling and requested native redraws. This is not a
measurement of CPU savings on the physical device.
