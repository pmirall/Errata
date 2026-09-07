# Pebblebol — identidad visual del bicho: 5 propuestas

**Estado:** exploración, ninguna decidida. Alimenta D-nueva ("lenguaje de diseño de criaturas").
**Idioma:** este documento va en castellano a propósito. Define tono, nombres y textos de
sabor que acaban literalmente en `core/strings_es.h`; traducirlo perdería el punto.

---

## 0. De dónde parte esto

El spec (§19, §53) pide 60 criaturas en ~20 familias de 3 estadios, con concepto
informático fuerte, silueta simple, paleta limitada, cada vez más destructivas — y
"lo bastante monas como para que te importen".

Lo que hay hoy en `data/sprites.h` no cumple nada de eso: es el set heredado de
Nottamagochi (`baby_gato`, `baby_seta`, `senior_buho`, `senior_punki`, `senior_moho`).
Animalito genérico, cero concepto de red. Y la Appendix A del spec propone un roster
puramente conceptual (`PING → FLOOD → DDOS`, `CACHE → MEMORY → LEAK`) sin decir en
ningún momento **cómo se dibuja** un PING.

Este documento propone cinco maneras de dibujarlo. No son cinco sabores de lo mismo:
cada una parte de un eje distinto (materia, lenguaje, interfaz, señal, biología) y
cada una es discutible por motivos propios.

### Restricciones que ninguna propuesta puede ignorar

| Restricción | Consecuencia de diseño |
|---|---|
| 128×64, **1 bit**, sin grises | La silueta es el 90% de la identidad. El tramado es la única "textura". |
| 24×24 base / 32×32 final | Cabe una silueta y un patrón interior. No caben dos ideas. |
| Pantalla apagada casi siempre (§7 HW) | El bicho se ve en ráfagas de segundos. Tiene que leerse **instantáneamente**. |
| Piezo de 1 canal | El sonido es un rasgo de marca tanto como el sprite. Un timbre por familia. |
| Genoma existente | `gene_pattern` 0..15, `gene_body_size` 0..7, `gene_ear_horn` 0..3 ya están ahí y quieren significar algo visual. |
| 2 botones | Nada de interacción compleja: la personalidad se transmite por animación pasiva. |

---

## 1. GUIJARRO — "una piedra con algo escrito dentro"

> El producto se llama **Pebble**bol. *Pebble* es guijarro. El nombre ya lleva la
> respuesta dentro y nadie la ha usado todavía.

**La locura:** el bicho **no se mueve**. Casi nunca. Es una piedra. En un género que
vive de animar monigotes, la mascota de Pebblebol se queda quieta y respira una vez
cada tres segundos. Toda la expresividad está en la **superficie**, no en el cuerpo.

**Regla de anatomía (genera las 60):**
- Silueta cerrada, convexa, rellena. Sin extremidades, sin cuello, sin cintura.
- La identidad de especie vive **entera** en el grabado interior (líneas en negativo
  sobre el relleno) y en 1–3 muescas del contorno.
- Prohibido: ojos con brillo especular, boca, patas, orejas, cualquier cosa que
  sugiera vertebrado.

**Evolución = geología, no musculatura:**
```
canto rodado liso  →  piedra agrietada, algo asoma por la grieta  →  geoda / cristal / monolito
```
El estadio final ya no es una piedra: es lo que salió de la piedra. "Cada vez más
destructivo" se lee como **cada vez menos contenido**.

**Cómo se comporta a 1 bit:** ideal. Silueta rellena sólida + grabado en negativo es
el contraste más fuerte que da un OLED mono. El tramado tipo granito aguanta a 32×32.
Y a 24×24 una piedra sigue siendo legible, cosa que un gato no.

**Encaje con el genoma:** directo, sin inventar nada. `gene_pattern` = el grabado
(16 grabados). `gene_body_size` = el canto. `gene_ear_horn` se reinterpreta como
muesca/protrusión del contorno. El genoma deja de ser estadísticas invisibles y pasa a
ser lo que **ves**.

**Sonido:** golpe seco. Un `tock` de piedra contra piedra. El registro por defecto es
el silencio; el bicho suena poco y por eso cuando suena importa.

**Caja / UI:** la Caja de 10 es una colección de minerales, rejilla con etiquetas.
Tipografía de museo geológico, no de videojuego.

**Por qué no es ninguno de los tres:** Tamagotchi, Digimon y Pokémon son todos
**cuerpos con cara**. Este no tiene cara. No hay confusión posible ni de lejos.

**Riesgo real:** sin cara, el vínculo afectivo es más difícil de construir, y el spec
pide explícitamente "monas como para que te importen". La mitigación es que el afecto
no venga de la cara sino del **reconocimiento**: entre diez piedras identificas la
tuya por el grabado, como se reconoce un canto que llevas en el bolsillo. Es un afecto
distinto, más lento, y hay que aceptar que lo es.

---

## 2. GLIFO — "criaturas hechas de caracteres"

**La locura:** el bicho está compuesto de caracteres imprimibles y **podrías
teclearlo**. Cada Pebble tiene una "cadena verdadera" y el sprite es esa cadena
renderizada. Enseñarle tu bicho a alguien por WhatsApp es pegar una línea de texto.
El genoma ya se serializa a `hex32` (`genome_to_hex32`) — la coherencia es total: este
juego ya cree que las criaturas son cadenas, solo falta dibujarlas así.

**Regla de anatomía:**
- Cuerpo = 3 a 7 glifos de una fuente fija de 8×8, montados en rejilla. Nada fuera de
  rejilla.
- La cara, si la hay, son glifos también: `o`, `°`, `x`, `Ø`.
- Prohibido: curva libre, antialias conceptual, cualquier forma no colocable en celda.

**Evolución = la cadena crece y se corrompe:**
```
o-o        →     <[o-o]>       →     ▚<[Ø≠Ø]>▚
tecleable        tecleable            YA NO tecleable
```
El salto al estadio 3 introduce caracteres de bloque que no están en ningún teclado.
Eso **es** la señal de que la criatura se salió del sistema. La destructividad se lee
como pérdida de imprimibilidad.

**A 1 bit:** nativo. Es texto. No hay forma de que se vea mal.

**Sonido:** teclado mecánico, tono de módem, el `BEL` de terminal. Cada familia tiene
su carácter-ancla y su bip.

**Caja / UI:** el juego entero usa la misma fuente que las criaturas. El bicho y el
menú están hechos de la misma materia — no hay frontera entre criatura e interfaz, y
eso es una declaración estética, no una limitación.

**Por qué no es ninguno de los tres:** es un emoticono que echó cuerpo. Ni Pokémon ni
Digimon ni Tamagotchi tienen nada tipográfico.

**Riesgo real:** frío, y la escala castiga — 60 especies de glifos empiezan a
parecerse entre sí mucho antes que 60 siluetas. Mitigación: reservar un glifo-ancla
exclusivo por familia y no reutilizarlo jamás; con 20 anclas hay margen.

---

## 3. VENTANA — "tu mascota es un elemento de interfaz"

**La locura:** el bicho es chrome de UI que cobró vida. Un cursor. Una barra de
progreso. Un checkbox. Un diálogo modal con barra de título. Y **se sale de su marco**:
es el único de los cinco donde la criatura y el menú están hechos del mismo material,
así que el bicho puede asomarse por el borde de una pantalla de menú, empujar la barra
de estado, o tapar el reloj. La cuarta pared del propio dispositivo es un juguete.

**Regla de anatomía:**
- Todo se construye con: marco de 1 px, esquinas rectas, relleno de tramado 50%,
  sombra dura de 1 px abajo-derecha.
- Prohibido: contorno orgánico, diagonal que no sea de 45°.

**Evolución = acumulación de chrome:**
```
cursor  →  ventana con barra de título y botón de cerrar  →  ventanas anidadas en recursión
```
El estadio final **ocupa la pantalla del juego y la tapa**. Destructividad literal: te
está quitando sitio a ti.

**A 1 bit:** perfecto, el chrome de UI monocromo es exactamente esto. Es el estilo que
menos trabajo de dibujo requiere y el que mejor escala.

**Sonido:** bips de sistema. Errores de Windows 3.1 en piezo. Tono de "acción no
permitida" cuando el bicho está de mal humor.

**Por qué no es ninguno de los tres:** ninguno tiene mascotas hechas de interfaz.
Digimon es lo más cercano en ficción ("criaturas de datos") pero su ejecución visual
es monstruo anime; esto es lo contrario.

**Riesgo real:** la ironía crea distancia. Es difícil querer a un rectángulo, y el
tono meta puede leerse como listillo. Mitigación: reglas de microanimación muy
blandas — el cursor "respira", la ventana se inclina 1 px al mirarla, el checkbox se
marca solo cuando está contento. La ternura tiene que venir del movimiento, porque de
la forma no va a venir.

---

## 4. TRAZO — "la criatura no es un sprite, es una función"

**La locura:** no hay arte en flash. El bicho se dibuja en tiempo real como una línea
continua, y su **estado de ánimo es su forma de onda**: contento = seno limpio;
enfermo = ruido; dormido = línea plana con un pico cada cuatro segundos; hambriento =
amplitud decayendo. No hay sprite de "triste": la tristeza es un parámetro.

**Regla de anatomía:**
- Un trazo continuo de 1 px que nunca se levanta del papel.
- La "cara" son dos huecos en el trazo, nada más.
- Prohibido: relleno, silueta cerrada, cualquier píxel que no pertenezca a la línea.

**Evolución = la señal se vuelve materia:**
```
onda simple  →  armónicos, la línea se anuda sobre sí misma  →  tan enredada que es una masa sólida
```

**Dos ventajas que ninguna otra propuesta tiene:**
1. **Cero bytes de sprite.** 60 especies son 60 juegos de parámetros, no 60 mapas de
   bits. En un ESP32-C3 con el presupuesto de flash de este proyecto, eso no es
   detalle: es holgura para todo lo demás.
2. **Imagen y sonido son el mismo dato.** El piezo toca literalmente la onda que ves
   en pantalla. Ninguna de las otras cuatro puede decir eso.

Y encaja con la ficción WiFi sin metáfora forzada: el bicho vive en la señal, así que
el bicho **es** una señal.

**Por qué no es ninguno de los tres:** ninguno tiene criaturas que sean una línea.

**Riesgo real:** el más serio de los cinco. Hacer 60 ondas distinguibles entre sí es
mucho más difícil que 60 siluetas, y "cuidar de una línea" pide un salto afectivo
grande. Mitigación: apoyar la identidad en el ancla sonora (cada familia tiene su
timbre, y lo oyes antes de verlo) y aceptar que el roster sería más corto — 30
criaturas muy distintas antes que 60 que se confunden.

---

## 5. GABINETE — "no eres entrenador, eres naturalista"

**La locura:** el encuadre, no la criatura. El dispositivo es un **frasco de captura**.
La Caja de 10 es una vitrina de entomología con alfileres y etiquetas manuscritas. Los
bichos son artrópodos de verdad, dibujados como láminas de grabado del XIX — pero su
anatomía está hecha de piezas de red: las antenas son antenas, el abdomen es un
paquete con cabecera, las alas son frentes de onda.

**Regla de anatomía:**
- Simetría bilateral estricta. Tres segmentos (cabeza / tórax / abdomen). Patas
  articuladas con codo visible.
- Ojos compuestos, en rejilla. **Nunca** ojo de dibujo animado con brillo.
- Grabado de línea fina, tramado direccional para el volumen.

**Evolución = metamorfosis real, no power-up:**
```
larva  →  pupa  →  imago
```
Esto es honesto biológicamente y además da algo que ninguna otra propuesta da: la
**pupa es una fase jugable rara** — inmóvil, vulnerable, tarda, no puedes hacer nada
salvo protegerla. Un hueco deliberado en el bucle de juego, que es justo lo que un
juego de sesiones cortas puede permitirse.

La destructividad se lee como paso de individuo a **plaga**: el sprite final no es un
bicho más grande, son muchos bichos en el mismo encuadre.

**Tono:** ficha de campo con nombre binomial. *Ping vulgaris* → *Ping cataracta* →
*Ping tempestas*. Los textos de sabor son notas de naturalista, no descripciones de
Pokédex: observadas, fechadas, con dudas del observador.

**A 1 bit:** el grabado de línea **es** literalmente arte de 1 bit. Las láminas
antiguas ya estaban tramadas en blanco y negro puro. Es el estilo con mejor techo
visual de los cinco.

**Por qué no es ninguno de los tres — y aquí hay que tener cuidado:** es el más
cercano a Pokémon, porque Pokémon ya tiene tipo bicho. La distancia **no la marca la
criatura, la marca el encuadre de museo**: no las entrenas para pelear, las
catalogas. Si en algún momento se afloja el encuadre, esta propuesta colapsa hacia
Pokémon sin avisar.

**Riesgo real:** el ya dicho, más uno de producción: el grabado fino es el estilo más
caro de dibujar bien y el que peor perdona un sprite mediocre. 60 láminas a este nivel
es mucho trabajo de arte.

---

## 6. Comparativa

| | GUIJARRO | GLIFO | VENTANA | TRAZO | GABINETE |
|---|---|---|---|---|---|
| Eje | materia | lenguaje | interfaz | señal | biología |
| Distancia a los 3 grandes | máxima | máxima | alta | máxima | **la menor** |
| Coste de arte | bajo | muy bajo | muy bajo | **nulo** | **alto** |
| Legibilidad a 24×24 | excelente | excelente | excelente | media | media |
| Facilidad de querer al bicho | media | baja | baja | **baja** | alta |
| Escala a 60 especies | buena | media | buena | **mala** | excelente |
| Encaje con el genoma actual | **directo** | bueno | flojo | bueno | bueno |
| Encaje con el nombre del producto | **literal** | — | — | — | — |
| Sonido de marca | tock | teclas | bips | **es la imagen** | estridulación |

---

## 7. Recomendación

**GUIJARRO como columna vertebral**, por tres razones que no son de gusto:

1. El nombre del producto ya lo dice. *Pebble* = guijarro. Una marca cuyo nombre y
   cuya criatura son la misma palabra no necesita explicarse.
2. Es la única cuyo lenguaje visual **cae directamente** sobre los genes que ya
   existen (`pattern` → grabado, `body_size` → canto, `ear_horn` → muesca). Convierte
   sistemática ya implementada en algo que se ve.
3. La contradicción central es buena y es gratis: una piedra es inerte, antigua,
   lenta; un bicho de red es efímero, sintético, rápido. Que el bicho de la red tenga
   cuerpo de canto rodado es la idea rara que hace que la marca se sostenga sola.

**Con un injerto concreto de GABINETE:** el tono de los textos. Notas de campo con
nombre binomial, observador que duda. Eso arregla el punto débil de GUIJARRO — sin
cara, el afecto tiene que venir por el lenguaje.

**Y con TRAZO reservado para la corrupción (§55):** cuando un Pebble se corrompe, el
grabado de la piedra se desestabiliza y se vuelve trazo vivo durante unos segundos.
Dos lenguajes visuales, uno para el estado sano y otro para el enfermo, con una
transición que se entiende sin texto.

**La apuesta arriesgada, si se quiere:** VENTANA. Techo creativo más alto de los cinco
por lo de romper la cuarta pared del dispositivo, y el más barato de producir. Pero
exige acertar con un tono cómico muy fino, y si falla queda frío en vez de gracioso.
No es una decisión de arte: es una decisión de si el proyecto quiere ser tierno o
quiere ser ingenioso.

---

## 8. Consecuencia que hay que asumir, se elija lo que se elija

Las cinco propuestas implican **tirar el set de sprites actual entero**. `baby_gato`,
`baby_seta`, `senior_buho`, `senior_punki`, `senior_moho` y compañía no sobreviven a
ninguna de ellas, porque son exactamente el "animal de fantasía genérico" que §53
prohíbe. `SPRITE_REV` sube, `sprite_src.py` se reescribe, y P4-C1 (`gen_content.py`)
genera el roster contra el lenguaje elegido, no contra el heredado.

Es la decisión más cara del documento y conviene tomarla antes de P4-C1, no después.
