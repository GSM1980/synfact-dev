/* SPDX-License-Identifier: GPL-2.0-or-later
 * Mix fast paths: redirect the three stock MAIN mix kernels (8/9/10 tracks)
 * to versions that skip the per-frame gain-ramp additions and the gain
 * store-back when every ramp increment is zero (the common steady state).
 * Bit-exact otherwise it tail-jumps into the stock kernel. No UI, no pages.
 *
 * Each kernel is f(out, in, gains, increments). The fast entry tests the
 * eight/nine/ten increment words; if any is non-zero it rebuilds the stock
 * prologue and jumps into the stock body, so ramps and saturation are the
 * stock code exactly. Validated by the exact differential (tools/stock-profile).
 */
#include <syntakt/modules.h>
#define MEM(a) ((volatile unsigned char *)(a))
#define K8 0x4008f0b6u   /* eight-track kernel entry */
#define K9 0x4008f024u   /* nine-track */
#define K10 0x4008ef8cu  /* ten-track */
extern void mixfast_k8(void), mixfast_k9(void), mixfast_k10(void);
extern st_u32 mixfast_lock(void);
extern void mixfast_unlock(st_u32), mixfast_code16(st_u32, st_u32), mixfast_sync(void);

static st_u32 be16(st_u32 at) { return (st_u32)MEM(at)[0] << 8 | MEM(at)[1]; }

/* 0 installed; else the kernel (8/9/10) whose stock bytes did not match. */
static st_u32 install(void)
{
    st_u32 sr;
    if (be16(K8) != 0x4fefu || be16(K8 + 2) != 0xffd8u ||
        be16(K8 + 4) != 0x48d7u || be16(K8 + 6) != 0x3cfcu) return 8;
    if (be16(K9) != 0x4fefu || be16(K9 + 2) != 0xffd4u ||
        be16(K9 + 4) != 0x48d7u || be16(K9 + 6) != 0x7cfcu) return 9;
    if (be16(K10) != 0x4fefu || be16(K10 + 2) != 0xffd0u ||
        be16(K10 + 4) != 0x7020u || be16(K10 + 6) != 0x48d7u) return 10;
    sr = mixfast_lock();
    /* entry word (jmp abs.l = 0x4ef9) written last, so a half-written patch
     * is never executed; the cache is invalidated once at the end. */
    mixfast_code16(K8 + 2, (st_u32)mixfast_k8 >> 16);
    mixfast_code16(K8 + 4, (st_u32)mixfast_k8 & 0xffffu);
    mixfast_code16(K8, 0x4ef9u);
    mixfast_code16(K9 + 2, (st_u32)mixfast_k9 >> 16);
    mixfast_code16(K9 + 4, (st_u32)mixfast_k9 & 0xffffu);
    mixfast_code16(K9, 0x4ef9u);
    mixfast_code16(K10 + 2, (st_u32)mixfast_k10 >> 16);
    mixfast_code16(K10 + 4, (st_u32)mixfast_k10 & 0xffffu);
    mixfast_code16(K10, 0x4ef9u);
    mixfast_sync();
    mixfast_unlock(sr);
    return 0;
}

static int start(const struct st_smz_api *api)
{
    if (!api || !api->version) return 1;
    install();            /* a mismatch leaves every kernel stock */
    return 0;
}
/* Init-only module: no event subscriptions. The core's bank validator
 * requires the subscriptions pointer to be the NONE sentinel when the
 * count is zero (a real pointer would be rejected). */
const struct st_smz_module mix_fast_module = {
    "MIX FAST", start, (const struct st_smz_subscription *)0xffffffffu, 0};
