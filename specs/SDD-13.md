# SDD-13 — CI/CD, empaquetado y firmados

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P1 · **Dependencias:** 01,07,11 · **Entregable:** releases reproducibles por OS.

**Historia:** Como usuario quiero instalar el plugin directamente en mi sistema.

**Diseño y tareas:** GitHub Actions matrix, artefactos con SHA256 y símbolos de depuración; Windows ZIP/installer firmado si certificado disponible; macOS bundle universal o arquitecturas separadas, codesign/notarización cuando haya credenciales; No empaquetar drivers Waves ni SDK restringidos sin licencia.

**Aceptación:** instala, OBS arranca, aparece la fuente, se elimina limpio; artefactos para 2 SO en cada release; matriz de compatibilidad visible. **Pruebas:** smoke en máquinas limpias, tests de firma y hashes.

