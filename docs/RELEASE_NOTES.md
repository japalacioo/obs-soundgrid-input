# Notas de versión

## 0.4.0 (23 de noviembre de 2026): monitor de audio en el Multiview

**Autores:** Mateo Salazar (monitor de audio, PR #2) y Joan Palacio.

### Nuevo
- **Monitor de audio en el Multiview** ([docs/AUDIO_MONITOR.md](AUDIO_MONITOR.md), ADR-016): una escena que se coloca en el recuadro inferior derecho del Multiview y muestra el audio de la fuente SoundGrid de la escena al aire. Incluye medidores L/R verde, amarillo y rojo con escala en dBFS y peak hold, un osciloscopio que avanza de derecha a izquierda, Peak y RMS en dBFS, un indicador de clip y el nombre de la fuente. **La escena del monitor se crea sola al abrir OBS**: no hay que crear nada para verlo en el Multiview (se puede desactivar en las propiedades del monitor). Si la escena del monitor llega a programa o vista previa, el plugin vuelve a la escena anterior.
- Pruebas: 35 unitarias (`test_audio_monitor.cpp`) y una de render real por GPU con libobs (`obs_monitor_render_test.cpp`, MON01–MON10). Probado en macOS 26 con OBS 33.0.2 y en Windows 11 con OBS 33.0.1 (Direct3D 11): el Multiview dibuja el monitor y sigue la escena al aire.

### Corregido
- **La protección del monitor podía sacar del aire una escena del director.** Contaba como escena del monitor cualquier escena que contuviera la fuente del monitor; ahora solo la escena que crea el plugin (marca interna). Las escenas del director nunca se revierten.
- Prueba de render del monitor en Windows: no encontraba `libobs-d3d11` (faltaba la carpeta de `obs.dll` en la búsqueda de DLL), chocaba con las macros `near`/`far` de `windows.h` y buscaba `obs-text` en la ruta de OBS anterior a la 33 (`obs-plugins/64bit` en vez de `core/obs-text`).
- Avisos C4244 de MSVC en `test_audio_monitor.cpp`.
- Los ajustes globales del plugin (`settings.json`) no se liberaban al cerrar OBS (5 fugas de memoria en el log de cierre de OBS).
- **macOS y Linux: la vigilancia global se evaluaba una sola vez, al arrancar OBS.** En Windows la reevalúa cada ~5 s el temporizador de la barra roja, que en macOS no existe. Ahora un hilo propio la reevalúa cada 5 s: reintenta si el driver Waves aparece con OBS abierto y se pausa o reanuda al añadir o quitar una fuente SoundGrid visible. Compilado y cargado en OBS 33.0.2 en macOS (sin driver Waves instalado); sin probar con hardware Waves.

## 0.3.0 (17 de noviembre de 2026): avisos y Control Panel sin necesidad de fuentes

**Autor:** Joan Palacio.

### Nuevo
- **Vigilancia global sin fuentes:** aunque no haya ninguna fuente SoundGrid, el plugin escucha el driver Waves (entradas 1–2) y la tarjeta de red de su Control Panel, y muestra la barra roja si deja de llegar audio, se cae el driver, se desconecta el cable o hay silencio prolongado. Se pausa mientras una fuente visible ya vigila el driver. Estado y desactivación en **Herramientas > SoundGrid Monitor**.
- **El Control Panel y el servicio de Waves se mantienen abiertos en todo OBS**, aunque no haya fuentes (antes hacía falta una fuente con el driver Waves). Nueva entrada **Herramientas > Abrir SoundGrid Driver Control Panel**.
- Ajustes globales del plugin en plugin_config\obs-soundgrid-input\settings.json.

### Calidad
- Probado en OBS 33.0.1 con la LV1 y sin fuentes SoundGrid: silencio real de la consola → barra roja; vuelta del audio → ámbar → verde → oculto (ver TEST_REPORT).
- El driver Waves SoundGrid V17 aceptó dos clientes ASIO a la vez.
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
