/* ===========================================================================
   POCKET ARCADE - manifest
   The single source of truth for what the hub offers. Adding a game is two
   steps: drop the file in src/games/ and add a row here. Nothing else in the
   hub knows the list.

   A row carries only what the hub needs before the game's own script has
   loaded (id, title, genre tag, where to fetch it). Everything visual - the
   tagline, the accent, the animated tile - lives in the game file and arrives
   when the script registers itself with A.games.
   =========================================================================== */
(function (A) {
  "use strict";

  /* The registry every game file pushes into. It used to be created by whichever
     game script happened to load first; now that games load on demand, nothing
     is guaranteed to have run before the hub reads it. */
  A.games = A.games || [];

  A.MANIFEST = [
    { id: "mobclash",    title: "Mob Clash",      genre: "3D Runner",      script: "src/games/mobclash.js" },
    { id: "voidmuncher", title: "Void Muncher",   genre: "Arena",          script: "src/games/voidmuncher.js" },
    { id: "roadhopper",  title: "Road Hopper",    genre: "Endless Hopper", script: "src/games/roadhopper.js" },
    { id: "chromerush",  title: "Chrome Rush",    genre: "Endless Driver", script: "src/games/chromerush.js" },
    { id: "blocks",      title: "Block Storm",    genre: "Puzzle",         script: "src/games/blockblast.js" },
    { id: "pins",        title: "Pin Rescue",     genre: "Physics Puzzle", script: "src/games/pinrescue.js" },
    { id: "helix",       title: "Helix Drop",     genre: "Arcade",         script: "src/games/helix.js" },
    { id: "splat",       title: "Roller Splat",   genre: "Puzzle",         script: "src/games/splat.js" },
    { id: "paper",       title: "Paper Territory", genre: "Arena",         script: "src/games/paperio.js" },
    { id: "horde",       title: "Horde Arena",    genre: "Survivor",       script: "src/games/horde.js" },
    { id: "runner",      title: "Rooftop Run",    genre: "Endless Runner", script: "src/games/runner.js" }
  ];

  /** Manifest row for an id, or null. */
  A.manifestRow = function (id) {
    for (var i = 0; i < A.MANIFEST.length; i++) {
      if (A.MANIFEST[i].id === id) return A.MANIFEST[i];
    }
    return null;
  };
})(window.A);
