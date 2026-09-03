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
        [Tooltip("Build the castle mass the rooms are cut into. Turn off when using a real tower prefab.")]
        public bool buildCastleShell = true;

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
        private GameObject _flag;
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

            int maxSlots = 1;
            for (int f = 0; f < level.floorCount; f++)
            {
                maxSlots = Mathf.Max(maxSlots, level.CountNodesOnFloor(f));
            }

            float shellWidth = maxSlots * slotSpacing + 3.2f;
            float shellHeight = baseHeight + level.floorCount * floorHeight + 1.4f;

            if (buildCastleShell)
            {
                GameObject shell = PrimitiveFactory.CreateCastleShell(shellWidth, shellHeight, 2.6f);
                shell.transform.SetParent(towerRoot, false);
                shell.transform.localPosition = Vector3.zero;
                _spawned.Add(shell);

                _flag = PrimitiveFactory.CreateFlag(Palette.Red);
                _flag.transform.SetParent(towerRoot, false);
                _flag.transform.localPosition = new Vector3(0f, shellHeight + 0.6f, 1.0f);
                _spawned.Add(_flag);
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

                instance.transform.localPosition = new Vector3(x, baseHeight + spec.floor * floorHeight, -0.9f);
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
            _flag = null;
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
                        Palette.Red);
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
                    Palette.Jade);
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
                RaiseVictoryBanner();
                _active = false;
                if (GameManager.Instance != null) GameManager.Instance.OnSiegeResolved(true);
            }
        }

        /// <summary>Swaps the keep's banner to the player's colours the moment the last room falls.</summary>
        private void RaiseVictoryBanner()
        {
            if (_flag == null) return;

            Transform banner = _flag.transform.Find("Banner");
            if (banner == null) return;

            Renderer renderer = banner.GetComponent<Renderer>();
            if (renderer == null) return;

            MaterialPropertyBlock block = new MaterialPropertyBlock();
            renderer.GetPropertyBlock(block);
            block.SetColor("_BaseColor", Palette.Blue);
            block.SetColor("_Color", Palette.Blue);
            renderer.SetPropertyBlock(block);
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
