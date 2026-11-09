# Silent machine bank test with a UI descriptor

This source-only example declares type 64 with 28 generic controls and its own
four-page SYN layout. Its AUDIO callbacks are silent. It exists to check the UI
descriptor, not a sound. Type 64 is for this test only: a new machine takes a free
type from 70 to 79 (ADAPTING.md, section 3).

```sh
python -m elekloader.sdk.syntakt.smz examples/ui-machine-test-st --out out/check/ui.smz4
```

The page rules and the callback signatures are in [docs/MACHINES.md](../../docs/MACHINES.md),
section 6. Copy this folder to start a machine with a layout of your own.
