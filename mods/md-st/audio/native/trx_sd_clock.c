/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/trx_sd_clock.c - TRX-SD's control clock in integers: mdkit's
 * trx_sd_md_control_clock_seed, trx_sd_sd_boundary and trx_sd_sd_controls
 * (mdkit/src/trx/sd/mdkit_trx_sd.c) without float32, as trx_bd_clock.c does for
 * TRX-BD: every value of the clock is a whole number of 1/128 (a target is a knob,
 * the others come from knobs and (3 raw + 128 target) / 4 in integers, divided by
 * 128, exact in a float), so the same storage holds the target as the knob and the
 * others as 128 times the value. These three are the clock's only readers and
 * writers in the voice. Included three times by the generated voice
 * (tools/md_port.py, NATIVE); MD_CLOCK_PART says which.
 */
#ifndef MD_SD_CLOCK_U32
#define MD_SD_CLOCK_U32
typedef uint32_t __attribute__((may_alias)) md_sd_clock_u32;
#define MD_SD_CLOCK(field) ((md_sd_clock_u32 *)(void *)(field))
#endif

#if MD_CLOCK_PART == 0
static inline void trx_sd_md_control_clock_seed(mdkit_control_clock *c, unsigned count, const unsigned char *knobs)
{
    c->count = count;
    for (unsigned i = 0; i < count; ++i) {
        MD_SD_CLOCK(c->target)[i] = knobs[i];
        MD_SD_CLOCK(c->raw)[i] = MD_SD_CLOCK(c->current)[i] = MD_SD_CLOCK(c->pending)[i] = (uint32_t)knobs[i] * 128u;
    }
    c->block = 0; c->next_update = 0; c->publish_block = 0; c->pending_valid = 0;
}
#elif MD_CLOCK_PART == 1
static void trx_sd_sd_boundary(mdkit_trx_sd *s)
{
    mdkit_control_clock *c = &s->clock;
    md_sd_clock_u32 *raw = MD_SD_CLOCK(c->raw), *pending = MD_SD_CLOCK(c->pending), *current = MD_SD_CLOCK(c->current),
                    *target = MD_SD_CLOCK(c->target);
    if (c->pending_valid && c->block >= c->publish_block) {
        for (unsigned i = 0; i < 8; ++i) current[i] = pending[i];
        c->pending_valid = 0;
    }
    if (c->block == c->next_update) {
        int changed = 0;
        for (unsigned i = 0; i < 8; ++i) {
            const uint32_t r = raw[i];                       /* (uint32_t)(128.0f * raw) */
            pending[i] = r;
            changed |= r != s->last_raw[i]; s->last_raw[i] = r;
            raw[i] = (3u * r + 128u * target[i]) / 4u;     /* that float / 128, times 128 */
        }
        c->pending_valid = changed; c->publish_block = c->block + 1; c->next_update += 12;
    }
    for (unsigned i = 0; i < 8; ++i)
        if (s->target_mask & (1u << i)) target[i] = s->queued_targets[i];
    s->target_mask = 0;
    if (s->pending_trigger) for (unsigned i = 0; i < 8; ++i) current[i] = raw[i];
    trx_sd_sd_controls(s);
    if (s->pending_trigger) {
        if (!s->triggered) {
            s->noise_a = 0x29a; s->noise_b = 1;
            s->filter_a = s->filter_b = s->filter_c = s->filter_d = 0;
        }
        s->body_amplitude = md_acc_acc_to_word(md_acc_add(md_acc_from24(0x50c335), md_acc_from24(s->body_amplitude)));
        s->shape = 0x7eb852;
        md_acc previous = md_acc_from24(s->last_output);
        if (md_acc_s56(previous) < 0) previous = md_acc_sub(md_acc_make(0, 0), previous);
        s->noise_amplitude = md_acc_acc_to_word(md_acc_add(md_acc_from24(0x4ccccd), md_acc_asl(previous, 4)));
        s->bump = s->attack_cursor = 0; s->phase_a = s->phase_b = 0;
        s->pending_trigger = 0; s->triggered = 1;
    }
    ++c->block;
}
#else
/* mdkit's body; raw[i] = (uint32_t)(current[i] * 128.0f) is the stored integer */
static void trx_sd_sd_controls(mdkit_trx_sd *s)
{
    uint32_t raw[8];
    for (unsigned i = 0; i < 8; ++i) raw[i] = MD_SD_CLOCK(s->clock.current)[i];
    s->decay = trx_sd_sd_DEC_cc128[raw[1] >> 7];
    s->noise_decay = trx_sd_sd_NOISE_DEC_cc128[raw[1] >> 7];
    s->tone_a = 0x7fffff - (raw[5] << 8);
    s->tone_b = raw[5] << 8;
    s->snap = raw[4] << 9;
    s->snap_boost = raw[4] < 8192 ? 0 : (((raw[4] - 8192) * (raw[4] - 8192) >> 4) & 0xfffc00);
    s->pitch_a = (raw[0] >> 2) + 0x1000;
    s->pitch_b = ((s->pitch_a >> 6) * (0xb9 + (raw[6] >> 9))) >> 1;
    s->shape_decay = trx_sd_sd_clip_raw[(raw[7] * 0x62b3) >> 21];
    s->bump_step = (raw[2] * raw[2]) >> 18;
    s->filter_index = (raw[0] >> 5) + 0x400;
    s->bump_decay = trx_sd_sd_benv_raw[(raw[3] * 5) >> 10];
}
#endif
#undef MD_CLOCK_PART
