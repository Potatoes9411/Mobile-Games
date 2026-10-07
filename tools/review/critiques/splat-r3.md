# Roller Splat - blind round 3, confirmation vs different plates (ours = A, reference wins, clear)
After the depth pass the board shrank: reference wins composition, rendering, props, effects, polish; OURS wins HUD and readability; palette tie.
Gaps (ours):
1. Board scale/camera: our board is a short slab ~35% of screen height with dead background above and below. Target: board >= 55% of screen height, top edge ~22% from top, camera pitch ~55 deg (far edge ~85% of near-edge width). The steeper tilt must NOT shrink the board.
2. Flat lighting: two flat tones on walls/sides. Target: three-tone wall shading (top, lit side, shadow side 30% darker), AO at wall bases (#000 @15%, 4px blur), soft board drop shadow (#000 @20%, offset 2% sw).
3. Sticker props: flat vector blobs crowding the edges. Target: 3-5 lit 3D props per theme, scaled 0.5-1.5x, cropped by the frame, gradient shading, cast shadows on the ground plane.
4. Ball presence: ~2.5% sw and matte. Target: ~5% sw, white specular dot at 25% of diameter, contact shadow, squash 1.2x0.8 for 80 ms on wall impact.
5. Level-complete payoff: confetti small, banner flat. Target: 150+ squares at 1.5-3% sw bursting from behind the banner, banner rotated -8 deg over an ink splat ~70% sw.
6. Level silhouettes all rectangular grids. Target: from level 10, non-rectangular layouts, diagonal staircase runs, floating path networks without a board; painted wall faces 25% darker than paint top (#FF2D7A top, #C2185B sides).
Keep: complete HUD (900-weight outlined title ~45% sw, pause, progress bar with nodes), swipe hand tutorial, theme-matched board rims, crisp painted/unpainted contrast.
