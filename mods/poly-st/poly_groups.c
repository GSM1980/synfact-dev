/* Laboratory core; callers provide all storage. No fixed production allocation. */
typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned short u16;
typedef signed int s32;


u32 poly_resolve(const volatile u8 *states,volatile u8 *owners)
{
    u32 t,j,width;
    for(t=0;t<12;t++)owners[t]=(u8)t;
    for(t=0;t<8;t++)if(states[t]>5)return 0;
    t=0;
    while(t<8) {
        if(states[t]<3){t++;continue;}
        width=states[t]-1u;
        if(t+width>8)return 0;
        for(j=1;j<width;j++)if(states[t+j]>=3)return 0;
        t+=width;
    }
    t=0;
    while(t<8) {
        if(states[t]<3){t++;continue;}
        width=states[t]-1u;
        for(j=1;j<width;j++)owners[t+j]=(u8)t;
        t+=width;
    }
    return 1;
}

u32 poly_owner(const volatile u8 *owners,u32 track)
{
    u32 owner;
    if(track>=8)return track;
    owner=owners[track];
    return owner<=track?owner:track;
}

/* 128 is an internal omitted-note sentinel, never sent as a MIDI note. */
u32 poly_note(volatile u32 root,volatile u32 interval)
{
    s32 note;
    if(root>127 || interval>127 || interval==64)return 128;
    note=(s32)root+(s32)interval-64;
    return note>=0 && note<=127?(u32)note:128;
}

