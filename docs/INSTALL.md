# Instalación

## Requisitos
- **OBS Studio 33.x** (probado con 33.0.1). Windows 10/11 x64, o macOS (sin validar en 0.1.0).
- Para audio real de la LV1:
  - **Waves SoundGrid Driver V17**: ASIO en Windows, Core Audio en macOS. Se instala con Waves Central.
  - Una NIC Gigabit dedicada conectada al switch SoundGrid.
- Visual C++ Redistributable 2015-2022 x64, que OBS ya instala.

## Windows
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
