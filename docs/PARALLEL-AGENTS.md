[elekloader docs](README.md) › Parallel agents on this workstation

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
- Untracked files that exist only in `C:\Desarrollo\synfact-dev` (for
  example `mods/main-profile-st/`, `mods/irq-timer-st/`,
  `tests/test_st_irq_timer*`). If you need one, ask the user; do not copy
  or commit someone else's untracked work.
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

## Current assignment (2026-10-29)

| Branch | Folder | Task |
|---|---|---|
| `feat/poly-analog-st` | `C:\Desarrollo\synfact-dev` (stays) | POLY on analog tracks 9 and 10 with no added CPU |
| `refactor/poly-copy-on-change` | `C:\Desarrollo\synfact-dev-pan` | copy-on-change POLY/M.LFO; native per-track engines |

The original feature branches start at `dbf6e12`. The pan work continues from
`096efaa` (PLAY modes) and `1efc52c` (remove MONO/LEG/PRIO); the current performance
branch includes both commits. Its bank still uses the same host-API-8 core/SYX.
At the time of the original assignment, the uncommitted
changes in `C:\Desarrollo\synfact-dev` are the analog POLY work; none of
them mentions pan. The pan agent checks there is nothing of its own among
them before it moves:

```bash
git -C /c/Desarrollo/synfact-dev worktree add ../synfact-dev-pan feat/poly-pan-per-voice
```
