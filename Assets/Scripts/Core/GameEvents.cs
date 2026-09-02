using System;
using UnityEngine;

namespace MobClash.Core
{
    /// <summary>
    /// Central, allocation free event hub. Every system talks through this class instead of
    /// holding hard references to each other. All events are reset on domain load so that
    /// "Enter Play Mode Options" (fast play mode, no domain reload) cannot leak stale handlers.
    /// </summary>
    public static class GameEvents
    {
        public static event Action<GameState> StateChanged;
        public static event Action<int> CrowdChanged;
        public static event Action<MathType, int, int, int, Vector3> GatePassed;
        public static event Action<int, bool> LevelStarted;
        public static event Action<bool, int> LevelFinished;
        public static event Action<int> GoldChanged;
        public static event Action<UpgradeType, int> UpgradePurchased;
        public static event Action<int, Vector3> NodeConquered;
        public static event Action<int> SiegeStarted;
        public static event Action<int, Vector3> UnitsLost;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        public static void ResetAll()
        {
            StateChanged = null;
            CrowdChanged = null;
            GatePassed = null;
            LevelStarted = null;
            LevelFinished = null;
            GoldChanged = null;
            UpgradePurchased = null;
            NodeConquered = null;
            SiegeStarted = null;
            UnitsLost = null;
        }

        public static void RaiseStateChanged(GameState state)
        {
            if (StateChanged != null) StateChanged(state);
        }

        public static void RaiseCrowdChanged(int count)
        {
            if (CrowdChanged != null) CrowdChanged(count);
        }

        public static void RaiseGatePassed(MathType type, int value, int before, int after, Vector3 position)
        {
            if (GatePassed != null) GatePassed(type, value, before, after, position);
        }

        public static void RaiseLevelStarted(int levelIndex, bool isFeeder)
        {
            if (LevelStarted != null) LevelStarted(levelIndex, isFeeder);
        }

        public static void RaiseLevelFinished(bool win, int goldAwarded)
        {
            if (LevelFinished != null) LevelFinished(win, goldAwarded);
        }

        public static void RaiseGoldChanged(int gold)
        {
            if (GoldChanged != null) GoldChanged(gold);
        }

        public static void RaiseUpgradePurchased(UpgradeType type, int newLevel)
        {
            if (UpgradePurchased != null) UpgradePurchased(type, newLevel);
        }

        public static void RaiseNodeConquered(int power, Vector3 position)
        {
            if (NodeConquered != null) NodeConquered(power, position);
        }

        public static void RaiseSiegeStarted(int floorCount)
        {
            if (SiegeStarted != null) SiegeStarted(floorCount);
        }

        public static void RaiseUnitsLost(int amount, Vector3 position)
        {
            if (UnitsLost != null) UnitsLost(amount, position);
        }
    }
}
