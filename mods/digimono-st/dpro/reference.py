# SPDX-License-Identifier: AGPL-3.0-or-later
"""Pinned De-Pattern DigiPRO interpreter (DPRO-WAVE, DPRO-DDRW, DPRO-DENS).

Runs the unmodified upstream DSL (dpro/upstream and the shared files already
pinned in fmplus/upstream) in run mode, with every source hash checked first.
The owner's Monomachine OS 1.32B supplies the tables; nothing of it is stored.

Voice(os_file, kind).block(pitch, controls, trig) is one 16-frame block at
44.1 kHz, in Q23 floats, for the same pitch and controls the C port receives.
"""
import ast
import hashlib
import json
from pathlib import Path
import sys
import types

HERE = Path(__file__).resolve().parent
FM = HERE.parent / 'fmplus'
PREFIX = '_dm_dpro'
KINDS = {12: 'wave', 13: 'ddrw', 14: 'dens'}


def verify():
    manifest = json.loads((HERE / 'provenance.json').read_text())
    for name, digest in manifest['sha256'].items():
        assert hashlib.sha256((HERE / 'upstream' / name).read_bytes()).hexdigest() == digest, name
    for name, digest in manifest['shared_sha256'].items():
        assert hashlib.sha256((FM / 'upstream' / name).read_bytes()).hexdigest() == digest, name
    return manifest


_CACHE = {}


def load(os_file, oversampling=None):
    """The upstream modules, built once per OS file; dpro.OS optionally overridden
    (1: De-Pattern's shipped DDRW/DENS, 2: the Monomachine's own)."""
    key = (str(os_file), oversampling)
    if key in _CACHE:
        return _CACHE[key]
    verify()
    for name in list(sys.modules):
        if name == PREFIX or name.startswith(PREFIX + '.'):
            del sys.modules[name]
    for name in (PREFIX, PREFIX + '.mnm', PREFIX + '.md', PREFIX + '.perc'):
        m = types.ModuleType(name); m.__path__ = []; sys.modules[name] = m

    def module(name, file, names=None, values=None, where=None):
        full = PREFIX + '.' + name
        m = types.ModuleType(full)
        m.__file__ = str((where or HERE / 'upstream') / file)
        m.__package__ = full.rsplit('.', 1)[0]
        sys.modules[full] = m
        setattr(sys.modules[m.__package__], full.rsplit('.', 1)[1], m)
        if values:
            m.__dict__.update(values)
        source = Path(m.__file__).read_text()
        if names is None:
            exec(compile(source, m.__file__, 'exec'), m.__dict__)
        else:
            tree = ast.parse(source)
            tree.body = [n for n in tree.body
                         if isinstance(n, (ast.FunctionDef, ast.ClassDef)) and n.name in names]
            exec(compile(tree, m.__file__, 'exec'), m.__dict__)
        return m

    shared = FM / 'upstream'
    dsl = module('perc.dsl', 'dsl.py', where=shared)
    decoder = module('md.rom', 'md_rom.py', {'nrv2b', 'RomError'}, where=shared)
    rom = module('mnm.rom', 'mnm_rom.py', where=shared)
    rom.path = lambda: Path(os_file or 'unused.syx')
    K = module('md.core', 'md_core.py', {'sat', 'w', 's', 'ss', 'ws'},
               dict(TOP=1 - 2 ** -23, Q23=2 ** -23, OUT0=195, S0=64, W0=0, Z_N=1,
                    MD_STRIDE=1024, Prog=dsl.Prog), where=shared)
    module('perc.machines', 'perc_machines.py', {'_recip_inline', 'recip', '_io'},
           dict(Prog=dsl.Prog))
    C = module('mnm.core', 'mnm_core.py', where=shared)
    X = module('mnm.exact', 'exact.py', where=shared)
    module('mnm.dec', 'dec.py')
    D = module('mnm.dpro', 'dpro.py')
    if oversampling is not None:
        D.OS = oversampling
        D.N2 = 16 * oversampling
        D.PHASE_SH = 3 if oversampling == 2 else 2
    wave = module('mnm.wave', 'wave.py')
    ddrw = module('mnm.ddrw', 'ddrw.py')
    dens = module('mnm.dens', 'dens.py')
    out = types.SimpleNamespace(dsl=dsl, rom=rom, K=K, C=C, X=X, D=D, wave=wave, ddrw=ddrw, dens=dens)
    _CACHE[key] = out
    return out


def layout(m):
    md = types.SimpleNamespace(state=0x100000, C={'mdb': 0x80000, 'fill': 0x80004})
    return m.C.Layout(types.SimpleNamespace(md=md), 0x200000, 0x400000)


def digibank(m, cache=None):
    """The Digibank stand-in exactly as De-Pattern's DSP builds it (dpro.build run
    to completion): 32 x 2048 24-bit words. Cached by source and OS hashes."""
    if cache is not None and Path(cache).is_file():
        import struct
        raw = Path(cache).read_bytes()
        return struct.unpack(f'<{len(raw) // 4}I', raw)
    dsl, C, D = m.dsl, m.C, m.D
    L = layout(m)
    mem = {}
    words = m.rom.wave_samples()
    for k, v in enumerate(words):
        mem[L.tables + 4 * k] = dsl.bits(v * C.Q23)
    at = 0
    dp = m.rom.dpro_words()
    for name, n in C.DP_TABLES:
        for i in range(n):
            mem[L.DT_SRC[name] + 4 * i] = dsl.bits(dp[at + i] * C.Q23)
        at += n
    mem[L.md.C['fill']] = 1
    M = dsl.Mem(lambda a: mem.get(a, 0))
    p = dsl.Prog('run', mem=M)
    while M.get(L.DG['w']) < 32:
        D.build(p, L)
    out = tuple(M.get(L.DIGI + 4 * k) & 0xFFFFFF for k in range(C.DIGI_WORDS))
    if cache is not None:
        import struct
        Path(cache).parent.mkdir(parents=True, exist_ok=True)
        Path(cache).write_bytes(struct.pack(f'<{len(out)}I', *out))
    return out


DDRW_BR = (4, 6)


def ddrw_host(m, controls):
    """De-Pattern's DN2 host mapping for DDRW (upstream machines._host_ddrw, v0.56),
    in the same float32 operations: BR1/BR2 1..127 to 64..127 (whole knobs), and
    the glide word from TIME. Returns (controls, glide)."""
    up = sys.modules[PREFIX + '.perc.machines']
    c = list(controls)
    for i in DDRW_BR:
        x = c[i] >> 8
        if x:
            c[i] = (64 + ((63 * x + 63) >> 7)) << 8
    t = controls[3] >> 8
    if not t:
        return c, 0.0
    p = m.dsl.Prog('run', mem=m.dsl.Mem(lambda a: 0))
    fade = 6000.0 / (127 * 127)
    rate = 0x21D10 / 2.0 ** 23 * 2048.0
    p.fconst(1, float(t))
    p.fop(2, 1, '*', 1)
    p.fconst(12, fade)
    p.fop(2, 2, '*', 12)
    p.fconst(12, 1.0)
    p.fop(2, 2, '+', 12)
    p.fconst(12, rate)
    p.fop(2, 2, '*', 12)
    up.recip(p, 3, 2)
    p.fconst(12, 1.0)
    p.fop(1, 1, '+', 12)
    p.fop(3, 3, '*', 1)
    return c, p.F(3)


def ddrw_rate(m, time):
    """DDRW's fade rate a block for TIME (0..127) with the host's glide word,
    as upstream ddrw._half computes it in float32 (one value, Q23 float)."""
    up = sys.modules[PREFIX + '.perc.machines']
    C = m.C
    _, gl = ddrw_host(m, [0, 0, 0, time << 8, 0, 0, 0, 0])
    p = m.dsl.Prog('run', mem=m.dsl.Mem(lambda a: 0))
    p.iconst(1, time + 1)
    p.iconst(12, 0)
    p.float_(3, 1, 12)
    up.recip(p, 3, 3)
    p.fconst(2, gl)
    if gl == 0.0:
        p.fconst(2, C.TOP)
    if time == 0:
        p.fconst(2, 0x7EB852 * C.Q23)
    p.fop(2, 2, '*', 3)
    p.fconst(12, 0x21D10 * C.Q23 * 2048.0)
    p.fop(2, 2, '*', 12)
    m.K.sat(p, 2)
    return p.F(2)


def r48(x):
    """x x 147/160 (44100/48000), floored as dpro.c's R48"""
    return (x // 160) * 147 + ((x % 160) * 147) // 160


def sync_period48(m, F):
    """exact.sync_period with 48,000 blocks of 16 a second (dpro.c at 48 kHz)"""
    X = m.X
    a = X.exp_word(X.acc(F))
    a = X.mpy(0x20B40, X.lim(a)) << 12
    x0 = X.lim(X.s56(a))
    return (0xBB80 << 12) // x0 if x0 else 0


def increments48(m, kind, A, F, knobs, pre):
    """The machine's exact increments at 48 kHz blocks: scaled by 147/160,
    DENS's mutes (in Hz) unchanged, WAVE's sync period in 48 kHz blocks."""
    M = getattr(m, KINDS[kind])
    d = dict(M.exact(A, F, knobs, pre))
    for name in ('i1', 'i2', 'i3', 'i4'):
        if name in d and not (kind == 12 and name == 'i2'):
            d[name] = r48(d[name])
    if kind == 12:
        sync, sfrq = knobs[4] << 16, knobs[5] << 16
        d['i2'] = (0 if sync < 0x2AAAAA else sync_period48(m, pre) if sync >= 0x555555
                   else 0 if sfrq == 0 else sync_period48(m, 52 * knobs[5] + 0x1000))
    return d


class Voice:
    """One machine on the pinned DSL, with the Monomachine's exact increments."""

    def __init__(self, os_file, kind, oversampling=None, bank=None):
        self.m = m = load(os_file, oversampling)
        self.kind = kind
        self.M = getattr(m, KINDS[kind])
        self.L = L = layout(m)
        dsl, C = m.dsl, m.C
        values = {}
        for k, v in enumerate(m.rom.wave_samples()):
            values[L.tables + 4 * k] = dsl.bits(v * C.Q23)
        at = 0
        dp = m.rom.dpro_words()
        for name, n in C.DP_TABLES:
            for i in range(n):
                values[L.DT[name] + 4 * i] = dsl.bits(dp[at + i] * C.Q23)
            at += n
        if kind != 12:
            for k, v in enumerate(bank if bank is not None else digibank(m)):
                values[L.DIGI + 4 * k] = v
            values[L.DG['w']] = 32
        self.mem = dsl.Mem(lambda a: values.get(a, 0))
        self.first = True

    def block(self, F, controls, trig=False, pre=0, glide=None, rate48=False):
        """F: the pitch word (0..0x5800); controls: eight 16-bit values (knob << 8,
        TUNE last). glide: DDRW's gl word as a float, or None (the model's 0)."""
        m, L = self.m, self.L
        dsl, C, K = m.dsl, m.C, m.K
        p = dsl.Prog('run', mem=self.mem)
        p.i[3] = L.md.state
        self.mem.put(L.md.C['mdb'], L.md.state)
        for k, x in enumerate(controls):
            p.fconst(0, x / 32768)
            p.sti(C.P0 + k, 3, 0)
        knobs = [min(127, x >> 8) for x in controls]
        A = m.X.increment(F)
        p.fconst(0, A / 2.0 ** 24)
        C.vws(p, 'A', 0)
        incs = increments48(m, self.kind, A, F, knobs, pre) if rate48 else self.M.exact(A, F, knobs, pre)
        if rate48 and glide:
            glide = glide * 147 / 160
        for name, v in incs.items():
            p.iconst(0, v)
            C.vws(p, name, 0)
        if self.kind == 13:
            p.fconst(0, glide or 0.0)
            p.st(L.W['gl'], 0)
        if self.first:
            self.M.init(p, L)
            self.first = False
        if trig:
            self.M.trig(p, L)
        out = L.md.state + 4 * K.OUT0
        p.iconst(0, out)
        p.seti(4, 0)
        self.M.half(p, L)
        return [dsl.flt(self.mem.get(out + 4 * k)) for k in range(16)]
