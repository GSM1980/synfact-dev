/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../mods/core-st/crc.h"
static unsigned slow(unsigned c,const unsigned char *p,unsigned n)
{unsigned j;while(n--){c^=*p++;for(j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1));}return c;}
int main(void)
{
    unsigned char data[4097];unsigned i,n,seed;
    for(i=0;i<sizeof(data);i++)data[i]=(unsigned char)(i*179u+13u);
    for(seed=0;seed<5;seed++)for(n=0;n<=4096;n+=17)
        assert(core_crc_update(0x12345678u*seed,data,n)==slow(0x12345678u*seed,data,n));
    assert(core_crc_update(0,data,4097)==slow(0,data,4097));
    puts("PASS: shared table CRC matches bitwise CRC for seeds, empty, fractional chunks and full blocks");return 0;
}
