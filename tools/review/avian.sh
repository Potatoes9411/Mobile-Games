#!/usr/bin/env bash
# Review captures for Avian Artillery (landscape, 1170x540).
# Usage: tools/review/avian.sh OUTDIR   (run from the repo root after ./native/build_headless.sh)
#
# Demo modes used (pa_demo_mode): 1 = title screen, 300 = level map with a
# part-played campaign, 100+L = level L with the auto-aim bot playing.
# Every run happens in a fresh temp dir so no save file leaks between runs.
set -euo pipefail
out="${1:?usage: tools/review/avian.sh OUTDIR}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cd "$work"

shot() {   # shot NAME DEMO TIMES  -> OUTDIR/NAME.png (or NAME_k.png for several)
    local name=$1 demo=$2 times=$3
    rm -f pocket-arcade.save
    python3 "$root/tools/shots.py" render avian "$work/r" --size 1170 540 --demo "$demo" --shots "$times" >/dev/null
    local n=0
    for f in "$work"/r/avian_*.png; do
        if [[ "$times" == *,* ]]; then mv "$f" "$out/${name}_$n.png"; else mv "$f" "$out/$name.png"; fi
        n=$((n + 1))
    done
    rm -rf "$work/r"
}

shot 01_title      1   0.8     # first screen: title / key art
shot 02_map        300 1.2     # level map, four worlds, stars per level
shot 03_aim        101 3.4     # level 1: pulling the sling, dotted arc
shot 04_flight     108 4.4     # canyon fortress, Bomb in flight
shot 05_collapse   108 5.0     # Bomb detonates, stone and wood come down
shot 06_slam       109 5.2     # Silver's loop slam through a five-floor tower
shot 07_results    101 15.6    # level cleared: stars + score count-up

# Motion strip: six frames 0.08 s apart through the explosion and collapse.
shot motion 108 4.84,4.92,5.00,5.08,5.16,5.24
python3 "$root/tools/shots.py" sheet "$out/08_motion.png" "$out"/motion_*.png --cols 3 --height 360 >/dev/null
rm -f "$out"/motion_*.png

ls "$out"
