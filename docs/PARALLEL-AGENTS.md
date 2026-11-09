[Synfact docs](README.md) › Parallel agents

# Parallel agents

Several coding agents may work on this repository at the same time, each on
its own branch. A branch alone does not isolate them: every agent working in
the same folder shares one working tree, so one agent's `git checkout`
changes the files another is editing, and uncommitted changes follow whichever
branch is checked out. Follow these rules. Paths below use `$REPO` for the
repository's root folder (see [SETUP.md](SETUP.md)); the examples use a
sibling folder for each worktree.

## One folder per agent

Each agent works in its own git worktree: a separate folder with its own
branch, sharing the same repository.

```bash
git -C "$REPO" worktree add "$REPO/../synfact-dev-<topic>" <existing-branch>
git -C "$REPO" worktree add -b <new-branch> "$REPO/../synfact-dev-<topic>" main
git -C "$REPO" worktree list
```

- The agent whose branch is checked out in `$REPO` stays there. Every other
  agent moves to its own worktree before it edits anything, and runs all its
  commands from that folder.
- In a folder another agent may be using, never run `git checkout`,
  `git switch`, `git stash`, `git reset`, `git clean` or `git restore`.
- Before committing, read `git diff` and `git status`. If you see changes
  you did not make, stop and tell the user; do not commit, move or discard
  them.
- When the work is merged, the user removes the worktree
  (`git worktree remove <path>`).

## What a new worktree does not have

- Ignored, generated files: `mods/poly-st/imported/` (regenerate with
  `tools/import_poly_kernels.py`), `mods/fm-tone-st/imported/` (regenerate
  with its `tools/import_digitone.py` and your Digitone OS 1.44 file),
  `mods/chorus-st/imported/` (the same, with `mods/chorus-st/tools/import_digitone.py`)
  and `out/`. Rebuild them in your own worktree as
  [SYNTAKT-BUILD.md](SYNTAKT-BUILD.md) says. The toolchain is shared and
  read-only: point every worktree at the same `SGDK_BIN` and `LLVM_MINGW_BIN`.
- Untracked files that exist only in another worktree. If you need one, ask
  the user; do not copy or commit someone else's untracked work.
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
