/* SPDX-License-Identifier: GPL-2.0-or-later
 * MAIN owns presentation only. Sound/locks/undo/transport stay in core.
 * Labels and layout follow the currently displayed Sound, as MD's UI does.
 */
#include <syntakt/machines.h>
#include <syntakt/modules.h>
#include <syntakt/host.h>
#include "types.h"
#include "mono2k/ens-ui.h"

#ifndef DM_VALUE
#define DM_VALUE(view,id,locked) ((st_u32 (*)(void *,st_u32,unsigned char *))0x40039160u)(view,id,locked)
#endif
static char dm_label[8][9] = {"TUNE", "UNIL", "UNIW", "UNIX", " ", "SUBX", "SUB1", "SUB2"};
static st_u32 dm_shown = DM_DEFAULT_TYPE;
static st_u32 dm_layout[16] = {
    0,1,2,3,0xffffffffu,5,6,7,
    DM_TYPE_CONTROL,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu
};
static const struct core_machine_param dm_controls[9] = {
    {dm_label[0],"TUNE",0,0x7f00,0x4000,0x1f1},
    ST_MACHINE_PARAM(dm_label[1],dm_label[1],0,0x7f00,0),
    ST_MACHINE_PARAM(dm_label[2],dm_label[2],0,0x7f00,0x2800),
    ST_MACHINE_PARAM(dm_label[3],dm_label[3],0,0x7f00,0),
    ST_MACHINE_PARAM(dm_label[4],dm_label[4],0,0x7f00,0),
    ST_MACHINE_PARAM(dm_label[5],dm_label[5],0,0x7f00,0),
    ST_MACHINE_PARAM(dm_label[6],dm_label[6],0,0x7f00,0),
    ST_MACHINE_PARAM(dm_label[7],dm_label[7],0,0x7f00,0),
    ST_MACHINE_PARAM("TYPE","MONOMACHINE TYPE",0,(DM_TYPES-1u)<<8,DM_DEFAULT_TYPE<<8)
};
const struct core_machine_def dm_monomachine={71,9,"SY MONOMACHINE","MONO","SYN",dm_controls,ST_MACHINE_NO_INIT,44,10,16};

static int dm_select_type(st_u32 t)
{
    st_u32 i,j;
    int changed = t != dm_shown;
    dm_shown = t;
    for(i=0;i<8;i++) {
        const char *s=dm_labels[t][i];
        for(j=0;s[j];j++)dm_label[i][j]=s[j];
        if(!j)dm_label[i][j++]=' '; /* descriptors require nonempty ASCII */
        dm_label[i][j]=0;
        dm_layout[i]=s[0]?i:0xffffffffu;
    }
    return changed;
}
static int dm_idle(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,st_u32 page)
{
    unsigned char locked=0;
    (void)api;(void)m;(void)page;
    return dm_select_type(dm_clamp_type(DM_VALUE(view,DM_ENGINE+DM_TYPE_CONTROL,&locked)&65535u));
}
/* The list's own Sound model supplies TYPE, even without opening SYN first.
 * UI query 32 only: do not hide runtime flags or change saved destinations. */
static int dm_destination_query(st_u32 q,st_u32 *args,void *model,st_u32 *value)
{
    st_u32 index,t,*vt;
    if(q!=ST_PARAMETER_UI_DEST_FLAGS || args[0]<DM_ENGINE ||
       (index=args[0]-DM_ENGINE)>DM_TYPE_CONTROL || !model)return 0;
    vt=*(st_u32 **)model;
    t=dm_clamp_type(((st_u32 (*)(void *,st_u32))vt[0x28/4])(model,DM_ENGINE+DM_TYPE_CONTROL)&65535u);
    dm_select_type(t);
    *value=index==DM_TYPE_CONTROL || dm_labels[t][index][0]?0xe00u:0;
    return 1;
}
static int dm_destination_init(const struct st_smz_api *api)
{
    if(!api || api->version<1 || !api->ui || api->ui->version<5 ||
       !api->ui->host || api->ui->host->version<15)return 1;
    api->ui->host->parameter_provider(dm_destination_query);
    return 0;
}
const struct st_smz_module dm_destination_module={
    "MONO DESTINATIONS",dm_destination_init,ST_MACHINE_NO_CALLBACK,0
};
/* Labels in LFO destinations are the same live strings as on SYN. */
static char dm_text[20];
static char *dm_number(char *p,int n)
{
    unsigned v;
    if(n<0){*p++='-';v=(unsigned)-n;}else v=(unsigned)n;
    if(v>=100)*p++='0'+v/100;
    if(v>=10)*p++='0'+v/10%10;
    *p++='0'+v%10;*p=0;return p;
}
static const char *dm_signed(int n)
{char *p=dm_text;if(n>0)*p++='+';dm_number(p,n);return dm_text;}
static const char *dm_format(const struct st_machine_ui_api *api,const struct core_machine_def *m,st_u32 index,st_u32 raw)
{
    unsigned v=raw>>8,t=dm_shown,p=index?index-1u:0;
    (void)api;(void)m;
    if(index==DM_TYPE_CONTROL)return dm_names[dm_clamp_type(raw)];
    if(index==0)return 0; /* native TUNE, including its fractional pitch */
    if(index>7 || !dm_labels[t][index][0])return "";
    if(v>127)v=127;
    if(t==DM_SID){
        static const char *const waves[]={"TRI","SAW","PULS","MIX","NOIS"};
        static const char *const mods[]={"OFF","RING","SYNC","R+S"};
        if(p==2)return v>=64?"ON":"OFF";
        if(p==3)return waves[(v*5)>>7];
        if(p==4)return mods[v>>5];
        if(p==5)return v>=64?"PRCH":"MFRQ";
    }
    if((t==DM_FMSTAT && (p==0 || p==4)) || (t==DM_FMPAR && (p==0 || p==2 || p==4))){
        static const char *const ratios[]={"1/64","1/32","1/16","3/32","1/8","5/32","3/16","1/4","5/16","3/8","7/16","1/2","5/8","3/4","7/8","1","1.25","1.5","1.75","2","2.5","3","3.5","4"};
        return ratios[(v*24+8)>>7];
    }
    if(t==DM_ENS && p<3)return T_ENS_PNAME[T_ENS_PIDX[v]];
    if(t==6 && p<3) {
        int s=(int)v-63;return dm_signed(s < -36 ? -36 : s > 36 ? 36 : s);
    }
    if(t==6 && (p==3 || p==4))return p==4 && v==64?"OFF":dm_signed((int)v-64);
    if(t==2 && p==2)return v<43?"1 SAW":v<86?"2 SAWS":"3 SAWS";
    if(t==5 && (p==0 || p==1)) {
        static const char *const vowels[]={"OO","U","AW","AH","UH","AE","EH","IH","EE","ER"};
        return vowels[(v*18+127)/254];
    }
    if(t==5 && p==4) {
        static const char *const consonants[]={"-","S","SH","F","H","T","K","P"};
        return consonants[v>>4];
    }
    if(t==7 && p<3) {
        static const char *const chords[]={"MAJ","MIN","SUS2","SUS4","DIM","AUG","MAJ6","MIN6","MAJ7","MIN7","DOM7","MMAJ7","M7B5","DIM7","7SUS4","ADD9","MADD9","MAJ9","MIN9","DOM9","QUART","FIFTH","OCT","UNI"};
        static const char *const inversions[]={"ROOT","1ST","2ND","3RD"};
        static const char *const spreads[]={"CLOSE","OPEN","WIDE","XWIDE"};
        return p==0?chords[(v*24)>>7]:p==1?inversions[v>>5]:spreads[v>>5];
    }
    dm_number(dm_text,(int)v);return dm_text;
}
const struct st_machine_ui dm_ui = {
    DM_TYPE,2,dm_layout,ST_MACHINE_NO_CALLBACK,dm_idle,
    ST_MACHINE_NO_CALLBACK,dm_format,ST_MACHINE_NO_CALLBACK
};
