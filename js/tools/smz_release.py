"""Prepare the artifacts a website serves for one release: the SMZ catalog and, optionally, the core package.

From a clean, committed source tree it builds the complete bank in the config's order, checks it with the core's
parser, exports the audited objects (smz_artifacts.py), validates the selections (smz_matrix.py): with --previous
only those containing a module whose artifact changed, the rest reused when every artifact is unchanged
(smz_merge.py --require-complete). With --core-build it also packages a verified core build (syntakt_package.py).
Writes RELEASE.json with the source commit and every pin. Needs the toolchain environment of docs/SYNTAKT-BUILD.md.
Outputs are local and private: never commit them.
"""
import argparse, hashlib, json, os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(__file__).resolve().parent


def run(*cmd, cwd=ROOT):
    print('>', ' '.join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, check=True)


def git(*cmd):
    return subprocess.run(['git', '-C', str(ROOT), *cmd], stdout=subprocess.PIPE, text=True, check=True).stdout.strip()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', type=Path, default=ROOT / 'js/config/syntakt-normal.json')
    p.add_argument('--parser', type=Path, required=True, help="the core's smz-parser.exe, built by the regressions")
    p.add_argument('--out', type=Path, required=True, help='new local directory')
    p.add_argument('--previous', type=Path, help="the previous release's complete catalog, to reuse its unchanged selections")
    p.add_argument('--core-build', type=Path, help='a verified Syntakt core build (verification.json, audio/) to package')
    p.add_argument('--meter', action='store_true', help='the core build used --system-info')
    p.add_argument('--jobs', type=int, default=2)
    p.add_argument('--allow-dirty', action='store_true', help='development only: sources with uncommitted changes')
    a = p.parse_args()
    if os.environ.get('ELEKLOADER_CROSS') is None:
        raise SystemExit('Set the toolchain environment of docs/SYNTAKT-BUILD.md first (ELEKLOADER_CROSS, PATH, GCC_EXEC_PREFIX).')

    config = json.loads(a.config.read_text(encoding='utf-8'))
    mods = config['modules']
    paths = [m['path'] for m in mods] + ['elekloader/sdk', 'js/config'] + (['mods/core-st', 'mods/machines-st'] if a.core_build else [])
    dirty = git('status', '--porcelain', '--', *paths)
    if dirty and not a.allow_dirty:
        raise SystemExit('Commit these sources first; the release must come from a commit:\n' + dirty)
    commit = git('rev-parse', 'HEAD')

    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    bank = out / 'bank' / 'synfact-machines.smz4'
    bank.parent.mkdir()
    # Absolute paths, as smz_matrix.py builds its references: the audited reports key sources by them.
    machines = [str(ROOT / m['path']) for m in mods if m['kind'] == 'machine']
    modules = [str(ROOT / m['path']) for m in mods if m['kind'] == 'module']

    # The complete bank, audited by the SDK, and the core's own parser.
    run(sys.executable, '-m', 'elekloader.sdk.syntakt.smz', *machines, *[x for m in modules for x in ('--module', m)], '--out', bank)
    run(a.parser.resolve(), bank)

    # Its objects, pinned with their sources.
    smz = out / 'smz'
    run(sys.executable, TOOLS / 'smz_artifacts.py', '--bank', bank,
        *[x for m in machines for x in ('--machine', m)], *[x for m in modules for x in ('--module', m)], '--out', smz)
    catalog = json.loads((smz / 'catalog.json').read_text(encoding='utf-8'))
    pins = {m['id']: m['sha256'] for m in catalog['modules']}

    # Validate what changed; reuse what did not.
    changed = list(pins)
    if a.previous:
        old = {m['id']: m['sha256'] for m in json.loads(a.previous.read_text(encoding='utf-8'))['modules']}
        changed = [i for i in pins if old.get(i) != pins[i]]
    print('Modules to validate:', ', '.join(changed) or 'none', flush=True)
    if changed:
        only = [] if not a.previous else [x for i in changed for x in ('--only-id', i)]
        run(sys.executable, TOOLS / 'smz_matrix.py', '--catalog', smz / 'catalog.json', '--config', a.config.resolve(),
            '--source-root', ROOT, '--parser', a.parser.resolve(), '--out', out / 'matrix', '--jobs', a.jobs, *only)
    final = smz / 'catalog-validated.json'
    if a.previous:
        final = smz / 'catalog-complete.json'
        run(sys.executable, TOOLS / 'smz_merge.py', '--catalog', smz / ('catalog-validated.json' if changed else 'catalog.json'),
            '--config', a.config.resolve(), '--reuse', a.previous.resolve(), '--out', final, '--require-complete')

    release = {
        'commit': commit, 'dirty': bool(dirty), 'config': a.config.resolve().relative_to(ROOT).as_posix(),
        'smz': {'catalog': final.relative_to(out).as_posix(), 'sha256': sha(final), 'changed': changed,
                'modules': [{'id': m['id'], 'version': json.loads((smz / m['file']).read_text())['version']} for m in catalog['modules']]},
    }

    # The core, when it changed: package the verified build with the mods it was built from.
    if a.core_build:
        core_doc = json.loads((ROOT / 'mods/core-st/mod.json').read_text(encoding='utf-8-sig'))
        machines_doc = json.loads((ROOT / 'mods/machines-st/mod.json').read_text(encoding='utf-8-sig'))
        run(sys.executable, TOOLS / 'syntakt_package.py', '--reference', a.core_build.resolve(),
            '--core', ROOT / f"mods/core-st/out/core-{core_doc['version']}.elemod",
            '--machines', ROOT / f"mods/machines-st/out/machines-st-{machines_doc['version']}.elemod",
            '--out', out / 'core', *(['--meter'] if a.meter else []))
        release['core'] = {'dir': 'core', 'manifest': 'syntakt.json', 'sha256': sha(out / 'core' / 'syntakt.json'), 'version': core_doc['version']}

    (out / 'RELEASE.json').write_text(json.dumps(release, indent=2) + '\n', encoding='utf-8')
    print(f"Release artifacts in {out}: commit {commit[:12]}{' (uncommitted changes)' if dirty else ''}, "
          f"catalog {release['smz']['sha256'][:12]}" + (f", core {release['core']['version']}" if a.core_build else ''))


if __name__ == '__main__':
    main()
