# Pin Rescue - blind round 2 (ours = A, reference wins, clear)
Reference wins composition, rendering, HUD, props, effects, polish; OURS wins readability (narrowly); palette tie.
"A uses the art kit like a template, B like a shipped game."
Gaps (ours):
1. Same level shape in every frame: one centred bottle, pin on the right, horizontal pins only. Target: >= 4 container shapes in levels 1-10; slanted walls, peg fields, tilted/offset/asymmetric containers; pins at 0, 45 and 90 deg; handles on 3 different sides. The review frames must show this variety.
2. Balls read too small and flat (critic measured ~0.01 sw - check the actual on-screen size in the frames you capture). Target: ~0.025 sw diameter, radial gradient base -> 20% darker, white highlight dot (#FFFFFF 70%, 30% of ball size) top-left, 1px darker rim.
3. Bombs have no payoff in frames. Target: 0.35 sw burst of 15-20 puffs #C81E1E->#FF6B5B over 0.4 s, 6px shake for 150 ms, balls caught turning grey #BDBDBD. Make sure a blast is captured.
4. Balls start lattice-packed in rectangular blocks (looks procedural). Target: ~200 balls not 500+, random offset +/-0.3 ball width, uneven top edge, restitution ~0.3 so the stream scatters.
5. Thin HUD: add 3 meta icons (collection, skins, achievements) ~0.07 sw with #E53935 notification dots; bar full width minus 0.04 sw margin. (They should open real screens or at least real panels.)
6. Weak tutorial hand: ~0.2 sw shaded hand with cuff and soft shadow gripping the pin ring, animated 0.3 sw drag in the pull direction, loop 1.2 s.
Keep: win screen ("LEVEL N COMPLETED!" + confetti + green NEXT); dense satisfying pour; yellow hint bulb; instant puzzle clarity.
