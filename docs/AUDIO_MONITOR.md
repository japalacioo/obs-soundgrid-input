# Monitor de audio en el Multiview

Para el operador de la consola que ve el Multiview de OBS en otra pantalla: muestra el audio de la escena que el director tiene al aire, en el recuadro inferior derecho del Multiview.

![Monitor de audio con tonos a −20 dBFS](img/monitor-audio-multiview.png)

## Qué muestra

| Elemento | Detalle |
|---|---|
| Etiqueta | Nombre de la fuente SoundGrid y de la escena al aire. `(+N)` si la escena tiene más fuentes SoundGrid visibles; `mono` si la fuente es mono (se ve igual en L y R). |
| Cabecera | Gris: hay audio. **Roja «SIN SEÑAL»**: no llega audio desde hace más de 0,5 s, la escena no tiene fuente SoundGrid o la fuente no existe. **Ámbar «SILENCIADA EN OBS»**: llega audio, pero la fuente está silenciada en el mezclador de OBS. |
| Medidores L y R | Como el medidor de volumen de OBS: verde hasta −20 dBFS, amarillo hasta −9 dBFS y rojo por encima. Escala de 0 a −60 dBFS. La barra cae 11,76 dB/s; la marca negra es el RMS. |
| Peak hold | Línea sobre cada barra y valor en dBFS encima. Se mantiene 20 s o hasta un pico mayor. |
| Osciloscopio | L arriba y R abajo. Lo más reciente está a la derecha y la onda avanza hacia la izquierda en tiempo real. Por defecto muestra 5 s (de 1 a 30), con una línea vertical por segundo. Las líneas horizontales marcan ±50 % (−6 dBFS). Si deja de llegar audio, la parte derecha queda vacía. |
| Peak / RMS | Sobre cada canal del osciloscopio: pico y RMS de los últimos 300 ms en dBFS. Se actualizan 5 veces por segundo. |
| Clip | El recuadro **CLIP** y un marco rojo en el medidor se encienden cuando una muestra llega a −0,1 dBFS (el mismo umbral que la alerta A09). Siguen encendidos 5 s. La onda saturada se dibuja en rojo. |

El audio es el que el plugin entrega a OBS, antes de los filtros y del volumen del mezclador de OBS, es decir, lo que manda la consola.

## Configuración

No hace falta crear ninguna escena. **Al abrir OBS** (y al cambiar de colección de escenas), el plugin crea la escena «Monitor de audio SoundGrid» si falta, con el monitor dentro. Basta con abrir el Multiview (**Vista > Abrir vista múltiple**). El log de OBS indica en qué recuadro queda.

1. Opcional: para que quede en el último recuadro, arrastra la escena a la posición que indica **Herramientas > SoundGrid: monitor de audio en el Multiview** (o el botón **Colocar en el Multiview…** de la fuente), según el diseño de Multiview (Ajustes > General > Multiview) y las escenas ocultas. Con el diseño por defecto, «Horizontal, arriba (8 escenas)», es la 8.ª escena visible.
2. En las propiedades de la fuente «Monitor de audio SoundGrid (Multiview)» puedes elegir:
   - **Mostrar la fuente SoundGrid de:** la escena de programa (por defecto), la vista previa (modo estudio) o una fuente fija.
   - **Segundos de forma de onda.**
   - **Crear esta escena al abrir OBS (en todo OBS)**, activado por defecto. Si lo desactivas y borras la escena, no se vuelve a crear.

En cada escena se usa la fuente SoundGrid visible que esté más arriba en la lista de fuentes, incluidas las que están dentro de grupos o de escenas anidadas.

## Avisos

- La escena del monitor aparece en la lista de escenas del director. Si alguien la pulsa (también desde el Multiview), el plugin vuelve inmediatamente a la escena anterior en programa y en vista previa, y lo registra en el log. Para evitar incluso ese cambio breve, desactiva en el ordenador del Multiview la opción de cambiar de escena con un clic (Ajustes > General > Multiview).
- Esa protección solo se aplica a la escena que creó el plugin (lleva una marca interna). Si añades la fuente del monitor a una escena tuya, esa escena puede salir al aire con normalidad.
- La escena que antes ocupaba el último recuadro deja de verse en el Multiview.
- Si hay menos escenas que recuadros, el monitor queda en el primer recuadro libre, no en la esquina. Elige un diseño de Multiview con menos recuadros.
- En los diseños «Horizontal, abajo» y «Vertical, derecha», el último recuadro de escena no es la esquina inferior derecha.

Decisión de diseño: [ADR-016](ADR/ADR-016-monitor-multiview.md).
