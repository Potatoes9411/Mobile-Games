# Android APK pipeline

Target: a signed, release `.apk` for `com.viralgames.mobclash`, IL2CPP / ARM64, minimum Android 8.0.

---

## 1. Install the toolchain

Unity Hub ▸ **Installs** ▸ your Unity version ▸ gear icon ▸ **Add modules**, and tick:

* **Android Build Support**
  * **OpenJDK** (Unity's bundled JDK 11/17 — do not point at a system JDK unless you have a reason)
  * **Android SDK & NDK Tools**

Verify afterwards in `Edit ▸ Preferences ▸ External Tools`. All three boxes — JDK, Android SDK,
Android NDK — should be ticked as *Installed with Unity*. If a path is red, untick and retick the
checkbox to make Unity re-resolve it.

Command line check that the SDK is really there:

```
# Windows
"%ProgramFiles%\Unity\Hub\Editor\<version>\Editor\Data\PlaybackEngines\AndroidPlayer\SDK\platform-tools\adb" devices
# macOS
"/Applications/Unity/Hub/Editor/<version>/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb" devices
```

Then switch platform once: `File ▸ Build Settings ▸ Android ▸ Switch Platform`. The first switch
re-imports every asset, so let it finish.

---

## 2. Player Settings

`Edit ▸ Project Settings ▸ Player`. `Tools ▸ Mob Clash ▸ 3. Apply Player Settings` sets all of this
for you; the table is the audit trail.

| Setting | Value |
| --- | --- |
| Company Name | `ViralGames` |
| Product Name | `Mob Clash: Gate Siege` |
| Package Name (Other Settings ▸ Identification) | `com.viralgames.mobclash` |
| Version / Bundle Version Code | `1.0` / `1` |
| Minimum API Level | **Android 8.0 (API level 26)** |
| Target API Level | **Automatic (highest installed)** — install the newest SDK platform via Android Studio's SDK Manager if Google Play rejects the level |
| Scripting Backend | **IL2CPP** |
| Api Compatibility Level | .NET Standard 2.1 |
| Target Architectures | **ARM64 ✓**, ARMv7 ✗, x86-64 ✗ |
| C++ Compiler Configuration | Release (Master for the final store build) |
| Default Orientation | Portrait |
| Colour Space | Linear |
| Optimized Frame Pacing | ✓ |
| Internet Access | Require (needed once you swap in a real ad SDK) |
| Managed Stripping Level | Low (raise to Medium once you have tested reflection-heavy SDKs) |

> **ARMv7 must stay off.** Google Play has required a 64-bit binary since 2019, and shipping both
> architectures roughly doubles the IL2CPP build time and adds ~8 MB to the APK for a slice of
> devices below this game's minimum spec anyway.

Quality settings for a hyper-casual title: one quality level, shadows disabled, anti-aliasing off,
VSync **Don't Sync** (`GameManager` sets `targetFrameRate = 60` itself).

Permissions: the only one the game needs is `VIBRATE`, which Unity adds automatically because
`HapticFeedback` touches the vibrator service. If you use a custom `AndroidManifest.xml`, keep:

```xml
<uses-permission android:name="android.permission.VIBRATE" />
```

---

## 3. Generate a release keystore

### GUI

`Project Settings ▸ Player ▸ Publishing Settings ▸ Keystore Manager ▸ Keystore ▸ Create New`.
Fill in password, alias, alias password and validity (50 years), then **Add Key**.

### Command line (preferred — scriptable, and the same key works in CI)

`keytool` ships with the JDK Unity installed:

```bash
# Windows PowerShell (adjust the Unity version)
& "$env:ProgramFiles\Unity\Hub\Editor\<version>\Editor\Data\PlaybackEngines\AndroidPlayer\OpenJDK\bin\keytool.exe" `
  -genkeypair -v `
  -keystore mobclash.keystore `
  -alias mobclash `
  -keyalg RSA -keysize 2048 `
  -validity 10000 `
  -storetype PKCS12
```

```bash
# macOS / Linux
keytool -genkeypair -v \
  -keystore mobclash.keystore \
  -alias mobclash \
  -keyalg RSA -keysize 2048 \
  -validity 10000 \
  -storetype PKCS12
```

Answer the name/organisation prompts (they appear in the certificate, not in the store listing).

**Back the file up and never commit it.** `.gitignore` already excludes `*.keystore`, `*.jks` and
`Builds/`. Losing this file means you can never update the app under the same listing again.

Point Unity at it: `Publishing Settings ▸ Custom Keystore ✓`, browse to the file, enter both
passwords, pick the alias.

For CI, leave the checkbox off and export these instead — `BuildScript` reads them and configures
signing at build time:

```
MOBCLASH_KEYSTORE_PATH   absolute path to mobclash.keystore
MOBCLASH_KEYSTORE_PASS   store password
MOBCLASH_KEY_ALIAS       mobclash
MOBCLASH_KEY_PASS        key password
```

Without them the build still succeeds, signed with Unity's debug key — fine for sideloading and QA,
rejected by the Play Store.

---

## 4. Build

### From the editor

1. `Tools ▸ Mob Clash ▸ 4. Setup Everything`
2. `Tools ▸ Mob Clash ▸ 5. Build Playable Scene` (adds `Assets/Scenes/Game.unity` to Build Settings)
3. `Tools ▸ Mob Clash ▸ 6. Build Android APK`

Output: `Builds/Android/MobClash_GateSiege.apk`.

Or the stock route — `File ▸ Build Settings`, confirm `Scenes/Game` is ticked and first in the list,
**Build App Bundle (Google Play)** *unticked* for an APK, then **Build**.

### From the command line

```bash
# Windows
"C:\Program Files\Unity\Hub\Editor\<version>\Editor\Unity.exe" ^
  -quit -batchmode -nographics ^
  -projectPath "%CD%" ^
  -executeMethod MobClash.EditorTools.BuildScript.CommandLineAndroid ^
  -logFile build.log

# macOS / Linux
Unity -quit -batchmode -nographics \
  -projectPath "$PWD" \
  -executeMethod MobClash.EditorTools.BuildScript.CommandLineAndroid \
  -logFile build.log
```

`BuildScript` applies the player settings, resolves the enabled scenes, configures signing from the
environment and exits with code 1 on failure, so it drops straight into CI.

### Install and verify

```
adb install -r Builds/Android/MobClash_GateSiege.apk
adb logcat -s Unity          # watch for the [MobClash] logs
```

Smoke test on device: gates fire exactly once per row, the crowd counter matches the units on
screen, haptics fire on gate pass and room capture, the mock interstitial appears from level 3
onward, and the rewarded button triples the payout.

### Play Store

Store submissions need an **AAB**, not an APK: tick `Build App Bundle (Google Play)` in Build
Settings (or set `EditorUserBuildSettings.buildAppBundle = true`) and build again. The APK stays the
right format for direct distribution, QA rigs and ad-network test flights.

---

## 5. Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `Gradle build failed` / `SDK platform not found` | Missing target SDK platform. Open Android Studio's SDK Manager and install the latest platform, or set Target API to a level you have. |
| `Unable to locate JDK/NDK` | Reinstall the module from Unity Hub; do not hand-edit the paths in External Tools. |
| IL2CPP build takes 20+ minutes | Normal for a first build. Later builds reuse the IL2CPP cache; keep `Library/` between builds in CI. |
| App installs but shows a black screen | The scene is not in Build Settings. Re-run `5. Build Playable Scene`. |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | A previous install used a different signing key. `adb uninstall com.viralgames.mobclash` first. |
| No vibration on device | System-level haptics are off, or the device has no vibrator. `HapticFeedback` degrades to a no-op by design. |
