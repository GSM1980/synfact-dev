# Synfact

Custom firmware and mods for the **Elektron Syntakt** (OS 1.41). You pick the
mods and supply the stock OS file Elektron publishes. The firmware is built on
your own machine, or in your browser, and you install it like any OS update.

Synfact is built on [elekloader](https://github.com/irpina/elekloader), a mod
loader for Elektron firmware: its engine, file format and checks are the
foundation here. This repository keeps only the Syntakt.

- **Nothing from Elektron is distributed.** A build starts from your own stock
  file, recognised by its hash. Mods carry their authors' bytes, the short
  instruction fragments needed to hook the OS, and hashes of the stock bytes
  they expect.
- **The bootloader is never touched.** Builds change MAIN and a bounded part
  of AUDIO only; bootstrap, updater and boot stub stay byte-identical, and
  each output is re-read and verified before delivery.
- **Mods are checked against each other before anything is built.** Two mods
  may not touch the same bytes, claim the same memory or the same named
  resource, or leave a requirement unmet.

## What it adds

The **core** ([mods/core-st](mods/core-st)) is the firmware: a hook bus, UI and
SETTINGS events, [optional SMZ4 banks](mods/core-st/SMZ4.md) of machines and
modules, [long-sample storage](mods/core-st/HZFS.md) and a
[project sample pool](mods/core-st/SAMPLE-POOL.md). The mods go in an SMZ4
bank that can change without reinstalling the firmware:

| mod | what |
|---|---|
| [SP sample machines](mods/samplers-st) | ONE SHOT, WERP, REPITCH and SLICE |
| [SY FM DTONE](mods/fm-tone-st) | an FM machine (FM TONE until 2026-11-02) |
| [Audio Recorder](mods/recorder-st) | record, trim, normalize and save samples |
| [Third LFO](mods/lfo3-st) | a third LFO on every track |
| [Arpeggiator](mods/arp-st) | a Digitone-style arpeggiator on the audio tracks |
| [POLY](mods/poly-st) | polyphonic play for digital and compatible analog tracks |
| [System info](mods/system-info-smz-st) | CPU and memory pages |
| [Sample Manager](mods/sample-manager-st) | the sample pool and library, in folders |

## Build it

First set up your machine: [docs/SETUP.md](docs/SETUP.md) lists every tool,
where to get it, how to install it and the environment variables it needs.
From source: [docs/SYNTAKT-BUILD.md](docs/SYNTAKT-BUILD.md) has the flags and
the exact commands for the core SYX and the SMZ4 bank. The same
build runs in a browser from validated, precompiled pieces
([docs/SYNFACT-WEB.md](docs/SYNFACT-WEB.md)).

Install the `.syx` like any OS update, then send the `.smz4` bank. If a bank
stops the unit from starting, a recovery core that never loads the bank lets
you replace it ([docs/SYNTAKT-BUILD.md](docs/SYNTAKT-BUILD.md), "The device").
The bootloader is never changed, so the stock OS file can be installed again.

## Write mods

[docs/MACHINES.md](docs/MACHINES.md) takes you from an empty folder to a
machine or module in the SMZ4 bank; [docs/ADAPTING.md](docs/ADAPTING.md) takes
you from the first build to done for any mod;
[examples/](examples) has a test machine and a test bank. Coding agents start
with [AGENTS.md](AGENTS.md). All the docs: [docs/README.md](docs/README.md).

## Licence

GPL-2.0-or-later; the TypeScript engine in `js/` is GPL-3.0-or-later. See
[LICENSE](LICENSE), and [NOTICE](NOTICE) for the code that comes from
elekloader and digikit and for the credits. Synfact is not affiliated with
Elektron. Installing custom firmware is at your own risk.
