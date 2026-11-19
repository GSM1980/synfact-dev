# Sample Manager (Syntakt)

Desktop OS 1.42 release: **1.2.1**, based on remote main `f48f3ab`.
Use core 2.2 or later and the matching bank (SMZ4 contract `0x53594606`).

Current desktop target: **Syntakt OS 1.42 / core 2.0**. Rebuild this mod in a
new bank; older hardware measurements below refer to the previous OS. See
[the port notes](../../docs/OS142-PORT.md).

Optional SMZ UI for the core-owned project sample pool and HZFS library.
Requires core 0.13.1 (host API 11), Syntakt OS 1.41. No firmware sites,
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
complete bank. Previous banks can load but do not supply this new menu.

Tests: native production UI + core preview fixtures in test_st_manager.c,
linked initializer/core binding in test_st_sample_manager_module_emu.py. The
latter rejects host 10 and verifies provider cleanup/rebind. No hardware access.

Core 1.8: a stereo sample's row (HZFS flag `ST_HZ_STEREO`) reads `ST ` before
its name, in the eMMC and POOL views.
