// index_html.h - the page the creator server serves at GET /.
//
// PLACEHOLDER. The Nottamagochi phone app (a 46,663 B generated blob holding a
// full pet dashboard, three browser minigames and a settings form) was removed
// with the rest of that product surface; the real Pebblebol creator page is
// built from committed source in Phase 8. Until then this file keeps the
// container - INDEX_HTML, INDEX_HTML_LEN and the WEB_HTML_MAX budget assert -
// so webui.cpp keeps compiling and a phone that scans the QR code gets a page
// that says what it is looking at instead of a connection error.
//
// INCLUDE THIS FROM webui.cpp ONLY. INDEX_HTML has internal linkage (const at
// namespace scope), so every translation unit that *uses* it keeps its own copy
// in .rodata; -Wl,--gc-sections only drops the copies nobody references.
//
// Serve it with the FOUR-argument send_P (the 3-arg form strlen_P()s the blob
// and would be wrong the moment a NUL ever appears):
//   server.sendHeader(F("Cache-Control"), F("no-cache"));
//   server.send_P(200, PSTR("text/html; charset=utf-8"), INDEX_HTML, INDEX_HTML_LEN);
//
// The page consumes no endpoint: the server has no API yet.
#ifndef NT_INDEX_HTML_H
#define NT_INDEX_HTML_H

#include <Arduino.h>
#include "../core/config.h"

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#1B1D19">
<title>Pebblebol creator &mdash; Phase 8</title>
<style>
:root{color-scheme:dark}
html,body{margin:0;height:100%}
body{background:#1B1D19;color:#E7E9E3;
 font:16px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
 display:flex;align-items:center;justify-content:center;padding:24px}
main{max-width:26rem;text-align:center}
h1{font-size:1.35rem;margin:0 0 .6rem;letter-spacing:.02em}
p{margin:.6rem 0;color:#A8AFA2}
code{color:#E7E9E3;background:#262A24;border-radius:4px;padding:.1em .4em}
</style>
</head><body>
<main>
<h1>Pebblebol creator &mdash; Phase 8</h1>
<p>The device is reachable and the local server is running.</p>
<p>The Pebble editor is not built yet. It arrives with the creator API;
until then this page is all there is to see.</p>
<p>Device screen: <code>SCAN ME</code></p>
</main>
</body></html>
)HTML";

static const size_t INDEX_HTML_LEN = sizeof(INDEX_HTML) - 1;

static_assert(sizeof(INDEX_HTML) - 1 <= WEB_HTML_MAX,
              "index_html.h blob exceeds WEB_HTML_MAX (48 KB, amendment A2)");

#endif // NT_INDEX_HTML_H
