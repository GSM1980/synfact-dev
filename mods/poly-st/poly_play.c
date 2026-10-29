/* Pattern-owned LFO behavior: shared M.LFO or independent POLY. */
static u8 poly_modes[8] DATA={0},poly_next_modes[8] DATA={0};
u8 poly_lfo_map[12] DATA={0,1,2,3,4,5,6,7,8,9,10,11};
static u8 poly_pre_map[12] DATA={0,1,2,3,4,5,6,7,8,9,10,11};
static u8 poly_post_map[12] DATA={0,1,2,3,4,5,6,7,8,9,10,11};
static u32 poly_independent_mask DATA=0;
static u32 poly_shared_mask DATA=0;
extern void poly_fast_publish(u8 *,const volatile u8 *);
extern void poly_mod_shared(void);
extern void poly_mod_all(void);

static void poly_play_maps(const volatile u8 *owners)
{
    u32 t,o,independent;
    poly_independent_mask=poly_shared_mask=0;
    for(t=0;t<12;t++) {
        o=owners[t];independent=t<8 && o<t && poly_modes[o]==1;
        poly_lfo_map[t]=(u8)(independent?t:o);
        poly_pre_map[t]=(u8)(independent?o:t);
        poly_post_map[t]=(u8)(independent?t:o);
        if(independent)poly_independent_mask|=1u<<t;
        else if(o<t)poly_shared_mask|=1u<<t;
    }
}

/* Project before native modulation. Controller inputs inherit the master
 * directly; restoring the slave's old inputs/configuration is not required.
 * Each voice retains its own pitch, velocity and oscillator state. */
static void poly_modulate(u8 *working)
{
    u32 t,o;
    poly_fast_publish(working,poly_pre_map);
    for(t=1;t<8;t++)if((o=poly_pre_map[t])<t) {
        ((volatile u16 *)0x800046f8u)[t]=((volatile u16 *)0x800046f8u)[o];
        ((volatile u16 *)0x80004712u)[t]=((volatile u16 *)0x80004712u)[o];
        ((volatile u16 *)0x8000472cu)[t]=((volatile u16 *)0x8000472cu)[o];
        ((volatile u16 *)0x80004746u)[t]=((volatile u16 *)0x80004746u)[o];
    }
    ((void (*)(u8 *))(poly_shared_mask?poly_mod_shared:poly_mod_all))(working);
}
