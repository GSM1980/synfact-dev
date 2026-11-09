"""Generate the changelogs of the Syntakt core and mods from git history.

    python tools/changelog.py

For each Syntakt mod (a folder under mods/ with a mod.json whose device is
"syntakt", and the core mods/core-st) this writes:

    mods/<id>/CHANGELOG.md   readable, newest version first
    mods/<id>/changelog.json the same data for the website

and one index, changelog/index.json, with the latest version of every mod.

A commit belongs to the version its mod.json names at that commit. Versions are
listed in semantic order, not in commit order, because branches are merged in
any order. Entries come from commit subjects only; merge commits are skipped.
The output is derived from git, so run the script again after new commits.
"""
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True,
                          encoding='utf-8', check=True).stdout


def version_key(text):
    """Sort key for dotted versions such as 0.12.18 or 1.7.1."""
    return tuple(int(part) if part.isdigit() else -1 for part in re.split(r'[.\-]', text))


def syntakt_mods():
    """(folder, manifest) for every Syntakt mod in the repository."""
    found = []
    for manifest in sorted(ROOT.glob('mods/*/mod.json')):
        data = json.loads(manifest.read_text(encoding='utf-8'))
        if data.get('device') == 'syntakt' or manifest.parent.name == 'core-st':
            found.append((manifest.parent, data))
    return found


def manifest_at(commit, folder):
    """The parsed mod.json of a commit, or None if the file is not there."""
    rel = (folder / 'mod.json').relative_to(ROOT).as_posix()
    try:
        return json.loads(git('show', f'{commit}:{rel}'))
    except (subprocess.CalledProcessError, json.JSONDecodeError):
        return None


def commits_of(folder):
    """Non-merge commits that touch the folder, oldest first."""
    rel = folder.relative_to(ROOT).as_posix()
    rows = git('log', '--no-merges', '--reverse', '--format=%H%x1f%ad%x1f%s',
               '--date=short', '--', rel).splitlines()
    result = []
    for row in rows:
        sha, date, subject = row.split('\x1f', 2)
        result.append({'sha': sha, 'short': sha[:7], 'date': date, 'subject': subject})
    return result


def build(folder, manifest):
    commits = commits_of(folder)
    versions = {}
    pending = []
    for commit in commits:
        info = manifest_at(commit['sha'], folder)
        if info is None or 'version' not in info:
            pending.append(commit)
            continue
        versions.setdefault(info['version'], []).append(commit)
    # Commits from before the first manifest belong to the earliest version.
    if pending and versions:
        earliest = min(versions, key=version_key)
        versions[earliest] = pending + versions[earliest]
    entries = []
    for version in sorted(versions, key=version_key, reverse=True):
        items = sorted(versions[version], key=lambda c: c['date'], reverse=True)
        entries.append({
            'version': version,
            'date': min(c['date'] for c in items),
            'commits': [{'sha': c['short'], 'date': c['date'], 'subject': c['subject']} for c in items],
        })
    return {
        'id': manifest['id'],
        'title': manifest.get('title', manifest['id']),
        'latest': entries[0]['version'] if entries else manifest.get('version'),
        'entries': entries,
    }


def write_markdown(data, path):
    lines = [f"# Changelog: {data['title']}", '',
             f"Generated from git history by `tools/changelog.py`. Latest version: {data['latest']}.", '']
    for entry in data['entries']:
        lines.append(f"## {entry['version']} ({entry['date']})")
        lines.append('')
        for commit in entry['commits']:
            lines.append(f"- {commit['subject']} (`{commit['sha']}`, {commit['date']})")
        lines.append('')
    path.write_text('\n'.join(lines), encoding='utf-8')


def main():
    index = {'schema': 1, 'mods': []}
    for folder, manifest in syntakt_mods():
        data = build(folder, manifest)
        data['slug'] = folder.name
        (folder / 'changelog.json').write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
        write_markdown(data, folder / 'CHANGELOG.md')
        index['mods'].append({'slug': folder.name, 'id': data['id'], 'title': data['title'], 'latest': data['latest'],
                              'path': folder.relative_to(ROOT).as_posix()})
        print(f"{data['id']}: {len(data['entries'])} versions")
    out = ROOT / 'changelog'
    out.mkdir(exist_ok=True)
    (out / 'index.json').write_text(json.dumps(index, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
