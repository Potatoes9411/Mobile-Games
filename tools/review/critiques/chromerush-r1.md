# Chrome Rush - blind round 1 (ours = A, reference wins, clear; was overwhelming)
Reference wins composition, rendering, characters, effects, polish; OURS wins HUD and readability; palette tie.
"It reads like a chart game with a meta loop. A reads like a well-built single loop."
Gaps (ours):
1. Player car model: flat rigid block ~0.45 sw, no visible tyres. Target: ~0.35 sw beveled toy car, black tyres #1E1E24 visible, roof highlight #FF6B6B over #E3303A body, 12 deg yaw + 8 deg roll on each lane change easing over 0.15 s.
2. KO payoff is a muddle of grey cracks, smoke and gold text; you cannot see what happened. Target: 1.6x zoom over 0.3 s onto the wreck, car tumbles 540 deg, <= 8 radial cracks from the impact point, KO text ~0.6 sw #FFC21A with #8A4B00 3D extrusion placed ABOVE the wreck, not over it.
3. Static camera (horizon fixed ~0.42 sh). Target: lateral camera lag ~0.05 sw toward steer, +8 deg FOV kick and slight drop on nitro, 4-6px shake on near-miss.
4. Crowded top HUD (0.3 sw distance number, BEST, progress bar and coins stack; near-miss text over scenery). Target: distance number ~0.18 sw, BEST only at run start, near-miss popups anchored ~0.1 sh above the player car with #2A0A3A shadow and 0.6 s fade.
5. Traffic personality: generic interchangeable emoji bubbles ~0.1 sw. Target: bubbles 0.13 sw with 4px white ring, 1 in 5 a unique portrait or text line ("&$!#%"), emotion swaps on near-miss.
6. Hazards/variety: Target: >= 8 traffic models per run, cone clusters (0.06 sw, 2-3), pile-up fireballs #FFB000->#FF5A00 with 12 smoke puffs, 4x6 garage grid.
Keep: full in-run HUD (coins, near-miss xN with +coin popups, nitro charge ring); coin trails as lane guidance; checkered checkpoint arch; rich roadside dressing per biome.
