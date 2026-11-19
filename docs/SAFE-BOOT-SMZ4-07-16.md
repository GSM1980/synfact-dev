# Issues 07 and 16: safe core/SMZ4 startup and installation

## Withdrawn development core 1.8.1: nonresident binder (2026-11-12)

This historical prototype is not the consolidated release core 1.8.1. The
release contains the validated resident implementation developed as 1.8.1.3;
see [the release mapping](MAIN-OPTIMIZATION-RELEASE.md).

Loading the new core/bank and restarting produced V04 at **40347BC2**.
That exactly matches 1.8.1's `core_modules_bind`; the shown return 46006410
is immediately after its call from `core_machine_load`. I moved the binder
to appended `.boot` to make room for native-route bitsets. This storage is
destroyed by native BSS startup after the early resident copy; it is not
application executable storage. The previous statement that it remained
callable after boot was incorrect.

Opcode audits and the host binder test passed because they did not model
native clearing. Target event fixtures directly seeded lists, bypassing the
actual binder. `test_st_module_startup_emu.py` now reproduces the exception
at the exact address by clearing appended storage after resident copying.
With the corrected core it executes the real binder/input reset/list builder/
publisher for empty/full/rejected/out-of-memory cases, checking lists, order,
publication and saved ABI. Heap and initializers remain explicit boundaries;
it is not a complete device boot. It is included in common regressions.

1.8.1.1 restores the original resident 1.8.0 binder/dispatcher and withdraws
route bitsets. It keeps the stock transport exit and the byte-identical
samplers 1.4.6/LFO3 1.5/ARP 1.13/POLY 1.6 bank. The builder and linked layout
test reject nonresident startup entries. AUDIO and the other OS sections stay
identical to the working 1.8.0 reference. Hardware retry remains pending.

The failed candidate may already be TRYING; recovery must not assume the
next boot retries that bank. Re-send the bank and verify displayed versions.

## Scope

Issue 07 asks for core/SMZ4 compatibility when the core is updated. Issue 16 asks for a restart notice, a compatibility check against the installed SYX before accepting an SMZ4, and protection against an update that makes the instrument fail to boot. The intended failure mode to avoid is a soft brick or repeatable exception during application startup. The stock bootloader is unchanged and FUNC+POWER recovery already exists, but needing a separate recovery SYX and multiple transfers is too costly for an ordinary incompatible bank.

This records the design and its core 1.7 implementation. It is not a claim
that arbitrary native code can be made exception-proof. The loader fails closed
before executing a bank with a missing or mismatched contract, and a failed
trial preserves a bootable path.

## Implemented in core 1.7

- SMZ4 feature bit 4 carries exact core contract `53594603` in the former
  global initializer field. The Python and TypeScript builders emit it;
  the core parser, boot loader, CLI and Transfer check it. The ID must be
  bumped whenever the resident host API or hook behavior changes. The
  installed core exposes it in `/mod-status` word 22.
- The old 16 MiB reservation holds two 8 MiB slots, with checksummed metadata
  in each slot's final sector. `/mods-stage` writes the inactive slot and
  validates its eMMC bytes before publishing a `CANDIDATE`. The native
  stream still handles transfer framing and bounds. The legacy record remains
  readable during migration; the first candidate uses slot 1.
- On reboot, the core preflights the candidate, marks it `TRYING` before any
  bank initializer, and uses the prior `GOOD` slot on rejection. The existing
  UI tick confirms it automatically after AUDIO is ready, sample-pool startup
  has finished (when present), and 150 normal UI ticks have run. A crash or
  restart before then skips the trial and selects the previous `GOOD` slot or
  stock machines. No Transfer reconnection or user confirmation is required.
  `/mod-status` words 23..26 expose trial and rollback state.
- Before relocation or any bank initializer, the core checks a compact table
  of every stock-code preimage patched by the current LFO3 and MODULATION SKIP
  modules. The table is generated from their own source declarations and a
  regression detects drift; a mismatch refuses the bank with status 11. The
  two patching initializers run after all other module initializers, while
  event subscription order stays unchanged. If an ordinary initializer fails,
  no bank hook has been installed.
- Native host tests cover corrupt records, interrupted staging, failed writes,
  contract mismatch, trial boot and automatic confirmation. The core build,
  bank matrix and site pairing are release gates. Hardware validation remains
  pending.

The generated table covers the two audited stock-code patchers in this bank;
adding another requires extending it and bumping the contract. The slot record
does not contain a cryptographic whole-file digest: the boot parser validates
the file's envelope checksum, MAIN checksum and AUDIO checksum before execution.
The contract is an explicit release identity, not a hash automatically derived
from every source change. Arbitrary native bank code cannot be guaranteed
exception-proof. Automatic confirmation proves startup and a short period of
UI activity; it cannot predict a crash after the five-second health window.
Do not mark 07 or 16 complete before physical power-loss, rollback and both
update-order tests.

## Core 1.7.1: the first trial boot crashed (2026-11-09)

**What happened.** The first `mods put` of a bank to `/mods-stage` with core
1.7, then a restart, gave `EXCEPTION DK0094 V03` (address error) at startup,
and would again at every startup. The words on that screen located it:
`4600289C` returns inside `core_mods_record_io`, `00019FFF` is slot 1's
record sector and `00000200` its 512 bytes.

**Why.** `machine-boot.c` read the card through its own accessor table,
`{fw_mmc_read, 0, sectors}`, written when the boot never wrote. The trial
added one write (CANDIDATE → TRYING, before any bank code) through that
table: `io->write` was 0, so the core called address 0. The record never
changed, so every boot chose the same candidate and crashed again: the
protection failed on its own first step, before any bank code ran. The bank
being tried had nothing to do with it.

**Why the tests passed.** Each test replaced the piece it did not test:
`test_core_st_smz.c` (the boot) replaced `core_mods_transition_io`,
`test_core_st_safe_storage.c` (the slots) used its own complete table, and
neither was in the regressions. The CLI's `put` had never reached the device
either: `data_rpc.py` refused `/mods-stage` until 2026-11-09.

**Fix and guards (1.7.1, contract unchanged: banks for 1.7 load as they are).**

- The boot table has `fw_mmc_write` (`machine-boot.c`).
- `storage.c` fails closed: every write path refuses a table without a write
  function, so a boot with such a table rejects the candidate instead of
  jumping (`test_core_st_safe_storage.c`).
- `tests/test_core_st_trial_boot.c` links the production loader, slot
  storage and parser, with no double, over a simulated card and a real bank:
  trial marked TRYING, crash and rollback, confirmation, corrupt candidate,
  GOOD restored, and the boot writing nothing but trial records. With 1.7's
  table it reproduces the crash (segmentation fault).
- `test_core_st_smz.c` checks that every storage call gets the boot's real
  accessors.
- `tests/test_st_common_hooks.py` checks, in the linked SYX, that every
  accessor table (`storage`, `io`) holds `fw_mmc_read`, `fw_mmc_write`.
- `tests/test_st_safe_boot_emu.py` runs the linked core's own
  `core_mods_transition_io` with both tables in Unicorn, behind the stock
  read/write entry points. On 1.7's `main.bin` it faults at PC 0, as the
  device did.
- The three host tests run in `tests/test_st_common_regressions.py`; the
  Unicorn one with `--unicorn-python` (reported as skipped without it).

**Before calling a safe-boot change done**, on the device, by the user:
install the core; `mods status` shows contract `53594603`; `mods put` a
bank; restart; `mods status` shows the trial and, a few seconds later, the
bank loaded with no rollback; restart again: loaded, no trial. Then the same
with a bank that fails its checksum (rollback to the previous bank).

## Baseline before core 1.5 (2026-11-07)

- `/mods` is one opaque file at `0x02400000..0x03400000`, with one length/CRC metadata sector at `0x03400000` (`mods/core-st/storage.c`, `STORAGE.md`). Upload invalidates the previous record **before** writing; interruption leaves no bank. A completed upload replaces the only copy. `mods.py` and the site read the bytes back, but this proves transfer integrity, not boot safety.
- `core_machine_load` validates the disk file, validates its RAM copy again, relocates it and only then calls machine initializers and `core_modules_bind` (`mods/core-st/machine-boot.c`). Missing/corrupt/unknown feature banks are skipped; stock machines continue. The parser checks bounds, checksums, pointers, event IDs and the general `0x454c0001` ABI (`mods/core-st/smz.c`). This is a valuable first barrier.
- The general ABI and feature bits do **not** name the exact core build or every service, hook preimage and behavioral contract a module expects. Initializers check some UI/host versions themselves, but that executes bank code. `core_modules_bind` runs initializers in bank order. For a concrete cross-version failure path, LFO3 accepts host API 7 and may install direct hooks; a later ARP initializer requires host API 10 and can reject. Clearing the public machine flag and subscriptions then cannot undo LFO3's earlier hooks. Structural validation cannot prove arbitrary code safe.
- The site already says to restart in its upload confirmation and result (`synfact-site/src/transfer/view.ts`); the CLI also prints it. Issue 16's notice is therefore partly present. The missing part is a persistent, explicit **new bank pending restart / trial** state and a device compatibility verdict before replacement.
- The site's local `inspectMods` checks the file structure and even recognizes legacy envelope variants, but `uploadMods` has no query for the installed core's contract before writing (`synfact-site/vendor/transfer/drop.mjs`). A locally valid bank can thus replace a working bank even if the device will reject it at the next boot.
- `/mod-status` reports the last boot result, but no core compatibility identity or candidate/rollback state. The existing recovery core deliberately skips `/mods`; installing it by FUNC+POWER, replacing the bank, then reinstalling the normal core remains the manual fallback.
- When updating the SYX, the site may not be connected to the running application (the updater has its own protocol). A host-only check cannot protect that direction. The **new core itself** must reject a stored bank it does not know how to run.

## Proposed contract: a matching pair, checked by the core

1. Give each core a machine-readable **compatibility identity** distinct from the human version. Initially use an exact, generated contract ID for each tested core build (or an explicit allowlist of tested IDs). Do not infer compatibility from semver or the unchanged general ABI. Expose the ID and core version through a read-only Data RPC status endpoint, available even with no loaded bank.
2. Put the required core contract ID(s), stock OS 1.41 identity, required core UI/host/module capabilities and bank build ID in a new, checksum-covered SMZ4 compatibility record. For the first safe generation, require one exact tested core ID. A future range may be added only after cross-version tests establish it. A new envelope version or required feature bit ensures **older cores reject the new bank before execution**.
3. In the new core, validate the compatibility record and all existing structural checks directly from bytes **before relocation, initializers, AUDIO publication or any bank hook**. A missing/unknown contract on a legacy bank is rejected by default unless its exact hash is on a maintained tested allowlist; stock mode remains available. This also protects a core update when an old bank remains on disk.
4. For modules that patch resident addresses, generate a declarative table of expected hook addresses and preimage bytes at bank build time. The core checks this table read-only before any initializer. Move installation behind a common commit step with preimage recheck and IRQ exclusion. Initializers may prepare private state but must not patch live code during preflight. This requires an audit/migration of existing modules; merely adding a version field is insufficient protection against a mismatched hook.
5. The builder rejects undeclared core requirements and package combinations not known to match the selected core. The Python and TypeScript builders emit identical metadata; the site and CLI use the same compatibility rules. The connected-device check is useful UX, while the boot check remains authoritative.

## Proposed storage: trial bank with rollback

The currently reserved 16 MiB `/mods` payload region can be split into two 8 MiB slots. A structurally valid bank is already bounded by a 2 MiB MAIN region plus at most 723,120 AUDIO bytes and padding, so an 8 MiB slot has room. Keep **two independent, checksummed metadata records** in reserved sectors, with slot, length, full-file digest, generation and state; verify their exact locations against the OS 1.41 eMMC map before implementation. Do not use the sample or project areas.

1. Upload writes only the inactive slot, then reads it back and runs the same structural/compatibility preflight the boot loader will use. A failed or interrupted upload leaves the old `GOOD` slot and its metadata untouched.
2. Only after full verification, commit a `CANDIDATE` record. The response says **uploaded, restart to try; previous bank retained**. The running bank is never unloaded in place.
3. At boot, the core checks candidate and current core ID. On a mismatch, it records a refusal and uses a compatible `GOOD` bank, or stock mode. Before it calls any candidate code, it durably changes `CANDIDATE` to `TRYING`. If this write/readback fails, it does not try the candidate.
4. The implemented core automatically confirms after sample/AUDIO startup and five seconds of normal UI ticks. It does **not** mark `GOOD` when an initializer merely returns. If power is cycled or the device crashes while still `TRYING`, the next boot selects the previous compatible `GOOD` bank (or stock mode). A later runtime crash cannot be inferred from a completed startup.
5. Metadata records use independent sectors, sequence numbers and checksums. Never overwrite the only `GOOD` slot while staging. On torn writes, select the last valid compatible `GOOD` record. If neither validates, boot stock. Keep the existing manual recovery SYX as a last resort.

Migration must preserve a valid legacy `/mods` file until the first new candidate has been verified. The old metadata and slot layout must be handled explicitly; a downgrade to an older core may load only its matching legacy bank or no bank, never an unrecognized candidate. The reserved-space split is a design possibility, not yet a validated disk migration.

## Installation flow

**Bank first:** the connected new core reports its contract ID. Transfer/CLI check that the selected SMZ4 declares this ID; the device repeats the check before accepting the candidate. An old core without the identity endpoint receives no safe-generation bank; the UI directs the user to update the core first. The new bank's required feature/version bit also makes an old core refuse it at boot.

**Core first:** the new SYX is installed through the stock updater. On its first boot the new core checks the stored bank's contract. If it is unknown, it boots with stock machines and a visible `MODS INCOMPATIBLE` status. The user can then upload the paired bank without reflashing a recovery core. A failed upload or trial never removes the stored `GOOD` bank.

After upload, Transfer shows **restart required**. On reconnect it reads status and shows a trial still being checked, rollback or incompatibility. The core commits a healthy startup itself; Transfer never needs to be connected to confirm it.

## Verification gate before shipping

- Unit tests of contract parsing on core and Python/TypeScript builders: matching pair, older/newer/unknown core, malformed/legacy bank, corrupt checksums and preimage mismatch. Do not relax existing parser checks.
- Storage fault injection after every payload and metadata write, plus readback failure: the previous `GOOD` bank or stock must always remain bootable. Test migration and downgrade paths.
- Boot-state tests for candidate accepted, rejected, initializer failure, crash/reboot before the health gate, automatic confirmation and rollback. Assert that no module callback, AUDIO page or direct hook is published before all preflight checks pass.
- Full bank regression and physical tests for both update orders, power loss during transfer, reboot before the health gate and normal automatic confirmation. Agents do not perform device writes; the user controls these tests.

No `SYX`/`SMZ4` binaries or stock-derived bytes belong in the repository. Do not mark either issue complete until the on-device rollback and both update orders pass.
