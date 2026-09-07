# Pebblebol — identidad visual del bicho: 5 propuestas

**Estado:** exploración, ninguna decidida.
**Idioma:** castellano a propósito — define tono, nombres y textos de sabor que acaban
literalmente en `core/strings_es.h`.

---

## 0. La ficción manda, y dice esto

Del §0 del spec:

> Un Pebble es una pequeña criatura digital — un "bicho informático" — **atrapada en
> redes inalámbricas**.

> "Hay bichos escondidos en las redes que me rodean. Me llevo el mío conmigo, los
> descubro, los colecciono y lo hago más fuerte."

Y del §70, la frase con la que se mide todo lo demás:

> **"¿Qué habrá encontrado mi bicho esta vez?"**

De ahí salen seis exigencias que el arte tiene que cumplir. No son gustos, son la
ficción:

| La ficción dice | El arte tiene que |
|---|---|
| **Bug = defecto.** No es fauna, es algo que *no debería estar ahí* | parecer una avería, no un animal de diseño |
| **Vive en redes ajenas** (§20: HOME/PUBLIC/BUSINESS/OPEN/HIDDEN) | tener ecología de infraestructura, no de bosque |
| **Las redes son invisibles** | hacer visible lo invisible; el aparato es una **lente**, no una jaula |
| **Son plagas**, pequeños y numerosos | admitir la idea de muchos, no solo de uno |
| **Cada vez más destructivos** (§19, §53) | escalar en *daño*, no en musculatura |
| **Corrupción** como mecánica insignia (§55) | tener el glitch en su vocabulario nativo, no pegado encima |

Y una nota importante sobre la que no hay que engañarse: **ninguno de los tres grandes
tiene criaturas que sean averías.** Pokémon es fauna. Digimon son habitantes de un
mundo. Tamagotchi es una mascota. Un Pebble es un **fallo con patas**. Esa es la
distancia gratis que tiene este proyecto y la que hay que gastar.

### Lo que hay hoy y no sirve

`data/sprites.h` sigue siendo el set heredado de Nottamagochi: `baby_gato`,
`baby_seta`, `baby_pajaro`, `senior_buho`, `senior_punki`, `senior_moho`. Es
exactamente el "animal de fantasía genérico" que §53 prohíbe, y no conecta con el
roster de la Appendix A (`PING → FLOOD → DDOS`, `CACHE → MEMORY → LEAK`). El spec dice
**qué** criaturas hay y nunca dice **cómo se dibuja un PING**. Este documento propone
cinco maneras.

### Restricciones de hardware que ninguna puede ignorar

128×64 **1 bit** sin grises · sprites 24×24 (base) y 32×32 (final) · pantalla apagada
casi siempre, se ve en ráfagas de segundos · piezo de un canal · dos botones ·
`gene_pattern` 0..15, `gene_body_size` 0..7, `gene_ear_horn` 0..3 ya existen y quieren
significar algo visible · `SpeciesDef.category_mask` ya reserva el bioma y hoy no
significa nada narrativamente.

---

## 1. ERRATA — "un bicho es un error que cogió cuerpo"

**La locura:** el bicho está hecho de lo que un sistema dibuja **cuando no sabe qué
dibujar**. El cuadrito de glifo ausente (▯). El damero de textura que falta. La línea
de barrido rota. El píxel muerto. El bloque de artefacto de compresión. El "?" de
marcador de posición. Tu mascota es un fallo de renderizado con carácter.

Es la idea más afilada disponible porque es literalmente la definición de bug, y
porque ninguno de los tres grandes puede tocarla sin dejar de ser lo que es.

**Regla de anatomía:**
- El cuerpo es una región **alineada a los ejes** que salió mal. Nunca una forma
  orgánica: una forma *rectangular a la que le pasó algo*.
- La familia es el **modo de fallo**: familia desgarro, familia damero, familia píxel
  muerto, familia relleno sin terminar, familia desplazamiento de fila.
- Los ojos son dos píxeles muertos. No ojos dibujados: **ausencias**.
- Prohibido: cualquier curva que no sea un artefacto, cualquier cosa que parezca
  *diseñada*.

**Evolución = el error se propaga:**
```
un píxel muerto  →  una región corrupta con forma  →  el error ES la pantalla
```
El estadio 3 **no cabe en su caja**: sangra fuera del bounding box y se mete en el
chrome de la UI. El "cada vez más destructivo" de §19 hecho literal — y sale gratis de
implementar, el sprite simplemente dibuja fuera de 32×32.

**Corrupción (§55) nativa.** Este es el argumento fuerte: en las otras cuatro
propuestas la corrupción es un efecto que se aplica encima. Aquí un Pebble sano es un
error **estable** — mantiene su forma. Corromperse es que la forma **deja de
sostenerse**. Mismo vocabulario, distinta disciplina. Se entiende sin una sola palabra
de texto.

**A 1 bit:** nativo. Todos los artefactos de la lista nacieron en blanco y negro puro.

**Sonido:** el piezo hace de avería. Zumbido, recorte, un tono que se desliza medio
semitono. Cada familia tiene su intervalo roto.

**Redes:** una red es *dónde ocurrió el error*. Los bichos HOME son errores
domésticos; los BUSINESS, corporativos; los HIDDEN son errores **que nadie ha visto
nunca**. Buen gancho de sabor y encaja con `category_mask` sin forzar.

**Riesgos reales, dos:**
1. El usuario puede leer un sprite roto como *"se me ha estropeado el aparato"*.
   Mitigación: el chrome de la UI tiene que ser impecable y estable como una roca. Solo
   la criatura se porta mal. El contraste hace el trabajo.
2. Si todo es glitch, el glitch no significa nada, y §55 pierde el golpe. Mitigación:
   la de arriba — sano = error estable, enfermo = error que se suelta.

---

## 2. PARÁSITO — "una entomología de los aparatos"

**La locura:** el encuadre. No eres entrenador, eres **naturalista de control de
plagas del entorno construido**. El aparato es un frasco de captura, la Caja es una
vitrina con alfileres y etiquetas manuscritas, y las fichas son notas de campo
fechadas, con las dudas del observador. Nombre binomial: *Ping vulgaris* → *Ping
cataracta* → *Ping tempestas*.

**Lo que desbloquea:** `SpeciesDef.category_mask` deja de ser un campo muerto y pasa a
ser **bioma**. Cada familia está adaptada a un tipo de infraestructura:
- bichos de **router**: patas-antena, ocelos parpadeantes, cuerpo plano para colarse
  detrás de la caja;
- bichos de **impresora**: mandíbulas de alimentador, abdomen de tóner, siempre sucios;
- bichos de **cámara**: un solo ojo-lente enorme, ningún otro órgano sensorial, no
  duermen nunca;
- bichos de **terminal de negocio**: acorazados, segmentados, burocráticos, se mueven
  en cola;
- bichos de **SSID oculto**: **sin ojos**, pálidos, adaptados a la cueva. Nunca
  observados a la intemperie.

**Regla de anatomía:** bilateral estricta, tres segmentos, patas articuladas con codo
visible, ojos compuestos en rejilla. **Nunca** ojo de dibujo animado con brillo. El
insecto pone la silueta; el aparato infestado pone la variación.

**Evolución = metamorfosis de verdad:** larva → pupa → imago, y el imago es el estadio
que **daña al huésped**. La escalada no es potencia, es **carga de infestación**: el
estadio 3 es cuando el aparato deja de funcionar. Regalo de diseño: la **pupa es una
fase jugable rara** — inmóvil, vulnerable, tarda, no puedes hacer nada salvo
protegerla. Un hueco deliberado en el bucle, que es justo lo que un juego de sesiones
cortas puede permitirse.

**A 1 bit:** el grabado de línea **es** arte de 1 bit. Las láminas del XIX ya estaban
tramadas en blanco y negro puro. Techo visual más alto de las cinco.

**Sonido:** estridulación, el clic de las patas sobre plástico, el zumbido del aparato
en el que viven.

**Riesgos reales, dos:**
1. Es la más cara de dibujar y el estilo que peor perdona un sprite mediocre. 60
   láminas grabadas es mucho arte.
2. Es la más cercana a Pokémon (tipo bicho). **Lo único que marca la distancia es el
   encuadre**, no la criatura. Si el encuadre se afloja — si en algún momento las
   entrenas en vez de catalogarlas — colapsa hacia Pokémon sin avisar.

---

## 3. ECO — "solo lo ves mientras hay señal"

**La locura:** el bicho **nunca se dibuja entero**. El aparato es un detector y el
sprite es una reconstrucción a partir de datos flojos. Contorno discontinuo, a trazos;
cuánto resuelve depende del RSSI y de cuánto tiempo llevas cerca de esa red.

Esto convierte el aparato de Pokéball en **lente**, que es lo que la ficción pide de
verdad: las redes son invisibles y el juego va de hacerlas visibles. Y asciende un
dato que §20 ya lista como entrada de spawn (potencia de señal) a rasgo estético
central.

**Regla de anatomía:**
- Solo contornos, sin relleno. Trazo discontinuo de 1 px.
- Los **ojos son lo único que siempre se dibuja entero**, y resuelven primero. Siempre
  ves que te está mirando antes de ver qué es.
- Prohibido: superficie sólida en cualquier estadio que no sea el final.

**Evolución = volverse presente:**
```
tres fragmentos  →  contorno cerrado pero hueco  →  sólido, opaco, ya no se puede des-ver
```
El estadio final es la única criatura del juego rellena de negro. Dejó de ser señal y
se volvió cosa.

**Sonido:** el piezo como detector de metales / contador Geiger. La proximidad se
**oye**. Es la única de las cinco donde el sonido es instrumento de juego y no
decoración, y encaja con §21 (que prohíbe hacer esperar al jugador en pantalla): la
resolución mejora **entre sesiones**, según cuánto has estado cerca de esa red. El
reconocimiento se acumula solo.

**Riesgo real, y es serio:** una mascota que no ves bien cuesta quererla, y 60
siluetas a medio dibujar son dificilísimas de distinguir — la mecánica que da la
identidad se come la legibilidad justo a escala de roster. Mitigación: existe arte
resuelto de cada especie y la Caja muestra la versión nítida una vez catalogada. **El
descubrimiento es borroso; la posesión es nítida.**

---

## 4. ENJAMBRE — "un bicho no es uno, es muchos"

**La locura:** un Pebble **nunca es un cuerpo**. Es una colonia renderizada como una
silueta. Es la lectura más honesta de "plaga" y de "cada vez más destructivo": una
población de bichos se vuelve destructiva **haciéndose numerosa**.

**Regla:**
- Estadio 1: **puedes contarlos** — 3 o 4 marcas sueltas, agrupadas flojas, cada una
  visiblemente individual.
- Estadio 2: racimo con forma emergente; en el borde aún distingues individuos, el
  centro ya es sólido.
- Estadio 3: tan denso que lee como una silueta maciza; solo el borde delata que es
  una multitud.

**Sistema generativo de verdad** (y esto es lo que la distingue en producción): la
especie es *marca unitaria* × *regla de agregación*. Marca: guion, punto, cheurón,
gancho, coma, aspa. Agregación: anillo, columna, espiral, retícula, deriva, cuña. 20
marcas × reglas dan 60 siluetas sin repetir una sola. No es un roster dibujado a mano,
es una gramática.

**A 1 bit el medio ES el mensaje:** el tramado **es** un enjambre. La única
herramienta de textura que tiene una pantalla de 1 bit es exactamente la materia de la
que está hecha esta criatura.

**Animación casi gratis:** la colonia respira desplazando individuos un píxel. El
estado de ánimo es la **tensión del racimo**: contento = suelto y regular; ansioso =
apretado; enfermo = disperso con rezagados; dormido = sedimentado en un montón. Muchísima
vida por muy poco arte.

**El gancho afectivo, que es único de esta propuesta:** uno de los individuos es
siempre un pelín más grande y va siempre un paso por delante. **El mismo, siempre.**
Acabas queriendo a ese. Resuelve de un plumazo el "monas como para que te importen" de
§53 sin dibujar una cara.

**Sonido:** muchos clics cortos con jitter de milisegundos. Una nube de ellos.

**Riesgo real:** el pipeline actual son frames XBM estáticos y esto quiere renderizado
procedural — hay coste de motor, no solo de arte. Y a escala, 60 racimos pueden
emborronarse si la gramática no se respeta con disciplina.

---

## 5. PATILLAS — "el chip que echó patas"

**La locura:** el chiste ya es verdad y nadie lo ha gastado. Un circuito integrado DIP
es un cuerpo negro con dos filas de patas, y en el argot **se le llama bicho**. La
mascota estaba dentro del hardware desde el principio.

**Regla de anatomía:** cuerpo rectangular relleno de negro sólido — la forma de máximo
contraste que puede hacer un OLED mono —, dos filas de patas, una muesca en un extremo
y un punto marcando la patilla 1. **El punto es el ojo. La muesca es la boca.** Sale
una cara gratis del marcado estándar de un encapsulado, y es una cara que no tiene
nadie más.

**Especie = encapsulado:** 8 patas, 14 patas, SOIC, QFP con patas en los cuatro
costados, TO-92 con tres patas gordas y barriga redonda. Más lo que lleve serigrafiado
en el lomo.

**Evolución = escala de integración:**
```
transistor de tres patas  →  chip de 8 patas  →  placa entera con chips encima
```
El estadio 3 es **una criatura hecha de otras criaturas**: una placa que lleva sus
propios componentes pequeños de pasajeros. Escalada legible y llamativa, y roza a
ENJAMBRE sin ser lo mismo.

**A 1 bit es la mejor de las cinco, sin discusión.** Un rectángulo negro macizo con
patas es inconfundible a 24×24, a un metro, a contraluz, en un OLED barato de tres
euros. Es la que mejor sobrevive al hardware real.

**Sonido:** clic de relé, silbido de fuente conmutada, el pop de arrancar.

**Sabor:** un SSID es un número de pieza. Las fichas se escriben como **hoja de
datos** — valores máximos absolutos, temperatura de funcionamiento, "aplicación
típica". En castellano es ridículo y muy gracioso.

**Riesgo real:** es la menos *whimsy* — sale más molona que tierna — y roza el
"bicho-robot" pixel-art genérico, que sí existe. La mitigación está entera en el tono
(humor de hoja de datos) y en que la animación sea blanda: las patas son flojas, no
mecánicas. Un chip que anda como un cachorro.

---

## 6. Comparativa

| | ERRATA | PARÁSITO | ECO | ENJAMBRE | PATILLAS |
|---|---|---|---|---|---|
| Faceta de "bug" que explota | defecto | plaga en infraestructura | señal invisible | población | silicio físico |
| Encaje con "no debería estar ahí" | **máximo** | medio | alto | medio | bajo |
| Encaje con §20 `category_mask` | bueno | **directo** | bueno | medio | medio |
| Corrupción §55 | **nativa** | añadida | añadida | añadida | añadida |
| Distancia a los 3 grandes | **máxima** | **la menor** | alta | máxima | alta |
| Legibilidad a 24×24 en OLED real | alta | media | **baja** | media | **máxima** |
| Coste de arte | bajo | **alto** | medio | bajo (alto de motor) | bajo |
| Escala a 60 especies | buena | excelente | **mala** | **excelente (generativa)** | buena |
| Facilidad de encariñarse | baja | **alta** | baja | media-alta | media |
| Sonido de marca | avería | estridulación | detector | nube de clics | relé |

---

## 7. Recomendación

La ficción tiene **dos mitades** y el error sería elegir una:

> **bicho = insecto que cazas en hábitats** · **bug = defecto que rompe cosas**

**PARÁSITO pone el esqueleto. ERRATA pone lo que pasa cuando escalan.**

Un Pebble sano se dibuja como grabado entomológico limpio: anatomía nítida, líneas
finas, ficha de campo. A medida que sube de estadio — y cuando se corrompe (§55) — el
grabado **se degrada al vocabulario de ERRATA**: la línea se desgarra, aparece damero
donde debería haber tramado, el cuerpo empieza a no caber en su caja. El estadio 3 es
un insecto que ya no consigue renderizarse.

Eso da, en una sola idea:
- las dos mitades del juego de palabras, sin elegir;
- §19 "cada vez más destructivo" hecho visible sin subir de tamaño;
- §55 corrupción como **el mismo eje** que la evolución, no un efecto aparte;
- `category_mask` convertido en bioma, con las cinco categorías de red como cinco
  ecologías con anatomías distintas;
- y la distancia a Pokémon que a PARÁSITO sola le falta: un insecto de lámina del XIX
  **que se desintegra** no se parece a nada de los tres grandes.

**El coste hay que decirlo claro:** es la combinación más cara de las posibles. El
grabado fino es el estilo que peor perdona, y 60 láminas con tres grados de
degradación cada una es el trabajo de arte más grande del proyecto.

**Si ese coste no cabe:**
- **PATILLAS** es la alternativa barata y honesta. Es la que mejor se ve en el OLED
  real, la más rápida de dibujar y la más graciosa. Se pierde ternura, se gana que
  funcione a la primera.
- **ENJAMBRE** es la respuesta si lo que asusta es el roster de 60: es la única con
  una gramática generativa de verdad (marca × agregación), y el truco del individuo
  que siempre va delante resuelve el afecto sin dibujar una cara.

**La que descartaría:** ECO. La idea es preciosa y el sonido como detector es la mejor
mecánica suelta del documento, pero una mascota que no ves bien pelea contra §63
(legibilidad) y contra §70 (encariñarse) a la vez, y a 60 especies se hunde. Lo que sí
merece rescatarse es su detalle de que **los ojos resuelvan primero**: sirve en
cualquiera de las otras cuatro.

---

## 8. Consecuencia que hay que asumir, se elija lo que se elija

Las cinco implican **tirar el set de sprites actual entero**. `baby_gato`, `baby_seta`,
`senior_buho`, `senior_punki`, `senior_moho` y compañía no sobreviven a ninguna, porque
son el "animal de fantasía genérico" que §53 prohíbe. `SPRITE_REV` sube,
`sprite_src.py` se reescribe, y P4-C1 (`gen_content.py`) genera el roster contra el
lenguaje elegido y no contra el heredado.

Es la decisión más cara del documento y conviene tomarla **antes de P4-C1**, no
después.
