/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native 40119684 updates this set before applying note priority. The head at
 * +410h is newest, following node[1] walks backwards in insertion order.
 * Never follow an unchecked address or write to the firmware's held-note set.
 */
#include "keyboard.h"
_Static_assert(__builtin_offsetof(struct arp_selector,count)==144,"ARP keyboard count offset");
_Static_assert(__builtin_offsetof(struct arp_selector,mask)==0,"ARP keyboard mask offset");
#ifdef __mcoldfire__
/* The assembly entry returns before setting up this work frame when all four
 * masks match. Native exclusion keeps its snapshot stable across the tail jump. */
int arp_keyboard_sync_changed(struct arp_selector *s,const volatile arp_u32 *native)
#else
int arp_keyboard_sync(struct arp_selector *s,const volatile arp_u32 *native)
#endif
{
    arp_u32 mask[4],seen[4]={0},base=(arp_u32)native,node,n,bit;
    unsigned char order[128];unsigned int i,count=0,kept=0,removed=0;
    int cursor=s->cursor;
#ifdef __mcoldfire__
    for(i=0;i<4;i++)mask[i]=native[i];
#else
    {
        int changed=0;
        for(i=0;i<4;i++){mask[i]=native[i];if(mask[i]!=s->mask[i])changed=1;}
        if(!changed)return (int)s->count;
    }
#endif
    node=native[0x410/4];
    while(node) {
        if(count>=128 || node<base+16 || node>=base+1040 || (node-base-16)%8)return -1;
        n=(node-base-16)/8;bit=1u<<(n&31);
        if(!(mask[n>>5]&bit) || seen[n>>5]&bit)return -1;
        seen[n>>5]|=bit;order[count++]=(unsigned char)n;
        node=((const volatile arp_u32 *)node)[1];
    }
    for(i=0;i<4;i++)if(mask[i]!=seen[i])return -1;
    /* Equivalent to releasing absent notes then arp_hold in insertion order,
     * with linear compaction instead of repeatedly shifting the order array. */
    for(i=0;i<s->count;i++) {
        n=s->order[i];
        if(mask[n>>5]&(1u<<(n&31)))s->order[kept++]=(unsigned char)n;
        else if((int)i<=cursor)removed++;
    }
    s->cursor=cursor-(int)removed;s->count=kept;
    /* The old bitmap is untouched until the final commit below. Read it
     * directly rather than copying another four words into the work frame. */
    for(i=count;i;i--){n=order[i-1];if(!(s->mask[n>>5]&(1u<<(n&31))))s->order[s->count++]=(unsigned char)n;}
    for(i=0;i<4;i++)s->mask[i]=mask[i];
    return (int)s->count;
}
