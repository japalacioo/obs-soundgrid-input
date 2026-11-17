# SDD-14 — Ruta B SoundGrid nativa y autorización

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** investigación bloqueante del objetivo «sin driver» · **Dependencias:** 00,02 · **Entregable:** `docs/NATIVE_FEASIBILITY.md` + ADR y PoC HIL solo si procede.

**Historia:** Como propietario quiero recibir audio LV1 sin driver/panel Waves, pero solo si es técnicamente posible y verificable.

**Diseño y tareas:** gestionar contacto con Waves para SDK/contrato, documentación de nodo, clock, streams y rutas; revisar seguridad/licencia; construir receptor de laboratorio detrás de `IAudioTransport` únicamente con base técnica autorizada. Ningún escaneo agresivo en producción.

**Aceptación de puerta:** (1) API/protocolo legalmente utilizable; (2) LV1 Classic V17 identificada; (3) patch/ruta audio establecida con control permitido; (4) PCM correcto, reloj y canales verificados; (5) ensayo 2 h con medidas de dropouts, latencia y reconexión; (6) Windows/macOS probados individualmente. Si no, estado `BLOQUEADO`, sin sustituir por suposiciones.

