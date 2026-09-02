using MobClash.Core;
using UnityEngine;

namespace MobClash.Track
{
    /// <summary>End of the swerve phase. Hands control to the siege state machine exactly once.</summary>
    [RequireComponent(typeof(BoxCollider))]
    public class FinishLine : MonoBehaviour
    {
        private bool _triggered;

        public void ResetLine()
        {
            _triggered = false;
            Collider collider = GetComponent<Collider>();
            if (collider != null) collider.enabled = true;
        }

        private void OnTriggerEnter(Collider other)
        {
            if (_triggered || other == null) return;
            if (!other.CompareTag(GameLayers.TagPlayer) &&
                other.gameObject.layer != GameLayers.PlayerLayer) return;

            _triggered = true;

            if (GameManager.Instance != null) GameManager.Instance.OnFinishLineReached();
        }
    }
}
