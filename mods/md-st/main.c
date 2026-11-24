/* SPDX-License-Identifier: GPL-2.0-or-later
 * main.c - the MD machine on MAIN: its descriptor, its SYN page, labels that
 * follow the voice, the voice list (the shared subtype picker) and the MD's budget.
 *
 * The voice (mdkit's TYPE) is a hidden ninth control, a configuration control
 * (ST_MACHINE_CONFIG_PARAMETER, host API 17: not modulated, not a destination, kept
 * by FUNC+NO's SYN reload). Its default is the first voice.
 *
 * Choosing a voice is <syntakt/subtype-picker.h> (docs/SUBTYPE-PICKER.md): double-tap
 * SYN, highlight MD KIT, YES opens the list of voices over the selector, YES
 * loads the machine with the voice at the cursor (a track with a voice or without),
 * NO goes back to the machine list. Arriving at an MD SYN page shows the voice's name
 * for a second.
 *
 * The budget: a track that is MD replaces a stock machine, and a project of seven
 * different stock machines takes about 5.7 points of CPU A a track (the user's
 * readings, mods/md-st/WORK.md). A voice's weight is its cost over that
 * (md_voice_weight, md-voices.h); the MD tracks' weights together stay within
 * MD_BUDGET, what such a project leaves under 95 %. The list shows a voice that would
 * pass it dimmed and does not take it; AUDIO keeps such a voice silent if a project
 * brings it (audio/md.c).
 *
 * The core asks for a control's label each time it draws one (ST_LABEL,
 * mods/core-st/machines.c) and reads it through the descriptor's pointer, so the
 * labels are buffers that idle() fills from the voice of the Sound on screen.
 */
#include <syntakt/machines.h>
#include <syntakt/modules.h>
#include <syntakt/host.h>
#include "md-voices.h"
typedef st_u32 u32;typedef unsigned char u8;

#define MD_TYPE 70u
#define MD_ENGINE (0x1f9u + (MD_TYPE - 64u) * 32u)   /* control i is MD_ENGINE + i */
#define MD_TYPE_CONTROL 8u
#define MD_TYPE_ID (MD_ENGINE + MD_TYPE_CONTROL)

/* OS 1.42 services, as the sample machines use them (mods/samplers-st) */
#ifndef MD_PICK_NATIVE
#define MD_GET ((u32 (*)(void *, u32, u8 *))0x40039160u)
#define MD_VIEW_TYPE ((u32 (*)(void *))0x40030622u)
#define MD_FILL ((void (*)(void *, int, int, int, int, u32))0x400f7fb6u)
/* a digital track's machine type and its TYPE control (slot 0x36, a configuration
 * control: the stored value, not the modulated one), from the playing Kit (syntakt/kit.h).
 * Until MD KIT 0.3 these were the SRAM copies AUDIO was last sent (80002f50 + t,
 * 800021cc + t * 8Eh): a track cleared or changed, or a new pattern, did not count
 * until each track played. */
#include <syntakt/kit.h>
#define MD_TRACK_TYPE(t) st_kit_track_machine(t)
#define MD_TRACK_VOICE(t) (st_kit_track_value((t), 0x36u) >> 8)
#else
#define STP_TEST_NATIVE 1
#endif

static char md_label[8][5] = {"PTCH", "DEC", "RAMP", "RDEC", "STRT", "NOIS", "HARM", "CLIP"};
static char md_long[8][16] = {"PTCH", "DEC", "RAMP", "RDEC", "STRT", "NOIS", "HARM", "CLIP"};
static char md_text[16];

#define K(i) {md_label[i], md_long[i], 0, 0x7f00, MD_FIRST_DEFAULT_##i << 8, 0x13}
static const struct core_machine_param md_controls[9] = {
    K(0), K(1), K(2), K(3), K(4), K(5), K(6), K(7),
    {"TYPE", "MACHINE TYPE", 0, (MD_VOICES - 1u) << 8, 0, ST_MACHINE_CONFIG_PARAMETER},
};
/* type 70 (docs/ADAPTING.md, section 3); MD_TYPE says the same */
/* MD KIT (MDKT): the MD prefix a category of its own, as SP is the samplers'. Menu category 10, beside
 * the SY machines: 13, which no stock machine uses, put it under the MIDI machines with no icon (the
 * unit, 2026-11-20) */
/* on_load registers the voice picker with the bank's SUBTYPE SELECTOR module */
int md_picker_on_load(const struct core_machine_def *m);
const struct core_machine_def md_machinedrum={70,9,"MD KIT","MDKT","SYN",md_controls,md_picker_on_load,44,10,15};

static int md_copy(char *to, const char *from, u32 room)
{
    int changed = 0;
    u32 i;
    for (i = 0; i + 1 < room && from[i]; ++i) { changed |= to[i] != from[i]; to[i] = from[i]; }
    changed |= to[i] != 0; to[i] = 0;
    return changed;
}

/* ---- the budget ---- */
/* the weights of the other digital tracks' MD voices */
static u32 md_used(u32 track)
{
    u32 t, sum = 0;
    for (t = 0; t < 8; ++t) {
        u32 voice;
        if (t == track || MD_TRACK_TYPE(t) != MD_TYPE) continue;
        voice = MD_TRACK_VOICE(t);
        sum += md_voice_weight[voice < MD_VOICES ? voice : 0];
    }
    return sum;
}
/* the voice fits the track: with the other MD tracks' voices, within the budget */
static int md_fits(u32 track, u32 voice)
{
    return voice < MD_VOICES && (track >= 8 || md_used(track) + md_voice_weight[voice] <= MD_BUDGET);
}

/* ---- the shared picker (docs/SUBTYPE-PICKER.md) ---- */
static int md_config_init(const struct st_smz_api *api)
{
    /* TYPE as a configuration control needs host API 17 (core 2.3.1) */
    return !api->ui->host || api->ui->host->version < 17;
}
static int md_spare_encoder(void *view, void *event, u32 id);
/* right of the voices' names: a drawing of a drum machine of our own (md-icon.h,
 * tools/md_icon.py), its runs of lit pixels a row at a time; LCD y up, its top row at 42 */
#include "md-icon.h"
#define MD_ICON_X 65
#define MD_ICON_TOP 42                          /* the list spans y 2..50: centred */
static void md_list_art(void *bitmap)
{
    u32 r;
    for (r = 0; r < MD_ICON_H; ++r) {
        int x = 0;
        while (x < MD_ICON_W) {
            int start;
            while (x < MD_ICON_W && !((md_icon[r][x >> 5] >> (31 - (x & 31))) & 1u)) ++x;
            if (x >= MD_ICON_W) break;
            start = x;
            while (x < MD_ICON_W && ((md_icon[r][x >> 5] >> (31 - (x & 31))) & 1u)) ++x;
            MD_FILL(bitmap, MD_ICON_X + start, MD_ICON_TOP - (int)r, MD_ICON_X + x - 1, MD_ICON_TOP - (int)r, 1);
        }
    }
}
#define STP_LIST_ART(bitmap) md_list_art(bitmap)
#define STP_LIST_RIGHT 60                           /* the cursor's bar stops left of it */
#define STP_MACHINE MD_TYPE
#define STP_CONTROL_ID MD_TYPE_ID
#define STP_CONTROL_INDEX MD_TYPE_CONTROL
#define STP_COUNT MD_VOICES
#define STP_NAMES md_voice_names
#define STP_DEFAULT 0u
#define STP_ON_LOAD md_picker_on_load
#define STP_CONFIG_INIT(api) md_config_init(api)
#define STP_ENABLED(track, index) md_fits(track, index)
#define STP_VIEW_ENCODER md_spare_encoder
static void md_load_voice(void *view, u32 type);
#define STP_LOAD(view, type) md_load_voice(view, type)
#ifdef MD_PICK_NATIVE
#define STP_GET MD_GET
#define STP_VIEW_TYPE MD_VIEW_TYPE
#define STP_FILL MD_FILL
#endif
#include <syntakt/subtype-picker.h>
#include "md-load.h"

/* MD KIT 0.3: a voice chosen from the list loads its own knob values (md-load.h, a
 * Machinedrum's as photographed), through the picker's native edit service, so the
 * Sound, undo and the screen follow as a turned knob. Only the knobs that differ. */
static void md_load_voice(void *view, u32 type)
{
    u32 i, now, want;
    if (type >= MD_VOICES) return;
    for (i = 0; i < 8; ++i) {
        now = stp_value(view, MD_ENGINE + i);
        want = (u32)md_voice_load[type][i] << 8;
        if (now != want) stp_api->edit(view, MD_ENGINE + i, (int)want - (int)now, i);
    }
}

/* a knob past the voice's last control does nothing (md.c sends the voice its own
 * controls only): its encoder is taken, so no value changes and no popup shows */
static int md_spare_encoder(void *view, void *event, u32 id)
{
    u32 index = id - MD_ENGINE;
    (void)event;
    if (!stp_syn_view(view) || stp_selecting(view) || MD_VIEW_TYPE(view) != MD_TYPE || index >= 8) return 0;
    return index >= md_voice_controls[stp_type_of(view)];
}

/* ---- the machine's UI ---- */
/* MD KIT 0.3: the voices whose PTCH the Machinedrum shows centred, -64..63 (PI-BD,
 * PI-SD, PI-MT, PI-ML, PI-RS); stored and played 0..127 as ever (md-load.h) */
#define MD_CENTRED ((1u << 21) | (1u << 22) | (1u << 23) | (1u << 24) | (1u << 26))
static u32 md_shown;                             /* the voice idle() last saw on screen */
static int md_centred(u32 type, u32 index) {return index == 0 && type < MD_VOICES && ((MD_CENTRED >> type) & 1u);}
static int md_idle(const struct st_machine_ui_api *api, const struct core_machine_def *m, void *view, u32 page)
{
    u32 type, i;
    int changed = 0;
    (void)api; (void)m; (void)page;
    type = stp_type_of(view);
    changed |= md_shown != type;
    md_shown = type;
    for (i = 0; i < 8; ++i) {
        changed |= md_copy(md_label[i], md_voice_labels[type][i], sizeof md_label[i]);
        changed |= md_copy(md_long[i], md_voice_labels[type][i], sizeof md_long[i]);
    }
    return stp_idle(view) | changed;
}

static void md_draw(const struct st_machine_ui_api *api, const struct core_machine_def *m, void *view, void *ctx, u32 page)
{
    u32 type, i;
    (void)api; (void)m; (void)page;
    if (!stp_syn_view(view) || stp_selecting(view) || MD_VIEW_TYPE(view) != MD_TYPE) return;
    /* the knobs past the voice's last control: an empty place (their dial and label;
     * knob i is column i % 4, row i / 4: dials 26 px apart, rows y 26..51 and 0..24) */
    type = stp_type_of(view);
    for (i = md_voice_controls[type]; i < 8; ++i) {
        const int x = 21 + 26 * (int)(i % 4u), y = i < 4 ? 26 : 0;
        MD_FILL(ctx, x, y, x + 24, y + 25, 0);
    }
    stp_draw(view, ctx);                         /* the voice's name, for a second */
}

static const char *md_format(const struct st_machine_ui_api *api, const struct core_machine_def *m, u32 index, u32 raw)
{
    (void)api; (void)m;
    if (index == MD_TYPE_CONTROL) {
        u32 t = raw >> 8;
        md_copy(md_text, md_voice_names[t < MD_VOICES ? t : 0], sizeof md_text);
        return md_text;
    }
    if (md_centred(md_shown, index)) {           /* -64..63, as the Machinedrum shows it */
        int n = (int)(raw >> 8) - 64;
        char *p = md_text;
        u32 v;
        if (n < 0) {*p++ = '-'; v = (u32)-n;} else v = (u32)n;
        if (v >= 10) *p++ = (char)('0' + v / 10);
        *p++ = (char)('0' + v % 10); *p = 0;
        return md_text;
    }
    return 0;                                   /* the other knobs: 0..127, as the native */
}

/* one page, the voice's eight knobs; the voice itself is on none */
static const u32 md_layout[8] __attribute__((aligned(4))) = {0, 1, 2, 3, 4, 5, 6, 7};
const struct st_machine_ui md_machinedrum_ui = {
    MD_TYPE, 1, md_layout, md_draw, md_idle, ST_MACHINE_NO_CALLBACK, md_format, ST_MACHINE_NO_CALLBACK
};
