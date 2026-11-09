[Synfact docs](README.md) › Writing a machine or a module

# Writing a machine or a module

This page takes you from an empty folder to a machine in an SMZ4 bank that builds,
passes the parser, and is ready for a pull request. It needs only this repository
and the toolchain in [SETUP.md](SETUP.md). The facts you rely on are in
[MACHINE-REFERENCE.md](MACHINE-REFERENCE.md). Where that page says a fact is not
recorded, do not guess it: ask, or leave it out.

Nothing here touches a unit. Builds and checks run on your machine. Installing
the bank on an instrument is the owner's step.

## 1. Which kind of mod

| You want | Write | Where it runs | Start from |
|---|---|---|---|
| A new sound with its own controls and pages | a **machine** | SMZ4 bank, type 64 to 79 | `examples/machine-test-st` |
| Pages, key or encoder behaviour, a helper, no sound | a **module** | SMZ4 bank, after the machines | `examples/smz-test-st` (section 7) |
| Something the core cannot offer yet (an event, a table, a firmware routine) | a **core change** first | `mods/core-st` | [CORE-CONTRIBUTING.md](CORE-CONTRIBUTING.md) |
| A byte or routine that changes firmware, not as a bank | an **ordinary mod** | `.elemod` in the core's 64 KiB reserve | [ADAPTING.md](ADAPTING.md), sections 0 to 2 |

This page covers the first two. The rules every mod follows (one owner per byte,
no copied firmware, the hook bus) are in [ADAPTING.md](ADAPTING.md), section 2.
Read them before you write any code.

## 2. Before you start

1. Install the tools in [SETUP.md](SETUP.md). A bank needs **SGDK 2.11** for the
   compiler. The stock OS file is optional for a bank.
2. Check that the builder works with the example, unchanged:

   ```bash
   python -m elekloader.sdk.syntakt.smz examples/machine-test-st --out out/check/machine.smz4
   ```

   It writes `machine.smz4`, `machine.smz4.json` (the report) and a `.work/`
   folder, all in `out/` (ignored by Git). For a machine with its own SYN page
   layout, check `examples/ui-machine-test-st` the same way.
3. Read, in this order: this page; the machine types in [ADAPTING.md](ADAPTING.md),
   section 3; [SMZ4.md](../mods/core-st/SMZ4.md); [MACHINE-REFERENCE.md](MACHINE-REFERENCE.md);
   and the headers in `elekloader/sdk/include/syntakt/` (section 9 says what each one is for).
4. Find the closest existing example before you write sound code. For
   sample playback: `grep -rl st_machine_sample mods/`. For a machine that
   keeps per-track state: `grep -rl st_machine_extended mods/`.

## 3. Your first machine, step by step

Use a short folder name that ends in `-st`, for example `my-fm-st`. Its `id` in
`mod.json` is the same name.

**Step 1. Pick a machine type.** Types are 64 to 79, and a bank holds at most 16,
each type once. Type 64 is the test machine and is never released. Take the lowest
free type in the table in [ADAPTING.md](ADAPTING.md), section 3, and add your row
to that table in the same change. A released type never changes: a Sound saved
with a machine keeps its type.

**Step 2. Copy the example.**

```bash
cp -r examples/machine-test-st mods/my-fm-st
```

**Step 3. Edit `mods/my-fm-st/mod.json`.** The fields are in section 8. Change at
least `id`, `version`, `title`, `description`, `author` and `license`, and point
`smz.descriptor` and the `audio` entries at your own symbols. The example uses the
single-machine form of `smz`. Switch to the `machines` list form when you add a UI
(section 6).

**Step 4. Edit `mods/my-fm-st/main.c`.** Set the type in the first field of
`core_machine_def`, your names, and your controls (section 4). Every global symbol
is visible to every mod in the bank, so prefix yours with your machine's name
(`myfm_controls`, not `controls`).

**Warning:** the example's `on_load` checks its own type (`loaded == 64`). If you
change the type and leave that check, the builder still succeeds, but the
initializer returns -1 on the unit and the core rejects the whole bank. Change the
check to your type, or use `ST_MACHINE_NO_INIT` if you need no initializer.

**Step 5. Write the sound.** Replace `audio.s` with your AUDIO callbacks (section 5).
The example's callbacks are silent `rts` stubs. They are enough to test the
descriptor and the pages.

**Step 6. Build the bank.** Your folder is one more machine directory. The builder
takes the machine folders first, then the `--module` folders. For a test with the
complete bank, use the command in the "SMZ4 bank" section of
[SYNTAKT-BUILD.md](SYNTAKT-BUILD.md) and add your folder after the machines it
already lists. That is a build argument, not a registry, so no doc needs a change
for a test build. If the build stops with `machine-ui-v1 requires a UI descriptor
for every machine`, give your machine a UI (section 6).

The builder prints a JSON report. Check that its `machines` list contains your
type: for a machine of type 70 it reads `"machines": [70]`. The same file has
`firmware_required`, which must be false for a bank.

**Step 7. Run the parser check.** The core's own parser reads your bank. Build it
and run it on its own, with no firmware and no regression folder:

```bash
"$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe" -O2 -Wl,--large-address-aware \
    -I elekloader/sdk/include tests/test_st_smz_extensions.c mods/core-st/smz.c \
    -o out/check/smz-parser.exe
out/check/smz-parser.exe out/check/synfact-machines.smz4
```

It must print `PASS: …` and exit with 0. The full regression in
[SYNTAKT-BUILD.md](SYNTAKT-BUILD.md) runs the same parser as one of its steps.
We ran this sequence on a bank with a test machine of type 70 next to the complete
set of modules: it printed `PASS`.

**Step 8. Run the checks that always apply.**

```bash
python tests/test_units.py
```

`test_adapting_lists_every_machine_type_in_use` checks that your type is in the
table of section 3, so step 1 is not optional.

**Step 9. Open the pull request.** The checklist is in section 10.

## 4. The descriptor (`main.c`)

A machine is one `struct core_machine_def`, exported from MAIN. The bank's parser
checks each field before anything runs. A value outside its range rejects the whole
bank, and the core keeps the previous bank or the stock machines.

```c
#include <syntakt/machines.h>
#define P(n) ST_MACHINE_PARAM("P" #n, "PARAMETER " #n, 0, 0x7f00, 0x4000)
static const struct core_machine_param controls[] = { P(1), P(2), /* ... up to 28 */ };
const struct core_machine_def my_fm = {
    65, 28, "MY FM", "MYFM", "SYN", controls, ST_MACHINE_NO_INIT, 44, 10, 14
};
```

| Field | Meaning | Rule |
|---|---|---|
| `type` | the machine type | 64 to 79, each type once in the bank |
| `params` | number of controls | 1 to 28 |
| `name` | long name, shown in the machine list | printable ASCII, at most 15 characters |
| `short_name` | name on the SYN page header | printable ASCII, at most 15 characters |
| `group` | the group name | printable ASCII, at most 15 characters. Use `"SYN"` |
| `parameter` | the controls array | see below |
| `on_load` | initializer, or `ST_MACHINE_NO_INIT` | returns 0 on success. See below |
| `borrow` | the OS presentation it borrows | **must be 44** |
| `category` | the menu category it appears under | 1 to 14. Only 5, 10 and 11 are used in this repository. See [MACHINE-REFERENCE.md](MACHINE-REFERENCE.md), section 4 |
| `rank` | its place in the machine menu | 0 to 127 |

The full limits and the aliases are in [MACHINE-REFERENCE.md](MACHINE-REFERENCE.md),
sections 1 and 5.

**Controls.** Each control is `ST_MACHINE_PARAM(short, long, min, max, initial)`.
The macro sets the alias to `0x13`. Use another alias only for the dial behaviour
it names (section 5 of the reference).

**Which Sound word stores each control** is fixed for every machine (reference,
section 2). Your AUDIO code reads the controls from those words.

**`on_load`.** Use `ST_MACHINE_NO_INIT` unless you need private setup. A real
initializer has the signature `int on_load(const struct core_machine_def *)`. It may
set up the machine's private state and must return 0. Any other return rejects the
whole bank. An initializer must not hook anything or change firmware.

**Reserve IDs before you implement.** Engine and UI IDs follow from the type
(reference, section 3). Check every consumer of an ID before you use it.

## 5. The sound: AUDIO callbacks

The AUDIO code runs on the second CPU, in blocks of **32 frames at 48 kHz**. It is
compiled from the sources in `smz.audio.sources`, and the two callbacks are named
in the manifest:

```c
void my_update(unsigned int pitch, void *voice, const unsigned short *params);
void my_render(void *out, void *voice);
```

The ABI, the voice block, the routines the audio code calls, and the units are in
[MACHINE-REFERENCE.md](MACHINE-REFERENCE.md), section 6. Read that section before
you write any AUDIO code.

**Choose the approach first.** A machine either reuses a stock audio backend or
brings its own engine.

- *Reuse a stock backend:* you write the machine's registry, parameters and
  transport. Its DSP cost is the stock backend's. Use it when that backend already
  does what you need.
- *Own engine:* you write the registry, parameters, transport, voice state, reset
  and render. You must show the architecture, the ABI, the sample format and the
  resources it needs. A DSP from another device does not transfer by itself.

**Backend checklist.** Check each of these before you call the backend done:

- Give the machine its own state region, or reuse fields you have shown to be free.
  Reset them when the machine type changes.
- Test A to B to A, two different tracks, a Sound change, a trigger during a
  change, and output with no trigger.
- Leave no oscillator, flag or reader active from a previous machine.
- Do not patch the shared stock reader. A machine that needs a change keeps a
  private copy.
- Compare cursors as unsigned when a limit is large (reference, section 6).
- Check `st_machine_sample` on every update, because the sample can be replaced.

## 6. SYN pages and the UI descriptor

A machine's controls appear on SYN pages. The page structure has two cases.

**Without a UI descriptor** the core builds the pages from the control count.
Page `k` (0 to 3) holds controls `8k` to `8k+7`, in order. The page count is
`ceil(params / 8)`, so 28 controls give four pages of 8, 8, 8 and 4 controls. The
slots after the last control are empty.

**With a UI descriptor** you choose the pages. The descriptor is an
`st_machine_ui` (`elekloader/sdk/include/syntakt/machines.h`):

| Field | Meaning |
|---|---|
| `type` | the machine type. It must match the descriptor's type |
| `pages` | 1 to 4. Pages after this number are empty |
| `layout` | `pages * 8` control indices, read in order: page 0 is entries 0 to 7, page 1 is entries 8 to 15, and so on |
| `draw`, `idle`, `dial`, `format`, `policy` | optional callbacks. `ST_MACHINE_NO_CALLBACK` means absent |

Each layout entry is a control index from 0 to 27. Use `0xffffffff` for an empty
slot. A page may repeat a control. An index at or above the control count is
treated as empty.

The example `examples/ui-machine-test-st` shows the full case. Its layout reverses
the first eight controls on page 0, and page 3 holds controls 24 to 27 and four
empty slots:

```c
static const unsigned int ui_layout[32]={
    7,6,5,4,3,2,1,0,
    8,9,10,11,12,13,14,15,
    16,17,18,19,20,21,22,23,
    24,25,26,27,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu
};
const struct st_machine_ui ui_machine_test_ui={
    64,4,ui_layout,
    ST_MACHINE_NO_CALLBACK,ST_MACHINE_NO_CALLBACK,ST_MACHINE_NO_CALLBACK,
    ST_MACHINE_NO_CALLBACK,ST_MACHINE_NO_CALLBACK
};
```

Attach the descriptor in the **`machines` list form** of `smz` (section 8):

```json
"smz": {
  "machines": [{"descriptor": "ui_machine_test_st", "ui": "ui_machine_test_ui",
                "update": "machine_test_update", "render": "machine_test_render"}],
  "features": ["machine-ui-v1"],
  "audio": {"sources": ["audio.s"]}
}
```

The single-machine form does not read `ui`, and it does not warn.

**The bank rule for UI.** The feature applies to the whole bank. If any module in
the bank requests `machine-ui-v1`, **every** machine in the bank needs a UI
descriptor. Otherwise the build stops with `machine-ui-v1 requires a UI descriptor
for every machine`. To find a complete example in the repository, run
`grep -rl st_machine_ui mods/`.

**Callbacks.** Each callback is a function pointer in the descriptor, and the
parser checks that it points inside the image:

| Callback | Signature | Where it runs |
|---|---|---|
| `draw` | `void f(const struct st_machine_ui_api *, const struct core_machine_def *, void *view, void *ctx, unsigned page)` | UI task. No IO, no waiting |
| `idle` | `int f(const struct st_machine_ui_api *, const struct core_machine_def *, void *view, unsigned page)` | UI task. Returns nonzero when a redraw is needed |
| `dial` | `int f(const struct st_machine_ui_api *, const struct core_machine_def *, unsigned index, const struct st_machine_dial *)` | UI task |
| `format` | `const char *f(const struct st_machine_ui_api *, const struct core_machine_def *, unsigned index, unsigned raw)` | UI task |
| `policy` | `unsigned f(unsigned index)` | returns an alias, or `0xffffffff` for the default |

The example uses no callbacks. The builder and the parser accept it. How the four
pages look on a unit has not been checked from this repository. Add callbacks when
a page needs more than the layout, and read the comments in `machines.h` first.

`features` in `mod.json` can request `machine-ui-v1` (needed for a UI descriptor)
and `sample-pool-v1` (needed to use `st_machine_sample`). Those are the only two
feature names the builder accepts.

## 7. Modules (no sound)

A module is a MAIN object. It subscribes to events and has no AUDIO code. The events
are in `elekloader/sdk/include/syntakt/modules.h` (reference, section 8). The
example is `examples/smz-test-st`. It draws a counter on the tick and draw events,
and nothing else. Its `mod.json` uses `smz_module` in place of `smz`:

```json
"smz_module": {"descriptor": "test_module"}
```

A module's manifest may not use the keys `sites`, `subscribe`, `contribute`,
`collections`, `fixed`, `storage`, `machines`, `dsp` or `weak`. The builder refuses
the module if it does (`elekloader/sdk/syntakt/smz.py`). Its initializer returns 0
and sets up private state only.

## 8. `mod.json` for a bank mod

| Key | Meaning |
|---|---|
| `id` | the mod's name. Printable ASCII, 1 to 23 characters. It appears in System info |
| `version` | the version. Printable ASCII, 1 to 15 characters |
| `title`, `description`, `category`, `author`, `license` | for people. Give an SPDX identifier in `license` |
| `device` | `"syntakt"` |
| `os` | `"1.41"` |
| `sources` | the MAIN sources (C and assembly, relative to the folder) |
| `cflags`, `asflags` | optional extra flags. The example adds `"-fno-jump-tables"`. The profile's own flags are fixed (see the Flags section of [SYNTAKT-BUILD.md](SYNTAKT-BUILD.md)); do not change them here |
| `smz` | the bank description (below), for a machine or a module built into MAIN |
| `smz_module` | the module description (section 7), instead of `smz` |

Inside `smz`:

| Key | Meaning |
|---|---|
| `descriptor` | the symbol of the machine's `core_machine_def`. Single-machine form |
| `audio.sources`, `audio.update`, `audio.render` | the AUDIO sources and the two callbacks. Single-machine form |
| `machines` | a list of `{descriptor, ui, update, render}`, one per machine. Use it for several machines in one folder, and for **any machine with a UI**. The entries share `sources` and `audio.sources` |
| `modules` | module symbols in MAIN that go into the bank with this folder |
| `features` | `"sample-pool-v1"` and/or `"machine-ui-v1"` |

In the single-machine form, `descriptor`, `audio.update` and `audio.render` are read.
A `ui` key there is ignored without an error. Write the machine as a one-entry
`machines` list when it has a UI.

`donor` is for a machine whose bytes come from the owner's own Digitone OS file.
Do not use it for a machine of your own.

## 9. The headers

Everything below is in `elekloader/sdk/include/syntakt/`. Include the ones you use.
Each header says at its top what it is for.

| Header | What it is for |
|---|---|
| `machines.h` | machine descriptors, controls, UI descriptors, the sample service and the extended-value service. **Start here** |
| `modules.h` | module subscriptions, the event IDs, the module API |
| `native.h` | the native transaction collection (event 11) and its fixed operations |
| `host.h` | borrowed core services: storage, pool, preview, events. Storage and telemetry are task-only; the view event is UI |
| `storage.h` | HZFS storage (task context only). Serialize calls with transfers |
| `sample-pool.h` | the project sample pool service |
| `mix.h` | the shared mix bus on MAIN (inverted Q31, stereo, 32 frames) |
| `pattern.h` | borrowed pattern services for the UI task |
| `trig-pages.h` | pages a module adds to the native TRIG view |
| `sound-meta.h` | Sound metadata and note acceptance (host API 10) |
| `sram.h` | the MAIN SRAM windows that the stock boot leaves zero, and their owners |

## 10. Checklist before a pull request

- [ ] The type is the lowest free one, and its row is in the table of [ADAPTING.md](ADAPTING.md), section 3.
- [ ] Every symbol of your folder starts with your machine's name.
- [ ] `mod.json` has `id`, `version`, `title`, `description`, `author`, `license`, `device`, `os`, `sources`.
- [ ] The bank builds, and the report lists your type.
- [ ] If the bank requests `machine-ui-v1`, your machine has a `ui` in the `machines` list form (section 6).
- [ ] The parser check prints `PASS` and exits 0 (step 7), and the PR names the bank it ran on.
- [ ] Every area in section 11 that your machine touches has a test, or the PR says why not.
- [ ] `python tests/test_units.py` passes.
- [ ] Every value you could not check is named in the PR: a regression you could not run, a control you could not hear, a page you could not see on a unit.
- [ ] No `.syx`, `.smz4`, `.bin` or `.elemod` file is staged (`git status`).
- [ ] The PR says that nothing was installed on a unit. The owner installs it, with the recovery text the build prints.

## 11. Tests for a new machine

Each area below is a contract a machine can break. For each area, find a test of
the same kind in `tests/` and use it as the model. The search is
`ls tests | grep -i <word>`, with the word from the area.

| Area | What to exercise | Search for a model |
|---|---|---|
| Registry and menu | order, cursor, highlight, availability, fallback to stock | `syn_page`, `menu` |
| Model and refresh | defaults, Sound selection | `machines`, `parameters` |
| UI | long and short labels, values, icons | `ui`, `format`, `source_view` |
| Encoders | speed, inversion, FUNC, locks | `snap`, `fast_paths`, `machine_input` |
| Transport | raw value to frame to receiver | `objects`, `parameters` |
| Dispatch and audio | backend, state, edge cases | `sample`, `instruction_audit` |
| Live voices | window limits, cursor, empty sample, pitch, reads past the end of the PCM | none recorded: write one |
| Sequencer | locks compiled and restored, modulation destinations | `locks`, `play` |
| Save | Sound and Kit round trip, coexistence with the stock machines | `persist`, `new_project` |
| Package and CPU | instruction audit, boot guard, recovery | `instruction_audit`, `safe_storage`, `safe_boot` |

A search can find a test that covers only part of an area. Read it before you copy it.

## 12. What is still missing

What the repository does not record. This page does not guess these. Add each one
to [MACHINE-REFERENCE.md](MACHINE-REFERENCE.md) when it is written down, with the
code or the measurement that shows it.

1. **The names of the menu categories 1 to 14**, other than 5, 10 and 11.
2. **The names of the stock routines** the audio code calls. The addresses and the
   call order are in the reference.
3. **The meaning of the voice bytes** beyond the table in the reference.
4. **An AUDIO test that runs on public inputs only.** Running a machine's audio code
   needs the stock state set up first. The reference, section 6, says what such a
   test has to check.
