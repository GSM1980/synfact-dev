/* SPDX-License-Identifier: GPL-2.0-or-later
 * Read-only host/wasm entry points for the unmodified firmware bank parser.
 * No firmware execution, relocation, allocator, filesystem or device access.
 */
#include "../../mods/core-st/smz.h"
extern volatile core_u32 core_smz_failure;

#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

EXPORT unsigned synfact_smz_api(void) { return 1; }
EXPORT unsigned synfact_smz_contract(void) { return CORE_SMZ_CONTRACT; }
EXPORT unsigned synfact_smz_failure(void) { return core_smz_failure; }
EXPORT int synfact_smz_validate(const unsigned char *data, unsigned size)
{
    struct core_smz_reader reader = {(void *)data, core_smz_memory_read, size};
    struct core_smz_view view;
    if (!data || size > CORE_MODS_MAX) return 0;
    return core_smz_validate(&reader, &view);
}

#ifdef __wasm__
extern unsigned char __heap_base;
EXPORT unsigned synfact_smz_input(void) { return (unsigned)&__heap_base; }
#endif
