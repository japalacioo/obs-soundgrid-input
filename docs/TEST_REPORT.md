# Informe de pruebas, v0.1.0

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
| CTest Release completo (unit, platform, obs, soak) | **80/80 PASS** |
| Unitarias (`sg_unit_tests`) | 75/75 PASS |
| Plataforma, hardware real no Waves (`sg_platform_tests`) | 3/3 PASS |
| Integración con libobs 33.0.1 real (`sg_obs_host_test`) | 15/15 comprobaciones PASS · `bnum_allocs` = 0 al cerrar |
| OBS 33.0.1 con interfaz (instalación real) | Carga en "Loaded Modules"; init ≈ 1 ms; descarga limpia; OBS sale con código 0 |
| Resistencia 2 min con 12 hilos de CPU al 100 % | PASS: 0 cortes, 0 huecos, latencia máx 0,19 ms, memoria estable |
| Resistencia 2 h (simulador en tiempo real) | Ver [§ Resistencia](#resistencia-2-h) |

## T01–T57

| ID | Caso | Estado | Ambiente / evidencia |
|---|---|---|---|
| T01 | Fuente registrada en OBS | **PASS** | libobs real (host) + GUI de OBS 33.0.1: log "Loaded Modules: obs-soundgrid-input.dll" |
| T02 | 1 kHz llega a OBS | **PASS** | Simulador → `obs_source_output_audio` → callback de captura de OBS: L = 1 kHz (Goertzel), −23,0 dBFS RMS. Sin archivo WAV |
| T03 | Mapeo L/R y mono | **PASS** | Unitarias + host (swap en caliente verificado tras OBS) |
| T04 | 2–32 canales sin cruces | **PASS** | Simulador de 32 canales, 4 fuentes × 8 canales, frecuencia única por canal |
| T05 | Conversión de formatos | **PASS** | int16/24/32 LE y BE, int32 con 24 bits, float32/64, entrelazado → planar; formato no soportado da silencio seguro |
| T06 | 48↔96 kHz | **PASS** (núcleo) | Los bloques llevan la frecuencia del driver y el cambio se marca como discontinuidad. Remuestreo de OBS a 96 kHz: NOT_RUN |
| T07 | Buffer vacío y desbordado | **PASS** | Desbordes contados sin bloqueo; productor y consumidor concurrentes (20 000 bloques) sin corrupción |
| T08 | Timestamps crecientes | **PASS** | Unitarias (jitter ±2 ms, saltos, re-anclaje) + host (monotonía tras OBS) |
| T09 | Guardar y reabrir la fuente | **PASS** | `obs_save_source`/`obs_load_source` en libobs real |
| T10 | Crear/eliminar 100 veces | **PASS** | Host libobs (0,82 s) + unitaria con simulador en tiempo real |
| T11 | Driver ausente, ocupado o incompatible | **PASS** | Simulador (BUSY y NOT_FOUND con reintento) + ASIO real (ReaRoute) |
| T12 | NIC desconectada/reconectada con audio real | NOT_RUN | Requiere laboratorio. Equivalente simulado `T44_Simulated` PASS |
| T13 | Cambio de frecuencia o buffer | **PASS** (simulado) / NOT_RUN (real) | `changeSampleRate` en el simulador |
| T14 | LV1 Main L/R con patch real | NOT_RUN | Sin hardware |
| T15 | Bus y canales 1–8 | NOT_RUN | Sin hardware |
| T16 | Renombrar canal LV1 | BLOCKED | No hay API autorizada de nombres; el plugin muestra puertos del driver |
| T17 | 2 h a 48 kHz con LV1 | NOT_RUN | Hay una versión simulada de 2 h (ver abajo), que no la sustituye |
| T18 | 8 h de resistencia con LV1 | NOT_RUN | |
| T19 | Sin abrir el panel con el driver ya configurado | NOT_RUN | Persistencia no documentada por Waves |
| T20 | Sin driver ni panel (ruta B) | BLOCKED | Protocolo propietario ([NATIVE_FEASIBILITY.md](NATIVE_FEASIBILITY.md)) |
| T21 | Desconexión sin ruido dañino | NOT_RUN | Diseño: silencio seguro (no se emite nada sin frames) |
| T22 | Instalar y desinstalar | **PASS** (Windows) / NOT_RUN (macOS) | ZIP con SHA-256 verificado → install.ps1 → archivos → uninstall.ps1 → sin restos |
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

## Hallazgos con hardware real (no Waves)
- **ReaRoute ASIO x64 sin REAPER abierto:**
  - Abre 16 canales a 48 kHz en int32 con buffer de 1024.
  - Avanza a **1,99× tiempo real** y repite siempre la misma mitad del doble buffer.
  - El plugin no descarta esos bloques (para no perder audio con drivers atípicos), pero los cuenta (`duplicateBuffers`). El ritmo anómalo produce re-anclajes de timestamp que A08 hace visibles.
  - Es justo el tipo de anomalía que hay que vigilar con el driver de Waves en HIL.
- **NIC:** de las 12 interfaces Ethernet listadas, solo una es física (Realtek, UP a 1000 Mbps). El selector las ordena con las físicas primero y marca las virtuales.

## Resistencia (2 h)

Simulador en tiempo real: 32 canales a 48 kHz, buffer de 128, 4 fuentes (estéreo, mono-suma, 8 canales y estéreo alto) y monitor y alertas activos. **No sustituye a T17.**

Resultado: *se completa al terminar la ejecución; ver `tests/reports/soak_120min.txt`.*

## Limitaciones de este informe
- Las pruebas de macOS no se ejecutaron: no hay Mac. El código Core Audio y NIC de macOS no se ha compilado.
- La CI está definida en `.github/workflows/ci.yml`, pero no se ha ejecutado porque el repositorio no tiene remoto.
- No hay grabación WAV de evidencia: T02 se verificó analizando el audio entregado por OBS a su callback de captura.
