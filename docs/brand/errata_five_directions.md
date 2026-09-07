# ERRATA — cinco direcciones

Desarrollo de la propuesta ERRATA de `bicho_identity_proposals.md`. La premisa
compartida no cambia:

> Un bicho es lo que un sistema dibuja **cuando no sabe qué dibujar**. El cuerpo es una
> región alineada a ejes a la que le pasó algo. Los ojos son ausencias, no dibujos. Un
> Pebble sano es un error **estable**; corromperse (§55) es que la forma **deja de
> sostenerse**.

Lo que cambia en cada dirección es el eje sobre el que se construyen las 20 familias, y
cada una arrastra su propio tono, su propia mecánica y su propio riesgo.

Los grids ASCII son **esquemas**, no arte final: `#` es píxel encendido, `.` apagado.
Sirven para ver la silueta a la escala real (24×24 base).

| | eje | pregunta que responde |
|---|---|---|
| **A. LA CAPA** | sistemático | *¿qué parte del sistema falló?* |
| **B. CON CARA** | emocional | *¿qué siente un error?* |
| **C. EL TESTIGO** | narrativo | *¿de qué desastre quedó esto?* |
| **D. RESIDENTE** | espacial | *¿dónde vive el error?* |
| **E. LA MÁQUINA** | estructural | *¿de qué está hecho un error?* |

---

## A. LA CAPA — "cada familia es un estrato de la pila"

**La locura:** es la más ordenada de las cinco y por eso la más rara. Las 20 familias
**no** son 20 criaturas inventadas: son los 20 sitios donde un sistema se puede romper,
ordenados de abajo arriba. Un bug de códec y un bug de fuente no se parecen en nada
porque **las capas no fallan igual**, y esa diferencia real hace el trabajo de diseño
por ti.

**El regalo que trae:** resuelve §12 sin inventar nada. El triángulo de tipos sale de la
profundidad de la capa.

```
capas bajas  (bit, línea, paquete, CRC)      → SIGNAL
capas medias (buffer, memoria, códec, fuente) → CORRUPT
capas altas  (widget, archivo, reloj, proceso)→ SYSTEM
```

**Taxonomía de partida** (14 de las 20, ancladas al roster de la Appendix A):

| capa | vocabulario visual | familia |
|---|---|---|
| bit | píxel invertido suelto, parpadeo | `PIXEL → ARTIFACT → CORRUPTION` |
| línea | ruido de sal y pimienta, colisión | `SIGNAL → NOISE → JAMMER` |
| paquete | bloque partido, trozos desalineados | `PACKET → FRAGMENT → SWARM` |
| CRC | contorno que no cierra por un píxel | `HASH → COLLISION → ENTROPY` |
| búfer | relleno que se sale por un lado | `BYTE → CORRUPT → OVERFLOW` |
| memoria | huecos, regiones que se repiten | `CACHE → MEMORY → LEAK` |
| códec | macrobloques, arrastre horizontal | `FILE → ARCHIVE → DATAHOARD` |
| textura | damero de textura ausente | `NULL → VOID → NULLPOINT` |
| fuente | caja de glifo ausente (tofu), mojibake | `SCRIPT → MACRO → PAYLOAD` |
| widget | chrome mal dibujado, marco doble | `PROXY → GATEWAY → FIREWALL` |
| archivo | truncado, mitad inferior en blanco | `LINK → DEADLINK → VOID` |
| reloj | se dibuja dos veces con retardo | `PING → FLOOD → DDOS` |
| proceso | zombi: sigue ahí pero no responde | `DAEMON → PROCESS → KERNEL` |
| protocolo | apretón de manos a medias | `PORT → OPENPORT → BACKDOOR` |

**Ejemplo, familia fuente, estadio 1** — la caja de glifo ausente. Los dos bloques de
arriba son los ojos (píxeles muertos); los de abajo son los dígitos hex del tofu:

```
########################
#......................#
#...####......####.....#
#...####......####.....#
#...####......####.....#
#......................#
#......................#
#......................#
#....##..........##....#
#...#..#........#..#...#
#...#..#........#..#...#
#....##..........##....#
#......................#
########################
```

**Evolución = el fallo baja de capa.** Y esto es lo bueno: un bug empieza fallando en
una capa alta (cosmético, se ve feo) y **desciende** hacia capas más bajas, donde el
daño es real e irreparable.

```
falla el widget  →  falla el búfer  →  falla el bit
(feo)               (roto)             (irrecuperable)
```

**Corrupción §55:** el bicho cae una capa temporalmente. Ves exactamente cuánto ha
bajado por cómo cambia su vocabulario, y sabes que volverá a subir. Es un termómetro
visual sin barra de vida.

**Redes:** la categoría de red sesga qué capas aparecen. HOME da bugs de capa alta
(cosméticos, domésticos). BUSINESS da capa media. **HIDDEN da capa física** — los bugs
más profundos y más raros salen de las redes que no anuncian su nombre. Eso convierte
`category_mask` en una curva de dificultad geográfica sin escribir una línea de balance.

**Sonido:** el timbre indica la capa. Capa alta = bips limpios y cortos. Capa baja =
zumbido sucio, casi DC. Oyes la profundidad antes de mirar.

**Coste:** bajo. Cada familia es una regla de dibujo, no un personaje. Encaja con el
pipeline XBM estático tal cual está.

**Riesgo:** es la más fría de las cinco. Sistemática impecable, cero ternura. Necesita
que otra dirección le preste el afecto (B es la candidata natural).

---

## B. CON CARA — "el error que aprendió a sonreír"

**La locura:** el bicho **está intentando ser una mascota y no le sale**. Se ha dibujado
una cara a sí mismo, con sus propios píxeles, y la ha dibujado mal: torcida,
descentrada, con un ojo más alto que el otro. La cara **no forma parte de su geometría
real** — es una pegatina sobre una cosa rota, y se nota.

Es la dirección que resuelve el punto débil declarado de ERRATA en el documento
anterior ("cuesta encariñarse con una avería"), y lo resuelve por el sitio menos
esperado: la ternura no viene de que sea mono, viene de que **se esfuerza**.

**Regla de anatomía:**
- El **cuerpo** obedece a ERRATA sin concesiones: rectangular, alineado a ejes, roto.
- La **cara** es de otro material: trazo fino de 1 px, mal centrada, con temblor de un
  píxel entre frames. Nunca simétrica. Nunca alineada a la rejilla del cuerpo.
- La cara puede **salirse del cuerpo**. Es la señal más importante del juego.

```
.######################.
.#....................#.
.#..##............##..#.
.#..##.............#..#.
.#.....................#
.#..#..............#..#.
.#...##########....#..#.
.#.....................#
.####################..#
....#..............#..#.
.....##############...#.
......................#.
```
(el ojo derecho un píxel más alto, la sonrisa desplazada, la esquina inferior sin
cerrar: el cuerpo está roto y la cara disimula)

**Evolución = la cara se despega.**

```
cara torcida pero pegada  →  cara desplazada, se ve el cuerpo debajo  →  la cara flota al lado del cuerpo
```

En el estadio 3 la cara ya no está encima del bicho: va **al lado**, arrastrada, como
un globo atado. El bicho ha dejado de fingir. Y esa es la lectura de "cada vez más
destructivo" que ninguna otra dirección da: no se vuelve más grande ni más agresivo,
**deja de intentar caerte bien**.

**Corrupción §55:** la cara se desprende y tarda unos segundos en volver a su sitio.
Verla buscar su propia cara es la escena más rara y más tierna que puede dar este
proyecto, y cuesta cuatro frames.

**Cuidado:** el estado de ánimo se lee en el **desajuste**, no en la expresión. Contento
= la cara casi encaja. Triste = la cara se ha resbalado hacia abajo. Enfermo = la cara
está pero el cuerpo debajo ha perdido una fila. Enfadado = la cara encaja
perfectamente, por una vez, y eso da más miedo que si estuviera torcida.

**Tono de textos:** el bicho habla en primera persona y con exceso de confianza sobre
lo que es. "Estoy bien." "Esto es normal." "Siempre he tenido esta forma."

**Sonido:** un tono limpio y bonito que se desafina medio semitono al final. Cada vez.

**Coste:** bajo — la cara es una capa separada de 1 px sobre el sprite, con un offset
que ya podría venir del estado del pet. Casi todo el gasto es de animación, no de arte.

**Riesgo:** el humor de "cosa rota que finge estar bien" puede volverse **triste** si se
insiste, y el spec pide juguete, no drama. Hay que decidir dónde está el tope y no
pasarlo: el bicho nunca se da cuenta de que está roto. Si se da cuenta, el juguete se
convierte en otra cosa.

---

## C. EL TESTIGO — "el bicho es lo que quedó cuando algo se cayó"

**La locura:** un bug no es una criatura, es el **residuo de un incidente concreto**.
Cada Pebble arrastra el desastre del que salió, y el desastre es siempre ridículamente
pequeño. Coleccionas supervivientes de catástrofes minúsculas.

**Vocabulario:** el del informe de fallo. La traza de pila. El volcado de memoria. La
miniatura de "última versión conocida". El 404. La imagen rota del navegador. El bicho
lleva encima **el trozo de aquello que se rompió**.

```
########################
#......................#
#....##############....#
#....#............#....#
#....#....####....#....#
#....#............#....#
#....##############....#
#......................#
####################....
....####################
#......................#
#..####..####..##..###.#
#..####..####..##..###.#
########################
```
(marco de imagen rota arriba, la fila desplazada donde se partió, y abajo el volcado
hexadecimal de lo que sea que estaba en memoria en ese momento)

**El sistema que desbloquea:** §20 ya alimenta el spawn con categoría de red, potencia,
franja horaria y día. Aquí eso **deja de ser semilla y pasa a ser biografía**. Cada
Pebble guarda su incidente de origen y su ficha lo cuenta como un post-mortem de una
línea:

> *Recogido el 3 de marzo, 18:40, en una red doméstica. Alguien intentó imprimir un PDF
> de cero páginas. Esto es lo que quedó.*

> *Red oculta, 04:12. No consta qué falló. No consta quién estaba.*

**Evolución = el incidente crece.**

```
un fallo que nadie notó  →  un fallo que alguien tuvo que arreglar  →  un fallo del que se habló
```

El estadio 3 es un bicho cuyo incidente **salió en las noticias**. La destructividad no
está en el bicho, está en la escala del desastre del que viene.

**Corrupción §55:** el bicho **vuelve al momento del incidente** durante unos segundos.
Se ve el fallo original repitiéndose. Un flashback.

**Tono:** documental y seco. Notas de incidencia, fechadas, con lo que no se sabe
marcado como no sabido. El humor sale de la desproporción entre el tono forense y la
insignificancia del suceso, y no hay que subrayarlo nunca.

**Sonido:** el bip de un sistema de aviso. Siempre el mismo, y por eso reconocible.

**Coste:** bajo de arte, **medio de datos**. Requiere guardar el incidente en la
instancia — unos pocos bytes por Pebble (categoría, marca de tiempo, índice de
plantilla) — y un banco de plantillas de texto en `strings_es.h`. Hay que mirar el
presupuesto de `PebbleInstance` antes de prometerlo.

**Riesgo:** es tono, no forma. Si el arte no aguanta solo, esta dirección se queda en
un buen sistema de textos de sabor encima de otra dirección. Lo cual, honestamente,
podría ser exactamente su sitio.

---

## D. RESIDENTE — "vive en tu pantalla, no en la ficción"

**La locura:** el bicho **no tiene pantalla propia**. No hay un HOME donde esperarte
quieto. Vive en la interfaz real del aparato: se sienta en la barra de estado, se come
una entrada del menú, se esconde detrás del icono de batería, aparece en la esquina de
AJUSTES. Encontrarlo es parte del juego, todos los días.

Es la versión máxima de "esto no debería estar aquí", y la única de las cinco donde la
frase se aplica **al propio aparato** y no a una ficción interna.

```
+----------------------+
| CUIDAR               |
| JUGAR                |
| CA##  ###            |
| RE#### ##            |
| AJUSTES              |
+----------------------+
```
(se ha comido dos letras de CAJA y RED, y el trozo que falta está en su cuerpo)

**Regla:** el bicho se dibuja **siempre** con el material del chrome que está ocupando.
Si está sobre el menú, tiene la tipografía del menú. Si está en la barra de estado, tiene
la altura de la barra. **No trae su propio estilo: roba el del sitio donde está.**

**Evolución = escala lo que ocupa.**

```
un glifo del menú  →  una fila entera  →  la pantalla no es tuya
```

El estadio 3 hace que el chrome empiece a fallar de verdad: el marco parpadea, una
entrada del menú sale con el glifo equivocado, el reloj se atrasa un segundo. Sigue
funcionando todo, pero mal, y es culpa suya.

**Corrupción §55:** se muda. Aparece en una pantalla donde no había estado nunca.

**La regla de seguridad, y es innegociable:** §47 exige que los errores reales sean
legibles, y §63 que la UI sea fiable. Así que el bicho **jamás toca**: la pantalla de
ERROR, la de confirmación de acciones destructivas, el indicador de batería baja, ni
nada que el jugador necesite para no perder datos. Hay una lista blanca de sitios donde
puede estar, y la disciplina de mantenerla es la mitad del trabajo de esta dirección.

**Sonido:** el sonido del sitio que ocupa, un poco mal. El clic de menú un pelín tarde.

**Coste:** el más alto de las cinco, y no en arte sino en **arquitectura**. El bicho deja
de ser un sprite que pinta `pet_view` y pasa a ser una capa que se compone encima de
cualquier pantalla. Eso toca `render.cpp`, la tabla de pantallas de `screen_table.cpp` y
el enrutado de entrada. No es un cambio de arte, es un cambio de motor.

**Riesgo:** el mayor de las cinco, y ya identificado — **"se me ha estropeado el
aparato"**. Aquí no se puede mitigar con "el chrome se mantiene limpio", porque la
propuesta *es* ensuciar el chrome. La única defensa es la lista blanca y que el bicho
sea inconfundiblemente un bicho: tiene que mirarte. Un fallo no te mira.

---

## E. LA MÁQUINA — "un diagrama pasándolo mal"

**La locura:** el bicho es un **diagrama de estados**. Nodos, aristas, y una ficha que
recorre su propio diagrama a la vista. Está vivo porque la ficha se mueve. Está roto
porque la ficha se queda atascada en un nodo, o toma una arista que no existe, o de
pronto hay dos fichas donde debería haber una.

Su personalidad **es su topología**. Un bicho con un bucle apretado es nervioso. Uno con
una cadena larga y sin retorno es fatalista. Uno con un nodo aislado al que la ficha no
llega nunca tiene algo que no puede hacer.

```
........####............
......##....##..........
.....#........#.........
....##...@@...##........
.....#........#.........
......##....##..........
........####............
..........##............
..........##............
........######..........
.......##......##.......
......#..........#......
```
(nodo con la ficha `@@` dentro, una arista bajando a otro nodo; la forma completa es la
especie)

**Especie = forma del grafo.** Anillo, cadena, estrella, árbol, malla, bucle con salida
única, dos componentes que no se tocan. 20 topologías dan 20 familias sin dibujar 20
personajes, y son distinguibles de un vistazo porque las siluetas de grafo son muy
distintas entre sí.

**Estado de ánimo = dónde está la ficha y a qué velocidad va.** Cero sprites de humor.
Contento = recorre todo el grafo con ritmo. Aburrido = da vueltas al mismo bucle.
Enfermo = se queda parada. Dormido = se apoya en un nodo y no se mueve. Muerta de miedo
= vibra entre dos nodos sin decidirse.

**Evolución = el grafo se degrada.**

```
grafo limpio y conexo  →  aparecen aristas que no llevan a ningún sitio  →  ya no es un grafo: es una maraña con dos fichas
```

Dos fichas en el estadio 3 es la mejor imagen de "más destructivo" que se puede hacer
con tan pocos píxeles: **el bicho ya no tiene un solo estado**, y eso en un ordenador es
exactamente lo que significa estar roto.

**Corrupción §55:** la ficha salta a un nodo al que no llegaba ninguna arista.

**Coste:** casi nulo de arte, **medio de motor**. Es render procedural: guardas la
topología y la posición de la ficha, no mapas de bits. Elimina las 60 láminas del
presupuesto de arte, pero pide un renderizador que hoy no existe (el pipeline son frames
XBM estáticos).

**Riesgo:** es la más abstracta y por tanto la más difícil de querer — el problema que B
resuelve, ella lo empeora. Y a 24×24 un grafo de más de 6 nodos deja de ser legible, lo
cual limita cuánto puede crecer un estadio 3.

---

## Comparativa

| | A. LA CAPA | B. CON CARA | C. EL TESTIGO | D. RESIDENTE | E. LA MÁQUINA |
|---|---|---|---|---|---|
| genera las 20 familias | **sí, sola** | no | no | no | **sí, sola** |
| resuelve §12 tipos | **sí, sola** | no | no | no | no |
| resuelve el afecto (§53, §70) | no | **sí** | parcial | parcial | no |
| corrupción §55 | termómetro | se le cae la cara | flashback | se muda | salto imposible |
| usa `category_mask` | **a fondo** | no | **a fondo** | no | no |
| coste de arte | bajo | bajo | bajo | bajo | **nulo** |
| coste de motor | **nulo** | bajo | bajo (datos) | **alto** | medio |
| legibilidad a 24×24 | alta | alta | media | alta | **baja** |
| riesgo dominante | fría | melancolía | es solo tono | "está roto" | ilegible |

---

## Recomendación

No son cinco alternativas excluyentes: **tres de ellas ocupan huecos distintos y encajan
sin pelearse.**

**A pone el esqueleto.** Es la única que genera 20 familias por sí sola y la única que
resuelve el triángulo SIGNAL/CORRUPT/SYSTEM sin inventar nada. Y su idea de evolución —
el fallo **baja de capa**, de cosmético a irreparable — es mejor que "se hace grande".

**B pone la ternura.** El cuerpo obedece a A; la cara mal dibujada va encima. Arregla el
único defecto serio de ERRATA, cuesta cuatro frames, y da la mejor escena del proyecto:
un bicho buscando su propia cara. El tope está claro y hay que respetarlo: **el bicho
nunca se entera de que está roto.**

**C pone la voz.** Ficha de incidente en vez de descripción de criatura. Convierte los
datos de spawn que §20 ya calcula en biografía, y hace que dos Pebbles de la misma
especie no sean el mismo objeto. Antes de comprometerlo hay que mirar cuántos bytes
libres quedan en `PebbleInstance`.

**D se guarda para eventos raros, no para el estado normal.** La idea es demasiado buena
para tirarla y demasiado peligrosa para vivir en ella. Como comportamiento constante
rompe §47 y §63 y hace que el juguete parezca defectuoso. Como **suceso** — una vez cada
muchos días, un bicho concreto, en un sitio de la lista blanca, y se va solo — es la
mejor anécdota que puede contar un dueño de Pebblebol.

**E sobrevive como una familia, no como el idioma.** `DAEMON → PROCESS → KERNEL` **es**
literalmente una máquina de estados. Que esa familia, y solo esa, se dibuje como grafo
con ficha la convierte en la rareza del roster en vez de en un problema de legibilidad
generalizado. Se gana la mejor imagen del juego (dos fichas en el estadio final) sin
pagar su coste.

En una frase:

> **Un bicho es un fallo de una capa concreta (A), que se ha dibujado una cara torcida
> para caerte bien (B), y que arrastra el pequeño desastre del que salió (C). Muy de vez
> en cuando, uno se sale de su pantalla (D). Y hay una familia que no es un dibujo, es
> un diagrama (E).**

---

## Siguiente paso propuesto

Ninguna de las cinco está probada hasta que se vea a 24×24 en el panel real. Lo barato y
sensato es:

1. Elegir una familia (`SCRIPT → MACRO → PAYLOAD`, la de capa fuente, es la más rápida
   de dibujar).
2. Dibujar sus tres estadios con A+B aplicados, en `sprite_src.py`.
3. Mirarlos en el OLED antes de generar nada más.

Es un día de trabajo y decide el resto del roster.
