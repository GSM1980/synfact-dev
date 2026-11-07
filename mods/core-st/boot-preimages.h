/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CORE_ST_BOOT_PREIMAGES_H
#define CORE_ST_BOOT_PREIMAGES_H
#include "storage.h"
/* The SMZ parser has already bounded and validated the 40-byte mod-info
 * records. This only reads resident OS code; no bank code runs. */
int core_boot_preimages_ok(const core_u8 *mod_info);
#endif
