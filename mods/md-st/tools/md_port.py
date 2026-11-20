# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the MD machine's voices for the Syntakt from mdkit's (mods/md-st/mdkit).

    python mods/md-st/tools/md_port.py           # write audio/voices and the tables
    python mods/md-st/tools/md_port.py --check   # fail if they are not what it writes

For each voice in VOICES it reads mdkit/src/<family>/<voice>/mdkit_<family>_<voice>.{c,h}
and writes audio/voices/ with these changes, each marked in the file:

1. <stdint.h>/<stddef.h> become "md-types.h" (the Syntakt build is freestanding).
2. The accumulator helpers (mid, add, mpy, acc_to_word...) call md-acc.h, which keeps the
   same 56-bit values in an encoding a 32-bit CPU handles in a few instructions.
3. The functions the recipe names, which mdkit computes in double precision, read a table
   under MD_ST_TABLES. The tables are those very functions evaluated on the host: this
   script compiles tools/md-tables.c with each voice (without MD_ST_TABLES) and writes
   audio/md-tables.c and audio/voices/md-tables.h, identical tables shared.
4. md_st_<voice>_render_words: the voice's render, writing its 24-bit words as int32_t in
   place of floats. The float render stays for the host; the Syntakt build leaves it out.
5. For the voices in INT_CLOCK, the control clock in integers (port_clock): mdkit keeps
   its values in floats, but each is a whole number of 1/128 (a target is a knob; the
   others come from knobs and (3 raw + 128 target) / 4, divided by 128, exact in a
   float), so the same storage holds the target as the knob and the others as 128
   times the value. Copies stay as they are (a float copy moves the same 32 bits);
   tests/test_md_st.py checks that such a voice calls no float routine on the Syntakt.
8. HAND voices: mdkit's EFM-BD is a double-precision model, not DSP code; the Syntakt runs
   a fixed-point port of it (audio/native/efm_bd.c), measured against mdkit by signal-to-
   error ratio (tests/test_md_st.py). Nothing is generated for it; its descriptor facts
   come from mdkit's own file.
7. EDITS: a few exact text changes a voice needs (each a reason in EDITS). PI-CC, HH and RC
   keep 65,536 words of DSP delay memory (256 KB a track) and use the 1,024 at 0x135600:
   their delay pages are base .. base + 0x3ff, modulo 256 each, from base = 0x135600 +
   low(mpy(0x300, fraction[0x142])), and fraction[0x142] is 0 from the boot table on. The
   port keeps those 1,024 words; an access elsewhere in the delay reads 0 and writes nothing,
   and under MD_ST_DOMAIN_CHECK fails the test (mdkit could only get there through
   metal_excitation's 65,536-pair loop, a whole block of DSP time; no test reaches it).
6. mdkit's own integer tables (static const arrays of 512 bytes or more) that are the same
   in several places (PI-SD and PI-RS hold one word table four times) are kept once in
   audio/md-tables.c under MD_ST_TABLES (share_arrays), in the narrowest type that holds
   their values when they are only read by index.

The host compiler is $LLVM_MINGW_BIN/x86_64-w64-mingw32-clang.exe (docs/SYNTAKT-BUILD.md),
or $MD_HOST_CC. tests/test_md_st.py checks every ported voice against mdkit word for word.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
MOD = HERE.parent
MDKIT = MOD / 'mdkit'
OUT = MOD / 'audio' / 'voices'

# Per voice: the double-precision functions to read from tables. Each has
#   func  the function whose body becomes a lookup; arg: its integer argument
#   name  the table; kind 'sine' (32768 words, md_sine_word) or n words, or lo..hi; a sine
#         with whole=True keeps its own quarter (no minus bitmap: less work where it is hot)
#   expr  the value at index i, computed by the voice's own code on the host
#   calls (optional) every call of func must start with this text (the lookup
#         ignores its other arguments)
#   key   (optional, with n) the table index as a C expression of arg, for a function
#         that depends on its argument only through it; check: (a, b), the arguments
#         over which the host proves func(x) == table[key(x)]. expr is then the value
#         at index i
#   below (optional, with lo..hi) the statement for an argument under lo, in place of 0
# Or, for a computation written inline rather than as a function:
#   text  a statement of the voice (one, whole), replaced under MD_ST_TABLES by
#   look  the statement that reads the table, with {t} for the table and {i} for
#   index the C expression it is read at, in 0..n-1; expr the value at index i
# Or a table only (only=True: name, n, expr), for a text recipe's look to read.
# host_check (optional): C statements the host runs when it makes the tables (return 1 fails)
# minus (optional): the name of a sine table; the table is kept as its words less that sine's,
#   in runs of 64: a 16-bit base a run (<name>_base) and an 8-bit offset a word (<name>), so a
#   word is the sine's + base[i >> 6] + offset[i] (the generator checks every one fits)
def trx_rom(v: str) -> list[dict]:
    """a TRX voice's model memory: 256 offset words (its own) and 8192 cos/exp words
    (the same in every TRX voice, so kept once)"""
    return [dict(func='trx_model_offset', arg='i', n=256, name=v + '_offset', expr='(uint32_t)trx_model_offset(i)&0xffffffu'),
            dict(func=v + '_trx_model_memory', arg='i', lo=256, hi=8448, name=v + '_rom', expr=v + '_trx_model_memory(i)',
                 below='return (uint32_t)trx_model_offset(at_)&0xffffffu;')]


def sine(func: str, name: str, arg: str = 'index') -> dict:
    return dict(func=func, arg=arg, kind='sine', name=name, expr='%s(i)' % func)


def pi_decay(v: str) -> list[dict]:
    """a PI voice's damp and env ROM words (mdkit_decay_radius, powers 1 and 128; the ROM reads
    them at 0x142949 + 0..511 and 0x142b49 + 0..511)"""
    x = v.split('_')[1]
    return [dict(func='%s_damp' % x, arg='index', n=512, name=v + '_damp', expr='%s_damp(i)' % x),
            dict(func='%s_env' % x, arg='index', n=512, name=v + '_env', expr='%s_env(i)' % x)]


def pi_metal(v: str) -> list[dict]:
    """PI-CC, HH, RC's ROM: damp and env, the sine of period 32,767 (32768 words at 0x148000, kept
    as differences from TRX-BD's sine) and the two filter-curve banks (4 x 128 words each)"""
    x = v.split('_')[1]
    line = lambda lo, bank: ('  if (a >= 0x%xu && a < 0x%xu) { unsigned r=(a-0x%xu)/4u,c=(a-0x%xu)%%4u; return mdkit_word('
                             'mdkit_filter_curve(%s_%s_coef[c],(double)r)); }' % (lo, lo + 0x200, lo, lo, x, bank))
    return pi_decay(v) + [
        dict(text='  if (a >= 0x148000u && a < 0x150000u) { unsigned i=(a-0x148000u)&0x7fffu; return mdkit_word(8388605.0*mdkit_sin_turn(i,32767.0)); }',
             index='(a-0x148000u)&0x7fffu', n=32768, name='pi_sine767', minus='trx_bd_sine',
             look='if (a >= 0x148000u && a < 0x150000u) return (md_sine_word(&md_trx_bd_sine, {i}) + '
                  '(uint32_t)((int32_t){t}_base[{i} >> 6] + {t}[{i}])) & 0xffffffu;',
             expr='mdkit_word(8388605.0*mdkit_sin_turn(i,32767.0))'),
        dict(text=line(0x145224, 'A'), index='(a-0x145224u)&511u', n=512, name=v + '_curve_a',
             look='if (a >= 0x145224u && a < 0x145424u) return {t}[{i}];',
             expr='mdkit_word(mdkit_filter_curve(%s_A_coef[i%%4u],(double)(i/4u)))' % x),
        dict(text=line(0x145424, 'B'), index='(a-0x145424u)&511u', n=512, name=v + '_curve_b',
             look='if (a >= 0x145424u && a < 0x145624u) return {t}[{i}];',
             expr='mdkit_word(mdkit_filter_curve(%s_B_coef[i%%4u],(double)(i/4u)))' % x)]


def efm(v: str, sine_func: str, pitch: list[tuple[str, int, str, str]] | None = None, decay: bool = True,
        whole: bool = True) -> list[dict]:
    """an EFM voice: its sine, mdkit_decay_index and its pitch polynomials. pitch: (coefficients,
    degree, the knob expression, the variable set); each is read at its call (the knob is a
    control, 0..127)"""
    r = [dict(func=sine_func, arg='index', kind='sine', name=v + '_sine', expr='%s((uint16_t)i)' % sine_func, whole=whole)]
    if decay:
        r.append(dict(func='mdkit_decay_index', arg='index', n=1024, name=v + '_decay', expr='mdkit_decay_index(i)'))
    for n, (coef, degree, knob, target) in enumerate(pitch or []):
        r.append(dict(text='    %s=mdkit_pitch_poly(%s,%d,%s);' % (target, coef, degree, knob), look=target + '={t}[{i}];',
                      index=knob, n=128, name='%s_pitch%d' % (v, n), expr='mdkit_pitch_poly(%s,%d,i)' % (coef, degree)))
    return r


VOICES = {
    'trx_bd': [
        dict(func='trx_bd_bd_sine', arg='index', kind='sine', name='trx_bd_sine', expr='trx_bd_local_sine(i)'),
        dict(func='trx_bd_trx_bd_board_a', arg='i', n=288, name='trx_bd_board_a', expr='trx_bd_trx_model_bd_board_a(i)'),
        dict(func='trx_bd_trx_bd_board_b', arg='i', n=288, name='trx_bd_board_b', expr='trx_bd_trx_model_bd_board_b(i)'),
    ],
    'trx_sd': [sine('trx_sd_local_sine', 'trx_sd_sine')] + trx_rom('trx_sd') + [
        # sd_attack(cursor + i), cursor <= 0xfe0, i < 32
        dict(func='sd_attack', arg='index', n=4096, name='trx_sd_attack', expr='sd_attack(i)')],
    'trx_xt': [sine('xt_local_sine', 'trx_xt_sine'),
               dict(func='xt_decay_curve', arg='i', n=220, name='trx_xt_decay', expr='xt_decay_curve(i)'),     # (r*220)>>14
               dict(func='xt_pitch_curve', arg='i', n=256, name='trx_xt_pitch', expr='xt_pitch_curve(i)'),     # r>>6
               dict(func='xt_attack_shape', arg='i', n=64, name='trx_xt_attack', expr='xt_attack_shape(i)')],
    'trx_cp': trx_rom('trx_cp') + [
        dict(func='trx_cp_trx_model_room_decay', arg='raw', n=64, key='raw>16383u?63u:raw>>8', check=(0, 1 << 24),
             name='trx_cp_room', expr='trx_cp_trx_model_room_decay(i<<8)')],
    'trx_rs': [sine('trx_rs_local_sine', 'trx_rs_sine')] + trx_rom('trx_rs') + [
        dict(func='trx_rs_rs_decay_curve', arg='i', n=160, name='trx_rs_decay', expr='trx_rs_rs_decay_curve(i)'),  # (160r)>>14
        # trx_rs_attack(attack + i), attack < 0x300, i < 32
        dict(func='trx_rs_trx_rs_attack', arg='i', n=800, name='trx_rs_attack', expr='trx_rs_trx_rs_attack(i)')],
    'trx_cb': [sine('trx_cb_local_sine', 'trx_cb_sine')] + trx_rom('trx_cb'),
    'trx_ch': trx_rom('trx_ch'),
    'trx_oh': trx_rom('trx_oh'),
    'trx_cy': trx_rom('trx_cy'),
    'trx_ma': trx_rom('trx_ma'),
    'trx_cl': [sine('trx_cl_local_sine', 'trx_cl_sine')] + trx_rom('trx_cl') + [
        dict(text=' p[0]=(uint32_t)(8388608.0-trx_cl_trx_model_exp(trx_cl_trx_model_cheb(trx_cl_cl_decay_coeff,7,x))+0.5);',
             look='p[0]={t}[{i}];', index='index', n=160, name='trx_cl_decay',    # index = (raw*5)>>9 <= 158
             expr='(uint32_t)(8388608.0-trx_cl_trx_model_exp(trx_cl_trx_model_cheb(trx_cl_cl_decay_coeff,7,2.0*i/159.0-1.0))+0.5)')],
    'trx_xc': [sine('xc_local_sine', 'trx_xc_sine'),
               dict(func='xc_decay_curve', arg='i', n=160, name='trx_xc_decay', expr='xc_decay_curve(i)'),     # (r*160)>>14
               dict(func='xc_pitch_curve', arg='i', n=256, name='trx_xc_pitch', expr='xc_pitch_curve(i)'),
               dict(func='xc_attack_shape', arg='i', n=32, name='trx_xc_attack', expr='xc_attack_shape(i)')],
    'trx_b2': [sine('b2_local_sine', 'trx_b2_sine'),
               dict(func='b2_dec_raw', arg='i', n=384, name='trx_b2_decay', expr='b2_dec_raw(i)'),            # (3r)>>7
               dict(func='b2_hold_raw', arg='i', n=256, name='trx_b2_hold', expr='b2_hold_raw(i)')],          # r>>6
    'efm_bd': [],                     # HAND: audio/native/efm_bd.c
    'efm_sd': [
        dict(func='sd_sine', arg='index', kind='sine', name='efm_sd_sine', expr='sd_sine((uint16_t)i)', whole=True),
        dict(func='mdkit_decay_index', arg='index', n=1024, name='efm_sd_decay', expr='mdkit_decay_index(i)'),
        dict(func='mdkit_pitch_poly', arg='knob', n=128, name='efm_sd_pitch', expr='mdkit_pitch_poly(mdkit_sd_pitch,3,i)',
             calls='mdkit_pitch_poly(mdkit_sd_pitch,3,'),
    ],
    'efm_xt': efm('efm_xt', 'xt_sine', decay=False, whole=False) + [    # TRX-B2's sine: kept as a bitmap
           # xt_build_tables fills these at init
        dict(text='        s->pitch[i] = (uint32_t)rounded & MDKIT_MASK24;', look='s->pitch[i]={t}[{i}];', index='i', n=128,
             name='efm_xt_pitch', expr='({ static mdkit_efm_xt t_; xt_build_tables(&t_); t_.pitch[i]; })'),
        dict(text='        s->decay[i] = (UINT32_C(0x800000) - (uint32_t)rounded) & MDKIT_MASK24;', look='s->decay[i]={t}[{i}];',
             index='i', n=326, name='efm_xt_decay', expr='({ static mdkit_efm_xt t_; xt_build_tables(&t_); t_.decay[i]; })')],
    'efm_cp': efm('efm_cp', 'cp_sine', [('mdkit_cp_pitch', 6, '(unsigned)k[MDKIT_EFM_CP_PTCH]', 's->op1_rate')]),
    'efm_rs': efm('efm_rs', 'rs_sine', [('mdkit_rs_sptc', 3, '(unsigned)k[MDKIT_EFM_RS_SPTC]', 's->a_rate'),
                                        ('mdkit_rs_pitch', 3, '(unsigned)k[MDKIT_EFM_RS_PTCH]', 's->b_rate')]),
    'efm_cb': efm('efm_cb', 'cb_sine', [('mdkit_cb_pitch', 8, 'ptch', 's->pitch_a')]),
    'efm_hh': efm('efm_hh', 'hh_sine', [('mdkit_hh_pitch', 3, '(unsigned)k[MDKIT_EFM_HH_PTCH]', 's->op_a_pitch'),
                                        ('mdkit_hh_tfrq', 4, '(unsigned)k[MDKIT_EFM_HH_TFRQ]', 's->op_c_phase_inc')]),
    'efm_cy': efm('efm_cy', 'cy_sine') + [     # its one polynomial is a declaration's value: read in the function
        dict(func='mdkit_pitch_poly', arg='knob', n=128, name='efm_cy_pitch', expr='mdkit_pitch_poly(mdkit_cy_pitch,3,i)',
             calls='mdkit_pitch_poly(mdkit_cy_pitch,3,')],
    'pi_bd': [dict(func='bd_sample', arg='index', n=1480, name='pi_bd_sample', expr='bd_sample(i)')] + pi_decay('pi_bd'),   # rom 0x142291..0x142859
    'pi_sd': [
        dict(func='sd_rom', arg='address', lo=0x142949, hi=0x1433b7, name='pi_sd_rom', expr='sd_rom(i)',
             outside=(0x142000, 0x145000)),
    ],
    'pi_mt': [dict(func='mt_sample', arg='index', n=1676, name='pi_mt_sample', expr='mt_sample(i)')] + pi_decay('pi_mt'),   # rom 0x143691..0x143d1d
    'pi_ml': [dict(func='ml_sample', arg='index', n=2682, name='pi_ml_sample', expr='ml_sample(i)')] + pi_decay('pi_ml'),   # rom 0x143f50..0x1449ca
    'pi_ma': pi_decay('pi_ma') + [
        # gain[a * 128 + b] (64 KB in mdkit) is exactly floor(row[a] col[b] / gain[0]), and that is
        # floor(row[a] C[b] / 2^23) with C[b] = ceil(col[b] 2^23 / gain[0]) (24 bits; the host step
        # checks all 16384 words): two 128-word tables and one exact product
        dict(only=True, name='pi_ma_gain_row', n=128, expr='mdkit_pi_ma_gain[i * 128u]'),
        dict(only=True, name='pi_ma_gain_col', n=128,
             expr='(uint32_t)((((uint64_t)mdkit_pi_ma_gain[i] << 23) + mdkit_pi_ma_gain[0] - 1u) / mdkit_pi_ma_gain[0])'),
        dict(text='out[1]=mdkit_pi_ma_gain[((MD_CLK(k)[0]>>7))*128+(MD_CLK(k)[2]>>7)];', index='0', n=1, name='pi_ma_gain',
             look='out[1]=(uint32_t)md_mul_shift_exact((int32_t)md_pi_ma_gain_row[MD_CLK(k)[0]>>7], '
                  '(int32_t)md_pi_ma_gain_col[MD_CLK(k)[2]>>7], 23);',
             expr='0', host_check='for (unsigned x = 0; x < 16384u; ++x) if (mdkit_pi_ma_gain[x] != (uint32_t)(((uint64_t)mdkit_pi_ma_gain[(x >> 7) * 128u] * ((((uint64_t)mdkit_pi_ma_gain[x & 127u] << 23) + mdkit_pi_ma_gain[0] - 1u) / mdkit_pi_ma_gain[0])) >> 23)) { fprintf(stderr, "pi_ma_gain: a word is not row x col"); return 1; }')],
    'pi_rs': [dict(func='rs_sample', arg='index', n=587, name='pi_rs_sample', expr='rs_sample(i)')] + pi_decay('pi_rs'),    # rom 0x144cb2..0x144efd
    'pi_rc': pi_metal('pi_rc'),
    'pi_cc': pi_metal('pi_cc'),
    'pi_hh': pi_metal('pi_hh'),
}

# Voices written by hand (point 8): their source, header and state type
HAND = {'efm_bd': ('audio/native/efm_bd.c', 'native/efm_bd.h', 'md_efm_bd')}

# The voices whose control clock keeps mdkit's floats (EFM: efm_clock.c, efm_bd.c); the others
# keep it in integers, a target as the knob and raw, pending and current as 128 times it
# (native/trx_bd_clock.c). audio/md.c sets a note's knobs in the clock (md_snap) by it.
FLOAT_CLOCK = {v for v in VOICES if v.startswith('efm_')}

# The approximate forms each voice takes (md-native.h, MD_ST_APPROX; MD_ST_EXACT turns them
# off): 'mid' the rounded and two-product words from the EMAC's read, 'floor' the
# md-floor.h words likewise, 'sine' the sine quarter without its fix word and minus bit.
# Kept where tests/md_st_voice_ser.c measured the error under -85 dBFS (WORK.md, 2026-11-17):
# EFM's FM feedback takes no sine change (EFM-CB and CY no mid either); TRX-CP's filters no
# floor. Dropping A0 from accumulators (products into md_acc) broke most voices: not offered.
APPROX = {v: ('mid', 'sine', 'floor') for v in VOICES if v.startswith(('trx_', 'pi_'))}
APPROX['trx_cp'] = ('mid', 'sine', 'floor')      # its cp_filter6 keeps the exact floor (MD_CP6_EXACT, NATIVE)
APPROX.update({v: ('mid',) for v in ('efm_sd', 'efm_xt', 'efm_cp', 'efm_rs', 'efm_hh')})
# 'fm' (md-native.h, MD_AP_FM): the FM loops' steps, rounded MACs and sine approximate, for the
# EFM voices that did not fit one track beside seven SY SWARM (the user's worst case,
# 2026-11-18). Their FM feedback makes them leave mdkit's words at once: no dBFS ceiling
# holds, the user judges them by ear (tests/test_md_st.py checks level and spectrum).
APPROX.update({v: APPROX.get(v, ()) + ('fm',) for v in ('efm_sd', 'efm_xt', 'efm_rs', 'efm_hh')})
APPROX['efm_cb'] = ('mid', 'fm')
APPROX['efm_cy'] = ('mid', 'fm')     # core 2.2: one instance beside seven SY SWARM (WORK.md)
# 'acc' (md-acc.h, MD_AP_ACC): accumulator products from the EMAC's read, A0 to 6 bits plus
# half a unit. Measured product by product (a site probe over every voice, WORK.md): the
# signal path takes it; what must stay exact is ACC_EXACT below and the natives' _x calls.
# Taken where it saves (PI-RC, CC and HH spend theirs in the metal filter, which stays exact).
APPROX.update({v: APPROX.get(v, ()) + ('acc',) for v in ('trx_cp', 'trx_rs', 'trx_ch', 'trx_oh', 'trx_cy',
                                                         'pi_bd', 'pi_sd', 'pi_mt', 'pi_ml', 'pi_ma', 'pi_rs')})
# 'struct' (MD_AP_STRUCT): the PI voices' structural forms, to fit one instance in 15 % of
# AUDIO CPU (the user, 2026-11-19). The waveform changes: tests/test_md_st.py checks level
# and spectrum (SPECTRUM), the user judges A/B renders. Each voice's PI_STRUCT entry lists
# the modes it leaves out, the ones that weigh least on its sound (MD_PI_SKIP_MODES: the
# modal filter, drum_modes and the coefficients skip them). PI-SD and PI-RS's drum_modes
# also limit the bus once, after the last mode, and drum_delays keep the noise filter in
# one word (native/pi_modes.c, pi_delays.c). Measured one by one (WORK.md):
# PI-ML: modes 2 and 4 (levels within 0.78 dB, bands within 5.0 dB; mode 2 alone 0.16 / 3.0 dB).
# PI-SD: modes 4, 6 and 7 (levels within 0.29 dB, bands within 2.2 dB); PI-RS the same (1.8 dB).
# PI-MT: modes 2 and 4 to 7 (0.95 dB, 4.0 dB; 4 to 7 alone 0.18, 0.9 dB). PI-SD, RS, MT and BD also run
# their three delay lines at half rate (pi_delays.c, pi_mt_block.c, pi_bd_block.c). PI-BD: modes 2 to 7 (0.03 dB, 1.2 dB), and its excitation,
# coefficients and modes take 'acc' and 'mid' (PI_BD_EXACT).
PI_STRUCT = {'pi_ml': (2, 4), 'pi_sd': (4, 6, 7), 'pi_rs': (4, 6, 7), 'pi_mt': (2, 4, 5, 6, 7),
             'pi_bd': (2, 3, 4, 5, 6, 7), 'pi_rc': (), 'pi_cc': (), 'pi_hh': ()}
# PI-RC, CC and HH take 'acc' with it: their metal filter's products from the read (pi_metal.c)
APPROX.update({v: APPROX[v] + (('acc',) if 'acc' not in APPROX[v] else ()) + ('struct',) for v in PI_STRUCT})
# the forms whose voices the tests judge by level and spectrum, not by a dBFS ceiling
# a voice's further 'struct' switches: PI-SD's half-rate delays rounded to whole words (0.2 to
# 0.4 % longer: bands 2.3 dB; PI-RS took them at 1.4 dB of level and keeps the half words)
PI_STRUCT_DEFINES = {'pi_sd': ('MD_PD_WHOLE', 'MD_PI_HALF'), 'pi_rs': ('MD_PI_HALF',),
                     'pi_rc': ('MD_PI_HALF',), 'pi_cc': ('MD_PI_HALF',), 'pi_hh': ('MD_PI_HALF',)}   # MD_PI_HALF: the whole voice at 22.05 kHz (native/pi_sd_half_*.c)
SPECTRUM = {'fm', 'struct'}
# Voices approved by ear past the spectrum test's limits (level 1.5 dB, bands 12 dB): their own,
# just over what they read over the full test's 60 scenarios, so that any change that makes them
# worse still fails. PI-RC, CC and HH at half rate (MD_PI_HALF): the band around 10 kHz, their
# strongest, is the one half rate loses; the user found the A/B renders right (2026-11-20).
SPECTRUM_LIMITS = {'pi_rc': (5.0, 18.5), 'pi_cc': (5.0, 21.0), 'pi_hh': (3.5, 26.0)}
# Under 'acc', the products that stay exact (md_acc_*_x): a voice's coefficients, made once a
# block (their frequencies and dampings took the approximation at -60 to -90 dBFS), and an
# address made from A0 (metal_activate). Functions by name (a suffix), and statements.
ACC_EXACT_FUNCS = ('_modal_coefficients', 'drum_coefficients', 'metal_activate')
ACC_EXACT_TEXT = (r'mdkit_mac\(mdkit_mpy\(complement,',            # PI-MT's interpolated coefficients
                  r'input = md_acc_mac_diff\(input, md_s\(s->hammer\)')   # PI-BD's hammer filter (-76 dBFS)


def voice_source(voice: str) -> str:
    """the voice's C file in the mod folder"""
    return HAND[voice][0] if voice in HAND else 'audio/voices/mdkit_%s.c' % voice


# Voices whose accumulator helpers are named <prefix>_mid, <prefix>_add... (not md_/mdkit_)
HELPER_PREFIX = {'trx_xt': 'xt', 'trx_xc': 'xc', 'trx_b2': 'b2'}

# Per voice: functions replaced by a hand-written version in audio/native/ that computes
# the same words in plain 32-bit integers (each file says why it is the same; the voice
# test proves it). -DMD_ST_NO_NATIVE builds mdkit's own function instead.
# An entry is (function, file) or (function, file, extra): extra is a number (a file with
# several functions picks one by MD_CLOCK_PART) or text put before the #include.
PI_CLOCK = '#define MD_CLOCK_PART %d\n#define MD_PI_EVENTS mdkit_%s_events\n#define MD_PI_DEFAULTS mdkit_%s_defaults'
RS_TYPES = '#define MD_PI_T mdkit_pi_rs\n#define MD_PI_MODAL_T mdkit_pi_rs_modal'   # pi_modes.c, pi_delays.c for PI-RS
METAL = '#define MD_METAL_PART %d\n#define MD_METAL(n) %s_##n'


# PI-BD's excitation, coefficients and modes stay exact under 'acc' and 'mid' (its long low
# modes amplify the read's error to -72 dBFS); under 'struct', judged by its spectrum, they do not
PI_BD_EXACT = '\n#ifndef MD_AP_STRUCT\n#define MD_%s_EXACT 1\n#endif'


def pi_excitation(v: str) -> tuple:
    """a PI voice's modal excitation from native/pi_excitation.c (the same code in each)"""
    base, count, sample = PI_SAMPLES[v]
    return ('mdkit_%s_modal_excitation' % v, 'pi_excitation.c',
            '#define MD_EXC mdkit_%s_modal_excitation\n#define MD_EXC_T mdkit_%s_modal\n#define MD_EXC_X mdkit_%s_modal_x'
            '\n#define MD_EXC_BASE 0x%xu\n#define MD_EXC_COUNT %du\n#define MD_EXC_SAMPLE %s' % (v, v, v, base, count, sample)
            + ('\n#define MD_EXC_GAP 0x%xu\n#define MD_EXC_GAP_COUNT 0x%xu' % (PI_SAMPLE_GAP[v][0], PI_SAMPLE_GAP[v][1] - PI_SAMPLE_GAP[v][0]))
            + (PI_BD_EXACT % 'EXC' if v == 'pi_bd' else ''))   # PI-BD: exact (its long low modes)


def pi_coefficients(v: str) -> tuple:
    """a PI voice's modal coefficients from native/pi_coefficients.c (the same code in each)"""
    return ('mdkit_%s_modal_coefficients' % v, 'pi_coefficients.c',
            '#define MD_COEF mdkit_%s_modal_coefficients\n#define MD_COEF_T mdkit_%s_modal\n'
            '#define MD_COEF_X mdkit_%s_modal_x\n#define MD_COEF_Y mdkit_%s_modal_y\n#define MD_COEF_DAMP %s_damp'
            % (v, v, v, v, v.split('_')[1])
            + (PI_BD_EXACT % 'COEF' if v == 'pi_bd' else ''))   # PI-BD: exact (its long low modes)


def pi_modal(v: str, extra: str = '') -> tuple:
    """a PI voice's modal filter from native/pi_modal_filter.c (the same code in each)"""
    return ('mdkit_%s_modal_filter' % v, 'pi_modal_filter.c',
            '#define MD_MODAL mdkit_%s_modal_filter\n#define MD_MODAL_T mdkit_%s_modal' % (v, v) + extra)


def pi_drum_coefficients(v: str) -> tuple:
    """PI-SD's and PI-RS's drum_coefficients from native/pi_drum_coefficients.c"""
    return ('drum_coefficients', 'pi_drum_coefficients.c',
            '#define MD_PI_T mdkit_%s\n#define MD_PI_X mdkit_%s_modal_x\n#define MD_PI_Y mdkit_%s_modal_y' % (v, v, v))

# a PI voice's sample table in its ROM: (first address, words, the voice's reader)
PI_SAMPLES = {'pi_bd': (0x142291, 1480, 'bd_sample'), 'pi_mt': (0x143691, 1676, 'mt_sample'),
              'pi_ml': (0x143f50, 2682, 'ml_sample')}
# the ROM's empty words past each sample (its reader gives 0 there up to the next table):
# every read after the hit's end falls in them
PI_SAMPLE_GAP = {'pi_bd': (0x142859, 0x142949), 'pi_mt': (0x143d1d, 0x145000), 'pi_ml': (0x1449ca, 0x145000)}


def metal(v: str, *parts: int) -> list[tuple]:
    """a TRX voice's metal and CP filter stages from native/trx_metal.c (the same code in
    CY, CH, OH, CB; MA, CP)"""
    names = {0: 'metal_source', 1: 'metal_resonant_lowpass', 2: 'metal_difference_cascade', 3: 'metal_difference_filter',
             4: 'cp_filter6', 5: 'cp_filter4'}
    return [('%s_%s' % (v, names[q]), 'trx_metal.c', METAL % (q, v)) for q in parts]


NATIVE = {
    'trx_cy': metal('trx_cy', 0, 1, 2),
    'trx_ch': metal('trx_ch', 0, 1, 3),
    'trx_oh': metal('trx_oh', 0, 1, 3),
    'trx_cb': metal('trx_cb', 2),
    'trx_ma': metal('trx_ma', 4) + [('trx_ma_ma_block', 'trx_ma_block.c')],
    'trx_cl': [('trx_cl_cl_filter', 'trx_cl_filter.c'), ('trx_cl_cl_scan', 'trx_cl_scan.c')],
    'pi_rc': [('mdkit_pi_rc_modal_wrap', 'pi_wrap.c', '#define MD_WRAP mdkit_pi_rc_modal_wrap'),
              ('metal_advance', 'pi_metal_advance.c'),
              ('metal_filter', 'pi_metal.c', '#define MD_PM_PART 0\n#define MD_PM_T mdkit_pi_rc'),
              ('metal_delay_read', 'pi_metal.c', '#define MD_PM_PART 1\n#define MD_PM_T mdkit_pi_rc'),
              ('metal_delay_write', 'pi_metal.c', '#define MD_PM_PART 2\n#define MD_PM_T mdkit_pi_rc'),
              pi_modal('pi_rc')] + [(f, 'pi_metal.c', '#define MD_PM_PART %d\n#define MD_PM_T mdkit_pi_rc' % n)
                                    for n, f in ((3, 'metal_coupling'), (4, 'metal_amplitude'), (5, 'metal_output'))],
    'pi_cc': [('mdkit_pi_cc_modal_wrap', 'pi_wrap.c', '#define MD_WRAP mdkit_pi_cc_modal_wrap'),
              ('metal_advance', 'pi_metal_advance.c'),
              ('metal_filter', 'pi_metal.c', '#define MD_PM_PART 0\n#define MD_PM_T mdkit_pi_cc'),
              ('metal_delay_read', 'pi_metal.c', '#define MD_PM_PART 1\n#define MD_PM_T mdkit_pi_cc'),
              ('metal_delay_write', 'pi_metal.c', '#define MD_PM_PART 2\n#define MD_PM_T mdkit_pi_cc'),
              pi_modal('pi_cc')] + [(f, 'pi_metal.c', '#define MD_PM_PART %d\n#define MD_PM_T mdkit_pi_cc' % n)
                                    for n, f in ((3, 'metal_coupling'), (4, 'metal_amplitude'), (5, 'metal_output'))],
    'pi_hh': [('mdkit_pi_hh_modal_wrap', 'pi_wrap.c', '#define MD_WRAP mdkit_pi_hh_modal_wrap'),
              ('metal_advance', 'pi_metal_advance.c'),
              ('metal_filter', 'pi_metal.c', '#define MD_PM_PART 0\n#define MD_PM_T mdkit_pi_hh'),
              ('metal_delay_read', 'pi_metal.c', '#define MD_PM_PART 1\n#define MD_PM_T mdkit_pi_hh'),
              ('metal_delay_write', 'pi_metal.c', '#define MD_PM_PART 2\n#define MD_PM_T mdkit_pi_hh'),
              pi_modal('pi_hh')] + [(f, 'pi_metal.c', '#define MD_PM_PART %d\n#define MD_PM_T mdkit_pi_hh' % n)
                                    for n, f in ((3, 'metal_coupling'), (4, 'metal_amplitude'), (5, 'metal_output'))],
    'pi_bd': [('mdkit_pi_bd_modal_wrap', 'pi_wrap.c', '#define MD_WRAP mdkit_pi_bd_modal_wrap'),
              pi_modal('pi_bd', PI_BD_EXACT % 'MODAL'),     # its long low modes take no 'acc' (-72 dBFS)
              pi_excitation('pi_bd'), ('bd_block', 'pi_bd_block.c')],
    'pi_mt': [('mdkit_pi_mt_modal_wrap', 'pi_wrap.c', '#define MD_WRAP mdkit_pi_mt_modal_wrap'),
              pi_modal('pi_mt'),
              pi_excitation('pi_mt'), ('mt_block', 'pi_mt_block.c')],
    'pi_ml': [('mdkit_pi_ml_modal_wrap', 'pi_wrap.c', '#define MD_WRAP mdkit_pi_ml_modal_wrap'),
              pi_modal('pi_ml'),
              pi_excitation('pi_ml'), ('ml_block', 'pi_ml_block.c')],
    'pi_ma': [('mdkit_pi_ma_modal_wrap', 'pi_wrap.c', '#define MD_WRAP mdkit_pi_ma_modal_wrap'),
              pi_modal('pi_ma')],
    'pi_rs': [('mdkit_pi_rs_modal_wrap', 'pi_wrap.c', '#define MD_WRAP mdkit_pi_rs_modal_wrap'),
              pi_modal('pi_rs'),
              ('drum_modes', 'pi_modes.c', RS_TYPES), ('drum_delays', 'pi_delays.c', RS_TYPES), pi_drum_coefficients('pi_rs'),
              ('rs_block', 'pi_rs_block.c')],
    'trx_rs': [('trx_rs_rs_filter', 'trx_rs_filter.c')],
    'trx_b2': [('b2_control_clock_seed', 'trx_b2_clock.c', 0), ('b2_controls', 'trx_b2_clock.c', 1),
               ('b2_boundary', 'trx_b2_clock.c', 2)],
    'trx_sd': [('trx_sd_sd_noise_filter', 'trx_sd_noise.c'), ('trx_sd_sd_tone', 'trx_sd_tone.c'), ('trx_sd_md_control_clock_seed', 'trx_sd_clock.c', 0),
               ('trx_sd_sd_boundary', 'trx_sd_clock.c', 1), ('trx_sd_sd_controls', 'trx_sd_clock.c', 2),
               ('trx_sd_sd_block', 'trx_sd_block.c')],
    'trx_cp': [(n, f, e + ('\n#define MD_CP6_EXACT 1' if 'PART 4' in e else '')) for n, f, e in metal('trx_cp', 4, 5)],   # cp_filter6: exact floor
    'trx_bd': [('trx_bd_bd_run_block', 'trx_bd.c'),
               ('trx_bd_md_control_clock_seed', 'trx_bd_clock.c', 0),
               ('trx_bd_md_trx_events_boundary', 'trx_bd_clock.c', 1),
               ('trx_bd_trx_bd_packet', 'trx_bd_clock.c', 2)],
    'efm_sd': [('efm_clock_boundary', 'efm_clock.c'), ('md_st_efm_sd_render_words', 'efm_sd.c')],
    'efm_cp': [('efm_clock_boundary', 'efm_clock.c'), ('md_st_efm_cp_render_words', 'efm_cp.c')],
    'efm_hh': [('efm_clock_boundary', 'efm_clock.c'), ('hh_resonance', 'efm_hh.c', '#define MD_EFM_HH_PART 0'),
               ('hh_core', 'efm_hh.c', '#define MD_EFM_HH_PART 1')],
    'efm_cb': [('efm_clock_boundary', 'efm_clock.c'), ('cb_core', 'efm_cb.c')],
    'efm_rs': [('efm_clock_boundary', 'efm_clock.c'), ('md_st_efm_rs_render_words', 'efm_rs.c')],
    'efm_cy': [('efm_clock_boundary', 'efm_clock.c'), ('md_st_efm_cy_render_words', 'efm_cy.c')],
    'efm_xt': [('efm_clock_boundary', 'efm_clock.c'), ('xt_core', 'efm_xt.c', '#define MD_EFM_XT_PART 0'),
               ('xt_tail', 'efm_xt.c', '#define MD_EFM_XT_PART 1')],
    'pi_sd': [('mdkit_pi_sd_modal_wrap', 'pi_wrap.c', '#define MD_WRAP mdkit_pi_sd_modal_wrap'),
              ('drum_modes', 'pi_modes.c'), ('drum_delays', 'pi_delays.c'), pi_drum_coefficients('pi_sd'),
              ('mdkit_events_init', 'pi_clock.c', PI_CLOCK % (0, 'pi_sd', 'pi_sd')),
              ('mdkit_events_boundary', 'pi_clock.c', PI_CLOCK % (1, 'pi_sd', 'pi_sd')),
              ('mdkit_pi_sd_packet', 'pi_sd_packet.c'), ('sd_block', 'pi_sd_block.c')],
}
# PI-RC, CC and HH: their block, at half rate under MD_PI_HALF (native/pi_metal_half.c)
for _v in ('pi_rc', 'pi_cc', 'pi_hh'):
    NATIVE[_v].append(('block', 'pi_%s_half_block.c' % _v[3:], '#define MD_PM_T mdkit_%s' % _v))
for _v in ('pi_bd', 'pi_sd', 'pi_mt', 'pi_ml', 'pi_ma', 'pi_rs', 'pi_rc', 'pi_cc', 'pi_hh'):
    NATIVE[_v] = NATIVE.get(_v, []) + [pi_coefficients(_v)]

OPS = ('s56', 'mid', 'low', 'from24', 'from48', 'to48', 'setmid', 'add', 'sub', 'asl', 'asr',
       'mpy_su', 'mpyr', 'mpy', 'macr', 'mac', 'and_mid', 'pack48', 'acc_to_word')
HELPER = re.compile(r'static\s+inline\s+(?:__attribute__\(\(unused\)\)\s+)?(?P<ret>u?int(?:32|64)_t)\s+'
                    r'(?P<name>\w+)\s*\((?P<params>[^)]*)\)\s*\{')


def helper_op(name: str, prefix: str | None = None) -> str | None:
    """the helper a name is (the longest op it ends with, after an md_/mdkit_ prefix or the
    voice's own)"""
    pre = 'md|mdkit' + ('|' + re.escape(prefix) if prefix else '')
    for op in sorted(OPS, key=len, reverse=True):
        if name.endswith('_' + op) and re.search(r'(?:^|_)(?:%s)$' % pre, name[:-len(op) - 1]): return op
    return None


def body_end(s: str, brace: int) -> int:
    """index just past the } matching the { at brace"""
    depth = 0
    for i in range(brace, len(s)):
        if s[i] == '{': depth += 1
        elif s[i] == '}':
            depth -= 1
            if not depth: return i + 1
    raise ValueError('unbalanced braces')


def definition(s: str, func: str) -> tuple[int, int, str]:
    """start, end and signature of func's definition"""
    m = re.search(r'(?m)^[ \t]*(?:static\s+)?(?:inline\s+)?(?:__attribute__\(\(unused\)\)\s+)?[\w ]*?\b' + func +
                  r'\s*\(([^)]*)\)\s*\{', s)
    if not m: raise ValueError('no definition of ' + func)
    start, end = m.start(), body_end(s, m.end() - 1)
    # a return type on the line before ("static inline unsigned\nname(...)") belongs to it
    before = s.rfind('\n', 0, max(start - 1, 0)) + 1
    if start and re.fullmatch(r'[ \t]*(?:static\s+)?(?:inline\s+)?(?:__attribute__\(\(unused\)\)\s+)?[\w \t*]+', s[before:start - 1]):
        start = before
    return start, end, s[start:m.end() - 1].rstrip()


def port_helpers(s: str, voice: str) -> str:
    out, at, seen = [], 0, set()
    for m in HELPER.finditer(s):
        op = helper_op(m.group('name'), HELPER_PREFIX.get(voice))
        if not op: continue
        params = [p.strip() for p in m.group('params').split(',') if p.strip()]
        names = [p.split()[-1] for p in params]
        end = body_end(s, m.end() - 1)
        out.append(s[at:m.start()])
        sig = s[m.start():m.end() - 1].rstrip()
        out.append('%s\n{ return md_acc_%s(%s); }  /* Synfact: md-acc.h */' % (sig, op, ', '.join(names)))
        at = end; seen.add(op)
    out.append(s[at:])
    s = ''.join(out)
    # A word's sign extension is md_s (two shifts), not signed_bits' 64-bit masks: the same value.
    s = re.sub(r'(int64_t \w*s24\(uint32_t (\w+)\)\s*\{\s*return )\w*signed_bits\(\s*\2\s*&\s*\w+\s*,\s*24\s*\)\s*;',
               r'\1md_s(\2); /* Synfact: md-native.h */', s)
    # A 48-bit literal is a plain number, not the helpers' two-word form.
    pre = 'md|mdkit' + ('|' + re.escape(HELPER_PREFIX[voice]) if voice in HELPER_PREFIX else '')
    s = re.sub(r'\b\w*?(?:' + pre + r')_from48\(\s*((?:UINT64_C\()?(?:0x[0-9a-fA-F]+|\d+)[uUlL]*\)?)\s*\)',
               r'md_acc_from48_raw(\1) /* Synfact: a literal */', s)
    # A voice's own rounding on the raw accumulator: clearing bit 24 (A1's lowest) is clearing
    # bit 0 of the high word t here (A0 is the low 24 bits, so 2^23 and the A0 mask stay as they
    # are). Any other raw bit at 24 or above would mean something else here: refuse it.
    s = re.sub(r'\b(\w+)\s*&=\s*~\(UINT64_C\(1\)\s*<<\s*24\);',
               r'\1 = md_acc_make((int32_t)((uint32_t)MD_T(\1) & ~1u), MD_L(\1));  /* Synfact: bit 24 is A1 bit 0 */', s)
    body = re.sub(r'(?s)static\s+inline[^{]*\{[^{}]*\}', '', s)          # the helpers themselves aside
    if re.search(r'UINT64_C\(1\)\s*<<\s*(?:2[4-9]|[3-5][0-9])', body):
        raise ValueError(voice + ': a raw accumulator bit at 24 or above outside the helpers')
    if not {'mid', 'add', 'mpy', 'acc_to_word'} <= seen:
        raise ValueError(voice + ': accumulator helpers not found (%s)' % sorted(seen))
    return s


# Voices whose float control clock port_clock rewrites in integers (TRX-BD, TRX-SD and
# TRX-B2 have theirs in audio/native/)
INT_CLOCK = ('trx_xt', 'trx_cp', 'trx_rs', 'trx_cb', 'trx_ch', 'trx_oh', 'trx_cy', 'trx_ma', 'trx_cl', 'trx_xc',
             'pi_bd', 'pi_mt', 'pi_ml', 'pi_ma', 'pi_rs', 'pi_rc', 'pi_cc', 'pi_hh')
CLOCK_RULES = [            # \b(\w+)-> is the clock's variable (c, trx_rs_c...)
    # the seed: target = the knob, the others 128 times it
    (r'const float v\s*=\s*\(float\)(\w+)\[i\];\s*\b(\w+)->target\[i\]\s*=\s*\2->raw\[i\]\s*=\s*\2->current\[i\]\s*=\s*\2->pending\[i\]\s*=\s*v;',
     r'MD_CLK(\2->target)[i] = \1[i]; MD_CLK(\2->raw)[i] = MD_CLK(\2->current)[i] = MD_CLK(\2->pending)[i] = (uint32_t)\1[i] * 128u;'),
    # (3 raw + 128 target) / 4, then / 128 as a float: the integer itself
    (r'\b(\w+)->raw\[i\]\s*=\s*\(float\)\(\(3\s*\*\s*(\w+)\s*\+\s*128\s*\*\s*\(uint32_t\)\1->target\[i\]\)\s*/\s*4\)\s*/\s*128(?:\.0f)?',
     r'MD_CLK(\1->raw)[i]=(3*\2+128*MD_CLK(\1->target)[i])/4'),
    (r'\b(\w+)->target\[i\]\s*=\s*\(float\)(\w+(?:->\w+)?\[i\])', r'MD_CLK(\1->target)[i]=\2'),
    (r'\(uint32_t\)\b(\w+)->target\[i\]', r'MD_CLK(\1->target)[i]'),
    # 128 times a value: the stored integer
    (r'\((?:uint32_t|unsigned)\)\(\s*128(?:\.0f)?\s*\*\s*([\w>.-]+(?:\[\w+\]))\s*\)', None),
    (r'\((?:uint32_t|unsigned)\)\(\s*([\w>.-]+(?:\[\w+\]))\s*\*\s*128(?:\.0f)?\s*\)', None),
    # a local k[] of knob values handed to a packet
    (r'\bk\[i\]\s*=\s*\(float\)\(raw\[i\]>>7\)', 'MD_CLK(k)[i]=(raw[i]>>7)*128u'),
    # (unsigned)k[X]: the knob
    (r'\(unsigned\)k\[(\w+)\]', r'(MD_CLK(k)[\1]>>7)'),
]


def port_clock(s: str, voice: str) -> str:
    """the voice's control clock in integers (INT_CLOCK, point 5 above)"""
    if voice not in INT_CLOCK: return s
    def stored(m):
        e = m.group(1)
        arr, idx = re.match(r'(.*)\[(\w+)\]$', e).groups()
        return 'MD_CLK(%s)[%s]' % (arr, idx)
    n = 0
    for pat, sub in CLOCK_RULES:
        s, k = re.subn(pat, stored if sub is None else sub, s)
        n += k
    if not n: raise ValueError(voice + ': no float clock to rewrite')
    first = s.index('#include "md-acc.h"')
    s = s[:first] + ('/* Synfact: the control clock in integers (tools/md_port.py, port_clock) */\n'
                     'typedef uint32_t __attribute__((may_alias)) md_clk_u32;\n'
                     '#define MD_CLK(f) ((md_clk_u32 *)(uintptr_t)(f))\n') + s[first:]
    return s


PI_DELAY_READ = 'if(address>=0x130000&&address<0x140000)return s->delay[address-0x130000];'
PI_DELAY_WRITE = 'else if(address>=0x130000&&address<0x140000)s->delay[address-0x130000]=word;'
PI_DELAY_INIT = 'for(unsigned i=0;i<65536u;++i)s->delay[i]=0;'
PI_DELAY_IN = ('\n/* Synfact: the 1,024 delay words the voice uses (tools/md_port.py, EDITS) */\n'
               '#if defined(MD_ST_TABLES) && defined(MD_ST_DOMAIN_CHECK)\n'
               '#define MD_PI_DELAY_IN(a) ((a) - 0x135600u < 1024u || (md_st_domain_fail("pi_delay", (a)), 0))\n'
               '#else\n#define MD_PI_DELAY_IN(a) ((a) - 0x135600u < 1024u)\n#endif\n')
PI_DELAY = {
    'h': [('delay[65536]', 'delay[1024] /* Synfact: the words at 0x135600 the voice uses */')],
    'c': [(PI_DELAY_READ, 'if(address>=0x130000&&address<0x140000)return MD_PI_DELAY_IN(address)?s->delay[address-0x135600u]:0u;'),
          (PI_DELAY_WRITE, 'else if(address>=0x130000&&address<0x140000){if(MD_PI_DELAY_IN(address))s->delay[address-0x135600u]=word;}'),
          (PI_DELAY_INIT, 'for(unsigned i=0;i<1024u;++i)s->delay[i]=0;'),
          # one copy each: inlined at every call they were ~1.5 KB of code a block run
          # about 300 instructions, each 16-byte line a cache miss (WORK.md)
          ('static inline uint32_t metal_read(', 'static __attribute__((noinline)) uint32_t metal_read('),
          ('static inline void metal_write(', 'static __attribute__((noinline)) void metal_write('),
          ('static inline uint32_t metal_product(', 'static __attribute__((noinline)) uint32_t metal_product(')]}
# PI-SD and PI-RS: the controls modes 4 to 7's coefficients were last made from
# (native/pi_drum_coefficients.c); init's zeroing leaves them unmade
PI_COEF_KEY = {
    'h': [(' uint32_t block[32];\n', ' uint32_t block[32];\n'
           ' uint32_t coef_key[4];   /* Synfact: valid, PTCH, DEC, the eighth parameter (native/pi_drum_coefficients.c) */\n')]}
# PI-MT: the interpolated mode tables last made, and the controls they were made from
# (native/pi_mt_block.c); init's zeroing leaves them unmade
PI_MT_TABLES = {
    'h': [(' uint32_t block[32];\n', ' uint32_t block[32];\n'
           ' uint32_t table_key[3], tables[24];   /* Synfact: valid, POS, TUNE; the words (native/pi_mt_block.c) */\n')]}
# PI-SD: its ROM read once (inlined at each call it was ~370 B of code a block)
PI_SD_EDITS = {'h': PI_COEF_KEY['h'],
               'c': [('\nstatic uint32_t sd_rom(uint32_t address)\n',
                      '\nstatic uint32_t sd_rom(uint32_t address) __attribute__((noinline));\nstatic uint32_t sd_rom(uint32_t address)\n')]}
EDITS = {'pi_cc': PI_DELAY, 'pi_hh': PI_DELAY, 'pi_rc': PI_DELAY, 'pi_sd': PI_SD_EDITS, 'pi_rs': PI_COEF_KEY, 'pi_mt': PI_MT_TABLES}


def port_edits(text: str, voice: str, part: str) -> str:
    """the voice's EDITS (point 7 above): each old text once, exactly"""
    for old, new in EDITS.get(voice, {}).get(part, []):
        if text.count(old) != 1: raise ValueError('%s: edit text not once: %s' % (voice, old[:50]))
        text = text.replace(old, new)
    if part == 'c' and EDITS.get(voice) is PI_DELAY:
        first = text.index('#include "md-acc.h"')
        text = text[:first] + PI_DELAY_IN.lstrip('\n') + text[first:]
    return text


def port_tables(s: str, voice: str) -> str:
    for t in VOICES[voice]:
        if t.get('only'): continue
        if 'text' in t:
            if s.count(t['text']) != 1: raise ValueError('%s: the text of %s is not once in the voice' % (voice, t['name']))
            look = t['look'].format(t='md_' + t['name'], i='k_')
            s = s.replace(t['text'], '\n#ifdef MD_ST_TABLES\n/* Synfact: computed on the host by this same code (md-tables.c) */\n'
                          '{ unsigned k_ = (unsigned)(%s); MD_ST_DOMAIN(k_ < %du, "%s", k_); %s }\n#else\n%s\n#endif\n'
                          % (t['index'], t['n'], t['name'], look, t['text']))
            continue
        start, end, sig = definition(s, t['func'])
        a = t['arg']
        dom = 'MD_ST_DOMAIN(%%s, "%s", %s);\n    ' % (t['name'], a)
        if t.get('kind') == 'sine':
            look = 'return md_sine_word(&md_%s, %s);' % (t['name'], a)
        elif 'key' in t:
            look = ('unsigned k_ = (unsigned)(%s);\n    ' % t['key'] + dom % ('k_ < %du' % t['n']) +
                    'return k_ < %du ? md_%s[k_] : 0u;' % (t['n'], t['name']))
        elif 'n' in t:
            look = dom % ('(%s) < %du' % (a, t['n'])) + 'return (%s) < %du ? md_%s[%s] : 0u;' % (a, t['n'], t['name'], a)
        else:
            ok = 'at_ - 0x%xu < 0x%xu' % (t['outside'][0], t['outside'][1] - t['outside'][0]) if 'outside' in t else '0'
            look = ('uint32_t at_ = (uint32_t)(%s) & 0xffffffu;\n    ' % a +
                    ('if (at_ < 0x%xu) { %s }\n    ' % (t['lo'], t['below']) if 'below' in t else '') +
                    dom % ('(at_ - 0x%xu < 0x%xu) || %s' % (t['lo'], t['hi'] - t['lo'], ok)) +
                    'return at_ >= 0x%xu && at_ < 0x%xu ? md_%s[at_ - 0x%xu] : 0u;' % (t['lo'], t['hi'], t['name'], t['lo']))
        if t.get('calls'):
            calls = [m.start() for m in re.finditer(r'\b' + t['func'] + r'\s*\(', s)]
            calls = [c for c in calls if not start <= c < end]
            if not calls or any(not s.startswith(t['calls'], c) for c in calls):
                raise ValueError('%s: a call of %s is not %s...' % (voice, t['func'], t['calls']))
        orig = s[start:end]
        s = s[:start] + ('#ifdef MD_ST_TABLES\n/* Synfact: computed on the host by this same code (md-tables.c) */\n'
                         '%s\n{\n    %s\n}\n#else\n%s\n#endif' % (sig, look, orig)) + s[end:]
    return s


def descriptor_render(s: str, voice: str) -> tuple[str, str]:
    m = re.search(r'const\s+mdkit_voice\s+mdkit_voice_' + voice + r'\s*=\s*\{', s)
    if not m: raise ValueError(voice + ': no descriptor')
    end = body_end(s, m.end() - 1)
    fields = [f.strip() for f in re.sub(r'\s+', ' ', s[m.end():end - 1]).split(',')]
    render = fields[8]
    if not re.fullmatch(r'\w+', render): raise ValueError(voice + ': render field ' + render)
    body = s[m.start():end]
    new = re.sub(r'\b' + render + r'\b', 'MD_ST_RENDER(' + render + ')', body)
    return s[:m.start()] + new + s[end:], render


def port_render(s: str, voice: str) -> str:
    s, render = descriptor_render(s, voice)
    start, end, sig = definition(s, render)
    orig = s[start:end]
    words = orig[len(sig):]
    words = re.sub(r'\(float\)', '(int32_t)', words)
    words = re.sub(r'\s*/\s*8388608(?:\.0f?)?\b', '', words)
    words = re.sub(r'\b0\.0f\b', '0', words)
    # the PI voices' copy: the trigger tested once a block, not a word
    words = re.sub(r'for\s*\(\s*unsigned\s+i\s*=\s*0\s*;\s*i\s*<\s*n\s*;\s*\+\+i\s*\)\s*out\[i\]\s*=\s*s->events\.triggered\s*\?\s*'
                   r'\(int32_t\)mdkit_s24\(s->block\[s->pos \+ i\]\)\s*:\s*0\s*;',
                   'if (s->events.triggered) for (unsigned i = 0; i < n; ++i) out[i] = md_s(s->block[s->pos + i]);\n'
                   '    else for (unsigned i = 0; i < n; ++i) out[i] = 0;  /* Synfact: the trigger tested once */', words)
    if 'float' in words or '8388608' in words or re.search(r'\d\.\d', words):
        raise ValueError(voice + ': render keeps float arithmetic')
    new_sig = re.sub(r'float\s*\*\s*out', 'int32_t *out', sig)
    new_sig = re.sub(r'^\s*static\s+', '', new_sig)
    new_sig = re.sub(r'\b' + render + r'\b', 'md_st_%s_render_words' % voice, new_sig)
    if new_sig == sig or 'int32_t *out' not in new_sig: raise ValueError(voice + ': render signature ' + sig)
    return (s[:start] + '#ifndef MD_ST_TARGET\n' + orig + '\n#endif\n'
            '/* Synfact: the same render, the words themselves */\n' + new_sig + words + s[end:])


# x = mac(x, a, k); x = sub(x, mpy(p, k)): x + 2 k a - 2 k p = x + 2 k (a - p) exactly
# (56-bit sums, no limit between), one product of a 25-bit difference (md_acc_mac_diff)
MAC_DIFF = re.compile(r'(\w+)\s*=\s*mdkit_mac\(\s*\1\s*,\s*([^,;()]+?)\s*,\s*([^;()]+?)\s*\);\s*'
                      r'\1\s*=\s*mdkit_sub\(\s*\1\s*,\s*mdkit_mpy\(\s*([^,;()]+?)\s*,\s*\3\s*\)\s*\);')


def port_mac_diff(s: str, voice: str) -> str:
    if not voice.startswith('pi_'): return s
    return MAC_DIFF.sub(lambda m: '%s = md_acc_mac_diff(%s, md_s(%s), md_s(%s), md_s(%s)); '
                        '/* Synfact: mac then sub mpy, one product (md_port.py, MAC_DIFF) */'
                        % (m.group(1), m.group(1), m.group(3), m.group(2), m.group(4)), s)


def port_acc_exact(s: str, voice: str) -> str:
    if 'acc' not in APPROX.get(voice, ()): return s
    exact = lambda t: re.sub(r'\b(?:mdkit|md_acc)_(mpy_su|mpy|macr|mac_diff|mac)\(', r'md_acc_\1_x(', t)
    names = sorted({m.group(1) for m in re.finditer(r'(?m)^[ \t]*(?:static\s+)?(?:inline\s+)?(?:__attribute__\(\(unused\)\)\s+)?'
                                                    r'[\w ]*?\b(\w+)\s*\([^;{]*\)\s*\{', s)
                    if m.group(1).endswith(ACC_EXACT_FUNCS)})
    for func in names:
        start, end, sig = definition(s, func)
        body = s[start:end]
        s = s[:start] + body[:len(sig)] + exact(body[len(sig):]) + s[end:]
    for pat in ACC_EXACT_TEXT:
        s = re.sub(pat, lambda m: exact(m.group(0)), s)
    return s


def port_native(s: str, voice: str) -> str:
    for entry in NATIVE.get(voice, []):
        func, file = entry[:2]
        extra = entry[2] if len(entry) > 2 else None
        part = '' if extra is None else ('#define MD_CLOCK_PART %d\n' % extra if isinstance(extra, int) else extra + '\n')
        if not (MOD / 'audio' / 'native' / file).is_file(): raise ValueError('%s: no audio/native/%s' % (voice, file))
        start, end, sig = definition(s, func)
        s = (s[:start] + '#ifndef MD_ST_NO_NATIVE\n/* Synfact: %s in plain 32-bit integers, the same words */\n'
             '%s#include "../native/%s"\n#else\n%s\n#endif' % (func, part, file, s[start:end]) + s[end:])
    return s


def port(voice: str) -> dict[str, str]:
    family, name = voice.split('_', 1)
    src = MDKIT / 'src' / family / name
    c = (src / ('mdkit_%s.c' % voice)).read_text()
    h = (src / ('mdkit_%s.h' % voice)).read_text()
    note = ('\n * Synfact: generated from mdkit/src/%s/%s by tools/md_port.py; do not edit.\n'
            ' * The changes are listed there, and marked "Synfact" here.\n */' % (family, name))
    c = re.sub(r'\*/', note, c, count=1)
    h = re.sub(r'\*/', note, h, count=1)
    for f in ('stdint.h', 'stddef.h'):
        c = c.replace('#include <%s>' % f, '#include "md-types.h"')
        h = h.replace('#include <%s>' % f, '#include "md-types.h"')
    if '#include "md-types.h"' not in h: h = h.replace('#include "mdkit_voice.h"', '#include "md-types.h"\n#include "mdkit_voice.h"')
    h = re.sub(r'(extern const mdkit_voice mdkit_voice_\w+;)',
               r'\1\n/* Synfact: the 24-bit words, sign-extended, at 44.1 kHz */\n'
               'void md_st_%s_render_words(void *state, int32_t *out, unsigned frames);' % voice, h)
    first = c.index('#include "mdkit_%s.h"' % voice)
    line = c.index('\n', first) + 1
    approx = ''.join('#define MD_AP_%s 1\n' % f.upper() for f in APPROX.get(voice, ()))
    if voice in PI_STRUCT:
        approx += ('#ifndef MD_PI_SKIP_MODES\n#define MD_PI_SKIP_MODES 0x%02xu   /* under MD_AP_STRUCT (PI_STRUCT) */\n#endif\n'
                   % sum(1 << m for m in PI_STRUCT[voice]))
        approx += ''.join('#define %s 1\n' % d for d in PI_STRUCT_DEFINES.get(voice, ()))
    if approx: approx = '/* Synfact: the approximate forms this voice takes (tools/md_port.py, APPROX) */\n' + approx
    c = c[:line] + approx + '#include "md-acc.h"\n#ifdef MD_ST_TABLES\n#include "md-tables.h"\n#endif\n' + c[line:]
    c = port_edits(c, voice, 'c')
    h = port_edits(h, voice, 'h')
    c = port_helpers(c, voice)
    c = port_clock(c, voice)
    c = port_tables(c, voice)
    c = port_render(c, voice)
    c = port_mac_diff(c, voice)
    c = port_acc_exact(c, voice)
    c = port_native(c, voice)
    return {'mdkit_%s.c' % voice: c, 'mdkit_%s.h' % voice: h}


def host_cc() -> list[str]:
    cc = os.environ.get('MD_HOST_CC')
    if not cc:
        b = os.environ.get('LLVM_MINGW_BIN')
        if not b: raise SystemExit('set LLVM_MINGW_BIN (docs/SETUP.md) or MD_HOST_CC')
        cc = str(pathlib.Path(b) / 'x86_64-w64-mingw32-clang.exe')
    return [cc, '-std=c99', '-O1', '-ffp-contract=off', '-w']


def tables(voices_dir: pathlib.Path, work: pathlib.Path) -> tuple[str, str, dict]:
    """evaluate every recipe table with the voices' own code; return md-tables.c/.h
    and each voice's descriptor facts"""
    found = {}       # table name -> ('sine'|'words', lo, words)
    info = {}
    for voice, recipe in VOICES.items():
        inc = work / ('%s.inc' % voice)
        lines = []
        for t in recipe:
            if t.get('kind') == 'sine': lo, n = 0, 32768
            elif 'n' in t: lo, n = 0, t['n']
            else: lo, n = t['lo'], t['hi'] - t['lo']
            lines.append('T("%s", %du, %du, %s)' % (t['name'], lo, n, t['expr']))
            if 'outside' in t:
                a, b = t['outside']
                lines.append('Z("%s", %du, %du, %du, %du, %s)' % (t['name'], a, b, t['lo'], t['hi'], t['expr']))
            if 'host_check' in t: lines.append(t['host_check'])
            if 'key' in t:
                key = re.sub(r'\b%s\b' % t['arg'], 'x', t['key'])
                lines.append('K("%s", %du, %du, %s, %s(x), %s)' % (t['name'], t['check'][0], t['check'][1], key,
                                                                    t['func'], t['expr']))
        inc.write_text('\n'.join(lines) + '\n')
        exe = work / ('tables-%s.exe' % voice)
        if voice in HAND:            # its descriptor facts from mdkit's own file
            family, name = voice.split('_', 1)
            vc, extra = MDKIT / 'src' / family / name / ('mdkit_%s.c' % voice), ['-I', str(MDKIT / 'include'), '-I', str(MDKIT / 'src' / family / name)]
        else:
            vc, extra = voices_dir / ('mdkit_%s.c' % voice), []
        subprocess.run(host_cc() + ['-DVOICE_C="%s"' % vc.as_posix(),
                                    '-DVOICE_TABLES="%s"' % inc.as_posix(), '-DVOICE_SYMBOL=mdkit_voice_' + voice, '-DMD_ST_NO_NATIVE',
                                    '-I', str(voices_dir)] + extra + [str(HERE / 'md-tables.c'), '-o', str(exe)], check=True)
        r = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
        for line in r.splitlines():
            if line.startswith('voice '):
                _, vname, state, count, *rest = line.split(); n = int(count)
                info[voice] = dict(name=vname, state=int(state), count=n, labels=rest[:n], defaults=[int(x) for x in rest[n:]])
                continue
            name, lo, *words = line.split()
            kind = next(t for t in recipe if t['name'] == name).get('kind', 'words')
            found[name] = (kind, int(lo), [int(w, 16) for w in words])
    # identical tables are kept once
    by_content, alias = {}, {}
    for name in sorted(found):
        key = hashlib.sha256(json.dumps(found[name]).encode()).hexdigest()
        if key in by_content: alias[name] = by_content[key]
        else: by_content[key] = name
    c = ['/* SPDX-License-Identifier: CC0-1.0',
         ' * Generated by mods/md-st/tools/md_port.py from the mdkit voices\' own functions',
         ' * (mdkit, Soejrd, CC0-1.0), evaluated on the host. Do not edit. */',
         '#include "voices/md-tables.h"']
    h = ['/* SPDX-License-Identifier: GPL-2.0-or-later',
         ' * md-tables.h - the tables the Syntakt build of the MD voices reads in place',
         ' * of their double-precision functions. Generated by tools/md_port.py, with',
         ' * audio/md-tables.c; do not edit. */',
         '#ifndef MD_TABLES_H', '#define MD_TABLES_H', '#include "md-types.h"', '',
         '/* A 32768-word sine kept as its first quarter (0..8192), rebuilt by odd',
         ' * symmetry about 16384 and even symmetry about 8192; the words that rebuild',
         ' * misses are the run of fix_count words from index fix_at, all fix_word (the',
         ' * trough, limited to -1.0). The voices\' sines differ only by rounding: one',
         ' * quarter is kept, and another sine is it less the bit of its minus bitmap.',
         ' * The generator checks every word of the rebuild. */',
         '/* Core 2.2, the 4 MiB bank window: the sines in FULL_SINES are also kept whole, 32768',
         ' * signed words (full), read with one load: the same words, no fold, no fix. */',
         'struct md_sine {const uint32_t *quarter; uint32_t fix_at, fix_count, fix_word; const uint8_t *minus; const int32_t *full;};',
         '/* MD_AP_SINE (MD_ST_APPROX), and MD_AP_FM with it: the quarter as it is, 1 LSB */',
         '#if defined(MD_AP_FM) && !defined(MD_AP_SINE)',
         '#define MD_AP_SINE 1',
         '#endif',
         'static inline uint32_t md_sine_word(const struct md_sine *t, unsigned i)',
         '{', '    uint32_t q, v;', '    i &= 32767u;',
         '    if (t->full) return (uint32_t)t->full[i] & 0xffffffu;',
         '#ifndef MD_AP_SINE',
         '    if (i - t->fix_at < t->fix_count) return t->fix_word;',
         '#endif',
         '    q = i & 16383u; q = q <= 8192u ? q : 16384u - q; v = t->quarter[q];',
         '#ifndef MD_AP_SINE',
         '    if (t->minus) v -= (t->minus[q >> 3] >> (q & 7u)) & 1u;',
         '#endif',
         '    return i & 16384u ? (0u - v) & 0xffffffu : v;', '}', '',
         '/* MD_ST_DOMAIN_CHECK (the tests): a lookup outside its table calls md_st_domain_fail */',
         '#ifdef MD_ST_DOMAIN_CHECK',
         'void md_st_domain_fail(const char *table, unsigned argument);',
         '#define MD_ST_DOMAIN(ok, name, a) do { if (!(ok)) md_st_domain_fail(name, (unsigned)(a)); } while (0)',
         '#else', '#define MD_ST_DOMAIN(ok, name, a) ((void)0)', '#endif', '']
    total = 0
    minus = {t['name']: t['minus'] for recipe in VOICES.values() for t in recipe if t.get('minus')}
    minus = {alias.get(n, n): alias.get(m, m) for n, m in minus.items()}
    # the sine every other one is at most one less than, word by word: kept whole
    quarters = {n: sine_parts(n, found[n][2])[0] for n in sorted(found) if n not in alias and found[n][0] == 'sine'}
    base = max(((n, q) for n, q in quarters.items()), key=lambda nq: sum(nq[1]), default=None)
    if base: h.append('extern const uint32_t md_%s_q[8193];' % base[0])
    whole = {t['name'] for recipe in VOICES.values() for t in recipe if t.get('whole')}
    whole = {alias.get(n, n) for n in whole}
    for name in sorted(found):
        if name in alias:
            h.append('#define md_%s md_%s  /* the same words */' % (name, alias[name])); continue
        kind, lo, words = found[name]
        if kind == 'sine':
            q, x = sine_parts(name, words)
            full = ''
            if name in FULL_SINES:
                assert len(words) == 32768, (name, len(words))
                signed = [w - (1 << 24) if w & 0x800000 else w for w in words]
                c.append('const int32_t md_%s_w[32768]={%s\n};' % (name, ''.join(
                    ('\n' if k % 8 == 0 else '') + '%d,' % v for k, v in enumerate(signed))))
                h.append('extern const int32_t md_%s_w[32768];  /* md_%s whole (FULL_SINES) */' % (name, name))
                full = ',md_%s_w' % name; total += 4 * 32768
            if name == base[0] or name in whole or any(not 0 <= b - v <= 1 for b, v in zip(base[1], q)):
                c.append('const uint32_t md_%s_q[8193]={%s\n};' % (name, rows(q)))
                if name != base[0]: h.append('extern const uint32_t md_%s_q[8193];' % name)
                h.append('static const struct md_sine md_%s={md_%s_q,0x%x,%d,0x%x,0%s};' % (name, name, x[0], x[1], x[2], full))
                total += 4 * 8193
            else:
                bits = [0] * 1025
                for k, (b, v) in enumerate(zip(base[1], q)): bits[k >> 3] |= (b - v) << (k & 7)
                c.append('const uint8_t md_%s_minus[1025]={%s\n};' % (name, ''.join(
                    ('\n' if k % 16 == 0 else '') + '0x%02x,' % b for k, b in enumerate(bits))))
                h.append('extern const uint8_t md_%s_minus[1025];  /* md_%s less these bits */' % (name, base[0]))
                h.append('static const struct md_sine md_%s={md_%s_q,0x%x,%d,0x%x,md_%s_minus%s};' % (name, base[0], x[0], x[1], x[2], name, full))
                total += 1025
        elif name in minus:
            ref = found[minus[name]][2]
            sw = lambda w: w - (1 << 24) if w & 0x800000 else w
            diff = [sw(w) - sw(b) for w, b in zip(words, ref)]
            runs = [diff[k:k + 64] for k in range(0, len(diff), 64)]
            bases = [(min(r) + max(r)) // 2 for r in runs]
            offsets = [d - bases[k >> 6] for k, d in enumerate(diff)]
            if any(not -32768 <= b < 32768 for b in bases) or any(not -128 <= o < 128 for o in offsets):
                raise ValueError(name + ': a difference does not fit a 16-bit base and an 8-bit offset')
            c.append('const int16_t md_%s_base[%d]={%s\n};' % (name, len(bases), ''.join(
                ('\n' if k % 16 == 0 else '') + '%d,' % b for k, b in enumerate(bases))))
            c.append('const int8_t md_%s[%d]={%s\n};' % (name, len(offsets), ''.join(
                ('\n' if k % 24 == 0 else '') + '%d,' % o for k, o in enumerate(offsets))))
            h.append('extern const int16_t md_%s_base[%d];' % (name, len(bases)))
            h.append('extern const int8_t md_%s[%d];  /* md_%s\'s words + md_%s_base[i >> 6] + these */'
                     % (name, len(offsets), minus[name], name))
            total += 2 * len(bases) + len(offsets)
        else:
            c.append('const uint32_t md_%s[%d]={%s\n};' % (name, len(words), rows(words)))
            h.append('extern const uint32_t md_%s[%d];' % (name, len(words)))
            total += 4 * len(words)
    h += ['/* %d bytes of tables */' % total, '#endif', '']
    return '\n'.join(c) + '\n', '\n'.join(h), info


# Sines kept whole as well (core 2.2's 4 MiB bank window): 128 KB each, one load a read.
# efm_cb_sine is every EFM voice's but EFM-XT's (efm_cp/cy/hh/rs/sd are its aliases).
FULL_SINES = {'efm_cb_sine'}


def rows(words: list[int]) -> str:
    return ''.join(('\n' if i % 8 == 0 else '') + '0x%06x,' % w for i, w in enumerate(words))


def sine_parts(name: str, full: list[int]) -> tuple[list[int], list[int]]:
    """the quarter and the run of words the rebuild misses (at, count, word), checked word by word"""
    def rebuild(i):
        q = i & 16383
        v = full[q] if q <= 8192 else full[16384 - q]
        return v if i < 16384 else (-v) & 0xffffff
    miss = [i for i in range(32768) if rebuild(i) != full[i]]
    if miss and (miss != list(range(miss[0], miss[0] + len(miss))) or len({full[i] for i in miss}) > 1):
        raise ValueError('%s: the words the sine rebuild misses are not one run of one word' % name)
    fix = [miss[0], len(miss), full[miss[0]]] if miss else [0, 0, 0]
    for i in range(32768):          # the lookup exactly as md_sine_word does it
        v = fix[2] if 0 <= i - fix[0] < fix[1] else rebuild(i)
        if v != full[i]: raise ValueError('%s: rebuild differs at %d' % (name, i))
    return full[:8193], fix


# The MD's budget (main.c): each voice's cost in CPU A, tenths of a point over a track of TwinShot
# (the cache model fitted to the unit's readings, WORK.md, bank 0.42; PI-RC, CC and HH with the 1.7
# the unit read over the fit). A track that is MD replaces a stock machine, about 5.7 points in a
# project of different stock machines; the MD tracks' weights, cost - 5.7, stay within 12.3, what
# such a project leaves under 95 %. A weight is capped at the budget: every voice fits alone.
MD_STOCK_TRACK = 57
MD_BUDGET = 123
VOICE_POINTS = {'trx_bd': 97, 'trx_sd': 119, 'trx_xt': 58, 'trx_cp': 119, 'trx_rs': 109, 'trx_cb': 118, 'trx_ch': 110, 'trx_oh': 110, 'trx_cy': 140, 'trx_ma': 121, 'trx_cl': 99, 'trx_xc': 57, 'trx_b2': 104, 'efm_bd': 82, 'efm_sd': 103, 'efm_xt': 98, 'efm_cp': 90, 'efm_rs': 105, 'efm_cb': 103, 'efm_hh': 98, 'efm_cy': 142, 'pi_bd': 154, 'pi_sd': 171, 'pi_mt': 166, 'pi_ml': 139, 'pi_ma': 65, 'pi_rs': 170, 'pi_rc': 180, 'pi_cc': 185, 'pi_hh': 185}

def voice_lists(info: dict[str, dict]) -> tuple[str, str]:
    """md-voices.h for MAIN (names, labels, defaults) and audio/md-voices.h (the calls)"""
    order = list(VOICES)
    head = ['/* SPDX-License-Identifier: GPL-2.0-or-later',
            ' * Generated by tools/md_port.py from the voices\' mdkit descriptors; do not edit.',
            ' * The order is TYPE\'s: a released order never changes (saved Sounds keep it). */']
    weights = [min(MD_BUDGET, max(0, VOICE_POINTS[v] - MD_STOCK_TRACK)) for v in order]
    budget = ["/* the MD's budget (tools/md_port.py VOICE_POINTS): a voice's weight, tenths of a point of CPU A",
              " * over an average stock track; the MD tracks' weights stay within MD_BUDGET */",
              '#define MD_BUDGET %d' % MD_BUDGET,
              'static const unsigned char md_voice_weight[MD_VOICES]={%s};' % ','.join(str(w) for w in weights)]
    m = head + ['#ifndef MD_VOICES_H', '#define MD_VOICES_H', '#define MD_VOICES %d' % len(order),
                'static const char md_voice_names[MD_VOICES][8]={%s};' % ','.join('"%s"' % info[v]['name'] for v in order),
                'static const unsigned char md_voice_controls[MD_VOICES]={%s};' % ','.join(str(info[v]['count']) for v in order),
                '/* the controls\' labels, "" past a voice\'s last control */',
                'static const char md_voice_labels[MD_VOICES][8][5]={']
    for v in order:
        labels = info[v]['labels'] + [''] * (8 - info[v]['count'])
        m.append('    {%s},' % ','.join('"%s"' % l for l in labels))
    m += ['};', 'static const unsigned char md_voice_defaults[MD_VOICES][8]={']
    for v in order:
        d = info[v]['defaults'] + [0] * (8 - info[v]['count'])
        m.append('    {%s},' % ','.join(str(x) for x in d))
    m += ['};', '/* the first TYPE\'s defaults: the descriptor\'s initial values */']
    m += ['#define MD_FIRST_DEFAULT_%d %d' % (i, (info[order[0]]['defaults'] + [0] * 8)[i]) for i in range(8)]
    m += budget
    m += ['#endif', '']
    a = head + ['#ifndef MD_AUDIO_VOICES_H', '#define MD_AUDIO_VOICES_H', '#include "voices/md-native.h"']
    a += ['#include "%s"' % (HAND[v][1] if v in HAND else 'voices/mdkit_%s.h' % v) for v in order]
    a += ['#define MD_VOICES %d' % len(order),
          'union md_voice_state {%s};' % ' '.join('%s %s;' % (HAND[v][2] if v in HAND else 'mdkit_' + v, v) for v in order),
          '#define MD_STATE_BYTES %d  /* the largest */' % max(info[v]['state'] for v in order),
          'static const mdkit_voice *const md_voice[MD_VOICES]={%s};' % ','.join('&mdkit_voice_%s' % v for v in order),
          '/* bit v: voice v\'s control clock keeps floats, else integers (tools/md_port.py FLOAT_CLOCK) */',
          '#define MD_FLOAT_CLOCKS 0x%08xu' % sum(1 << i for i, v in enumerate(order) if v in FLOAT_CLOCK),
          *budget,
          'static void (*const md_voice_words[MD_VOICES])(void *,int32_t *,unsigned)={%s};'
          % ','.join('md_st_%s_render_words' % v for v in order),
          '#endif', '']
    return '\n'.join(m), '\n'.join(a)



def manifest(order: list[str]) -> str:
    """mod.json with the AUDIO sources: the glue, the runtime, the tables and every voice"""
    s = (MOD / 'mod.json').read_text()
    src = ['audio/md.c', 'audio/md-runtime.c', 'audio/md-tables.c'] + [voice_source(v) for v in order]
    lines = ',\n                          '.join(', '.join('"%s"' % x for x in src[i:i + 3]) for i in range(0, len(src), 3))
    new, n = re.subn(r'"audio": \{"sources": \[.*?\]\}', '"audio": {"sources": [%s]}' % lines, s, flags=re.S)
    if n != 1: raise ValueError('mod.json: no audio sources')
    return new


ARRAY = re.compile(r'(?m)^static const (u?int(?:8|16|32)_t|unsigned char|unsigned short|unsigned|int) (\w+)\[(\d+)\]\s*=\s*\{([^{}]*)\};')
NUMBER = re.compile(r'\s*(-?(?:0[xX][0-9a-fA-F]+|\d+))[uUlL]*\s*$')


def share_arrays(files: dict[str, str]) -> tuple[str, str, int]:
    """keep mdkit's repeated integer tables once (point 6 above): rewrites the voices in
    files and returns the tables' C, their declarations and the bytes saved"""
    found = {}                       # content -> [(path, name, type, start, end)]
    for path, text in files.items():
        if not (path.startswith('audio/voices/mdkit_') and path.endswith('.c')): continue
        for m in ARRAY.finditer(text):
            words = m.group(4).split(',')
            if words and not words[-1].strip(): words = words[:-1]
            values = [NUMBER.match(w) for w in words]
            if len(values) != int(m.group(3)) or not all(values): continue
            values = tuple(int(v.group(1), 0) for v in values)
            size = {'uint8_t': 1, 'unsigned char': 1, 'uint16_t': 2, 'int16_t': 2, 'unsigned short': 2}.get(m.group(1), 4)
            if len(values) * size < 512: continue
            signed = m.group(1) in ('int', 'int16_t', 'int32_t', 'int8_t')
            found.setdefault((signed, values), []).append((path, m.group(2), m.group(1)))
    c, h, saved = [], [], 0
    for (signed, values), places in sorted(found.items(), key=lambda kv: kv[1][0][1]):
        if len(places) < 2: continue
        lo, hi = min(values), max(values)
        typ = ('uint8_t' if 0 <= lo and hi < 256 else 'uint16_t' if 0 <= lo and hi < 65536 else 'uint32_t') if not signed else \
              ('int16_t' if -32768 <= lo and hi < 32768 else 'int32_t')
        name = 'md_shared_' + places[0][1].replace('mdkit_', '')
        c.append('const %s %s[%d]={%s\n};' % (typ, name, len(values), ''.join(
            ('\n' if i % 12 == 0 else '') + '%d,' % v for i, v in enumerate(values))))
        h.append('extern const %s %s[%d];  /* %s */' % (typ, name, len(values), ', '.join(p[1] for p in places)))
        for path, aname, atype in places:
            text = files[path]
            m = next(m for m in ARRAY.finditer(text) if m.group(2) == aname)
            files[path] = (text[:m.start()] + '#ifdef MD_ST_TABLES\n#define %s %s  /* Synfact: kept once (md-tables.c) */\n#else\n'
                           % (aname, name) + m.group(0) + '\n#endif' + text[m.end():])
        width = {'uint8_t': 1, 'uint16_t': 2, 'int16_t': 2}.get(typ, 4)
        saved += sum(len(values) * {'uint8_t': 1, 'unsigned char': 1, 'uint16_t': 2, 'int16_t': 2, 'unsigned short': 2}.get(t, 4)
                     for _, _, t in places) - len(values) * width
    return '\n'.join(c), '\n'.join(h), saved


def weights() -> str:
    """audio/md-weights.h: 4-point Hermite (Catmull-Rom) weights for the 160 phases of
    44.1 -> 48 kHz (a voice sample is 160 phases; an output advances 147), Q14, each row
    summing to exactly 16384 so a constant passes unchanged"""
    rows_ = []
    for p in range(160):
        f = p / 160.0
        w = [(-f ** 3 + 2 * f * f - f) / 2, (3 * f ** 3 - 5 * f * f + 2) / 2,
             (-3 * f ** 3 + 4 * f * f + f) / 2, (f ** 3 - f * f) / 2]
        q = [int(round(x * 16384)) for x in w]
        q[1] += 16384 - sum(q)
        rows_.append('{%d,%d,%d,%d}' % tuple(q))
    # Stored in the order a block reads them: each output's phase is the last one's
    # - 13 (mod 160), so row k is phase (-13 k) mod 160 = (147 k) mod 160 and phase p
    # is row (123 p) mod 160 (123 = -1/13 mod 160). A block's 32 outputs read 32 rows
    # in a row, half the cache lines of rows 13 apart; the first 32 rows follow again
    # so that no block wraps.
    walk = [rows_[(147 * k) % 160] for k in range(160 + 32)]
    return ('/* SPDX-License-Identifier: GPL-2.0-or-later\n'
            ' * Generated by tools/md_port.py; do not edit. md.c says how they are used.\n'
            ' * Row k holds phase (147 k) mod 160, rows 160.. repeat 0..31: MD_WEIGHT(p) is phase p. */\n'
            '#define MD_WEIGHT(p) (md_weights[(123u * (unsigned)(p)) % 160u])\n'
            'static const short md_weights[160 + 32][4] __attribute__((aligned(16)))={\n' +
            ',\n'.join(','.join(walk[i:i + 4]) for i in range(0, 192, 4)) + '\n};\n')


def generate() -> dict[str, str]:
    """every generated file, by its path in the mod folder"""
    files = {'audio/md-weights.h': weights()}
    for voice in VOICES:
        if voice in HAND: continue
        for n, t in port(voice).items(): files['audio/voices/' + n] = t
    for f in ('mdkit.h', 'mdkit_voice.h'):
        files['audio/voices/' + f] = (MDKIT / 'include' / f).read_text().replace('#include <stddef.h>', '#include "md-types.h"')
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp); voices = work / 'voices'; voices.mkdir()
        for n, t in files.items(): (voices / pathlib.Path(n).name).write_text(t, newline='\n')
        for f in ('md-types.h', 'md-acc.h', 'md-native.h'):    # hand-written, needed by the generator
            (voices / f).write_text((OUT / f).read_text(), newline='\n')
        c, h, info = tables(voices, work)
    files['audio/md-tables.c'] = c
    files['audio/voices/md-tables.h'] = h
    files['md-voices.h'], files['audio/md-voices.h'] = voice_lists(info)
    shared_c, shared_h, saved = share_arrays(files)
    if shared_c:
        files['audio/md-tables.c'] += '/* mdkit\'s own tables, kept once (tools/md_port.py, share_arrays) */\n' + shared_c + '\n'
        head, _, tail = files['audio/voices/md-tables.h'].rpartition('#endif\n')       # the header's last #endif
        files['audio/voices/md-tables.h'] = (head + '/* mdkit\'s own tables kept once (%d bytes saved) */\n%s\n#endif\n'
                                             % (saved, shared_h) + tail)
    files['mod.json'] = manifest(order=list(VOICES))
    return files


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='compare with the files in the mod, write nothing')
    a = ap.parse_args()
    files = generate()
    if a.check:
        bad = [n for n, t in files.items() if not (MOD / n).is_file() or (MOD / n).read_text() != t]
        print('md_port: ' + ('stale: ' + ', '.join(bad) if bad else 'up to date (%d files)' % len(files)))
        return 1 if bad else 0
    for n, t in files.items():
        (MOD / n).parent.mkdir(parents=True, exist_ok=True)
        (MOD / n).write_text(t, newline='\n')
    print('md_port: wrote %d files in mods/md-st' % len(files))
    return 0


if __name__ == '__main__':
    sys.exit(main())
