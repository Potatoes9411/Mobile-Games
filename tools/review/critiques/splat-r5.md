# Roller Splat - blind round 5 vs plate set 1 after r4 changes (ours = B, reference wins, SLIGHT)
Reference wins composition, rendering, palette (narrowly), props, effects, polish; OURS wins HUD and readability.
The round-4 board (big, near top-down) beat set 2 but lost to set 1's steep, art-directed look. Both must hold at once.
Gaps (ours):
1. Flat camera: near top-down. Target: steep tilt ~35-40 deg from vertical, board top edge ~0.70x the bottom edge width, board ~0.75-0.85 sw - AND still filling >= 55% of screen height (set 2's demand).
2. Walls lack height and shading: low, evenly lit. Target: wall height ~0.6x tile width, side faces 30% darker than top (e.g. top #3AA0FF, side #1F6FD0), floor occlusion gradient at wall bases (#000 @18% fading over half a tile).
3. Hard dark-blue outline around the board looks like clip-art. Drop it; use a thick shaded plinth 0.04 sw deep plus soft drop shadow (#000 @20%, 0.03 sw blur).
4. Thin scene dressing (white level nearly empty; props cut off at edges). Target: 3-5 large props per level fully in frame, 0.25-0.4 sw, clay-shaded with rim light, at least one crossing behind the board for depth.
5. Flat paint: add speckled grainy paint texture (+/-8% value noise); glossy specular ball ~0.05 sw.
6. Win screen: ~150 confetti in 5 hues; splat banner 0.85 sw, -8 deg, irregular paint drips.
Keep: progress bar with nodes, pause, bold outlined title, swipe hint, large board footprint, strong unpainted/wall contrast.
Flip: steep tilt + tall shaded walls with floor AO + no hard outline.
