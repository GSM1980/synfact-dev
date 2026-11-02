# Contributing to Synfact

Contributions are welcome: fixes, mods, core additions and docs for the
Syntakt. A draft pull request that says what it could not finish is fine.

Good things to send:

- **A core addition**: an event, a table or a firmware export your mod
  needed and the core lacked
  ([docs/CORE-CONTRIBUTING.md](docs/CORE-CONTRIBUTING.md)).
- **A mod** for the Syntakt, following [docs/ADAPTING.md](docs/ADAPTING.md).
- **Fixes and docs**: a refusal message that misled you, a step that did
  not work, a page that was unclear.
- **Engine changes** (`elekloader/`, `js/`): the engine comes from
  [elekloader](https://github.com/irpina/elekloader), which supports every
  device. Propose engine changes there first.

Using a coding agent? Point it at [AGENTS.md](AGENTS.md).

Before you open one:

- run the tests you can (`python tests/test_units.py` always; the Syntakt's
  suites and regressions: [docs/TESTS.md](docs/TESTS.md)), and say in the
  pull request which ran;
- commit no firmware: no `.syx`, `.smz4`, `.bin` or built `.elemod` files,
  and nothing extracted from or derived from an Elektron OS;
- keep the licence: GPL-2.0-or-later for the Python and the mods,
  GPL-3.0-or-later for `js/`.
