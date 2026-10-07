#!/usr/bin/env bash
# Builds the headless capture binary for Linux/macOS: the real hub and games,
# no window, scripted input in, frames out. Used for every visual review.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${OUT:-$here/out/pa_headless}"
mkdir -p "$(dirname "$out")"
sources=("$here/src/raster.c" "$here/src/font.c" "$here/src/audio.c" "$here/src/save.c"
         "$here/src/hub.c" "$here/src/meta.c" "$here/src/platform_headless.c")
for f in "$here"/src/games/*.c; do sources+=("$f"); done
${CC:-cc} -O2 -Wall -Wextra -std=gnu99 -o "$out" "${sources[@]}" -lm
echo "$out"
