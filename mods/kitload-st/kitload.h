/* SPDX-License-Identifier: GPL-2.0-or-later
 * kitload-st: OS 1.42 routines, buffers and layouts. Called, never patched.
 * Pointers and integers come back in d0 (the OS's ABI), hence u32 returns. */
#ifndef KITLOAD_H
#define KITLOAD_H
typedef unsigned int u32;
typedef struct { u32 d0, d1, mgr, inv; } fn_t;                         /* std::function, 16 bytes */

#ifndef KL_DEFAULT
#define KL_DEFAULT 128                  /* the project offered when the bank is turned on (1..128) */
#endif

/* menu items, as PatternReloadMenuView's constructor (40181f40) builds its own */
#define NEW      ((u32 (*)(u32))0x4010394cu)                           /* operator new */
#define STRCTOR  ((u32 (*)(void *,const char *,void *))0x401bf31eu)    /* std::string(const char *) */
#define ITEMCTOR ((void (*)(void *,fn_t *,fn_t *,fn_t *,fn_t *,int,int))0x400fa84cu)
#define ADDITEM  ((void (*)(void *,void *))0x400fa082u)                /* Menu::addItem */
#define FNDTOR   ((void (*)(fn_t *))0x4016dc10u)                       /* ~std::function */
#define STRDTOR  ((void (*)(void *))0x401bd86eu)                       /* ~std::string */

/* The OS's ConfirmWindow, as CLEAR TRACK SOUND? (400653ae) opens one: 0x7c
 * bytes from operator new, constructed with (this, question, second line,
 * std::function<void(bool)>, 0), owned by a shared_ptr whose 16-byte control
 * block is {CW_SP_VT, use 1, weak 1, ptr}, pushed on the view manager. YES
 * (400f6304) or NO (400f6330) closes it, then calls the function through
 * 40184a4e: invoker(any, answer) with YES 1, NO 0. */
#define CW_SIZE  0x7cu
#define CW_CTOR  ((void (*)(u32,void *,void *,fn_t *,u32))0x400f6156u)
#define CW_SP_VT 0x401c88b0u
#define DRAW_MGR 0x40181508u            /* KIT DATA item's draw functor: manager and invoker */
#define DRAW_INV 0x40180cf6u

/* saved projects */
#define MGR      ((u32 (*)(void))0x4016f292u)                          /* the saved-project manager */
#define SSTAT    ((int (*)(u32,u32))0x4011067eu)                       /* slot status: 13 = project, current format */
#define SREFR    ((void (*)(u32,u32))0x40110952u)                      /* (mgr, slot): reread the slot's header */
#define SDEV     ((u32 (*)(u32))0x401101c4u)                           /* storage state: 2 = ready */
#define SREAD    ((int (*)(u32,void *,u32,u32,u32))0x40110892u)        /* (mgr, dst, slot, 512-byte block, bytes) */
/* The manager's slot cache, as SREFR (40110952) fills it from each slot's
 * 512-byte header (BEEFBACE, status, 16-byte name): at mgr + 12 + 28 * slot,
 * valid byte +0 (set when the header has a name), status +4 (what SSTAT
 * returns), name +8, NUL-padded. */
#define SLOT_CACHE 12u
#define SLOT_STRIDE 28u
#define SLOT_STATUS 4u
#define SLOT_NAME 8u
#define SWRITE   ((int (*)(u32,void *,u32,u32,u32))0x40111c0cu)        /* (mgr, src, slot, block, bytes): 0 = ok */

/* Kit codec: the entries the core hooks (core_meta_kit_decode/encode), so
 * the core's ENCODE/DECODE notifications (host query 34) run. */
#define KDEC     ((unsigned char (*)(void *,void *))0x400a78a0u)      /* (Kit in RAM, stored record) */
#define KENC     ((unsigned char (*)(void *,void *,u32))0x400a78f4u)  /* (record buffer, Kit in RAM, 0) */
#define KNOTE    ((void (*)(void *,u32,u32))0x40172414u)               /* tell the engine: (obj + 304, index, 0) */
#define MEMCPY   ((void (*)(void *,const void *,u32))0x4011613cu)      /* the core's COPY path (core_meta_copy) */
#define SPREL    ((void (*)(u32 *))0x4016dfacu)                        /* release a shared_ptr (control block) */
#define SPCOPY   ((void (*)(u32 *,u32 *))0x4016eec2u)                  /* copy a shared_ptr (dst, src control block) */
#define TMPKIT   0x40177318u            /* new temporary Kit: shared_ptr in (a0) */

/* messages, current pattern, project */
#define MSGCTX   ((u32 (*)(void))0x4016ea6eu)
#define MSG      ((void (*)(u32,const char *))0x400c347cu)             /* one-line popup */
#define CURSRC   ((u32 (*)(void))0x4016eb0au)                          /* as the OS's KIT DATA action */
#define CURIDX   ((u32 (*)(u32))0x400c2154u)                           /* current pattern's index */
#define CUROBJ   ((u32 (*)(void))0x4016e9c8u)                          /* the project object */
#define SCRATCH  ((void *)0x405fe354u)                                 /* the stock reload's buffer */
#define SAVEBUF  ((void *)0x40600554u)                                 /* the stock SAVE KIT buffer */

/* views */
#define UIROOT   ((u32 (*)(void))0x4016e044u)                          /* UI root; +64 = the view manager */
#define VM_LIST  20u                    /* view manager: std::list<shared_ptr<View>> sentinel (40100d86) */
#define NODE_VIEW 8u                    /* list node: next +0, prev +4, View * +8 */
#define MENU_SEL 9u                     /* Menu vtable slot (+36): the selected row */
#define ROW_MODE 3u                     /* rows 0..2 the OS's, 3 MODE, 4.. the kits */
#define INVALIDATE ((void (*)(void *))0x400ffe22u)                     /* (view + 0x38): redraw */
#define PUSHVIEW ((u32 (*)(u32,u32 *,u32))0x40100d86u)                 /* (manager, shared_ptr<View> *, replace) */
#define PM_KITVIEW 472u                 /* PatternMenuView: RELOAD FROM PROJ view (+476: control block) */

/* keys: KeyEvent (400f970c), vtable at +0, code at +12, flags at +16 */
#define KEYEV_VT 0x401c23f4u
#define KEY_SETTINGS 6u
#define KEY_YES  12u
#define EV_DOWN  1u
#define EV_REPT  8u
#define EV_LONG  0x20u

/* Kit layouts. Sounds as the stock track-sound reload (4001e412): 12 audio
 * tracks of 291 bytes at +42; the FX track's 71 words (142 bytes) at +3534. */
#define SND_AT   42u
#define SND_LEN  291u
#define SND_N    12u
#define FXS_AT   (SND_AT + SND_LEN * SND_N)
#define FXS_LEN  142u
#define KIT_STORED   5632u              /* 0x1600: the encoded Kit, module records included */
#define KIT_RAM      5656u              /* 0x1618: a Kit in RAM, as the OS copies it */
#define KITS_AT      3015168u           /* 512 + 128 * 0x5c00: the kits in a project slot */
#define KITS_RAM_OFS 17417756u
#define NAME_AT  4u                     /* the record begins with 11, then the 16-byte name */
#define NAME_MAX 15
#define PAT_AT   13204u                 /* pattern records: name object at +44, vfunc +40 = char * */
#define PAT_SZ   1664u
#define PAT_NAME 44u
#define NAME_FLD 16u
#endif
