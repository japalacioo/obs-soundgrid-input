# SDD-10 — Reconexión y resiliencia

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P0 · **Dependencias:** 02,03/04/05,09 · **Entregable:** máquina de estados y backoff.

**Historia:** Como operador quiero que se recupere una conexión sin detener ni bloquear OBS.

**Diseño y tareas:** reintentos limitados y exponenciales con jitter; no reconectar por silencio legítimo; manejar driver reiniciado, cable retirado, cambio de NIC, sleep/resume, cambio de sample rate; state snapshots y reinicio seguro sin leaks.

**Aceptación:** OBS sigue vivo y fuente muestra estado correcto; reconexión cuando se recupera transporte y enrutamiento; jamás modificar estado de la consola. **Pruebas:** 1000 ciclos con Mock, 20 ciclos de interrupción controlada HIL por OS disponible.

