/* PR: retain the experimental four-code rank geometry, including OFF voice
 * preferences, inside the existing 24-bit PQ payload. Only codes 0/1 remain active.
 * No adjacent native header byte is used. PQ still decodes as shared LFO. */
static const u32 poly_counts[9]={1,3,21,135,873,5319,33561,210519,1320057};
static const u32 poly_pow3[4]={1,3,9,27};

static u32 poly_crc(const volatile u8 *p)
{
    u32 i,j,crc=65535u;
    for(i=0;i<5;i++) {
        crc=xor_bits(crc,(u32)p[i]<<8);
        for(j=0;j<8;j++)crc=xor_bits(crc<<1,(crc&32768u)?0x1021u:0u)&65535u;
    }
    return crc;
}

static u32 poly_decode_record(const volatile u8 *p,u8 *states,u8 *modes)
{
    u32 i,rank,n,w,block,code,j,analog=0,tag=p[1];
    for(i=0;i<12;i++)states[i]=0;
    for(i=0;i<POLY_MODE_TRACKS;i++)modes[i]=0;
    if(p[0]!=0x50)return 0;
    rank=((u32)p[2]<<16)+((u32)p[3]<<8)+p[4];
    if(p[1]==0x51 || (p[1]&0xf8u)==0x58u) {
        if(!legacy_valid(p))return 0;
        for(i=0;i<8;i++)states[i]=(u8)((rank>>(21-3*i))&7u);
        if(p[1]!=0x51) {
            if(p[1]&4u)states[8]=(p[1]&2u)?4:3;
            if(p[1]&1u)states[9]=3;
        }
        return 1;
    }
    /* PT uses the PS payload and selects P.MOD for its analog group. */
    if(tag==0x53 || tag==0x54){analog=rank&3u;rank>>=2;}
    if(tag<0x52 || tag>0x54 || (tag==0x54 && !analog) ||
       poly_crc(p)!=(((u32)p[5]<<8)+p[6]) || rank>=poly_counts[8])return 0;
    if(analog==1)states[8]=3;
    if(analog==2)states[8]=4;
    if(analog==3)states[9]=3;
    if(tag==0x54)modes[analog==3?9:8]=1;
    i=0;
    while(i<8) {
        n=8-i;block=3*poly_counts[n-1];
        if(rank<block) {
            states[i]=(u8)(rank/poly_counts[n-1]);
            rank%=poly_counts[n-1];i++;continue;
        }
        rank-=block;
        for(w=2;w<=4 && w<=n;w++) {
            block=4*poly_pow3[w-1]*poly_counts[n-w];
            if(rank>=block){rank-=block;continue;}
            states[i]=(u8)(w+1);
            code=rank/poly_counts[n-w];rank%=poly_counts[n-w];
            modes[i]=(u8)(code/poly_pow3[w-1]);code%=poly_pow3[w-1];
            for(j=1;j<w;j++){states[i+j]=(u8)(code%3);code/=3;}
            break;
        }
        if(w>4 || w>n)return 0;
        i+=w;
    }
    return rank==0;
}

/* Keep the experimental PR geometry readable. Retired mono/legato values
 * migrate to M.LFO, retaining the group and every OFF voice preference. */
static u32 poly_decode(const volatile u8 *p,u8 *states,u8 *modes)
{
    u32 ok=poly_decode_record(p,states,modes),i;
    for(i=0;i<8;i++)if(modes[i]>1)modes[i]=0;
    return ok;
}

static u32 poly_rank(const u8 *states,const u8 *modes,u32 i)
{
    u32 n=8-i,w,j,rank,code=0;
    if(!n)return 0;
    if(states[i]<3)return states[i]*poly_counts[n-1]+poly_rank(states,modes,i+1);
    w=states[i]-1;
    rank=3*poly_counts[n-1];
    for(j=2;j<w;j++)rank+=4*poly_pow3[j-1]*poly_counts[n-j];
    for(j=1;j<w;j++)code+=states[i+j]*poly_pow3[j-1];
    code+=modes[i]*poly_pow3[w-1];
    return rank+code*poly_counts[n-w]+poly_rank(states,modes,i+w);
}

static void poly_encode(u8 *p,const u8 *states,const u8 *modes)
{
    u32 i,bits=0,crc,analog=states[8]>=3?(states[8]==4?2u:1u):states[9]>=3?3u:0u;
    u32 analog_independent=analog && modes[analog==3?9:8]==1,extended=analog_independent;
    for(i=0;i<8;i++) {
        bits=(bits<<3)+states[i];
        if(states[i]>=3 && modes[i])extended=1;
    }
    if(extended)bits=poly_rank(states,modes,0);
    p[0]=0x50;p[1]=(u8)(extended?0x52:0x51);
    if(analog) {
        if(extended){bits=(bits<<2)|analog;p[1]=(u8)(analog_independent?0x54:0x53);}
        else p[1]=(u8)(analog==1?0x5c:analog==2?0x5e:0x59);
    }
    p[2]=(u8)(bits>>16);p[3]=(u8)(bits>>8);p[4]=(u8)bits;
    crc=poly_crc(p);p[5]=(u8)(crc>>8);p[6]=(u8)crc;
}

/* The stock pattern writer saves exactly 40 header bytes. Re-rank digital
 * configurations into 35 bits and the analog group into nine, so all values
 * fit the existing seven-byte record with a four-bit tag and CRC-8. */
typedef unsigned long long poly_rank_t;
#define UNISON_VALUES (POLY_UNISON_MAX+1u)
static const u32 poly_unison_counts[8]={1,3,315,2781,110241,1479141,41591637,686252169};

static u32 poly_unison_crc(const volatile u8 *p)
{
    u32 i,j,crc=0;
    for(i=0;i<6;i++) {
        crc=xor_bits(crc,p[i]);
        for(j=0;j<8;j++)crc=xor_bits(crc<<1,(crc&128u)?7u:0u)&255u;
    }
    return crc;
}

/* The factor is at most 2*51*27. Two word products avoid wide division and
 * keep this configuration-only codec independent of compiler runtime calls. */
static poly_rank_t poly_unison_product(u32 unit,u32 factor)
{
    return ((poly_rank_t)((unit>>16)*factor)<<16)+(unit&65535u)*factor;
}

static u32 poly_unison_take(poly_rank_t *rank,u32 unit,u32 size)
{
    u32 low=0,mid;
    while(size-low>1) {
        mid=(size+low)>>1;
        if(poly_unison_product(unit,mid)<=*rank)low=mid;
        else size=mid;
    }
    *rank-=poly_unison_product(unit,low);
    return low;
}

static u32 poly_decode_settings(const volatile u8 *p,u8 *states,u8 *modes,u8 *values)
{
    poly_rank_t rank,block;
    u32 i,n,w,j,unit,size,code,analog,active=0;
    for(i=0;i<POLY_MODE_TRACKS;i++)values[i]=0;
    if((p[0]&0xf0u)!=0xa0u)return poly_decode(p,states,modes);
    if(p[6]!=poly_unison_crc(p))return 0;
    for(i=0;i<12;i++)states[i]=0;
    for(i=0;i<POLY_MODE_TRACKS;i++)modes[i]=0;
    analog=((u32)(p[0]&8u)<<5)|p[5];
    if(analog>6*UNISON_VALUES)return 0;
    if(analog) {
        analog--;code=analog/UNISON_VALUES;
        i=code>=4?9u:8u;
        states[i]=code==2 || code==3?4:3;
        modes[i]=(u8)(code&1u);
        values[i]=(u8)(analog%UNISON_VALUES);active=values[i];
    }
    rank=((poly_rank_t)(p[0]&7u)<<32)|((u32)p[1]<<24)|((u32)p[2]<<16)|((u32)p[3]<<8)|p[4];
    i=0;
    while(i<8) {
        n=8-i;unit=poly_unison_counts[n-1];
        block=poly_unison_product(unit,3);
        if(rank<block) {
            states[i++]=(u8)poly_unison_take(&rank,unit,3);continue;
        }
        rank-=block;
        for(w=2;w<=4 && w<=n;w++) {
            unit=poly_unison_counts[n-w];size=2*UNISON_VALUES*poly_pow3[w-1];
            block=poly_unison_product(unit,size);
            if(rank>=block){rank-=block;continue;}
            code=poly_unison_take(&rank,unit,size);
            states[i]=(u8)(w+1);
            for(j=1;j<w;j++){states[i+j]=(u8)(code%3);code/=3;}
            modes[i]=(u8)(code&1u);
            values[i]=(u8)(code>>1);active|=values[i];
            break;
        }
        if(w>4 || w>n)return 0;
        i+=w;
    }
    return rank==0 && active;
}

static void poly_encode_settings(u8 *p,const u8 *states,const u8 *modes,const u8 *values)
{
    poly_rank_t rank=0;
    u32 i,j,n,w,code,analog=0,active=0;
    for(i=0;i<POLY_MODE_TRACKS;i++)if(states[i]>=3)active|=values[i];
    if(!active){poly_encode(p,states,modes);return;}
    if(states[8]>=3)analog=1+(2u*(states[8]-3u)+modes[8])*UNISON_VALUES+values[8];
    if(states[9]>=3)analog=1+(4u+modes[9])*UNISON_VALUES+values[9];
    i=0;
    while(i<8) {
        n=8-i;
        if(states[i]<3) {
            rank+=poly_unison_product(poly_unison_counts[n-1],states[i]);i++;continue;
        }
        w=states[i]-1;
        rank+=poly_unison_product(poly_unison_counts[n-1],3);
        for(j=2;j<w;j++)rank+=poly_unison_product(poly_unison_counts[n-j],2*UNISON_VALUES*poly_pow3[j-1]);
        code=(2u*values[i]+modes[i])*poly_pow3[w-1];
        for(j=1;j<w;j++)code+=states[i+j]*poly_pow3[j-1];
        rank+=poly_unison_product(poly_unison_counts[n-w],code);
        i+=w;
    }
    p[0]=(u8)(0xa0u|((analog>>5)&8u)|(u32)(rank>>32));
    p[1]=(u8)(rank>>24);p[2]=(u8)(rank>>16);p[3]=(u8)(rank>>8);p[4]=(u8)rank;
    p[5]=(u8)analog;p[6]=(u8)poly_unison_crc(p);
}

static u32 valid(const volatile u8 *p)
{
    u8 states[12],modes[POLY_MODE_TRACKS],values[POLY_MODE_TRACKS];
    return poly_decode_settings(p,states,modes,values);
}

/* Legacy analog groups retain the same shared-mode PQ/PX serialization. */
static void record_encode(const u8 *states,u8 *p)
{const u8 modes[POLY_MODE_TRACKS]={0};poly_encode(p,states,modes);}
