/* SPDX-License-Identifier: GPL-2.0-or-later
 * kitload-st: a kit bank in a saved project, from SETTINGS > PATTERN >
 * RELOAD FROM PROJ. See README.md.
 *
 * After the OS's WHOLE PATTERN / KIT DATA / SEQUENCE DATA rows the menu gets
 * a MODE row and 128 rows "A01 NAME" ... "H16 NAME", the kits of project
 * KL_BANK. LOAD copies a bank kit's sounds into the current pattern's kit;
 * SAVE writes the current pattern's kit into a bank position. Both ask for a
 * second YES. A long press of SETTINGS opens the page from anywhere.
 *
 * Events only, no code site of its own:
 *   ST_EV_RELOAD_MENU  appends the rows (core 2.7);
 *   ST_EV_PATTERN_MENU keeps PatternMenuView, which owns the page (core 2.7);
 *   ST_EV_KEY          long press of SETTINGS opens the page; another key
 *                      cancels a pending confirmation.
 * The OS builds the SETTINGS menus once at boot, before the saved-project
 * table exists: construction touches no storage; the bank is read when a
 * row is drawn or chosen.
 */
#include <syntakt/modules.h>
#include <syntakt/host.h>
#include "kitload.h"
#define STR2(x) #x
#define STR(x) STR2(x)
#define DATA __attribute__((section(".data")))
#define LBL 24

char kl_lbl[129][LBL] DATA = {{0}};     /* 0: status row; 1..128: "A01 NAME" */
static char msg_buf[LBL + 8] DATA = {0};
static char lab_buf[LBL + 8] DATA = {0};
u32 kl_valid DATA = 0;                  /* kl_lbl holds the bank's names */
u32 kl_mode DATA = 0;                   /* 0 LOAD, 1 SAVE */
u32 kl_armed DATA = 0;                  /* row (1..128) waiting for its second YES */
u32 kl_pm DATA = 0;                     /* PatternMenuView */
u32 kl_eat DATA = 0;                    /* SETTINGS opened the page: hold it until released */

static const char s_stat[] = "STATUS ";
static const char s_ok[] = " (13=OK)";
static const char s_dev[] = "DEVICE STATE ";
static const char s_dok[] = " (2=OK)";
static const char s_none[] = "BANK P" STR(KL_BANK) " NOT SAVED";
static const char s_found[] = "Bank P" STR(KL_BANK) " found";
static const char s_dash[] = "-";
static const char s_mload[] = "MODE: LOAD KIT";
static const char s_msave[] = "MODE: SAVE KIT";
static const char s_pload[] = "List mode: LOAD";
static const char s_psave[] = "List mode: SAVE";
static const char s_pre[] = "SAVE ";
static const char e_slot[] = "Bank P" STR(KL_BANK) " not saved";
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

static void num_label(char *d, const char *pre, int v, const char *post)
{
    char t[12];
    u32 n = 0, k = 0, u;
    cpy(d, pre, LBL);
    while (d[n]) n++;
    if (v < 0) { d[n++] = '-'; u = (u32)(-v); } else u = (u32)v;
    do { t[k++] = (char)('0' + u % 10u); u /= 10u; } while (u && k < 11);
    while (k) d[n++] = t[--k];
    cpy(d + n, post, LBL - n);
}

static void read_name(u32 mgr, u32 slot, u32 j)        /* 512 bytes: the record is 512-aligned */
{
    char name[NAME_MAX + 1];
    const char *p = (const char *)SCRATCH;
    u32 k;
    name[0] = 0;
    if (SREAD(mgr, SCRATCH, slot, (KITS_AT + KIT_STORED * j) >> 9, 512) == 0 && *(const u32 *)SCRATCH == 11u) {
        for (k = 0; k < NAME_MAX; k++) name[k] = p[NAME_AT + k];
        name[NAME_MAX] = 0;
    }
    label_for(j, name);
}

/* Is the bank a saved project in the current format? Reads the 128 names once. */
static int bank_ok(void)
{
    u32 mgr = MGR(), j;
    if (SSTAT(mgr, KL_BANK - 1u) != 13) { kl_valid = 0; return 0; }
    if (!kl_valid) {
        for (j = 0; j < 128u; j++) read_name(mgr, KL_BANK - 1u, j);
        kl_valid = 1;
    }
    return 1;
}

/* Bank kit src -> current project's kit dst. Decoded through the native
 * decoder into a temporary Kit; only the sounds move, through the core's COPY
 * path, as the stock track-sound reload does. */
static void load(u32 src, u32 dst)
{
    u32 mgr, slot = KL_BANK - 1u, base, obj, t, tmp[2] = {0, 0};
    void *kit;
    if (src > 127u || dst > 127u) return;
    obj = CUROBJ();
    mgr = MGR();
    if (SSTAT(mgr, slot) != 13) { say(e_slot); return; }
    if (SREAD(mgr, SCRATCH, slot, (KIT_STORED * src + KITS_AT) >> 9, KIT_STORED) != 0) { say(e_read); return; }
    base = ((u32 (*)(u32))(*(u32 **)obj)[10])(obj);                     /* vfunc +40: the kits */
    kit = (void *)(base + KITS_RAM_OFS + KIT_RAM * dst);
    kl_tmpkit(tmp);
    if (!tmp[0] || KDEC((void *)tmp[0], SCRATCH) == 0) {
        if (tmp[1]) SPREL(&tmp[1]);
        say(e_dec);
        return;
    }
    for (t = 0; t < SND_N; t++)
        MEMCPY((char *)kit + SND_AT + SND_LEN * t, (const char *)tmp[0] + SND_AT + SND_LEN * t, SND_LEN);
    MEMCPY((char *)kit + FXS_AT, (const char *)tmp[0] + FXS_AT, FXS_LEN);
    SPREL(&tmp[1]);
    KNOTE((char *)obj + 304, dst, 0);
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
 * writer, as SAVE KIT. The whole 0x1600-byte record is written, so module
 * records in the encoded tail travel with it. The project in memory and its
 * "kit changed" flags are untouched. */
static void save(u32 src, u32 dst)
{
    u32 mgr, slot = KL_BANK - 1u, base, obj, k;
    char name[NAME_MAX + 1];
    void *kit;
    int r;
    if (src > 127u || dst > 127u) return;
    obj = CUROBJ();
    mgr = MGR();
    if (SSTAT(mgr, slot) != 13) { say(e_slot); return; }
    base = ((u32 (*)(u32))(*(u32 **)obj)[10])(obj);
    kit = (void *)(base + KITS_RAM_OFS + KIT_RAM * src);
    if (KENC(SAVEBUF, kit, 0) == 0) { say(e_enc); return; }
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
    u32 ok = (u32)bank_ok();
    if (j == 0)
        s = !ok ? s_none : (kl_mode ? s_msave : s_mload);
    else if (!ok) {                                     /* no bank: status codes on rows 1 and 2 */
        if (j == 1) { num_label(lab_buf, s_stat, SSTAT(MGR(), KL_BANK - 1u), s_ok); s = lab_buf; }
        else if (j == 2) { num_label(lab_buf, s_dev, (int)SDEV(MGR()), s_dok); s = lab_buf; }
        else s = s_dash;
    } else if (kl_mode) {                               /* SAVE marks every row */
        cpy(lab_buf, s_pre, LBL);
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

/* YES on a row: row 0 switches the mode (or retries a missing bank); LOAD and
 * SAVE want a second YES on the same row. */
void kl_act_inv(u32 *any, void *item)
{
    u32 j = any[0];
    (void)item;
    if (j > 128u) return;
    if (j == 0) {
        kl_armed = 0;
        if (!bank_ok()) {
            SREFR(MGR(), KL_BANK - 1u);
            say(bank_ok() ? s_found : e_slot);
            return;
        }
        kl_mode ^= 1u;
        kl_valid = 0;                                   /* reread the names */
        bank_ok();
        say(kl_mode ? s_psave : s_pload);
        return;
    }
    if (!bank_ok()) { kl_armed = 0; say(e_slot); return; }
    if (kl_armed != j) {
        kl_armed = j;
        if (kl_mode) {                                  /* "SAVE A01 IN P128?" names the project written */
            cpy(msg_buf, "SAVE ", 6);
            msg_buf[5] = kl_lbl[j][0]; msg_buf[6] = kl_lbl[j][1]; msg_buf[7] = kl_lbl[j][2];
            cpy(msg_buf + 8, " IN P" STR(KL_BANK) "? YES", 14);
        } else {
            cpy(msg_buf, "LOAD ", 6);
            msg_buf[5] = kl_lbl[j][0]; msg_buf[6] = kl_lbl[j][1]; msg_buf[7] = kl_lbl[j][2];
            cpy(msg_buf + 8, "? YES again", 12);
        }
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
    u32 j;
    kl_mode = 0; kl_armed = 0; kl_valid = 0;
    for (j = 0; j <= 128u; j++) add_item(menu, j);
}

/* ST_EV_PATTERN_MENU: f(menu), PatternMenuView; its RELOAD FROM PROJ view is at +472/+476. */
void kl_pattern_menu(void *menu) { kl_pm = (u32)menu; }

/* Push the page on the GLOBAL view manager, as the main loop's messages do.
 * PatternMenuView's own manager (+100) only exists while that menu is open. */
static void open_page(void)
{
    u32 sp[2];
    const u32 *pm = (const u32 *)kl_pm;
    kl_armed = 0;
    sp[0] = pm[PM_KITVIEW / 4];
    SPCOPY(&sp[1], (u32 *)&pm[(PM_KITVIEW + 4) / 4]);
    PUSHVIEW(UIROOT() + 64u, sp, 0);
    SPREL(&sp[1]);
}

/* ST_EV_KEY (global, before the views): int f(brain, event), nonzero consumes.
 * A long press of SETTINGS (flags bit 5) opens the page instead of the LED
 * INTENSITY quick view; SETTINGS is then held until an event with none of
 * bits 0 (pressed), 3 (repeat), 5 (long): its release, so no later SETTINGS
 * event of that press reaches the views. Any other key
 * press cancels a pending confirmation. */
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

static int initialize(const struct st_smz_api *api)
{
    if (!api || !api->ui || !api->ui->host || api->ui->host->version < 18) return 1;   /* core 2.7 */
    return 0;
}
static const struct st_smz_subscription subscriptions[] = {
    {ST_EV_RELOAD_MENU, (void (*)(void))kl_reload_menu},
    {ST_EV_PATTERN_MENU, (void (*)(void))kl_pattern_menu},
    {ST_EV_KEY, (void (*)(void))kl_key}};
const struct st_smz_module kitload_module = {"KITLOAD", initialize, subscriptions, 3};
