"""Adapt the pinned eight-word lock backend to shared LFO3/ARP lanes."""
import re


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
