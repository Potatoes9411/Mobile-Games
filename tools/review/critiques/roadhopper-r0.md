# Road Hopper - blind round 0 (ours = B, reference wins, overwhelming)

Scorecard: reference wins composition, rendering, palette, HUD, characters/props, effects, polish; readability narrowly. Ours "looks like a weekend prototype".

Gaps (ours), ranked:
1. Flat, prop-free world. Bottom grass and lane edges are empty checkerboard; reference packs 8-12 distinct props per screen. Target: 6+ prop types per biome (rocks, flowers, fences, bushes, stumps) at 0.05-0.10 screen width, 2-3 per grass row; 1-voxel bevel highlight (+15% value on top face). Camera too far back: a third of each frame is empty grass.
2. Box-primitive vehicles (grey box + coloured cab). Target: 4+ vehicle styles - truck ~0.30 sw, car, bus, taxi - with wheels, windows #2A3550, bumpers, headlights.
3. Dead death screen: dark overlay + "FLATTENED/SPLASH" + score, no motion; consecutive frames identical. Target: squashed-chicken pancake / water splash plume of 12-20 white voxel particles, 150 ms shake, rounded card ~0.8 sw, big retry button 0.35 sw #4CD964, score pop-in 1.3x.
4. Broken HUD: coin counter hidden behind the pause button. Target: coin icon + digits ~0.07 sw tall, yellow #FFD400 with 3px black outline, left of pause with 0.03 sw gap; "TOP" >= 0.035 sw with outline.
5. Single biome, monotone acid-lime palette. Target: grass #7ED957/#6FCB4A, warm-tinted shadows #3B5A2A @35%; 3+ biome palettes changing ground, water and hazards.
6. Tiny generic hero (~0.06 sw). Target: chicken ~0.09 sw, hop squash-stretch 0.8/1.2, landing dust puff of 4 cubes; collectible character roster.

Keep: calm lane legibility (road/rail/river/grass separate cleanly); crossing-signal pole is a clear early hazard tell.
