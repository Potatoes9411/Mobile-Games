using System;
using UnityEngine;

namespace MobClash.Juice
{
    /// <summary>
    /// Pooled world space combat text. Billboards to the camera, rises, punches in and fades out.
    /// Uses the built in TextMesh so no font assets or text packages are required.
    /// </summary>
    public class FloatingText : MonoBehaviour
    {
        public TextMesh textMesh;
        public float riseSpeed = 2.4f;
        public float lifetime = 0.9f;
        public float popScale = 1.35f;

        private float _timer;
        private Color _color;
        private Vector3 _baseScale = Vector3.one;
        private Transform _cameraTransform;
        private Action<FloatingText> _onFinished;
        private bool _playing;

        private void Awake()
        {
            if (textMesh == null) textMesh = GetComponent<TextMesh>();
            if (textMesh == null) textMesh = GetComponentInChildren<TextMesh>();
            _baseScale = transform.localScale;
        }

        public void Play(Vector3 position, string content, Color color, float duration, Action<FloatingText> onFinished)
        {
            if (textMesh == null) textMesh = GetComponentInChildren<TextMesh>();

            transform.position = position;
            transform.localScale = _baseScale;

            _color = color;
            _timer = 0f;
            lifetime = Mathf.Max(0.2f, duration);
            _onFinished = onFinished;
            _playing = true;

            if (textMesh != null)
            {
                textMesh.text = content;
                textMesh.color = color;
            }

            Camera camera = Camera.main;
            _cameraTransform = camera != null ? camera.transform : null;
        }

        private void Update()
        {
            if (!_playing) return;

            float dt = Time.unscaledDeltaTime;
            _timer += dt;

            float t = Mathf.Clamp01(_timer / lifetime);

            transform.position += Vector3.up * riseSpeed * dt;

            float pop = 1f + (popScale - 1f) * Mathf.Sin(Mathf.Clamp01(t * 3f) * Mathf.PI * 0.5f) * (1f - t);
            transform.localScale = _baseScale * pop;

            if (_cameraTransform != null)
            {
                transform.rotation = Quaternion.LookRotation(
                    transform.position - _cameraTransform.position, Vector3.up);
            }

            if (textMesh != null)
            {
                Color color = _color;
                color.a = 1f - Mathf.SmoothStep(0f, 1f, Mathf.InverseLerp(0.55f, 1f, t));
                textMesh.color = color;
            }

            if (t >= 1f)
            {
                _playing = false;
                if (_onFinished != null)
                {
                    Action<FloatingText> callback = _onFinished;
                    _onFinished = null;
                    callback(this);
                }
            }
        }
    }
}
