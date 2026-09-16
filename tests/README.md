# Native contour edge regression

`contour-aa.cpp` renders the production fragment shader in an offscreen OpenGL ES context. A 54-pixel circle uses the launcher's antialiased alpha and shadow, with opaque white glass over black. The check requires fractional edge coverage, an opaque interior, no pixels beyond the silhouette margin, and low error against an analytic circle edge. It catches the former hard alpha cutoff (8 partially covered edge pixels; corrected: 70).

With Qt 6 development packages and a running Wayland session:

```sh
c++ -std=c++20 -O2 tests/contour-aa.cpp -o /tmp/hyprglass-contour-aa $(pkg-config --cflags --libs Qt6Gui)
QT_QPA_PLATFORM=wayland /tmp/hyprglass-contour-aa
```

Exit code 0 means the coverage checks passed; 1 means a rendering regression; 2 means the required graphics context or shader could not be created. This test does not load or replace a compositor plugin.
