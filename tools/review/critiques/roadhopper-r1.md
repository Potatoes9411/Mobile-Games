# Road Hopper - blind round 1 (ours = B, reference wins, clear; was overwhelming)
Reference wins composition, rendering, palette, props (overwhelmingly), effects, polish; OURS wins HUD and readability.
Gaps (ours):
1. Empty lanes: 2-4 props per screen vs 15-25. Target: 6-10 props per visible grass/sand band - trees with 2-3 voxel canopy tiers, bushes, fences, flowers, rocks; 0.04-0.08 sw footprints, varied heights. Keep the player readable.
2. Flat lighting: uniform ground colours. Target: per-tile brightness jitter +/-4-6%, AO where props meet ground (-25%), directional drop shadows offset ~0.03 sw at 35%, biome light accent (sun shafts, warm/cool overlay).
3. Vehicles: two-colour boxes. Target: 4-6 colour components per vehicle (cab, windows #2A3550, wheels, trim stripe, lights) plus one exaggerated "hero" hazard per biome >= 0.4 sw (plane, shark...).
4. Biome identity: snow and desert reuse assets. Target: each biome its own lane-hazard mechanic and 5+ unique props (snow: frozen river, sled, pines with #F4F8FF caps).
5. Effects: Target: 6-10 puffs on every hop landing, splash rings on logs, coin sparkle (4-point star #FFE14D), squash/stretch 0.85/1.15.
6. Ground texture: gravel specks on ballast, cracked kerb tiles, 3-5 grass tufts per tile row, #8FD6FF foam where water meets logs.
Keep: player readability; "TOP n" label; results screen with NEW TOP and gift meter; red glowing rail lights; rounded typography.
