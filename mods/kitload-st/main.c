/* SPDX-License-Identifier: GPL-2.0-or-later
 * kitload-st: a kit bank in a saved project, from SETTINGS > PATTERN >
 * RELOAD FROM PROJ. See README.md.
 *
 * Off until SETTINGS > KIT BANK names a project (P1..P128, P128 offered
 * first). Then the menu gets, after the OS's WHOLE PATTERN / KIT DATA /
 * SEQUENCE DATA rows, a MODE row and 128 rows "A01 NAME" ... "H16 NAME", the
 * kits of that project. With KIT BANK off the page is stock: the rows are
 * built at boot but kept out of the menu's item list until the bank is on
 * (one row, BANK Pn NOT SAVED, while that project is not saved). LOAD replaces the current pattern's kit with a bank
 * kit; SAVE writes the current pattern's kit into a bank position. Both ask
 * for a second YES: the row then reads the question ("SAVE A09 IN P128? YES")
 * until it is answered or cancelled. A long press of SETTINGS opens the page.
 *
 * Events only, no code site of its own:
 *   ST_EV_RELOAD_MENU  appends the rows (core 2.7);
 *   ST_EV_PATTERN_MENU keeps PatternMenuView, which owns the page (core 2.7);
 *   ST_EV_SETTINGS     the KIT BANK row;
 *   ST_EV_KEY          long press of SETTINGS opens the page; any other key
 *                      (the arrows included) cancels a pending confirmation;
 *   ST_EV_ENC          any encoder event cancels it: every move of the
 *                      selection is an arrow key or an encoder event, so no
 *                      movement between two ticks is missed;
 *   ST_EV_TICK         page entry (rereads the names, shows the rows the bank
 *                      state wants) and leaving it (cancels), and a selection
 *                      other than the armed row (cancels).
 * The OS builds the SETTINGS menus once at boot, before the saved-project
 * table exists: construction touches no storage.
 */
#include <syntakt/modules.h>
#include <syntakt/host.h>
#include "kitload.h"
#define DATA __attribute__((section(".data")))
#define LBL 24

char kl_lbl[129][LBL] DATA = {{0}};     /* 1..128: "A01 NAME" */
static char msg_buf[LBL + 16] DATA = {0};
static char lab_buf[LBL + 8] DATA = {0};
static char ask_buf[LBL + 8] DATA = {0};  /* the armed row's question, shown in place of its label */
u32 kl_bank DATA = 0;                   /* 0 off, else the bank project 1..128 */
u32 kl_pick DATA = KL_DEFAULT;          /* the project KIT BANK offers */
u32 kl_valid DATA = 0;                  /* names: 0 to read, 1 read, 2 a read failed (retry on entry) */
u32 kl_mode DATA = 0;                   /* 0 LOAD, 1 SAVE */
u32 kl_armed DATA = 0;                  /* row (1..128) waiting for its second YES */
u32 kl_pm DATA = 0;                     /* PatternMenuView */
u32 kl_rm DATA = 0;                     /* PatternReloadMenuView */
u32 kl_open DATA = 0;                   /* the page was on the view stack at the last tick */
u32 kl_eat DATA = 0;                    /* SETTINGS opened the page: hold it until released */
u32 kl_base DATA = 0;                   /* the menu's own rows (3) */
u32 kl_rows DATA = 0;                   /* our rows in the menu's list now: 0, 1 or 129 */
u32 kl_managed DATA = 0;                /* the 129 rows were found where we added them */
u32 kl_item[129][2] DATA = {{0}};       /* their shared_ptr<MenuItem>s, to put back */
static const struct st_machine_host_api *kl_host DATA = 0;

static const char s_off[] = "KIT BANK OFF: SETTINGS";
static const char s_dash[] = "-";
static const char s_mload[] = "MODE: LOAD KIT";
static const char s_msave[] = "MODE: SAVE KIT";
static const char s_pload[] = "List mode: LOAD";
static const char s_psave[] = "List mode: SAVE";
static const char e_off[] = "Turn on KIT BANK";
static const char e_read[] = "Bank read error";
static const char e_dec[] = "Bank kit invalid";
static const char e_enc[] = "Kit encode failed";
static const char e_prot[] = "Bank is protected";
static const char e_wr[] = "Bank write error";

u32 kl_tmpkit(u32 *out);                /* TMPKIT takes its result's address in a0 */
__asm__(".text\n.globl kl_tmpkit\nkl_tmpkit:\n  move.l 4(%sp),%a0\n  jmp 0x40177318\n");

static void say(const char *s) { MSG(MSGCTX(), s); }

static void cpy(char *d, const char *s, u32 n)
{
    u32 i = 0;
    while (i + 1 < n && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static u32 put_num(char *d, u32 v)      /* decimal, returns its length */
{
    char t[12];
    u32 k = 0, n = 0;
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 11);
    while (k) d[n++] = t[--k];
    d[n] = 0;
    return n;
}

/* pre + "P" + bank + post, into d (LBL + 8 bytes) */
static const char *bank_text(char *d, const char *pre, const char *post)
{
    u32 n;
    cpy(d, pre, LBL);
    for (n = 0; d[n]; n++) ;
    d[n++] = 'P';
    n += put_num(d + n, kl_bank);
    cpy(d + n, post, LBL + 8 - n);
    return d;
}

static void label_for(u32 j, const char *name)          /* "A01 " + kit j's name */
{
    char *d = kl_lbl[j + 1];
    u32 n = 0, k;
    d[n++] = (char)('A' + (j >> 4));
    d[n++] = (char)('0' + ((j & 15u) + 1u) / 10u);
    d[n++] = (char)('0' + ((j & 15u) + 1u) % 10u);
    for (k = 0; k < NAME_MAX && name[k]; k++) {
        if (name[k] < 0x20 || name[k] > 0x7e) break;
        if (k == 0) d[n++] = ' ';
        d[n++] = name[k];
    }
    while (n > 4 && d[n - 1] == ' ') n--;
    d[n] = 0;
}

static int read_name(u32 mgr, u32 slot, u32 j)         /* 512 bytes: the record is 512-aligned */
{
    char name[NAME_MAX + 1];
    const char *p = (const char *)SCRATCH;
    u32 k;
    name[0] = 0;
    if (SREAD(mgr, SCRATCH, slot, (KITS_AT + KIT_STORED * j) >> 9, 512) != 0) {
        name[0] = '?'; name[1] = 0;     /* unknown: shown as "A01 ?", read again on the next entry */
        label_for(j, name);
        return 0;
    }
    if (*(const u32 *)SCRATCH == 11u) {
        for (k = 0; k < NAME_MAX; k++) name[k] = p[NAME_AT + k];
        name[NAME_MAX] = 0;
    }
    label_for(j, name);
    return 1;
}

/* Is the bank on and a saved project in the current format? Reads the 128
 * names once per page entry; a failed read is not cached as done. */
static int bank_ok(void)
{
    u32 mgr, j, ok = 1;
    if (!kl_bank) return 0;
    mgr = MGR();
    if (SSTAT(mgr, kl_bank - 1u) != 13) { kl_valid = 0; return 0; }
    if (!kl_valid) {
        for (j = 0; j < 128u; j++) ok &= (u32)read_name(mgr, kl_bank - 1u, j);
        kl_valid = ok ? 1u : 2u;
    }
    return 1;
}

static u32 live_kit(u32 obj, u32 idx)
{
    u32 base = ((u32 (*)(u32))(*(u32 **)obj)[10])(obj);               /* vfunc +40: the kits */
    return base + KITS_RAM_OFS + KIT_RAM * idx;
}

/* A stored Kit record -> the Kit at kit, whole. The native decoder fills a
 * temporary Kit (the core's DECODE notifications give every module its
 * record); one 0x1618-byte copy through the core's COPY path then replaces
 * the Kit, as the OS copies kits, so modules move their per-kit state (COMP,
 * Playhead, LFO3, Chorus's FX values) with it. The pattern, its trigs and
 * every other sequencer field are outside the Kit and untouched. */
int kl_restore_kit(u32 kit, const void *record)
{
    u32 tmp[2] = {0, 0};
    kl_tmpkit(tmp);
    if (!tmp[0] || KDEC((void *)tmp[0], (void *)record) == 0) {
        if (tmp[1]) SPREL(&tmp[1]);
        return 0;
    }
    MEMCPY((void *)kit, (const void *)tmp[0], KIT_RAM);
    SPREL(&tmp[1]);
    return 1;
}

/* ... the current project's Kit dst, then the engine is told. */
int kl_restore(u32 obj, u32 dst, const void *record)
{
    if (!kl_restore_kit(live_kit(obj, dst), record)) return 0;
    KNOTE((char *)obj + 304, dst, 0);
    return 1;
}

/* Bank kit src -> current project's kit dst. */
static void load(u32 src, u32 dst)
{
    u32 mgr, slot = kl_bank - 1u, obj;
    if (src > 127u || dst > 127u || !kl_bank) return;
    obj = CUROBJ();
    mgr = MGR();
    if (SSTAT(mgr, slot) != 13) { say(bank_text(msg_buf, "Bank ", " not saved")); return; }
    if (SREAD(mgr, SCRATCH, slot, (KIT_STORED * src + KITS_AT) >> 9, KIT_STORED) != 0) { say(e_read); return; }
    if (!kl_restore(obj, dst, SCRATCH)) { say(e_dec); return; }
    cpy(msg_buf, "LOADED ", 8);
    cpy(msg_buf + 7, kl_lbl[src + 1], 16);                              /* popups hold 22 characters */
    say(msg_buf);
}

/* The pattern's name, if it has one (as the PATTERN NAME editor reads it). */
static void pattern_name(u32 obj, u32 idx, char *dst)
{
    u32 no = obj + PAT_AT + PAT_SZ * idx + PAT_NAME, k;
    const char *n = (const char *)((u32 (*)(u32))(*(u32 **)no)[10])(no);
    if (!n || !n[0]) return;
    for (k = 0; k < NAME_FLD && n[k]; k++) dst[k] = n[k];
    for (; k < NAME_FLD; k++) dst[k] = 0;
}

/* Current kit src -> bank position dst: the native encoder, then the slot
 * writer, as SAVE KIT. The whole 0x1600-byte record is written, module
 * records in the encoded tail included. The project in memory and its "kit
 * changed" flags are untouched. */
static void save(u32 src, u32 dst)
{
    u32 mgr, slot = kl_bank - 1u, obj, k;
    char name[NAME_MAX + 1];
    int r;
    if (src > 127u || dst > 127u || !kl_bank) return;
    obj = CUROBJ();
    mgr = MGR();
    if (SSTAT(mgr, slot) != 13) { say(bank_text(msg_buf, "Bank ", " not saved")); return; }
    if (KENC(SAVEBUF, (void *)live_kit(obj, src), 0) == 0) { say(e_enc); return; }
    pattern_name(obj, src, (char *)SAVEBUF + NAME_AT);
    r = SWRITE(mgr, SAVEBUF, slot, (KIT_STORED * dst + KITS_AT) >> 9, KIT_STORED);
    if (r != 0) { say(r == 3 ? e_prot : e_wr); return; }
    for (k = 0; k < NAME_MAX; k++) name[k] = ((const char *)SAVEBUF)[NAME_AT + k];
    name[NAME_MAX] = 0;
    label_for(dst, name);
    cpy(msg_buf, "SAVED ", 7);
    cpy(msg_buf + 6, kl_lbl[dst + 1], 17);
    say(msg_buf);
}

/* ---- the rows' std::function objects: the row index is the functor's data, no heap */
unsigned char kl_mgr(u32 *dest, const u32 *src, u32 op)
{
    switch (op) {
    case 1: dest[0] = (u32)src; break;                  /* the functor's address */
    case 2: dest[0] = src[0]; dest[1] = src[1]; break;  /* copy */
    default: if (op == 0) dest[0] = 0; break;           /* typeinfo (unused), destroy (nothing) */
    }
    return 0;
}

/* label: std::string(MenuItem &), result's address in a0, at every draw */
u32 kl_label_c(void *ret, u32 j)
{
    char dummy;
    const char *s;
    if (!kl_bank)
        s = j == 0 ? s_off : s_dash;
    else if (!bank_ok())
        s = j == 0 ? bank_text(lab_buf, "BANK ", " NOT SAVED") : s_dash;
    else if (j == 0)
        s = kl_mode ? s_msave : s_mload;
    else if (j == kl_armed)                             /* waiting for its second YES */
        s = ask_buf;
    else if (kl_mode) {                                 /* SAVE marks every row */
        cpy(lab_buf, "SAVE ", LBL);
        cpy(lab_buf + 5, kl_lbl[j], LBL + 3);
        s = lab_buf;
    } else
        s = kl_lbl[j];
    STRCTOR(ret, s, &dummy);
    return (u32)ret;
}
__asm__(".text\n.globl kl_label_inv\nkl_label_inv:\n"
        "  move.l 4(%sp),%a1\n"
        "  move.l (%a1),%d1\n"
        "  move.l %d1,-(%sp)\n"
        "  move.l %a0,-(%sp)\n"
        "  jsr kl_label_c\n"
        "  addq.l #8,%sp\n"
        "  rts\n");

/* ---- which rows the menu shows. Menu keeps its rows in a
 * std::vector<shared_ptr<MenuItem>> at +4 (begin), +8 (end), +12 (capacity),
 * 8 bytes each (Menu::addItem, 400fa082); its row count is (end - begin) / 8
 * (vtable +12) and vtable +36 clamps the selection to it. The 129 rows are
 * added once, at boot; hiding them moves end back, showing them writes the
 * same shared_ptrs back below the capacity they were added with. Nothing is
 * freed or allocated, and nothing else adds rows to this menu. */
static u32 rows_wanted(void)
{
    if (!kl_bank) return 0;
    return SSTAT(MGR(), kl_bank - 1u) == 13 ? 129u : 1u;
}

static void set_rows(u32 n)
{
    u32 *v, j;
    if (!kl_managed || n == kl_rows) return;
    v = (u32 *)(kl_rm + 4u);
    if (v[0] + 8u * (kl_base + n) > v[2]) return;
    for (j = 0; j < n; j++) {
        u32 *e = (u32 *)(v[0] + 8u * (kl_base + j));
        e[0] = kl_item[j][0]; e[1] = kl_item[j][1];
    }
    v[1] = v[0] + 8u * (kl_base + n);
    kl_rows = n;
    ((u32 (*)(u32))(*(u32 **)kl_rm)[MENU_SEL])(kl_rm);                    /* clamp the selection */
}

/* YES on a row: row 0 switches the mode; LOAD and SAVE want a second YES
 * on the same row. */
void kl_act_inv(u32 *any, void *item)
{
    u32 j = any[0];
    (void)item;
    if (j > 128u) return;
    if (!kl_bank) { kl_armed = 0; say(e_off); return; }
    if (j == 0) {
        kl_armed = 0;
        if (!bank_ok()) {                       /* BANK Pn NOT SAVED: look again */
            SREFR(MGR(), kl_bank - 1u);
            kl_valid = 0;
            if (!bank_ok()) { say(bank_text(msg_buf, "Bank ", " not saved")); return; }
        } else if (!kl_managed || kl_rows == 129u) {
            kl_mode ^= 1u;
            say(kl_mode ? s_psave : s_pload);
            return;
        }
        say(bank_text(msg_buf, "Bank ", " found"));    /* saved since the page opened */
        set_rows(129u);
        return;
    }
    if (!bank_ok()) { kl_armed = 0; say(bank_text(msg_buf, "Bank ", " not saved")); return; }
    if (kl_armed != j) {
        kl_armed = j;
        cpy(msg_buf, kl_mode ? "SAVE " : "LOAD ", 6);
        msg_buf[5] = kl_lbl[j][0]; msg_buf[6] = kl_lbl[j][1]; msg_buf[7] = kl_lbl[j][2];
        if (kl_mode) bank_text(msg_buf + 8, " IN ", "? YES");     /* "SAVE A09 IN P128? YES" */
        else cpy(msg_buf + 8, "? YES again", 12);
        cpy(ask_buf, msg_buf, LBL + 8);                 /* the row reads it too, until answered */
        say(msg_buf);
        return;
    }
    kl_armed = 0;
    if (!kl_mode) load(j - 1u, CURIDX(CURSRC()));
    else save(CURIDX(CURSRC()), j - 1u);
}

static void add_item(void *menu, u32 j)
{
    extern void kl_label_inv(void);
    fn_t lab = {j, 0, (u32)kl_mgr, (u32)kl_label_inv};
    fn_t act = {j, 0, (u32)kl_mgr, (u32)kl_act_inv};
    fn_t drw = {0, 0, DRAW_MGR, DRAW_INV};
    fn_t emp = {0, 0, 0, 0};
    u32 item;
    drw.d0 = NEW(1);                                    /* the OS's manager frees it */
    item = NEW(0x54);
    ITEMCTOR((void *)item, &lab, &act, &drw, &emp, -1, 8);
    ADDITEM(menu, (void *)item);
    FNDTOR(&emp); FNDTOR(&drw); FNDTOR(&act); FNDTOR(&lab);
}

/* ST_EV_RELOAD_MENU: f(menu), PatternReloadMenuView fully built. No storage access here. */
void kl_reload_menu(void *menu)
{
    u32 j, *v = (u32 *)((u32)menu + 4u);
    kl_rm = (u32)menu;
    kl_mode = 0; kl_armed = 0; kl_valid = 0;
    kl_base = (v[1] - v[0]) / 8u;
    for (j = 0; j <= 128u; j++) add_item(menu, j);
    kl_managed = v[1] - v[0] == 8u * (kl_base + 129u);
    if (!kl_managed) return;                    /* not as expected: leave the rows shown */
    for (j = 0; j <= 128u; j++) {
        const u32 *e = (const u32 *)(v[0] + 8u * (kl_base + j));
        kl_item[j][0] = e[0]; kl_item[j][1] = e[1];
    }
    kl_rows = 129;
    set_rows(0);                                /* the bank is off at boot */
}

/* ST_EV_PATTERN_MENU: f(menu), PatternMenuView; its RELOAD FROM PROJ view is at +472/+476. */
void kl_pattern_menu(void *menu) { kl_pm = (u32)menu; }

/* ---- SETTINGS > KIT BANK: OFF, or the bank project. YES turns it on (with
 * the last project offered, P128 at first) or off; LEFT/RIGHT pick the
 * project and turn it on. Kept until power-off. */
const char *kl_bank_text(void)
{
    return kl_bank ? bank_text(lab_buf, "KIT BANK: ", "") : "KIT BANK: OFF";
}
__asm__(".text\n.globl kl_bank_label\nkl_bank_label:\n"
        "  move.l %a0,-(%sp)\n"
        "  jsr kl_bank_text\n"
        "  move.l (%sp),%a0\n"
        "  pea 3(%sp)\n"                                /* the allocator argument: unused */
        "  move.l %d0,-(%sp)\n"
        "  move.l %a0,-(%sp)\n"
        "  jsr 0x401bf31e\n"                            /* std::string(const char *) */
        "  lea 12(%sp),%sp\n"
        "  move.l (%sp)+,%d0\n"
        "  rts\n");

static void bank_changed(u32 bank)
{
    kl_bank = bank;
    if (bank) kl_pick = bank;
    kl_valid = 0; kl_armed = 0; kl_mode = 0;
    set_rows(rows_wanted());
}
void kl_bank_select(const u32 *fn)
{
    bank_changed(kl_bank ? 0u : kl_pick);
    if (fn && fn[0]) INVALIDATE((char *)fn[0] + 0x38);
}
void kl_bank_change(const u32 *fn, int amount)
{
    int p = (int)(kl_bank ? kl_bank : kl_pick) + amount;
    if (p < 1) p = 1;
    if (p > 128) p = 128;
    bank_changed((u32)p);
    if (fn && fn[0]) INVALIDATE((char *)fn[0] + 0x38);
}
void kl_bank_draw(const u32 *fn, void *item, void *bmp, int x, int y)
{
    (void)fn; (void)item; (void)bmp; (void)x; (void)y;
}
void kl_settings(void *menu)
{
    extern void kl_bank_label(void);
    static const void *row[4] = {(const void *)kl_bank_label, (const void *)kl_bank_select,
                                 (const void *)kl_bank_draw, (const void *)kl_bank_change};
    kl_host->additem(menu, row);
}

/* ---- page state */

/* Is the RELOAD FROM PROJ view on the global view stack? Both entries (the
 * shortcut and SETTINGS > PATTERN) push on that manager. */
static int page_on_stack(void)
{
    u32 mgr = UIROOT() + 64u, sentinel = mgr + VM_LIST, node, n;
    u32 view = kl_pm ? ((const u32 *)kl_pm)[PM_KITVIEW / 4] : 0;
    node = *(const u32 *)sentinel;
    for (n = 0; n < 32u && node && node != sentinel; n++) {
        u32 v = *(const u32 *)(node + NODE_VIEW);
        if (v && (v == kl_rm || v == view)) return 1;
        node = *(const u32 *)node;
    }
    return 0;
}

/* ST_EV_TICK: void f(ctrl). Entering the page rereads the names; leaving it,
 * or a selection other than the armed row (the encoder or the arrows),
 * cancels a pending confirmation. */
void kl_tick(void *ctrl)
{
    u32 open;
    if (!kl_rm) return;
    open = (u32)page_on_stack();
    if (open && !kl_open) {
        kl_valid = 0; kl_armed = 0;
        u32 want = rows_wanted();
        if (kl_rows != want) {
            set_rows(want);
            if (ctrl) *((unsigned char *)ctrl + 0x20) = 1;                /* redraw */
        }
    }
    if (!open) kl_armed = 0;
    else if (kl_armed) {
        u32 row = ((u32 (*)(u32))(*(u32 **)kl_rm)[MENU_SEL])(kl_rm);
        if (row != ROW_MODE + kl_armed) kl_armed = 0;
    }
    kl_open = open;
}

/* Push the page on the GLOBAL view manager, as the main loop's messages do.
 * PatternMenuView's own manager (+100) only exists while that menu is open. */
static void open_page(void)
{
    u32 sp[2];
    const u32 *pm = (const u32 *)kl_pm;
    kl_armed = 0; kl_valid = 0;
    set_rows(rows_wanted());
    sp[0] = pm[PM_KITVIEW / 4];
    SPCOPY(&sp[1], (u32 *)&pm[(PM_KITVIEW + 4) / 4]);
    PUSHVIEW(UIROOT() + 64u, sp, 0);
    SPREL(&sp[1]);
}

/* ST_EV_KEY (global, before the views): int f(brain, event), nonzero consumes.
 * A long press of SETTINGS (flags bit 5) opens the page instead of the LED
 * INTENSITY quick view; SETTINGS is then held until an event with none of
 * bits 0 (pressed), 3 (repeat), 5 (long): its release, so no later SETTINGS
 * event of that press reaches the views. Any other key press cancels a
 * pending confirmation. */
int kl_key(void *brain, const unsigned char *ev)
{
    u32 code, fl;
    (void)brain;
    if (*(const u32 *)ev != KEYEV_VT) return 0;
    code = *(const u32 *)(ev + 12);
    fl = *(const u32 *)(ev + 16);
    if (code != KEY_SETTINGS) {
        if (code != KEY_YES && (fl & EV_DOWN)) kl_armed = 0;
        return 0;
    }
    if (kl_eat) {
        if (!(fl & (EV_DOWN | EV_REPT | EV_LONG))) kl_eat = 0;   /* the release: held too */
        return 1;
    }
    if ((fl & EV_LONG) && kl_pm) {
        kl_eat = 1;
        open_page();
        return 1;
    }
    return 0;
}

/* ST_EV_ENC (global, before the views): int f(brain, event). Any encoder
 * event cancels a pending confirmation; never consumed. */
int kl_enc(void *brain, const void *ev)
{
    (void)brain; (void)ev;
    kl_armed = 0;
    return 0;
}

static int initialize(const struct st_smz_api *api)
{
    if (!api || !api->ui || !api->ui->host || api->ui->host->version < 18) return 1;   /* core 2.7 */
    kl_host = api->ui->host;
    return 0;
}
static const struct st_smz_subscription subscriptions[] = {
    {ST_EV_RELOAD_MENU, (void (*)(void))kl_reload_menu},
    {ST_EV_PATTERN_MENU, (void (*)(void))kl_pattern_menu},
    {ST_EV_SETTINGS, (void (*)(void))kl_settings},
    {ST_EV_KEY, (void (*)(void))kl_key},
    {ST_EV_ENC, (void (*)(void))kl_enc},
    {ST_EV_TICK, (void (*)(void))kl_tick}};
const struct st_smz_module kitload_module = {"KITLOAD", initialize, subscriptions, 6};
