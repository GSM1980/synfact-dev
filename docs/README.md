# Synfact docs

Everything past the [front page](../README.md). The pages are plain Markdown
and change with the code, so a commit's docs match its code.

## Building and testing

- **[Consolidated MAIN optimization release](MAIN-OPTIMIZATION-RELEASE.md)**:
  released versions, development-label mapping and retained behavior.

- **[Setting up a workstation](SETUP.md)**: every tool, where to get it, how
  to install it, the environment variables, and the steps to a pull request.
  Start here.
- **[Syntakt builds](SYNTAKT-BUILD.md)**: the flags and the exact commands for
  the core SYX, the SMZ4 bank, the regressions and the emulator checks.
- **[Tests](TESTS.md)**: each suite and the files it needs.
- **[Unused-module MAIN work](IDLE-MAIN-PERFORMANCE.md)**: idle dispatch,
  stock transport, LFO3/ARP/stereo improvements and hardware comparison limits.
- **[Modulation Skip MAIN](MODULATION-SKIP-MAIN-PERFORMANCE.md)**: decision,
  saturation and scan optimization with exact stock/output/cadence comparisons.
- **[Core MAIN](CORE-MAIN-PERFORMANCE.md)**: native-service dispatch and mixer
  capture, complete-call counts, legacy profiling and cross-core checks.
- **[Parallel agents on this workstation](PARALLEL-AGENTS.md)**: one worktree
  per agent.
- **[Safe core/SMZ4 startup proposal](SAFE-BOOT-SMZ4-07-16.md)**: compatibility
  checks, trial installation and rollback for issues 07 and 16.

## Writing mods

- **[Machine reference](MACHINE-REFERENCE.md)**: the limits, the control and page
  mapping, the IDs, the menu and aliases, the audio voice and its calls.
- **[Writing a machine or a module](MACHINES.md)**: from an empty folder to
  a machine in the SMZ4 bank that is ready for a pull request. The descriptor,
  the sound contract, `mod.json`, the headers, and a checklist. Start here for
  a Syntakt machine.
- **[Adapting your mod](ADAPTING.md)**: the rules, the Syntakt core's events
  and tables, every command with the output it should print, every refusal
  with its fix, and a definition of done. Coding agents: start with
  [AGENTS.md](../AGENTS.md).
- **[Growing the core](CORE-CONTRIBUTING.md)**: when what your mod needs
  belongs in the core, and how to build it so it can move there.
- **[The file format](FORMAT.md)**: `.elemod`, whole builds (format 1) and
  linkable mods (format 2).
- **[The Syntakt's profile](DEVICES.md)**: what the engine knows about the
  Syntakt's firmware, and how a new OS version is added.
- **The core and the mods' own pages:** [mods/core-st](../mods/core-st)
  (the core: hook bus, SMZ4 banks, storage, sample pool), with
  [mods/core](../mods/core) (the two sources it shares with elekloader's
  core), [mods/machines-st](../mods/machines-st), and each mod's folder.
- **Examples** ([examples/](../examples)): `machine-test-st`, a test machine,
  and `smz-test-st`, a test SMZ4 bank.

## Versions and changes

- **Changelogs** of the Syntakt core and every Syntakt mod: `mods/<mod>/CHANGELOG.md`
  (for people) and `mods/<mod>/changelog.json` (for the website). Each version lists
  its commits. `changelog/index.json` gives the latest version of each mod.
  They are generated from git history by `python tools/changelog.py`. Run it after
  a commit that changes a mod, then commit the result.

## Building in the browser

- **[Synfact web architecture](SYNFACT-WEB.md)**: building the core SYX and
  the SMZ4 bank in a visitor's browser, and keeping the catalog of validated
  selections.
- **[The kit](INTEGRATING.md)**: the builder worker, its calls, and the tools
  that prepare the core package and the SMZ catalog.
- **[The engine in TypeScript](../js/README.md)**: the OS files, the mod
  checks, the linker and the build, with no dependencies and no Python. For
  the same stock file and mods it writes the same files as the Python, byte
  for byte. It is GPL-3.0-or-later.
