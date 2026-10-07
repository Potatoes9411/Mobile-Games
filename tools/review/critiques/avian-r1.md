# Avian Artillery - blind round 1 (ours = B, reference wins, overwhelming)
Reference wins composition, rendering, palette, HUD, characters, effects, polish; OURS wins readability.
"Next to A, B looks like a competent prototype."
Gaps (ours):
1. Structures too small and sparse: 2-3 blocks high, ~12% sw, in the lower 35% with empty sky above. Target: 2-3 structures per level, each 25-45% of screen height, 15-30 pieces mixing wood, stone and glass/ice; multi-level fortresses, floating islands, several depths; camera framing 2-3 structures.
2. Flat rendering (one fill per block). Target: top-left highlight (+20% luminance), 2px dark outline (#3A2410 on wood), contact shadow under every block, wood grain / stone crack texture.
3. Empty low-depth background (sky gradient + 3 clip-art clouds). Target: 4 parallax layers (far silhouettes 30% saturation, mid rocks/trees, near foliage framing the edges 8-12% sw), light shafts or haze; per-level colour keys (teal jungle, molten red/orange, cold navy storm).
4. Weak characters: pigs ~3% sw blank green circles. Target: pigs 5-7% sw with snouts, brows, helmet variants; boss pig ~15% sw with crown; bird on slingshot >= 4% sw with angry brow and expressive face.
5. Thin impact effects. Target: 12-20 debris particles per break coloured by material, smoke puff #E8E0D0 alpha 0.6 for 0.4 s, 4-6px shake for 120 ms, 2-frame white hit-flash, splinters.
6. Underweight HUD: "LEVEL 1" ~1% sh. Target: pause 8% sw with 3px bevel (#F6A623->#C46A00) and drop shadow; score in bold display font ~5% sh with dark stroke; bird cards ~9% sw with portrait, frame and count badge.
Keep: dotted trajectory preview and legible playfield; level-cleared screen (stars, New highscore, Next); floating score numbers (bigger, bouncier).
