/* Shared configuration in the existing SRAM bank, private oscillator state.
 * No DDR parameter copy: the renderer keeps its original source pointer.
 * Slave configuration may be overwritten (user-approved performance policy). */
typedef u32 lfo3_param_word __attribute__((may_alias));
static const u16 *lfo3_voice_bank(const u16 *params)
{
    const u8 *owners,*states;u32 query=1,t,o;lfo3_param_word *d;const lfo3_param_word *s;
    if(host->version<8 || !host->native_query)return params;
    owners=(const u8 *)host->native_query(ST_NATIVE_OWNERS,0);
    if(!owners)return params;
    states=(const u8 *)host->native_query(ST_NATIVE_OWNERS,&query);
    if(!states)return params;
    /* Both base and lock-overlay rows are mutable, four-byte-aligned SRAM
     * records supplied by lfo3_events_bank (or the same bank's DDR fallback). */
    for(t=1;t<8;t++)if((o=owners[t])<t && states[t]==t) {
        d=(lfo3_param_word *)(params+t*8);s=(const lfo3_param_word *)(params+o*8);
        d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3];
    }
    return params;
}
