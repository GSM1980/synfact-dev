/* SPDX-License-Identifier: GPL-2.0-or-later
 * md_st_picker_check.c - the MD machine's MAIN UI (mods/md-st/main.c) on the host with the
 * shared subtype picker (<syntakt/subtype-picker.h>); only native services are doubles,
 * as tests/test_st_digimono_picker.c does for SY MnM.
 * - TYPE: a configuration control (ST_MACHINE_CONFIG_PARAMETER), the first voice by default,
 *   0..MD_VOICES-1; the module needs host API 17.
 * - The list over the selector: YES on MD opens it, encoders and UP/DOWN move it, NO goes
 *   back to the machine list, YES loads; the next MD page sets the voice.
 * - The budget: with other MD tracks' voices, a voice past MD_BUDGET is dimmed, the cursor
 *   passes it, YES takes nothing; the list opens on the nearest voice that fits.
 * - The knobs past a voice's last control take no turn; labels follow the voice.
 * - MD KIT 0.3: a chosen voice loads its own knobs (md-load.h), once, and again after late
 *   native defaults.
 * Built and run by tests/test_md_st.py.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned values[12], knobs[12][8], machine = 70, track = 3, selecting, trigs, recording, edits, knob_edits;
static unsigned lists, rectangles, dimmed;
static unsigned char track_type[12];
static unsigned short track_voice[12];
static unsigned get(void *view, unsigned id, unsigned char *locked)
{
    const unsigned engine = 0x1f9u + 6 * 32;
    (void)view; assert(id >= engine && id <= engine + 8); *locked = 0;
    return id == engine + 8 ? values[track] : knobs[track][id - engine];
}
static void fill(void *ctx, int x1, int y1, int x2, int y2, unsigned color)
{
    (void)ctx; assert(x1 >= 0 && x2 < 128 && y1 >= 0 && y2 < 64 && x1 <= x2 && y1 <= y2);
    if (x1 == 20 && y1 == 0 && x2 == 127 && y2 == 63 && !color) lists++;
    if (x1 == 23 && y1 == y2 && x2 > 30 && x2 < 60) dimmed++;           /* a name struck through */
    rectangles++;
}
#define MD_PICK_NATIVE 1
#define MD_GET get
#define MD_VIEW_TYPE(v) ((void)(v), machine)
#define MD_FILL fill
#define MD_TRACK_TYPE(t) track_type[t]
#define MD_TRACK_VOICE(t) (track_voice[t] >> 8)
#define STP_VIEW_TRACK(v) ((void)(v), track)
#define STP_TRIG(v) ((void)(v), trigs)
#define STP_RECORD(v) ((void)(v), recording)
#define STP_SELECTING(v) ((void)(v), selecting)
#include "../mods/md-st/main.c"
static void edit(void *view, st_u32 id, int delta, st_u32 knob)
{
    (void)view; assert(!selecting && !trigs && !recording && machine == MD_TYPE);
    if (id == MD_TYPE_ID) {assert(knob == MD_TYPE_CONTROL); values[track] = (unsigned)((int)values[track] + delta); edits++; return;}
    /* MD KIT 0.3: the chosen voice's knobs, each through its own control and knob */
    assert(id >= MD_ENGINE && id < MD_ENGINE + 8 && knob == id - MD_ENGINE && delta);
    knobs[track][knob] = (unsigned)((int)knobs[track][knob] + delta); knob_edits++;
    assert(knobs[track][knob] <= 0x7f00u && !(knobs[track][knob] & 0xffu));
}
static int loaded(unsigned type)
{
    unsigned i;
    for (i = 0; i < 8; ++i) if (knobs[track][i] != (unsigned)md_voice_load[type][i] << 8) return 0;
    return 1;
}
static int key(unsigned k, unsigned flags) {unsigned event[5] = {0, 0, 0, k, flags}; return stp_key_global(0, event);}
static int turn(int delta) {unsigned event[5] = {0, 0, 0, 5, (unsigned)delta}; return stp_enc_global(0, event);}
static unsigned tick(void) {unsigned char ctrl[0x40] = {0}; stp_tick(ctrl); return ctrl[0x20];}
static void seen(void *v) {stp_view_draw(v, 0, 0);}
static int idle(void *v) {return md_idle(0, &md_machinedrum, v, 0);}
static void open(void *v)
{
    selecting = 1; seen(v);
    assert(key(STP_KEY_YES, 1) == 1 && stp_sel.active);
    assert(key(STP_KEY_YES, 0) == 1);
}
static void choose(unsigned type)
{
    while (stp_sel.cursor != type) assert(turn(type > stp_sel.cursor ? 1 : -1));
    assert(!key(STP_KEY_YES, 1) && !stp_sel.active);
    assert(!key(STP_KEY_YES, 0)); selecting = 0;
}
static unsigned voice_of(const char *name)
{
    unsigned v;
    for (v = 0; v < MD_VOICES; ++v) if (!strcmp(md_voice_names[v], name)) return v;
    assert(!"no such voice"); return 0;
}
int main(void)
{
    unsigned char view[0x110] = {0}; static unsigned pages[] = {6};
    unsigned model, v, i;
    const unsigned rc = voice_of("PI-RC"), sd = voice_of("PI-SD"), xc = voice_of("TRX-XC"), bd = voice_of("TRX-BD");
    *(unsigned *)(view + 0x88) = 0x14; *(unsigned **)(view + 0x7c) = pages; *(void **)(view + 0x6c) = &model;
    struct st_machine_host_api host = {0}; struct st_machine_ui_api ui = {0}; struct st_smz_api api = {2, &ui};
    host.version = 17; ui.version = 7; ui.host = &host; ui.edit = edit;
    host.version = 16; assert(stp_init(&api)); host.version = 17;
    assert(!stp_init(&api));
    /* TYPE: configuration, the first voice by default, every voice in range */
    assert(md_machinedrum.params == 9 && md_machinedrum_ui.pages == 1);
    assert(md_controls[8].initial == 0 && md_controls[8].max == (MD_VOICES - 1u) << 8);
    assert(md_controls[8].alias == ST_MACHINE_CONFIG_PARAMETER);
    for (i = 0; i < 8; ++i) assert(md_layout[i] != 8);
    for (v = 0; v < MD_VOICES; ++v) {
        assert(md_voice_weight[v] <= MD_BUDGET);              /* every voice fits alone */
        for (const char *s = md_voice_names[v]; *s; s++) assert(stp_glyph(*s));
    }
    /* the list: no other MD track, every voice fits */
    values[3] = 4 << 8;
    selecting = 1; seen(view); machine = 66;
    assert(!key(STP_KEY_YES, 1) && !turn(1) && !stp_sel.active); machine = 70;
    open(view); assert(stp_sel.cursor == 4 && tick() && !tick());
    { unsigned count = lists; dimmed = 0; stp_frame_draw(0, 0); assert(lists == count + 1 && !dimmed); }
    assert(turn(5) && stp_sel.cursor == 5); assert(key(STP_KEY_UP, 1) && stp_sel.cursor == 4);
    assert(key(STP_KEY_DOWN, 3) && stp_sel.cursor == 11); assert(key(STP_KEY_UP, 3) && stp_sel.cursor == 4);
    assert(key(STP_KEY_NO, 1) && !stp_sel.active && key(STP_KEY_NO, 0));   /* back to the machine list */
    assert(!key(STP_KEY_NO, 1) && !key(STP_KEY_NO, 0) && selecting && !edits);
    open(view); choose(sd); assert(stp_pend.track == 4 && !edits && !knob_edits);
    assert(idle(view) && values[3] == sd << 8 && edits == 1 && loaded(sd));
    /* MD KIT 0.3: the voice's own knobs, set once; a turned knob afterwards stays */
    { unsigned n = knob_edits; knobs[3][1] = 5 << 8; idle(view); assert(knob_edits == n && knobs[3][1] == 5 << 8); }
    /* late native defaults put the first voice back: TYPE and the knobs are set again */
    values[3] = 0; idle(view); assert(values[3] == sd << 8 && edits == 2 && loaded(sd));
    /* a track with a voice opens the list on it, and loads another (TRX-BD: its own knobs too) */
    open(view); assert(stp_sel.cursor == sd); choose(bd); idle(view); assert(values[3] == bd << 8 && edits == 3 && loaded(bd));
    /* the budget: PI-RC on track 0 takes it whole; on track 3 only TRX-XC (weight 0) fits */
    track_type[0] = MD_TYPE; track_voice[0] = (unsigned short)(rc << 8);
    assert(!md_fits(3, bd) && md_fits(3, xc) && md_fits(0, rc));
    open(view); assert(stp_sel.cursor == xc);                 /* the nearest that fits */
    assert(!turn(1) || stp_sel.cursor == xc); assert(!turn(-1) || stp_sel.cursor == xc);
    assert(key(STP_KEY_DOWN, 1) && stp_sel.cursor == xc && key(STP_KEY_UP, 3) && stp_sel.cursor == xc);
    { unsigned count = lists; dimmed = 0; stp_frame_draw(0, 0); assert(lists == count + 1 && dimmed > 0); }
    stp_sel.cursor = bd; assert(key(STP_KEY_YES, 1) == 1 && stp_sel.active);   /* a dimmed voice: nothing */
    stp_sel.cursor = xc; choose(xc); idle(view); assert(values[3] == xc << 8 && edits == 4 && loaded(xc));
    /* another machine on track 0 frees the budget; a stock track counts nothing */
    track_type[0] = 66; assert(md_fits(3, rc));
    track_type[0] = MD_TYPE; track_voice[0] = (unsigned short)(sd << 8);
    assert(md_fits(3, xc) && md_fits(3, voice_of("PI-MA")) && !md_fits(3, bd));
    track_voice[0] = (unsigned short)(200 << 8); assert(md_fits(3, sd) == (md_voice_weight[0] + md_voice_weight[sd] <= MD_BUDGET));
    track_type[0] = 0;
    /* the knobs past the voice's last control take no turn; labels follow the voice */
    values[3] = voice_of("TRX-RS") << 8; selecting = 0; idle(view);
    assert(md_voice_controls[voice_of("TRX-RS")] == 3 && !strcmp(md_label[0], "PTCH") && !md_label[3][0]);
    assert(!md_spare_encoder(view, 0, MD_ENGINE + 2) && md_spare_encoder(view, 0, MD_ENGINE + 3));
    assert(!md_spare_encoder(view, 0, MD_ENGINE + 8));
    machine = 66; assert(!md_spare_encoder(view, 0, MD_ENGINE + 3)); machine = 70;
    /* the format: a voice's name, an old "none" as the first voice */
    assert(!strcmp(md_format(0, &md_machinedrum, MD_TYPE_CONTROL, rc << 8), "PI-RC"));
    assert(!strcmp(md_format(0, &md_machinedrum, MD_TYPE_CONTROL, MD_VOICES << 8), md_voice_names[0]));
    /* MD KIT 0.3: PI-BD/SD/MT/ML/RS show PTCH centred, as the Machinedrum; the rest 0..127 */
    for (v = 0; v < MD_VOICES; ++v) {
        const char *n = md_voice_names[v];
        const int centred = !strcmp(n, "PI-BD") || !strcmp(n, "PI-SD") || !strcmp(n, "PI-MT") || !strcmp(n, "PI-ML") || !strcmp(n, "PI-RS");
        values[3] = v << 8; idle(view);
        assert(!md_format(0, &md_machinedrum, 1, 22u << 8));
        assert(centred == (md_format(0, &md_machinedrum, 0, 22u << 8) != 0));
    }
    values[3] = voice_of("PI-MT") << 8; idle(view);
    assert(!strcmp(md_format(0, &md_machinedrum, 0, (unsigned)md_voice_load[voice_of("PI-MT")][0] << 8), "-42"));
    assert(!strcmp(md_format(0, &md_machinedrum, 0, 0), "-64") && !strcmp(md_format(0, &md_machinedrum, 0, 64u << 8), "0"));
    assert(!strcmp(md_format(0, &md_machinedrum, 0, 127u << 8), "63") && !strcmp(md_format(0, &md_machinedrum, 0, 80u << 8), "16"));
    puts("PASS main.c: TYPE as configuration, the list over the selector (YES/NO/encoders, a voice loaded or not), the budget (dimmed, passed, refused), spare knobs, labels, format, PI PTCH centred");
    return 0;
}
