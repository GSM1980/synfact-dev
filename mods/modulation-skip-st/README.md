# Skip the stock modulation when nothing changed

The MAIN audio IRQ runs the stock modulation `40097afc` (the velocity and
source modulation matrix) in every block: about 6,600 instructions, 5.7 CPU
points measured on the device. Between trigs and edits it
writes exactly what it wrote in the previous block. This module writes those
values itself in such blocks and returns, and runs the stock routine otherwise.
Study and design: [docs/SYNTAKT-CONVERSION-SKIP.md](../../docs/SYNTAKT-CONVERSION-SKIP.md).

## What decides a skip

The stock body writes, for each track `t`, `working + 0x1c + 0x8e·t + 2·index`
for 4 entries of 12 velocity groups and 13 × 5 source groups. Each entry is an
`{amount, index}` longword of the track's unsmoothed machine data
(`working + 0x754 + 0x7c·t`, 6 groups of 16 bytes). Its output depends only on:

- the velocity and source tables: `80004670..88`, `800046b8..ec`, `800046f8..80004760` (45 longwords);
- the machine data it reads: 13 × 24 longwords;
- the values of its destination words on entry (the conversion's output there).

The machine data is the conversion's copy of the unsmoothed mirror, which only
`400a30c4` writes (sound publication, machine change). That routine is hooked
too (its two prologue instructions move to the module): a counter goes up on
entry and on exit, and the 312 longs are compared only in blocks that saw a
change of the counter, and every 64 blocks anyway. A difference there with no
write counted means an unknown writer: ON turns OFF, VERIFY counts it (VERIFY
compares them in every block).

When the first two equal the last real run's and every destination word holds
its value from then, the module writes the run's final values to those words
(each once) and returns. Otherwise it runs the stock body and records the
destinations, their values on entry and their final values.

## Site and modes

`40097b04` is the stock body after its prologue. core-st's modulation adapter
jumps there when no module replaces the routine (POLY with groups uses its
own copy and is not affected). Its six bytes (two instructions) become
`jmp abs.l`; the stock path re-executes them. Installed once at init, nothing
if the bytes differ.

| Mode | What it does |
|---|---|
| ON (the module as built) | skips; every 64th matching block runs stock and compares; the first difference turns it OFF (stock for the rest of the session) |
| VERIFY (built with `MODULATION_SKIP_VERIFY=1`, diagnostics) | never skips; every matching block runs stock and compares, counting differences |

Header page after System info's: `MSKIP ON  H99 S98
E000` = mode (`ON `, `VER`, `OFF`, `NO1`/`NO2` refused at the modulation site /
the writer), % of blocks whose inputs matched, % skipped, differences. ON shows
it only after a difference, OFF or a refusal; VERIFY always.

## Checks

- `tests/test_st_modulation_skip.py --cc <clang>` (host logic: refusal, install,
  equality with a model of the stock body, reruns on each input change, the
  periodic check turning ON off, VERIFY never skipping, header), and with
  `--bank`/`--reference`/`--stock`/`--core` the banks' composition and the site.
- `tests/test_st_modulation_skip_emu.py --bank <bank>` (Unicorn, stock MAIN):
  the bank's real code in OFF, VERIFY and ON against the stock routine, with
  edits, publications, velocity/table/amount changes, machine-data entries and
  smoothed destination changes; SRAM and the LFO heap identical after every
  conversion, modulation and LFO call.
