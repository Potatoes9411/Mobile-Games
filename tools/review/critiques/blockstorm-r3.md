# Block Storm - blind round 3, confirmation vs different plates (ours = B, reference wins, SLIGHT)
Reference wins composition, rendering, props, effects (narrowly), readability, polish; ours wins HUD (goal panel, combo pill, PLAY AGAIN); palette tie.
"B looks like a finished game in places, but its layering and scaling mistakes give it away."
Gaps (ours):
1. End screen buried in confetti: confetti covers crown, best and score (top 20%); score dimmed grey. Target: confetti BEHIND the HUD, <= 60 pieces, score solid #FFFFFF.
2. "EXCELLENT!" fills 100% of the board width, touching both edges. Target: cap at 0.70 sw centred, 1-cell gap from board edges, scale 0.6->1.1->1.0 over 250 ms.
3. Line clear is mush: cells blur into translucent purple squares. Target: sharp 1-cell beam in a light tint of the cleared colour, cells pop to 1.2x and fade over 180 ms, 6-8 bright shards per cell.
4. Tray scale inconsistent: the 5-long piece is drawn smaller than others. Target: every tray cell the same size (~0.045 sw) regardless of piece length (slot width must allow 5 cells).
5. Dragged piece can hang past the board edge. Target: piece ~1.5 cells above the finger, clamped so it never extends past the board.
6. Opening frame is an empty navy board. Target: start runs with 6-10 pre-placed blocks or a 400 ms fill-in animation.
Keep: goal panel with star/gem bars, COMBO pill, floating +N popup, green PLAY AGAIN.
Flip: fix the celebration layer (confetti behind HUD, praise text <= 0.70 sw, sharp beam-and-shatter clear).
