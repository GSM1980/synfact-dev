/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise the real read-only preflight against the linked MAIN image.
 * Target code never executes; one corrupt preimage must refuse the bank. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mods/core-st/boot-preimages.h"
static core_u8 *image;static unsigned long length;
core_u8 core_boot_byte(core_u32 at)
{
    unsigned long offset=at-0x40000400u;
    assert(at>=0x40000400u && offset<length);
    return image[offset];
}
int main(int argc,char **argv)
{
    FILE *f;core_u8 info[120]={0};unsigned long at;
    assert(argc==2);f=fopen(argv[1],"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));length=(unsigned long)ftell(f);rewind(f);
    image=malloc(length);assert(image && fread(image,1,length,f)==length);fclose(f);
    memcpy(info,"lfo3-st",8);memcpy(info+24,"1.0",4);
    memcpy(info+40,"modulation-skip-st",19);memcpy(info+64,"1.0",4);
    assert(core_boot_preimages_ok(info));
    at=0x40011e4au-0x40000400u;image[at]^=1;
    assert(!core_boot_preimages_ok(info));image[at]^=1;
    at=0x40097b04u-0x40000400u;image[at]^=1;
    assert(!core_boot_preimages_ok(info));image[at]^=1;
    memset(info,0,sizeof(info));memcpy(info,"system-info-smz-st",19);
    assert(core_boot_preimages_ok(info));assert(!core_boot_preimages_ok(0));
    free(image);puts("PASS: generated stock preimages, both hook modules and corrupt-site refusal");
    return 0;
}
