[Synfact docs](README.md) › Machine reference

# Machine reference

The facts a machine or a module depends on, with the place each one comes from:
a header, the SMZ4 contract, the builder, the parser, or code in this repository
that uses it. [MACHINES.md](MACHINES.md) is the guide that uses them. Where a
fact is not recorded anywhere in the repository, this page says so.

## 1. Bank limits and descriptor fields

Checked by the core's parser (`mods/core-st/smz.c`) before anything runs.

| Field | Rule |
|---|---|
| machine type | 64 to 79. Each type once. At most 16 machines per bank |
| parameter count | 1 to 28 |
| `name`, `short_name`, `group` | printable ASCII, at most 15 characters |
| parameter short label | printable ASCII, at most 8 characters |
| parameter long label | printable ASCII, at most 63 characters |
| `min`, `max`, `default` | 16-bit. `min <= default <= max` and `max <= 0xffff` |
| parameter alias | at most `0x1f8` |
| `borrow` | must be 44 (the stock SYN presentation template) |
| `category` | 1 to 14 |
| `rank` | 0 to 127 |
| SMZ4 feature bits | `sample-pool-v1` and `machine-ui-v1`, the only names the builder accepts |

The builder (`elekloader/sdk/syntakt/smz.py`) checks structure and sources. It
does not check label lengths or alias ranges; the parser does.

Values are fixed point with 8 fraction bits: `0x0100` is 1.00, `0x7f00` is 127.00.

## 2. Control to Sound word

Control `i` of a machine is stored in a Sound word. The mapping is the same for
every machine (`mods/core-st/SMZ4.md`):

| Control index | Sound word (hex) |
|---|---|
| 0 to 7 | `12, 13, 14, 15, 16, 17, 1a, 19` |
| 8 to 23 | `36` to `45` (16 words) |
| 24 to 27 | `0, 11, 18, 46` |
| 28 to 31 | reserved. Declaring them is refused |

Controls 0 to 7 are stored in the native words that the stock machines use.
Controls 8 to 27 reach AUDIO through the extension transport (section 6).

### Control to SYN page

Without a UI descriptor, page `k` holds controls `8k` to `8k+7`, and the page
count is `ceil(params / 8)`. With a UI descriptor, each page's slots come from
the layout (`MACHINES.md`, section 6). A layout entry is a control index, and
`0xffffffff` is an empty slot.

## 3. Engine and UI IDs

- Engine IDs of type `t` are `0x1f9 + (t - 64) * 32`.
- UI IDs of type `t` are `0x2e0 + (t - 64) * 32`.
- The parameter providers for modules use `ST_MODULE_ID` (`0x3f9`) and
  `ST_MODULE_UI` (`0x4e0`), 128 of each (`machines.h`).

The numbers next to a range are not free because they look free. Check every
consumer of an ID before you assign it: arrays, comparisons, maps, the codec,
conversions and dispatch. A menu entry that shows a machine does not prove the
rest of the code accepts it.

## 4. Menu: category, rank, and the stock order

- **Category** (`category`, 1 to 14). The native registry reads it through its
  menu-category hook. Values used in this repository: **5** (the generic example
  and older banks), **10** (a Digitone-style FM machine), **11** (samples, which
  share it with the stock TwinShot machine, `mods/core-st/SMZ4.md`).
  **The names of categories 1 to 14 are not recorded in this repository.**
  Check the unit before you choose one.
- **Rank** (`rank`). The native menu builder sorts its entries by a rank that it
  asks for per machine type. The core answers that query from the descriptor
  (`elekloader/sdk/include/syntakt/host.h`, query 22). Stock machines keep a fixed
  order. The stock types in the special menu group, in that fixed order, are
  37, 5, 4, 36, 20, 38, 39, 40 and 44 (44 is TwinShot). In one menu test, a bank
  machine placed directly after TwinShot was given rank 9.

## 5. Aliases

The parameter alias is the native dial behaviour a control borrows. These are the
aliases in use:

| Alias | Dial | Use |
|---|---|---|
| `0x13` | NONE: a scalar, unipolar dial that works with parameter locks | most controls. Sample START, LEN, LOOP, PLAY and GRID use it |
| `0x1f1` | TUNE: the native TUNE dial, with its FUNC snap | TUNE controls. Do not put a start position on it: it reads -64 to +64 |
| `0x1f2` | SAMP: the project sample slot | sample slot controls. Randomize never draws it (`docs/ADAPTING.md`) |
| `0x1f7` | OVER: the stock OVER dial | overdrive controls that keep the stock behaviour |

Any other alias value is not listed in the repository.

## 6. Audio: the scheduler, the voice, and the calls

### Scheduler tables (stock)

- The scheduler is at `0x40004324`.
- Type to backend: table at `0x40014950`. Update entries: `0x40014920`. Render
  entries: `0x400148f0`.
- A new backend keeps the stock entries.

### Callbacks

- `update(pitch, voice, params)`. `pitch` is Q16, so `60 << 16` is C4. `params`
  is an array of the native Sound words.
- `render(out, voice)`. Writes 32 frames at 48 kHz.
- Both run on the second CPU, in the audio image. The voice is a native block
  of one track.

### The voice block

Each track has one native voice block. The stride is `0x708` (1800 bytes), so
track `t` is at `0x80000000 + t * 0x708`. The block cannot grow: every owner of
its bytes must be checked first.

| Offset | What it is | Notes |
|---|---|---|
| `+0x38` | trigger flag, set by the scheduler on a new note | the update reads it |
| `+0x56c` | a pointer that the stock voice reset reads | the reset zeroes 48 bytes at its target. Do not use this word |
| `+0x570` to `+0x708` | a window the stock voice reset rewrites | a machine can keep its own state here and reinstall it when its signature is missing |
| `+0x6b0` | the stock reader 1 state | |
| `+0x6d4` | the stock reader 2 base | a machine that does not use the stock reader can store its own PCM pointer and length here |
| `+0x6e8` | reader 2 active flag | zero means inactive. A machine that uses `+0x6d4` keeps it at zero |
| `+0x6f8` | a signature word | a machine writes its signature here so that it can detect its own state |

The meaning of the other bytes is not recorded in this repository.

### Routines the audio code calls

The repository does not name these routines. The addresses and the call order
are the reference.

- `0x400097dc`: the TwinShot update, the stock sample-playback update. It takes
  the note, the voice and the native words.
- `0x40009a8a`: the TwinShot render. It makes four calls in this order:
  `0x400068ea` (the buffer and the voice's `+0x6f8`), `0x40003d42` (the voice),
  `0x400045a0` (the buffer and the voice), and `0x400049f4` (two buffers, the
  output, the locals and the voice).
- `0x40006764`: the TwinShot PCM reader.
- `0x40003ee0`: the stock voice reset (section above, `+0x56c`).

### After the render: the track chain on CPU2

The machine's `render` writes the track's dry signal. The filters run after it,
in the AUDIO IRQ (`0x40000934`), not in the render. The IRQ calls, in this order:
the scheduler (`0x40004324`), then `0x4000112c`, then `0x40002320`, then
`0x400019d6` once per track, then `0x4000168e`. It then sends the eight
tracks' buffers (`0x80003840`, 128 bytes per track) to MAIN by DMA. MAIN mixes
them; it does not filter them.

- `0x40002320` is the base-width filter (BASE, WDTH). It reads the row words
  `0x33` (BASE) and `0x34` (WDTH) and takes two Q31 poles from the table at
  `0x40040060`: `T[BASE>>6]` for a one-pole high-pass and
  `T[(BASE+WDTH)>>6]` for a one-pole low-pass, which runs first. The poles
  change once per block, with no smoothing. The stock table is 508 poles and
  zeros up to index 1023. A zero pole leaves the low-pass open. The core
  bounds the low-pass index to 508 (`mods/core-st/audio-bw-filter.s`),
  because the AUDIO companion runs in that zero tail.
- What the other three routines do is not recorded in this repository. The
  multimode filter (FREQ, RESO, TYPE) has not been located.

### Units and limits

- Pitch Q16, parameters Q8, playback cursor Q12. Write the unit in every function.
- A large Q12 limit sets the sign bit. Compare cursors as unsigned when the limit
  is high.
- The longest sample tested was 524287 frames.
- The MCF54415 has EMAC and no FPU. Use integer and fixed-point code.

## 7. Pool, extended controls, and the UI

- `st_machine_sample(slot)` returns a borrowed `{frames, pcm}` for a project pool
  slot, or null. Check it on every update, because the sample can be replaced.
- `st_machine_extended(params)` returns the extended words for controls 8 to 27,
  or null. Null means the value has not arrived yet for this machine type. Keep
  your own default. Never read the null case as zero.
- The UI descriptor (`st_machine_ui`) needs `machine-ui-v1`. If any machine in a
  bank has it, every machine in that bank needs one. The builder reports
  `machine-ui-v1 requires a UI descriptor for every machine`.
- Callbacks that read a field added after UI API version 1 check `api->version`
  first (`machines.h`).

## 8. Modules: events

The event IDs and signatures are in `elekloader/sdk/include/syntakt/modules.h`:
tick, draw, key, encoder, settings, render in and out, file routing and writes,
MAIN mix, the mixer FX events, and the native transaction collection (event 11,
`native.h`). A subscription can also name a view (`ST_EV_VIEW`) or request the
pre-native KEY phase (`ST_EV_PRE`).

## 9. Not recorded in this repository

- The names of the menu categories 1 to 14, other than 5, 10 and 11.
- The names of the stock routines the audio code calls. Only addresses and the
  call order are recorded.
- The meaning of the voice bytes not listed in section 6.
- A test that runs AUDIO callbacks from public inputs only. Running them needs
  the stock state set up first. Section 6 says what such a test has to check.
