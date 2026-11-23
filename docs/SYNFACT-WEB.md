# Synfact web architecture

`synfact-dev` holds the firmware SDK, browser engine and release tools.
`synfact-site` holds the page, Transfer integration and pinned release metadata.
Cloudflare Pages receives a production static build by direct upload, after
local review. The public production domain is synfact.app.

## Visitor flow

1. Select any permitted modules and supply the official Syntakt OS 1.42 file.
2. Supply Digitone OS 1.44 for SY FM DTONE/Chorus, Digitakt OS 1.54 for COMP,
   and Monomachine OS 1.32B for SY MnM, only when selected. Shared requirements
   use one input. Incorrect files disable generation until replaced.
3. The same-origin worker restores blanked objects, links selected MAIN/AUDIO,
   builds the SMZ4 and validates it with the actual pinned C core parser as wasm.
   Directed golden selections also require their exact output hash. The core
   SYX always matches its independently verified reference hashes.
4. Download the files or open Transfer. Instrument installation and Web MIDI
   permission belong to the user; no instrument is accessed by release tooling.

OS bytes stay in the tab. Target C/assembly compilation happens on the build
machine using the documented compiler; the browser links precompiled audited
objects. The parser only reads the generated bank. It establishes structure,
not musical behavior or CPU capacity.

## Release validation

The normal config has thirteen optional groups and three always-included
optimizations. ARP requires LFO3. The owner chose free selection with schema 4
input pins, explicit rules and parser validation on every browser build.
The compact native comparison suite covers all singles/pairs, the full bank,
full-minus-one cases and selected interactions: 109 distinct banks for this
configuration, measured at 62.39 seconds with audited objects prepared. It
does not exhaust all 6,144 selections. The legacy exhaustive policy is still
available explicitly; old catalogs keep mandatory output approval pins.

Run the module/core regressions appropriate to the change as well as donor
reconstruction, damaged-parser-input, engine, site and browser tests. Review
changelogs and the actual production build locally before deployment.
The commands and worker API are in [INTEGRATING.md](INTEGRATING.md), and the
target compiler/build procedure in [SYNTAKT-BUILD.md](SYNTAKT-BUILD.md).

Serve only blanked donor objects, never stock OS files, extracted firmware,
full AUDIO images or generated SYX/SMZ4 files. Keep generated artifacts ignored.
Publish corresponding source and license notices for the engine, mods, parser
and Monomachine reconstruction code with their object code. Passing tests does
not replace provenance review. See [WEB-REVIEW.md](WEB-REVIEW.md) for this update.
