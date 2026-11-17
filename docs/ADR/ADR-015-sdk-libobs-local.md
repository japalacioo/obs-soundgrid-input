# ADR-015: Compilar contra la instalación de OBS mediante un SDK local

**Estado:** Aceptada · **Fecha:** 2026-11-17

El `obs-plugintemplate` oficial fija OBS 31.1.1. `tools/make-obs-sdk.ps1`:
- toma las cabeceras de la etiqueta exacta de OBS instalada (33.0.1, desde github.com/obsproject/obs-studio);
- genera `obs.lib` y `obs-frontend-api.lib` con `dumpbin`/`lib` a partir de las DLL instaladas, sin modificar la instalación.

Así el ABI coincide con el OBS que se va a usar. La CI hace lo mismo con el ZIP portable de la release.
