# Seguridad

## Versiones con soporte

Solo la última versión publicada en [Releases](https://github.com/japalacioo/obs-soundgrid-input/releases) recibe correcciones.

## Informar de una vulnerabilidad

No abras un issue público. Usa **[Report a vulnerability](https://github.com/japalacioo/obs-soundgrid-input/security/advisories/new)** (pestaña Security del repositorio) e incluye la versión, los pasos para reproducirlo y su impacto. Se responderá en cuanto sea posible.

## Qué hace el plugin en tu equipo

Para que puedas evaluarlo:
- Se carga dentro de OBS y lee audio del driver ASIO de Waves; **no envía tráfico de red** ni se conecta a Internet.
- Lee el estado de la tarjeta de red (enlace, velocidad, errores) con las API de Windows, sin privilegios de administrador.
- Lee, sin modificarlos, el registro de Windows (drivers ASIO, ubicación del software de Waves) y el archivo de preferencias del SoundGrid Driver Control Panel (`%APPDATA%\Waves Audio\Preferences\SoundGrid.Driver.preferences`) para saber qué tarjeta de red usa.
- Si la opción está activada, **arranca** el SoundGrid Driver Control Panel y el servicio `SGDawNodeService` con el mismo comando que registra Waves, sin elevar permisos.
- Escribe solo en su carpeta de configuración (`%APPDATA%\obs-studio\plugin_config\obs-soundgrid-input`). Los registros no contienen IP ni MAC.
- El instalador escribe en `%ProgramData%\obs-studio\plugins\obs-soundgrid-input` y registra su desinstalador. **No está firmado digitalmente**: comprueba el SHA-256 publicado en cada versión.
