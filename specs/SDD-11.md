# SDD-11 — UX multiplataforma SoundGrid Monitor

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P1 · **Dependencias:** 06,08,09 · **Entregable:** propiedades OBS, dock opcional, traducciones.

**Historia:** Como operador quiero una UX coherente de estado, selección y alertas en ambos SO.

**Diseño y tareas:** `obs_properties_t` primero; Qt6 dock opcional ajustado a ABI OBS; vistas `Conexión`, `Canales`, `Audio`, `Alertas`, `Diagnósticos`. Etiquetas accesibles y logs exportables sin secretos. Mostrar claramente `Driver Waves no disponible` o `Configuración requerida` según diagnóstico.

**Aceptación:** UI sin congelamientos, accesible, persiste por escena/fuente; misma semántica en Windows/macOS. **Pruebas:** smoke UI por OS, escalado DPI, cambio tema.

