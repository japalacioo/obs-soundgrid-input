# Cabeceras ASIO (de OBS Studio, GPLv3)

`asio-compat.h` e `iasiodrv.h` se copiaron sin cambios de
https://github.com/obsproject/obs-studio/tree/master/plugins/win-asio (commit `e8a9a13bfc9da18391e873efc71522dbdc47be06`, 16 nov 2026).

- Autor: pkv <pkv@obsproject.com>, 2022-2026.
- Licencia: GNU GPL v3 o posterior (ver la cabecera de cada archivo). Según sus autores, se derivan
  del Steinberg ASIO SDK 2.3.4, publicado con licencia dual (propietaria / GPLv3).
- Consecuencia: el binario Windows de OBS SoundGrid Input que las incluye se distribuye bajo **GPLv3**
  (compatible con OBS, que es GPLv2 o posterior). Ver `docs/LICENSING.md`.

"ASIO" es marca registrada de Steinberg Media Technologies GmbH; su uso está sujeto a sus reglas de marca.
