# Preserve closes requested during UI composition (core 1.8.2)

OS 1.41's deferred view cleanup (`40100ea4`) walks its list and removes closed
views under the native manager mutex. After removal it recomputes visibility
(`40100ade`), which synchronously composes the UI (`4000a0e6`) and executes
the core tick. A tick may close another view which the cleanup walk already
visited. Native close marks its byte at `+30h` and sets manager `+22h`.

The original cleanup then clears `+22h` **after** recomposition, erasing that
new request. The closed view remains attached/in the list. Future key walks
skip it, but only run cleanup when `+22h` is set. In the reported ARP sequence
this obsolete stack tail prevents ARP from opening again.

The 18-byte site at `40100f2c..40100f3e` now clears the consumed request before
calling visibility. The existing conditional call, mutex, list traversal,
shared owners, scratch registers and epilogue remain. A close raised during
composition survives return and is processed by the next native key walk.

```text
before: if (removed) recompute_visibility(manager); manager.pending = 0;
after:  manager.pending = 0; if (removed) recompute_visibility(manager);
```

The inline site in `mod.json` pins the complete stock instruction window.
Its branch/PC-relative call still target the original epilogue/visibility
routine. Both paths execute the same instructions/count; there is no extra
hook, resident trampoline, RAM, API change or audio/MAIN render work. The
resident reserve stays at 65,516/65,536 bytes, with 20 bytes free.

`tests/test_st_view_cleanup_emu.py` runs the full original close/cleanup path,
not the earlier direct shared-pair removal shortcut. It reproduces the old
core's lost request, closed attached ARP node and absent screen/LEDs; explicitly
requesting another cleanup recovers that old core. The same bank with 1.8.2
preserves the request and paints ARP blue after ordinary TRIG navigation.
The test covers digital/analog tracks, POLY, retained old owners and the
control where mute is removed while still on ARP. It enforces identical
complete-cleanup instruction counts in the reference case.

The native UI composition/constructor/LED boundaries are documented in
`test_st_arp_reentrant_view_emu.py`. No device IO or physical CPU claim is made.
The complete bank remains byte-identical to the ARP 1.10.4 delivery; installing
the **core 1.8.2 SYX** is required for this correction.
