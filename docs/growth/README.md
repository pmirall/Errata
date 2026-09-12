# El recon de crecimiento (2026-09-11)

Diez direcciones de crecimiento, mapeadas contra el código fuente por veinte
agentes en paralelo: **un lector por elemento, y un escéptico detrás de cada
lector** cuyo único trabajo era refutarlo abriendo los ficheros. 1.164 lecturas
de fichero en total.

Esto se guarda porque **re-derivarlo cuesta veinte agentes**, y porque lo que
encontró no cabía en una conversación.

---

## CÓMO SE LEE ESTO, Y ES IMPORTANTE

Cada fichero tiene **dos mitades y la primera no es de fiar**.

| Sección | Quién la escribió | Cuánto vale |
|---|---|---|
| *Lo que ya existe*, *La costura*, *Pasos*, *Guardas*, *Alcance* | el lector | **contiene afirmaciones demostradas falsas** |
| *EL ESCÉPTICO* → *Afirmaciones REFUTADAS* | el escéptico, con el fichero abierto | esto es lo que hay que creer |
| *PASOS CORREGIDOS* | el escéptico | **usa estos, no los del lector** |

Los diez escépticos refutaron entre **5 y 10 afirmaciones cada uno**. No son
matices: son cosas que costarían una tarde a quien las siguiera. Tres ejemplos
reales:

- `csp_install()` **no acepta un slot de destino** — instala en `rec.slot`, y el
  plan de A1 nunca lo reescribía.
- El paso 11 de A6 **muere dentro de `tools/gen_content.py:299`** antes de que
  `check.sh` llegue a arrancar.
- El test de falsación que A7 proponía como su garantía principal **no podía
  fallar**: seguía pasando después de romper lo que decía proteger. Que es
  exactamente el defecto recurrente que CLAUDE.md nombra.

**Un párrafo de la mitad del lector citado como hecho es una cita de algo que
puede estar refutado dos pantallas más abajo.** Si vas a apoyar una decisión en
una línea de estos ficheros, comprueba antes que el escéptico no la tumbó.

---

## Los diez elementos

| | Qué es | globals | ¿un commit? |
|---|---|---|---|
| **A1** | Un bicho dibujado puede intercambiarse a otro aparato | ~204 B | no, tres |
| **A2** | Ataques que usen los efectos que el motor ya implementa | 0 B | no |
| **A3** | Más objetos dentro de las clases que ya funcionan | 0 B | casi |
| **A4** | Más familias de especies | 0 B | ~ |
| **A5** | La exploración usa los seis bits que el escáner ya paga | 0 B | no, tres |
| **A6** | La corrupción como eje, no como cosmético de 24 h | 0–4 B | no, dos y media |
| **A7** | La wiki de a bordo da objetivos | 0 B | la mitad de código, sí |
| **B2** | Un diario de lugares desde los hashes de red que ya existen | ~128 B | sí, pero no el que decía |
| **B4** | Códigos impresos en el manual que canjean algo | ~26 B | la mitad de firmware, sí |
| **B6** | Un séptimo minijuego de decisión, no de reflejos | ~22 B | sí |

**A4 se descartó** por decisión del propietario: su coste no es memoria, son
horas de dibujo, y `docs/creature_style.md` dice que un 24×24 de ruido pasa
todas las comprobaciones automáticas. Su dossier se queda por si vuelve.

---

## Los dos hallazgos que cambiaron el plan

**1. Los globals no eran el límite.** La suma de los diez es **~226 B de los
3.724 libres**, derivado de forma independiente por los diez escépticos con `nm`
sobre el ELF. La intuición previa —que cuatro pantallas nuevas se comerían el
presupuesto— era falsa. Lo caro es el trabajo, no la memoria.

**2. Nada de esto es un commit.** Los diez escépticos llegaron a la misma
conclusión por caminos distintos. El conjunto son unos 15-18 commits reales.

Y uno concreto que merece su propia línea, porque invierte el enunciado del
problema: **el rechazo de los bichos del creador en el cable es un solo `if` del
lado receptor** (`protocol.cpp:126-132`). No hay ninguna guarda al enviar. Hoy
un jugador **ya puede ofrecer** un bicho dibujado: se serializa, viaja, y el otro
aparato lo rechaza con `VR_WIRE_CUSTOM_UNRESOLVED`. La función no está
bloqueada, está **rota en el extremo lejano**.

**Y A1 es intercambio, no cría.** `breeding.cpp:74` rechaza `compat_group == 0`
y `validate_custom_species()` exige que sea 0. El hijo se deriva de la *familia*
de los padres, y un bicho dibujado no tiene ninguna. No es un problema de
protocolo y no se arregla con un mensaje nuevo.

---

## Orden de ejecución acordado

Los ficheros se pisan —`strings_es.h`, las cabeceras generadas, y las **cinco**
guardas posicionales de `ScreenId`— así que el diseño va en paralelo y la
implementación en serie:

1. **Contenido**: A2, A3, A5, A6
2. **Firmware sin pantalla nueva**: A7
3. **Pantallas nuevas**: B2, B4, B6 — cinco guardas cada una
4. **Protocolo**: A1

---

*Nota de procedencia: esto es salida de agentes, guardada por su valor de
investigación, no prosa revisada a mano. Los números y las citas `fichero:línea`
se verificaron; la redacción no se ha pulido. Se guarda crudo a propósito — una
versión resumida perdería justamente las refutaciones, que son la mitad útil.*
