[Synfact docs](README.md) › Growing the core

# Growing the core

Every event, table and service the Syntakt's core (`mods/core-st`) has came
from a mod that needed it. This page is for mod authors, and for the coding
agents they work with, whose mod needs something the core does not offer yet:
how to tell whether that need belongs in the core, how to build it so that it
can move there, and how to move it.

A mod that hooks a site itself owns those bytes, and no other mod can hook
them (rule 5 in [ADAPTING.md](ADAPTING.md#2-the-rules)). The same site as a
core event serves every mod.

## 1. Look first

Before you hook a stock site, look for what the core already has:

- the events and tables: `collections` in
  [mods/core-st/mod.json](../mods/core-st/mod.json);
- each event's handler, its arguments and what its return means: the
  comment over the code that calls it, in the core's sources;
- the services and C names for machines and modules: the SDK's headers,
  `elekloader/sdk/include/syntakt/*.h`;
- the "Syntakt core" sections of [ADAPTING.md](ADAPTING.md) and the core's
  [README](../mods/core-st/README.md).

If one of them fits, subscribe to it, or import it. You are done here.

## 2. Is it a core thing?

When nothing fits, ask of each site, address or table your mod needs:
**would a different mod plausibly want the same thing?**

| your mod needs | it belongs | as |
|---|---|---|
| to see or change what the UI, the keys, the knobs, the LEDs, the menus or the screens do | in the core | an event |
| to see or change the audio, the voices, the sound parameters or the MIDI the unit gets | in the core | an event |
| to see or change what the sequencer fires, or the project the unit loads and saves | in the core | an event or a table |
| a firmware routine or variable, by its address | in the core | a service or an export |
| a list that other mods add entries to (machines, menu entries, pages) | in the core | a table |
| your feature itself: its sound, its pages, its state | in your mod | |
| a constant only your feature changes | in your mod | a `bytes` site |

An event's handler returns nonzero to take what it saw (a key, a CC), so a
mod that replaces stock behaviour works as an event too. When the answer is
unclear, treat it as a core thing: it costs a few lines more now, and nothing
later.

## 3. Build it in core's shape

Build your mod in two parts, so that one of them can later move into the core
as it is:

- **the core part**: the site, the call to each handler, and the table (or
  the firmware address), in a source file of its own (`core_candidate.s` or
  `.c`) with the site and the table in your `mod.json`;
- **your mod**: everything else. It subscribes to the core part's event as it
  will to the core's.

The core part follows the core's conventions: an event's table is
`ev_<what>`, the code the site calls `core_<what>`; the handler's C signature
and the context it runs in (a task, or interrupt level) are written over the
code; with no subscriber the unit behaves exactly as stock; a hot site (the
render, the voices) keeps its empty path a few instructions, and says what
was measured; the core part saves what it uses and gives the stock code back
the registers it expects.

## 4. Move it into the core

1. Put its code in a new file in `mods/core-st/`, last in `sources`, so the
   code before it does not move.
2. Move its site into the core's `sites`, its table into `collections`, and
   its C names into the SDK's `syntakt/` headers.
3. Raise the core's version in `mods/core-st/mod.json`.
4. Keep everything the core had: the same sites with the same bytes, the same
   tables and exports, every symbol where it was.
5. Add a section to [ADAPTING.md](ADAPTING.md) headed with the core version
   ("Syntakt core 0.10: machine UI and optional modules" is one).
6. Build the core SYX and the complete bank, and run the regressions and the
   emulator checks ([SYNTAKT-BUILD.md](SYNTAKT-BUILD.md)). Say what ran.

Then your mod drops its core part and subscribes to the same table by the
same name.

**For coding agents:** when the mod you are building has a core part, tell
the user, and offer to move it into the core.

## Which sites mods hook themselves

`lint` lists each code site a mod hooks itself under `core_candidates`.
`elekloader.sdk.coresites` lists the stock code more than one mod of a set
hooks for itself; `elekloader.sdk.bounties` lists every piece of stock code a
mod still hooks, one entry per stock routine. Each is a place where one core
event could serve those mods.

```bash
python -m elekloader.sdk.coresites <folder of .elemod files> --stock Syntakt_OS1.41.syx [--json]
python -m elekloader.sdk.bounties <folder of .elemod files> --stock Syntakt_OS1.41.syx [--json | --markdown]
```

## Checklist

- [ ] Nothing in the core already does it (section 1).
- [ ] The core part is in a file of its own, under the core's names, with its
      handler written down.
- [ ] With no subscriber, the unit behaves as stock.
- [ ] The core keeps everything it had.
- [ ] ADAPTING.md has its section, and the regressions ran.
