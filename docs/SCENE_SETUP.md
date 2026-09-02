# Scene, layers and physics setup

Everything below is what `Tools ▸ Mob Clash ▸ 4. Setup Everything` and
`Tools ▸ Mob Clash ▸ 5. Build Playable Scene` do for you. It is written out in full so you can
build the scene by hand, audit what the tool did, or rebuild it inside an existing project.

---

## 1. Project settings

### Tags

`Edit ▸ Project Settings ▸ Tags and Layers ▸ Tags`

| Tag | Used by |
| --- | --- |
| `Player` | built in; the crowd pivot, read by `Gate` and `FinishLine` |
| `SwarmUnit` | crowd members (filtering only, they carry no colliders) |
| `Gate` | gate halves |
| `Obstacle` | track hazards |
| `Track` | ground and rails |
| `TowerNode` | siege rooms |

`PrimitiveFactory.SafeSetTag` swallows a missing tag with a warning instead of throwing, so a
half-configured project still runs.

### Layers

`Edit ▸ Project Settings ▸ Tags and Layers ▸ Layers`. The indices matter — `GameLayers` falls back
to them by number when a name lookup fails.

| Index | Name |
| --- | --- |
| 8 | `Player` |
| 9 | `SwarmUnit` |
| 10 | `Gate` |
| 11 | `Obstacle` |
| 12 | `Track` |
| 13 | `TowerNode` |

### Physics collision matrix

`Edit ▸ Project Settings ▸ Physics ▸ Layer Collision Matrix`

**Uncheck every pair among the six gameplay layers, then re-check exactly two.**

| | Player | SwarmUnit | Gate | Obstacle | Track | TowerNode |
| --- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Player** | ✗ | ✗ | **✓** | **✓** | ✗ | ✗ |
| **SwarmUnit** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **Gate** | **✓** | ✗ | ✗ | ✗ | ✗ | ✗ |
| **Obstacle** | **✓** | ✗ | ✗ | ✗ | ✗ | ✗ |
| **Track** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **TowerNode** | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |

Why so aggressive:

* **SwarmUnit collides with nothing** because crowd members have **no colliders at all**. 500+
  rigidbodies jostling each other is the single biggest cause of the framerate collapse and the
  "crowd explodes through the rail" glitch in this genre. Cohesion is solved analytically in
  `SwarmManager`, and hazards remove units with a distance check against a registry of nearby
  obstacles — a few thousand float comparisons per frame instead of a physics broadphase.
* **Track collides with nothing** because movement is entirely code driven; the ground is a visual.
* **TowerNode collides with nothing** because room selection uses `Physics.Raycast` with a layer
  mask, and raycasts ignore the collision matrix entirely.
* That leaves **Player × Gate** (trigger, drives the maths) and **Player × Obstacle** (trigger, drives
  camera shake and haptics only — the actual unit loss is computed in `SwarmManager`).

`GameManager.Awake` calls `GameLayers.ApplyRuntimeCollisionMatrix()`, which re-applies these rules
at runtime. The build is therefore correct even if the project settings asset was never touched.

### Player settings (see `docs/ANDROID_BUILD.md` for the full list)

Company `ViralGames` · Product `Mob Clash: Gate Siege` · Bundle `com.viralgames.mobclash` ·
Portrait only · Linear colour space.

---

## 2. Scene hierarchy

```
Game.unity
├── Directional Light                 rotation (52, -35, 0), shadows off
├── CameraRig                         Camera + AudioListener + CameraRig
│                                     pos (0, 9.5, -11.5), rot (26, 0, 0), FOV 58
├── Player                            layer Player, tag Player
│                                     Rigidbody (kinematic, no gravity)
│                                     SphereCollider (trigger, r 0.75, centre y 0.75)
│                                     PlayerSwerve
├── Swarm                             SwarmManager + ObjectPool
│   └── PoolParent                    inactive pooled crowd members live here
├── Level                             LevelBuilder
│   └── LevelContent                  ground, rails, gates, hazards, finish line (rebuilt per level)
├── Siege                             SiegeManager
│   └── TowerRoot                     floors and rooms (rebuilt per level)
├── Managers                          EconomyManager, AdManager, JuiceManager, GameManager
└── UI                                UIManager  (builds its own Canvas + EventSystem in Awake)
```

### Wiring checklist

| Component | Field | Value |
| --- | --- | --- |
| `CameraRig` | `target` | `Player` transform |
| `SwarmManager` | `pivot` | `Player` transform |
| `SwarmManager` | `unitPool` | the `ObjectPool` on `Swarm` |
| `ObjectPool` | `poolParent` | `Swarm/PoolParent` |
| `SiegeManager` | `swarm` | `Swarm` |
| `SiegeManager` | `towerRoot` | `Siege/TowerRoot` |
| `LevelBuilder` | `contentRoot` | `Level/LevelContent` |
| `JuiceManager` | `cameraRig` | `CameraRig` |
| `GameManager` | `generator` | leave empty for the default tuning, or assign a `LevelGenerator` asset |
| `GameManager` | `levelBuilder`, `swarm`, `siege`, `player`, `cameraRig` | the matching objects |

`GameManager.ResolveMissingReferences()` finds anything you leave empty, so a missed drag is a
warning-free recovery rather than a null reference at runtime.

---

## 3. Optional: authored levels

`Create ▸ Mob Clash ▸ Level Data` makes a `LevelData` asset. Set its `levelIndex`, drop it into the
`authoredLevels` array on a `LevelGenerator` asset, and that index is served from the asset instead
of the procedural generator — the "semi-procedural" path for hand-tuning a specific level without
giving up generation everywhere else.

`Create ▸ Mob Clash ▸ Level Generator` makes a generator asset if you want to tune the pacing
constants in the inspector rather than in code.

---

## 4. Performance notes for mid-range Android

* `Application.targetFrameRate = 60`, vSync off (`GameManager`).
* Crowd budget: `SwarmManager.maxVisualUnits = 600`. The *logical* count is uncapped, so the HUD can
  read `1240` while 600 capsules are drawn. Drop to 300 for low-end devices.
* Enable **GPU Instancing** on the crowd material (the generated one already has it on) and keep the
  unit mesh under ~200 triangles.
* Shadows are off on every generated renderer, along with light probes, reflection probes and motion
  vectors.
* Pool prewarm is 320 units, so the first gate that triples the crowd allocates nothing.
* No `Update` on any crowd member: one loop in `SwarmManager` writes all 600 transforms.
