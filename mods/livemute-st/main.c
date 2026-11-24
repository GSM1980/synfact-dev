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
 */
#include <syntakt/modules.h>
#include <syntakt/host.h>
#include <syntakt/native.h>
#include "mute.h"

#define LM_TRACKS 12u          /* audio tracks; 12 is the FX track */
#define LM_KIND_LIVE 2u        /* event +12: 1 sequencer, 2 live input */
#define LM_NOTE_ON 1u          /* event +4: 1 note on, 2 release */

unsigned int lm_dropped __attribute__((section(".data"))) = 0;

/* args = {event, sound, effective time, frame end} */
int lm_native(unsigned int op, const unsigned int *a, unsigned int *result)
{
    const unsigned int *e;
    if (op != ST_NATIVE_NOTE_ACCEPT || !a || !a[0]) return 0;
    e = (const unsigned int *)a[0];
    if (e[3] != LM_KIND_LIVE || e[1] != LM_NOTE_ON || e[2] >= LM_TRACKS) return 0;
    if (!((st_muted_tracks() >> e[2]) & 1u)) return 0;
    lm_dropped++;
    *result = 1;
    return 1;
}

static int initialize(const struct st_smz_api *api)
{
    const struct st_machine_host_api *h;
    if (!api || !api->ui || !(h = api->ui->host) || h->version < 10 || !h->native_interest) return 1;
    h->native_interest((void (*)(void))lm_native, 1u << ST_NATIVE_NOTE_ACCEPT);
    return 0;
}

static const struct st_smz_subscription subscriptions[] = {
    {ST_EV_NATIVE, (void (*)(void))lm_native}};
const struct st_smz_module livemute_module = {"LIVE MUTE", initialize, subscriptions, 1};
