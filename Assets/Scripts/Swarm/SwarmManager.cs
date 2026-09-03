using System.Collections.Generic;
using MobClash.Core;
using MobClash.Meta;
using MobClash.Track;
using UnityEngine;

namespace MobClash.Swarm
{
    /// <summary>
    /// Owns the crowd: pooling, cohesion, hazard attrition and gate arithmetic.
    ///
    /// Performance model
    ///   * Crowd members carry no Collider and no per-unit MonoBehaviour Update.
    ///   * One flat loop animates every unit from cached arrays (no GetComponent, no LINQ, no allocs).
    ///   * Formation slots come from a sunflower (golden angle) distribution, which packs units evenly
    ///     inside a disc and keeps the silhouette readable at any count.
    ///   * The logical count can exceed the rendered count (maxVisualUnits) so the HUD can show
    ///     "1240" while the GPU only ever draws the configured budget.
    /// </summary>
    public class SwarmManager : MonoBehaviour
    {
        private const float GoldenAngle = 2.39996323f;

        [Header("Wiring")]
        public Transform pivot;
        public ObjectPool unitPool;
        public GameObject unitPrefab;

        [Header("Budget")]
        [Tooltip("Maximum crowd members rendered while running. Each is 4 renderers, so 450 is " +
                 "about the ceiling on mid range Android. Drop it for low end hardware.")]
        public int maxVisualUnits = 450;

        [Tooltip("Rendered members during the siege. Fewer bodies keeps the tower readable.")]
        public int maxSiegeUnits = 150;

        public int prewarmUnits = 320;

        [Header("Formation")]
        public float unitSpacing = 0.42f;
        public float maxFormationRadius = 4.6f;

        [Tooltip("Siege formation: a wide, shallow battle line massed at the tower base.")]
        public float siegeLineWidth = 12f;
        public float siegeLineDepth = 3.6f;
        public float baseDamp = 0.11f;
        public float dampVariance = 0.05f;
        public float jitterAmount = 0.18f;

        [Header("Animation")]
        public float bobHeight = 0.16f;
        public float bobSpeed = 13f;
        public float squash = 0.14f;
        public float punchDuration = 0.22f;
        public float punchAmount = 0.35f;

        [Header("Look")]
        public Color unitColor = new Color(0.239f, 0.545f, 1f, 1f);

        private Transform[] _transforms;
        private SwarmVisual[] _visuals;
        private Vector3[] _velocities;
        private int _visualCount;
        private int _logicalCount;
        private float _punchTimer;
        private float _groundY;

        private readonly List<int> _pendingKills = new List<int>(32);
        private readonly List<Obstacle> _nearbyObstacles = new List<Obstacle>(8);
        private float _obstacleRefreshTimer;

        public int Count { get { return _logicalCount; } }
        public int RenderedCount { get { return _visualCount; } }
        public Transform Pivot { get { return pivot; } }

        /// <summary>True once the run has handed over to the tower, where the crowd re-forms.</summary>
        private bool InSiegePhase
        {
            get
            {
                if (GameManager.Instance == null) return false;
                GameState state = GameManager.Instance.State;
                return state == GameState.SiegeMode || state == GameState.LevelWin ||
                       state == GameState.LevelFail;
            }
        }

        /// <summary>Rendered-unit budget for the current phase.</summary>
        private int VisualCap()
        {
            if (GameManager.Instance == null) return maxVisualUnits;

            GameState state = GameManager.Instance.State;
            bool siegeBudget = state == GameState.TransitionToSiege || state == GameState.SiegeMode ||
                               state == GameState.LevelWin || state == GameState.LevelFail;

            return siegeBudget ? Mathf.Min(maxSiegeUnits, maxVisualUnits) : maxVisualUnits;
        }

        public float CrowdRadius
        {
            get { return Mathf.Min(maxFormationRadius, unitSpacing * Mathf.Sqrt(Mathf.Max(1, _visualCount))); }
        }

        public Vector3 CrowdCenter
        {
            get { return pivot != null ? pivot.position : transform.position; }
        }

        private void Awake()
        {
            if (pivot == null) pivot = transform;
            _groundY = pivot.position.y;

            _transforms = new Transform[maxVisualUnits];
            _visuals = new SwarmVisual[maxVisualUnits];
            _velocities = new Vector3[maxVisualUnits];

            EnsurePool();
        }

        private void EnsurePool()
        {
            if (unitPrefab == null)
            {
                unitPrefab = PrimitiveFactory.CreateSwarmUnit(unitColor);
                unitPrefab.name = "SwarmUnit_Runtime";
                unitPrefab.SetActive(false);
                unitPrefab.transform.SetParent(transform, false);
            }

            if (unitPool == null)
            {
                unitPool = gameObject.AddComponent<ObjectPool>();
            }

            unitPool.hardCap = Mathf.Max(unitPool.hardCap, maxVisualUnits + 16);
            unitPool.Initialise(unitPrefab, Mathf.Min(prewarmUnits, maxVisualUnits));
        }

        /// <summary>Clears the crowd and repopulates it at <paramref name="count"/> for a fresh run.</summary>
        public void ResetSwarm(int count)
        {
            for (int i = _visualCount - 1; i >= 0; i--) DespawnAt(i);

            _visualCount = 0;
            _logicalCount = 0;
            _punchTimer = 0f;
            _pendingKills.Clear();
            _nearbyObstacles.Clear();
            _obstacleRefreshTimer = 0f;

            SetCount(count);
        }

        /// <summary>Sets the logical crowd size and reconciles the rendered population.</summary>
        public void SetCount(int target)
        {
            target = Mathf.Clamp(target, 0, GateMath.MaxCrowd);
            _logicalCount = target;

            int visualTarget = Mathf.Min(target, VisualCap());
            while (_visualCount < visualTarget)
            {
                if (!SpawnOne()) break;
            }

            while (_visualCount > visualTarget)
            {
                DespawnAt(_visualCount - 1);
            }

            GameEvents.RaiseCrowdChanged(_logicalCount);
        }

        /// <summary>
        /// Applies a gate operation, including the player's purchased Gate Bonus upgrade.
        /// Returns the new crowd size.
        /// </summary>
        public int ApplyGate(MathType type, int value)
        {
            int before = _logicalCount;
            int flatBonus = 0;
            float multiplierBonus = 0f;

            if (EconomyManager.Instance != null)
            {
                flatBonus = EconomyManager.Instance.GateFlatBonus;
                multiplierBonus = EconomyManager.Instance.GateMultiplierBonus;
            }

            int after;
            if (type == MathType.Add)
            {
                after = GateMath.Apply(before, MathType.Add, value + flatBonus);
            }
            else if (type == MathType.Multiply)
            {
                float effective = value + multiplierBonus;
                after = Mathf.Clamp(Mathf.RoundToInt(before * effective), 0, GateMath.MaxCrowd);
            }
            else
            {
                after = GateMath.Apply(before, type, value);
            }

            SetCount(after);
            Punch();
            return after;
        }

        /// <summary>Removes units, used by hazards and by siege casualties.</summary>
        public void KillUnits(int amount, Vector3 at)
        {
            if (amount <= 0 || _logicalCount <= 0) return;

            amount = Mathf.Min(amount, _logicalCount);
            SetCount(_logicalCount - amount);
            GameEvents.RaiseUnitsLost(amount, at);
        }

        public void Punch()
        {
            _punchTimer = punchDuration;
        }

        private bool SpawnOne()
        {
            if (_visualCount >= maxVisualUnits) return false;

            GameObject instance = unitPool != null ? unitPool.Get() : null;
            if (instance == null) return false;

            Transform t = instance.transform;
            t.SetParent(transform, false);

            Vector3 spawnPosition = CrowdCenter + new Vector3(
                Random.Range(-0.6f, 0.6f), 0f, Random.Range(-0.6f, 0.6f));
            spawnPosition.y = _groundY;
            t.position = spawnPosition;
            t.rotation = pivot != null ? Quaternion.Euler(0f, pivot.eulerAngles.y, 0f) : Quaternion.identity;

            SwarmVisual visual = instance.GetComponent<SwarmVisual>();
            if (visual == null) visual = instance.AddComponent<SwarmVisual>();
            visual.phase = Random.Range(0f, 6.2831853f);
            visual.damp = baseDamp + Random.Range(-dampVariance, dampVariance);
            visual.jitter = new Vector3(
                Random.Range(-jitterAmount, jitterAmount), 0f, Random.Range(-jitterAmount, jitterAmount));

            int index = _visualCount;
            _transforms[index] = t;
            _visuals[index] = visual;
            _velocities[index] = Vector3.zero;
            _visualCount++;
            return true;
        }

        private void DespawnAt(int index)
        {
            if (index < 0 || index >= _visualCount) return;

            Transform t = _transforms[index];
            if (t != null && unitPool != null) unitPool.Release(t.gameObject);

            int last = _visualCount - 1;
            if (index != last)
            {
                _transforms[index] = _transforms[last];
                _visuals[index] = _visuals[last];
                _velocities[index] = _velocities[last];
            }

            _transforms[last] = null;
            _visuals[last] = null;
            _velocities[last] = Vector3.zero;
            _visualCount--;
        }

        /// <summary>Reconciles the rendered population when the phase changes the budget.</summary>
        private void ApplyVisualCap()
        {
            int target = Mathf.Min(_logicalCount, VisualCap());

            while (_visualCount > target) DespawnAt(_visualCount - 1);
            while (_visualCount < target)
            {
                if (!SpawnOne()) break;
            }
        }

        private void Update()
        {
            ApplyVisualCap();
            if (_visualCount == 0) return;

            float dt = Time.deltaTime;
            float time = Time.time;
            float radius = CrowdRadius;
            Vector3 center = CrowdCenter;
            Quaternion facing = pivot != null
                ? Quaternion.Euler(0f, pivot.eulerAngles.y, 0f)
                : Quaternion.identity;

            if (_punchTimer > 0f) _punchTimer -= dt;
            float punch = _punchTimer > 0f
                ? Mathf.Sin(Mathf.Clamp01(_punchTimer / Mathf.Max(0.0001f, punchDuration)) * Mathf.PI) * punchAmount
                : 0f;

            RefreshNearbyObstacles(dt, center);
            _pendingKills.Clear();

            float invCount = 1f / _visualCount;

            // Running: a round blob that reads as a crowd. Siege: a wide, shallow battle line so
            // the army masses at the foot of the tower instead of burying it.
            bool line = InSiegePhase;
            float spreadX = line ? Mathf.Min(siegeLineWidth, unitSpacing * 2.7f * Mathf.Sqrt(_visualCount)) : radius;
            float spreadZ = line ? Mathf.Min(siegeLineDepth, unitSpacing * 0.8f * Mathf.Sqrt(_visualCount)) : radius;

            for (int i = 0; i < _visualCount; i++)
            {
                Transform t = _transforms[i];
                if (t == null) continue;

                SwarmVisual visual = _visuals[i];
                float slot = (i + 0.5f) * invCount;
                float angle = i * GoldenAngle;
                float r = Mathf.Sqrt(slot);

                Vector3 offset = new Vector3(Mathf.Cos(angle) * r * spreadX, 0f, Mathf.Sin(angle) * r * spreadZ);
                if (visual != null) offset += visual.jitter;

                Vector3 target = center + offset;
                target.y = _groundY;

                Vector3 current = t.position;
                float damp = visual != null ? visual.damp : baseDamp;
                Vector3 flat = Vector3.SmoothDamp(
                    new Vector3(current.x, _groundY, current.z), target, ref _velocities[i], damp, 60f, dt);

                float phase = visual != null ? visual.phase : 0f;
                float bob = Mathf.Abs(Mathf.Sin(time * bobSpeed + phase)) * bobHeight;
                flat.y = _groundY + bob;
                t.position = flat;

                Vector3 planarVelocity = _velocities[i];
                planarVelocity.y = 0f;
                if (planarVelocity.sqrMagnitude > 0.35f)
                {
                    Quaternion look = Quaternion.LookRotation(planarVelocity.normalized, Vector3.up);
                    t.rotation = Quaternion.Slerp(t.rotation, look, dt * 9f);
                }
                else
                {
                    t.rotation = Quaternion.Slerp(t.rotation, facing, dt * 6f);
                }

                if (visual != null && visual.body != null)
                {
                    Vector3 baseScale = visual.BodyBaseScale;
                    float wave = Mathf.Sin(time * bobSpeed + phase) * squash;
                    float sx = 1f - wave * 0.5f + punch;
                    float sy = 1f + wave + punch;
                    visual.body.localScale = new Vector3(
                        baseScale.x * sx, baseScale.y * sy, baseScale.z * sx);
                }

                if (_nearbyObstacles.Count > 0 && IsInsideHazard(flat)) _pendingKills.Add(i);
            }

            ResolvePendingKills();
        }

        private void RefreshNearbyObstacles(float dt, Vector3 center)
        {
            _obstacleRefreshTimer -= dt;
            if (_obstacleRefreshTimer > 0f) return;

            _obstacleRefreshTimer = 0.1f;
            _nearbyObstacles.Clear();

            List<Obstacle> all = Obstacle.Active;
            for (int i = 0; i < all.Count; i++)
            {
                Obstacle obstacle = all[i];
                if (obstacle == null) continue;

                float dz = obstacle.transform.position.z - center.z;
                if (dz < -14f || dz > 14f) continue;

                _nearbyObstacles.Add(obstacle);
            }
        }

        private bool IsInsideHazard(Vector3 position)
        {
            for (int i = 0; i < _nearbyObstacles.Count; i++)
            {
                Obstacle obstacle = _nearbyObstacles[i];
                if (obstacle == null) continue;

                Vector3 op = obstacle.transform.position;
                float dx = position.x - op.x;
                float dz = position.z - op.z;
                float radius = obstacle.killRadius;
                if (dx * dx + dz * dz <= radius * radius) return true;
            }

            return false;
        }

        private void ResolvePendingKills()
        {
            if (_pendingKills.Count == 0) return;

            int killed = _pendingKills.Count;
            Vector3 at = CrowdCenter;

            // Remove from the back so swapped indices stay valid.
            for (int i = _pendingKills.Count - 1; i >= 0; i--)
            {
                int index = _pendingKills[i];
                if (index < _visualCount) DespawnAt(index);
            }

            int logicalPerVisual = 1;
            if (_visualCount > 0 && _logicalCount > _visualCount)
            {
                logicalPerVisual = Mathf.Max(1, Mathf.RoundToInt(_logicalCount / (float)_visualCount));
            }

            int logicalLoss = Mathf.Min(_logicalCount, killed * logicalPerVisual);
            _logicalCount = Mathf.Max(0, _logicalCount - logicalLoss);

            GameEvents.RaiseCrowdChanged(_logicalCount);
            GameEvents.RaiseUnitsLost(logicalLoss, at);

            _pendingKills.Clear();
        }
    }
}
