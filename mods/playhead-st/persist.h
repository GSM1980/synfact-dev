/* SPDX-License-Identifier: GPL-2.0-or-later
 * The modes saved with each pattern's kit (persist.c). */
#ifndef PLAYHEAD_PERSIST_H
#define PLAYHEAD_PERSIST_H
#include <syntakt/host.h>
/* Host API 10 or newer: saving and loading on; otherwise, and with null,
 * modes last until a restart. */
void playhead_persist_init(const struct st_machine_host_api *host);
/* ev_native: ST_NATIVE_SOUND_META only. */
int playhead_native(unsigned int op,const unsigned int *args,unsigned int *result);
/* A live kit's modes changed: the firmware re-encodes its mirrors, as after a
 * native kit edit, so the change survives a power cycle. 1 done; 0 the
 * native object is not the stock one (the change waits for a save). */
unsigned int playhead_persist_changed(unsigned int k);
/* The native recursive task mutex around every change of the modes. */
void playhead_enter(void);
void playhead_leave(void);
/* An encoded kit slot's record (1600h-byte slot): 'PLH1', the thirteen modes,
 * three zero bytes, a hash of those 20 bytes. After LFO3's trailer (14e0h)
 * and snapshot carrier (15b8h..15cfh); the native codec uses [0, 14d2h). */
#define PLAYHEAD_RECORD 0x15e0u
#define PLAYHEAD_RECORD_BYTES 24u
#define PLAYHEAD_MAGIC 0x504c4831u
#endif
