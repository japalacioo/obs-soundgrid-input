# Notas de versión

## 0.2.0 (17 de noviembre de 2026): primera captura con la LV1, avisos en OBS e instalador

**Autor:** Joan Palacio.

**Estado:** apto para pruebas de laboratorio. **Prueba con hardware Waves PARCIAL:** se recibió audio de una eMotion LV1 Classic V17 por el driver Waves SoundGrid V17 hasta OBS (Windows). Faltan las pruebas HIL de duración, reconexión, patch confirmado y alertas con la consola (`tests/hil/HIL_PROCEDURE.md`) antes de usarlo en un show.

### Nuevo
- **Aviso rojo en la parte superior de OBS** (Windows) cuando se pierde el audio, con la fuente, la causa y la hora local. Pasa a ámbar mientras se verifica la vuelta y a verde al recuperarse. Un clic abre el panel de la fuente.
- **Mantiene abiertas las aplicaciones de Waves** (Windows):
  - Si el SoundGrid Driver Control Panel o el servicio de Waves (`SGDawNodeService`) no están abiertos, o se cierran durante una transmisión, el plugin los vuelve a abrir en ~2 s.
  - Se abren minimizados, sin quitar el foco a OBS y sin pedir permisos de administrador.
  - Tras 5 cierres seguidos deja de insistir. Hay un botón "Abrir SoundGrid Driver Control Panel" y una opción por fuente para desactivarlo.
- **Panel más simple:**
  - Pasos numerados: 1 · Conexión, 2 · Canales, 3 · Avisos, con un resumen de una línea arriba.
  - Botón para detectar el driver Waves y la tarjeta de red del Control Panel; las fuentes nuevas se preconfiguran solas.
  - Umbrales y perfiles ocultos tras "Mostrar ajustes avanzados".
- **Instalador de Windows** (`.exe`, Inno Setup): español e inglés, desinstalador en Aplicaciones, guías en el menú Inicio y modo desatendido.
- **Sonda HIL de solo lectura** `sg_asio_probe`: pico, RMS, clipping y ritmo por entrada del driver.

### Corregido
- El driver Waves SoundGrid V17 no aparecía: su instalador solo registra la entrada ASIO de 32 bits aunque instala la DLL de 64 bits. Ahora se detecta si existe la DLL de 64 bits.
- Al cambiar de driver en las propiedades, las listas L/R mostraban las entradas del driver anterior.
- OBS preguntaba por "cambios sin guardar" al cancelar el diálogo de propiedades.

### Calidad
- 106 pruebas automáticas, todas PASS.
- Con la LV1: 4 entradas a 48 kHz, 480 000 frames en 10 s, 0 buffers repetidos; −17 dBFS RMS en las entradas 1 y 2 hasta OBS (ver TEST_REPORT).

## 0.1.0 (17 de noviembre de 2026): core multiplataforma (nivel 1 de ADR-008)

**Estado:** apto para pruebas de laboratorio. NO PROBADO EN HARDWARE con Waves eMotion LV1 Classic V17.

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

## Limitaciones
Ver [LIMITATIONS.md](LIMITATIONS.md). Requiere el driver de Waves. La ruta sin driver está bloqueada por el protocolo propietario.
