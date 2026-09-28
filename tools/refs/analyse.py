"""
Turn a folder of store screenshots into two things:

  sheet_<game>.png  - a contact sheet, so the references can actually be looked at
  palette.json      - measured colour facts per game

The measurement is the point. "Make it look more like the real game" is not
actionable; "the reference's dominant ground colour sits at value 0.84,
saturation 0.62 and ours sits at 0.68 / 0.55" is.
"""
import colorsys, json, os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
os.chdir(os.path.join(HERE, "plates"))

GAMES = sorted(d for d in os.listdir(".") if os.path.isdir(d) and not d.startswith("_"))

def load(path, width=220):
    im = Image.open(path).convert("RGB")
    h = int(im.height * width / im.width)
    return im.resize((width, h), Image.LANCZOS)

def sheet(game, shots, cols=5, tw=180):
    thumbs = [load(p, tw) for p in shots]
    rows = (len(thumbs) + cols - 1) // cols
    cell_h = max(t.height for t in thumbs)
    out = Image.new("RGB", (cols * (tw + 6) + 6, rows * (cell_h + 6) + 6), (18, 16, 30))
    for i, t in enumerate(thumbs):
        x = 6 + (i % cols) * (tw + 6)
        y = 6 + (i // cols) * (cell_h + 6)
        out.paste(t, (x, y))
    out.save(f"sheet_{game}.png")

def palette(shots, k=10):
    """
    Dominant colours across all shots. Each shot is quantised on its own (median
    cut), then clusters are merged across shots by distance. Weighting is by
    pixel share, so a big field of grass outranks a small bright coin - which is
    exactly the right bias, since the big fields are what set the overall look.
    """
    bins = []  # [r,g,b,weight]
    for p in shots:
        im = load(p, 160)
        q = im.quantize(colors=12, method=Image.Quantize.MEDIANCUT)
        pal = q.getpalette()
        counts = q.getcolors()
        total = sum(c for c, _ in counts)
        for c, idx in counts:
            r, g, b = pal[idx*3:idx*3+3]
            w = c / total / len(shots)
            for bn in bins:
                if (bn[0]-r)**2 + (bn[1]-g)**2 + (bn[2]-b)**2 < 26**2:
                    t = bn[3] + w
                    bn[0] = (bn[0]*bn[3] + r*w) / t
                    bn[1] = (bn[1]*bn[3] + g*w) / t
                    bn[2] = (bn[2]*bn[3] + b*w) / t
                    bn[3] = t
                    break
            else:
                bins.append([r, g, b, w])
    bins.sort(key=lambda b: -b[3])
    out = []
    for r, g, b, w in bins[:k]:
        h, s, v = colorsys.rgb_to_hsv(r/255, g/255, b/255)
        out.append({"hex": "#%02X%02X%02X" % (int(r), int(g), int(b)),
                    "share": round(w, 3), "h": round(h*360), "s": round(s, 2), "v": round(v, 2)})
    return out

def stats(shots):
    """Pixel-weighted mean saturation and value, plus the share of pixels that
       are 'vivid' (s>0.45 and v>0.6). That last number is the one that best
       separates finished hypercasual art from placeholder art."""
    n = sv = vv = vivid = 0
    for p in shots:
        im = load(p, 120)
        for r, g, b in (im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata()):
            h, s, v = colorsys.rgb_to_hsv(r/255, g/255, b/255)
            n += 1; sv += s; vv += v
            if s > 0.45 and v > 0.6: vivid += 1
    return {"mean_s": round(sv/n, 3), "mean_v": round(vv/n, 3), "vivid_share": round(vivid/n, 3)}

report = {}
for g in GAMES:
    shots = sorted(os.path.join(g, f) for f in os.listdir(g)
                   if f.lower().endswith((".png", ".jpg", ".jpeg", ".webp")))
    if not shots: continue
    sheet(g, shots)
    report[g] = {"shots": len(shots), "stats": stats(shots), "palette": palette(shots)}
    st = report[g]["stats"]
    top = ", ".join(f"{c['hex']}({c['share']*100:.0f}%)" for c in report[g]["palette"][:5])
    print(f"{g:12s} n={len(shots):2d}  mean s={st['mean_s']:.2f} v={st['mean_v']:.2f}  vivid={st['vivid_share']*100:4.1f}%  | {top}")

json.dump(report, open(os.path.join(HERE, "palette.json"), "w"), indent=2)
