using UnityEngine;

namespace MobClash.Core
{
    /// <summary>
    /// Single source of truth for gate arithmetic. Used both by the runtime swarm and by the
    /// offline level generator, so the pacing simulation can never drift from actual gameplay.
    /// </summary>
    public static class GateMath
    {
        /// <summary>Hard ceiling that keeps the crowd inside int range and inside sane draw budgets.</summary>
        public const int MaxCrowd = 99999;

        public static int Apply(int crowd, MathType type, int value)
        {
            int result;
            switch (type)
            {
                case MathType.Add:
                    result = crowd + value;
                    break;
                case MathType.Subtract:
                    result = crowd - value;
                    break;
                case MathType.Multiply:
                    result = value <= 0 ? crowd : crowd * value;
                    break;
                case MathType.Divide:
                    result = value <= 1 ? crowd : Mathf.RoundToInt(crowd / (float)value);
                    break;
                default:
                    result = crowd;
                    break;
            }

            return Mathf.Clamp(result, 0, MaxCrowd);
        }

        /// <summary>Human readable gate label, e.g. "+25", "-10", "x3", "/2".</summary>
        public static string Format(MathType type, int value)
        {
            switch (type)
            {
                case MathType.Add: return "+" + value;
                case MathType.Subtract: return "-" + value;
                case MathType.Multiply: return "x" + value;
                case MathType.Divide: return "/" + value;
                default: return value.ToString();
            }
        }

        /// <summary>True when the operation can only help the player.</summary>
        public static bool IsPositive(MathType type, int value)
        {
            switch (type)
            {
                case MathType.Add: return value > 0;
                case MathType.Multiply: return value > 1;
                case MathType.Subtract: return value <= 0;
                case MathType.Divide: return value <= 1;
                default: return false;
            }
        }

        /// <summary>Colour language of the genre: green helps, red hurts.</summary>
        public static Color TintFor(MathType type, int value)
        {
            return IsPositive(type, value)
                ? new Color(0.16f, 0.83f, 0.42f, 1f)
                : new Color(0.90f, 0.22f, 0.26f, 1f);
        }
    }
}
