"""Before/after FM feedback guard: native x2 parity and unchanged no-feedback.

Uses actual compiled banks. Diagnostic x2 is only selected in emulator memory.
CPU cost is counted as emulated instructions for one steady update/render,
not hardware cycles or a device CPU percentage. No bank/device writes.
"""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn import UC_HOOK_CODE
from test_st_fm_sweep import Fixture, difference


def count_block(engine, note):
    count = [0]
    def instruction(uc, address, size, context):
        count[0] += 1
    hook = engine.uc.hook_add(UC_HOOK_CODE, instruction)
    try:
        engine.uc.ctl_flush_tb()  # cached blocks predate the newly installed hook
        engine.block(note=note)
    finally:
        engine.uc.hook_del(hook)
    assert count[0] > 100, 'Instruction hooks did not execute'
    return count[0]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('before', 'after', 'stock', 'digitone-audio', 'digitone-main', 'out'):
        p.add_argument('--'+name, required=True, type=Path)
    p.add_argument('--recount', action='store_true', help='Refresh instruction counts in an existing audio report')
    p.add_argument('--short', action='store_true', help='64 blocks per case; use default for the 500ms C4 test')
    a = p.parse_args()
    before = Fixture(a.before.resolve(), a.stock, a.digitone_audio, a.digitone_main)
    after = Fixture(a.after.resolve(), a.stock, a.digitone_audio, a.digitone_main)
    if a.recount:
        result = json.loads(a.out.read_text())
        assert result['before_sha256'] == hashlib.sha256(before.body).hexdigest()
        assert result['after_sha256'] == hashlib.sha256(after.body).hexdigest()
        for row in result['cases']:
            cfg = before.settings(row['algorithm'], 'modulated', 'FDBK', row['feedback'])
            old, new = before.engines(cfg, row['note']), after.engines(cfg, row['note'])
            engines = old[:2]+new[:2]
            for block in range(64):
                for e in engines:
                    e.block(note=row['note'], trigger=block == 0)
            counts = [count_block(e, row['note']) for e in engines]
            row['instruction_counts'] = dict(zip(('old_x1', 'old_x2', 'fixed_x1', 'fixed_x2'), counts))
            row['instruction_count_warmup_blocks'] = 64
        a.out.write_text(json.dumps(result, indent=2)+'\n')
        print('PASS nonzero instruction counts refreshed for', len(result['cases']), 'cases')
        return
    result = dict(before_sha256=hashlib.sha256(before.body).hexdigest(),
                  after_sha256=hashlib.sha256(after.body).hexdigest(),
                  physical_output=False, hardware_cpu_measured=False,
                  c4_blocks=64 if a.short else 750, cases=[])
    a.out.parent.mkdir(parents=True, exist_ok=True)
    for algorithm in range(1, 9):
        for note in (48, 60, 84):
            for feedback in (0, 32, 127):
                cfg = before.settings(algorithm, 'modulated', 'FDBK', feedback)
                old = before.engines(cfg, note)
                new = after.engines(cfg, note)
                engines = old[:2]+new[:2]+[new[3]]
                samples = [[] for _ in engines]
                blocks = 750 if note == 60 and not a.short else 64
                for block in range(blocks):
                    raw = [e.block(note=note, trigger=block == 0) for e in engines]
                    if feedback:
                        assert raw[3] == raw[4], ('fixed x2 != native', algorithm, note, feedback, block)
                    else:
                        assert raw[0] == raw[2] and raw[1] == raw[3], ('changed zero feedback', algorithm, note, block)
                    for dst, pcm in zip(samples, raw):
                        dst.extend(pcm)
                counts = [count_block(e, note) for e in engines[:4]]
                row = dict(algorithm=algorithm, note=note, feedback=feedback, harm=70.5,
                           blocks=blocks, no_feedback_unchanged=not feedback,
                           fixed_x2_vs_digitone=difference(samples[3], samples[4]),
                           old_x2_vs_digitone=difference(samples[1], samples[4]),
                           x1_before_after=difference(samples[0], samples[2]),
                           instruction_counts=dict(zip(('old_x1', 'old_x2', 'fixed_x1', 'fixed_x2'), counts)))
                result['cases'].append(row)
                before.assert_code(old); after.assert_code(new)
            print('PASS feedback guard, native x2 and zero-feedback regression ALGO', algorithm, 'note', note, flush=True)
            a.out.write_text(json.dumps(result, indent=2)+'\n')


if __name__ == '__main__':
    main()
