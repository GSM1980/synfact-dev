# Sample Manager (Syntakt)

Desktop OS 1.42 release: **1.3.1**.
Requires core 2.3 or later (sample-pool API 2) and a matching bank
(SMZ4 contract `0x53594606`).

The header places SAMPLE MANAGER at the left and a two-line Project MEM /
(used/total) MB block at the right of the 128-pixel LCD. The list shows six
rows. Sample rows show PCM size in MB at the right, with two decimal places;
names that would overlap it end in `...`. Folders keep their native labels.
MB uses 1024 x 1024 bytes. Stereo sizes include both channels. The project
meter counts unique AUDIO extents in slots 65..128, including 2 KiB page
padding; slots 1..64 use a separate budget. It refreshes when opening the
manager and after every live edit, including failed edits and removals.
The current capacity is 63.62 MB. Core preflight rejects an entire batch
with AUDIO FULL before loading anything when its allocations cannot fit.

Current desktop target: **Syntakt OS 1.42 / core 2.3**. Rebuild this mod in a
new bank; older hardware measurements below refer to the previous OS. See
[the port notes](../../docs/OS142-PORT.md).

Optional SMZ UI for the core-owned project sample pool and HZFS library.
Uses host API 11's pool pointer and requires pool API 2. No firmware sites,
patch installer, storage implementation, audio renderer or alternate pool.

This module owns native views, menu rows, navigation, selection and progress
painting. It uses existing global KEY and TICK events. The existing core
Sound-menu hook calls its registered menu builder. The core retains HZFS,
CRC, block IO, pool references, AUDIO sync/rollback, preview streaming and
mixing. Menu/progress callbacks bind through host->pool and are cleared by the
core on bank initialization/failure. Nothing survives a failed bank bind.

YES enters [folders]; the `..` row returns to the parent. Normal sample rows
keep their selection behavior; FUNC+YES previews through the core service.
FUNC+UP/DOWN moves eight rows; after a FUNC+YES audition of a sample it moves
one row while FUNC stays held (hold FUNC, then DOWN, YES, DOWN, YES… to
audition neighbours). Pressing or releasing FUNC, or any key pressed without
FUNC, restores the eight-row step.
Selected samples from the displayed folder can be added to positions 65–128.
The project pool remains flat. The 64 stock samples remain protected.

Without this module, samples/pool playback and storage still work; the Sample
Manager menu is absent. Add `--module mods/sample-manager-st` when building a
complete bank. Rebuild banks that include an older manager against core 2.3.

Tests: native production UI + core preview fixtures in test_st_manager.c,
linked initializer/core binding in test_st_sample_manager_module_emu.py. The
latter rejects host 10 / pool API 1 and verifies provider cleanup/rebind.
`test_st_manager_draw_emu.py` executes the compiled manager with native OS 1.42
fonts, list/selection and bitmap routines, and can capture pool/library PNGs.
No hardware access.

Core 1.8: a stereo sample's row (HZFS flag `ST_HZ_STEREO`) reads `ST ` before
its name, in the eMMC and POOL views.

1.3.1 (core 2.8): a canonical wavetable's row (HZFS flag `ST_HZ_WAVETABLE`,
wavetable.h) reads `WT ` before its name, in the same views.
