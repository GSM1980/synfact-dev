# Sample Manager (Syntakt)

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
