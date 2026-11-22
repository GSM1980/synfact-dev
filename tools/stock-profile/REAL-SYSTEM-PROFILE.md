# Profiling the real core + bank in analog-emu

The earlier MAIN CPU work (docs/STOCK-MAIN-CPU-PROFILE.md) profiled the **stock**
OS. This recipe profiles the **real synfact system** — our core SYX plus a full
SMZ4 bank, with every module installed and running — in analog-emu, so a
measurement reflects what actually ships, not stock alone.

It was built to answer whether a better I-cache lock set (one that covers the
mods' per-block code, not just stock) could recover more than the device's
1-2 points. The answer turned out to be no (the per-block hot code is the
stock audio block; the mods are not a cache-miss source, and the 8 KB I-cache
vs ~22 KB working set caps locking at ~16-19% coverage). But the capability is
reusable for any future MAIN/AUDIO CPU question, so it is kept here.

## Why it needs a patched emulator

analog-emu refuses an unknown MAIN/DSP hash (no symbol table). Our patched OS
has a new hash every build, but it keeps the stock 1.42 RTOS/idle at the
profile's addresses, so the 1.42 profile applies. `analog-emu-xprof.patch`
adds `DTEMU_ST142_MODIFIED=1`, which forces the 1.42 profile (MAIN and DSP)
for any image. The same patch adds the exact instruction/call-graph profiler
(XPROF), the I/D cache model with per-line miss histograms (XPROF_CACHE), a
half-cache simulation (XPROF_DHALF / XPROF_ILOCK / XPROF_IFREE), the exact
differential harness (XDIFF), a memory dump (DTBOOT_DUMP) and byte pokes
(--poke16 / --poke-file). It applies cleanly to analog-emu `4177ad4`.

```sh
git -C "$ANALOG_EMU" apply tools/stock-profile/analog-emu-xprof.patch
# build dtboot in a COPY (do not commit it into analog-emu); run its own toolchain
cargo build --release --bin dtboot
```

## Steps

1. **Build the core + full bank** (docs/SYNTAKT-BUILD.md) into `$BUILD`:
   the SYX and `synfact-machines.smz4`.

2. **Make a card with the bank in a GOOD slot** (no device install needed):
   ```sh
   "$ANALOG_EMU/target/release/dtcard" card.img format
   python tools/stock-profile/make_card.py "$BUILD/synfact-machines.smz4" card.img
   ```

3. **Boot, confirm the bank installed, snapshot a playing state.** Forcing the
   profile; a mod's install is visible as a patched stock site (e.g. mix-fast
   turns `4008f0b6` into a `jmp`, `4ef9....`), and `core_machine_status`
   (`verification.json`) reaches 0:
   ```sh
   DTEMU_ST142_MODIFIED=1 DTBOOT_PEEK=4008f0b6 \
     dtboot "$BUILD"/*.syx --card card.img --press PLAY@3500M \
            --instr 4500M --save play.snap
   ```

4. **Profile the audio block** from the snapshot (JIT off for the cache model):
   ```sh
   DTEMU_ST142_MODIFIED=1 DTEMU_NO_JIT=1 XPROF_CACHE=1 XPROF_OUT=prof.txt \
     dtboot "$BUILD"/*.syx --card card.img --load play.snap \
            --instr 4800M --profile-after 4500M
   python tools/stock-profile/report.py rank prof.txt 40
   ```
   `prof.txt` also carries the per-line I/D miss histograms (`iline`/`dline`)
   for lock-set work, and the `self`/`incl` call-graph.

## Notes and limits

- A fresh project is near-idle: the stock audio block dominates and the mods'
  code shows ~0 cache misses (confirmed empty and with injected `--midi`
  notes). For an active-mod profile, program a pattern (trigs, an LFO, POLY)
  with `--press`/`--midi`; the stock mixer/reverb/delay cost is the same
  either way.
- The MAIN parameter smoother's arrays live in SRAM (uncached), and the smoother
  runs with `MACSR=0x28`; its one-pole has a history-dependent dead band, so
  "converged" is not a single comparable value. Both facts matter for anyone
  revisiting a smoother skip.
- `DTBOOT_DUMP=HEXADDR,LEN,FILE` (comma-separated; MSYS mangles colons) dumps a
  memory range at the end of a run — used here to measure smoother convergence.
- These are emulator estimates, never device CPU percentages. The device A/B
  (same core, banks differing by one module) remains the only real measurement.
