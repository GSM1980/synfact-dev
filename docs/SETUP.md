[Synfact docs](README.md) › Setting up a workstation

# Setting up a workstation

This page is for people and for coding agents. It lists every tool the
repository uses, where to get it, how to install it, and how to check it. Do
this once per machine. The build commands themselves are in
[SYNTAKT-BUILD.md](SYNTAKT-BUILD.md).

**No paths are written into this repository.** Each machine says where its
tools are with environment variables (section 2). Commands in the docs use the
variable names, so the same command works on every machine. Do not commit a
path from your machine.

## 1. Tools

| Tool | What it is for | Where to get it | Check |
|---|---|---|---|
| Git | every change; pull requests | <https://git-scm.com>. On Windows, the installer includes **Git Bash**, which the commands assume | `git --version` |
| GitHub CLI `gh` | opening pull requests | <https://cli.github.com>, then `gh auth login` | `gh auth status` |
| Python 3.9 or newer (3.12 is the version used for testing) | the engine, the SDK, the tests | <https://www.python.org/downloads/> (on Windows, tick "Add python.exe to PATH") | `python --version` |
| Node.js 22.18 or newer | the TypeScript engine's tests (`js/`) | <https://nodejs.org> | `node --version` |
| SGDK 2.11 | its m68k compiler (GCC 13.2.0) builds the core and the mods | <https://github.com/Stephane-D/SGDK/releases>, the 2.11 release. Unzip it anywhere; the compiler is in its `bin` folder | `"$SGDK_BIN/gcc" --version` |
| llvm-mingw, UCRT x86_64 build | the host clang (`i686-w64-mingw32-clang`) that compiles the host-side regression tests | <https://github.com/mstorsjo/llvm-mingw/releases>: the `llvm-mingw-<date>-ucrt-x86_64.zip` file. Unzip it; its `bin` folder is `LLVM_MINGW_BIN` | `ls "$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe"` |
| elektron-firmware-tool | the compact packer that writes the core's `.syx` (`--eft`) | <https://github.com/mischa85/elektron-firmware-tool> (MIT licence). Build it or download it as its README says. Set `SYNTAKT_EFT` to the executable, not to its folder | run it once with no arguments and read its usage text |
| Unicorn (Python package) | the emulator tests only | `pip install unicorn==2.1.4` (the version those tests were written against) | `python -c "import unicorn; print(unicorn.__version__)"` |
| Elektron OS files | the stock input for every build. **They are never in this repository.** | Elektron's support pages: Syntakt at <https://www.elektron.se/download-support-syntakt>, Digitone at <https://www.elektron.se/download-support-digitone>. Use the `.syx` file, unzipped if Elektron ships a zip | the engine checks the file's SHA-256 and refuses any other file |

Notes:

- **The stock file must match exactly.** The engine knows the supported
  releases by hash (`elekloader/devices/syntakt.py`). The Syntakt build needs
  **Syntakt OS 1.41**. The Digitone-based import steps need
  If Elektron's page offers a different version, the engine refuses the file,
  which is the intended behaviour.
- **SGDK's binary names.** The SGDK 2.11 build this repository was tested with has unprefixed tools
  (`gcc.exe`). Look at your `bin` folder: if the compiler is `m68k-elf-gcc`,
  follow the prefixed variant in section 2. The engine's default prefix is
  `m68k-elf-`.
- **Keep the OS files out of Git.** `.gitignore` covers the usual names, but
  `git status` before every commit is still your check.

## 2. Environment variables

Set these once per machine: in `~/.bashrc` (Git Bash, macOS or Linux), or in
your PowerShell profile (Windows). Replace each `/path/to/...` with your own.
Variables you do not need can be left unset.

### Git Bash, macOS or Linux

```bash
export PY=python                                             # or the full path to your Python
export SGDK_BIN=/path/to/sgdk211/bin                         # folder with gcc, as, ld, objcopy
export PATH="$SGDK_BIN:$PATH"
export LLVM_MINGW_BIN=/path/to/llvm-mingw/bin                # folder with i686-w64-mingw32-clang
export SYNTAKT_STOCK=/path/to/Syntakt_OS1.41.syx             # your stock Syntakt OS file
export SYNTAKT_EFT=/path/to/elektron-firmware-tool           # the executable
export DIGITONE_OS=/path/to/Digitone_and_Digitone_Keys_OS1.44.syx   # optional: Digitone-based import steps
```

If your SGDK binaries have **no** prefix (`gcc`), add these two lines. Without
them, the engine looks for `m68k-elf-gcc`:

```bash
export ELEKLOADER_CROSS=''
export GCC_EXEC_PREFIX="$SGDK_BIN/"
```

If they **do** have the prefix (`m68k-elf-gcc`), set neither variable.

### Windows PowerShell

```powershell
$env:PY = 'python'
$env:SGDK_BIN = 'C:\path\to\sgdk211\bin'
$env:PATH = "$env:SGDK_BIN;$env:PATH"
$env:LLVM_MINGW_BIN = 'C:\path\to\llvm-mingw\bin'
$env:SYNTAKT_STOCK = 'C:\path\to\Syntakt_OS1.41.syx'
$env:SYNTAKT_EFT = 'C:\path\to\elektron-firmware-tool.exe'
$env:DIGITONE_OS = 'C:\path\to\Digitone_and_Digitone_Keys_OS1.44.syx'
```

For unprefixed binaries, also set `$env:ELEKLOADER_CROSS = ''` and
`$env:GCC_EXEC_PREFIX = "$env:SGDK_BIN\"`.

### Optional: inputs the repository does not include

Some regression and emulator checks need files that are **not** in this
repository. Without them those checks print `SKIPPED` and do not count as
passed (see [TESTS.md](TESTS.md)). You can ignore these unless you are
working on that part.

| Variable | Used by |
|---|---|
| `SYNFACT_SRC` | a source tree this repository does not include. Some checks read it; without it they print `SKIPPED` |
| `SYNTAKT_WORK` | a folder of emulator fixtures this repository does not include. Without it, the checks that need it print `SKIPPED` |
| `DIGITONE_SECTIONS` | a folder of Digitone 1.44 section extracts this repository does not include |
| `LIBUNICORN_PATH` | only a custom Unicorn build. A `pip install` does not need it |
| `CFBENCH` | analog-emu's `cfbench` (its `target/release/cfbench`): `tests/test_st_stereo_natives_emu.py` runs EMAC code on its ColdFire core |

## 3. Check the setup

Run these from the repository root. The first two need no inputs.

```bash
python tests/test_units.py
(cd js && node --test "test/*.test.ts")
python -m elekloader.sdk.syntakt --stock "$SYNTAKT_STOCK" --eft "$SYNTAKT_EFT" --system-info \
    --out "out/setup-check-$(date +%Y%m%d%H%M)"
```

The third command builds the core and writes into `out/` (ignored by Git). It
must print `VERIFIED` and then `MAIN RAM: … FREE: N bytes` with N ≥ 0. If it
fails, the message names the missing piece: a variable not set, the wrong OS
file, or a tool that does not run.

## 4. What runs with what

| Check | Needs | Without it |
|---|---|---|
| `python tests/test_units.py` | Python | nothing to skip |
| `node --test "test/*.test.ts"` in `js/` | Node.js | nothing to skip |
| Core `.syx` build | SGDK, `SYNTAKT_STOCK`, `SYNTAKT_EFT` | refused with the missing name |
| SMZ4 bank build (machines and modules) | SGDK; the stock file is optional | refused with the missing name |
| `tests/test_st_fm_recipe.py` | `DIGITONE_OS` | prints `SKIPPED` |
| Common regressions | a build folder and `LLVM_MINGW_BIN`. `SYNFACT_SRC` and a Python with Unicorn add comparisons | prints each skipped comparison |
| Emulator tests | Unicorn; the fixtures in `SYNTAKT_WORK` for some | those needing fixtures are skipped |

When you report a result, say which checks ran and which were skipped.

## 5. Working on a change and opening a pull request

1. **Get the code.** Clone the repository, `git clone https://github.com/GSM1980/synfact-dev.git`,
   and fork it on GitHub if you cannot push to it. From inside your clone,
   `gh repo fork --remote` adds your fork as a remote.
2. **Branch.** `git switch -c <topic>` from `main`. If another agent works on
   the same clone, use a separate worktree
   ([PARALLEL-AGENTS.md](PARALLEL-AGENTS.md)).
3. **Read the rules first.** [AGENTS.md](../AGENTS.md) for coding agents,
   [MACHINES.md](MACHINES.md) for a machine or a module in the bank, and
   [ADAPTING.md](ADAPTING.md) for any mod: its section 6 is the definition of done.
4. **Build and test** as [SYNTAKT-BUILD.md](SYNTAKT-BUILD.md) and
   [TESTS.md](TESTS.md) say. Check the output of each command, not only its
   exit code.
5. **Check that no firmware is staged.** This must print nothing:

   ```bash
   git diff --cached --name-only | grep -E '\.(syx|smz4|bin|elemod|dtmod)$|^out/'
   ```

6. **Commit and push to your fork:**

   ```bash
   git add <the files you changed>
   git commit -m "<what changed and why>"
   git push -u <your fork's remote> <topic>
   ```

7. **Open the pull request** against `main` of the repository:

   ```bash
   gh pr create --base main --title "<title>" --body-file <file with the description>
   ```

   The description says which checks ran, which were skipped and why, and
   whether the device was touched. The answer is always "no": nothing in this
   repository sends MIDI or USB or writes an instrument. Installing a build on
   a unit is the owner's step, with the recovery text the build prints.

Engine changes (`elekloader/`, `js/`) go first to the upstream project,
[elekloader](https://github.com/irpina/elekloader), as
[CONTRIBUTING.md](../CONTRIBUTING.md) explains.

## 6. Safety

- **Your stock file stays on your machine.** Do not upload it or commit it.
- **Keep a recovery path.** The bootloader is never changed; the stock file
  always restores the unit. A bank that stops the unit from booting has a
  recovery procedure in [SYNTAKT-BUILD.md](SYNTAKT-BUILD.md), "The device".
- **Agents do not touch hardware.** Builds, tests and emulator runs are
  offline. Sending a build to a unit is always the owner's action.
