# SPDX-License-Identifier: GPL-3.0-only
"""Explicit raw-knob fixtures. No remapping to hide differences between engines."""
FAMILIES = {
    'SAW': (2, 4), 'PULSE': (3, 5), 'ENS': (4, 14), 'VO': (5, 11),
    'SID-6581': (8, 3), 'FM+STAT': (9, 8), 'FM+PAR': (10, 9), 'FM+DYN': (11, 10),
}


def cases():
    result = []

    def add(family, suffix, p, feature, duration=2, events=None, note=48):
        bank, original = FAMILIES[family]
        result.append({'name': family.lower().replace('+', '_').replace('-', '_')+'_'+suffix,
                       'family': family, 'bank_type': bank, 'original_machine': original,
                       'note': note, 'tune': 64, 'params': list(p), 'feature': feature,
                       'duration_s': duration, 'events': events or [(0, note)]})

    base = [0]*7
    add('SAW', 'base', base, 'Una sierra, sin unison ni subs')
    for index, name in [(4, 'subx'), (5, 'sub1'), (6, 'sub2')]:
        for value in (64, 127):
            p = base.copy();p[index] = value
            add('SAW', f'{name}_{value}', p, f'{name.upper()}={value}')
    for width in (16, 64, 127):
        p = base.copy();p[0] = 127;p[1] = width
        add('SAW', f'unison_{width}', p, f'UNIL=127, UNIW={width}')
    for extended in (64, 127):
        add('SAW', f'unix_{extended}', [127, 64, extended, 0, 0, 0, 0], f'Unison extendido UNIX={extended}')

    base = [0, 0, 0, 0, 64, 0, 127]
    add('PULSE', 'base', base, 'Pulso PW=64, sin unison ni subs')
    for value in (0, 8, 32, 96, 127):
        p = base.copy();p[4] = value
        add('PULSE', f'pw_{value}', p, f'PW={value}')
    for index in (2, 3):
        p = base.copy();p[index] = 127
        add('PULSE', f'sub{index-1}', p, f'SUB{index-1}=127')
    add('PULSE', 'unison', [127,127,0,0,64,0,127], 'UNIL=127, UNIW=127')
    for speed in (16, 64, 127):
        p = base.copy();p[5] = speed
        add('PULSE', f'pwad_{speed}', p, f'Barrido PWAD={speed}')
    for reset in (0, 127):
        add('PULSE', f'pwrs_{reset}', [0,0,0,0,32,64,reset], f'PWRS={reset}, notas repetidas',
            events=[(0,48),(.64,48),(1.28,48)])

    base = [63, 63, 63, 0, 64, 0, 127]
    add('ENS', 'base', base, 'PCH2/3/4=63, WAVE=0, CHRL=0')
    for interval in (0, 48, 61, 64, 75, 96, 127):
        p = base.copy();p[0] = interval
        add('ENS', f'pch2_{interval}', p, f'PCH2={interval}; otros intervalos en 63')
    for wave in (64, 127):
        p = base.copy();p[3] = wave;p[4] = 32
        add('ENS', f'wave_{wave}', p, f'WAVE={wave}, PW=32')
    for width in (0, 64, 127):
        p = base.copy();p[5] = 127;p[6] = width
        add('ENS', f'chorus_{width}', p, f'CHRL=127, CHRW={width}; ocho segundos', duration=8)

    base = [64, 64, 0, 0, 0, 64, 100]
    add('VO', 'base', base, 'Vocal sostenida VOC1=VOC2=64', note=60)
    for vowel in (0, 32, 96, 127):
        p = base.copy();p[0] = p[1] = vowel
        add('VO', f'vowel_{vowel}', p, f'VOC1=VOC2={vowel}', note=60)
    for speed in (32, 96):
        add('VO', f'switch_{speed}', [16,112,speed,0,0,64,100], f'Transición VOC1=16 -> VOC2=112, V-SW={speed}', note=60)
    for consonant in (32, 96, 127):
        p = base.copy();p[4] = consonant
        add('VO', f'cons_{consonant}', p, f'CONS={consonant}, CLEN=64, CVOL=100', note=60)

    base = [0,0,127,0,0,0,64]
    add('SID-6581', 'base', base, 'TRI, MOD=OFF')
    for value, wave in [(26,'SAW'),(52,'PULS'),(77,'MIX'),(103,'NOIS')]:
        p = base.copy();p[3] = value
        add('SID-6581', wave.lower(), p, f'WAVE={wave}')
    for width in (32, 64, 127):
        p = base.copy();p[3] = 52;p[0] = width
        add('SID-6581', f'pw_{width}', p, f'PULS, PW={width}')
    for mode, label in [(32,'ring'),(64,'sync'),(96,'ring_sync')]:
        p = base.copy();p[4] = mode;p[6] = 48
        add('SID-6581', label, p, f'MOD={label.upper()}, MFRQ=48')
    add('SID-6581', 'pwad', [0,64,127,52,0,0,64], 'PULS, PWAD=64')

    base = [82,64,0,0,82,0,127]
    add('FM+STAT', 'base', base, 'Portadora sin modulación, TONE abierto')
    for depth in (16, 32, 64, 96, 127):
        p = base.copy();p[2] = depth
        add('FM+STAT', f'env_{depth}', p, f'1ENV={depth}, ratio 1:1')
    for tone in (32, 64, 96):
        p = base.copy();p[2] = 64;p[6] = tone
        add('FM+STAT', f'tone_{tone}', p, f'1ENV=64, TONE={tone}')
    for feedback in (32, 48, 64, 127):
        p = base.copy();p[2] = 64;p[3] = feedback
        add('FM+STAT', f'fb_{feedback}', p, f'1ENV=64, 1FB={feedback}')
    for ratio in (32, 61, 125):
        p = base.copy();p[0] = ratio;p[2] = 64
        add('FM+STAT', f'ratio_{ratio}', p, f'1FRQ={ratio}, 1ENV=64')

    base = [82,0,96,0,112,0,127]
    add('FM+PAR', 'base', base, 'Portadora sin modulación')
    for depth in (32, 64, 96):
        add('FM+PAR', f'env_{depth}', [82,depth,96,depth,112,depth,127],
            f'Tres moduladores en paralelo, ENV={depth}')

    base = [64,64,0,64,74,0,0]
    add('FM+DYN', 'base', base, 'Portadora sin modulación')
    add('FM+DYN', 'depth', [64,64,64,64,74,64,0], 'Profundidad estática, ambos moduladores')
    for envelope in (0, 32, 96):
        add('FM+DYN', f'ven_{envelope}', [64,64,96,envelope,74,64,0], f'1VOL=96, 1VEN={envelope}')
    for envelope in (32, 96):
        add('FM+DYN', f'fen_{envelope}', [64,envelope,64,64,74,64,0], f'1FEN={envelope}')
    return result
