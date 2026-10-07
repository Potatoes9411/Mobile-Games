# Mob Clash - blind round 1 (ours = A, reference wins, overwhelming)

Scorecard: reference wins composition, rendering, palette, props, effects, polish; ours wins readability slightly; HUD tie leaning ours.
"A has clean UI and a readable layout, but the game underneath is flat, sparse and procedural-looking."

Gaps (ours), ranked:
1. Crowd scale and mass: a thin trickle of ~0.02 sw blobs, < 150 units. Target: units ~0.035 sw, 300+ on screen, fill #2F8BFF with highlight #9FD4FF, a blob shadow under each unit (30% black), packed formation that fills the lane.
2. Lighting/materials: flat unlit fills. Target: 45 deg directional light, AO under castle/gates/cannon, rim light on units, road texture with cracks and lane decals.
3. Boss/enemy presence: red pancake mob, small yellow brute. Target: hero-sized boss 0.4-0.5 sw with idle bob, white hit-flash, HP bar 0.3 sw above its head; enemy crowd a solid #E8302A carpet running down the lane.
4. Camera/depth: high and flat; bottom 35% dead. Target: lower chase camera (~35 deg), cannon at ~80% sh, lane converging to a vanishing point at ~10% sh. Varied track geometry/environments (tunnel, desert, concrete) instead of one snowy field.
5. Gate design: thin translucent slabs ~0.12 sw. Target: gates ~0.3 sw x 0.08 sh, bevelled 3D frame #1E5BD8, fill gradient from #6FB8FF @60% alpha, magenta #D23CE0 for strong multipliers, numerals ~0.08 sw; stacked +1 ladders on the side.
6. Impact VFX: tiny pixel puffs. Target: 12-20 debris chunks per hit, white flash ring 0.15 sw, 4-frame shake, dust cloud #D9CFC4 on castle collapse.

Keep: clear multiplier labels; clean pause/level/boss header; tidy victory panel with NEXT LEVEL and UPGRADES; fire-charge meter; red negative gates; "CASTLE DOWN!" callout.
