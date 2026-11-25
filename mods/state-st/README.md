# Core parameter storage (state-st 1.0.0)

Internal MAIN service for Sound/Kit metadata, native object lifetimes,
project codecs, +Drive Sound extensions and shared parameter-lock lanes.
The native SDK includes it once before any LFO3, ARP or Playhead consumer.
The browser catalog always includes it and does not offer it as a feature.
It requires core 2.9 / host API 20.

LFO3 2.0 is a separate feature initializer. Without that initializer the
service exposes no third LFO page or parameter metadata, installs no LFO
renderer into SRAM and subscribes to no LFO render operation. ARP retains
its existing locks and Sound persistence without selecting LFO3; Playhead
receives the same lifecycle notifications independently. The service keeps
the established `lfo3_*` backend symbols/source paths for lock/provenance
compatibility; these names no longer identify an optional feature dependency.

The core supplies full Kit-slot capacity, full-project mirror context and
the actual application-owned KitActive notifier. LFO3/Playhead keep their
existing inline Kit records. ARP keeps the existing APC1 carrier in the
associated pattern slot; individual live-Kit mirrors use their registered
full-project mirror and validate each native Sound binding before restore.
Short exports, foreign temporary Kits and unrecognized slots never authorize
out-of-bounds metadata access. No saved project layout or lock format changes.
