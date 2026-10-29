/* Pattern-owned LFO behavior: shared M.LFO or independent POLY. */
extern const volatile u8 poly_owner_map[12];
static u8 poly_modes[8] DATA={0},poly_next_modes[8] DATA={0};
u8 poly_lfo_map[12] DATA={0,1,2,3,4,5,6,7,8,9,10,11};
static u8 poly_post_map[12] DATA={0,1,2,3,4,5,6,7,8,9,10,11};
static u32 poly_shared_mask DATA=0;
extern void poly_fast_publish(u8 *,const volatile u8 *);
extern void poly_mod_shared(void);
extern void poly_mod_all(void);

static void poly_play_maps(const volatile u8 *owners)
{
    u32 t,o,independent;
    poly_shared_mask=0;
    for(t=0;t<12;t++) {
        o=owners[t];independent=t<8 && o<t && poly_modes[o]==1;
        poly_lfo_map[t]=(u8)(independent?t:o);
        poly_post_map[t]=(u8)(independent?t:o);
        if(!independent && o<t)poly_shared_mask|=1u<<t;
    }
}
