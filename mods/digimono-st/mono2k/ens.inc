/* SPDX-License-Identifier: GPL-3.0-only
 * Extracted MONO2K SWAVE-ENS; generated, do not edit. */
    case M_ENS: {                      /* PCH2 PCH3 PCH4 WAVE PW CHRL CHRW TUNE */
        /* after the Monomachine's SWAVE-ENS, measured on its OS (tools/ref/chain_ens15..20): the root and up to three
         * voices off its interval table (equal tempered, and the just 3/2 4/3 5/4 6/5 beside them), no level shared
         * between them. A voice is two saws an octave under its note, the second PW / 128 of a period on; WAVE fades
         * their sum out and their difference in at 0.63 (weights 1 - 0.37 x and 1 - 1.63 x, x = WAVE / 127): PW 64
         * one saw at the note, WAVE 127 a pulse an octave under. The chorus (tools/ref/chain_ens41.spec): two copies
         * off a half-rate line, each delayed by a sine at 0.2 Hz swinging +-16.8 c, the second a quarter turn on;
         * CHRL their mix (dry 1 - 0.83 x, wet 0.85 x). CHRW sets them apart across the stereo: the left hears
         * (1 + w) / 2 of the first and (1 - w) / 2 of the second, the right the other way round (w = 0.355
         * (CHRW / 127)^1.15, T_ENS_W), so at 0 both hear their mean (mono); out as the mid here and the side (eside) */
        int32_t base = pitch + (PB8(t, P_SYN + 7) >> 7) - 12 * 64;
        int32_t w8 = P8(t, P_SYN + 3);
        int32_t wa = 32768 - (w8 * 12124) / (127 * 256);                   /* Q15 (2^15 by 2^16) */
        int32_t wb = 32768 - (w8 * 53412) / (127 * 256);
        uint32_t off = (uint32_t)P8(t, P_SYN + 4) << 17;
        uint32_t inc[4], inv[4];
        for (int k = 0; k < 4; k++) {
            int32_t iv = k ? T_ENS_PCH[P7(t, P_SYN + k - 1)] : 0;
            inc[k] = iv == ENS_OFF ? 0 : inc_of(base + iv);
            inv[k] = 0xFFFFFFFFu / (inc[k] | 1);
        }
        int32_t chrl = P8(t, P_SYN + 5), chrw = P8(t, P_SYN + 6);                  /* Q8 */
        int32_t dry = 32768 - (chrl * 27197) / (127 * 256), wet = (chrl * 27853) / (127 * 256);    /* Q15 */
        wet = (wet * tq_u16(T_ENS_G, chrw)) >> 15;                                 /* each channel at one copy's level */
        int32_t wside = (wet * tq_u16(T_ENS_W, chrw)) >> 15;                        /* the side's share, Q15 */
        int16_t *cm = t->line;
        int16_t *es = side;
        for (int i = 0; i < M2K_SUB; i++) {
            int32_t s = 0;
            for (int k = 0; k < 4; k++) {
                if (!inc[k]) continue;
                s += (blep_saw(t->ph[k], inc[k], inv[k]) * wa + blep_saw(t->ph[k] + off, inc[k], inv[k]) * wb) >> 15;
                t->ph[k] += inc[k];
            }
            s = (s * ENS_GAIN) >> 15;
            /* the line runs at half rate, always written (a CHRL turned up finds it full): a pair's mean */
            t->ms[0] += s;
            if (i & 1) { cm[t->ph[5]++ & (ENS_LEN - 1)] = (int16_t)sat16(t->ms[0] >> 1); t->ms[0] = 0; }
            t->ph[4] += ENS_CINC;
            es[i] = 0;
            if (chrl) {
                int32_t y[2];
                for (int c = 0; c < 2; c++) {                                       /* the two taps */
                    int32_t d8 = (2 << 8) + ENS_CDEP + ((ENS_CDEP * sinp(t->ph[4] + ((uint32_t)c << 30))) >> 15);   /* 1.4e9 */
                    uint32_t pos = (t->ph[5] << 8) + ((uint32_t)(i & 1) << 7) - (uint32_t)d8;   /* unsigned: it wraps */
                    uint32_t j = pos >> 8;
                    int32_t f = (int32_t)(pos & 255);
                    y[c] = (cm[j & (ENS_LEN - 1)] * (256 - f) + cm[(j + 1) & (ENS_LEN - 1)] * f) >> 8;
                }
                /* 32-bit products, their sum in 64 bits: the taps 16-bit, wside under 2^14, s under 2^16 by dry's 2^15,
                 * wet under 2^16 */
                es[i] = (int16_t)sat16(((y[0] - y[1]) * wside) >> 16);
                s = add_sh15(s * dry, ((y[0] + y[1]) >> 1) * wet);
            }
            o[i] = (int16_t)sat16(s);
        }
        break;
    }
