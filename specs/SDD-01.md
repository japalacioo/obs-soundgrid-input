# SDD-01 — Repositorio, módulos y CI multiplataforma

> Contrato general: consultar `../SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md`. Alcance exclusivo Windows/macOS. No confundir Mock con prueba HIL real.

**Prioridad:** P0 · **Dependencias:** 00 · **Entregable:** `CMakeLists.txt`, `CMakePresets.json`, pipeline y plugin vacío que carga en OBS.

**Historia:** Como desarrollador quiero compilar el mismo núcleo para Windows/macOS en cada PR.

**Diseño y tareas:** iniciar con `obsproject/obs-plugintemplate`, C++20, `libobs`, Qt6 solo si se requiere dock; runners Windows x64 y macOS Intel/arm64 o universal; análisis estático, clang-format, CTest y checks de empaquetado. Registrar versiones fijadas y artefactos separados por plataforma.

**Aceptación:** CI verde en 2 SO; OBS carga/desinstala el plugin sin crash; builds sin driver Waves. **Pruebas:** smoke de plugin, ABI, instalación y desinstalación en VM/host.

