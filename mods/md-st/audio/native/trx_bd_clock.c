/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/trx_bd_clock.c - TRX-BD's control clock in integers: mdkit's
 * trx_bd_md_control_clock_seed, trx_bd_md_trx_events_boundary and
 * trx_bd_trx_bd_packet (mdkit/src/trx/bd/mdkit_trx_bd.c) without float32.
 *
 * mdkit keeps the clock's values in floats, but every one is a whole number
 * of 1/128: a target is a knob (0..127), and raw, pending and current are
 * set from knobs and from (3 raw + 128 target) / 4 in integer arithmetic,
 * then divided by 128 (exact in a float). So the same storage holds them as
 * integers: target as the knob, the others as 128 times the value, and
 * (unsigned)k is k >> 7. The Syntakt then needs no float routine a block.
 * Included three times by the generated voice (tools/md_port.py, NATIVE),
 * once per function; MD_CLOCK_PART says which.
 */
typedef uint32_t __attribute__((may_alias)) md_clock_u32;
#define MD_CLOCK(field) ((md_clock_u32 *)(void *)(field))

#if MD_CLOCK_PART == 0
static inline void trx_bd_md_control_clock_seed(mdkit_control_clock *c, unsigned count,
                                                const unsigned char *knobs)
{
    c->count = count;
    for (unsigned i = 0; i < count; ++i) {
        MD_CLOCK(c->target)[i] = knobs[i];
        MD_CLOCK(c->raw)[i] = MD_CLOCK(c->current)[i] = MD_CLOCK(c->pending)[i] = (uint32_t)knobs[i] * 128u;
    }
    c->block = 0; c->next_update = 0; c->publish_block = 0; c->pending_valid = 0;
}
#elif MD_CLOCK_PART == 1
static inline unsigned trx_bd_md_trx_events_boundary(mdkit_control_clock *c, mdkit_trx_bd_events *e, int trigger_only)
{
    md_clock_u32 *raw = MD_CLOCK(c->raw), *pending = MD_CLOCK(c->pending), *current = MD_CLOCK(c->current),
                 *target = MD_CLOCK(c->target);
    unsigned result = 0, i;
    if (c->pending_valid && c->block >= c->publish_block) {
        for (i = 0; i < 8; ++i) current[i] = pending[i];
        c->pending_valid = 0; result = trx_bd_MD_TRX_PUBLISH;
    }
    if (c->block == c->next_update) {
        int changed = 0;
        for (i = 0; i < 8; ++i) {
            uint32_t r = raw[i];                       /* (uint32_t)(128.0f * raw) */
            pending[i] = r; changed |= r != e->last_raw[i]; e->last_raw[i] = r;
            raw[i] = (3u * r + 128u * target[i]) / 4u; /* the float of that / 128, times 128 */
        }
        c->pending_valid = changed && !trigger_only; c->publish_block = c->block + 1; c->next_update += 12;
    }
    for (i = 0; i < 8; ++i) if (e->targets & (1u << i)) target[i] = e->queued[i];
    e->targets = 0;
    if (e->trigger) {
        for (i = 0; i < 8; ++i) current[i] = raw[i];
        result = trx_bd_MD_TRX_PUBLISH | trx_bd_MD_TRX_TRIGGER | (e->triggered ? 0 : trx_bd_MD_TRX_FIRST);
        e->trigger = 0; e->triggered = 1;
    }
    ++c->block;
    return result;
}
#else
static void trx_bd_trx_bd_packet(const float k[8], uint32_t p[13])
{
    const md_clock_u32 *q = (const md_clock_u32 *)(const void *)k;
#define K(i) (q[i] >> 7)                               /* (unsigned)k[i] */
    p[0] = trx_bd_control_word_0[K(1)]; p[1] = trx_bd_control_word_1[K(4)]; p[2] = trx_bd_control_word_2[K(0)];
    p[3] = trx_bd_control_word_3[K(2)]; p[4] = trx_bd_control_word_4[K(3)]; p[5] = trx_bd_control_word_5[K(7)];
    p[6] = trx_bd_control_word_6[K(7)]; p[7] = trx_bd_control_word_7[K(7)]; p[8] = trx_bd_control_word_8[K(7)];
    p[9] = trx_bd_control_word_9[K(7)]; p[10] = trx_bd_control_word_10[K(5)]; p[11] = trx_bd_control_word_11[K(5)];
    p[12] = trx_bd_control_word_12[K(6)];
#undef K
}
#endif
#undef MD_CLOCK_PART
