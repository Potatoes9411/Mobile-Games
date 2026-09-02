using System.Collections.Generic;
using UnityEngine;

namespace MobClash.Core
{
    /// <summary>
    /// Builds every visual the game needs out of Unity primitives and code generated materials.
    /// This keeps the repository free of binary art assets while still producing a shippable build:
    /// drop the scripts into an empty project, press Play, and the full game renders.
    /// Replace the factory calls with real prefabs later by assigning the prefab fields on
    /// LevelBuilder / SwarmManager / SiegeManager.
    /// </summary>
    public static class PrimitiveFactory
    {
        private static readonly Dictionary<int, Material> MaterialCache = new Dictionary<int, Material>(32);
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
            if (material.HasProperty("_Smoothness")) material.SetFloat("_Smoothness", 0.15f);
            if (material.HasProperty("_Glossiness")) material.SetFloat("_Glossiness", 0.15f);
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

        /// <summary>A single crowd member. Colliderless by design: 500+ colliders would destroy mobile perf.</summary>
        public static GameObject CreateSwarmUnit(Color color)
        {
            GameObject root = new GameObject("SwarmUnit");
            root.layer = GameLayers.SwarmUnitLayer;

            GameObject body = Primitive(PrimitiveType.Capsule, "Body", color, false);
            body.transform.SetParent(root.transform, false);
            body.transform.localScale = new Vector3(0.34f, 0.34f, 0.34f);
            body.transform.localPosition = new Vector3(0f, 0.34f, 0f);
            body.layer = GameLayers.SwarmUnitLayer;

            GameObject head = Primitive(PrimitiveType.Sphere, "Head", Color.Lerp(color, Color.white, 0.55f), false);
            head.transform.SetParent(root.transform, false);
            head.transform.localScale = new Vector3(0.26f, 0.26f, 0.26f);
            head.transform.localPosition = new Vector3(0f, 0.72f, 0f);
            head.layer = GameLayers.SwarmUnitLayer;

            SwarmVisual visual = root.AddComponent<SwarmVisual>();
            visual.body = body.transform;

            return root;
        }

        /// <summary>One half of a math gate: a translucent slab plus a world space number label.</summary>
        public static GameObject CreateGateHalf()
        {
            GameObject root = new GameObject("Gate");
            root.layer = GameLayers.GateLayer;
            SafeSetTag(root, GameLayers.TagGate);

            GameObject slab = Primitive(PrimitiveType.Cube, "Slab", new Color(0.2f, 0.85f, 0.4f, 1f), false);
            slab.transform.SetParent(root.transform, false);
            slab.transform.localScale = new Vector3(5.4f, 3.2f, 0.25f);
            slab.transform.localPosition = new Vector3(0f, 1.6f, 0f);
            slab.layer = GameLayers.GateLayer;

            BoxCollider trigger = root.AddComponent<BoxCollider>();
            trigger.isTrigger = true;
            trigger.size = new Vector3(5.4f, 3.2f, 1.0f);
            trigger.center = new Vector3(0f, 1.6f, 0f);

            GameObject label = CreateWorldText("Label", "x2", 0.6f);
            label.transform.SetParent(root.transform, false);
            label.transform.localPosition = new Vector3(0f, 1.9f, -0.3f);

            return root;
        }

        public static GameObject CreateObstacle()
        {
            GameObject root = new GameObject("Obstacle");
            root.layer = GameLayers.ObstacleLayer;
            SafeSetTag(root, GameLayers.TagObstacle);

            GameObject body = Primitive(PrimitiveType.Cylinder, "Body", new Color(0.85f, 0.18f, 0.22f, 1f), false);
            body.transform.SetParent(root.transform, false);
            body.transform.localScale = new Vector3(1.6f, 1.2f, 1.6f);
            body.transform.localPosition = new Vector3(0f, 1.2f, 0f);
            body.layer = GameLayers.ObstacleLayer;

            SphereCollider trigger = root.AddComponent<SphereCollider>();
            trigger.isTrigger = true;
            trigger.radius = 1.1f;
            trigger.center = new Vector3(0f, 1.1f, 0f);

            return root;
        }

        public static GameObject CreateTowerNode()
        {
            GameObject root = new GameObject("TowerNode");
            root.layer = GameLayers.TowerNodeLayer;
            SafeSetTag(root, GameLayers.TagTowerNode);

            GameObject body = Primitive(PrimitiveType.Cube, "Body", new Color(0.9f, 0.35f, 0.3f, 1f), true);
            body.transform.SetParent(root.transform, false);
            body.transform.localScale = new Vector3(3.2f, 2.4f, 3.2f);
            body.transform.localPosition = new Vector3(0f, 1.2f, 0f);
            body.layer = GameLayers.TowerNodeLayer;

            GameObject label = CreateWorldText("Label", "0", 0.8f);
            label.transform.SetParent(root.transform, false);
            label.transform.localPosition = new Vector3(0f, 1.4f, -1.75f);

            return root;
        }

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

        public static GameObject CreateGround(float length, float width)
        {
            GameObject root = new GameObject("Ground");
            root.layer = GameLayers.TrackLayer;
            SafeSetTag(root, GameLayers.TagTrack);

            GameObject floor = Primitive(PrimitiveType.Cube, "Floor", new Color(0.18f, 0.21f, 0.30f, 1f), false);
            floor.transform.SetParent(root.transform, false);
            floor.transform.localScale = new Vector3(width, 0.5f, length);
            floor.transform.localPosition = new Vector3(0f, -0.25f, length * 0.5f);
            floor.layer = GameLayers.TrackLayer;

            Color railColor = new Color(0.30f, 0.36f, 0.52f, 1f);
            for (int side = -1; side <= 1; side += 2)
            {
                GameObject rail = Primitive(PrimitiveType.Cube, side < 0 ? "RailLeft" : "RailRight", railColor, false);
                rail.transform.SetParent(root.transform, false);
                rail.transform.localScale = new Vector3(0.6f, 1.1f, length);
                rail.transform.localPosition = new Vector3(side * (width * 0.5f + 0.3f), 0.3f, length * 0.5f);
                rail.layer = GameLayers.TrackLayer;
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
