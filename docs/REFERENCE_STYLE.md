# Reference style sheet

Every game in the arcade is an original title modeled on a format that already
charts. This sheet records what those reference games **actually look like**,
measured from their official App Store screenshots, so art decisions stop being
guesses.

**Nothing here is a copied asset.** The screenshots are copyrighted; they are
fetched to a gitignored folder, looked at, and measured. Only the measurements
and the observations are committed. Regenerate the image pools locally with:

```
python3 tools/refs/fetch_refs.py   # official store screenshots -> tools/refs/plates/<game>/
python3 tools/refs/analyse.py      # contact sheets + tools/refs/palette.json
```

138 screenshots across 12 titles. Store screenshots are the publisher's own
choice of what to show, which makes them the most honest single statement of
the intended look. Several are framed with marketing banners (EAT!, RELAXING!);
those skew the palette numbers and are called out where they matter.

## How to read the numbers

- **Mean saturation / value** — pixel-weighted averages across every plate.
- **Vivid pixels** — share of pixels with saturation above 0.45 *and* value above
  0.6. This is the single number that best separates finished hypercasual art
  from placeholder art: placeholder art is dark or grey, finished art is not.
- **Palette** — dominant colours by pixel share, merged across all plates. The
  big fields come first on purpose: they set the overall look far more than any
  small bright accent does.

For comparison, the first native Road Hopper measured **value 0.68 /
saturation 0.55 on grass and 0.35 / 0.18 on road**.

---

## Road Hopper ← Crossy Road (Hipster Whale) · 20 plates

| mean saturation **0.47** · mean value **0.64** · vivid pixels **30%** |
|---|
| `#505470` 9% `#29222D` 7% `#3E4756` 7% `#EDF4F0` 6% `#2FA1FA` 5% `#727D84` 4% |

The palette mixes ten themed worlds (jungle, city, underwater, farm, arctic,
castle, desert, Halloween), so it averages lower than any single world.

**Camera.** The whole world is rotated: lanes run **downhill to the right at
about 18 degrees**, not horizontally. Boxes show three faces - a light top, a
mid-tone left face and a dark right face. *Ours has horizontal lanes with a
shear, which is the single biggest reason it does not read as the real thing.*

**Shadows are hard.** Every prop casts a crisp, flat, dark parallelogram offset
**down and to the right**, about 30% darker than the ground under it. *Ours uses
soft radial blobs, which is the wrong idiom for this style.*

**Density and scale.** Grass rows carry several props each; trees and bushes are
stacked voxel blocks of varied height. Cars nearly fill a lane and trucks run
three to four tiles. The character is small - roughly half a tile.

**HUD.** Score top-left in huge blocky white digits with a thick black outline.
Coins top-right in the same treatment in yellow, with a small red coin glyph.
Pause as a plain `II` under the coins. Nothing else on screen.

**Rails.** Purple-grey rails over brown sleepers; a red-and-white striped signal
post with a black crossbar and a light that glows red before a train.

## Void Muncher ← Hole.io (Voodoo) · 14 plates

| mean saturation **0.48** · mean value **0.76** · vivid pixels **48%** |
|---|
| `#87718B` 9% `#6059B4` 7% `#332F68` 6% `#C5BAC1` 5% `#394C8F` 4% `#5D76BC` 4% |

Heavily framed in purple marketing banners, which inflate the purple share.

**Palette is bright and pastel.** Light lavender-grey roads with white
crosswalks; buildings in pink, peach, lavender and pale blue with rows of
windows and visible rooftops. *Ours is a dark navy district with saturated
primary-coloured boxes - the opposite end of the value range.*

**The hole has a thick coloured rim** - a saturated ring with a highlight that
reads as a 3D lip, not a thin stroke. Skins change the rim's shape entirely.

**Camera** is a steep three-quarter view; buildings show real side walls.

**HUD.** A timer pill with a clock glyph at the top, a yellow progress bar with a
percentage under it, and a name, level and small score bar floating above each
hole.

## Mob Clash ← Mob Control (Voodoo) · 10 plates

| mean saturation **0.45** · mean value **0.75** · vivid pixels **41%** |
|---|
| `#9B8892` 9% `#37345B` 6% `#B3B7C5` 4% `#EDCD9D` 4% `#4D36A4` 3% `#6D4AD4` 3% |

**Crowds are dense blobs** of bright blue capsule people against red enemies -
the colour contrast *is* the readability. Gates are translucent blue or purple
panels carrying large white `x3` / `+1` text. The track is light concrete grey
on a pale background. Gem counter top-left and coin counter top-right, each in a
rounded pill.

## Helix Drop ← Helix Jump (Voodoo) · 12 plates

| mean saturation **0.55** · mean value **0.75** · vivid pixels **46%** |
|---|
| `#E4F2F3` 10% `#010002` 7% `#737075` 5% `#56E8FD` 5% `#02EBFE` 4% `#DFCDB9` 4% |

**Light scene, dark hazards.** A thick white or light-grey cylindrical pillar on
a pale pastel sky, often with a city silhouette. Each level uses **one theme
colour** for its platforms (green, yellow, pink); hazard wedges are a brighter
red. *Ours is a dark teal scene with teal platforms - inverted.*

**Paint splats.** The ball leaves colour splatter decals on every platform it
bounces on, which is most of the game's sense of motion.

**HUD.** Big score at the top; a level progress bar between two numbered
circles (current and next level).

## Block Storm ← Block Blast (Hungry Studio) · 12 plates

| mean saturation **0.62** · mean value **0.69** · vivid pixels **76%** |
|---|
| `#4B64B8` 62% `#242B54` 15% `#314E9B` 3% `#ADD67A` 2% `#9948B0` 2% `#6474AB` 1% |

**Background is a flat medium blue**, 62% of all pixels - not dark purple. The
board is an inset darker navy with thin grid lines.

**Blocks are sharp-cornered squares** with a classic bevel: lighter top-left
edge, darker bottom-right edge, fully saturated primaries. *Ours are rounded
with a cap highlight, which reads as a different game.*

**Tray.** Three pieces at reduced scale below the board, **no slot boxes**.

**HUD.** Large white score centred at the top; a crown and best score in gold at
top-left; a settings gear top-right. Praise text (`Amazing!`, `Combo 6`) in
italic gold with a glow.

## Roller Splat ← Roller Splat! (Voodoo) · 12 plates

| mean saturation **0.60** · mean value **0.93** · vivid pixels **74%** |
|---|
| `#EBDF52` 22% `#00E326` 13% `#F8FCFE` 12% `#64E140` 6% `#CCDDF0` 5% `#409CFE` 5% |

**Solid bright background, one colour per level** - blue, yellow, green or near
white. The maze is a **raised white slab tilted in 3D**, walls pale blue-grey,
and the painted path one vivid colour. Floating decorative 3D shapes drift in the
background. `LEVEL N` in white at the top. *Ours is a dark purple flat grid.*

## Paper Territory ← Paper.io 2 (Voodoo) · 10 plates

| mean saturation **0.46** · mean value **0.95** · vivid pixels **45%** |
|---|
| `#E9FDF5` 29% `#FED000` 9% `#FEE740` 5% `#FFC30F` 4% `#F8E8EF` 4% `#F8FAD1` 4% |

**Near-white mint ground** (`#E9FDF5`, 29% of pixels). Territories are **soft
pastel blobs with rounded organic edges**; trails are soft and translucent.
Players are cute cube mascots with name labels. A thin progress bar with a
percentage runs along the top.

## Pin Rescue ← Pull the Pin (Popcore) · 10 plates

| mean saturation **0.06** · mean value **0.82** · vivid pixels **6%** |
|---|
| `#D7D7D7` 52% `#BEBFBE` 29% `#F7F7F7` 11% `#A78377` 6% `#A55C50` 2% `#E8E7E7` 1% |

**A bright studio backdrop** - light grey with a soft radial falloff, near white
at the centre. Glass tubes drawn as white rounded outlines, blue pins with ring
handles, confetti-coloured balls, a glass cup with a coloured rim and a
percentage pill beneath it. Level number and tool icons in a white rounded bar at
the top.

## Rooftop Run ← Subway Surfers (SYBO) · 10 plates

| mean saturation **0.46** · mean value **0.69** · vivid pixels **39%** |
|---|
| `#958783` 8% `#B16E5E` 6% `#DA906D` 5% `#514A4D` 5% `#B75246` 5% `#502F45` 4% |

Behind-the-runner 3D camera down three lanes of track; saturated warm
environments, red and white trains, rails, gold coins in lines. Pause top-left
in a blue rounded square; multiplier and score top-right in a dark translucent
pill, coins beneath.

## Horde Arena ← Survivor!.io (Habby) · 10 plates

| mean saturation **0.42** · mean value **0.53** · vivid pixels **25%** |
|---|
| `#26262C` 17% `#3B414F` 13% `#100D10` 8% `#6E737A` 7% `#535864` 6% `#FD7C01` 5% |

**Comic style: thick black outlines on everything.** Chunky cartoon characters,
a grey tiled floor, dense enemy swarms, and UI panels with heavy outlines and
yellow headers. The outline weight is the signature - without it the same
shapes read as generic.

## Chrome Rush ← Dashy Crashy GO! (:DUMPLING design) · 8 plates

| mean saturation **0.51** · mean value **0.80** · vivid pixels **43%** |
|---|
| `#F5F4F5` 13% `#FE9416` 10% `#C1C6EA` 6% `#9496A8` 5% `#2D0E7C` 4% `#C0BDBA` 4% |

**Chase camera, not top-down.** The view sits behind a chunky toy-like car on a
lavender road, under vivid sky gradients (purple night, red sunset, blue day)
with a city skyline. A thin progress bar with a car icon runs along the top.
*Ours is top-down, which is a different game.*

## Avian Artillery ← Angry Birds 2 (Rovio) · 10 plates

| mean saturation **0.54** · mean value **0.64** · vivid pixels **30%** |
|---|
| `#711522` 10% `#183446` 4% `#134763` 4% `#5770B5` 4% `#0C3531` 4% `#2A4755` 4% |

Landscape, lush painterly backgrounds, structures of wood, ice and stone, green
pigs. The most detailed art in the set and the hardest to match with flat
shading; ours is a deliberately simplified take.

---

## What the plates agree on

Across all twelve, finished art has these in common:

1. **Bright scenes.** Mean value sits at 0.64 or above in every title except the
   comic-style Survivor!.io. Dark backgrounds are the exception, not the rule.
2. **One dominant background field.** Block Blast is 62% one blue, Paper.io 2 is
   29% one mint, Roller Splat is one flat colour per level.
3. **Saturated accents on a calmer field** - the player, the hazards, the
   collectibles are the vivid parts; the ground is not competing with them.
4. **Chunky, outlined HUD numerals** at the top corners, with very little else.
