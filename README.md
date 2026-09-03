# Pocket Arcade

Three complete mobile games in one hub, sharing an engine, a cross-game progression
layer, and a procedural character system that generates and animates every fighter
from a seed — no character art in the repository.

| Game | Genre | Loop |
| --- | --- | --- |
| **Mob Clash: Gate Siege** | Crowd runner + puzzle | Swerve a mob through `+ − × ÷` gates, then storm a keep room by room |
| **Horde Arena** | Survivor auto-battler | Steer only; draft an upgrade every level, evolve weapons, survive 20 waves |
| **Rooftop Run** | Endless runner | Three lanes, vault and slide, chain power-ups, chase distance |

Above them sits an account level, gems, a prestige shop, daily missions and a
streak — so a run always feeds something.

---

## Play it right now on Windows 11

**The executable.** Download [`dist/PocketArcade.exe`](dist/PocketArcade.exe) and
double-click it. 222 KB, no installer, nothing to unpack.

All three games are embedded inside the executable. On launch it writes itself to
`%LOCALAPPDATA%\PocketArcade\` and opens in a chrome-less app window — no tabs,
no address bar, its own taskbar entry — using the Edge that ships with Windows 11
(or Chrome, if you have it). Saves live in a private profile beside it.

> Windows SmartScreen warns about an unsigned executable the first time:
> **More info → Run anyway**.

**No download.** Open `web/index.html` in any browser, or the single-file bundle
`dist/pocket-arcade.html`.

**A fully native app.** `cd desktop && npm install && npm run dist` packages an
Electron build with an NSIS installer, or let
`.github/workflows/build-windows.yml` build both executables for you.

Controls: drag or `A`/`D` to steer, swipe (or arrows) in Rooftop Run, tap to
select. Everything saves locally.

---

## The character system

Every fighter — the mob, the arena hero, the tower garrison, the runner — comes out
of `A.Rig`. A seed produces proportions, an HSL palette and gear; a clip name
produces a pose; the renderer walks the skeleton and draws each limb as a tapered
capsule with an ink outline.

Fifteen clips (`run`, `sprint`, `jump`, `slide`, `roll`, `shoot`, `attack`, `die`,
`cheer`, …) are plain functions of normalised time, so animation is code, not data.

Crowds do not pay for that. `Rig.bake` renders a clip into a sprite strip once at
boot, so 260 runners or 120 arena enemies cost one `drawImage` each while heroes
and bosses are still drawn live from the skeleton.

Full architecture: [`docs/ARCADE.md`](docs/ARCADE.md).

---

## The Unity project

`Assets/` holds a complete Unity 3D implementation of Mob Clash: Gate Siege,
targeting Android (APK), Windows and WebGL — the same pacing math, economy curves
and three-phase loop as the web version.

```
Tools ▸ Mob Clash ▸ 4. Setup Everything        # layers, tags, physics matrix, player settings
Tools ▸ Mob Clash ▸ 5. Build Playable Scene    # generates Assets/Scenes/Game.unity, fully wired
Tools ▸ Mob Clash ▸ 6. Build Android APK       # or 7. Windows x64 EXE, or 8. WebGL
```

No prefabs or art assets to import: every visual is generated from primitives and
code-built materials, so the scripts alone are a running, art-directed game.
See [`docs/SCENE_SETUP.md`](docs/SCENE_SETUP.md) and
[`docs/ANDROID_BUILD.md`](docs/ANDROID_BUILD.md).

---

## Retention design

Mob Clash's level pacing is generated, not authored, and is verified before it
ships to the player:

| Levels | Band | Design |
| --- | --- | --- |
| 1–3 | The Hook | Only positive gates, ×2/×3/×5 ladders, no hazards, 0.35 tower ratio. 100% win rate. |
| 4–10 | Mastery | Every row is a decision: `+N now` versus `×M guarded by hazards`. |
| 11+ | Core Loop | Procedural rows, trap pairs, hazard density and tower ratio both ramp. |
| every 4th | Gold Rush | Gift rows, no hazards, ×2 gold, easy tower — the breather that prevents churn. |

Difficulty is **relative**: the generator simulates a reference run through the
level it just built and sizes the tower against that result, then a greedy solver
proves the tower is beatable before it ships. Every formula and a generated level
table: [`docs/PACING_MATH.md`](docs/PACING_MATH.md).

Horde Arena and Rooftop Run carry their own curves — in-run drafting and weapon
evolutions for the former, stacking power-ups and a six-line shop for the latter.

---

## Repository map

```
web/                      the arcade: hub, engine, three games
dist/PocketArcade.exe     prebuilt Windows executable, everything embedded
dist/pocket-arcade.html   single-file bundle (node web/build-single.js)
desktop/win-launcher/     C source for the .exe (mingw-w64 cross build)
desktop/                  Electron shell for a fully native Windows app
Assets/                   Unity 3D project (Mob Clash, Android/Windows/WebGL)
docs/                     arcade architecture, scene setup, build pipelines, pacing math
```

## Licence

MIT. All audio is synthesised at runtime and all geometry and characters are
generated, so there is nothing here to clear before shipping.
