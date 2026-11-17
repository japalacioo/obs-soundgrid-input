# SDD-15 — Pruebas HIL y validación de lanzamiento

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P0 para cualquier claim de hardware · **Dependencias:** todas las funciones incluidas en release.

**Historia:** Como equipo técnico quiero publicar solo aquello probado con Waves LV1 Classic V17 real.

**Diseño y tareas:** checklist rack/switch/NIC/firmware, versión exacta de LV1 + driver, copia de configuración, autorización del responsable de sonido, patch reversible, señal de prueba, captura WAV de diagnóstico, CPU/RAM/xruns, jitter, reconexión y 2 h grabando. Evitar cualquier cambio en la mezcla FOH. Medir umbrales de calidad con instrumentación; no inferir latencia total desde tamaño de buffer.

**Aceptación:** reporte `tests/hil/TEST_REPORT-OS-CPU.md` firmado por responsable, evidencia de WAV y métricas, errores/limitaciones por OS. **Pruebas:** stereo L/R, 8/16 canales disponibles, cambio de cable bajo condiciones seguras, silencio PCM y corte de stream, CPU alta.

