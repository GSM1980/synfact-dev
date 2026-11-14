"""Adapt the pinned eight-word lock backend to shared LFO3/ARP lanes."""
import re


PLAY_START = '''/* The first step's events as PLAY starts (4008c0b0) are static and take
 * each track's static lock block (444e3884 + 4 * track), outside the pool of
 * 64: capture, convert, apply and the extra-lock reader find it here too. */
static locks_t *queued(u32 lock)
{
    events_t *c=lfo3_events_context;u32 base=*(const u32 *)0x444db730u,off,i;
    if(!c || !c->stride || !lock) return 0;
    if(base && lock>=base) {
        off=lock-base;i=off/c->stride;
        if(i<64 && off==i*c->stride) return &c->queued[i];
    }
    for(i=0;i<13;i++) if(lock==*(const volatile u32 *)(0x444e3884u+4*i)) return &start_locks[i];
    return 0;
}'''


ROW_LOOKUP = '''
/* Resolve the same first (lowest-track) match as thirteen owned_lookup calls.
 * Each owner is inspected once. Ties retain live/object-list/temporary order;
 * TRACK clipboard owners remain excluded, as in matrix_cell. No pointer memo
 * or writer-side bookkeeping is introduced. Unsigned subtraction also handles
 * the original candidate matrix wrapping below zero. */
static u32 event_row(u32 address,u32 first,u32 count,u32 *index)
{
    u32 off=address-first,i=off/PATTERN,t,base;
    off-=i*PATTERN;
    if(i>=count) return 13;
    if(!off && address && address>=first) {*index=i;return 0;}
    t=off/ROW;base=address-t*ROW;
    if(t>=13 || off!=t*ROW || !base || base<first) return 13;
    *index=i;return t;
}
store_t *lfo3_owned_row_lookup(u32 address,u32 *track)
{
    context_t *c=lfo3_owned_context;old_context_t *old=lfo3_pattern_context;
    object_t *o;store_t *best=0;u32 limit=13,t,i,off;
    if(c) {
        if(c->patterns) {
            t=event_row(address,c->patterns+MATRIX,128,&i);
            if(t<limit) {best=&c->live[i].store;limit=t;if(!t) goto found;}
        }
        for(o=c->objects;o;o=o->next) if(o->kind!=TRACK) {
            t=event_row(address,payload(o)+MATRIX,o->count,&i);
            if(t<limit) {best=&o->cells[i].store;limit=t;if(!t) goto found;}
        }
    } else if(old && old->patterns) {
        t=event_row(address,old->patterns+MATRIX,128,&i);
        if(t<limit) {best=&old->live[i];limit=t;if(!t) goto found;}
    }
    if(old) for(i=0;i<8;i++) if(old->temporary[i].matrix) {
        off=address-old->temporary[i].matrix;t=off/ROW;
        if(t<limit && off==t*ROW) {
            best=&old->temporary[i].store;limit=t;if(!t) break;
        }
    }
found:
    if(best) *track=limit;
    return best;
}
'''


def event_hot_paths(name, text):
    """Keep generated event optimizations reproducible from the pinned donor."""
    if name == 'owned.c':
        assert 'lfo3_owned_row_lookup' not in text
        return text + ROW_LOOKUP
    if name != 'events.c':
        return text
    old = 'extern void *lfo3_owned_lookup(u32);'
    assert text.count(old) == 1
    text = text.replace(old, old+'\nextern void *lfo3_owned_row_lookup(u32,u32 *);')
    old = '''    for(t=0;t<13;t++) {
        u32 base=matrix-t*0x2447u;void *s=lfo3_owned_lookup(base);
        if(s) {q->mask=lfo3_locks_apply(s,t,step,&q->record.v[0]);break;}
    }'''
    new = '''    {
        void *s=lfo3_owned_row_lookup(matrix,&t);
        if(s) q->mask=lfo3_locks_apply(s,t,step,&q->record.v[0]);
    }'''
    assert text.count(old) == 1
    text = text.replace(old, new)
    # Keep all payload bytes and halfword access order, even with mask zero.
    # No extra guard, wider alignment requirement or overlap assumption.
    old = 'if(q) for(i=0;i<16;i++) c->active[track].record.v[i]=q->record.v[i];'
    new = 'if(q) {\n' + ''.join(
        f'            c->active[track].record.v[{i}]=q->record.v[{i}];\n'
        for i in range(16)) + '        }'
    assert text.count(old) == 1
    text = text.replace(old, new).replace(
        'u32 i,result=lfo3_events_native_apply', 'u32 result=lfo3_events_native_apply')
    text = text.replace('extern u32 lfo3_active_locks;',
        'extern u32 lfo3_active_locks;\nextern void lfo3_output_track(u32),lfo3_output_reset(void);')
    old = 'for(i=0;i<13;i++) {start_locks[i].mask=0;c->active[i].mask=0;for(j=0;j<16;j++) c->active[i].record.v[j]=0;}'
    assert text.count(old) == 1
    text = text.replace(old, old+'\n    lfo3_output_reset();')
    text = text.replace('u32 result=lfo3_events_native_apply', 'u32 previous,result=lfo3_events_native_apply')
    old = '        c->active[track].mask=q ? q->mask : 0;'
    assert text.count(old) == 1
    text = text.replace(old, '        previous=c->active[track].mask;\n'+old)
    old = '''        if(c->active[track].mask) lfo3_active_locks|=1u<<track;
        else lfo3_active_locks&=~(1u<<track);'''
    assert text.count(old) == 1
    text = text.replace(old, '''        if(previous!=c->active[track].mask) {
            if(c->active[track].mask) lfo3_active_locks|=1u<<track;
            else lfo3_active_locks&=~(1u<<track);
        }''')
    old = '            c->active[track].record.v[15]=q->record.v[15];\n        }'
    assert text.count(old) == 1
    text = text.replace(old, old+'\n        if((previous|c->active[track].mask)&0x88u)lfo3_output_track(track);')
    old = '''{if(lfo3_events_context && track<13) {
        lfo3_events_context->active[track].mask=0;lfo3_active_locks&=~(1u<<track);}}'''
    assert text.count(old) == 1
    text = text.replace(old, '''{if(lfo3_events_context && track<13) {
        u32 previous=lfo3_events_context->active[track].mask;
        if(!previous)return;
        lfo3_events_context->active[track].mask=0;lfo3_active_locks&=~(1u<<track);
        if(previous&0x88u)lfo3_output_track(track);}}''')
    old = 'for(i=0;i!=8;i++)out[i]=q->record.v[8+i];'
    new = '\n    '.join(f'out[{i}]=q->record.v[{8+i}];' for i in range(8))
    assert text.count(old) == 1
    return text.replace(old, new).replace('locks_t *q;u32 i,mask;', 'locks_t *q;u32 mask;')


def play_start_locks(text):
    start = text.index('static locks_t *queued(u32 lock)')
    end = text.index('\n}\n', start) + 2
    text = text[:start] + PLAY_START + text[end:]
    text = text.replace('void lfo3_events_init(', 'static locks_t start_locks[13];\nvoid lfo3_events_init(', 1)
    init = 'for(i=0;i<13;i++) {c->active[i].mask=0;'
    assert init in text
    return text.replace(init, 'for(i=0;i<13;i++) {start_locks[i].mask=0;c->active[i].mask=0;', 1)


def shared_locks(name, text):
    if name not in ('locks.c', 'bounded.c', 'owned.c', 'pattern.c', 'events.c', 'pattern_entry.s'):
        return text
    if name == 'pattern_entry.s':
        return text.replace('cmpi.l #8,%d0', 'cmpi.l #16,%d0')
    text = text.replace('\r', '')
    text = text.replace('0x5f', '0x67')
    text = text.replace('[104]', '[208]').replace('<104', '<208')
    text = text.replace('release(index,416)', 'release(index,832)')
    text = re.sub(r'\b(track|t|lane->track|p->track|encoded\[i\]\.track)\*8', r'\1*16', text)
    text = text.replace('i%8', 'i%16').replace('i/8', 'i/16')
    text = re.sub(r'\b(param|parameter)>=8', r'\1>=16', text)
    # Loops over the eight parameters widen; loops over the eight temporary
    # pattern slots (temporary[8]) do not: widening them overran the context.
    text = '\n'.join(line if 'temporary' in line else re.sub(r'\b(i|p)<8(?![0-9])', r'\1<16', line)
                     for line in text.split('\n'))
    if name in ('events.c',):
        text = text.replace('u16 v[8]', 'u16 v[16]')
        text = text.replace('j<8', 'j<16')
        text = play_start_locks(text)
    if name == 'owned.c':
        text = text.replace('u16 value[208][64]', 'u16 value[80][64]')
        text = text.replace('b->index.value[i]=v ? b->value[i] : 0;',
                            'b->index.value[i]=v ? b->value[count] : 0;')
        text = text.replace('b->value[i][j]=v[j]', 'b->value[count][j]=v[j]')
        text = text.replace('v=b->value[slot];for(j=0;j<64;j++) v[j]=EMPTY;', '''u32 k,n;for(k=0;k<80;k++) {
            for(n=0;n<208;n++) if(b->index.value[n]==b->value[k]) break;
            if(n==208) break;
        }
        if(k==80)return 0;
        v=b->value[k];for(j=0;j<64;j++) v[j]=EMPTY;''')
        text = text.replace('slot=lane->track*16+p;v=b->value[slot];',
                            'slot=lane->track*16+p;v=b->value[*count];')
    return text
