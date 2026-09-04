/* ===========================================================================
   PIN RESCUE
   The pull-the-pin rescue puzzle: the single most spammed mobile ad format
   there is. Yank the pins in the right order so the lava drains away from your
   hero and the gold pours into their basin. Pull them in the wrong order and
   you cook them.

   Fluids are a falling-sand cellular automaton rather than rigid-body physics:
   it reads identically to the real thing, it is deterministic, and a 44x60 grid
   costs nothing on a phone.
   =========================================================================== */
(function (A) {
  "use strict";
  A.games = A.games || [];

  var EMPTY = 0, WALL = 1, LAVA = 2, GOLD = 3, WATER = 4, HERO = 5, PIN = 6;

  var TINT = {};
  TINT[WALL] = [86, 74, 122];
  TINT[LAVA] = [255, 96, 58];
  TINT[GOLD] = [255, 196, 74];
  TINT[WATER] = [90, 180, 255];
  TINT[HERO] = [63, 217, 138];

  /*
   * Levels are generated into a portrait grid rather than hand drawn, so the
   * structure is guaranteed rather than hoped for:
   *
   *   two top chambers, one lava one gold, each behind a gate pin
   *   a shaft under each chamber, each with its own side-drain pin
   *   both shafts merge into the hero basin at the bottom
   *
   * The correct play is: open the drain under the lava, dump the lava into the
   * pit, then drop the gold. Open the wrong drain and the gold is gone; drop the
   * lava before its drain and you cook the hero. Higher levels add a third
   * chamber and a shared middle shaft.
   */
  /*
   * Levels are generated on a fixed portrait layout rather than hand drawn, so
   * the structure is guaranteed rather than hoped for. Columns, left to right:
   *
   *   0  wall   1 pit   2 wall+drain   3-6 shaft   7 wall
   *   8-11 shaft   12 wall+drain   13 pit   14 wall
   *
   * Each shaft is a column of material sitting on a floor pin, with a drain pin
   * in its side wall leading to a dead-end pit. Below the floor pins both shafts
   * open into a chamber with the hero's basin at the bottom.
   *
   * So the play is: drain the lava out the side, then drop the gold through the
   * floor. Drop the lava instead and you cook the hero. From level 3 a shaft
   * holds lava stacked on top of gold, which forces drain-then-drop order.
   */
  var COLS = 15, ROWS = 18;
  var SHAFTS = [{ x0: 3, x1: 6, wall: 2, pit: 1 }, { x0: 8, x1: 11, wall: 12, pit: 13 }];
  var TOP = 1, BOTTOM = 9, DRAIN_ROW = 8, FLOOR_ROW = 10, BASIN_ROW = 15, GROUND_ROW = 16;

  function buildLevel(index) {
    var rand = A.rng(index * 7919 + 13);
    var rows = [];
    for (var y = 0; y < ROWS; y++) rows.push(new Array(COLS).fill("#"));

    function put(x, y, ch) {
      if (x >= 0 && y >= 0 && x < COLS && y < ROWS) rows[y][x] = ch;
    }

    // Carve the two shafts and the two pits.
    for (var y2 = TOP; y2 <= BASIN_ROW; y2++) {
      SHAFTS.forEach(function (shaft) {
        for (var x = shaft.x0; x <= shaft.x1; x++) put(x, y2, ".");
        if (y2 >= TOP + 4) put(shaft.pit, y2, ".");
      });
    }

    // Below the floor pins the shafts merge into one chamber above the basin.
    for (var y3 = FLOOR_ROW + 1; y3 <= BASIN_ROW; y3++) {
      for (var x2 = SHAFTS[0].x0; x2 <= SHAFTS[1].x1; x2++) put(x2, y3, ".");
    }

    var goldShaft = rand.int(0, 1);
    var mixed = index >= 3;
    var bothMixed = index >= 6;
    var goldCells = 0;

    SHAFTS.forEach(function (shaft, i) {
      var isGold = i === goldShaft;
      var stack = (isGold && mixed) || (bothMixed && !isGold);

      for (var y4 = TOP; y4 <= BOTTOM; y4++) {
        var kind;
        if (stack) kind = y4 <= TOP + 3 ? "L" : (isGold ? "G" : "L");
        else kind = isGold ? "G" : "L";

        for (var x3 = shaft.x0; x3 <= shaft.x1; x3++) {
          put(x3, y4, kind);
          if (kind === "G") goldCells++;
        }
      }

      // Side drain, then the floor the whole column is resting on.
      var drain = String.fromCharCode(97 + i * 2);
      var floor = String.fromCharCode(97 + i * 2 + 1);
      put(shaft.wall, DRAIN_ROW, drain);
      for (var x4 = shaft.x0; x4 <= shaft.x1; x4++) put(x4, FLOOR_ROW, floor);
    });

    // Hero basin, and a floor under everything including the pits.
    for (var x5 = 0; x5 < COLS; x5++) put(x5, GROUND_ROW, "#");
    for (var hx = 6; hx <= 8; hx++) put(hx, BASIN_ROW, "H");

    var out = rows.map(function (r) { return r.join(""); });
    out.goldCells = goldCells;
    if (typeof window !== "undefined") window.__pinDump = function () { return out.join("\n"); };
    return out;
  }

  function create(host) {
    var save = host.save;
    var S = null;
    var layout = null;
    var ui = {};
    var heroStyle = null;

    /* ----------------------------------------------------------- setup -- */
    function levelSpec(index) {
      return buildLevel(index);
    }

    function begin() {
      var spec = levelSpec(save.level);
      var h = spec.length, w = spec[0].length;

      S = {
        w: w, h: h,
        cell: new Uint8Array(w * h),
        pinOf: new Int8Array(w * h).fill(-1),
        pins: {},
        pinOrder: [],
        gold: 0,
        goldGoal: 0,
        collected: 0,
        lavaHit: false,
        over: false,
        won: false,
        t: 0,
        settle: 0,
        heroCells: [],
        pulls: 0,
        level: save.level,
        heroHurt: 0
      };

      // Extra pins granted by upgrades are not a thing; the shop sells retries
      // and a coolant that buys you time, so the puzzle stays the puzzle.
      for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
          var ch = spec[y][x];
          var i = y * w + x;

          if (ch === "#") S.cell[i] = WALL;
          else if (ch === "L") S.cell[i] = LAVA;
          else if (ch === "G") { S.cell[i] = GOLD; S.goldGoal++; }
          else if (ch === "W") S.cell[i] = WATER;
          else if (ch === "H") { S.cell[i] = EMPTY; S.heroCells.push(i); }
          else if (ch >= "a" && ch <= "f") {
            S.cell[i] = PIN;
            var pinId = ch.charCodeAt(0) - 97;
            S.pinOf[i] = pinId;
            if (!S.pins[pinId]) { S.pins[pinId] = { id: pinId, cells: [], pulled: 0, min: x, max: x, y: y }; S.pinOrder.push(pinId); }
            S.pins[pinId].cells.push(i);
            S.pins[pinId].min = Math.min(S.pins[pinId].min, x);
            S.pins[pinId].max = Math.max(S.pins[pinId].max, x);
          }
        }
      }

      // The goal has to be a share of what the basin can actually hold, not of
      // all the gold in the level: most of it lands outside the catch zone.
      S.catchMinX = Math.min.apply(null, S.heroCells.map(function (i) { return i % w; }));
      S.catchMaxX = Math.max.apply(null, S.heroCells.map(function (i) { return i % w; }));
      S.catchMinY = Math.max(0, Math.floor(S.heroCells[0] / w) - 4);
      S.catchMaxY = Math.floor(S.heroCells[0] / w);
      S.goldGoal = A.clamp(Math.round(S.goldGoal * 0.28), 3, 14);
      S.coolant = save.upCoolant;
      A.Fx.reset();
      computeLayout();
      paintHud();

      if (typeof window !== "undefined") {
        window.__pinProbe = function () {
          return { x: layout.x, y: layout.y, cell: layout.cell,
                   collected: S.collected, goal: S.goalGoal || S.goldGoal,
                   over: S.over, won: S.won };
        };
      }
    }

    /* ------------------------------------------------------- simulation -- */
    function at(x, y) {
      if (x < 0 || y < 0 || x >= S.w || y >= S.h) return WALL;
      return S.cell[y * S.w + x];
    }

    function set(x, y, v) { S.cell[y * S.w + x] = v; }

    function isFluid(v) { return v === LAVA || v === GOLD || v === WATER; }
    function isBlocking(v) { return v === WALL || v === PIN; }

    /**
     * One pass of falling-sand: down, then diagonals, then sideways for liquids.
     * Scanned bottom-up with alternating row direction so piles do not lean.
     */
    function stepFluid() {
      var moved = 0;

      for (var y = S.h - 2; y >= 0; y--) {
        var leftToRight = (S.t * 60 + y) % 2 === 0;
        for (var k = 0; k < S.w; k++) {
          var x = leftToRight ? k : S.w - 1 - k;
          var v = at(x, y);
          if (!isFluid(v)) continue;

          if (at(x, y + 1) === EMPTY) { set(x, y, EMPTY); set(x, y + 1, v); moved++; continue; }

          var dir = ((x + y + Math.floor(S.t * 12)) % 2) ? 1 : -1;
          if (at(x + dir, y + 1) === EMPTY) { set(x, y, EMPTY); set(x + dir, y + 1, v); moved++; continue; }
          if (at(x - dir, y + 1) === EMPTY) { set(x, y, EMPTY); set(x - dir, y + 1, v); moved++; continue; }

          // Liquids spread sideways; gold behaves more like sand and mostly does not.
          var spread = v === GOLD ? 0.35 : 1;
          if (Math.random() < spread) {
            if (at(x + dir, y) === EMPTY) { set(x, y, EMPTY); set(x + dir, y, v); moved++; continue; }
            if (at(x - dir, y) === EMPTY) { set(x, y, EMPTY); set(x - dir, y, v); moved++; }
          }
        }
      }

      // Water quenches lava on contact, which is the escape valve in later levels.
      for (var i = 0; i < S.cell.length; i++) {
        if (S.cell[i] !== LAVA) continue;
        var lx = i % S.w, ly = (i / S.w) | 0;
        if (at(lx + 1, ly) === WATER || at(lx - 1, ly) === WATER ||
            at(lx, ly + 1) === WATER || at(lx, ly - 1) === WATER) {
          S.cell[i] = WALL;
          spark(lx, ly, [220, 220, 235]);
        }
      }

      return moved;
    }

    /** Counts what has settled in the column of cells above the hero's basin. */
    function checkGoal() {
      var goldInBasin = 0;
      var lavaInBasin = 0;

      for (var y = S.catchMinY; y <= S.catchMaxY; y++) {
        for (var x = S.catchMinX; x <= S.catchMaxX; x++) {
          var v = S.cell[y * S.w + x];
          if (v === GOLD) goldInBasin++;
          else if (v === LAVA) lavaInBasin++;
        }
      }

      S.collected = goldInBasin;

      if (lavaInBasin > 0 && !S.over) {
        if (S.coolant > 0) {
          S.coolant--;
          S.heroHurt = 1;
          for (var j = 0; j < S.heroCells.length; j++) {
            if (S.cell[S.heroCells[j]] === LAVA) S.cell[S.heroCells[j]] = WALL;
          }
          host.toast("COOLANT USED");
          A.Fx.flashScreen(0.4, [120, 200, 255]);
          paintHud();
          return;
        }
        finish(false);
      }
    }

    /* -------------------------------------------------------- pin pull -- */
    function pullPin(pin) {
      if (pin.pulled > 0 || S.over) return;
      pin.pulled = 0.001;
      S.pulls++;
      A.Audio.sfx("select");
      A.vibrate(18);
    }

    function updatePins(dt) {
      for (var id in S.pins) {
        var pin = S.pins[id];
        if (pin.pulled <= 0 || pin.pulled >= 1) continue;

        pin.pulled = Math.min(1, pin.pulled + dt * 3.4);
        if (pin.pulled >= 1) {
          for (var i = 0; i < pin.cells.length; i++) {
            var index = pin.cells[i];
            if (S.cell[index] === PIN) S.cell[index] = EMPTY;
            spark(index % S.w, (index / S.w) | 0, [200, 190, 230]);
          }
          A.Fx.kick(0.18);
          A.Audio.sfx("hit");
        }
      }
    }

    function spark(gx, gy, color) {
      if (!layout) return;
      A.Fx.burst(layout.x + (gx + 0.5) * layout.cell, layout.y + (gy + 0.5) * layout.cell, 0, 3, {
        color: color, speed: 90, up: 0, gravity: 300, life: 0.4,
        size: layout.cell * 0.16, screen: true, bounce: false
      });
    }

    /* ---------------------------------------------------------- finish -- */
    function finish(won) {
      if (S.over) return;
      S.over = true;
      S.won = won;

      var coins = Math.round((won ? 40 + S.collected * 6 + save.level * 8 : 8) *
        host.coinMultiplier() * (1 + save.upCoin * 0.15));
      save.coins += coins;
      save.runs++;
      if (won) { save.level++; save.wins++; save.best = Math.max(save.best, save.level - 1); }
      host.commit();

      var xp = Math.round((won ? 24 + save.level * 3 : 6) * host.xpMultiplier());
      host.addXp(xp);
      host.progress("run", 1);
      host.progress("coins", coins);

      if (won) {
        A.Audio.sfx("win");
        A.Fx.flashScreen(0.4, [255, 196, 74]);
        A.Fx.kick(0.5);
        A.vibrate([20, 40, 30]);
      } else {
        A.Audio.sfx("lose");
        A.Fx.flashScreen(0.5, [255, 96, 58]);
        A.vibrate(120);
      }

      setTimeout(function () {
        host.results({
          win: won,
          title: won ? "RESCUED" : "TOO LATE",
          subtitle: won ? "Level " + (save.level - 1) + " cleared" : "The lava got there first",
          stats: [
            ["Level", String(S.level)],
            ["Gold saved", S.collected + " / " + S.goldGoal],
            ["Pins pulled", String(S.pulls)],
            ["Coins", "+" + A.formatNumber(coins)]
          ],
          buttons: [
            { label: won ? "NEXT LEVEL" : "TRY AGAIN", className: "go",
              onClick: function () { host.modal.hide(); begin(); } },
            { label: "UPGRADES", className: "gold", onClick: openShop },
            { label: "MENU", className: "ghost", onClick: host.exit }
          ]
        });
      }, won ? 700 : 500);
    }

    function openShop() {
      var rows = [
        { key: "upCoolant", name: "Coolant Flask", desc: "Survive one splash of lava per level", max: 3,
          cost: function (l) { return Math.round(400 * Math.pow(1.8, l)); } },
        { key: "upCoin", name: "Fortune", desc: "+15% coins from rescues", max: 10,
          cost: function (l) { return Math.round(220 * Math.pow(1.35, l)); } }
      ];

      var card = host.el("div", "card");
      card.appendChild(host.el("h2", null, "UPGRADES"));
      card.appendChild(host.el("p", "sub", A.formatNumber(save.coins) + " coins"));

      rows.forEach(function (r) {
        var row = host.el("div", "shopRow");
        var left = host.el("div");
        left.appendChild(host.el("h4", null, r.name + "  ·  LV " + save[r.key]));
        left.appendChild(host.el("p", null, r.desc));
        row.appendChild(left);

        var maxed = save[r.key] >= r.max;
        var cost = r.cost(save[r.key]);
        var buy = host.el("button", "buy");
        buy.innerHTML = maxed ? "MAX" : A.formatNumber(cost) + "<small>COINS</small>";
        buy.disabled = maxed || save.coins < cost;
        buy.addEventListener("click", function () {
          if (save.coins >= cost) {
            save.coins -= cost;
            save[r.key]++;
            host.commit();
            A.Audio.sfx("coin");
            openShop();
          }
        });
        row.appendChild(buy);
        card.appendChild(row);
      });

      card.appendChild(host.button("BACK", "ghost", function () { host.modal.hide(); begin(); }));
      host.modal.show(card);
    }

    /* ---------------------------------------------------------- layout -- */
    function computeLayout() {
      if (!S) return;
      var vw = A.View.w, vh = A.View.h;
      var top = Math.max(110, vh * 0.15);
      var bottom = Math.max(70, vh * 0.10);
      var cell = Math.min((vw - 28) / S.w, (vh - top - bottom) / S.h);

      layout = {
        cell: cell,
        x: (vw - cell * S.w) / 2,
        y: top + ((vh - top - bottom) - cell * S.h) / 2
      };
    }

    /* ----------------------------------------------------------- input -- */
    function update(dt) {
      S.t += dt;
      if (S.heroHurt > 0) S.heroHurt -= dt;

      updatePins(dt);

      // Fluid runs on a fixed sub-step so behaviour does not change with framerate.
      S.settle += dt;
      var steps = 0;
      while (S.settle >= 1 / 60 && steps < 3) {
        S.settle -= 1 / 60;
        stepFluid();
        steps++;
      }

      checkGoal();

      if (!S.over && S.collected >= S.goldGoal) {
        var stillFalling = false;
        for (var i = 0; i < S.cell.length; i++) {
          if (S.cell[i] === GOLD && S.cell[i + S.w] === EMPTY) { stillFalling = true; break; }
        }
        if (!stillFalling) finish(true);
      }

      if (A.Input.pressed && !S.over) {
        var gx = Math.floor((A.Input.x - layout.x) / layout.cell);
        var gy = Math.floor((A.Input.y - layout.y) / layout.cell);
        for (var id in S.pins) {
          var pin = S.pins[id];
          if (pin.pulled > 0) continue;
          if (gy >= pin.y - 1 && gy <= pin.y + 1 && gx >= pin.min - 1 && gx <= pin.max + 1) {
            pullPin(pin);
            break;
          }
        }
        paintHud();
      }
    }

    /* ---------------------------------------------------------- render -- */
    function render(g) {
      var vw = A.View.w, vh = A.View.h;

      var bg = g.createLinearGradient(0, 0, 0, vh);
      bg.addColorStop(0, "#2A1836");
      bg.addColorStop(1, "#140B1E");
      g.fillStyle = bg;
      g.fillRect(0, 0, vw, vh);

      A.Fx.applyShake(g);

      var l = layout;
      A.roundRect(g, l.x - 10, l.y - 10, l.cell * S.w + 20, l.cell * S.h + 20, 16);
      g.fillStyle = "#1B1030";
      g.fill();
      g.strokeStyle = "#4A3568";
      g.lineWidth = 3;
      g.stroke();

      // Basin highlight so the goal is never ambiguous.
      for (var i = 0; i < S.heroCells.length; i++) {
        var hx = S.heroCells[i] % S.w, hy = (S.heroCells[i] / S.w) | 0;
        g.fillStyle = "rgba(63,217,138,0.16)";
        g.fillRect(l.x + hx * l.cell, l.y + hy * l.cell, l.cell + 0.5, l.cell + 0.5);
      }

      for (var y = 0; y < S.h; y++) {
        for (var x = 0; x < S.w; x++) {
          var v = S.cell[y * S.w + x];
          if (v === EMPTY || v === PIN) continue;

          var px = l.x + x * l.cell, py = l.y + y * l.cell;

          if (v === WALL) {
            g.fillStyle = "#4B3E70";
            g.fillRect(px, py, l.cell + 0.5, l.cell + 0.5);
            g.fillStyle = "rgba(255,255,255,0.07)";
            g.fillRect(px, py, l.cell + 0.5, l.cell * 0.22);
            continue;
          }

          var color = TINT[v];
          var shimmer = v === LAVA ? Math.sin(S.t * 6 + x * 0.7 + y * 0.4) * 0.12 : 0;
          g.fillStyle = A.rgb(A.shade(color, shimmer));
          g.fillRect(px, py, l.cell + 0.5, l.cell + 0.5);

          if (v === GOLD) {
            g.fillStyle = "rgba(255,255,255,0.35)";
            g.fillRect(px + l.cell * 0.2, py + l.cell * 0.18, l.cell * 0.3, l.cell * 0.26);
          }
        }
      }

      drawPins(g);
      drawHero(g);

      A.Fx.drawParticles(g, null);
      A.Fx.drawTexts(g, null);

      g.restore();
      A.Fx.drawFlash(g, vw, vh);
    }

    function drawPins(g) {
      var l = layout;
      for (var id in S.pins) {
        var pin = S.pins[id];
        if (pin.pulled >= 1) continue;

        var slide = pin.pulled * l.cell * (S.w * 0.6);
        var x0 = l.x + pin.min * l.cell + slide;
        var wpx = (pin.max - pin.min + 1) * l.cell;
        var y = l.y + pin.y * l.cell;

        g.globalAlpha = 1 - pin.pulled * 0.7;

        A.roundRect(g, x0, y + l.cell * 0.12, wpx, l.cell * 0.76, l.cell * 0.38);
        var grad = g.createLinearGradient(0, y, 0, y + l.cell);
        grad.addColorStop(0, "#E6E1F5");
        grad.addColorStop(0.5, "#B9B0D8");
        grad.addColorStop(1, "#8076A8");
        g.fillStyle = grad;
        g.fill();
        g.strokeStyle = "#2E2547";
        g.lineWidth = 2;
        g.stroke();

        // Ring handle on the right, the affordance that says "pull me".
        g.beginPath();
        g.arc(x0 + wpx + l.cell * 0.34, y + l.cell * 0.5, l.cell * 0.34, 0, A.TAU);
        g.lineWidth = Math.max(2, l.cell * 0.16);
        g.strokeStyle = "#FFC24B";
        g.stroke();

        g.globalAlpha = 1;
      }
    }

    function drawHero(g) {
      if (!heroStyle || !S.heroCells.length) return;
      var l = layout;

      var minX = S.w, maxX = 0, maxY = 0;
      for (var i = 0; i < S.heroCells.length; i++) {
        var x = S.heroCells[i] % S.w, y = (S.heroCells[i] / S.w) | 0;
        minX = Math.min(minX, x); maxX = Math.max(maxX, x); maxY = Math.max(maxY, y);
      }

      var cx = l.x + ((minX + maxX + 1) / 2) * l.cell;
      var cy = l.y + (maxY + 1) * l.cell;
      var size = l.cell * 2.6;

      var clip = S.over && !S.won ? "die" : (S.over ? "cheer" : (S.heroHurt > 0 ? "hurt" : "idle"));
      var t = clip === "die" ? A.clamp01(S.t % 1)
            : clip === "hurt" ? 1 - S.heroHurt
            : A.wrap(S.t * (clip === "cheer" ? 3 : 0.5), 1);

      var pose = A.Rig.pose(clip, t);
      A.Rig.draw(g, heroStyle, pose, cx, cy, size, 1, { noShadow: true });
    }

    /* -------------------------------------------------------------- ui -- */
    function mount(root) {
      var hud = host.el("div", "hud");

      var top = host.el("div", "row");
      ui.level = host.el("div", "chip", "LEVEL 1");
      ui.goal = host.el("div", "meter");
      ui.goalFill = host.el("i");
      ui.goal.appendChild(ui.goalFill);
      ui.coins = host.el("div", "chip gold", "0");
      top.appendChild(ui.level);
      top.appendChild(ui.goal);
      top.appendChild(ui.coins);
      hud.appendChild(top);

      ui.cap = host.el("div", "cap", "gold rescued");
      hud.appendChild(ui.cap);

      ui.hint = host.el("div", "hint", "Tap a pin to pull it");
      hud.appendChild(ui.hint);

      root.appendChild(hud);
    }

    function paintHud() {
      if (!ui.level) return;
      ui.level.textContent = "LEVEL " + save.level;
      ui.coins.textContent = A.formatNumber(save.coins);
      ui.goalFill.style.width = A.clamp01(S.collected / S.goldGoal) * 100 + "%";
      ui.cap.textContent = S.collected + " / " + S.goldGoal + " gold rescued" +
        (S.coolant > 0 ? "   ·   " + S.coolant + " coolant" : "");
      ui.hint.textContent = S.pulls === 0 ? "Tap a pin to pull it" : "";
    }

    return {
      mount: mount,
      start: function () {
        if (!heroStyle) {
          heroStyle = A.Rig.style(3300, {
            hue: host.hue, helmet: "none", cape: false, weapon: "none", bulk: 0.95
          });
        }
        begin();
      },
      stop: function () { S = null; },
      update: function (dt) { if (S && layout) update(dt); },
      render: function (g) { if (S && layout) render(g); },
      onResize: computeLayout
    };
  }

  /* ------------------------------------------------------ registration -- */
  A.games.push({
    id: "pins",
    name: "Pin Rescue",
    tagline: "Pull the pins in the right order. Drain the lava, pour the gold, save your hero.",
    accent: "#FF6B4A",
    unlock: 1,
    template: { coins: 0, runs: 0, wins: 0, level: 1, best: 0, upCoolant: 0, upCoin: 0 },
    bestLine: function (s) {
      return s.runs ? "Level " + s.level + "  ·  " + s.wins + " rescues" : "New";
    },
    thumb: function (g, w, h, t) {
      g.fillStyle = "#20122E";
      g.fillRect(0, 0, w, h);

      var pour = (Math.sin(t * 1.6) * 0.5 + 0.5);
      g.fillStyle = "#FF603A";
      g.fillRect(w * 0.12, 6, w * 0.28, h * 0.30);
      g.fillStyle = "#FFC44A";
      g.fillRect(w * 0.60, 6, w * 0.28, h * 0.30);

      for (var i = 0; i < 7; i++) {
        var yy = h * 0.36 + i * 4 + pour * 10;
        if (yy > h * 0.74) continue;
        g.fillStyle = "#FFC44A";
        g.fillRect(w * 0.68, yy, 4, 4);
        g.fillStyle = "#FF603A";
        g.fillRect(w * 0.22, yy, 4, 4);
      }

      g.strokeStyle = "#E6E1F5";
      g.lineWidth = 4;
      g.beginPath();
      g.moveTo(w * 0.10, h * 0.36);
      g.lineTo(w * 0.44 - pour * 8, h * 0.36);
      g.moveTo(w * 0.58 + pour * 8, h * 0.36);
      g.lineTo(w * 0.92, h * 0.36);
      g.stroke();

      g.fillStyle = "#3FD98A";
      g.fillRect(w * 0.36, h * 0.78, w * 0.28, h * 0.14);
    },
    create: create
  });
})(window.A);
