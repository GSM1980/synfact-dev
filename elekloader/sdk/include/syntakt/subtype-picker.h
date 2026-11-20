/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bank-owned subtype selector, adapted from the uncommitted MD prototype.
 * See docs/SUBTYPE-PICKER.md for provenance, config and native boundaries.
 * Include once per MAIN translation unit with STP_MACHINE, STP_CONTROL_ID,
 * STP_CONTROL_INDEX, STP_COUNT, STP_NAMES, STP_DEFAULT, STP_MODULE,
 * STP_MODULE_NAME and STP_CONFIG_INIT(api). No core hooks or AUDIO work.
 */
#ifndef SYNTAKT_SUBTYPE_PICKER_IMPLEMENTATION_H
#define SYNTAKT_SUBTYPE_PICKER_IMPLEMENTATION_H
#include <syntakt/modules.h>
typedef st_u32 stp_u32;typedef unsigned char stp_u8;
/* OS 1.42 services, as the sample machines use them (mods/samplers-st) */
#ifndef STP_TEST_NATIVE
#define STP_GET ((stp_u32 (*)(void *, stp_u32, stp_u8 *))0x40039160u)
#define STP_VIEW_TYPE ((stp_u32 (*)(void *))0x40030622u)
#define STP_VIEW_TRACK ((stp_u32 (*)(void *))0x4001ff74u)
#define STP_TRIG ((stp_u32 (*)(void *))0x400c2dd6u)
#define STP_RECORD ((stp_u32 (*)(void *))0x400c3582u)
#define STP_FILL ((void (*)(void *, int, int, int, int, stp_u32))0x400f7fb6u)
#define STP_SELECTING ((stp_u32 (*)(void *))0x400c3340u)   /* the SYN model shows the machine selector */
#endif
#define STP_KEY_FUNC 1u
#define STP_KEY_YES 12u
#define STP_KEY_NO 13u
#define STP_KEY_UP 14u
#define STP_KEY_DOWN 15u
#define STP_ROWS (STP_COUNT < 7u ? STP_COUNT : 7u)


/* the Sound's (or the lock's) value of a control, as the sample machines read it */
static stp_u32 stp_value(void *view, stp_u32 id)
{stp_u8 locked = 0; return STP_GET(view, id, &locked) & 65535u;}

/* Stored subtype; out-of-range legacy values display the configured default. */
static stp_u32 stp_type_of(void *view)
{stp_u32 t = stp_value(view, STP_CONTROL_ID) >> 8; return t < STP_COUNT ? t : STP_DEFAULT;}


static const struct st_machine_ui_api *stp_api;
#define STP_TAG_TICKS 30u
static struct {stp_u32 view,track,type,seen,until,drawn;} stp_tag;

/* ---- SYN views ---- */
/* a machine's SYN view (page vector 6...), as the core's gate reads it */
static int stp_syn_view(void *view)
{
    stp_u8 *v = view;
    const stp_u32 *pages;
    stp_u32 vt;
    if (!view || *(stp_u32 *)(v + 0x88) != 0x14) return 0;
#ifndef STP_TEST_NATIVE
    /* a machine parameter view (the core's core_machine_parameter_view classes) */
    vt = *(stp_u32 *)v;
    if (vt != 0x401c7ad8u && vt != 0x401c8ad8u && vt != 0x401ca37cu && vt != 0x401c9034u &&
        vt != 0x40240294u && vt != 0x401c9a9cu) return 0;
#else
    (void)vt;
#endif
    pages = *(const stp_u32 *const *)(v + 0x7c);
    return pages && pages[0] == 6;
}
static void *stp_model(void *view) {return *(void **)((stp_u8 *)view + 0x6c);}
static stp_u32 stp_track(void *view) {return STP_VIEW_TRACK(*(void **)((stp_u8 *)view + 0x74));}
static int stp_selecting(void *view) {return (stp_u8)STP_SELECTING(stp_model(view)) != 0;}
/* each track's SYN view and model, as last drawn (the module's DRAW of every view); the
 * global KEY and ENC see no view */
static void *stp_views[12], *stp_models[12];
/* the SYN view last drawn showing the machine selector, and its track: the one on screen
 * (several tracks' views may share a model) */
static void *stp_sel_view;
static stp_u32 stp_sel_view_track;
static void stp_seen(void *view)
{
    const stp_u32 track = stp_track(view);
    if (track < 12) { stp_views[track] = view; stp_models[track] = stp_model(view); }
}
/* The subtype the track's next SYN page sets (track + 1; 0: none), for two seconds: while
 * the native confirms a machine the SYN view can still show the one it had (a YES
 * release goes through it), so another machine there is no reason to drop it */
#define STP_PEND_TICKS 60u
static volatile stp_u32 stp_now;                     /* TICKs seen (the module's subscription) */
static struct {stp_u32 track, type, until, applied; void *model;} stp_pend;
static int stp_pending(stp_u32 track)
{
    if (stp_pend.track && (int)(stp_now - stp_pend.until) >= 0) stp_pend.track = 0;
    return stp_pend.track && stp_pend.track == track + 1u;
}


static stp_u32 stp_step(stp_u32 cursor, int delta)
{
    int next = (int)cursor + delta;
    if (next < 0) next = 0;
    if (next > (int)STP_COUNT - 1) next = STP_COUNT - 1;
    return (stp_u32)next;
}


/* ---- letters of md-st's own ---- */
/* 5 x 5 letters, a row a byte (bit 4 = left), and their widths: the voices' names and
 * the selector list's title */
struct stp_glyph {char c; stp_u8 w; stp_u8 rows[5];};
static const struct stp_glyph stp_glyphs[] = {
    {'A', 4, {0x06, 0x09, 0x0f, 0x09, 0x09}}, {'B', 4, {0x0e, 0x09, 0x0e, 0x09, 0x0e}},
    {'C', 4, {0x07, 0x08, 0x08, 0x08, 0x07}}, {'D', 4, {0x0e, 0x09, 0x09, 0x09, 0x0e}},
    {'E', 4, {0x0f, 0x08, 0x0e, 0x08, 0x0f}}, {'F', 4, {0x0f, 0x08, 0x0e, 0x08, 0x08}},
    {'H', 4, {0x09, 0x09, 0x0f, 0x09, 0x09}}, {'I', 3, {0x07, 0x02, 0x02, 0x02, 0x07}},
    {'L', 4, {0x08, 0x08, 0x08, 0x08, 0x0f}}, {'M', 5, {0x11, 0x1b, 0x15, 0x11, 0x11}},
    {'O', 4, {0x06, 0x09, 0x09, 0x09, 0x06}}, {'P', 4, {0x0e, 0x09, 0x0e, 0x08, 0x08}},
    {'R', 4, {0x0e, 0x09, 0x0e, 0x0a, 0x09}}, {'S', 4, {0x07, 0x08, 0x06, 0x01, 0x0e}},
    {'T', 3, {0x07, 0x02, 0x02, 0x02, 0x02}}, {'V', 5, {0x11, 0x11, 0x0a, 0x0a, 0x04}},
    {'X', 5, {0x11, 0x0a, 0x04, 0x0a, 0x11}}, {'Y', 5, {0x11, 0x0a, 0x04, 0x04, 0x04}},
    {'2', 4, {0x0e, 0x01, 0x06, 0x08, 0x0f}}, {'-', 3, {0x00, 0x00, 0x07, 0x00, 0x00}},
    {'N', 4, {0x09,0x0d,0x0b,0x09,0x09}}, {'U', 4, {0x09,0x09,0x09,0x09,0x06}},
    {'W', 5, {0x11,0x11,0x15,0x1b,0x11}}, {'G', 4, {0x07,0x08,0x0b,0x09,0x07}},
    {'J', 4, {0x03,0x01,0x01,0x09,0x06}}, {'K', 4, {0x09,0x0a,0x0c,0x0a,0x09}},
    {'Q', 5, {0x0c,0x12,0x12,0x16,0x0d}}, {'Z', 4, {0x0f,0x02,0x04,0x08,0x0f}},
    {'0', 4, {0x06,0x09,0x09,0x09,0x06}}, {'1', 3, {0x02,0x06,0x02,0x02,0x07}},
    {'3', 4, {0x0e,0x01,0x06,0x01,0x0e}}, {'4', 4, {0x09,0x09,0x0f,0x01,0x01}},
    {'5', 4, {0x0f,0x08,0x0e,0x01,0x0e}}, {'6', 4, {0x07,0x08,0x0e,0x09,0x06}},
    {'7', 4, {0x0f,0x01,0x02,0x04,0x04}}, {'8', 4, {0x06,0x09,0x06,0x09,0x06}},
    {'9', 4, {0x06,0x09,0x07,0x01,0x0e}}, {'+', 3, {0x00,0x02,0x07,0x02,0x00}},
    {' ', 2, {0x00, 0x00, 0x00, 0x00, 0x00}},
};
static const struct stp_glyph *stp_glyph(char c)
{
    stp_u32 i;
    for (i = 0; i < sizeof stp_glyphs / sizeof stp_glyphs[0]; ++i) if (stp_glyphs[i].c == c) return stp_glyphs + i;
    return 0;
}
/* text at one pixel a dot, its top row at y (LCD y up), in ink 1 or 0 */
static void stp_print(void *ctx, int x, int top, const char *s, stp_u32 ink)
{
    const struct stp_glyph *g;
    int r, b;
    for (; *s; ++s) {
        if (!(g = stp_glyph(*s))) continue;
        for (r = 0; r < 5; ++r)
            for (b = 0; b < g->w; ++b)
                if (g->rows[r] & (1u << (g->w - 1 - b))) STP_FILL(ctx, x + b, top - r, x + b, top - r, ink);
        x += g->w + 1;
    }
}


/* ---- subtype list in the native machine selector ---- */
/* track: the selector's track while the list is up; swallow: a key whose release
 * the list keeps; dirty: a redraw to ask for at the next TICK */
static struct {stp_u32 active, track, cursor, swallow, dirty;} stp_sel;

/* the list is up while that track's SYN model still shows the selector */
static int stp_sel_up(void)
{
    if (stp_sel.active && (!stp_sel_view || stp_sel.track!=stp_sel_view_track ||
        STP_VIEW_TYPE(stp_sel_view)!=STP_MACHINE ||
        !(stp_models[stp_sel.track] && (stp_u8)STP_SELECTING(stp_models[stp_sel.track])))) {
        stp_sel.active = 0; stp_sel.dirty = 1;
    }
    return stp_sel.active;
}

/* the global KEY, before the native and whatever view takes the key */
static int stp_key_global(void *brain, void *event)
{
    const stp_u32 *words = event;
    stp_u32 key, flags, pressed, t;
    (void)brain;
    if (!event) return 0;
    key = words[3]; flags = words[4]; pressed = flags & 1u;
    if (pressed && stp_pend.applied) stp_pend.track = 0;
    if (stp_sel.swallow && key == stp_sel.swallow) {      /* the release of a key the list took */
        if (!pressed) stp_sel.swallow = 0;
        return 1;
    }
    if (stp_sel_up()) {
        if (key == STP_KEY_FUNC) return 0;
        if (!pressed) return 1;
        if (key == STP_KEY_UP || key == STP_KEY_DOWN) {
            stp_sel.cursor = stp_step(stp_sel.cursor, (key == STP_KEY_UP ? -1 : 1) * (flags & 2u ? (int)STP_ROWS : 1));
            stp_sel.dirty = 1;
        } else if (key == STP_KEY_YES && !(flags & 8u)) {
            if ((stp_u8)STP_TRIG(stp_models[stp_sel.track]) ||
                (stp_u8)STP_RECORD(stp_models[stp_sel.track])) return 1;
            /* Native confirmation loads the machine; its next SYN page sets the subtype. */
            stp_pend.track = stp_sel.track + 1u; stp_pend.type = stp_sel.cursor;
            stp_pend.until = stp_now + STP_PEND_TICKS; stp_pend.applied = 0;
            stp_pend.model = stp_models[stp_sel.track];
            stp_sel.active = 0; stp_sel.dirty = 1;
            return 0;
        } else if (key == STP_KEY_NO) {                  /* back to the selector as it was */
            stp_sel.active = 0; stp_sel.dirty = 1; stp_sel.swallow = key;
        }
        return 1;
    }
    /* YES on this machine opens the subtype list before native confirmation. */
    if (key != STP_KEY_YES || !pressed || (flags & 8u)) return 0;
    {
        void *view = stp_sel_view;
        if (!view || stp_sel_view_track >= 12 || !stp_syn_view(view) || !stp_selecting(view)) return 0;
        if (STP_VIEW_TYPE(view) != STP_MACHINE) return 0;
        t = stp_sel_view_track;
        stp_models[t] = stp_model(view); stp_views[t] = view;
        stp_sel.active = 1; stp_sel.track = t; stp_sel.dirty = 1; stp_sel.swallow = key;
        stp_tag.view=0;
        stp_sel.cursor = stp_type_of(view) < STP_COUNT ? stp_type_of(view) : 0;
        return 1;
    }
}

/* the global ENC: while the list is up, every encoder moves the cursor */
static int stp_enc_global(void *brain, void *event)
{
    const stp_u32 *words = event;
    int delta;
    (void)brain;
    if (!event) return 0;
    if (stp_pend.applied) stp_pend.track = 0;
    if (!stp_sel_up()) return 0;
    delta = (int)words[4];
    if (delta) { stp_sel.cursor = stp_step(stp_sel.cursor, delta > 0 ? 1 : -1); stp_sel.dirty = 1; }
    return 1;
}

/* over the selector: the page right of the track rail, as the list on the page */
static void stp_sel_draw(void *bitmap)
{
    stp_u32 first, i, n;
    int top, y, thumb;
    first = stp_sel.cursor > 2u ? stp_sel.cursor - 2u : 0;
    if (first > STP_COUNT - STP_ROWS) first = STP_COUNT - STP_ROWS;
    STP_FILL(bitmap, 20, 0, 127, 63, 0);
    STP_FILL(bitmap, 20, 54, 127, 63, 1);
    stp_print(bitmap, 23, 61, "CHOOSE A TYPE", 0);
    for (i = 0; i < STP_ROWS; ++i) {
        n = first + i;
        top = 49 - (int)i * 7;        /* 49 .. 7 */
        if (n == stp_sel.cursor) { STP_FILL(bitmap, 20, top - 5, 123, top + 1, 1); stp_print(bitmap, 24, top, STP_NAMES[n], 0); }
        else stp_print(bitmap, 24, top, STP_NAMES[n], 1);
    }
    for (y = 1; y <= 51; ++y) { STP_FILL(bitmap, 126, y, 126, y, (stp_u32)y & 1u); STP_FILL(bitmap, 127, y, 127, y, ((stp_u32)y ^ 1u) & 1u); }
    thumb = 48 - (int)(first * 47u / (STP_COUNT > STP_ROWS ? STP_COUNT - STP_ROWS : 1u));
    STP_FILL(bitmap, 125, thumb, 127, thumb + 3, 1);
}

/* DRAW | VIEW: after each complete native view: remembers the SYN views (the one that
 * shows the selector included) */
static void stp_view_draw(void *view, void *bitmap, stp_u32 phase)
{
    (void)bitmap; (void)phase;
    if (!stp_syn_view(view)) return;
    stp_seen(view);
    if (!stp_selecting(view) && STP_VIEW_TYPE(view)!=STP_MACHINE) stp_tag.view=0;
    if (stp_selecting(view)) { stp_sel_view = view; stp_sel_view_track = stp_track(view); }
    else if (stp_sel_view == view) stp_sel_view = 0;
}
/* DRAW: over the whole frame, after every view (the selector's machine list is a view
 * drawn after the SYN view's), the list while it is up */
static void stp_frame_draw(void *bitmap, void *ctrl)
{
    (void)ctrl;
    if (stp_sel_up()) stp_sel_draw(bitmap);
}


/* ---- subtype name for one second ---- */

/* the name's width at two pixels a dot, one dot between letters */
static int stp_tag_width(const char *s)
{
    int w = 0;
    const struct stp_glyph *g;
    for (; *s; ++s) if ((g = stp_glyph(*s))) w += 2 * (g->w + 1);
    return w ? w - 2 : 0;
}
/* The MD prototype's bordered box, extended to fit longer subtype names, in
 * the middle of the screen (LCD y up: rows 22..41), the name in black at two pixels a dot. */
static void stp_tag_draw(void *ctx, const char *name)
{
    int scale = stp_tag_width(name) > 96 ? 1 : 2;
    int text_width = (stp_tag_width(name) + 2) * scale / 2 - scale;
    int width = text_width + 12, r, b;
    const struct stp_glyph *g;
    if (width < 68) width = 68;
    if (width > 108) width = 108;
    int left = 64 - width / 2, x = 64 - text_width / 2;
    STP_FILL(ctx,left,22,left+width-1,41,1);
    STP_FILL(ctx,left+1,23,left+width-2,40,0);
    STP_FILL(ctx,left+2,24,left+width-3,39,1);
    for (; *name; ++name) {
        if (!(g = stp_glyph(*name))) continue;
        for (r=0;r<5;r++) for (b=0;b<g->w;b++) if (g->rows[r] & (1u << (g->w-1-b)))
            STP_FILL(ctx,x+scale*b,35-scale*r,x+scale*b+scale-1,35-scale*r+scale-1,0);
        x += scale*(g->w+1);
    }
}
/* A new SYN page, track or subtype starts the box; its
 * end asks for a redraw that leaves it out */
static int stp_tag_idle(void *view, stp_u32 type)
{
    const stp_u32 now = stp_now, track = stp_track(view);
    if (!stp_api) return 0;                      /* no module: no TICK to end the box */
    if (stp_tag.view != (stp_u32)view || stp_tag.track != track || stp_tag.type != type || now - stp_tag.seen > 2u) {
        stp_tag.view = (stp_u32)view; stp_tag.track = track; stp_tag.type = type;
        stp_tag.until = now + STP_TAG_TICKS; stp_tag.seen = now;
        return 1;
    }
    stp_tag.seen = now;
    if (stp_tag.drawn && (int)(now - stp_tag.until) >= 0) { stp_tag.drawn = 0; return 1; }
    return 0;
}

/* Apply through the native edit service after the selector closes. A bounded
 * retry covers late native defaults; subsequent user input, a different model
 * or a non-default value cancels it. Trig holds/recording never create a lock. */
static int stp_pend_apply(void *view)
{
    stp_u8 *v = view;
    int delta;
    if (!stp_api || STP_VIEW_TYPE(view) != STP_MACHINE || !stp_pending(stp_track(view))) return 0;
    if (stp_model(view)!=stp_pend.model) {stp_pend.track=0;return 0;}
    if ((stp_u8)STP_TRIG(*(void **)(v + 0x6c)) || (stp_u8)STP_RECORD(*(void **)(v + 0x6c))) return 0;
    stp_u32 value = stp_value(view, STP_CONTROL_ID);
    if (stp_pend.applied && value != (stp_pend.type << 8) && value != (STP_DEFAULT << 8)) {
        stp_pend.track = 0; return 0;
    }
    delta = (int)(stp_pend.type << 8) - (int)value;
    stp_pend.applied=1;
    if (!delta) return 0;
    stp_api->edit(view, STP_CONTROL_ID, delta, STP_CONTROL_INDEX);
    return 1;
}

/* ---- the machine's UI ---- */

static int stp_idle(void *view)
{
    if (!stp_api || !stp_syn_view(view) || stp_selecting(view) || STP_VIEW_TYPE(view)!=STP_MACHINE) return 0;
    int changed=stp_pend_apply(view);
    return stp_tag_idle(view,stp_type_of(view)) | changed;
}
static void stp_draw(void *view,void *bitmap)
{
    if (!stp_api || !stp_syn_view(view) || stp_selecting(view) || STP_VIEW_TYPE(view)!=STP_MACHINE) return;
    stp_u32 type=stp_type_of(view);
    if (stp_tag.view==(stp_u32)view && stp_tag.track==stp_track(view) && stp_tag.type==type &&
        (int)(stp_now-stp_tag.until)<0) {stp_tag_draw(bitmap,STP_NAMES[type]);stp_tag.drawn=1;}
}
static int stp_init(const struct st_smz_api *api)
{
    if (!api || api->version<2 || !api->ui || api->ui->version<7 || !api->ui->edit) return 1;
    if (STP_CONFIG_INIT(api)) return 1;
    stp_api=api->ui;stp_sel_view=0;stp_sel.active=stp_sel.swallow=stp_sel.dirty=0;
    stp_pend.track=stp_pend.applied=0;stp_now=0;stp_tag.view=stp_tag.drawn=0;
    for (stp_u32 t=0;t<12;t++){stp_views[t]=0;stp_models[t]=0;}
    return 0;
}
static void stp_tick(void *ctrl)
{
    stp_now++;
    if (stp_sel.dirty) {stp_sel.dirty=0;((stp_u8 *)ctrl)[0x20]=1;}
}
static const struct st_smz_subscription stp_events[]={
    {ST_EV_TICK,(void (*)(void))stp_tick},
    {ST_EV_KEY,(void (*)(void))stp_key_global},
    {ST_EV_ENC,(void (*)(void))stp_enc_global},
    {ST_EV_DRAW|ST_EV_VIEW,(void (*)(void))stp_view_draw},
    {ST_EV_DRAW,(void (*)(void))stp_frame_draw}};
const struct st_smz_module STP_MODULE={STP_MODULE_NAME,stp_init,stp_events,5};
#endif
