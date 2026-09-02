using System.Collections;
using MobClash.Core;
using MobClash.Meta;
using MobClash.Player;
using MobClash.Swarm;
using UnityEngine;

namespace MobClash.Juice
{
    /// <summary>
    /// The satisfaction layer. Everything that is not simulation lives here:
    /// camera shake, code driven squash and stretch, floating combat text, hit stop and haptics.
    /// Systems call into it through the singleton so no gameplay script depends on presentation.
    /// </summary>
    public class JuiceManager : MonoBehaviour
    {
        public static JuiceManager Instance { get; private set; }

        [Header("Wiring")]
        public CameraRig cameraRig;
        public AudioSource sfxSource;
        public ObjectPool floatingTextPool;
        public GameObject floatingTextPrefab;

        [Header("Floating text")]
        public int floatingTextPrewarm = 16;
        public float floatingTextLifetime = 0.9f;
        public float floatingTextScale = 1.1f;

        [Header("Shake")]
        public float shakeScale = 1f;

        [Header("Audio")]
        [Range(0f, 1f)] public float sfxVolume = 0.65f;

        private Coroutine _hitStopRoutine;

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(this);
                return;
            }

            Instance = this;
            EnsureAudio();
            EnsureTextPool();
        }

        private void OnDestroy()
        {
            if (Instance == this) Instance = null;
        }

        private void Start()
        {
            if (cameraRig == null) cameraRig = FindCameraRig();
        }

        private static CameraRig FindCameraRig()
        {
#if UNITY_2023_1_OR_NEWER
            return Object.FindFirstObjectByType<CameraRig>();
#else
            return Object.FindObjectOfType<CameraRig>();
#endif
        }

        private void EnsureAudio()
        {
            if (sfxSource == null)
            {
                sfxSource = gameObject.AddComponent<AudioSource>();
                sfxSource.playOnAwake = false;
                sfxSource.spatialBlend = 0f;
                sfxSource.volume = sfxVolume;
            }
        }

        private void EnsureTextPool()
        {
            if (floatingTextPrefab == null)
            {
                GameObject prefab = PrimitiveFactory.CreateWorldText("FloatingText", "0", floatingTextScale);
                prefab.AddComponent<FloatingText>();
                prefab.SetActive(false);
                prefab.transform.SetParent(transform, false);
                floatingTextPrefab = prefab;
            }

            if (floatingTextPool == null)
            {
                floatingTextPool = gameObject.AddComponent<ObjectPool>();
                floatingTextPool.hardCap = 64;
            }

            floatingTextPool.Initialise(floatingTextPrefab, floatingTextPrewarm);
        }

        // ------------------------------------------------------------------ camera

        public void Shake(float amplitude, float duration)
        {
            if (cameraRig == null) cameraRig = FindCameraRig();
            if (cameraRig == null) return;

            cameraRig.Shake(amplitude * shakeScale, duration);
        }

        /// <summary>Brief slow motion that sells a big impact. Uses unscaled time internally.</summary>
        public void HitStop(float scale, float duration)
        {
            if (_hitStopRoutine != null) StopCoroutine(_hitStopRoutine);
            _hitStopRoutine = StartCoroutine(HitStopRoutine(Mathf.Clamp(scale, 0.05f, 1f), duration));
        }

        private IEnumerator HitStopRoutine(float scale, float duration)
        {
            float previous = Time.timeScale;
            Time.timeScale = scale;

            float elapsed = 0f;
            while (elapsed < duration)
            {
                elapsed += Time.unscaledDeltaTime;
                yield return null;
            }

            Time.timeScale = previous <= 0.05f ? 1f : previous;
            _hitStopRoutine = null;
        }

        // ------------------------------------------------------------------ floating text

        public void SpawnFloatingText(Vector3 position, string content, Color color)
        {
            EnsureTextPool();
            if (floatingTextPool == null) return;

            GameObject instance = floatingTextPool.Get();
            if (instance == null) return;

            instance.transform.SetParent(transform, false);

            FloatingText text = instance.GetComponent<FloatingText>();
            if (text == null) text = instance.AddComponent<FloatingText>();

            text.Play(position, content, color, floatingTextLifetime, ReleaseFloatingText);
        }

        private void ReleaseFloatingText(FloatingText text)
        {
            if (text == null || floatingTextPool == null) return;
            floatingTextPool.Release(text.gameObject);
        }

        // ------------------------------------------------------------------ squash and stretch

        /// <summary>Procedural squash and stretch. No animation clips, no DOTween dependency.</summary>
        public void PunchScale(Transform target, float amount, float duration)
        {
            if (target == null) return;
            StartCoroutine(PunchRoutine(target, amount, Mathf.Max(0.05f, duration)));
        }

        private IEnumerator PunchRoutine(Transform target, float amount, float duration)
        {
            Vector3 baseScale = target.localScale;
            float elapsed = 0f;

            while (elapsed < duration && target != null)
            {
                elapsed += Time.unscaledDeltaTime;
                float t = Mathf.Clamp01(elapsed / duration);
                float wave = Mathf.Sin(t * Mathf.PI) * amount;

                target.localScale = new Vector3(
                    baseScale.x * (1f + wave),
                    baseScale.y * (1f - wave * 0.55f),
                    baseScale.z * (1f + wave));

                yield return null;
            }

            if (target != null) target.localScale = baseScale;
        }

        // ------------------------------------------------------------------ audio

        public void PlaySound(AudioClip clip, float volumeScale)
        {
            if (clip == null) return;
            if (EconomyManager.Instance != null && !EconomyManager.Instance.SoundEnabled) return;

            EnsureAudio();
            sfxSource.PlayOneShot(clip, Mathf.Clamp01(sfxVolume * volumeScale));
        }

        public void PlayGateSound(bool positive)
        {
            PlaySound(positive ? SfxLibrary.GatePositive : SfxLibrary.GateNegative, 1f);
        }

        public void PlayImpact() { PlaySound(SfxLibrary.Impact, 1f); }
        public void PlayCoin() { PlaySound(SfxLibrary.Coin, 0.8f); }
        public void PlaySelect() { PlaySound(SfxLibrary.Select, 0.7f); }
        public void PlayVictory() { PlaySound(SfxLibrary.Victory, 1f); }
        public void PlayDefeat() { PlaySound(SfxLibrary.Defeat, 1f); }

        // ------------------------------------------------------------------ haptics

        private bool HapticsAllowed
        {
            get { return EconomyManager.Instance == null || EconomyManager.Instance.HapticsEnabled; }
        }

        public void HapticSelection() { if (HapticsAllowed) HapticFeedback.Selection(); }
        public void HapticLight() { if (HapticsAllowed) HapticFeedback.Light(); }
        public void HapticMedium() { if (HapticsAllowed) HapticFeedback.Medium(); }
        public void HapticHeavy() { if (HapticsAllowed) HapticFeedback.Heavy(); }
        public void HapticSuccess() { if (HapticsAllowed) HapticFeedback.Success(); }
        public void HapticFailure() { if (HapticsAllowed) HapticFeedback.Failure(); }
    }
}
