/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYNFACT_ST_ARP_H
#define SYNFACT_ST_ARP_H
typedef unsigned int arp_u32;
/* Native audio tracks only; 12 is the FX track and has no Sound ARP. */
#define ARP_TRACKS 12u
/* Sound-independent selector. All storage belongs to the caller. No firmware
 * addresses, allocations, UI calls, timers or floating point in this layer. */
/* scale (1.8, list item 28): 1 = every output snaps to the nearest note of
 * the pattern's keyboard scale. It takes the byte that was padding (5), which
 * earlier ARPs wrote as 0: their Sounds read SCALE OFF. */
struct arp_settings {
    unsigned char mode, speed, range, note_length, length, scale;
    unsigned short enabled;
    signed char offset[16];
};
/* RANGE (list item 27): 0..3 climb 1..4 octaves, as on the Digitone; 4..6
 * fall 2..4 octaves (shown -2..-4; -1 would be 1 again). Stored values 0..3
 * keep their meaning, so earlier Sounds and locks read the same. The encoder
 * moves through the shown order -4,-3,-2,1,2,3,4: positions 0..6. */
#define ARP_RANGE_MAX 6u
static __inline__ int arp_range_label(unsigned int r){return r<=3?(int)r+1:-(int)(r-2);}
static __inline__ int arp_range_position(unsigned int r){return r<=3?(int)r+3:6-(int)r;}
static __inline__ unsigned int arp_range_value(int position){return position<3?(unsigned int)(6-position):(unsigned int)(position-3);}
void arp_activate(struct arp_settings *);
struct arp_selector {
    arp_u32 mask[4];
    unsigned char order[128];
    arp_u32 count, generation;
    int step, word, ascending, last, cursor, octave;
    arp_u32 remaining;
};
void arp_reset(struct arp_selector *,arp_u32 generation);
int arp_hold(struct arp_selector *,unsigned int note);
int arp_release(struct arp_selector *,unsigned int note);
/* Donor result convention: -2 = stale generation/no held notes; -1 = rest.
 * A nonnegative result still needs the native scale/transpose/range policy. */
int arp_next(struct arp_selector *,const struct arp_settings *,arp_u32 generation);
#endif
