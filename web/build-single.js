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

fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(outFile, html);

const kb = (Buffer.byteLength(html) / 1024).toFixed(1);
console.log("Inlined " + inlined + " scripts -> " + outFile + " (" + kb + " KB)");
