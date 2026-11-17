# SDD-05 — Compatibilidad cruzada Windows/macOS y arranque automatizado

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P0 · **Dependencias:** 01,02,03,04 · **Entregable:** `PlatformCompatibilitySuite`, checklist de arranque y documentación por plataforma.

**Historia:** Como operador quiero que el plugin funcione de forma coherente en Windows y Mac, con diagnósticos precisos cuando SoundGrid todavía no se pueda usar.

**Diseño y tareas:** validar la misma API `IAudioTransport` para ASIO y Core Audio; comprobar carga de plugin, dependencias, selección persistente de dispositivo/NIC, permisos, compatibilidad de arquitectura, autoarranque de fuentes OBS, reconexión y estado `CONFIGURATION_REQUIRED` cuando el driver o el patch necesitan pasos oficiales. No simular descubrimiento LV1 al detectar un controlador.

**Aceptación:** compilación y carga en OBS de Windows y macOS, pruebas de audio simulado idénticas, guías de instalación específicas, y estados de diagnóstico trazables. **Pruebas:** smoke de arranque/reinicio de OBS, persistencia, driver ausente, driver ocupado, sample rate cambiado y compatibilidad Intel/Apple Silicon.

