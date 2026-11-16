# Recorder: audio reducido después de grabar

Implementado el 2026-11-16 en **core 1.9.0 + Recorder 1.2**, como build
experimental. Requiere instalar ambos componentes; el recorder nuevo rechaza
un core que no exponga host API 15.

La toma terminada solicita el modo reducido al siguiente bloque de MAIN,
siempre que el transporte esté parado. Mientras se normaliza, edita o guarda,
se omiten el mezclador musical nativo, delay/reverb/chorus, los cuerpos de
modulación/LFO y los observadores render-in. El recorder conserva su reloj
y la preescucha de la selección normalizada. La rutina nativa de MAIN sigue
atendiendo DMA, controles, CPU2 y las transferencias del sample pool; su etapa
final entrega los bloques de silencio/preview a USB y al codec.

| Situación | Comportamiento |
|---|---|
| Recorder vacío, ARMED o grabando | Audio normal |
| STOP o final por steps, con toma y transporte parado | Audio reducido |
| Normalizar, editar, nombrar, asignar o guardar | Mantiene audio reducido |
| FUNC+YES en READY | Preview mono/estéreo de la selección |
| PLAY en la sesión de toma terminada | Consume pulsación, repetición y liberación, incluidos los modales |
| Cerrar el recorder, cubrirlo con una vista ajena o descartar la toma | Restaura audio normal al siguiente bloque |
| Volver a entrar con una toma retenida | Para el transporte y recupera audio reducido, conservando PCM y recorte |
| FUNC+NO dentro del recorder | Descarta la toma y recupera audio normal |

Recorder 1.3 mantiene el transporte parado mientras posee una toma terminada,
también al reabrirla o terminar la captura con YES. Un START externo hace que
el core ejecute audio normal mientras el transporte esté en PLAY; el siguiente
tick de UI emite STOP y recupera el modo reducido. No se engancha el transporte
MIDI. FUNC+YES sigue bloqueado mientras se normaliza el buffer en el mismo lugar.

Recorder 1.4 hace explícito el STOP manual: al terminar una captura llama a la operación
nativa de parada desde el evento de UI y consume pulsación, repetición y
liberación. No exige alcanzar `sam_limit` ni depende de la propagación de STOP
por la vista opaca. El siguiente bloque entra en la ruta reducida, incluso al
terminar antes de cualquiera de los límites configurados. Se prueban las nueve
longitudes, mono/estéreo, tres estados de transporte y STOP/FUNC+STOP: 108 casos.

**Límite experimental:** las líneas de delay/reverb conservan su estado
congelado, de modo que sus colas pueden continuar al salir. Se mantienen la
síntesis en AUDIO y los canales USB por pista. El ahorro es de MAIN, que
comparte CPU con normalización/guardado; la velocidad física de eMMC no cambia.

## Implementación

El servicio genérico está en `mods/core-st/audio-idle.c` y `audio-idle.s`.
Host API 15 añade un único slot de propietario a partir del offset 100,
preservando el prefijo anterior. El propietario publica un descriptor
inmutable durante init; un fallo posterior de activación del banco lo retira.
El core comprueba el transporte y la elegibilidad una vez al entrar en cada
bloque. Sin propietario o sin una toma válida ejecuta el camino normal.

El sitio nuevo es la llamada de mezcla `400a4fce`. La salida normal llama a
`4008f1ca`; la reducida llama a `mods/recorder-st/idle.s`, que conserva la ABI
nativa, limpia seis buses, ejecuta reloj/preview y reutiliza la salida nativa
en `4008fb38`. Render-out y su contabilidad siguen ejecutándose. No se cambian
el timer, el vector de interrupción, el protocolo con AUDIO ni el formato SMZ.

La selección de vista acepta el recorder y sus propios diálogos de nombre y
asignación. Cerrar o destruir la vista deja de solicitar el modo; el core no
conserva un puntero al objeto UI. PLAY también consume una liberación que
llegue después de cerrar, para no reenviar un borde sin su pulsación.

El core ocupa **65.524 / 65.536 bytes**, con **12 bytes libres**. Se conserva
el perfil de compilación documentado, sin ampliar el límite de memoria.

La integración con main conserva el arreglo de vistas y las rutas nativas de
core 1.8.2. Para mantener el límite residente, el dispatcher de vistas calcula
su máscara de fase fuera del recorrido de callbacks, y la clasificación de
los dos módulos que parchean durante init comparte una comparación completa
de nombre. Se mantienen el orden, los retornos de consumo y la publicación
atómica; las pruebas cubren fases KEY, ENC/DRAW y nombres con prefijos/sufijos.

## Validación

- Regresiones comunes completas: PASS, incluidos los 39 unitarios, pruebas
  del recorder/guardado/HZFS/CRC, sample pool, parser SMZ, cambios de transporte,
  ABI, instrucciones y escrituras de safe-boot ejecutadas en Unicorn.
- 96 combinaciones de estado/fuente y pruebas del ciclo de vida, preview,
  PLAY bloqueado, modales y fallo de inicialización del banco: PASS.
- `tests/test_st_audio_idle_cfbench.py`, usando analog-emu: PASS. Ejecuta la
  salida nativa real con EMAC; compara los 256 words del frame del codec con
  el mezclador original completo y USB MAIN con una referencia matemática.
  Comprueba mono/estéreo, saturación, registros, estado EMAC físico completo,
  fallback normal y un control negativo que detecta una fuga de ADC.
- La llamada de mezcla aislada pasa de **33.449 a 8.195 instrucciones** en el
  mismo fixture con preview. No es una medida de la interrupción completa,
  un porcentaje de CPU física ni una medición de velocidad de guardado.
- AUDIO del core y del banco permanece idéntico al build de referencia.
  Bootloader/bootstrap/updater y destinos permanecen sin cambios.
- Se omite una comparación opcional de FM contra el árbol de referencia
  Synfact, que no se proporcionó. No se ha accedido al instrumento.

Para repetir las pruebas, configurar las herramientas como en SETUP.md:

```powershell
python tests/test_st_common_regressions.py <build> --cc <i686-clang> --previous-build <referencia> --unicorn-python <python>
python tests/test_st_audio_idle_cfbench.py --build <build> --cfbench <cfbench.exe> --out <carpeta-nueva>
```

## Prueba en el instrumento

Recorder 1.3/1.4 cambia solo el banco; conserva el core 1.9.0 anterior. Con el mismo proyecto, comparar
CPU MAIN y tiempos al terminar una toma por STOP y otra por número de steps.
Probar PLAY (debe ignorarse), FUNC+YES mono/estéreo, recorte, nombre/asignación,
guardado y salida con NO. Al salir comprobar que vuelven el audio y el
transporte. Reabrir sin guardar debe volver al modo reducido sin perder la
toma ni su recorte; FUNC+NO debe permitir PLAY y preparar otra toma. El rendimiento real,
las salidas físicas y la restauración de colas siguen pendientes de esta
prueba en hardware.
