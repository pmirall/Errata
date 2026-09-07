// =============================================================================
//  PEBBLEBOL - data/index_html.h
//
//  GENERATED FILE. Do not edit: tools/gen_index_html.py rewrites it from
//  web/creator/*. `tools/gen_index_html.py --check` fails the gate if this
//  file and the page source have drifted apart.
//
//  THE CREATOR PAGE, AS THE BYTES GET / SERVES (spec sections 33 and 37).
//
//  ONE DOCUMENT AND ONE ROUTE. index.html, app.js and sprite_editor.js are
//  inlined into a single blob, so the phone makes exactly one request for the
//  page and the device serves exactly one asset - spec section 38's "serve
//  only required assets", held by there being nothing else to serve.
//
//  INCLUDE THIS FROM webui.cpp ONLY. INDEX_HTML has internal linkage (const at
//  namespace scope), so every translation unit that *uses* it keeps its own
//  copy in .rodata; -Wl,--gc-sections only drops the copies nobody references.
//
//  Serve it with the FOUR-argument send_P. The 3-arg form strlen_P()s the blob
//  (WebServer.cpp:619) and the 1-arg sendContent_P has the same bug:
//    server.sendHeader(F("Cache-Control"), F("no-cache"));
//    server.send_P(200, PSTR("text/html; charset=utf-8"), INDEX_HTML, INDEX_HTML_LEN);
//
//  IT IS .rodata AND COSTS ZERO GLOBALS. The page is FLASH; the row to watch
//  in phase 8 is globals, and this file is not on it.
//
//  THE PAGE IS PRINTABLE ASCII, which is why a raw string literal is safe here
//  and why sizeof() - 1 is the byte count, the character count and the
//  Content-Length all at once. The generator refuses anything else.
//
//  WHAT THE PAGE ENFORCES IS A COURTESY AND NEVER A CONTROL: every rule it
//  mirrors is enforced again on this device by game/validate.cpp, over the
//  bytes that actually arrived. See the banner of web/creator/app.js.
// =============================================================================

#ifndef NT_INDEX_HTML_H
#define NT_INDEX_HTML_H

#include <Arduino.h>
#include "../core/config.h"
#include "../core/version.h"

// The creator API version this page was generated against, copied out of
// core/version.h by the generator and substituted into app.js. The assert
// below is what makes it a TIE rather than a note: bump CREATOR_API_VERSION
// without regenerating the page and the firmware stops compiling, instead of
// shipping a page that tells the phone the wrong number.
#define INDEX_HTML_API_VERSION  1

static const char INDEX_HTML[] PROGMEM = R"PBHTML(
<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#12140f">
<title>Pebblebol &mdash; Creador</title>
<style>
:root{
--bg:#12140f; --panel:#1b1e18; --panel2:#242820; --line:#333930;
--ink:#e8eae2; --dim:#98a18f; --acc:#7fd45a; --acc2:#5aa63c;
--warn:#e0a33a; --bad:#e0584a; --tap:44px;
color-scheme:dark;
}
*{box-sizing:border-box}
html,body{margin:0;background:var(--bg);color:var(--ink)}
body{
font:16px/1.45 ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
-webkit-text-size-adjust:100%;
overscroll-behavior-y:contain;
padding-bottom:calc(64px + env(safe-area-inset-bottom));
}
h2{font-size:1.05rem;letter-spacing:.06em;margin:0 0 .2rem}
p{margin:.35rem 0;color:var(--dim);font-size:.9rem}
b{color:var(--ink)}
#hdr{
position:sticky;top:0;z-index:5;background:var(--bg);
border-bottom:1px solid var(--line);
padding:calc(6px + env(safe-area-inset-top)) 12px 8px;
}
#hdr-top{display:flex;justify-content:space-between;align-items:baseline;gap:8px}
#hdr-title{font-weight:700;letter-spacing:.14em;font-size:.8rem;color:var(--acc)}
#hdr-dev{font-size:.75rem;color:var(--dim);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#steps{display:flex;gap:4px;list-style:none;margin:8px 0 0;padding:0}
#steps li{
flex:1;height:4px;border-radius:2px;background:var(--panel2);
}
#steps li.done{background:var(--acc2)}
#steps li.now{background:var(--acc)}
#steps-name{font-size:.72rem;letter-spacing:.12em;color:var(--dim);margin-top:6px}
#budget{margin-top:8px}
#budget-row{display:flex;justify-content:space-between;font-size:.72rem;letter-spacing:.12em}
#budget-pct{font-variant-numeric:tabular-nums;font-weight:700}
#budget-bar{height:10px;border:1px solid var(--line);border-radius:2px;margin-top:4px;overflow:hidden;background:var(--panel)}
#budget-fill{display:block;height:100%;width:0;background:var(--acc);transition:width .12s linear}
#budget.over #budget-fill{background:var(--bad)}
#budget.over #budget-pct{color:var(--bad)}
#budget-sub{font-size:.72rem;color:var(--dim);margin-top:3px;font-variant-numeric:tabular-nums}
main{padding:14px 12px 4px;max-width:34rem;margin:0 auto}
.scr[hidden]{display:none}
.card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px;margin:10px 0}
.row{display:flex;align-items:center;gap:10px}
.row+.row{margin-top:8px}
.grow{flex:1;min-width:0}
.mono{font-variant-numeric:tabular-nums;font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
.hint{font-size:.78rem;color:var(--dim)}
.bad{color:var(--bad)} .ok{color:var(--acc)} .warn{color:var(--warn)}
button,input,select{font:inherit;color:inherit}
button{
min-height:var(--tap);padding:0 14px;border-radius:8px;
background:var(--panel2);border:1px solid var(--line);
letter-spacing:.06em;cursor:pointer;touch-action:manipulation;
}
button:disabled{opacity:.4;cursor:default}
button.pri{background:var(--acc2);border-color:var(--acc);color:#0c1208;font-weight:700}
button.sel{background:var(--acc2);border-color:var(--acc);color:#0c1208}
button.sq{min-width:var(--tap);padding:0}
input[type=text],input[type=tel]{
min-height:var(--tap);width:100%;padding:0 10px;border-radius:8px;
background:var(--panel2);border:1px solid var(--line);
}
input[type=range]{width:100%;height:var(--tap);accent-color:var(--acc)}
.tools{display:flex;flex-wrap:wrap;gap:6px}
.tools button{flex:1 1 auto;min-width:64px;font-size:.8rem}
#pad{
width:100%;aspect-ratio:1;display:block;margin:0 auto;
background:#000;border:1px solid var(--line);border-radius:4px;
touch-action:none;               /* the thumb paints, it never scrolls */
image-rendering:pixelated;
}
#padinfo{display:flex;justify-content:space-between;font-size:.75rem;color:var(--dim);margin-top:4px}
.prevbox{display:flex;gap:14px;align-items:flex-end;justify-content:center;margin-top:10px}
.prevbox figure{margin:0;text-align:center}
.prevbox figcaption{font-size:.7rem;color:var(--dim);letter-spacing:.1em;margin-top:4px}
canvas.prev{background:#000;border:1px solid var(--line);image-rendering:pixelated;display:block}
#atklist{display:flex;flex-direction:column;gap:6px;max-height:52vh;overflow:auto;-webkit-overflow-scrolling:touch}
.atk{
display:flex;align-items:center;gap:8px;min-height:var(--tap);
padding:6px 10px;border-radius:8px;background:var(--panel2);
border:1px solid var(--line);text-align:left;width:100%;
}
.atk.sel{background:var(--acc2);border-color:var(--acc);color:#0c1208}
.atk .an{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:.9rem}
.atk .am{font-size:.72rem;opacity:.85;font-variant-numeric:tabular-nums;white-space:nowrap}
.stat{margin-bottom:6px}
.stat .lab{display:flex;justify-content:space-between;font-size:.8rem;letter-spacing:.08em}
.stat .val{font-variant-numeric:tabular-nums;font-weight:700}
#nav{
position:fixed;left:0;right:0;bottom:0;z-index:6;display:flex;gap:8px;
padding:8px 12px calc(8px + env(safe-area-inset-bottom));
background:var(--bg);border-top:1px solid var(--line);
}
#nav button{flex:1}
#toast{
position:fixed;left:12px;right:12px;bottom:calc(76px + env(safe-area-inset-bottom));
z-index:7;background:var(--panel2);border:1px solid var(--line);
border-radius:8px;padding:10px 12px;font-size:.85rem;box-shadow:0 6px 20px #0008;
}
#toast.bad{border-color:var(--bad)}
#toast.ok{border-color:var(--acc)}
#verdict{font-size:.9rem}
#verdict .code{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:.78rem;color:var(--dim)}
.kv{display:flex;justify-content:space-between;font-size:.85rem;padding:3px 0;border-bottom:1px solid var(--line)}
.kv:last-child{border-bottom:0}
.kv span:last-child{font-variant-numeric:tabular-nums}
</style>
</head>
<body>
<header id="hdr">
<div id="hdr-top">
<span id="hdr-title">PEBBLEBOL &middot; CREADOR</span>
<span id="hdr-dev"></span>
</div>
<ol id="steps"></ol>
<div id="steps-name">CONEXI&Oacute;N</div>
<div id="budget" hidden>
<div id="budget-row"><span>PRESUPUESTO</span><span id="budget-pct">0 %</span></div>
<div id="budget-bar"><i id="budget-fill"></i></div>
<div id="budget-sub"></div>
</div>
</header>
<main id="main">
<section class="scr" id="sc-connect">
<h2>CONEXI&Oacute;N</h2>
<p>Introduce el PIN que aparece en la pantalla del dispositivo.
Sin el PIN correcto puedes ver la conexi&oacute;n pero no modificar nada.</p>
<div class="card">
<div class="row">
<input id="pin" type="tel" inputmode="numeric" autocomplete="off"
maxlength="4" placeholder="PIN" aria-label="PIN">
<button id="btn-pin" class="pri" type="button">ENTRAR</button>
</div>
<div id="pin-msg" class="hint" style="margin-top:8px"></div>
</div>
<div class="card" id="devbox" hidden>
<div class="kv"><span>Dispositivo</span><span id="d-name">&mdash;</span></div>
<div class="kv"><span>Firmware</span><span id="d-fw">&mdash;</span></div>
<div class="kv"><span>Huecos en la caja</span><span id="d-box">&mdash;</span></div>
<div class="kv"><span>Huecos de especie</span><span id="d-cs">&mdash;</span></div>
<div class="kv"><span>Reloj</span><span id="d-clock">&mdash;</span></div>
<div class="row" style="margin-top:10px">
<button id="btn-clock" type="button" class="grow">SINCRONIZAR RELOJ</button>
</div>
<p class="hint">El dispositivo decide si acepta la hora del tel&eacute;fono.</p>
</div>
</section>
<section class="scr" id="sc-info" hidden>
<h2>NOMBRE</h2>
<p>Como se llamar&aacute; la especie. Lo dibuja la pantalla del dispositivo,
as&iacute; que el juego de caracteres es el que esa pantalla sabe pintar.</p>
<div class="card">
<input id="name" type="text" autocomplete="off" spellcheck="false"
placeholder="Bicho" aria-label="Nombre de la especie">
<div class="row" style="margin-top:8px">
<span id="name-count" class="hint mono grow">0 / 12</span>
<span id="name-msg" class="hint"></span>
</div>
</div>
</section>
<section class="scr" id="sc-type" hidden>
<h2>TIPO</h2>
<p>El tipo decide qu&eacute; ataques puede aprender: los suyos y los
neutrales. Cambiarlo m&aacute;s tarde vac&iacute;a los ataques elegidos.</p>
<div class="card" id="typebox"></div>
</section>
<section class="scr" id="sc-body" hidden>
<h2>CUERPO</h2>
<p>Reparte los puntos entre las cuatro estad&iacute;sticas. El total tiene
que caer dentro de la banda que marca el dispositivo.</p>
<div class="card" id="statbox"></div>
<div class="card">
<div class="kv"><span>Total</span><span id="stat-total" class="mono">0</span></div>
<div class="kv"><span>Banda permitida</span><span id="stat-band" class="mono">&mdash;</span></div>
<div id="stat-msg" class="hint" style="margin-top:6px"></div>
</div>
</section>
<section class="scr" id="sc-sprite" hidden>
<h2>SPRITE</h2>
<canvas id="pad" width="24" height="24" role="img"
aria-label="Rejilla de 24 por 24 pixeles"></canvas>
<div id="padinfo"><span id="pad-xy" class="mono">&mdash;</span><span id="pad-on" class="mono"></span></div>
<div class="card">
<div class="row" style="margin-bottom:8px">
<button id="f0" class="sq sel" type="button">1</button>
<button id="f1" class="sq" type="button">2</button>
<button id="fcopy" class="grow" type="button">COPIAR 1 &rarr; 2</button>
</div>
<div class="tools">
<button id="t-draw" class="sel" type="button">PINTAR</button>
<button id="t-erase" type="button">BORRAR</button>
<button id="t-fill" type="button">RELLENO</button>
<button id="t-undo" type="button">DESHACER</button>
<button id="t-fliph" type="button">ESPEJO H</button>
<button id="t-flipv" type="button">ESPEJO V</button>
<button id="t-clear" type="button">LIMPIAR</button>
</div>
<p class="hint">Un pixel encendido se dibuja en blanco sobre negro.
La paleta es el formato: un bit por pixel, sin color y sin
transparencia.</p>
</div>
<div class="prevbox">
<figure><canvas id="p1" class="prev" width="24" height="24"></canvas>
<figcaption>1x</figcaption></figure>
<figure><canvas id="p2" class="prev" width="48" height="48"></canvas>
<figcaption>2x</figcaption></figure>
</div>
</section>
<section class="scr" id="sc-attacks" hidden>
<h2>ATAQUES</h2>
<p>Cuatro ataques distintos, del tipo de la especie o neutrales, y al menos
uno que haga da&ntilde;o.</p>
<div class="row" style="margin-bottom:8px">
<span id="atk-count" class="hint mono grow">0 / 4</span>
<span id="atk-cost" class="hint mono"></span>
</div>
<div id="atklist"></div>
<div id="atk-msg" class="hint" style="margin-top:8px"></div>
</section>
<section class="scr" id="sc-validate" hidden>
<h2>VALIDAR</h2>
<p>Lo que ves arriba lo calcula esta p&aacute;gina. Lo que decide es el
dispositivo: pulsa y te contesta &eacute;l.</p>
<div class="card">
<button id="btn-validate" class="pri" type="button" style="width:100%">
PREGUNTAR AL DISPOSITIVO</button>
<div id="verdict" style="margin-top:10px">&mdash;</div>
</div>
<div class="card" id="localbox">
<div class="kv"><span>Revisi&oacute;n local</span><span id="local-sum">&mdash;</span></div>
<div id="local-list" class="hint" style="margin-top:6px"></div>
<p class="hint">La revisi&oacute;n local es una cortes&iacute;a para no
hacerte perder el rato. No es un permiso: el dispositivo vuelve a
comprobarlo todo sobre los bytes que le llegan.</p>
</div>
</section>
<section class="scr" id="sc-preview" hidden>
<h2>VISTA PREVIA</h2>
<div class="prevbox">
<figure><canvas id="q1" class="prev" width="24" height="24"></canvas>
<figcaption>1x</figcaption></figure>
<figure><canvas id="q2" class="prev" width="48" height="48"></canvas>
<figcaption>2x</figcaption></figure>
</div>
<div class="card" id="sumbox"></div>
<p class="hint">La animaci&oacute;n alterna los dos fotogramas, que es lo que
hace el dispositivo con cualquier Pebble.</p>
</section>
<section class="scr" id="sc-upload" hidden>
<h2>ENVIAR</h2>
<p>El dispositivo elige el hueco y te dice cu&aacute;l ha usado. Si algo
falla no escribe nada.</p>
<div class="card">
<button id="btn-upload" class="pri" type="button" style="width:100%">
CREAR EL PEBBLE</button>
<div id="upload-msg" style="margin-top:10px">&mdash;</div>
</div>
<div class="card" id="donebox" hidden>
<div class="kv"><span>Hueco en la caja</span><span id="u-slot">&mdash;</span></div>
<div class="kv"><span>Hueco de especie</span><span id="u-cs">&mdash;</span></div>
<div class="kv"><span>Id de especie</span><span id="u-species">&mdash;</span></div>
<div class="kv"><span>Id del Pebble</span><span id="u-id">&mdash;</span></div>
<div class="kv"><span>Presupuesto</span><span id="u-pct">&mdash;</span></div>
<p class="hint">El dispositivo dibuja el cuerpo del atlas hasta que el
renderizador lea los sprites de creador (fase 9). El dibujo ya est&aacute;
guardado.</p>
</div>
</section>
</main>
<nav id="nav">
<button id="btn-back" type="button">ATR&Aacute;S</button>
<button id="btn-next" class="pri" type="button">SIGUIENTE</button>
</nav>
<div id="toast" hidden role="status" aria-live="polite"></div>
<script>
var SE = (function () {
'use strict';
var W = 24, H = 24, NF = 2;          // overwritten by SE.geom() from /api/schema
var UNDO_MAX = 32;
var px   = [];                        // NF Uint8Array(W*H), 0 or 1
var cur  = 0;                         // frame being edited
var tool = 'draw';
var undo = [];                        // {f:frame, a:Uint8Array copy}
var onChange = null;
var pad = null, pctx = null;          // the editing canvas
var cell = 12;                        // CSS px per cell, recomputed on resize
var drawing = false, paintTo = 1, lastX = -1, lastY = -1;
function alloc() {
px = [];
for (var f = 0; f < NF; f++) px.push(new Uint8Array(W * H));
undo = [];
}
function geom(w, h, frames) {
W = w; H = h; NF = frames;
alloc();
if (pad) { pad.width = W; pad.height = H; }
}
function get(f, x, y) {
if (x < 0 || y < 0 || x >= W || y >= H) return 0;
return px[f][y * W + x];
}
function set(f, x, y, v) {
if (x < 0 || y < 0 || x >= W || y >= H) return;
px[f][y * W + x] = v ? 1 : 0;
}
function pushUndo() {
undo.push({ f: cur, a: px[cur].slice(0) });
if (undo.length > UNDO_MAX) undo.shift();
}
function doUndo() {
var u = undo.pop();
if (!u) return false;
px[u.f] = u.a;
changed();
return true;
}
function clear() { pushUndo(); px[cur].fill(0); changed(); }
function flipH() {
pushUndo();
var p = px[cur], y, x, t;
for (y = 0; y < H; y++)
for (x = 0; x < (W >> 1); x++) {
t = p[y * W + x];
p[y * W + x] = p[y * W + (W - 1 - x)];
p[y * W + (W - 1 - x)] = t;
}
changed();
}
function flipV() {
pushUndo();
var p = px[cur], y, x, t;
for (y = 0; y < (H >> 1); y++)
for (x = 0; x < W; x++) {
t = p[y * W + x];
p[y * W + x] = p[(H - 1 - y) * W + x];
p[(H - 1 - y) * W + x] = t;
}
changed();
}
function copyFrame(from, to) {
if (from === to || from >= NF || to >= NF) return;
undo.push({ f: to, a: px[to].slice(0) });
px[to] = px[from].slice(0);
changed();
}
function fill(x0, y0) {
var want = get(cur, x0, y0);
var v = want ? 0 : 1;
pushUndo();
var stack = [x0 + y0 * W], seen = new Uint8Array(W * H);
while (stack.length) {
var i = stack.pop();
if (seen[i]) continue;
seen[i] = 1;
var x = i % W, y = (i - x) / W;
if (get(cur, x, y) !== want) continue;
set(cur, x, y, v);
if (x > 0)     stack.push(i - 1);
if (x < W - 1) stack.push(i + 1);
if (y > 0)     stack.push(i - W);
if (y < H - 1) stack.push(i + W);
}
changed();
}
var HEXD = '0123456789abcdef';
function stride() { return (W + 7) >> 3; }
function hex(f) {
var s = '', y, b, i, x, v, p = px[f], sw = stride();
for (y = 0; y < H; y++) {
for (b = 0; b < sw; b++) {
v = 0;
for (i = 0; i < 8; i++) {
x = b * 8 + i;
if (x < W && p[y * W + x]) v |= (1 << i);
}
s += HEXD.charAt(v >> 4) + HEXD.charAt(v & 15);
}
}
return s;
}
function allHex() {
var a = [], f;
for (f = 0; f < NF; f++) a.push(hex(f));
return a;
}
function isEmpty(f) {
var p = px[f], i;
for (i = 0; i < p.length; i++) if (p[i]) return false;
return true;
}
function anyEmpty() {
var f;
for (f = 0; f < NF; f++) if (isEmpty(f)) return true;
return false;
}
function blit(canvas, f, scale) {
if (!canvas) return;
var w = W * scale, h = H * scale;
if (canvas.width !== w) canvas.width = w;
if (canvas.height !== h) canvas.height = h;
var ctx = canvas.getContext('2d');
var img = ctx.createImageData(w, h);
var d = img.data, p = px[f], x, y, sx, sy, o, on;
for (y = 0; y < h; y++) {
sy = (y / scale) | 0;
for (x = 0; x < w; x++) {
sx = (x / scale) | 0;
on = p[sy * W + sx];
o = (y * w + x) * 4;
d[o] = d[o + 1] = d[o + 2] = on ? 232 : 0;
d[o + 3] = 255;
}
}
ctx.putImageData(img, 0, 0);
}
function paint() {
if (!pad) return;
var dpr = window.devicePixelRatio || 1;
var side = Math.max(1, Math.round(pad.clientWidth));
var want = Math.round(side * dpr);
if (pad.width !== want) { pad.width = want; pad.height = want; }
cell = pad.width / W;
var ctx = pad.getContext('2d');
ctx.fillStyle = '#000';
ctx.fillRect(0, 0, pad.width, pad.height);
ctx.fillStyle = '#e8eae2';
var p = px[cur], x, y;
for (y = 0; y < H; y++)
for (x = 0; x < W; x++)
if (p[y * W + x])
ctx.fillRect(Math.floor(x * cell), Math.floor(y * cell),
Math.ceil(cell), Math.ceil(cell));
ctx.strokeStyle = 'rgba(152,161,143,.28)';
ctx.lineWidth = 1;
ctx.beginPath();
for (x = 1; x < W; x++) {
var gx = Math.floor(x * cell) + 0.5;
ctx.moveTo(gx, 0); ctx.lineTo(gx, pad.height);
}
for (y = 1; y < H; y++) {
var gy = Math.floor(y * cell) + 0.5;
ctx.moveTo(0, gy); ctx.lineTo(pad.width, gy);
}
ctx.stroke();
}
function changed() {
paint();
if (onChange) onChange();
}
function cellAt(ev) {
var r = pad.getBoundingClientRect();
var x = Math.floor((ev.clientX - r.left) / (r.width / W));
var y = Math.floor((ev.clientY - r.top) / (r.height / H));
if (x < 0) x = 0; if (x >= W) x = W - 1;
if (y < 0) y = 0; if (y >= H) y = H - 1;
return { x: x, y: y };
}
function stroke(x0, y0, x1, y1, v) {
var dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
var dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
var err = dx + dy, e2;
for (;;) {
set(cur, x0, y0, v);
if (x0 === x1 && y0 === y1) break;
e2 = 2 * err;
if (e2 >= dy) { err += dy; x0 += sx; }
if (e2 <= dx) { err += dx; y0 += sy; }
}
}
var reportXY = null;
function onDown(ev) {
ev.preventDefault();
var c = cellAt(ev);
if (reportXY) reportXY(c.x, c.y);
if (tool === 'fill') { fill(c.x, c.y); return; }
pushUndo();
drawing = true;
paintTo = (tool === 'erase') ? 0 : 1;
lastX = c.x; lastY = c.y;
set(cur, c.x, c.y, paintTo);
if (pad.setPointerCapture && ev.pointerId !== undefined)
try { pad.setPointerCapture(ev.pointerId); } catch (e) {}
changed();
}
function onMove(ev) {
var c = cellAt(ev);
if (reportXY) reportXY(c.x, c.y);
if (!drawing) return;
ev.preventDefault();
if (c.x === lastX && c.y === lastY) return;
stroke(lastX, lastY, c.x, c.y, paintTo);
lastX = c.x; lastY = c.y;
changed();
}
function onUp(ev) {
if (!drawing) return;
drawing = false;
if (pad.releasePointerCapture && ev.pointerId !== undefined)
try { pad.releasePointerCapture(ev.pointerId); } catch (e) {}
}
function mount(canvas, opts) {
pad = canvas;
pctx = pad.getContext('2d');
onChange = opts.onChange || null;
reportXY = opts.onHover || null;
pad.addEventListener('pointerdown', onDown);
pad.addEventListener('pointermove', onMove);
pad.addEventListener('pointerup', onUp);
pad.addEventListener('pointercancel', onUp);
pad.addEventListener('pointerleave', function () { if (reportXY) reportXY(-1, -1); });
window.addEventListener('resize', paint);
alloc();
paint();
}
return {
mount: mount, geom: geom, paint: paint, blit: blit,
frames: function () { return NF; },
w: function () { return W; },
h: function () { return H; },
frame: function () { return cur; },
setFrame: function (f) { if (f >= 0 && f < NF) { cur = f; changed(); } },
setTool: function (t) { tool = t; },
tool: function () { return tool; },
undo: doUndo,
clear: clear,
flipH: flipH,
flipV: flipV,
copyFrame: copyFrame,
hex: hex,
allHex: allHex,
isEmpty: isEmpty,
anyEmpty: anyEmpty,
get: get,
set: function (f, x, y, v) {
undo.push({ f: f, a: px[f].slice(0) });
if (undo.length > UNDO_MAX) undo.shift();
set(f, x, y, v);
changed();
},
lit: function (f) {
var n = 0, p = px[f], i;
for (i = 0; i < p.length; i++) if (p[i]) n++;
return n;
}
};
})();
</script>
<script>
(function () {
'use strict';
var API_EXPECTED = 1;
var S = null;          // the schema document, once fetched
var pin = '';          // in memory only, never stored
var linked = false;    // the device has answered a gated route
var step = 0;
var pingTimer = 0, previewTimer = 0;
var M = {              // the Pebble being built
name: '',
type: 0,
base: [],            // sized from the schema
moves: []            // attack ids, in pick order
};
var STEPS = [
['sc-connect',  'CONEXI\u00d3N'],
['sc-info',     'NOMBRE'],
['sc-type',     'TIPO'],
['sc-body',     'CUERPO'],
['sc-sprite',   'SPRITE'],
['sc-attacks',  'ATAQUES'],
['sc-validate', 'VALIDAR'],
['sc-preview',  'VISTA PREVIA'],
['sc-upload',   'ENVIAR']
];
var STAT_LABEL = ['VIDA', 'ATAQUE', 'DEFENSA', 'VELOCIDAD'];
function $(id) { return document.getElementById(id); }
function on(id, fn) { var e = $(id); if (e) e.addEventListener('click', fn); }
function txt(id, s) { var e = $(id); if (e) e.textContent = s; }
var MSG = {
pin:      'PIN incorrecto.',
nopin:    'El dispositivo no tiene la pantalla del creador abierta.',
locked:   'Demasiados intentos. Espera.',
arg:      'El dispositivo no conoce esa ruta.',
body:     'Esa petici\u00f3n no lleva cuerpo.',
len:      'Falta la longitud del cuerpo.',
big:      'El dibujo o el nombre ocupan m\u00e1s de lo que el dispositivo acepta.',
type:     'Formato de env\u00edo no aceptado.',
ro:       'El dispositivo est\u00e1 en s\u00f3lo lectura y no puede guardar.',
nopet:    'El dispositivo todav\u00eda no tiene ning\u00fan Pebble.',
boxfull:  'La caja est\u00e1 llena. Libera un hueco en el dispositivo.',
csfull:   'No quedan huecos de especie. Libera uno en el dispositivo.',
install:  'El dispositivo no pudo instalar la especie.',
box:      'El dispositivo no pudo crear el Pebble.',
flash:    'El dispositivo no pudo guardar en memoria.',
internal: 'El dispositivo rechaz\u00f3 el Pebble ya construido.',
CP_EMPTY:       'No lleg\u00f3 nada.',
CP_SYNTAX:      'El documento no tiene la forma esperada.',
CP_TRAILING:    'Sobran bytes al final del documento.',
CP_UNKNOWN_KEY: 'El documento lleva un campo que el dispositivo no conoce.',
CP_DUP_KEY:     'Un campo aparece dos veces.',
CP_MISSING_KEY: 'Falta un campo obligatorio.',
CP_BAD_TYPE:    'Un campo lleva un valor del tipo equivocado.',
CP_NUMBER:      'Un n\u00famero est\u00e1 fuera de rango.',
CP_ARRAY_LEN:   'Una lista no tiene la longitud exacta.',
CP_STRING_LEN:  'El nombre es demasiado largo.',
CP_NUL:         'El texto lleva un byte nulo.',
CP_UTF8:        'El nombre lleva un car\u00e1cter fuera de Latin-1.',
CP_NAME_CHAR:   'El nombre lleva un car\u00e1cter que la pantalla no dibuja.',
CP_SPRITE_LEN:  'El sprite no tiene el tama\u00f1o exacto.',
CP_SPRITE_HEX:  'El sprite lleva un car\u00e1cter que no es hexadecimal.',
CP_VERSION:     'Esta p\u00e1gina habla otra versi\u00f3n del API.',
VR_CS_BAD_HEADER:      'La cabecera de la especie no es v\u00e1lida.',
VR_CS_RESERVED:        'Campos reservados no vac\u00edos.',
VR_CS_BAD_TYPE:        'Tipo no v\u00e1lido.',
VR_CS_BAD_STAT:        'Una estad\u00edstica est\u00e1 fuera de rango.',
VR_CS_STAT_BUDGET:     'El total de estad\u00edsticas est\u00e1 fuera de la banda.',
VR_CS_UNKNOWN_MOVE:    'Un ataque no existe.',
VR_CS_MOVE_REPEATED:   'Hay un ataque repetido.',
VR_CS_MOVE_OFF_TYPE:   'Un ataque no es del tipo de la especie ni neutral.',
VR_CS_NO_DAMAGING_MOVE:'Hace falta al menos un ataque que haga da\u00f1o.',
VR_CS_POWER_CAP:       'Un ataque supera la potencia m\u00e1xima.',
VR_CS_ATTACK_BUDGET:   'Los ataques superan el presupuesto.',
VR_CS_BUDGET_MISMATCH: 'El dispositivo calcula otro coste.',
VR_CS_BAD_NAME:        'El nombre no es v\u00e1lido.',
VR_CS_BAD_COMPAT:      'Una especie de creador no cr\u00eda.'
};
function say(err, why) {
var base = MSG[err] || ('El dispositivo rechaz\u00f3 la petici\u00f3n (' + err + ').');
if (why) base = (MSG[why] || base) + ' ';
return base;
}
var toastTimer = 0;
function toast(s, kind) {
var t = $('toast');
t.textContent = s;
t.className = kind || '';
t.hidden = false;
if (toastTimer) clearTimeout(toastTimer);
toastTimer = setTimeout(function () { t.hidden = true; }, 4200);
}
function req(method, path, body) {
var h = { 'Accept': 'application/json' };
if (pin) h['X-Pin'] = pin;
if (body !== undefined) h['Content-Type'] = 'application/json';
return fetch(path, { method: method, headers: h, body: body, cache: 'no-store' })
.then(function (r) {
return r.text().then(function (t) {
var j = null;
if (t) { try { j = JSON.parse(t); } catch (e) { j = null; } }
return { status: r.status, j: j, raw: t };
});
});
}
function refusal(r) {
if (r.status === 429) return 'Demasiadas peticiones seguidas. Prueba otra vez.';
if (r.status >= 200 && r.status < 300) return null;
if (!r.j || !r.j.err) return 'El dispositivo contest\u00f3 ' + r.status + '.';
var s = say(r.j.err, r.j.why);
if (r.j.err === 'locked' && typeof r.j.s === 'number')
s = MSG.locked + ' ' + r.j.s + ' s.';
if (r.j.err === 'big' && typeof r.j.max === 'number')
s = MSG.big + ' (m\u00e1ximo ' + r.j.max + ' bytes)';
if (r.j.why) s += '[' + r.j.why + ']';
return s;
}
function codeOf(r) { return (r.j && r.j.why) ? r.j.why : (r.j && r.j.err) || String(r.status); }
function pct(statUsed, atkUsed) {
var T = S.stat.max, B = S.atkbudget;
var D = T * B;
var n = 50 * (B * statUsed + T * atkUsed);
var p = Math.floor((n + Math.floor(D / 2)) / D);
return p > 100 ? 100 : p;
}
function statUsed() {
var s = 0, i;
for (i = 0; i < M.base.length; i++) s += M.base[i];
return s;
}
function atkOf(id) {
var i;
for (i = 0; i < S.atk.length; i++) if (S.atk[i][0] === id) return S.atk[i];
return null;
}
function atkUsed() {
var c = 0, i, a;
for (i = 0; i < M.moves.length; i++) { a = atkOf(M.moves[i]); if (a) c += a[4]; }
return c;
}
function nameCharOk(cp) {
if (cp === 0x22 || cp === 0x5c) return false;   // stricter than the device, see banner
if (cp >= 0x20 && cp <= 0x7e) return true;
return [0xa1, 0xaa, 0xb0, 0xb7, 0xba, 0xbf,
0xc1, 0xc9, 0xcd, 0xd3, 0xda, 0xd1, 0xdc,
0xe1, 0xe9, 0xed, 0xf3, 0xfa, 0xf1, 0xfc].indexOf(cp) >= 0;
}
function nameProblem() {
var n = M.name, cps = Array.from(n), i, cp;
if (cps.length === 0) return 'El nombre no puede estar vac\u00edo.';
if (cps.length > S.name.max) return 'M\u00e1ximo ' + S.name.max + ' caracteres.';
if (cps[0] === ' ' || cps[cps.length - 1] === ' ')
return 'Sin espacio al principio ni al final.';
for (i = 0; i < cps.length; i++) {
cp = cps[i].codePointAt(0);
if (!nameCharOk(cp))
return 'El car\u00e1cter "' + cps[i] + '" no lo dibuja la pantalla.';
}
return null;
}
function localProblems() {
var out = [], i, a, seen = {}, dmg = false, maxp = 0;
var np = nameProblem();
if (np) out.push(['VR_CS_BAD_NAME', np]);
if (M.type < 0 || M.type >= S.types) out.push(['VR_CS_BAD_TYPE', 'Tipo no elegido.']);
for (i = 0; i < M.base.length; i++)
if (M.base[i] < S.stat.lo || M.base[i] > S.stat.hi)
out.push(['VR_CS_BAD_STAT', STAT_LABEL[i] + ' fuera de ' + S.stat.lo + '..' + S.stat.hi]);
var su = statUsed();
if (su < S.stat.min || su > S.stat.max)
out.push(['VR_CS_STAT_BUDGET',
'Total ' + su + ', la banda es ' + S.stat.min + '..' + S.stat.max + '.']);
if (M.moves.length !== S.moves)
out.push(['VR_CS_UNKNOWN_MOVE', 'Hacen falta ' + S.moves + ' ataques.']);
for (i = 0; i < M.moves.length; i++) {
if (seen[M.moves[i]]) out.push(['VR_CS_MOVE_REPEATED', 'Ataque repetido.']);
seen[M.moves[i]] = 1;
a = atkOf(M.moves[i]);
if (!a) { out.push(['VR_CS_UNKNOWN_MOVE', 'Ataque desconocido.']); continue; }
if (a[1] !== M.type && a[1] !== S.types)
out.push(['VR_CS_MOVE_OFF_TYPE', S.an[i] + ' no es de este tipo.']);
if (a[2] > 0) dmg = true;
if (a[2] > maxp) maxp = a[2];
}
if (M.moves.length === S.moves && !dmg)
out.push(['VR_CS_NO_DAMAGING_MOVE', 'Ning\u00fan ataque hace da\u00f1o.']);
if (maxp > S.powcap)
out.push(['VR_CS_POWER_CAP', 'Potencia m\u00e1xima ' + S.powcap + '.']);
var au = atkUsed();
if (au > S.atkbudget)
out.push(['VR_CS_ATTACK_BUDGET', 'Coste ' + au + ' sobre ' + S.atkbudget + '.']);
if (SE.anyEmpty())
out.push(['-', 'Alg\u00fan fotograma del sprite est\u00e1 vac\u00edo.']);
return out;
}
function drawBudget() {
if (!S) return;
var su = statUsed(), au = atkUsed(), p = pct(su, au);
var over = su > S.stat.max || au > S.atkbudget;
$('budget').hidden = (step < 3);
$('budget').className = over ? 'over' : '';
txt('budget-pct', p + ' %');
$('budget-fill').style.width = Math.min(100, p) + '%';
txt('budget-sub', 'estad\u00edsticas ' + su + '/' + S.stat.max +
'  \u00b7  ataques ' + au + '/' + S.atkbudget);
}
function stepReady(i) {
if (i === 0) return linked;
if (i === 1) return nameProblem() === null;
if (i === 2) return M.type >= 0 && M.type < S.types;
if (i === 3) { var s = statUsed(); return s >= S.stat.min && s <= S.stat.max; }
if (i === 4) return !SE.anyEmpty();
if (i === 5) {
if (M.moves.length !== S.moves) return false;
var k, a;
for (k = 0; k < M.moves.length; k++) { a = atkOf(M.moves[k]); if (a && a[2] > 0) return true; }
return false;
}
return true;
}
function show(i) {
if (i < 0) i = 0;
if (i > STEPS.length - 1) i = STEPS.length - 1;
step = i;
var k;
for (k = 0; k < STEPS.length; k++) $(STEPS[k][0]).hidden = (k !== i);
var lis = $('steps').children;
for (k = 0; k < lis.length; k++)
lis[k].className = (k < i) ? 'done' : (k === i ? 'now' : '');
txt('steps-name', STEPS[i][1]);
$('btn-back').disabled = (i === 0);
$('btn-next').disabled = (i === STEPS.length - 1) || !stepReady(i);
drawBudget();
if (previewTimer) { clearInterval(previewTimer); previewTimer = 0; }
if (i === 4) SE.paint();
if (i === 5) renderAttacks();
if (i === 6) renderLocal();
if (i === 7) startPreview();
window.scrollTo(0, 0);
}
function refresh() {
drawBudget();
$('btn-next').disabled = (step === STEPS.length - 1) || !stepReady(step);
}
function doConnect() {
var v = $('pin').value.replace(/[^0-9]/g, '');
if (v.length !== 4) { txt('pin-msg', 'El PIN tiene cuatro cifras.'); return; }
pin = v;
$('btn-pin').disabled = true;
req('GET', '/api/state').then(function (r) {
$('btn-pin').disabled = false;
var bad = refusal(r);
if (bad) { pin = ''; linked = false; txt('pin-msg', bad); return; }
linked = true;
txt('pin-msg', '');
var d = r.j;
$('devbox').hidden = false;
txt('d-name', d.name || '\u2014');
txt('d-fw', d.fw + '  (API ' + d.v + ')');
txt('d-box', d.box.free + ' de ' + (d.box.free + d.box.used));
txt('d-cs', d.cs.free + ' de ' + (d.cs.free + d.cs.used));
txt('d-clock', ['sin hora', 'estimada', 'puesta a mano', 'del tel\u00e9fono'][d.cal] || '?');
txt('hdr-dev', d.name || '');
if (d.ro) toast(MSG.ro, 'bad');
if (d.v !== API_EXPECTED)
toast('Esta p\u00e1gina es de otra versi\u00f3n del dispositivo (' +
API_EXPECTED + ' contra ' + d.v + '). Recarga.', 'bad');
if (d.box.free === 0) toast(MSG.boxfull, 'bad');
else if (d.cs.free === 0) toast(MSG.csfull, 'bad');
startPing();
refresh();
}).catch(function () {
$('btn-pin').disabled = false;
txt('pin-msg', 'No se pudo hablar con el dispositivo.');
});
}
function startPing() {
if (pingTimer) clearInterval(pingTimer);
pingTimer = setInterval(function () {
req('POST', '/api/ping').then(function (r) {
if (r.status === 403) { linked = false; toast(say(r.j && r.j.err), 'bad'); }
}).catch(function () {});
}, 60000);
}
function doClock() {
var body = '{"v":' + API_EXPECTED + ',"epoch":' +
Math.floor(Date.now() / 1000) + '}';
req('POST', '/api/time', body).then(function (r) {
var bad = refusal(r);
if (bad) { toast(bad, 'bad'); return; }
txt('d-clock', ['sin hora', 'estimada', 'puesta a mano', 'del tel\u00e9fono'][r.j.cal] || '?');
toast(r.j.ok ? 'Reloj sincronizado.' : 'El dispositivo no acept\u00f3 la hora.',
r.j.ok ? 'ok' : 'bad');
}).catch(function () { toast('No se pudo hablar con el dispositivo.', 'bad'); });
}
function onName() {
M.name = $('name').value;
var cps = Array.from(M.name);
txt('name-count', cps.length + ' / ' + S.name.max);
var p = nameProblem();
txt('name-msg', p || 'Correcto.');
$('name-msg').className = 'hint ' + (p ? 'bad' : 'ok');
refresh();
}
function renderTypes() {
var box = $('typebox'), i;
box.innerHTML = '';
for (i = 0; i < S.types; i++) {
var b = document.createElement('button');
b.type = 'button';
b.style.width = '100%';
b.style.marginBottom = '8px';
b.textContent = S.tn[i];
b.className = (i === M.type) ? 'sel' : '';
b.setAttribute('data-t', String(i));
b.addEventListener('click', function () {
var t = parseInt(this.getAttribute('data-t'), 10);
if (t === M.type) return;
M.type = t;
if (M.moves.length) { M.moves = []; toast('Los ataques se han vaciado.'); }
renderTypes();
refresh();
});
box.appendChild(b);
}
}
function renderStats() {
var box = $('statbox'), i;
box.innerHTML = '';
for (i = 0; i < M.base.length; i++) {
(function (k) {
var wrap = document.createElement('div');
wrap.className = 'stat';
var lab = document.createElement('div');
lab.className = 'lab';
var l = document.createElement('span'); l.textContent = STAT_LABEL[k];
var v = document.createElement('span'); v.className = 'val'; v.id = 'sv' + k;
v.textContent = String(M.base[k]);
lab.appendChild(l); lab.appendChild(v);
var r = document.createElement('input');
r.type = 'range'; r.min = String(S.stat.lo); r.max = String(S.stat.hi);
r.step = '1'; r.value = String(M.base[k]);
r.setAttribute('aria-label', STAT_LABEL[k]);
r.addEventListener('input', function () {
M.base[k] = parseInt(r.value, 10);
txt('sv' + k, String(M.base[k]));
updateStatMsg();
refresh();
});
wrap.appendChild(lab); wrap.appendChild(r);
box.appendChild(wrap);
})(i);
}
txt('stat-band', S.stat.min + ' .. ' + S.stat.max);
updateStatMsg();
}
function updateStatMsg() {
var s = statUsed();
txt('stat-total', String(s));
var m = $('stat-msg');
if (s < S.stat.min) {
m.textContent = 'Faltan ' + (S.stat.min - s) + ' puntos.'; m.className = 'hint bad';
} else if (s > S.stat.max) {
m.textContent = 'Sobran ' + (s - S.stat.max) + ' puntos.'; m.className = 'hint bad';
} else {
m.textContent = 'Dentro de la banda.'; m.className = 'hint ok';
}
}
function renderAttacks() {
var list = $('atklist'), i;
list.innerHTML = '';
var au = atkUsed();
for (i = 0; i < S.atk.length; i++) {
(function (a, name) {
if (a[1] !== M.type && a[1] !== S.types) return;
var picked = M.moves.indexOf(a[0]) >= 0;
var b = document.createElement('button');
b.type = 'button';
b.className = 'atk' + (picked ? ' sel' : '');
var over = (!picked && a[2] > S.powcap) ||
(!picked && au + a[4] > S.atkbudget);
b.disabled = over || (!picked && M.moves.length >= S.moves);
var n = document.createElement('span');
n.className = 'an'; n.textContent = name;
var m = document.createElement('span');
m.className = 'am';
m.textContent = (a[2] ? 'POT ' + a[2] : 'APOYO') + '  ' + a[3] + '%  ' +
'C' + a[4];
b.appendChild(n); b.appendChild(m);
b.addEventListener('click', function () {
var at = M.moves.indexOf(a[0]);
if (at >= 0) M.moves.splice(at, 1);
else if (M.moves.length < S.moves) M.moves.push(a[0]);
renderAttacks();
refresh();
});
list.appendChild(b);
})(S.atk[i], S.an[i]);
}
txt('atk-count', M.moves.length + ' / ' + S.moves);
txt('atk-cost', 'coste ' + au + ' / ' + S.atkbudget);
var dmg = false, k;
for (k = 0; k < M.moves.length; k++) { var a2 = atkOf(M.moves[k]); if (a2 && a2[2] > 0) dmg = true; }
var msg = $('atk-msg');
if (M.moves.length < S.moves) {
msg.textContent = 'Elige ' + (S.moves - M.moves.length) + ' m\u00e1s.';
msg.className = 'hint';
} else if (!dmg) {
msg.textContent = 'Al menos uno tiene que hacer da\u00f1o.'; msg.className = 'hint bad';
} else {
msg.textContent = 'Conjunto v\u00e1lido para esta p\u00e1gina. Lo decide el dispositivo.';
msg.className = 'hint ok';
}
}
function renderLocal() {
var p = localProblems();
txt('local-sum', p.length ? (p.length + ' aviso(s)') : 'sin avisos');
$('local-sum').className = p.length ? 'bad' : 'ok';
var l = $('local-list'), i;
l.innerHTML = '';
for (i = 0; i < p.length; i++) {
var d = document.createElement('div');
d.textContent = p[i][1] + '  [' + p[i][0] + ']';
l.appendChild(d);
}
}
function body() {
var n = M.name, i;
for (i = 0; i < n.length; i++) {
var c = n.charCodeAt(i);
if (c === 0x22 || c === 0x5c || c < 0x20)
throw new Error('the name carries a character the device reader cannot take');
}
var hexes = SE.allHex(), sp = [];
for (i = 0; i < hexes.length; i++) sp.push('"' + hexes[i] + '"');
return '{"v":' + API_EXPECTED +
',"name":"' + n + '"' +
',"type":' + M.type +
',"base":[' + M.base.join(',') + ']' +
',"moves":[' + M.moves.join(',') + ']' +
',"sprite":[' + sp.join(',') + ']}';
}
function doValidate() {
var v = $('verdict');
var b;
try { b = body(); } catch (e) { v.innerHTML = ''; v.textContent = String(e.message); return; }
$('btn-validate').disabled = true;
req('POST', '/api/validate', b).then(function (r) {
$('btn-validate').disabled = false;
v.innerHTML = '';
var bad = refusal(r);
var head = document.createElement('div');
var code = document.createElement('div');
code.className = 'code';
if (bad) {
head.className = 'bad';
head.textContent = bad;
code.textContent = 'HTTP ' + r.status + '  ' + codeOf(r);
} else {
head.className = 'ok';
head.textContent = 'El dispositivo lo acepta. Presupuesto ' + r.j.pct + ' %.';
code.textContent = 'estad\u00edsticas ' + r.j.stat + '  ataques ' + r.j.atk +
'  huecos caja ' + r.j.box + '  huecos especie ' + r.j.cs;
var mine = pct(statUsed(), atkUsed());
if (mine !== r.j.pct)
toast('La barra dec\u00eda ' + mine + ' % y el dispositivo dice ' +
r.j.pct + ' %. Manda el dispositivo.', 'bad');
}
v.appendChild(head); v.appendChild(code);
}).catch(function () {
$('btn-validate').disabled = false;
v.textContent = 'No se pudo hablar con el dispositivo.';
});
}
function startPreview() {
var box = $('sumbox'), f = 0;
box.innerHTML = '';
function kv(k, val) {
var d = document.createElement('div'); d.className = 'kv';
var a = document.createElement('span'); a.textContent = k;
var b = document.createElement('span'); b.textContent = val;
d.appendChild(a); d.appendChild(b); box.appendChild(d);
}
kv('Nombre', M.name);
kv('Tipo', S.tn[M.type]);
var i, names = [];
for (i = 0; i < M.base.length; i++) names.push(STAT_LABEL[i] + ' ' + M.base[i]);
kv('Estad\u00edsticas', names.join('  '));
for (i = 0; i < M.moves.length; i++) {
var idx = -1, k;
for (k = 0; k < S.atk.length; k++) if (S.atk[k][0] === M.moves[i]) idx = k;
kv('Ataque ' + (i + 1), idx >= 0 ? S.an[idx] : String(M.moves[i]));
}
kv('Presupuesto', pct(statUsed(), atkUsed()) + ' %');
previewTimer = setInterval(function () {
f = (f + 1) % SE.frames();
SE.blit($('q1'), f, 1);
SE.blit($('q2'), f, 2);
}, 480);
SE.blit($('q1'), 0, 1);
SE.blit($('q2'), 0, 2);
}
function doUpload() {
var m = $('upload-msg'), b;
try { b = body(); } catch (e) { m.textContent = String(e.message); return; }
$('btn-upload').disabled = true;
req('POST', '/api/pebble', b).then(function (r) {
var bad = refusal(r);
if (bad) {
$('btn-upload').disabled = false;
m.className = 'bad';
m.textContent = bad + ' [HTTP ' + r.status + ' ' + codeOf(r) + ']';
return;
}
m.className = 'ok';
m.textContent = 'Creado. El dispositivo no escribe nada hasta que las dos ' +
'mitades son buenas, as\u00ed que esto ya est\u00e1 guardado.';
$('donebox').hidden = false;
txt('u-slot', String(r.j.slot));
txt('u-cs', String(r.j.cs));
txt('u-species', String(r.j.species));
txt('u-id', String(r.j.id));
txt('u-pct', r.j.pct + ' %');
}).catch(function () {
$('btn-upload').disabled = false;
m.textContent = 'No se pudo hablar con el dispositivo.';
});
}
function buildSteps() {
var ol = $('steps'), i;
for (i = 0; i < STEPS.length; i++) ol.appendChild(document.createElement('li'));
}
function wire() {
on('btn-pin', doConnect);
on('btn-clock', doClock);
on('btn-back', function () { show(step - 1); });
on('btn-next', function () { if (stepReady(step)) show(step + 1); });
on('btn-validate', doValidate);
on('btn-upload', doUpload);
$('pin').addEventListener('keydown', function (e) { if (e.key === 'Enter') doConnect(); });
$('name').addEventListener('input', onName);
$('name').setAttribute('maxlength', String(S.name.max));
on('f0', function () { SE.setFrame(0); frameTabs(); });
on('f1', function () { SE.setFrame(1); frameTabs(); });
on('fcopy', function () { SE.copyFrame(0, 1); toast('Fotograma 1 copiado al 2.'); });
on('t-draw',  function () { SE.setTool('draw');  toolTabs(); });
on('t-erase', function () { SE.setTool('erase'); toolTabs(); });
on('t-fill',  function () { SE.setTool('fill');  toolTabs(); });
on('t-undo',  function () { if (!SE.undo()) toast('Nada que deshacer.'); });
on('t-clear', function () { SE.clear(); });
on('t-fliph', function () { SE.flipH(); });
on('t-flipv', function () { SE.flipV(); });
}
function frameTabs() {
$('f0').className = 'sq' + (SE.frame() === 0 ? ' sel' : '');
$('f1').className = 'sq' + (SE.frame() === 1 ? ' sel' : '');
afterDraw();
}
function toolTabs() {
var t = SE.tool();
$('t-draw').className  = (t === 'draw')  ? 'sel' : '';
$('t-erase').className = (t === 'erase') ? 'sel' : '';
$('t-fill').className  = (t === 'fill')  ? 'sel' : '';
}
function afterDraw() {
SE.blit($('p1'), SE.frame(), 1);
SE.blit($('p2'), SE.frame(), 2);
txt('pad-on', SE.lit(SE.frame()) + ' px');
refresh();
}
function boot() {
req('GET', '/api/schema').then(function (r) {
if (r.status !== 200 || !r.j) {
txt('pin-msg', 'El dispositivo no sirvi\u00f3 el esquema.');
return;
}
S = r.j;
if (S.v !== API_EXPECTED)
toast('Esta p\u00e1gina es de otra versi\u00f3n del dispositivo. Recarga.', 'bad');
var i;
M.base = [];
for (i = 0; i < S.sprite.f * 0 + 4; i++) M.base.push(S.stat.lo);
var spread = Math.floor(S.stat.min / M.base.length);
for (i = 0; i < M.base.length; i++) M.base[i] = spread;
i = 0;
while (statUsed() < S.stat.min) { M.base[i % M.base.length]++; i++; }
SE.geom(S.sprite.w, S.sprite.h, S.sprite.f);
SE.mount($('pad'), {
onChange: afterDraw,
onHover: function (x, y) {
txt('pad-xy', x < 0 ? '\u2014' : (x + ' , ' + y));
}
});
buildSteps();
wire();
renderTypes();
renderStats();
frameTabs();
toolTabs();
show(0);
$('pin').focus();
}).catch(function () {
txt('pin-msg', 'No se pudo hablar con el dispositivo.');
});
}
if (document.readyState === 'loading')
document.addEventListener('DOMContentLoaded', boot);
else boot();
window.PB = {
model: M,
schema: function () { return S; },
pct: pct,
body: body,
show: show,
setPin: function (p) { pin = p; },
problems: localProblems,
statUsed: statUsed,
atkUsed: atkUsed
};
})();
</script>
</body>
</html>
)PBHTML";

static const size_t INDEX_HTML_LEN = sizeof(INDEX_HTML) - 1;

static_assert(INDEX_HTML_API_VERSION == CREATOR_API_VERSION,
              "the committed creator page was generated against a different "
              "CREATOR_API_VERSION than this firmware speaks: run "
              "python3 tools/gen_index_html.py");
static_assert(sizeof(INDEX_HTML) - 1 <= WEB_HTML_MAX,
              "index_html.h blob exceeds WEB_HTML_MAX (48 KB, amendment A2)");

#endif // NT_INDEX_HTML_H
