# Mob Clash: Gate Siege

A hybrid-casual mobile game in the Voodoo / SayGames / Supersonic mould: swerve a crowd down a
track, gamble it through `+ - × ÷` gates, then spend the resulting horde storming a tower one room
at a time. Between runs you sink gold into three upgrade tracks and go again.

The repository ships **two complete implementations of the same design**:

| Build | Where it lives | Runs on |
| --- | --- | --- |
| Unity 3D project | `Assets/` | Android (APK), Windows 11 (EXE), WebGL |
| Canvas/JS build | `web/index.html` | Any browser, no install; wrappable as a Windows `.exe` via `desktop/` |

Both share the same pacing math, the same economy curves and the same three-phase loop, so tuning
one and porting the numbers to the other is a copy-paste job.

---

## Play it right now (Windows 11, no toolchain)

1. Download the repo (green **Code → Download ZIP**) and unzip it.
2. Open `web\index.html` — double-click it, or double-click `web\PlayOnWindows.bat`.

That is the whole install. Mouse drag or `A` / `D` steers, `Space` starts, clicking a tower room
attacks it. Progress saves to `localStorage`.

**Want a real `.exe`?** Two options, both in `docs/WINDOWS_AND_WEB_BUILD.md`:

* *Edge/Chrome PWA* — open the page, `⋯ → Apps → Install this site as an app`. You get a Start-menu
  entry and a desktop shortcut with no build step at all.
* *Electron package* — `cd desktop && npm install && npm run dist` produces
  `MobClashGateSiege-1.0.0-portable.exe` plus an NSIS installer under `desktop/dist/`.

## Build the Unity version

```
Tools ▸ Mob Clash ▸ 4. Setup Everything        # layers, tags, physics matrix, player settings
Tools ▸ Mob Clash ▸ 5. Build Playable Scene    # generates Assets/Scenes/Game.unity, fully wired
Tools ▸ Mob Clash ▸ 6. Build Android APK       # or 7. Windows x64 EXE, or 8. WebGL
```

There are no prefabs, materials or `.meta`-bound art assets to import: every visual is generated
from Unity primitives and code-built materials by `PrimitiveFactory`, so the scripts alone are a
running game. Swap in real art later by assigning the prefab fields on `LevelBuilder`,
`SwarmManager` and `SiegeManager`.

Full walkthroughs: [`docs/SCENE_SETUP.md`](docs/SCENE_SETUP.md) and
[`docs/ANDROID_BUILD.md`](docs/ANDROID_BUILD.md).

---

## The loop

1. **Swerve** — one finger on the X axis, `SmoothDamp` steering for weight, constant forward speed.
2. **Math gates** — static, sliding and rotating gate pairs; taking one half instantly consumes the other.
3. **Tower siege** — rooms unlock floor by floor. Take a room when `crowd > defenders`; conquering it
   absorbs the defenders (`crowd += defenders`, minus the level's casualty ratio). Stall out with no
   beatable room and the siege is lost.
4. **Meta** — spend gold on Starting Mob, Gold Multiplier and Gate Bonus.

## Retention pacing

| Levels | Band | Design |
| --- | --- | --- |
| 1–3 | The Hook | Only positive gates, ×2/×3/×5 ladders, zero hazards, tower at 0.35× the expected crowd. 100% win rate. |
| 4–10 | Mastery | Every row is a decision: `+N now` versus `×M guarded by moving hazards`. Traps appear at level 7. |
| 11+ | Core Loop | Procedural rows (safe-vs-risky, multiplier race, lesser-evil trap pairs), hazard density and tower ratio both ramp. |
| every 4th | Gold Rush | 4 gift rows, no hazards, ×2 gold, easy tower. The breather that prevents churn. |

Difficulty is **relative, not absolute**: the generator simulates a reference run through the level
it just built and sizes the tower against that result, so a player with maxed crowd upgrades still
meets a proportionate tower. A greedy solver then proves every generated tower is beatable before it
ships. The numbers behind all of this are in [`docs/PACING_MATH.md`](docs/PACING_MATH.md).

## Repository map

```
Assets/Scripts/Core/      GameManager (FSM), events, layers, gate maths, input, primitive factory
Assets/Scripts/Data/      LevelData ScriptableObject + LevelGenerator (the pacing brain)
Assets/Scripts/Level/     LevelBuilder - turns a LevelData into scene geometry
Assets/Scripts/Player/    PlayerSwerve, CameraRig
Assets/Scripts/Swarm/     ObjectPool, SwarmManager (500+ units, one flat update loop)
Assets/Scripts/Track/     Gate, Obstacle, FinishLine
Assets/Scripts/Siege/     SiegeManager, TowerNode
Assets/Scripts/Meta/      EconomyManager (JSON in PlayerPrefs)
Assets/Scripts/Juice/     JuiceManager, FloatingText, HapticFeedback, procedural SfxLibrary
Assets/Scripts/Ads/       AdManager (mock mediation, real call shape)
Assets/Scripts/UI/        UIManager, UIFactory, UpgradeRowUI (uGUI built in code)
Assets/Editor/            ProjectSetup, SceneBootstrapper, BuildScript
web/index.html            Complete browser build, single file, zero dependencies
desktop/                  Electron shell for a Windows .exe
docs/                     Scene setup, Android pipeline, Windows/web pipeline, pacing math
```

## Licence

MIT. Sound effects are synthesised at runtime and all geometry is generated, so there is nothing in
here you need to clear before shipping.
