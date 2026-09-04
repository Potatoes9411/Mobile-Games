# Pocket Arcade — native build

A real Windows executable. No browser, no embedded webview, no scripting
runtime: a Win32 window, a software rasterizer, a WinMM tone synth, and the
games compiled in.

```
./native/build.sh            # -> dist/PocketArcadeNative.exe
```

Cross-compiles from Linux with `x86_64-w64-mingw32-gcc`, linked `-static` so it
runs on a clean Windows box with no mingw runtime present.

## Layout

| File | What it owns |
| --- | --- |
| `src/pa.h` | the whole public surface: math, colour, canvas, paints, primitives, text, input, audio, the game interface |
| `src/raster.c` | the software rasterizer |
| `src/font.c` | the stroke font |
| `src/audio.c` | WinMM streaming mixer and the synth |
| `src/platform_win32.c` | window, frame clock, input, `StretchDIBits` present, headless capture |
| `src/hub.c` | the shell: card grid, launch/exit, pause |
| `src/games/*.c` | one file per game |

## The rasterizer

One scanline polygon filler with the non-zero winding rule and analytic coverage
antialiasing does all the work; circles, rounded rectangles, strokes and glyphs
are flattened to polygons and pushed through it, so exactly one piece of code
has to be correct about edges. Each pixel row is sampled at five sub-scanlines
and every sub-scanline contributes exact horizontal coverage, which is far
cheaper than supersampling the whole framebuffer and keeps thin geometry from
crawling when the camera moves a fraction of a pixel.

Gradients are evaluated per pixel inside the span loop rather than resolved into
a temporary surface, so a gradient fill costs about the same as a flat one.

Axis-aligned opaque rectangles — by far the most common call — skip the scanline
machinery entirely and go straight to a row memset.

## Verifying without a screen

The binary can render headlessly and write a BMP, which is how it is checked
from a Linux box with no Windows and no display:

```
wine dist/PocketArcadeNative.exe --shot out.bmp 40        # 40 frames, then capture
wine dist/PocketArcadeNative.exe --play 0 --shot g.bmp 30 # launch game 0 first
wine dist/PocketArcadeNative.exe --size 720 1280          # windowed, custom size
```

It prints the frame count and elapsed time, so a performance regression shows up
in the same run as a visual one.

## Two bugs worth remembering

`pa_mix` originally wrote `PA_R(b) - (int)PA_R(a)`. The extraction macros yield
*unsigned*, so the whole expression promoted to unsigned and any darkening step
wrapped to about four billion; the float-to-int conversion of that is undefined.
It landed somewhere harmless under one compiler and produced rainbow scanlines
under another. Every gradient in the engine was a coin flip. Both channels are
pulled out as `int` before subtracting now.

The rasterizer's edge list is file static, not automatic. At 80KB a frame it
relied on Windows growing the stack correctly through guard-page probing from a
deep call chain, and the failure mode there is silent garbage rather than a
crash.

## Porting a game

The drawing API deliberately mirrors the shape of the Canvas 2D calls the
browser build uses, so a game's rendering code ports as a mechanical translation
rather than a redesign. A game is a `PA_Game` with `start`, `stop`, `update`,
`render` and `thumb`; add the file to `src/games/`, add an `extern` and a table
entry in `hub.c`, and the grid picks it up.
