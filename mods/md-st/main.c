/* SPDX-License-Identifier: GPL-2.0-or-later
 * main.c - the MD machine on MAIN: its descriptor, its SYN page, labels that
 * follow the voice, the voice list and the voice's name.
 *
 * The voice (mdkit's TYPE) is a hidden ninth control: it is in the Sound, on no
 * page. Its default is MD_PICK, "not chosen".
 *
 * The voice list in the machine selector. YES on MD MACHINEDRUM in the selector does
 * not reach the native: the selector stays open and the list of voices is painted over
 * it (the module's DRAW over the whole frame: the selector's machine list is a view
 * drawn after the SYN view; its keys and encoders through the global KEY and ENC,
 * before the native, whatever view takes them). In the list, any encoder or
 * UP/DOWN (FUNC: a page) moves the cursor; YES goes on to the native, which confirms
 * MD MACHINEDRUM and closes the selector, and the track's next MD page then sets the
 * voice at the cursor (an explicit edit of the hidden control, through the native
 * Sound/undo path, after the defaults a new machine gets); NO takes the list away and
 * leaves the selector as it was (another NO closes it, natively). A highlighted
 * machine is the selected one (the selector's preview runs the native select), so
 * the SYN view's machine is the one YES confirms.
 *
 * The voice list on the page: a Sound with MD and no voice (a machine's defaults
 * when the list was skipped) shows the same list in place of the knobs; YES loads.
 * AUDIO keeps a track with no voice silent (audio/md.c).
 *
 * The voice's name: arriving at an MD track's SYN page (from another page, another
 * track, the selector) or loading a voice shows its name in a box in the middle of the
 * screen for a second (MD_TAG_TICKS of the core's 30 Hz TICK), drawn by the machine's
 * draw in large letters of its own (md_glyphs).
 *
 * The core asks for a control's label each time it draws one (ST_LABEL,
 * mods/core-st/machines.c) and reads it through the descriptor's pointer, so the
 * labels are buffers that idle() fills from the voice of the Sound on screen. The
 * global KEY/ENC/DRAW/TICK use follows SYSTEM INFO's list (mods/system-info-st).
 */
#include <syntakt/machines.h>
#include <syntakt/modules.h>
#include "md-voices.h"
typedef st_u32 u32;typedef unsigned char u8;

#define MD_TYPE 70u
#define MD_ENGINE (0x1f9u + (MD_TYPE - 64u) * 32u)   /* control i is MD_ENGINE + i */
#define MD_TYPE_CONTROL 8u
#define MD_TYPE_ID (MD_ENGINE + MD_TYPE_CONTROL)
#define MD_PICK MD_VOICES                            /* the hidden control: no voice chosen yet */

/* OS 1.42 services, as the sample machines use them (mods/samplers-st) */
#ifndef MD_PICK_NATIVE
#define MD_GET ((u32 (*)(void *, u32, u8 *))0x40039160u)
#define MD_VIEW_TYPE ((u32 (*)(void *))0x40030622u)
#define MD_VIEW_TRACK ((u32 (*)(void *))0x4001ff74u)
#define MD_DELTA ((int (*)(void *, u32, u32))0x400f6866u)
#define MD_TRIG ((u32 (*)(void *))0x400c2dd6u)
#define MD_RECORD ((u32 (*)(void *))0x400c3582u)
#define MD_INVALIDATE ((void (*)(void *))0x400ffe22u)
#define MD_FONT ((void *(*)(void *))0x400f9478u)
#define MD_TEXT ((void (*)(void *, void *, int, int, u32, const char *))0x400f8b68u)
#define MD_FILL ((void (*)(void *, int, int, int, int, u32))0x400f7fb6u)
#define MD_PIXEL ((void (*)(void *, int, int, u32))0x400f7304u)
#define MD_FONT_DATA ((void *)0x443db210u)
#define MD_SELECTING ((u32 (*)(void *))0x400c3340u)   /* the SYN model shows the machine selector */
#endif
#define MD_KEY_FUNC 1u
#define MD_KEY_YES 12u
#define MD_KEY_NO 13u
#define MD_KEY_UP 14u
#define MD_KEY_DOWN 15u
#define MD_PICK_ROWS 7u

static char md_label[8][5] = {"PTCH", "DEC", "RAMP", "RDEC", "STRT", "NOIS", "HARM", "CLIP"};
static char md_long[8][16] = {"PTCH", "DEC", "RAMP", "RDEC", "STRT", "NOIS", "HARM", "CLIP"};
static char md_text[16];

#define K(i) {md_label[i], md_long[i], 0, 0x7f00, MD_FIRST_DEFAULT_##i << 8, 0x13}
static const struct core_machine_param md_controls[9] = {
    K(0), K(1), K(2), K(3), K(4), K(5), K(6), K(7),
    {"TYPE", "MACHINE TYPE", 0, MD_PICK << 8, MD_PICK << 8, 0x13},
};
/* type 70 (docs/ADAPTING.md, section 3); MD_TYPE says the same */
const struct core_machine_def md_machinedrum={70,9,"MD MACHINEDRUM","MDRM","SYN",md_controls,ST_MACHINE_NO_INIT,44,10,15};

/* the Sound's (or the lock's) value of a control, as the sample machines read it */
static u32 md_value(void *view, u32 id)
{u8 locked = 0; return MD_GET(view, id, &locked) & 65535u;}

/* the voice of the Sound on screen; MD_PICK when none is chosen */
static u32 md_type_of(void *view)
{u32 t = md_value(view, MD_TYPE_ID) >> 8; return t < MD_VOICES ? t : MD_PICK;}

static int md_copy(char *to, const char *from, u32 room)
{
    int changed = 0;
    u32 i;
    for (i = 0; i + 1 < room && from[i]; ++i) { changed |= to[i] != from[i]; to[i] = from[i]; }
    changed |= to[i] != 0; to[i] = 0;
    return changed;
}

static const struct st_machine_ui_api *md_api;

/* ---- SYN views ---- */
/* a machine's SYN view (page vector 6...), as the core's gate reads it */
static int md_syn_view(void *view)
{
    u8 *v = view;
    const u32 *pages;
    u32 vt;
    if (!view || *(u32 *)(v + 0x88) != 0x14) return 0;
#ifndef MD_PICK_NATIVE
    /* a machine parameter view (the core's core_machine_parameter_view classes) */
    vt = *(u32 *)v;
    if (vt != 0x401c7ad8u && vt != 0x401c8ad8u && vt != 0x401ca37cu && vt != 0x401c9034u &&
        vt != 0x40240294u && vt != 0x401c9a9cu) return 0;
#else
    (void)vt;
#endif
    pages = *(const u32 *const *)(v + 0x7c);
    return pages && pages[0] == 6;
}
static void *md_model(void *view) {return *(void **)((u8 *)view + 0x6c);}
static u32 md_track(void *view) {return MD_VIEW_TRACK(*(void **)((u8 *)view + 0x74));}
static int md_selecting(void *view) {return (u8)MD_SELECTING(md_model(view)) != 0;}
/* each track's SYN view and model, as last drawn (the module's DRAW of every view); the
 * global KEY and ENC see no view */
static void *md_views[12], *md_models[12];
/* the SYN view last drawn showing the machine selector, and its track: the one on screen
 * (several tracks' views may share a model) */
static void *md_sel_view;
static u32 md_sel_view_track;
static void md_seen(void *view)
{
    const u32 track = md_track(view);
    if (track < 12) { md_views[track] = view; md_models[track] = md_model(view); }
}
/* the voice the track's next MD page sets (track + 1; 0: none), for two seconds: while
 * the native confirms a machine the SYN view can still show the one it had (a YES
 * release goes through it), so another machine there is no reason to drop it */
#define MD_PEND_TICKS 60u
static volatile u32 md_now;                     /* TICKs seen (the module's subscription) */
static struct {u32 track, type, until;} md_pend;
static int md_pending(u32 track)
{
    if (md_pend.track && md_now >= md_pend.until) md_pend.track = 0;
    return md_pend.track && md_pend.track == track + 1u;
}

/* ---- the voice list on the page (a Sound with MD and no voice) ---- */
static struct {u32 view, track, cursor, shown, swallow, swallow_view;} md_pick;

static int md_pick_open(void *view)
{
    u32 track;
    if (!md_syn_view(view)) return 0;
    md_seen(view);
    if (md_selecting(view)) return 0;
    track = md_track(view);
    if (MD_VIEW_TYPE(view) != MD_TYPE) return 0;
    if (md_pending(track) || md_type_of(view) != MD_PICK) return 0;
    if (md_pick.view != (u32)view || md_pick.track != track) {
        md_pick.view = (u32)view; md_pick.track = track; md_pick.shown = MD_VOICES; md_pick.cursor = 0;
    }
    return 1;
}

static u32 md_step(u32 cursor, int delta)
{
    int next = (int)cursor + delta;
    if (next < 0) next = 0;
    if (next > (int)MD_VOICES - 1) next = MD_VOICES - 1;
    return (u32)next;
}

/* a knob past the voice's last control does nothing (md.c sends the voice its own
 * controls only): its encoder is taken, so no value changes and no popup shows */
static int md_spare_knob(void *view, u32 id)
{
    u32 type, index = id - MD_ENGINE;
    if (!md_syn_view(view) || md_selecting(view) || MD_VIEW_TYPE(view) != MD_TYPE || index >= 8) return 0;
    type = md_type_of(view);
    return type < MD_VOICES && index >= md_voice_controls[type];
}

static int md_pick_encoder(void *view, void *event, u32 id)
{
    u32 next;
    if (md_spare_knob(view, id)) return 1;
    if (!md_pick_open(view)) return 0;
    next = md_step(md_pick.cursor, MD_DELTA(event, 1, 1));
    if (next != md_pick.cursor) { md_pick.cursor = next; MD_INVALIDATE(view); }
    return 1;                                   /* the knobs are not on screen: take every turn */
}

static int md_pick_key(void *view, void *event, u32 phase)
{
    const u32 *words = event;
    u8 *v = view;
    u32 key, flags, pressed;
    if (phase != 1) return 0;
    key = words[3]; flags = words[4]; pressed = flags & 1u;
    if (md_pick.swallow && key == md_pick.swallow) {   /* the release of a key the list took */
        const int mine = md_pick.swallow_view == (u32)view;
        if (!pressed) md_pick.swallow = 0;
        if (mine) return 1;
    }
    if (!md_pick_open(view)) return 0;
    if (key == MD_KEY_UP || key == MD_KEY_DOWN) {
        if (pressed) {
            const u32 next = md_step(md_pick.cursor, (key == MD_KEY_UP ? -1 : 1) * (flags & 2u ? (int)MD_PICK_ROWS : 1));
            if (next != md_pick.cursor) { md_pick.cursor = next; MD_INVALIDATE(view); }
        }
        return 1;
    }
    if (key != MD_KEY_YES || !pressed || (flags & 8u)) return 0;
    /* A held trig or live recording would make the edit a lock: not a voice choice. */
    if ((u8)MD_TRIG(*(void **)(v + 0x6c)) || (u8)MD_RECORD(*(void **)(v + 0x6c))) return 1;
    md_api->edit(view, MD_TYPE_ID, (int)(md_pick.cursor << 8) - (int)md_value(view, MD_TYPE_ID), MD_TYPE_CONTROL);
    md_pick.swallow = key; md_pick.swallow_view = (u32)view;
    MD_INVALIDATE(view);
    return 1;
}

static char *md_number(char *p, u32 n) {if (n >= 10) *p++ = '0' + n / 10; *p++ = '0' + n % 10; *p = 0; return p;}

static void md_pick_draw(void *ctx)
{
    char row[16], *p;
    const char *s;
    u32 first, i, n;
    int y, thumb;
    void *font = MD_FONT(MD_FONT_DATA);
    first = md_pick.cursor > 2u ? md_pick.cursor - 2u : 0;
    if (first > MD_VOICES - MD_PICK_ROWS) first = MD_VOICES - MD_PICK_ROWS;
    /* Native LCD coordinates increase upwards: y = 54..63 is the header. The list
     * covers the page right of the track rail (x < 20), the knobs included. */
    MD_FILL(ctx, 20, 0, 127, 63, 0);
    MD_TEXT(ctx, font, 23, 56, 104, "CHOOSE A VOICE");
    MD_FILL(ctx, 20, 54, 127, 63, 0xffffffffu);
    for (i = 0; i < MD_PICK_ROWS; ++i) {
        n = first + i; p = row;
        if (n < 9) *p++ = '0';
        p = md_number(p, n + 1); *p++ = ' ';
        for (s = md_voice_names[n]; *s;) *p++ = *s++;
        *p = 0;
        y = 45 - (int)i * 7;          /* 45 .. 3 */
        MD_TEXT(ctx, font, 24, y, 96, row);
        if (n == md_pick.cursor) MD_FILL(ctx, 20, y - 1, 123, y + 5, 0xffffffffu);
    }
    for (y = 1; y <= 51; ++y) { MD_PIXEL(ctx, 126, y, (u32)y & 1u); MD_PIXEL(ctx, 127, y, ((u32)y ^ 1u) & 1u); }
    thumb = 48 - (int)(first * 47u / (MD_VOICES - MD_PICK_ROWS));
    MD_FILL(ctx, 125, thumb, 127, thumb + 3, 1);
    md_pick.shown = md_pick.cursor;
}

/* ---- letters of md-st's own ---- */
/* 5 x 5 letters, a row a byte (bit 4 = left), and their widths: the voices' names and
 * the selector list's title */
struct md_glyph {char c; u8 w; u8 rows[5];};
static const struct md_glyph md_glyphs[] = {
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
    {' ', 2, {0x00, 0x00, 0x00, 0x00, 0x00}},
};
static const struct md_glyph *md_glyph(char c)
{
    u32 i;
    for (i = 0; i < sizeof md_glyphs / sizeof md_glyphs[0]; ++i) if (md_glyphs[i].c == c) return md_glyphs + i;
    return 0;
}
/* text at one pixel a dot, its top row at y (LCD y up), in ink 1 or 0 */
static void md_print(void *ctx, int x, int top, const char *s, u32 ink)
{
    const struct md_glyph *g;
    int r, b;
    for (; *s; ++s) {
        if (!(g = md_glyph(*s))) continue;
        for (r = 0; r < 5; ++r)
            for (b = 0; b < g->w; ++b)
                if (g->rows[r] & (1u << (g->w - 1 - b))) MD_FILL(ctx, x + b, top - r, x + b, top - r, ink);
        x += g->w + 1;
    }
}

/* ---- the voice list in the machine selector ---- */
/* track: the selector's track while the list is up; swallow: a key whose release
 * the list keeps; dirty: a redraw to ask for at the next TICK */
static struct {u32 active, track, cursor, swallow, dirty;} md_sel;

/* the list is up while that track's SYN model still shows the selector */
static int md_sel_up(void)
{
    if (md_sel.active && !(md_models[md_sel.track] && (u8)MD_SELECTING(md_models[md_sel.track]))) {
        md_sel.active = 0; md_sel.dirty = 1;
    }
    return md_sel.active;
}

/* the global KEY, before the native and whatever view takes the key */
static int md_key_global(void *brain, void *event)
{
    const u32 *words = event;
    u32 key, flags, pressed, t;
    (void)brain;
    if (!event) return 0;
    key = words[3]; flags = words[4]; pressed = flags & 1u;
    if (md_sel.swallow && key == md_sel.swallow) {      /* the release of a key the list took */
        if (!pressed) md_sel.swallow = 0;
        return 1;
    }
    if (md_sel_up()) {
        if (key == MD_KEY_FUNC) return 0;
        if (!pressed) return 1;
        if (key == MD_KEY_UP || key == MD_KEY_DOWN) {
            md_sel.cursor = md_step(md_sel.cursor, (key == MD_KEY_UP ? -1 : 1) * (flags & 2u ? (int)MD_PICK_ROWS : 1));
            md_sel.dirty = 1;
        } else if (key == MD_KEY_YES && !(flags & 8u)) {
            /* the native confirms MD MACHINEDRUM; the track's next MD page sets the voice */
            md_pend.track = md_sel.track + 1u; md_pend.type = md_sel.cursor; md_pend.until = md_now + MD_PEND_TICKS;
            md_sel.active = 0; md_sel.dirty = 1;
            return 0;
        } else if (key == MD_KEY_NO) {                  /* back to the selector as it was */
            md_sel.active = 0; md_sel.dirty = 1; md_sel.swallow = key;
        }
        return 1;
    }
    /* YES on MD MACHINEDRUM in a selector: the list instead of the native's confirmation */
    if (key != MD_KEY_YES || !pressed || (flags & 8u)) return 0;
    {
        void *view = md_sel_view;
        if (!view || md_sel_view_track >= 12 || !md_syn_view(view) || !md_selecting(view)) return 0;
        if (MD_VIEW_TYPE(view) != MD_TYPE) return 0;
        t = md_sel_view_track;
        md_models[t] = md_model(view); md_views[t] = view;
        md_sel.active = 1; md_sel.track = t; md_sel.dirty = 1; md_sel.swallow = key;
        md_sel.cursor = md_type_of(view) < MD_VOICES ? md_type_of(view) : 0;
        return 1;
    }
}

/* the global ENC: while the list is up, every encoder moves the cursor */
static int md_enc_global(void *brain, void *event)
{
    const u32 *words = event;
    int delta;
    (void)brain;
    if (!event || !md_sel_up()) return 0;
    delta = (int)words[4];
    if (delta) { md_sel.cursor = md_step(md_sel.cursor, delta > 0 ? 1 : -1); md_sel.dirty = 1; }
    return 1;
}

/* over the selector: the page right of the track rail, as the list on the page */
static void md_sel_draw(void *bitmap)
{
    u32 first, i, n;
    int top, y, thumb;
    first = md_sel.cursor > 2u ? md_sel.cursor - 2u : 0;
    if (first > MD_VOICES - MD_PICK_ROWS) first = MD_VOICES - MD_PICK_ROWS;
    MD_FILL(bitmap, 20, 0, 127, 63, 0);
    MD_FILL(bitmap, 20, 54, 127, 63, 1);
    md_print(bitmap, 23, 61, "CHOOSE A VOICE", 0);
    for (i = 0; i < MD_PICK_ROWS; ++i) {
        n = first + i;
        top = 49 - (int)i * 7;        /* 49 .. 7 */
        if (n == md_sel.cursor) { MD_FILL(bitmap, 20, top - 5, 123, top + 1, 1); md_print(bitmap, 24, top, md_voice_names[n], 0); }
        else md_print(bitmap, 24, top, md_voice_names[n], 1);
    }
    for (y = 1; y <= 51; ++y) { MD_FILL(bitmap, 126, y, 126, y, (u32)y & 1u); MD_FILL(bitmap, 127, y, 127, y, ((u32)y ^ 1u) & 1u); }
    thumb = 48 - (int)(first * 47u / (MD_VOICES - MD_PICK_ROWS));
    MD_FILL(bitmap, 125, thumb, 127, thumb + 3, 1);
}

/* DRAW | VIEW: after each complete native view: remembers the SYN views (the one that
 * shows the selector included) */
static void md_view_draw(void *view, void *bitmap, u32 phase)
{
    (void)bitmap; (void)phase;
    if (!md_syn_view(view)) return;
    md_seen(view);
    if (md_selecting(view)) { md_sel_view = view; md_sel_view_track = md_track(view); }
    else if (md_sel_view == view) md_sel_view = 0;
}
/* DRAW: over the whole frame, after every view (the selector's machine list is a view
 * drawn after the SYN view's), the list while it is up */
static void md_frame_draw(void *bitmap, void *ctrl)
{
    (void)ctrl;
    if (md_sel_up()) md_sel_draw(bitmap);
}

/* ---- the voice's name, for a second ---- */
#define MD_TAG_TICKS 30u                         /* ev_tick is 30 Hz (docs/ADAPTING.md) */
static struct {u32 view, track, type, seen, until, drawn;} md_tag;

/* the name's width at two pixels a dot, one dot between letters */
static int md_tag_width(const char *s)
{
    int w = 0;
    const struct md_glyph *g;
    for (; *s; ++s) if ((g = md_glyph(*s))) w += 2 * (g->w + 1);
    return w ? w - 2 : 0;
}
/* The box, as the mock-up the user chose: a white box with a black line inside it in
 * the middle of the screen (LCD y up: rows 22..41), the name in black at two pixels a dot. */
static void md_tag_draw(void *ctx, const char *name)
{
    int x = 64 - md_tag_width(name) / 2, r, b;
    const struct md_glyph *g;
    MD_FILL(ctx, 30, 22, 97, 41, 1);
    MD_FILL(ctx, 31, 23, 96, 40, 0);
    MD_FILL(ctx, 32, 24, 95, 39, 1);
    for (; *name; ++name) {
        if (!(g = md_glyph(*name))) continue;
        for (r = 0; r < 5; ++r)
            for (b = 0; b < g->w; ++b)
                if (g->rows[r] & (1u << (g->w - 1 - b)))
                    MD_FILL(ctx, x + 2 * b, 35 - 2 * r, x + 2 * b + 1, 36 - 2 * r, 0);
        x += 2 * (g->w + 1);
    }
}
/* idle on an MD SYN page with a voice: a new page, track or voice starts the box; its
 * end asks for a redraw that leaves it out */
static int md_tag_idle(void *view, u32 type)
{
    const u32 now = md_now, track = md_track(view);
    if (!md_api) return 0;                      /* no module: no TICK to end the box */
    if (md_tag.view != (u32)view || md_tag.track != track || md_tag.type != type || now - md_tag.seen > 2u) {
        md_tag.view = (u32)view; md_tag.track = track; md_tag.type = type;
        md_tag.until = now + MD_TAG_TICKS; md_tag.seen = now;
        return 1;
    }
    md_tag.seen = now;
    if (md_tag.drawn && now >= md_tag.until) { md_tag.drawn = 0; return 1; }
    return 0;
}

/* the voice the selector's list chose, set on the track's MD page once the selector has
 * closed; not with a trig held. It is held for its two seconds, not set once: from
 * another machine the native's confirmation can write MD's defaults (no voice) after
 * the page has shown MD, and the voice is then set again. */
static int md_pend_apply(void *view)
{
    u8 *v = view;
    int delta;
    if (!md_api || MD_VIEW_TYPE(view) != MD_TYPE || !md_pending(md_track(view))) return 0;
    if ((u8)MD_TRIG(*(void **)(v + 0x6c)) || (u8)MD_RECORD(*(void **)(v + 0x6c))) return 0;
    delta = (int)(md_pend.type << 8) - (int)md_value(view, MD_TYPE_ID);
    if (!delta) return 0;
    md_api->edit(view, MD_TYPE_ID, delta, MD_TYPE_CONTROL);
    return 1;
}

/* ---- the machine's UI ---- */
static int md_idle(const struct st_machine_ui_api *api, const struct core_machine_def *m, void *view, u32 page)
{
    u32 type, i;
    int changed;
    (void)api; (void)m; (void)page;
    changed = md_pend_apply(view);
    if (md_pick_open(view)) return md_pick.shown != md_pick.cursor;
    type = md_type_of(view);
    if (type == MD_PICK) return changed;
    for (i = 0; i < 8; ++i) {
        changed |= md_copy(md_label[i], md_voice_labels[type][i], sizeof md_label[i]);
        changed |= md_copy(md_long[i], md_voice_labels[type][i], sizeof md_long[i]);
    }
    return md_tag_idle(view, type) | changed;
}

static void md_draw(const struct st_machine_ui_api *api, const struct core_machine_def *m, void *view, void *ctx, u32 page)
{
    u32 type;
    (void)api; (void)m; (void)page;
    if (md_pick_open(view)) { md_pick_draw(ctx); return; }
    type = md_type_of(view);
    /* the knobs past the voice's last control: an empty place (their dial and label;
     * knob i is column i % 4, row i / 4: dials 26 px apart, rows y 26..51 and 0..24) */
    if (type < MD_VOICES) {
        u32 i;
        for (i = md_voice_controls[type]; i < 8; ++i) {
            const int x = 21 + 26 * (int)(i % 4u), y = i < 4 ? 26 : 0;
            MD_FILL(ctx, x, y, x + 24, y + 25, 0);
        }
    }
    if (type < MD_VOICES && md_tag.view == (u32)view && md_tag.track == md_track(view) && md_tag.type == type &&
        md_now < md_tag.until) {
        md_tag_draw(ctx, md_voice_names[type]);
        md_tag.drawn = 1;
    }
}

static const char *md_format(const struct st_machine_ui_api *api, const struct core_machine_def *m, u32 index, u32 raw)
{
    (void)api; (void)m;
    if (index == MD_TYPE_CONTROL) {
        u32 t = raw >> 8;
        md_copy(md_text, t < MD_VOICES ? md_voice_names[t] : "---", sizeof md_text);
        return md_text;
    }
    return 0;                                   /* the knobs: 0..127, as the native */
}

/* one page, the voice's eight knobs; the voice itself is on none */
static const u32 md_layout[8] __attribute__((aligned(4))) = {0, 1, 2, 3, 4, 5, 6, 7};
const struct st_machine_ui md_machinedrum_ui = {
    MD_TYPE, 1, md_layout, md_draw, md_idle, ST_MACHINE_NO_CALLBACK, md_format, ST_MACHINE_NO_CALLBACK
};

static int md_pick_init(const struct st_smz_api *api)
{
    if (!api || api->version < 3 || !api->ui || api->ui->version < 7 || !api->ui->edit) return 1;
    md_api = api->ui;
    md_pick.view = md_pick.swallow = md_pick.swallow_view = 0;
    md_sel.active = md_sel.swallow = md_sel.dirty = 0;
    md_pend.track = 0;
    md_tag.view = md_tag.drawn = 0;
    return 0;
}
/* TICK: the time base of the name's box, and the list's redraw over the selector */
static void md_tick(void *ctrl)
{
    ++md_now;
    md_sel_up();
    if (md_sel.dirty) { md_sel.dirty = 0; ((u8 *)ctrl)[0x20] = 1; }
}
static const struct st_smz_subscription md_pick_events[] = {
    {ST_EV_ENC | ST_EV_VIEW, (void (*)(void))md_pick_encoder},
    {ST_EV_KEY | ST_EV_VIEW | ST_EV_PRE, (void (*)(void))md_pick_key},
    {ST_EV_TICK, (void (*)(void))md_tick},
    {ST_EV_KEY, (void (*)(void))md_key_global},
    {ST_EV_ENC, (void (*)(void))md_enc_global},
    {ST_EV_DRAW | ST_EV_VIEW, (void (*)(void))md_view_draw},
    {ST_EV_DRAW, (void (*)(void))md_frame_draw}};
const struct st_smz_module md_picker_module = {"MD VOICE PICKER", md_pick_init, md_pick_events, 7};
