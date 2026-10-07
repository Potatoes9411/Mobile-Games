#!/usr/bin/env bash
# Helix Drop review capture. Run from the repo root after ./native/build_headless.sh
#   tools/review/helix.sh OUTDIR
# Deterministic: every run is the demo bot (pa_demo_mode) on fixed levels.
#   --demo 1  level 1 (meadow) -> 3-floor fireball smashes a red wedge ->
#             LEVEL 1 COMPLETED trophy card (third key earned) -> chest room,
#             three chests opened -> level map with the YOU marker ->
#             level 2 (sponge) -> lands on red -> fail / continue card
#   --demo 2  level 5, the boss level (grey, spiked red rings, eyeball skin)
set -euo pipefail
out="${1:?usage: tools/review/helix.sh OUTDIR}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
bin="native/out/pa_headless"
[ -x "$bin" ] || { echo "build first: ./native/build_headless.sh" >&2; exit 1; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

shots() {  # name demo times... -> $tmp/<name>/helix_NN.png
    local name="$1" demo="$2"; shift 2
    local list; list="$(IFS=,; echo "$*")"
    python3 tools/shots.py render helix "$tmp/$name" --demo "$demo" --shots "$list" >/dev/null
}

# Main run: start, bounce, smash, complete, chests, map, sponge level, fail.
shots d1 1 0.6 2.45 3.82 9.1 13.2 16.6 19.6 24.5
# Motion strip across the fireball and the smash, 0.08 s apart.
shots strip 1 3.58 3.66 3.74 3.82 3.90 3.98
shots d2 2 6.2

cp "$tmp/d1/helix_00.png" "$out/01_start.png"
cp "$tmp/d1/helix_01.png" "$out/02_early_bounce.png"
cp "$tmp/d1/helix_02.png" "$out/03_smash_through_red.png"
cp "$tmp/d1/helix_06.png" "$out/04_level2_sponge.png"
cp "$tmp/d2/helix_00.png" "$out/05_boss_level.png"
cp "$tmp/d1/helix_03.png" "$out/06_level_complete.png"
cp "$tmp/d1/helix_04.png" "$out/07_chest_pick.png"
cp "$tmp/d1/helix_05.png" "$out/08_level_map.png"
cp "$tmp/d1/helix_07.png" "$out/09_fail_continue.png"
python3 tools/shots.py sheet "$out/10_motion_strip.png" "$tmp"/strip/helix_0*.png --height 1170 >/dev/null
ls "$out"
