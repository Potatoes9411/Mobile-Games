"""
Review harness for the native build.

  python3 tools/shots.py render GAME OUTDIR [--shots 1,3,6] [--input SCRIPT]
                                [--auto N] [--size 540 1170]
      Builds nothing; runs native/out/pa_headless and writes OUTDIR/GAME_NN.png
  python3 tools/shots.py sheet OUT.png IMG [IMG ...] [--cols N] [--height H]
      Contact sheet, every image scaled to the same height.
  python3 tools/shots.py blind OUTDIR KEYFILE --ref REF [REF ...] --ours OURS [OURS ...]
      Writes OUTDIR/A.png and OUTDIR/B.png (each a sheet of one side, same
      height, no labels), randomly assigned. The key goes to KEYFILE, which a
      reviewer must never be shown.
"""
import os, random, subprocess, sys, json
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "native", "out", "pa_headless")

def render(argv):
    game, outdir = argv[0], argv[1]
    rest = argv[2:]
    os.makedirs(outdir, exist_ok=True)
    prefix = os.path.join(outdir, game)
    cmd = [BIN, "--game", game, "--out", prefix] + rest
    if "--size" not in rest: cmd += ["--size", "540", "1170"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit(r.stderr or r.stdout)
    outs = []
    for line in r.stdout.splitlines():
        ppm = line.split()[0]
        png = ppm[:-4] + ".png"
        Image.open(ppm).save(png); os.remove(ppm); outs.append(png)
        print(png, line.split()[1])
    return outs

def sheet(out, imgs, cols=None, height=900):
    ims = [Image.open(p).convert("RGB") for p in imgs]
    ims = [im.resize((max(1, round(im.width * height / im.height)), height)) for im in ims]
    cols = cols or len(ims)
    rows = (len(ims) + cols - 1) // cols
    gap = 12
    widths = [max(im.width for im in ims[r*cols:(r+1)*cols]) for r in range(rows)]
    W = max(sum(im.width for im in ims[r*cols:(r+1)*cols]) + gap*(cols-1) for r in range(rows))
    H = rows*height + gap*(rows-1)
    canvas = Image.new("RGB", (W, H), (128, 128, 128))
    for i, im in enumerate(ims):
        r, c = divmod(i, cols)
        x = sum(ims[r*cols+k].width + gap for k in range(c))
        canvas.paste(im, (x, r*(height+gap)))
    canvas.save(out)
    return out

def blind(argv):
    outdir, keyfile = argv[0], argv[1]
    i_ref, i_ours = argv.index("--ref"), argv.index("--ours")
    refs = argv[i_ref+1:i_ours] if i_ref < i_ours else argv[i_ref+1:]
    ours = argv[i_ours+1:i_ref] if i_ours < i_ref else argv[i_ours+1:]
    os.makedirs(outdir, exist_ok=True)
    flip = random.random() < 0.5
    a, b = (ours, refs) if flip else (refs, ours)
    sheet(os.path.join(outdir, "A.png"), a)
    sheet(os.path.join(outdir, "B.png"), b)
    os.makedirs(os.path.dirname(os.path.abspath(keyfile)), exist_ok=True)
    json.dump({"ours": "A" if flip else "B"}, open(keyfile, "w"))
    print(os.path.join(outdir, "A.png"), os.path.join(outdir, "B.png"))

if __name__ == "__main__":
    cmd, argv = sys.argv[1], sys.argv[2:]
    if cmd == "render": render(argv)
    elif cmd == "sheet":
        cols = height = None
        if "--cols" in argv: i = argv.index("--cols"); cols = int(argv[i+1]); del argv[i:i+2]
        if "--height" in argv: i = argv.index("--height"); height = int(argv[i+1]); del argv[i:i+2]
        print(sheet(argv[0], argv[1:], cols, height or 900))
    elif cmd == "blind": blind(argv)
    else: sys.exit(__doc__)
