[elekloader docs](README.md) â€º Parallel agents on this workstation

# Parallel agents on this workstation

Several coding agents may work on this repository at the same time, each on
its own branch. A branch alone does not isolate them: every agent working in
`C:\Desarrollo\synfact-dev` shares one working tree, so one agent's
`git checkout` changes the files another is editing, and uncommitted changes
follow whichever branch is checked out. Follow these rules.

## One folder per agent

Each agent works in its own git worktree: a separate folder with its own
branch, sharing the same repository.

```bash
git -C /c/Desarrollo/synfact-dev worktree add ../synfact-dev-<topic> <existing-branch>
git -C /c/Desarrollo/synfact-dev worktree add -b <new-branch> ../synfact-dev-<topic> main
git -C /c/Desarrollo/synfact-dev worktree list
```

- The agent whose branch is checked out in `C:\Desarrollo\synfact-dev`
  stays there. Every other agent moves to its own worktree before it edits
  anything, and runs all its commands from that folder.
- In a folder another agent may be using, never run `git checkout`,
  `git switch`, `git stash`, `git reset`, `git clean` or `git restore`.
- Before committing, read `git diff` and `git status`. If you see changes
  you did not make, stop and tell the user; do not commit, move or discard
  them.
- When the work is merged, the user removes the worktree
  (`git worktree remove ../synfact-dev-<topic>`).

## What a new worktree does not have

- Ignored, generated files: `mods/poly-st/imported/` (regenerate with
  `tools/import_poly_kernels.py`) and `out/`. Rebuild them in your own
  worktree as [SYNTAKT-BUILD.md](SYNTAKT-BUILD.md) says. The toolchain in
  `C:\Desarrollo\syntakt-dev\toolchains` is shared and read-only.
- Untracked files that exist only in `C:\Desarrollo\synfact-dev`. If you
  need one, ask the user; do not copy or commit someone else's untracked
  work.
- The stock OS files and firmware outputs: they are never committed, in
  any worktree.

## Overlapping files

Separate folders avoid clobbering, not merge conflicts. Two branches that
edit the same files (for example `mods/poly-st/`: POLY on the analog tracks
and per-voice pan) conflict when merged.

- Keep each change to the files its task needs; no drive-by reformatting
  of shared files.
- The user decides the merge order. The first branch merges into `main`;
  each later one rebases on the new `main`, resolves conflicts, and reruns
  its tests and the cost checks before it merges.
- No agent pushes, merges or opens a pull request unless the user asks.

## Integration (2026-10-29)

Analog POLY and its keyboard Sound fix are already in main (`1128465`).
The pan work's POLY 0.7 was physically accepted at MAIN 91% POLY / 93% M.LFO.
`integrate/poly-analog-mlfo` in `C:\Desarrollo\synfact-dev-pan` resolves both
histories as POLY 0.8 before main advances. It retains analog setup/restore,
its native skip kernels, and the digital copy-on-change/native voice paths.

The arp agent works separately in `C:\Desarrollo\synfact-dev-arp` on
`feat/arp-digitone-st`; do not switch or modify its worktree. The primary
`C:\Desarrollo\synfact-dev` worktree contains unrelated untracked profiling
mods/tests; the integration leaves them untouched. No worktree is removed and
no remote push is part of this merge.


## Folder implementation (2026-10-29)

`feat/sample-folders` uses `C:\Desarrollo\synfact-dev-pan`; it includes the
previous feedback-status documentation branches. Core 0.13.0 retains storage,
pool, preview/audio and all native hooks; `sample-manager-st` is optional UI.
A separate `feat/sample-folders` in `C:\Desarrollo\synfact-release` contains
Transfer 0.0.6. Transfer's previous changes were committed and pushed at the
user's explicit request as `1c39aa6` before this new work. The new folder work
is not merged or pushed. No instrument IO has been performed.

## ARP integration (2026-10-30)

User authorized merging and pushing ARP to main after TEST06. The ARP branch
rebases on `10f3590`, preserving POLY 0.8, independent/shared digital LFOs and
analog groups. Host API 10 aliases native_query/native_dispatch at the same
offset 84, retaining the 88-byte record. Its Sound codec directions share a
bounded helper to keep the combined core inside the unchanged 64 KiB reserve.
Validation output: `out/core-st-0.12.22-arp-main-validated/` in the ARP worktree.
This integration advances main by fast-forward only after verification. The
untracked profiling work in the primary worktree and the sample-folder worktree
remain untouched; no worktree is removed.

## Folder/ARP integration (2026-10-30)

The folder and ARP branches meet in core 0.13.1, host API 11. The native
query/dispatch union stays at byte 84; the pool service is appended at byte 88.
Sample Manager requires API 11 so API 10 cannot expose an out-of-bounds pointer.
The complete bank includes LFO3, ARP, POLY and Sample Manager together.

Validation: `out/folders-arp-main-20261030/` in the pan worktree. Full common
regressions (including units), linked UI provider lifecycle, 9,600 POLY blocks,
native note admission, ARP Sound persistence and analog ARP pass. POLY MAIN
fixture: 12,286 instructions before/after; resident 55,876/65,536 bytes.
Transfer stays outside main: remote `move-transfer-to-elekloader` at `7210e52`
includes folders, the prior Elekloader conversion/upload work and the user-
authorized browser preview. Synfact-release main remains `142c68d`.
