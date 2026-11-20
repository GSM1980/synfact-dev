/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DIGIMONO_ST_TYPES_H
#define DIGIMONO_ST_TYPES_H
#include "symbols.h"
#include "upstream/mono.h"
#include "mono2k/voice.h"
#define DM_TYPE 71u
#define DM_ENGINE (0x1f9u + (DM_TYPE - 64u) * 32u)
#define DM_TYPES 12u
#define DM_DEFAULT_TYPE 2u
#define DM_TYPE_CONTROL 8u
/* Contiguous public TYPE values: upstream slot 7 belongs to MACRO, omitted. */
static const unsigned char dm_engine_type[DM_TYPES] = {
    MONO_SIN, MONO_NOIS, MONO_SAW, MONO_PULS,
    MONO_ENS, MONO_VO, MONO_PSIN, MONO_CHORD,
    255, 255, 255, 255 /* MONO2K types dispatch separately. */
};
static const char *const dm_names[DM_TYPES] = {
    "SIN", "NOISE", "SAW", "PULSE", "ENS", "VO", "POLY SIN", "CHORDS",
    "SID-6581", "FM+STAT", "FM+PAR", "FM+DYN"
};
/* TUNE followed by the engine's p[0..6], in upstream mono.h order. */
static const char *const dm_labels[DM_TYPES][8] = {
    {"TUNE", "", "", "", "", "", "", ""},
    {"TUNE", "ST", "RED", "STON", "", "", "", ""},
    {"TUNE", "UNIL", "UNIW", "UNIX", "", "SUBX", "SUB1", "SUB2"},
    {"TUNE", "UNIL", "UNIW", "SUB1", "SUB2", "PW", "PWAD", "PWRS"},
    {"TUNE", "PCH2", "PCH3", "PCH4", "WAVE", "PW", "CHRL", "CHRW"},
    {"TUNE", "VOC1", "VOC2", "V-SW", "VOIC", "CONS", "CLEN", "CVOL"},
    {"TUNE", "NOT1", "NOT2", "NOT3", "EDEP", "ESPD", "", ""},
    {"TUNE", "CHRD", "INV", "SPRD", "WAVE", "PW", "CHRL", "CHRW"},
    {"TUNE", "PW", "PWAD", "PWRS", "WAVE", "MOD", "MSRC", "MFRQ"},
    {"TUNE", "1FRQ", "1FIN", "1ENV", "1FB", "2FRQ", "2VOL", "TONE"},
    {"TUNE", "1FRQ", "1ENV", "2FRQ", "2ENV", "3FRQ", "3ENV", "TONE"},
    {"TUNE", "1FRQ", "1FEN", "1VOL", "1VEN", "2FRQ", "2ENV", "2FB"}
};
/* Reference init patches in ENGINE order, not Digitakt knob order.
 * Used by offline tests and documented as starting points; TYPE never
 * overwrites the Sound's knobs (or its parameter locks) behind the user.
 */
#define DM_INIT_PATCHES { \
    {64,0,0,0,0,0,0,0}, {64,0,0,0,0,0,0,0}, \
    {64,0,40,0,0,0,0,0}, {64,0,40,0,0,64,0,40}, \
    {64,63,63,63,0,64,0,127}, {64,43,113,64,0,0,40,100}, \
    {64,63,63,63,64,64,0,0}, {64,0,0,0,16,64,0,127}, \
    {64,0,0,127,0,0,0,64}, {64,61,64,80,32,82,64,64}, \
    {64,61,64,82,64,104,80,64}, {64,64,64,64,0,74,80,32} }
static inline unsigned dm_clamp_type(unsigned raw)
{ unsigned t = raw >> 8; return t < DM_TYPES ? t : DM_TYPES - 1u; }
#endif
