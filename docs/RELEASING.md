# Publicar una versión

Cómo se genera y publica una versión con los instaladores de **Windows** y **macOS**. La v0.4.0 se publicó así: primero los archivos de Windows y después, en la misma release, los de macOS (Mateo Salazar).

## Qué lleva cada release

Una sola release por versión (etiqueta `vX.Y.Z`), con **8 archivos**:

| Sistema | Archivo | Para qué |
|---|---|---|
| Windows | `obs-soundgrid-input-X.Y.Z-windows-x64-setup.exe` | Instalador (Inno Setup), para todos los usuarios |
| Windows | `obs-soundgrid-input-X.Y.Z-windows-x64-setup.exe.sha256` | Comprobar la descarga |
| Windows | `obs-soundgrid-input-X.Y.Z-windows-x64.zip` | Alternativa sin instalador (`install.ps1`) |
| Windows | `obs-soundgrid-input-X.Y.Z-windows-x64.zip.sha256` | Comprobar la descarga |
| macOS | `obs-soundgrid-input-X.Y.Z-macos-universal.pkg` | Instalador universal (Apple Silicon e Intel), por usuario |
| macOS | `obs-soundgrid-input-X.Y.Z-macos-universal.pkg.sha256` | Comprobar la descarga |
| macOS | `obs-soundgrid-input-X.Y.Z-macos-universal.zip` | Alternativa sin instalador (carpeta `.plugin`) |
| macOS | `obs-soundgrid-input-X.Y.Z-macos-universal.zip.sha256` | Comprobar la descarga |

Ningún instalador está firmado: no hay certificado de Windows ni cuenta de desarrollador de Apple.

## 1. Preparar la versión

1. La versión está en `CMakeLists.txt` (`project(obs-soundgrid-input VERSION X.Y.Z ...)`). Los scripts de empaquetado la leen de ahí.
2. Actualiza [RELEASE_NOTES.md](RELEASE_NOTES.md), [TEST_REPORT.md](TEST_REPORT.md) y, si cambian los requisitos o el estado, [README](../README.md), [INSTALL.md](INSTALL.md), [LIMITATIONS.md](LIMITATIONS.md) y [COMPATIBILITY_MATRIX.md](COMPATIBILITY_MATRIX.md).
3. Fusiona en `main` y crea la etiqueta sobre ese commit:

```bash
git tag vX.Y.Z && git push origin vX.Y.Z
```

Los dos sistemas se compilan desde **el mismo commit** de la etiqueta.

## 2. Windows

En Windows 10/11 x64 con Visual Studio 2022, CMake 3.28+, OBS Studio 33 e Inno Setup 6 (ver [CONTRIBUTING.md](../CONTRIBUTING.md#preparar-el-entorno-windows)):

```bash
powershell -ExecutionPolicy Bypass -File tools/make-obs-sdk.ps1
```

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DSG_OBS_SDK_DIR=.deps/obs-sdk
```

```bash
cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

```bash
powershell -ExecutionPolicy Bypass -File packaging/windows/build-installer.ps1
```

```bash
powershell -ExecutionPolicy Bypass -File packaging/windows/package.ps1
```

Quedan en `dist/` el `.exe`, el `.zip` y sus `.sha256`. En la v0.4.0: 142/142 pruebas.

**Prueba antes de publicar:** instala el `.exe` en un OBS real, comprueba en el log `OBS SoundGrid Input X.Y.Z`, abre el Multiview y cierra OBS con `Number of memory leaks: 0`.

## 3. macOS

En un Mac con Xcode Command Line Tools, CMake 3.28+ y OBS 30+ en `/Applications/OBS.app` (ver [CONTRIBUTING.md](../CONTRIBUTING.md#preparar-el-entorno-macos)). Borra `build-macos` para no mezclar versiones:

```bash
git checkout vX.Y.Z && rm -rf build-macos
```

```bash
tools/make-obs-sdk.sh
```

```bash
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release -DSG_OBS_SDK_DIR=$PWD/.deps/obs-sdk
```

```bash
cmake --build build-macos -j8 && ctest --test-dir build-macos --output-on-failure
```

```bash
packaging/macos/package.sh build-macos
```

- Quedan en `dist/` el `.pkg`, el `.zip` y sus `.sha256`. En la v0.4.0: 141/141 pruebas, con `OBS_Monitor_Render` (MON01–MON10).
- `package.sh` imprime las arquitecturas: deben ser `x86_64 arm64`. Si sale una sola, el sufijo cambia a `macos-arm64` o `macos-x86_64`: no lo publiques como universal.
- Para firmar y notarizar, define `SG_INSTALLER_ID` y `SG_NOTARY_PROFILE` antes de `package.sh` (requiere cuenta de desarrollador de Apple).

**Prueba antes de publicar** (con OBS cerrado):

```bash
installer -pkg dist/obs-soundgrid-input-X.Y.Z-macos-universal.pkg -target CurrentUserHomeDirectory
```

Abre OBS y comprueba lo mismo que en Windows: la versión en el log, la escena del monitor creada sola, el Multiview y `Number of memory leaks: 0`. Las pruebas de la v0.4.0 están en [TEST_REPORT.md](TEST_REPORT.md#instalador-de-macos-v040).

Copia de los archivos publicados de macOS: [releases/v0.4.0/macos](../releases/v0.4.0/macos/).

## 4. Subir a GitHub

Crea **una sola release** con la etiqueta `vX.Y.Z` y sube los archivos de cada sistema a la misma. Si un sistema se compila después, añade sus archivos a la release existente: no crees otra ni sustituyas los del otro sistema.

**Desde la web:** Releases > la versión > **Edit release** (icono del lápiz) > arrastra los archivos a «Attach binaries» > **Update release**. Deja marcada **Set as the latest release**.

**Con GitHub CLI** (sesión iniciada con permiso de escritura):

```bash
gh release upload vX.Y.Z --repo japalacioo/obs-soundgrid-input dist/obs-soundgrid-input-X.Y.Z-macos-universal.*
```

```bash
gh release view vX.Y.Z --repo japalacioo/obs-soundgrid-input --json assets --jq '.assets[].name'
```

Deben salir los 8 archivos.

## 5. Texto de la release

La descripción sigue este orden (ver la [v0.4.0](https://github.com/japalacioo/obs-soundgrid-input/releases/tag/v0.4.0)):

1. Una frase de qué es el plugin.
2. **Descargar e instalar (Windows 10/11 x64):** 4 pasos, enlace a [INSTALL.md](INSTALL.md) y requisitos.
3. **Descargar e instalar (macOS, experimental):** 4 pasos (incluido el permiso de micrófono y la tarjeta de red), enlace a [INSTALL.md#macos](INSTALL.md#macos), requisitos y diferencias con Windows.
4. **Novedades** de la versión, con el autor de cada aporte.
5. **Incluido desde versiones anteriores.**
6. **Estado:** qué se probó en cada sistema y con qué hardware, y qué no.
7. **SHA-256** de los 4 instaladores y ZIP, en un bloque de código.
8. **English summary** con las mismas ideas, incluido macOS.

## 6. Comprobar la descarga pública

Descarga cada instalador desde la página de la release y compara su SHA-256 con el de su `.sha256` y con el del texto de la release:

```bash
shasum -a 256 obs-soundgrid-input-X.Y.Z-macos-universal.pkg
```

```bash
powershell -Command "(Get-FileHash .\obs-soundgrid-input-X.Y.Z-windows-x64-setup.exe -Algorithm SHA256).Hash"
```

## Reglas

- **No afirmar lo que no se ha probado.** El estado de la release y el [TEST_REPORT](TEST_REPORT.md) distinguen `PASS` y `NOT_RUN`, y dicen si hubo hardware Waves.
- Si algo no compila o una prueba falla, no se publica: se corrige en `main` y se vuelve a etiquetar.
- Nunca se modifica la consola LV1 ni la configuración de Waves para probar una versión.
