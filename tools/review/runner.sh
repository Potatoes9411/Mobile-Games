#!/usr/bin/env bash
# Rooftop Run review captures. Run from the repo root after ./native/build_headless.sh.
#   tools/review/runner.sh OUTDIR
# Deterministic: --demo 1 seeds a fixed opening (jump, roll, ramp onto a train
# roof, magnet, oncoming train) driven by a dodging bot that steers into a
# train at ~21 s. Every capture starts from a fresh save in a temp directory,
# so best score and keys are identical run to run.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "${1:?usage: runner.sh OUTDIR}"
out="$(cd "$1" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"   # the headless build saves into the cwd; keep that out of the repo

shot() {  # name time
  rm -f pocket-arcade.save
  python3 "$root/tools/shots.py" render runner "$tmp/r" --shots "$2" --demo 1 \
      --input "1.0:tap:0.5f:0.6f" >/dev/null
  mv "$tmp/r/runner_00.png" "$out/$1.png"
}

shot 01_start_screen    0.6
shot 02_early_play      3.0
shot 03_jump_barrier    5.5
shot 04_roll_under      7.45
shot 05_train_roof      10.4
shot 06_busy_magnet_x2  14.3
shot 07_crash           21.95
shot 08_revive_offer    24.0
shot 09_results         28.6

# Motion strip: six frames 0.08 s apart through the first barrier jump.
rm -f pocket-arcade.save
python3 "$root/tools/shots.py" render runner "$tmp/m" --shots 5.30,5.38,5.46,5.54,5.62,5.70 \
    --demo 1 --input "1.0:tap:0.5f:0.6f" >/dev/null
python3 "$root/tools/shots.py" sheet "$out/10_motion_strip.png" "$tmp"/m/runner_0*.png --height 900 >/dev/null
ls "$out"
