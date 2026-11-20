# Machine subtype selector

The bank-owned implementation in
[`subtype-picker.h`](../elekloader/sdk/include/syntakt/subtype-picker.h) adapts
the MAIN UI from the unfinished `feat/md-machine` work. It adds no resident
patch, firmware contract or AUDIO work. Digi Mono 0.5 uses it for its
twelve existing TYPE values; machine type 71 and all nine Sound controls keep
their IDs, ranges, defaults and codec slots. TYPE is on no SYN page.

## Source and scope

The source was found by inspecting all registered worktrees. The MD prototype
is the uncommitted `mods/md-st/main.c` and its picker tests on branch
`feat/md-machine`, based at `121e9655f4bd12117dc6752308bcd8fa75b7b88c`.
That commit alone does **not** contain this UI. The exact working-file SHA-256
values are in [`picker-provenance.json`](../mods/digimono-st/picker-provenance.json).
The files were read into a stable local snapshot; the original worktree was
not edited, staged, stashed or committed. No Machinedrum oscillator, tables,
decoder modifications or unfinished DSP work is imported.

The adapted list, global/view drawing split, YES/NO flow, deferred native edit,
30 Hz timing and original five-row glyphs retain the MD design. The helper
parameterizes machine/control IDs and names so MD can adopt the same source
when its branch is integrated. Digi Mono keeps SAW as its valid initial TYPE;
MD's unchosen/silent sentinel and fallback page list are not required here.

The imported code and this helper are GPL-2.0-or-later. Digi Mono's wrapper is
GPL-3.0-only, consistent with its combined clean-room generators.

## User flow

1. Double-tap SYN to open the native machine selector.
2. Highlight **SY MnM** and press **YES**. The native selector remains
   open underneath a list headed **CHOOSE A TYPE**.
3. Any encoder moves one item per event; UP/DOWN move one item and
   FUNC+UP/DOWN move seven. The list clamps at both ends.
4. **YES** confirms the machine natively, then the SYN page applies only the
   chosen hidden TYPE through `ui->edit`. **NO** returns to the machine list;
   its release is consumed, and another NO reaches the native selector.

Confirmation and the deferred edit refuse trig-held and live-recording states
to avoid writing a parameter lock. The pending choice belongs to its track and
model, expires after 60 ticks (two seconds), and covers a late write of native
machine defaults. Once applied, subsequent user input or a non-default TYPE
cancels the retry. The eight knob values and existing locks are not reset by
the subtype helper. Replacing a different machine still follows the core's
ordinary machine-default behavior before the TYPE edit.

SYN has one page: TUNE and up to seven parameters, with the existing per-type
empty slots. Reopening a saved Sound keeps its TYPE. An old view left on SYN 2
is clamped to the one current page. TYPE is excluded from LFO and Sound Setup
destinations. Its ID/storage remain stable, but legacy modulation/locks aimed
at TYPE no longer reach AUDIO: it is a configuration value, selected explicitly.
Core 2.3.2 also excludes configuration from the native SYN reload/randomize
list: FUNC+NO preserves TYPE while reloading synthesis values, and FUNC+YES
keeps its page randomization. Explicit selection and Sound/project loading
still load TYPE. Digi Mono 0.5.1 names the machine **SY MnM**.
Only controls used by the active subtype (and TUNE) appear in its destination
block. The common native destinations remain available. Audio continues to
latch an explicit TYPE change on the next trigger.

Arriving at a Digi Mono SYN page, changing tracks or loading a subtype shows
its name in a bordered box for 30 ticks (about one second). Its end requests
one redraw. Longer names such as SID-6581 fit without clipping. The list uses
the global DRAW layer so native selector views cannot paint over it; the name
box uses the machine's own SYN layer so later overlays cover it.

## Reuse and native boundaries

Include the implementation once in a machine's MAIN translation unit. Define
`STP_MACHINE`, `STP_CONTROL_ID`, `STP_CONTROL_INDEX`, `STP_COUNT`, `STP_NAMES`,
`STP_DEFAULT`, `STP_MODULE`, `STP_MODULE_NAME` and `STP_CONFIG_INIT(api)` first.
Use `stp_idle(view)` and `stp_draw(view, bitmap)` from the machine UI. The module
uses TICK, global KEY/ENC, view DRAW and global DRAW in the existing event bus;
it requires module service API 2 and UI API 7 with `edit`. Digi Mono's config
requires host API 17 for the non-modulated TYPE. Its existing
module symbol remains `dm_destination_module`, now named **MONO TYPE SELECTOR**.
There is no second registry, core event site or additional module slot.

Two optional definitions extend it without changing a machine that leaves them out:

- `STP_ENABLED(track, index)`: 0 for a subtype that cannot be chosen on that track now.
  The list draws its name struck through (a line across its middle row), the cursor passes it
  (encoders, UP/DOWN, FUNC+UP/DOWN stop on the next one that can be chosen, or stay), YES
  takes nothing, and the list opens on the stored subtype or the nearest one that can be
  chosen. MD KIT uses it for its CPU budget (`mods/md-st/main.c`).
- `STP_VIEW_ENCODER`: a function `(view, event, id)` added as an `ENC | VIEW` subscription,
  for a machine's own handling of its knobs (MD's knobs past a voice's last control).

MD KIT (type 70) uses the helper for its thirty voices since its branch merged this
one: `md_picker_module`, **MD TYPE SELECTOR**, six subscriptions. Its TYPE is a configuration
control as SY MnM's; an older Sound's out-of-range TYPE shows and plays as the first voice.

The separate user requirement that TYPE cannot be modulated needs core 2.3.1:
the generic alias marker `ST_MACHINE_CONFIG_PARAMETER` (`fffffffe`) identifies
an extended control at index 8..23 as saved configuration. Metadata returns
no destination flags; cached aliases use the native scalar editor. MAIN's
parameter sender uses the owning Sound mirror instead of the modulated working
row. Explicit edits, IDs, storage, codec and the AUDIO protocol stay unchanged.
Normal controls still use effective working values. No bank-specific machine
ID or hook is added to the core. API 17 adds no field and keeps API 15's idle
mixer ABI and API 16's destination filtering. Older cores refuse this bank's
descriptor before module initialization. The sender test covers every eligible
control, both packed word halves, POLY owners, old effective routes, stored edits and ordinary
modulation after clearing the marker.

The OS 1.42 boundaries come from MD's prototype and the existing sample/core
UI: value getter `40039160`, view machine `40030622`, track `4001ff74`, selector
predicate `400c3340`, held trigs `400c2dd6`, recording `400c3582`, and native
rectangle fill `400f7fb6`. SYN view classes/geometry match the current core:
tag `+88 = 14`, page vector `+7c` begins with 6, model `+6c`, track model `+74`.
Only recognized native parameter views are interpreted this way. Pointers are
borrowed from the currently drawn SYN/selector context; the helper neither
owns nor destroys native views. This remains a prototype UI pending physical
validation; the tests do not simulate a complete native view lifecycle.

## Validation

`test_st_digimono_picker.c` compiles the actual machine UI and helper with
native service doubles. It exercises twelve choices, scrolling limits, YES/NO
and release behavior, foreign machines, track/model changes, trig/recording
guards, late defaults, expiration, unchanged knob values and wrapping ticks.
The real core page test verifies the hidden TYPE and clamping of old SYN 2.

`test_st_digimono_picker_emu.py` executes the linked bank's initialization,
selector, edits, timing and glyph paths in ColdFire, with actual OS 1.42 Bitmap
construction/fill. It writes 128x64 PBM previews of twelve distinct name boxes
and the selector. Native UI/model/get/edit boundaries are explicit doubles;
native machine confirmation, undo, modal lifetimes and hardware timing are
not claimed as emulated. The separate destination-list test still executes
native enumeration/getter/sorting against the linked provider.
The linked MAIN sender also receives conflicting stored and effective TYPE
values on all eight tracks: effective changes must not publish a new type;
an explicit stored edit must reach the FPGA mailbox.

```sh
python tests/test_st_digimono.py --cc "$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe"
python tests/test_st_digimono_picker_emu.py "$BUILD" --out "$BUILD/picker-emu"
python tests/test_units.py
```

The bank composition checker validates the exact five UI subscriptions and
pinned picker source, with negative controls for unknown callbacks, counts,
titles and missing inputs. Firmware artifacts and PBM/PNG previews stay in
ignored `out/` directories. The four AUDIO objects are required to remain
byte-identical to the preceding bank; this change has no oscillator cost.
