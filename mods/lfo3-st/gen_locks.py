"""Generate the LFO3 p-lock sources of lfo3-st from Synfact (read only).

Reproduces Synfact src/features/lfo3/locks_link.py integrate(): the sparse
lock library becomes the owned-bank backend, pattern/owned writers get the
recursive writer mutex, events publish under the frame busy counter. Module
adaptations: elekloader LFO3 IDs (0x3d9..), the module's publication counter
(lfo3_publication_begin/end) and its memcpy chain (lfo3_memcpy_entry). The
engine overlay (events_before) and the store routing live in ui.c.
Native lock edits count lanes only when the combined-capacity guard needs it;
the original sparse-lock writes and writer mutex remain unchanged.
Output: locks/*.c, locks/*.s (committed; regenerate only from the pinned
Synfact sources below).
"""
import hashlib
import re
import sys
from pathlib import Path

SYNFACT = Path('C:/Desarrollo/synfact/src')
R = SYNFACT / 'research-runtime/lfo3-coherence-20261021'
sys.path.insert(0, str(SYNFACT / 'features/lfo3'))
from locks_link import definition, locked_source          # noqa: E402  (pure text helpers)

HERE = Path(__file__).resolve().parent
OUT = HERE / 'locks'
LFO3_ID = 0x3d9
provenance = []


def read(path):
    data = path.read_bytes(); provenance.append((path.relative_to(SYNFACT).as_posix(), hashlib.sha256(data).hexdigest()))
    return data.decode()


def ids(text):
    """Synfact engine IDs 0x245.. -> elekloader 0x3d9.. (DEST3 0x248)."""
    text = re.sub(r'0x245u', f'{LFO3_ID:#x}u', text)
    text = text.replace('#0x245', f'#{LFO3_ID:#x}').replace('#0x248', f'#{LFO3_ID + 3:#x}')
    return text


def native_set_census_on_demand(text):
    """Preserve capacity decisions without scanning 923 flags on every edit.

    The original total is used only by sparse writes or by the native guard
    for a previously unused lane with sparse locks present. Matrix flags are
    read under the same writer mutex as before; no cached count is introduced.
    Exact source matches refuse a silently changed donor contract.
    """
    old = '''    native=lfo3_pattern_native_lanes(matrix);
    if(slot>=0x58 && slot<=0x5f) {'''
    new = '''    /* Module: only sparse writes or a new native lane need a census. */
    if(slot>=0x58 && slot<=0x5f) {
        native=lfo3_pattern_native_lanes(matrix);'''
    assert text.count(old) == 1
    text = text.replace(old, new)
    old = '''    if(track<13 && slot<71 && s->count && native+s->count>=79 &&
        !*((const u8 *)matrix+track*0x2447u+0x2400u+slot)) return 0;'''
    new = '''    if(track<13 && slot<71 && s->count &&
        !*((const u8 *)matrix+track*0x2447u+0x2400u+slot) &&
        lfo3_pattern_native_lanes(matrix)+s->count>=79) return 0;'''
    assert text.count(old) == 1
    return text.replace(old, new)


def publication_owner_invalidation(text):
    """Invalidate task publication pointer memos before owner metadata changes.

    Allocation/release run under the owned writer mutex. Query create/delete
    modes enter through the guarded shadow service; relocation is guarded too.
    Read-only queries keep their original semantics and do not invalidate.
    """
    for name, statement in (
        ('lfo3_shadow_owner_allocate', 'lfo3_publication_forget_owned();'),
        ('lfo3_shadow_owner_release', 'lfo3_publication_forget_owned();'),
        ('lfo3_shadow_owned_record_body', 'if(create)lfo3_publication_forget_owned();'),
        ('lfo3_shadow_owner_relocate_body', 'lfo3_publication_forget_owned();')):
        pattern = r'(?m)^\w+(?: \*)?\s*'+name+r'\([^;]*?\)\s*\{'
        text, count = re.subn(pattern, lambda m:m[0]+'\n    '+statement, text)
        assert count == 1, name
    return 'extern void lfo3_publication_forget_owned(void);\n'+text


def main():
    OUT.mkdir(exist_ok=True)
    files = {}
    aliases = {}
    wrappers = []
    text = read(R / 'locks_proto/lfo3_locks.c')
    for name in ('clear', 'set', 'clone', 'decode'):
        text = re.sub(r'\blfo3_locks_' + name + r'\b', 'lfo3_sparse_' + name, text)
        aliases['lfo3_locks_' + name] = 'lfo3_owned_' + name
    files['locks.c'] = text
    text = native_set_census_on_demand(read(R / 'pattern_proto/lfo3_pattern.c'))
    prototypes = []
    for name, result, args in (('lookup', 'store_t *', 'u32'), ('register', 'u32', 'u32'),
                               ('unregister', 'u32', 'u32'), ('decode', 'u32', 'void *,const void *,u32')):
        full = 'lfo3_pattern_' + name
        text = definition(text, full, full + '_legacy')
        prototypes.append(f'extern {result} {full}({args});')
        aliases[full] = 'lfo3_owned_pattern_decode' if name == 'decode' else 'lfo3_owned_' + name
    marker = 'u32 lfo3_pattern_context_bytes(void)'
    assert text.count(marker) == 1
    text = text.replace(marker, '\n'.join(prototypes) + '\n' + marker)
    names = ['lfo3_pattern_' + n for n in ('copy', 'compress', 'encode', 'update', 'clear', 'set', 'delete', 'clear_row')]
    text, generated = locked_source(text, names)
    files['pattern.c'] = text + '\n' + generated
    files['pattern_entry.s'] = read(R / 'pattern_proto/pattern_entry.s')
    text = read(R / 'budget_proto/lfo3_budget.c')
    text = 'extern void *lfo3_owned_native_allocate(unsigned int);\nextern void lfo3_owned_native_release(void *);\n' + text
    text = text.replace('((void *(*)(u32))0x4010350c)(n)', 'lfo3_owned_native_allocate(n)')
    text = text.replace('((void (*)(void *))0x40103712)(p)', 'lfo3_owned_native_release(p)')
    files['budget.c'] = text
    files['bounded.c'] = read(R / 'budget_proto/lfo3_bounded.c')
    text = read(R / 'owned_matrix_proto/lfo3_owned.c')
    text = 'extern unsigned int lfo3_shadow_owner_allocate(unsigned int,unsigned int);\nextern void lfo3_shadow_owner_release(unsigned int);\n' + text
    text = text.replace('    if(!p || !c) return p;',
        '    if(!p || !c) return p;\n    if(!lfo3_shadow_owner_allocate((u32)p,bytes)){lfo3_owned_native_release(p);return 0;}')
    text = text.replace('if(!o) {lfo3_owned_native_release(p);return 0;}',
        'if(!o) {lfo3_shadow_owner_release((u32)p);lfo3_owned_native_release(p);return 0;}')
    text = text.replace('    lfo3_owned_native_release(p);\n}', '    lfo3_shadow_owner_release((u32)p);\n    lfo3_owned_native_release(p);\n}')
    names = ['lfo3_owned_' + n for n in ('allocate', 'release', 'register', 'unregister', 'set', 'clear',
        'clone', 'decode', 'pattern_decode', 'copy', 'move', 'clear_track', 'copy_pattern', 'paste_patterns',
        'copy_page', 'copy_track', 'paste_page', 'paste_track', 'clear_page', 'clear_track_action')]
    text, generated = locked_source(text, names)
    files['owned.c'] = text + '\n' + generated
    entry = read(R / 'owned_matrix_proto/owned_entry.s')
    entry += '''
| Allocator filters (Synfact locks_link): no tracked object fits below 0x8e.
.globl lfo3_owned_allocate_entry
lfo3_owned_allocate_entry:
 move.l 4(%sp),%d0
 cmpi.l #0x8e,%d0
 bcs.w lfo3_owned_native_allocate
 jmp lfo3_owned_allocate
.globl lfo3_owned_release_entry
lfo3_owned_release_entry:
 move.l 4(%sp),-(%sp)
 jsr lfo3_release_is_small
 addq.l #4,%sp
 tst.l %d0
 bne.w lfo3_owned_native_release
 jmp lfo3_owned_release
'''
    assert entry.count('copy_owned:\n        jmp lfo3_owned_copy') == 1
    entry = entry.replace('copy_owned:\n        jmp lfo3_owned_copy',
        'copy_owned:\n        jsr lfo3_irq_mask\n        bne.w copy_previous\n        jmp lfo3_owned_copy')
    files['owned_entry.s'] = entry.replace('lfo3_owned_previous_copy', 'lfo3_memcpy_entry')
    text = read(R / 'event_proto/lfo3_events.c')
    text = text.replace('!lfo3_pattern_context || lfo3_pattern_context->busy || track>=13', 'track>=13')
    text = text.replace('    if(!lfo3_pattern_context || lfo3_pattern_context->busy) return result;\n', '')
    text = definition(text, 'lfo3_events_before', 'lfo3_events_before_legacy')
    text = text.replace('u32 lfo3_events_context_bytes(void)', 'extern u32 lfo3_active_locks;\nu32 lfo3_events_context_bytes(void)')
    text = text.replace('c->stride=20+8*native_slots;', 'c->stride=20+8*native_slots;lfo3_active_locks=0;')
    text = text.replace('        c->active[track].mask=q ? q->mask : 0;', '''        c->active[track].mask=q ? q->mask : 0;
        if(c->active[track].mask) lfo3_active_locks|=1u<<track;
        else lfo3_active_locks&=~(1u<<track);''')
    text = text.replace('{if(lfo3_events_context && track<13) lfo3_events_context->active[track].mask=0;}', '''{if(lfo3_events_context && track<13) {
        lfo3_events_context->active[track].mask=0;lfo3_active_locks&=~(1u<<track);}}''')
    for name in ('apply', 'restore', 'clear', 'fx_restore'):
        full = 'lfo3_events_' + name
        match = re.search(r'(?m)^u32 ' + full + r'\(([^)]*)\)\s*\{', text); assert match, full
        args = match[1]; values = [re.search(r'(\w+)$', a.strip())[1] for a in args.split(',')]
        text = definition(text, full, full + '_body')
        text += f'\nu32 {full}({args})' + '{u32 result;lfo3_publication_begin();' + f'result={full}_body(' + ','.join(values) + ');lfo3_publication_end(0,0,0,0);return result;}\n'
    text = text.replace('extern u32 lfo3_active_locks;',
        'extern u32 lfo3_active_locks;\nextern void lfo3_publication_begin(void),lfo3_publication_end(u32,u32,u32,u32);')
    files['events.c'] = text
    entry = read(R / 'event_proto/event_entry.s')
    # The engine runs from the core LFO operation (main.c), not 400a4ad8.
    frame = entry[entry.index('        .globl lfo3_events_frame_entry'):entry.index('        .globl lfo3_events_capture_entry')]
    assert 'jsr lfo3_events_before' in frame
    files['event_entry.s'] = entry.replace(frame, '')
    files['owners.c'] = publication_owner_invalidation(read(SYNFACT / 'features/lfo3/shadow_owners.c'))
    text = ids(read(R / 'lock_ui_proto/lfo3_lock_ui.c'))
    text = text.replace('extern void lfo3_frame_store(void *,u32,s32,s32,u32,u32);',
                        'extern u32 lfo3_actions_store(void *,u32,s32,s32,u32,u32);')
    text = text.replace('    lfo3_frame_store(proxy,id,value,track,0,0);', '    lfo3_actions_store(proxy,id,value,track,0,0);')
    text = text.replace('extern s32 lfo3_editor_get(void *,u32,s32);', 'extern s32 lfo3_ui_get(void *,u32,u8 *);')
    text = text.replace('    return lfo3_editor_get(view,id,-1);', '    return lfo3_ui_get(view,id,0);')
    files['lock_ui.c'] = text
    entry = ids(read(R / 'lock_ui_proto/lock_ui_entry.s'))
    # Previous handlers in this module: FX store and view getter entries; the
    # Sound store is the core's (query 24 reaches ui.c), so its entry replays it.
    entry = entry.replace('lfo3_previous_fx_store', 'lfo3_fx_store').replace('lfo3_previous_view_get', 'lfo3_widget_view_get_entry')
    entry = entry.replace('lfo3_previous_sound_store', '0x40013304')
    files['lock_ui_entry.s'] = entry
    # The engine overlay with locks is the module's (ui.c lfo3_events_before):
    # drop Synfact's frame-based legacy body and its frame/render externs.
    text = files['events.c']
    start = text.index('void lfo3_events_before_legacy(')
    depth, i = 0, text.index('{', start)
    while True:
        depth += {'{': 1, '}': -1}.get(text[i], 0); i += 1
        if not depth: break
    text = text[:start] + text[i:]
    for line in ('extern frame_t *lfo3_frame_context;\n', 'extern void lfo3_render(u8 *,u32,u32,const u16 *,u8 *,u32 *);\n',
                 'extern void lfo3_frame_before(u8 *,u32,u32);\n'):
        assert text.count(line) == 1, line; text = text.replace(line, '')
    files['events.c'] = text
    # Aliases: references to replaced public names bind to the owned backend;
    # the owned copy chains to this module's memcpy entry.
    aliases['lfo3_owned_previous_copy'] = 'lfo3_memcpy_entry'
    aliases['lfo3_frame_publication_begin'] = 'lfo3_publication_begin'
    aliases['lfo3_frame_publication_end'] = 'lfo3_publication_end'
    for name, text in files.items():
        for old, new in aliases.items():
            text = re.sub(r'\b' + old + r'\b(?!_legacy|_body)', new, text)
        header = ('| ' if name.endswith('.s') else '/* ') + f'Generated by gen_locks.py from Synfact sources; do not edit.' + ('' if name.endswith('.s') else ' */') + '\n'
        (OUT / name).write_text(header + text, encoding='utf-8')
    (OUT / 'PROVENANCE.txt').write_text(''.join(f'{h}  {p}\n' for p, h in sorted(set(provenance))), encoding='ascii')
    print(len(files), 'files;', ', '.join(sorted(files)))


if __name__ == '__main__':
    main()
