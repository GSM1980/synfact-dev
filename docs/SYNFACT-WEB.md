# Planned Synfact web architecture

The planned site is a separate private project. `synfact-dev` holds the source of the firmware SDK and browser build engine. The site would serve static HTML, CSS, JavaScript, a pinned core package and a pinned catalog of audited SMZ objects, and integrate the JavaScript edition of Synfact Transfer. Cloudflare Pages can deploy it from a private repository and attach a custom domain. No site has been deployed as part of this work.

The source repository of the site and Transfer may remain private. Browser code and artifacts served to visitors are nevertheless downloadable and inspectable by those visitors. Review third-party licenses, corresponding-source obligations, module provenance and redistribution rights before publishing. In particular, the local FM TONE source includes private firmware-derived material; passing a build test does not give permission to distribute it. Never deploy stock OS files, complete AUDIO images, extracted firmware or patched firmware.

## Intended visitor flow

1. The visitor selects modules and supplies their own official Syntakt OS 1.41. The site checks the OS hash locally.
2. A same-origin worker builds MAIN and the AUDIO companion from pinned core artifacts, compresses, packages and verifies the `.syx` locally. The core output must match the pinned desktop reference. The stock file is not sent to a server.
3. A separate catalog provides the compiled, relocatable objects for the selected SMZ machines and modules. TypeScript links their MAIN and AUDIO sections and creates the `.smz4` locally. The worker returns only a selection whose output hash is in the pinned, independently verified catalog.
4. The visitor can download the files or use the separately maintained web edition of Synfact Transfer. It uses Web MIDI/SysEx, which requires HTTPS and device permission. Browser support and physical device behavior require their own checks.

Compilation of C and ColdFire assembly occurs on the development/build machine. Neither GCC nor the native linker runs in the visitor's browser. The `.smz4` contains machine code; it is linked from precompiled objects, not constructed by concatenating banks. Selection order is the catalog order, preserving the LFO3, ARP and POLY event order. `samplers-st` currently supplies four machines as one selectable group.

## Updating the catalog

`js/config/syntakt-normal.json` defines the current normal build's eight groups. Diagnostic, profiling and timing modules are excluded. `js/tools/smz_artifacts.py` exports objects from a freshly audited full SDK build, checks source and object hashes, and records their provenance. The catalog is initially unapproved: it cannot generate visitor downloads.

For every nonempty selection, `js/tools/smz_matrix.py` builds a fresh reference using the normal native compiler and linker; builds the TypeScript candidate; runs each through the core's independently compiled parser; checks MAIN relocation tables, the rest of MAIN and all AUDIO; and pins successful candidate hashes. Equal text at alternative merged-string addresses is accepted. Failures remain excluded and must be investigated. Requirements or conflicts can later be specified in the config, but the current normal matrix imposes none. The empty selection requires no SMZ file.

`js/tools/smz_release.py` runs the steps below for one release from a commit; see [INTEGRATING.md](INTEGRATING.md). After adding or changing a module, rebuild and re-audit the full object set, revise the config, rerun the matrix, run appropriate module and integration regressions, and publish only a deliberately reviewed catalog with a newly pinned hash. Matrix and parser checks do not prove musical behavior, runtime performance or safe recovery; emulator and hardware tests remain separate. Review and validate the matching core version and Transfer support before any release.

On 2026-10-31, the eight-group configuration was checked for all 255 nonempty selections. Each candidate and its native reference passed the core parser; relocation tables, MAIN and AUDIO were compared, allowing only equivalent merged-string targets. After the ARP persistence sentinel fix and version bump to 0.0.3, all 128 selections containing ARP were rebuilt and checked again; the other 127 were reused only after their artifact hashes matched. The complete core/bank native regression suite passed with no optional comparisons skipped, and the ARP persistence emulator test passed. Chrome worker smoke tests matched both the core SYX and a validated SMZ bank. No device was accessed. Firmware, compiled objects, reports and catalogs used for these checks are local and are not part of the source repository.

The complete commands and worker API are in [INTEGRATING.md](INTEGRATING.md). The workstation's exact target compiler, flags and native build procedure are in [SYNTAKT-BUILD.md](SYNTAKT-BUILD.md).
