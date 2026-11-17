# SDD-06 — Detección de NIC y capacidades reales por OS

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P0 · **Dependencias:** 01,02 · **Entregable:** `INetworkTelemetry` con implementaciones por plataforma.

**Historia:** Como operador quiero distinguir cable desconectado, NIC degradada y audio inexistente.

**Diseño y tareas:** Windows con IP Helper/Windows APIs, y macOS con APIs de red/Sysctl o equivalentes; capturar carrier, velocidad negociada, interfaz, contadores si disponibles. Los valores no disponibles se expresan como `UNKNOWN` (no cero). NIC seleccionada se guarda por identificador estable y se revalida.

**Aceptación:** cable conectado/desconectado cambia estado sin congelar OBS; NIC a 100 Mbps genera advertencia; sin contadores se informa limitación. **Pruebas:** mocks OS, cambios de NIC y permisos.

