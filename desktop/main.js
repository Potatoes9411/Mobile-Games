// Electron shell that turns the browser build into a real Windows 11 desktop app.
// Run `npm start` to play, `npm run dist` to produce an installer and a portable .exe.
const { app, BrowserWindow, Menu } = require("electron");
const path = require("path");

function createWindow() {
  const win = new BrowserWindow({
    width: 620,
    height: 1000,
    minWidth: 380,
    minHeight: 640,
    backgroundColor: "#070912",
    title: "Mob Clash: Gate Siege",
    autoHideMenuBar: true,
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false
    }
  });

  Menu.setApplicationMenu(null);
  win.loadFile(path.join(__dirname, "game", "index.html"));
}

app.whenReady().then(() => {
  createWindow();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
