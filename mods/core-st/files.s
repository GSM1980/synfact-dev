| SPDX-License-Identifier: GPL-2.0-or-later
| Native Data RPC extension points, Syntakt OS 1.41 only. Task context.
| Nontrivial C++ result objects have their return slot in A0.
| ev_fs_route: int f(router, const std::string *path, Result<FileInfo> *out).
| ev_fs_write: int f(router, FileInfo *info, Result<Writer> *out).
| A nonzero return supplies a complete result; zero declines to stock.
| No subscribers: replay the exact displaced prologue and continue natively.
        .section .run,"ax"
        .globl core_fs_route,core_fs_write,core_fs_written
core_fs_route:
        lea -16(%sp),%sp
        movem.l %a2-%a5,(%sp)
        movea.l %a0,%a2
        movea.l 20(%sp),%a3
        movea.l 24(%sp),%a4
        .globl core_fs_route_table
core_fs_route_table:
        lea ev_fs_route,%a5
1:      move.l (%a5)+,%d0
        beq.s 3f
        move.l %a2,-(%sp)
        move.l %a4,-(%sp)
        move.l %a3,-(%sp)
        movea.l %d0,%a0
        jsr (%a0)
        lea 12(%sp),%sp
        tst.l %d0
        beq.s 1b
        move.l %a2,%d0
        movem.l (%sp),%a2-%a5
        lea 16(%sp),%sp
        rts
3:      movea.l %a2,%a0
        movem.l (%sp),%a2-%a5
        lea 16(%sp),%sp
        link %a6,#-40
        movem.l %d2-%d3/%a2-%a5,(%sp)
        jmp FS_ROUTE_RET
core_fs_write:
        lea -16(%sp),%sp
        movem.l %a2-%a5,(%sp)
        movea.l %a0,%a2
        movea.l 20(%sp),%a3
        movea.l 24(%sp),%a4
        .globl core_fs_write_table
core_fs_write_table:
        lea ev_fs_write,%a5
1:      move.l (%a5)+,%d0
        beq.s 3f
        move.l %a2,-(%sp)
        move.l %a4,-(%sp)
        move.l %a3,-(%sp)
        movea.l %d0,%a0
        jsr (%a0)
        lea 12(%sp),%sp
        tst.l %d0
        beq.s 1b
        move.l %a2,%d0
        movem.l (%sp),%a2-%a5
        lea 16(%sp),%sp
        rts
3:      movea.l %a2,%a0
        movem.l (%sp),%a2-%a5
        lea 16(%sp),%sp
        link %a6,#-76
        movem.l %d2-%d5/%a2-%a4,(%sp)
        jmp FS_WRITE_RET
| After native header/footer checks AND successful writer close, only at
| DataWriteClose's existing completion callback (not copy/move callbacks).
| ev_fs_written: void f(const capture*, const BackupHeader*, Status *status).
| Status +0 success byte, +4 owned native string; only turn success into error.
core_fs_written:
        lea -12(%sp),%sp
        movem.l %a2-%a4,(%sp)
        movea.l 16(%sp),%a2
        movea.l 20(%sp),%a3
        move.l %a3,-(%sp)
        move.l %a2,-(%sp)
        jsr FW_FS_WRITTEN
        addq.l #8,%sp
        .globl core_fs_written_table
core_fs_written_table:
        lea ev_fs_written,%a4
1:      move.l (%a4)+,%d0
        beq.s 2f
        pea -88(%a6)
        move.l %a3,-(%sp)
        move.l %a2,-(%sp)
        movea.l %d0,%a0
        jsr (%a0)
        lea 12(%sp),%sp
        bra.s 1b
2:      movem.l (%sp),%a2-%a4
        lea 12(%sp),%sp
        rts
