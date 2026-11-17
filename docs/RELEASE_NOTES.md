# Notas de versión

## 0.1.0 (17 de noviembre de 2026): core multiplataforma (nivel 1 de ADR-008)

**Estado:** apto para pruebas de laboratorio. **NO PROBADO EN HARDWARE con Waves eMotion LV1 Classic V17**: no usar en un show sin completar antes las pruebas HIL de `tests/hil/HIL_PROCEDURE.md`.

### Nuevo
- Fuente de audio **SoundGrid Input (Waves)** para OBS 33.
- **Captura ASIO** en Windows con hasta 128 canales del driver.
  - Usa un hilo COM dedicado y nunca cambia la frecuencia, el buffer ni el reloj.
  - Probada con un driver ASIO real de terceros.
- **Captura Core Audio** en macOS: código incluido, sin compilar ni validar.
- **Dispositivo compartido:**
  - Varias fuentes (hasta 32) sobre un único stream del driver.
  - Modos mono (promedio), estéreo y multicanal (hasta 8, el límite de OBS).
  - Los cambios de canales se aplican sin reabrir el driver.
- **Alertas A01–A10:**
  - Debounce, histéresis, cooldown, correlación de causa raíz (un incidente por fallo físico) y estado UNKNOWN.
  - Perfiles Ensayo, Transmisión y Grabación, con umbrales editables.
  - Reconocer y silenciar.
- **Telemetría de red pasiva:** enlace, velocidad y errores de la NIC, sin tráfico ni privilegios.
- **Reconexión automática** solo del receptor: backoff de 1 a 30 s con jitter y detección de bloqueo a 3 s.
- **Panel de estado** con indicadores separados (NIC, driver, patch, audio, LV1 y latencia) y acciones: reconectar, reconocer, silenciar, exportar diagnóstico y simular fallos.
  - Se abre desde **Herramientas > SoundGrid Monitor**.
- **Registro:** log de OBS + `alerts.jsonl` rotativo + `diagnostics.json`, sin IP ni MAC.
- **Simulador integrado** de 32 canales: tonos, ruido rosa, silencio, impulsos, clipping y fallo de flujo.

### Calidad
- 87 pruebas automáticas (unitarias, plataforma, integración con libobs 33.0.1 real y resistencia corta), todas PASS.
- 0 asignaciones en el callback de audio.
- Resistencia de 2 h con el simulador: 0 cortes, 0 huecos y 0 desbordes en 4 fuentes; memoria +0,1 MB (ver TEST_REPORT).
- Revisión de concurrencia y tiempo real: 12 hallazgos corregidos con pruebas de regresión (ver REVIEW_2026-11-17).

### Limitaciones
Ver [LIMITATIONS.md](LIMITATIONS.md). Requiere el driver de Waves. La ruta sin driver está bloqueada por el protocolo propietario.
