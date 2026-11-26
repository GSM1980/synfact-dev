/* SPDX-License-Identifier: GPL-2.0-or-later
 * kitload-st: a kit bank in a saved project, from SETTINGS > PATTERN >
 * RELOAD FROM PROJ. See README.md.
 *
 * Off until SETTINGS > KIT BANK names a project (P1..P128, P128 offered
 * first), or a saved project is named KITBANK (or KIT BANK): until KIT BANK is
 * changed by hand, that project is the bank, so the choice survives power-off
 * with nothing written. The name is read from the project manager's slot
 * cache; the first lookup after power-on refreshes (SREFR) the entries that
 * are not valid, once, and the OS refreshes an entry after each save. Then the menu gets, after the OS's WHOLE PATTERN / KIT DATA /
 * SEQUENCE DATA rows, a MODE row and 128 rows "A01 NAME" ... "H16 NAME", the
 * kits of that project. With KIT BANK off the page is stock: the rows are
 * built at boot but kept out of the menu's item list until the bank is on
 * (one row while that slot holds no loadable project: CREATE KITBANK IN Pn
 * for an empty slot, which makes the bank with PROJECT's own copy, paste and
 * rename, or BANK Pn FORMAT n). LOAD replaces the current pattern's kit with a bank
 * kit at once, on YES; SAVE writes the current pattern's kit into a bank
 * position under a name typed in the OS's own naming box (the one PROJECT
 * NAME opens): its YES saves, NO closes it. Creating the bank asks in the
 * OS's confirmation box (the one CLEAR TRACK SOUND? uses). A long press of
 * SETTINGS opens the page.
 *
 * Events only, no code site of its own:
 *   ST_EV_RELOAD_MENU  appends the rows (core 2.7);
 *   ST_EV_PATTERN_MENU keeps PatternMenuView, which owns the page (core 2.7);
 *   ST_EV_SETTINGS     the KIT BANK row;
 *   ST_EV_KEY          long press of SETTINGS opens the page;
 *   ST_EV_TICK         page entry (rereads the names, shows the rows the bank
 *                      state wants) and a redraw after a label changes.
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
u32 kl_bank DATA = 0;                   /* 0 off, else the bank project 1..128 */
u32 kl_pick DATA = KL_DEFAULT;          /* the project KIT BANK offers */
u32 kl_scanned DATA = 0;                /* the slot cache was completed once since power-on */
u32 kl_user DATA = 0;                   /* persisted manual KIT BANK choice: no naming */
u32 kl_valid DATA = 0;                  /* names: 0 to read, 1 read, 2 a read failed (retry on entry) */
u32 kl_mode DATA = 0;                   /* 0 LOAD, 1 SAVE */
u32 kl_pm DATA = 0;                     /* PatternMenuView */
u32 kl_rm DATA = 0;                     /* PatternReloadMenuView */
u32 kl_open DATA = 0;                   /* the page was on the view stack at the last tick */
u32 kl_eat DATA = 0;                    /* SETTINGS opened the page: hold it until released */
u32 kl_base DATA = 0;                   /* the menu's own rows (3) */
u32 kl_rows DATA = 0;                   /* our rows in the menu's list now: 0, 1 or 129 */
u32 kl_dirty DATA = 0;                  /* a label changed: redraw at the next tick */
u32 kl_managed DATA = 0;                /* the 129 rows were found where we added them */
u32 kl_item[129][2] DATA = {{0}};       /* their shared_ptr<MenuItem>s, to put back */
u32 kl_busy DATA = 0;                   /* creating the bank */
char kl_name[NAME_FLD + 1] DATA = {0};  /* the name typed for the next SAVE */
u32 kl_named DATA = 0;                  /* kl_name is set */
u32 kl_from DATA = 0, kl_to DATA = 0;   /* its source and destination slots, 0..127 */
static const struct st_machine_host_api *kl_host DATA = 0;
static void bank_preferences(u32 write)
{
    u32 v[3]={kl_user,kl_bank,kl_pick};
    if(!kl_host || kl_host->version<20 || !kl_host->persistence)return;
    if(kl_host->persistence->preferences(ST_PREF_KIT_BANK,v,3,write)!=1 || write)return;
    if(v[0]>1 || v[1]>128 || v[2]<1 || v[2]>128)return;
    kl_user=v[0];kl_bank=v[1];kl_pick=v[2];
}

static const char s_off[] = "KIT BANK OFF: SETTINGS";
static const char s_dash[] = "-";
static const char s_confirm[] = "CONFIRM Y/N";   /* the OS's own second line (4024f15d) */
static const char e_box[] = "No confirm box";
static const char s_creating[] = "CREATING KITBANK...";
static const char s_kitbank[] = "KITBANK";
static const char s_jobname[] = "Kit Bank Create";
static const char e_unsaved[] = "SAVE THIS PROJECT FIRST";
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

/* A message in the OS's text box, drawn over the menu (the message popup,
 * 400c347c, is drawn behind it). */
static void say(const char *m)
{
    u32 str[1], obj, sp[2];
    char alloc;
    obj = NEW(TB_SIZE);
    if (!obj) return;
    STRCTOR(str, m, &alloc);
    TB_CTOR(obj, str);
    TB_SP(sp, obj);
    PUSHVIEW(VIEWMGR(UIROOT()), sp, 0);
    SPREL(&sp[1]);
    STRDTOR(str);
}

/* The menu does not repaint after a row's action: the OS's message popup is
 * drawn behind it, and a changed label shows only once something repaints.
 * Invalidate the view (as the KIT BANK row does) and ask the tick for one. */
static void redraw(void)
{
    if (kl_rm) INVALIDATE((char *)kl_rm + 0x38);
    kl_dirty = 1;
}


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

/* pre + "P" + project + post, into d (LBL + 8 bytes) */
static const char *proj_text(char *d, const char *pre, u32 project, const char *post)
{
    u32 n;
    cpy(d, pre, LBL);
    for (n = 0; d[n]; n++) ;
    d[n++] = 'P';
    n += put_num(d + n, project);
    cpy(d + n, post, LBL + 8 - n);
    return d;
}

static const char *bank_text(char *d, const char *pre, const char *post)
{
    return proj_text(d, pre, kl_bank, post);
}

/* Why the bank is not usable: "BANK P128 EMPTY" (no saved project there) or
 * "BANK P128 FORMAT 12" (a header whose status is not 13, the only one the OS
 * itself loads from: 40019db4, 4001e452). */
static const char *bank_why(char *d, int upper)
{
    int st = SSTAT(MGR(), kl_bank - 1u);
    u32 n;
    if (st < 0) return bank_text(d, upper ? "BANK " : "Bank ", upper ? " EMPTY" : " empty");
    bank_text(d, upper ? "BANK " : "Bank ", upper ? " FORMAT " : " format ");
    for (n = 0; d[n]; n++) ;
    put_num(d + n, (u32)st);
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

/* The lowest saved project named KITBANK or KIT BANK (any case, trailing
 * spaces ignored), 1..128, or 0. */
static int named(const char *n)
{
    static const char a[] = "KITBANK", b[] = "KIT BANK";
    char t[17];
    u32 k, len;
    for (k = 0; k < 16u && n[k]; k++) t[k] = (char)(n[k] >= 'a' && n[k] <= 'z' ? n[k] - 32 : n[k]);
    while (k && t[k - 1] == ' ') k--;
    t[k] = 0;
    len = k;
    for (k = 0; k <= len; k++) if (t[k] != a[k]) break;
    if (k > len) return 1;
    for (k = 0; k <= len; k++) if (t[k] != b[k]) break;
    return k > len;
}

u32 kl_named_bank(void)
{
    u32 mgr = MGR(), s;
    if (!kl_scanned && SDEV(mgr) == 2u) {     /* a slot not read yet looks like an empty one */
        kl_scanned = 1;
        for (s = 0; s < 128u; s++)
            if (!*(const unsigned char *)(mgr + SLOT_CACHE + SLOT_STRIDE * s)) SREFR(mgr, s);
    }
    for (s = 0; s < 128u; s++) {
        const unsigned char *e = (const unsigned char *)(mgr + SLOT_CACHE + SLOT_STRIDE * s);
        if (e[0] && *(const u32 *)(e + SLOT_STATUS) == 13u && named((const char *)e + SLOT_NAME)) return s + 1u;
    }
    return 0;
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
    if (SSTAT(mgr, slot) != 13) { say(bank_why(msg_buf, 0)); return; }
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
    if (SSTAT(mgr, slot) != 13) { say(bank_why(msg_buf, 0)); return; }
    if (KENC(SAVEBUF, (void *)live_kit(obj, src), 0) == 0) { kl_named = 0; say(e_enc); return; }
    if (kl_named) {                                     /* the name typed in the naming box */
        for (k = 0; k < NAME_FLD; k++) ((char *)SAVEBUF)[NAME_AT + k] = kl_name[k];
        kl_named = 0;
    } else
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
        s = j != 0 ? s_dash
          : kl_busy ? s_creating
          : SSTAT(MGR(), kl_bank - 1u) < 0 ? bank_text(lab_buf, "CREATE KITBANK IN ", "")
          : bank_why(lab_buf, 1);
    else if (j == 0)
        s = kl_mode ? s_msave : s_mload;
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

/* Create the bank: PROJECT's own COPY of the current project's saved slot,
 * PASTE into the empty bank slot, and RENAME to KITBANK, as one job on the
 * OS's job queue. The current project stays current and is not touched; the
 * project clipboard is replaced, as COPY PROJECT replaces it. Only into an
 * empty slot, and only from a saved project. */
u32 kl_create_work(const u32 *any)
{
    u32 st = PROJS(), h;
    (void)any;
    if (!PCOPY(st, kl_from) || !PPASTE(st, kl_to, 0)) return 0;
    h = PHEAD(st, kl_to);
    if (h) {
        fn_t none = {0, 0, 0, 0};
        char *n = (char *)(h + 8u);
        u32 k;
        for (k = 0; k < 16u; k++) n[k] = k < 7u ? s_kitbank[k] : 0;
        PHEADW(st, kl_to, &none);
        FNDTOR(&none);
    }
    return 1;
}

void kl_create_done(const u32 *any, u32 ok)
{
    (void)any;
    kl_busy = 0;
    SREFR(MGR(), kl_to);
    kl_valid = 0;
    if (kl_bank == kl_to + 1u) set_rows(rows_wanted());
    redraw();
    say(proj_text(msg_buf, (ok & 0xffu) ? "KITBANK CREATED IN " : "KITBANK NOT CREATED IN ", kl_to + 1u, ""));
}

static void create(void)
{
    u32 cur = CURSLOT(CUROBJ()), mgr = MGR();
    fn_t work = {0, 0, (u32)kl_mgr, (u32)kl_create_work};
    fn_t done = {0, 0, (u32)kl_mgr, (u32)kl_create_done};
    if (kl_busy || !kl_bank) return;
    if (SSTAT(mgr, kl_bank - 1u) >= 0) { say(bank_why(msg_buf, 0)); return; }   /* never over a project */
    if (cur > 127u || SSTAT(mgr, cur) != 13) { say(e_unsaved); return; }
    kl_from = cur; kl_to = kl_bank - 1u; kl_busy = 1;
    redraw();
    JOBPOST(JOBQ(), s_jobname, &work, &done);
}

/* SAVE: the OS's naming box, "SAVE TO A09", offering the current pattern's
 * name, or the slot's own when the pattern has none. Its YES saves under the
 * typed name; NO closes it. 0 if it could not be opened. */
void kl_name_inv(const u32 *any, const u32 *str, u32 unused);
static int name_box(u32 j)
{
    u32 q[1], nm[1], *cb, sp[2], k;
    char alloc, start[NAME_FLD + 1];
    fn_t fn = {j, 0, (u32)kl_mgr, (u32)kl_name_inv};
    cpy(msg_buf, "SAVE TO ", 9);
    msg_buf[8] = kl_lbl[j][0]; msg_buf[9] = kl_lbl[j][1]; msg_buf[10] = kl_lbl[j][2]; msg_buf[11] = 0;
    for (k = 0; k <= NAME_FLD; k++) start[k] = 0;
    pattern_name(CUROBJ(), CURIDX(CURSRC()), start);
    if (!start[0] && kl_lbl[j][4] && kl_lbl[j][4] != '?' && kl_lbl[j][4] != '-')
        cpy(start, kl_lbl[j] + 4, NAME_FLD + 1);
    start[NAME_FLD] = 0;
    cb = (u32 *)NEW(NB_SIZE);
    if (!cb) return 0;
    cb[0] = NB_SP_VT; cb[1] = 1; cb[2] = 1; cb[3] = (u32)cb + 16u;
    STRCTOR(q, msg_buf, &alloc);
    STRCTOR(nm, start, &alloc);
    NB_CTOR((u32)cb + 16u, q, nm, 0, NB_LEN, 0, &fn);
    sp[0] = (u32)cb + 16u; sp[1] = (u32)cb;
    PUSHVIEW(UIROOT() + 64u, sp, 0);                    /* the manager keeps its own reference */
    SPREL(&sp[1]);
    STRDTOR(nm); STRDTOR(q);
    return 1;
}

/* The naming box's answer: invoker(any, std::string &name, int), any = {row,
 * 0}. Called on YES only, after the box trimmed trailing spaces. */
void kl_name_inv(const u32 *any, const u32 *str, u32 unused)
{
    u32 j = any[0], k;
    const char *n = str ? (const char *)str[0] : 0;
    (void)unused;
    if (j < 1u || j > 128u || !kl_bank || !bank_ok()) return;
    for (k = 0; k < NAME_FLD; k++) kl_name[k] = 0;
    for (k = 0; n && k < NAME_FLD && n[k]; k++) kl_name[k] = n[k];
    if (!kl_name[0]) cpy(kl_name, "KIT", 4);            /* never an empty name */
    kl_named = 1;
    save(CURIDX(CURSRC()), j - 1u);
    redraw();
}

void kl_confirm_inv(const u32 *any, u32 yes);
static int confirm(u32 j, u32 mode)
{
    u32 q[1], sub[1], box, *cb, sp[2];
    char alloc;
    fn_t fn = {j, mode, (u32)kl_mgr, (u32)kl_confirm_inv};
    bank_text(msg_buf, "CREATE KITBANK ", "?");        /* "CREATE KITBANK P128?" */
    box = NEW(CW_SIZE);
    if (!box) return 0;
    STRCTOR(q, msg_buf, &alloc);
    STRCTOR(sub, s_confirm, &alloc);
    CW_CTOR(box, q, sub, &fn, 0);
    cb = (u32 *)NEW(16u);
    cb[0] = CW_SP_VT; cb[1] = 1; cb[2] = 1; cb[3] = box;
    sp[0] = box; sp[1] = (u32)cb;
    PUSHVIEW(UIROOT() + 64u, sp, 0);                    /* the manager keeps its own reference */
    SPREL(&sp[1]);
    STRDTOR(sub); STRDTOR(q);                           /* after the push, as 400653ae does */
    return 1;
}

/* YES on a row: row 0 switches the mode; LOAD and SAVE want a second YES
 * on the same row. */
void kl_act_inv(u32 *any, void *item)
{
    u32 j = any[0];
    (void)item;
    if (j > 128u) return;
    if (!kl_bank) { say(e_off); return; }
    if (j == 0) {
        if (!bank_ok()) {                       /* BANK Pn EMPTY/FORMAT: look again */
            SREFR(MGR(), kl_bank - 1u);
            kl_valid = 0;
            if (!bank_ok()) {
                if (kl_busy) return;
                if (SSTAT(MGR(), kl_bank - 1u) < 0) {   /* empty: offer to create it */
                    if (!confirm(0, 2)) say(e_box);
                    return;
                }
                say(bank_why(msg_buf, 0));
                return;
            }
        } else if (!kl_managed || kl_rows == 129u) {
            kl_mode ^= 1u;
            say(kl_mode ? s_psave : s_pload);
            redraw();
            return;
        }
        say(bank_text(msg_buf, "Bank ", " found"));    /* saved since the page opened */
        set_rows(129u);
        redraw();
        return;
    }
    if (!bank_ok()) { say(bank_why(msg_buf, 0)); return; }
    if (!kl_mode) {                                     /* LOAD: at once, no confirmation */
        load(j - 1u, CURIDX(CURSRC()));
        redraw();
        return;
    }
    if (!name_box(j)) say(e_box);
}

/* The box's answer: invoker(any, answer), any = {row, mode} as confirm() set
 * it. Runs after the box has closed itself. */
void kl_confirm_inv(const u32 *any, u32 yes)
{
    if (!(yes & 0xffu) || !kl_bank) return;
    if (any[1] == 2u) create();                         /* the only question asked in this box */
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
    kl_mode = 0; kl_valid = 0;
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
 * project and turn it on. Persisted through the core preferences service. */
static void auto_bank(void);
const char *kl_bank_text(void)
{
    auto_bank();
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
    kl_valid = 0; kl_mode = 0;
    set_rows(rows_wanted());
}

/* Until KIT BANK is changed by hand, the bank is the project named KITBANK. */
static void auto_bank(void)
{
    u32 b;
    if (kl_user) return;
    b = kl_named_bank();
    if (b != kl_bank) bank_changed(b);
}
void kl_bank_select(const u32 *fn)
{
    kl_user = 1;
    bank_changed(kl_bank ? 0u : kl_pick);
    bank_preferences(1);
    if (fn && fn[0]) INVALIDATE((char *)fn[0] + 0x38);
}
/* The OS calls change(storage, item, amount), amount -1 for LEFT and +1 for RIGHT (the
 * encoder: its accelerated step, signed): the amount is the third argument. */
void kl_bank_change(const u32 *fn, void *item, int amount)
{
    int p;
    kl_user = 1;
    p = (int)(kl_bank ? kl_bank : kl_pick) + amount;
    if (p < 1) p = 1;
    if (p > 128) p = 128;
    bank_changed((u32)p);
    bank_preferences(1);
    if (fn && fn[0]) INVALIDATE((char *)fn[0] + 0x38);
}
/* The KIT BANK row's icon, as Sample Manager draws its own (its third
 * callback, at the label's x/y): four pads, the first filled, 12x11, in the
 * OS's column format (bit 31 the top row), a non-owning bitmap descriptor
 * (400f72be) blitted by 400f8f70. */
static const u32 kl_icon[12] = {0xfbe00000u, 0xfa200000u, 0xfa200000u, 0xfa200000u, 0xfbe00000u, 0,
                                0xfbe00000u, 0x8a200000u, 0x8a200000u, 0x8a200000u, 0xfbe00000u, 0};
static const u32 kl_icon_mask[12] = {0xffe00000u, 0xffe00000u, 0xffe00000u, 0xffe00000u, 0xffe00000u, 0xffe00000u,
                                     0xffe00000u, 0xffe00000u, 0xffe00000u, 0xffe00000u, 0xffe00000u, 0xffe00000u};
void kl_bank_draw(const u32 *fn, void *item, void *ctx, int x, int y)
{
    u32 bitmap[8];
    (void)fn; (void)item;
    ((void (*)(void *, u32, u32, const void *, const void *))0x400f72beu)(bitmap, 12, 11, kl_icon, kl_icon_mask);
    ((void (*)(void *, void *, int, int, u32))0x400f8f70u)(ctx, bitmap, x, y, 0);
    ((void (*)(void *))0x400f71f0u)(bitmap);
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

/* ST_EV_TICK: void f(ctrl). Entering the page rereads the names and shows
 * the rows the bank state wants; a changed label asks for one redraw. */
void kl_tick(void *ctrl)
{
    u32 open;
    if (!kl_rm) return;
    open = (u32)page_on_stack();
    if (open && !kl_open) {
        u32 want;
        kl_valid = 0;
        auto_bank();
        want = rows_wanted();
        if (kl_rows != want) {
            set_rows(want);
            if (ctrl) *((unsigned char *)ctrl + 0x20) = 1;                /* redraw */
        }
    }
    kl_open = open;
    if (kl_dirty) {
        kl_dirty = 0;
        if (open && ctrl) *((unsigned char *)ctrl + 0x20) = 1;          /* redraw */
    }
}

/* Push the page on the GLOBAL view manager, as the main loop's messages do.
 * PatternMenuView's own manager (+100) only exists while that menu is open. */
static void open_page(void)
{
    u32 sp[2];
    const u32 *pm = (const u32 *)kl_pm;
    kl_valid = 0;
    auto_bank();
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
 * event of that press reaches the views. */
int kl_key(void *brain, const unsigned char *ev)
{
    u32 code, fl;
    (void)brain;
    if (*(const u32 *)ev != KEYEV_VT) return 0;
    code = *(const u32 *)(ev + 12);
    fl = *(const u32 *)(ev + 16);
    if (code != KEY_SETTINGS) return 0;
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
    kl_host = api->ui->host;
    bank_preferences(0);
    return 0;
}
static const struct st_smz_subscription subscriptions[] = {
    {ST_EV_RELOAD_MENU, (void (*)(void))kl_reload_menu},
    {ST_EV_PATTERN_MENU, (void (*)(void))kl_pattern_menu},
    {ST_EV_SETTINGS, (void (*)(void))kl_settings},
    {ST_EV_KEY, (void (*)(void))kl_key},
    {ST_EV_TICK, (void (*)(void))kl_tick}};
const struct st_smz_module kitload_module = {"KITLOAD", initialize, subscriptions, 5};
