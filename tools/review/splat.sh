#!/usr/bin/env bash
# Roller Splat review captures. Run from the repo root after
# ./native/build_headless.sh:   tools/review/splat.sh OUTDIR
#
# Every run uses --demo N, which starts on level N with the greedy solver
# playing, so the frames are real play and fully deterministic.
#   01  first screen        level 1, sky world, swipe hint
#   02  early play          level 1, mid roll, wet paint at the ball
#   03  busy mid-game       level 15, ice world, dense 13x21 board mid-solve
#   04  signature mechanic  level 8, mustard world, a long roll spattering paint
#   05  level complete      level 17, lawn world, splat badge and confetti
#   06-10 motion strip      level 8, one 12-tile roll into a wall, 0.08 s apart
set -euo pipefail
out="${1:?usage: tools/review/splat.sh OUTDIR}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
bin="$root/native/out/pa_headless"
[ -x "$bin" ] || { echo "build first: ./native/build_headless.sh" >&2; exit 1; }
mkdir -p "$out"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# shoot DEMO_LEVEL "t1,t2,..." name1 name2 ...
shoot() {
    local demo="$1" times="$2"; shift 2
    rm -rf "$tmp/run"; mkdir -p "$tmp/run"
    python3 "$root/tools/shots.py" render splat "$tmp/run" --demo "$demo" --shots "$times" >/dev/null
    local i=0
    for name in "$@"; do
        mv "$tmp/run/splat_$(printf %02d "$i").png" "$out/$name.png"
        i=$((i + 1))
    done
}

shoot 1  "0.75,2.95"   01_first_screen 02_early_play
shoot 15 "9.25"        03_busy_midgame
shoot 8  "6.30"        04_signature_spatter
shoot 17 "21.75"       05_level_complete
shoot 8  "5.23,5.31,5.39,5.47,5.55" \
    06_motion_a 07_motion_b 08_motion_c 09_motion_d 10_motion_e
ls "$out"/*.png
