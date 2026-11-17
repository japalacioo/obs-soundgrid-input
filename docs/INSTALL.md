# Instalación

## Requisitos
- **OBS Studio 33.x** (probado con 33.0.1). Windows 10/11 x64, o macOS (sin validar en 0.1.0).
- Para audio real de la LV1:
  - **Waves SoundGrid Driver V17**: ASIO en Windows, Core Audio en macOS. Se instala con Waves Central.
  - Una NIC Gigabit dedicada conectada al switch SoundGrid.
- Visual C++ Redistributable 2015-2022 x64, que OBS ya instala.

## Windows con el instalador (recomendado)
1. Copia `obs-soundgrid-input-<versión>-windows-x64-setup.exe` al otro equipo. Puedes verificarlo con el `.sha256` que lo acompaña.
2. Cierra OBS (el instalador lo detecta y espera).
3. Ejecuta el instalador y acepta el aviso de Windows (UAC). No está firmado: si SmartScreen lo bloquea, pulsa **Más información > Ejecutar de todas formas**.
4. Sigue el asistente (español o inglés). Al final indica si encontró el driver Waves SoundGrid.
5. Abre OBS > Fuentes > **+** > **SoundGrid Input (Waves)**. Si el driver Waves está instalado, la fuente se configura sola con el driver y la tarjeta de red de su Control Panel.

Se instala en `%ProgramData%\obs-studio\plugins\obs-soundgrid-input\` para todos los usuarios, con la guía en el menú Inicio. Se desinstala desde **Configuración > Aplicaciones > OBS SoundGrid Input**; tus escenas y la configuración del plugin se conservan.

Instalación desatendida (por ejemplo, para varios equipos):
```bash
obs-soundgrid-input-0.1.0-windows-x64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
```
Devuelve 0 si se instaló; si OBS está abierto, no instala y devuelve un código distinto de 0.

Para generar el instalador desde el código (requiere Inno Setup 6):
```bash
powershell -ExecutionPolicy Bypass -File packaging/windows/build-installer.ps1
```

## Windows con el ZIP (sin instalador)
1. Cierra OBS.
2. Descomprime `obs-soundgrid-input-<versión>-windows-x64.zip`.
3. Ejecuta `install.ps1` con clic derecho > "Ejecutar con PowerShell", o:
   ```bash
   powershell -ExecutionPolicy Bypass -File install.ps1
   ```
   Se instala en `%ProgramData%\obs-studio\plugins\obs-soundgrid-input\`, el formato de plugins de OBS 33. No requiere permisos de administrador y no modifica la instalación de OBS.
4. Abre OBS > Fuentes > **+** > **SoundGrid Input (Waves)**.

Para desinstalar, ejecuta `uninstall.ps1`. Tus escenas no se modifican; la carpeta de configuración del plugin se conserva.

Desde el código fuente:
```bash
powershell -ExecutionPolicy Bypass -File tools/install-windows.ps1
```

## macOS (NO VALIDADO)
```bash
packaging/macos/package.sh build-macos Release
```
El `.pkg` instala `obs-soundgrid-input.plugin` en `/Library/Application Support/obs-studio/plugins/`. Sin firma ni notarización, macOS lo bloquea: hay que autorizarlo en Ajustes > Privacidad y seguridad. OBS necesita permiso de micrófono para leer dispositivos Core Audio.

## Primera configuración con la LV1 (ruta oficial; se hace fuera del plugin)
1. Instala el SoundGrid Driver V17 y reinicia.
2. Conecta el PC al switch SoundGrid con la NIC dedicada.
3. En el **SoundGrid Driver Control Panel**, elige esa NIC.
4. En la LV1, ve a **Setup > Inventory > I/O**, asigna el driver del PC y fija el número de canales y el buffer de red.
5. En la LV1, ve a **Patch > Output** y envía Main L/R, un bus o canales a las entradas del driver. **Hazlo en una ventana autorizada y fuera de show.**
6. En OBS, en la fuente **SoundGrid Input**:
   - **Dispositivo:** el driver marcado "driver Waves SoundGrid".
   - **NIC:** la conectada al switch. Activa la supervisión de red (A04–A06).
   - **Modo:** Estéreo, con L y R en las entradas del driver que parcheaste.
   - **Alertas:** elige el perfil. Arma A01 solo si esa fuente debe tener señal continua.
7. Comprueba el panel **Estado y alertas**. Debe mostrar Driver STREAMING y Audio "Recibiendo PCM" con nivel.

Si algo no aparece, consulta [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
