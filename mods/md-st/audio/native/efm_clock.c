/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/efm_clock.c - an EFM voice's efm_clock_boundary (mdkit/src/efm/*),
 * the same floats with less work, and a return value: nonzero when current
 * changed, so the render rebuilds its coefficients only then (they are a
 * function of current alone).
 *
 * Every 12 blocks mdkit moves raw a quarter of the way to target in float32:
 * raw = (3 raw + target) * 0.25. Once raw is target that gives target again,
 * exactly (target is a knob, 0..127: 3t, 4t and t/4 * 4 are exact), so a raw
 * already at its target is left as it is. Equal is compared on the bits: the
 * values are never negative zero or NaN.
 * Included by the generated voice in place of the function (tools/md_port.py,
 * NATIVE).
 */
typedef uint32_t __attribute__((may_alias)) md_eclock_u32;

static int efm_clock_boundary(mdkit_control_clock *c)
{
    const md_eclock_u32 *target = (const md_eclock_u32 *)(const void *)c->target;
    md_eclock_u32 *raw = (md_eclock_u32 *)(void *)c->raw;
    int changed = 0;
    if (c->pending_valid && c->block >= c->publish_block) {
        for (unsigned i = 0; i < c->count; ++i) c->current[i] = c->pending[i];
        c->pending_valid = 0;
        changed = 1;
    }
    if (c->block == c->next_update) {
        for (unsigned i = 0; i < c->count; ++i) {
            c->pending[i] = c->raw[i];
            if (raw[i] != target[i]) c->raw[i] = (3.0f * c->raw[i] + c->target[i]) * 0.25f;
        }
        c->pending_valid = 1;
        c->publish_block = c->block + 1;
        c->next_update += 12;
    }
    c->block += 1;
    return changed;
}
