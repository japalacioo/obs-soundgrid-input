# SDD-04 — Captura macOS Core Audio V17

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P0 (macOS) · **Dependencias:** 00,02 · **Entregable:** `CoreAudioTransport`.

**Historia:** Como operador en Mac quiero usar el driver Waves instalado con el mismo panel de OBS.

**Diseño y tareas:** enumerar AudioDevices y streams Core Audio/HAL; seleccionar SoundGrid cuando se presente al OS; mapear formatos; reaccionar a cambios de dispositivos/frecuencia; verificar OBS nativo Intel y Apple Silicon, instalación Rosetta y permisos según las combinaciones reales. Probar comportamiento ante sleep/wake y cambios de red.

**Aceptación:** PCM real en macOS Intel y/o Apple Silicon únicamente donde HIL lo demuestre; registrar incompatibilidad y dropouts por combinación. **Pruebas:** HIL separado por CPU, reconexión y 2 h; jamás etiquetar «Universal = driver compatible».

