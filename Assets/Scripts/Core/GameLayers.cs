using UnityEngine;

namespace MobClash.Core
{
    /// <summary>
    /// Canonical layer and tag names. The editor tool <c>Tools/Mob Clash/Setup Project Layers and Tags</c>
    /// writes these into ProjectSettings; <see cref="ApplyRuntimeCollisionMatrix"/> re-applies the
    /// collision matrix at runtime so the build is correct even if the settings asset was never touched.
    /// </summary>
    public static class GameLayers
    {
        public const string Player = "Player";
        public const string SwarmUnit = "SwarmUnit";
        public const string Gate = "Gate";
        public const string Obstacle = "Obstacle";
        public const string Track = "Track";
        public const string TowerNode = "TowerNode";

        public const string TagPlayer = "Player";
        public const string TagSwarmUnit = "SwarmUnit";
        public const string TagGate = "Gate";
        public const string TagObstacle = "Obstacle";
        public const string TagTrack = "Track";
        public const string TagTowerNode = "TowerNode";

        /// <summary>Layer indices reserved for the project. Index 8 is the first user layer.</summary>
        public const int PlayerIndex = 8;
        public const int SwarmUnitIndex = 9;
        public const int GateIndex = 10;
        public const int ObstacleIndex = 11;
        public const int TrackIndex = 12;
        public const int TowerNodeIndex = 13;

        public static readonly string[] OrderedLayerNames =
        {
            Player, SwarmUnit, Gate, Obstacle, Track, TowerNode
        };

        public static readonly string[] RequiredTags =
        {
            TagPlayer, TagSwarmUnit, TagGate, TagObstacle, TagTrack, TagTowerNode
        };

        /// <summary>
        /// Resolves a layer by name and falls back to the reserved index when the project settings
        /// have not been generated yet.
        /// </summary>
        public static int Resolve(string layerName, int fallbackIndex)
        {
            int index = LayerMask.NameToLayer(layerName);
            return index >= 0 ? index : fallbackIndex;
        }

        public static int PlayerLayer { get { return Resolve(Player, PlayerIndex); } }
        public static int SwarmUnitLayer { get { return Resolve(SwarmUnit, SwarmUnitIndex); } }
        public static int GateLayer { get { return Resolve(Gate, GateIndex); } }
        public static int ObstacleLayer { get { return Resolve(Obstacle, ObstacleIndex); } }
        public static int TrackLayer { get { return Resolve(Track, TrackIndex); } }
        public static int TowerNodeLayer { get { return Resolve(TowerNode, TowerNodeIndex); } }

        public static LayerMask TowerNodeMask { get { return 1 << TowerNodeLayer; } }

        /// <summary>
        /// Applies the performance critical collision matrix. Only Player vs Gate and
        /// Player vs Obstacle are allowed to generate contacts; everything else is disabled so the
        /// broadphase never sees the 500+ crowd members.
        /// </summary>
        public static void ApplyRuntimeCollisionMatrix()
        {
            int[] projectLayers =
            {
                PlayerLayer, SwarmUnitLayer, GateLayer, ObstacleLayer, TrackLayer, TowerNodeLayer
            };

            for (int i = 0; i < projectLayers.Length; i++)
            {
                for (int j = 0; j < projectLayers.Length; j++)
                {
                    Physics.IgnoreLayerCollision(projectLayers[i], projectLayers[j], true);
                }
            }

            Physics.IgnoreLayerCollision(PlayerLayer, GateLayer, false);
            Physics.IgnoreLayerCollision(PlayerLayer, ObstacleLayer, false);
        }
    }
}
