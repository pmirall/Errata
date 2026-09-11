# Errata — reglas de trabajo

Firmware de un bicho virtual portátil sobre ESP32-C3. Español en la UI y en la
conversación; inglés en el código y en los mensajes de commit.

---

## LA REGLA DE LA GUÍA

**Si un cambio toca algo que el jugador ve, la documentación del jugador se
actualiza en el mismo commit, y la actualiza un agente de guía al final del
trabajo.**

Son **dos artefactos y los dos cuentan**:

| | Qué es | Cómo llega |
|---|---|---|
| `docs/manual/` → `docs/uso.pdf` | El cuadernillo A6 que va en la caja | Impreso, grapado, dentro de la caja |
| `docs/index.html` | La web pública: bestiario, pantallas, ataques, objetos | QR del aparato → `pmirall.github.io/Errata` |

Ninguno es un `README` que se puede quedar viejo sin consecuencias. El primero
se imprime y se vende; el segundo es lo que abre el móvil de cualquiera que
escanee el QR. Un manual que describe un aparato que ya no existe es peor que no
tener manual, y una web que enseña un bicho con otro nombre es peor que no tener
web.

### Cuándo se dispara

Cualquiera de estas cosas obliga a pasar por el agente **antes de commitear**:

- Una pantalla nueva (`ScreenId`), o una que desaparece.
- Una fila nueva en un menú, o una que cambia de nombre o de sitio.
- Un gesto que cambia de significado en cualquier pantalla.
- Una mecánica de juego nueva, o una que cambia de reglas.
- Un texto de `core/strings_es.h` que la guía cita literalmente.
- Un golden de `tests/golden/screens/` que se mueve — porque
  `docs/manual/assets/screens/*.svg` se regenera de ahí, y la web los enseña.
- Cualquier cosa de `tools/content/*.json` o `tools/sprites/*.txt`: una especie
  nueva, un nombre, un stat, un sprite redibujado, un ataque. Eso lo caza el
  gate (ver abajo), pero la **prosa** de la web puede quedarse mintiendo igual.

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
manual y de la web salen de ellos.

El agente se ocupa de los dos artefactos. Dile cuál toca: normalmente los dos,
pero un cambio de dato puro (retocar un stat) solo mueve la web, y un cambio de
gesto solo mueve el cuadernillo.

### Por qué un agente y no yo

Porque el manual tiene reglas propias que no se parecen a las del firmware
(bilingüe párrafo a párrafo, 40 páginas múltiplo de 4, ningún dato legal en
prosa, ninguna captura dibujada a mano, ningún número de página escrito) y
porque revisar 1.100 líneas de Typst buscando lo que quedó obsoleto es
exactamente el trabajo que se hace mal cuando se hace de pasada al final de
otra cosa.

### Qué es mecánico y qué es de criterio

Esto es lo importante de entender, porque la regla no sirve de nada si se
confunden las dos mitades.

**Mecánico — lo caza el gate, no hace falta acordarse:**

| Gate | Qué garantiza |
|---|---|
| `tools/pbm2svg.py --check` | ningún SVG del manual se quedó atrás de su golden |
| `tools/build_manual.sh --verify` | `docs/uso.pdf` corresponde a sus fuentes |
| `tools/build_wiki.py --check` | `docs/index.html` corresponde al roster, los sprites y las capturas |
| el cruce `#scr[...]` ↔ `strings_es.h` | el cuadernillo no cita un texto que el firmware ya no tiene |
| el destino de `MANUAL_URL` | el QR no apunta a un 404 |

**De criterio — no lo caza nada, y es para lo que existe el agente:**

- Que la prosa siga siendo verdad. Una frase puede quedarse mintiendo sin que se
  mueva un solo píxel ni un solo string: pasó con `LUZ`, una fila de menú que se
  borró en la fase 3 y que el manual siguió describiendo durante meses.
- Que lo nuevo esté **explicado**, no solo dibujado. Una captura enseña un
  estado de una pantalla y no dice nada de una frase tres páginas más allá.
- Las 40 páginas múltiplo de 4, y de dónde sale el sitio si hace falta una hoja.

**Instala `typst` antes de tocar el manual** (`docs/manual/README.md` tiene el
comando). Sin él, `--verify` se salta el PDF entero en silencio y el gate dice
`GATE OK` con el cuadernillo obsoleto — que es exactamente lo que pasó durante
dos commits.

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
  `ScreenId`. Insertar un id por el medio dispara **cinco** guardas
  independientes: `dev/diag_core.cpp`, los tres recuentos de
  `screen_table.cpp`, `kAudit[]` de `test_screens.cpp`, y en
  `test_statemachine.cpp` **tanto `kExits[]` como `kRender[]`**. Las cinco son
  correctas; hay que atender a las cinco. (Esta nota decía cuatro y se dejaba
  `kRender[]` fuera — lo encontró meter `SCR_WIKI` en P10-C11.)
- Un layout persistido **nunca se edita en su sitio**. Se añade a `reserved[]`,
  o se sube `SAVE_SCHEMA_VERSION`. Y antes de coger bytes de un `reserved[]`,
  mira qué lleva el fixture v1: lleva datos reales.
- Los binarios de test **no ejecutan la ruta de carga del guardado**, así que lo
  que el firmware ata ahí (`dex_bind()`, por ejemplo) un test lo tiene que atar
  por su cuenta o cada llamada será un no-op silencioso.

## Pines

Se quedan exactamente como están hasta que lleguen los componentes.
