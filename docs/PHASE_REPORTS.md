# Informes de avance por fase (formato del plan §10)

Fecha de ejecución: 16 y 17 de noviembre de 2026 · Agente autónomo, sin supervisión · Equipo: Windows 11 x64, OBS 33.0.1.

---
```text
FASE: 0 — Investigación técnica y viabilidad
ESTADO: PASS (con ruta B BLOQUEADA)
IMPLEMENTADO: docs/FEASIBILITY.md, docs/NATIVE_FEASIBILITY.md, docs/WAVES_SDK_REQUEST.md (borrador NO enviado),
              docs/ADR/ADR-001..015, docs/COMPATIBILITY_MATRIX.md, docs/LICENSING.md
COMPILACIÓN: n/a
PRUEBAS EJECUTADAS: viabilidad con hardware local: ASIO real (ReaRoute) open/start/stop PASS; NIC real PASS;
                    plugin cargado en OBS 33.0.1 PASS
PRUEBAS NO EJECUTADAS: referencia de captura con SoundGrid ASIO V17 y LV1 (sin hardware) -> HIL_REQUIRED
ALERTAS: n/a
MONITOREO: n/a
EVIDENCIA EN HARDWARE LV1 V17: no
BLOQUEADORES: protocolo SoundGrid propietario sin SDK público (ruta B); sin LV1 ni driver Waves para HIL
RIESGOS PARA AUDIO EN VIVO: persistencia de la configuración del driver tras reiniciar sin documentar; posible
                            limitación a un cliente por driver; Waves desaconseja Mac para grabar
SIGUIENTE TAREA: enviar WAVES_SDK_REQUEST.md por un canal oficial (lo hace el propietario) y reservar una ventana HIL
```

---
```text
FASE: 1 — Repositorio, compilación y fuente simulada
ESTADO: PASS (Windows) / PARTIAL (macOS: CI definida, no ejecutada)
IMPLEMENTADO: CMake C++20 (sg_core sin libobs + plugin + pruebas), tools/make-obs-sdk.ps1 (cabeceras 33.0.1 +
              import libs), MockTransport (silencio, seno por canal, ruido rosa, impulsos, clipping; hasta 128
              canales; fallos), fuente "SoundGrid Input (Waves)", persistencia nativa de OBS, logs
COMPILACIÓN: cmake -G "Visual Studio 17 2022" -A x64 -DSG_OBS_SDK_DIR=.deps/obs-sdk ; MSVC 19.44 /W4: 0 avisos
             propios; Debug y Release OK
PRUEBAS EJECUTADAS: T01 PASS, T02 PASS, T03 PASS, T09 PASS, T10 PASS (libobs real); CTest 80/80
PRUEBAS NO EJECUTADAS: grabación WAV desde la GUI de OBS (verificado por análisis espectral del audio entregado
                       por OBS); CI en GitHub (no hay remoto)
ALERTAS: n/a
MONITOREO: n/a
EVIDENCIA EN HARDWARE LV1 V17: no
BLOQUEADORES: ninguno para Windows; falta un Mac para macOS
RIESGOS PARA AUDIO EN VIVO: ninguno nuevo
SIGUIENTE TAREA: publicar el repositorio y ejecutar la CI (Windows + macOS arm64/Intel)
```

---
```text
FASE: 2 — Captura real por adaptadores (ruta A)
ESTADO: PARTIAL
IMPLEMENTADO: AsioTransport (hilo COM STA, 128 canales, int16/24/32/float, no cambia frecuencia ni buffer,
              mensajes reset/resync/overload, ranuras para 8 drivers); CoreAudioTransport (IOProc, listeners de
              dispositivo vivo, frecuencia y sobrecarga; SIN COMPILAR); DeviceHub con reconexión
COMPILACIÓN: Windows OK; macOS no compilado
PRUEBAS EJECUTADAS: Platform_ASIO con ReaRoute: 16 canales, 48 kHz, int32, buffer 1024; stop() sin callbacks
                    posteriores; detecta driver a 1,99x tiempo real. T05, T06, T07, T11, T13 (simulado) PASS
PRUEBAS NO EJECUTADAS: captura con SoundGrid ASIO V17 y LV1 (T14, T15); macOS Intel/Apple Silicon
ALERTAS: n/a
MONITOREO: xruns del driver observables si el driver soporta kAsioOverload / kAudioDeviceProcessorOverload
EVIDENCIA EN HARDWARE LV1 V17: no -> PENDIENTE HIL
BLOQUEADORES: hardware Waves; Mac
RIESGOS PARA AUDIO EN VIVO: comportamiento real del SoundGrid ASIO (un cliente, reloj, persistencia) sin verificar
SIGUIENTE TAREA: HIL Windows con LV1 V17 (tests/hil/HIL_PROCEDURE.md §1–§3)
```

---
```text
FASE: 3 — UX de descubrimiento y selección de canales
ESTADO: PARTIAL
IMPLEMENTADO: lista de drivers (marca "driver Waves SoundGrid" solo por nombre), selector de NIC (físicas primero,
              estado en vivo), modo mono/estéreo/multicanal, entradas con nombres de puertos del driver, 6
              indicadores independientes, mensajes de configuración inicial, Herramientas > SoundGrid Monitor
COMPILACIÓN: OK
PRUEBAS EJECUTADAS: T50 PASS; propiedades construidas en libobs real; panel renderizado (texto verificado)
PRUEBAS NO EJECUTADAS: T19 (sin panel tras reiniciar, HIL); usabilidad en la GUI con operador
ALERTAS: n/a
MONITOREO: n/a
EVIDENCIA EN HARDWARE LV1 V17: no
BLOQUEADORES: nombres de la LV1 e identificación de la consola sin API autorizada (BLOCKED); dock Qt sin SDK Qt
RIESGOS PARA AUDIO EN VIVO: el operador debe llevar su propia correspondencia entre entradas del driver y patch
SIGUIENTE TAREA: dock Qt opcional (ADR-014) cuando haya un SDK Qt 6.11 compatible
```

---
```text
FASE: 3A — Monitoreo, alertas y recuperación
ESTADO: PASS (simulado) / NOT_RUN (HIL)
IMPLEMENTADO: HealthMonitor (250 ms), AlertEngine A01–A10 (debounce, histéresis 5 s, cooldown 30 s, correlación
              A04>A03>A02, UNKNOWN, reconocer, silenciar, perfiles), AlertDispatcher acotado (1024), JSONL rotativo,
              historial, log de OBS, diagnóstico JSON, reconexión con backoff 1–30 s y detección de bloqueo a 3 s
COMPILACIÓN: OK
PRUEBAS EJECUTADAS: T25–T43 PASS, T48–T55 PASS (T49 con 30 s), T44/T45 simulados PASS
PRUEBAS NO EJECUTADAS: T44–T47, T56, T57 con LV1 (NOT_RUN)
ALERTAS: A01–A10 soportadas. Observabilidad real en Windows: enlace, velocidad y errores de la NIC (medido), estado
         y xruns del driver (si los reporta), PCM y niveles (medido), latencia del plugin (medida). Latencia
         extremo a extremo: NO DISPONIBLE. Pérdidas SoundGrid: no observables
MONITOREO: overhead: callback sin asignaciones; latencia callback->OBS 0,2–0,3 ms; memoria estable en resistencia
EVIDENCIA EN HARDWARE LV1 V17: no
BLOQUEADORES: hardware para calibrar umbrales
RIESGOS PARA AUDIO EN VIVO: umbrales iniciales sin calibrar en sala; no hay notificación emergente (solo panel y log)
SIGUIENTE TAREA: calibrar los umbrales en HIL (T44–T47)
```

---
```text
FASE: 4 — Integración SoundGrid nativa (ruta B)
ESTADO: BLOCKED
IMPLEMENTADO: nada (src/soundgrid_native/README.md: NO IMPLEMENTADO)
COMPILACIÓN: n/a
PRUEBAS EJECUTADAS: ninguna
PRUEBAS NO EJECUTADAS: T20 (BLOCKED)
ALERTAS: n/a
MONITOREO: n/a
EVIDENCIA EN HARDWARE LV1 V17: no
BLOQUEADORES: protocolo propietario sin SDK ni especificación pública; acceso solo por partnership con Waves
RIESGOS PARA AUDIO EN VIVO: n/a
SIGUIENTE TAREA: respuesta de Waves a WAVES_SDK_REQUEST.md
```

---
```text
FASE: 5 — Calidad de audio y rendimiento
ESTADO: PARTIAL
IMPLEMENTADO: sg_soak (resistencia en tiempo real con métricas); pruebas de estrés y tiempo real
COMPILACIÓN: Release OK
PRUEBAS EJECUTADAS: T42 (12 hilos de CPU al 100 %, 2 min): 0 cortes, 0 huecos, latencia máx 0,19 ms;
                    resistencia simulada de 2 h PASS (4 fuentes, 0 cortes/huecos/desbordes, memoria +0,1 MB,
                    latencia máx 15,8 ms en un único pico); T48 0 asignaciones; T49 30 s
PRUEBAS NO EJECUTADAS: 48/96 kHz y 2–128 canales con LV1; latencia de extremo a extremo con impulsos; 8 h con LV1
ALERTAS: matriz A01–A10 validada con simulador; estabilidad ante pausas (T26: 60 s de silencio sin alerta)
MONITOREO: memoria ~24 MB estable; CPU no perfilada de forma aislada
EVIDENCIA EN HARDWARE LV1 V17: no
BLOQUEADORES: hardware
RIESGOS PARA AUDIO EN VIVO: comportamiento del reloj real del driver Waves (deriva, resincronizaciones) sin medir
SIGUIENTE TAREA: T17/T18 con LV1
```

---
```text
FASE: 6 — Instalador, documentación y entrega
ESTADO: PARTIAL
IMPLEMENTADO: ZIP de Windows con install.ps1/uninstall.ps1 + SHA-256; tools/install-windows.ps1; script .pkg/.zip de
              macOS (sin ejecutar); README, INSTALL, TROUBLESHOOTING, SUPPORTED_HARDWARE, LIMITATIONS,
              RELEASE_NOTES, ALERTS, PROFILES, ARCHITECTURE, TEST_PLAN, TEST_REPORT, LICENSING
COMPILACIÓN: dist/obs-soundgrid-input-0.1.0-windows-x64.zip
PRUEBAS EJECUTADAS: T22 Windows PASS (instalar desde el ZIP con hash verificado, desinstalar sin restos); carga
                    en la GUI de OBS 33.0.1 PASS
PRUEBAS NO EJECUTADAS: instalador .exe firmado (sin certificado ni herramienta); macOS; T23 (no hay versión
                       anterior); rollback formal
ALERTAS: documentadas en ALERTS.md
MONITOREO: n/a
EVIDENCIA EN HARDWARE LV1 V17: no
BLOQUEADORES: certificados de firma (Windows y Apple)
RIESGOS PARA AUDIO EN VIVO: binario sin firmar (SmartScreen o antivirus pueden advertir)
SIGUIENTE TAREA: firma de código e instalador; HIL; publicar la versión 0.1.0 como "laboratorio"
```

---

## Resumen de cumplimiento de los requisitos originales

| Requisito (plan §1) | Ruta A (driver Waves) | Ruta B (sin driver) |
|---|---|---|
| Plugin nativo de OBS para Windows | **Sí** (probado con OBS 33.0.1) | — |
| Plugin nativo de OBS para macOS | **Parcial** (código escrito, sin compilar) | — |
| Recibir audio de la LV1 por SoundGrid | **No demostrado** (implementado; requiere HIL) | **No demostrado** (BLOQUEADO) |
| No abrir el SoundGrid Driver Panel en cada sesión | **No demostrado** (depende de la persistencia del driver, T19) | No demostrado |
| No instalar ni configurar el driver ni el panel de Waves | **No** (la ruta A lo requiere, y así se declara) | No demostrado (BLOQUEADO) |
| Detectar la NIC conectada | **Sí** (estado, velocidad y errores) | — |
| Identificar la LV1 y mostrar nombres reales de canal | **No** (sin API autorizada; se muestran puertos del driver) | No demostrado |
| Elegir Main L/R, buses o canales parcheados | **Sí** (por entradas del driver; mono, estéreo y multicanal; varias fuentes) | — |
| Audio estable y recuperación sin bloquear OBS | **Sí con simulador y driver ASIO de terceros**; no demostrado con Waves | — |
| Centro de alertas A01–A10 con umbrales y silencio de avisos | **Sí** (panel en propiedades, log y JSONL; sin dock Qt ni notificaciones emergentes) | — |
| Evidencia de cada alerta, medido frente a inferido | **Sí** | — |
| Persistencia de selección, canales, umbrales y alertas | **Sí** | — |
| No modificar la consola | **Sí** (no hay ninguna API de escritura) | — |
