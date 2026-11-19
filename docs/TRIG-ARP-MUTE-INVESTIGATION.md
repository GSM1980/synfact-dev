# TRIG / ARP / mute investigation

## Native deferred cleanup loses the next close (core 1.8.2)

The owner still reproduced mute → ARP → first TRIG → close mute → ARP with
1.10.4, leaving both screen and keyboard absent. The earlier tests removed
mute directly via the shared-pair manager operation, bypassing its actual
deferred cleanup. Replacing that shortcut with native close `400ffd02` and
full cleanup `40100ea4` reproduces the complete reported failure.

Cleanup visits ARP before mute. After removing mute, visibility synchronously
composes and calls core tick. ARP becomes foreground on native page zero and
closes; its logical session ends, but its already-visited node remains in
the list. Stock cleanup then clears manager `+22h`, erasing this new close
request. Subsequent native key walks skip the closed node but do not remove
it; that unrelated stack tail blocks both ARP opening paths. A later cleanup
request recovers it, matching the earlier need for another modifier action.

Core 1.8.2 moves the existing flag clear before visibility in the same pinned
18-byte window. The newly raised request survives for the next native key
walk. No bank changes, polling, resident growth, new callback or added
instruction are needed. The complete bank is byte-identical to 1.10.4.
Installing the new core SYX is required; replacing only the bank cannot fix
the old core. The regression exercises both cores against the same bank,
native deferred cleanup, real composition/tick, retained owners and LEDs.

## Synchronous composition exposed by normal opening (ARP 1.10.4)

The owner reported that 1.10.3 worked when entering via mute, but normal
opening left screen and LEDs empty. This reproduces with native push,
visibility update `40100ade`, synchronous compose `4000a0e6` and actual
`core_tick`. The previous native-stack fixture stubbed compose, hiding this
reentrancy even though its manager/list operations were real.

Push composes before attaching the new view. The key handler previously
advanced the TRIG index only after `arp_open` returned. During the nested
tick, the new view was already published but its page was still the preceding
native page. The tick closed it, and 1.10.3 immediately forgot its binding;
the key handler could no longer advance the index through `arp_main`.
The native mute fallback already had the target index, so it avoided this.

1.10.4 passes the target subpage into `arp_open` and selects it before native
push. Failed validation/allocation does not change the native index. Native
construction, composition and lifetime semantics are unchanged. The expanded
regression executes the actual compose prologue/core tick/epilogue and real
base constructor/destructor; the LCD portion after core tick is an explicit
boundary. It checks ordinary entry and all earlier mute/retained-owner cases
with this path enabled, and rejects delivered 1.10.3 as a negative control.

## Closed view retained across wrap (ARP 1.10.3)

The owner confirmed 1.10.2's screen behavior, then reported: mute first,
navigate through ARP back to native page zero, close mute and return to ARP.
The screen drew but blue LEDs were absent until FUNC/TRK/BANK was pressed.

The native shared-owner regression reproduces the same visible state with
1.10.2. A retained strong owner keeps the old closed view alive after native
stack removal; `arp_dispose` has not run, so the module's `arp_view` remains
non-null. Opening refuses a second view; DRAW still uses the old MainScreen
binding, while foreground/LED checks reject its closed flag. Releasing the
retained owner invokes the destructor and permits the blue keyboard again.
The particular hardware callback holding that reference is not identified;
the test establishes a legal native lifecycle causing the reported symptom.

1.10.3 ends logical ownership at close time, independently of memory lifetime.
The native close, list removal and reference counts are unchanged. A later
old destructor only clears a binding if it still belongs to that view, so it
cannot cancel a new session. Tests execute native key traversal, shared copy,
removal/destruction, LED gate and LED walk, across digital/analog and POLY.
They retain the old view while the new view is already showing blue and
editing steps, then release it while a new step is held. Model/panel IO,
unrelated native LED handlers and ARP base construction remain boundaries.
No extra polling, render work or new core hooks are introduced.

## Mute-first order fixed in ARP 1.10.2

Hardware feedback narrowed the remaining failure to opening mute before
navigating to ARP. Opening ARP first, then mute, worked. The owner clarified
the contract: ARP content always draws on its selected page; mute remains
above it and owns the keys, and removing mute exposes ARP's keyboard.

1.10.1 blocked all new ARP views under mute. 1.10.2 lets native TRIG finish
its press/release, then the tick creates ARP and moves its newly retained
node immediately before the native mute node. It uses the native list
primitives and manager mutex; ownership, highest opaque and all periodic
foreground/LED/render paths are unchanged. Other unrelated covers still
block creation. The native-stack regression reproduces the 1.10.1 failure,
then checks both orders, both subpages, list backlinks and native removal
of mute, including restored blue keys and step editing on the ARP page.
It covers digital/analog tracks and POLY. No hardware access occurs.

## Current-main integration: ARP 1.10.1

The fix is now based on remote main `d322360` (core 1.8.1, ARP 1.10,
Chorus 1.2 and the consolidated MAIN optimizations). The old 1.9.x
deliveries and the measurements below describe the earlier investigation,
not this delivery's component versions. Only `mods/arp-st/view.c` changes
runtime behavior relative to current main. Its optimized clock, selector,
keyboard and render code is retained. The normal bank configuration is
unchanged, and the core is rebuilt from current main sources.

The mute cost gate compares against an unmodified bank built from the same
`d322360` sources, not the old ARP 1.9 bank. The native-stack check still
reproduces the two earlier blank-screen deliveries as negative controls.
Physical installation and panel confirmation remain the owner's steps.

The normal-bank LFO3 activity test initially failed on both unmodified main
and this candidate: its Chorus initializer double left the native callback
registered for every operation, creating a fictitious LFO subscriber. The
fixture now publishes the actual Chorus UI initializer's PAGE_LAYOUT-only
interest through the real core setter and asserts that route. Its inactive
LFO, live activation, POLY ownership and missed-update negative controls
remain enforced. No Chorus or LFO3 runtime source changes are involved.

Investigated on local `main` commit `e4af43a`: core 1.8.0, ARP 1.9 (blue
steps), POLY 1.5, against official Syntakt OS 1.41. The reproductions below
describe that unmodified baseline; ARP 1.9.1 fixes them as described at the end.
The original checkout was older; all work is isolated on branch
`investigate/trig-pages-arp-mute`.

## Findings

The reports have a common UI ownership problem. ARP keeps a transparent view
above MainScreen. `arp_foreground()` accepts it only when it is the **last**
view in the manager's stack. Permanent mute is also transparent and is placed
above ARP. MainScreen can still receive parameter-page events below both.

ARP's global key/encoder handlers stop handling its pages while covered, and
its tick stops synchronizing them. Its LED handler and draw handler continue
using their saved state. Losing foreground also does not clear its private
held-step mask. These different rules create the cases below.

### 1. Last TRIG page repeats the ARP parameter page

On an independent audio track, outside REC:

1. Open the ARP parameter page (the one with MODE / SPEED / RANGE / LEN).
2. Enter permanent mute with FUNC+BANK, then release the modifier keys.
3. Press and release TRIG while mute is still on top.

The stock TRIG key handler advances the native index on **release**. ARP's
`arp_page` remains zero because `arp_view_key()` returned before the TRIG
handling, and `arp_view_tick()` does not synchronize while covered.
`arp_view_draw()` uses the native index to decide whether it owns the area,
but uses the saved `arp_page` to decide what to draw. Thus the last page shows
the ARP parameters again instead of the chord page:

| Available pages | Native index displayed | Content actually drawn |
| --- | --- | --- |
| TRIG, Euclidean, ARP, chord | 4/4 | ARP from 3/4 |
| TRIG, Euclidean, POLY, ARP, chord | 5/5 | ARP from 4/5 |

The diagnostic compares drawing operations: page contents are identical;
only the indicator changes. Removing mute and running ARP's tick puts the
native index back on its old ARP parameter page, rather than accepting the
native page change.

**Limit of the match to the report:** this reproduces the third page repeated
when there are four pages. With five pages it repeats the fourth, ARP. It
does **not** demonstrate the POLY page itself being drawn at 5/5. Ordinary
ARP-to-chord navigation with no covering view works in both configurations.
Other transparent views can create the same mismatch if they pass TRIG to
the native view; only permanent mute was exercised here.

### 2. Mixed mute/ARP colors and keys with different owners

From the same ARP parameter page, enter mute outside REC. No TRIG press is
required. The native mute LED handler paints the twelve audio-track trigs.
ARP's LED handler still runs below it: it checks REC, lock mode and chord
page, but does not check foreground or the visible native parameter page.
The native setter keeps the first LED write, so ARP supplies trigs 13..16.

With default LEN and all steps enabled, the result is twelve native mute LEDs
plus four blue ARP LEDs. Taps on those blue keys pass through ARP because it
is covered; they do not edit the blue steps. Shorter LEN makes some of the
four dark, and disabled steps can make them red, explaining different color
combinations. Grid REC, live REC, and the chord page suppress ARP's LED
handler and do not produce this particular leak.

### 3. A held trig becomes stuck across mute

1. On the ARP parameter page outside REC, hold a trig (the diagnostic uses 4).
2. Enter mute without releasing that trig.
3. Release the trig while mute covers ARP.
4. Leave mute and turn an encoder.

The initial press sets `arp_steps_held`. The covered release returns before
`arp_step_key()`, and neither focus loss nor the next tick clears the bit.
ARP now treats every encoder A..F as an offset edit for a supposedly held
step. The diagnostic turns SPEED (B): step 4's OFS changes instead. This
explains persistently incorrect controls after the colors return to blue.
Closing and reopening ARP clears the private held-step state.

### 4. Changing parameter pages under mute leaves ARP active on another page

1. Open the ARP parameter page, then enter mute.
2. Press SYN, FLTR, AMP or LFO while mute covers ARP.
3. Leave mute.

`arp_view_key()` returns at its foreground check, before the normal
close-on-parameter-page branch. Native MainScreen selects the new parameter
view. ARP remains on the stack and becomes foreground again when mute goes
away. On the same track, its tick never closes it just because MainScreen is
no longer showing TRIG. Its keys, LEDs and encoders can therefore still edit
ARP while another parameter page is selected.

The diagnostic executes the native mute/MainScreen SYN key switches and
models the actual parameter-view replacement at the existing fixture
boundary. After the replacement and return, it proves ARP still consumes a
trig tap, changes its enabled-step bit, and paints all sixteen LEDs blue.
FLTR/AMP/LFO follow the same early return in the source, but are not separately
executed in this diagnostic.

A clean mute round trip, with no page changes or held trigs, recovers normal
step editing in the diagnostic. For a hardware workaround after getting into
a bad state, leave mute, switch to SYN/FLTR/AMP/LFO so ARP can close normally,
and then navigate back to ARP.

## Evidence and scope

`tests/investigate_st_arp_ui.py` runs against an unmodified compiled bank. It
is a diagnostic that asserts the **broken** states, not a regression claiming
a fix. Its two page-count fixtures represent the presence/absence of POLY;
POLY's native subscriber is not installed in this fixture.

Executed target code includes ARP key/tick/draw/LED/encoder handlers, native
MainScreen FUNC+BANK and SYN switches (`40033ee0`), native permanent-mute key
handling (`4003cdbc`), native TRIG navigation (`4003bc74`, increment at
`4003c50a`) and native mute LED selection (`400375dc`).

View construction, stack publication/removal and parameter-view replacement
are explicit doubles, as are shared-owner operations, title strings, model
reads and the LED output sink. The LED sink preserves first-write ownership,
matching the stock setter `40008220`; the full native manager LED walk is not
executed. This is offline evidence, not a physical-panel, full-scheduler or
audio reproduction. No instrument was accessed.

Checks run:

- A fresh core build verified the output SYX and protected stock sections.
- A fresh bank compiled LFO3, ARP and POLY with the documented SGDK flags.
- Existing `test_st_arp_view_emu.py` passed with the corrected ColdFire
  Unicorn library. The default pip DLL raised a CPU exception in the existing
  gate test; its failed run is not counted as a pass.
- The new diagnostic reproduced all four cases and passed normal navigation,
  clean mute round-trip, REC and chord-page controls.
- `test_st_trig_pages.c` and the firmware-independent unit suite were run;
  the latter reported 39 passed, zero failed.
- Full product-bank/common regressions and hardware validation were not run;
  no production code was changed.

Example invocation, with outputs from the normal build commands:

```bash
python tests/investigate_st_arp_ui.py \
  --core "$BUILD/main.bin" --bank "$BANK" --out "$BUILD/arp-ui-investigation.json"
```

## Repair direction

Keep the native page index, ARP subpage and active MainScreen parameter view
consistent across covering views. ARP should relinquish LEDs/inputs to native
mute and close or suspend coherently when native navigation leaves its page.
Cancel private held-step state on that transition, retaining consistent
ownership of press/release pairs. Cover these native forwarding paths in the
regressions; a foreground-only TRIG fixture misses them.

Changing the page-vector size/mask or the ARP generator alone would not fix
the reproduced failures. This investigation deliberately leaves the firmware
implementation unchanged during diagnosis.

## ARP 1.9.1 correction and MAIN cost

The bank now draws from the native page index and adopts native navigation
when it regains foreground. Parameter-page key presses close ARP even when
it is covered. Its LEDs relinquish foreground, and a cancelled held step
retains only ownership of its release. Automatic opening waits until
MainScreen is actually at the top of the stack. A real track rebuild still
retains the selected ARP subpage.

No audio processing or core source changed. The UI tick reuses one foreground
check, and REC reads inline the exact native byte getters including their
selection-active gate. One steady tick + draw + LED call in the linked fixture:

| UI state | ARP 1.9 instructions | ARP 1.9.1 instructions |
| --- | ---: | ---: |
| No ARP view, no previous UI event | 214 | 213 |
| Native TRIG page | 259 | 257 |
| ARP parameter page | 4,468 | 4,442 |
| ARP chord page | 3,456 | 3,428 |
| Mute over ARP | 3,969 | 3,588 |

MAIN's ARP render callback is unchanged in the same before/after fixtures:

| Active tracks | Not due | Due |
| --- | ---: | ---: |
| 0 | 8 | 8 |
| 1 | 244 | 691 |
| 2 | 466 | 1,351 |
| 4 | 910 | 2,674 |
| 8 | 1,798 | 5,322 |

The new `tests/test_st_arp_mute_emu.py` reproduces native key forwarding,
checks corrected behavior, rejects the original bank, and enforces the
render equality and combined UI non-increase. The native close fixture was
also corrected to write the stock **byte** flag at view +30h rather than a
big-endian word ending three bytes later. Direct REC reads are checked
against the original getters for all eight active/grid/live combinations.

These instruction counts do not model cache misses, scheduler timing or
physical CPU percentages. View allocation/ownership, stack publication and
parameter-view replacement remain the explicit boundaries described above.

The complete normal bank (including stereo samplers, FM, Recorder, LFO3,
POLY, System info, Sample Manager, Chorus and modulation skip) was also built
with ARP 1.9.1. Common regressions, safe-boot emulation, the original ARP view
fixture, the new mute/cost fixture, concurrent digital ARPs, analog ARPs and
Chorus UI passed. The legacy FM source/reference comparison was omitted: its
test expects `imported/provenance.json` from the retired importer, which the
current Digitone recipe does not generate. Its attempted run failed on that
missing input; no check was weakened. The final common runner records that
one optional skip.
The current FM recipe check passed against official Digitone OS 1.44 (four
outputs rebuilt and verified).

The delivered core SYX was reverified against official OS 1.41. Core MAIN and
AUDIO, and the bank's complete AUDIO section, match the baseline byte for
byte. The fixes are in the ARP bank module, not the resident core. Installation
and physical-panel/audio/CPU confirmation remain the owner's steps.

## Hardware feedback: empty pages in 1.9.1; incomplete 1.9.2 fix

The owner reported both ARP screens empty on the delivered 1.9.1 bank. The
opening guard added in that version required the actual stack tail to equal
MainScreen. This omitted its separately published native ParameterView:
MainScreen's page switch at `4003384a` replaces/publishes that child through
manager `401015b8`. A transparent TRIG child can therefore be above the
highest opaque MainScreen without being a modal or mute overlay.

With that child at the stack tail, the delivered 1.9.1 bank reproducibly
returns zero from key opening, leaves `arp_view` null after the fallback
tick, and produces no text drawing calls on an added ARP page. The old
fixtures incorrectly placed MainScreen itself at the actual tail before
opening, so their prior passing results missed this regression.

1.9.2 permits the current native TRIG child as well as MainScreen in both
opening paths, while preserving the rejection of unrelated covering views.
The shared fixture now models this child by default. The regression also
executes both entry states explicitly, including MODE and chord drawing,
close/reopen and tick fallback, with and without POLY. It rejects 1.9.1 via
`--blank-before` and retains the original mute and MAIN instruction gates.
No renderer, audio engine or core source change is needed for this correction.

## Full native publication reproduces 1.9.2 failure (1.9.3)

The owner reported that 1.9.2 still had empty ARP pages, and answered that
the blue keyboard did not activate either. The preceding fixture's stack
publication double still omitted normal MainScreen children above TRIG.

Executing MainScreen show `40033b74`, publication `400ffd62`, parameter
replacement `401015b8`, shared ownership and manager push `40100bc2` gives:
MainScreen, child `+228h`, TRIG `+c0h`, children `+e8h`, `+218h`, `+f0h`,
`+220h`. Both 1.9.1 and 1.9.2 reject this ordinary stack tail. Their ARP view
remains null after key opening and fallback, and no ARP labels are drawn.

1.9.3 accepts the six persistent children on opening. Mute at `+230h` and
other unrelated tails still block opening. The new native stack regression
runs manager `401007ce` and the contextual core DRAW bus instead of invoking
the ARP drawing callback directly. It checks both pages, blue LED claims and
step toggling, key/tick opening, digital and analog tracks with/without POLY,
and rejection above a natively inserted mute view. Both delivered blank
banks are negative controls. Application/model IO, bitmap primitives, base
ARP construction and optional maintenance after normal child publication
are explicit boundaries. No instrument is accessed.

The fix changes only the opening check. Measured steady UI counts equal
1.9.2, and MAIN render counts equal the original 1.9 at 0/1/2/4/8 active
ARPs. Core MAIN/AUDIO and bank AUDIO remain byte-identical. These are
offline checks; the owner's panel confirmation remains outstanding.
