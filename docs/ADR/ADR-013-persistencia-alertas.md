# ADR-013: Persistencia de configuración y de alertas

**Estado:** Aceptada · **Fecha:** 2026-11-17

**Configuración (umbrales, perfil, reglas):**
- Va en los ajustes de cada fuente de OBS (`obs_data`), dentro de la colección de escenas.
- Esquema versionado (`alerts.schema`): valores ausentes, de tipo incorrecto o fuera de rango se sustituyen por los del perfil.

**Eventos:**
- `alerts.jsonl` rotativo: 5 MB y 5 archivos, en `%APPDATA%\obs-studio\plugin_config\obs-soundgrid-input\` (macOS: `~/Library/Application Support/obs-studio/plugin_config/obs-soundgrid-input/`).
- Historial en memoria de 500 eventos.
- Log de OBS.

**Privacidad:** sin IP ni MAC. La NIC aparece por su alias visible.
