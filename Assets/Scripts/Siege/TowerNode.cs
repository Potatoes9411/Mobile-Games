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

        [Header("Colours")]
        public Color lockedColor = new Color(0.23f, 0.25f, 0.31f, 1f);
        public Color beatableColor = new Color(0.90f, 0.38f, 0.28f, 1f);
        public Color blockedColor = new Color(0.38f, 0.11f, 0.14f, 1f);
        public Color clearedColor = new Color(0.18f, 0.50f, 0.93f, 1f);
        public Color bossColor = new Color(0.62f, 0.16f, 0.72f, 1f);

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

            SetState(NodeState.Locked);
            RefreshThreat(0);
        }

        public void SetState(NodeState state)
        {
            _state = state;
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
