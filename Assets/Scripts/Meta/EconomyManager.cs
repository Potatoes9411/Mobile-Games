using System;
using MobClash.Core;
using UnityEngine;

namespace MobClash.Meta
{
    /// <summary>Serialisable player profile. Stored as JSON inside a single PlayerPrefs key.</summary>
    [Serializable]
    public class SaveData
    {
        public string version = "1.0.0";
        public int gold;
        public int levelIndex = 1;
        public int startingCrowdLevel;
        public int goldMultiplierLevel;
        public int gateBonusLevel;
        public int totalRuns;
        public int totalWins;
        public int consecutiveFails;
        public int bestCrowd;
        public bool soundEnabled = true;
        public bool hapticsEnabled = true;
    }

    /// <summary>
    /// Persistent meta progression: gold, level pointer and the three upgrade tracks.
    /// Costs follow the classic hyper casual curve  cost = base * growth ^ level , which keeps the
    /// first three purchases nearly free (fast dopamine) and the tenth expensive (retention sink).
    /// </summary>
    public class EconomyManager : MonoBehaviour
    {
        public const string SaveKey = "MobClash.Save.v1";

        public static EconomyManager Instance { get; private set; }

        [Header("Starting Crowd Size")]
        public int startingCrowdBase = 20;
        public int startingCrowdPerLevel = 2;
        public int startingCrowdCostBase = 120;
        public float startingCrowdCostGrowth = 1.35f;

        [Header("Gold Multiplier")]
        public float goldMultiplierPerLevel = 0.15f;
        public int goldMultiplierCostBase = 300;
        public float goldMultiplierCostGrowth = 1.45f;

        [Header("Gate Bonus")]
        public int gateFlatBonusPerLevel = 1;
        public float gateMultiplierBonusPerLevel = 0.05f;
        public int gateBonusCostBase = 500;
        public float gateBonusCostGrowth = 1.5f;

        [Header("Limits")]
        public int maxUpgradeLevel = 50;

        [Header("Dynamic difficulty")]
        [Tooltip("Tower power is reduced by this much for every consecutive loss.")]
        public float ddsReliefPerFail = 0.08f;
        public float ddsMinimumBias = 0.60f;

        private SaveData _data = new SaveData();
        private bool _loaded;

        public SaveData Data { get { EnsureLoaded(); return _data; } }

        public int Gold { get { return Data.gold; } }
        public int LevelIndex { get { return Mathf.Max(1, Data.levelIndex); } }
        public int TotalRuns { get { return Data.totalRuns; } }
        public int TotalWins { get { return Data.totalWins; } }
        public int BestCrowd { get { return Data.bestCrowd; } }
        public bool SoundEnabled { get { return Data.soundEnabled; } }
        public bool HapticsEnabled { get { return Data.hapticsEnabled; } }

        /// <summary>Crowd size the player starts every run with.</summary>
        public int StartingCrowd
        {
            get { return startingCrowdBase + startingCrowdPerLevel * Data.startingCrowdLevel; }
        }

        /// <summary>Multiplier applied to all gold earned.</summary>
        public float GoldMultiplier
        {
            get { return 1f + goldMultiplierPerLevel * Data.goldMultiplierLevel; }
        }

        /// <summary>Extra units granted by every additive gate.</summary>
        public int GateFlatBonus
        {
            get { return gateFlatBonusPerLevel * Data.gateBonusLevel; }
        }

        /// <summary>Extra multiplier added to every multiplicative gate (x3 becomes x3.25 and so on).</summary>
        public float GateMultiplierBonus
        {
            get { return gateMultiplierBonusPerLevel * Data.gateBonusLevel; }
        }

        /// <summary>Difficulty relief after repeated failures. 1 = full difficulty.</summary>
        public float DifficultyBias
        {
            get
            {
                float bias = 1f - ddsReliefPerFail * Data.consecutiveFails;
                return Mathf.Clamp(bias, ddsMinimumBias, 1f);
            }
        }

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }

            Instance = this;
            EnsureLoaded();
        }

        private void OnDestroy()
        {
            if (Instance == this) Instance = null;
        }

        private void OnApplicationPause(bool paused)
        {
            if (paused) Save();
        }

        private void OnApplicationQuit()
        {
            Save();
        }

        // ------------------------------------------------------------------ persistence

        public void EnsureLoaded()
        {
            if (_loaded) return;
            _loaded = true;
            Load();
        }

        public void Load()
        {
            _loaded = true;

            string json = PlayerPrefs.GetString(SaveKey, string.Empty);
            if (string.IsNullOrEmpty(json))
            {
                _data = new SaveData();
                return;
            }

            try
            {
                SaveData parsed = JsonUtility.FromJson<SaveData>(json);
                _data = parsed ?? new SaveData();
            }
            catch (Exception exception)
            {
                Debug.LogWarning("[MobClash] Save file corrupt, starting fresh: " + exception.Message);
                _data = new SaveData();
            }

            if (_data.levelIndex < 1) _data.levelIndex = 1;
            if (_data.gold < 0) _data.gold = 0;
        }

        public void Save()
        {
            EnsureLoaded();

            try
            {
                PlayerPrefs.SetString(SaveKey, JsonUtility.ToJson(_data));
                PlayerPrefs.Save();
            }
            catch (Exception exception)
            {
                Debug.LogWarning("[MobClash] Could not write save: " + exception.Message);
            }
        }

        public void ResetProgress()
        {
            _data = new SaveData();
            _loaded = true;
            Save();
            GameEvents.RaiseGoldChanged(_data.gold);
        }

        // ------------------------------------------------------------------ currency

        public void AddGold(int amount)
        {
            if (amount <= 0) return;

            EnsureLoaded();
            _data.gold = Mathf.Clamp(_data.gold + amount, 0, int.MaxValue - 1);
            Save();
            GameEvents.RaiseGoldChanged(_data.gold);
        }

        public bool SpendGold(int amount)
        {
            EnsureLoaded();
            if (amount <= 0) return true;
            if (_data.gold < amount) return false;

            _data.gold -= amount;
            Save();
            GameEvents.RaiseGoldChanged(_data.gold);
            return true;
        }

        // ------------------------------------------------------------------ upgrades

        public int GetUpgradeLevel(UpgradeType type)
        {
            EnsureLoaded();
            switch (type)
            {
                case UpgradeType.StartingCrowd: return _data.startingCrowdLevel;
                case UpgradeType.GoldMultiplier: return _data.goldMultiplierLevel;
                case UpgradeType.GateBonus: return _data.gateBonusLevel;
                default: return 0;
            }
        }

        public bool IsMaxed(UpgradeType type)
        {
            return GetUpgradeLevel(type) >= maxUpgradeLevel;
        }

        public int GetUpgradeCost(UpgradeType type)
        {
            int level = GetUpgradeLevel(type);
            if (level >= maxUpgradeLevel) return int.MaxValue;

            switch (type)
            {
                case UpgradeType.StartingCrowd:
                    return Mathf.RoundToInt(startingCrowdCostBase * Mathf.Pow(startingCrowdCostGrowth, level));
                case UpgradeType.GoldMultiplier:
                    return Mathf.RoundToInt(goldMultiplierCostBase * Mathf.Pow(goldMultiplierCostGrowth, level));
                case UpgradeType.GateBonus:
                    return Mathf.RoundToInt(gateBonusCostBase * Mathf.Pow(gateBonusCostGrowth, level));
                default:
                    return int.MaxValue;
            }
        }

        /// <summary>Player facing value of the upgrade at its current level, e.g. "24" or "1.45x".</summary>
        public string GetUpgradeValueLabel(UpgradeType type)
        {
            switch (type)
            {
                case UpgradeType.StartingCrowd:
                    return StartingCrowd.ToString();
                case UpgradeType.GoldMultiplier:
                    return GoldMultiplier.ToString("0.00") + "x";
                case UpgradeType.GateBonus:
                    return "+" + GateFlatBonus + " / +" + GateMultiplierBonus.ToString("0.00") + "x";
                default:
                    return string.Empty;
            }
        }

        public bool CanAfford(UpgradeType type)
        {
            if (IsMaxed(type)) return false;
            return Gold >= GetUpgradeCost(type);
        }

        public bool TryPurchase(UpgradeType type)
        {
            if (IsMaxed(type)) return false;

            int cost = GetUpgradeCost(type);
            if (!SpendGold(cost)) return false;

            switch (type)
            {
                case UpgradeType.StartingCrowd:
                    _data.startingCrowdLevel++;
                    break;
                case UpgradeType.GoldMultiplier:
                    _data.goldMultiplierLevel++;
                    break;
                case UpgradeType.GateBonus:
                    _data.gateBonusLevel++;
                    break;
            }

            Save();
            GameEvents.RaiseUpgradePurchased(type, GetUpgradeLevel(type));
            return true;
        }

        // ------------------------------------------------------------------ run bookkeeping

        public void RegisterRunStarted()
        {
            EnsureLoaded();
            _data.totalRuns++;
            Save();
        }

        public void RegisterRunFinished(bool win, int finalCrowd)
        {
            EnsureLoaded();

            if (finalCrowd > _data.bestCrowd) _data.bestCrowd = finalCrowd;

            if (win)
            {
                _data.totalWins++;
                _data.consecutiveFails = 0;
                _data.levelIndex = Mathf.Max(1, _data.levelIndex + 1);
            }
            else
            {
                _data.consecutiveFails++;
            }

            Save();
        }

        public void SetSoundEnabled(bool enabled)
        {
            EnsureLoaded();
            _data.soundEnabled = enabled;
            Save();
        }

        public void SetHapticsEnabled(bool enabled)
        {
            EnsureLoaded();
            _data.hapticsEnabled = enabled;
            Save();
        }
    }
}
