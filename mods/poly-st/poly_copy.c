/* Non-destructive voice setup (1.7), as analog slaves always had: a digital
 * slave keeps its own saved Sound in the KIT. Its voice takes the master's
 * Sound by publication only, on group entry, voice growth and task-side
 * edits; every note routed to it binds the master's kit Sound
 * (poly_voice_target). Leaving the group publishes the slave's own Sound
 * again (poly_commit). The render loop never copies or publishes Sounds.
 * Native event copies already share the master's reference-counted lock
 * object, so p-locks are applied by the native per-track event path. */
static volatile u32 poly_copy_pending DATA=0;

static void poly_copy_mark(u32 owner)
{
    u32 t;
    for(t=owner+1;t<8;t++)if(poly_owner_map[t]==owner)poly_copy_pending|=1u<<t;
}

static void poly_copy_group(u32 track,u32 owner)
{
    u32 kit=*(volatile u32 *)0x43bd843cu;
    if(!kit || track>=8 || owner>=track)return;
    /* Publish from the master: optional LFO3 knows that Sound's own record.
     * This path runs at the existing IRQ-masked configuration boundary. */
    native_publish_sound((const volatile u8 *)(kit+0x2au+owner*0x123u),track);
    poly_copy_pending|=1u<<track;
}

/* UI task after a completed native setter: publish the master's edited Sound
 * to its slaves. LFO3 takes the shadow record of that source Sound, so the
 * slaves' own records and shadows stay untouched. No heap work in an IRQ, no
 * polling/comparing 291-byte records on every render block. */
static void poly_copy_ui(void)
{
    u32 pending=poly_copy_pending,kit=*(volatile u32 *)0x800030b8u,t,o;
    u32 command[6]={0x504f4c59u,0x434f5059u,0,0,0,0};
    if(!pending || !kit)return;
    /* The native private-command boundary exchanges the mask under IRQ
     * exclusion; a newly queued voice cannot be lost between load/clear. */
    native_immediate(command);pending=command[5];
    for(t=1;t<8;t++) {
        o=poly_owner_map[t];
        if(o>=t || !(pending&(1u<<t)))continue;
        native_publish_sound((const volatile u8 *)(kit+0x2au+o*0x123u),t);
    }
}
