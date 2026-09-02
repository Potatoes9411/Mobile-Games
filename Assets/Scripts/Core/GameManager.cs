using System.Collections;
using MobClash.Ads;
using MobClash.Data;
using MobClash.Juice;
using MobClash.Level;
using MobClash.Meta;
using MobClash.Player;
using MobClash.Siege;
using MobClash.Swarm;
using UnityEngine;

namespace MobClash.Core
{
    /// <summary>
    /// Singleton state machine and level director.
    ///
    /// MainMenu -> Running -> TransitionToSiege -> SiegeMode -> LevelWin | LevelFail -> MainMenu
    ///
    /// It owns level loading, the currency award formula, global game speed and the cross system
    /// event dispatch. Every other system reacts to <see cref="GameEvents"/> instead of polling.
    /// </summary>
    public class GameManager : MonoBehaviour
    {
        public static GameManager Instance { get; private set; }

        [Header("Systems")]
        public LevelGenerator generator;
        public LevelBuilder levelBuilder;
        public SwarmManager swarm;
        public SiegeManager siege;
        public PlayerSwerve player;
        public CameraRig cameraRig;

        [Header("Flow timing")]
        public float transitionTravelTime = 0.95f;
        public float siegeIntroDelay = 0.35f;
        public float resultDelay = 0.6f;

        [Header("Performance")]
        public int targetFrameRate = 60;
        public bool disableVSync = true;

        [Header("Rewards")]
        [Tooltip("Fraction of the earned gold still paid out after a failed siege.")]
        [Range(0f, 1f)] public float consolationRatio = 0.25f;
        public int rewardedAdMultiplier = 3;

        private GameState _state = GameState.Boot;
        private LevelData _currentLevel;
        private int _pendingReward;
        private bool _rewardClaimed;
        private bool _rewardDoubled;
        private Coroutine _flowRoutine;

        public GameState State { get { return _state; } }
        public LevelData CurrentLevel { get { return _currentLevel; } }
        public SwarmManager Swarm { get { return swarm; } }
        public SiegeManager Siege { get { return siege; } }
        public int PendingReward { get { return _pendingReward; } }
        public bool RewardDoubled { get { return _rewardDoubled; } }

        public int LevelIndex
        {
            get { return EconomyManager.Instance != null ? EconomyManager.Instance.LevelIndex : 1; }
        }

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }

            Instance = this;

            Application.targetFrameRate = targetFrameRate;
            if (disableVSync) QualitySettings.vSyncCount = 0;
            Screen.sleepTimeout = SleepTimeout.NeverSleep;

            GameLayers.ApplyRuntimeCollisionMatrix();
        }

        private void OnDestroy()
        {
            if (Instance == this) Instance = null;
        }

        private void Start()
        {
            ResolveMissingReferences();

            if (EconomyManager.Instance != null) EconomyManager.Instance.EnsureLoaded();

            PrepareLevel();
            SetState(GameState.MainMenu);
        }

        private void ResolveMissingReferences()
        {
            if (generator == null) generator = LevelGenerator.CreateDefault();
            if (levelBuilder == null) levelBuilder = FindFirst<LevelBuilder>();
            if (swarm == null) swarm = FindFirst<SwarmManager>();
            if (siege == null) siege = FindFirst<SiegeManager>();
            if (player == null) player = FindFirst<PlayerSwerve>();
            if (cameraRig == null) cameraRig = FindFirst<CameraRig>();

            if (siege != null && siege.swarm == null) siege.swarm = swarm;
            if (cameraRig != null && player != null) cameraRig.SetTarget(player.transform);
            if (swarm != null && player != null && swarm.pivot == null) swarm.pivot = player.transform;
        }

        private static T FindFirst<T>() where T : Object
        {
#if UNITY_2023_1_OR_NEWER
            return Object.FindFirstObjectByType<T>();
#else
            return Object.FindObjectOfType<T>();
#endif
        }

        // ------------------------------------------------------------------ state machine

        private void SetState(GameState state)
        {
            if (_state == state) return;

            _state = state;
            GameEvents.RaiseStateChanged(state);
        }

        /// <summary>Generates and builds the level the player is currently on.</summary>
        public void PrepareLevel()
        {
            StopFlow();

            int levelIndex = LevelIndex;
            int startingCrowd = EconomyManager.Instance != null
                ? EconomyManager.Instance.StartingCrowd
                : 20;
            float bias = EconomyManager.Instance != null
                ? EconomyManager.Instance.DifficultyBias
                : 1f;

            if (generator == null) generator = LevelGenerator.CreateDefault();

            LevelData previous = _currentLevel;
            _currentLevel = generator.Generate(levelIndex, startingCrowd, bias);

            if (previous != null && previous != _currentLevel) Destroy(previous);

            if (siege != null) siege.ClearTower();
            if (levelBuilder != null) levelBuilder.Build(_currentLevel);

            if (player != null)
            {
                player.ResetForLevel(_currentLevel);
                player.SetControlEnabled(false);
            }

            if (swarm != null)
            {
                swarm.ResetSwarm(_currentLevel.startingCrowd);
                if (player != null) player.SetCrowdRadius(swarm.CrowdRadius);
            }

            if (cameraRig != null)
            {
                cameraRig.EnterRunView();
                cameraRig.SetCrowdRadius(swarm != null ? swarm.CrowdRadius : 1.5f);
                cameraRig.Snap();
            }

            _pendingReward = 0;
            _rewardClaimed = false;
            _rewardDoubled = false;

            GameEvents.RaiseLevelStarted(levelIndex, _currentLevel.isFeeder);
        }

        /// <summary>Called by the tap-to-start UI.</summary>
        public void StartRun()
        {
            if (_state != GameState.MainMenu && _state != GameState.LevelWin && _state != GameState.LevelFail)
            {
                return;
            }

            if (_currentLevel == null) PrepareLevel();

            SetState(GameState.Running);

            if (player != null) player.SetControlEnabled(true);
            if (EconomyManager.Instance != null) EconomyManager.Instance.RegisterRunStarted();
        }

        public void RetryLevel()
        {
            PrepareLevel();
            SetState(GameState.MainMenu);
        }

        public void NextLevel()
        {
            PrepareLevel();
            SetState(GameState.MainMenu);
        }

        // ------------------------------------------------------------------ run flow

        private void Update()
        {
            if (_state != GameState.Running) return;

            if (swarm != null)
            {
                if (player != null) player.SetCrowdRadius(swarm.CrowdRadius);
                if (cameraRig != null) cameraRig.SetCrowdRadius(swarm.CrowdRadius);

                if (swarm.Count <= 0)
                {
                    OnCrowdWipedOut();
                }
            }
        }

        private void OnCrowdWipedOut()
        {
            SetState(GameState.TransitionToSiege);
            StopFlow();
            _flowRoutine = StartCoroutine(FailRoutine());
        }

        /// <summary>Invoked by the finish line trigger.</summary>
        public void OnFinishLineReached()
        {
            if (_state != GameState.Running) return;

            SetState(GameState.TransitionToSiege);
            StopFlow();
            _flowRoutine = StartCoroutine(TransitionRoutine());
        }

        private IEnumerator TransitionRoutine()
        {
            if (player != null) player.SetControlEnabled(false);

            if (siege != null) siege.BuildTower(_currentLevel);

            if (cameraRig != null && siege != null)
            {
                cameraRig.EnterSiegeView(siege.TowerBase, siege.TowerHeight);
            }

            Vector3 start = player != null ? player.transform.position : Vector3.zero;
            Vector3 end = new Vector3(0f, 0f, _currentLevel.TowerDistance - 8.5f);

            float elapsed = 0f;
            while (elapsed < transitionTravelTime)
            {
                elapsed += Time.deltaTime;
                float t = Mathf.SmoothStep(0f, 1f, Mathf.Clamp01(elapsed / transitionTravelTime));

                if (player != null) player.transform.position = Vector3.Lerp(start, end, t);
                yield return null;
            }

            if (player != null)
            {
                player.transform.position = end;
                player.transform.rotation = Quaternion.identity;
            }

            yield return new WaitForSeconds(siegeIntroDelay);

            SetState(GameState.SiegeMode);
            if (siege != null) siege.BeginSiege();

            _flowRoutine = null;
        }

        /// <summary>Invoked by the SiegeManager when the tower is cleared or the player stalls out.</summary>
        public void OnSiegeResolved(bool win)
        {
            if (_state != GameState.SiegeMode && _state != GameState.TransitionToSiege) return;

            StopFlow();
            _flowRoutine = StartCoroutine(ResolveRoutine(win));
        }

        private IEnumerator ResolveRoutine(bool win)
        {
            if (siege != null) siege.StopSiege();

            int survivors = swarm != null ? swarm.Count : 0;
            _pendingReward = CalculateReward(win, survivors);
            _rewardClaimed = false;
            _rewardDoubled = false;

            if (JuiceManager.Instance != null)
            {
                if (win)
                {
                    JuiceManager.Instance.PlayVictory();
                    JuiceManager.Instance.HapticSuccess();
                    JuiceManager.Instance.Shake(0.35f, 0.35f);
                }
                else
                {
                    JuiceManager.Instance.PlayDefeat();
                    JuiceManager.Instance.HapticFailure();
                }
            }

            if (EconomyManager.Instance != null)
            {
                EconomyManager.Instance.RegisterRunFinished(win, survivors);
            }

            yield return new WaitForSeconds(resultDelay);

            SetState(win ? GameState.LevelWin : GameState.LevelFail);
            GameEvents.RaiseLevelFinished(win, _pendingReward);

            if (AdManager.Instance != null && win) AdManager.Instance.NotifyLevelCompleted();

            _flowRoutine = null;
        }

        private IEnumerator FailRoutine()
        {
            yield return new WaitForSeconds(0.5f);
            OnSiegeResolved(false);
        }

        // ------------------------------------------------------------------ rewards

        private int CalculateReward(bool win, int survivors)
        {
            if (_currentLevel == null) return 0;

            int clearedRooms = siege != null ? siege.ClearedNodes() : 0;
            int raw = survivors * Mathf.Max(1, _currentLevel.goldPerUnit)
                      + clearedRooms * _currentLevel.floorClearBonus;

            float multiplier = _currentLevel.goldMultiplier;
            if (EconomyManager.Instance != null) multiplier *= EconomyManager.Instance.GoldMultiplier;

            float total = raw * multiplier;
            if (!win) total *= consolationRatio;

            return Mathf.Max(win ? 1 : 0, Mathf.RoundToInt(total));
        }

        /// <summary>Banks the pending reward. Safe to call twice; the second call is ignored.</summary>
        public void ClaimReward()
        {
            if (_rewardClaimed) return;
            _rewardClaimed = true;

            if (EconomyManager.Instance != null && _pendingReward > 0)
            {
                EconomyManager.Instance.AddGold(_pendingReward);
            }

            if (JuiceManager.Instance != null) JuiceManager.Instance.PlayCoin();
        }

        /// <summary>Rewarded video path: multiplies the pending reward, then banks it.</summary>
        public void ClaimRewardWithAd()
        {
            if (_rewardClaimed || _rewardDoubled) return;

            if (AdManager.Instance == null)
            {
                _rewardDoubled = true;
                _pendingReward *= rewardedAdMultiplier;
                ClaimReward();
                return;
            }

            AdManager.Instance.ShowRewarded(AdPlacement.RewardedTripleGold, delegate(bool rewarded)
            {
                if (rewarded)
                {
                    _rewardDoubled = true;
                    _pendingReward *= rewardedAdMultiplier;
                }

                ClaimReward();
            });
        }

        /// <summary>Advances the meta loop: bank gold, optionally show an interstitial, load the next level.</summary>
        public void ContinueAfterResult()
        {
            bool won = _state == GameState.LevelWin;
            ClaimReward();

            if (AdManager.Instance != null && won)
            {
                int levelJustFinished = LevelIndex - 1;
                AdManager.Instance.ShowInterstitial(levelJustFinished, NextLevel);
                return;
            }

            if (won) NextLevel();
            else RetryLevel();
        }

        // ------------------------------------------------------------------ misc

        public void SetGameSpeed(float scale)
        {
            Time.timeScale = Mathf.Clamp(scale, 0.05f, 4f);
        }

        private void StopFlow()
        {
            if (_flowRoutine != null)
            {
                StopCoroutine(_flowRoutine);
                _flowRoutine = null;
            }
        }
    }
}
