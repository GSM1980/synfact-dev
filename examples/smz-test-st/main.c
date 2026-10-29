/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optional module, not a machine. No firmware sites or binary patch plan.
 */
#include <syntakt/modules.h>
static st_u32 ticks;
static int start(const struct st_smz_api *api){ticks=0;return api->version==1?0:1;}
static void tick(void *controller){(void)controller;ticks++;}
static void draw(void *bitmap,void *controller)
{
    (void)controller;
    ((void (*)(void *,void *,int,int,st_u32,const char *,...))0x400f8a0cu)
        (bitmap,(void *)0x443da210u,88,0,0,"SMZ %lu",(unsigned long)ticks);
}
static const struct st_smz_subscription events[] __attribute__((aligned(4)))={
    {ST_EV_TICK,(void (*)(void))tick},{ST_EV_DRAW,(void (*)(void))draw}};
const struct st_smz_module test_module={"SMZ TEST",start,events,2};
