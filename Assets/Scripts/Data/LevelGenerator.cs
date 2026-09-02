using System.Collections.Generic;
using MobClash.Core;
using UnityEngine;

namespace MobClash.Data
{
    /// <summary>
    /// Deterministic, retention tuned level factory.
    ///
    /// Pacing contract:
    ///   Levels 1-3   The Hook       only positive gates, huge multipliers, no hazards, 0.35 tower ratio.
    ///   Levels 4-10  Mastery        every row is a "+N now" versus "xM behind hazards" decision.
    ///   Levels 11+   Core Loop      procedural rows, trap pairs, hazard density and tower ratio ramp.
    ///   Every 4th    Gold Rush      5 multiplier rows, no hazards, 2x gold, easy tower (feeder / breather).
    ///
    /// Difficulty is expressed relative to a simulated reference run, so a player who bought crowd
    /// upgrades still meets a tower sized for their actual power (see <see cref="Generate"/>).
    /// </summary>
    [CreateAssetMenu(fileName = "LevelGenerator", menuName = "Mob Clash/Level Generator", order = 1)]
    public class LevelGenerator : ScriptableObject
    {
        [Header("Authored overrides")]
        [Tooltip("Optional hand made levels. A matching levelIndex wins over procedural generation.")]
        public LevelData[] authoredLevels = new LevelData[0];

        [Header("Rhythm")]
        public int tutorialLevels = 3;
        public int masteryLevels = 10;
        public int feederInterval = 4;
        public int randomSeed = 90211;

        [Header("Track scaling")]
        public float baseTrackLength = 110f;
        public float trackLengthPerLevel = 6f;
        public float maxTrackLength = 300f;
        public float trackHalfWidth = 6f;
        public float baseRunSpeed = 12f;
        public float runSpeedPerLevel = 0.11f;
        public float maxRunSpeed = 18f;

        [Header("Gate rows")]
        public int tutorialGateRows = 3;
        public int masteryGateRows = 4;
        public int feederGateRows = 4;
        public int maxGateRows = 6;
        public float gateLane = 0.55f;
        public float firstRowDistance = 26f;
        public float finalRowPadding = 20f;

        [Header("Gate values")]
        [Tooltip("Safe additive gate value as a fraction of the simulated crowd at that point.")]
        public float safeAddFraction = 0.65f;
        public float trapSubtractFraction = 0.35f;

        [Header("Hazards")]
        public int maxScatteredObstacles = 14;
        public float obstacleKillRadius = 1.15f;
        [Tooltip("Crowd fraction shaved per hazard for an imperfect player.")]
        public float attritionPerObstacle = 0.10f;

        [Header("Tower difficulty")]
        public int maxFloors = 6;
        public float tutorialDifficulty = 0.35f;
        public float feederDifficulty = 0.30f;
        public float coreDifficultyStart = 0.55f;
        public float coreDifficultyEnd = 0.95f;
        public int difficultyPlateauLevel = 40;
        public float floorWeightGrowth = 0.75f;
        public float maxCasualtyRatio = 0.25f;

        [Header("Economy")]
        public int goldPerUnit = 1;
        public float feederGoldMultiplier = 2f;

        private System.Random _rng;

        /// <summary>Creates a generator instance with default tuning when no asset was assigned.</summary>
        public static LevelGenerator CreateDefault()
        {
            LevelGenerator generator = CreateInstance<LevelGenerator>();
            generator.name = "RuntimeLevelGenerator";
            return generator;
        }

        public bool IsFeeder(int levelIndex)
        {
            return levelIndex > tutorialLevels && feederInterval > 0 && levelIndex % feederInterval == 0;
        }

        public bool IsTutorial(int levelIndex)
        {
            return levelIndex <= tutorialLevels;
        }

        /// <summary>
        /// Builds the level. <paramref name="startingCrowd"/> comes from the meta upgrades and
        /// <paramref name="ddsBias"/> is the dynamic difficulty multiplier (below 1 after repeated fails).
        /// </summary>
        public LevelData Generate(int levelIndex, int startingCrowd, float ddsBias)
        {
            levelIndex = Mathf.Max(1, levelIndex);
            startingCrowd = Mathf.Clamp(startingCrowd, 5, 500);
            ddsBias = Mathf.Clamp(ddsBias, 0.55f, 1.25f);

            LevelData authored = FindAuthored(levelIndex);
            if (authored != null)
            {
                LevelData clone = Instantiate(authored);
                clone.name = "Level_" + levelIndex + "_Authored";
                clone.startingCrowd = startingCrowd;
                return clone;
            }

            _rng = new System.Random(randomSeed + levelIndex * 7919);

            LevelData data = CreateInstance<LevelData>();
            data.name = "Level_" + levelIndex;
            data.levelIndex = levelIndex;
            data.isTutorial = IsTutorial(levelIndex);
            data.isFeeder = IsFeeder(levelIndex);
            data.startingCrowd = startingCrowd;
            data.trackHalfWidth = trackHalfWidth;

            float rawLength = baseTrackLength + trackLengthPerLevel * (levelIndex - 1);
            data.trackLength = Mathf.Min(maxTrackLength, rawLength) * (data.isFeeder ? 0.92f : 1f);
            data.runSpeed = Mathf.Min(maxRunSpeed, baseRunSpeed + runSpeedPerLevel * (levelIndex - 1));

            List<GateSpec> gates = new List<GateSpec>(16);
            List<ObstacleSpec> obstacles = new List<ObstacleSpec>(24);

            ScatterObstacles(data, obstacles);

            int expected = BuildGateRows(data, gates, obstacles);
            data.gates = gates.ToArray();
            data.obstacles = obstacles.ToArray();
            data.expectedCrowdAtTower = Mathf.Max(5, expected);

            BuildTower(data, ddsBias);

            data.goldPerUnit = goldPerUnit;
            data.goldMultiplier = data.isFeeder ? feederGoldMultiplier : 1f;
            data.floorClearBonus = 20 + 5 * levelIndex;

            return data;
        }

        private LevelData FindAuthored(int levelIndex)
        {
            if (authoredLevels == null) return null;
            for (int i = 0; i < authoredLevels.Length; i++)
            {
                if (authoredLevels[i] != null && authoredLevels[i].levelIndex == levelIndex)
                {
                    return authoredLevels[i];
                }
            }
            return null;
        }

        // ------------------------------------------------------------------ track content

        private int GateRowCount(LevelData data)
        {
            if (data.isTutorial) return tutorialGateRows;
            if (data.isFeeder) return feederGateRows;
            if (data.levelIndex <= masteryLevels) return masteryGateRows;
            return Mathf.Clamp(masteryGateRows + (data.levelIndex - masteryLevels) / 6, masteryGateRows, maxGateRows);
        }

        private float SkillModel(LevelData data)
        {
            if (data.isTutorial) return 0.99f;
            if (data.isFeeder) return 0.95f;
            if (data.levelIndex <= masteryLevels) return 0.86f;
            return Mathf.Lerp(0.86f, 0.70f, Mathf.InverseLerp(masteryLevels, difficultyPlateauLevel, data.levelIndex));
        }

        private void ScatterObstacles(LevelData data, List<ObstacleSpec> obstacles)
        {
            if (data.isTutorial || data.isFeeder) return;

            int count = Mathf.RoundToInt(Mathf.Lerp(1f, maxScatteredObstacles,
                Mathf.InverseLerp(4f, 30f, data.levelIndex)));

            float from = firstRowDistance - 8f;
            float to = data.trackLength - finalRowPadding - 6f;
            if (to <= from) return;

            for (int i = 0; i < count; i++)
            {
                ObstacleSpec spec = new ObstacleSpec();
                spec.distance = RangeFloat(from, to);
                spec.lane = RangeFloat(-0.85f, 0.85f);
                spec.killRadius = obstacleKillRadius;

                if (data.levelIndex >= 16 && Chance(0.30f))
                {
                    spec.kind = ObstacleKind.Spinner;
                    spec.motionAmplitude = RangeFloat(2.0f, 3.2f);
                    spec.motionSpeed = RangeFloat(0.45f, 0.85f);
                }
                else if (data.levelIndex >= 11 && Chance(0.45f))
                {
                    spec.kind = ObstacleKind.Sweeper;
                    spec.motionAmplitude = RangeFloat(1.8f, 3.4f);
                    spec.motionSpeed = RangeFloat(0.35f, 0.7f);
                }
                else
                {
                    spec.kind = ObstacleKind.Static;
                }

                obstacles.Add(spec);
            }
        }

        /// <summary>Builds every decision row and returns the simulated crowd size at the tower.</summary>
        private int BuildGateRows(LevelData data, List<GateSpec> gates, List<ObstacleSpec> obstacles)
        {
            int rows = GateRowCount(data);
            float skill = SkillModel(data);
            int expected = data.startingCrowd;

            float firstZ = firstRowDistance;
            float lastZ = Mathf.Max(firstZ + 12f, data.trackLength - finalRowPadding);
            float previousZ = 0f;
            bool trapUsed = false;

            for (int row = 0; row < rows; row++)
            {
                float t = rows <= 1 ? 0.5f : row / (float)(rows - 1);
                float z = Mathf.Lerp(firstZ, lastZ, t) + RangeFloat(-2f, 2f);

                GateSpec left = new GateSpec();
                GateSpec right = new GateSpec();
                left.rowId = row;
                right.rowId = row;
                left.distance = z;
                right.distance = z;
                left.lane = -gateLane;
                right.lane = gateLane;

                if (data.isTutorial)
                {
                    ComposeTutorialRow(data, row, rows, expected, ref left, ref right);
                }
                else if (data.isFeeder)
                {
                    ComposeFeederRow(row, expected, ref left, ref right);
                }
                else if (data.levelIndex <= masteryLevels)
                {
                    ComposeMasteryRow(data, expected, ref trapUsed, ref left, ref right);
                }
                else
                {
                    ComposeCoreRow(data, expected, ref trapUsed, ref left, ref right);
                }

                if (Chance(0.5f))
                {
                    GateSpec swap = left;
                    left = right;
                    right = swap;
                    float lane = left.lane;
                    left.lane = right.lane;
                    right.lane = lane;
                }

                GuardRiskyHalf(data, ref left, obstacles);
                GuardRiskyHalf(data, ref right, obstacles);

                gates.Add(left);
                gates.Add(right);

                int leftResult = GateMath.Apply(expected, left.mathType, left.value);
                int rightResult = GateMath.Apply(expected, right.mathType, right.value);
                int best = Mathf.Max(leftResult, rightResult);
                int worst = Mathf.Min(leftResult, rightResult);
                expected = Mathf.Max(1, Mathf.RoundToInt(Mathf.Lerp(worst, best, skill)));

                int hazardsInSegment = CountObstaclesBetween(obstacles, previousZ, z);
                if (hazardsInSegment > 0)
                {
                    float survival = Mathf.Pow(1f - attritionPerObstacle * (1f - skill), hazardsInSegment);
                    expected = Mathf.Max(1, Mathf.RoundToInt(expected * survival));
                }

                previousZ = z;
            }

            int trailing = CountObstaclesBetween(obstacles, previousZ, data.trackLength);
            if (trailing > 0)
            {
                float survival = Mathf.Pow(1f - attritionPerObstacle * (1f - skill), trailing);
                expected = Mathf.Max(1, Mathf.RoundToInt(expected * survival));
            }

            return Mathf.Clamp(expected, 1, GateMath.MaxCrowd);
        }

        /// <summary>
        /// Levels 1-3. Both halves are gifts, so the win rate stays at 100 percent, and the
        /// multiplier ladder ramps the finale crowd 600 -> 900 -> 1500 across the three levels.
        /// </summary>
        private void ComposeTutorialRow(LevelData data, int row, int rows, int expected,
            ref GateSpec left, ref GateSpec right)
        {
            int[][] ladders =
            {
                new[] { 2, 3, 5 },
                new[] { 3, 3, 5 },
                new[] { 3, 5, 5 }
            };

            int[] ladder = ladders[Mathf.Clamp(data.levelIndex - 1, 0, ladders.Length - 1)];
            int ladderIndex = Mathf.Clamp(row, 0, ladder.Length - 1);
            if (row == rows - 1) ladderIndex = ladder.Length - 1;

            left.mathType = MathType.Multiply;
            left.value = ladder[ladderIndex];
            left.motion = GateMotion.Static;

            right.mathType = MathType.Add;
            right.value = Mathf.Max(10, Mathf.RoundToInt(expected * 0.6f));
            right.motion = GateMotion.Static;
        }

        /// <summary>
        /// Gold rush breather. Multiplier rows alternate with additive rows on purpose: four
        /// stacked multipliers would compound into a five figure crowd and wreck the gold curve,
        /// so the level lands around 800-900 runners - a visible horde, not an outlier.
        /// </summary>
        private void ComposeFeederRow(int row, int expected, ref GateSpec left, ref GateSpec right)
        {
            if (row % 2 == 0)
            {
                left.mathType = MathType.Multiply;
                left.value = 3;
                left.motion = GateMotion.Static;

                right.mathType = MathType.Multiply;
                right.value = 2;
                right.motion = GateMotion.Static;
            }
            else
            {
                left.mathType = MathType.Add;
                left.value = Mathf.Max(25, Mathf.RoundToInt(expected * 1.2f));
                left.motion = GateMotion.Static;

                right.mathType = MathType.Add;
                right.value = Mathf.Max(15, Mathf.RoundToInt(expected * 0.7f));
                right.motion = GateMotion.Static;
            }
        }

        private void ComposeMasteryRow(LevelData data, int expected, ref bool trapUsed,
            ref GateSpec left, ref GateSpec right)
        {
            int safeAdd = Mathf.Max(10, Mathf.RoundToInt(expected * safeAddFraction));
            int riskyMultiplier = (data.levelIndex >= 8 && Chance(0.4f)) ? 3 : 2;

            bool makeTrap = !trapUsed && data.levelIndex >= 7 && Chance(0.35f);
            if (makeTrap)
            {
                trapUsed = true;
                left.mathType = MathType.Subtract;
                left.value = Mathf.Max(5, Mathf.RoundToInt(expected * trapSubtractFraction));
            }
            else
            {
                left.mathType = MathType.Add;
                left.value = safeAdd;
            }

            left.motion = GateMotion.Static;

            right.mathType = MathType.Multiply;
            right.value = riskyMultiplier;
            right.isRiskyChoice = true;
            ApplyRiskyMotion(data, ref right);
        }

        private void ComposeCoreRow(LevelData data, int expected, ref bool trapUsed,
            ref GateSpec left, ref GateSpec right)
        {
            float roll = RangeFloat(0f, 1f);

            if (roll < 0.45f)
            {
                // Safe versus guarded multiplier.
                left.mathType = MathType.Add;
                left.value = Mathf.Max(12, Mathf.RoundToInt(expected * safeAddFraction));
                left.motion = GateMotion.Static;

                right.mathType = MathType.Multiply;
                right.value = Chance(0.35f) ? 3 : 2;
                right.isRiskyChoice = true;
                ApplyRiskyMotion(data, ref right);
            }
            else if (roll < 0.75f)
            {
                // Multiplier race: both good, one clearly better but defended.
                left.mathType = MathType.Multiply;
                left.value = 2;
                left.motion = GateMotion.Static;

                right.mathType = MathType.Multiply;
                right.value = Chance(0.3f) ? 4 : 3;
                right.isRiskyChoice = true;
                ApplyRiskyMotion(data, ref right);
            }
            else if (!trapUsed && expected > 40)
            {
                // Lesser evil: both halves hurt, the player minimises the damage.
                trapUsed = true;
                left.mathType = MathType.Divide;
                left.value = 2;
                left.motion = GateMotion.Static;

                right.mathType = MathType.Subtract;
                right.value = Mathf.Max(10, Mathf.RoundToInt(expected * trapSubtractFraction));
                right.motion = Chance(0.5f) ? GateMotion.Horizontal : GateMotion.Static;
                right.motionAmplitude = 2.2f;
                right.motionSpeed = 0.45f;
            }
            else
            {
                left.mathType = MathType.Add;
                left.value = Mathf.Max(15, Mathf.RoundToInt(expected * 0.5f));
                left.motion = GateMotion.Horizontal;
                left.motionAmplitude = 2.0f;
                left.motionSpeed = 0.4f;

                right.mathType = MathType.Multiply;
                right.value = 2;
                right.isRiskyChoice = true;
                ApplyRiskyMotion(data, ref right);
            }
        }

        private void ApplyRiskyMotion(LevelData data, ref GateSpec spec)
        {
            if (data.levelIndex >= 9 && Chance(0.4f))
            {
                spec.motion = GateMotion.Rotating;
                spec.motionAmplitude = RangeFloat(2.2f, 3.0f);
                spec.motionSpeed = RangeFloat(0.30f, 0.55f);
            }
            else if (data.levelIndex >= 6)
            {
                spec.motion = GateMotion.Horizontal;
                spec.motionAmplitude = RangeFloat(1.8f, 2.8f);
                spec.motionSpeed = RangeFloat(0.35f, 0.60f);
            }
            else
            {
                spec.motion = GateMotion.Static;
            }
        }

        /// <summary>Places the hazards that make the greedy multiplier half a real decision.</summary>
        private void GuardRiskyHalf(LevelData data, ref GateSpec spec, List<ObstacleSpec> obstacles)
        {
            if (!spec.isRiskyChoice || data.isTutorial || data.isFeeder) return;

            int guards = data.levelIndex >= 12 ? 2 : 1;
            for (int i = 0; i < guards; i++)
            {
                ObstacleSpec guard = new ObstacleSpec();
                guard.distance = spec.distance - 6f - i * 4.5f;
                guard.lane = spec.lane + RangeFloat(-0.12f, 0.12f);
                guard.killRadius = obstacleKillRadius;

                if (data.levelIndex >= 10)
                {
                    guard.kind = ObstacleKind.Sweeper;
                    guard.motionAmplitude = RangeFloat(1.2f, 2.2f);
                    guard.motionSpeed = RangeFloat(0.45f, 0.8f);
                }
                else
                {
                    guard.kind = ObstacleKind.Static;
                }

                if (guard.distance > 8f) obstacles.Add(guard);
            }
        }

        private static int CountObstaclesBetween(List<ObstacleSpec> obstacles, float fromZ, float toZ)
        {
            int count = 0;
            for (int i = 0; i < obstacles.Count; i++)
            {
                float d = obstacles[i].distance;
                if (d > fromZ && d <= toZ) count++;
            }
            return count;
        }

        // ------------------------------------------------------------------ tower

        private void BuildTower(LevelData data, float ddsBias)
        {
            int floors;
            if (data.isTutorial || data.isFeeder) floors = 2;
            else floors = Mathf.Clamp(2 + data.levelIndex / 6, 2, maxFloors);

            data.floorCount = floors;

            float ratio;
            if (data.isTutorial) ratio = tutorialDifficulty;
            else if (data.isFeeder) ratio = feederDifficulty;
            else ratio = Mathf.Lerp(coreDifficultyStart, coreDifficultyEnd,
                Mathf.InverseLerp(4f, difficultyPlateauLevel, data.levelIndex));

            ratio *= ddsBias;
            data.difficultyRatio = ratio;

            data.casualtyRatio = (data.isTutorial || data.isFeeder)
                ? 0f
                : Mathf.Lerp(0f, maxCasualtyRatio, Mathf.InverseLerp(8f, 30f, data.levelIndex));

            int totalPower = Mathf.Max(floors * 3, Mathf.RoundToInt(data.expectedCrowdAtTower * ratio));

            float weightSum = 0f;
            float[] weights = new float[floors];
            for (int f = 0; f < floors; f++)
            {
                weights[f] = 1f + floorWeightGrowth * f;
                weightSum += weights[f];
            }

            List<TowerNodeSpec> nodes = new List<TowerNodeSpec>(16);
            for (int f = 0; f < floors; f++)
            {
                int floorPower = Mathf.Max(2, Mathf.RoundToInt(totalPower * (weights[f] / weightSum)));
                bool isTop = f == floors - 1;

                if (isTop)
                {
                    TowerNodeSpec boss = new TowerNodeSpec();
                    boss.floor = f;
                    boss.slot = 0;
                    boss.power = floorPower;
                    boss.isBoss = true;
                    nodes.Add(boss);
                    continue;
                }

                int slots = (data.levelIndex >= 15 && Chance(0.45f)) ? 3 : 2;
                float[] shares = BuildShares(slots);
                for (int s = 0; s < slots; s++)
                {
                    TowerNodeSpec node = new TowerNodeSpec();
                    node.floor = f;
                    node.slot = s;
                    node.power = Mathf.Max(1, Mathf.RoundToInt(floorPower * shares[s]));
                    nodes.Add(node);
                }
            }

            BalanceTower(data, nodes);

            data.nodes = nodes.ToArray();
            data.totalTowerPower = data.SumPower();
        }

        private float[] BuildShares(int slots)
        {
            float[] shares = new float[slots];
            float sum = 0f;
            for (int i = 0; i < slots; i++)
            {
                shares[i] = (0.6f + i * 0.35f) * RangeFloat(0.9f, 1.1f);
                sum += shares[i];
            }

            for (int i = 0; i < slots; i++) shares[i] /= sum;
            return shares;
        }

        /// <summary>
        /// Guarantees the produced tower is beatable by the reference crowd (and, for the hook and
        /// feeder levels, by a much weaker crowd) while staying non trivial for the core loop.
        /// </summary>
        private void BalanceTower(LevelData data, List<TowerNodeSpec> nodes)
        {
            float safetyMargin = (data.isTutorial || data.isFeeder) ? 0.70f : 0.90f;
            int referenceCrowd = Mathf.Max(3, Mathf.RoundToInt(data.expectedCrowdAtTower * safetyMargin));
            int trivialCrowd = Mathf.Max(2, Mathf.RoundToInt(data.expectedCrowdAtTower * 0.40f));

            if (!data.isTutorial && !data.isFeeder)
            {
                int tighten = 0;
                while (tighten < 12 && LevelData.IsSolvable(nodes, trivialCrowd, data.casualtyRatio))
                {
                    ScaleNodes(nodes, 1.08f);
                    tighten++;
                }
            }

            int relax = 0;
            while (relax < 32 && !LevelData.IsSolvable(nodes, referenceCrowd, data.casualtyRatio))
            {
                ScaleNodes(nodes, 0.88f);
                relax++;
            }

            if (!LevelData.IsSolvable(nodes, referenceCrowd, data.casualtyRatio))
            {
                // Absolute fallback: rebuild a guaranteed beatable ladder.
                for (int i = 0; i < nodes.Count; i++)
                {
                    TowerNodeSpec spec = nodes[i];
                    spec.power = Mathf.Max(1, Mathf.RoundToInt(referenceCrowd * 0.35f) + spec.floor * 2);
                    nodes[i] = spec;
                }
            }
        }

        private static void ScaleNodes(List<TowerNodeSpec> nodes, float scale)
        {
            for (int i = 0; i < nodes.Count; i++)
            {
                TowerNodeSpec spec = nodes[i];
                spec.power = Mathf.Clamp(Mathf.RoundToInt(spec.power * scale), 1, GateMath.MaxCrowd);
                nodes[i] = spec;
            }
        }

        // ------------------------------------------------------------------ rng helpers

        private float RangeFloat(float min, float max)
        {
            if (_rng == null) _rng = new System.Random(randomSeed);
            return min + (float)_rng.NextDouble() * (max - min);
        }

        private bool Chance(float probability)
        {
            return RangeFloat(0f, 1f) < probability;
        }
    }
}
