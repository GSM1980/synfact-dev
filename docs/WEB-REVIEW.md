# Web update review

The release includes thirteen optional groups and three always-included internal
optimizations. `js/config/syntakt-normal.json` is the release config;
`syntakt-web-next.json` preserves the same order used for this local review.
MD kit uses id `md-st`; SY MnM uses `digimono-st`.

## Browser reconstruction and validation

Visitors supply Syntakt OS 1.42. A selection also asks for Digitone OS 1.44
(SY FM DTONE or Chorus), Digitakt OS 1.54 (COMP), and Monomachine OS 1.32B
(SY MnM), as needed. The worker recognizes each by hash, restores blanked
objects and links the bank locally. No OS file is uploaded. All nine
Monomachine table transformations match the independent pinned importer.

The owner chose a free selector with schema 4 catalogs: audited input hashes,
explicit dependencies/conflicts and the actual C core parser compiled to wasm.
Every generated bank must pass this parser before it is returned. Directed
golden outputs retain their additional exact-hash check. Legacy schemas 1–3
still require an independently approved hash for every production selection.

The first directed run passed 109 native/browser comparisons in 62.39 seconds
with audited compiled objects available. It covers every single optional group,
every pair with dependency closure, the full bank, full-minus-one cases and
four selected interactions. AUDIO stays byte-identical to the native linker;
MAIN differences are limited to proven equivalent string targets. This is
not an exhaustive check of all 6,144 selections. Parser checks establish
structure, not musical behavior, runtime CPU usage or device recovery.

Core 2.6.1 was built with the system-info meter and verified against stock
protected sections. Its existing native regressions passed; four optional
comparisons lacked their extra fixtures. The MD/SY MnM coexistence regression
scopes static symbols to their compilation unit. No instrument was accessed.

## Before publication

The site is built in production mode and served locally with the same selector,
worker and blanked artifacts intended for publication. The owner edits the
per-mod changelogs and reviews the local site and instrument behavior. Source
changes must be committed and their public source links updated before serving
the corresponding object code. Cloudflare deployment remains pending review.
