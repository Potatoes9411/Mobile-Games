using UnityEngine;

namespace MobClash.Core
{
    /// <summary>
    /// The game's committed art direction: golden hour siege.
    /// Warm amber sky, deep teal fields, sandstone road, cobalt mob against crimson defenders.
    /// These are the same values the browser build uses, so both implementations look identical.
    /// Change a colour here and every generated material follows.
    /// </summary>
    public static class Palette
    {
        private static Color Hex(int rgb)
        {
            return new Color(
                ((rgb >> 16) & 0xFF) / 255f,
                ((rgb >> 8) & 0xFF) / 255f,
                (rgb & 0xFF) / 255f,
                1f);
        }

        // sky and atmosphere
        public static readonly Color SkyTop = Hex(0x1B1436);
        public static readonly Color SkyMid = Hex(0x7A3C68);
        public static readonly Color SkyLow = Hex(0xF4894B);
        public static readonly Color Sun = Hex(0xFFDE96);
        public static readonly Color Fog = Hex(0xECA074);

        // terrain
        public static readonly Color Grass = Hex(0x2E7458);
        public static readonly Color GrassDark = Hex(0x1E5642);
        public static readonly Color Road = Hex(0xE2C7A3);
        public static readonly Color RoadDark = Hex(0xC6A984);
        public static readonly Color Lane = Hex(0xF7E8CE);
        public static readonly Color Wall = Hex(0x7E6058);
        public static readonly Color WallTop = Hex(0xB08C7A);

        // teams
        public static readonly Color Blue = Hex(0x3D8BFF);
        public static readonly Color BlueLight = Hex(0xB0DAFF);
        public static readonly Color BlueDark = Hex(0x1A4896);
        public static readonly Color Red = Hex(0xF04D5A);
        public static readonly Color RedLight = Hex(0xFFB2AC);
        public static readonly Color RedDark = Hex(0x821A26);

        // masonry
        public static readonly Color Stone = Hex(0xC6B6A2);
        public static readonly Color StoneDark = Hex(0xA89684);
        public static readonly Color StoneDeep = Hex(0x706258);

        // accents
        public static readonly Color Gold = Hex(0xFFC24B);
        public static readonly Color GoldDark = Hex(0xB87116);
        public static readonly Color Jade = Hex(0x3FD98A);
        public static readonly Color JadeDark = Hex(0x1E8C55);
        public static readonly Color Ink = Hex(0x241A38);
        public static readonly Color Tree = Hex(0x26684E);
        public static readonly Color TreeDark = Hex(0x184A38);
        public static readonly Color Trunk = Hex(0x543A2E);

        /// <summary>Positive gates read green, punishing gates read red. Genre convention, do not invert.</summary>
        public static Color GateTint(bool positive)
        {
            return positive ? Jade : Red;
        }
    }
}
