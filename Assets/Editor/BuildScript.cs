using System;
using System.IO;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace MobClash.EditorTools
{
    /// <summary>
    /// One click and command line builds for Android (APK), Windows x64 (EXE) and WebGL.
    ///
    /// Command line usage (see docs/ANDROID_BUILD.md and docs/WINDOWS_AND_WEB_BUILD.md):
    ///   Unity -quit -batchmode -nographics -projectPath . -executeMethod MobClash.EditorTools.BuildScript.CommandLineAndroid
    ///   Unity -quit -batchmode -nographics -projectPath . -executeMethod MobClash.EditorTools.BuildScript.CommandLineWindows
    ///   Unity -quit -batchmode -nographics -projectPath . -executeMethod MobClash.EditorTools.BuildScript.CommandLineWebGL
    ///
    /// Release signing is read from environment variables so no secret ever lands in the repository:
    ///   MOBCLASH_KEYSTORE_PATH, MOBCLASH_KEYSTORE_PASS, MOBCLASH_KEY_ALIAS, MOBCLASH_KEY_PASS
    /// </summary>
    public static class BuildScript
    {
        public const string OutputFolder = "Builds";
        public const string ApkName = "MobClash_GateSiege.apk";
        public const string WindowsExeName = "MobClashGateSiege.exe";

        // ------------------------------------------------------------------ menu items

        [MenuItem("Tools/Mob Clash/6. Build Android APK", false, 30)]
        public static void BuildAndroid()
        {
            RunBuild(BuildTarget.Android, Path.Combine(OutputFolder, "Android"), ApkName, true);
        }

        [MenuItem("Tools/Mob Clash/7. Build Windows x64 EXE", false, 31)]
        public static void BuildWindows()
        {
            RunBuild(BuildTarget.StandaloneWindows64, Path.Combine(OutputFolder, "Windows"), WindowsExeName, false);
        }

        [MenuItem("Tools/Mob Clash/8. Build WebGL", false, 32)]
        public static void BuildWebGL()
        {
            RunBuild(BuildTarget.WebGL, Path.Combine(OutputFolder, "WebGL"), string.Empty, false);
        }

        // ------------------------------------------------------------------ command line entry points

        public static void CommandLineAndroid()
        {
            BuildAndroid();
        }

        public static void CommandLineWindows()
        {
            BuildWindows();
        }

        public static void CommandLineWebGL()
        {
            BuildWebGL();
        }

        // ------------------------------------------------------------------ implementation

        private static void RunBuild(BuildTarget target, string folder, string fileName, bool android)
        {
            ProjectSetup.ApplyPlayerSettings();

            string[] scenes = GetEnabledScenes();
            if (scenes.Length == 0)
            {
                Fail("No scenes are enabled in Build Settings. Run Tools/Mob Clash/5. Build Playable Scene first.");
                return;
            }

            string outputFolder = ResolveOutputFolder(folder);
            Directory.CreateDirectory(outputFolder);

            string locationPath = string.IsNullOrEmpty(fileName)
                ? outputFolder
                : Path.Combine(outputFolder, fileName);

            if (android) ConfigureAndroidSigning();

            BuildTargetGroup group = BuildPipeline.GetBuildTargetGroup(target);
            if (!EditorUserBuildSettings.SwitchActiveBuildTarget(group, target))
            {
                Fail("Could not switch the active build target to " + target +
                     ". Install the matching module through Unity Hub.");
                return;
            }

            BuildPlayerOptions options = new BuildPlayerOptions();
            options.scenes = scenes;
            options.target = target;
            options.targetGroup = group;
            options.locationPathName = locationPath;
            options.options = BuildOptions.None;

            Debug.Log("[MobClash] Building " + target + " -> " + locationPath);

            BuildReport report = BuildPipeline.BuildPlayer(options);
            BuildSummary summary = report.summary;

            if (summary.result == BuildResult.Succeeded)
            {
                Debug.Log("[MobClash] Build succeeded: " + locationPath +
                          "  (" + (summary.totalSize / (1024f * 1024f)).ToString("0.0") + " MB, " +
                          summary.totalTime.TotalSeconds.ToString("0") + "s)");
            }
            else
            {
                Fail("Build " + summary.result + " with " + summary.totalErrors + " error(s).");
            }
        }

        private static void ConfigureAndroidSigning()
        {
            EditorUserBuildSettings.buildAppBundle = false;

            string keystorePath = Environment.GetEnvironmentVariable("MOBCLASH_KEYSTORE_PATH");
            string keystorePass = Environment.GetEnvironmentVariable("MOBCLASH_KEYSTORE_PASS");
            string keyAlias = Environment.GetEnvironmentVariable("MOBCLASH_KEY_ALIAS");
            string keyPass = Environment.GetEnvironmentVariable("MOBCLASH_KEY_PASS");

            bool hasKeystore = !string.IsNullOrEmpty(keystorePath) &&
                               !string.IsNullOrEmpty(keystorePass) &&
                               !string.IsNullOrEmpty(keyAlias) &&
                               !string.IsNullOrEmpty(keyPass);

            if (!hasKeystore)
            {
                PlayerSettings.Android.useCustomKeystore = false;
                Debug.LogWarning("[MobClash] No release keystore in the environment. " +
                                 "Building with Unity's debug key: fine for sideloading, not for the Play Store.");
                return;
            }

            PlayerSettings.Android.useCustomKeystore = true;
            PlayerSettings.Android.keystoreName = keystorePath;
            PlayerSettings.Android.keystorePass = keystorePass;
            PlayerSettings.Android.keyaliasName = keyAlias;
            PlayerSettings.Android.keyaliasPass = keyPass;

            Debug.Log("[MobClash] Release signing configured from environment variables.");
        }

        private static string ResolveOutputFolder(string relativeFolder)
        {
            string overridePath = Environment.GetEnvironmentVariable("MOBCLASH_OUTPUT");
            if (!string.IsNullOrEmpty(overridePath)) return overridePath;

            string projectRoot = Directory.GetParent(Application.dataPath).FullName;
            return Path.Combine(projectRoot, relativeFolder);
        }

        private static string[] GetEnabledScenes()
        {
            EditorBuildSettingsScene[] all = EditorBuildSettings.scenes;
            int count = 0;

            for (int i = 0; i < all.Length; i++)
            {
                if (all[i].enabled && File.Exists(all[i].path)) count++;
            }

            string[] scenes = new string[count];
            int index = 0;

            for (int i = 0; i < all.Length; i++)
            {
                if (all[i].enabled && File.Exists(all[i].path)) scenes[index++] = all[i].path;
            }

            return scenes;
        }

        private static void Fail(string message)
        {
            Debug.LogError("[MobClash] " + message);

            if (Application.isBatchMode) EditorApplication.Exit(1);
        }
    }
}
