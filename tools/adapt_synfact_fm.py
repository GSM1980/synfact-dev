"""Prepare authored SMZ adapters from the pinned private Synfact source import.

This maintenance tool is not part of SMZ compilation. No firmware or emulator
is read or executed here; its inputs are the imported assembly and Synfact C.
"""
from pathlib import Path
import argparse,re

ROOT=Path(__file__).resolve().parents[1]
FOLDER=ROOT/'mods/fm-tone-st'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--synfact',type=Path,required=True)
    args=parser.parse_args()
    text=(FOLDER/'imported/adapter-original.s').read_text()
    # MAIN sends extended controls through the generic ABI, not Synfact's
    # obsolete frame-tail/FM20 mailbox. No new IRQ transport is introduced.
    start=text.index('fm_offsets_receive:');end=text.index('fm_copy_long:',start)
    text=text[:start]+'''fm_offsets_receive:
        moveq #0,%d0
        rts
fm_snapshot_receive:
        | update's 20-byte save, one cached flag, and this BSR's return.
        move.l 40(%sp),-(%sp)
        move.l %d2,-(%sp)
        jsr fm_receive_controls
        addq.l #8,%sp
        rts
'''+text[end:]
    start=text.index('        moveq   #0,%d0\n        move.b  FM_RECORD',text.index('fm_cache_loop:'))
    end=text.index('fm_cache_done:',start)
    text=text[:start]+text[end:]
    start=text.index('| parameters 9..24:');end=text.index('12:     lea     fm_ext',start)
    text=text[:start]+text[end:]
    # All graphics and cached native policy data remain MAIN-owned; AUDIO only
    # sees Q8/raw scalar controls delivered by the core's generic interface.
    text=text.replace('fm_contexts:', '.section .rodata,"a"\n.balign 4\nfm_contexts:',1)
    text=text.replace('.text','.text\n.include "imported/bindings.s"',1)
    for name in ('fm_contexts','fm_templates','fm_params','fm_bounds','fm_template','fm_bounds2','fm_ext_defaults',
                 'fm_bake_mod','fm_bake_car'):
        text=text.replace(name+':',f'.globl {name}\n{name}:',1)
    (FOLDER/'audio.s').write_text('''| Synfact FM_CACHE adapter, imported as symbolic assembler source.
| Adaptation: generic elekloader extended-control receiver. See imported provenance.
'''+text)
    source=(args.synfact/'src/current/fm_digitone_ui.c').read_text()
    # Keep exact drawing primitives, coordinate formulae and resources. Remove
    # Synfact-specific storage/transport; the shared core now owns those parts.
    start=source.index('void fm_ui_text(');end=source.index('/* Enter the unchanged native encoder dispatch')
    drawing=source[start:end]
    pre=source[:source.index('int fm_ui_supported(')]
    pre=pre[:pre.index('u32 fm_ui_action_begin(')] if 'u32 fm_ui_action_begin(' in pre else pre
    # Drop source-only frame state and action metadata which is core-owned.
    pre=re.sub(r'^static u32 (offset_cursor|frame_generation|poly_frame_override|action_depth|ratio_dispatch_depth).*\n','',pre,flags=re.M)
    pre=re.sub(r'^static u32 action_layout.*\n','',pre,flags=re.M)
    pre=re.sub(r'^static const u16 slots.*\n','',pre,flags=re.M)
    pre=pre.replace('#define DATA __attribute__((section(".data")))','#define DATA')
    pre=pre.replace('char *fm_ui_format(u32 id,u32 raw){return fm_ui_format_to(id,raw,formatted);}',
                    '')
    # Machine-local aliases keep all original drawing/formulas readable. IDs
    # are translated by the get/format boundary to this core's 32-position map.
    pre=pre.replace('return GET(view,id,&locked)&65535u;', 'return fm_get(view,id,&locked)&65535u;')
    drawing=drawing.replace('GET(view,id,&locked)','fm_get(view,id,&locked)').replace('GET(view,0x218u,&other)','fm_get(view,0x218u,&other)')
    # Ownership, selector state and page selection belong exclusively to the
    # shared core dispatcher, just as for SP renderers. Keep artwork unchanged.
    drawing=drawing.replace('void fm_ui_draw(u8 *view,void *ctx)',
                            'static void fm_ui_draw(u8 *view,void *ctx,u32 page)')
    drawing=drawing.replace('u32 r[24],page,i,id,v;', 'u32 r[24],i,id,v;')
    drawing=drawing.replace('    if(!fm_ui_supported(view))return;\n','')
    drawing=drawing.replace('active_view=(u32)view;page=*(u32 *)(view+0x90);if(page>3)return;',
                            'active_view=(u32)view;if(page>3)return;')
    prefix='''/* Synfact FM TONE UI: same drawing source and private imported resources.
 * Core services own page navigation, storage, locks and parameter transport. */
#include <syntakt/machines.h>
#include "imported/ui-data.h"
typedef st_u32 u32;typedef unsigned char u8;
static u32 fm_get(void *,u32,u8 *);
#define FM_PAGE_IDS {{0x201,0x202,0x203,0x204,0x205,0x206,0x207,0x208}, \\
 {0x239,0x23a,0x23b,0x23c,0,0,0,0},{0x209,0x20a,0x20b,0x20c,0x20d,0x20e,0x20f,0x210}, \\
 {0x211,0x212,0x213,0x214,0x215,0x216,0x217,0}}
'''
    # Test doubles can replace firmware functions in a native rendering fixture.
    for name in ('GET','TYPE','PIX','FILL','RECT','TEXT','TOGGLE'):
        pre=re.sub(r'^(#define '+name+r' .*)$',r'#ifndef '+name+'\n'+r'\1'+'\n#endif',pre,flags=re.M)
    drawing=drawing.replace('((void (*)(void *,void *,u32,u32,u32))0x4003a1c2u)(view,ctx,4,page,7);',
                            'FM_PAGE_INDICATOR(view,ctx,4,page,7);')
    pre+='\n#ifndef FM_PAGE_INDICATOR\n#define FM_PAGE_INDICATOR ((void (*)(void *,void *,u32,u32,u32))0x4003a1c2u)\n#endif\n'
    (FOLDER/'ui.c').write_text(prefix+pre+drawing+'''
static u32 engine(u32 id)
{return id>=0x201 && id<=0x218?0x219+id-0x201:id>=0x239 && id<=0x23c?0x231+id-0x239:0;}
static u32 fm_get(void *view,u32 id,u8 *locked)
{return GET(view,engine(id),locked);}
static const char *format(const struct st_machine_ui_api *api,const struct core_machine_def *m,u32 index,u32 raw)
{(void)api;(void)m;return fm_ui_format_to(index<24?0x201+index:0x239+index-24,raw,formatted);}
static void draw(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,void *ctx,u32 page)
{(void)api;(void)m;fm_ui_draw(view,ctx,page);}
static u32 policy(u32 index)
{return index==4 || index==5 || index==6 || index>=24?0x1f1:0xffffffffu;}
static const u32 layout[32]={0,1,2,3,4,5,6,7,24,25,26,27,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,
 8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,0xffffffffu};
const struct st_machine_ui fm_tone_ui={65,4,layout,draw,ST_MACHINE_NO_CALLBACK,ST_MACHINE_NO_CALLBACK,format,policy};
#include "events.h"
''')


if __name__=='__main__':main()
