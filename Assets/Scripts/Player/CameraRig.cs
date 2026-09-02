using UnityEngine;

namespace MobClash.Player
{
    /// <summary>
    /// Smooth chase camera with two framings: the runner view and the siege view.
    /// Owns the shake offset so the JuiceManager never fights the follow logic.
    /// </summary>
    public class CameraRig : MonoBehaviour
    {
        [Header("Target")]
        public Transform target;

        [Header("Run framing")]
        public Vector3 runOffset = new Vector3(0f, 9.5f, -11.5f);
        public Vector3 runLookOffset = new Vector3(0f, 1.6f, 7f);
        public float followSmoothTime = 0.18f;

        [Tooltip("How much of the runner's horizontal movement the camera copies. Under 1 adds parallax.")]
        [Range(0f, 1f)]
        public float horizontalFollow = 0.55f;

        [Header("Siege framing")]
        public Vector3 siegeOffset = new Vector3(0f, 7f, -18f);
        public float siegeTransitionTime = 0.9f;

        [Header("Field of view")]
        public float baseFieldOfView = 58f;
        public float maxFieldOfView = 72f;
        public float fieldOfViewSmooth = 2.5f;

        private Camera _camera;
        private Vector3 _positionVelocity;
        private Vector3 _shakeOffset;
        private float _shakeAmplitude;
        private float _shakeTimer;
        private float _shakeDuration;
        private bool _siegeMode;
        private Vector3 _siegeAnchor;
        private float _siegeHeight;
        private float _crowdRadius;

        private void Awake()
        {
            _camera = GetComponent<Camera>();
            if (_camera == null) _camera = GetComponentInChildren<Camera>();
            if (_camera != null) _camera.fieldOfView = baseFieldOfView;
        }

        public void SetTarget(Transform newTarget)
        {
            target = newTarget;
        }

        public void SetCrowdRadius(float radius)
        {
            _crowdRadius = radius;
        }

        /// <summary>Frames the tower for the siege phase.</summary>
        public void EnterSiegeView(Vector3 towerBase, float towerHeight)
        {
            _siegeMode = true;
            _siegeAnchor = towerBase;
            _siegeHeight = towerHeight;
        }

        public void EnterRunView()
        {
            _siegeMode = false;
        }

        /// <summary>Snaps the camera to its ideal position without interpolation.</summary>
        public void Snap()
        {
            transform.position = IdealPosition();
            transform.rotation = Quaternion.LookRotation(LookPoint() - transform.position, Vector3.up);
            _positionVelocity = Vector3.zero;
        }

        public void Shake(float amplitude, float duration)
        {
            if (amplitude <= _shakeAmplitude && _shakeTimer > 0f) return;

            _shakeAmplitude = amplitude;
            _shakeDuration = Mathf.Max(0.01f, duration);
            _shakeTimer = _shakeDuration;
        }

        private Vector3 IdealPosition()
        {
            if (_siegeMode)
            {
                return _siegeAnchor + siegeOffset + new Vector3(0f, _siegeHeight * 0.45f, 0f);
            }

            if (target == null) return transform.position;

            Vector3 basePosition = target.position + runOffset;
            basePosition.x = target.position.x * horizontalFollow + runOffset.x;
            return basePosition;
        }

        private Vector3 LookPoint()
        {
            if (_siegeMode)
            {
                return _siegeAnchor + new Vector3(0f, _siegeHeight * 0.5f, 0f);
            }

            if (target == null) return transform.position + Vector3.forward;

            Vector3 point = target.position + runLookOffset;
            point.x = target.position.x * horizontalFollow;
            return point;
        }

        private void LateUpdate()
        {
            float dt = Time.deltaTime;
            if (dt <= 0f) return;

            float smooth = _siegeMode ? siegeTransitionTime : followSmoothTime;
            Vector3 desired = IdealPosition();
            Vector3 smoothed = Vector3.SmoothDamp(
                transform.position - _shakeOffset, desired, ref _positionVelocity, smooth, 200f, dt);

            UpdateShake(dt);

            transform.position = smoothed + _shakeOffset;

            Quaternion look = Quaternion.LookRotation(LookPoint() - transform.position, Vector3.up);
            transform.rotation = Quaternion.Slerp(transform.rotation, look, 1f - Mathf.Exp(-8f * dt));

            if (_camera != null)
            {
                float wanted = Mathf.Lerp(baseFieldOfView, maxFieldOfView, Mathf.InverseLerp(1.5f, 5f, _crowdRadius));
                _camera.fieldOfView = Mathf.Lerp(_camera.fieldOfView, wanted, 1f - Mathf.Exp(-fieldOfViewSmooth * dt));
            }
        }

        private void UpdateShake(float dt)
        {
            if (_shakeTimer <= 0f)
            {
                _shakeOffset = Vector3.zero;
                return;
            }

            _shakeTimer -= dt;
            float falloff = Mathf.Clamp01(_shakeTimer / _shakeDuration);
            float strength = _shakeAmplitude * falloff * falloff;

            _shakeOffset = new Vector3(
                (Mathf.PerlinNoise(Time.time * 26f, 0.13f) - 0.5f) * 2f * strength,
                (Mathf.PerlinNoise(0.71f, Time.time * 26f) - 0.5f) * 2f * strength,
                0f);

            if (_shakeTimer <= 0f)
            {
                _shakeAmplitude = 0f;
                _shakeOffset = Vector3.zero;
            }
        }
    }
}
