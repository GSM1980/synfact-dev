/* SPDX-License-Identifier: GPL-2.0-or-later
 * Task-only kit lifecycle, shared parameter-provider notifications (API 16).
 * Callback 34 receives {kind,a,b,n,phase}; phase 0 before codec, 1 after.
 * Query 35 may supply a synchronous task memcpy entry (same C ABI), allowing
 * LFO3's established owning-object copy path to stay intact. */
#include <syntakt/sound-meta.h>
typedef unsigned int u32;
extern int core_parameter_provider(u32,u32 *,void *,u32 *);
extern void *core_meta_copy_stock(void *,const void *,u32);
void core_kit_notify(u32 kind,u32 a,u32 b,u32 n,u32 phase)
{u32 args[5]={kind,a,b,n,phase};core_parameter_provider(34,args,0,0);}
void *core_meta_copy_task(void *dst,const void *src,u32 n)
{
    u32 a[3]={(u32)dst,(u32)src,n},fn=0;void *result;
    core_parameter_provider(35,a,0,&fn);
    result=fn?((void *(*)(void *,const void *,u32))fn)(dst,src,n):core_meta_copy_stock(dst,src,n);
    core_kit_notify(ST_SOUND_COPY,(u32)dst,(u32)src,n,1);return result;
}
