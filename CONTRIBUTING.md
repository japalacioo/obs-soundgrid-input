# Contribuir

Gracias por tu interés. Este proyecto es un plugin de OBS Studio para recibir audio de consolas Waves eMotion LV1 por el driver SoundGrid y avisar cuando se pierde.

## Informar de un problema

Abre un [issue](https://github.com/japalacioo/obs-soundgrid-input/issues/new/choose) con la plantilla de error e incluye:
- Versión del plugin, de OBS, de Windows y del SoundGrid Driver.
- Qué esperabas y qué pasó.
- El diagnóstico exportado (botón **Exportar diagnóstico** del panel) y las líneas `[soundgrid]` del log de OBS (**Ayuda > Archivos de registro**). No contienen IP ni MAC; revísalos igualmente antes de publicarlos.

Para fallos de seguridad, **no abras un issue público**: ver [SECURITY.md](SECURITY.md).

## Reglas del proyecto

- **Nunca modificar la consola.** El plugin solo recibe audio: no cambia patch, reloj, ganancia, mute ni escenas de la LV1 (ver [ADR-006](docs/ADR/ADR-006-sin-escritura-lv1.md)).
- **Sin ingeniería inversa de SoundGrid** ni dependencias de APIs privadas de Waves. La ruta sin driver está bloqueada por diseño ([NATIVE_FEASIBILITY](docs/NATIVE_FEASIBILITY.md)).
- **El hilo de audio no asigna memoria ni bloquea.** Hay una prueba que lo comprueba (T48).
- **No afirmar lo que no se ha probado.** Los informes distinguen `PASS`, `NOT_RUN` y `BLOCKED`; las pruebas con hardware Waves se documentan en [TEST_REPORT](docs/TEST_REPORT.md) con evidencia.
- Cambios en transporte, audio, hilos o alertas actualizan [ARCHITECTURE](docs/ARCHITECTURE.md), los ADR y las pruebas en el mismo cambio.

## Preparar el entorno (Windows)

Requisitos: Visual Studio 2022 con "Desarrollo para escritorio con C++", CMake 3.28+, Git, OBS Studio 33 instalado y, para el instalador, Inno Setup 6.

1. SDK local de libobs: descarga las cabeceras de OBS de tu versión instalada y genera las bibliotecas de importación en `.deps/obs-sdk`:
   ```bash
   powershell -ExecutionPolicy Bypass -File tools/make-obs-sdk.ps1
   ```
2. Configurar, compilar y probar:
   ```bash
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DSG_OBS_SDK_DIR=.deps/obs-sdk
   ```
   ```bash
   cmake --build build --config Release
   ```
   ```bash
   ctest --test-dir build -C Release --output-on-failure
   ```
3. Instalar en tu OBS (cierra OBS antes):
   ```bash
   powershell -ExecutionPolicy Bypass -File tools/install-windows.ps1
   ```
   Si instalaste antes con el `.exe`, la carpeta es de administrador: usa el instalador generado con `packaging/windows/build-installer.ps1`.

El núcleo (`sg_core`) no depende de OBS ni de Qt y se prueba sin hardware con un simulador de 32 canales. Para medir un driver real en solo lectura: `build/tests/Release/sg_asio_probe.exe SoundGrid 10`.

## Enviar cambios

1. Crea una rama desde `main`.
2. Añade o actualiza pruebas. `ctest` debe pasar completo.
3. Abre un pull request explicando qué cambia, cómo lo probaste y con qué hardware (o "solo simulador").

Las contribuciones se publican bajo la licencia del proyecto, **GPLv3 o posterior**.

## Integración continua

`.github/workflows/ci.yml` está definida pero **se ejecuta a mano** (Actions > ci > Run workflow) porque aún no se ha validado en los runners de GitHub. Las pruebas con hardware Waves nunca se ejecutan en CI.
