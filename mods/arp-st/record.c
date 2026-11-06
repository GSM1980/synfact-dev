/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native live recording writes the last pressed key as a step's NOTE and
 * leaves NOT2..NOT4 inherited. For a held ARP chord, restore the first key
 * as the root and write the other keys as intervals after that native write.
 * This follows POLY's recorder handoff, without changing the stock recorder.
 */
struct arp_rec_chord {arp_u32 sequence,age;unsigned char dirty,step,count,latest,notes[4];};
static struct arp_rec_chord arp_rec_chords[ARP_TRACKS] DATA={{0}};

static arp_u32 arp_rec_active(void)
{
    arp_u32 state=*(volatile arp_u32 *)0x43bd7424u|*(volatile arp_u32 *)0x43bd73bcu;
    arp_u32 object=*(volatile arp_u32 *)0x444e13ecu;
    return state==1 && object && *(volatile unsigned char *)(object+0x169u);
}

static void arp_rec_note(arp_u32 track,arp_u32 note)
{
    unsigned char position[4];int step;
    const volatile unsigned char *data;
    struct arp_rec_chord *chord;
    arp_u32 sequence=*(volatile arp_u32 *)0x43bd7430u,length;
    if(track>=ARP_TRACKS || note>127 || !sequence || !arp_rec_active())return;
    data=(const volatile unsigned char *)sequence+track*ST_CHORD_TRACK_BYTES;
    ((void (*)(arp_u32,arp_u32,unsigned char *))0x4008da6eu)
        (*(volatile arp_u32 *)0x800046ecu,track,position);
    step=(signed char)position[0];
    if(step<0)step=0;if(step>=ST_CHORD_STEPS)step=ST_CHORD_STEPS-1;
    length=data[0x458];
    if(length){
        step=(signed char)data[0x459u+(unsigned int)step%length];
        if(step<0)step=0;if(step>=ST_CHORD_STEPS)step=ST_CHORD_STEPS-1;
    }
    chord=&arp_rec_chords[track];
    if(chord->count && chord->sequence==sequence && chord->step==(unsigned char)step){
        unsigned int i;
        for(i=0;i<chord->count;i++)if(chord->notes[i]==note)break;
        if(i==chord->count && chord->count<4)
            chord->notes[chord->count++]=(unsigned char)note;
        chord->dirty=chord->count>1;
    }else {
        chord->sequence=sequence;chord->step=(unsigned char)step;
        chord->notes[0]=(unsigned char)note;chord->count=1;chord->dirty=0;
    }
    chord->latest=(unsigned char)note;chord->age=0;
}

/* UI tick: the native recorder runs in this task and may still be queued.
 * Wait until its final key has reached the track model before fixing lanes. */
static void arp_rec_ui(void)
{
    arp_u32 t,k,interval,app,pattern,track;
    volatile unsigned char *model,*mirror;
    struct arp_rec_chord *chord;
    unsigned char lanes[4];
    if(!arp_rec_active()){
        for(t=0;t<ARP_TRACKS;t++){arp_rec_chords[t].count=0;arp_rec_chords[t].dirty=0;}
        return;
    }
    for(t=0;t<ARP_TRACKS;t++){
        chord=&arp_rec_chords[t];
        if(!chord->dirty)continue;
        if(chord->sequence!=*(volatile arp_u32 *)0x43bd7430u || ++chord->age>2000u){
            chord->count=0;chord->dirty=0;continue;
        }
        app=*(volatile arp_u32 *)0x444e13f4u;
        if(!app)continue;
        pattern=((arp_u32 (*)(arp_u32))0x40019000u)(app);
        if(!pattern)continue;
        track=pattern+(t+1u)*0x70u;
        model=((volatile unsigned char *(*)(arp_u32))(*(arp_u32 **)track)[0x28/4])(track);
        if(!model)continue;
        model+=chord->step;
        if(model[ST_CHORD_STEP(0,0)]!=chord->latest ||
           model[ST_CHORD_STEP(1,0)]!=ST_CHORD_INHERIT ||
           model[ST_CHORD_STEP(2,0)]!=ST_CHORD_INHERIT ||
           model[ST_CHORD_STEP(3,0)]!=ST_CHORD_INHERIT)continue;
        lanes[0]=chord->notes[0];
        for(k=1;k<4;k++){
            lanes[k]=ST_CHORD_OFF;
            if(k<chord->count){
                interval=(arp_u32)chord->notes[k]+ST_CHORD_OFF-chord->notes[0];
                if(interval<=127u)lanes[k]=(unsigned char)interval;
            }
        }
        mirror=(volatile unsigned char *)chord->sequence+t*ST_CHORD_TRACK_BYTES+chord->step;
        for(k=0;k<4;k++){
            model[ST_CHORD_STEP(k,0)]=lanes[k];
            mirror[ST_CHORD_STEP(k,0)]=lanes[k];
        }
        chord->dirty=0;
    }
}
