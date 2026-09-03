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
        public Transform postLeft;
        public Transform postRight;
        public Transform lintel;
        public Renderer bannerRenderer;

        private bool _consumed;
        private Vector3 _origin;
        private float _phase;
        private float _bounceTimer;
        private Vector3 _slabBaseScale = Vector3.one;
        private float _travelLimit = 2.5f;
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

            if (postLeft == null) postLeft = transform.Find("PostLeft");
            if (postRight == null) postRight = transform.Find("PostRight");
            if (lintel == null) lintel = transform.Find("Lintel");

            if (bannerRenderer == null)
            {
                Transform bannerTransform = transform.Find("Banner");
                if (bannerTransform != null) bannerRenderer = bannerTransform.GetComponent<Renderer>();
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

            // A row's two halves each own half the road and tile it edge to edge. Centring them
            // on the lane value instead would overlap them in the middle of the track, which makes
            // the choice unreadable and lets either collider claim the pass.
            float x = (spec.lane < 0f ? -1f : 1f) * (halfWidth * 0.5f);
            transform.position = new Vector3(x, 0f, spec.distance);
            transform.rotation = Quaternion.identity;
            _origin = transform.position;
            _phase = Random.Range(0f, 6.2831853f);
            _bounceTimer = 0f;

            FitToHalfWidth(halfWidth);

            gameObject.layer = GameLayers.GateLayer;
            SetVisible(true);
            Repaint();
        }

        /// <summary>Sizes the structure so the half exactly covers its side of the road.</summary>
        private void FitToHalfWidth(float halfWidth)
        {
            float span = halfWidth;
            float inner = span - 0.24f;

            // Cap the sway so a moving half can never wander across the centre line and end up
            // on its sibling's side of the road.
            _travelLimit = span * 0.62f;

            if (slabTransform != null)
            {
                Vector3 scale = slabTransform.localScale;
                slabTransform.localScale = new Vector3(inner, scale.y, scale.z);
                _slabBaseScale = slabTransform.localScale;
            }

            float postX = span * 0.5f - 0.17f;
            if (postLeft != null)
            {
                Vector3 p = postLeft.localPosition;
                postLeft.localPosition = new Vector3(-postX, p.y, p.z);
            }
            if (postRight != null)
            {
                Vector3 p = postRight.localPosition;
                postRight.localPosition = new Vector3(postX, p.y, p.z);
            }

            if (lintel != null)
            {
                Vector3 scale = lintel.localScale;
                lintel.localScale = new Vector3(span, scale.y, scale.z);
            }

            if (bannerRenderer != null)
            {
                Transform bannerTransform = bannerRenderer.transform;
                Vector3 scale = bannerTransform.localScale;
                bannerTransform.localScale = new Vector3(span - 0.3f, scale.y, scale.z);
            }

            BoxCollider box = GetComponent<BoxCollider>();
            if (box != null)
            {
                Vector3 size = box.size;
                box.size = new Vector3(span, size.y, size.z);
            }
        }

        private void Repaint()
        {
            Color tint = GateMath.TintFor(mathType, value);

            Tint(slabRenderer, tint);
            Tint(bannerRenderer, Color.Lerp(tint, Color.white, 0.35f));

            if (label != null)
            {
                label.text = GateMath.Format(mathType, value);
                label.color = Color.white;
            }
        }

        private void Tint(Renderer target, Color color)
        {
            if (target == null) return;

            if (_propertyBlock == null) _propertyBlock = new MaterialPropertyBlock();
            target.GetPropertyBlock(_propertyBlock);
            _propertyBlock.SetColor("_BaseColor", color);
            _propertyBlock.SetColor("_Color", color);
            target.SetPropertyBlock(_propertyBlock);
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
                    float travel = Mathf.Min(motionAmplitude, _travelLimit);
                    float x = _origin.x + Mathf.Sin(t * motionSpeed * 6.2831853f + _phase) * travel;
                    transform.position = new Vector3(x, _origin.y, _origin.z);
                    break;
                }
                case GateMotion.Rotating:
                {
                    float travel = Mathf.Min(motionAmplitude, _travelLimit);
                    float x = _origin.x + Mathf.Sin(t * motionSpeed * 3.1415926f + _phase) * travel * 0.5f;
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
                    positive ? Palette.Jade : Palette.Red);

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
