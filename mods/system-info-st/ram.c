/* SPDX-License-Identifier: GPL-2.0-or-later; ported from Synfact. */
/* SYSTEM INFO: committed external DDR / total external DDR, OS 1.41.
 * Fixed OS image+BSS reservations are counted even when their PCM is silent.
 * The native buddy arena is already inside that reservation: subtract its
 * validated free blocks, never add its allocations a second time.
 * This collector does not classify uncommitted addresses as safe to allocate.
 */
typedef unsigned int si_u32;
typedef int si_s32;
struct si_ram_result { si_u32 total, used, valid; };
#define SI_DDR_BASE 0x40000000u
#define SI_DDR_END  0x48000000u
#define SI_IMAGE   0x40000400u
#define SI_MAIN_END 0x45565490u
#define SI_MAIN_FIXED_EXTRA ((0x478af2d0u-0x45570000u)+(0x48000000u-0x47c00000u))
#define SI_AUDIO_END 0x4404f980u
#define SI_HEAP_CB ((volatile si_u32 *)0x402d4fd4u)
#define SI_ARENA 0x444e38c0u
#define SI_HEADS 0x454e38c0u
#define SI_UNITS 0x100000u
#define SI_MUTEX ((void *)0x44484004u)
#define SI_LOCK ((void (*)(void *))0x40001980u)
#define SI_UNLOCK ((void (*)(void *))0x40001aa2u)

/* Whole percent rounded to nearest, with no overflowing bytes*100 product.
 * Every input is bounded by 128 MiB before multiplication in 16-byte units.
 */
si_u32 si_ram_percent(const struct si_ram_result *r)
{
    si_u32 units;
    if(!r->valid || r->total!=SI_DDR_END-SI_DDR_BASE || r->used>r->total)
        return 0xffffffffu;
    units=r->used>>4;
    return (units*100u+(r->total>>5))/(r->total>>4);
}

void si_ram_main(struct si_ram_result *out)
{
    si_u32 order,units,free_units=0,n,steps,base,next,prev,end,remaining;
    volatile si_u32 *block;
    out->total=SI_DDR_END-SI_DDR_BASE;out->used=0;out->valid=0;
    SI_LOCK(SI_MUTEX);
    if(SI_HEAP_CB[0]!=1 || SI_HEAP_CB[1]!=SI_ARENA ||
       SI_HEAP_CB[3]!=SI_HEADS || SI_HEAP_CB[4]!=20 || SI_HEAP_CB[5]!=SI_UNITS)
        goto done;
    for(order=0;order<=20;order++) {
        units=1u<<order;n=((volatile si_u32 *)SI_HEADS)[order];
        prev=0xffffffffu;steps=0;
        while((si_s32)n>=0) {
            __asm__("move.l #0x100000,%0\nsub.l %1,%0":"=&d"(remaining):"d"(n):"cc");
            if(++steps>4096u || n>=SI_UNITS || (n&(units-1u)) || units>remaining)
                goto done;
            base=SI_ARENA+(n<<4);end=base+(units<<4);
            block=(volatile si_u32 *)base;
            if(block[1]!=prev || block[0]+block[1]+block[2]!=0xffffffffu ||
               *(volatile si_u32 *)(end-4)!=units || units>SI_UNITS-free_units)
                goto done;
            next=block[2];
            if(next!=0xffffffffu && (next>=SI_UNITS || next==n || (next&(units-1u))))
                goto done;
            free_units+=units;prev=n;n=next;
        }
        if(n!=0xffffffffu)goto done;
    }
    /* The BSS end is not the end of MAIN's reservations. Delay, fixed I/O
     * arenas, the 32 MiB send blob, its staging worker and reverb live above
     * it through uncached DDR aliases. Count the union, never both aliases.
     * See system_info_memory_map.py for pinned native bounds/witnesses. */
    out->used=SI_MAIN_END-SI_IMAGE+SI_MAIN_FIXED_EXTRA-(free_units<<4);out->valid=1;
done:
    SI_UNLOCK(SI_MUTEX);
}

/* AUDIO uses static sample/DSP reservations, not MAIN's buddy heap. The
 * runtime stack descends from 48000000. Caller supplies the observed low-water
 * SP from the IRQ meter; zero means the stack measurement is unavailable.
 */
void si_ram_audio(si_u32 stack_low,struct si_ram_result *out)
{
    out->total=SI_DDR_END-SI_DDR_BASE;out->used=0;out->valid=0;
    if((stack_low&3u) || stack_low<SI_AUDIO_END || stack_low>SI_DDR_END)return;
    out->used=SI_AUDIO_END-SI_IMAGE+SI_DDR_END-stack_low;
    out->valid=1;
}

