# Solución de problemas

Empieza por el panel **Estado y alertas** (Herramientas > SoundGrid Monitor): cada indicador se evalúa por separado.

| Lo que ves | Causa probable | Qué hacer |
|---|---|---|
| La fuente "SoundGrid Input" no aparece en OBS | Plugin no instalado o en una ruta incorrecta | Busca `[soundgrid] OBS SoundGrid Input ... cargado` en Ayuda > Archivos de registro. La DLL debe estar en `%ProgramData%\obs-studio\plugins\obs-soundgrid-input\obs-soundgrid-input.dll` |
| El driver de Waves no aparece en "Dispositivo" | SoundGrid Driver V17 no instalado, o es un ASIO de 32 bits | Instálalo con Waves Central y reinicia. El plugin lee `HKLM\SOFTWARE\ASIO` (64 bits) y también `HKLM\SOFTWARE\WOW6432Node\ASIO` cuando el driver tiene DLL de 64 bits: el instalador de SoundGrid Drivers 17.3.37.122 solo registra la entrada en esta segunda clave |
| Driver: `ERROR · el driver rechazó init()` | Driver sin NIC configurada, servicio `SGDawNodeService` detenido u otro programa usándolo | Abre el SoundGrid Driver Control Panel y elige la NIC. Si el driver responde "Error Initializing (0000048c), Make sure SoundGrid is configured properly…" y el log `%LOCALAPPDATA%\Waves Audio\Logs\SGDawNodeService.log` dice "Could not Start Kernel Driver Streaming err: -1036", el driver aún no transmite: asígnalo en la LV1 (Setup > Inventory). Después de asignarlo, el servicio de Waves no reintenta solo: en el Control Panel cambia el puerto a "None" y vuelve a elegir la NIC. Cierra DAWs y otras fuentes ASIO (incluida la fuente ASIO nativa de OBS 33.1+) |
| Driver: `BUSY` | Otro cliente tiene el driver | Igual que la fila anterior. Varias fuentes de **este** plugin comparten un solo stream: no compiten entre sí |
| Driver: `NOT_SUPPORTED · el driver no expone entradas` | El driver no tiene entradas asignadas | En la LV1, ve a Setup > Inventory y asigna canales al driver |
| Driver: `FORMAT_UNSUPPORTED` | Se pidió una frecuencia distinta a la de la sesión | El plugin nunca cambia el reloj: usa la frecuencia de la sesión LV1 |
| Driver STREAMING pero "Audio: silencio digital" | Nada parcheado a esas entradas, canal en mute o entrada equivocada | Revisa Patch > Output en la LV1 y las entradas L/R elegidas. Los números son entradas del driver, no canales LV1 |
| A04 "Sin conexión de red" | Cable o switch | Revisa el cable y el switch. La alerta se basa en el estado físico de la NIC elegida |
| A05 "Enlace degradado" (100 Mbps) | Cable dañado o puerto del switch | Cambia el cable o el puerto. No es una pérdida de audio demostrada |
| A08 "Cortes de audio" | CPU saturada, driver irregular o buffer pequeño | Sube el buffer del driver o de la red y revisa la CPU. Mira los contadores en el panel |
| El driver va "más rápido" que el tiempo real (muchas discontinuidades) | Driver sin reloj de host (p. ej. ReaRoute sin REAPER) | Revisa el reloj del driver y de la red SoundGrid |
| El nombre de canal no coincide con la LV1 | Comportamiento esperado: no hay API pública de nombres | Lleva tu propia correspondencia (ver PROFILES.md) |
| No hay notificaciones emergentes | En 0.1.0 no hay dock Qt (ADR-014) | Revisa el panel, el log de OBS y `alerts.jsonl` |

**Para pedir soporte**, pulsa **Exportar diagnóstico** y adjunta:
- `%APPDATA%\obs-studio\plugin_config\obs-soundgrid-input\diagnostics.json`
- `alerts.jsonl`
- el log de OBS

No contienen IP ni MAC.
