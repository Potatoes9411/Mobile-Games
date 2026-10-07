#!/usr/bin/env bash
# Block Storm review captures: tools/review/blockstorm.sh OUTDIR
# Run from the repo root after ./native/build_headless.sh. Deterministic: every
# frame comes from a pa_demo_mode() run with a fixed seed and a scripted bot.
#   --demo 1  bot plays from an empty board (first screen, early, mid-game)
#   --demo 2  a streak one move from a four-line clear (ghost preview, burst)
#   --demo 3  a record run one move from the end (no space, results, new best)
set -euo pipefail
out="${1:?usage: tools/review/blockstorm.sh OUTDIR}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
mkdir -p "$out"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

shots() { python3 tools/shots.py render blockstorm "$tmp/$1" --demo "$2" --shots "$3" >/dev/null; }

shots a 1 0.70,7.00,45.00
shots b 2 1.45,1.90
shots c 3 3.30,6.70
shots s 2 1.66,1.74,1.82,1.90,1.98,2.06

cp "$tmp/a/blockstorm_00.png" "$out/01_first_screen.png"
cp "$tmp/a/blockstorm_01.png" "$out/02_early_play.png"
cp "$tmp/a/blockstorm_02.png" "$out/03_busy_midgame.png"
cp "$tmp/b/blockstorm_00.png" "$out/04_drag_ghost_preview.png"
cp "$tmp/b/blockstorm_01.png" "$out/05_four_line_clear_combo.png"
cp "$tmp/c/blockstorm_00.png" "$out/06_no_more_space.png"
cp "$tmp/c/blockstorm_01.png" "$out/07_results_new_best.png"
python3 tools/shots.py sheet "$out/08_motion_strip.png" "$tmp"/s/blockstorm_0*.png --height 1170 >/dev/null
ls "$out"
