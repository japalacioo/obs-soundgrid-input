# SDD-08 — Selección de canales y metadatos verificables

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P0 · **Dependencias:** 03/04/05,07 · **Entregable:** `IChannelCatalog` y UI de selección.

**Historia:** Como usuario quiero seleccionar Main L/R, buses y canales solamente si están disponibles para el receptor.

**Diseño y tareas:** catálogo de índices PCM expuestos; etiquetar `Input 1…N` si no hay API de nombres; selección persistente por fuente OBS; confirmación de patch mediante PCM/metadata observables, no adivinación. Distinguir «nombre suministrado por usuario» de «nombre reportado por LV1».

**Aceptación:** cambio de mapeo sin crash/ruido, 0 canales y canales fuera de rango gestionados; nunca anunciar Main L/R por defecto sin correspondencia verificada. **Pruebas:** mapeo stereo, swap, pérdida de canal.

