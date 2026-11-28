/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <syntakt/sound-meta.h>
typedef unsigned int u32;

extern int core_parameter_provider(u32,u32 *,void *,u32 *);
extern void core_kit_notify(u32,u32,u32,u32,u32);

volatile u32 core_voice_groups=~0u;
static int pitch_offsets[13];  /* FX, track 12, always retains its native pitch. */
const int *core_note_pitch_bias;

void core_voice_groups_set(u32 grouped,const int *offsets)
{
    u32 i;
    if(offsets)for(i=0;i<12;i++)pitch_offsets[i]=offsets[i];
    core_note_pitch_bias=offsets?pitch_offsets:0;
    core_voice_groups=grouped;
}

void core_sound_setup_changed(void *record,void *model)
{
    core_kit_notify(ST_SOUND_SETUP,(u32)record,(u32)model,1,1);
}

void core_track_sound_loaded(u32 track)
{
    core_kit_notify(ST_SOUND_TRACK_LOAD,track,0,0,1);
}

u32 core_track_owner(u32 track)
{
    u32 owner;
    if(track<12 && core_parameter_provider(38,&track,0,&owner) && owner<=track)return owner;
    return track;
}

u32 core_relation_ui_read(const void *kit,u32 source,u32 mask)
{
    u32 args[3]={(u32)kit,source,mask},result;
    return core_parameter_provider(39,args,0,&result)?result:mask;
}

void core_relation_ui_set(void *model,u32 source,u32 target,u32 enabled)
{
    u32 owner=core_track_owner(source),voice,bit,targets,mask;
    const unsigned short *kit;
    if(source>=13 || target>=13 || core_track_owner(target)!=target || owner==target)return;
    bit=1u<<target;targets=bit;
    for(voice=target+1;voice<12 && core_track_owner(voice)==target;voice++)targets|=1u<<voice;
    if(owner==source && targets==bit) {
        ((void (*)(void *,u32,u32,u32))0x40010056u)(model,source,target,enabled);
        return;
    }
    kit=((const unsigned short *(*)(void *))(*(const u32 **)model)[0x34/4])(model);
    if(!kit)return;
    mask=kit[0xaf2u+owner]&~targets;
    ((void (*)(void *,u32,u32))0x4001013au)(model,owner,mask|(enabled?bit:0));
}
