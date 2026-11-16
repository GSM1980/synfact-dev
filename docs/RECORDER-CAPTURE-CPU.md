# Recorder 1.3: MAIN LR estéreo

El caso reportado es MAIN LR, STEREO y POST FX: CPU MAIN del 92% en PLAY y
99% al grabar. Esas cifras proceden del instrumento del usuario; las medidas
de esta página son instrucciones ejecutadas en analog-emu, no ciclos ni
porcentajes de CPU física.

## Cambios

- El bloque completo de 32 frames convierte directamente a PCM16 y calcula
  los picos de ambos canales en una pasada. El bucle en ensamblador procesa
  cuatro frames por iteración y evita dos arrays temporales, una segunda
  lectura y comprobar estado/límite por muestra.
- Las correcciones PRE/POST usan EMAC entero con el desplazamiento de cada
  producto antes de sumarlo. Se conserva el redondeo de valores impares y
  el operando +32768; no se estrechan los operandos a signed word.
- La rutina guarda y restaura ACC0, ACCext01 y MACSR físico. ACC1..3 y MASK
  permanecen intactos, incluidos flags y extensiones de los acumuladores.
- Un banco de correcciones conocido como cero evita volver a escribir sus
  64 valores y permite copiar el high word de cada entrada USB directamente.
  El cache es del IRQ y se invalida bajo exclusión al primar otra toma.
- ARMED, el disparo por umbral, bloques sin corrección primada y finales
  parciales mantienen el camino general. Se procesan todos los frames a 48 kHz.

El core conserva su versión 1.9.0 y no incorpora nuevos sitios ni consume
más RAM residente. Recorder pasa de 1.2 a 1.3 en el banco SMZ4.

## Medición del callback real

Comparación del banco entregado con Recorder 1.2 y el nuevo banco, relocalizados
por separado. La medida excluye configuración, primer bloque y comprobaciones.
Son instrucciones de `recorder_audio` por bloque de 32 frames, con tres bloques
estables por caso; no incluye toda la interrupción de audio ni la carga de UI.

| Caso estéreo MAIN LR | Recorder 1.2 | Recorder 1.3 | Reducción aproximada |
|---|---:|---:|---:|
| POST, retornos FX activos | 4.253 | 2.222 | 48% |
| POST, ganancias de retorno cero | 2.934 | 684 | 77% |
| POST, retornos silenciosos con ganancias activas | 3.581 | 1.330 | 63% |
| PRE, retornos activos | 4.067 | 2.028 | 50% |

MAIN LR mono comparte el nuevo cálculo de retornos EMAC y el cache de ceros;
ya tenía una ruta C directa de captura. En los mismos fixtures POST pasa de
**2.995 a 2.664 instrucciones** con retornos activos (11% menos), de 1.681 a
1.462 con ganancias cero (13% menos) y de 2.328 a 2.108 con retornos silenciosos
(9% menos). PRE con retornos activos pasa de 2.810 a 2.478 (12% menos).
IN y TRK mantienen su captura anterior; estas cifras corresponden a MAIN.

La integración de Recorder 1.4 con main conserva además su salida rápida de
callback cerrado y omite llamar a preview cuando no hay reproducción. Con el
mismo banco de referencia, los casos de la tabla quedan en 681, 2.219, 1.327
y 2.025 instrucciones; los cuatro casos mono, en 1.459, 2.661, 2.105 y 2.475.

La cantidad exacta cambia ligeramente con las ramas de pico y saturación.
No se puede convertir el 48% del callback en una reducción equivalente del
99% total: el patrón, los otros módulos y el mezclador siguen trabajando.

## Verificación offline

`tests/test_st_recorder_capture_cfbench.py` ejecuta código real de ambos
bancos en analog-emu con EMAC. Compara PCM exacto, picos/estado/reloj,
los dos bancos de correcciones y el estado físico completo de EMAC.

Además de ocho escenas fijas mono/estéreo de 32 bloques, ejecuta 2.048 bloques con
entradas aleatorias y extremas y cambios entre ganancias cero y no cero,
casos de finales de 47 frames y ARMED/umbral PRE/POST, y 512 vectores de
los dos kernels contra un oráculo entero independiente. Incluye saturación,
productos impares, ganancias ±32768, MACSR/extensiones/MASK variados y una
comprobación explícita de la ABI del kernel PCM. Los controles negativos
corrompen una muestra/corrección y deben ser detectados.

```powershell
python tests/test_st_recorder_capture_cfbench.py --reference-bank <banco-1.2> --bank <banco-1.3> --cfbench <cfbench.exe> --out <carpeta-nueva>
python tests/test_st_common_regressions.py <build> --cc <i686-clang> --previous-build <build-1.2> --unicorn-python <python>
```

## Prueba en el instrumento

Mantener el core 1.9.0 y actualizar el banco. En el mismo patrón y con los
mismos MAIN LR/STEREO/POST FX, comparar MAIN CPU en PLAY y durante grabación.
Comprobar ambas salidas estéreo, el final por steps, normalización, recorte,
FUNC+YES y guardado. Salir con una toma sin guardar, reanudar PLAY fuera y
volver al recorder: debe parar y recuperar el modo reducido sin perder PCM
ni recorte. PLAY permanece bloqueado mientras se edita; FUNC+NO descarta y
restaura el procesamiento completo. El ciclo del bypass se documenta en
[RECORDER-AUDIO-PAUSE.md](RECORDER-AUDIO-PAUSE.md).
