# Fase 0: viabilidad técnica (SDD-00)

**Fecha:** 17 de noviembre de 2026 · **Estado:** decisión tomada (ver [ADR-002](ADR/ADR-002-rutas-A-B.md)) · **Hardware LV1 V17:** no disponible (`HIL_REQUIRED`).

Leyenda de confianza: **V** = verificado leyendo la fuente · **I** = inferido · **NF** = no encontrado.
Las fuentes se consultaron el 16 y 17 de noviembre de 2026. Ninguna consulta envió datos ni contactó a terceros.

## 1. Decisión

| Ruta | Windows 10/11 x64 | macOS Intel / Apple Silicon |
|---|---|---|
| **A (driver oficial Waves)** | **VIABLE.** Cliente ASIO del Waves SoundGrid ASIO Driver V17. Implementado y probado con un driver ASIO real (ReaRoute), **sin LV1**. | **VIABLE PERO FRÁGIL.** Cliente Core Audio. Código escrito, **no compilado ni probado** (no hay Mac). Waves desaconseja Mac para grabar. |
| **B (SoundGrid nativo sin driver)** | **BLOQUEADO POR PROTOCOLO** | **BLOQUEADO POR PROTOCOLO** |

**Motivo del bloqueo de la ruta B:** SoundGrid es propietario, no hay SDK ni especificación pública, y el acceso solo existe mediante un acuerdo con Waves. Ver [NATIVE_FEASIBILITY.md](NATIVE_FEASIBILITY.md) y el borrador de consulta [WAVES_SDK_REQUEST.md](WAVES_SDK_REQUEST.md). La consulta **no se ha enviado**: debe hacerla el propietario del proyecto.

## 2. Componentes del sistema

| Componente | Papel | ¿Lo controla el plugin? |
|---|---|---|
| Waves eMotion LV1 Classic V17 | Mezcla; envía audio al driver mediante el patch | **No.** Prohibido escribir (ADR-006) |
| I/O SoundGrid y servidor DSP | Conversión y proceso en la red | No |
| Switch Gigabit dedicado | Transporte SoundGrid | No (solo telemetría pasiva de la NIC del PC) |
| NIC del PC receptor | Puerto Ethernet hacia el switch | Solo lectura: enlace, velocidad y errores |
| Waves SoundGrid Driver V17 (ASIO/Core Audio) + `SGDawNodeService` | Expone los canales parcheados como dispositivo de audio | Solo como cliente (abrir, leer y cerrar) |
| OBS Studio 33.0.1 | Mezcla, grabación y streaming | Fuente de audio del plugin |

## 3. Hallazgos

### 3.1 Waves SoundGrid Driver V17
Fuente: <https://www.waves.com/support/tech-specs/system-requirements> (13-jul-2026).

- **V · Sistemas:** Windows 10 64 bits y Windows 11. macOS Sonoma 14, Sequoia 15, Tahoe 26 y Golden Gate 27.
- **V · Capacidad:** hasta 128 entradas y 128 salidas; 44.1, 48, 88.2 y 96 kHz.
- **V · Buffer de red:** de 104 a 1024 muestras, sumado al buffer ASIO/Core Audio.
- **V · Restricciones relevantes:**
  - Los Intel Core Ultra no están soportados para rec/pb.
  - Waves recomienda Windows frente a Apple Silicon por "intermittent dropouts".
  - No se recomienda grabar más de 32 canales por el Ethernet integrado del Mac.
  - En Windows con USB-C recomiendan adaptadores ASIX AX88179A.
  - Todos los equipos de la red deben llevar el driver V17.
- **V/I · Apple Silicon:** la página indica "via Rosetta" (parece texto heredado; hay que verificarlo en el equipo).
  - <https://www.waves.com/support/how-to-set-up-soundgrid-apps-and-driver-on-apple-silicon-macs> (28-ene-2024): User Mode puede cortar a 96 kHz y Kernel Mode exige "Reduced Security".

### 3.2 Cómo recibe audio un PC desde la LV1
Fuentes:
- <https://www.waves.com/support/add-a-recording-playback-computer-to-a-soundgrid-network> (02-mar-2026)
- <https://www.waves.com/support/how-to-setup-tracks-live-for-recording-and-playback-with-emotion-lv1> (22-sep-2026)

Pasos (V):
1. Instalar el "SoundGrid V17 ASIO/Core Audio Rec/PB Driver" con Waves Central.
2. Cablear el PC al switch.
3. Elegir la NIC en el **SoundGrid Driver Control Panel**.
4. En la LV1, ir a **Setup > Inventory > I/O**, asignar el driver y fijar el número de canales y el buffer de red.
5. En **Patch > Output** (o Device-to-Device), enviar canales, buses o salidas a las entradas del driver.

Además:
- **NF · Persistencia tras reiniciar:** no está documentado si la NIC y la asignación se conservan. Requiere HIL (prueba T19).
- **I · Control Panel:** probablemente no hace falta tenerlo abierto, porque el motor es el servicio `SGDawNodeService` (<https://www.waves.com/support/how-to-solve-version-not-available-in-soundgrid-drive-control-panel-in-windows>, 24-ene-2024). Requiere HIL.

### 3.3 SDK y API
- **V · Protocolo propietario:** "Waves' proprietary Audio-over-IP (AoIP) protocol" (<https://www.waves.com/support/waves-live-common-questions-answered>, 14-jul-2026).
- **V · Desarrolladores:** la página para desarrolladores (16-sep-2014) habla de partnerships con "tools, support and supervision", sin descargas ni términos (<https://www.waves.com/soundgrid-for-hardware-and-software-developers>).
- **NF · SDK y API pública:** las búsquedas "SoundGrid SDK" y "SoundGrid developer" no dieron resultados.
- **NF · Nombres de canal de la LV1:** solo existe la integración propia de Tracks Live ("Waves Integration") y las apps MyRemote, con protocolo no documentado. No se encontró OSC ni API pública.

### 3.4 OBS Studio
- **V · Versión:** la estable es 33.0.1 (29-oct-2026) y es la instalada en el equipo de desarrollo. Licencia GPLv2 o posterior.
- **V · Límites de audio** (`libobs/media-io/audio-io.h`, verificado en las cabeceras 33.0.1):
  - `MAX_AUDIO_CHANNELS = 8` por fuente.
  - `MAX_AUDIO_MIXES = 6` pistas.
  - **Consecuencia de diseño:** para más de 8 canales (stems) hacen falta varias fuentes. El plugin las resuelve compartiendo un único stream del driver (ADR-009).
- **V · ASIO nativo en OBS:** el PR #12733 ("win-asio: ASIO host") se fusionó el 03-nov-2026 para **OBS 33.1**. No está en la 33.0.1.
  - Limita cada dispositivo a 32 canales (`MAX_DEVICE_CHANNELS 32` en `asio-common.h`).
  - Reduce el valor diferencial del plugin en Windows, que queda en:
    - varias fuentes sobre un stream de hasta 128 canales;
    - alertas A01–A10 y diagnóstico de red;
    - estados honestos (NIC, driver, patch, audio).
- **V · macOS:** OBS ya captura cualquier dispositivo Core Audio (`coreaudio_input_capture`) con hasta 8 canales por fuente.
- **V · Plugins de terceros en OBS 33 (Windows):** `%ProgramData%\obs-studio\plugins\<nombre>\<nombre>.dll` + `data\`. **Verificado en este equipo:** OBS 33.0.1 carga el plugin desde esa ruta.

### 3.5 Licencias
- **V · ASIO SDK 2.3.4:** licencia dual, propietaria o GPLv3.
  - Mismo dato en el espejo con LICENSE.txt (<https://codeberg.org/rubiefawn/asiosdk>, 22-jul-2026), en el paquete MSYS2 `asiosdk` 2.3.4 ("GPL-3.0-only") y en la nota de Steinberg y OBS (15-oct-2025).
  - No se pudo leer la página oficial de Steinberg porque se renderiza con JavaScript.
- **Decisión:** el plugin usa las cabeceras mínimas GPLv3 que publica OBS en `plugins/win-asio` (derivadas del SDK 2.3.4). El plugin se distribuye bajo **GPLv3**. Ver [LICENSING.md](LICENSING.md).
- **V · Andersama/obs-asio:** GPL-3.0, último commit en enero de 2021. Se descartó como base por estar abandonado; no se copió código.

### 3.6 Telemetría de red sin privilegios
- **V · Windows:** `GetIfTable2` → `MIB_IF_ROW2` (`MediaConnectState`, `ReceiveLinkSpeed`, `InErrors`, `InDiscards`, `OutErrors`). Verificado en este equipo sin administrador.
- **V/I · macOS:** `getifaddrs` + `if_data` (`ifi_baudrate`, `ifi_ierrors`, `ifi_oerrors`, `ifi_iqdrops`). **No existe `ifi_link_state`**, así que el carrier se lee con `SIOCGIFMEDIA` (`IFM_ACTIVE`).

## 4. Qué se puede detectar SIN el driver de Waves

| Capacidad | Sin driver | Con driver V17 |
|---|---|---|
| NIC presente, enlace UP/DOWN, velocidad, errores | **Sí** (API del SO) | Sí |
| Que haya una red SoundGrid en ese puerto | **No** (solo "red no identificada" con IP APIPA, y eso no prueba nada) | No por API pública |
| Que exista una consola LV1 y su identidad | **No** | **No** (no hay API pública) |
| Nombres de canal de la LV1 | **No** | **No** (solo puertos del driver) |
| PCM de canales parcheados | **No** | **Sí**, si NIC, Inventory y patch están configurados |

## 5. Pruebas de viabilidad ejecutadas en el equipo de desarrollo

Equipo: i7-1255U, Windows 11 (build 26300), OBS 33.0.1.

| Prueba | Resultado |
|---|---|
| Driver ASIO real (ReaRoute ASIO x64, de REAPER) | `open/start/stop/close` correctos. 16 canales, 48 kHz, int32, buffer de 1024. Llegaron callbacks. **Sin REAPER abierto el driver corre a 1,99× tiempo real y no alterna el índice de buffer**: el plugin lo cuenta como anomalía y lo ve como discontinuidades. |
| NIC real (Realtek PCIe GbE) | UP a 1000 Mbps y contadores de error legibles sin administrador |
| OBS 33.0.1 real (GUI) | El plugin carga desde `%ProgramData%` ("Loaded Modules"), inicializa en ~1 ms y se descarga limpio |
| libobs 33.0.1 real sin interfaz | 15/15 comprobaciones (T01, T02, T03, T08, T09, T10, T40, T50) |

## 6. Riesgos abiertos
1. **Sin LV1 ni driver Waves:** toda la captura real con SoundGrid queda `NOT_RUN` (T14–T21, T44–T47, T56–T57).
2. **Un solo cliente por driver:** si el SoundGrid ASIO solo admite un cliente, el plugin choca con la fuente ASIO nativa de OBS 33.1 o con un DAW abierto a la vez. El plugin informa `BUSY`; hay que verificarlo en HIL.
3. **Persistencia de la configuración del driver tras reiniciar:** no está documentada (T19).
4. **macOS:** sin compilar ni probar. Waves desaconseja Mac para grabar (cortes en Apple Silicon y más de 32 canales por el Ethernet integrado).
5. **Intel Core Ultra** no está soportado por Waves para rec/pb.
6. **Plantilla de plugin de OBS:** el `buildspec` oficial sigue fijado en 31.1.1. Aquí se compila contra cabeceras 33.0.1 y bibliotecas de importación generadas de la instalación (ver `tools/make-obs-sdk.ps1`).
