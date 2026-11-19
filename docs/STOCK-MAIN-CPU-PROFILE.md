[Synfact docs](README.md) › Stock MAIN CPU profile

# Dónde gasta MAIN el firmware stock (OS 1.42) y qué se puede ganar

Evaluación de 2026-11-18: ¿merece la pena reescribir en ensamblador partes del
código **stock** de MAIN para recuperar CPU? Respuesta corta: sí hay margen,
pero no está donde parece. Los bucles DSP de Elektron (reverb, delay, mezcla)
ya son EMAC escrito a mano y bien planificado; reescribirlos da poco. **La mitad
del coste de MAIN son fallos de la caché de instrucciones**, y ahí están las
oportunidades grandes.

Todo lo de abajo son estimaciones del emulador con un modelo de caché
calibrado, no lecturas del medidor. Ninguna cifra sustituye la comparación en
la unidad.

## Método

- Firmware: Syntakt OS 1.42 stock, MAIN SHA-256 `1a9723b1…3eb3e7`, sin core ni banco.
- analog-emu (los dos ColdFire) con el parche
  [`tools/stock-profile/analog-emu-xprof.patch`](../tools/stock-profile/analog-emu-xprof.patch):
  cuenta **exacta** de instrucciones por bloque básico (no muestreo), pila de
  llamadas en sombra por tarea (tiempo inclusivo, llamadas y aristas) y un
  modelo de caché I y D de 8 KB, 4 vías, línea de 16 bytes, LRU (MCF54415).
- Escena: proyecto por defecto (tarjeta vacía), PLAY. 33.750 bloques de audio
  medidos (32 frames a 48 kHz, 1.500 bloques/s). Parado da casi lo mismo
  (52 % frente a 54 % de ocupación del emulador): el coste es el motor de audio,
  constante, no el secuenciador.
- Modelo de coste, ajustado en la unidad para código de AUDIO
  (`mods/md-st/WORK.md` en la rama de MD): **ciclos ≈ 0,85 × instrucciones +
  40 × fallos de caché**. Un bloque son 166.667 ciclos a 250 MHz.
- Comprobación del modelo con un dato de la unidad: el primer port de Chorus
  tocaba 5,4 KB de código y 2,0 KB de datos en DDR por bloque
  ([chorus README](../mods/chorus-st/README.md)); el modelo predice +12–14
  puntos y la unidad pasó de 88 % a 100 % (saturada). Encaja.

## Resultado

**La rutina de bloque de audio (`400a38ce`, un manejador de interrupción) es el
97,5 % de las instrucciones que ejecuta MAIN.** UI, secuenciador y tareas son
el 2,5 % restante.

Por bloque de audio, todo MAIN:

| | por bloque | ciclos modelo | % de MAIN a 250 MHz |
|---|---:|---:|---:|
| instrucciones | 72.400 | 61.500 | 36,9 % |
| fallos de I-cache | 1.466 | 58.600 | 35,2 % |
| fallos de D-cache | 206 | 8.200 | 4,9 % |
| **total** | | **128.400** | **77,0 %** |

El código que se ejecuta en cada bloque ocupa unos **22 KB** (1.394 líneas de
16 bytes fallan ≥ 0,9 veces por bloque) y la I-cache tiene 8 KB: cada bloque
recorre el código en streaming y casi cada línea caliente falla una vez por
bloque. De ahí salen dos reglas para valorar cualquier cambio:

- **1 KB menos de código ejecutado por bloque ≈ 64 fallos ≈ 1,5 puntos de MAIN.**
- **1.000 instrucciones menos por bloque ≈ 0,5 puntos.**
- El código lineal que corre una vez por bloque cuesta unos 10 ciclos por
  instrucción (casi todo en fallos); el de un bucle, unos 0,85.

### Funciones por coste propio (modelo)

| función | qué es | instr/bloque | I-miss | D-miss | % MAIN | tipo de código |
|---|---|---:|---:|---:|---:|---|
| `4008f242` | mezclador stock (llamado en `400a4fce`) | 6.086 | 207 | 28 | 8,8 | C compilado, 3,3 KB |
| `40096eb4` | reverb nativa | 8.655 | 136 | 7 | 7,9 | EMAC a mano |
| `400a38ce` | cuerpo del ISR de audio | 2.647 | 184 | 18 | 6,2 | C compilado, ~6 KB, ~90 llamadas |
| `400979ca` | suavizado de 938 parámetros de 16 bits | 7.203 | 17 | 0 | 4,1 | bucle EMAC |
| `4011942a` | LFO nativo | 3.311 | 42 | 26 | 3,3 | |
| `4008f0b6` | núcleo de mezcla (×5/bloque) | 5.488 | 10 | 0 | 3,0 | bucle EMAC |
| `4009f1be` | (×2/bloque) | 1.090 | 79 | 15 | 2,8 | C, dominado por fallos |
| `40096908` | delay nativo | 2.684 | 49 | 1 | 2,6 | EMAC a mano |
| `40097b24` | grupo de modulación (×51/bloque) | 4.736 | 4 | 0 | 2,5 | C, llamada con pila |
| `40096360` | | 2.460 | 32 | 6 | 2,2 | |
| `4008ef8c` | (×2/bloque) | 3.248 | 10 | 1 | 1,9 | bucle |
| `4008ffc0` | (×4/bloque) | 675 | 49 | 9 | 1,7 | C, dominado por fallos |
| `40090442` | | 275 | 50 | 4 | 1,4 | C, dominado por fallos |
| `40097b74` | modulación stock (Modulation Skip ya actúa aquí) | 1.878 | 19 | 0 | 1,4 | C |
| `40095e1c`, `4009590c` | | 709 | 75 | 8 | 2,4 | C, dominado por fallos |

Las 45 primeras suman el 70 % de MAIN; la lista completa sale con
`report.py rank`. El árbol inclusivo (`report.py tree`): el mezclador con sus
hijos (reverb, delay, núcleos) es el 39 % del bloque.

## Oportunidades, de más a menos rentable

### A. Bloquear media I-cache con código caliente (sin reescribir nada)

El V4 tiene `CACR[IHLCK]`: las vías 0–1 de la I-cache quedan fijas y solo se
reemplazan las 2–3. Con 256 líneas (4 KB, dos por conjunto) de código que se
ejecuta en cada bloque bloqueadas, el modelo baja **de 77,0 % a 71,7 %
(−5,3 puntos)**: se ahorran ~270 fallos por bloque y el resto, con solo dos
vías, pierde ~50. Como casi todo el código caliente falla una vez por bloque,
la elección exacta de las líneas importa poco.

**Implementado como módulo experimental del banco:
[`mods/icache-lock-st`](../mods/icache-lock-st/README.md)** (sin sitios, sin
parches). Comprobado:

- el procedimiento está en el manual del MCF5441x (6.4.5 "Cache Locking",
  tabla 6-3): tras invalidar, la caché asigna primero la vía inválida más
  baja; con IHLCK solo asigna en las vías 2–3; `INTOUCH` carga una línea;
- en MAIN stock solo hay un `movec cacr`, en el arranque (`4000055a`). En el
  core y los mods, cada cambio de código termina escribiendo `a40ce100`
  (invalidar I-cache y caché de saltos), que también quita el bloqueo: nunca
  queda código viejo bloqueado, y el módulo vuelve a bloquear una vez por
  segundo desde el tick;
- ninguna de esas invalidaciones ocurre por bloque: solo al instalar o al
  cambiar de modo.

Sin comprobar: el emulador no carga bancos SMZ4, así que la simulación es solo
con el stock. El código de los mods también compite por las dos vías
restantes, y la cifra real puede ser menor. La decide la unidad.

El equivalente con SRAM (4 KB de código caliente fuera de la caché) daría
−6,5 puntos, pero la MAIN SRAM libre ya la ocupan LFO3 y Chorus
(`<syntakt/sram.h>`).

De paso: el arranque escribe `CACR = a50ce100`. Según el manual, eso deja
activas las cachés de datos (por defecto sin caché; la DDR, copyback por
ACR0) y de instrucciones, el store buffer y la caché de saltos (bit 19, BEC).
No queda nada gratis por activar.

### B. Compactar el código C que se ejecuta una vez por bloque

Las funciones donde mandan los fallos (cuerpo del ISR, parte propia del
mezclador, `4009f1be`, `40090442`, `4008ffc0`, `40095e1c`, `4009590c`,
`4008e9f0`, `4009b73c`, `4008e520`, `4011a1ac`, `40095734`…) suman unos 800
fallos por bloque, **≈ 19 % de MAIN**. Son C compilado: cálculo de ganancias
por pista desenrollado, llamadas con argumentos en pila (`4008edc0` ×24,
`400a1792` ×14, `4008ff34` ×12 por bloque), constantes recargadas. Reescribirlas
en bucles de ensamblador compactos podría reducir su tamaño a la mitad y sus
instrucciones un 30 %: **estimación −8 a −10 puntos** si se hiciera con todas.

- La versión nueva, al ser más corta, cabe en el sitio de la stock: no gasta la
  reserva residente de 64 KB, que tiene solo 12–276 bytes libres.
- Debe ser bit-exacta: diferencial con `cfbench`/`cfdiff` de analog-emu como en
  los trabajos de MAIN anteriores, entradas reales y aleatorias.
- El mezclador lleva dentro los ganchos del core (`4008f420`, `4008f828`,
  `4008f952`, la llamada de `400a4fce`): reescribirlo obliga a rehacer esos
  ganchos en su nueva forma. Es el de más valor (8,8 % propio) y el de más
  riesgo.
- Empezar por una sola función y medirla en la unidad antes de seguir.

**Primer paso hecho: los núcleos de mezcla**
([`mods/mix-fast-st`](../mods/mix-fast-st/README.md)). Los tres núcleos de
mezcla que llama el mezclador (`4008f0b6` 8 pistas, `4008f024` 9, `4008ef8c`
10) suman cada bloque las rampas de ganancia muestra a muestra; cuando no hay
rampa (lo normal), esos incrementos son cero y las sumas y el store-back
sobran. El módulo redirige las tres entradas a versiones que, si todos los
incrementos son cero, ejecutan la misma secuencia MSAC sin ellas, y si no,
saltan al núcleo stock. Instrucciones por llamada: 686→437, 750→422, 812→469.
Probado con el diferencial exacto (XDIFF): 212.625 llamadas durante la
reproducción, todas idénticas al stock (registros, los cuatro acumuladores,
MACSR, SRAM, E/S), más 600 casos *fuzz* por núcleo con ganancias y muestras
extremas. Una limitación del método: la comparación de audio de reproducción
completa en el emulador no vale aquí, porque el `jmp` extra de cualquier
redirección desplaza el tiempo del emulador (acotado por instrucciones) y una
copia verbatim diverge igual; en hardware, a ritmo fijo, no cambia el audio.
Mejora de CPU pendiente de la unidad.

### C. Saltarse trabajo que no cambia nada (exacto)

- `400979ca` suaviza los 938 parámetros en cada bloque (4,1 %) y luego
  `memcpy` copia 1.612 bytes (0,6 %), aunque nada haya cambiado. Saltarlo
  cuando los destinos no cambian y los estados han convergido recuperaría
  hasta ~4 puntos en régimen estable. Hay que demostrar que el punto fijo es
  exacto (un filtro de un polo con redondeo puede oscilar ±1) y saber quién
  escribe los destinos, como hizo Modulation Skip.
- Reverb (7,9 %) y delay (2,6 %) trabajan aunque no reciban nada. Dormirlos
  con entrada y estado a cero es exacto si su salida es exactamente cero
  (hay que comprobar los términos de sesgo y redondeo). Solo ayuda cuando no
  se usan; no sube el techo con FX activos.

### D. Micro-optimizar los bucles DSP (poco rentable)

El all-pass de la reverb (`4009704c`, 10 instrucciones por muestra y etapa,
MAC con carga), el delay, el bucle de `400979ca` y los núcleos de mezcla ya
están planificados a mano. Estimación: como mucho un 10–15 % de sus
instrucciones, **≤ 1,5 puntos en total**, con alto riesgo de romper la
exactitud. No lo recomiendo como primer paso.

### Lo que no ayuda

- Reordenar código: el problema es el tamaño del recorrido por bloque, no los
  conflictos entre conjuntos.
- La D-cache: solo el 5 %. La configuración de caché stock es razonable (DDR
  copyback por ACR0, store buffer e I-cache activas).

## Resumen

| palanca | estimación (modelo) | esfuerzo | riesgo |
|---|---:|---|---|
| A. bloqueo de media I-cache | −5 puntos | hecho: `icache-lock-st` 0.1, pendiente de la unidad | bajo |
| B. compactar glue C por bloque | núcleos de mezcla hechos: `mix-fast-st` 0.1, pendiente de la unidad; resto −6 a −8 | grande | medio (exactitud, ganchos del core) |
| C. saltar suavizado/FX sin cambios | hasta −4 (suavizado); −10 con FX parados | medio | medio (demostrar exactitud) |
| D. bucles DSP | ≤ −1,5 | medio | alto |

No es una locura, pero el orden importa: primero A (barato y se mide con un
solo core), luego una función de B y medir, y C en paralelo si interesa el
caso sin FX. Ninguna cifra está medida en la unidad todavía. El proyecto por
defecto no es el patrón de referencia: con voces sonando cambian sobre todo
las partes por pista, no el coste fijo de mezcla y FX.

## Reproducir

Parche sobre analog-emu `4177ad4` (con sus cambios locales de Syntakt), en una
copia aparte para no tocar sus binarios:

```sh
git -C "$ANALOG_EMU_COPY" apply tools/stock-profile/analog-emu-xprof.patch
cargo build --release --bin dtboot          # en la copia, con su toolchain
DTEMU_NO_JIT=1 XPROF_CACHE=1 XPROF_OUT=prof.txt \
  "$ANALOG_EMU_COPY/target/release/dtboot" "$SYNTAKT_STOCK" \
  --instr 4500M --press PLAY@3500M --profile-after 4G
python tools/stock-profile/report.py rank prof.txt 45
python tools/stock-profile/report.py tree prof.txt 3
python tools/stock-profile/report.py lines prof.txt OUTDIR   # escribe ilock.txt, ifree{2,4,8}.txt
XPROF_ILOCK=OUTDIR/ilock.txt ...            # misma ejecución, simulando el bloqueo
XPROF_IFREE=OUTDIR/ifree4.txt ...           # o 4 KB fuera de la caché
```

Sin `XPROF_CACHE` y sin `DTEMU_NO_JIT` la cuenta de instrucciones es la misma
y va más rápido. Para el listado anotado, desensamblar la sección MAIN
descomprimida con el objdump de SGDK:
`objdump -D -b binary -m m68k:isa-c:emac --adjust-vma=0x40000400 main.bin`
y `report.py ann prof.txt listado.txt 4008f242`. Ni el firmware ni sus
secciones se guardan en el repositorio.
