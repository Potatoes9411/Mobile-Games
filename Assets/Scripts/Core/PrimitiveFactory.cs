using System.Collections.Generic;
using UnityEngine;

namespace MobClash.Core
{
    /// <summary>
    /// Builds every visual the game needs out of Unity primitives and code generated materials,
    /// following the palette in <see cref="Palette"/>. This keeps the repository free of binary art
    /// assets while still producing a shippable, art-directed build: drop the scripts into an empty
    /// project, press Play, and the full game renders.
    ///
    /// Replace the factory calls with real prefabs later by assigning the prefab fields on
    /// LevelBuilder / SwarmManager / SiegeManager. Nothing else has to change.
    ///
    /// Renderer budget: a crowd member is four renderers (torso, head, helmet, crest). At the
    /// default 450 rendered units that is ~1800 instanced renderers, which holds 60fps on a mid
    /// range Android device. For low end hardware, either drop SwarmManager.maxVisualUnits or
    /// disable the Helmet and Crest children on the unit prefab to halve the count.
    /// </summary>
    public static class PrimitiveFactory
    {
        private static readonly Dictionary<int, Material> MaterialCache = new Dictionary<int, Material>(48);
        private static Font _builtinFont;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        private static void ResetStatics()
        {
            MaterialCache.Clear();
            _builtinFont = null;
        }

        /// <summary>Finds the best available opaque shader for the active render pipeline.</summary>
        public static Shader OpaqueShader()
        {
            Shader shader = Shader.Find("Universal Render Pipeline/Lit");
            if (shader == null) shader = Shader.Find("HDRP/Lit");
            if (shader == null) shader = Shader.Find("Standard");
            if (shader == null) shader = Shader.Find("Legacy Shaders/Diffuse");
            if (shader == null) shader = Shader.Find("Sprites/Default");
            return shader;
        }

        /// <summary>Returns a cached, GPU-instancing enabled material tinted with the requested colour.</summary>
        public static Material GetMaterial(Color color)
        {
            int key = ColorKey(color);
            Material cached;
            if (MaterialCache.TryGetValue(key, out cached) && cached != null) return cached;

            Material material = new Material(OpaqueShader());
            material.name = "MobClash_" + key.ToString("X8");
            material.enableInstancing = true;
            SetColor(material, color);
            material.hideFlags = HideFlags.DontSaveInEditor;
            MaterialCache[key] = material;
            return material;
        }

        public static void SetColor(Material material, Color color)
        {
            if (material == null) return;
            if (material.HasProperty("_BaseColor")) material.SetColor("_BaseColor", color);
            if (material.HasProperty("_Color")) material.SetColor("_Color", color);
            if (material.HasProperty("_Smoothness")) material.SetFloat("_Smoothness", 0.12f);
            if (material.HasProperty("_Glossiness")) material.SetFloat("_Glossiness", 0.12f);
        }

        public static Font BuiltinFont()
        {
            if (_builtinFont != null) return _builtinFont;

            _builtinFont = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            if (_builtinFont == null) _builtinFont = Resources.GetBuiltinResource<Font>("Arial.ttf");
            if (_builtinFont == null)
            {
                Font[] all = Resources.FindObjectsOfTypeAll<Font>();
                if (all != null && all.Length > 0) _builtinFont = all[0];
            }

            return _builtinFont;
        }

        /// <summary>Assigns a tag only when it exists in the TagManager, so a fresh project never throws.</summary>
        public static void SafeSetTag(GameObject go, string tag)
        {
            if (go == null || string.IsNullOrEmpty(tag)) return;
            try
            {
                go.tag = tag;
            }
            catch (UnityException)
            {
                Debug.LogWarning("[MobClash] Tag '" + tag + "' is not declared. Run Tools/Mob Clash/Setup Project Layers and Tags.");
            }
        }

        public static void SafeDestroy(Object target)
        {
            if (target == null) return;
            if (Application.isPlaying) Object.Destroy(target);
            else Object.DestroyImmediate(target);
        }

        // ------------------------------------------------------------------ building blocks

        private static GameObject Primitive(PrimitiveType type, string name, Color color, bool keepCollider)
        {
            GameObject go = GameObject.CreatePrimitive(type);
            go.name = name;

            Collider collider = go.GetComponent<Collider>();
            if (collider != null && !keepCollider) SafeDestroy(collider);

            Renderer renderer = go.GetComponent<Renderer>();
            if (renderer != null)
            {
                renderer.sharedMaterial = GetMaterial(color);
                renderer.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
                renderer.receiveShadows = false;
                renderer.lightProbeUsage = UnityEngine.Rendering.LightProbeUsage.Off;
                renderer.reflectionProbeUsage = UnityEngine.Rendering.ReflectionProbeUsage.Off;
                renderer.motionVectorGenerationMode = MotionVectorGenerationMode.ForceNoMotion;
            }

            return go;
        }

        /// <summary>Adds a shaped primitive as a child at a local position and scale.</summary>
        private static GameObject Part(Transform parent, PrimitiveType type, string name, Color color,
            Vector3 position, Vector3 scale, int layer)
        {
            GameObject part = Primitive(type, name, color, false);
            part.transform.SetParent(parent, false);
            part.transform.localPosition = position;
            part.transform.localScale = scale;
            part.layer = layer;
            return part;
        }

        // ------------------------------------------------------------------ characters

        /// <summary>
        /// A single crowd member. Colliderless by design: 450+ colliders would destroy mobile perf,
        /// and hazards remove units with a distance check instead (see SwarmManager).
        /// </summary>
        public static GameObject CreateSwarmUnit(Color color)
        {
            return CreateFighter("SwarmUnit", color, Palette.BlueLight, Palette.Gold, GameLayers.SwarmUnitLayer);
        }

        /// <summary>A tower defender. Same silhouette as the mob, opposing colours.</summary>
        public static GameObject CreateDefender()
        {
            return CreateFighter("Defender", Palette.Red, Palette.RedLight, Palette.Stone, GameLayers.TowerNodeLayer);
        }

        /// <summary>
        /// Four part fighter: torso, head, helmet dome and plume. Reads as a character at 20px tall
        /// in a crowd of hundreds, which is the only size that matters here.
        /// </summary>
        public static GameObject CreateFighter(string name, Color body, Color light, Color crest, int layer)
        {
            GameObject root = new GameObject(name);
            root.layer = layer;

            GameObject torso = Part(root.transform, PrimitiveType.Capsule, "Torso", body,
                new Vector3(0f, 0.40f, 0f), new Vector3(0.34f, 0.30f, 0.34f), layer);

            Part(root.transform, PrimitiveType.Sphere, "Head", light,
                new Vector3(0f, 0.78f, 0f), new Vector3(0.26f, 0.26f, 0.26f), layer);

            // Squashed sphere reads as a helmet without a second mesh.
            Part(root.transform, PrimitiveType.Sphere, "Helmet", body,
                new Vector3(0f, 0.83f, 0f), new Vector3(0.30f, 0.20f, 0.30f), layer);

            GameObject plume = Part(root.transform, PrimitiveType.Cube, "Crest", crest,
                new Vector3(0f, 0.94f, 0.02f), new Vector3(0.05f, 0.16f, 0.16f), layer);
            plume.transform.localRotation = Quaternion.Euler(28f, 0f, 0f);

            SwarmVisual visual = root.AddComponent<SwarmVisual>();
            visual.body = torso.transform;

            return root;
        }

        // ------------------------------------------------------------------ gates

        /// <summary>
        /// One half of a math gate, built as an actual structure: two stone posts, a lintel with a
        /// coloured banner, and a curtain of light between them carrying the number.
        /// Gate.Configure resizes the curtain and the trigger so a row's two halves tile the road.
        /// </summary>
        public static GameObject CreateGateHalf()
        {
            GameObject root = new GameObject("Gate");
            root.layer = GameLayers.GateLayer;
            SafeSetTag(root, GameLayers.TagGate);

            int layer = GameLayers.GateLayer;

            // The curtain is what Gate.Configure scales and recolours, so it is named and kept first.
            Part(root.transform, PrimitiveType.Cube, "Slab", Palette.Jade,
                new Vector3(0f, 1.75f, 0f), new Vector3(3f, 3.5f, 0.16f), layer);

            Part(root.transform, PrimitiveType.Cube, "PostLeft", Palette.Stone,
                new Vector3(-1.5f, 2.15f, 0f), new Vector3(0.34f, 4.3f, 0.42f), layer);
            Part(root.transform, PrimitiveType.Cube, "PostRight", Palette.Stone,
                new Vector3(1.5f, 2.15f, 0f), new Vector3(0.34f, 4.3f, 0.42f), layer);

            Part(root.transform, PrimitiveType.Cube, "Lintel", Palette.Stone,
                new Vector3(0f, 4.05f, 0f), new Vector3(3.5f, 0.55f, 0.5f), layer);
            Part(root.transform, PrimitiveType.Cube, "Banner", Palette.Jade,
                new Vector3(0f, 3.95f, -0.28f), new Vector3(3.3f, 0.22f, 0.08f), layer);

            BoxCollider trigger = root.AddComponent<BoxCollider>();
            trigger.isTrigger = true;
            trigger.size = new Vector3(3f, 3.5f, 1.0f);
            trigger.center = new Vector3(0f, 1.75f, 0f);

            GameObject label = CreateWorldText("Label", "x2", 0.85f);
            label.transform.SetParent(root.transform, false);
            label.transform.localPosition = new Vector3(0f, 1.9f, -0.22f);

            return root;
        }

        // ------------------------------------------------------------------ hazards

        /// <summary>A spinning saw on a post, with a ground ring warning where it sweeps.</summary>
        public static GameObject CreateObstacle()
        {
            GameObject root = new GameObject("Obstacle");
            root.layer = GameLayers.ObstacleLayer;
            SafeSetTag(root, GameLayers.TagObstacle);

            int layer = GameLayers.ObstacleLayer;

            Part(root.transform, PrimitiveType.Cylinder, "Ring", Palette.RedDark,
                new Vector3(0f, 0.03f, 0f), new Vector3(2.3f, 0.02f, 2.3f), layer);
            Part(root.transform, PrimitiveType.Cylinder, "Post", Palette.StoneDeep,
                new Vector3(0f, 0.55f, 0f), new Vector3(0.22f, 0.55f, 0.22f), layer);

            GameObject blade = Part(root.transform, PrimitiveType.Cylinder, "Blade", Palette.StoneDark,
                new Vector3(0f, 1.05f, 0f), new Vector3(2.1f, 0.09f, 2.1f), layer);
            Part(blade.transform, PrimitiveType.Cylinder, "Hub", Palette.Red,
                new Vector3(0f, 0.7f, 0f), new Vector3(0.42f, 0.9f, 0.42f), layer);

            SphereCollider trigger = root.AddComponent<SphereCollider>();
            trigger.isTrigger = true;
            trigger.radius = 1.1f;
            trigger.center = new Vector3(0f, 1.0f, 0f);

            return root;
        }

        // ------------------------------------------------------------------ tower

        /// <summary>
        /// A siege room: a stone surround with a recessed interior. TowerNode recolours the
        /// interior and scales the whole thing by defender count.
        /// </summary>
        public static GameObject CreateTowerNode()
        {
            GameObject root = new GameObject("TowerNode");
            root.layer = GameLayers.TowerNodeLayer;
            SafeSetTag(root, GameLayers.TagTowerNode);

            int layer = GameLayers.TowerNodeLayer;

            GameObject surround = Primitive(PrimitiveType.Cube, "Surround", Palette.StoneDark, false);
            surround.transform.SetParent(root.transform, false);
            surround.transform.localPosition = new Vector3(0f, 1.35f, 0.14f);
            surround.transform.localScale = new Vector3(3.5f, 2.9f, 1.0f);
            surround.layer = layer;

            // "Body" is the recess: TowerNode tints this to signal locked / beatable / cleared.
            GameObject body = Primitive(PrimitiveType.Cube, "Body", Palette.RedDark, true);
            body.transform.SetParent(root.transform, false);
            body.transform.localPosition = new Vector3(0f, 1.30f, -0.20f);
            body.transform.localScale = new Vector3(3.0f, 2.4f, 0.9f);
            body.layer = layer;

            // Plaque hangs on the wall below the room so it never fights the defenders inside.
            GameObject plaque = Part(root.transform, PrimitiveType.Cube, "Plaque", Palette.StoneDeep,
                new Vector3(0f, -0.45f, -0.42f), new Vector3(1.9f, 0.62f, 0.16f), layer);

            GameObject label = CreateWorldText("Label", "0", 0.9f);
            label.transform.SetParent(plaque.transform, false);
            label.transform.localPosition = new Vector3(0f, 0f, -0.62f);
            label.transform.localScale = new Vector3(1f / 1.9f, 1f / 0.62f, 1f / 0.16f);

            return root;
        }

        /// <summary>The castle mass the rooms are cut into: wall, buttresses and battlements.</summary>
        public static GameObject CreateCastleShell(float width, float height, float depth)
        {
            GameObject root = new GameObject("CastleShell");
            root.layer = GameLayers.TrackLayer;
            int layer = GameLayers.TrackLayer;

            Part(root.transform, PrimitiveType.Cube, "Wall", Palette.Stone,
                new Vector3(0f, height * 0.5f, depth * 0.5f), new Vector3(width, height, depth), layer);

            float buttress = Mathf.Min(1.3f, width * 0.12f);
            for (int side = -1; side <= 1; side += 2)
            {
                Part(root.transform, PrimitiveType.Cube, side < 0 ? "ButtressLeft" : "ButtressRight",
                    Palette.StoneDark,
                    new Vector3(side * (width * 0.5f - buttress * 0.4f), height * 0.52f, depth * 0.5f - depth * 0.18f),
                    new Vector3(buttress, height * 1.04f, depth * 1.35f), layer);
            }

            int merlons = 9;
            float merlonW = width / merlons;
            for (int i = 0; i < merlons; i += 2)
            {
                float x = -width * 0.5f + merlonW * (i + 0.5f);
                Part(root.transform, PrimitiveType.Cube, "Merlon" + i, Palette.Stone,
                    new Vector3(x, height + 0.42f, depth * 0.5f),
                    new Vector3(merlonW * 0.9f, 0.85f, depth), layer);
            }

            // Gatehouse: a dark recess at the foot of the wall.
            Part(root.transform, PrimitiveType.Cube, "Gatehouse", Palette.Ink,
                new Vector3(0f, 1.0f, -0.05f), new Vector3(2.8f, 2.0f, 0.5f), layer);

            return root;
        }

        /// <summary>Flagpole with a banner. Tint it blue once the tower falls.</summary>
        public static GameObject CreateFlag(Color bannerColor)
        {
            GameObject root = new GameObject("Flag");
            int layer = GameLayers.TrackLayer;

            Part(root.transform, PrimitiveType.Cylinder, "Pole", Palette.StoneDark,
                new Vector3(0f, 1.5f, 0f), new Vector3(0.12f, 1.5f, 0.12f), layer);

            GameObject banner = Part(root.transform, PrimitiveType.Cube, "Banner", bannerColor,
                new Vector3(0.75f, 2.55f, 0f), new Vector3(1.5f, 0.85f, 0.06f), layer);
            banner.name = "Banner";

            return root;
        }

        // ------------------------------------------------------------------ world text

        /// <summary>World space number label built on the built-in <see cref="TextMesh"/> (no package dependency).</summary>
        public static GameObject CreateWorldText(string name, string text, float scale)
        {
            GameObject go = new GameObject(name);

            TextMesh mesh = go.AddComponent<TextMesh>();
            mesh.text = text;
            mesh.characterSize = 0.28f * scale;
            mesh.fontSize = 96;
            mesh.anchor = TextAnchor.MiddleCenter;
            mesh.alignment = TextAlignment.Center;
            mesh.color = Color.white;
            mesh.fontStyle = FontStyle.Bold;

            Font font = BuiltinFont();
            if (font != null)
            {
                mesh.font = font;
                MeshRenderer renderer = go.GetComponent<MeshRenderer>();
                if (renderer != null)
                {
                    renderer.sharedMaterial = font.material;
                    renderer.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
                    renderer.receiveShadows = false;
                }
            }

            return go;
        }

        // ------------------------------------------------------------------ terrain

        /// <summary>
        /// Track surface: a wide grass plain, the sandstone road, lane rungs that sell the speed,
        /// and stone side walls with lit top caps.
        /// </summary>
        public static GameObject CreateGround(float length, float width)
        {
            GameObject root = new GameObject("Ground");
            root.layer = GameLayers.TrackLayer;
            SafeSetTag(root, GameLayers.TagTrack);

            int layer = GameLayers.TrackLayer;
            float halfWidth = width * 0.5f;

            Part(root.transform, PrimitiveType.Cube, "Fields", Palette.Grass,
                new Vector3(0f, -0.55f, length * 0.5f), new Vector3(width + 120f, 0.6f, length + 120f), layer);

            Part(root.transform, PrimitiveType.Cube, "Shoulder", Palette.RoadDark,
                new Vector3(0f, -0.26f, length * 0.5f), new Vector3(width + 3.2f, 0.5f, length), layer);

            Part(root.transform, PrimitiveType.Cube, "Road", Palette.Road,
                new Vector3(0f, -0.22f, length * 0.5f), new Vector3(width, 0.5f, length), layer);

            // Rungs across the road. Spaced every 6m to match the browser build's speed read.
            int rungs = Mathf.Clamp(Mathf.RoundToInt(length / 6f), 0, 90);
            for (int i = 0; i < rungs; i++)
            {
                float z = (i + 0.5f) * (length / rungs);
                Part(root.transform, PrimitiveType.Cube, "Rung" + i, Palette.Lane,
                    new Vector3(0f, 0.015f, z), new Vector3(width - 0.7f, 0.03f, 1.5f), layer);
            }

            for (int side = -1; side <= 1; side += 2)
            {
                float x = side * (halfWidth + 0.5f);
                Part(root.transform, PrimitiveType.Cube, side < 0 ? "WallLeft" : "WallRight", Palette.Wall,
                    new Vector3(x, 0.85f, length * 0.5f), new Vector3(0.7f, 1.75f, length), layer);
                Part(root.transform, PrimitiveType.Cube, side < 0 ? "WallCapLeft" : "WallCapRight", Palette.WallTop,
                    new Vector3(x + side * 0.25f, 1.78f, length * 0.5f), new Vector3(1.1f, 0.18f, length), layer);
            }

            return root;
        }

        public static GameObject CreateFloorSlab(float width, float depth, Color color)
        {
            GameObject slab = Primitive(PrimitiveType.Cube, "FloorSlab", color, false);
            slab.transform.localScale = new Vector3(width, 0.4f, depth);
            slab.layer = GameLayers.TrackLayer;
            return slab;
        }

        // ------------------------------------------------------------------ scenery

        /// <summary>Roadside dressing. Parallax past the camera, which is most of the speed read.</summary>
        public static GameObject CreateTree()
        {
            GameObject root = new GameObject("Tree");
            int layer = GameLayers.TrackLayer;

            Part(root.transform, PrimitiveType.Cylinder, "Trunk", Palette.Trunk,
                new Vector3(0f, 0.75f, 0f), new Vector3(0.28f, 0.75f, 0.28f), layer);

            for (int i = 0; i < 3; i++)
            {
                float y = 1.5f + i * 0.85f;
                float r = 1.5f - i * 0.35f;
                Part(root.transform, PrimitiveType.Cube, "Crown" + i,
                    i == 0 ? Palette.TreeDark : Palette.Tree,
                    new Vector3(0f, y, 0f), new Vector3(r, 0.75f, r), layer)
                    .transform.localRotation = Quaternion.Euler(0f, 45f, 0f);
            }

            return root;
        }

        public static GameObject CreateRock()
        {
            GameObject root = new GameObject("Rock");
            int layer = GameLayers.TrackLayer;

            Part(root.transform, PrimitiveType.Sphere, "Mass", Palette.StoneDeep,
                new Vector3(0f, 0.34f, 0f), new Vector3(1.3f, 0.75f, 1.1f), layer);
            Part(root.transform, PrimitiveType.Sphere, "Chip", Palette.StoneDark,
                new Vector3(-0.35f, 0.52f, 0.1f), new Vector3(0.7f, 0.5f, 0.6f), layer);

            return root;
        }

        public static GameObject CreateBanner()
        {
            GameObject root = new GameObject("Banner");
            int layer = GameLayers.TrackLayer;

            Part(root.transform, PrimitiveType.Cylinder, "Pole", Palette.Trunk,
                new Vector3(0f, 1.3f, 0f), new Vector3(0.1f, 1.3f, 0.1f), layer);
            Part(root.transform, PrimitiveType.Cube, "Cloth", Palette.Red,
                new Vector3(0.5f, 2.2f, 0f), new Vector3(1.0f, 0.6f, 0.05f), layer);

            return root;
        }

        private static int ColorKey(Color color)
        {
            int r = Mathf.Clamp(Mathf.RoundToInt(color.r * 255f), 0, 255);
            int g = Mathf.Clamp(Mathf.RoundToInt(color.g * 255f), 0, 255);
            int b = Mathf.Clamp(Mathf.RoundToInt(color.b * 255f), 0, 255);
            int a = Mathf.Clamp(Mathf.RoundToInt(color.a * 255f), 0, 255);
            return (r << 24) | (g << 16) | (b << 8) | a;
        }
    }
}
