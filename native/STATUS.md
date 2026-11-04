# Native host: current state

This directory is a **1:1 host for `Spaceship.hta`**. It does not reimplement the
game. It parses the original document, runs the original `<script>` unmodified on
an embedded ES5 interpreter (MuJS), and renders the live DOM with a layout and
paint engine driven by the original stylesheet.

Nothing in `Spaceship.hta` is copied, rewritten, simplified or "improved".

## What works (verified on the real 706 KB document)

| Stage | Status |
|---|---|
| Document parse (706,020 bytes, body markup) | working |
| Stylesheet parse (263,794 bytes, 660 rules) | working |
| Style resolution / cascade over the real DOM | working |
| Layout engine | working |
| DOM + CSS + style + layout pipeline, end to end | working |
| MuJS state + browser bridge install | working |
| Painting / PNG capture | working |
| **Running the game script** | **not yet** — aborts inside MuJS error reporting |

## The one open defect

`Engine::Impl::runScript` aborts instead of reporting an error. The abort comes
from MuJS raising a JS error while no `setjmp` frame is active
(`js_newevalerror` -> `abort`), so it is a host-side exception-frame bug, not a
bug in `Spaceship.hta` and not a missing browser API.

Every allocation-capable call is already wrapped (`js_ploadstring`, the bridge
install), so the remaining frame is inside one of the native callbacks reached
during the first script turn.

## Build

MuJS and stb are vendored (ISC / public domain). Build the headless driver with:

    gcc -std=c99 -O1 -c vendor/mujs/*.c
    g++ -std=c++17 -O1 -I src -I vendor/mujs -I vendor/stb \
        src/*.cpp tools/headless_main.cpp *.o -lm -o headless
    ./headless --input ../Spaceship.hta --capture frame.png

`src/canvas_win.cpp` is Windows-only and is replaced by `src/canvas_null.cpp`
for the headless build.
