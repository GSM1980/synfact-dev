/* Pattern-owned LFO behavior: shared M.LFO or independent POLY. */
static u8 poly_modes[8] DATA={0},poly_next_modes[8] DATA={0};
u8 poly_lfo_map[12] DATA={0,1,2,3,4,5,6,7,8,9,10,11};
static u8 poly_pre_map[12] DATA={0,1,2,3,4,5,6,7,8,9,10,11};
static u8 poly_post_map[12] DATA={0,1,2,3,4,5,6,7,8,9,10,11};
static u8 poly_lfo_saved[8][80] DATA={{0}};
static u32 poly_lfo_saved_mask DATA=0;
extern void poly_fast_publish(u8 *,const volatile u8 *);
extern void poly_mod_shared(void);

static void poly_play_maps(const volatile u8 *owners)
{
    u32 t,o,i,bit,independent;u8 old[8];
    volatile u8 *state;
    const volatile u8 *source;
    for(t=0;t<12;t++) {
        o=owners[t];bit=1u<<t;
        independent=t<8 && o<t && poly_modes[o]==1;
        if(t<8)old[t]=poly_pre_map[t];
        poly_lfo_map[t]=(u8)(independent?t:o);
        poly_pre_map[t]=(u8)(independent?o:t);
        poly_post_map[t]=(u8)(independent?t:o);
    }
    /* Restore departing voices before another group can inherit their state. */
    for(t=1;t<8;t++)if((poly_lfo_saved_mask&(1u<<t)) &&
        (poly_pre_map[t]==t || old[t]!=poly_pre_map[t])) {
        state=(volatile u8 *)(0x444da72cu+t*80u);
        for(i=0;i<80;i++)state[i]=poly_lfo_saved[t][i];
        poly_lfo_saved_mask&=~(1u<<t);
    }
    for(t=1;t<8;t++) {
        o=poly_pre_map[t];bit=1u<<t;
        state=(volatile u8 *)(0x444da72cu+t*80u);
        if(o<t && !(poly_lfo_saved_mask&bit)) {
            source=(const volatile u8 *)(0x444da72cu+o*80u);
            for(i=0;i<80;i++){poly_lfo_saved[t][i]=state[i];state[i]=source[i];}
            poly_lfo_saved_mask|=bit;
        }
    }
}

/* Projection must happen before modulation: late copying would overwrite
 * the slave's own LFO result. Controller inputs inherit the master; each
 * voice keeps its own pitch and velocity. No saved Sound is overwritten. */
static void poly_modulate(u8 *working)
{
    static const u32 inputs[4]={0x800046f8u,0x80004712u,0x8000472cu,0x80004746u};
    u16 saved[8][4];u32 t,o,j;
    poly_fast_publish(working,poly_pre_map);
    for(t=1;t<8;t++)if((o=poly_pre_map[t])<t)
        for(j=0;j<4;j++) {
            volatile u16 *v=(volatile u16 *)inputs[j];
            saved[t][j]=v[t];v[t]=v[o];
        }
    ((void (*)(u8 *))poly_mod_shared)(working);
    for(t=1;t<8;t++)if(poly_pre_map[t]<t)
        for(j=0;j<4;j++)((volatile u16 *)inputs[j])[t]=saved[t][j];
}

