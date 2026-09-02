using System.Collections.Generic;
using MobClash.Core;
using MobClash.Data;
using MobClash.Track;
using UnityEngine;

namespace MobClash.Level
{
    /// <summary>
    /// Turns a <see cref="LevelData"/> description into scene geometry: ground, rails, gate rows,
    /// hazards and the finish trigger. Prefab fields are optional; when they are empty the
    /// PrimitiveFactory produces stand-in geometry so the game is playable with zero art assets.
    /// </summary>
    public class LevelBuilder : MonoBehaviour
    {
        [Header("Prefabs (optional)")]
        public GameObject gatePrefab;
        public GameObject obstaclePrefab;
        public GameObject groundPrefab;

        [Header("Wiring")]
        public Transform contentRoot;
        public FinishLine finishLine;

        private readonly List<GameObject> _spawned = new List<GameObject>(64);
        private LevelData _current;

        public LevelData Current { get { return _current; } }

        private void Awake()
        {
            if (contentRoot == null)
            {
                GameObject root = new GameObject("LevelContent");
                root.transform.SetParent(transform, false);
                contentRoot = root.transform;
            }
        }

        /// <summary>Destroys the previous level and instantiates the new one.</summary>
        public void Build(LevelData data)
        {
            Clear();
            if (data == null) return;

            _current = data;

            BuildGround(data);
            BuildGates(data);
            BuildObstacles(data);
            BuildFinishLine(data);
        }

        public void Clear()
        {
            for (int i = 0; i < _spawned.Count; i++)
            {
                if (_spawned[i] != null) Destroy(_spawned[i]);
            }

            _spawned.Clear();
            _current = null;
        }

        private void Register(GameObject instance)
        {
            if (instance != null) _spawned.Add(instance);
        }

        private void BuildGround(LevelData data)
        {
            float length = data.TowerDistance + 40f;
            GameObject ground;

            if (groundPrefab != null)
            {
                ground = Instantiate(groundPrefab, contentRoot);
                ground.transform.localPosition = Vector3.zero;
                ground.transform.localScale = new Vector3(
                    data.trackHalfWidth * 2f, 1f, length);
            }
            else
            {
                ground = PrimitiveFactory.CreateGround(length, data.trackHalfWidth * 2f);
                ground.transform.SetParent(contentRoot, false);
                ground.transform.localPosition = new Vector3(0f, 0f, -12f);
            }

            Register(ground);
        }

        private void BuildGates(LevelData data)
        {
            if (data.gates == null) return;

            Dictionary<int, Gate> rowFirstHalf = new Dictionary<int, Gate>(8);

            for (int i = 0; i < data.gates.Length; i++)
            {
                GateSpec spec = data.gates[i];

                GameObject instance = gatePrefab != null
                    ? Instantiate(gatePrefab, contentRoot)
                    : PrimitiveFactory.CreateGateHalf();

                if (gatePrefab == null) instance.transform.SetParent(contentRoot, false);

                Gate gate = instance.GetComponent<Gate>();
                if (gate == null) gate = instance.AddComponent<Gate>();

                gate.Configure(spec, data.trackHalfWidth);
                Register(instance);

                Gate sibling;
                if (rowFirstHalf.TryGetValue(spec.rowId, out sibling) && sibling != null)
                {
                    gate.sibling = sibling;
                    sibling.sibling = gate;
                }
                else
                {
                    rowFirstHalf[spec.rowId] = gate;
                }
            }
        }

        private void BuildObstacles(LevelData data)
        {
            if (data.obstacles == null) return;

            for (int i = 0; i < data.obstacles.Length; i++)
            {
                ObstacleSpec spec = data.obstacles[i];

                GameObject instance = obstaclePrefab != null
                    ? Instantiate(obstaclePrefab, contentRoot)
                    : PrimitiveFactory.CreateObstacle();

                if (obstaclePrefab == null) instance.transform.SetParent(contentRoot, false);

                Obstacle obstacle = instance.GetComponent<Obstacle>();
                if (obstacle == null) obstacle = instance.AddComponent<Obstacle>();

                obstacle.Configure(spec, data.trackHalfWidth);
                Register(instance);
            }
        }

        private void BuildFinishLine(LevelData data)
        {
            if (finishLine == null)
            {
                GameObject line = new GameObject("FinishLine");
                line.transform.SetParent(contentRoot, false);
                finishLine = line.AddComponent<FinishLine>();

                BoxCollider collider = line.GetComponent<BoxCollider>();
                if (collider == null) collider = line.AddComponent<BoxCollider>();
                collider.isTrigger = true;

                line.layer = GameLayers.GateLayer;
            }

            BoxCollider box = finishLine.GetComponent<BoxCollider>();
            if (box != null)
            {
                box.isTrigger = true;
                box.size = new Vector3(data.trackHalfWidth * 2.4f, 6f, 1.5f);
                box.center = new Vector3(0f, 3f, 0f);
            }

            finishLine.transform.position = new Vector3(0f, 0f, data.trackLength);
            finishLine.ResetLine();
        }
    }
}
