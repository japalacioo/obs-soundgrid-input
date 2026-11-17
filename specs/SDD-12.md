# SDD-12 — Seguridad, configuración y telemetría

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P1 · **Dependencias:** 01,09 · **Entregable:** settings versionados, logs, política de privacidad.

**Historia:** Como administrador quiero diagnósticos útiles sin exponer configuraciones ni alterar la red de producción.

**Diseño y tareas:** JSON de settings con migración y respaldo, rotación de logs, redacción de PII/identificadores sensibles, métricas locales por defecto, sin telemetría remota sin consentimiento; auditar dependencias y accesos al driver. Guardar rutas OS correctas, evitar permisos privilegiados.

**Aceptación:** upgrades conservan perfiles, export de diagnóstico no incluye secretos, rotación y reinstalación estables. **Pruebas:** settings corruptos, migración, permisos restringidos.

