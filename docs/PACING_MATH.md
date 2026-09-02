# Pacing math

Every number the generator uses, and why it is that number. The Unity implementation is
`Assets/Scripts/Data/LevelGenerator.cs`; the browser port is the `TUNE` object and `generateLevel()`
in `web/index.html`. They produce identical levels for identical inputs.

---

## The central idea: relative difficulty

A player who has bought ten levels of *Starting Mob* arrives at the tower with a far bigger crowd
than a fresh install. Sizing towers against absolute numbers would make the game trivial for the
first player and impossible for the second.

So the generator does this instead:

1. Build the track (gate rows and hazards) for the level index.
2. **Simulate a reference run** through the track it just built, using a skill model that says what
   fraction of decisions a player at this stage gets right.
3. Size the tower as a fraction of that simulated result.
4. **Prove the tower is beatable** with a greedy solver before shipping it.

`Generate(levelIndex, startingCrowd, ddsBias)` therefore takes the player's real starting crowd as an
input. Difficulty is a ratio, never a constant.

## Level bands

```
isTutorial = levelIndex <= 3
isFeeder   = !isTutorial && levelIndex % 4 == 0
isMastery  = !isTutorial && !isFeeder && levelIndex <= 10
isCore     = everything else
```

| Band | Gate rows | Hazards | Row composition |
| --- | --- | --- | --- |
| Hook (1–3) | 3 | 0 | Both halves are gifts. Ladders `[2,3,5]`, `[3,3,5]`, `[3,5,5]` → finale crowd ≈ 600 / 900 / 1500. |
| Gold Rush (every 4th) | 4 | 0 | Multiplier rows (×3 / ×2) alternate with big additive rows. ×2 gold, 0.30 tower ratio. |
| Mastery (4–10) | 4 | ramping | `+N now` vs `×M guarded`. Sliding gates from L6, rotating from L9, one trap row from L7. |
| Core (11+) | 4 → 6 | ramping | 45% safe-vs-risky, 30% multiplier race, 25% lesser-evil trap pair. |

> **Why the Gold Rush alternates.** Four stacked multipliers compound to ~675×, which sends the crowd
> to five figures and — because the payout is `survivors × goldMultiplier` — hands the player 26,000
> gold on level 4 and flattens the entire upgrade curve. Alternating multiplier and additive rows
> lands the horde at ~900–1,300: visibly larger than the ~300–600 of neighbouring levels, which is
> the whole point of a breather, without breaking the economy.

## Track scaling

```
trackLength = min(300, 110 + 6 × (level − 1))       × 0.92 on feeder levels
runSpeed    = min(18,  12 + 0.11 × (level − 1))
trackHalfWidth = 6
```

Hazard count for non-feeder, non-tutorial levels:

```
scattered  = round(lerp(1, 14, invLerp(4, 30, level)))
guards     = 1 per risky gate half, 2 from level 12
```

## The skill model

```
skill = 0.99                                        levels 1-3
      = 0.95                                        gold rush
      = 0.86                                        levels 4-10
      = lerp(0.86, 0.70, invLerp(10, 40, level))     level 11+
```

Applied per row as `expected = lerp(worstOutcome, bestOutcome, skill)`, and per hazard as
`expected ×= (1 − 0.10 × (1 − skill)) ^ hazardsSincePreviousRow`. A perfect player takes every good
gate; the model assumes a real one takes 86% of them at level 10 and 70% by level 40.

## Tower sizing

```
floors      = 2                                     tutorial and gold rush
            = clamp(2 + floor(level / 6), 2, 6)      otherwise

ratio       = 0.35                                  tutorial
            = 0.30                                  gold rush
            = lerp(0.55, 0.95, invLerp(4, 40, level)) otherwise
ratio      ×= ddsBias

totalPower  = max(floors × 3, round(expectedCrowd × ratio))
floorWeight = 1 + 0.75 × floorIndex                 (normalised; upper floors are heavier)
casualties  = lerp(0, 0.25, invLerp(8, 30, level))  fraction of a room's power lost on capture
```

The top floor is always a single **boss** room holding that floor's whole allocation. Lower floors
split into 2 rooms (3 from level 15, 45% of the time) with jittered shares.

## Dynamic difficulty (DDS)

```
ddsBias = clamp(1 − 0.08 × consecutiveFails, 0.60, 1.00)
```

Three losses in a row shave 24% off tower power; a win resets the counter. It is invisible, bounded,
and only ever makes the game easier — the failure mode of an aggressive DDS is a player who notices
they are being handled.

## The solvability guarantee

`LevelData.IsSolvable(nodes, crowd, casualtyRatio)` runs the optimal greedy strategy — always attack
the weakest available room — and reports whether the tower falls.

```
referenceCrowd = expectedCrowd × 0.70   (tutorial and gold rush: a wide safety margin)
               = expectedCrowd × 0.90   (everything else)
trivialCrowd   = expectedCrowd × 0.40

if not tutorial/feeder:  while solvable at trivialCrowd     → scale all rooms ×1.08   (max 12 times)
                         while NOT solvable at reference    → scale all rooms ×0.88   (max 32 times)
fallback:                rebuild as a guaranteed ladder
```

The tighten pass stops the tower being a walkover; the relax pass guarantees it is winnable. The
relax pass runs last, so solvability always wins over difficulty. A level that cannot be beaten is
never shipped to a player.

## Generated output (seed 90211, fresh-ish save)

| Lvl | Band | Track | Rows | Hazards | Expected crowd | Ratio | Floors | Tower power | Weakest ground room | Solvable |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | hook | 110 | 3 | 0 | 591 | 0.35 | 2 | 207 | 31 | yes |
| 2 | hook | 116 | 3 | 0 | 889 | 0.35 | 2 | 311 | 45 | yes |
| 3 | hook | 122 | 3 | 0 | 1480 | 0.35 | 2 | 518 | 74 | yes |
| 4 | gold rush | 118 | 4 | 0 | 905 | 0.30 | 2 | 272 | 41 | yes |
| 5 | mastery | 134 | 4 | 6 | 292 | 0.56 | 2 | 408 | 57 | yes |
| 6 | mastery | 140 | 4 | 6 | 291 | 0.57 | 3 | 422 | 31 | yes |
| 7 | mastery | 146 | 4 | 7 | 265 | 0.58 | 3 | 391 | 29 | yes |
| 8 | gold rush | 140 | 4 | 0 | 988 | 0.30 | 2 | 296 | 38 | yes |
| 9 | mastery | 158 | 4 | 8 | 612 | 0.61 | 3 | 935 | 72 | yes |
| 10 | mastery | 164 | 4 | 8 | 868 | 0.62 | 3 | 1345 | 92 | yes |
| 11 | core | 170 | 4 | 8 | 142 | 0.63 | 3 | 213 | 6 | yes |
| 12 | gold rush | 162 | 4 | 0 | 1073 | 0.30 | 2 | 322 | 42 | yes |
| 13 | core | 182 | 4 | 14 | 1349 | 0.65 | 4 | 2203 | 99 | yes |
| 14 | core | 188 | 4 | 12 | 393 | 0.66 | 4 | 661 | 29 | yes |
| 15 | core | 194 | 4 | 13 | 342 | 0.67 | 4 | 564 | 6 | yes |
| 16 | gold rush | 184 | 4 | 0 | 1162 | 0.30 | 2 | 349 | 25 | yes |
| 17 | core | 206 | 5 | 16 | 381 | 0.69 | 4 | 662 | 19 | yes |
| 18 | core | 212 | 5 | 18 | 1687 | 0.71 | 5 | 3002 | 52 | yes |
| 19 | core | 218 | 5 | 19 | 1069 | 0.72 | 5 | 1926 | 31 | yes |
| 20 | gold rush | 206 | 4 | 0 | 1244 | 0.30 | 2 | 373 | 52 | yes |

Read the rhythm down the *Expected crowd* and *Ratio* columns: the hook climbs, level 4 hands out a
horde against a 0.30 tower, levels 5–7 tighten hard (0.56–0.58 with hazards appearing), level 8
releases again, and the core loop ramps toward 0.95 with a gold rush every fourth level. That
sawtooth is the whole retention design — sustained pressure is what makes players churn.

The table was produced by running `generateLevel()` from `web/index.html` under Node for levels
1-40; every one of the 40 towers passes the solvability check at its reference crowd.

## Economy

```
StartingCrowd    value = 20 + 2 × level        cost = 120 × 1.35 ^ level
GoldMultiplier   value = 1 + 0.15 × level      cost = 300 × 1.45 ^ level
GateBonus        value = +1 flat and +0.05×    cost = 500 × 1.50 ^ level
                 per level on additive and multiplicative gates respectively
maxUpgradeLevel = 50

reward = (survivors × goldPerUnit + roomsCleared × floorClearBonus)
       × levelGoldMultiplier          (2 on gold rush levels)
       × playerGoldMultiplier
       × 0.25 on a loss
       × 3 if the rewarded video is watched
```

The cost bases are matched to the payout scale: a first run pays roughly 600 gold, which buys about
three levels of Starting Mob — fast enough to teach the loop, steep enough (1.35^n) that level 15 of
the same track costs ~11,000 and becomes a real goal.

## Ad cadence

Interstitials: never before level 3, at most one per two completed levels, minimum 45 seconds apart.
Rewarded video: offered on the victory screen for ×3 gold, always optional, never gating progress.
