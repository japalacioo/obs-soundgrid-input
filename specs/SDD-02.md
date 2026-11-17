# SDD-02 — Abstracción IAudioTransport y Mock

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P0 · **Dependencias:** 01 · **Entregable:** API y Mock con tonos, ruido, silencio, dropout, drift y desconexión.

**Historia:** Como QA quiero reproducir todos los fallos sin afectar una consola real.

**Diseño y tareas:** state machine `UNAVAILABLE→DISCOVERED→CONFIGURED→STREAMING→DEGRADED→RECONNECTING`; inyección controlada de audio estéreo/multicanal; simulador determinista con reloj virtual.

**Aceptación:** mismo núcleo en Windows y macOS, 2/8/16/32 canales simulados, stop/start repetibles, errores clasificables. **Pruebas:** fixtures bit-exact y cambios de estado.

