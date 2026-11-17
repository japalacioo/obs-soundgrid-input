# Perfiles de ejemplo

Los números de entrada son entradas del **driver SoundGrid** del PC. Lo que llega a cada una depende de lo que el operador parchee en la LV1 (Patch > Output); el plugin no lo puede verificar ni cambiar. Anota tu correspondencia en la tabla de la sesión.

Los perfiles de alertas (Ensayo, Transmisión y Grabación) son independientes de estos perfiles de canales.

## 1. Main L/R
| Fuente OBS | Modo | Entradas | Alertas sugeridas |
|---|---|---|---|
| `LV1 Main L/R` | Estéreo | L = 1, R = 2 | Perfil Transmisión; **armar A01** solo si el programa debe sonar siempre |

Patch en la LV1: Main L → entrada 1 del driver; Main R → entrada 2.

## 2. Bus de streaming
| Fuente OBS | Modo | Entradas | Notas |
|---|---|---|---|
| `LV1 Bus Stream` | Estéreo | L = 3, R = 4 | Mezcla dedicada al streaming (independiente del FOH) |
| `LV1 Main (respaldo)` | Estéreo | L = 1, R = 2 | Desactivada en el mezclador o en otra pista; sirve de respaldo |

Ambas fuentes comparten un único stream del driver: no hay doble apertura.

## 3. 16 stems
OBS admite 8 canales por fuente y mezcla las disposiciones envolventes a estéreo, así que los stems se hacen con **8 fuentes estéreo** sobre el mismo dispositivo. Asigna a cada una sus pistas de grabación en Mezclador > Propiedades avanzadas de audio (OBS tiene 6 pistas de grabación; para 8 stems estéreo en archivos separados usa la grabación de origen o varias salidas).

| Fuente | Entradas (L/R) | Contenido típico |
|---|---|---|
| `Stem 01-02 Voces` | 1/2 | Bus de voces |
| `Stem 03-04 Coros` | 3/4 | Bus de coros |
| `Stem 05-06 Batería` | 5/6 | Bus de batería |
| `Stem 07-08 Bajo` | 7/8 | Bajo (mono duplicado) |
| `Stem 09-10 Guitarras` | 9/10 | Bus de guitarras |
| `Stem 11-12 Teclados` | 11/12 | Bus de teclados |
| `Stem 13-14 FX` | 13/14 | Retornos de efectos |
| `Stem 15-16 Ambiente` | 15/16 | Micrófonos de ambiente |

Para crearlas rápido: crea la primera fuente, ajústala y duplícala (copiar y pegar en OBS). Cambia solo las entradas L/R: el driver no se reabre (T52).

Para un stem mono, usa el modo **Mono** con una entrada. Si eliges varias entradas en Mono, se promedian, nunca se suman con clipping.
