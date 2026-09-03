# Pocket Arcade

Three complete games behind one hub, sharing an engine, a progression layer and a
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
  games (Horde Arena at 2, Rooftop Run at 3).
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

### Mob Clash: Gate Siege
Swerve a crowd down a track, gamble it through `+ − × ÷` gates, then spend the
horde storming a keep one room at a time. Rooms unlock floor by floor and
conquering one absorbs its defenders. The pacing generator is documented in
[`PACING_MATH.md`](PACING_MATH.md) — it simulates a reference run and proves every
tower it produces is solvable before shipping it.

### Horde Arena
A survivor-style auto-battler. You only steer; weapons fire themselves. Kills drop
XP, XP levels you mid-run, and every level you draft one of three upgrades from a
pool of six weapons and eight perks. Four weapons **evolve** when carried to max
alongside a specific perk — Blaster + Haste becomes Storm Repeater, Arc Chain +
Focus becomes Tesla Cascade. Twenty waves, a boss every fifth.

### Rooftop Run
Three-lane endless runner. Swipe across to switch lanes, up to vault, down to
slide; each obstacle carries a chevron telling you which. Speed climbs with
distance. Magnet, shield and boost pickups stack, and the shop turns coins into
longer power-ups, a starting shield and a head start.

---

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
