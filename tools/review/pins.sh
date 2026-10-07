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

grab 01_first_screen     1   1.15   # level 1: the hand shows which pin to pull
grab 02_early_pour       1   2.25   # first pin out, balls pouring into the cup
grab 03_busy_mid_game   10   5.00   # two wells of fresh colour pour into the cup
grab 04_colour_wave      9   2.05   # grey balls take colour on contact, a wave through the pile
grab 05_bombs_overhead    8   2.30   # bombs with lit sparks while the balls drain out beneath them
grab 06_bomb_blast      217   2.08   # wrong order: colour lands on the bombs, the first one blows
grab 07_level_complete    1   4.70   # cup full, confetti, NEXT
grab 08_level_failed    203   3.10   # grey ball reached the cup

# motion strip: six frames 0.08 s apart while the pegs scatter the pour
python3 "$shots" render pins "$work/strip" --shots 2.00,2.08,2.16,2.24,2.32,2.40 --demo 5 > /dev/null
python3 "$shots" sheet "$out/09_motion_strip.png" "$work"/strip/pins_0*.png --height 1170 > /dev/null

ls "$out"
