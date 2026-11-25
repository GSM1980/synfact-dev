| SPDX-License-Identifier: GPL-2.0-or-later
| Core 2.7: the PATTERN menu's build events (SMZ events 16 and 17).
|
| ev_pattern_menu: at 0x4004ae0c, the end of PatternMenuView's constructor
| (0x4004a8e0), replacing movem.l 16(sp),d2-d6/a2-a6 (6 bytes) by jmp.
| ev_reload_menu: at 0x40182212, the end of PatternReloadMenuView's
| constructor (0x40181f40, SETTINGS > PATTERN > RELOAD FROM PROJ), replacing
| movem.l 24(sp),d2-d6/a2-a6 (6 bytes) by jmp.
|
| At both sites a2 = the menu, fully built: its native items are added and
| (PatternMenuView) its member views exist, e.g. the RELOAD FROM PROJ view
| as a shared_ptr at +472/+476. Handlers: void f(void *menu), in order,
| task (UI main loop) context. They may add items after the native ones
| (core_additem, or the OS's MenuItem constructor and Menu::addItem) and may
| keep the menu's address: the OS builds each of these menus once, at boot.
| No IO. d0-d1/a0-a1 are restored for the stock epilogue; a handler keeps
| the C ABI (d2-d7/a2-a6). With no subscriber the stock instructions run
| at once.

        .section .run,"ax"
        .globl core_pattern_menu,core_pattern_menu_table
core_pattern_menu:
        lea -20(%sp),%sp
        movem.l %d0-%d1/%a0-%a1/%a3,(%sp)
core_pattern_menu_table:
        lea ev_pattern_menu,%a3
        bsr.s .Lmenu_each
        movem.l (%sp),%d0-%d1/%a0-%a1/%a3
        lea 20(%sp),%sp
        movem.l 16(%sp),%d2-%d6/%a2-%a6  | the instruction this replaced
        jmp 0x4004ae12

        .globl core_reload_menu,core_reload_menu_table
core_reload_menu:
        lea -20(%sp),%sp
        movem.l %d0-%d1/%a0-%a1/%a3,(%sp)
core_reload_menu_table:
        lea ev_reload_menu,%a3
        bsr.s .Lmenu_each
        movem.l (%sp),%d0-%d1/%a0-%a1/%a3
        lea 20(%sp),%sp
        movem.l 24(%sp),%d2-%d6/%a2-%a6  | the instruction this replaced
        jmp 0x40182218

| a3 = the list, a2 = the menu. Each handler: f(menu).
.Lmenu_each:
1:      move.l (%a3)+,%d0
        beq.s 2f
        move.l %a2,-(%sp)
        movea.l %d0,%a0
        jsr (%a0)
        addq.l #4,%sp
        bra.s 1b
2:      rts
