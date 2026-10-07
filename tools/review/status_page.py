"""Render the programme status page: tools/review/status.json + reviews/latest/*.jpg
-> reviews/status/index.html (self-contained; thumbnails are our own renders)."""
import base64, datetime, html, json, os
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
S = json.load(open(os.path.join(ROOT, "tools/review/status.json")))
PHASE = {"building": ("Builder working", "warn"), "review": ("With critic", "info"),
         "fixing": ("Fixing critic gaps", "warn"), "won": ("Ours wins blind", "good"),
         "todo": ("Not started", "idle")}
ENG = {"done": ("Done", "good"), "building": ("Building", "warn"), "partial": ("Partial", "info"), "todo": ("Not started", "idle")}
e = html.escape

def thumb(gid):
    p = os.path.join(ROOT, "reviews/latest", gid + ".jpg")
    if not os.path.exists(p):
        return '<div class="nothumb">No native frames yet</div>'
    b = base64.b64encode(open(p, "rb").read()).decode()
    return f'<img class="strip" alt="Latest frames of our build" src="data:image/jpeg;base64,{b}">'

games = S["games"]
won = sum(g["phase"] == "won" for g in games)
working = sum(g["phase"] in ("building", "fixing") for g in games)
review = sum(g["phase"] == "review" for g in games)
now = S.get("updated") or datetime.datetime.utcnow().strftime("%d %b %Y, %H:%M UTC")

cards = []
for g in games:
    label, tone = PHASE.get(g["phase"], ("?", "idle"))
    verdict = e(g["verdict"]) if g["verdict"] else "No blind review yet"
    cards.append(f'''
<article class="game">
  <header>
    <div><h3>{e(g["name"])}</h3><p class="ref">vs {e(g["ref"])}</p></div>
    <span class="pill {tone}">{label}</span>
  </header>
  {thumb(g["id"])}
  <dl>
    <div><dt>Last blind verdict</dt><dd>{verdict}{f' <span class="rnd">round {g["round"]}</span>' if g["verdict"] else ""}</dd></div>
    <div><dt>Biggest open gaps</dt><dd>{e(g["gap"])}</dd></div>
  </dl>
</article>''')

eng = "".join(f'<li><span class="pill {ENG[x["state"]][1]}">{ENG[x["state"]][0]}</span><div><strong>{e(x["name"])}</strong><p>{e(x["note"])}</p></div></li>' for x in S["engine"])
log = "".join(f"<li>{e(x)}</li>" for x in reversed(S["log"]))

page = f'''<title>Pocket Arcade Remakes</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Fredoka:wght@500;600&family=IBM+Plex+Sans:wght@400;500;600&family=IBM+Plex+Mono:wght@500&display=swap">
<style>
/* Layout: summary band, then a responsive grid of 12 game cards, then engine track and log side by side. */
:root {{
  --bg: #F2F3F8; --panel: #FFFFFF; --ink: #161A2E; --muted: #5C6384; --line: #DCDFEB; --accent: #2F55F4;
  --good: #137A43; --good-bg: #DDF4E6; --warn: #8A5A00; --warn-bg: #FCEFD2; --info: #2240C4; --info-bg: #E2E8FF;
  --idle: #5C6384; --idle-bg: #ECEEF5;
  --display: "Fredoka", "Trebuchet MS", system-ui, sans-serif;
  --body: "IBM Plex Sans", system-ui, -apple-system, "Segoe UI", sans-serif;
  --mono: "IBM Plex Mono", ui-monospace, Menlo, monospace;
}}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{
  --bg: #0F1120; --panel: #181B30; --ink: #EDEFFA; --muted: #A2A8C6; --line: #2A2F4C; --accent: #7C95FF;
  --good: #6BE29E; --good-bg: #12301F; --warn: #F5C46A; --warn-bg: #352812; --info: #9DB1FF; --info-bg: #1C2550;
  --idle: #A2A8C6; --idle-bg: #232742; color-scheme: dark; }} }}
:root[data-theme="dark"] {{
  --bg: #0F1120; --panel: #181B30; --ink: #EDEFFA; --muted: #A2A8C6; --line: #2A2F4C; --accent: #7C95FF;
  --good: #6BE29E; --good-bg: #12301F; --warn: #F5C46A; --warn-bg: #352812; --info: #9DB1FF; --info-bg: #1C2550;
  --idle: #A2A8C6; --idle-bg: #232742; color-scheme: dark; }}
body {{ background: var(--bg); color: var(--ink); font: 15px/1.5 var(--body); }}
.wrap {{ max-width: 1240px; margin: 0 auto; padding-inline: 20px; padding-block: 28px 48px; display: grid; gap: 28px; }}
h1, h2, h3 {{ font-family: var(--display); font-weight: 600; text-wrap: balance; margin: 0; }}
h1 {{ font-size: clamp(28px, 4vw, 40px); letter-spacing: .01em; }}
h2 {{ font-size: 20px; }}
h3 {{ font-size: 18px; }}
.top {{ display: flex; flex-wrap: wrap; align-items: end; justify-content: space-between; gap: 12px; }}
.top p {{ margin: 4px 0 0; color: var(--muted); max-width: 62ch; }}
.stamp {{ font: 500 12px var(--mono); color: var(--muted); }}
.tally {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; }}
.tally div {{ background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 14px 16px; }}
.tally b {{ display: block; font: 600 30px/1.1 var(--display); font-variant-numeric: tabular-nums; }}
.tally span {{ color: var(--muted); font-size: 13px; }}
.grid {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(min(100%, 360px), 1fr)); gap: 16px; }}
.game {{ background: var(--panel); border: 1px solid var(--line); border-radius: 12px; padding: 16px; display: grid; gap: 12px; align-content: start; min-width: 0; }}
.game header {{ display: flex; justify-content: space-between; align-items: start; gap: 10px; }}
.ref {{ margin: 2px 0 0; color: var(--muted); font-size: 13px; }}
.strip {{ width: 100%; height: auto; border-radius: 6px; display: block; background: #000; }}
.nothumb {{ aspect-ratio: 760 / 390; max-width: 100%; border: 1px dashed var(--line); border-radius: 6px; display: grid; place-items: center; color: var(--muted); font-size: 13px; }}
dl {{ margin: 0; display: grid; gap: 8px; }}
dt {{ font-size: 11px; text-transform: uppercase; letter-spacing: .08em; color: var(--muted); }}
dd {{ margin: 2px 0 0; }}
.rnd {{ font: 500 11px var(--mono); color: var(--muted); }}
.pill {{ display: inline-block; white-space: nowrap; font-size: 12px; font-weight: 600; padding: 3px 9px; border-radius: 999px; }}
.good {{ color: var(--good); background: var(--good-bg); }} .warn {{ color: var(--warn); background: var(--warn-bg); }}
.info {{ color: var(--info); background: var(--info-bg); }} .idle {{ color: var(--idle); background: var(--idle-bg); }}
.lower {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 420px), 1fr)); gap: 16px; }}
.lower section {{ background: var(--panel); border: 1px solid var(--line); border-radius: 12px; padding: 18px; display: grid; gap: 12px; align-content: start; min-width: 0; }}
.eng {{ list-style: none; margin: 0; padding: 0; display: grid; gap: 12px; }}
.eng li {{ display: grid; grid-template-columns: 104px 1fr; gap: 10px; align-items: start; }}
.eng p {{ margin: 2px 0 0; color: var(--muted); font-size: 14px; }}
.log {{ margin: 0; padding-left: 18px; display: grid; gap: 6px; }}
.method {{ color: var(--muted); font-size: 14px; max-width: 75ch; margin: 0; }}
</style>
<div class="wrap">
  <div class="top">
    <div><h1>Pocket Arcade Remakes</h1>
    <p>Twelve native games rebuilt against the real games they are modelled on. A game is done only when a fresh critic, shown our frames and the reference's side by side without knowing which is which, picks ours.</p></div>
    <span class="stamp">Updated {e(now)}</span>
  </div>
  <div class="tally">
    <div><b>{won}/12</b><span>Ours wins blind</span></div>
    <div><b>{working}</b><span>Builder working</span></div>
    <div><b>{review}</b><span>With critic</span></div>
    <div><b>{sum(1 for g in games if g["verdict"])}</b><span>Games blind-reviewed</span></div>
  </div>
  <div class="grid">{"".join(cards)}</div>
  <div class="lower">
    <section><h2>Shared engine</h2><ul class="eng">{eng}</ul></section>
    <section><h2>Programme log</h2><ol class="log" reversed>{log}</ol>
    <p class="method">Frames are our own headless renders at phone resolution (540 wide). Reference screenshots are only shown to critics and are never published here.</p></section>
  </div>
</div>'''
out = os.path.join(ROOT, "reviews/status/index.html")
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, "w").write(page)
print(out, len(page))
