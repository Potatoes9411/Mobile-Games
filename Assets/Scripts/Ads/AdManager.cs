using System;
using System.Collections;
using MobClash.Core;
using UnityEngine;

namespace MobClash.Ads
{
    /// <summary>Ad placements used by the monetisation flow.</summary>
    public enum AdPlacement
    {
        InterstitialLevelComplete = 0,
        RewardedTripleGold = 1,
        RewardedRevive = 2
    }

    /// <summary>
    /// Production shaped mock ad framework.
    ///
    /// The public surface (Initialise / IsRewardedReady / ShowInterstitial / ShowRewarded) matches
    /// the shape of every major mediation SDK, so swapping in LevelPlay, AppLovin MAX or AdMob is a
    /// matter of replacing the bodies of the four Show/Load methods. Until then the mock renders a
    /// full screen placeholder, waits, and fires the same callbacks, which lets the whole
    /// monetisation flow be tested in the editor and in an APK with no network.
    /// </summary>
    public class AdManager : MonoBehaviour
    {
        public static AdManager Instance { get; private set; }

        [Header("Cadence")]
        [Tooltip("Show an interstitial at most this often, in seconds.")]
        public float interstitialCooldown = 45f;

        [Tooltip("First level index that is allowed to show an interstitial.")]
        public int firstInterstitialLevel = 3;

        [Tooltip("Show an interstitial every N completed levels.")]
        public int interstitialEveryNLevels = 2;

        [Header("Mock behaviour")]
        public float mockInterstitialDuration = 1.6f;
        public float mockRewardedDuration = 2.2f;
        public bool simulateFillFailures;
        [Range(0f, 1f)] public float mockNoFillChance = 0.05f;

        [Header("Debug")]
        public bool verboseLogging = true;

        private float _lastInterstitialTime = -999f;
        private int _levelsSinceInterstitial;
        private bool _adInProgress;
        private Canvas _placeholderCanvas;
        private CanvasGroup _placeholderGroup;
        private UnityEngine.UI.Text _placeholderLabel;

        public bool IsAdPlaying { get { return _adInProgress; } }

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }

            Instance = this;
            Initialise();
        }

        private void OnDestroy()
        {
            if (Instance == this) Instance = null;
        }

        /// <summary>Replace with the mediation SDK initialise call.</summary>
        public void Initialise()
        {
            Log("Ad SDK initialised (mock).");
        }

        public bool IsInterstitialReady
        {
            get { return !_adInProgress; }
        }

        public bool IsRewardedReady
        {
            get { return !_adInProgress; }
        }

        /// <summary>Decides whether the level complete interstitial should be shown this time.</summary>
        public bool ShouldShowInterstitial(int levelIndex)
        {
            if (levelIndex < firstInterstitialLevel) return false;
            if (Time.realtimeSinceStartup - _lastInterstitialTime < interstitialCooldown) return false;
            if (_levelsSinceInterstitial < Mathf.Max(1, interstitialEveryNLevels)) return false;
            return true;
        }

        public void NotifyLevelCompleted()
        {
            _levelsSinceInterstitial++;
        }

        /// <summary>Shows an interstitial and invokes <paramref name="onClosed"/> exactly once.</summary>
        public void ShowInterstitial(int levelIndex, Action onClosed)
        {
            if (!ShouldShowInterstitial(levelIndex) || _adInProgress)
            {
                if (onClosed != null) onClosed();
                return;
            }

            _lastInterstitialTime = Time.realtimeSinceStartup;
            _levelsSinceInterstitial = 0;
            StartCoroutine(PlayMockAd(AdPlacement.InterstitialLevelComplete, mockInterstitialDuration, null, onClosed));
        }

        /// <summary>
        /// Shows a rewarded video. <paramref name="onResult"/> receives true when the reward was
        /// earned (the user watched to the end) and false on skip or no fill.
        /// </summary>
        public void ShowRewarded(AdPlacement placement, Action<bool> onResult)
        {
            if (_adInProgress)
            {
                if (onResult != null) onResult(false);
                return;
            }

            if (simulateFillFailures && UnityEngine.Random.value < mockNoFillChance)
            {
                Log("Rewarded no fill (mock).");
                if (onResult != null) onResult(false);
                return;
            }

            StartCoroutine(PlayMockAd(placement, mockRewardedDuration, onResult, null));
        }

        private IEnumerator PlayMockAd(AdPlacement placement, float duration, Action<bool> onResult, Action onClosed)
        {
            _adInProgress = true;

            float previousTimeScale = Time.timeScale;
            Time.timeScale = 0f;

            ShowPlaceholder(placement);

            float elapsed = 0f;
            while (elapsed < duration)
            {
                elapsed += Time.unscaledDeltaTime;
                UpdatePlaceholder(placement, duration - elapsed);
                yield return null;
            }

            HidePlaceholder();

            Time.timeScale = previousTimeScale <= 0.01f ? 1f : previousTimeScale;
            _adInProgress = false;

            Log("Ad finished: " + placement);

            if (onResult != null) onResult(true);
            if (onClosed != null) onClosed();
        }

        // ------------------------------------------------------------------ placeholder UI

        private void EnsurePlaceholder()
        {
            if (_placeholderCanvas != null) return;

            GameObject canvasObject = new GameObject("MockAdCanvas");
            canvasObject.transform.SetParent(transform, false);

            _placeholderCanvas = canvasObject.AddComponent<Canvas>();
            _placeholderCanvas.renderMode = RenderMode.ScreenSpaceOverlay;
            _placeholderCanvas.sortingOrder = 999;
            canvasObject.AddComponent<UnityEngine.UI.CanvasScaler>();
            canvasObject.AddComponent<UnityEngine.UI.GraphicRaycaster>();

            _placeholderGroup = canvasObject.AddComponent<CanvasGroup>();
            _placeholderGroup.alpha = 0f;
            _placeholderGroup.blocksRaycasts = false;

            GameObject background = new GameObject("Background");
            background.transform.SetParent(canvasObject.transform, false);
            UnityEngine.UI.Image image = background.AddComponent<UnityEngine.UI.Image>();
            image.color = new Color(0.03f, 0.04f, 0.08f, 0.96f);
            RectTransform backgroundRect = image.rectTransform;
            backgroundRect.anchorMin = Vector2.zero;
            backgroundRect.anchorMax = Vector2.one;
            backgroundRect.offsetMin = Vector2.zero;
            backgroundRect.offsetMax = Vector2.zero;

            GameObject labelObject = new GameObject("Label");
            labelObject.transform.SetParent(canvasObject.transform, false);
            _placeholderLabel = labelObject.AddComponent<UnityEngine.UI.Text>();
            _placeholderLabel.font = PrimitiveFactory.BuiltinFont();
            _placeholderLabel.fontSize = 36;
            _placeholderLabel.alignment = TextAnchor.MiddleCenter;
            _placeholderLabel.color = Color.white;
            RectTransform labelRect = _placeholderLabel.rectTransform;
            labelRect.anchorMin = Vector2.zero;
            labelRect.anchorMax = Vector2.one;
            labelRect.offsetMin = Vector2.zero;
            labelRect.offsetMax = Vector2.zero;

            canvasObject.SetActive(true);
        }

        private void ShowPlaceholder(AdPlacement placement)
        {
            EnsurePlaceholder();
            _placeholderGroup.alpha = 1f;
            _placeholderGroup.blocksRaycasts = true;
            UpdatePlaceholder(placement, 0f);
        }

        private void UpdatePlaceholder(AdPlacement placement, float remaining)
        {
            if (_placeholderLabel == null) return;

            string title = placement == AdPlacement.InterstitialLevelComplete
                ? "INTERSTITIAL AD"
                : "REWARDED VIDEO";

            _placeholderLabel.text = title + "\n(mock placement)\n\n" +
                                    Mathf.Max(0f, remaining).ToString("0.0") + "s";
        }

        private void HidePlaceholder()
        {
            if (_placeholderGroup == null) return;
            _placeholderGroup.alpha = 0f;
            _placeholderGroup.blocksRaycasts = false;
        }

        private void Log(string message)
        {
            if (verboseLogging) Debug.Log("[Ads] " + message);
        }
    }
}
