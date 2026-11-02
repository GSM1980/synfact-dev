# Synfact docs

Everything past the [front page](../README.md). The pages are plain Markdown
and change with the code, so a commit's docs match its code.

## Building and testing

- **[Syntakt builds on this workstation](SYNTAKT-BUILD.md)**: the toolchain,
  the flags, and the exact commands for the core SYX, the SMZ4 bank, the
  regressions and the emulator checks.
- **[Tests](TESTS.md)**: each suite and the files it needs.
- **[Parallel agents on this workstation](PARALLEL-AGENTS.md)**: one worktree
  per agent.

## Writing mods

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
