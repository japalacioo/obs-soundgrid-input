# Informe de pruebas, v0.3.0

**Fecha:** 17 de noviembre de 2026 · **Equipo:** i7-1255U, Windows 11 build 26300, OBS 33.0.1, MSVC 19.44, CMake 4.4.3.
**Hardware Waves:** NINGUNO. No hubo eMotion LV1, SoundGrid Driver V17, switch ni red SoundGrid.
Marcadores: `PASS`, `FAIL`, `NOT_RUN`, `BLOCKED`, `NOT_APPLICABLE`. Una prueba se marca `PASS` solo con ejecución y evidencia; la columna "Ambiente" indica qué se probó realmente.

## Cómo reproducir

```bash
powershell -ExecutionPolicy Bypass -File tools/make-obs-sdk.ps1
```

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DSG_OBS_SDK_DIR=.deps/obs-sdk
```

```bash
cmake --build build --config Release
```

```bash
ctest --test-dir build -C Release --output-on-failure
```

Para T49 con 30 s, define `SG_T49_SECONDS=30` antes de ejecutar `build/tests/Release/sg_unit_tests.exe --gtest_filter=T49*`.
Para la resistencia de 2 h, ejecuta `build/tests/Release/sg_soak.exe 120 128 32 4 0`.

## Resultado global

| Suite | Resultado |
|---|---|
| CTest Release completo (unit, platform, obs, soak) | **106/106 PASS** |
| Unitarias (`sg_unit_tests`) | 100/100 PASS (incluye 7 regresiones de la [revisión de código](REVIEW_2026-11-17.md), 11 del aviso de audio perdido y 7 de la reapertura de Waves) |
| Plataforma, hardware real (`sg_platform_tests`) | 4/4 PASS (incluye localizar las aplicaciones de Waves) |
| Integración con libobs 33.0.1 real (`sg_obs_host_test`) | 21/21 comprobaciones PASS (incluye autor, créditos y el panel simplificado) · `bnum_allocs` = 0 al cerrar |
| OBS 33.0.1 con interfaz (instalación real) | Carga en "Loaded Modules"; init ≈ 1 ms; descarga limpia; OBS sale con código 0 |
| Resistencia 2 min con 12 hilos de CPU al 100 % | PASS: 0 cortes, 0 huecos, latencia máx 0,19 ms, memoria estable |
| Resistencia 2 h (simulador en tiempo real) | PASS: 0 cortes, 0 huecos, 0 desbordes; memoria +0,1 MB ([§ Resistencia](#resistencia-2-h)) |
| Resistencia 30 min con el binario corregido y carga de CPU | PASS: 0 cortes, latencia máx 0,66 ms |

## T01–T57

| ID | Caso | Estado | Ambiente / evidencia |
|---|---|---|---|
| T01 | Fuente registrada en OBS | **PASS** | libobs real (host) + GUI de OBS 33.0.1: log "Loaded Modules: obs-soundgrid-input.dll" |
| T02 | 1 kHz llega a OBS | **PASS** | Simulador → `obs_source_output_audio` → callback de captura de OBS: L = 1 kHz (Goertzel), −23,0 dBFS RMS. Sin archivo WAV |
| T03 | Mapeo L/R y mono | **PASS** | Unitarias + host (swap en caliente verificado tras OBS) |
| T04 | 2–32 canales sin cruces | **PASS** | Simulador de 32 canales, 4 fuentes × 8 canales, frecuencia única por canal |
| T05 | Conversión de formatos | **PASS** | int16/24/32 LE y BE, int32 con 24 bits, float32/64, entrelazado → planar; formato no soportado da silencio seguro |
| T06 | 48↔96 kHz | **PASS** (núcleo) | Los bloques llevan la frecuencia del driver y el cambio se marca como discontinuidad. Remuestreo de OBS a 96 kHz: NOT_RUN |
| T07 | Buffer vacío y desbordado | **PASS** | Desbordes contados una sola vez y sin bloqueo; productor y consumidor concurrentes (20 000 bloques) sin corrupción |
| T08 | Timestamps crecientes | **PASS** | Unitarias (jitter ±2 ms, saltos, re-anclaje) + host (monotonía tras OBS) |
| T09 | Guardar y reabrir la fuente | **PASS** | `obs_save_source`/`obs_load_source` en libobs real |
| T10 | Crear/eliminar 100 veces | **PASS** | Host libobs (0,82 s) + unitaria con simulador en tiempo real |
| T11 | Driver ausente, ocupado o incompatible | **PASS** | Simulador (BUSY y NOT_FOUND con reintento) + ASIO real (ReaRoute) |
| T12 | NIC desconectada/reconectada con audio real | NOT_RUN | Requiere laboratorio. Equivalente simulado `T44_Simulated` PASS |
| T13 | Cambio de frecuencia o buffer | **PASS** (simulado) / NOT_RUN (real) | `changeSampleRate` en el simulador |
| T14 | LV1 Main L/R con patch real | **PASS parcial** | 17 nov: audio de la LV1 por el driver Waves V17 hasta OBS (entradas 1–2, −17 dBFS RMS). Falta confirmar con el operador qué está parcheado (L/R distintos) |
| T15 | Bus y canales 1–8 | NOT_RUN | Sin hardware |
| T16 | Renombrar canal LV1 | BLOCKED | No hay API autorizada de nombres; el plugin muestra puertos del driver |
| T17 | 2 h a 48 kHz con LV1 | NOT_RUN | Hay una versión simulada de 2 h (ver abajo), que no la sustituye |
| T18 | 8 h de resistencia con LV1 | NOT_RUN | |
| T19 | Sin abrir el panel con el driver ya configurado | NOT_RUN | Persistencia no documentada por Waves |
| T20 | Sin driver ni panel (ruta B) | BLOCKED | Protocolo propietario ([NATIVE_FEASIBILITY.md](NATIVE_FEASIBILITY.md)) |
| T21 | Desconexión sin ruido dañino | NOT_RUN | Diseño: silencio seguro (no se emite nada sin frames) |
| T22 | Instalar y desinstalar | **PASS** (Windows) / NOT_RUN (macOS) | ZIP con SHA-256 verificado → install.ps1 → uninstall.ps1 → sin restos. Instalador `.exe` (Inno Setup): con OBS abierto no instala (código 1); instalación silenciosa como usuario y como administrador (UAC) con código 0, entrada en Aplicaciones y menú Inicio; OBS 33.0.1 carga el plugin; desinstalación sin restos y conservando la configuración del usuario. Sin firma digital |
| T23 | Las sesiones de OBS se conservan tras actualizar | NOT_RUN | No hay versión anterior. Persistencia nativa probada en T09 |
| T24 | La mezcla de streaming no altera el FOH | NOT_RUN | Por diseño no hay API de escritura (ADR-006) |
| T25 | A01 con RMS bajo y fuente ARMED | **PASS** | Reloj virtual: se activa a los 10 s, no antes; evidencia en dBFS |
| T26 | A01: 60 s de silencio sin ARMED | **PASS** | 0 eventos |
| T27 | A02: sin frames | **PASS** | Margen de 1 s; no confunde con silencio |
| T28 | A03: driver invalidado | **PASS** | Debounce de 500 ms; recuperación con A10 |
| T29 | A04: NIC DOWN o ausente | **PASS** | 1 s; fuente "NIC del SO" |
| T30 | A05: 100 Mbps | **PASS** | 5 s; no afirma pérdida de paquetes |
| T31 | A06: aumento de errores | **PASS** | Delta en ventana de 5 s, con procedencia |
| T32 | A06 sin contadores | **PASS** | UNKNOWN, sin evento |
| T33 | A07 medida > 80 ms durante 5 s | **PASS** | Indica tramo, unidad y umbral, más "extremo a extremo: NO DISPONIBLE" |
| T34 | A07 sin timestamps fiables | **PASS** | UNKNOWN / NO DISPONIBLE |
| T35 | A08: cortes | **PASS** | > 3 por minuto (Transmisión); ≥ 1 (Grabación); CRITICAL |
| T36 | A09: clipping sostenido | **PASS** | Un pico aislado no dispara |
| T37 | A10: recuperación con histéresis | **PASS** | 5 s; el aleteo no duplica incidentes |
| T38 | A04 provoca A02 y A03 | **PASS** | Un solo incidente notificado; las consecuencias, correlacionadas |
| T39 | Reconocer, silenciar y cooldown | **PASS** | La detección continúa; silenciado = `notify=false` |
| T40 | Persistencia de reglas y umbrales | **PASS** | Ida y vuelta, valores corruptos y migración sin versión; libobs real |
| T41 | 100 reconexiones y reglas concurrentes | **PASS** | 100 ciclos de suscripción con stream en tiempo real; 100 desconexiones de cable simuladas |
| T42 | CPU saturada + callback | **PASS** | 12 hilos al 100 % durante 2 min: 0 cortes |
| T43 | JSONL, rotación y privacidad | **PASS** | JSON por línea, rotación con 3 archivos, sin IP ni MAC |
| T44 | Cable físico desconectado con LV1 | NOT_RUN | Equivalente simulado `T44_Simulated` PASS |
| T45 | Driver deshabilitado con LV1 | NOT_RUN | Equivalente simulado `T45_Simulated` PASS (A03 ≠ A04) |
| T46 | 100 Mbps y errores reales | NOT_RUN | |
| T47 | Silencio real, cortes y recuperación | NOT_RUN | |
| T48 | Callback sin asignaciones ni locks | **PASS** | `operator new` instrumentado: 0 asignaciones con buffers de 32, 64, 128, 1024 y 4096 frames y 128 canales, también al desbordar |
| T49 | Consumidor/UI congelado 30 s | **PASS** | 11 250 callbacks en 30 s (esperados 11 250), desbordes contados, recuperación y discontinuidad marcada |
| T50 | NIC UP sin driver; driver sin patch | **PASS** | Indicadores separados; nunca se afirma una LV1 |
| T51 | 100 fallos de A04 | **PASS** | 100 A04 notificados, 0 A02/A03 notificados, 100 A10 |
| T52 | Cambiar el mapa sin reabrir el driver | **PASS** | `opens() == 1` (hub y sesión) |
| T53 | Cola saturada | **PASS** | 1000 eventos con capacidad 16: 984 descartes contados; un sumidero que lanza excepciones no afecta |
| T54 | Reloj que retrocede o timestamps inválidos | **PASS** | Sin alertas inventadas |
| T55 | Reapertura con fuentes duplicadas | **PASS** (simulado) | Dos fuentes sobre un hub reabierto reciben audio |
| T56 | Conflictos de patch L/R con la LV1 | NOT_RUN | |
| T57 | Cambio de NIC o cable con la LV1 | NOT_RUN | |

## Aviso de audio perdido en OBS (Windows)

Probado en OBS 33.0.1 con interfaz, con una fuente temporal del simulador y su botón "fallo de flujo" (la consola no se tocó):

| Paso | Resultado |
|---|---|
| Fallo de flujo activado | Barra roja "SE PERDIÓ EL AUDIO" bajo el menú, con fuente, causa ("el driver dejó de responder") y hora local |
| Reconexiones del driver sin audio | Sigue en rojo. Antes de una corrección pasaba a ámbar ("restablecido") aunque no llegaban muestras; se corrigió y tiene prueba |
| Clic en la barra | Abre las propiedades de la fuente afectada, con el resumen en rojo |
| Fallo desactivado | Ámbar "AUDIO RESTABLECIDO, VERIFICANDO" → verde "AUDIO RECUPERADO" 5 s → oculto (log: RESOLVED A03/A02 y RECOVERED A10) |
| Alerta que no es pérdida de audio (A05, enlace a 100 Mbps) | Sin barra; el audio siguió llegando |
| Fuente quitada de la escena | Sin barra (solo cuentan las fuentes visibles en una escena o como audio global) |
| Cancelar el diálogo de propiedades | Ya no pregunta por "cambios sin guardar" (los textos informativos no se guardan como ajustes) |

Panel: una fuente nueva quedó preconfigurada sola con el driver Waves SoundGrid, la tarjeta de red del Control Panel y las entradas 1 y 2.

## Reapertura de las aplicaciones de Waves (Windows)

7 pruebas unitarias de la lógica (abre si falta, no duplica mientras arranca, reabre tras un cierre, se rinde tras 5 cierres rápidos o errores, olvida fallos tras 2 min estable, sin bucle si pide administrador). En OBS 33.0.1 real:

| Paso | Resultado |
|---|---|
| OBS arranca con el Control Panel cerrado | A los ~3 s el plugin lo abre minimizado (proceso hijo de OBS); log "no estaba abierto; se abrió (minimizado)" |
| Cerrar el Control Panel desde su ventana | Reabierto en menos de 1 s; OBS conserva el foco |
| Panel de la fuente | Opción "Mantener abiertos…" activada, botón "Abrir SoundGrid Driver Control Panel" y la línea "Aplicaciones Waves: servicio en marcha · Control Panel abierto (reabierto 2 veces)" |
| Servicio `SGDawNodeService` cerrado | NOT_RUN: no se cerró para no cortar el driver; la lógica es la misma y tiene pruebas unitarias |

Durante esta prueba el enlace con la consola estaba caído desde las 18:55:54 (cable o consola, ajeno al plugin): A04 y la barra roja lo mostraron correctamente. El driver Waves siguió entregando silencio digital con el enlace caído; por eso A04 (estado de la NIC) es la alerta útil en ese caso.

## Vigilancia global sin fuentes (Windows, LV1 real)

OBS 33.0.1 con la colección del operador, que ya no tenía ninguna fuente SoundGrid (Mic/Aux usaba la entrada WDM "Line In (Waves SoundGrid)"):

| Paso | Resultado |
|---|---|
| Arranque sin fuentes SoundGrid | "vigilancia global activa: driver 'asio:Waves SoundGrid', entradas 1–2, red 'Ethernet 2', con aviso de silencio prolongado"; el Control Panel se mantuvo abierto |
| Enlace a 100 Mbps | A05 una sola vez (antes de la pausa salía doble con fuente y vigilancia global) |
| La consola envió silencio (−132 dBFS) | A01 a las 19:35:31 y barra roja "SE PERDIÓ EL AUDIO … silencio" |
| Volvió el audio (−17 dBFS) | Ámbar 19:35:36 → RESOLVED A01 y verde 19:35:41 → oculto 19:35:46 |
| Herramientas > SoundGrid Monitor sin fuentes | Ventana con el estado de la vigilancia global y la opción de desactivarla |
| Dos clientes ASIO a la vez | El driver Waves aceptó la sonda `sg_asio_probe` mientras OBS lo tenía abierto (−17 dBFS en 1–2) |
| Pausa con una fuente visible | NOT_RUN en OBS (no se volvió a añadir la fuente que el operador había quitado) |

## Monitor de audio en el Multiview (macOS, OBS 33.0.2)

Fecha: 2026-11-20 · macOS 26.6 en Apple Silicon · OBS 33.0.2 (Homebrew) · dispositivo simulado (sin hardware Waves).

| Prueba | Resultado |
|---|---|
| `test_audio_monitor.cpp`: 35 pruebas unitarias (escala, zonas, historial, Peak/RMS de un seno, balística de OBS, clip, forma de onda de derecha a izquierda, huecos sin audio, distribución en 5 lienzos, selección por escena y plan del Multiview) | PASS |
| `obs_monitor_render_test.cpp` (MON01–MON10): render real por GPU (libobs-opengl) y comprobación por píxeles. Colores verde/amarillo/rojo del medidor, onda a −20 dBFS, clip encendido con señal saturada, cabecera roja sin señal, textos y cambio de fuente. Sin asignaciones de libobs pendientes tras `obs_shutdown` | PASS |
| OBS real con 7 escenas (una fuente SoundGrid simulada en cada una), el monitor en la 8.ª y el Multiview abierto. El Multiview crea la etiqueta de la escena del monitor y el monitor dibuja 1280×720 | PASS |
| El monitor sigue la escena de programa (Escena 3 → Escena 2) y, en modo estudio, la transición (→ Escena 4). Con «vista previa» sigue la vista previa (Escena 6) | PASS |
| Protección: al poner la escena del monitor en programa vuelve a «Escena 2»; en vista previa (modo estudio) vuelve a «Escena 4» | PASS |
| Cierre de OBS: «Number of memory leaks: 0» (antes 5, por los ajustes globales del plugin que no se liberaban; corregido) | PASS |
| Windows | Sin probar |

Los cambios de escena se hicieron por obs-websocket, activado solo en una configuración temporal de OBS que se borró después. Un clic en el Multiview usa la misma ruta del frontend (`SetCurrentScene`), pero no se probó con el ratón.

## Revisión de código
Una revisión independiente de concurrencia y tiempo real encontró 12 problemas. El más grave era un bloqueo de la UI al cambiar de dispositivo con audio en marcha, que ninguna prueba cubría. Todos se corrigieron con pruebas de regresión: ver [REVIEW_2026-11-17.md](REVIEW_2026-11-17.md).

## Hallazgos con hardware real (no Waves)
- **ReaRoute ASIO x64 sin REAPER abierto:**
  - Abre 16 canales a 48 kHz en int32 con buffer de 1024.
  - Avanza a **1,99× tiempo real** y repite siempre la misma mitad del doble buffer.
  - El plugin no descarta esos bloques (para no perder audio con drivers atípicos), pero los cuenta (`duplicateBuffers`). El ritmo anómalo produce re-anclajes de timestamp que A08 hace visibles.
  - Es justo el tipo de anomalía que hay que vigilar con el driver de Waves en HIL.
- **Waves SoundGrid Drivers 17.3.37.122 con una LV1 conectada directamente (17 nov 2026, primera prueba con hardware Waves):**
  - El instalador registra la entrada ASIO solo en `HKLM\SOFTWARE\WOW6432Node\ASIO`, aunque instala y registra la DLL de 64 bits (`System32\SoundGridAsio.dll`). El plugin no la veía; ahora la incluye si su CLSID tiene un servidor COM de 64 bits. Solo lee el registro.
  - Enlace a la consola: primero 100 Mbps (cable defectuoso); tras cambiar el cable, 1 Gbps full duplex sin errores.
  - El SoundGrid Driver Control Panel tiene elegida la NIC correcta (`Ethernet 2`).
  - Antes de asignar el driver, `init()` de "Waves SoundGrid ASIO" fallaba con "Error Initializing (0000048c), Make sure SoundGrid is configured properly and devices are connected to the network". El servicio `SGDawNodeService` registraba "Could not Start Kernel Driver Streaming err: -1036" y dejaba de reintentar.
  - Tras asignar el driver en la LV1 (Setup > Inventory), el servicio **no reintentó solo**. Hubo que cambiar el puerto a "None" y volver a la NIC en el SoundGrid Driver Control Panel; entonces `ActivateInterface()` devolvió `true`.
  - **Audio de la LV1 recibido** (sonda `sg_asio_probe SoundGrid 10`, solo lectura):
    - El driver abre 4 entradas a 48 kHz, buffer de 256, float32, con 8,67 ms de latencia de entrada informada.
    - 480 000 frames en 10 s (1875 bloques exactos), intervalo máximo entre callbacks 6,5 ms (nominal 5,33 ms), 0 buffers repetidos.
    - Entradas 1 y 2: pico −10,3 dBFS, RMS −17,1 dBFS, sin clipping. Entradas 3 y 4: silencio digital.
  - **Hasta OBS** (`SG_HIL_DEVICE="asio:Waves SoundGrid"` con `sg_obs_host_test`, libobs 33.0.1 real):
    - 5 s recibidos en el callback de captura de OBS: L y R a −10,3 dBFS de pico y −17,2 dBFS RMS. Timestamps crecientes.
    - El panel muestra "Audio OK", driver STREAMING, 0 desbordes, 0 discontinuidades y ninguna alerta.
  - **No verificado:** qué envía la LV1 a cada entrada (el patch no se confirmó con el operador), ni duración, reconexión o alertas con la consola real.
  - No se cambió nada en la consola. En el driver solo se volvió a elegir la misma NIC.
- **NIC:** de las 12 interfaces Ethernet listadas, solo una es física (Realtek, UP a 1000 Mbps). El selector las ordena con las físicas primero y marca las virtuales.

## Resistencia (2 h)

Simulador en tiempo real: 32 canales a 48 kHz, buffer de 128, 4 fuentes (estéreo, mono-suma, 8 canales y estéreo alto) y monitor y alertas activos. **No sustituye a T17.**

**Resultado: PASS** (7200,1 s; resumen en `tests/reports/soak_120min.txt`, salida completa en `tests/reports/soak_120min_raw.txt`).

| Métrica | Valor |
|---|---|
| Frames por segundo, 4 fuentes | 48 000 exactos (345 604 736 frames en la fuente 0) |
| Huecos de timestamp / discontinuidades / desbordes | 0 / 0 / 0 en las 4 fuentes |
| Latencia medida callback → entrega | Máx 15,80 ms, un único pico entre los segundos 60 y 90 (antes, máx 0,38 ms). Queda muy por debajo del umbral de A07 (80 ms) y no produjo cortes |
| Memoria | 23,4 MB tras calentamiento, máx 23,6 MB (crecimiento 0,1 MB en 2 h) |
| Alertas notificadas / eventos descartados | 0 / 0 |

Esta ejecución usó el binario **anterior** a la revisión de código, porque empezó antes que ella. Por eso se repitió con el binario corregido.

### Repetición con el binario corregido (30 min, 4 hilos de carga de CPU)

**Resultado: PASS** (1800,4 s; `tests/reports/soak_30min_fixed_raw.txt`). Comando: `sg_soak.exe 30 128 32 4 4`.

| Métrica | Valor |
|---|---|
| Frames por segundo, 4 fuentes | 48 000 exactos |
| Huecos / discontinuidades / desbordes | 0 / 0 / 0 en las 4 fuentes |
| Latencia medida callback → entrega | Máx 0,66 ms |
| Memoria | 23,5 MB tras calentamiento, máx 23,7 MB (crecimiento 0,1 MB) |
| Alertas notificadas / eventos descartados | 0 / 0 |

Además, OBS 33.0.1 con interfaz cargó el DLL corregido instalado desde ProgramData: `obs_init_module` tardó 0,68 ms, la descarga fue limpia y OBS salió con código 0.

## Limitaciones de este informe
- Las pruebas de macOS no se ejecutaron: no hay Mac. El código Core Audio y NIC de macOS no se ha compilado.
- La CI está definida en `.github/workflows/ci.yml`, pero no se ha ejecutado porque el repositorio no tiene remoto.
- No hay grabación WAV de evidencia: T02 se verificó analizando el audio entregado por OBS a su callback de captura.
