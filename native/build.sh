#!/usr/bin/env bash
# Cross-compiles the native Pocket Arcade for Windows x64 from Linux.
# No browser, no runtime, no engine dependency - one static executable.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
out="$here/../dist/PocketArcadeNative.exe"
cc=${CC:-x86_64-w64-mingw32-gcc}

sources=(
  "$here/src/raster.c"
  "$here/src/font.c"
  "$here/src/audio.c"
  "$here/src/hub.c"
  "$here/src/platform_win32.c"
)
for f in "$here"/src/games/*.c; do sources+=("$f"); done

echo "==> Compiling ${#sources[@]} translation units"
mkdir -p "$(dirname "$out")"

# -static so the exe runs on a clean Windows box with no mingw runtime present.
"$cc" -O2 -Wall -Wextra -std=c99 \
  -o "$out" "${sources[@]}" \
  -lgdi32 -luser32 -lwinmm -lm \
  -static -static-libgcc \
  -Wl,--subsystem,console

ls -la "$out"
echo "==> Done: dist/$(basename "$out")"
