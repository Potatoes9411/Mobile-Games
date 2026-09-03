using System.IO;
using MobClash.Ads;
using MobClash.Core;
using MobClash.Juice;
using MobClash.Level;
using MobClash.Meta;
using MobClash.Player;
using MobClash.Siege;
using MobClash.Swarm;
using MobClash.UI;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace MobClash.EditorTools
{
    /// <summary>
    /// Generates the complete, playable scene described in docs/SCENE_SETUP.md:
    /// Managers, Camera Rig, Player, Swarm, Level root, Tower root and the UI canvas,
    /// all wired together. Run it once and press Play.
    /// </summary>
    public static class SceneBootstrapper
    {
        public const string ScenesFolder = "Assets/Scenes";
        public const string ScenePath = "Assets/Scenes/Game.unity";

        [MenuItem("Tools/Mob Clash/5. Build Playable Scene", false, 20)]
        public static void BuildScene()
        {
            if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;

            ProjectSetup.SetupLayersAndTags();

            Scene scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

            CreateEnvironment();
            CameraRig cameraRig = CreateCameraRig();
            PlayerSwerve player = CreatePlayer();
            SwarmManager swarm = CreateSwarm(player.transform);
            LevelBuilder levelBuilder = CreateLevelRoot();
            SiegeManager siege = CreateSiege(swarm);
            CreateManagers(cameraRig, player, swarm, levelBuilder, siege);

            if (!Directory.Exists(ScenesFolder)) Directory.CreateDirectory(ScenesFolder);
            AssetDatabase.Refresh();

            EditorSceneManager.MarkSceneDirty(scene);
            EditorSceneManager.SaveScene(scene, ScenePath);

            RegisterSceneInBuildSettings();

            Debug.Log("[MobClash] Scene generated at " + ScenePath + ". Press Play to run the game.");
        }

        /// <summary>
        /// Golden hour lighting to match the palette: a low warm key light, a violet sky bounce and
        /// linear fog in the horizon colour so distant geometry dissolves instead of popping.
        /// </summary>
        private static void CreateEnvironment()
        {
            GameObject lightObject = new GameObject("Sun");
            Light light = lightObject.AddComponent<Light>();
            light.type = LightType.Directional;
            light.color = Palette.Sun;
            light.intensity = 1.15f;
            light.shadows = LightShadows.None;
            lightObject.transform.rotation = Quaternion.Euler(24f, 12f, 0f);

            GameObject fillObject = new GameObject("Sky Fill");
            Light fill = fillObject.AddComponent<Light>();
            fill.type = LightType.Directional;
            fill.color = Palette.SkyMid;
            fill.intensity = 0.35f;
            fill.shadows = LightShadows.None;
            fillObject.transform.rotation = Quaternion.Euler(140f, -40f, 0f);

            RenderSettings.ambientMode = UnityEngine.Rendering.AmbientMode.Trilight;
            RenderSettings.ambientSkyColor = Palette.SkyMid;
            RenderSettings.ambientEquatorColor = Palette.Fog;
            RenderSettings.ambientGroundColor = Palette.GrassDark;

            RenderSettings.fog = true;
            RenderSettings.fogMode = FogMode.Linear;
            RenderSettings.fogColor = Palette.Fog;
            RenderSettings.fogStartDistance = 45f;
            RenderSettings.fogEndDistance = 210f;
        }

        private static CameraRig CreateCameraRig()
        {
            GameObject rig = new GameObject("CameraRig");

            Camera camera = rig.AddComponent<Camera>();
            camera.tag = "MainCamera";
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Palette.Fog;
            camera.fieldOfView = 58f;
            camera.nearClipPlane = 0.3f;
            camera.farClipPlane = 400f;
            camera.allowHDR = false;
            camera.allowMSAA = false;

            rig.AddComponent<AudioListener>();

            CameraRig cameraRig = rig.AddComponent<CameraRig>();
            rig.transform.position = new Vector3(0f, 9.5f, -11.5f);
            rig.transform.rotation = Quaternion.Euler(26f, 0f, 0f);

            return cameraRig;
        }

        private static PlayerSwerve CreatePlayer()
        {
            GameObject playerObject = new GameObject("Player");
            playerObject.layer = GameLayers.PlayerLayer;
            PrimitiveFactory.SafeSetTag(playerObject, GameLayers.TagPlayer);

            Rigidbody body = playerObject.AddComponent<Rigidbody>();
            body.isKinematic = true;
            body.useGravity = false;

            SphereCollider collider = playerObject.AddComponent<SphereCollider>();
            collider.isTrigger = true;
            collider.radius = 0.75f;
            collider.center = new Vector3(0f, 0.75f, 0f);

            return playerObject.AddComponent<PlayerSwerve>();
        }

        private static SwarmManager CreateSwarm(Transform pivot)
        {
            GameObject swarmObject = new GameObject("Swarm");

            SwarmManager swarm = swarmObject.AddComponent<SwarmManager>();
            swarm.pivot = pivot;

            ObjectPool pool = swarmObject.AddComponent<ObjectPool>();
            pool.prewarmCount = 320;
            pool.hardCap = 700;
            swarm.unitPool = pool;

            GameObject poolParent = new GameObject("PoolParent");
            poolParent.transform.SetParent(swarmObject.transform, false);
            pool.poolParent = poolParent.transform;

            return swarm;
        }

        private static LevelBuilder CreateLevelRoot()
        {
            GameObject levelObject = new GameObject("Level");
            LevelBuilder builder = levelObject.AddComponent<LevelBuilder>();

            GameObject content = new GameObject("LevelContent");
            content.transform.SetParent(levelObject.transform, false);
            builder.contentRoot = content.transform;

            return builder;
        }

        private static SiegeManager CreateSiege(SwarmManager swarm)
        {
            GameObject siegeObject = new GameObject("Siege");
            SiegeManager siege = siegeObject.AddComponent<SiegeManager>();
            siege.swarm = swarm;

            GameObject towerRoot = new GameObject("TowerRoot");
            towerRoot.transform.SetParent(siegeObject.transform, false);
            siege.towerRoot = towerRoot.transform;

            return siege;
        }

        private static void CreateManagers(CameraRig cameraRig, PlayerSwerve player, SwarmManager swarm,
            LevelBuilder levelBuilder, SiegeManager siege)
        {
            GameObject managers = new GameObject("Managers");

            managers.AddComponent<EconomyManager>();
            managers.AddComponent<AdManager>();

            JuiceManager juice = managers.AddComponent<JuiceManager>();
            juice.cameraRig = cameraRig;

            GameManager gameManager = managers.AddComponent<GameManager>();
            gameManager.cameraRig = cameraRig;
            gameManager.player = player;
            gameManager.swarm = swarm;
            gameManager.levelBuilder = levelBuilder;
            gameManager.siege = siege;

            cameraRig.SetTarget(player.transform);

            GameObject uiObject = new GameObject("UI");
            uiObject.AddComponent<UIManager>();
        }

        private static void RegisterSceneInBuildSettings()
        {
            EditorBuildSettingsScene[] existing = EditorBuildSettings.scenes;

            for (int i = 0; i < existing.Length; i++)
            {
                if (existing[i].path == ScenePath)
                {
                    existing[i].enabled = true;
                    EditorBuildSettings.scenes = existing;
                    return;
                }
            }

            EditorBuildSettingsScene[] updated = new EditorBuildSettingsScene[existing.Length + 1];
            System.Array.Copy(existing, updated, existing.Length);
            updated[existing.Length] = new EditorBuildSettingsScene(ScenePath, true);
            EditorBuildSettings.scenes = updated;
        }
    }
}
