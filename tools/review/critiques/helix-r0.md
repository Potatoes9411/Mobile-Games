# Helix Drop - blind round 0 (ours = B, reference wins, clear)

Gaps (ours), ranked:
1. No break/smash feedback: passed rings just vanish. Target: 30-50 cube shards per ring ~0.015 sw thrown outward fading over 0.4 s, 0.3 s white flash on the ring, 4px shake.
2. No fireball/combo state. Target: after 3+ consecutive rings, flame trail ~0.25 sw (#FFD200->#FF4A00), ball tinted #FF6A00, combo words ("WOW!", "GODLIKE!") ~0.08 sw tall #7CFF3A with 3px dark outline, elastic scale-in; stacked "+N" popups.
3. Broken score typography: doubled misregistered glyph ("10" ghosted over "10"), "+10" collides with it; hint pill in a tiny monospace debug-style font. Target: one bold rounded number ~0.12 sw tall, #2B2B2B with #FFFFFF 4px stroke; "+10" offset 0.06 sw below in #FFB300.
4. Single art theme. Target: 4+ palettes rotating every few levels, each with its own column texture and platform material (glossy, sponge, stone), gradient skies; boss levels with spiked rings.
5. Untextured, unlit materials. Target: top-face highlight +15%, sides -35%, soft AO where platforms meet the column, subtle noise texture, red hazard glow pulsing at 1 Hz.
6. No meta/economy UI. Target: coin pill top-right ~0.2 sw, level badge on the progress bar, results screen with chest/reward; skinned balls.

Keep: hazard readability (red #E53935 vs green #3DD65A); clean HUD hierarchy and pause button; fail state (tower fades, "SPIKED!" reads clearly); persistent splat decals.
