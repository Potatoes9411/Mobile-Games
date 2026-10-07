#!/usr/bin/env bash
# Review capture for Paper Territory (id: paper).
#   ./native/build_headless.sh && tools/review/paper.sh OUTDIR
# Writes, in order:
#   01_title.png    first screen: logo, skin cards, your cube on its start blob
#   02_early.png    a few seconds in: leaving home, first trail out, rivals near
#   03_busy.png     mid-game: a large claimed territory among rival lands
#   04_claim.png    the signature moment: a loop closing and the fill sweeping in
#   05_kill.png     cutting a rival's trail: KILL banner, debris, shock ring
#   06_cutoff.png   your own trail cut by a rival
#   07_results.png  results: podium, leaderboard with %, rank, best, retry
#   08_motion.png   motion strip, six frames 0.08 s apart across a claim
# Every run is deterministic: the --demo modes self-drive from a fixed seed and
# never touch the save file.
#   demo 1: your cube self-drives and expands (rivals leave your land alone)
#   demo 2: the bot overreaches late in the run and a rival cuts its trail
#   demo 3: the bot hunts rival trails
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:?usage: tools/review/paper.sh OUTDIR}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

shots() {   # name demo times
    (cd "$tmp" && python3 "$here/tools/shots.py" render paper "$tmp/$1" --demo "$2" --shots "$3" >/dev/null)
}

shots a 1 "0.6,3.0,30.0,40.2"
shots k 3 "8.78"
shots d 2 "19.32,21.9"
shots m 1 "40.00,40.08,40.16,40.24,40.32,40.40"

cp "$tmp/a/paper_00.png" "$out/01_title.png"
cp "$tmp/a/paper_01.png" "$out/02_early.png"
cp "$tmp/a/paper_02.png" "$out/03_busy.png"
cp "$tmp/a/paper_03.png" "$out/04_claim.png"
cp "$tmp/k/paper_00.png" "$out/05_kill.png"
cp "$tmp/d/paper_00.png" "$out/06_cutoff.png"
cp "$tmp/d/paper_01.png" "$out/07_results.png"
python3 "$here/tools/shots.py" sheet "$out/08_motion.png" "$tmp"/m/paper_0*.png --height 900 >/dev/null
ls "$out"
