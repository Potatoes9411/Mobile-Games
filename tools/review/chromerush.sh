#!/usr/bin/env bash
# Chrome Rush review captures. Run from the repo root after ./native/build_headless.sh
#   tools/review/chromerush.sh OUTDIR
# Deterministic: every run uses the demo profile (fixed save, fixed seed) and
# the built-in demo bot, which weaves traffic, fires nitro, crosses a level
# gate from night into sunset, then picks a car to hit for the KO sequence.
#   --demo 1  title screen -> night run -> nitro -> gate -> sunset -> crash -> results
#   --demo 2  garage
#   --demo 3  day-biome run, dense traffic
set -euo pipefail
out="${1:?usage: tools/review/chromerush.sh OUTDIR}"
mkdir -p "$out"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

shots() { python3 tools/shots.py render chromerush "$tmp/$1" --shots "$2" --demo "$3" > /dev/null; }

shots run   0.8,3.5,6.6,8.0,10.3,11.6,14.0 1
shots day   3.0 3
shots gar   0.5 2
shots strip 6.20,6.28,6.36,6.44,6.52,6.60 1

cp "$tmp/run/chromerush_00.png" "$out/01_title.png"
cp "$tmp/run/chromerush_01.png" "$out/02_early_night_run.png"
cp "$tmp/day/chromerush_00.png" "$out/03_busy_day_traffic.png"
cp "$tmp/run/chromerush_02.png" "$out/04_nitro_smash.png"
cp "$tmp/run/chromerush_03.png" "$out/05_near_miss_level_gate.png"
cp "$tmp/run/chromerush_04.png" "$out/06_sunset_near_misses.png"
cp "$tmp/run/chromerush_05.png" "$out/07_crash_ko.png"
cp "$tmp/run/chromerush_06.png" "$out/08_results.png"
cp "$tmp/gar/chromerush_00.png" "$out/09_garage.png"
python3 tools/shots.py sheet "$out/10_motion_strip.png" "$tmp"/strip/chromerush_0*.png --height 900 > /dev/null
ls "$out"
