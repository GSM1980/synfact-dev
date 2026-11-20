/* SPDX-License-Identifier: GPL-2.0-or-later
 * md_st_picker_check.c - the MD machine's MAIN side (mods/md-st/main.c) on the
 * host, its firmware services replaced (MD_PICK_NATIVE): the descriptor and the
 * one-page layout, the voice list (on the page and over the machine selector) and
 * the voice's name box.
 *
 * Checks: the hidden voice control defaults to "none chosen" and is on no page. On a
 * page with no voice: the list, encoders, UP/DOWN, YES loads (not with a trig held),
 * its release swallowed. In the machine selector: YES on MD MACHINEDRUM is taken and
 * the list is drawn over the selector's view (another machine's YES is the native's);
 * encoders and UP/DOWN move it; NO takes the list away and keeps the selector, a second
 * NO is the native's; YES in the list goes on to the native and the track's next MD
 * page sets the voice; the list goes when the selector does. The name box: a second
 * after arriving, again after a gap, another track or voice. The voice's popup.
 * Built and run by tests/test_md_st.py.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef unsigned int hu32;
static hu32 type_value, view_type = 70, view_track = 3;
static int invalidated, edits, trig_held, selecting, boxes, dots, lists, pixels, spares;
static char texts[16][32];
static int ntexts;
struct host_event {hu32 words[8]; int delta;};

static hu32 host_get(void *view, hu32 id, unsigned char *locked)
{(void)view; *locked = 0; return id == 0x1f9u + 6u * 32u + 8u ? type_value : 0x4000u;}
static void host_text(int x, int y, const char *s)
{(void)x; (void)y; if (ntexts < 16) { strncpy(texts[ntexts], s, 31); texts[ntexts][31] = 0; ++ntexts; }}
static void host_fill(void *ctx, int a, int b, int c, int d, hu32 e)
{
    (void)ctx;
    if (a == 30 && b == 22 && c == 97 && d == 41 && e == 1) ++boxes;
    if (c == a + 1 && d == b + 1 && !e) ++dots;
    if (a == 20 && b == 0 && c == 127 && d == 63 && !e) ++lists;
    if (c == a && d == b) ++pixels;
    if (c == a + 24 && d == b + 25 && !e && (a - 21) % 26 == 0 && (b == 0 || b == 26)) ++spares;
}
static void host_pixel(void *ctx, int a, int b, hu32 c) {(void)ctx; (void)a; (void)b; (void)c;}

#define MD_PICK_NATIVE 1
#define MD_GET(view, id, locked) host_get(view, id, locked)
#define MD_VIEW_TYPE(view) view_type
#define MD_VIEW_TRACK(model) view_track
#define MD_DELTA(event, a, b) (((struct host_event *)(event))->delta)
#define MD_TRIG(x) ((hu32)trig_held)
#define MD_RECORD(x) 0u
#define MD_INVALIDATE(view) ((void)++invalidated)
#define MD_FONT(x) ((void *)0)
#define MD_TEXT(ctx, font, x, y, w, s) host_text(x, y, s)
#define MD_FILL host_fill
#define MD_PIXEL host_pixel
#define MD_FONT_DATA 0
#define MD_SELECTING(model) ((hu32)selecting)
#include "../mods/md-st/main.c"

static void host_edit(void *view, st_u32 id, int delta, st_u32 knob)
{(void)view; (void)knob; if (id == MD_TYPE_ID) type_value = (hu32)((int)type_value + delta); ++edits;}

static int bad;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); bad = 1; } } while (0)

static int drawn(const char *s)
{
    for (int i = 0; i < ntexts; ++i) if (!strcmp(texts[i], s)) return 1;
    return 0;
}
static void draw(void *view) {ntexts = 0; md_machinedrum_ui.draw(0, &md_machinedrum, view, 0, 0);}
static int idle(void *view) {return md_machinedrum_ui.idle(0, &md_machinedrum, view, 0);}
static int turn(void *view, int delta) {struct host_event e = {{0}, delta}; return md_pick_encoder(view, &e, MD_ENGINE);}
static int key(void *view, hu32 k, hu32 flags) {struct host_event e = {{0, 0, 0, k, flags}, 0}; return md_pick_key(view, &e, 1);}
static int gkey(hu32 k, hu32 flags) {struct host_event e = {{0, 0, 0, k, flags}, 0}; return md_key_global(0, &e);}
static int genc(hu32 encoder, int delta) {struct host_event e = {{0, 0, 0, encoder, (hu32)delta}, 0}; return md_enc_global(0, &e);}
static int view_draw(void *view) {lists = 0; pixels = 0; md_view_draw(view, 0, 0); return lists;}
static int frame_draw(void) {lists = 0; pixels = 0; md_frame_draw(0, 0); return lists;}
static int tick(void) {unsigned char ctrl[0x40] = {0}; md_tick(ctrl); return ctrl[0x20];}

int main(void)
{
    static unsigned char view[0x100] __attribute__((aligned(8)));
    static const hu32 syn_pages[2] = {6, 0};
    static int model;
    struct st_machine_ui_api ui;
    struct st_smz_api api;
    unsigned i;
    memset(&ui, 0, sizeof ui); ui.version = 7; ui.edit = host_edit;
    api.version = 3; api.ui = &ui;
    *(hu32 *)(view + 0x88) = 0x14;
    memcpy(view + 0x7c, &(const hu32 *){syn_pages}, sizeof(const hu32 *));   /* a machine's SYN page vector */
    memcpy(view + 0x6c, &(void *){&model}, sizeof(void *));                   /* the view's model */

    /* the descriptor: the voice is the ninth control, "none chosen" by default, on no page */
    CHECK(md_machinedrum.params == 9 && md_controls[8].initial == MD_PICK << 8 && md_controls[8].max == MD_PICK << 8,
          "the voice control does not default to none");
    CHECK(md_machinedrum_ui.pages == 1, "%u pages", md_machinedrum_ui.pages);
    for (i = 0; i < 8; ++i) CHECK(md_layout[i] == i, "page slot %u holds %u", i, md_layout[i]);
    CHECK(md_pick_init(&api) == 0, "the module refused a UI API 7 core");

    /* ---- the list on the page: a Sound with MD and no voice ---- */
    type_value = MD_PICK << 8;
    CHECK(idle(view), "idle does not ask for the list");
    draw(view);
    CHECK(drawn("CHOOSE A VOICE") && drawn("01 TRX-BD") && drawn("07 TRX-CH") && !drawn("08 TRX-OH"), "the list is not drawn");
    CHECK(!idle(view), "idle redraws a list that did not change");
    invalidated = 0;
    CHECK(turn(view, 3) == 1 && md_pick.cursor == 3 && invalidated == 1, "a turn did not move the cursor");
    CHECK(turn(view, -10) == 1 && md_pick.cursor == 0, "the cursor passed the first voice");
    CHECK(turn(view, 100) == 1 && md_pick.cursor == MD_VOICES - 1, "the cursor passed the last voice");
    draw(view);
    CHECK(drawn("30 PI-HH") && drawn("24 PI-MT") && !drawn("23 PI-SD"), "the end of the list is not drawn");
    CHECK(turn(view, -16) == 1 && md_pick.cursor == 13, "the cursor did not come back");
    CHECK(key(view, MD_KEY_UP, 1) == 1 && md_pick.cursor == 12 && key(view, MD_KEY_UP, 0) == 1, "UP");
    CHECK(key(view, MD_KEY_DOWN, 1 | 2) == 1 && md_pick.cursor == 12 + MD_PICK_ROWS, "FUNC+DOWN");
    CHECK(key(view, MD_KEY_UP, 1 | 2) == 1 && key(view, MD_KEY_DOWN, 1) == 1 && md_pick.cursor == 13, "UP, DOWN");
    CHECK(!key(view, MD_KEY_NO, 1), "NO on the page list was taken");
    trig_held = 1; edits = 0;
    CHECK(key(view, MD_KEY_YES, 1) == 1 && !edits && type_value == MD_PICK << 8, "YES with a trig held edited");
    trig_held = 0;
    CHECK(key(view, MD_KEY_YES, 1) == 1 && edits == 1 && type_value == 13u << 8, "YES loaded %u", type_value >> 8);
    CHECK(key(view, MD_KEY_YES, 0) == 1 && !key(view, MD_KEY_YES, 0), "the release of YES");
    draw(view);
    CHECK(ntexts == 0 && !turn(view, 1) && !key(view, MD_KEY_YES, 1), "the list stayed over a chosen voice");
    idle(view);
    for (i = 0; i < 8; ++i) CHECK(!strcmp(md_label[i], md_voice_labels[13][i]), "label %u is %s", i, md_label[i]);

    /* ---- the list over the machine selector ---- */
    /* the selector open on track 3 (EFM-BD), MD highlighted: its view draws natively */
    selecting = 1;
    CHECK(view_draw(view) == 0 && frame_draw() == 0 && md_views[3] == view, "the selector's view is not remembered");
    CHECK(!genc(1, 1) && !gkey(MD_KEY_DOWN, 1), "the selector's encoder or DOWN was taken");
    /* YES on another machine: the native's */
    view_type = 66;
    CHECK(!gkey(MD_KEY_YES, 1) && !md_sel.active && !gkey(MD_KEY_YES, 0), "YES on another machine was taken");
    view_type = 70;
    /* YES on MD MACHINEDRUM: the list, on the voice the Sound has */
    CHECK(gkey(MD_KEY_YES, 1) == 1 && md_sel.active && md_sel.track == 3 && md_sel.cursor == 13, "YES on MD did not open the list");
    CHECK(gkey(MD_KEY_YES, 0) == 1, "the release of that YES reached the native");
    CHECK(tick() == 1 && tick() == 0, "the list asked for no redraw");
    CHECK(view_draw(view) == 0, "the list is drawn under the selector's later views");
    CHECK(frame_draw() == 1 && pixels > 50, "the list is not drawn over the frame");
    draw(view);
    CHECK(ntexts == 0, "the page list is drawn over the selector");
    /* the cursor: encoders (one a turn) and UP/DOWN, FUNC passes */
    CHECK(genc(5, 3) == 1 && md_sel.cursor == 14 && tick() == 1, "an encoder did not move the cursor");
    CHECK(gkey(MD_KEY_DOWN, 1) == 1 && gkey(MD_KEY_DOWN, 0) == 1 && md_sel.cursor == 15, "DOWN");
    CHECK(gkey(MD_KEY_UP, 1 | 2) == 1 && md_sel.cursor == 15 - MD_PICK_ROWS, "FUNC+UP");
    CHECK(gkey(MD_KEY_DOWN, 1 | 2) == 1 && md_sel.cursor == 15, "FUNC+DOWN");
    CHECK(!gkey(MD_KEY_FUNC, 1) && !gkey(MD_KEY_FUNC, 0), "FUNC was taken");
    CHECK(gkey(40, 1) == 1 && gkey(40, 0) == 1, "another key reached the native under the list");
    /* NO: the list goes, the selector stays; a second NO is the native's */
    CHECK(gkey(MD_KEY_NO, 1) == 1 && !md_sel.active && gkey(MD_KEY_NO, 0) == 1 && tick() == 1, "NO did not close the list");
    CHECK(frame_draw() == 0 && view_draw(view) == 0, "the list is drawn after NO");
    CHECK(!gkey(MD_KEY_NO, 1) && !gkey(MD_KEY_NO, 0), "the second NO was taken");
    /* YES on MD again: the list again; YES there goes on to the native */
    CHECK(gkey(MD_KEY_YES, 1) == 1 && md_sel.active && md_sel.cursor == 13 && gkey(MD_KEY_YES, 0) == 1, "YES on MD again");
    genc(1, 1); genc(1, 1);
    edits = 0;
    CHECK(!gkey(MD_KEY_YES, 1) && !md_sel.active && md_pend.track == 4 && md_pend.type == 15, "YES in the list was taken");
    CHECK(!gkey(MD_KEY_YES, 0), "the release of the confirming YES was taken");
    CHECK(!edits, "the voice was set while the selector was open");
    /* the native confirms and closes the selector: the MD page sets the voice */
    selecting = 0;
    draw(view);
    CHECK(ntexts == 0, "the page list showed before the voice was set");
    CHECK(idle(view) && edits == 1 && type_value == 15u << 8, "the chosen voice was not set (%u)", type_value >> 8);
    CHECK(!idle(view) && edits == 1, "the held voice was set again with nothing to change");
    /* from another machine: the list, YES; the native confirms while the SYN view still
     * shows the old machine (the YES release goes through it), then MD with its defaults
     * (no voice), then the voice the list chose; never the page list in between */
    view_type = 66; selecting = 1; view_draw(view); view_type = 70;
    gkey(MD_KEY_YES, 1); gkey(MD_KEY_YES, 0); genc(1, -1);
    gkey(MD_KEY_YES, 1);
    selecting = 0; view_type = 66;
    gkey(MD_KEY_YES, 0); key(view, MD_KEY_YES, 0); turn(view, 1); idle(view);
    CHECK(md_pend.track == 4, "the old machine's view dropped the pending voice");
    view_type = 70; type_value = MD_PICK << 8;
    draw(view);
    CHECK(ntexts == 0, "the page list showed over a pending voice");
    CHECK(idle(view) && type_value == 14u << 8, "the voice after the defaults is %u", type_value >> 8);
    /* the native's defaults land after the page showed the voice: set again, no page list */
    type_value = MD_PICK << 8; tick();
    draw(view);
    CHECK(ntexts == 0, "the page list showed when the defaults came late");
    CHECK(idle(view) && type_value == 14u << 8, "the late defaults left %u", type_value >> 8);
    /* after its two seconds the voice is no longer held: no voice shows the page list */
    for (i = 0; i < MD_PEND_TICKS; ++i) tick();
    type_value = MD_PICK << 8; idle(view); draw(view);
    CHECK(type_value == MD_PICK << 8 && drawn("CHOOSE A VOICE"), "the voice was held past two seconds");
    type_value = 14u << 8;
    /* the selector closed under the list (another way out): the list goes */
    selecting = 1; view_draw(view);
    gkey(MD_KEY_YES, 1); gkey(MD_KEY_YES, 0);
    CHECK(md_sel.active, "the list did not open");
    selecting = 0;
    CHECK(!gkey(MD_KEY_DOWN, 1) && !md_sel.active && !genc(1, 1), "the list stayed without the selector");
    /* a pending voice not set within two seconds (the track never showed MD): forgotten,
     * so a Sound loaded later keeps its own voice */
    md_pend.track = 4; md_pend.type = 2; md_pend.until = md_now + MD_PEND_TICKS;
    for (i = 0; i < MD_PEND_TICKS; ++i) tick();
    type_value = 9u << 8;
    idle(view);
    CHECK(!md_pend.track && type_value == 9u << 8, "a stale pending voice was set");
    /* YES with no selector anywhere: the native's */
    CHECK(!gkey(MD_KEY_YES, 1) && !md_sel.active && !gkey(MD_KEY_YES, 0), "YES with no selector");

    /* ---- the voice's name for a second ---- */
    type_value = 15u << 8;
    tick(); boxes = dots = 0;
    idle(view); md_tag.view = 0;                /* arrive afresh */
    CHECK(idle(view), "arriving did not ask for the box");
    draw(view);
    CHECK(boxes == 1 && dots > 20 && ntexts == 0, "the box is not drawn (%d, %d dots)", boxes, dots);
    for (i = 1; i < MD_TAG_TICKS; ++i) {
        tick();
        CHECK(!idle(view), "idle redrew the box at tick %u", i);
    }
    draw(view);
    CHECK(boxes == 2, "the box left early");
    tick();
    CHECK(idle(view), "the box's end asked for no redraw");
    draw(view);
    CHECK(boxes == 2, "the box stayed after a second");
    tick();
    CHECK(!idle(view), "idle redrew after the box");
    tick(); tick(); tick();                     /* another page for three TICKs */
    CHECK(idle(view), "coming back did not start the box");
    draw(view); CHECK(boxes == 3, "the box is not drawn coming back");
    tick(); view_track = 4;
    CHECK(idle(view), "another track did not start the box");
    tick(); type_value = 16u << 8;
    CHECK(idle(view), "another voice did not start the box");
    view_track = 3;
    for (i = 0; i < MD_VOICES; ++i) {
        const char *n;
        for (n = md_voice_names[i]; *n; ++n) CHECK(md_glyph(*n) != 0, "no letter %c (%s)", *n, md_voice_names[i]);
        CHECK(md_tag_width(md_voice_names[i]) <= 62, "%s is %d wide", md_voice_names[i], md_tag_width(md_voice_names[i]));
    }
    {
        const char *n;
        for (n = "CHOOSE A VOICE"; *n; ++n) CHECK(md_glyph(*n) != 0, "no letter %c in the title", *n);
    }

    /* ---- the knobs past a voice's last control: hidden, their encoders taken ---- */
    selecting = 0; md_tag.view = 0;
    for (i = 0; i < MD_VOICES; ++i) {
        type_value = i << 8; spares = 0;
        draw(view);
        CHECK(spares == 8 - (int)md_voice_controls[i], "%s hides %d knobs", md_voice_names[i], spares);
    }
    type_value = 6u << 8;                       /* TRX-CH: five controls */
    edits = 0;
    CHECK(turn(view, 1) == 0, "");
    {
        struct host_event e = {{0}, 1};
        CHECK(md_pick_encoder(view, &e, MD_ENGINE + 4) == 0, "TRX-CH's fifth knob was taken");
        CHECK(md_pick_encoder(view, &e, MD_ENGINE + 5) == 1 && md_pick_encoder(view, &e, MD_ENGINE + 7) == 1,
              "TRX-CH's spare knobs were not taken");
        view_type = 66;
        CHECK(md_pick_encoder(view, &e, MD_ENGINE + 5) == 0, "another machine's knob was taken");
        view_type = 70;
    }

    /* ---- another machine's view, and the voice's popup ---- */
    type_value = MD_PICK << 8; view_type = 66;
    CHECK(!turn(view, 1) && !key(view, MD_KEY_YES, 1), "the list took another machine's input");
    view_type = 70;
    CHECK(!strcmp(md_format(&ui, &md_machinedrum, 8, MD_PICK << 8), "---"), "none chosen is not ---");
    CHECK(!strcmp(md_format(&ui, &md_machinedrum, 8, 13u << 8), "EFM-BD"), "voice 13 is not EFM-BD");
    CHECK(!md_format(&ui, &md_machinedrum, 0, 64u << 8), "a knob is not native");
    if (!bad) printf("PASS main.c: one page, the voice hidden, the list on the page and over the selector "
                     "(YES on MD, cursor, NO back to the selector, YES confirms then sets the voice), the name box\n");
    return bad;
}
