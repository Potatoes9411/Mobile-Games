# Mob Clash - blind round 2 (ours = A, reference wins, clear; was overwhelming)
Reference wins composition, rendering, palette, props, effects, polish; OURS wins HUD; readability tie.
"A looks like a tidy vertical slice: its UI is clean but the world is sparse and its crowds are small."
Gaps (ours):
1. Crowd scale: ~40-80 figures vs 500+. Target: 300+ units per side, the red mass covering ~70% of the lane width (#E8231E), blue blob filling the lane. Use cached sprites and cheap per-unit draw.
2. Empty lower screen (y 55-100% bare grey road). Target: gate rows from ~60% sh, side rails of stacked +1 gates (each ~0.12 sw), lane-edge decals, the cannon squad down there.
3. Lighting/materials: Target: 45 deg key light, soft shadow under every figure, rim light #BFE6FF on blue units, glossy specular, textured ground.
4. Boss presence (~0.35 sw block figure). Target: boss 0.6-0.8 sw at hero scale, camera push-in, HP bar 0.5 sw.
5. Impact effects (grey puff). Target: 0.25 sw white core flash #FFFFFF->#FFD23F, 8-12 debris chunks, short shake, outlined damage numbers ~0.08 sw.
6. Cannon progression: always one small cannon. Target: visible cannon fleet growing 1->6+ across upgrades, army footprint 0.15->0.5 sw.
Keep: full HUD with BOSS tag, pause, fire meter; HOLD TO FIRE tutorial; VICTORY panel with reward breakdown and UPGRADES badge; gate colour coding; CASTLE DOWN!/CHAMPION! callouts.
