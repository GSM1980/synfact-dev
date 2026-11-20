/* SPDX-License-Identifier: GPL-2.0-or-later
 * md-tables.c - evaluates one voice's double-precision functions for every
 * input, with the voice's own code, for tools/md_port.py.
 *
 * Built on the host with -DVOICE_C="<ported voice .c>" and
 * -DVOICE_TABLES="<list>", without MD_ST_TABLES, so the voice's functions
 * are mdkit's. The list has one line per table:
 *   T(name, lo, n, expr)            print "name lo w0 w1 ...", expr at i = lo..lo+n-1
 *   Z(name, a, b, lo, hi, expr)     check expr is 0 for i in a..b-1 outside lo..hi-1
 *   K(name, a, b, key, f, expr)     check f == expr at i = key, for x in a..b-1 (key, f of x)
 * and first a line "voice <name> <state bytes> <controls> <label>... <default>...",
 * read from the voice's descriptor (-DVOICE_SYMBOL=mdkit_voice_<voice>).
 */
#include <stdio.h>
#include <stdlib.h>
#include VOICE_C

static void check(const char *name, unsigned i, uint32_t v)
{
    if (v > 0xffffffu) { fprintf(stderr, "%s[%u] = %#x is not a 24-bit word\n", name, i, (unsigned)v); exit(1); }
}

int main(void)
{
    unsigned i;
    const mdkit_voice *d = &VOICE_SYMBOL;
    printf("voice %s %u %u", d->name, (unsigned)d->state_bytes, d->control_count);
    for (i = 0; i < d->control_count; ++i) printf(" %s", d->control_names[i]);
    for (i = 0; i < d->control_count; ++i) printf(" %u", d->control_defaults ? d->control_defaults[i] : 64u);
    printf("\n");
#define T(name, lo, n, expr) \
    printf("%s %u", name, (unsigned)(lo)); \
    for (i = (lo); i < (lo) + (n); ++i) { uint32_t v_ = (uint32_t)(expr); check(name, i, v_); printf(" %x", (unsigned)v_); } \
    printf("\n");
#define Z(name, a, b, lo, hi, expr) \
    for (i = (a); i < (b); ++i) if ((i < (lo) || i >= (hi)) && (uint32_t)(expr)) { \
        fprintf(stderr, "%s: %#x is outside the table and not 0\n", name, i); return 1; }
#define K(name, a, b, key, f, expr) \
    { unsigned long long x_; for (x_ = (a); x_ < (b); ++x_) { unsigned x = (unsigned)x_; i = (key); \
        if ((uint32_t)(f) != (uint32_t)(expr)) { fprintf(stderr, "%s: f(%#x) is not the table at %u\n", name, x, i); return 1; } } }
#include VOICE_TABLES
    return 0;
}
