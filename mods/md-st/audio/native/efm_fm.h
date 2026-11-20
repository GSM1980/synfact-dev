/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/efm_fm.h - the EFM operators' 32-bit phases, for MD_AP_FM (md-native.h).
 *
 * mdkit keeps an operator's phase as a 48-bit pair (A1, A0) and reads its sine at
 * A1 & 0x7fff, so what matters is the phase modulo 2^39 (15 bits of A1, 24 of A0).
 * Here it is one 32-bit word, its top 32 bits: p = (A1 << 17) | (A0 >> 7), wrapping as
 * the mask does; the sine's index is p >> 17. A step or a modulation in 48-bit units is
 * >> 7 here: floor(x y / 2^k) becomes the EMAC's read floor(x y / 2^17) << (10 - k)
 * (k = 4: << 6; k = 7: << 3), its low bits zero. "idx = (phase + mod) & 0x7fff; phase =
 * idx - mod + step" is then idx = (p + m) >> 17; p += step. The 7 dropped bits of A0 and
 * the read's dropped bits are under 2^-31 of a turn a sample. State stays mdkit's: the
 * pairs are converted at a block's start and end.
 */
#ifndef MD_EFM_FM_H
#define MD_EFM_FM_H
/* the sine at a 32-bit phase: the EFM sine whole (md-tables.h FULL_SINES), its
 * word at the 15-bit index, the same as md_sine_word gives: one load */
static inline int32_t md_fm_sine(uint32_t p) {return md_efm_cb_sine_w[p >> 17];}
static inline uint32_t md_fm_from48(md_acc a) {return ((uint32_t)MD_T(a) << 17) | ((MD_L(a) & 0xffffffu) >> 7);}
static inline md_acc md_fm_to48(uint32_t p) {return md_acc_make((int32_t)(p >> 17), (p & 0x1ffffu) << 7);}
/* floor(x y / 2^k) in 48-bit units, as a 32-bit phase quantity (k = 4 or 7) */
static inline uint32_t md_fm_step(int32_t x, int32_t y, unsigned k) {return (uint32_t)md_ar1(x, y) << (10 - k);}
#endif
