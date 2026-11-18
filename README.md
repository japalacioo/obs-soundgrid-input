# OBS SoundGrid Input

Plugin de OBS Studio para recibir audio de una consola **Waves eMotion LV1 Classic V17** a través del **Waves SoundGrid Driver V17**: ASIO en Windows y Core Audio en macOS. Incluye monitoreo y alertas operativas para producción en vivo.

> **Estado 0.3.0:** compilado y probado en Windows 11 con OBS 33.0.1. **Prueba con hardware Waves PARCIAL:** audio de una LV1 Classic V17 recibido por el driver Waves SoundGrid V17 hasta OBS; faltan las pruebas HIL de duración, reconexión y patch. macOS: código escrito, sin compilar.
> **Requiere el driver de Waves.** Recibir SoundGrid sin driver está bloqueado por ser un protocolo propietario (ver [docs/NATIVE_FEASIBILITY.md](docs/NATIVE_FEASIBILITY.md)).

## Qué hace
- **Fuente de audio "SoundGrid Input (Waves)":**
  - Elige el driver, la NIC de la red SoundGrid y los canales en modo mono, estéreo o multicanal (hasta 8, el límite de OBS).
  - Varias fuentes comparten un único stream del driver (hasta 128 canales): sirve para stems sin reabrir ni competir por el driver.
- **Alertas A01–A10:**
  - Detecta silencio en fuentes armadas, flujo interrumpido, driver perdido, cable desconectado, enlace a 100 Mbps, errores de NIC, latencia medida, cortes y clipping, y confirma la recuperación.
  - Correlaciona causas: un cable suelto produce un incidente, no una cascada.
- **Reconexión automática** solo del receptor. Nunca toca la consola, el reloj ni el patch.
- **Aviso rojo en OBS** (Windows) en cuanto se deja de recibir audio, con la causa y la hora; ámbar mientras se verifica la recuperación y verde al recuperarse. Un clic abre el panel.
  - Funciona **aunque no haya fuentes**: la vigilancia global escucha el driver Waves (entradas 1–2), incluido el silencio prolongado.
- **Mantiene abiertos** el SoundGrid Driver Control Panel y el servicio de Waves (los reabre si se cierran).
- **Configuración guiada:** detecta el driver Waves SoundGrid y la tarjeta de red que usa su Control Panel; los ajustes avanzados de alertas quedan ocultos hasta que se piden.
- **Panel de estado** con indicadores separados (NIC, driver, patch, audio, consola y latencia). Nunca muestra un único "Conectado".
  - Se abre desde **Herramientas > SoundGrid Monitor**.
- **Registros locales** sin IP ni MAC: log de OBS, `alerts.jsonl` rotativo y `diagnostics.json`.

## Documentación
| | |
|---|---|
| Instalar y configurar con la LV1 | [docs/INSTALL.md](docs/INSTALL.md) |
| Alertas | [docs/ALERTS.md](docs/ALERTS.md) |
| Problemas frecuentes | [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) |
| Limitaciones | [docs/LIMITATIONS.md](docs/LIMITATIONS.md) |
| Perfiles (Main L/R, bus de streaming, 16 stems) | [docs/PROFILES.md](docs/PROFILES.md) |
| Arquitectura | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) · [ADR](docs/ADR/README.md) |
| Viabilidad (Fase 0) | [docs/FEASIBILITY.md](docs/FEASIBILITY.md) · [matriz de compatibilidad](docs/COMPATIBILITY_MATRIX.md) |
| Pruebas | [docs/TEST_PLAN.md](docs/TEST_PLAN.md) · [docs/TEST_REPORT.md](docs/TEST_REPORT.md) · [HIL](tests/hil/HIL_PROCEDURE.md) |
| Especificación | [PLAN_AGENTE_…](PLAN_AGENTE_OBS_WAVES_LV1_CLASSIC_V17_WINDOWS_MACOS.md) · [SDD](SDD_AGILES_OBS_SOUNDGRID_WINDOWS_MACOS.md) · [specs/](specs/) |
| Licencias (GPLv3) | [docs/LICENSING.md](docs/LICENSING.md) |

## Compilar (Windows)
Requisitos: Visual Studio 2022 (C++), CMake 3.28 o superior y OBS Studio 33 instalado.

```bash
powershell -ExecutionPolicy Bypass -File tools/make-obs-sdk.ps1
```

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DSG_OBS_SDK_DIR=.deps/obs-sdk
```

```bash
cmake --build build --config Release
```

```bash
ctest --test-dir build -C Release --output-on-failure
```

```bash
powershell -ExecutionPolicy Bypass -File tools/install-windows.ps1
```

Para distribuir:
- **Instalador** (Inno Setup 6): `packaging/windows/build-installer.ps1` genera `dist/obs-soundgrid-input-<ver>-windows-x64-setup.exe` y su `.sha256`. Ver [docs/INSTALL.md](docs/INSTALL.md).
- **ZIP**: `packaging/windows/package.ps1` genera `dist/obs-soundgrid-input-<ver>-windows-x64.zip` y su `.sha256`.

## Estructura
```
src/common          reloj, tipos y ajustes versionados
src/audio           anillos SPSC, conversión, mapeo, medidores y timestamps
src/transport       IAudioTransport, simulador, DeviceHub y TransportManager
src/platform        ASIO y NIC (Windows), Core Audio y NIC (macOS)
src/observability   instantáneas, motor de alertas y salidas (JSONL, historial)
src/core            sesión por fuente, monitor y runtime
src/obs             módulo OBS: fuente, propiedades y diagnóstico
src/soundgrid_native  ruta B, NO IMPLEMENTADA (bloqueada)
tests/              unitarias, integración (libobs real), resistencia y HIL (procedimiento)
docs/               documentación, ADR e informes
```

## Créditos
Desarrollado por **Joan Palacio**.

## Licencia
Copyright (C) 2026 Joan Palacio. GPLv3 o posterior (ver [LICENSE](LICENSE) y [docs/LICENSING.md](docs/LICENSING.md)). No afiliado a Waves Audio ni a Steinberg.
