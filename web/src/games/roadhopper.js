/* ===========================================================================
   ROAD HOPPER
   The endless lane-hopper. Tap to hop forward, swipe to sidestep, and thread a
   blocky little animal through traffic, log rivers and express trains. The
   camera creeps forward the whole time, so standing still is its own way of
   dying - which is what turns a calm puzzle into a panic.

   Every character is generated from a seed: no sprite sheets, no atlases, just
   boxes stacked by a recipe. That keeps the gacha honest - a new bird really is
   a new bird, not a hue shift of the last one.
   =========================================================================== */
(function (A) {
  "use strict";
  A.games = A.games || [];

  var HALF = 5;                 // playfield spans -HALF..HALF columns
  var EDGE = HALF + 0.5;        // where the slab actually ends
  var SAFE_ROWS = 2;            // opening grass strip, no hazards
  var HOP_TIME = 0.13;          // seconds per hop, deliberately snappy
  var IDLE_LIMIT = 6.0;         // seconds of no progress before the eagle comes

  /* Unlockable characters. `seed` drives the generated body, `theme` retints the
     world so a new pick actually changes the run's look. Cost curve is the usual
     gacha shape: three cheap, then a wall, then a couple of trophies. */
  var CHARS = [
    { id: "chick",  name: "Chick",       seed: 101, cost: 0,    theme: null },
    { id: "duck",   name: "Duck",        seed: 214, cost: 120,  theme: null },
    { id: "piglet", name: "Piglet",      seed: 337, cost: 260,  theme: null },
    { id: "frog",   name: "Frog",        seed: 452, cost: 420,  theme: { grass: 0x2F6B4A, road: 0x3A3F46 } },
    { id: "cat",    name: "Alley Cat",   seed: 578, cost: 700,  theme: null },
    { id: "robot",  name: "Tin Bot",     seed: 691, cost: 1100, theme: { grass: 0x4A5568, road: 0x2C3140 } },
    { id: "ghost",  name: "Night Owl",   seed: 806, cost: 1700, theme: { sky: 0x161232, grass: 0x27404F, road: 0x1E2233 } },
    { id: "dragon", name: "Wee Dragon",  seed: 933, cost: 2600, theme: { grass: 0x6B4636, road: 0x3A2A26, water: 0xB4472F } }
  ];

  function create(host) {
    var save = host.save;
    var S = null;
    var ui = {};
    var layout = null;
    var charCache = {};

    /* ------------------------------------------------------ character bake -- */
    /** Builds a blocky animal from a seed. Returns a stack of boxes in world
        units (1 = one tile) plus a palette, drawn fresh each frame so the hop
        squash and the head bob stay live. */
    function buildChar(seed) {
      if (charCache[seed]) return charCache[seed];
      var rand = A.rng(seed);

      var hue = rand();
      var body = A.hsl(hue, rand.range(0.45, 0.78), rand.range(0.52, 0.68));
      var belly = A.shade(body, rand.range(0.20, 0.38));
      var beak = A.hsl(A.wrap(hue + rand.range(0.08, 0.18), 1), 0.85, 0.58);
      var feet = A.shade(beak, -0.18);

      var parts = [];
      var bodyW = rand.range(0.60, 0.76);
      var bodyH = rand.range(0.38, 0.54);
      var headW = bodyW * rand.range(0.72, 0.94);
      var headH = rand.range(0.34, 0.48);

      /* legs, body, belly patch, head, beak, eyes, and one of three crests */
      parts.push({ tag: "leg", x: -bodyW * 0.26, z: 0, w: 0.11, h: 0.16, d: 0.11, c: feet });
      parts.push({ tag: "leg", x: bodyW * 0.26, z: 0, w: 0.11, h: 0.16, d: 0.11, c: feet });
      parts.push({ tag: "body", x: 0, z: 0.14, w: bodyW, h: bodyH, d: bodyW * 0.82, c: body });
      parts.push({ tag: "belly", x: 0, z: 0.17, w: bodyW * 0.62, h: bodyH * 0.55, d: bodyW * 0.90, c: belly });
      parts.push({ tag: "head", x: 0, z: 0.14 + bodyH, w: headW, h: headH, d: headW * 0.86, c: body });
      parts.push({ tag: "beak", x: 0, z: 0.14 + bodyH + headH * 0.30, w: headW * 0.30, h: headH * 0.30, d: headW * 0.95, c: beak, front: true });

      var crest = rand.int(0, 3);
      if (crest === 1) {
        parts.push({ tag: "crest", x: 0, z: 0.14 + bodyH + headH, w: headW * 0.24, h: 0.14, d: headW * 0.24, c: beak });
      } else if (crest === 2) {
        parts.push({ tag: "ear", x: -headW * 0.34, z: 0.14 + bodyH + headH * 0.92, w: 0.10, h: 0.16, d: 0.08, c: body });
        parts.push({ tag: "ear", x: headW * 0.34, z: 0.14 + bodyH + headH * 0.92, w: 0.10, h: 0.16, d: 0.08, c: body });
      } else if (crest === 3) {
        parts.push({ tag: "horn", x: 0, z: 0.14 + bodyH + headH, w: headW * 0.55, h: 0.08, d: headW * 0.30, c: A.shade(beak, -0.25) });
      }

      var wings = rand.chance(0.55);
      if (wings) {
        parts.push({ tag: "wing", x: -bodyW * 0.54, z: 0.20, w: 0.09, h: bodyH * 0.66, d: bodyW * 0.58, c: A.shade(body, -0.16) });
        parts.push({ tag: "wing", x: bodyW * 0.54, z: 0.20, w: 0.09, h: bodyH * 0.66, d: bodyW * 0.58, c: A.shade(body, -0.16) });
      }

      var built = {
        parts: parts,
        body: body,
        eyeZ: 0.14 + bodyH + headH * 0.62,
        headW: headW,
        tint: A.toHex(body)
      };
      charCache[seed] = built;
      return built;
    }

    function activeChar() {
      var id = save.charId;
      for (var i = 0; i < CHARS.length; i++) if (CHARS[i].id === id) return CHARS[i];
      return CHARS[0];
    }

    /* ---------------------------------------------------------- row build -- */
    /** Colour set for a row, after the active character's theme override. */
    function palette() {
      var t = (activeChar().theme) || {};
      return {
        sky: A.hex(t.sky || 0x8FD4E8),
        grassA: A.hex(t.grass || 0x4FAE63),
        grassB: A.shade(A.hex(t.grass || 0x4FAE63), -0.10),
        road: A.hex(t.road || 0x4A4F5A),
        water: A.hex(t.water || 0x2E7BD6),
        rail: A.hex(0x6B6257)
      };
    }

    /** Rows are generated on demand as the camera advances and dropped once they
        fall behind, so an infinite track costs a fixed amount of memory. The row
        seed is a hash of the index, so the same run always regenerates the same
        world even after rows have been forgotten and rebuilt. */
    function rowSeed(index) { return (0x9E3779B1 ^ (index * 2654435761)) >>> 0; }

    function buildRow(index, runSeed, prevType, streak) {
      var rand = A.rng(rowSeed(index) ^ runSeed);
      var row = { index: index, cars: [], trees: {}, coin: -99, type: "grass" };

      if (index <= SAFE_ROWS) {
        row.type = "grass";
        row.tint = (Math.abs(index) % 2) ? "grassB" : "grassA";
        /* Rows behind the start line exist only to fill the bottom of the
           screen. Nobody hops back there, so they get dense scenery - an empty
           green field under the player looks like the level failed to load. */
        if (index < 0) {
          var n = rand.int(2, 5);
          for (var i = 0; i < n; i++) {
            var c = rand.int(-HALF, HALF);
            if (row.trees[c]) continue;
            row.trees[c] = { h: rand.range(0.55, 1.45), s: rand.range(0.62, 0.90) };
          }
        }
        return row;
      }

      /* Difficulty ramps by row but flattens out - past row 220 the game is
         about nerve, not about ever-faster cars. */
      var d = A.clamp01(index / 220);

      /* Run lengths keep bands readable. A single road between two grass strips
         is a tutorial; four in a row is the thing people screenshot. */
      var type = prevType;
      if (streak >= maxStreak(prevType, rand, d)) {
        var roll = rand();
        if (prevType === "grass") type = roll < 0.52 ? "road" : (roll < 0.80 ? "water" : "rail");
        else if (prevType === "road") type = roll < 0.55 ? "grass" : (roll < 0.85 ? "water" : "rail");
        else if (prevType === "water") type = roll < 0.62 ? "grass" : "road";
        else type = rand() < 0.6 ? "grass" : "road";
      }
      row.type = type;

      if (type === "grass") {
        row.tint = (index % 2) ? "grassB" : "grassA";
        /* Trees block tiles. Density climbs but never above half the width, and
           never the whole row - a sealed row would be an unwinnable board. */
        var count = rand.int(1, 2 + Math.round(d * 4));
        var placed = 0;
        for (var i = 0; i < count * 2 && placed < count; i++) {
          var c = rand.int(-HALF, HALF);
          if (row.trees[c]) continue;
          row.trees[c] = { h: rand.range(0.55, 1.35), s: rand.range(0.62, 0.86) };
          placed++;
        }
        if (rand.chance(0.30)) {
          for (var t = 0; t < 8; t++) {
            var cc = rand.int(-HALF + 1, HALF - 1);
            if (!row.trees[cc]) { row.coin = cc; break; }
          }
        }
      } else if (type === "road") {
        row.dir = rand.chance(0.5) ? 1 : -1;
        row.speed = A.lerp(1.9, 5.6, d) * rand.range(0.82, 1.20);
        row.tint = "road";
        row.markings = rand.chance(0.5);
        var gap = A.lerp(6.2, 3.3, d) * rand.range(0.9, 1.25);
        var len = rand.chance(0.22) ? rand.range(1.7, 2.3) : rand.range(0.95, 1.35);
        var span = (HALF * 2 + 4);
        var x = rand.range(-span / 2, span / 2);
        var guard = 0;
        while (x < span / 2 && guard++ < 40) {
          row.cars.push({ x: x, len: len, hue: rand(), kind: len > 1.6 ? "truck" : "car" });
          x += len + gap * rand.range(0.85, 1.15);
        }
        row.span = span;
        if (rand.chance(0.18)) {
          for (var t2 = 0; t2 < 6; t2++) {
            var c2 = rand.int(-HALF + 1, HALF - 1);
            row.coin = c2; break;
          }
        }
      } else if (type === "water") {
        row.dir = rand.chance(0.5) ? 1 : -1;
        row.speed = A.lerp(1.3, 3.1, d) * rand.range(0.85, 1.15);
        row.tint = "water";
        var lgap = A.lerp(2.4, 3.4, d) * rand.range(0.9, 1.2);
        var llen = rand.range(1.6, 3.4);
        var lspan = (HALF * 2 + 6);
        var lx = rand.range(-lspan / 2, lspan / 2);
        var g2 = 0;
        while (lx < lspan / 2 && g2++ < 40) {
          row.cars.push({ x: lx, len: llen, hue: 0.09, kind: "log" });
          lx += llen + lgap;
        }
        row.span = lspan;
      } else {
        row.tint = "rail";
        row.dir = rand.chance(0.5) ? 1 : -1;
        row.warn = 0;
        row.trainX = null;
        row.nextTrain = rand.range(1.6, 4.2);
        row.speed = A.lerp(14, 22, d);
        row.span = HALF * 2 + 8;
      }

      return row;
    }

    function maxStreak(type, rand, d) {
      if (type === "grass") return rand.int(1, 3);
      if (type === "road") return rand.int(1, 2 + Math.round(d * 2));
      if (type === "water") return rand.int(1, 3);
      return rand.int(1, 2);
    }

    /* --------------------------------------------------------------- run --- */
    function begin() {
      var runSeed = (Math.random() * 0xFFFFFF) >>> 0;
      S = {
        runSeed: runSeed,
        rows: {},
        first: 0,
        built: -1,
        prevType: "grass",
        streak: 0,
        pal: palette(),
        chr: buildChar(activeChar().seed),

        px: 0, py: 0,               // logical tile position (py is the row index)
        drawX: 0, drawY: 0,         // interpolated draw position
        hop: null,                  // { fx, fy, tx, ty, t }
        onLog: null,
        facing: 1,                  // -1 left, 1 right, 0 forward, 2 back

        camY: -2.6,
        score: 0, coins: 0,
        idle: 0,
        over: false, deathKind: "",
        deathT: 0,
        eagle: 0,
        started: false,
        revived: false
      };

      computeLayout();
      /* Build a run-up of grass behind the start line. Without it the bottom of
         the screen is empty sky on the very first frame. */
      S.first = -(layout.behind + 4);
      S.built = S.first - 1;
      ensureRows(0);
      A.Fx.reset();
      paintHud();
    }

    /** Generates every row up to `ahead` and forgets the ones behind the camera. */
    function ensureRows(ahead) {
      var target = Math.ceil(ahead) + (layout ? layout.ahead + 4 : 22);
      while (S.built < target) {
        S.built++;
        var r = buildRow(S.built, S.runSeed, S.prevType, S.streak);
        S.streak = (r.type === S.prevType) ? S.streak + 1 : 1;
        S.prevType = r.type;
        S.rows[S.built] = r;
      }
      var cut = Math.floor(S.camY) - (layout ? layout.behind + 4 : 12) - 2;
      while (S.first < cut) {
        delete S.rows[S.first];
        S.first++;
      }
    }

    function computeLayout() {
      var w = A.View.w, h = A.View.h;
      /* Column width is set by the narrow axis. Row depth is deliberately much
         larger than a tile is wide: the real thing is viewed down a rotated
         diagonal, so bands read far apart on screen even though the world grid
         is square. Matching that spacing matters more than matching the maths. */
      var tile = w / (HALF * 2 + 3.4);
      var depth = tile * 1.52;
      var cy = h * 0.76;
      var ahead = Math.ceil((cy + 90) / depth) + 2;
      var behind = Math.ceil((h - cy + 90) / depth) + 2;
      layout = {
        w: w, h: h,
        tile: tile,
        depth: depth,
        rise: tile * 1.02,         // screen pixels per unit of world height
        /* The slab leans as it recedes. Anchoring the lean at the middle of the
           visible range keeps both ends on screen instead of walking the far
           rows off the left edge. */
        shear: -tile * 0.15,
        midDy: (ahead - behind) * 0.5,
        edgeDrop: tile * 0.55,     // thickness of the slab's cut edge
        cx: w * 0.5,
        cy: cy,
        /* Rows needed to cover the screen in each direction, resolved once here
           so the render loop never guesses and leaves a bare band. */
        ahead: ahead,
        behind: behind
      };
    }

    function project(col, row, z) {
      var dy = row - S.camY;
      return {
        x: layout.cx + col * layout.tile + (dy - layout.midDy) * layout.shear,
        y: layout.cy - dy * layout.depth - (z || 0) * layout.rise
      };
    }

    /* ------------------------------------------------------------- input -- */
    function tryHop(dx, dy) {
      if (S.over || S.hop) return;

      var fromX = S.onLog ? S.drawX : S.px;
      var tx = Math.round(fromX) + dx;
      var ty = S.py + dy;

      if (tx < -HALF || tx > HALF) return;
      if (ty < S.first) return;

      ensureRows(Math.max(ty, S.camY));
      var dest = S.rows[ty];
      if (dest && dest.type === "grass" && dest.trees[tx]) {
        A.Audio.sfx("thud");
        return;
      }

      S.hop = { fx: fromX, fy: S.py, tx: tx, ty: ty, t: 0 };
      S.facing = dy > 0 ? 0 : (dy < 0 ? 2 : (dx > 0 ? 1 : -1));
      S.onLog = null;
      S.started = true;
      A.Audio.sfx("hop");
    }

    function readInput() {
      var s = A.Input.consumeSwipe();
      if (s === "up" || s === "tap") tryHop(0, 1);
      else if (s === "down") tryHop(0, -1);
      else if (s === "left") tryHop(-1, 0);
      else if (s === "right") tryHop(1, 0);
    }

    /* -------------------------------------------------------------- step -- */
    function update(dt) {
      A.Fx.update(dt);

      if (S.over) {
        S.deathT += dt;
        if (S.deathKind === "eagle") S.eagle = Math.min(1, S.eagle + dt * 1.8);
        return;
      }

      readInput();

      /* Hop interpolation. The arc is a plain sine so the peak lands exactly at
         the halfway point, which is what makes landings feel on-beat. */
      if (S.hop) {
        S.hop.t += dt / HOP_TIME;
        var t = A.clamp01(S.hop.t);
        S.drawX = A.lerp(S.hop.fx, S.hop.tx, t);
        S.drawY = A.lerp(S.hop.fy, S.hop.ty, t);
        S.hopZ = Math.sin(t * Math.PI) * 0.42;
        if (t >= 1) {
          if (S.hop.ty > S.py) {
            S.score = Math.max(S.score, S.hop.ty);
            S.idle = 0;
            if (S.hop.ty > 0 && S.hop.ty % 25 === 0) {
              A.Fx.text(S.hop.tx, S.hop.ty, 0.9, S.hop.ty + "!", [255, 214, 84], { life: 0.9, rise: 40 });
              A.Audio.sfx("levelup");
            }
          }
          S.px = S.hop.tx;
          S.py = S.hop.ty;
          S.drawX = S.px;
          S.drawY = S.py;
          S.hopZ = 0;
          S.hop = null;
          landed();
        }
      } else {
        S.hopZ = 0;
        S.drawY = S.py;
      }

      ensureRows(Math.max(S.py, S.camY));

      /* Traffic, logs and trains. */
      for (var key in S.rows) {
        var r = S.rows[key];
        if (r.index < S.camY - 10 || r.index > S.camY + 20) continue;

        if (r.type === "road" || r.type === "water") {
          var lim = r.span / 2 + 4;
          for (var i = 0; i < r.cars.length; i++) {
            var c = r.cars[i];
            c.x += r.dir * r.speed * dt;
            if (r.dir > 0 && c.x - c.len / 2 > lim) c.x -= r.span + 8;
            if (r.dir < 0 && c.x + c.len / 2 < -lim) c.x += r.span + 8;
          }
        } else if (r.type === "rail") {
          if (r.trainX === null) {
            r.nextTrain -= dt;
            r.warn = r.nextTrain < 1.4 ? 1 : 0;
            if (r.nextTrain <= 0) {
              r.trainX = r.dir > 0 ? -(r.span / 2 + 9) : (r.span / 2 + 9);
              A.Audio.sfx("horn");
            }
          } else {
            r.trainX += r.dir * r.speed * dt;
            if (Math.abs(r.trainX) > r.span / 2 + 10) {
              r.trainX = null;
              r.warn = 0;
              r.nextTrain = 2.2 + Math.random() * 3.4;
            }
          }
        }
      }

      /* Ride the log we are standing on. */
      var here = S.rows[S.py];
      if (!S.hop && here && here.type === "water") {
        if (!S.onLog) {
          S.onLog = findLog(here, S.drawX);
          if (!S.onLog) { die("water"); return; }
          S.logOffset = S.drawX - S.onLog.x;
        }
        S.drawX = S.onLog.x + S.logOffset;
        S.px = Math.round(S.drawX);
        if (S.drawX < -HALF - 0.6 || S.drawX > HALF + 0.6) { die("water"); return; }
      } else if (!S.hop) {
        S.onLog = null;
        S.drawX = S.px;
      }

      /* Camera creep. It accelerates with score, and always keeps the player in
         frame if they sprint ahead of it. */
      var creep = A.lerp(0.55, 1.35, A.clamp01(S.score / 200));
      if (S.started) S.camY += creep * dt;
      var want = S.drawY - 2.4;
      if (want > S.camY) S.camY = A.approach(S.camY, want, 9, dt);

      /* Falling behind the camera is fatal, with a short grace band. */
      if (S.drawY < S.camY - 3.2) { die("eagle"); return; }

      S.idle += dt;
      if (S.idle > IDLE_LIMIT && S.started) { die("eagle"); return; }

      checkHazards();
      if (S.over) return;

      grabCoin();
      ensureRows(S.camY);
      paintHud();
    }

    function findLog(row, x) {
      for (var i = 0; i < row.cars.length; i++) {
        var c = row.cars[i];
        if (x > c.x - c.len / 2 - 0.30 && x < c.x + c.len / 2 + 0.30) return c;
      }
      return null;
    }

    function landed() {
      var r = S.rows[S.py];
      if (!r) return;
      if (r.type === "water") {
        S.onLog = findLog(r, S.drawX);
        if (!S.onLog) { die("water"); return; }
        S.logOffset = A.clamp(S.drawX - S.onLog.x, -S.onLog.len / 2 + 0.2, S.onLog.len / 2 - 0.2);
        S.drawX = S.onLog.x + S.logOffset;
      }
      A.Fx.burst(S.drawX, S.py, 0, 4, { color: [255, 255, 255], speed: 40, life: 0.22 });
    }

    function checkHazards() {
      var r = S.rows[Math.round(S.drawY)];
      if (!r) return;
      /* Mid-hop the player is airborne over the *destination* row, so we test
         against wherever they will land - being clipped by a car you already
         cleared is the single most infuriating failure in this genre. */
      var testRow = S.hop ? S.rows[S.hop.ty] : r;
      if (!testRow) return;
      var x = S.drawX;

      if (testRow.type === "road") {
        for (var i = 0; i < testRow.cars.length; i++) {
          var c = testRow.cars[i];
          if (Math.abs(c.x - x) < c.len / 2 + 0.34) {
            if (!S.hop || S.hop.t > 0.55) { die("car"); return; }
          }
        }
      } else if (testRow.type === "rail" && testRow.trainX !== null) {
        if (Math.abs(testRow.trainX - x) < 5.0) {
          if (!S.hop || S.hop.t > 0.4) { die("train"); return; }
        }
      }
    }

    function grabCoin() {
      var r = S.rows[S.py];
      if (!r || r.coin === -99 || S.hop) return;
      if (Math.abs(r.coin - S.drawX) < 0.5) {
        r.coin = -99;
        S.coins += 1;
        A.Audio.sfx("coin");
        A.Fx.text(S.drawX, S.py, 0.8, "+1", [255, 214, 84], { life: 0.55, rise: 44 });
      }
    }

    /* ------------------------------------------------------------- death -- */
    function die(kind) {
      if (S.over) return;
      S.over = true;
      S.deathKind = kind;
      S.deathT = 0;
      A.Fx.kick(kind === "train" ? 14 : 8);
      A.Audio.sfx("lose");
      A.vibrate(60);
      setTimeout(finish, 900);
    }

    function finish() {
      if (!S || !S.over) return;

      var coins = Math.round((S.coins * 4 + S.score * 0.6) * host.coinMultiplier());
      save.coins += coins;
      save.runs++;
      if (S.score > save.best) {
        save.best = S.score;
        host.toast("NEW BEST  ·  " + S.score);
      }
      save.totalHops += S.score;
      host.commit();

      var xp = Math.round((14 + S.score * 0.35) * host.xpMultiplier());
      host.addXp(xp);
      host.progress("run", 1);
      host.progress("coins", coins);

      var reason = {
        car: "Flattened by traffic",
        train: "The express does not brake",
        water: "Into the drink",
        eagle: "The eagle got bored of waiting"
      }[S.deathKind] || "";

      var actions = [
        { label: "HOP AGAIN", className: "go", onClick: function () { host.modal.hide(); begin(); } },
        { label: "CHARACTERS", className: "gold", onClick: function () { host.modal.hide(); openShop(); } },
        { label: "MENU", className: "ghost", onClick: host.exit }
      ];

      host.results({
        title: S.score >= save.best ? "NEW BEST" : "SQUASHED",
        subtitle: reason,
        rows: [
          { label: "Distance", value: S.score },
          { label: "Best", value: save.best },
          { label: "Coins found", value: S.coins },
          { label: "Coins", value: "+" + A.formatNumber(coins) },
          { label: "XP", value: "+" + xp }
        ],
        actions: actions
      });
    }

    /* -------------------------------------------------------------- shop -- */
    function openShop() {
      var card = host.el("div", "card");
      card.appendChild(host.el("h2", null, "CHARACTERS"));
      card.appendChild(host.el("p", "sub", A.formatNumber(save.coins) + " coins"));

      CHARS.forEach(function (c) {
        var owned = save.owned.indexOf(c.id) >= 0;
        var row = host.el("div", "shopRow");
        var left = host.el("div");
        left.appendChild(host.el("h4", null, c.name));
        left.appendChild(host.el("p", null,
          c.theme ? "Retints the whole world" : (owned ? "Unlocked" : "Locked")));
        row.appendChild(left);

        var btn = host.el("button", "buy");
        if (save.charId === c.id) {
          btn.textContent = "IN USE";
          btn.disabled = true;
        } else if (owned) {
          btn.textContent = "EQUIP";
          btn.addEventListener("click", function () {
            save.charId = c.id;
            host.commit();
            A.Audio.sfx("select");
            host.modal.hide();
            openShop();
          });
        } else {
          btn.textContent = A.formatNumber(c.cost);
          btn.disabled = save.coins < c.cost;
          btn.addEventListener("click", function () {
            if (save.coins < c.cost) return;
            save.coins -= c.cost;
            save.owned.push(c.id);
            save.charId = c.id;
            host.commit();
            A.Audio.sfx("buy");
            host.toast("UNLOCKED  ·  " + c.name);
            host.modal.hide();
            openShop();
          });
        }
        row.appendChild(btn);
        card.appendChild(row);
      });

      card.appendChild(host.button("BACK", "ghost", function () { host.modal.hide(); begin(); }));
      host.modal.show(card);
    }

    /* ------------------------------------------------------------ drawing -- */
    /** One extruded box in world space, drawn with a top face and a front face.
        Everything in the world is made of these, which is why the whole scene
        stays consistent without a single texture. */
    function box(g, col, row, z, w, h, d, color, alpha) {
      var p = project(col, row, z + h);
      var tw = w * layout.tile;
      var td = d * layout.depth;
      var th = h * layout.rise;

      g.globalAlpha = alpha === undefined ? 1 : alpha;

      /* Front face (the side that faces the camera). */
      g.fillStyle = A.rgb(A.shade(color, -0.26));
      g.beginPath();
      g.moveTo(p.x - tw / 2, p.y + td / 2);
      g.lineTo(p.x + tw / 2, p.y + td / 2);
      g.lineTo(p.x + tw / 2, p.y + td / 2 + th);
      g.lineTo(p.x - tw / 2, p.y + td / 2 + th);
      g.closePath();
      g.fill();

      /* Right side, sheared so the geometry agrees with the row tilt. */
      var sx = layout.shear * d;
      g.fillStyle = A.rgb(A.shade(color, -0.42));
      g.beginPath();
      g.moveTo(p.x + tw / 2, p.y - td / 2 + sx);
      g.lineTo(p.x + tw / 2 + 0.001, p.y + td / 2);
      g.lineTo(p.x + tw / 2, p.y + td / 2 + th);
      g.lineTo(p.x + tw / 2, p.y - td / 2 + th + sx);
      g.closePath();
      g.fill();

      /* Top face. */
      g.fillStyle = A.rgb(color);
      g.beginPath();
      g.moveTo(p.x - tw / 2 + sx, p.y - td / 2 + sx);
      g.lineTo(p.x + tw / 2 + sx, p.y - td / 2 + sx);
      g.lineTo(p.x + tw / 2, p.y + td / 2);
      g.lineTo(p.x - tw / 2, p.y + td / 2);
      g.closePath();
      g.fill();

      g.globalAlpha = 1;
    }

    function drawRow(g, r) {
      var pal = S.pal;
      var col = pal[r.tint] || pal.grassA;
      /* The slab is finite. Seeing where it ends is what sells the tilt - a
         full-width band would look like a flat stripe no matter how much shear
         we apply, because a horizontal line slides onto itself. */
      var a = project(-EDGE, r.index - 0.5, 0);
      var b = project(EDGE, r.index - 0.5, 0);
      var c = project(EDGE, r.index + 0.5, 0);
      var d = project(-EDGE, r.index + 0.5, 0);

      /* Cut edge first, so the row surface lands on top of its own thickness. */
      var drop = layout.edgeDrop;
      g.fillStyle = A.rgb(A.shade(col, -0.44));
      g.beginPath();
      g.moveTo(a.x, a.y); g.lineTo(d.x, d.y);
      g.lineTo(d.x, d.y + drop); g.lineTo(a.x, a.y + drop);
      g.closePath();
      g.fill();
      g.beginPath();
      g.moveTo(b.x, b.y); g.lineTo(c.x, c.y);
      g.lineTo(c.x, c.y + drop); g.lineTo(b.x, b.y + drop);
      g.closePath();
      g.fill();

      g.fillStyle = A.rgb(col);
      g.beginPath();
      g.moveTo(a.x, a.y); g.lineTo(b.x, b.y); g.lineTo(c.x, c.y); g.lineTo(d.x, d.y);
      g.closePath();
      g.fill();

      if (r.type === "road" && r.markings) {
        g.strokeStyle = "rgba(255,255,255,0.45)";
        g.lineWidth = Math.max(1.5, layout.tile * 0.05);
        g.setLineDash([layout.tile * 0.4, layout.tile * 0.4]);
        var m1 = project(-EDGE, r.index, 0);
        var m2 = project(EDGE, r.index, 0);
        g.beginPath(); g.moveTo(m1.x, m1.y); g.lineTo(m2.x, m2.y); g.stroke();
        g.setLineDash([]);
      }

      if (r.type === "water") {
        /* Two drifting highlight bands do the job of an animated water shader. */
        g.strokeStyle = "rgba(255,255,255,0.14)";
        g.lineWidth = Math.max(1.5, layout.tile * 0.06);
        for (var k = 0; k < 2; k++) {
          var off = A.wrap(A.Loop.time * 0.5 + k * 0.5 + r.index * 0.13, 1) * 2 - 1;
          var w1 = project(-EDGE, r.index + off * 0.3, 0);
          var w2 = project(EDGE, r.index + off * 0.3, 0);
          g.beginPath(); g.moveTo(w1.x, w1.y); g.lineTo(w2.x, w2.y); g.stroke();
        }
      }

      if (r.type === "rail") {
        g.strokeStyle = "rgba(30,26,20,0.55)";
        g.lineWidth = Math.max(2, layout.tile * 0.08);
        for (var s = -1; s <= 1; s += 2) {
          var r1 = project(-EDGE, r.index + s * 0.16, 0);
          var r2 = project(EDGE, r.index + s * 0.16, 0);
          g.beginPath(); g.moveTo(r1.x, r1.y); g.lineTo(r2.x, r2.y); g.stroke();
        }
        if (r.warn) {
          var blink = (Math.floor(A.Loop.time * 6) % 2) === 0;
          for (var side = -1; side <= 1; side += 2) {
            var lp = project(side * (HALF + 1.1), r.index, 0.85);
            g.fillStyle = blink ? "#FF4B5C" : "#5A2028";
            g.beginPath(); g.arc(lp.x, lp.y, layout.tile * 0.17, 0, A.TAU); g.fill();
          }
        }
      }

      if (r.coin !== -99) {
        var cp = project(r.coin, r.index, 0.30 + Math.sin(A.Loop.time * 4 + r.index) * 0.06);
        var rr = layout.tile * 0.20;
        g.fillStyle = "rgba(0,0,0,0.20)";
        g.beginPath();
        g.ellipse(cp.x, project(r.coin, r.index, 0).y, rr * 0.9, rr * 0.42, 0, 0, A.TAU);
        g.fill();
        var grad = g.createLinearGradient(cp.x - rr, cp.y - rr, cp.x + rr, cp.y + rr);
        grad.addColorStop(0, "#FFE9A0");
        grad.addColorStop(0.5, "#FFC93C");
        grad.addColorStop(1, "#C98A12");
        g.fillStyle = grad;
        g.beginPath();
        g.ellipse(cp.x, cp.y, rr * 0.62, rr, 0, 0, A.TAU);
        g.fill();
      }
    }

    function drawRowProps(g, r) {
      var i;
      if (r.type === "grass") {
        for (var key in r.trees) {
          var t = r.trees[key];
          var c = parseFloat(key);
          /* Narrow trunk, wide crown, and a lighter cap on top. Making the crown
             the same width as the trunk turned every tree into a green domino. */
          box(g, c, r.index, 0, 0.26, 0.34, 0.26, A.hex(0x6B4A2E));
          box(g, c, r.index, 0.30, t.s, t.h * 0.62, t.s, A.hex(0x2E7A47));
          box(g, c, r.index - 0.04, 0.30 + t.h * 0.42, t.s * 0.68, t.h * 0.34, t.s * 0.68, A.hex(0x3EA063));
        }
      } else if (r.type === "road") {
        for (i = 0; i < r.cars.length; i++) {
          var car = r.cars[i];
          if (Math.abs(car.x) > HALF + 4) continue;
          var body = A.hsl(car.hue, 0.72, 0.56);
          var flip = r.dir;

          box(g, car.x, r.index, 0.05, car.len, 0.30, 0.62, body);
          if (car.kind === "truck") {
            box(g, car.x - flip * car.len * 0.28, r.index, 0.35, car.len * 0.40, 0.26, 0.56, A.shade(body, 0.22));
            box(g, car.x + flip * car.len * 0.24, r.index, 0.35, car.len * 0.48, 0.16, 0.54, A.shade(body, -0.30));
          } else {
            /* A clearly narrower cabin, plus a dark screen band. At the same
               width as the body it just read as a second car parked on top. */
            box(g, car.x - flip * car.len * 0.06, r.index, 0.30, car.len * 0.46, 0.18, 0.46, A.shade(body, 0.30));
            var scr = project(car.x + flip * car.len * 0.14, r.index, 0.48);
            g.fillStyle = "rgba(26,34,54,0.62)";
            g.fillRect(scr.x - car.len * 0.05 * layout.tile, scr.y - layout.depth * 0.22,
              car.len * 0.10 * layout.tile, layout.depth * 0.44);
          }
          /* Headlights on the leading edge, so oncoming traffic reads instantly. */
          var lp = project(car.x + flip * (car.len / 2), r.index, 0.16);
          g.fillStyle = "rgba(255,246,200,0.92)";
          g.beginPath();
          g.ellipse(lp.x, lp.y, layout.tile * 0.06, layout.tile * 0.05, 0, 0, A.TAU);
          g.fill();
        }
      } else if (r.type === "water") {
        for (i = 0; i < r.cars.length; i++) {
          var log = r.cars[i];
          if (Math.abs(log.x) > HALF + 5) continue;
          box(g, log.x, r.index, 0, log.len, 0.20, 0.66, A.hex(0x7A5433));
          box(g, log.x, r.index - 0.02, 0.20, log.len * 0.98, 0.05, 0.60, A.hex(0x8E653F));
        }
      } else if (r.type === "rail" && r.trainX !== null) {
        box(g, r.trainX, r.index, 0.05, 9.0, 0.78, 0.78, A.hex(0xC2453D));
        box(g, r.trainX, r.index - 0.04, 0.83, 8.4, 0.10, 0.70, A.hex(0xE8E2D6));
        for (var w = -3; w <= 3; w++) {
          var wp = project(r.trainX + w * 1.25, r.index - 0.34, 0.42);
          g.fillStyle = "rgba(180,225,255,0.85)";
          g.fillRect(wp.x - layout.tile * 0.16, wp.y - layout.tile * 0.13,
            layout.tile * 0.32, layout.tile * 0.26);
        }
      }
    }

    function drawPlayer(g) {
      var chr = S.chr;
      var z = S.hopZ || 0;
      var squash = S.hop ? 1 : (1 + Math.sin(A.Loop.time * 6) * 0.02);

      /* Shadow lands on the ground, not on the character, so height reads. */
      var sp = project(S.drawX, S.drawY, 0);
      var shrink = 1 - z * 0.5;
      g.fillStyle = "rgba(0,0,0,0.24)";
      g.beginPath();
      g.ellipse(sp.x, sp.y, layout.tile * 0.30 * shrink, layout.depth * 0.30 * shrink, 0, 0, A.TAU);
      g.fill();

      if (S.over && S.deathKind === "car") {
        /* Pancaked: flatten the whole stack in place. */
        var flat = A.clamp01(S.deathT * 6);
        for (var f = 0; f < chr.parts.length; f++) {
          var pf = chr.parts[f];
          box(g, S.drawX + pf.x, S.drawY, pf.z * (1 - flat * 0.9),
            pf.w * (1 + flat * 0.5), pf.h * (1 - flat * 0.85), pf.d * (1 + flat * 0.5), pf.c);
        }
        return;
      }

      for (var i = 0; i < chr.parts.length; i++) {
        var p = chr.parts[i];
        var dz = p.front && S.facing === 0 ? 0.10 : 0;
        box(g, S.drawX + p.x, S.drawY - (p.front ? 0.16 : 0) + dz, (p.z + z) * squash,
          p.w, p.h * squash, p.d, p.c);
      }

      /* Eyes, drawn flat on top so they always face the camera. */
      var ep = project(S.drawX, S.drawY - 0.20, (chr.eyeZ + z) * squash);
      var er = layout.tile * 0.055;
      g.fillStyle = "#141024";
      for (var e = -1; e <= 1; e += 2) {
        g.beginPath();
        g.arc(ep.x + e * chr.headW * layout.tile * 0.26, ep.y, er, 0, A.TAU);
        g.fill();
      }
    }

    function drawEagle(g) {
      if (S.eagle <= 0) return;
      var t = S.eagle;
      var p = project(S.drawX, S.drawY, A.lerp(4.5, 0.35, A.smooth(t)));
      var w = layout.tile * 1.5;
      g.fillStyle = "#2A2438";
      g.beginPath();
      g.moveTo(p.x, p.y);
      g.lineTo(p.x - w, p.y - w * 0.35 + Math.sin(A.Loop.time * 14) * w * 0.16);
      g.lineTo(p.x - w * 0.3, p.y + w * 0.12);
      g.lineTo(p.x + w * 0.3, p.y + w * 0.12);
      g.lineTo(p.x + w, p.y - w * 0.35 - Math.sin(A.Loop.time * 14) * w * 0.16);
      g.closePath();
      g.fill();
      g.fillStyle = "#5A4B6E";
      g.beginPath();
      g.ellipse(p.x, p.y + w * 0.06, w * 0.28, w * 0.20, 0, 0, A.TAU);
      g.fill();
    }

    function render(g) {
      var pal = S.pal;
      var vw = layout.w, vh = layout.h;

      /* Sky up top, a deeper tone underneath. The slab floats over the darker
         half, which is what gives it its cut-out silhouette. */
      var sky = g.createLinearGradient(0, 0, 0, vh);
      sky.addColorStop(0, A.rgb(A.shade(pal.sky, 0.20)));
      sky.addColorStop(0.42, A.rgb(pal.sky));
      sky.addColorStop(0.62, A.rgb(A.shade(pal.sky, -0.34)));
      sky.addColorStop(1, A.rgb(A.shade(pal.sky, -0.52)));
      g.fillStyle = sky;
      g.fillRect(0, 0, vw, vh);

      g.save();
      A.Fx.applyShake(g);

      /* Painter's algorithm along the row axis: far rows first. Rows above the
         top of the screen are skipped, which is what keeps an endless track
         costing a fixed number of draws per frame. */
      var far = Math.ceil(S.camY) + layout.ahead;
      var near = Math.floor(S.camY) - layout.behind;
      var order = [];
      for (var i = far; i >= near; i--) {
        if (S.rows[i]) order.push(S.rows[i]);
      }

      var k;
      for (k = 0; k < order.length; k++) drawRow(g, order[k]);
      for (k = 0; k < order.length; k++) {
        drawRowProps(g, order[k]);
        /* The player is drawn inside the row loop so trees and cars in front of
           them actually occlude them. */
        if (Math.round(S.drawY) === order[k].index) drawPlayer(g);
      }
      if (S.drawY > far || S.drawY < near) drawPlayer(g);

      drawEagle(g);

      A.Fx.drawParticles(g, function (x, y, z) {
        var p = project(x, y, z);
        return { x: p.x, y: p.y, s: layout.tile / 40 };
      });
      A.Fx.drawTexts(g, function (x, y, z) {
        var p = project(x, y, z);
        return { x: p.x, y: p.y, s: layout.tile / 40 };
      });

      g.restore();

      /* Idle warning vignette - the eagle's tell. */
      if (S.idle > IDLE_LIMIT - 2 && !S.over) {
        var warn = A.clamp01((S.idle - (IDLE_LIMIT - 2)) / 2);
        var vg = g.createRadialGradient(vw / 2, vh / 2, vh * 0.25, vw / 2, vh / 2, vh * 0.75);
        vg.addColorStop(0, "rgba(0,0,0,0)");
        vg.addColorStop(1, "rgba(120,20,30," + (warn * 0.55).toFixed(3) + ")");
        g.fillStyle = vg;
        g.fillRect(0, 0, vw, vh);
      }

      A.Fx.drawFlash(g, vw, vh);
    }

    /* ----------------------------------------------------------------- ui -- */
    function mount(root) {
      var hud = host.el("div", "hud");
      var top = host.el("div", "row");
      ui.score = host.el("div", "chip", "0");
      ui.best = host.el("div", "chip", "BEST 0");
      ui.coins = host.el("div", "chip gold", "0");
      top.appendChild(ui.score);
      top.appendChild(ui.best);
      top.appendChild(ui.coins);
      hud.appendChild(top);

      ui.hint = host.el("div", "hint", "Tap to hop  ·  swipe to sidestep  ·  don't dawdle");
      hud.appendChild(ui.hint);
      root.appendChild(hud);
    }

    function paintHud() {
      if (!ui.score || !S) return;
      ui.score.textContent = S.score;
      ui.best.textContent = "BEST " + Math.max(save.best, S.score);
      ui.coins.textContent = A.formatNumber(save.coins + S.coins);
      if (S.score > 3) ui.hint.textContent = "";
    }

    return {
      mount: mount,
      start: begin,
      stop: function () { S = null; },
      update: function (dt) { if (S && layout) update(dt); },
      render: function (g) { if (S && layout) render(g); },
      onResize: computeLayout
    };
  }

  A.games.push({
    id: "roadhopper",
    name: "Road Hopper",
    tagline: "Hop across traffic, log rivers and express rails. The camera never stops creeping.",
    accent: "#7BD84F",
    unlock: 1,
    template: { coins: 0, runs: 0, best: 0, totalHops: 0, charId: "chick", owned: ["chick"] },
    bestLine: function (s) { return s.runs ? "Best " + s.best + "  ·  " + s.owned.length + " characters" : "New"; },
    thumb: function (g, w, h, t) {
      var bands = [
        ["#8FD4E8", 0.00, 0.16],
        ["#4FAE63", 0.16, 0.34],
        ["#4A4F5A", 0.34, 0.54],
        ["#2E7BD6", 0.54, 0.74],
        ["#47A25C", 0.74, 1.00]
      ];
      for (var i = 0; i < bands.length; i++) {
        g.fillStyle = bands[i][0];
        g.fillRect(0, h * bands[i][1], w, h * (bands[i][2] - bands[i][1]) + 1);
      }
      /* A car sliding across the road band and a log on the river. */
      var cx = A.wrap(t * 0.32, 1) * (w + 40) - 20;
      g.fillStyle = "#E2455F";
      A.roundRect(g, cx, h * 0.39, w * 0.22, h * 0.10, 3); g.fill();
      g.fillStyle = "#FF8A9B";
      A.roundRect(g, cx + w * 0.05, h * 0.365, w * 0.12, h * 0.05, 2); g.fill();

      var lx = w - A.wrap(t * 0.22 + 0.4, 1) * (w + 50);
      g.fillStyle = "#7A5433";
      A.roundRect(g, lx, h * 0.60, w * 0.34, h * 0.09, 4); g.fill();

      /* The hopper, bobbing. */
      var bob = Math.abs(Math.sin(t * 3)) * h * 0.05;
      var px = w * 0.5, py = h * 0.80 - bob;
      g.fillStyle = "rgba(0,0,0,0.20)";
      g.beginPath(); g.ellipse(px, h * 0.845, w * 0.09, h * 0.022, 0, 0, A.TAU); g.fill();
      g.fillStyle = "#F5D53F";
      A.roundRect(g, px - w * 0.08, py - h * 0.07, w * 0.16, h * 0.09, 3); g.fill();
      g.fillStyle = "#FFE87A";
      A.roundRect(g, px - w * 0.065, py - h * 0.125, w * 0.13, h * 0.065, 3); g.fill();
      g.fillStyle = "#E8892F";
      A.roundRect(g, px - w * 0.018, py - h * 0.105, w * 0.036, h * 0.024, 1); g.fill();
      g.fillStyle = "#141024";
      g.beginPath(); g.arc(px - w * 0.032, py - h * 0.108, w * 0.014, 0, A.TAU); g.fill();
      g.beginPath(); g.arc(px + w * 0.032, py - h * 0.108, w * 0.014, 0, A.TAU); g.fill();
    },
    create: create
  });
})(window.A);
