# Syntakt machine companion

This ordinary SDK module declares the MAIN capability used by core-st's optional
machine loader. It contains no machine implementation. Its matching AUDIO
services are source-built by `python -m elekloader.sdk.syntakt`.

Use that explicit MAIN/AUDIO builder; the MAIN-only patch CLI cannot install the
CPU2 service image. See [core-st](../core-st/README.md) and [SMZ4](../core-st/SMZ4.md).
No valid bank is required for ordinary mods to run.


From core 0.6 the explicit builder also supplies the core-owned sample-service
image compiled from `mods/core-st/audio-pool.c` as an additional source in this
capability module. Its 4 KiB count toward the same 64 KiB reserve. It is not
machine code and does not come from SMZ4. All editable sources stay in the core.
