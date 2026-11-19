# Same-layout idle diagnostic

Diagnostic only, excluded from normal banks and the web catalog. Build via
`tools/build_st_idle_bisect.py --out out/<fresh directory>` using the normal
SGDK environment and flags. The tool builds a full bank from local sources
once, then changes only module directory initializer pointers and registered
callback pointers to the C-ABI no-op/initializer authored here. The existing
checksums are regenerated; the unmodified core parser must accept both files.
There is no new firmware hook, core, SRAM allocation or periodic profiler.

`R-completo.smz4` retains all behavior. `S-catalogo-sin-callbacks.smz4` keeps
only System Info/Modulation Skip initialization and callbacks; other entries
decline through a no-op and register zero native interest. Both retain exactly
the same machine catalog, AUDIO, executable bytes, code/data image offsets,
relocations, file size, module directory/list lengths and version directory.
The code is still present in S but its optional features are disabled. It is
for a stock pattern, not for checking the operation of those features.

This controls the code-layout/AUDIO differences in an ordinary rebuild with
modules removed. Runtime initialization/allocation and cache behavior may
still differ. Empty callback calls remain, so S measures the catalog/framework
and those list entries together; it is not identical to the minimal bank A.
The common diagnostic initializer reserves one native-interest slot in both
banks; active LFO3/ARP/POLY occupy the other three. A missing zero-interest
registration would turn on the native dispatcher's fail-open path: the linked
negative-control test must detect it.

Use the same core 1.8.1.2, stock pattern, tempo and meter screen, restart after
each bank and let startup readings settle. Compare R to the preceding complete
bank first, then S to R and the owner's minimal A reference. Do not infer a
specific callback's CPU contribution from this aggregate comparison.

The owner measured R=86%, S=82% (minimal A=81%). To split the remaining
service work without moving that tested image, run
`tools/split_st_idle_bisect.py --reference-build <R build directory> --out
out/<fresh directory>`. It verifies the original R/source/object hashes
and uses its ELF and diagnostic entry points; it does not rebuild code.

- T disables only LFO3/ARP; the other services, including POLY, remain on.
- U keeps LFO3/ARP and disables the other optional services, including POLY,
  recorder, Sample Manager and the stereo callbacks.
- Both retain System Info, Modulation Skip and the diagnostic initializer.

T/U retain R's exact executable bytes, image offsets, file size, relocations,
catalog/AUDIO and directory/list lengths. Restart and measure the same stock
pattern. Compare each to S=82%; R=86% remains the full reference. Differences
need not add linearly because the services and memory behavior can interact.
These are diagnostic variants, not new performance releases.

After the owner measured T=84%, U=85%, `--set lfo` produces:

- V: only LFO3 services plus System Info/Modulation Skip. ARP is disabled.
- W: V with only LFO3's native subscription routed to the no-op. Its real
  initializer and global hooks remain installed, along with SRAM, shared
  locks, parameter provider and tick/draw. Native LFO/page sync/navigation/
  layout dispatch is disabled by zero native interest.

Compare V to U to isolate ARP with its shared LFO3 backend retained; compare
W to V to isolate LFO3 native callback/route work (including its per-block
engine), and W to S for the remaining installed hooks/services. W is only
for the stock-pattern CPU test; it does not provide normal LFO3 operation.
All variants retain the same code positions and AUDIO as R.
