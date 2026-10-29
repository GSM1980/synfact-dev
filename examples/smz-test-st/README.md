# Optional SMZ module example

This MAIN module subscribes to the existing tick and draw events and displays
a small `SMZ` tick counter. It defines no machine or firmware hook site. It
requires Syntakt core 0.8 and is not included in the default sampler bank.

```sh
python -m elekloader.sdk.syntakt.smz --module examples/smz-test-st --out out/module.smz4
python -m elekloader.sdk.syntakt.smz --preset samplers --module examples/smz-test-st --out out/mixed.smz4
```

Upload the chosen bank with `python -m elekloader.sdk.syntakt.mods put
out/mixed.smz4 --yes-write-device`, then reboot. `/mods` is a single file:
uploading the example alone replaces a previous machine bank. Source descriptors
use `syntakt/modules.h`; event signatures and bounds are specified in
[SMZ4.md](../../mods/core-st/SMZ4.md). Build outputs stay in ignored `out/`.
