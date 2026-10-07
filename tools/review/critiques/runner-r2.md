# Rooftop Run - blind round 2, both plate sets (ours lost both, clear)
Reference wins composition, rendering (by a lot), palette, characters (overwhelmingly), effects, polish; HUD tie; ours wins readability on one set.
"A competent flat-vector clone ... looks like flat vector clip-art on a 3D track."
Gaps (ours), merged from both critics:
1. Trains/obstacles are flat-shaded rounded slabs. Target: bevel every edge (~2% sw), top rim highlight #FFF4D6 @60%, contact-shadow strip under each car #000 @35%, >= 5 detail elements per car face (windows, doors, panel lines, roof vents, grime decals), headlights #FFF4C2 with ~0.04 sw bloom.
2. Hero: small, faceless, oversized helmet, stiff limbs; shrinks to ~0.08 sw in some frames. Target: steady 0.18-0.3 sw, visible face on turn/jump frames, 3 tonal steps per material + 2px dark outline #2A1A3A, bright hero colour (e.g. #E8302A cap with white outline), clear arm swing over 4+ key poses, chunky hoverboard.
3. Repeated scenery: identical window grids and the same bunting gantry in every frame. Target: 2-3 hero landmarks per world 15-25% sw (taller than 0.4 sh occasionally) every 3-4 s, >= 6 facade modules per biome, the gantry at most once per 3 screens, no element repeated within visible depth.
4. Lighting/depth: no light direction. Target: one key light per world (warm #FFC27A side, cool #5B4B8A shadow), 3-step ramp per material, rim #FFD9A0, sky-tinted distance fog over the far 25% of track (foreground full saturation, background ~60%), contact shadow under the player (~0.25 sw, 30%).
5. Palette discipline: pastel-on-pastel, beige western frames. Target: 3-colour key per world + one complementary accent (e.g. terracotta #D9663A, sky #7FC8F0, teal #2BB3A3); trains >= 40% luminance contrast with the ground.
6. Vertical layer + speed: rooftop running must be visible in captures with ramps; camera lifts ~8% sh on roofs; +8 deg FOV kick on boost, speed streaks in the outer 10%, coin spin with glint 1.5x coin size.
Also: barrier was drawn over the airborne player (ambiguous collision) - sort the player above obstacles while airborne, show a height shadow, raise the jump arc >= 0.1 sh. Lower camera: horizon ~35% sh.
Keep: power-up timer bars, results card, guard-and-dog chase framing, sparkle pickup ring, lane readability and hazard stripes.
