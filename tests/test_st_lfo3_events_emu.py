"""Differential for linked event ownership, conversion and exact lock copies.

The reference bank executes the old thirteen-lookup converter. Native event
bodies/allocator/mutex are explicit boundaries; no hardware CPU claim is made.
"""
import argparse
import hashlib
import json
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_st_lfo3_publication_emu import Runtime, SCRATCH

PATTERN, MATRIX, ROW = 0x2138c, 0x3bc5, 0x2447
CTX, OLD, OBJECTS, OUT = [SCRATCH+x for x in (0x8000, 0xa000, 0xc000, 0x7000)]
EVENTS, LOCK, STATIC = SCRATCH+0x4000, 0x44000000, SCRATCH+0x6000


def configure(r, patterns, objects=(), temporary=(), owned=True, legacy=True):
    r.u.mem_write(CTX, bytes(0x2000))
    r.u.mem_write(OLD, bytes(0x1000))
    r.u.mem_write(OBJECTS, bytes(0x4000))
    r.write(r.sym['lfo3_owned_context'], CTX if owned else 0)
    r.write(r.sym['lfo3_pattern_context'], OLD if legacy else 0)
    r.write(CTX, patterns)
    r.write(OLD, patterns)
    for i, (base, size, count, kind) in enumerate(objects):
        at = OBJECTS+i*0x800
        r.u.mem_write(at, struct.pack('>5I', at+0x800 if i+1<len(objects) else 0,
                                     base, size, count, kind))
    r.write(CTX+8, OBJECTS if objects else 0)
    for i, matrix in enumerate(temporary):
        r.write(OLD+1032+12*i, matrix)


def lookup_cases(old, new):
    rng = random.Random(67109)
    checks = 0

    def check(address):
        nonlocal checks
        address &= 0xffffffff
        expected, track = 0, 0xfeedface
        for t in range(13):
            expected = old.call('lfo3_owned_lookup', [(address-t*ROW)&0xffffffff])[0]
            if expected:
                track = t
                break
        new.write(OUT, 0xfeedface)
        actual = new.call('lfo3_owned_row_lookup', [address, OUT])[0]
        assert (actual, int.from_bytes(new.read(OUT, 4), 'big')) == (expected, track), hex(address)
        checks += 1

    layouts = [
        (0x40a38b34, [], [], True, True),
        (0x40a38b34, [(0x45000000, PATTERN*3, 3, 1),
                      (0x46000000, 141756, 1, 1),
                      (0x46100000, 10468, 1, 2)], [], True, True),
        # A temporary or an overlapping owner can win at a lower track than
        # the live row. Equal-track ties keep the old list/temporary order.
        (0x41000000, [(0x41000000+2*ROW, PATTERN, 1, 1),
                      (0x41000000+ROW, PATTERN*2, 2, 1)],
         [0x41000000+MATRIX+3*ROW, 0x41000000+MATRIX+3*ROW], True, True),
        (0x41000000, [], [0x46000000, 0x46000000+ROW], False, True),
        (0, [], [0x46000000], False, True),
        (0x41000000, [], [], True, False),
        (0, [], [], False, False),
        # Exact unsigned candidate subtraction, including matrix zero and
        # rows crossing 2^32. Stores are fixture metadata, not dereferenced OS.
        (0xffff0000, [(0xffff1000, PATTERN*2, 2, 1)],
         [0xfffffff0, 0xffff0000, 0], True, True),
    ]
    for patterns, objects, temporary, owned, legacy in layouts:
        for r in (old, new):
            configure(r, patterns, objects, temporary, owned, legacy)
        addresses = [0, 1, MATRIX, 0xffffffff, 0xfffffff0]
        bases = [patterns+MATRIX] if patterns else []
        for base, size, count, kind in objects:
            bases += [base+(20 if kind==1 and size==141756 else 0)+MATRIX,
                      base+0x49d]  # TRACK must not be accepted as a matrix owner.
        bases += temporary
        for base in bases:
            for i in (0, 1, 127, 128):
                for t in range(13):
                    addresses.append(base+i*PATTERN+t*ROW)
            addresses += [base-1, base+1, base+12*ROW+1, base+13*ROW]
        addresses += [rng.randrange(0x100000000) for _ in range(40)]
        for address in dict.fromkeys(addresses):
            check(address)
    return checks


class Events(Runtime):
    def code(self, u, at, size, user):
        if at == self.sym.get('lfo3_output_track'):
            self.counts['output_updates'] += 1
        name = self.native.get(at, '')
        if name.startswith('event_'):
            self.instructions += 1
            self.counts[name] += 1
            self.ret(0xa1234567)
        else:
            super().code(u, at, size, user)


def event_cases(native, before, after):
    old, new = [Events(native, p) for p in (before, after)]
    rows = []
    for r in (old, new):
        assert r.call('lfo3_locks_setup', [])[0] == 1
        r.u.mem_map(LOCK, 0x1000000)
        r.write(0x444db730, LOCK)
        r.write(r.sym['lfo3_events_context'], EVENTS)
        r.call('lfo3_events_init', [EVENTS, 71])
        for name in ('apply', 'convert', 'clear', 'restore', 'fx_restore'):
            r.native[r.sym['lfo3_events_native_'+name]] = 'event_'+name

    def check(label, name, args, cost=True):
        a, ac = old.call(name, args)
        b, bc = new.call(name, args)
        assert a == b, label
        assert old.read(EVENTS, 2776) == new.read(EVENTS, 2776), label
        assert old.read(OUT, 96) == new.read(OUT, 96), label
        assert old.read(old.sym['lfo3_active_locks'], 4) == new.read(new.sym['lfo3_active_locks'], 4), label
        assert old.read(old.local['frame'], 282) == new.read(new.local['frame'], 282), label
        assert {k:v for k,v in ac.items() if k not in ('instructions','output_updates')} == {
            k:v for k,v in bc.items() if k not in ('instructions','output_updates')}, label
        if cost:
            updates=bc.get('output_updates',0)
            # 1.7 moves output eligibility to event publication. Charge that
            # bounded cold operation separately; empty/unrelated paths retain
            # the original non-increase gate. This fixture has no host/router;
            # the real first/last route update and amortization are measured by
            # test_st_lfo3_activity_emu, including the actual resident core.
            assert bc['instructions'] <= ac['instructions']+128*updates, (label, ac, bc)
        rows.append(dict(case=label, before=ac['instructions'], after=bc['instructions'],
                         cold_output_updates=bc.get('output_updates',0)))

    # Every live track, with real lane storage and non-neutral LFO3/ARP values.
    for r in (old, new):
        store = r.call('lfo3_owned_lookup', [0x40a38b34+MATRIX])[0]
        for t in range(13):
            for p in (0, 3, 8, 15):
                value = 0x100 if p==3 else 0x1234+t+p
                assert r.call('lfo3_owned_set', [store, t, 3, p, value, 0])[0] == 1
        r.irq = 0x500
    for t in range(13):
        for step in (3, 4, 63, 64):
            check(f'convert-live-{t}-{step}', 'lfo3_events_convert',
                  [LOCK, 0x40a38b34+MATRIX+t*ROW, 0, 0, step, 0])
    owners = []
    for r in (old, new):
        r.irq = 0
        base = r.call('lfo3_owned_allocate', [PATTERN])[0]
        assert base
        owners.append(base)
        store = r.call('lfo3_owned_lookup', [base+MATRIX])[0]
        assert store
        for t in range(13):
            assert r.call('lfo3_owned_set', [store, t, 3, 8, t+1, 0])[0] == 1
        r.irq = 0x500
    assert owners[0] == owners[1]
    for t in range(13):
        check(f'convert-owned-{t}', 'lfo3_events_convert',
              [LOCK, owners[0]+MATRIX+t*ROW, 0, 0, 3, 0])
    for t in range(13):
        for r in (old, new):
            r.write(0x444e3884+4*t, STATIC+4*t)
        check(f'convert-static-{t}', 'lfo3_events_convert',
              [STATIC+4*t, 0x40a38b34+MATRIX+t*ROW, 0, 0, 3, 0])
    for r in (old, new):
        r.u.mem_write(0x444e3884, bytes(13*4))
    # Queued and static PLAY-start locks; all active payload words must remain
    # exact even if mask zero. ARP output may be halfword aligned or overlap.
    for static in (False, True):
        lock = STATIC if static else LOCK
        for r in (old, new):
            if static:
                r.write(0x444e3884+12*4, STATIC)
        for t in (0, 7, 11, 12, 13):
            for step in (3, 4):
                for r in (old, new):
                    r.call('lfo3_events_capture', [lock, 0x40a38b34, t, step])
                    r.u.mem_write(OUT, b'\x9c'*96)
                check(f'apply-{static}-{t}-{step}', 'lfo3_events_apply', [t, lock, 0])
                for address in (OUT, OUT+2):
                    check(f'arp-{static}-{t}-{step}-{address&3}', 'synfact_extra_locks', [t, lock, address])
                if t<12:
                    check(f'arp-active-{static}-{t}-{step}', 'synfact_extra_locks', [t, 0xffffffff, OUT])
                check(f'clear-{static}-{t}-{step}', 'lfo3_events_clear', [t])
    for lock in (0, LOCK+1, LOCK+64*588, STATIC+4):
        check('apply-invalid-'+hex(lock), 'lfo3_events_apply', [0, lock, 0])
        check('arp-invalid-'+hex(lock), 'synfact_extra_locks', [0, lock, OUT])
        check('convert-invalid-'+hex(lock), 'lfo3_events_convert', [lock, 0, 0, 0, 3, 0])
    for offset in (0, 2, 14, 16, 18, 28):
        for r in (old, new):
            r.u.mem_write(EVENTS+4, struct.pack('>I16H', 0xaaaa, *range(16)))
        address = EVENTS+8+offset
        check('arp-overlap-'+str(offset), 'synfact_extra_locks', [0, LOCK, address])
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('core', 'before', 'after', 'out'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    native = (a.core/'main.bin').read_bytes()
    old, new = [Runtime(native, path) for path in (a.before, a.after)]
    cases = lookup_cases(old, new)
    rows = event_cases(native, a.before, a.after)
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    report = dict(ok=True, ownership_cases=cases, event_cases=rows,
                  before_sha256=sha(a.before), after_sha256=sha(a.after),
                  limits=['Native event bodies/allocator/mutex are explicit boundaries.',
                          'Instruction counts are per call, not hardware CPU percentages.'],
                  device_access=False)
    a.out.write_text(json.dumps(report, indent=2)+'\n')
    print(f'PASS: {cases} ownership cases; {len(rows)} exact event/copy cases; no device access')


if __name__ == '__main__':
    main()
