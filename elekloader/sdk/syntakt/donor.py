# SPDX-License-Identifier: GPL-2.0-or-later
"""Data a mod takes from another instrument's OS file, at build time.

FM TONE's DSP, tables and drawings come from the Digitone's OS. This
repository cannot hold those bytes, so it holds a recipe instead: the regions
of the OS file to read, each pinned by its sha256, and for each output a list
of operations that rebuild it from them:

  ["copy", region, offset, length]   bytes from a region
  ["fill", pattern, count]           a pattern of 1, 2 or 4 bytes (hex), repeated
  ["data", hex]                      our own bytes

The owner supplies their own OS file: `rebuild` checks it and every output's
sha256. `make` writes a recipe from the outputs and an OS file (the
maintainers' step). At every position it copies the longest run a region
holds, so no `minimum` bytes in a "data" operation appear in any region.
"""
import hashlib
import json

from ... import devices, formats

FORMAT = 'synfact-donor'
SCHEMA = 1
FILL_RUN = 16          # a repeated pattern this long becomes a "fill"
CANDIDATES = 4096      # copies tried per position, for the commonest runs


class DonorError(ValueError):
    pass


def sha(data):
    return hashlib.sha256(data).hexdigest()


def regions(recipe, raw):
    """The recipe's regions, read from the owner's OS file `raw` (bytes) and
    checked. -> {name: bytes}."""
    try:
        parsed, dev, rel = formats.load(raw)
    except (devices.UnknownFirmware, formats.FormatError) as e:
        raise DonorError('%s needs %s; this file is not one (%s)' % (recipe['title'], wanted(recipe), e))
    if dev.key != recipe['device'] or rel.version not in recipe['versions']:
        raise DonorError('%s needs %s; this file is %s %s' % (recipe['title'], wanted(recipe), dev.name, rel.version))
    out = {}
    for r in recipe['regions']:
        data = parsed.section(r['section'])[r['start']:r['start'] + r['length']]
        if len(data) != r['length'] or sha(data) != r['sha256']:
            raise DonorError('%s %s: section %d is not the one %s was made from'
                             % (dev.name, rel.version, r['section'], recipe['title']))
        out[r['name']] = data
    return out


def wanted(recipe):
    """-> 'the <device> OS <versions> file' a recipe reads."""
    name = next((d.name for d in devices.devices() if d.key == recipe['device']), recipe['device'])
    return 'the %s OS %s file' % (name, ' or '.join(recipe['versions']))


def apply(ops, sources):
    """Operations -> bytes. Refuses an operation it cannot follow exactly."""
    out = bytearray()
    for op in ops:
        kind = op[0] if isinstance(op, list) and op else None
        if kind == 'copy' and len(op) == 4:
            _, name, at, n = op
            src = sources.get(name)
            if src is None or not (isinstance(at, int) and isinstance(n, int)) or at < 0 or n <= 0 or at + n > len(src):
                raise DonorError('copy outside its region: %r' % (op,))
            out += src[at:at + n]
        elif kind == 'fill' and len(op) == 3:
            pattern, n = bytes.fromhex(op[1]), op[2]
            if len(pattern) not in (1, 2, 4) or not isinstance(n, int) or n <= 0:
                raise DonorError('bad fill: %r' % (op,))
            out += pattern * n
        elif kind == 'data' and len(op) == 2:
            out += bytes.fromhex(op[1])
        else:
            raise DonorError('unknown operation: %r' % (op,))
    return bytes(out)


def rebuild(recipe, raw):
    """The owner's OS file -> {output name: bytes}, each checked."""
    if recipe.get('format') != FORMAT or recipe.get('schema') != SCHEMA:
        raise DonorError('not a %s recipe of schema %d' % (FORMAT, SCHEMA))
    sources = regions(recipe, raw)
    out = {}
    for o in recipe['outputs']:
        data = apply(o['ops'], sources)
        if len(data) != o['length'] or sha(data) != o['sha256']:
            raise DonorError('%s: rebuilt %s differs from the one the recipe was made for' % (recipe['title'], o['name']))
        out[o['name']] = data
    return out


def _common(a, ai, b, bi, limit):
    n = 0
    while n < limit:
        k = min(64, limit - n)
        if a[ai + n:ai + n + k] == b[bi + n:bi + n + k]:
            n += k
            continue
        while a[ai + n] == b[bi + n]:
            n += 1
        return n
    return n


def _fill(target, i):
    for p in (1, 2, 4):
        pattern = target[i:i + p]
        if len(pattern) < p:
            break
        n = 1
        while target[i + n * p:i + (n + 1) * p] == pattern:
            n += 1
        if n * p >= FILL_RUN:
            return pattern, n
    return None


def ops(target, sources, minimum=4):
    """`target` -> operations over `sources` ({name: bytes})."""
    index = {}
    for name, data in sources.items():
        for i in range(len(data) - minimum + 1):
            index.setdefault(data[i:i + minimum], []).append((name, i))
    out = []

    def data(b):
        if out and out[-1][0] == 'data':
            out[-1][1] += b.hex()
        else:
            out.append(['data', b.hex()])
    i = 0
    while i < len(target):
        fill = _fill(target, i)
        if fill:
            out.append(['fill', fill[0].hex(), fill[1]])
            i += len(fill[0]) * fill[1]
            continue
        best = None
        for name, at in index.get(target[i:i + minimum], ())[:CANDIDATES]:
            src = sources[name]
            n = _common(target, i, src, at, min(len(target) - i, len(src) - at))
            if best is None or n > best[2]:
                best = (name, at, n)
                if i + n == len(target):
                    break
        if best and best[2] >= minimum:
            out.append(['copy', best[0], best[1], best[2]])
            i += best[2]
        else:
            data(target[i:i + 1])
            i += 1
    return out


def spec(recipe):
    """What a build needs to read an owner's file for a recipe: no outputs."""
    return {k: recipe[k] for k in ('title', 'device', 'versions', 'minimum', 'regions')}


def fills(data, sources, minimum=4):
    """A compiled section's bytes -> (the bytes with the regions' runs blanked to
    zero, the copies that put them back: [at, region, offset, length]). Runs of
    one repeated byte stay: they are nobody's. Refuses a result whose own bytes
    hold `minimum` bytes in a row of a region (literal_runs)."""
    blanked, copies, at = bytearray(data), [], 0
    section_ops = ops(data, sources, minimum)
    if literal_runs(section_ops, sources, minimum):
        raise DonorError('a run the regions hold was left in place')
    for op in section_ops:
        if op[0] == 'copy':
            n = op[3]
            if len(set(data[at:at + n])) > 1:
                copies.append([at, op[1], op[2], n])
                blanked[at:at + n] = bytes(n)
            at += n
        elif op[0] == 'fill':
            at += len(op[1]) // 2 * op[2]
        else:
            at += len(op[1]) // 2
    return bytes(blanked), copies


def unfill(data, copies, sources):
    """The blanked bytes and their copies -> the section's bytes."""
    out = bytearray(data)
    for c in copies:
        if not (isinstance(c, list) and len(c) == 4):
            raise DonorError('bad copy: %r' % (c,))
        at, name, offset, n = c
        src = sources.get(name)
        if (src is None or not all(isinstance(v, int) for v in (at, offset, n)) or n <= 0 or at < 0 or offset < 0
                or at + n > len(out) or offset + n > len(src) or any(out[at:at + n])):
            raise DonorError('copy outside its section or region: %r' % (c,))
        out[at:at + n] = src[offset:offset + n]
    return bytes(out)


def literal_runs(recipe_ops, sources, minimum=4):
    """-> the "data" bytes of `recipe_ops` that a region holds `minimum` of in a
    row: none, for a recipe `ops` made. A check, for tests and reviews."""
    found = []
    for op in recipe_ops:
        if op[0] != 'data':
            continue
        b = bytes.fromhex(op[1])
        for i in range(len(b) - minimum + 1):
            if any(b[i:i + minimum] in src for src in sources.values()):
                found.append(b[i:i + minimum].hex())
    return found


def make(title, raw, device, versions, region_specs, outputs, minimum=4):
    """The maintainers' step: outputs ({name: bytes}) and an OS file -> a recipe."""
    parsed, dev, rel = formats.load(raw)
    if dev.key != device or rel.version not in versions:
        raise DonorError('expected %s %s, got %s %s' % (device, versions, dev.key, rel.version))
    specs, sources = [], {}
    for r in region_specs:
        data = parsed.section(r['section'])[r['start']:r['start'] + r['length']]
        if len(data) != r['length']:
            raise DonorError('region %s is outside section %d' % (r['name'], r['section']))
        specs.append(dict(r, sha256=sha(data)))
        sources[r['name']] = data
    recipe = {'format': FORMAT, 'schema': SCHEMA, 'title': title, 'device': device,
              'versions': list(versions), 'minimum': minimum, 'regions': specs, 'outputs': []}
    for name, data in outputs.items():
        recipe['outputs'].append({'name': name, 'length': len(data), 'sha256': sha(data),
                                  'ops': ops(data, sources, minimum)})
    return recipe


def dumps(recipe):
    """A recipe as text: one operation per line, so a change reads as a diff."""
    head = {k: v for k, v in recipe.items() if k != 'outputs'}
    lines = ['{', '  ' + json.dumps(head)[1:-1] + ',', '  "outputs": [']
    for k, o in enumerate(recipe['outputs']):
        meta = {key: v for key, v in o.items() if key != 'ops'}
        lines.append('    {' + json.dumps(meta)[1:-1] + ', "ops": [')
        lines += ['      ' + json.dumps(op) + (',' if j + 1 < len(o['ops']) else '') for j, op in enumerate(o['ops'])]
        lines.append('    ]}' + (',' if k + 1 < len(recipe['outputs']) else ''))
    lines += ['  ]', '}']
    return '\n'.join(lines) + '\n'
