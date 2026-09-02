using System.Collections;
using System.Collections.Generic;
using MobClash.Core;
using MobClash.Data;
using MobClash.Juice;
using MobClash.Swarm;
using UnityEngine;

namespace MobClash.Siege
{
    /// <summary>
    /// Phase 3: the end of run tower puzzle.
    ///
    /// Rules
    ///   * Rooms unlock floor by floor. Only rooms on the lowest uncleared floor can be tapped.
    ///   * A room can be taken when crowd > defenders. Taking it absorbs the defenders
    ///     (crowd += defenders), minus the level's casualty ratio.
    ///   * When no unlocked room is beatable the siege is lost, which is the real puzzle:
    ///     spend the crowd in the right order or stall out.
    /// </summary>
    public class SiegeManager : MonoBehaviour
    {
        [Header("Wiring")]
        public SwarmManager swarm;
        public Transform towerRoot;
        public GameObject nodePrefab;

        [Header("Layout")]
        public float floorHeight = 3.4f;
        public float slotSpacing = 4.4f;
        public float baseHeight = 1.2f;
        public bool buildFloorSlabs = true;

        [Header("Battle pacing")]
        public float chargeDuration = 0.26f;
        public float absorbDuration = 0.30f;
        public float betweenBattlesDelay = 0.12f;
        public float deadlockDelay = 1.1f;

        [Header("Camera")]
        public float towerViewPadding = 2f;

        private readonly List<TowerNode> _nodes = new List<TowerNode>(16);
        private readonly List<GameObject> _spawned = new List<GameObject>(24);
        private LevelData _level;
        private int _unlockedFloor;
        private bool _active;
        private bool _resolving;
        private float _deadlockTimer;
        private Camera _camera;

        public bool Active { get { return _active; } }
        public int UnlockedFloor { get { return _unlockedFloor; } }
        public Vector3 TowerBase { get { return towerRoot != null ? towerRoot.position : Vector3.zero; } }
        public float TowerHeight { get { return baseHeight + floorHeight * Mathf.Max(1, _level != null ? _level.floorCount : 1); } }

        private void Awake()
        {
            if (towerRoot == null)
            {
                GameObject root = new GameObject("TowerRoot");
                root.transform.SetParent(transform, false);
                towerRoot = root.transform;
            }

            _camera = Camera.main;
        }

        /// <summary>Instantiates the tower for the level. Called during the transition, before the siege starts.</summary>
        public void BuildTower(LevelData level)
        {
            ClearTower();
            if (level == null) return;

            _level = level;
            towerRoot.position = new Vector3(0f, 0f, level.TowerDistance);

            int maxPower = 1;
            for (int i = 0; i < level.nodes.Length; i++)
            {
                if (level.nodes[i].power > maxPower) maxPower = level.nodes[i].power;
            }

            if (buildFloorSlabs)
            {
                for (int f = 0; f < level.floorCount; f++)
                {
                    float width = Mathf.Max(6f, slotSpacing * 3.1f);
                    Color color = f % 2 == 0
                        ? new Color(0.16f, 0.18f, 0.26f, 1f)
                        : new Color(0.20f, 0.22f, 0.31f, 1f);

                    GameObject slab = PrimitiveFactory.CreateFloorSlab(width, 5.2f, color);
                    slab.transform.SetParent(towerRoot, false);
                    slab.transform.localPosition = new Vector3(0f, baseHeight + f * floorHeight - 0.2f, 0f);
                    _spawned.Add(slab);
                }
            }

            for (int i = 0; i < level.nodes.Length; i++)
            {
                TowerNodeSpec spec = level.nodes[i];

                GameObject instance = nodePrefab != null
                    ? Instantiate(nodePrefab, towerRoot)
                    : PrimitiveFactory.CreateTowerNode();

                if (nodePrefab == null) instance.transform.SetParent(towerRoot, false);

                TowerNode node = instance.GetComponent<TowerNode>();
                if (node == null) node = instance.AddComponent<TowerNode>();

                node.Configure(spec, maxPower);

                int slotsOnFloor = level.CountNodesOnFloor(spec.floor);
                float centreOffset = (slotsOnFloor - 1) * 0.5f;
                float x = (spec.slot - centreOffset) * slotSpacing;

                instance.transform.localPosition = new Vector3(x, baseHeight + spec.floor * floorHeight, 0f);
                instance.transform.localRotation = Quaternion.identity;

                _nodes.Add(node);
                _spawned.Add(instance);
            }
        }

        public void ClearTower()
        {
            for (int i = 0; i < _spawned.Count; i++)
            {
                if (_spawned[i] != null) Destroy(_spawned[i]);
            }

            _spawned.Clear();
            _nodes.Clear();
            _active = false;
            _resolving = false;
            _unlockedFloor = 0;
            _deadlockTimer = 0f;
        }

        /// <summary>Starts accepting taps. Call once the camera has framed the tower.</summary>
        public void BeginSiege()
        {
            if (_level == null) return;

            _active = true;
            _resolving = false;
            _unlockedFloor = 0;
            _deadlockTimer = 0f;

            UnlockFloor(0);
            RefreshNodes();

            GameEvents.RaiseSiegeStarted(_level.floorCount);
        }

        public void StopSiege()
        {
            _active = false;
        }

        private void UnlockFloor(int floor)
        {
            _unlockedFloor = floor;

            for (int i = 0; i < _nodes.Count; i++)
            {
                TowerNode node = _nodes[i];
                if (node == null) continue;
                if (node.State == NodeState.Cleared) continue;

                node.SetState(node.floor == floor ? NodeState.Available : NodeState.Locked);
            }
        }

        private void RefreshNodes()
        {
            int crowd = swarm != null ? swarm.Count : 0;
            for (int i = 0; i < _nodes.Count; i++)
            {
                if (_nodes[i] != null) _nodes[i].RefreshThreat(crowd);
            }
        }

        private void Update()
        {
            if (!_active || _resolving) return;

            HandleInput();
            CheckDeadlock();
        }

        private void HandleInput()
        {
            if (!TouchInput.PressedThisFrame) return;
            if (TouchInput.IsPointerOverUI) return;

            if (_camera == null) _camera = Camera.main;
            if (_camera == null) return;

            Ray ray = _camera.ScreenPointToRay(TouchInput.Position);
            RaycastHit hit;

            if (!Physics.Raycast(ray, out hit, 400f, GameLayers.TowerNodeMask, QueryTriggerInteraction.Ignore))
            {
                return;
            }

            TowerNode node = hit.collider.GetComponentInParent<TowerNode>();
            if (node == null) return;

            Attack(node);
        }

        private void Attack(TowerNode node)
        {
            if (node == null || node.State != NodeState.Available) return;

            int crowd = swarm != null ? swarm.Count : 0;

            if (crowd <= node.power)
            {
                node.PlayReject();

                if (JuiceManager.Instance != null)
                {
                    JuiceManager.Instance.Shake(0.16f, 0.18f);
                    JuiceManager.Instance.HapticFailure();
                    JuiceManager.Instance.SpawnFloatingText(
                        node.transform.position + Vector3.up * 2.2f,
                        "TOO STRONG",
                        new Color(1f, 0.45f, 0.42f));
                }

                return;
            }

            StartCoroutine(ResolveBattle(node));
        }

        /// <summary>High speed sequential clash: the crowd pours in, then pours back out with recruits.</summary>
        private IEnumerator ResolveBattle(TowerNode node)
        {
            _resolving = true;

            int startCrowd = swarm.Count;
            int power = node.power;
            int casualties = Mathf.RoundToInt(power * (_level != null ? _level.casualtyRatio : 0f));
            int finalCrowd = Mathf.Max(1, startCrowd + power - casualties);
            int trough = Mathf.Max(1, startCrowd - power);

            if (JuiceManager.Instance != null)
            {
                JuiceManager.Instance.PlaySelect();
                JuiceManager.Instance.HapticMedium();
            }

            float elapsed = 0f;
            while (elapsed < chargeDuration)
            {
                elapsed += Time.deltaTime;
                float t = Mathf.Clamp01(elapsed / chargeDuration);
                swarm.SetCount(Mathf.RoundToInt(Mathf.Lerp(startCrowd, trough, t)));
                RefreshNodes();
                yield return null;
            }

            node.PlayConquer();

            if (JuiceManager.Instance != null)
            {
                JuiceManager.Instance.Shake(0.28f, 0.22f);
                JuiceManager.Instance.PlayImpact();
                JuiceManager.Instance.HitStop(0.35f, 0.06f);
                JuiceManager.Instance.SpawnFloatingText(
                    node.transform.position + Vector3.up * 2.4f,
                    "+" + (power - casualties),
                    new Color(0.4f, 1f, 0.6f));
            }

            elapsed = 0f;
            while (elapsed < absorbDuration)
            {
                elapsed += Time.deltaTime;
                float t = Mathf.Clamp01(elapsed / absorbDuration);
                swarm.SetCount(Mathf.RoundToInt(Mathf.Lerp(trough, finalCrowd, t)));
                RefreshNodes();
                yield return null;
            }

            swarm.SetCount(finalCrowd);
            node.SetState(NodeState.Cleared);
            GameEvents.RaiseNodeConquered(power, node.transform.position);

            if (JuiceManager.Instance != null) JuiceManager.Instance.HapticSuccess();

            if (IsFloorCleared(_unlockedFloor))
            {
                if (_unlockedFloor + 1 < _level.floorCount)
                {
                    UnlockFloor(_unlockedFloor + 1);
                }
            }

            RefreshNodes();

            yield return new WaitForSeconds(betweenBattlesDelay);
            _resolving = false;

            if (AllCleared())
            {
                _active = false;
                if (GameManager.Instance != null) GameManager.Instance.OnSiegeResolved(true);
            }
        }

        private bool IsFloorCleared(int floor)
        {
            for (int i = 0; i < _nodes.Count; i++)
            {
                TowerNode node = _nodes[i];
                if (node == null) continue;
                if (node.floor == floor && node.State != NodeState.Cleared) return false;
            }

            return true;
        }

        private bool AllCleared()
        {
            for (int i = 0; i < _nodes.Count; i++)
            {
                if (_nodes[i] != null && _nodes[i].State != NodeState.Cleared) return false;
            }

            return true;
        }

        private bool HasBeatableMove()
        {
            int crowd = swarm != null ? swarm.Count : 0;

            for (int i = 0; i < _nodes.Count; i++)
            {
                TowerNode node = _nodes[i];
                if (node == null || node.State != NodeState.Available) continue;
                if (crowd > node.power) return true;
            }

            return false;
        }

        private void CheckDeadlock()
        {
            if (AllCleared()) return;

            if (HasBeatableMove())
            {
                _deadlockTimer = 0f;
                return;
            }

            _deadlockTimer += Time.deltaTime;
            if (_deadlockTimer < deadlockDelay) return;

            _active = false;
            if (GameManager.Instance != null) GameManager.Instance.OnSiegeResolved(false);
        }

        /// <summary>Number of rooms still standing, used by the HUD.</summary>
        public int RemainingNodes()
        {
            int count = 0;
            for (int i = 0; i < _nodes.Count; i++)
            {
                if (_nodes[i] != null && _nodes[i].State != NodeState.Cleared) count++;
            }
            return count;
        }

        public int ClearedNodes()
        {
            return _nodes.Count - RemainingNodes();
        }
    }
}
