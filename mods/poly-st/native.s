| Synfact native services retain their original ownership and queue semantics.
        .text
        .globl native_event_alloc,native_event_copy,native_event_free
        .globl native_enqueue,native_immediate,native_cancel_track
        .globl native_release_track,native_publish_sound
        .set native_event_alloc,0x4011a4c2
        .set native_event_copy,0x4011a52c
        .set native_event_free,0x4011a4f8
        .set native_enqueue,0x4011a648
        .set native_immediate,0x4011a722
        .set native_cancel_track,0x4008a9b0
        .set native_release_track,0x400a5708
        .set native_publish_sound,0x400a31a2
        .include "imported/kernels.s"
