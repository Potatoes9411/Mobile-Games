using System.Collections.Generic;
using MobClash.Core;
using MobClash.Data;
using MobClash.Juice;
using UnityEngine;

namespace MobClash.Track
{
    /// <summary>
    /// Track hazard. It does not damage through physics: the SwarmManager reads the shared
    /// <see cref="Active"/> registry and removes any crowd member inside <see cref="killRadius"/>.
    /// That gives the satisfying "the blade shaves units off the blob" feel for the price of a few
    /// distance checks, instead of 500 rigidbodies.
    /// The trigger collider is used only for camera shake and haptics on the crowd pivot.
    /// </summary>
    public class Obstacle : MonoBehaviour
    {
        public static readonly List<Obstacle> Active = new List<Obstacle>(32);

        public ObstacleKind kind = ObstacleKind.Static;
        public float motionAmplitude = 2f;
        public float motionSpeed = 0.5f;
        public float killRadius = 1.15f;

        private Vector3 _origin;
        private float _phase;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        private static void ResetRegistry()
        {
            Active.Clear();
        }

        private void OnEnable()
        {
            if (!Active.Contains(this)) Active.Add(this);
        }

        private void OnDisable()
        {
            Active.Remove(this);
        }

        public void Configure(ObstacleSpec spec, float halfWidth)
        {
            kind = spec.kind;
            motionAmplitude = spec.motionAmplitude;
            motionSpeed = spec.motionSpeed;
            killRadius = spec.killRadius > 0f ? spec.killRadius : killRadius;

            float x = Mathf.Clamp(spec.lane, -1f, 1f) * (halfWidth - killRadius);
            transform.position = new Vector3(x, 0f, spec.distance);
            _origin = transform.position;
            _phase = Random.Range(0f, 6.2831853f);

            SphereCollider sphere = GetComponent<SphereCollider>();
            if (sphere != null) sphere.radius = killRadius;

            gameObject.layer = GameLayers.ObstacleLayer;
        }

        private void Awake()
        {
            _origin = transform.position;
        }

        private void Update()
        {
            switch (kind)
            {
                case ObstacleKind.Sweeper:
                {
                    float x = _origin.x + Mathf.Sin(Time.time * motionSpeed * 6.2831853f + _phase) * motionAmplitude;
                    transform.position = new Vector3(x, _origin.y, _origin.z);
                    break;
                }
                case ObstacleKind.Spinner:
                {
                    float angle = (Time.time * motionSpeed + _phase) * 6.2831853f;
                    transform.position = new Vector3(
                        _origin.x + Mathf.Cos(angle) * motionAmplitude,
                        _origin.y,
                        _origin.z + Mathf.Sin(angle) * motionAmplitude * 0.35f);
                    transform.Rotate(Vector3.up, 240f * Time.deltaTime, Space.Self);
                    break;
                }
                default:
                    break;
            }
        }

        private void OnTriggerEnter(Collider other)
        {
            if (other == null) return;
            if (JuiceManager.Instance == null) return;

            JuiceManager.Instance.Shake(0.18f, 0.22f);
            JuiceManager.Instance.HapticLight();
        }
    }
}
