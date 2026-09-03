// Copies the bundled arcade into the Electron package folder.
// Run `node web/build-single.js` first; the npm scripts do that for you.
// Keeping one source of truth (web/index.html) means the desktop and browser
// versions can never drift apart.
const fs = require("fs");
const path = require("path");

const source = path.join(__dirname, "..", "dist", "pocket-arcade.html");
const targetDir = path.join(__dirname, "game");
const target = path.join(targetDir, "index.html");

fs.mkdirSync(targetDir, { recursive: true });
fs.copyFileSync(source, target);
console.log("Synced " + source + " -> " + target);
