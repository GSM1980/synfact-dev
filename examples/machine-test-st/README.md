# Silent machine bank test

This source-only example declares type 64 with 28 generic controls. It exercises
four SYN pages and MAIN private initialization. Its AUDIO callbacks are silent;
it is a loader/UI example, not FM Tone or a finished sound engine.

```sh
python -m elekloader.sdk.syntakt.smz examples/machine-test-st --out out/machine-test.smz4
python -m elekloader.sdk.syntakt.mods_object pack out/machine-test.smz4 out/machine-test.obj
```

The bank belongs in `/mods` through native Data RPC, followed by reboot. These
commands are offline and do not contact an instrument. Machine code is excluded
from the resident firmware. See [the contract](../../mods/core-st/SMZ4.md).
