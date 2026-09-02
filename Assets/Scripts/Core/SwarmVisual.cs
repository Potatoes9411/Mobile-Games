using UnityEngine;

namespace MobClash.Core
{
    /// <summary>
    /// Data-only marker on a crowd member. It deliberately has no Update: all 500+ units are
    /// animated from a single loop inside SwarmManager to keep the per-frame cost flat.
    /// </summary>
    public class SwarmVisual : MonoBehaviour
    {
        [Tooltip("Transform that receives the squash and stretch. Usually the capsule child.")]
        public Transform body;

        [HideInInspector] public float phase;
        [HideInInspector] public float damp = 0.12f;
        [HideInInspector] public Vector3 jitter;

        private Vector3 _bodyBaseScale = Vector3.one;
        private bool _cached;

        public Vector3 BodyBaseScale
        {
            get
            {
                if (!_cached)
                {
                    _bodyBaseScale = body != null ? body.localScale : Vector3.one;
                    _cached = true;
                }
                return _bodyBaseScale;
            }
        }
    }
}
