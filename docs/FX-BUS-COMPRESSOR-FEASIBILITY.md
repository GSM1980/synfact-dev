# Compresor Digitakt en el bus FX del Syntakt

Evaluación del 2026-11-20, desde `origin/main` en `ce91a3b` (OS 1.42).
Incluye la investigación inicial y la integración posterior **comp-st 0.2.0**
con core 2.4.0. El apartado final recoge la medición del módulo integrado y
sus límites. No se ha instalado ni probado en el instrumento.

**Actualización 0.2.0:** añade **484–509 instrucciones comprimiendo**, 43
apagado, 377–379 bajo umbral y 485–510 al cambiar controles. Recupera el
margen vacío de LFO3 para 256 bytes de código, 56 de estado y 8 de guardas
en SRAM, conservando Chorus. El apartado final documenta la optimización.
Las cifras anteriores se conservan debajo como antecedentes, no como el
estado actual del banco.

## Resultado y alcance del routing

El objetivo es comprimir **exclusivamente lo enrutado a la pista FX**.
Las pistas y retornos fuera de FX no alimentan el detector ni reciben reducción
de ganancia. El VOL del Chorus entra en `ST_FX_TRACK_BUS`; sus retornos DEL/REV
dependen del routing de esos efectos.

El núcleo original de cálculo de ganancia de Digitakt cuesta **2.896–3.051
instrucciones por bloque de 32 frames**, en las escenas probadas. Una reserva
de diseño razonable es **3.500 instrucciones/bloque** para núcleo y adaptador
ADC/VCA. La reserva incluye unos 450 adicionales sobre el máximo observado;
el adaptador todavía no está escrito ni medido y esto no es un límite WCET.

Con Chorus 1.2.1 la suma presupuestada es aproximadamente **5.700–7.500
instrucciones/bloque** en los casos siguientes. La integración es viable por
tamaño en DDR; **no cabe otro núcleo de este tamaño en las ventanas SRAM
actuales junto a Chorus y LFO3**. La viabilidad temporal es plausible, pero
depende de la caché y del margen MAIN del banco completo.

## Medición del código original

`tools/bench_st_digitakt_compressor.py` lee el SysEx del propietario y exige
Digitakt 1.54 SHA-256:

`f78ba80fa7b1da5fb0e1ff61ad61e9e71aafe79f4364fc49679f3651353e3cf6`.

MAIN decodificado:
`5c58bf9e3949ef09977c5fc007a61e8d026931f67f1621238379dfb8ee4d31a2`.

Rutina original `[4007425e,4007472a)`, **1.228 bytes**, SHA-256:
`5c3b022df757ab8ec86b50c38dd85a3220ba880d49c96232573d205bf9a5b023`.

Se ejecuta en Unicorn ColdFire/EMAC con estado inicializado por la rutina
original `400741d8`, sin sustituir llamadas DSP. Se prueban silencio, continua,
dos senos, ruido, impulsos y extremos signed32; para cada señal, seis presets
con controles normales, extremos de ratio/ataque, selección de sidechain y
filtro. Cada caso tiene ocho bloques de preparación y ocho medidos.

| Medida | Instrucciones por 32 frames |
|---|---:|
| Compresor digital original completo | 3.860–4.015 |
| Cálculo de ganancia conservado para un actuador VCA, laboratorio | 2.896–3.051 |
| Reserva de diseño para el futuro módulo FX | 3.500 |

La variante de laboratorio cambia únicamente RAM emulada: en `40074644`
salta al epílogo `40074714`, después de calcular y suavizar las 32 ganancias.
Evita multiplicar el PCM, reescalarlo y mezclar dry/wet digital. En los **288
bloques medidos por variante**, la última ganancia coincide con la rutina
completa en ese mismo emulador. No se guarda una imagen parcheada.

Caso de senos con controles normales:

| Etapa | Original | Variante para extraer ganancia |
|---|---:|---:|
| Entrada, selección/filtro de sidechain | 529 | 529 |
| Pico estéreo | 517 | 517 |
| Logaritmo, umbral y ratio | 840 | 840 |
| Release y peak hold | 295 | 295 |
| Ataque, makeup y exponencial | 633 | 633 |
| Suavizado de ganancia | 196 | 196 |
| Aplicación al PCM | 294 | 1 (salto del laboratorio) |
| Reescalado x16 del PCM | 262 | 0 |
| Dry/wet y salida | 414 | 5 (epílogo) |
| **Total** | **3.980** | **3.016** |

Son cuentas de instrucciones invitadas. No son ciclos, porcentaje de CPU,
prueba de peor caso, certificación numérica de EMAC físico ni simulación del
sistema completo. Los cambios continuos de parámetros y todos los estados
posibles del detector quedan fuera de esta matriz acotada.

## Punto de aplicación: solo FX

Compartir CPU/ISA permite reutilizar el DSP, pero no iguala las topologías.
`ST_FX_TRACK_BUS` (`8000dad0`, `syntakt/mix.h`) es el bus digital anterior al
bloque analógico FX. Aplicar allí el compresor digital no demuestra comprimir
todo lo que el instrumento puede enrutar a FX por su circuito analógico.

Para el bus FX completo, el diseño a estudiar es detector alimentado por la
captura ADC del bus FX y actuador en sus VCA izquierdo/derecho. La investigación
histórica identifica ADC 6/7 y CV 35/36 para ese bus. La comparación directa de
stock 1.42 confirma el clamp y las escrituras `hw+70/+72` en
`400905b6..400905da` (el sitio histórico de 1.41 era `4009053e`). Esto identifica
un candidato; no constituye un hook implementado ni una validación de todas
las fases ADC/CV en 1.42.

La ganancia de Digitakt se calcula por muestra; el control de esos VCA se
actualiza por bloque. Conservar el detector y usar su ganancia final **no da
una salida idéntica muestra a muestra** al compresor digital de Digitakt.
Hay que comprobar escala/polaridad ADC, ley del VCA, clamps, orden de
captura/escritura y latencia. No se incluye ni se presupuesta aquí un dry/wet
analógico equivalente al MIX digital de Digitakt.

No existe un evento CV publicado equivalente a los eventos de mezcla del
core. Una implementación posterior debe resolver ese punto como servicio
del core, con su ABI y coste, sin ocupar otro sitio de audio desde el módulo.

## Con el chorus de main remoto

Se ha compilado Chorus 1.2.1 desde este worktree, con sus tablas reconstruidas
desde Digitone 1.44 y verificación de entrada Syntakt 1.42. Banco de laboratorio
solo Chorus: 18.896 bytes, MAIN 15.592 bytes. Se ejecutó el benchmark existente
`tools/bench_st_chorus.py`: diez escenas con SRAM y las mismas diez forzando
DDR. Las cuentas reproducen el README vigente; los stubs de eventos del core
no están incluidos.

| Escena | Chorus medido | + reserva FX de 3.500 |
|---|---:|---:|
| Un envío, VOL | 2.183,75 | 5.683,75 |
| Cuatro envíos, VOL | 2.360,75 | 5.860,75 |
| Doce envíos, VOL | 3.053,75 | 6.553,75 |
| Doce envíos, DEL+REV+VOL | 3.802,75 | 7.302,75 |
| Tres envíos, retornos moviéndose | 4.036,75 | 7.536,75 |

La suma es aritmética entre una medida y una reserva de diseño. No es una
medición de ambos integrados. El número de pistas en FX no debería multiplicar
el coste del detector: se analiza un único bus estéreo de 32 frames.

## Memoria y margen temporal

Chorus ocupa 468 bytes de estado y 1.640 de código en la primera ventana
SRAM (4 bytes libres); su segunda ventana usa 874 de 884 bytes (10 libres).
Esos 14 bytes no alojan los 1.228 bytes del DSP. Las colas no asignadas de
SRAM no se consideran espacio disponible sin auditoría.

En DDR, código, tablas originales, scratch, estado y adaptador sugieren un
orden de **16–20 KiB adicionales**, sin contar una UI completa. El builder
permite 2 MiB para MAIN del banco; el tamaño por sí solo no parece el límite.
No se ha enlazado un banco completo con compresor ni reclamado memoria nueva.

El original tocó hasta **1.168 bytes de líneas de código y 1.120 de tablas**
en un bloque. Son huellas de líneas de 16 bytes, no fallos medidos de caché.
El modelo de `STOCK-MAIN-CPU-PROFILE.md` usa 0,85 ciclos/instrucción y
40 ciclos/fallo: si esas líneas se recargasen una vez por bloque, una reserva
de 3.500 instrucciones más ese tráfico costaría unos **5,2 puntos MAIN** a
250 MHz. Presupuestar **5–6 puntos adicionales sobre el mismo banco con
Chorus** es una aproximación para decidir si prototipar; no es lectura del
medidor ni techo. La contención con el resto del banco puede cambiarla.

Con alrededor de diez puntos MAIN libres manteniendo Chorus, hay un margen
razonable para el prototipo. Con solo dos o tres puntos libres no se puede
dar por válido este diseño. Las cifras stock 77,0 % o 71,7 % con I-cache lock
no son el consumo actual del banco del usuario y no se usan como base real.

## Reproducción y estado

```sh
python -B tools/bench_st_digitakt_compressor.py --digitakt "$DIGITAKT_OS" --out out/fx-compressor.json
python -B mods/chorus-st/tools/import_digitone.py "$DIGITONE_OS"
python -B -m elekloader.sdk.syntakt.smz --module mods/chorus-st --stock "$SYNTAKT_STOCK" --out out/fx-study/chorus.smz4
python -B tools/bench_st_chorus.py --bank out/fx-study/chorus.smz4 --out out/fx-study/chorus.json
```

Usar el entorno de compilación de `SYNTAKT-BUILD.md` y Unicorn ColdFire con
EMAC. `DIGITAKT_OS` es la ruta del SysEx original 1.54 del propietario.
La herramienta no incorpora firmware ni escribe direcciones del instrumento.

Resultado de comprobaciones: 36 casos / 288 bloques medidos por variante del
compresor; 20 escenarios/ubicaciones del benchmark Chorus; suite
`tests/test_units.py`: **39 passed, 0 failed**. No se ha ejecutado la matriz
de arranque ni la suite de una entrega de firmware: no hay entrega nueva.
Pendientes: adaptador real del bus FX, controles/persistencia, medición del
banco integrado y CPU/audio físicos. Ningún envío ni acceso al dispositivo.

## Compresor compacto Synthack: medición posterior

Fuente propuesta por el usuario, fijada en commit
[`c12cf23ad88e68d3f85422a7d09c2a911659997f`](https://github.com/Angelo-politek/Synthack-OS/blob/c12cf23ad88e68d3f85422a7d09c2a911659997f/mods/master-comp/comp.S).
SHA-256 de `comp.S`:
`83a1afe4ed83f3cc15657c567d358a556fcd1e32c75d2247bd53956fa097f3ce`.

Es la arquitectura adecuada para el bus FX: detector ADC 6/7 y actuador en
CV 35/36. El README de esa revisión aclara que el nombre `master-comp` se
conserva por compatibilidad y que actúa solamente sobre el routing FX.

No es una copia literal del DSP original: reemplaza el detector por muestra
por el pico del bloque y resuelve ataque/release con a^32, r^32 y un término
cruzado. Los coeficientes se recalculan cuando cambian los controles. Las
curvas siguen el diseño Digitakt; la trayectoria de ganancia puede diferir
respecto del original por muestra. Las tablas log/exp tienen 17 puntos e
interpolación, evitando las grandes tablas originales.

Se ensambló el `comp.S` completo sin modificarlo con SGDK, `-mcpu=54415`,
usando los generadores de tablas/constantes del propio proyecto. Solo se
ejecutó `comp_hook` y sus subrutinas en RAM emulada, con buffers ADC, Kit,
raíz UI y estructura CV simulados. No se ejecutó el builder de firmware
externo ni se aplicaron sus parches al Syntakt.

| Estado | Instrucciones por bloque de 32 frames |
|---|---:|
| Apagado, valores estables | 98 |
| Encendido, valores estables | 564–611 |
| Encendido, valores cambiando cada bloque | 1.013–1.059 |
| Primer bloque encendido, incluyendo conversión inicial | 1.016–1.044 |

Incluye comprobación del Kit/controles, detector, medidor GR, escalado/clamp
de los dos CV y preservación de registros/MACSR. Excluye el JSR/salto del
caller, setters UI y adaptación al core Synfact. Se midieron 18 escenarios
(seis señales por tres modos), 48 bloques por escenario después de 16 de
preparación: **864 bloques medidos**. Los máximos observados no son WCET.
Estas cuentas no certifican la equivalencia numérica del EMAC físico.

| Escena | Chorus medido | + compresor estable, 611 | + controles cambiando, 1.059 |
|---|---:|---:|---:|
| Un envío, VOL | 2.183,75 | 2.794,75 | 3.242,75 |
| Doce envíos, VOL | 3.053,75 | 3.664,75 | 4.112,75 |
| Doce envíos, DEL+REV+VOL | 3.802,75 | 4.413,75 | 4.861,75 |
| Tres envíos, retornos moviéndose | 4.036,75 | 4.647,75 | 5.095,75 |

Son sumas de benchmarks separados. Reservar alrededor de **650 instrucciones
en régimen y 1.100 durante cambios** es más apropiado para este compacto que
las 3.500 de la propuesta inicial. El último caso cubre cambios simultáneos
del chorus y compresor dentro de estas escenas, sin ser un máximo universal.

El blob completo con UI y datos generados ocupa 8.060 bytes; texto, 1.892.
Un bloque activo de senos tocó hasta 752 bytes de líneas de código y 128 de
datos del blob; durante cambios, 1.136 y 208. No son fallos de caché. La huella
es menor que la del original, pero no queda SRAM suficiente para copiarlo
entero con Chorus/LFO3. No se traslada al compacto la estimación anterior de
5–6 puntos MAIN: falta medir su coste de caché dentro del banco completo.

El código enlazado conserva direcciones de Syntakt 1.41 y su almacenamiento
propio. Integrarlo en main remoto requiere portar las referencias a 1.42,
encajarlo en los servicios/IDs del core y revisar los campos persistidos.
No es un módulo listo para copiar sobre el banco actual.

Reproducción, con el checkout fuente en esa revisión y el entorno Unicorn:

```sh
python -B tools/bench_st_synthack_compact.py --source "$SYNTHACK_SRC" --toolchain "$SGDK_BIN" --work out/compact-bench --out out/compact-bench.json
```

La herramienta no incorpora fuentes externas ni firmware; requiere el
checkout del propietario como entrada y genera únicamente el laboratorio.

## Integración comp-st 0.1.0 (core 2.4.0)

Implementado sobre `origin/main` ce91a3b: solo el bus FX, controles originales
de Digitakt 1.54 extraídos localmente del SysEx. THR/ATK/REL/MUP en A-D, RAT
en E y MIX en H; F/G vacíos. No hay SCS ni SCF. MIX=0 es bypass y el valor
por defecto. La interpolación de ganancia añade cinco instrucciones activas
y una conversión de coeficiente cuando cambia MIX; no necesita un segundo
bucle de audio. El DSP es el compacto Synthack, con la UI de Digitakt.

Medición final de `tools/bench_st_comp.py`, incluyendo el punto compartido
del core, consulta de ajustes, detector, medidor con destino UI válido, MIX
y VCA, descontando la ventana nativa original:

| Estado | Instrucciones extra por bloque de 32 frames estéreo |
|---|---:|
| MIX=0 | 187 |
| Activo, controles quietos | 657–699 |
| Controles en movimiento | 1017–1052 |
| Core sin suscriptor COMP | 7 |

El banco completo contiene Chorus y COMP. En veinte escenas, Chorus produce
exactamente los mismos buses y conserva el mismo conteo que el banco de
referencia sin COMP, tanto con SRAM disponible como ocupada. Sumando los
benchmarks de efectos separados con la cota activa de COMP:

| Escena | Chorus + COMP, instrucciones/bloque aproximadas |
|---|---:|
| 1 envío, retorno VOL | 2883 |
| 4 envíos, retorno VOL | 3060 |
| 12 envíos, retorno VOL | 3753 |
| 12 envíos, DEL+REV+VOL | 4502 |
| Chorus y controles COMP en movimiento, escena de 3 envíos | 5089 |

COMP usa DDR del banco, sin ocupar ventanas SRAM del chorus o LFO3. El core
enlazado ocupa 55.860/65.536 bytes residentes: quedan **9.676 bytes libres**.
El banco completo ocupa 729.808 bytes (591.508 MAIN, 108.640 AUDIO, más
cabeceras/relocaciones). Sus seis valores Q8 se guardan en los 16 bytes
`15d0..15e0` del Kit codificado, entre LFO3 y Playhead. No se amplía el SysEx
nativo de Kit; ese transporte no incluye los valores COMP en esta versión.

Las cifras respaldan que **cabe con Chorus** en código, memoria e
instrucciones de estas escenas. No son ciclos ni porcentaje de CPU; queda
pendiente comprobar margen de caché/tiempo real y audio en el instrumento.
El ensayo no envía MIDI/USB ni instala firmware.

## Medidor GR, comp-st 0.1.1

Se añade la barra vertical de reducción de ganancia siguiendo el dibujo
nativo de Digitakt 1.54 (`40038f80`, lectura GR `400784e0`): cuantización de
medio dB y 50 píxeles para 31,5 dB, creciendo hacia abajo. Se satura dentro
de su rectángulo. La columna original x=126 pisa los indicadores de página
del Syntakt; aquí ocupa **x=122..123, y=1..50**, dentro del hueco libre
120..124. MUP y MIX/DRY–CMP llegan a x=119; los indicadores están en 125..127.

El dato procede del medidor suavizado que ya ejecutaba el DSP. Solo cambia
la constante de escala del aviso de refresco, de 18 a 31,5 dB; no se añaden
instrucciones al bucle de audio ni cambia el cálculo de ganancia. El dibujo
limpia su propio rectángulo antes de pintar, y MIX=0 muestra la barra vacía.
El core/SYX 2.4.0 es idéntico al entregado con 0.1.0.

La prueba nueva ejecuta el cálculo original de Digitakt y compara 36 pantallas
nativas a distintos valores y extremos de controles: todos los píxeles fuera
de la barra, incluidos los indicadores, quedan intactos. Comprueba también
la caída y el borrado sin limpiar antes el framebuffer, y la subida/caída y
el refresco a partir de audio real procesado por el compacto en emulación.

Repetida la medición de 0.1.1: 187 instrucciones extra apagado, 657–699
activo y 1.017–1.061 durante cambios. Cambia la frecuencia de las ramas de
publicación del medidor por la escala ampliada; no se añade trabajo DSP de
ganancia. El banco 0.1.1 ocupa 730.064 bytes, 256 más que 0.1.0; AUDIO y
core/SYX permanecen iguales.

## Optimización 0.2.0: primero COMP, después SRAM

El cálculo sigue siendo digital y el actuador sigue siendo el VCA AMP
izquierdo/derecho del bus FX, post DRIVE/filtro. No cambia el routing, la
curva, los controles ni el dibujo/escala de GR.

Se elimina el wrapper C de audio y sus prólogos anidados, el copiado y
comparación de seis controles en cada bloque y la conversión de coeficientes
en IRQ. La tarea prepara solamente los coeficientes afectados y publica una
fila de 40 bytes bajo exclusión IRQ. Una tabla de fórmulas de unos 66 KiB
conserva los 16.384 pares ATK/REL con la truncación real del EMAC. El detector
recorre los 64 valores ADC contiguos usando el signo de MOVE; la ruta apagada
no guarda registros en pila. Un límite conservador permite saltar log,
envolvente y exp únicamente con estado cero y nivel por debajo de umbral.
MIX/VCA usan multiplicaciones de 16 bits con operandos acotados.

El medidor deja de dividir y solicitar repintados globales desde audio. La
tarea invalida solo COMP visible cuando cambia su altura en píxeles. También
se elimina un segundo dibujo completo de los seis controles: el evento DRAW
ya llega después del método nativo de la vista. Las pruebas ejecutan ahora
ese método real y exigen exactamente una pintura nativa por frame.

La auditoría del **binario enlazado actual** corrige el dato SRAM histórico:
LFO3 ocupa **822/1.152 bytes**, dejando **330**, no los 64 calculados con su
antiguo tamaño de 1.088. LFO3 1.3.3 conserva sus 822 bytes y 10 de alineación
hasta `80007680`. COMP aprovecha `80007680..80007780` para 256 bytes de
detector/curva/escalado y `80007780..800077c0` para 56 de estado y 8 de
guardas. Los otros márgenes son 4 bytes en Chorus estado/código, 10 en sus
kernels y cero en el banco LFO3 de 944 bytes; quedan además cabeceras de
alineación de 16 bytes al inicio de cada cola. No son código ejecutado inútil.
No se desplaza ninguna rutina de Chorus/LFO3, ni se usa memoria no auditada.

Se probaron desenrollados de 2/4/8/16/32/64 muestras: en DDR, cuatro daba el
menor proxy con caché fría; más código apenas ahorra instrucciones y sube
los fallos potenciales. En SRAM cabe el de ocho junto con curva/VCA.
Ambas copias tienen comprobación y alternativa DDR independiente. Las
comprobaciones periódicas recuperan DDR conservando la historia DSP. La
invalidación de I-cache se hace al instalar la copia, nunca por bloque.

`icache-lock-st` conserva sus dos vías/4 KiB de líneas stock perfiladas;
las otras dos siguen disponibles para el banco. COMP no ocupa líneas
bloqueadas ni provoca invalidaciones por bloque. Sacar su núcleo a SRAM
evita competir por esa caché sin perder la protección del audio nativo.

| Instrucciones adicionales / 32 frames estéreo | 0.1.1 | 0.2.0 |
|---|---:|---:|
| MIX=0 estable | 187 | 43 |
| Activo, bajo umbral y estado cero | incluido en 657–699 | 377–379 |
| Comprimiendo, controles quietos | 657–699 | 484–509 |
| Cambiando controles, solo audio IRQ | 1.017–1.061 | 485–510 |

En una escena de compresión, el código toca 65 líneas I-cache y 10 líneas de
datos del banco antes; 50/7 con COMP optimizado en DDR; **35/4 con SRAM**.
Cada línea son 16 bytes. El proxy frío `0,85*instrucciones + 40*líneas` pasa
de 3.564 a 2.695 y a 1.975 ciclos. Son límites de huella suponiendo un fallo
por línea, no simulación del tráfico intercalado del firmware ni CPU física.
El setter también se mide: 253–255 instrucciones antes, 400–465 ahora en
la tarea; la conversión deja de bloquear el audio. No se presenta la mejora
de IRQ como un porcentaje global de CPU. La versión GCC de copia por
asignación de structs resultó 36 instrucciones peor por setter y se descartó.

La validación añade 2.624 bloques contra 0.1.1 en DDR y otros 2.624 en SRAM,
comparando salida, envolventes y GR exactamente, incluyendo controles Q8,
todos los MUP enteros, extremos ADC/VCA y cambios de Kit. Otros 320 bloques
verifican alternativas DDR por ocupación y corrupción, con vecinos intactos.
Se ejecuta el instalador real de LFO3 en ambos órdenes con COMP y el de Chorus
con todas sus copias SRAM activas. Se conservan las 36 pantallas exactas del
medidor, codecs y convivencia de proveedores. El core 2.4.0 y sus secciones
protegidas son idénticos a 0.1.1; solo cambia el banco. Sigue pendiente medir
CPU y escuchar en la unidad.

El 2026-11-20 el propietario informa de que comp-st 0.2.0 funciona bien en
la unidad. No se ha recibido una medida física de CPU. Se mantiene el núcleo
compacto de Synthack-OS: no se incorpora el DSP original de Digitakt. Las
comparaciones bit exactas de la optimización son frente a comp-st 0.1.1;
no certifican identidad con un binario original de Synthack-OS o Digitakt.
