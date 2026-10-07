# Pin Rescue - blind round 1 (ours = A, reference wins, clear)

"Same format, same grey stage, same rainbow balls. B renders it as a real 3D product; A is a flat 2D vector version."
Scorecard: reference wins composition, rendering, HUD (slightly), props, readability, polish; OURS wins effects/juice (only build with a win beat); palette tie.

Gaps (ours), ranked:
1. Flat containers: hairline outlines with pale fill. Target: wall thickness ~0.035 sw rounded glass tubes, body #E6E7EC, top-left highlight stripe #FFFFFF at 0.3 of wall width, drop shadow #B5B7C0 @40% offset 0.008 sw.
2. Hairline pins (1-2px line, tiny ring). Target: cylindrical rod ~0.018 sw thick #2F7DE1 with highlight #7DB8FF, chunky ring handle outer diameter ~0.075 sw, stroke 0.015 sw; extends ~0.1 sw while dragged.
3. Small timid playfield (~0.47 sw centred, dead grey sides). Target: container bounds 0.65-0.75 sw wide, ~0.55 sh tall; varied silhouettes - tilted walls, U-bends, crossing pins, asymmetric shapes.
4. Pinprick balls (~0.01 sw flat dots). Target: ~0.02 sw diameter, radial shade base -> 30% darker, white specular dot upper-left. Keep the high ball count (pour mass is a strength) - render bigger.
5. No visible hazards: bombs only appear as a red cloud. Target: visible bomb sphere ~0.08 sw #2A2A2E with specular #8C8C94, brown fuse 0.03 sw, flickering spark #FFC83A.
6. Flat cup and tutorial hand: thin cup rim; flat pale hand cropped at right edge. Target: torus rim ~0.045 sw tall with darker underside, inner glow in rim colour @25%; shaded 3D-ish hand ~0.3 sw tall with cuff, fully on screen, dragging along the pin axis.
Also: bolder HUD icon set (ours are thin hairline glyphs).

Keep: high ball density and heavy pour; grey-to-colour infection reads instantly; the level-complete screen (best single frame in either set).
