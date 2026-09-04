# Pocket Arcade

Seven complete games behind one hub, sharing an engine, a progression layer and a
procedural character system. Everything runs from `web/index.html` with classic
`<script>` tags, so it works from a `file://` double-click with no bundler.

```
web/
  index.html                  hub shell, styles, script order
  build-single.js             inlines everything into dist/pocket-arcade.html
  src/hub.js                  account level, gems, missions, shop, game lifecycle
  src/engine/core.js          math, seeded RNG, save, audio, input, loop, canvas
  src/engine/rig.js           procedural skeletal characters
  src/engine/fx.js            particles, floating text, shake, hit stop, flash
  src/engine/view3d.js        pinhole camera for the lane games
  src/games/mobclash.js       Mob Clash: Gate Siege
  src/games/blockblast.js     Block Storm
  src/games/pinrescue.js      Pin Rescue
  src/games/helix.js          Helix Drop
  src/games/splat.js          Roller Splat
  src/games/paperio.js        Paper Territory
  src/games/horde.js          Horde Arena
  src/games/runner.js         Rooftop Run
```

---

## The character system

No character art is authored anywhere in this repository. `A.Rig` turns a seed
into a fighter and a clip name into a pose.

**Style from a seed.** `Rig.style(seed, opts)` produces height, bulk, limb
lengths, head size, a palette derived in HSL from a hue, plus gear flags (helmet
shape, cape, shoulder pads, weapon). Two seeds are two visibly different
characters. Games pass `hue` so the player's squad matches the colour they bought
in the prestige shop.

**Pose from a clip.** `Rig.pose(clip, t)` writes joint angles for a normalised
time. The library covers `idle, walk, march, run, sprint, jump, fall, slide,
roll, attack, shoot, cast, hurt, die, cheer`. Clips are plain functions of `t`,
so they are readable, tweakable and free of animation data.

**Draw by walking the skeleton.** `Rig.draw` solves forward kinematics from the
hips out and renders every segment as a *tapered capsule* — the convex hull of two
circles — with a heavy ink outline, back limbs shaded darker for depth, then the
head, helmet, plume, cape and weapon. Roughly 20 path fills per character.

**Two consumption paths**, which is what makes it affordable:

| Path | Cost | Used for |
| --- | --- | --- |
| `Rig.draw` live | ~20 path fills | heroes, bosses, menu avatar, tower garrisons |
| `Rig.bake` → `Rig.blit` | one `drawImage` | crowds — 260 runners, 120 arena enemies |

`Rig.bake(style, clip, frames, opts)` renders a clip into an offscreen sprite
strip once at boot. A crowd of hundreds costs one `drawImage` each, while still
being animated by the same rig that draws the hero live.

---

## Shared progression

The hub owns everything that survives a run.

* **Account level** — every game awards account XP. Levels grant gems and unlock
  games (Helix Drop and Paper Territory at 2, Horde Arena at 2, Rooftop Run at 3).
* **Gems** — the premium-shaped currency. Earned from levels, daily streaks and
  missions; spent in the **prestige shop** on permanent cross-game multipliers
  (coin gain, XP gain, extra revives) and a squad recolour.
* **Daily missions** — three, rerolled from a pool on a date seed, mixing
  cumulative goals ("collect 900 coins") and personal bests ("reach wave 9").
  Games call `host.progress(type, amount)`; the hub decides what counts.
* **Daily streak** — a seven-pip meter; each new day pays a bonus that grows with
  the streak.
* **Per-game soft currency** — each game keeps its own coins and its own upgrade
  shop, so the meta never collapses into one number.

The layering is deliberate: a run feeds a game's shop, a game feeds the account,
and the account feeds every game. There is always something a few minutes away.

---

## The games

Seven games, chosen against what is actually charting rather than from memory:
puzzle is the second largest mobile genre by revenue and block-drop leads global
downloads, so the arcade covers that; the rest are the formats you have seen a
thousand ad impressions of.

| Game | Format | The hook |
| --- | --- | --- |
| **Mob Clash: Gate Siege** | Crowd runner + puzzle | Gamble a mob through `+ − × ÷` gates, then storm a keep room by room |
| **Block Storm** | Block puzzle | Three pieces, no rotation, clear rows and columns, chain the combo multiplier |
| **Pin Rescue** | Pull-the-pin | Drain the lava out the side, then drop the gold. Wrong order cooks your hero |
| **Helix Drop** | Helix tower | Spin the tower into the gaps; three clean drops and the ball smashes through |
| **Roller Splat** | Paint maze | Swipe, the ball rolls until it hits a wall, painting everything it crosses |
| **Paper Territory** | Territory io | Drive out, loop back, claim what you enclosed; three rivals want the same map |
| **Horde Arena** | Survivor auto-battler | Steer only; draft an upgrade per level and evolve weapons across 20 waves |
| **Rooftop Run** | Endless runner | Three lanes, vault and slide, stack magnet, shield and boost |

Two implementation notes worth knowing:

**Pin Rescue fluids** are a falling-sand cellular automaton, not rigid-body
physics. It reads identically to the real thing, it is deterministic, and a
15x18 grid costs nothing. Levels are generated onto a fixed portrait layout
(shaft, side drain, dead-end pit, shared basin) so the structure is guaranteed
rather than hoped for, and the win threshold is a share of what the basin can
physically hold rather than of all the gold in the level.

**Paper Territory captures** by flooding from the arena border across everything
that is not yours and not your live trail; whatever the flood cannot reach was
enclosed, so it becomes yours. That is one BFS per capture over 3,136 cells.

## Adding a game

Push a definition into `A.games` before `hub.js` loads:

```js
A.games.push({
  id: "mygame",
  name: "My Game",
  tagline: "One line for the tile",
  accent: "#3D8BFF",
  unlock: 2,                       // account level required
  template: { coins: 0, best: 0 }, // per-game save slot defaults
  bestLine: function (save) { return "Best " + save.best; },
  thumb: function (g, w, h, time) { /* animated tile art */ },
  create: function (host) {
    return {
      mount: function (root) {},    // build the HUD
      start: function () {},
      stop: function () {},
      update: function (dt) {},
      render: function (g) {},
      onResize: function () {}
    };
  }
});
```

`host` provides the save slot, DOM helpers, the modal stack, the results card,
mission reporting, the prestige multipliers and `exit()`. The hub handles the
frame loop, input, the canvas, effects and everything above the game.

---

## Performance notes

* Crowds are baked sprites, never live skeletons. Mob Clash renders 260 runners,
  Horde Arena up to 120 enemies, at one `drawImage` each.
* Device pixel ratio is capped at 2. The renderers are fill-rate bound rather than
  draw-call bound: the full-screen sky, ground and vignette gradients dominate, so
  the cheapest win on a weak device is lowering DPR, not culling entities.
* Effects share one capped pool (520 particles) across every game and are reset on
  each game switch.
* Nothing allocates per frame in the hot loops beyond the sort arrays.

## Adding a game

The hub is manifest driven. To add a game:

1. Drop the file in `web/src/games/`. It registers itself:
   `A.games.push({ id, name, tagline, accent, unlock, template, bestLine, thumb, create })`.
2. Add a row to `web/src/manifest.js` with `id`, `title`, `genre` and `script`.

Nothing else in the hub knows the list. `GameManager.preloadAll()` injects each
script at boot and the grid renders tiles as they register, so a slow file never
blocks the rest of the hub. `GameManager.ensure(id)` will also load a game on
demand if it is not yet registered.

The single-file build inlines every manifest script instead, because a page
opened from disk cannot fetch its siblings. The manager checks its registry
before it ever tries to fetch, so the inlined copies simply mean it never has to.

## Lifecycle and teardown

`GameManager` hands every game a **scope** on `host.scope`. Anything registered
on it is released when the player leaves, in reverse order of registration:

| Call | Releases |
| --- | --- |
| `scope.onCleanup(fn)` | runs `fn` on unload |
| `scope.on(target, type, fn)` | detaches the listener |
| `scope.raf(step)` | cancels the animation frame |
| `scope.timeout/interval(fn, ms)` | clears the timer |
| `scope.node(el)` | removes the element |
| `scope.three(renderer, scene)` | disposes every geometry, material and texture on the graph, then the renderer, then forces context loss |
| `scope.matter(engine, runner, render)` | stops the runner and render, clears the world and engine, drops engine events |

This matters because browsers cap the number of live WebGL contexts. Hot-swapping
games without disposing their GPU resources exhausts that cap and takes the tab
with it. `unloadCurrentGame()` also resets the shared effects layer, clears held
keys and pointer flags, wipes the 2D stage, and removes any canvas a game
appended without registering.

Verified by driving every game twice in a row headlessly: canvas count, game-UI
child count and the live scope all return to their boot values, with no console
errors.

## Settings

A global settings card (home screen, or from the pause overlay) controls:

- **Volume** — a master multiplier applied on top of every per-sound gain.
- **Graphics** — caps the canvas backing-store scale (`low` pins device pixel
  ratio to 1, which is the single biggest win on a fill-rate bound device).
- **Vibration** — gates `A.vibrate` on phones.

`Esc` pauses a running game, or closes an open card if one is showing. A paused
game keeps painting but stops updating, and its input is dropped so it cannot
lurch the moment the overlay is dismissed.
