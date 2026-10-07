#!/usr/bin/env bash
# Review captures for Void Muncher (Hole.io reference).
#   ./native/build_headless.sh && tools/review/voidmuncher.sh OUTDIR
# One deterministic demo round (--demo 1: a bot steers the player hole and
# hunts props, one rival is kept working the blocks beside it). The run starts
# in a fresh temp directory so no save file can change the city or the best.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:?usage: tools/review/voidmuncher.sh OUTDIR}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cd "$work"

# Start screen, early play, a cab mid-tilt into the hole, a rival alongside,
# a rival being swallowed (kill), a building going down mid-round, the grown
# hole late with the camera pulled out, the results ranking.
python3 "$root/tools/shots.py" render voidmuncher "$work/a" --demo 1 --size 540 1170 \
    --shots 0.6,4.0,8.3,14.0,17.7,60.0,70.0,124.5 > /dev/null
names=(01_start 02_early_play 03_swallow_car_tipping 04_rival_alongside 05_kill_rival \
       06_busy_mid_round 07_grown_hole_late 08_results_ranking)
for i in "${!names[@]}"; do
    mv "$work/a/voidmuncher_0$i.png" "$out/${names[$i]}.png"
done

# Motion strip: the same cab going over the lip, 0.08 s apart.
python3 "$root/tools/shots.py" render voidmuncher "$work/m" --demo 1 --size 540 1170 \
    --shots 8.0,8.08,8.16,8.24,8.32,8.40 > /dev/null
python3 "$root/tools/shots.py" sheet "$out/09_motion_strip.png" "$work"/m/voidmuncher_0*.png \
    --height 900 > /dev/null
ls -1 "$out"/*.png
