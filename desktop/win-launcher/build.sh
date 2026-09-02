#!/usr/bin/env bash
# Cross-compiles the Windows launcher from Linux or macOS.
#   sudo apt-get install mingw-w64      (Debian/Ubuntu)
#   brew install mingw-w64              (macOS)
# On Windows, use MSYS2 and swap the compiler for gcc.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
out="$here/../../dist"
mkdir -p "$out"

echo "==> Embedding web/index.html"
node "$here/embed.js"

echo "==> Compiling MobClashGateSiege.exe"
x86_64-w64-mingw32-gcc \
  "$here/main.c" \
  -o "$out/MobClashGateSiege.exe" \
  -O2 -s -mwindows -DUNICODE -D_UNICODE \
  -I"$here" \
  -lshell32

ls -lh "$out/MobClashGateSiege.exe"
echo "==> Done: dist/MobClashGateSiege.exe"
