/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/trx_b2_clock.c - TRX-B2's control clock in integers: mdkit's
 * b2_control_clock_seed, b2_controls and b2_boundary (mdkit/src/trx/b2/
 * mdkit_trx_b2.c) without float32, as trx_bd_clock.c and trx_sd_clock.c do: every
 * clock value is a whole number of 1/128, so the storage holds the target as the
 * knob and the others as 128 times the value; (unsigned)k is k >> 7 and
 * (uint32_t)(128.0f * k) is k. These three are the clock's only users in the voice
 * (b2_controls runs every block). Included three times by the generated voice
 * (tools/md_port.py, NATIVE); MD_CLOCK_PART says which.
 */
#ifndef MD_B2_CLOCK_U32
#define MD_B2_CLOCK_U32
typedef uint32_t __attribute__((may_alias)) md_b2_clock_u32;
#define MD_B2_CLOCK(field) ((md_b2_clock_u32 *)(void *)(field))
#endif

#if MD_CLOCK_PART == 0
static inline void b2_control_clock_seed(mdkit_control_clock *c, unsigned count, const unsigned char *knobs)
{
    c->count = count;
    for (unsigned i = 0; i < count; ++i) {
        MD_B2_CLOCK(c->target)[i] = knobs[i];
        MD_B2_CLOCK(c->raw)[i] = MD_B2_CLOCK(c->current)[i] = MD_B2_CLOCK(c->pending)[i] = (uint32_t)knobs[i] * 128u;
    }
    c->block = 0; c->next_update = 0; c->publish_block = 0; c->pending_valid = 0;
}
#elif MD_CLOCK_PART == 1
static void b2_controls(mdkit_trx_b2 *s)
{
    const md_b2_clock_u32 *raw = MD_B2_CLOCK(s->clock.current);    /* (uint32_t)(128.0f * k[i]) */
#define K(i) (raw[i] >> 7)                                         /* (unsigned)k[i] */
    const unsigned dist = K(MDKIT_TRX_B2_DIST);
    const unsigned dirt = K(MDKIT_TRX_B2_DIRT);
    s->pitch = b2_trx_b2_PTCH_cc128[K(MDKIT_TRX_B2_PTCH)];
    s->ramp_amount = b2_trx_b2_RAMP_cc128[K(MDKIT_TRX_B2_RAMP)];
    s->decay = b2_trx_b2_DEC_cc128[K(MDKIT_TRX_B2_DEC)];
    s->hold_decay = b2_trx_b2_HOLD_cc128[K(MDKIT_TRX_B2_HOLD)];
    s->warp[0] = b2_trx_b2_DIST0_cc128[dist];
    s->warp[1] = b2_trx_b2_DIST1_cc128[dist];
    s->warp[2] = b2_trx_b2_DIST2_cc128[dist];
    s->warp[3] = b2_trx_b2_DIST3_cc128[dist];
    s->dirt_shift = b2_trx_b2_DIRT_SHIFT_cc128[dirt];
    s->dirt_bias = b2_trx_b2_DIRT_BIAS_cc128[dirt];
    s->noise_level = b2_trx_b2_NOIS_cc128[K(MDKIT_TRX_B2_NOIS)];
    s->tick_level = b2_trx_b2_TICK_cc128[K(MDKIT_TRX_B2_TICK)];
#undef K
    if (raw[MDKIT_TRX_B2_RAMP] & 127)
        s->ramp_amount = (76087u * raw[MDKIT_TRX_B2_RAMP]) >> 14;
    if (raw[MDKIT_TRX_B2_DEC] & 127)
        s->decay = b2_dec_raw((3 * raw[MDKIT_TRX_B2_DEC]) >> 7);
    if (raw[MDKIT_TRX_B2_HOLD] & 127)
        s->hold_decay = b2_hold_raw(raw[MDKIT_TRX_B2_HOLD] >> 6);
    if (raw[MDKIT_TRX_B2_DIST] & 127) {
        uint32_t amount = raw[MDKIT_TRX_B2_DIST] * raw[MDKIT_TRX_B2_DIST] >> 12;
        s->warp[0] = (0u - 0x8000u - 60*amount) & MD_MASK24;
        s->warp[1] = (0x8000u + 70*amount) & MD_MASK24;
        s->warp[2] = (0u - 0x8000u - 50*amount) & MD_MASK24;
        s->warp[3] = (0x8000u + 40*amount) & MD_MASK24;
    }
    if (raw[MDKIT_TRX_B2_NOIS] & 127) s->noise_level = raw[MDKIT_TRX_B2_NOIS] << 9;
    if (raw[MDKIT_TRX_B2_TICK] & 127) s->tick_level = raw[MDKIT_TRX_B2_TICK] << 8;
}
#else
static void b2_boundary(mdkit_trx_b2 *s)
{
    mdkit_control_clock *c = &s->clock;
    md_b2_clock_u32 *raw = MD_B2_CLOCK(c->raw), *pending = MD_B2_CLOCK(c->pending), *current = MD_B2_CLOCK(c->current),
                    *target = MD_B2_CLOCK(c->target);
    if (c->pending_valid && c->block >= c->publish_block) {
        for (unsigned i = 0; i < 8; ++i) current[i] = pending[i];
        c->pending_valid = 0;
    }
    if (c->block == c->next_update) {
        int changed = 0;
        for (unsigned i = 0; i < 8; ++i) {
            const uint32_t r = raw[i];
            pending[i] = r;
            changed |= r != s->last_raw[i];
            s->last_raw[i] = r;
            raw[i] = (3u * r + 128u * target[i]) / 4u;
        }
        c->pending_valid = changed;
        c->publish_block = c->block + 1;
        c->next_update += 12;
    }
    for (unsigned i = 0; i < 8; ++i)
        if (s->target_mask & (1u << i)) target[i] = s->queued_targets[i];
    s->target_mask = 0;
    if (!s->pending_trigger) {
        ++c->block;
        b2_controls(s);
        return;
    }
    for (unsigned i = 0; i < 8; ++i) current[i] = raw[i];
    b2_controls(s);
    if (!s->triggered) {
        s->noise_a = 0x12e70c;
        s->noise_b = 0x1914a5;
        s->phase = s->amplitude = 0;
    }
    s->ramp = s->ramp_amount;
    s->attack_step = 0x147ae;
    s->noise_amplitude = s->noise_level;
    s->tick = s->tick_level;
    s->hold = 0x7fffff;
    s->pending_trigger = 0;
    s->triggered = 1;
    ++c->block;
}
#endif
#undef MD_CLOCK_PART
