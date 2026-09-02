using MobClash.Core;
using MobClash.Data;
using UnityEngine;

namespace MobClash.Player
{
    /// <summary>
    /// Single finger swerve controller.
    ///
    /// Feel rules that make the difference between "cheap" and "premium":
    ///   * Input is a screen space DRAG delta, not an absolute position, so the crowd never snaps
    ///     to the finger and the player can re-grip anywhere on screen.
    ///   * The drag feeds a target X which is chased with SmoothDamp, giving weight and inertia.
    ///   * Forward speed is constant and frame rate independent; steering never affects it.
    ///   * The clamp shrinks with the crowd radius so a huge blob cannot clip through the rails.
    /// </summary>
    [RequireComponent(typeof(Rigidbody))]
    public class PlayerSwerve : MonoBehaviour
    {
        [Header("Movement")]
        public float forwardSpeed = 12f;
        public float halfWidth = 6f;

        [Tooltip("World units travelled when the finger crosses the full screen width.")]
        public float swipeWorldWidth = 14f;

        [Tooltip("Lower is snappier. 0.06-0.12 feels premium.")]
        public float steerSmoothTime = 0.085f;

        public float maxSteerSpeed = 26f;

        [Header("Lean")]
        public float maxLeanAngle = 14f;
        public float leanSmoothTime = 0.12f;

        private Rigidbody _rigidbody;
        private float _targetX;
        private float _currentX;
        private float _steerVelocity;
        private float _lean;
        private float _leanVelocity;
        private float _startZ;
        private float _radiusPadding;
        private bool _controlEnabled;

        /// <summary>Metres travelled down the track since the run started.</summary>
        public float TravelDistance { get { return transform.position.z - _startZ; } }

        public bool ControlEnabled { get { return _controlEnabled; } }

        private void Awake()
        {
            _rigidbody = GetComponent<Rigidbody>();
            _rigidbody.isKinematic = true;
            _rigidbody.useGravity = false;
            _rigidbody.constraints = RigidbodyConstraints.FreezeRotation;
            _rigidbody.collisionDetectionMode = CollisionDetectionMode.ContinuousSpeculative;

            gameObject.layer = GameLayers.PlayerLayer;
            PrimitiveFactory.SafeSetTag(gameObject, GameLayers.TagPlayer);
        }

        /// <summary>Places the runner at the start line and applies the level's tuning.</summary>
        public void ResetForLevel(LevelData level)
        {
            if (level != null)
            {
                forwardSpeed = level.runSpeed;
                halfWidth = level.trackHalfWidth;
            }

            transform.position = new Vector3(0f, 0f, 0f);
            transform.rotation = Quaternion.identity;

            _startZ = transform.position.z;
            _targetX = 0f;
            _currentX = 0f;
            _steerVelocity = 0f;
            _lean = 0f;
            _leanVelocity = 0f;
            _controlEnabled = false;
        }

        public void SetControlEnabled(bool enabled)
        {
            _controlEnabled = enabled;
        }

        /// <summary>Keeps the blob inside the rails as the crowd grows.</summary>
        public void SetCrowdRadius(float radius)
        {
            _radiusPadding = Mathf.Clamp(radius * 0.45f, 0f, halfWidth * 0.6f);
        }

        private void Update()
        {
            if (GameManager.Instance == null) return;
            if (GameManager.Instance.State != GameState.Running) return;

            float dt = Time.deltaTime;

            if (_controlEnabled) ReadSteering();

            float limit = Mathf.Max(0.5f, halfWidth - _radiusPadding);
            _targetX = Mathf.Clamp(_targetX, -limit, limit);

            _currentX = Mathf.SmoothDamp(_currentX, _targetX, ref _steerVelocity, steerSmoothTime, maxSteerSpeed, dt);

            Vector3 position = transform.position;
            position.x = _currentX;
            position.z += forwardSpeed * dt;
            transform.position = position;

            float targetLean = Mathf.Clamp(-_steerVelocity / Mathf.Max(1f, maxSteerSpeed), -1f, 1f) * maxLeanAngle;
            _lean = Mathf.SmoothDamp(_lean, targetLean, ref _leanVelocity, leanSmoothTime, 360f, dt);
            transform.rotation = Quaternion.Euler(0f, 0f, _lean);
        }

        private void ReadSteering()
        {
            if (!TouchInput.IsPressed) return;

            Vector2 delta = TouchInput.DeltaPixels;
            if (Mathf.Approximately(delta.x, 0f)) return;

            float screenWidth = Mathf.Max(1f, Screen.width);
            _targetX += delta.x / screenWidth * swipeWorldWidth;
        }
    }
}
