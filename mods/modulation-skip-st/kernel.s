| Pinned OS 1.41 modulation kernel with per-track execution guards.
| The source bytes are shared with the audited POLY kernel in this repository.
.text
.globl modulation_skip_filtered,modulation_skip_first,modulation_skip_first_continue,modulation_skip_after_first
.globl modulation_skip_second,modulation_skip_second_continue,modulation_skip_return
modulation_skip_filtered:
.byte 79,239,255,196,72,215,124,252,36,111,0,64,38,10,6,131,0,0,0,28,36,10,40,3,73,249,128,0,70,112,6,130
.byte 0,0,7,84
.word 0x4bfa
.word modulation_skip_helper_bridge-.

modulation_skip_first:
.word 0x4ef9
.long modulation_skip_first_guard
.word 0x4e71,0x4e71
modulation_skip_first_continue:

.word 0x47fa
.word modulation_skip_helper_bridge-.
.byte 47,0,47,4,78,149,6,132,0,0,0,142,79,239,0,12,185,252,128,0,70,136,102,218
modulation_skip_after_first:
| With every velocity track skipped, a3 has not yet been initialized by the
| first-loop continuation. FX can still be selected in the second loop.
.word 0x47fa
.word modulation_skip_helper_bridge-.
.byte 73,249,128,0,70,184,66,130,77,249,128,0,71,70,75,249,128,0,71,44
modulation_skip_second:
.word 0x4ef9
.long modulation_skip_second_guard
.word 0x4e71,0x4e71
modulation_skip_second_continue:
.byte 47,3,78,147,72,106,7,116,123,117,40,0,47,5,47,3,78,147,72,106,7,132,65,249,128,0,71,18,125,112,40,0
.byte 47,6,47,3,78,147,79,239,0,36,72,106,7,148,65,249,128,0,70,248,113,112,40,0,47,0,47,3,78,147,34,28
.byte 224,129,67,234,7,164,112,4,32,67,6,129,255,255,196,0,40,25,123,68,168,153,65,129,125,240,90,0,72,70,4,134
.byte 64,0,0,0,161,199,222,135,76,135,222,135,76,135,222,135,76,135,222,135,76,135,156,135,76,134,220,134,76,134,226,134
.byte 6,134,64,0,0,0,72,70,49,134,90,0,83,128,110,194,112,26,84,130,6,131,0,0,0,142,69,234,0,124,79,239
.byte 0,12,176,130,102,0,255,80
modulation_skip_return:
.byte 76,215,124,252,79,239,0,60,78,117
modulation_skip_helper_bridge:
jmp 0x40097aac
modulation_skip_first_guard:
 move.l %a4,%d0
 subi.l #0x80004670,%d0
 lsr.l #1,%d0
 cmpi.l #12,%d0
 bhs.s mod_first_run
 move.l modulation_skip_run_mask,%d1
 btst %d0,%d1
 bne.s mod_first_run
 addq.l #2,%a4
 addi.l #124,%d2
 addi.l #142,%d4
 cmpa.l #0x80004688,%a4
 bne.w modulation_skip_first
 jmp modulation_skip_after_first
mod_first_run:
 move.l %d2,-(%sp)
 mvs.w (%a4)+,%d0
 addi.l #124,%d2
 jmp modulation_skip_first_continue
modulation_skip_second_guard:
 move.l %d2,%d0
 lsr.l #1,%d0
 cmpi.l #13,%d0
 bhs.s mod_second_run
 move.l modulation_skip_run_mask,%d1
 btst %d0,%d1
 bne.s mod_second_run
 addq.l #2,%d2
 addi.l #142,%d3
 lea 124(%a2),%a2
 addq.l #4,%a4
 moveq #26,%d0
 cmp.l %d2,%d0
 bne.w modulation_skip_second
 jmp modulation_skip_return
mod_second_run:
 pea 1892(%a2)
 mvs.w (%a6,%d2.l),%d4
 move.l %d4,-(%sp)
 jmp modulation_skip_second_continue
