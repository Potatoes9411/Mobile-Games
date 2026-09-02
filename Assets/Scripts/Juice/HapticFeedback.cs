using UnityEngine;

namespace MobClash.Juice
{
    /// <summary>
    /// Native Android vibration with graceful degradation.
    /// API 26+ uses VibrationEffect (amplitude control), older devices fall back to the legacy
    /// millisecond vibrate call, and every other platform falls back to Handheld.Vibrate or a no-op.
    /// Requires android.permission.VIBRATE, which Unity adds automatically when Handheld.Vibrate
    /// is referenced; the manifest snippet is also listed in docs/ANDROID_BUILD.md.
    /// </summary>
    public static class HapticFeedback
    {
        private static bool _initialised;
        private static bool _available;

#if UNITY_ANDROID && !UNITY_EDITOR
        private static AndroidJavaObject _vibrator;
        private static AndroidJavaClass _vibrationEffectClass;
        private static int _sdkInt;
#endif

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        private static void ResetStatics()
        {
            _initialised = false;
            _available = false;
#if UNITY_ANDROID && !UNITY_EDITOR
            _vibrator = null;
            _vibrationEffectClass = null;
            _sdkInt = 0;
#endif
        }

        private static void Initialise()
        {
            if (_initialised) return;
            _initialised = true;

#if UNITY_ANDROID && !UNITY_EDITOR
            try
            {
                using (AndroidJavaClass version = new AndroidJavaClass("android.os.Build$VERSION"))
                {
                    _sdkInt = version.GetStatic<int>("SDK_INT");
                }

                using (AndroidJavaClass player = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
                {
                    AndroidJavaObject activity = player.GetStatic<AndroidJavaObject>("currentActivity");
                    _vibrator = activity.Call<AndroidJavaObject>("getSystemService", "vibrator");
                }

                if (_sdkInt >= 26)
                {
                    _vibrationEffectClass = new AndroidJavaClass("android.os.VibrationEffect");
                }

                _available = _vibrator != null && _vibrator.Call<bool>("hasVibrator");
            }
            catch (System.Exception exception)
            {
                Debug.LogWarning("[MobClash] Haptics unavailable: " + exception.Message);
                _available = false;
            }
#else
            _available = SystemInfo.supportsVibration;
#endif
        }

        /// <summary>Fires a one shot vibration. Amplitude is 1-255 and is ignored below API 26.</summary>
        public static void Vibrate(long milliseconds, int amplitude)
        {
            Initialise();
            if (!_available) return;

            milliseconds = (long)Mathf.Clamp(milliseconds, 1, 2000);
            amplitude = Mathf.Clamp(amplitude, 1, 255);

#if UNITY_ANDROID && !UNITY_EDITOR
            try
            {
                if (_sdkInt >= 26 && _vibrationEffectClass != null)
                {
                    AndroidJavaObject effect = _vibrationEffectClass.CallStatic<AndroidJavaObject>(
                        "createOneShot", milliseconds, amplitude);
                    _vibrator.Call("vibrate", effect);
                }
                else
                {
                    _vibrator.Call("vibrate", milliseconds);
                }
            }
            catch (System.Exception exception)
            {
                Debug.LogWarning("[MobClash] Vibrate failed: " + exception.Message);
                _available = false;
            }
#else
            if (Application.isMobilePlatform) Handheld.Vibrate();
#endif
        }

        public static void Selection() { Vibrate(10, 60); }
        public static void Light() { Vibrate(18, 90); }
        public static void Medium() { Vibrate(35, 160); }
        public static void Heavy() { Vibrate(60, 235); }

        public static void Success()
        {
            Vibrate(22, 120);
        }

        public static void Failure()
        {
            Vibrate(90, 255);
        }
    }
}
