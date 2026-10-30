/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <syntakt/modules.h>
#include <syntakt/host.h>
#include <syntakt/sample-pool.h>
static const struct st_machine_host_api *host;
#define MANAGER_SERVICES 1
#define MALLOC host->storage->alloc
#define FREE host->storage->free
#define MMC_READ host->storage->read
#define core_hzfs_open host->hz_open
#define core_hzfs_close host->hz_close
#define core_hzfs_native_io (*host->storage)
#define core_pool_valid host->pool->valid
#define core_pool_read host->pool_read
#define core_pool_write host->pool_write
#define core_pool_ready host->pool_ready
#define core_pool_cached_name host->pool->cached_name
#define st_pool_live_sync host->pool_apply
#define st_pool_preview_stop host->preview_stop
#define manager_preview host->pool->preview
#define manager_refill host->pool->refill
#define manager_preview_release host->pool->release
#include "ui.c"
static void menu(void *m){st_pool_ui_menu(m);}
static const struct st_pool_ui_callbacks callbacks={1,menu,st_pool_ui_progress_begin,st_pool_ui_progress_end,manager_file,manager_progress};
static int manager_key(void *app,void *ev){return st_pool_ui_pre_event(app,ev);}
static int initialize(const struct st_smz_api *api)
{
    if(!api || api->version<1 || !api->ui || api->ui->version<5 || !api->ui->host || api->ui->host->version<11 || !api->ui->host->pool || api->ui->host->pool->version!=1)return 1;
    host=api->ui->host;return !host->pool->bind_ui(&callbacks);
}
static const struct st_smz_subscription subscriptions[]={
    {ST_EV_KEY,(void (*)(void))manager_key},
    {ST_EV_TICK,(void (*)(void))manager_tick}};
const struct st_smz_module sample_manager_module={"Sample Manager",initialize,subscriptions,2};
