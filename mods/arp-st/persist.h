/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARP_PERSIST_H
#define ARP_PERSIST_H
#include "arp.h"
void arp_persist_init(void);
const struct arp_settings *arp_sound_settings(arp_u32 sound);
void arp_sound_read(arp_u32 sound,struct arp_settings *);
int arp_sound_store(arp_u32 sound,const struct arp_settings *);
void arp_sound_meta(const arp_u32 *);
#endif
