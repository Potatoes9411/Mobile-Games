#!/usr/bin/env bash
# Horde Arena review captures. Run from the repo root after
# ./native/build_headless.sh:   tools/review/horde.sh OUTDIR
#
# Every capture is deterministic: --demo N runs a fixed seed and a fixed save
# profile, a bot steers (kiting the swarm, collecting gems) and auto-picks the
# draft, and N >= 2 preloads a moment of the run:
#   1 title, then a fresh run    2 mid-game at 2:20 with six weapons
#   3 first boss at 3:48         4 a low-health stand that ends in defeat
#   5 an elite chest at 1:40     6 the final boss at 7:50 (victory)
set -euo pipefail
out="${1:?usage: tools/review/horde.sh OUTDIR}"
mkdir -p "$out"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/horde-review.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

shot() {   # shot NAME DEMO TIME [INPUT]
  local name="$1" demo="$2" t="$3" input="${4:-}"
  local args=(render horde "$tmp/$name" --shots "$t" --demo "$demo")
  if [ -n "$input" ]; then args+=(--input "$input"); fi
  python3 tools/shots.py "${args[@]}" > /dev/null
  mv "$tmp/$name/horde_00.png" "$out/$name.png"
}

start_tap="1.0:tap:0.5f:0.88f"
shot 01_title          1 0.6
shot 02_early_play     1 12.0 "$start_tap"
shot 03_swarm_midgame  2 4.0
shot 04_skill_draft    2 6.0
shot 05_zombies_incoming 2 14.4
shot 06_boss_cage      3 20.0
shot 07_lucky_chest    5 3.9
shot 08_defeated       4 4.0
shot 09_victory        6 18.0

# Motion strip: six frames 0.08 s apart in the thick of the fight.
python3 tools/shots.py render horde "$tmp/strip" --demo 2 \
  --shots 3.00,3.08,3.16,3.24,3.32,3.40 > /dev/null
python3 tools/shots.py sheet "$out/10_motion_strip.png" "$tmp"/strip/horde_0*.png > /dev/null
ls "$out"
