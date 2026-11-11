/* SPDX-License-Identifier: GPL-3.0-only
 * Generated from pinned MONO2K by Inigo Hernandez; see upstream/provenance.json.
 * Run python mods/digimono-st/mono2k/generate.py; do not edit. */
    case M_SID: {                      /* PW PWAD PWRS WAVE MOD MSRC MFRQ TUNE */
        /* after the Monomachine's SID-6581, measured on its OS (tools/ref/chain_sid36.spec): an oscillator on the note,
         * TRI SAW PULS MIX NOIS at one peak level, band-limited. The pulse's duty is |PW - 64| / 128 (a square at 0
         * and 127, silent at 64); PWAD sweeps PW on from the trig, 2.70 steps a second a step, round and round, PWRS
         * ON starting it again at each trig. A second oscillator at MFRQ (C3 at 0, an octave every 32 steps, the same
         * at every note) or on the previous track's note (MSRC): SYNC plays it in the wave, hard-synced to the note;
         * RING multiplies the two triangles, unipolar, whatever the wave; R+S rings with the second synced */
        uint32_t inc1 = inc_of(pitch + (PB8(t, P_SYN + 7) >> 7));
        int mod = P7(t, P_SYN + 4) >> 5, wave = (P7(t, P_SYN + 3) * 5) >> 7;
        /* Previous digital track: T1 wraps to T8 on Syntakt. */
        uint32_t inc2 = P7(t, P_SYN + 5) >= 64 ? inc_of(previous_pitch)
                                                : inc_of(48 * 64 + PK(t, P_SYN + 6, 24));
        t->ms[4] = (int32_t)(((uint32_t)t->ms[4] + (uint32_t)PK(t, P_SYN + 1, 64)) & 0x7FFFFFu);   /* the sweep, Q16 */
        int32_t pw = ((P8(t, P_SYN + 0) << 8) + t->ms[4]) & 0x7FFFFF;                             /* PW, Q16, 0..128 */
        pw -= 64 << 16;
        uint32_t duty = (uint32_t)(pw < 0 ? -pw : pw) << 9;                  /* of a cycle (2^32): |PW - 64| / 128 */
        int sync = mod >= 2, ring = mod & 1, two = mod == 2;                 /* SYNC: the second is what's heard */
        uint32_t inc = two ? inc2 : inc1, iv = 0xFFFFFFFFu / (inc | 1), iv1 = 0xFFFFFFFFu / (inc1 | 1);
        for (int i = 0; i < M2K_SUB; i++) {
            uint32_t p1 = t->ph[0];
            t->ph[0] += inc1;
            t->ph[1] += inc2;
            if (sync && t->ph[0] < p1)  /* the second restarts half a cycle on with the note's cycle (its partials
                                         * say so), as far into it as the note is */
                t->ph[1] = (1u << 31) + mulu_hi(t->ph[0] * iv1, inc2, 32);
            uint32_t ph = two ? t->ph[1] : t->ph[0];
            int32_t s;
            if (ring) {                /* two unipolar triangles multiplied, its mean taken off (the chain takes the rest) */
                int32_t u1 = (int32_t)((t->ph[0] ^ (uint32_t)((int32_t)t->ph[0] >> 31)) >> 16);    /* 0..32767 */
                int32_t u2 = (int32_t)((t->ph[1] ^ (uint32_t)((int32_t)t->ph[1] >> 31)) >> 16);
                s = (((u1 * u2) >> 14) - 16384) * SID_A >> 16;
            } else if (wave == 0) {
                s = ((int32_t)((ph ^ (uint32_t)((int32_t)ph >> 31)) >> 15) - 32768) * SID_A >> 15;
            } else if (wave == 1) {
                s = blep_saw(ph, inc, iv) * SID_A >> 15;
            } else if (wave == 2) {    /* two saws a duty apart: a pulse between +-SID_A, its mean left (the chain's) */
                s = (blep_saw(ph, inc, iv) - blep_saw(ph - duty, inc, iv) + 32768 - (int32_t)(duty >> 16)) * SID_A >> 15;
            } else if (wave == 3) {    /* MIX: the triangle AND the saw, as the chip combines them, its mean taken off */
                uint32_t tri = (ph ^ (uint32_t)((int32_t)ph >> 31)) << 1;
                s = ((int32_t)((tri & ph) >> 20) - SID_MIX0) * SID_MIXA >> 11;
            } else {                   /* NOIS: the chip's 23-bit register, clocked by bit 19 of the cycle's 24, its 8 */
                if (((ph ^ (ph - inc)) & (1u << 27)) && (ph & (1u << 27))) {     /* output bits */
                    uint32_t x = t->lfsr & 0x7FFFFFu;
                    if (!x) x = 0x7FFFF8u;
                    x = ((x << 1) | (((x >> 22) ^ (x >> 17)) & 1u)) & 0x7FFFFFu;
                    t->lfsr = x;
                    t->ms[0] = (int32_t)(((x >> 13) & 0x80) | ((x >> 12) & 0x40) | ((x >> 9) & 0x20) | ((x >> 7) & 0x10) |
                                         ((x >> 6) & 0x08) | ((x >> 3) & 0x04) | ((x >> 1) & 0x02) | (x & 0x01));
                }
                s = (t->ms[0] * 2 - 255) * (SID_A * 33 / 32) >> 8;    /* 0.3 dB over: as measured */
            }
            o[i] = (int16_t)sat16(s);
        }
        break;
    }
    case M_FMS: {                      /* 1FRQ 1FIN 1ENV 1FB 2FRQ 2VOL TONE TUNE */
        /* after the Monomachine's FM+STAT, measured on its OS (tools/ref/chain_fm25.spec): a sine carrier on the note,
         * half GND-SIN's level, phase-modulated by two sine operators side by side. Each operator's ratio on one 24-step
         * table (1/64 .. 4); the first's fine-tuned by 0.75 .. 1.25 and fed back on itself; 1ENV its depth up to 64
         * (index 0 .. 1.56), above 64 that depth decaying from the trig (10 s .. 5 ms); 2VOL the second's depth, the same
         * law to 64 and on to index 14 at 127; both through a lowpass, TONE its cutoff (30 Hz at 0, 1.37 kHz at 64, open at
         * 127), so a high modulator goes in shallower; TUNE +-2 semitones */
        uint32_t inc = inc_of(pitch + (PB8(t, P_SYN + 7) >> 7));
        uint32_t r1 = mulu_hi(T_FMR24[fr24(P7(t, P_SYN + 0))], 192 * 256 + (uint32_t)P8(t, P_SYN + 1), 16);
        uint32_t r2 = T_FMR24[fr24(P7(t, P_SYN + 4))];
        uint32_t i1 = mulu_hi(inc, r1, 16), i2 = mulu_hi(inc, r2, 16);
        int32_t env = P8(t, P_SYN + 2);
        uint32_t k1 = tq_u32(T_FMB, env < 64 * 256 ? env : 64 * 256);
        if (env >= 65 * 256) {         /* the depth from the trig, decaying (T_FMB[64] 32837: a word) */
            k1 = (T_FMB[64] * (t->menv[0] >> 15)) >> 15;
            t->menv[0] = mulu_hi(t->menv[0], tq_u32(T_FMEC, env - 64 * 256), 31);
        }
        uint32_t k2 = tq_u32(T_FMB, P8(t, P_SYN + 5));
        int32_t lp = tq_u16(T_FMLP, P8(t, P_SYN + 6));
        uint32_t kf = tq_u32(T_FMFB, P8(t, P_SYN + 3));
        for (int i = 0; i < M2K_SUB; i++) {
            int32_t m1 = sinp(t->ph[1] + (uint32_t)((t->ms[0] + t->ms[1]) >> 1) * kf);   /* the low word alone */
            t->ms[1] = t->ms[0];
            t->ms[0] = m1;
            /* each modulator through two one-poles at TONE's cutoff on its way to the carrier (at 64, 1.37 kHz: its
             * depth measured 0.88 at 523 Hz, 0.30 at 2 kHz, 0.10 at 4 kHz; the feedback taken before), states Q23 */
            uint64_t d = mul2_q15(lp, m1 * 256 - t->ms[2], sinp(t->ph[2]) * 256 - t->ms[4]);   /* the two firsts */
            t->ms[2] += (int32_t)d;
            t->ms[4] += (int32_t)(d >> 32);
            d = mul2_q15(lp, t->ms[2] - t->ms[3], t->ms[4] - t->ms[5]);                       /* and seconds */
            t->ms[3] += (int32_t)d;
            t->ms[5] += (int32_t)(d >> 32);
            uint32_t pm = (uint32_t)phase_depth(t->ms[3], k1) + (uint32_t)phase_depth(t->ms[5], k2);
            o[i] = (int16_t)(sinp(t->ph[0] + pm) >> 1);
            t->ph[0] += inc;
            t->ph[1] += i1;
            t->ph[2] += i2;
        }
        break;
    }
    case M_FMD: {                      /* 1FRQ 1FEN 1VOL 1VEN 2FRQ 2ENV 2FB TUNE */
        /* after the Monomachine's FM+DYN, measured on its OS (tools/ref/chain_fm26.spec): FM+STAT's carrier and two
         * operators side by side. The first's ratio 1FRQ / 64 (2 at 127), its frequency pushed from the trig by 1FEN (a
         * note-independent amount added before the ratio, up over 64, a quarter of it down under) and falling back; 1VOL
         * its depth, on FM+STAT's law; 1VEN over 64 pushes that law up from the trig by up to 128 of its steps, falling
         * back on 1FEN's time (under 64's depth the push adds twice as fast, and stops there). The second's ratio
         * (2FRQ / 64)^2 (4 at 127), 2ENV its depth or its decay as FM+STAT's 1ENV, 2FB its feedback as FM+STAT's 1FB;
         * TUNE +-2 semitones */
        uint32_t inc = inc_of(pitch + (PB8(t, P_SYN + 7) >> 7));
        int32_t f1 = P8(t, P_SYN + 0), f2 = P8(t, P_SYN + 4);              /* Q8: between steps too */
        int32_t r1 = (f1 << 2) + (f1 > 126 * 256 ? (f1 - 126 * 256) << 2 : 0);   /* Q16, v / 64, 2 at 127 */
        uint32_t r2 = f2 <= 126 * 256 ? ((uint32_t)f2 * (uint32_t)f2) >> 12          /* (v / 64)^2, 4 at 127 */
                                      : (126u * 126u << 4) + ((508u << 4) * (uint32_t)(f2 - 126 * 256) >> 8);   /* to 128^2 */
        int32_t fen = P8(t, P_SYN + 1), ven = P8(t, P_SYN + 3), env = P8(t, P_SYN + 5);
        int64_t b1 = inc;
        if (fen != 64 * 256) {
            int32_t fe = T_FMFEN[fen >> 8];
            if (fen & 255) fe += (int32_t)mul_sh(T_FMFEN[(fen >> 8) + 1] - fe, fen & 255, 8);
            b1 += mul_sh(fe, (int32_t)(t->menv[1] >> 15), 15);
            t->menv[1] = mulu_hi(t->menv[1], tq_u32(T_FMDEC, fen > 64 * 256 ? fen - 64 * 256 : 64 * 256 - fen), 31);
        }
        uint32_t i1 = (uint32_t)(int32_t)mul64_sh(b1, r1, 16), i2 = mulu_hi(inc, r2, 16);
        int32_t vol8 = P8(t, P_SYN + 2);
        int32_t k1 = (int32_t)fmd_at(vol8);
        /* the envelope's 2^15 by a step count, a knob or 64's depth (T_FMDV[64] 32691) fits a word; the turn's not */
        if (ven > 64 * 256) {          /* a push from the trig: 4 steps of the law a step, to 128 */
            int32_t e8 = ((ven < 96 * 256 ? 4 * (ven - 64 * 256) : 128 * 256) * (int32_t)(t->menv[0] >> 15)) >> 15;
            t->menv[0] = mulu_hi(t->menv[0], tq_u32(T_FMDEC, ven - 64 * 256), 31);
            if (vol8 + e8 >= 64 * 256) {
                k1 = (int32_t)fmd_at(vol8 + e8);
            } else {                   /* under 64's depth it adds linearly, twice as fast, and stops there */
                k1 += (int32_t)((T_FMDV[64] * (uint32_t)e8) >> 13);
                if (k1 > (int32_t)T_FMDV[64]) k1 = (int32_t)T_FMDV[64];
            }
        } else if (ven < 64 * 256) {   /* a cut from the trig, 1/32 of 1VOL a step; on 64's depth, past half of it the
                                        * operator turns over (to its whole depth, inverted, at 0) */
            int32_t c = ((64 * 256 - ven) * (int32_t)(t->menv[0] >> 15)) >> 13;                /* Q15 of 1VOL */
            t->menv[0] = mulu_hi(t->menv[0], tq_u32(T_FMDEC, 64 * 256 - ven), 31);
            int32_t f = 32768 - c;                                               /* Q15, -1 .. 1 */
            int32_t v8 = ((f < 0 && vol8 > 64 * 256 ? 64 * 256 : vol8) * f) >> 15;
            if (v8 >= 64 * 256) {
                k1 = (int32_t)fmd_at(v8);
            } else if (vol8 >= 64 * 256) {
                /* from over 64 the turn comes lower: at 32 steps from 64, at 0 from 127 */
                int32_t h = vol8 - 64 * 256;
                int32_t z = 32 * 256 - (int32_t)mul_sh(h * h, 2113, 26);
                k1 = ((int32_t)T_FMDV[64] * (v8 - z)) >> 13;
                if (k1 > (int32_t)T_FMDV[64]) k1 = (int32_t)T_FMDV[64];
            } else {
                k1 = (int32_t)mul_sh(k1, 2 * f - 32768, 15);
            }
        }
        uint32_t k2 = fmd_at(env < 64 * 256 ? env : 64 * 256);
        if (env >= 65 * 256) {
            k2 = (T_FMDV[64] * (t->menv[2] >> 15)) >> 15;
            t->menv[2] = mulu_hi(t->menv[2], tq_u32(T_FMEC, env - 64 * 256), 31);
        }
        uint32_t kf = tq_u32(T_FMFB, P8(t, P_SYN + 6));
        for (int i = 0; i < M2K_SUB; i++) {
            int32_t m2 = sinp(t->ph[2] + (uint32_t)((t->ms[0] + t->ms[1]) >> 1) * kf);
            t->ms[1] = t->ms[0];
            t->ms[0] = m2;
            uint32_t pm = (uint32_t)sinp(t->ph[1]) * (uint32_t)k1 + (uint32_t)m2 * k2;   /* the low words alone */
            o[i] = (int16_t)(sinp(t->ph[0] + pm) >> 1);
            t->ph[0] += inc;
            t->ph[1] += i1;
            t->ph[2] += i2;
        }
        break;
    }
    case M_FMP: {                      /* 1FRQ 1ENV 2FRQ 2ENV 3FRQ 3ENV TONE TUNE */
        /* after the Monomachine's FM+PAR, measured on its OS (tools/ref/chain_fm26.spec): FM+STAT's carrier, lowpass and
         * laws with three operators side by side, each on the 24 steps; an ENV up to 64 its depth, over 64 the depth of
         * FM+STAT's 2VOL at that value, falling from the trig on 1ENV's time; TONE as FM+STAT's; TUNE +-2 semitones */
        uint32_t inc = inc_of(pitch + (PB8(t, P_SYN + 7) >> 7));
        uint32_t ii[3], kk[3];
        for (int j = 0; j < 3; j++) {
            ii[j] = mulu_hi(inc, T_FMR24[fr24(P7(t, P_SYN + 2 * j))], 16);
            int32_t env = P8(t, P_SYN + 2 * j + 1);
            uint32_t k = tq_u32(T_FMB, env);
            if (env >= 65 * 256) {
                k = mulu_hi(k, t->menv[j] >> 15, 15);
                t->menv[j] = mulu_hi(t->menv[j], tq_u32(T_FMEC, env - 64 * 256), 31);
            }
            kk[j] = k;
        }
        int32_t lp = tq_u16(T_FMLP, P8(t, P_SYN + 6));
        for (int i = 0; i < M2K_SUB; i++) {
            uint32_t pm = 0;
            for (int j = 0; j < 3; j++) {
                int32_t *a = &t->ms[2 * j];
                a[0] += (int32_t)gain_q15(sinp(t->ph[1 + j]) * 256 - a[0], lp);
                a[1] += (int32_t)gain_q15(a[0] - a[1], lp);
                pm += (uint32_t)phase_depth(a[1], kk[j]);
                t->ph[1 + j] += ii[j];
            }
            o[i] = (int16_t)(sinp(t->ph[0] + pm) >> 1);
            t->ph[0] += inc;
        }
        break;
    }
