using MobClash.Core;
using MobClash.Juice;
using MobClash.Meta;
using UnityEngine;
using UnityEngine.UI;

namespace MobClash.UI
{
    /// <summary>
    /// Builds and drives the whole interface: HUD, main menu, upgrade shop, win and fail screens.
    /// The layout is generated in code against a 1080x1920 reference so it scales to every phone,
    /// and every element reacts to <see cref="GameEvents"/> rather than polling the managers.
    /// </summary>
    public class UIManager : MonoBehaviour
    {
        [Header("Build")]
        [Tooltip("Generate the interface at runtime. Disable when using authored prefabs instead.")]
        public bool autoBuild = true;

        [Header("Panels (auto filled when autoBuild is on)")]
        public GameObject hudPanel;
        public GameObject mainMenuPanel;
        public GameObject upgradePanel;
        public GameObject winPanel;
        public GameObject failPanel;

        [Header("Labels")]
        public Text levelLabel;
        public Text goldLabel;
        public Text crowdLabel;
        public Text hintLabel;
        public Text feederBanner;
        public Text winRewardLabel;
        public Text failRewardLabel;
        public Text menuGoldLabel;
        public Text upgradeGoldLabel;

        [Header("Buttons")]
        public Button tapToStartButton;
        public Button openUpgradesButton;
        public Button closeUpgradesButton;
        public Button continueButton;
        public Button rewardedButton;
        public Button retryButton;

        [Header("Rows")]
        public UpgradeRowUI[] upgradeRows = new UpgradeRowUI[0];

        private Canvas _canvas;
        private float _bannerTimer;

        private void Awake()
        {
            UIFactory.EnsureEventSystem();
            if (autoBuild) BuildInterface();
        }

        private void OnEnable()
        {
            GameEvents.StateChanged += HandleStateChanged;
            GameEvents.CrowdChanged += HandleCrowdChanged;
            GameEvents.GoldChanged += HandleGoldChanged;
            GameEvents.LevelStarted += HandleLevelStarted;
            GameEvents.LevelFinished += HandleLevelFinished;
            GameEvents.UpgradePurchased += HandleUpgradePurchased;
        }

        private void OnDisable()
        {
            GameEvents.StateChanged -= HandleStateChanged;
            GameEvents.CrowdChanged -= HandleCrowdChanged;
            GameEvents.GoldChanged -= HandleGoldChanged;
            GameEvents.LevelStarted -= HandleLevelStarted;
            GameEvents.LevelFinished -= HandleLevelFinished;
            GameEvents.UpgradePurchased -= HandleUpgradePurchased;
        }

        private void Start()
        {
            RefreshGold();
            RefreshUpgradeRows();
            ShowOnly(GameState.MainMenu);
        }

        // ------------------------------------------------------------------ construction

        private void BuildInterface()
        {
            _canvas = UIFactory.CreateCanvas("GameCanvas", 10, transform);
            Transform root = _canvas.transform;

            BuildHud(root);
            BuildMainMenu(root);
            BuildUpgradePanel(root);
            BuildWinPanel(root);
            BuildFailPanel(root);
        }

        private void BuildHud(Transform root)
        {
            hudPanel = new GameObject("HUD");
            hudPanel.transform.SetParent(root, false);
            RectTransform rect = hudPanel.AddComponent<RectTransform>();
            UIFactory.Stretch(rect, 0f, 0f, 0f, 0f);

            levelLabel = UIFactory.Label(hudPanel.transform, "LevelLabel", "LEVEL 1", 46,
                TextAnchor.MiddleLeft, UIFactory.Ink,
                new Vector2(0f, 1f), new Vector2(0f, 1f), new Vector2(220f, -90f), new Vector2(380f, 70f));

            goldLabel = UIFactory.Label(hudPanel.transform, "GoldLabel", "0", 46,
                TextAnchor.MiddleRight, UIFactory.Warning,
                new Vector2(1f, 1f), new Vector2(1f, 1f), new Vector2(-220f, -90f), new Vector2(380f, 70f));

            crowdLabel = UIFactory.Label(hudPanel.transform, "CrowdLabel", "0", 120,
                TextAnchor.MiddleCenter, UIFactory.Ink,
                new Vector2(0.5f, 1f), new Vector2(0.5f, 1f), new Vector2(0f, -240f), new Vector2(700f, 160f));

            feederBanner = UIFactory.Label(hudPanel.transform, "FeederBanner", "GOLD RUSH  x2", 56,
                TextAnchor.MiddleCenter, UIFactory.Warning,
                new Vector2(0.5f, 1f), new Vector2(0.5f, 1f), new Vector2(0f, -370f), new Vector2(900f, 90f));
            feederBanner.gameObject.SetActive(false);

            hintLabel = UIFactory.Label(hudPanel.transform, "HintLabel", string.Empty, 42,
                TextAnchor.MiddleCenter, new Color(0.85f, 0.9f, 1f, 0.9f),
                new Vector2(0.5f, 0f), new Vector2(0.5f, 0f), new Vector2(0f, 300f), new Vector2(980f, 90f));
        }

        private void BuildMainMenu(Transform root)
        {
            mainMenuPanel = UIFactory.Panel(root, "MainMenu", new Color(0.03f, 0.05f, 0.10f, 0.55f));

            tapToStartButton = UIFactory.FullScreenButton(mainMenuPanel.transform, "TapToStart");
            tapToStartButton.onClick.AddListener(OnTapToStart);

            UIFactory.Label(mainMenuPanel.transform, "Title", "MOB CLASH", 96,
                TextAnchor.MiddleCenter, UIFactory.Ink,
                new Vector2(0.5f, 1f), new Vector2(0.5f, 1f), new Vector2(0f, -430f), new Vector2(1000f, 130f));

            UIFactory.Label(mainMenuPanel.transform, "Subtitle", "GATE SIEGE", 64,
                TextAnchor.MiddleCenter, UIFactory.Accent,
                new Vector2(0.5f, 1f), new Vector2(0.5f, 1f), new Vector2(0f, -540f), new Vector2(1000f, 100f));

            UIFactory.Label(mainMenuPanel.transform, "TapHint", "TAP TO START", 58,
                TextAnchor.MiddleCenter, UIFactory.Ink,
                new Vector2(0.5f, 0.5f), new Vector2(0.5f, 0.5f), new Vector2(0f, -120f), new Vector2(900f, 100f));

            menuGoldLabel = UIFactory.Label(mainMenuPanel.transform, "MenuGold", "0", 52,
                TextAnchor.MiddleCenter, UIFactory.Warning,
                new Vector2(0.5f, 0f), new Vector2(0.5f, 0f), new Vector2(0f, 470f), new Vector2(600f, 80f));

            openUpgradesButton = UIFactory.TextButton(mainMenuPanel.transform, "OpenUpgrades", "UPGRADES",
                UIFactory.Accent,
                new Vector2(0.5f, 0f), new Vector2(0.5f, 0f), new Vector2(0f, 320f), new Vector2(560f, 140f), 52);
            openUpgradesButton.onClick.AddListener(OpenUpgrades);
        }

        private void BuildUpgradePanel(Transform root)
        {
            upgradePanel = UIFactory.Panel(root, "Upgrades", new Color(0.03f, 0.04f, 0.09f, 0.97f));

            UIFactory.Label(upgradePanel.transform, "Title", "UPGRADES", 78,
                TextAnchor.MiddleCenter, UIFactory.Ink,
                new Vector2(0.5f, 1f), new Vector2(0.5f, 1f), new Vector2(0f, -180f), new Vector2(900f, 110f));

            upgradeGoldLabel = UIFactory.Label(upgradePanel.transform, "Gold", "0", 56,
                TextAnchor.MiddleCenter, UIFactory.Warning,
                new Vector2(0.5f, 1f), new Vector2(0.5f, 1f), new Vector2(0f, -290f), new Vector2(700f, 90f));

            UpgradeRowUI[] rows = new UpgradeRowUI[3];
            rows[0] = BuildUpgradeRow(upgradePanel.transform, UpgradeType.StartingCrowd, "STARTING MOB", 0);
            rows[1] = BuildUpgradeRow(upgradePanel.transform, UpgradeType.GoldMultiplier, "GOLD MULTIPLIER", 1);
            rows[2] = BuildUpgradeRow(upgradePanel.transform, UpgradeType.GateBonus, "GATE BONUS", 2);
            upgradeRows = rows;

            closeUpgradesButton = UIFactory.TextButton(upgradePanel.transform, "Close", "BACK",
                new Color(0.30f, 0.33f, 0.42f, 1f),
                new Vector2(0.5f, 0f), new Vector2(0.5f, 0f), new Vector2(0f, 220f), new Vector2(520f, 130f), 50);
            closeUpgradesButton.onClick.AddListener(CloseUpgrades);

            upgradePanel.SetActive(false);
        }

        private UpgradeRowUI BuildUpgradeRow(Transform parent, UpgradeType type, string title, int index)
        {
            float y = -460f - index * 270f;

            Image container = UIFactory.Box(parent, "Row_" + type, new Color(0.10f, 0.12f, 0.20f, 1f),
                new Vector2(0.5f, 1f), new Vector2(0.5f, 1f), new Vector2(0f, y), new Vector2(940f, 230f));

            UpgradeRowUI row = container.gameObject.AddComponent<UpgradeRowUI>();

            row.titleLabel = UIFactory.Label(container.transform, "Title", title, 46,
                TextAnchor.MiddleLeft, UIFactory.Ink,
                new Vector2(0f, 1f), new Vector2(0f, 1f), new Vector2(280f, -60f), new Vector2(520f, 60f));

            row.valueLabel = UIFactory.Label(container.transform, "Value", "0", 52,
                TextAnchor.MiddleLeft, UIFactory.Accent,
                new Vector2(0f, 0f), new Vector2(0f, 0f), new Vector2(300f, 70f), new Vector2(560f, 70f));

            row.levelLabel = UIFactory.Label(container.transform, "Level", "LV 1", 38,
                TextAnchor.MiddleLeft, new Color(0.72f, 0.76f, 0.86f, 1f),
                new Vector2(0f, 0f), new Vector2(0f, 0f), new Vector2(120f, 70f), new Vector2(200f, 60f));

            Button buy = UIFactory.TextButton(container.transform, "Buy", "0", UIFactory.Positive,
                new Vector2(1f, 0.5f), new Vector2(1f, 0.5f), new Vector2(-170f, 0f), new Vector2(280f, 130f), 46);
            row.buyButton = buy;
            row.buyLabel = buy.GetComponentInChildren<Text>();

            row.Bind(type, title);
            return row;
        }

        private void BuildWinPanel(Transform root)
        {
            winPanel = UIFactory.Panel(root, "WinPanel", new Color(0.03f, 0.10f, 0.06f, 0.92f));

            UIFactory.Label(winPanel.transform, "Title", "TOWER CLEARED", 84,
                TextAnchor.MiddleCenter, UIFactory.Positive,
                new Vector2(0.5f, 1f), new Vector2(0.5f, 1f), new Vector2(0f, -420f), new Vector2(1000f, 120f));

            winRewardLabel = UIFactory.Label(winPanel.transform, "Reward", "+0", 96,
                TextAnchor.MiddleCenter, UIFactory.Warning,
                new Vector2(0.5f, 0.5f), new Vector2(0.5f, 0.5f), new Vector2(0f, 120f), new Vector2(900f, 140f));

            rewardedButton = UIFactory.TextButton(winPanel.transform, "Rewarded", "WATCH AD  x3 GOLD",
                UIFactory.Warning,
                new Vector2(0.5f, 0f), new Vector2(0.5f, 0f), new Vector2(0f, 560f), new Vector2(820f, 160f), 46);
            rewardedButton.onClick.AddListener(OnRewardedPressed);

            continueButton = UIFactory.TextButton(winPanel.transform, "Continue", "CONTINUE",
                UIFactory.Positive,
                new Vector2(0.5f, 0f), new Vector2(0.5f, 0f), new Vector2(0f, 360f), new Vector2(820f, 160f), 52);
            continueButton.onClick.AddListener(OnContinuePressed);

            winPanel.SetActive(false);
        }

        private void BuildFailPanel(Transform root)
        {
            failPanel = UIFactory.Panel(root, "FailPanel", new Color(0.11f, 0.03f, 0.05f, 0.92f));

            UIFactory.Label(failPanel.transform, "Title", "SIEGE FAILED", 84,
                TextAnchor.MiddleCenter, UIFactory.Danger,
                new Vector2(0.5f, 1f), new Vector2(0.5f, 1f), new Vector2(0f, -420f), new Vector2(1000f, 120f));

            failRewardLabel = UIFactory.Label(failPanel.transform, "Reward", "+0", 72,
                TextAnchor.MiddleCenter, UIFactory.Warning,
                new Vector2(0.5f, 0.5f), new Vector2(0.5f, 0.5f), new Vector2(0f, 120f), new Vector2(900f, 120f));

            retryButton = UIFactory.TextButton(failPanel.transform, "Retry", "TRY AGAIN",
                UIFactory.Accent,
                new Vector2(0.5f, 0f), new Vector2(0.5f, 0f), new Vector2(0f, 380f), new Vector2(820f, 160f), 52);
            retryButton.onClick.AddListener(OnContinuePressed);

            failPanel.SetActive(false);
        }

        // ------------------------------------------------------------------ button handlers

        private void OnTapToStart()
        {
            if (GameManager.Instance == null) return;
            if (upgradePanel != null && upgradePanel.activeSelf) return;

            if (JuiceManager.Instance != null) JuiceManager.Instance.HapticSelection();
            GameManager.Instance.StartRun();
        }

        private void OpenUpgrades()
        {
            if (upgradePanel == null) return;

            RefreshUpgradeRows();
            upgradePanel.SetActive(true);
            if (JuiceManager.Instance != null) JuiceManager.Instance.PlaySelect();
        }

        private void CloseUpgrades()
        {
            if (upgradePanel == null) return;
            upgradePanel.SetActive(false);
        }

        private void OnContinuePressed()
        {
            if (GameManager.Instance == null) return;

            if (JuiceManager.Instance != null) JuiceManager.Instance.HapticSelection();
            GameManager.Instance.ContinueAfterResult();
        }

        private void OnRewardedPressed()
        {
            if (GameManager.Instance == null) return;

            if (rewardedButton != null) rewardedButton.interactable = false;
            GameManager.Instance.ClaimRewardWithAd();

            if (winRewardLabel != null)
            {
                winRewardLabel.text = "+" + GameManager.Instance.PendingReward;
            }
        }

        // ------------------------------------------------------------------ event handlers

        private void HandleStateChanged(GameState state)
        {
            ShowOnly(state);
        }

        private void HandleCrowdChanged(int count)
        {
            if (crowdLabel != null) crowdLabel.text = count.ToString();
        }

        private void HandleGoldChanged(int gold)
        {
            RefreshGold();
            RefreshUpgradeRows();
        }

        private void HandleLevelStarted(int levelIndex, bool isFeeder)
        {
            if (levelLabel != null) levelLabel.text = "LEVEL " + levelIndex;

            if (feederBanner != null)
            {
                feederBanner.gameObject.SetActive(isFeeder);
                _bannerTimer = isFeeder ? 3f : 0f;
            }

            RefreshGold();
        }

        private void HandleLevelFinished(bool win, int reward)
        {
            if (win && winRewardLabel != null) winRewardLabel.text = "+" + reward;
            if (!win && failRewardLabel != null) failRewardLabel.text = "+" + reward;

            if (rewardedButton != null) rewardedButton.interactable = win && reward > 0;
        }

        private void HandleUpgradePurchased(UpgradeType type, int newLevel)
        {
            RefreshUpgradeRows();
            RefreshGold();
        }

        // ------------------------------------------------------------------ helpers

        private void ShowOnly(GameState state)
        {
            bool menu = state == GameState.MainMenu;
            bool win = state == GameState.LevelWin;
            bool fail = state == GameState.LevelFail;

            if (mainMenuPanel != null) mainMenuPanel.SetActive(menu);
            if (winPanel != null) winPanel.SetActive(win);
            if (failPanel != null) failPanel.SetActive(fail);
            if (hudPanel != null) hudPanel.SetActive(true);
            if (upgradePanel != null && !menu) upgradePanel.SetActive(false);

            if (hintLabel != null)
            {
                switch (state)
                {
                    case GameState.Running:
                        hintLabel.text = "DRAG TO STEER";
                        break;
                    case GameState.SiegeMode:
                        hintLabel.text = "TAP A ROOM YOU OUTNUMBER";
                        break;
                    default:
                        hintLabel.text = string.Empty;
                        break;
                }
            }

            if (crowdLabel != null) crowdLabel.gameObject.SetActive(!menu);
        }

        private void RefreshGold()
        {
            EconomyManager economy = EconomyManager.Instance;
            if (economy == null) return;

            string gold = economy.Gold.ToString();
            if (goldLabel != null) goldLabel.text = gold;
            if (menuGoldLabel != null) menuGoldLabel.text = gold;
            if (upgradeGoldLabel != null) upgradeGoldLabel.text = gold;
        }

        private void RefreshUpgradeRows()
        {
            if (upgradeRows == null) return;

            for (int i = 0; i < upgradeRows.Length; i++)
            {
                if (upgradeRows[i] != null) upgradeRows[i].Refresh();
            }
        }

        private void Update()
        {
            if (_bannerTimer > 0f)
            {
                _bannerTimer -= Time.deltaTime;

                if (feederBanner != null)
                {
                    float pulse = 1f + Mathf.Sin(Time.time * 8f) * 0.06f;
                    feederBanner.transform.localScale = new Vector3(pulse, pulse, 1f);
                }

                if (_bannerTimer <= 0f && feederBanner != null)
                {
                    feederBanner.transform.localScale = Vector3.one;
                }
            }
        }
    }
}
