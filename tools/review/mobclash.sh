#!/usr/bin/env bash
# Review captures for Mob Clash: Gate Siege (reference: Mob Control).
# Usage, from the repo root after ./native/build_headless.sh:
#   tools/review/mobclash.sh OUTDIR
# Every run is pa_demo_mode() self-play with fixed seeds that never reads the
# save file, so the frames are identical from run to run.
#   demo 1  level 1 from a fresh install, the bot holds fire and aims for gates
#   demo 2  level 7 with a few upgrades: moving gates, a -N trap, red waves
#   demo 3  level 9 with a hesitant thumb: the defence line is breached
#   demo 4  level 5 boss level, well upgraded: castle falls, results, shop
#   demo 5  the upgrade screen, two purchases
#   demo 6  level 15 boss level: late-game crowds and the boss brute
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:?usage: tools/review/mobclash.sh OUTDIR}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cd "$work"

shot() {   # shot NAME DEMO TIMES
    python3 "$root/tools/shots.py" render mobclash "$work/$1" --demo "$2" --shots "$3" >/dev/null
}

shot a 1 0.5,6.0
mv a/mobclash_00.png "$out/01_first_screen.png"
mv a/mobclash_01.png "$out/02_early_play.png"
shot b 6 12.0
mv b/mobclash_00.png "$out/03_busy_boss_level.png"
shot c 2 6.5
mv c/mobclash_00.png "$out/04_gates_vs_red_wave.png"
shot d 4 23.0,26.5
mv d/mobclash_00.png "$out/05_castle_destroyed.png"
mv d/mobclash_01.png "$out/06_victory_results.png"
shot e 3 17.0
mv e/mobclash_00.png "$out/07_defeat_results.png"
shot f 5 2.2
mv f/mobclash_00.png "$out/08_upgrade_screen.png"
shot g 6 10.00,10.08,10.16,10.24,10.32,10.40
python3 "$root/tools/shots.py" sheet "$out/09_motion_strip.png" g/mobclash_0*.png --height 900 >/dev/null
ls "$out"/*.png
