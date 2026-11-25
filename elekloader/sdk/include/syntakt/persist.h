/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ELEKLOADER_ST_PERSIST_H
#define ELEKLOADER_ST_PERSIST_H
/* Host API 20, task-only. Buffers/containers are borrowed for this call.
 * kit_slot returns 1600h only for a full known encoded Kit. container returns
 * its full project container, or zero for individual/short buffers.
 * kit_container returns the current live Kit's first registered full mirror.
 * preferences: key 1..8, up to eight words; read returns 1 found, 0 absent,
 * -1 IO/corruption; write commits and verifies a redundant record. */
struct st_persistence_api {
    unsigned int version;
    unsigned int (*kit_slot)(unsigned int);
    unsigned int (*container)(unsigned int);
    unsigned int (*kit_container)(unsigned int);
    int (*kit_changed)(unsigned int);
    int (*preferences)(unsigned int,unsigned int *,unsigned int,unsigned int);
};
#define ST_PREF_RECORDER 1u
#define ST_PREF_KIT_BANK 2u
#endif
