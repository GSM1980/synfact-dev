| Chorus tables: the Digitone's bytes, rebuilt from the owner's OS file by
| tools/import_digitone.py into imported/. Labels are ours.
        .section .rodata,"a"
        .balign 4
        .globl chorus24_lut,chorus24_hpf24
| 32 entries of the delay modulation's shaping curve (Q31)
chorus24_lut:
        .incbin "imported/lut.bin"
| 128 one-pole HPF coefficients for HPF 0..127, retuned to 24 kHz (Q31)
chorus24_hpf24:
        .incbin "imported/hpf24.bin"
