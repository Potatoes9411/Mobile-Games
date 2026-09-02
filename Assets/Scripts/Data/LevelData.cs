using System;
using System.Collections.Generic;
using MobClash.Core;
using UnityEngine;

namespace MobClash.Data
{
    /// <summary>Placement and behaviour description for a single math gate half.</summary>
    [Serializable]
    public struct GateSpec
    {
        /// <summary>Distance along the track in world units (Z).</summary>
        public float distance;

        /// <summary>Horizontal placement, normalised to [-1, 1] where 1 is the right rail.</summary>
        public float lane;

        public MathType mathType;
        public int value;
        public GateMotion motion;

        /// <summary>Lateral travel (Horizontal) or orbit radius (Rotating), in world units.</summary>
        public float motionAmplitude;

        /// <summary>Cycles per second for the motion.</summary>
        public float motionSpeed;

        /// <summary>Marks the high risk / high reward half of a decision pair.</summary>
        public bool isRiskyChoice;

        /// <summary>Index of the decision row this half belongs to. Halves in a row cancel each other.</summary>
        public int rowId;
    }

    /// <summary>Placement and behaviour description for a track hazard.</summary>
    [Serializable]
    public struct ObstacleSpec
    {
        public float distance;
        public float lane;
        public ObstacleKind kind;
        public float motionAmplitude;
        public float motionSpeed;

        /// <summary>Crowd members inside this radius are removed from the swarm.</summary>
        public float killRadius;
    }

    /// <summary>A single conquerable room inside the end-of-level tower.</summary>
    [Serializable]
    public struct TowerNodeSpec
    {
        /// <summary>Zero based floor, 0 is the ground floor and unlocks first.</summary>
        public int floor;

        /// <summary>Horizontal slot inside the floor.</summary>
        public int slot;

        /// <summary>Number of defenders. Beatable when the crowd is strictly larger.</summary>
        public int power;

        public bool isBoss;
    }

    /// <summary>
    /// Immutable-by-convention description of one playable level.
    /// Authored levels can be created as assets (Create -> Mob Clash -> Level Data);
    /// levels 11+ are produced at runtime by <see cref="LevelGenerator"/>.
    /// </summary>
    [CreateAssetMenu(fileName = "LevelData", menuName = "Mob Clash/Level Data", order = 0)]
    public class LevelData : ScriptableObject
    {
        [Header("Identity")]
        public int levelIndex = 1;
        public bool isFeeder;
        public bool isTutorial;

        [Header("Track")]
        public float trackLength = 120f;
        public float trackHalfWidth = 6f;
        public float runSpeed = 12f;
        public int startingCrowd = 20;

        [Header("Content")]
        public GateSpec[] gates = new GateSpec[0];
        public ObstacleSpec[] obstacles = new ObstacleSpec[0];

        [Header("Tower")]
        public int floorCount = 2;
        public TowerNodeSpec[] nodes = new TowerNodeSpec[0];

        /// <summary>Fraction of a conquered room's power lost as casualties. 0 = pure absorb.</summary>
        [Range(0f, 0.6f)]
        public float casualtyRatio;

        [Header("Economy")]
        public int goldPerUnit = 1;
        public float goldMultiplier = 1f;
        public int floorClearBonus = 25;

        [Header("Diagnostics (generated)")]
        public int expectedCrowdAtTower = 20;
        public float difficultyRatio = 0.5f;
        public int totalTowerPower;

        /// <summary>Highest floor index present in <see cref="nodes"/>.</summary>
        public int TopFloor
        {
            get
            {
                int top = 0;
                for (int i = 0; i < nodes.Length; i++)
                {
                    if (nodes[i].floor > top) top = nodes[i].floor;
                }
                return top;
            }
        }

        /// <summary>World Z position where the tower is built.</summary>
        public float TowerDistance
        {
            get { return trackLength + 18f; }
        }

        public int CountNodesOnFloor(int floor)
        {
            int count = 0;
            for (int i = 0; i < nodes.Length; i++)
            {
                if (nodes[i].floor == floor) count++;
            }
            return count;
        }

        public int SumPower()
        {
            int sum = 0;
            for (int i = 0; i < nodes.Length; i++) sum += nodes[i].power;
            return sum;
        }

        /// <summary>
        /// Greedy solver used by the generator to guarantee every produced tower can actually be
        /// beaten by the reference crowd. Always attacks the weakest available room first.
        /// </summary>
        public static bool IsSolvable(IList<TowerNodeSpec> nodes, int crowd, float casualtyRatio)
        {
            if (nodes == null || nodes.Count == 0) return true;

            int count = nodes.Count;
            bool[] cleared = new bool[count];
            int remaining = count;

            int topFloor = 0;
            for (int i = 0; i < count; i++)
            {
                if (nodes[i].floor > topFloor) topFloor = nodes[i].floor;
            }

            int guard = 0;
            while (remaining > 0 && guard < 4096)
            {
                guard++;

                int unlockedFloor = 0;
                for (int floor = 0; floor <= topFloor; floor++)
                {
                    bool floorDone = true;
                    for (int i = 0; i < count; i++)
                    {
                        if (nodes[i].floor == floor && !cleared[i])
                        {
                            floorDone = false;
                            break;
                        }
                    }

                    if (!floorDone)
                    {
                        unlockedFloor = floor;
                        break;
                    }

                    unlockedFloor = floor + 1;
                }

                int best = -1;
                for (int i = 0; i < count; i++)
                {
                    if (cleared[i] || nodes[i].floor != unlockedFloor) continue;
                    if (nodes[i].power >= crowd) continue;
                    if (best < 0 || nodes[i].power < nodes[best].power) best = i;
                }

                if (best < 0) return false;

                int power = nodes[best].power;
                crowd += power - Mathf.RoundToInt(power * casualtyRatio);
                cleared[best] = true;
                remaining--;
            }

            return remaining == 0;
        }
    }
}
