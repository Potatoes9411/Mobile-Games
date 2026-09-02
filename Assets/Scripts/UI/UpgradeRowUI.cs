using MobClash.Core;
using MobClash.Juice;
using MobClash.Meta;
using UnityEngine;
using UnityEngine.UI;

namespace MobClash.UI
{
    /// <summary>
    /// One row of the meta upgrade screen. Binds an <see cref="UpgradeType"/> to a title, the current
    /// value, the next cost and a buy button, and keeps itself in sync with the economy.
    /// </summary>
    public class UpgradeRowUI : MonoBehaviour
    {
        public UpgradeType upgradeType = UpgradeType.StartingCrowd;

        public Text titleLabel;
        public Text valueLabel;
        public Text levelLabel;
        public Button buyButton;
        public Text buyLabel;

        private string _title = "Upgrade";

        public void Bind(UpgradeType type, string title)
        {
            upgradeType = type;
            _title = title;

            if (titleLabel != null) titleLabel.text = title;
            if (buyButton != null)
            {
                buyButton.onClick.RemoveAllListeners();
                buyButton.onClick.AddListener(Purchase);
            }

            Refresh();
        }

        private void OnEnable()
        {
            Refresh();
        }

        public void Purchase()
        {
            EconomyManager economy = EconomyManager.Instance;
            if (economy == null) return;

            bool bought = economy.TryPurchase(upgradeType);

            if (JuiceManager.Instance != null)
            {
                if (bought)
                {
                    JuiceManager.Instance.PlayCoin();
                    JuiceManager.Instance.HapticSuccess();
                    JuiceManager.Instance.PunchScale(transform, 0.10f, 0.18f);
                }
                else
                {
                    JuiceManager.Instance.HapticFailure();
                }
            }

            Refresh();
        }

        public void Refresh()
        {
            EconomyManager economy = EconomyManager.Instance;
            if (economy == null) return;

            if (titleLabel != null) titleLabel.text = _title;
            if (valueLabel != null) valueLabel.text = economy.GetUpgradeValueLabel(upgradeType);
            if (levelLabel != null) levelLabel.text = "LV " + (economy.GetUpgradeLevel(upgradeType) + 1);

            bool maxed = economy.IsMaxed(upgradeType);
            bool affordable = economy.CanAfford(upgradeType);

            if (buyLabel != null)
            {
                buyLabel.text = maxed ? "MAX" : economy.GetUpgradeCost(upgradeType).ToString();
            }

            if (buyButton != null)
            {
                buyButton.interactable = !maxed && affordable;

                Image image = buyButton.targetGraphic as Image;
                if (image != null)
                {
                    image.color = maxed
                        ? new Color(0.35f, 0.37f, 0.44f, 1f)
                        : (affordable ? UIFactory.Positive : new Color(0.30f, 0.32f, 0.38f, 1f));
                }
            }
        }
    }
}
