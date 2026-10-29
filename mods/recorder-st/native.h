/* OS 1.41 borrowed services. Test doubles replace individual calls, never
 * execute firmware or clone owning native objects. Normal build stays local. */
#ifndef REC_VIEW_DTOR
#define REC_VIEW_DTOR ((void (*)(void *))0x400ffdfcu)
#define REC_VIEW_CTOR ((void (*)(void *,const char *))0x400ffba6u)
#define REC_VIEW_TABLE ((const u32 *)0x40241bd4u)
#define REC_KEY_MANAGER ((u32 (*)(void *,const void *))0x401012d4u)
#define REC_MANAGER_CLEANUP ((void (*)(void *))0x40100ea4u)
#define REC_NAME_KEY ((u32 (*)(void *,const void *))0x400fce90u)
#define REC_TOGGLE ((void (*)(void *,u32,void *,s32,s32,u32))0x40070362u)
#define REC_BPM (*(volatile u32 *)0x402d7010u)
/* Sequencer state 43bd73bc: 1 playing, 2 stopped. 4008c288 is the native
 * STOP (STOP key 40046ed8, MIDI 4010407a): UI task only, own IRQ mask. */
#define REC_SEQ_STATE (*(volatile u32 *)0x43bd73bcu)
#define REC_SEQ_STOP ((void (*)(void))0x4008c288u)
#define REC_BRAIN ((u8 *(*)(void))0x4016e804u)
#define REC_SET_MACHINE ((u32 (*)(void *,u32,u32,u32,u32))0x4000f7beu)
#define REC_TRACK_MODEL ((u8 *(*)(void *,u32))0x4000f04cu)
#define REC_SET_PARAM ((void (*)(void *,u32,u32,u32,u32,u32))0x40013304u)
#define REC_AVAILABILITY ((u32 (*)(void *,u32))0x4000f72au)
#define REC_TRACK_CTOR ((void (*)(void *,void *,u32,void *))0x4006e1c6u)
#define REC_QSTR_DTOR ((void (*)(void *))0x401bd5d2u)
#define REC_NAME_CTOR ((void (*)(void *,void *,void *,u32,u32,u32,void *))0x400fc8a0u)
#endif
