// Copies the canonical browser build into the Electron package folder.
// Keeping one source of truth (web/index.html) means the desktop and browser
// versions can never drift apart.
const fs = require("fs");
const path = require("path");

const source = path.join(__dirname, "..", "web", "index.html");
const targetDir = path.join(__dirname, "game");
const target = path.join(targetDir, "index.html");

fs.mkdirSync(targetDir, { recursive: true });
fs.copyFileSync(source, target);
console.log("Synced " + source + " -> " + target);
