/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_clock.c - a PI voice's control clock in integers: mdkit's
 * mdkit_events_init, mdkit_events_boundary and mdkit_pi_<voice>_packet
 * (mdkit/src/pi/sd/mdkit_pi_sd.c) without float32.
 *
 * As TRX-BD's (native/trx_bd_clock.c): every value the clock keeps is a whole
 * number of 1/128 (a target is a knob; raw, pending and current come from
 * knobs and (3 raw + 128 target) / 4 in integer arithmetic, then / 128), so
 * the same storage holds target as the knob and the others as 128 times the
 * value; a voice's packet (native/pi_<voice>_packet.c) reads
 * (unsigned)(128.0f * k) as that integer. Included once per function by the
 * generated voice (tools/md_port.py, NATIVE); MD_CLOCK_PART says which,
 * MD_PI_EVENTS and MD_PI_DEFAULTS name the voice's own.
 */
#ifndef MD_PCLOCK
typedef uint32_t __attribute__((may_alias)) md_pclock_u32;
#define MD_PCLOCK(field) ((md_pclock_u32 *)(void *)(field))
#endif

#if MD_CLOCK_PART == 0
static inline void mdkit_events_init(mdkit_control_clock *c, MD_PI_EVENTS *e)
{
    for (unsigned i = 0; i < 8u; ++i) {
        MD_PCLOCK(c->target)[i] = MD_PI_DEFAULTS[i];
        MD_PCLOCK(c->raw)[i] = MD_PCLOCK(c->current)[i] = MD_PCLOCK(c->pending)[i] = (uint32_t)MD_PI_DEFAULTS[i] * 128u;
    }
    c->count = 8; c->block = 0; c->publish_block = 0; c->pending_valid = 0;
    c->next_update = 2;
    e->targets = e->trigger = e->triggered = 0;
    for (unsigned i = 0; i < 8; ++i) {
        e->queued[i] = MD_PI_DEFAULTS[i];
        e->last_raw[i] = (unsigned)MD_PI_DEFAULTS[i] * 128u;
    }
}
#elif MD_CLOCK_PART == 1
static inline unsigned mdkit_events_boundary(mdkit_control_clock *c, MD_PI_EVENTS *e, int trigger_only)
{
    md_pclock_u32 *raw = MD_PCLOCK(c->raw), *pending = MD_PCLOCK(c->pending), *current = MD_PCLOCK(c->current),
                  *target = MD_PCLOCK(c->target);
    unsigned result = 0, i;
    if (c->pending_valid && c->block >= c->publish_block) {
        for (i = 0; i < 8; ++i) current[i] = pending[i];
        c->pending_valid = 0;
        result = MDKIT_PI_PUBLISH;
    }
    if (c->block == c->next_update) {
        for (i = 0; i < 8; ++i) {
            uint32_t r = raw[i];                       /* (uint32_t)(128.0f * raw) */
            pending[i] = r;
            e->last_raw[i] = r;
            raw[i] = (3u * r + 128u * target[i]) / 4u;
        }
        c->pending_valid = !trigger_only;
        c->publish_block = c->block + 1;
        c->next_update += 12;
    }
    for (i = 0; i < 8; ++i)
        if (e->targets & (1u << i)) target[i] = e->queued[i];
    e->targets = 0;
    if (e->trigger) {
        for (i = 0; i < 8; ++i) current[i] = raw[i];
        result = MDKIT_PI_PUBLISH | MDKIT_PI_TRIGGER | (e->triggered ? 0 : MDKIT_PI_FIRST);
        e->trigger = 0;
        e->triggered = 1;
    }
    ++c->block;
    return result;
}
#endif
#undef MD_CLOCK_PART
