#!/usr/bin/env bash
# Road Hopper review capture. Run from the repo root after ./native/build_headless.sh
#   tools/review/roadhopper.sh OUTDIR
# Deterministic: every frame comes from a --demo bot run with a fixed seed and a
# fixed save fixture (the game ignores the save file in demo mode).
#   demo 1  chicken, meadow world, ends under a car
#   demo 2  emu, outback world, ends under a train
#   demo 3  penguin, arctic world, ends in the river
#   demo 4  pumpkin, spooky world, idles until the eagle takes it
#   demo 5  the character roster
set -euo pipefail
out="${1:?usage: tools/review/roadhopper.sh OUTDIR}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
shots="$root/tools/shots.py"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$out"

grab() {   # grab DEMO TIME NAME
    python3 "$shots" render roadhopper "$tmp/$3" --demo "$1" --shots "$2" >/dev/null
    mv "$tmp/$3/roadhopper_00.png" "$out/$3.png"
}

grab 1 0.70  01_title
grab 1 3.10  02_early_meadow
grab 2 8.00  03_busy_outback
grab 2 13.30 04_train_signal
grab 3 9.62  05_arctic_splash
grab 1 12.62 06_squashed
grab 4 10.30 07_eagle
grab 1 14.40 08_results
grab 5 4.60  09_roster

# Motion strip: six frames 0.08 s apart - a near miss, a hop back into traffic, the hit.
strip="11.96,12.04,12.12,12.20,12.28,12.36"
python3 "$shots" render roadhopper "$tmp/strip" --demo 1 --shots "$strip" >/dev/null
python3 "$shots" sheet "$out/10_motion_strip.png" "$tmp"/strip/roadhopper_*.png --height 1170 >/dev/null

ls "$out"
