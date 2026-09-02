using MobClash.Core;
using MobClash.Data;
using MobClash.Juice;
using MobClash.Swarm;
using UnityEngine;

namespace MobClash.Track
{
    /// <summary>
    /// A single math gate half. Gates come in pairs (a "decision row"); passing either half
    /// immediately consumes its sibling so one swarm pass can never bank both rewards.
    /// </summary>
    [RequireComponent(typeof(BoxCollider))]
    public class Gate : MonoBehaviour
    {
        [Header("Maths")]
        public MathType mathType = MathType.Multiply;
        public int value = 2;

        [Header("Motion")]
        public GateMotion motion = GateMotion.Static;
        public float motionAmplitude = 2f;
        public float motionSpeed = 0.5f;

        [Header("Wiring")]
        public Gate sibling;
        public TextMesh label;
        public Renderer slabRenderer;
        public Transform slabTransform;

        private bool _consumed;
        private Vector3 _origin;
        private float _phase;
        private float _bounceTimer;
        private Vector3 _slabBaseScale = Vector3.one;
        private MaterialPropertyBlock _propertyBlock;

        public bool Consumed { get { return _consumed; } }

        private void Awake()
        {
            CacheVisuals();
            _origin = transform.position;
            _phase = Random.Range(0f, 6.2831853f);
        }

        private void CacheVisuals()
        {
            if (slabTransform == null)
            {
                Transform found = transform.Find("Slab");
                if (found != null) slabTransform = found;
            }

            if (slabRenderer == null && slabTransform != null)
            {
                slabRenderer = slabTransform.GetComponent<Renderer>();
            }

            if (label == null) label = GetComponentInChildren<TextMesh>();
            if (slabTransform != null) _slabBaseScale = slabTransform.localScale;
        }

        /// <summary>Applies a generated spec: position, maths, motion, colour and label.</summary>
        public void Configure(GateSpec spec, float halfWidth)
        {
            CacheVisuals();

            mathType = spec.mathType;
            value = spec.value;
            motion = spec.motion;
            motionAmplitude = spec.motionAmplitude;
            motionSpeed = spec.motionSpeed;
            _consumed = false;

            float x = Mathf.Clamp(spec.lane, -1f, 1f) * (halfWidth * 0.5f);
            transform.position = new Vector3(x, 0f, spec.distance);
            transform.rotation = Quaternion.identity;
            _origin = transform.position;
            _phase = Random.Range(0f, 6.2831853f);
            _bounceTimer = 0f;

            gameObject.layer = GameLayers.GateLayer;
            SetVisible(true);
            Repaint();
        }

        private void Repaint()
        {
            Color tint = GateMath.TintFor(mathType, value);

            if (slabRenderer != null)
            {
                if (_propertyBlock == null) _propertyBlock = new MaterialPropertyBlock();
                slabRenderer.GetPropertyBlock(_propertyBlock);
                _propertyBlock.SetColor("_BaseColor", tint);
                _propertyBlock.SetColor("_Color", tint);
                slabRenderer.SetPropertyBlock(_propertyBlock);
            }

            if (label != null)
            {
                label.text = GateMath.Format(mathType, value);
                label.color = Color.white;
            }
        }

        private void SetVisible(bool visible)
        {
            if (slabTransform != null) slabTransform.gameObject.SetActive(visible);
            if (label != null) label.gameObject.SetActive(visible);

            Collider collider = GetComponent<Collider>();
            if (collider != null) collider.enabled = visible;
        }

        private void Update()
        {
            float t = Time.time;

            switch (motion)
            {
                case GateMotion.Horizontal:
                {
                    float x = _origin.x + Mathf.Sin(t * motionSpeed * 6.2831853f + _phase) * motionAmplitude;
                    transform.position = new Vector3(x, _origin.y, _origin.z);
                    break;
                }
                case GateMotion.Rotating:
                {
                    float x = _origin.x + Mathf.Sin(t * motionSpeed * 3.1415926f + _phase) * motionAmplitude * 0.5f;
                    transform.position = new Vector3(x, _origin.y, _origin.z);
                    transform.rotation = Quaternion.Euler(0f, Mathf.Sin(t * motionSpeed * 6.2831853f) * 35f, 0f);
                    break;
                }
                default:
                    break;
            }

            if (_bounceTimer > 0f)
            {
                _bounceTimer -= Time.deltaTime;
                float k = Mathf.Clamp01(_bounceTimer / 0.25f);
                float pop = Mathf.Sin(k * Mathf.PI) * 0.35f;
                if (slabTransform != null)
                {
                    slabTransform.localScale = new Vector3(
                        _slabBaseScale.x * (1f + pop),
                        _slabBaseScale.y * (1f - pop * 0.4f),
                        _slabBaseScale.z);
                }

                if (_bounceTimer <= 0f) SetVisible(false);
            }
        }

        private void OnTriggerEnter(Collider other)
        {
            if (_consumed || other == null) return;
            if (!other.CompareTag(GameLayers.TagPlayer) &&
                other.gameObject.layer != GameLayers.PlayerLayer) return;

            if (GameManager.Instance != null && GameManager.Instance.State != GameState.Running) return;

            Trigger();
        }

        private void Trigger()
        {
            _consumed = true;

            SwarmManager swarm = GameManager.Instance != null ? GameManager.Instance.Swarm : null;
            int before = swarm != null ? swarm.Count : 0;
            int after = before;

            if (swarm != null) after = swarm.ApplyGate(mathType, value);

            Vector3 position = transform.position + Vector3.up * 2.2f;
            GameEvents.RaiseGatePassed(mathType, value, before, after, position);

            if (JuiceManager.Instance != null)
            {
                bool positive = after >= before;
                JuiceManager.Instance.SpawnFloatingText(
                    position,
                    (after - before >= 0 ? "+" : "") + (after - before),
                    positive ? new Color(0.35f, 1f, 0.55f) : new Color(1f, 0.4f, 0.4f));

                JuiceManager.Instance.Shake(positive ? 0.12f : 0.2f, 0.16f);
                if (positive) JuiceManager.Instance.HapticLight();
                else JuiceManager.Instance.HapticMedium();
                JuiceManager.Instance.PlayGateSound(positive);
            }

            _bounceTimer = 0.25f;

            if (sibling != null) sibling.ConsumeSilently();
        }

        /// <summary>Called on the other half of the row so the player cannot collect both.</summary>
        public void ConsumeSilently()
        {
            if (_consumed) return;
            _consumed = true;
            _bounceTimer = 0.12f;
        }
    }
}
