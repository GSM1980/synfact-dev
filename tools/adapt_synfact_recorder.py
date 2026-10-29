"""One-time source port of the authored Synfact recorder. Normal builds are local.

No legacy builders, firmware extraction, binary overlays or emulator imports.
The output records hashes of every authored source used by the port.
"""
import argparse,hashlib,json,re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def replace_function(text,name,replacement):
    match=re.search(r'(?m)^(?:static )?(?:void|u32|s32) '+name+r'\([^\n]*\)\n\{',text)
    if not match:raise ValueError('missing function '+name)
    begin=match.end()-1;depth=1;at=begin+1
    while depth:
        depth+=(text[at]=='{')-(text[at]=='}');at+=1
    return text[:match.start()]+replacement+text[at:]

def adapt(synfact):
    out=ROOT/'mods/recorder-st';out.mkdir(exist_ok=True)
    sources=['src/current/pool/sampling.c','src/current/pool/sampling_trim.c',
             'src/current/pool/sampling_save.c','src/features/ui-performance/headroom/runtime.c',
             'src/features/lfo3/recorder_normalize/runtime.c']
    read=lambda name:(synfact/name).read_text(encoding='utf-8')
    original=read(sources[0]);text=original
    text=text.replace('static u32 sam_encoder_table[5] DATA={0};\n','')
    text=text.replace('#include "sampling_save.c"','#include "save.c"')
    text=text.replace('#include "sampling_trim.c"','#include "trim.c"')
    text=replace_function(text,'sam_normalize_step',
        'static void sam_normalize_step(u32 visible)\n{(void)visible;normalize_fast();}')
    text=text.replace('void sampler_tick(void)','static void sampler_base_tick(void)')
    text=text.replace('return x<<16;','return (s32)((u32)x<<16);').replace('value=acc<<16;','value=(s32)((u32)acc<<16);')
    text=text.replace('*(volatile u32 *)0x402d7010u','REC_BPM')
    text=replace_function(text,'sampler_idle','''static void recorder_ui_tick(void *controller)
{
    u32 began=sam_clock,q,before;(void)controller;
    if(!sam_view){sam_enabled=0;return;}
    sam_enabled=sam_foreground();
    if(sam_enabled)sam_name_view=0;
    if(!sam_enabled)sampler_suspend();
    for(q=0;q<32 && sam_state==SAM_NORMALIZING;q++){
        before=sam_norm_pos;sam_normalize_step(sam_enabled);
        if(before==sam_norm_pos || sam_clock-began>=192u)break;
    }
    if(sam_enabled && sam_assign_phase)sam_assign_step();
    for(q=0;q<32 && sam_enabled && sam_save_phase;q++){
        sam_save_step();if(sam_clock-began>=192u)break;
    }
    if(sam_enabled){
        sam_report[3]++;
        if(sam_clock-recorder_last_paint>=3200u){recorder_last_paint=sam_clock;sam_redraw();}
    }
}''')
    # One global key subscription owns both the gesture and recorder controls.
    # The start transition primes both MAIN correction banks under IRQ exclusion.
    text=text.replace('sam_state=armed?SAM_ARMED:SAM_RECORDING;',
                      'recorder_prime();sam_state=armed?SAM_ARMED:SAM_RECORDING;')
    text=text.replace('u32 sampler_key(u8 *iface,const u8 *ev)',
                      'static u32 sampler_key_base(u8 *iface,const u8 *ev)')
    text=text.replace('sam_sound_pending=0;sam_assign_phase=SAM_ASSIGN_IDLE;sam_report[7]++;',
        'sam_sound_pending=0;sam_assign_phase=SAM_ASSIGN_IDLE;sam_report[7]++;\n        if(sam_save_phase)sam_save_cleanup("SAVE CANCELLED");else recorder_save_release();')
    text=text.replace('void sampler_draw(u8 *iface,void *ctx)','''static u32 sampler_key(u8 *iface,const u8 *ev)
{
    u32 ready=sam_state==SAM_READY && sam_frames,result;
    result=sampler_key_base(iface,ev);
    if(ready && sam_state==SAM_EMPTY && !sam_frames)recorder_resume();
    return result;
}
void sampler_draw(u8 *iface,void *ctx)''')
    # Owned native view has one draw bridge into ev_draw. Input and periodic
    # work are subscriptions; the native key/encoder/idle methods stay native.
    text=text.replace('    sam_iface_table[2+2]=(u32)sampler_key;\n','')
    text=text.replace('    sam_iface_table[2+4]=(u32)sampler_draw;sam_iface_table[2+11]=(u32)sampler_idle;',
                      '    /* Complete-view ev_draw paints this view; keep its stock draw method. */')
    start=text.index("    /* The plain base IView's encoder interface is at +4. */")
    end=text.index('    sam_view=view;',start)
    text=text[:start]+text[end:]
    # Full native base cleanup is retained; the PCM remains owned between opens.
    native={
      '((void (*)(void *))0x400ffdfcu)':'REC_VIEW_DTOR',
      '((void (*)(void *,const char *))0x400ffba6u)':'REC_VIEW_CTOR',
      '((u32 (*)(void *,const void *))0x401012D4u)':'REC_KEY_MANAGER',
      '((void (*)(void *))0x40100ea4u)':'REC_MANAGER_CLEANUP',
      '((u32 (*)(void *,const void *))0x400fce90u)':'REC_NAME_KEY',
      '((const u32 *)0x40241bd4u)':'REC_VIEW_TABLE'}
    for old,new in native.items():text=text.replace(old,new)
    text=text.replace('#define SAM_TOGGLE ((void (*)(void *,u32,void *,s32,s32,u32))0x40070362u)',
                      '#define SAM_TOGGLE REC_TOGGLE')
    (out/'recorder.c').write_text(text,encoding='utf-8')
    (out/'trim.c').write_text(read(sources[1]),encoding='utf-8')
    save=read(sources[2]).replace('0x21Cu','0x23Cu')
    save=save.replace('#define BRAIN ((u8 *(*)(void))0x4016E804u)','#define BRAIN REC_BRAIN')
    for old,new in {
      '((u32 (*)(void *,u32,u32,u32,u32))0x4000F7BEu)':'REC_SET_MACHINE',
      '((u8 *(*)(void *,u32))0x4000F04Cu)':'REC_TRACK_MODEL',
      '((void (*)(void *,u32,u32,u32,u32,u32))0x40013304u)':'REC_SET_PARAM',
      '((u32 (*)(void *,u32))0x4000F72Au)':'REC_AVAILABILITY',
      '((void (*)(void *,void *,u32,void *))0x4006E1C6u)':'REC_TRACK_CTOR',
      '((void (*)(void *))0x401bd5d2u)':'REC_QSTR_DTOR',
      '((void (*)(void *,void *,void *,u32,u32,u32,void *))0x400fc8a0u)':'REC_NAME_CTOR',
    }.items():save=save.replace(old,new)
    save=save.replace('#define MMC_WRITE ((s32 (*)(u32,u32,const void *))0x4010F634u)\n','')
    save=replace_function(save,'sam_index_read','')
    old='''    for(k=0;k<2;k++) {
        status=sam_index_read(k?HZ_INDEX_B:HZ_INDEX_A,copies+k,lens+k);
        if(status<0){error=status==-2?"NO MEMORY":"INDEX READ ERROR";goto fail;}
    }'''
    new='''    {
        struct st_hzfs fs;
        status=host->hz_open(&fs,host->storage);
        if(status!=ST_HZ_OK){error=status==ST_HZ_MEMORY?"NO MEMORY":"INDEX READ ERROR";goto fail;}
        for(k=0;k<2;k++){copies[k]=fs.copy[k];lens[k]=fs.length[k];fs.copy[k]=0;}
        host->hz_close(&fs);
    }'''
    if old not in save:raise ValueError('catalogue source changed')
    save=save.replace(old,new)
    save=save.replace('static void sam_save_add(void)\n{',
        'static void sam_save_add(void)\n{\n    if(!recorder_save_acquire()){sam_save_cleanup("TRANSFER BUSY");return;}')
    save=save.replace('    if(CARD_SECTORS/HZ_BLOCK_SECTORS<=HZ_DATA_FIRST)goto fail;',
        '    if(!recorder_save_acquire()){error="TRANSFER BUSY";goto fail;}\n    if(CARD_SECTORS/HZ_BLOCK_SECTORS<=HZ_DATA_FIRST)goto fail;')
    save=save.replace('    text_set(sam_save_message,message,24);sam_redraw();',
        '    recorder_save_release();text_set(sam_save_message,message,24);sam_redraw();')
    save=save.replace('for(slot=0;slot<SLOTS && pool_now[slot];slot++){}',
                      'for(slot=64;slot<SLOTS && pool_now[slot];slot++){}')
    save=save.replace('if(!bytes || (bytes&511u) || bytes>SAM_IO || lba<HZ_INDEX_A ||',
        'if(card>0x800000u)card=0x800000u;\n    if(!bytes || (bytes&511u) || bytes>SAM_IO || lba<HZ_INDEX_A ||')
    # Explicit PCM16BE serialization gives the same target bytes and allows
    # native-host storage tests to inspect the real on-disk format.
    save=save.replace('sam_save_io[i]=(i<logical && p>=64)?((u8 *)sam_pcm)[2*sam_save_start+p-64]:0;',
        'sam_save_io[i]=0;\n            if(i<logical && p>=64){\n                u32 q=p-64;u16 value=(u16)sam_pcm[sam_save_start+(q>>1)];\n                sam_save_io[i]=(u8)(q&1?value:value>>8);\n            }')
    (out/'save.c').write_text(save,encoding='utf-8')
    capture=read(sources[3]).replace('#define DATA __attribute__((section(".data")))\n','')
    (out/'capture.c').write_text(capture,encoding='utf-8')
    norm=read(sources[4]);start=norm.index('        __asm__ volatile(');end=norm.index('        if(lo<sam_detail_low',start)
    asm=norm[start:end]
    norm=norm[:start]+'''#ifdef __m68k__
'''+asm+'''#else
        do {
            value=(s32)(short)(((u32)(s32)*at*(u32)gain)>>16);
            *at++=(short)value;if(value<lo)lo=value;if(value>hi)hi=value;
        }while(at<limit);
#endif
'''+norm[end:]
    (out/'normalize.c').write_text(norm,encoding='utf-8')
    record={'source':'Synfact authored recorder C, ported to the Elekloader event bus',
            'source_sha256':{name:hashlib.sha256((synfact/name).read_bytes()).hexdigest() for name in sources},
            'changes':['shared ev_key/ev_enc/ev_tick/ev_draw/ev_main_mix subscriptions',
                'core HZFS validation, CRC, IO, pool and preview services',
                'SP ONE SAMP engine id 0x23c; free pool slots start at 65',
                'no binary overlays, runtime firmware patches or extra hook sites',
                'original UI/trim and optimized capture/normalization arithmetic retained']}
    (out/'provenance.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--synfact',required=True,type=Path)
    adapt(p.parse_args().synfact)
