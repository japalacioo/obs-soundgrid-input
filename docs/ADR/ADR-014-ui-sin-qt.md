# ADR-014: Panel SoundGrid Monitor y aviso de audio perdido sin Qt

**Estado:** Aceptada (provisional) · **Fecha:** 2026-11-17 · **Revisada:** 2026-11-17 (aviso nativo en Windows)

**Contexto:**
- Un dock o un widget Qt necesita las cabeceras y bibliotecas de desarrollo de Qt 6.11 de la misma ABI que OBS, que no están en el entorno de desarrollo.
- El operador pidió que la pérdida de audio se vea en OBS, en rojo y en la parte superior, sin abrir ningún panel.

**Decisión:**
- El "SoundGrid Monitor" son las propiedades de la fuente (API C de `obs_properties`), ordenadas en "1 · Conexión", "2 · Canales", "3 · Avisos y alertas" y "Estado y diagnóstico", con un resumen de una línea arriba. Se abre desde **Herramientas > SoundGrid Monitor** o con un clic en el aviso.
- **Aviso de audio perdido (Windows):** ventana Win32 emergente "propiedad" de la ventana principal de OBS (`obs_frontend_get_main_window_handle`), colocada bajo el menú a todo el ancho.
  - Se crea en `OBS_FRONTEND_EVENT_FINISHED_LOADING` y se destruye en `OBS_FRONTEND_EVENT_EXIT`, en el hilo de interfaz de OBS, cuyo bucle de mensajes la atiende.
  - Un temporizador de 250 ms consulta el estado de las fuentes; un `SetWinEventHook` la recoloca al mover o redimensionar OBS.
  - Rojo: se perdió el audio (A02, A03, A04 y A01 si está armada). Ámbar: el audio volvió y se verifica (histéresis). Verde 5 s: recuperado. Si OBS no está en primer plano, la barra de tareas parpadea.
  - La lógica (qué mostrar) está en `sg_core` (`AudioLossBannerModel`) y tiene pruebas; la capa Win32 solo dibuja.
- macOS y Linux: sin aviso nativo todavía (`AlertBannerStub.cpp`); las alertas siguen en el panel y el log.

**Consecuencias:**
- La barra se superpone a la parte alta de los paneles de OBS mientras dura el aviso (no reorganiza su diseño).
- El desplazamiento bajo el menú es una constante (22 ppp lógicos escalados por DPI), no una medida del menú de Qt.

**Siguiente paso:** con un SDK Qt compatible, sustituir la barra por un widget dentro del diseño de OBS (también en macOS).
