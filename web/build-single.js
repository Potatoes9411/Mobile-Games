/*
 * Bundles the multi-file arcade into one self-contained HTML file.
 * The repo keeps the sources split for review; the .exe, the Electron package
 * and any single-file share link use the bundle this produces.
 *
 *   node web/build-single.js   ->   dist/pocket-arcade.html
 */
const fs = require("fs");
const path = require("path");

const webDir = __dirname;
const outDir = path.join(webDir, "..", "dist");
const outFile = path.join(outDir, "pocket-arcade.html");

let html = fs.readFileSync(path.join(webDir, "index.html"), "utf8");

const scriptTag = /<script src="([^"]+)"><\/script>/g;
let inlined = 0;

html = html.replace(scriptTag, (match, src) => {
  const file = path.join(webDir, src);
  if (!fs.existsSync(file)) {
    throw new Error("Missing script referenced by index.html: " + src);
  }
  inlined++;
  const code = fs.readFileSync(file, "utf8");
  return "<script>\n/* ---- " + src + " ---- */\n" + code + "\n</script>";
});

if (inlined === 0) throw new Error("No scripts were inlined - did index.html change shape?");

/*
 * Game scripts are not referenced by index.html any more - the GameManager
 * injects them from the manifest at runtime. A single file opened from disk
 * cannot fetch siblings, so the bundle inlines every manifest entry instead.
 * The manager checks its registry before fetching, so the inlined copies simply
 * mean it never has to.
 */
const manifestSrc = fs.readFileSync(path.join(webDir, "src", "manifest.js"), "utf8");
const scriptPaths = [];
const rowPattern = /script:\s*"([^"]+)"/g;
let rowMatch;
while ((rowMatch = rowPattern.exec(manifestSrc)) !== null) scriptPaths.push(rowMatch[1]);

if (scriptPaths.length === 0) throw new Error("No game scripts found in the manifest");

const gameBlobs = scriptPaths.map((src) => {
  const file = path.join(webDir, src);
  if (!fs.existsSync(file)) throw new Error("Manifest points at a missing file: " + src);
  inlined++;
  return "<script>\n/* ---- " + src + " ---- */\n" + fs.readFileSync(file, "utf8") + "\n</script>";
});

if (!html.includes("<!--ARCADE_GAME_SCRIPTS-->")) {
  throw new Error("index.html is missing the ARCADE_GAME_SCRIPTS placeholder");
}
html = html.replace("<!--ARCADE_GAME_SCRIPTS-->", gameBlobs.join("\n"));

fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(outFile, html);

const kb = (Buffer.byteLength(html) / 1024).toFixed(1);
console.log("Inlined " + inlined + " scripts -> " + outFile + " (" + kb + " KB)");
