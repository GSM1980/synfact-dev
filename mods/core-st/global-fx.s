| SPDX-License-Identifier: GPL-2.0-or-later
| Shared OS 1.42 global parameter transactions and FX VCA event.
        .section .run,"ax"
        .globl core_global_has,core_global_get,core_global_set
core_global_has:
        moveq #32,%d0
        lea .Lstock_has,%a1
        bra.s .Lglobal
core_global_get:
        moveq #33,%d0
        lea .Lstock_get,%a1
        bra.s .Lglobal
core_global_set:
        moveq #24,%d0
        lea .Lstock_set,%a1
.Lglobal:
        tst.l core_parameter_providers
        beq.s .Lfallback_direct
        lea -12(%sp),%sp
        clr.l (%sp)
        move.l %a1,8(%sp)
        lea 16(%sp),%a0
        pea (%sp)
        clr.l -(%sp)
        move.l %a0,-(%sp)
        move.l %d0,-(%sp)
        jsr core_parameter_provider
        lea 16(%sp),%sp
        tst.l %d0
        beq.s .Lfallback
        move.l (%sp),%d0
        lea 12(%sp),%sp
        rts
.Lfallback:
        movea.l 8(%sp),%a1
        lea 12(%sp),%sp
.Lfallback_direct:
        jmp (%a1)
.Lstock_has:
        move.l %d2,-(%sp)
        moveq #124,%d2
        move.l 8(%sp),%d1
        jmp 0x4000d878
.Lstock_get:
        move.l %d2,-(%sp)
        move.l 8(%sp),%d1
        move.l 12(%sp),%d0
        jmp 0x4000d954
.Lstock_set:
        move.l %d3,-(%sp)
        move.l %d2,-(%sp)
        move.l 12(%sp),%d1
        jmp 0x4000da3a

| At 400905b6, native MOVCLR left ACC0/1 empty. Callback receives
| struct st_fx_cv {hardware,left,right}; may use ACC0/1, keeps ACC2/3.
        .globl core_fx_cv,core_fx_cv_table
core_fx_cv:
        move.l %a0,-(%sp)
core_fx_cv_table:
        lea ev_fx_cv,%a0
        tst.l (%a0)
        beq.s .Lcv_replay
        lea -76(%sp),%sp
        movem.l %d0-%d7/%a0-%a6,(%sp)
        move.l %macsr,%d0
        move.l %d0,60(%sp)
        move.l %a3,64(%sp)
        move.l %d1,68(%sp)
        move.l %d3,72(%sp)
        movea.l %a0,%a2
.Lcv_next:
        move.l (%a2)+,%d0
        beq.s .Lcv_done
        movea.l %d0,%a0
        pea 64(%sp)
        jsr (%a0)
        addq.l #4,%sp
        bra.s .Lcv_next
.Lcv_done:
        move.l 68(%sp),4(%sp)
        move.l 72(%sp),12(%sp)
        movclr.l %acc0,%d0
        movclr.l %acc1,%d0
        move.l 60(%sp),%d0
        move.l %d0,%macsr
        movem.l (%sp),%d0-%d7/%a0-%a6
        lea 76(%sp),%sp
.Lcv_replay:
        movea.l (%sp)+,%a0
        cmpi.l #65535,%d1
        rts
