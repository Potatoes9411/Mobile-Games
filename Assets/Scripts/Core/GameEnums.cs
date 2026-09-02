namespace MobClash.Core
{
    /// <summary>Top level finite state machine states driven by <see cref="GameManager"/>.</summary>
    public enum GameState
    {
        Boot = 0,
        MainMenu = 1,
        Running = 2,
        TransitionToSiege = 3,
        SiegeMode = 4,
        LevelWin = 5,
        LevelFail = 6
    }

    /// <summary>Mathematical operation applied by a multiplier gate to the crowd size.</summary>
    public enum MathType
    {
        Add = 0,
        Subtract = 1,
        Multiply = 2,
        Divide = 3
    }

    /// <summary>How a gate behaves over time. Moving gates create split second decisions.</summary>
    public enum GateMotion
    {
        Static = 0,
        Horizontal = 1,
        Rotating = 2
    }

    /// <summary>Track hazard behaviour.</summary>
    public enum ObstacleKind
    {
        Static = 0,
        Sweeper = 1,
        Spinner = 2
    }

    /// <summary>Lifecycle of a single tower room during the siege phase.</summary>
    public enum NodeState
    {
        Locked = 0,
        Available = 1,
        Cleared = 2
    }

    /// <summary>Meta progression upgrade identifiers.</summary>
    public enum UpgradeType
    {
        StartingCrowd = 0,
        GoldMultiplier = 1,
        GateBonus = 2
    }
}
