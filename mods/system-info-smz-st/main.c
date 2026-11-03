/* SPDX-License-Identifier: GPL-2.0-or-later
 * Same telemetry implementation; UI/collectors live in the optional bank.
 */
#include <syntakt/modules.h>
#include <syntakt/host.h>
static const struct st_machine_host_api *si_host;
#define ST_SYSTEM_INFO_MODULE 1
#define core_clock_snapshot si_host->clock_snapshot
#define core_additem si_host->additem
#define core_pool_busy si_host->pool_busy
#define core_pool_query_owner si_host->query_owner
#define SI_CORE_INFO (si_host->core_info)
#define SI_BANK_INFO (si_host->bank_info ? *si_host->bank_info : 0)
#define fw_invalidate ((void (*)(void *))0x400ffc5eu)
#define fw_fillrect ((void (*)(void *,int,int,int,int,unsigned int))0x400f7df2u)
#define fw_bitmap_ctor ((void (*)(void *,unsigned int,unsigned int,const void *,const void *))0x400f70fau)
#define fw_bitmap_dtor ((void (*)(void *))0x400f702cu)
#define fw_blit ((void (*)(void *,const void *,int,int,unsigned int))0x400f8dacu)
extern void si_label(void);
#include "../system-info-st/system_info.c"
static int si_initialize(const struct st_smz_api *api)
{
    if(!api || !api->ui || api->ui->version<6 || !api->ui->host || api->ui->host->version<12)return 1;
    si_host=api->ui->host;return 0;
}
static const struct st_smz_subscription si_subscriptions[]={
    {ST_EV_SETTINGS,(void (*)(void))si_settings},
    {ST_EV_TICK,(void (*)(void))si_tick},
    {ST_EV_DRAW,(void (*)(void))si_draw},
    {ST_EV_KEY,(void (*)(void))si_key},
    {ST_EV_ENC,(void (*)(void))si_enc}};
const struct st_smz_module system_info_module={"SYSTEM INFO",si_initialize,si_subscriptions,5};
