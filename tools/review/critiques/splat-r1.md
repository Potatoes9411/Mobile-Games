# Roller Splat - blind round 1 (ours = B, reference wins, CLEAR)

Scorecard: reference wins composition, rendering, effects, readability, polish; ours wins HUD (progress bar, pause, swipe hint); tie palette and props.

Gaps (ours), ranked:
1. Flat camera, no wall depth after level 1: in later worlds (frames 4-5, yellow/green) the maze is cut straight into the background. Target: ~25 deg pitch, walls 0.35 tile tall, wall tops e.g. #0AA0F5 with side faces #0770B8, soft shadow 30% black offset 0.02 sw. EVERY world must have the extruded look.
2. Noisy ragged maze shapes (frames 3-4 look like random noise with single-cell nubs; hard to tell walls from unpainted floor). Target: 9-13 columns, corridors exactly one tile wide, walls in straight runs >= 2 tiles, <= 2 isolated single-cell nubs per level; layouts that read as deliberate shapes/symbols. Consider a library of hand-authored or shape-templated levels.
3. No board slab in later levels. Target: maze always on a coloured slab ~0.78 sw wide with a 0.035 sw contrasting rim and a 0.02 sw shadow.
4. Weak paint material. Target: painted tiles get a 2px inner edge 15% darker; 20-30 sparkle particles over the last 3 tiles fading in 0.4 s.
5. Small win celebration (splat small, confetti clustered at top). Target: banner ~0.8 sw on a 2-tone splat tilted -8 deg, 150+ confetti squares 0.01-0.025 sw spread over the full height, board pulse to 1.05.
6. Dated typography (condensed pixel font with heavy outline). Target: rounded bold sans, white, cap ~0.05 sw, soft 25% shadow, outline < 2px. (Engine font upgrade in progress.)

Keep: level progress bar with numbered nodes; pause; swipe-to-roll hint with hand cue; glossy prop highlights.
