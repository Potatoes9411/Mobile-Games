# Builder brief — Pocket Arcade remake programme

You are one builder in a programme rebuilding 12 mobile games in a native C
engine until each one beats its reference game in a blind side-by-side review.
The reference game is the quality bar, not loose inspiration. An independent,
fresh-context critic will judge your work from rendered frames only, never from
your code or your report. So nothing counts unless it is visible in the frames.

## The product

- `native/` is a self-contained C99 engine: software rasterizer (`raster.c`),
  stroke font (`font.c`), synth audio (`audio.c`), key/value save (`save.c`),
  the hub (`hub.c`), platform layers (Win32 exe, Android APK, headless capture).
  The public API is `native/src/pa.h`. Read it fully before writing code.
- Each game is one file `native/src/games/<id>.c` exporting `PA_GAME_<ID>`.
- The phone is the primary target: portrait, logical canvas 540 wide by about
  1100-1200 tall (it varies with the phone; handle 540x960 to 540x1200 and the
  960x540 desktop window gracefully). Avian Artillery is landscape: call
  `pa_set_landscape(1)` in start; capture it at `--size 1170 540`.
- Simulation runs on a fixed 1/120 s step. Never put logic in render. Results
  must be identical at any refresh rate.
- Pause: the hub draws a round pause button. Call
  `pa_hub_pause_anchor(cx, cy, r)` from render to put it where the reference
  HUD would have its pause/settings button, or `pa_hub_hide_pause()` while a
  results card or modal is up. The Android back button pauses for free.
- Meta: when a run or level ends, call `pa_meta_report(id, &report)` (see the
  Shell section of `pa.h`: score, coins, won, level, stars). The hub turns it
  into account XP, gems, missions and per-game stats on the home screen.
- Save keys must be namespaced: `"<id>.best"`, `"<id>.coins"` ...
- Audio: `pa_tone`, `pa_noise`, `pa_sfx`. Give the game its own sound palette
  through tones; silence is a defect.

## Ownership — strict

You may create or edit only:
- `native/src/games/<id>.c` (your game; split into `<id>_*.c` beside it if huge)
- `tools/review/<id>.sh` (your capture script)

Do not edit `pa.h`, `raster.c`, `font.c`, `hub.c`, `audio.c`, `save.c`, the
platform files, build scripts, or another game. Other builders are working in
parallel and the lead merges everything. If you need an engine helper, write
it `static` inside your own file. If you believe a shared-engine change is
genuinely required, say so in your report with the exact proposed API.

Text: use `pa_text` / `pa_text_bold` / `pa_text_width` only, never hand-drawn
letters. A font upgrade is landing in parallel and must reach every game.

## References

- Screenshots: `/home/user/Mobile-Games/tools/refs/plates/<plate-dir>/` (official
  App Store shots; some carry marketing banners - ignore the banners). These
  are copyrighted: look at them and measure them, never copy them into the
  repo, never commit them, never trace pixels from them.
- Measurements and observations: `docs/REFERENCE_STYLE.md`, your game's section
  and "What the plates agree on".
- Our earlier browser implementation of the same game: `web/src/games/*.js`.
  It is our own code: reuse its rules, level data, economy and pacing where
  they are good. Its art is not the bar; the reference is.
- Your knowledge of how the reference game actually plays: controls, camera,
  pacing, feedback, juice, progression, results/fail screens, the level-to-level
  loop. Match the real game's feel, not just its stills.

## Render and look

```
./native/build_headless.sh                         # -> native/out/pa_headless
python3 tools/shots.py render <id> /tmp/<id>-look --shots 0.5,3,8 --demo 1 \
        --input "1.0:tap:0.5f:0.6f;2.0:swipe:left"
python3 tools/shots.py sheet /tmp/<id>-look/sheet.png /tmp/<id>-look/*.png
```
Then Read the PNGs and compare them against the plates yourself, often.
`pa_demo_mode()` returns the `--demo` value in captures (0 in shipping builds):
use it to self-play (a bot steering, an auto-solver, a forced results card) so
captures show real gameplay states. Input script verbs: `tap`, `down`,
`move`, `up`, `swipe`, `key` (see `native/src/platform_headless.c`).

## Deliverable: `tools/review/<id>.sh OUTDIR`

A capture script that writes 6-10 PNGs into OUTDIR showing, in order: the
first screen a player sees, early play, a busy mid-game moment, the signature
mechanic happening, a fail or results screen, and a motion strip (5-6 frames
about 0.08 s apart during action). Make the script deterministic. It must run
from the repo root after `./native/build_headless.sh`. The critic sees exactly
these frames, so choose moments a real player would screenshot.

## Bar for done

- Composition, proportions, palette, camera, HUD layout and typography scale
  read as the reference at a glance, at phone size.
- Every element is finished: no placeholder shapes, no programmer-art HUD, no
  "coming soon", no missing states (start, play, fail, win, results, retry).
- Feel: input response, easing, squash, particles, screen shake, floating
  score text, combo feedback and sound where the reference has them.
- Progression and loop: levels or difficulty ramp, best score, currency or
  unlocks where the reference has them, a results screen that leads to the
  next run.
- Builds clean: `./native/build_headless.sh` and `./native/build.sh` (mingw,
  `-Wall -Wextra`) with no warnings from your file.

## Finish

Commit on your branch (no push). Reply in under 250 words: what changed, the
review script path, and what you know is still weaker than the reference. Do
not claim it matches the reference; the critic decides that.
