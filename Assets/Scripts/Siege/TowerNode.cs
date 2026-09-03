using System.Collections.Generic;
using MobClash.Core;
using MobClash.Data;
using UnityEngine;

namespace MobClash.Siege
{
    /// <summary>
    /// One conquerable room in the siege tower. Displays its defender count, recolours itself to
    /// tell the player at a glance whether the room is currently winnable, and plays the
    /// conquer / reject feedback. It contains no game rules: SiegeManager owns resolution.
    /// </summary>
    public class TowerNode : MonoBehaviour
    {
        [Header("Data")]
        public int floor;
        public int slot;
        public int power = 10;
        public bool isBoss;

        [Header("Visuals")]
        public Transform bodyTransform;
        public Renderer bodyRenderer;
        public TextMesh label;

        [Header("Defenders")]
        [Tooltip("Optional figure prefab. Falls back to the generated defender.")]
        public GameObject defenderPrefab;
        public int maxDefenderFigures = 3;
        public float defenderScale = 0.8f;

        [Header("Colours")]
        public Color lockedColor = new Color(0.18f, 0.16f, 0.22f, 1f);
        public Color beatableColor = new Color(0.36f, 0.10f, 0.14f, 1f);
        public Color blockedColor = new Color(0.20f, 0.07f, 0.10f, 1f);
        public Color clearedColor = new Color(0.10f, 0.28f, 0.59f, 1f);
        public Color bossColor = new Color(0.45f, 0.13f, 0.52f, 1f);

        private readonly List<GameObject> _defenders = new List<GameObject>(3);
        private NodeState _state = NodeState.Locked;
        private bool _beatable;
        private Vector3 _baseScale = Vector3.one;
        private Vector3 _configuredScale = Vector3.one;
        private float _pulse;
        private float _rejectTimer;
        private MaterialPropertyBlock _propertyBlock;

        public NodeState State { get { return _state; } }
        public bool Beatable { get { return _beatable; } }

        private void Awake()
        {
            CacheVisuals();
        }

        private void CacheVisuals()
        {
            if (bodyTransform == null)
            {
                Transform found = transform.Find("Body");
                if (found != null) bodyTransform = found;
            }

            if (bodyRenderer == null && bodyTransform != null)
            {
                bodyRenderer = bodyTransform.GetComponent<Renderer>();
            }

            if (label == null) label = GetComponentInChildren<TextMesh>();
            if (bodyTransform != null) _baseScale = bodyTransform.localScale;
        }

        /// <summary>Applies a generated spec and sizes the room by its defender count.</summary>
        public void Configure(TowerNodeSpec spec, int maxPowerInTower)
        {
            CacheVisuals();

            floor = spec.floor;
            slot = spec.slot;
            power = Mathf.Max(1, spec.power);
            isBoss = spec.isBoss;

            float t = maxPowerInTower > 0 ? Mathf.Clamp01(power / (float)maxPowerInTower) : 0.5f;
            float scale = Mathf.Lerp(0.85f, 1.35f, t);

            _configuredScale = new Vector3(_baseScale.x * scale, _baseScale.y, _baseScale.z * scale);
            if (bodyTransform != null) bodyTransform.localScale = _configuredScale;

            gameObject.layer = GameLayers.TowerNodeLayer;
            if (bodyTransform != null) bodyTransform.gameObject.layer = GameLayers.TowerNodeLayer;

            BuildDefenders();
            SetState(NodeState.Locked);
            RefreshThreat(0);
        }

        /// <summary>
        /// Puts a garrison in the window. Purely decorative - the room's strength is its number -
        /// but an empty box reads as scenery while figures read as something to fight.
        /// </summary>
        private void BuildDefenders()
        {
            for (int i = 0; i < _defenders.Count; i++)
            {
                if (_defenders[i] != null) PrimitiveFactory.SafeDestroy(_defenders[i]);
            }
            _defenders.Clear();

            int count = Mathf.Clamp(1 + power % 3, 1, Mathf.Max(1, maxDefenderFigures));
            for (int i = 0; i < count; i++)
            {
                GameObject figure = defenderPrefab != null
                    ? Instantiate(defenderPrefab, transform)
                    : PrimitiveFactory.CreateDefender();

                if (defenderPrefab == null) figure.transform.SetParent(transform, false);

                float spread = (i - (count - 1) * 0.5f) * 0.85f;
                figure.transform.localPosition = new Vector3(spread, 0.12f, -0.55f);
                figure.transform.localScale = Vector3.one * defenderScale;
                figure.transform.localRotation = Quaternion.Euler(0f, 180f, 0f);

                _defenders.Add(figure);
            }
        }

        private void SetDefendersVisible(bool visible)
        {
            for (int i = 0; i < _defenders.Count; i++)
            {
                if (_defenders[i] != null) _defenders[i].SetActive(visible);
            }
        }

        public void SetState(NodeState state)
        {
            _state = state;
            SetDefendersVisible(state != NodeState.Cleared);
            Repaint();
        }

        /// <summary>Recolours against the live crowd size so the player can read the puzzle instantly.</summary>
        public void RefreshThreat(int playerCount)
        {
            _beatable = playerCount > power;
            Repaint();

            if (label != null)
            {
                label.text = power.ToString();
                if (_state == NodeState.Cleared) label.color = new Color(0.75f, 0.88f, 1f, 1f);
                else if (_state == NodeState.Locked) label.color = new Color(0.65f, 0.67f, 0.72f, 1f);
                else label.color = _beatable ? new Color(0.55f, 1f, 0.6f, 1f) : new Color(1f, 0.62f, 0.6f, 1f);
            }
        }

        private void Repaint()
        {
            if (bodyRenderer == null) return;

            Color color;
            switch (_state)
            {
                case NodeState.Cleared:
                    color = clearedColor;
                    break;
                case NodeState.Available:
                    color = _beatable ? (isBoss ? bossColor : beatableColor) : blockedColor;
                    break;
                default:
                    color = lockedColor;
                    break;
            }

            if (_propertyBlock == null) _propertyBlock = new MaterialPropertyBlock();
            bodyRenderer.GetPropertyBlock(_propertyBlock);
            _propertyBlock.SetColor("_BaseColor", color);
            _propertyBlock.SetColor("_Color", color);
            bodyRenderer.SetPropertyBlock(_propertyBlock);
        }

        public void PlayReject()
        {
            _rejectTimer = 0.32f;
        }

        public void PlayConquer()
        {
            _pulse = 0.35f;
        }

        private void Update()
        {
            if (bodyTransform == null) return;

            float dt = Time.deltaTime;
            float scaleMultiplier = 1f;
            float lateralOffset = 0f;

            if (_pulse > 0f)
            {
                _pulse -= dt;
                float k = Mathf.Clamp01(_pulse / 0.35f);
                scaleMultiplier = 1f + Mathf.Sin(k * Mathf.PI) * 0.28f;
            }
            else if (_rejectTimer > 0f)
            {
                _rejectTimer -= dt;
                lateralOffset = Mathf.Sin(Time.time * 55f) * 0.16f * Mathf.Clamp01(_rejectTimer / 0.32f);
            }
            else if (_state == NodeState.Available && _beatable)
            {
                scaleMultiplier = 1f + Mathf.Sin(Time.time * 3.2f + floor) * 0.025f;
            }

            bodyTransform.localScale = new Vector3(
                _configuredScale.x * scaleMultiplier,
                _configuredScale.y * (2f - scaleMultiplier),
                _configuredScale.z * scaleMultiplier);

            Vector3 local = bodyTransform.localPosition;
            bodyTransform.localPosition = new Vector3(lateralOffset, local.y, local.z);
        }
    }
}
