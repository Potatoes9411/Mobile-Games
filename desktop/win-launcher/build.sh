#!/usr/bin/env bash
# Cross-compiles the Windows launcher from Linux or macOS.
#   sudo apt-get install mingw-w64      (Debian/Ubuntu)
#   brew install mingw-w64              (macOS)
# On Windows, use MSYS2 and swap the compiler for gcc.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
out="$here/../../dist"
mkdir -p "$out"

echo "==> Bundling the arcade"
node "$here/../../web/build-single.js"

echo "==> Embedding dist/pocket-arcade.html"
node "$here/embed.js"

echo "==> Compiling PocketArcade.exe"
x86_64-w64-mingw32-gcc \
  "$here/main.c" \
  -o "$out/PocketArcade.exe" \
  -O2 -s -mwindows -DUNICODE -D_UNICODE \
  -I"$here" \
  -lshell32

ls -lh "$out/PocketArcade.exe"
echo "==> Done: dist/PocketArcade.exe"
