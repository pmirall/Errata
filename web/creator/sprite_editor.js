// =============================================================================
//  PEBBLEBOL CREATOR - THE SPRITE EDITOR (P8-C4, spec section 37, plan T13).
//
//  24 x 24, TWO frames, one bit per pixel. It owns the pixels and nothing
//  else: it does not know what a Pebble is, it never talks to the device, and
//  the only thing it hands out is hex.
//
//  ==========================================================================
//  THE BYTE ORDER IS XBM's AND IT IS THE HALF THAT CANNOT BE "NEARLY RIGHT"
//  ==========================================================================
//  The device stores CustomSpeciesRec.sprite as a 24x24 XBM frame: rows padded
//  to whole bytes, so 3 bytes per row and 72 bytes per frame, and inside each
//  byte the LOW bit is the LEFTMOST pixel. That is u8g2's drawXBM layout and
//  ui/xbm_mirror.h's xbm_stride(); getting the bit order backwards produces a
//  sprite that decodes, validates, stores and renders as a horizontal mirror
//  of every group of eight pixels - a defect that looks like art until someone
//  draws a letter. tools/page_test.mjs paints known cells in a real browser
//  and hands the exported body to the REAL device decoder, which is the only
//  place the two ends of this sentence can be compared.
//
//  ==========================================================================
//  THE PALETTE RULE OF SPEC 35 IS THE FORMAT, NOT A CHECK
//  ==========================================================================
//  One bit per pixel means monochrome by construction: there is no palette to
//  bound, no transparency to normalise and no dimension to convert, which is
//  also why this editor accepts no image upload. Spec section 37 asks the
//  pipeline to normalise dimensions, palette, transparency and storage format;
//  a fixed 24x24 1bpp grid is all four of those, decided before the user draws.
//
//  ==========================================================================
//  TOUCH
//  ==========================================================================
//  Pointer events with capture, so a stroke that leaves the canvas keeps
//  painting until the thumb lifts, and Bresenham between samples so a fast
//  drag does not leave gaps. The canvas sets touch-action:none in the
//  stylesheet: the thumb paints, it never scrolls the page.
//
//  A 24-cell grid on a phone gives cells of roughly 13-16 CSS px. That is
//  smaller than the 44 px every BUTTON here respects and it cannot be made
//  bigger without dropping cells, so the mitigations are the ones that fit:
//  the grid takes the full width available, the cell under the thumb is
//  reported live as x,y (a thumb hides what it touches), and DESHACER is a
//  first-class tool rather than a menu item. Whether that is enough for a
//  thumb is a phone question and nobody in this build has held one.
//
//  Vanilla ES5-compatible JS, no framework, no CDN, no network. English
//  identifiers and comments; the Spanish the user reads lives in index.html.
// =============================================================================
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

  // ---------------------------------------------------------------------------
  //  GEOMETRY COMES FROM THE DEVICE, NEVER FROM A LITERAL HERE.
  //  app.js calls this with GET /api/schema's sprite block before the editor is
  //  ever shown. If the device's grid ever stops being 24x24x2 the editor
  //  follows it; a copy of "24" in this file would be a second source of truth
  //  and the record the device can hold is the only one that counts.
  // ---------------------------------------------------------------------------
  function geom(w, h, frames) {
    W = w; H = h; NF = frames;
    alloc();
    if (pad) { pad.width = W; pad.height = H; }
  }

  // ---- pixels --------------------------------------------------------------
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

  // ---- tools ---------------------------------------------------------------
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

  // Four-connected flood fill, iterative. A recursive one would be a stack
  // depth the page cannot bound; 576 cells fit an explicit list with room to
  // spare.
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

  // ---- the XBM export ------------------------------------------------------
  //  Row-major, xbm_stride(W) bytes per row, LSB of each byte is the LEFTMOST
  //  pixel of its group of eight. Lower-case hex; the device's reader accepts
  //  both cases and case is not a security property, but one case keeps the
  //  body byte-identical for the same drawing, which is what makes a recorded
  //  test vector mean something.
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

  // ---- rendering -----------------------------------------------------------
  //  Every canvas here is sized in DEVICE pixels equal to the sprite's own
  //  pixels and scaled up by CSS with image-rendering:pixelated. Drawing one
  //  fillRect per lit cell at display scale would blur on a high-DPI phone and
  //  cost 576 rects a frame; this costs one ImageData.
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

  // The editing grid: the pixels, then a one-device-pixel lattice so a thumb
  // can tell cells apart. Drawn at CSS resolution rather than sprite
  // resolution because the lattice has to land between cells.
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

  // ---- input ---------------------------------------------------------------
  function cellAt(ev) {
    var r = pad.getBoundingClientRect();
    var x = Math.floor((ev.clientX - r.left) / (r.width / W));
    var y = Math.floor((ev.clientY - r.top) / (r.height / H));
    if (x < 0) x = 0; if (x >= W) x = W - 1;
    if (y < 0) y = 0; if (y >= H) y = H - 1;
    return { x: x, y: y };
  }

  // Bresenham between two samples. A phone reports pointermove every frame at
  // best; at 24 cells across a fast diagonal drag skips three or four cells
  // between samples, and a line of holes is what the user sees.
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

  // ---- mount ---------------------------------------------------------------
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
    // One cell, and it PUSHES UNDO, because from the outside it is a stroke.
    // The private set() above is the one the tools use inside a stroke they
    // have already recorded; exporting that one would let a caller reach a
    // state DESHACER cannot take back, which is a state the UI cannot produce.
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
