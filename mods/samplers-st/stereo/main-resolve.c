/* SPDX-License-Identifier: GPL-2.0-or-later
 * A closed C code span: all references leaving it are absolute bank
 * relocations. It may run in SRAM, with read_block remaining in DDR.
 */
#include "main-state.h"
#ifndef WIN
#define WIN(offset) (*(const volatile u32 *)(ST_STEREO_MAIN+(offset)))
#endif
u32 st_stereo_resolve(void)
{
    u32 print=0,printed=0,k,got=0;
    if(resolved)return use?use->mask:0;
    resolved=1;
    if(!tagged)return 0;
    if(have && !slot[0].mask && WIN(ST_STEREO_SEQ)==slot[0].sequence){
        st_stereo_main_counters.mask=0;return 0;
    }
    if(have && WIN(ST_STEREO_SEQ)==slot[0].sequence){
        if(slot[0].mask){print=st_stereo_print(TRACK_PCM);printed=1;}
        if(!slot[0].mask || slot[0].print==print)use=&slot[0];
    }else{
        /* AUDIO wrote since ev_render_in, or that read was torn: read again.
         * Its DMA takes some 15 us, so a torn read is retried a few times. */
        for(k=0;k<8 && !(got=st_stereo_read_block(&slot[1]));k++){}
        if((got && slot[1].mask) || (have && slot[0].mask)){
            print=st_stereo_print(TRACK_PCM);printed=1;
        }
        if(got && ((!slot[1].mask && !printed) || slot[1].print==print)){
            use=&slot[1];st_stereo_main_counters.late++;
        }else if(have && (!slot[0].mask || (printed && slot[0].print==print)))use=&slot[0];
    }
    if(!use){st_stereo_main_counters.dropped++;return 0;}
    /* A block MAIN has mixed before: AUDIO had no new block, MAIN mixes the
     * same L again, and its D is right again. Only a block from before this
     * boot is never used (the window is not cleared at a restart). */
    if(use->sequence==boot_sequence && !alive){st_stereo_main_counters.stale++;use=0;return 0;}
    alive=1;
    if(use->sequence==last_used)st_stereo_main_counters.again++;
    last_used=use->sequence;
    if(use->mask)st_stereo_main_counters.received++;
    st_stereo_main_counters.mask=use->mask;
    return use->mask;
}
