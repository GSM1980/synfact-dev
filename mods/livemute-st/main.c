/* SPDX-License-Identifier: GPL-2.0-or-later
 * livemute-st: a muted track also ignores live notes. See README.md.
 *
 * The OS's mute filter (400a421a) drops only sequencer trigs (event kind 1);
 * live notes (kind 2: MIDI in, the trig keys in KEYBOARD mode) reach a muted
 * track and sound. At NOTE_ACCEPT (core native bus, host API 10), after the
 * stock filter and before the note's effects, a live note-on (action 1) on a
 * track the sequencer mutes is skipped: the subscriber supplies a nonzero
 * result, which reaches the same native event release. Note-offs, sequencer
 * trigs and every other event pass, so nothing is left hanging. MAIN IRQ;
 * reads three OS words, writes nothing.
 *
 * SETTINGS > LIVE MUTE turns it on and off (YES, or LEFT/RIGHT). ON until
 * changed, since a bank with the module asks for it. With host API 20 (core
 * 2.9) the choice is kept in the core's preferences (key ST_PREF_LIVE_MUTE)
 * across power-off; before it, until power-off.
 */
#include <syntakt/modules.h>
#include <syntakt/host.h>
#include <syntakt/native.h>
#include <syntakt/persist.h>
#include "mute.h"

#define LM_TRACKS 12u          /* audio tracks; 12 is the FX track */
#define LM_KIND_LIVE 2u        /* event +12: 1 sequencer, 2 live input */
#define LM_NOTE_ON 1u          /* event +4: 1 note on, 2 release */

#define INVALIDATE ((void (*)(void *))0x400ffe22u)   /* (view + 0x38): redraw, as KIT BANK does */

unsigned int lm_dropped __attribute__((section(".data"))) = 0;
unsigned int lm_on __attribute__((section(".data"))) = 1;   /* SETTINGS > LIVE MUTE */
static const struct st_machine_host_api *lm_host __attribute__((section(".data"))) = 0;

/* The core's preferences (host API 20, task only): one word, 0 or 1. */
static void lm_prefs(unsigned int write)
{
    unsigned int v[1] = {lm_on};
    if (!lm_host || lm_host->version < 20 || !lm_host->persistence) return;
    if (lm_host->persistence->preferences(ST_PREF_LIVE_MUTE, v, 1, write) != 1 || write) return;
    if (v[0] <= 1u) lm_on = v[0];
}

/* args = {event, sound, effective time, frame end} */
int lm_native(unsigned int op, const unsigned int *a, unsigned int *result)
{
    const unsigned int *e;
    if (op != ST_NATIVE_NOTE_ACCEPT || !lm_on || !a || !a[0]) return 0;
    e = (const unsigned int *)a[0];
    if (e[3] != LM_KIND_LIVE || e[1] != LM_NOTE_ON || e[2] >= LM_TRACKS) return 0;
    if (!((st_muted_tracks() >> e[2]) & 1u)) return 0;
    lm_dropped++;
    *result = 1;
    return 1;
}

/* SETTINGS > LIVE MUTE: the row's four MenuItem callbacks (label, select,
 * draw, change), added with the host's additem, as KIT BANK's. */
const char *lm_text(void) { return lm_on ? "LIVE MUTE: ON" : "LIVE MUTE: OFF"; }
__asm__(".text\n.globl lm_label\nlm_label:\n"
        "  move.l %a0,-(%sp)\n"
        "  jsr lm_text\n"
        "  move.l (%sp),%a0\n"
        "  pea 3(%sp)\n"                                /* the allocator argument: unused */
        "  move.l %d0,-(%sp)\n"
        "  move.l %a0,-(%sp)\n"
        "  jsr 0x401bf31e\n"                            /* std::string(const char *) */
        "  lea 12(%sp),%sp\n"
        "  move.l (%sp)+,%d0\n"
        "  rts\n");
void lm_select(const unsigned int *fn)
{
    lm_on ^= 1u;
    lm_prefs(1);
    if (fn && fn[0]) INVALIDATE((char *)fn[0] + 0x38);
}
void lm_change(const unsigned int *fn, int amount)
{
    lm_on = amount > 0;
    lm_prefs(1);
    if (fn && fn[0]) INVALIDATE((char *)fn[0] + 0x38);
}
void lm_draw(const unsigned int *fn, void *item, void *bmp, int x, int y)
{
    (void)fn; (void)item; (void)bmp; (void)x; (void)y;
}
void lm_settings(void *menu)
{
    extern void lm_label(void);
    static const void *row[4] = {(const void *)lm_label, (const void *)lm_select,
                                 (const void *)lm_draw, (const void *)lm_change};
    if (lm_host) lm_host->additem(menu, row);
}

static int initialize(const struct st_smz_api *api)
{
    const struct st_machine_host_api *h;
    if (!api || !api->ui || !(h = api->ui->host) || h->version < 10 || !h->native_interest) return 1;
    lm_host = h;
    lm_prefs(0);
    h->native_interest((void (*)(void))lm_native, 1u << ST_NATIVE_NOTE_ACCEPT);
    return 0;
}

static const struct st_smz_subscription subscriptions[] = {
    {ST_EV_SETTINGS, (void (*)(void))lm_settings},
    {ST_EV_NATIVE, (void (*)(void))lm_native}};
const struct st_smz_module livemute_module = {"LIVE MUTE", initialize, subscriptions, 2};
