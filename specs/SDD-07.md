# SDD-07 — AudioEngine y compatibilidad con libobs

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P0 · **Dependencias:** 02 · **Entregable:** buffers RT, conversión, salida OBS, resampler si aplica.

**Historia:** Como productor quiero audio continuo y sincronizable con el video en Windows y macOS.

**Diseño y tareas:** ring SPSC preasignado, contador de pérdida, mapeo PCM al formato `obs_source_audio`, timestamps monotónicos en nanosegundos, conversión 44.1/48/88.2/96k donde el transporte lo soporte, test de canales y reloj, downmix configurable sin afectar FOH.

**Aceptación:** ningún lock de UI/disco/red en hilo RT; 2/8/16/32 canales simulados, no desbordes, tolerancia a resync medida. **Pruebas:** WAV de referencia, pruebas de drift y stress >2 h bajo carga.

