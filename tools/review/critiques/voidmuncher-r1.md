# Void Muncher - blind round 1 (ours = B, reference wins, clear; was overwhelming)
Reference wins composition, rendering (overwhelming), palette, props, effects, polish; OURS wins HUD and readability.
Gaps (ours):
1. Flat lighting: no cast shadows or depth. Target: key light upper-left, cast shadows 35% #2A2350, AO darkening the bottom 10% of each building, warm rim on roof edges #FFE3B0.
2. Hole has no drama (blue ring + near-black disc + faint swirl). Target: rim 6% of diameter, interior radial #0B0420->#3A1A7A, animated 3-arm spiral, outer bloom #8F5BFF reaching 20% past the rim; one themed skin per rival (lava, lightning vortex...).
3. Swallowing juice not visible: objects just overlap the hole in frames. Target: objects tilt 30 deg into the hole and shrink over 0.3 s, 8-12 debris chunks, dust ring 1.3x object width, coin flying to the HUD bar. Make sure frames capture it.
4. Empty streets: asphalt ~50% of screen, ~10 props. Target: 25-40 props per screen, rooftop clutter on every building (2-4 AC units/tanks/antennas), roads <= 35% of the screen.
5. Muddy palette: grey-lavender roads #8A86B8 dominate. Target: roads #6E6A9E, parks #6BCB4B, water #3ED6D0, warm facade accents #F2A65A / #E8584F on >= 30% of buildings.
6. Camera: high, nearly top-down; zooms out until objects are ~1% sw. Target: tilt ~15 deg lower with slight perspective, hole stays 25-35% sw at every level (zoom out slower); one more themed map (harbour or desert).
Keep: whole HUD (pause, timer, kills, progress bar, "NOW EAT POLICE CARS!" objective banner), coloured rival holes with readable tags and big +N popups, ranking card with NEXT CITY.
