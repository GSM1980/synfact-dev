/* Shared Sound ownership, independent per-voice oscillator state. This is
 * queried through the core; no dependency on POLY's symbols or allocations. */
static u16 lfo3_voice_params[13][8];
static u8 lfo3_saved_voice[8][40];
static u32 lfo3_saved_voices;
static u8 lfo3_voice_owner[8];

static const u16 *lfo3_voice_bank(const u16 *params)
{
    const u8 *owners,*states;u32 query=1,t,o,i,mask=0,bit;
    if(host->version<8 || !host->native_query)return params;
    owners=(const u8 *)host->native_query(ST_NATIVE_OWNERS,0);
    states=owners?(const u8 *)host->native_query(ST_NATIVE_OWNERS,&query):0;
    if(owners && states)for(t=1;t<8;t++)
        if(owners[t]<t && states[t]==t)mask|=1u<<t;
    if(!mask && !lfo3_saved_voices)return params;
    for(t=1;t<8;t++)if((lfo3_saved_voices&(1u<<t)) &&
        (!(mask&(1u<<t)) || lfo3_voice_owner[t]!=owners[t])) {
        for(i=0;i<40;i++)lfo3_bank->state[t][i]=lfo3_saved_voice[t][i];
        lfo3_saved_voices&=~(1u<<t);
    }
    for(t=1;t<8;t++) {
        bit=1u<<t;
        if((mask&bit) && !(lfo3_saved_voices&bit)) {
            o=owners[t];
            lfo3_voice_owner[t]=(u8)o;
            for(i=0;i<40;i++) {
                lfo3_saved_voice[t][i]=lfo3_bank->state[t][i];
                lfo3_bank->state[t][i]=lfo3_bank->state[o][i];
            }
        }
    }
    lfo3_saved_voices=mask;
    if(!mask)return params;
    for(t=0;t<13;t++) {
        o=(mask&(1u<<t))?owners[t]:t;
        for(i=0;i<8;i++)lfo3_voice_params[t][i]=params[o*8+i];
    }
    return &lfo3_voice_params[0][0];
}
