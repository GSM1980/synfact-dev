# COMP: paso alto fijo en el sidechain

Evaluación del 2026-11-21 sobre `origin/main` 48e870a (comp-st 0.2.0,
core 2.4.0, OS 1.42), seguida de su implementación en **comp-st 0.3.0**
(apartado final, con medidas). No se ha probado en la unidad. Los apartados
anteriores al final son la estimación previa y se conservan como tal.

## Qué se pide

Un control en G, la posición del SCF de Digitakt, con el icono y el dial de
Digitakt. Selecciona un paso alto con unas pocas frecuencias fijas, que
actúa solo sobre lo que **escucha** el detector. La ganancia se sigue
aplicando a la señal completa en los VCA del bus FX. Así los graves
(bombo, subgraves) dejan de dirigir la reducción.

## Filtro: un polo, un MAC por canal

`lp += a·(x − lp)`, `y = x − lp`. Es un paso alto de primer orden
(6 dB/oct). El estado `lp` de cada canal vive en un acumulador EMAC durante
el bloque: ACC0 para L y ACC1 para R. Los callbacks de FX_CV pueden usar
ACC0/1. Por muestra, el filtro solo añade leer el acumulador, una resta que
también hace la lectura de memoria y un `MSAC`. El SUB deja el signo de
`y` en el CCR, porque el EMAC no toca el CCR, así que el valor absoluto
del canal R no necesita `TST`.

El filtro de Digitakt usa 3 coeficientes interpolados por muestra (6 MAC por
frame). Este usa 1 MAC por canal y un coeficiente fijo por posición.

| Posición | a (Q31) | 50 Hz | 100 Hz | 1 kHz |
|---|---|---:|---:|---:|
| OFF | — | 0 | 0 | 0 |
| 60 Hz | `010059ec` | −3,9 dB | −1,3 | −0,0 |
| 90 Hz | `017fc61b` | −6,3 dB | −2,6 | −0,0 |
| 150 Hz | `027d1f6e` | −10,0 dB | −5,1 | −0,1 |
| 250 Hz | `041ef963` | −14,1 dB | −8,6 | −0,3 |
| 400 Hz | `068795a9` | −18,1 dB | −12,3 | −0,6 |
| 700 Hz | `0b35124e` | −22,9 dB | −17,0 | −1,7 |
| 1 kHz | `0fb471cf` | −26,0 dB | −20,0 | −3,0 |

`a = 1 − e^(−2π·fc/48000)`. Las atenuaciones son las de un primer orden
ideal. Cualquier frecuencia cuesta lo mismo, así que la lista es libre;
hasta 8 posiciones caben en 3 bits. Un segundo orden (12 dB/oct, dos polos
en cascada) necesitaría ACC2/3, que hay que conservar. Costaría el doble
y no cabría en la SRAM.

## Bytes y SRAM

La ventana de COMP `80007680..800077c0` tiene 320 B. Hoy está llena:
pico 128 B, curva 88 B, escala 40 B, estado 56 B y guardas 8 B. El
prototipo la reorganiza así:

| Parte | Hoy | Propuesta |
|---|---:|---:|
| Pico sin filtro | 128 B (desenrollado ×8) | 70 B (×4 y suelo de pico) |
| Bucle con filtro | — | 46 B (44 de bucle y un salto) |
| Curva + escala | 128 B | 128 B |
| **Código** | **256 B** | **244 B** |
| Estado (fila de 40 B → 44 B con `a`, más `lpL`/`lpR`) | 56 B | 68 B |
| Guardas | 8 B | 8 B |
| **Total** | **320 B** | **320 B** |

Para que encaje, la preparación por bloque del filtro y los `LEA`/`MOVEQ`
de ambos bucles pasan al kernel, que ya está en DDR. Esa preparación
consiste en guardar y fijar MACSR, cargar y guardar los dos acumuladores
con `MOVEM` y comprobar `a`. En la SRAM quedan solo los bucles. El ajuste
es exacto, sin margen. Si al integrarlo apareciera algún byte de
alineación, el pico sin filtro pasaría a ×2: libera 24 B y cuesta +32
instrucciones con el filtro en OFF. Bucle, estado y buses están todos en
la SRAM interna, de modo que el filtro no toca la caché.

La README de 0.2.0 ya indica que, en DDR, el desenrollado ×4 tenía el mejor
proxy de caché fría. En SRAM cuesta 8 iteraciones más.

## Coste

Instrucciones por bloque de 32 frames estéreo, frente a comp-st 0.2.0:

| Estado | Delta |
|---|---:|
| MIX = 0 | 0 |
| Filtro OFF | +18 (×4 en lugar de ×8: +16; comprobar `a`: +2) |
| Filtro activo, cualquier frecuencia | **~+220** |

Con filtro activo, el bucle cuesta 15–19 instrucciones por frame, frente a
8,5–12,5 sin filtro: +6,5 por frame, unas +208 por bloque. Se suman ~12 de
preparación en el kernel. El pico comprimiendo pasaría de 484–509 a unas
700–730 instrucciones.

Para convertirlo en CPU se usa el modelo de
[STOCK-MAIN-CPU-PROFILE](STOCK-MAIN-CPU-PROFILE.md): 0,85
ciclos/instrucción y 250 MHz, con 166.667 ciclos por bloque. Con filtro
son unos **190 ciclos: ~0,11 puntos MAIN**. Como mucho se añaden 2 líneas
frías de la preparación en el kernel (DDR), ≤80 ciclos más, o sea ≤0,16
puntos en el peor caso. En OFF son unos 15 ciclos (~0,01 puntos). Como
referencia, COMP 0.2.0 comprimiendo cuesta unos 1,2 puntos con el mismo
modelo.

El primer diseño, el SCF de Digitakt de 3 coeficientes, costaba ~+400
instrucciones y no cabía en SRAM.

## Datos y UI

- **Kit**: no hacen falta bytes nuevos. RAT se guarda en Q8 con el byte bajo
  a cero: 0.2.0 lo fuerza al editar y lo exige al leer. El índice del
  filtro (0 = OFF) va en ese byte bajo. Los Kits de 0.2.0 se leen como OFF.
  Si un Kit guardado con filtro se carga en un banco 0.2.0, ese banco
  rechaza el registro y restaura los valores por defecto del COMP.
- **Tarea**: el setter publica `a` en la fila de configuración, bajo la
  misma exclusión IRQ que ya existe. Al pasar de OFF a una frecuencia
  conviene poner `lp` a cero, lo que da como mucho un bloque de detección
  de más. Las 8 constantes son inmediatos generados por fórmula; no se
  copian datos del firmware.
- **Página COMP**: G muestra la etiqueta y el dial del SCF de Digitakt 1.54
  (registro `401ab34c`, "SCF" / "Sidechain Filter"), con el importador que
  ya existe. El valor se muestra como texto, igual que RAT: OFF, 60, 90,
  150… Falta localizar en el OS de Digitakt el grupo de bitmaps del SCF.
  F sigue vacía.
- **Desbordamiento**: `lp − x` se desborda solo si la entrada está cerca
  del fondo de escala y `lp` tiene el signo contrario y un valor grande.
  Ese caso da un pico erróneo en una muestra. Evitarlo con una entrada
  desplazada un bit cuesta +2 instrucciones por frame; no se propone.

## Conclusión

Un paso alto de un polo, en posiciones fijas, cuesta **~+220
instrucciones por bloque (~0,1 puntos MAIN)** activo y +18 en OFF. **Cabe
entero en la SRAM actual de COMP**, sin quitar nada a Chorus ni a LFO3,
pasando el pico sin filtro a ×4 y la preparación al kernel. No necesita
bytes nuevos en el Kit. Hay que comprobar la integración del byte exacto,
medir con `tools/bench_st_comp.py` y escucharlo en la unidad.

## Implementación comp-st 0.3.0: medidas

Implementado como se propone arriba, con el control en G. El importador toma
de Digitakt 1.54 la etiqueta `SCF` / "Sidechain Filter" (registro
`401ab34c`) y su dial: la animación bipolar de 27 fotogramas `421fa2fc`,
dibujada por `400601f0` (paso bajo en 0, plano en 13, paso alto en 26). Cada
posición muestra el fotograma que Digitakt muestra para su propio paso alto
a ese corte: OFF 13, 60 Hz 15, 90 Hz 16, 150 Hz 17, 250 Hz 18, 400 Hz 19,
700 Hz 20 y 1 kHz 21. El parámetro es `0x410` (UI `0x4f7`).

| Comprobación | Resultado |
|---|---|
| Bloque SRAM de código | 244 B: pico ×4 70, bucle SCF 46, curva 88, VCA 40 |
| Estado SRAM | 68 B + 8 de guardas: ventana `80007680..800077c0` exacta |
| Bucle SCF y pico frente a modelo de 64 bits (cfbench) | 2.688 bloques, 0 diferencias |
| Coeficientes de `params.c` frente a la fórmula | iguales |
| Respuesta a senos, 7 posiciones × 6 frecuencias | la de un polo exacto (±0,1 dB) |
| SCF OFF frente a 0.2.0 (Unicorn, DDR y SRAM) | 2.624 bloques bit a bit iguales |

Instrucciones extra por bloque, `tools/bench_st_comp.py --sram`:

| Estado | 0.2.0 | 0.3.0 |
|---|---:|---:|
| MIX = 0 | 43 | 43 |
| SCF OFF, bajo umbral | 377–379 | 395–397 |
| SCF OFF, comprimiendo | 484–509 | 502–528 |
| SCF 150 Hz, bajo umbral | — | 600 |
| SCF 150 Hz, comprimiendo | — | 705–738 |

El bucle SCF cuesta 211 instrucciones por bloque más que el pico sin filtro
(cfbench), en línea con las ~220 estimadas. Con SCF activo, una escena
comprimiendo toca 39 líneas de código y 5 de datos en caché, frente a 36/5
con SCF en OFF: la preparación del filtro corre en el kernel, en DDR. Según
el modelo de 0,85 ciclos/instrucción y 40 por línea fría, son unos 170
ciclos más en caliente y 290 en frío por bloque: **0,1–0,2 % de MAIN**.

Cuánto deja de oír el detector, medido en cfbench: un bombo a 50 Hz baja
3,8 dB a 60 Hz, 6,2 a 90 Hz, 9,9 a 150 Hz y 14,0 a 250 Hz. A 1 kHz o más,
las posiciones hasta 250 Hz no lo tocan (≤0,1 dB). Cerca de Nyquist, las
posiciones altas suben hasta +0,4 dB, propio del filtro digital de un polo.

Sigue pendiente escucharlo y medir la CPU en la unidad. Un Kit guardado con
SCF activo que se cargue en un banco 0.2.0 vuelve a los valores por defecto
del COMP, porque 0.2.0 rechaza un byte bajo de RAT distinto de cero.

## Consumo en la pantalla del COMP

En la unidad, la CPU subía mucho con la página COMP abierta. La causa no es
el DSP, sino el medidor GR. La pantalla tiene doble búfer: cada repintado
recompone todas las vistas visibles, y solo la página COMP ya cuesta unas
30.000 instrucciones. Con batería, el medidor de 0.2.0 pedía un repintado
en 24–26 de los 30 ticks de UI por segundo (`tools/bench_st_comp_redraws.py`).

En 0.3, el medidor repinta como mucho cada tres ticks (10 Hz) e ignora los
movimientos de 1 píxel (0,6 dB), salvo para volver a cero. Con batería quedan
8–10 repintados por segundo, entre un 60 y un 68 % menos, y ninguno con una
señal estable. No se ha medido en la unidad cuánto baja la lectura de CPU.

## 128 posiciones (en 0.3)

A petición del propietario, el SCF pasa de 8 posiciones a 128, como un
control continuo de Digitakt: OFF y 127 cortes en escala logarítmica de
60 Hz a 1 kHz, a un 2 % de distancia. Los valores anteriores siguen entre
ellos (posiciones 1, 19, 42, 65, 86, 111 y 127). La página muestra el valor
en Hz (`60Hz` … `1kHz`). La tabla de coeficientes y de Hz es `scf-tables.h`,
generada por fórmula con `tools/generate_config_tables.py`: 512 + 256 bytes
en el banco, fuera de la SRAM, que solo lee la tarea al mover el control. La
posición (0..127) sigue cabiendo en el byte bajo de RAT.

**No consume más.** El audio lee un único coeficiente preparado, sea cual sea
la posición, y su bucle no tiene ningún camino que dependa del corte. Medido
en el kernel enlazado (`bench_st_comp.py --sram`): 43 con MIX a 0, 397 sin
filtro bajo umbral, 600 con filtro bajo umbral y 705–738 comprimiendo, lo
mismo que con 8 posiciones. En cfbench, un bloque del bucle cuesta 1.075–1.079
instrucciones en las posiciones 1, 42, 65 y 127: la diferencia es la del
pico, que guarda un nuevo máximo más o menos veces según la señal filtrada.
El banco crece 912 bytes.

Mover el control entre cortes ya no reinicia el estado del filtro: un filtro
de un polo admite el nuevo coeficiente sin saltos y el barrido del mando es
suave. Solo se reinicia al pasar de OFF a un corte o al activar MIX. Girar el
mando hace más llamadas al setter de la tarea, como cualquier otro control
continuo (THR, ATK…), solo mientras se gira.
