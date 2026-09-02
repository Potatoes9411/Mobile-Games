# Windows 11 and browser builds

Four ways to run the game on a Windows 11 machine, ordered from "zero setup" to "full pipeline".

| Route | Toolchain | Output |
| --- | --- | --- |
| A. Open the file | none | plays in Edge/Chrome/Firefox |
| B. Install as an app | Edge or Chrome | Start-menu entry, own window, offline |
| C. Electron package | Node.js | real `.exe` + NSIS installer |
| D. Unity standalone | Unity | native `.exe` sharing code with the Android build |

---

## A. Just open it

1. `web\index.html` → double-click.
2. Or double-click `web\PlayOnWindows.bat`, which opens it in your default browser.

Controls: drag with the mouse (or `A` / `D`) to steer, `Space` to start, click a tower room to
attack it. Progress lives in `localStorage`, keyed `mobclash.save.v1`, and survives closing the tab.

The page loads two typefaces from Google Fonts and nothing else. Offline it falls back to system
fonts and plays identically — there is no other network dependency, no bundler and no build step.

> Serving over `file://` is fine here. If you would rather serve it over HTTP (needed only if you
> later add `fetch`-based assets): `npx serve web` or `python -m http.server -d web 8080`.

## B. Install it as a Windows app (no build tools)

Open the page in Edge, then `⋯ ▸ Apps ▸ Install this site as an app`. In Chrome it is
`⋮ ▸ Cast, save and share ▸ Install page as app`.

You get a desktop shortcut and a Start-menu entry, the game runs in its own chrome-less window, and
Windows treats it like any other installed app (including uninstall via Settings ▸ Apps). This is
the fastest path to something that *feels* like an `.exe` without producing one.

## C. Build a real `.exe` with Electron

Requires [Node.js 18+](https://nodejs.org).

```powershell
cd desktop
npm install
npm run dist
```

Outputs into `desktop\dist\`:

* `MobClashGateSiege-1.0.0-portable.exe` — single file, no install, runs from a USB stick.
* `MobClashGateSiege-1.0.0-nsis.exe` — installer with a desktop shortcut and an uninstaller.

`npm start` runs it without packaging. Both scripts first run `node sync.js`, which copies
`web/index.html` into `desktop/game/` — `web/index.html` stays the single source of truth, so the
desktop and browser builds can never drift.

The window is 620 × 1000 (portrait, like the phone target), resizable, with the menu bar hidden.
Change those in `desktop/main.js`.

**Code signing.** The unsigned `.exe` triggers a SmartScreen warning on first run ("Windows
protected your PC" → *More info* → *Run anyway*). For distribution, add an OV/EV code-signing
certificate to `desktop/package.json`:

```json
"win": {
  "certificateFile": "cert.pfx",
  "certificatePassword": "${env.WIN_CERT_PASSWORD}"
}
```

## D. Unity Windows standalone

The Unity project targets Windows too — same C#, same pacing, real 3D.

1. Unity Hub ▸ Add modules ▸ **Windows Build Support (IL2CPP)** (Mono is fine for local testing and
   builds faster; `ProjectSetup` selects Mono for Standalone by default).
2. In the editor: `Tools ▸ Mob Clash ▸ 4. Setup Everything`, then `5. Build Playable Scene`.
3. `Tools ▸ Mob Clash ▸ 7. Build Windows x64 EXE`.

Output: `Builds\Windows\MobClashGateSiege.exe` alongside its `_Data` folder — ship the whole folder.

Command line:

```powershell
& "C:\Program Files\Unity\Hub\Editor\<version>\Editor\Unity.exe" `
  -quit -batchmode -nographics `
  -projectPath "$PWD" `
  -executeMethod MobClash.EditorTools.BuildScript.CommandLineWindows `
  -logFile build.log
```

Mouse and keyboard work out of the box: `TouchInput` reads the mouse as a pointer and
`PlayerSwerve` consumes drag deltas, so the swerve feel is identical to touch.

### Unity WebGL

`Tools ▸ Mob Clash ▸ 8. Build WebGL` produces `Builds/WebGL/`, which must be served over HTTP
(`npx serve Builds/WebGL`) — browsers refuse to load WebAssembly from `file://`. For a hyper-casual
playable, prefer `web/index.html`: it starts in under a second, where a Unity WebGL build is a
multi-megabyte download and a several-second Wasm warm-up.

---

## Which build should I use for what?

* **Playtesting the design, ad-network playables, sharing a link** → `web/index.html`. Instant, tiny,
  and the pacing math is a line-for-line port of `LevelGenerator.cs`.
* **Shipping to players** → the Unity Android APK.
* **A desktop build for a demo machine or a games night** → Electron (route C) or the Unity
  standalone (route D).

## Keeping the two implementations in sync

Tuning constants live in exactly two places:

| Concept | Unity | Web |
| --- | --- | --- |
| Level pacing | `LevelGenerator` fields | `TUNE` object |
| Upgrade curves | `EconomyManager` fields | `ECON` object |
| Gate arithmetic | `GateMath.Apply` | `applyGate()` |
| Tower solver | `LevelData.IsSolvable` | `isSolvable()` |

Change one, mirror it in the other, and both builds stay identical in feel.
