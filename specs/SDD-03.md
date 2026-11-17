# SDD-03 — Captura Windows SoundGrid ASIO V17

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P0 (Windows) · **Dependencias:** 00,02 · **Entregable:** `AsioTransport`.

**Historia:** Como operador en Windows quiero recibir PCM parcheado desde LV1 sin abrir un DAW.

**Diseño y tareas:** enumeración ASIO, ciclo de vida driver, callbacks, buffers, formatos, canales y xrun; no abrir panel desde el flujo normal si la configuración persiste. Documentar instalación driver/selección NIC y patch iniciales que deban hacerse fuera del plugin.

**Aceptación:** capturar L/R real en HIL, medir discontinuidades, iniciar/detener 100 veces; no afirmar «sin driver». **Pruebas:** unit de conversiones; HIL 2/8/16 canales cuando disponibles, sesión 2 h sin cortes atribuibles al plugin.

