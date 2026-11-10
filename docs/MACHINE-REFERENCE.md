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
- A track changes backend only when it is triggered. Each block, the scheduler
  copies the voice's `+0x34` (this block's trig) to `+0x38`. When `+0x38` is set
  and the Sound's type maps to another backend, it calls the voice reset
  `0x40003ee0` and installs the new backend in `+0x0` and `+0x4`. It then calls
  the update and render of whatever backend `+0x0` names. Until its first trig,
  a machine placed on a track is never called; the previous backend keeps
  running.

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
| `+0x6f8` | a signature word, or the render filter's state | a machine that does not call `0x400068ea` can write its signature here to detect its own state. On the stock sample path, `+0x6f8` and `+0x6fc` are that filter's two coefficients and `+0x700`, `+0x704` its memory (next section) |

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
  The four calls:

  | Routine | Arguments | What it does | State |
  |---|---|---|---|
  | `0x400068ea` | input, output (may be the same), a 4-word block | Two one-pole stages per sample, saturating; the output is the first stage minus the second | The block: coefficients at `+0`, `+4`, memory at `+8`, `+12`. TwinShot passes the voice's `+0x6f8` |
  | `0x40003d42` | the voice | Once a block, no audio: a declick stage machine on the voice's `+0x3d0` to `+0x3f0` (level `+0x3d0`, stage `+0x3dc`), driven by the trig words `+0x34`, `+0x38`, `+0x3c` | Control only |
  | `0x400045a0` | buffer, voice | Multiplies the buffer, in place, by a gain ramped over the block from `+0x3d4` (last block's) to `+0x3d0`, negated; then stores `+0x3d0` in `+0x3d4` | Control only |
  | `0x400049f4` | buffer A, buffer B, output, `{a, b}` indices, voice | Output = gain(a) × A + gain(b) × B, each gain ramped from the voice's `+0x18 + 4i` to `+0x8 + 4i`; then copies the targets to the ramp starts | Control only. The samplers write `+0x8` = `0x3fffffff`, `+0xc` = 0 (A only, unity) |

- `0x40006764`: the TwinShot PCM reader.
- `0x40003ee0`: the stock voice reset (section above, `+0x56c`).

### After the render: the track chain on CPU2

The machine's `render` writes the track's dry signal. The filters run after it,
in the AUDIO IRQ (`0x40000934`), not in the render. The IRQ calls, in this order:
the scheduler (`0x40004324`), then `0x4000112c`, then `0x40002320`, then
`0x400019d6` once per track, then `0x4000168e`. It then sends the eight
tracks' buffers (`0x80003840`, 128 bytes per track) to MAIN by DMA. MAIN mixes
them; it does not filter them. The transfer is in "The shared window between the two CPUs" below.

- `0x40002320` is the base-width filter (BASE, WDTH). It reads the row words
  `0x33` (BASE) and `0x34` (WDTH) and takes two Q31 poles from the table at
  `0x40040060`: `T[BASE>>6]` for a one-pole high-pass and
  `T[(BASE+WDTH)>>6]` for a one-pole low-pass, which runs first. The poles
  change once per block, with no smoothing. The stock table is 508 poles and
  zeros up to index 1023. A zero pole leaves the low-pass open. The core
  bounds the low-pass index to 508 (`mods/core-st/audio-bw-filter.s`),
  because the AUDIO companion runs in that zero tail.

Each routine works in place on the eight 128-byte buffers at `0x80003840`
and reads the 142-byte parameter rows at `0x80003c40` (one per track). The
names below are this repository's; the stock has none. Addresses are AUDIO
SRAM. "Signal" state is the filter memory that depends on the audio; "control"
state is the smoothed coefficients, gains and envelope, which depend only on
the parameters and the trigs.

| Routine | Arguments | What it does | State per track `t` |
|---|---|---|---|
| `0x4000112c` drive | buffers, rows | Tracks in pairs. A gain from row word `0x19` (squared, doubled), ramped over the block, a memoryless polynomial clip, then a crossfade with the dry signal set by row word `0x28` through a table at `0x800081b0` | Control only: `0x800083b0` to `0x8000844f` (targets `0x800083f0`, current `0x80008430`, weights). Scratch `0x80008450` and `0x800084d0` |
| `0x40002320` base-width | buffers, rows, trig mask | Tracks in pairs. A one-pole low-pass then a one-pole high-pass (above) | Signal: `0x800088a0 + 8t` (two words). Cleared when the flag `0x80008550 + t` and row word `0x41` are set |
| `0x400019d6` multimode filter | row `+0x52`, buffer `0x80003840 + 128t`, `*(0x80008188 + 4t)`, `t`, trig mask, row word `0x41` | One track per call. The type at `0x80008560 + 4t` (0 to 7) picks the response; coefficients are computed each block and ramped over its 32 frames | Signal: `0x80008920 + 8t` (two words), `0x80008580 + 4t` (one word). Control: coefficients `0x80008740 + 32t` (eight words), mix weights `0x80008840 + 12t` (three words), `0x800088e0 + 4t`, the type, flags `0x80008550 + t` and `0x80008558 + t`. Scratch `0x80008720`, `0x80008900`, `0x80008960`, `0x800089e0` |
| `0x4000168e` AMP | buffers, rows, trig mask, retrig mask, a flag byte | For each track, calls `0x40001476` with the buffer, the envelope state, row `+0x6a` (word `0x35`), its trig bits and `t`. That routine runs a stage machine (stages 0 to 6, time tables `0x40013c70`, `0x40014070`) and multiplies the buffer by the ramped level (`0x40001428`) | Control only: `0x800049e0 + 12t` (stage, counter, level). No signal memory |

So the track's AMP is the last stage on AUDIO; level and pan are applied on
MAIN. The drive's clip and the saturating arithmetic of the filters are not
linear, so two signals through a stage do not sum like one. The multimode filter and the envelope run one track per call; the drive and the
base-width filter run on all eight tracks in pairs, from the buffer address
they are given.

### The shared window between the two CPUs

The two CPUs exchange each block through a memory on FlexBus chip select 0,
16 bits wide, with one wait state. MAIN sees it at `0x10000000` and AUDIO at
`0x00000000`. Both CPUs decode 128 KiB there; only `0x000` to `0xfff` is known
to hold memory. Whether anything above `0x1000` exists, or mirrors the first
4 KiB, has not been tested.

| Offset | Size | Use |
|---|---|---|
| `0x000` | 432 bytes | The native frame, MAIN to AUDIO: handshake words at `0x0`, `0x2`, `0x4`, `0x8` and `0xc`, then the parameters. MAIN writes it by DMA; AUDIO copies it to `0x80008000` with the CPU at the start of its IRQ. The core uses byte `0x1a9` |
| `0x1b0` | 1024 bytes | The eight digital tracks' audio, AUDIO to MAIN: 128 bytes (32 Q31 words) per track |
| `0x5b0` | 592 bytes | No access found in either stock image or in the core; AUDIO's boot clears only `0x000` to `0x5af`. On the unit (2026-11-10), `tools/probes/memory-probe-st` held a pattern in `0x5b0` to `0x7df` for several minutes of normal use with no word changed. Stereo tracks use `0x5b0` to `0x7ef` ("Stereo tracks" below) |
| `0x800` | 1024 bytes | Stock boot pages (2 KiB, `0x800` to `0xfff`). After boot the core uses it for the pool and System info mailbox |
| `0xc00` | 1024 bytes | After boot: the core's extended parameter banks (`0xc00`, `0xe00`) |

Both transfers use eDMA channel 47 (TCD at `0xfc0455e0`), started by software
(SSRT, `0xfc04401e`) each block. The TCD moves 16-byte bursts: source and
destination offsets 16, `NBYTES` 16, `SLAST` and `DLAST_SGA` zero. `CITER` and
`BITER` link the channel to itself (`0xde40`: 64 minor loops, so 1024 bytes;
`0xde1b`: 27 loops, 432 bytes). Software waits for `CSR.DONE` (bit 7 of
`0xfc0455fe`) before it reuses the channel.

- AUDIO, at the end of the IRQ (`0x40000ac6` to `0x40000afe`): `0x80003840` to
  window `0x1b0`, 1024 bytes. The flag at `0x4004f6e8` marks a transfer in
  flight; the next IRQ waits for DONE before the scheduler runs. The window
  base is the pointer at `0x4004f6e0`, which stays zero. AUDIO uses no other
  eDMA channel.
- MAIN, in its audio routine: window `0x1b0` to `0x80003d50`
  (`ST_TRACK_PCM`), 1024 bytes, at `0x400a4782` and `0x400a4a74`. Then
  `0x80002000` to window `0x000`, 432 bytes, at `0x400a4dc4`, after the wait at
  `0x400a2036`. While AUDIO is not ready (`0x43bdc824` zero), MAIN clears
  `0x80003d50` instead of reading. MAIN also uses eDMA channels 15, 29, 30, 34
  to 37, 42, 52, 54 and 59.
- What starts the AUDIO IRQ is DMA timer 0's event (`0xfc070003` is cleared at
  its entry). The source of that event, and the time between AUDIO's write and
  MAIN's read, are not recorded.

### AUDIO's SRAM

AUDIO's SRAM is `0x80000000` to `0x8000ffff`; its stack is in DDR (`SP`
`0x48000000`). The boot copies data to `0x80000000` to `0x80007f8f` and
`0x80008000` to `0x8000e89f` and zeroes the rest. FM TONE's scratch is
`0x8000e8a0` to `0x8000f397`, used only once a track has loaded FM TONE. The
tail `0x8000f398` to `0x8000ffff` (3176 bytes) has no known user: no stock
reference was found, and on the unit (2026-11-10) the memory probe found it
zero at its start and held a pattern there for several minutes of normal use
with no word changed. The stereo engine owns it now (next section).

### Stereo tracks

A track is mono up to MAIN's mixer, and MAIN mixes it with level × pan gains
(left gL, right gR). For a stereo track, L goes the native way and R is sent
as a difference:

1. The machine's render writes L into its output, as before, and R into
   `ST_STEREO_R(t)` (`st_stereo_claim(t)`, `<syntakt/stereo.h>`). It runs the
   render filter `0x400068ea` on R with L's coefficients and R's own memory,
   and the two gain ramps (`0x400045a0`, `0x400049f4`) from the start values
   L used.
2. At the chain end (core 1.8, `40000ac6`, after AMP and before the track
   DMA) the engine runs the native chain on R: drive and base-width on all
   eight R buffers, the multimode and the AMP envelope (`0x40001476`, per
   track) of the claimed tracks. Only the chain writes the chain's state
   (a scan of the AUDIO image: the chain routines and the boot), so the state
   from before the L pass is the one the previous chain end left. The engine
   saves the control state the L pass left, loads the one from before it,
   swaps in R's signal memory, runs the chain, then swaps L's memory back and
   reloads L's control state. Mono tracks and L are what the stock chain made.
3. It sends D = R − L through the shared window, `0x5b0` to `0x7df`: 32
   16-bit mantissas per track, one shift per track and block
   (D = mantissa << (shift + 1)), the track mask, a tag and the sequence at
   both ends. The block is built in AUDIO SRAM and copied to the window in
   ascending order. A DMA on AUDIO eDMA channel 46 was tried: its block never
   reached the window on the unit (the profile, written by the CPU, did), and
   sending cost 5.1 % of the block against 6 % word by word, so most of that
   cost is packing D, not the bus.
4. MAIN adds gR × D to every bus the right gain feeds: the delay input
   (`ev_delay_in`), the reverb input (`ev_reverb_in`) and the two main buses
   (`ev_main_mix`). Left stays gL × L and right becomes gR × R; pan acts as a
   balance.

Pairing D with its L. MAIN's audio interrupt reads L by DMA (`0x400a4a74`)
and mixes it later in the same interrupt (`0x400a4fce`), and AUDIO can finish
a block at any moment of it: on the unit, with D read at `ev_render_in` and
dropped whenever the sequence moved before the mix, about 20 blocks a second
were dropped, even in STOP. The right channel then switched between R and L
and sounded broken. So D carries a fingerprint of its block's L
(`st_stereo_print`, samples 0, 8, 16 and 24 of each track). MAIN reads D at
`ev_render_in`; at the first mix event it fingerprints the L it is mixing
(`ST_TRACK_PCM`) and takes the D that matches: the one already read, or, if
AUDIO wrote since, the new one; a read caught mid-write is retried. When
AUDIO has not finished a new block, MAIN mixes the same L again, and the same
D with it. The only block never used is the one a restart left in the window,
until the engine has sent one of this boot. The track is not meant to fall
back to mono: `DROP` on the test probe counts the blocks where no D matched.

Which tracks are stereo: the samplers claim a track while it plays a stereo
sample (core 1.8, `st_machine_sample_channels`; SAMPLE-POOL.md) and while the
previous block still sounds on L or R after the chain, so a filter's tail
stays stereo to its end. A mono sample costs nothing, a track whose D is zero
leaves the mask, and in STOP the R pass and MAIN's work cost nothing.

The gains are the stock kernels' own: each kernel ramps a track's gain over
the block from the value in its state array to the target and stores the end
value back. At the events the arrays hold the end values; MAIN keeps the
start. Right-channel state arrays (tracks 0 to 7 first): delay `0x80006308`,
reverb `0x800062c0`, main bus `0x8000e160` (bus `0x8000dad0`), direct bus
`0x8000e110` (bus `0x8000dfd0`).

| Address (AUDIO) | Size | Use |
|---|---|---|
| `0x8000f400` | 1024 bytes | R buffers, 8 × 32 Q31 |
| `0x8000f800` | 160 bytes | R's chain memory: multimode `0x80008580` (8 words) and `0x80008920` (16), base-width `0x800088a0` (16) |
| `0x8000f8a0`, `0x8000fb50` | 688 bytes each | the chain's control state, before and after the L pass: drive `0x800083b0` (40 words), flags and types `0x80008550` (12), multimode coefficients and mix weights `0x80008740` (88), `0x800088e0` (8), AMP `0x800049e0` (24). The one free after a chain end stages the block the DMA sends |
| `0x8000fe00` | 128 bytes | R's render-filter blocks, 4 words per track |
| `0x8000fe80` | 128 bytes | render scratch for R |

Window words: `0x5b0` sequence, `0x5b4` mask, `0x5b8` eight shifts, `0x5c0`
the fingerprint, `0x5c4` the tag `STR2`, `0x5c8` blocks with a stereo track,
`0x5cc` chain-end calls, `0x5d0` to `0x7cf` the mantissas, `0x7d0` the
sequence again; `0x7e0` to `0x7ef` a profile for test pages
(`<syntakt/stereo.h>`). AUDIO writes in ascending order, so MAIN reads
`0x7d0`, the block, then `0x5b0`: equal means one whole block. The window is
not cleared at boot: MAIN needs the tag.

Measured on the unit (2026-11-10, test build: R is the same sample 10 ms
behind, `mods/samplers-st/README.md`), CPU A: 8 sampler tracks 52 % mono, 99 %
stereo; 4 stereo samplers and 4 FM TONE 99 %, and the FM tracks start to
break up; 3 stereo samplers, 4 FM TONE and the probe 93 %; 7 stereo samplers
93 %. A stereo track costs about 6 points, as much again as the whole mono
track: the native chain, run a second time, is most of a track's cost.
With 7 stereo samplers the profile was, in permille of the block: state
copies 20, drive and base-width 66, multimode 110, AMP 17, sending D 60 (word
by word then), the samplers' whole renders 324. In STOP, CPU A 43 %: stereo
costs nothing there.

With the fingerprint pairing and CPU writes (2026-11-10, 7 stereo samplers and
the probe; the R channel audible as width): CPU A 93 %, CPU M 88 %; profile
state 20, drive and base-width 66, multimode 110, AMP 7, sending 80, renders
322; MAIN reading D 38 and mixing it 43 (permille of MAIN's block); DROP 0,
TORN 0, LATE and AGAIN 10 each over several minutes. 8 stereo samplers: CPU A
99 %.

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
