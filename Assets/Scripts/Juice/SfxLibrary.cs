using UnityEngine;

namespace MobClash.Juice
{
    /// <summary>
    /// Procedurally synthesised sound effects. Keeps the repository binary free while still giving
    /// the ASMR layer real audio feedback. Every clip is generated once and cached.
    /// </summary>
    public static class SfxLibrary
    {
        private const int SampleRate = 44100;

        private static AudioClip _gatePositive;
        private static AudioClip _gateNegative;
        private static AudioClip _impact;
        private static AudioClip _coin;
        private static AudioClip _victory;
        private static AudioClip _defeat;
        private static AudioClip _select;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        private static void ResetStatics()
        {
            _gatePositive = null;
            _gateNegative = null;
            _impact = null;
            _coin = null;
            _victory = null;
            _defeat = null;
            _select = null;
        }

        public static AudioClip GatePositive
        {
            get
            {
                if (_gatePositive == null)
                {
                    _gatePositive = Sweep("sfx_gate_positive", 520f, 1180f, 0.16f, 12f, 0.35f);
                }
                return _gatePositive;
            }
        }

        public static AudioClip GateNegative
        {
            get
            {
                if (_gateNegative == null)
                {
                    _gateNegative = Sweep("sfx_gate_negative", 420f, 160f, 0.20f, 9f, 0.45f);
                }
                return _gateNegative;
            }
        }

        public static AudioClip Impact
        {
            get
            {
                if (_impact == null) _impact = Noise("sfx_impact", 0.16f, 22f, 0.55f);
                return _impact;
            }
        }

        public static AudioClip Coin
        {
            get
            {
                if (_coin == null) _coin = Sweep("sfx_coin", 880f, 1760f, 0.10f, 20f, 0.28f);
                return _coin;
            }
        }

        public static AudioClip Select
        {
            get
            {
                if (_select == null) _select = Sweep("sfx_select", 680f, 720f, 0.06f, 26f, 0.25f);
                return _select;
            }
        }

        public static AudioClip Victory
        {
            get
            {
                if (_victory == null)
                {
                    float[] notes = { 523.25f, 659.25f, 783.99f, 1046.5f };
                    _victory = Arpeggio("sfx_victory", notes, 0.10f, 0.32f);
                }
                return _victory;
            }
        }

        public static AudioClip Defeat
        {
            get
            {
                if (_defeat == null)
                {
                    float[] notes = { 440f, 349.23f, 261.63f };
                    _defeat = Arpeggio("sfx_defeat", notes, 0.14f, 0.30f);
                }
                return _defeat;
            }
        }

        private static AudioClip Sweep(string name, float startHz, float endHz, float duration, float decay, float volume)
        {
            int sampleCount = Mathf.Max(16, Mathf.RoundToInt(duration * SampleRate));
            float[] samples = new float[sampleCount];
            float phase = 0f;

            for (int i = 0; i < sampleCount; i++)
            {
                float t = i / (float)sampleCount;
                float frequency = Mathf.Lerp(startHz, endHz, t);
                phase += 2f * Mathf.PI * frequency / SampleRate;

                float envelope = Mathf.Exp(-decay * t);
                float wave = Mathf.Sin(phase) * 0.75f + Mathf.Sin(phase * 2f) * 0.25f;
                samples[i] = wave * envelope * volume;
            }

            return FromSamples(name, samples);
        }

        private static AudioClip Noise(string name, float duration, float decay, float volume)
        {
            int sampleCount = Mathf.Max(16, Mathf.RoundToInt(duration * SampleRate));
            float[] samples = new float[sampleCount];
            float low = 0f;

            for (int i = 0; i < sampleCount; i++)
            {
                float t = i / (float)sampleCount;
                float white = Random.Range(-1f, 1f);
                low = Mathf.Lerp(low, white, 0.28f);
                samples[i] = low * Mathf.Exp(-decay * t) * volume;
            }

            return FromSamples(name, samples);
        }

        private static AudioClip Arpeggio(string name, float[] notes, float noteDuration, float volume)
        {
            int perNote = Mathf.Max(16, Mathf.RoundToInt(noteDuration * SampleRate));
            float[] samples = new float[perNote * notes.Length];

            for (int n = 0; n < notes.Length; n++)
            {
                float phase = 0f;
                for (int i = 0; i < perNote; i++)
                {
                    float t = i / (float)perNote;
                    phase += 2f * Mathf.PI * notes[n] / SampleRate;
                    float envelope = Mathf.Sin(Mathf.Clamp01(t) * Mathf.PI);
                    samples[n * perNote + i] = Mathf.Sin(phase) * envelope * volume;
                }
            }

            return FromSamples(name, samples);
        }

        private static AudioClip FromSamples(string name, float[] samples)
        {
            AudioClip clip = AudioClip.Create(name, samples.Length, 1, SampleRate, false);
            clip.SetData(samples, 0);
            return clip;
        }
    }
}
