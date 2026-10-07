#!/usr/bin/env bash
# Pin Rescue review captures. Run from the repo root after ./native/build_headless.sh:
#   tools/review/pins.sh OUTDIR
# Every frame comes from --demo self-play: demo N plays level N with its
# solution (the tutorial hand taps each pin), demo 200+N plays a losing order.
set -euo pipefail
out="${1:?usage: tools/review/pins.sh OUTDIR}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
shots="$root/tools/shots.py"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cd "$work"     # keep the headless save file out of the repo

grab() {  # grab NAME DEMO TIME
    python3 "$shots" render pins "$work/$1" --shots "$3" --demo "$2" > /dev/null
    cp "$work/$1/pins_00.png" "$out/$1.png"
}

grab 01_first_screen      0   0.30   # level 1 as a new player sees it: the hand grips the ring (then drags, 1.2 s loop)
grab 02_diagonal_pour     2   2.10   # a 45-degree pin slides out, the pile avalanches down the slope
grab 03_busy_l_tube       4   2.20   # offset L-shaped vessel: colour pours round the bend onto grey
grab 04_colour_wave       6   5.00   # twin flasks: colour lands on the grey hold and the wave spreads
grab 05_bombs_overhead    7   2.30   # bombs with lit sparks while the balls drain out beneath them
grab 06_bomb_blast      219   2.20   # wrong order: the blast scorches the caught balls grey
grab 07_level_complete    1   4.70   # cup full, confetti, NEXT
grab 08_level_failed    204   3.20   # grey ball reached the cup

# a meta panel opened from the HUD bar (ball skins)
python3 "$shots" render pins "$work/panel" --shots 1.2 --demo 5 --input "0.5:tap:0.729f:39" > /dev/null
cp "$work/panel/pins_00.png" "$out/09_skins_panel.png"

# motion strip: six frames 0.08 s apart as the full cup overflows and spills
python3 "$shots" render pins "$work/strip" --shots 2.80,2.88,2.96,3.04,3.12,3.20 --demo 1 > /dev/null
python3 "$shots" sheet "$out/10_motion_strip.png" "$work"/strip/pins_0*.png --height 1170 > /dev/null

ls "$out"
