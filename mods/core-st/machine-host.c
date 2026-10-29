/* SPDX-License-Identifier: GPL-2.0-or-later
 * Expose existing services; no new hooks, storage implementation or dispatcher.
 */
#include <syntakt/host.h>
#include "sample-pool.h"
#include "crc.h"
extern int st_pool_live_sync(const unsigned short *);
extern void st_pool_preview_stop(void);
extern int core_event_view(unsigned int,void *,void *,unsigned int);
extern void core_clock_snapshot(unsigned int *),core_additem(void *,const void *);
extern unsigned int core_native_original(unsigned int,const unsigned int *);
extern unsigned int core_native(unsigned int,const unsigned int *);
extern unsigned int core_pool_query_owner;
extern volatile unsigned int core_machine_telemetry[];
extern void core_native_profile(unsigned int *);
extern void core_native_interest_set(void (*)(void),unsigned int);
extern void core_native_page_view_set(void (*)(void),unsigned int);
extern void core_parameter_provider_set(int (*)(unsigned int,unsigned int *,void *,unsigned int *));
const struct st_machine_host_api core_machine_host_api={
    9,&core_hzfs_native_io,core_hzfs_open,core_hzfs_close,core_crc_update,
    core_pool_read,core_pool_write,core_pool_ready,st_pool_live_sync,
    st_pool_preview_stop,core_event_view,core_clock_snapshot,core_additem,
    &core_pool_busy,&core_pool_query_owner,core_native_original,core_machine_telemetry,
    core_native_profile,core_native_interest_set,core_native_page_view_set,
    core_parameter_provider_set,core_native,&core_sample_pool_api};
