"""
Pull official App Store screenshots for the games the arcade's titles are
modeled on. Store screenshots are what the publisher chose to show, so they are
the most honest statement of what each game is supposed to look like.

Images stay in the scratchpad. They are copyrighted and are fetched to be looked
at and measured, not redistributed; only derived facts get committed.

Usage (from the repo root):

    python3 tools/refs/fetch_refs.py        # -> tools/refs/plates/<game>/*.png
    python3 tools/refs/analyse.py           # -> contact sheets + palette.json

The plates folder is gitignored. Anyone can regenerate the pools locally; the
repository only carries the measurements taken from them.
"""
import json, os, re, sys, time, urllib.parse, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
PLATES = os.path.join(HERE, "plates")
os.makedirs(PLATES, exist_ok=True)
os.chdir(PLATES)

# The Search API rate-limits aggressively (roughly 20 calls a minute). A title
# that keeps hitting 403 can be fetched by its store id through /lookup instead.
LOOKUP_IDS = {"runner": 512939461}

# our game id -> (search term, developer substring that must match)
TARGETS = {
    "roadhopper":  ("crossy road",        "hipster whale"),
    "voidmuncher": ("hole.io",            "voodoo"),
    "mobclash":    ("mob control",        "voodoo"),
    "helix":       ("helix jump",         "voodoo"),
    "blockstorm":  ("block blast",        "hungry studio"),
    "splat":       ("roller splat",       "voodoo"),
    "paper":       ("paper.io 2",         "voodoo"),
    "pins":        ("pull the pin",       ""),
    "runner":      ("subway surfers",     "sybo"),
    "horde":       ("survivor io",        "habby"),
    "chromerush":  ("dashy crashy",       ""),
    "avian":       ("angry birds 2",      "rovio"),
}

def query(term):
    url = "https://itunes.apple.com/search?" + urllib.parse.urlencode(
        {"term": term, "entity": "software", "limit": 8, "country": "us"})
    with urllib.request.urlopen(url, timeout=30) as r:
        return json.load(r).get("results", [])

def upsize(u, w=600, h=1300):
    # Store thumbnails end in /<w>x<h>bb.<ext>; asking for larger returns the
    # original up to its native size, so this is a request, not an upscale.
    return re.sub(r"/\d+x\d+bb\.(png|jpg|jpeg|webp)$", rf"/{w}x{h}bb.\1", u)

def lookup(app_id):
    url = f"https://itunes.apple.com/lookup?id={app_id}&country=us"
    with urllib.request.urlopen(url, timeout=30) as r:
        return json.load(r).get("results", [])

def main():
    summary = {}
    for gid, (term, dev) in TARGETS.items():
        try:
            results = lookup(LOOKUP_IDS[gid]) if gid in LOOKUP_IDS else query(term)
            time.sleep(3.5)   # stay under the Search API's rate limit
        except Exception as e:
            print(f"{gid:12s} query failed: {e}"); continue

        pick = None
        for r in results:
            name = (r.get("trackName") or "").lower()
            artist = (r.get("artistName") or "").lower()
            if dev and dev not in artist: continue
            if term.split()[0] in name:
                pick = r; break
        if not pick and results and not dev:
            pick = results[0]
        if not pick:
            print(f"{gid:12s} no match for '{term}' / '{dev}'"); continue

        shots = pick.get("screenshotUrls", []) + pick.get("ipadScreenshotUrls", [])
        os.makedirs(gid, exist_ok=True)
        got = 0
        for i, u in enumerate(shots):
            big = upsize(u)
            ext = big.rsplit(".", 1)[-1]
            path = os.path.join(gid, f"{i:02d}.{ext}")
            if os.path.exists(path) and os.path.getsize(path) > 1000:
                got += 1; continue
            try:
                urllib.request.urlretrieve(big, path); got += 1
            except Exception as e:
                print(f"   {gid} shot {i} failed: {e}")
            time.sleep(0.15)
        summary[gid] = {"app": pick.get("trackName"), "dev": pick.get("artistName"),
                        "shots": got}
        print(f"{gid:12s} {got:2d} shots  <- {pick.get('trackName')} ({pick.get('artistName')})")

    json.dump(summary, open("summary.json", "w"), indent=2)

if __name__ == "__main__":
    main()
