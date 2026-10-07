#!/usr/bin/env bash
# Arcade home screen (hub / lobby) review captures. Run from the repo root
# after ./native/build_headless.sh:   tools/review/hub.sh OUTDIR
#
# Every run passes --demo N, which seeds the meta layer (level, gems, coins,
# missions, streak, per-game stats) in memory and never touches a real save:
#   --demo 1  a returning player, today's login reward already taken
#   --demo 2  the same player opening the app on a new day (streak popup)
#   --demo 3  the player coming back from a Helix Drop run that levels them up
# Taps are canvas pixels at 540x1170 unless a frame says otherwise.
#
#   01  home screen               lobby at rest
#   02  scrolled                  after a fling, mid-momentum
#   03  scrolled, settled         the grid further down
#   04  tile pressed              finger held on Road Hopper: squash into its lip
#   05-08 launch strip            tap Road Hopper: card expands, title, iris opens on the game
#   09  missions panel            DAILY MISSIONS sheet, one mission ready to claim
#   10  mission claimed           gems flying from CLAIM into the header counter
#   11  streak popup              DAILY REWARD on a new day
#   12  run reward                back from a run: +XP flying to the level bar
#   13  level up                  LEVEL UP popup with gem reward and confetti
#   14  pause sheet               pause sheet over a live Helix Drop game
#   15  home 540x960              short phone
#   16  home 960x540              desktop window
set -euo pipefail
out="${1:?usage: tools/review/hub.sh OUTDIR}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
bin="$root/native/out/pa_headless"
[ -x "$bin" ] || { echo "build first: ./native/build_headless.sh" >&2; exit 1; }
mkdir -p "$out"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# shoot GAME "ARGS" "t1,t2,..." name1 name2 ...
shoot() {
    local game="$1" args="$2" times="$3"; shift 3
    rm -rf "$tmp/run"; mkdir -p "$tmp/run"
    # shellcheck disable=SC2086
    python3 "$root/tools/shots.py" render "$game" "$tmp/run" $args --shots "$times" >/dev/null
    local i=0
    for name in "$@"; do
        mv "$tmp/run/${game}_$(printf %02d "$i").png" "$out/$name.png"
        i=$((i + 1))
    done
}

shoot hub "--demo 1" "1.60" 01_home
shoot hub "--demo 1 --input 1.2:down:270:1000;1.24:move:270:930;1.28:move:270:800;1.32:move:270:640;1.36:move:270:480;1.37:up" \
    "1.50,3.40" 02_scrolled_fling 03_scrolled_settled
shoot hub "--demo 1 --input 1.2:down:139:764" "1.40" 04_tile_pressed
shoot hub "--demo 1 --input 1.2:tap:139:764" "1.36,1.52,1.78,2.02" \
    05_launch_a 06_launch_b 07_launch_c 08_launch_d
shoot hub "--demo 1 --input 1.2:tap:170:500;2.2:tap:427:438" "1.90,2.75" 09_missions_panel 10_mission_claimed
shoot hub "--demo 2" "1.80" 11_streak_popup
shoot hub "--demo 3" "1.15,2.80" 12_run_reward 13_level_up
shoot helix "--demo 1 --input 1.5:key:esc" "2.30" 14_pause_sheet
shoot hub "--demo 1 --size 540 960" "1.60" 15_home_540x960
shoot hub "--demo 1 --size 960 540" "1.60" 16_home_960x540
ls "$out"/*.png
