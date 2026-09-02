# Windows 11 and browser builds

Four ways to run the game on a Windows 11 machine, ordered from "zero setup" to "full pipeline".

| Route | Toolchain | Output |
| --- | --- | --- |
| A. Run the prebuilt `.exe` | none | 104 KB executable, already in `dist/` |
| B. Open the HTML file | none | plays in Edge/Chrome/Firefox |
| C. Install as a PWA | Edge or Chrome | Start-menu entry, own window, offline |
| D. Electron package | Node.js | fully native app + NSIS installer |
| E. Unity standalone | Unity | native `.exe` sharing code with the Android build |

---

## A. The prebuilt executable

`dist\MobClashGateSiege.exe` — double-click it. That is the whole install.

### How it works

The launcher is a 104 KB native Win32 program (`desktop/win-launcher/main.c`) with the entire game
embedded in its data section as a byte array. On launch it:

1. Creates `%LOCALAPPDATA%\MobClashGateSiege\` and writes `game.html` there (overwriting each run,
   so the shipped version is always what runs).
2. Looks for `msedge.exe`, then `chrome.exe`, under `%ProgramFiles(x86)%`, `%ProgramFiles%` and
   `%LOCALAPPDATA%`.
3. Launches it with `--app=file:///…`, which gives a chrome-less window — no tabs, no address bar,
   its own taskbar entry — plus `--user-data-dir` pointing at a private profile so saves persist and
   stay out of your normal browsing profile.
4. Falls back to `ShellExecuteW` on the HTML file if no Chromium browser is installed, so the game
   always starts.

It imports only `kernel32`, `shell32` and `user32`. No runtime, no installer, no registry writes.

### Rebuilding it

```bash
sudo apt-get install mingw-w64      # Debian/Ubuntu; brew install mingw-w64 on macOS
./desktop/win-launcher/build.sh     # embeds web/index.html and cross-compiles
```

On Windows use MSYS2 and swap `x86_64-w64-mingw32-gcc` for `gcc` in `build.sh`.

> **Toolchain gotcha, already handled:** mingw-w64 maps bare `swprintf()` to the non-conforming
> MSVC variant that takes no buffer-size argument. Passing one shifts every following argument and
> the program wanders off before doing any work — it compiles clean and hangs at launch. All string
> building goes through the `FormatW` helper (`_vsnwprintf`) to avoid it.

### SmartScreen

An unsigned executable downloaded from the internet triggers "Windows protected your PC" on first
run: **More info → Run anyway**. To remove the warning permanently you need an OV or EV
code-signing certificate and `signtool sign /fd SHA256 /tr <timestamp-url> /td SHA256 MobClashGateSiege.exe`.

## B. Just open the HTML

1. `web\index.html` → double-click.
2. Or double-click `web\PlayOnWindows.bat`, which opens it in your default browser.

Controls: drag with the mouse (or `A` / `D`) to steer, `Space` to start, click a tower room to
attack it. Progress lives in `localStorage`, keyed `mobclash.save.v1`, and survives closing the tab.

The page loads two typefaces from Google Fonts and nothing else. Offline it falls back to system
fonts and plays identically — there is no other network dependency, no bundler and no build step.

> Serving over `file://` is fine here. If you would rather serve it over HTTP (needed only if you
> later add `fetch`-based assets): `npx serve web` or `python -m http.server -d web 8080`.

## C. Install it as a Windows app (no build tools)

Open the page in Edge, then `⋯ ▸ Apps ▸ Install this site as an app`. In Chrome it is
`⋮ ▸ Cast, save and share ▸ Install page as app`.

You get a desktop shortcut and a Start-menu entry, the game runs in its own chrome-less window, and
Windows treats it like any other installed app (including uninstall via Settings ▸ Apps). This is
the fastest path to something that *feels* like an `.exe` without producing one.

## D. Build a fully native app with Electron

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

## E. Unity Windows standalone

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
* **A desktop build for a demo machine or a games night** → the prebuilt launcher (route A) for
  something you can hand over on a USB stick, Electron (route D) for a self-contained app that does
  not depend on an installed browser, or the Unity standalone (route E) for the real 3D build.

## Continuous builds

`.github/workflows/build-windows.yml` builds both Windows targets on every push and on demand
(**Actions → Build Windows → Run workflow**): the mingw launcher on an Ubuntu runner and the
Electron installer plus portable build on a Windows runner. Both land as downloadable run
artifacts.

## Keeping the two implementations in sync

Tuning constants live in exactly two places:

| Concept | Unity | Web |
| --- | --- | --- |
| Level pacing | `LevelGenerator` fields | `TUNE` object |
| Upgrade curves | `EconomyManager` fields | `ECON` object |
| Gate arithmetic | `GateMath.Apply` | `applyGate()` |
| Tower solver | `LevelData.IsSolvable` | `isSolvable()` |

Change one, mirror it in the other, and both builds stay identical in feel.
