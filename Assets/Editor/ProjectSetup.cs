using MobClash.Core;
using UnityEditor;
using UnityEngine;

namespace MobClash.EditorTools
{
    /// <summary>
    /// One click project configuration: layers, tags, the physics collision matrix and the
    /// Player Settings required by the Android and Windows build pipelines.
    /// Everything it does is also documented step by step in docs/SCENE_SETUP.md.
    /// </summary>
    public static class ProjectSetup
    {
        private const string TagManagerPath = "ProjectSettings/TagManager.asset";
        private const string DynamicsManagerPath = "ProjectSettings/DynamicsManager.asset";

        [MenuItem("Tools/Mob Clash/1. Setup Project Layers and Tags", false, 10)]
        public static void SetupLayersAndTags()
        {
            Object[] assets = AssetDatabase.LoadAllAssetsAtPath(TagManagerPath);
            if (assets == null || assets.Length == 0)
            {
                Debug.LogError("[MobClash] Could not open " + TagManagerPath);
                return;
            }

            SerializedObject tagManager = new SerializedObject(assets[0]);

            SerializedProperty layers = tagManager.FindProperty("layers");
            if (layers != null)
            {
                for (int i = 0; i < GameLayers.OrderedLayerNames.Length; i++)
                {
                    int index = GameLayers.PlayerIndex + i;
                    if (index >= layers.arraySize) break;

                    SerializedProperty element = layers.GetArrayElementAtIndex(index);
                    if (element.stringValue != GameLayers.OrderedLayerNames[i])
                    {
                        element.stringValue = GameLayers.OrderedLayerNames[i];
                    }
                }
            }

            SerializedProperty tags = tagManager.FindProperty("tags");
            if (tags != null)
            {
                for (int i = 0; i < GameLayers.RequiredTags.Length; i++)
                {
                    string wanted = GameLayers.RequiredTags[i];
                    if (wanted == "Player") continue; // built in tag

                    bool exists = false;
                    for (int t = 0; t < tags.arraySize; t++)
                    {
                        if (tags.GetArrayElementAtIndex(t).stringValue == wanted)
                        {
                            exists = true;
                            break;
                        }
                    }

                    if (exists) continue;

                    tags.InsertArrayElementAtIndex(tags.arraySize);
                    tags.GetArrayElementAtIndex(tags.arraySize - 1).stringValue = wanted;
                }
            }

            tagManager.ApplyModifiedPropertiesWithoutUndo();
            AssetDatabase.SaveAssets();

            Debug.Log("[MobClash] Layers 8-13 and gameplay tags configured.");
        }

        [MenuItem("Tools/Mob Clash/2. Apply Physics Collision Matrix", false, 11)]
        public static void ApplyCollisionMatrix()
        {
            // Runtime application always happens in GameManager.Awake; this writes the same rules
            // into ProjectSettings so the editor matrix matches what ships.
            Object[] assets = AssetDatabase.LoadAllAssetsAtPath(DynamicsManagerPath);
            if (assets == null || assets.Length == 0)
            {
                Debug.LogWarning("[MobClash] Could not open " + DynamicsManagerPath +
                                 ". The runtime matrix in GameManager still applies.");
                return;
            }

            SerializedObject dynamics = new SerializedObject(assets[0]);
            SerializedProperty matrix = dynamics.FindProperty("m_LayerCollisionMatrix");

            if (matrix == null || !matrix.isArray)
            {
                Debug.LogWarning("[MobClash] m_LayerCollisionMatrix not found. " +
                                 "Set the matrix by hand using the table in docs/SCENE_SETUP.md.");
                return;
            }

            uint[] masks = BuildCollisionMasks();

            for (int layer = 0; layer < matrix.arraySize && layer < masks.Length; layer++)
            {
                SerializedProperty element = matrix.GetArrayElementAtIndex(layer);
                element.longValue = masks[layer];
            }

            dynamics.ApplyModifiedPropertiesWithoutUndo();
            AssetDatabase.SaveAssets();

            Debug.Log("[MobClash] Physics collision matrix written: only Player x Gate and " +
                      "Player x Obstacle remain enabled among the gameplay layers.");
        }

        /// <summary>
        /// Starts from "everything collides" and removes every pair that involves a project layer,
        /// then re-enables the two pairs the game actually needs.
        /// </summary>
        private static uint[] BuildCollisionMasks()
        {
            uint[] masks = new uint[32];
            for (int i = 0; i < masks.Length; i++) masks[i] = 0xFFFFFFFF;

            int[] projectLayers =
            {
                GameLayers.PlayerIndex,
                GameLayers.SwarmUnitIndex,
                GameLayers.GateIndex,
                GameLayers.ObstacleIndex,
                GameLayers.TrackIndex,
                GameLayers.TowerNodeIndex
            };

            for (int i = 0; i < projectLayers.Length; i++)
            {
                for (int j = 0; j < projectLayers.Length; j++)
                {
                    Disable(masks, projectLayers[i], projectLayers[j]);
                }
            }

            Enable(masks, GameLayers.PlayerIndex, GameLayers.GateIndex);
            Enable(masks, GameLayers.PlayerIndex, GameLayers.ObstacleIndex);

            return masks;
        }

        private static void Disable(uint[] masks, int a, int b)
        {
            masks[a] &= ~(1u << b);
            masks[b] &= ~(1u << a);
        }

        private static void Enable(uint[] masks, int a, int b)
        {
            masks[a] |= 1u << b;
            masks[b] |= 1u << a;
        }

        [MenuItem("Tools/Mob Clash/3. Apply Player Settings", false, 12)]
        public static void ApplyPlayerSettings()
        {
            PlayerSettings.companyName = "ViralGames";
            PlayerSettings.productName = "Mob Clash: Gate Siege";

            PlayerSettings.defaultInterfaceOrientation = UIOrientation.Portrait;
            PlayerSettings.allowedAutorotateToPortrait = true;
            PlayerSettings.allowedAutorotateToPortraitUpsideDown = false;
            PlayerSettings.allowedAutorotateToLandscapeLeft = false;
            PlayerSettings.allowedAutorotateToLandscapeRight = false;

            PlayerSettings.colorSpace = ColorSpace.Linear;
            PlayerSettings.gpuSkinning = true;

            try
            {
                PlayerSettings.SetApplicationIdentifier(BuildTargetGroup.Android, "com.viralgames.mobclash");
                PlayerSettings.SetScriptingBackend(BuildTargetGroup.Android, ScriptingImplementation.IL2CPP);
                PlayerSettings.SetApiCompatibilityLevel(BuildTargetGroup.Android, ApiCompatibilityLevel.NET_Standard_2_0);
                PlayerSettings.Android.targetArchitectures = AndroidArchitecture.ARM64;
                PlayerSettings.Android.minSdkVersion = AndroidSdkVersions.AndroidApiLevel26;
                PlayerSettings.Android.targetSdkVersion = AndroidSdkVersions.AndroidApiLevelAuto;
                PlayerSettings.Android.forceInternetPermission = true;
                PlayerSettings.Android.bundleVersionCode = Mathf.Max(1, PlayerSettings.Android.bundleVersionCode);
                PlayerSettings.Android.optimizedFramePacing = true;
            }
            catch (System.Exception exception)
            {
                Debug.LogWarning("[MobClash] Some Android settings were not applied: " + exception.Message);
            }

            try
            {
                PlayerSettings.SetApplicationIdentifier(BuildTargetGroup.Standalone, "com.viralgames.mobclash");
                PlayerSettings.SetScriptingBackend(BuildTargetGroup.Standalone, ScriptingImplementation.Mono2x);
                PlayerSettings.defaultScreenWidth = 540;
                PlayerSettings.defaultScreenHeight = 960;
                PlayerSettings.resizableWindow = true;
                PlayerSettings.fullScreenMode = FullScreenMode.Windowed;
                PlayerSettings.runInBackground = true;
            }
            catch (System.Exception exception)
            {
                Debug.LogWarning("[MobClash] Some Standalone settings were not applied: " + exception.Message);
            }

            AssetDatabase.SaveAssets();
            Debug.Log("[MobClash] Player Settings applied for Android (IL2CPP / ARM64 / API 26+) and Windows.");
        }

        [MenuItem("Tools/Mob Clash/4. Setup Everything", false, 13)]
        public static void SetupEverything()
        {
            SetupLayersAndTags();
            ApplyCollisionMatrix();
            ApplyPlayerSettings();
            Debug.Log("[MobClash] Project setup complete. Now run Tools/Mob Clash/5. Build Playable Scene.");
        }
    }
}
