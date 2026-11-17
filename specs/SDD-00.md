# SDD-00 — Feasibility / compatibilidad V17

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P0 · **Dependencias:** ninguna · **Entregable:** `docs/FEASIBILITY.md`, `docs/COMPATIBILITY_MATRIX.md`, ADR-001 y ADR-002.

**Historia:** Como responsable técnico quiero saber qué puede hacer cada OS para no construir funciones sobre capacidades inexistentes.

**Diseño y tareas:** registrar matriz de versiones reales (OBS, Waves V17, CPU, NIC), evidencias oficiales de controladores Windows/macOS, requerimientos de ASIO SDK, permisos Core Audio, soporte ARM/macOS y disponibilidad de APIs SoundGrid. Identificar si LV1 Classic permite patch a computador receptor y cómo se confirma PCM. Solicitar acceso oficial a Waves si se necesita ruta B.

**Aceptación:** (a) decisión de ruta A/B por OS (ADR-002) con URL/fecha; (b) riesgos y bloqueos; (c) no afirmar recepción nativa sin driver sin prueba; (d) no aceptar mock como prueba HIL. **Pruebas:** revisión de ADR y matriz contra fuentes oficiales.

