# Errata — reglas de trabajo

Firmware de un bicho virtual portátil sobre ESP32-C3. Español en la UI y en la
conversación; inglés en el código y en los mensajes de commit.

---

## LA REGLA DE LA GUÍA

**Si un cambio toca algo que el jugador ve, la guía impresa se actualiza en el
mismo commit, y la actualiza un agente de guía al final del trabajo.**

`docs/manual/` es el manual que va en la caja, y es la única documentación que
un jugador va a leer. No es un `README` que se puede quedar viejo sin
consecuencias: se imprime, se mete en una caja y se vende. Un manual que
describe un aparato que ya no existe es peor que no tener manual.

### Cuándo se dispara

Cualquiera de estas cosas obliga a pasar por el agente **antes de commitear**:

- Una pantalla nueva (`ScreenId`), o una que desaparece.
- Una fila nueva en un menú, o una que cambia de nombre o de sitio.
- Un gesto que cambia de significado en cualquier pantalla.
- Una mecánica de juego nueva, o una que cambia de reglas.
- Un texto de `core/strings_es.h` que la guía cita literalmente.
- Un golden de `tests/golden/screens/` que se mueve — porque
  `docs/manual/assets/screens/*.svg` se regenera de ahí.

Cambios que **no** lo disparan: refactors sin efecto visible, tests, gates,
documentación interna (`docs/decisions.md`, `docs/budget.md`, `docs/bench.md`),
y cualquier cosa que el jugador no puede notar.

### Cómo se dispara

Al final del trabajo, cuando el código ya está terminado y los goldens ya están
grabados:

```
Agent(subagent_type: "guide", prompt: "<qué cambió, en detalle>")
```

La definición vive en `.claude/agents/guide.md`. Se lanza **en último lugar**,
no en paralelo con el código: necesita los goldens finales, porque los SVG del
manual salen de ellos.

### Por qué un agente y no yo

Porque el manual tiene reglas propias que no se parecen a las del firmware
(bilingüe párrafo a párrafo, 40 páginas múltiplo de 4, ningún dato legal en
prosa, ninguna captura dibujada a mano, ningún número de página escrito) y
porque revisar 1.100 líneas de Typst buscando lo que quedó obsoleto es
exactamente el trabajo que se hace mal cuando se hace de pasada al final de
otra cosa.

### El agujero que esta regla tapa

`tools/check.sh` compara los SVG generados contra los goldens y falla si uno se
movió sin regenerarse. **No comprueba que la prosa mencione la pantalla nueva**,
y se salta la verificación del PDF entero si `typst` no está instalado — que es
lo que pasó: `docs/uso.pdf` llevaba dos commits obsoleto y el gate decía `GATE
OK`. Instala typst (`docs/manual/README.md` tiene el comando) antes de tocar el
manual, o el agente trabajará a ciegas.

---

## Ramas y push

- Se desarrolla en `claude/repo-exploration-sync-bbonku`.
- **El usuario ha autorizado empujar a `main`.** Ninguna otra rama sin permiso.
- `git push -u origin <rama>`, reintentando 2s/4s/8s/16s si falla por red.
- **No se abre un pull request salvo que el usuario lo pida.**

## Las puertas

Ninguna de las tres es opcional y **se ejecutan de una en una** — dos builds a
la vez corrompen la salida:

```sh
make -C tests check      # 62 binarios de host
tools/check.sh           # EL gate: pureza de capas, fakes declarados, ASAN, SVG
tools/build_matrix.sh    # 6 variantes de firmware, cero warnings, topes de flash
```

## El defecto recurrente de este proyecto

**Lo que se prueba no es lo que se envía.** Ha pasado suficientes veces como
para que sea la regla de casa: un test que maneja un array que el propio test
posee sigue pasando cuando el firmware ata los bytes equivocados.

El estándar de prueba es: **romper lo que un test protege y ver fallar un test
CON NOMBRE.** Si al romperlo no falla nada, el test no protegía nada. Esto vale
para el código y vale para la guía: un párrafo del manual que sigue siendo
correcto después de borrar la función que describe no estaba describiendo nada.

## Detalles que muerden

- `core/strings_es.h` es **UTF-8**. Un byte suelto de Latin-1 (`0xBA` por `º`)
  lo caza `fb_bad_utf8()`, pero solo si el string llega a dibujarse.
- El bloque de menú de `strings_es.h` es **contiguo** y hay un `static_assert`
  que lo cuenta.
- `ScreenDef[]` en `ui/screen_table.cpp` es **posicional y paralelo** a
  `ScreenId`. Insertar un id por el medio dispara cuatro guardas independientes:
  `dev/diag_core.cpp`, los tres recuentos de `screen_table.cpp`, `kAudit[]` de
  `test_screens.cpp` y `kExits[]` de `test_statemachine.cpp`. Las cuatro son
  correctas; hay que atender a las cuatro.
- Un layout persistido **nunca se edita en su sitio**. Se añade a `reserved[]`,
  o se sube `SAVE_SCHEMA_VERSION`. Y antes de coger bytes de un `reserved[]`,
  mira qué lleva el fixture v1: lleva datos reales.
- Los binarios de test **no ejecutan la ruta de carga del guardado**, así que lo
  que el firmware ata ahí (`dex_bind()`, por ejemplo) un test lo tiene que atar
  por su cuenta o cada llamada será un no-op silencioso.

## Pines

Se quedan exactamente como están hasta que lleguen los componentes.
