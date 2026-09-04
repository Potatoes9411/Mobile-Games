/* ===========================================================================
   BLOCK STORM
   Eight-by-eight block puzzle. Three pieces at a time, no rotation, drop them
   to fill rows and columns. Chosen because puzzle is the second largest mobile
   genre by revenue and the block-drop format currently leads global downloads.
   The whole game is one screen and one rule, which is the point.
   =========================================================================== */
(function (A) {
  "use strict";
  A.games = A.games || [];

  var N = 8;
  var TAU = A.TAU;

  var COLORS = [
    [93, 168, 255], [63, 217, 138], [255, 194, 75],
    [255, 109, 129], [185, 139, 255], [123, 224, 255], [255, 145, 77]
  ];

  /* Standard block-puzzle piece set: lines, squares, corners and rectangles. */
  var SHAPES = (function () {
    function cells() { return Array.prototype.slice.call(arguments); }
    var list = [
      { w: 1, h: 1, c: cells([0, 0]), weight: 6 },
      { w: 2, h: 1, c: cells([0, 0], [1, 0]), weight: 8 },
      { w: 1, h: 2, c: cells([0, 0], [0, 1]), weight: 8 },
      { w: 3, h: 1, c: cells([0, 0], [1, 0], [2, 0]), weight: 8 },
      { w: 1, h: 3, c: cells([0, 0], [0, 1], [0, 2]), weight: 8 },
      { w: 4, h: 1, c: cells([0, 0], [1, 0], [2, 0], [3, 0]), weight: 5 },
      { w: 1, h: 4, c: cells([0, 0], [0, 1], [0, 2], [0, 3]), weight: 5 },
      { w: 5, h: 1, c: cells([0, 0], [1, 0], [2, 0], [3, 0], [4, 0]), weight: 2 },
      { w: 1, h: 5, c: cells([0, 0], [0, 1], [0, 2], [0, 3], [0, 4]), weight: 2 },
      { w: 2, h: 2, c: cells([0, 0], [1, 0], [0, 1], [1, 1]), weight: 7 },
      { w: 3, h: 3, c: cells([0, 0], [1, 0], [2, 0], [0, 1], [1, 1], [2, 1], [0, 2], [1, 2], [2, 2]), weight: 1 },
      { w: 2, h: 2, c: cells([0, 0], [0, 1], [1, 1]), weight: 5 },
      { w: 2, h: 2, c: cells([1, 0], [0, 1], [1, 1]), weight: 5 },
      { w: 2, h: 2, c: cells([0, 0], [1, 0], [0, 1]), weight: 5 },
      { w: 2, h: 2, c: cells([0, 0], [1, 0], [1, 1]), weight: 5 },
      { w: 3, h: 2, c: cells([0, 0], [1, 0], [2, 0], [0, 1], [1, 1], [2, 1]), weight: 3 },
      { w: 2, h: 3, c: cells([0, 0], [1, 0], [0, 1], [1, 1], [0, 2], [1, 2]), weight: 3 },
      { w: 3, h: 3, c: cells([0, 0], [0, 1], [0, 2], [1, 2], [2, 2]), weight: 3 },
      { w: 3, h: 3, c: cells([2, 0], [2, 1], [0, 2], [1, 2], [2, 2]), weight: 3 },
      { w: 3, h: 3, c: cells([0, 0], [1, 0], [2, 0], [0, 1], [0, 2]), weight: 3 },
      { w: 3, h: 3, c: cells([0, 0], [1, 0], [2, 0], [2, 1], [2, 2]), weight: 3 },
      { w: 3, h: 2, c: cells([0, 0], [1, 0], [2, 0], [1, 1]), weight: 4 },
      { w: 2, h: 3, c: cells([0, 0], [0, 1], [1, 1], [0, 2]), weight: 4 }
    ];

    var bag = [];
    list.forEach(function (shape, index) {
      shape.index = index;
      for (var i = 0; i < shape.weight; i++) bag.push(shape);
    });
    return { list: list, bag: bag };
  })();

  function create(host) {
    var save = host.save;
    var S = null;
    var layout = null;
    var ui = {};
    var heroStyle = null;

    /* --------------------------------------------------------- helpers -- */
    function idx(x, y) { return y * N + x; }

    function newPiece(rand) {
      var shape = rand.pick(SHAPES.bag);
      return {
        shape: shape,
        color: rand.int(0, COLORS.length - 1),
        placed: false,
        pop: 0
      };
    }

    function fits(piece, gx, gy) {
      if (!piece || piece.placed) return false;
      var c = piece.shape.c;
      for (var i = 0; i < c.length; i++) {
        var x = gx + c[i][0], y = gy + c[i][1];
        if (x < 0 || y < 0 || x >= N || y >= N) return false;
        if (S.grid[idx(x, y)]) return false;
      }
      return true;
    }

    function pieceFitsAnywhere(piece) {
      if (!piece || piece.placed) return false;
      for (var y = 0; y <= N - piece.shape.h; y++) {
        for (var x = 0; x <= N - piece.shape.w; x++) {
          if (fits(piece, x, y)) return true;
        }
      }
      return false;
    }

    function anyMoveLeft() {
      for (var i = 0; i < S.tray.length; i++) {
        if (pieceFitsAnywhere(S.tray[i])) return true;
      }
      return false;
    }

    function refillTray() {
      var all = S.tray.every(function (p) { return !p || p.placed; });
      if (!all) return;
      S.tray = [newPiece(S.rand), newPiece(S.rand), newPiece(S.rand)];
      S.trayPop = 0.3;
    }

    /* ----------------------------------------------------------- state -- */
    function begin() {
      S = {
        rand: A.rng((Date.now() & 0x7fffffff) || 7),
        grid: new Int8Array(N * N),
        cellPop: new Float32Array(N * N),
        tray: null,
        score: 0,
        combo: 0,
        bestCombo: 0,
        coins: 0,
        clears: 0,
        placements: 0,
        over: false,
        drag: null,
        trayPop: 0,
        heroCheer: 0,
        undos: save.upUndo,
        bombs: save.upBomb,
        history: null,
        t: 0
      };
      S.tray = [newPiece(S.rand), newPiece(S.rand), newPiece(S.rand)];

      // A few pre-set blocks give the first placement something to build against.
      var seedCount = Math.max(0, 6 - save.upClean * 2);
      for (var i = 0; i < seedCount; i++) {
        var x = S.rand.int(0, N - 1), y = S.rand.int(0, N - 1);
        S.grid[idx(x, y)] = S.rand.int(1, COLORS.length);
      }

      A.Fx.reset();
      paintHud();
    }

    function snapshot() {
      S.history = {
        grid: S.grid.slice(0),
        tray: S.tray.map(function (p) {
          return p ? { shape: p.shape, color: p.color, placed: p.placed, pop: 0 } : null;
        }),
        score: S.score,
        combo: S.combo,
        coins: S.coins
      };
    }

    function undo() {
      if (!S.history || S.undos <= 0) return;
      S.undos--;
      S.grid.set(S.history.grid);
      S.tray = S.history.tray;
      S.score = S.history.score;
      S.combo = S.history.combo;
      S.coins = S.history.coins;
      S.history = null;
      S.over = false;
      A.Audio.sfx("select");
      paintHud();
    }

    function useBomb(gx, gy) {
      if (S.bombs <= 0) return false;
      S.bombs--;

      var cleared = 0;
      for (var y = gy - 1; y <= gy + 1; y++) {
        for (var x = gx - 1; x <= gx + 1; x++) {
          if (x < 0 || y < 0 || x >= N || y >= N) continue;
          if (S.grid[idx(x, y)]) {
            S.grid[idx(x, y)] = 0;
            S.cellPop[idx(x, y)] = 0.3;
            cleared++;
            spark(x, y, [255, 145, 77]);
          }
        }
      }

      A.Audio.sfx("hit");
      A.Fx.kick(0.4);
      A.vibrate(30);
      paintHud();
      return cleared > 0;
    }

    /* --------------------------------------------------------- placing -- */
    function place(piece, gx, gy) {
      snapshot();

      var c = piece.shape.c;
      for (var i = 0; i < c.length; i++) {
        var x = gx + c[i][0], y = gy + c[i][1];
        S.grid[idx(x, y)] = piece.color + 1;
        S.cellPop[idx(x, y)] = 0.22;
      }

      piece.placed = true;
      S.placements++;
      S.score += c.length;

      resolveClears();
      refillTray();

      if (!anyMoveLeft()) finish();
      paintHud();
    }

    function resolveClears() {
      var fullRows = [], fullCols = [];

      for (var y = 0; y < N; y++) {
        var full = true;
        for (var x = 0; x < N; x++) { if (!S.grid[idx(x, y)]) { full = false; break; } }
        if (full) fullRows.push(y);
      }
      for (var cx = 0; cx < N; cx++) {
        var fullc = true;
        for (var cy = 0; cy < N; cy++) { if (!S.grid[idx(cx, cy)]) { fullc = false; break; } }
        if (fullc) fullCols.push(cx);
      }

      var lines = fullRows.length + fullCols.length;
      if (lines === 0) {
        S.combo = 0;
        A.Audio.sfx("select");
        return;
      }

      S.combo++;
      S.bestCombo = Math.max(S.bestCombo, S.combo);
      S.clears += lines;

      fullRows.forEach(function (y) {
        for (var x = 0; x < N; x++) {
          var color = COLORS[(S.grid[idx(x, y)] - 1) % COLORS.length];
          S.grid[idx(x, y)] = 0;
          S.cellPop[idx(x, y)] = 0.32;
          spark(x, y, color);
        }
      });
      fullCols.forEach(function (x) {
        for (var y2 = 0; y2 < N; y2++) {
          var color2 = COLORS[Math.max(0, S.grid[idx(x, y2)] - 1) % COLORS.length];
          S.grid[idx(x, y2)] = 0;
          S.cellPop[idx(x, y2)] = 0.32;
          spark(x, y2, color2);
        }
      });

      // Quadratic in lines, multiplied by the combo streak: clearing two at once
      // is worth far more than two separate clears, which is the whole game.
      var gained = lines * lines * 10 * (1 + (S.combo - 1) * 0.5);
      S.score += Math.round(gained);
      S.coins += Math.round(lines * 3 * (1 + save.upCoin * 0.15));

      S.heroCheer = 1.1;
      A.Fx.kick(0.25 + lines * 0.08);
      A.Fx.flashScreen(0.18 + lines * 0.05, [255, 255, 255]);
      A.Audio.sfx(lines > 1 ? "levelup" : "good");
      A.vibrate(lines > 1 ? [15, 30, 20] : 18);

      if (layout) {
        A.Fx.text(A.View.w / 2, A.View.h * 0.42, 0,
          (lines > 1 ? lines + "  LINES" : "CLEAR") + (S.combo > 1 ? "   x" + S.combo : ""),
          lines > 1 ? [255, 194, 75] : [63, 217, 138],
          { screen: true, scale: 1.2, life: 1, rise: -34 });
      }
    }

    function spark(gx, gy, color) {
      if (!layout) return;
      var p = cellCentre(gx, gy);
      A.Fx.burst(p.x, p.y, 0, 5, {
        color: color, speed: 150, up: 0, gravity: 380, life: 0.5,
        size: layout.cell * 0.10, screen: true, bounce: false
      });
    }

    /* ---------------------------------------------------------- finish -- */
    function finish() {
      if (S.over) return;
      S.over = true;

      var coins = Math.round(S.coins * host.coinMultiplier());
      save.coins += coins;
      save.runs++;
      save.best = Math.max(save.best, S.score);
      save.bestCombo = Math.max(save.bestCombo, S.bestCombo);
      host.commit();

      var xp = Math.round((S.score / 90 + S.clears * 2) * host.xpMultiplier());
      host.addXp(xp);
      host.progress("run", 1);
      host.progress("coins", coins);

      A.Audio.sfx("lose");
      A.vibrate(110);

      var buttons = [];
      if (S.history && save.upUndo > 0 && S.undos > 0) {
        buttons.push({
          label: "UNDO LAST MOVE", className: "gold", sub: S.undos + " left",
          onClick: function () { undo(); host.modal.hide(); }
        });
      }
      buttons.push({ label: "PLAY AGAIN", className: "go", onClick: function () { host.modal.hide(); begin(); } });
      buttons.push({ label: "UPGRADES", className: "gold", onClick: openShop });
      buttons.push({ label: "MENU", className: "ghost", onClick: host.exit });

      host.results({
        win: false,
        title: "NO MOVES LEFT",
        subtitle: S.score > save.best ? "New personal best" : "Best " + A.formatNumber(save.best),
        stats: [
          ["Score", A.formatNumber(S.score)],
          ["Lines", String(S.clears)],
          ["Best combo", "x" + S.bestCombo],
          ["Coins", "+" + A.formatNumber(coins)]
        ],
        buttons: buttons
      });
    }

    function openShop() {
      var rows = [
        { key: "upUndo", name: "Rewind", desc: "Start each game with an undo", max: 3,
          cost: function (l) { return Math.round(200 * Math.pow(1.6, l)); } },
        { key: "upBomb", name: "Demolition", desc: "Start with a 3x3 bomb charge", max: 3,
          cost: function (l) { return Math.round(260 * Math.pow(1.6, l)); } },
        { key: "upClean", name: "Clean Slate", desc: "Fewer pre-set blocks on the board", max: 3,
          cost: function (l) { return Math.round(320 * Math.pow(1.7, l)); } },
        { key: "upCoin", name: "Prospector", desc: "+15% coins from clears", max: 10,
          cost: function (l) { return Math.round(180 * Math.pow(1.35, l)); } }
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
      var w = A.View.w, h = A.View.h;
      var top = Math.max(118, h * 0.16);
      var bottomReserve = Math.max(150, h * 0.20);
      var available = Math.min(w - 32, h - top - bottomReserve);
      var board = Math.max(180, available);
      var cell = board / N;

      layout = {
        cell: cell,
        board: board,
        x: (w - board) / 2,
        y: top,
        trayY: top + board + Math.max(22, h * 0.035),
        trayCell: Math.max(14, cell * 0.52)
      };
    }

    function cellCentre(gx, gy) {
      return {
        x: layout.x + (gx + 0.5) * layout.cell,
        y: layout.y + (gy + 0.5) * layout.cell
      };
    }

    function traySlotRect(i) {
      var slotW = A.View.w / 3;
      return { x: slotW * i, y: layout.trayY, w: slotW, h: layout.trayCell * 5 };
    }

    /* ----------------------------------------------------------- input -- */
    function update(dt) {
      S.t += dt;
      if (S.heroCheer > 0) S.heroCheer -= dt;
      if (S.trayPop > 0) S.trayPop -= dt;
      for (var i = 0; i < S.cellPop.length; i++) {
        if (S.cellPop[i] > 0) S.cellPop[i] = Math.max(0, S.cellPop[i] - dt);
      }

      if (S.over) return;

      if (A.Input.pressed) {
        if (S.bombArmed) {
          var cell = pointToCell(A.Input.x, A.Input.y);
          if (cell) {
            useBomb(cell.x, cell.y);
            S.bombArmed = false;
            if (!anyMoveLeft()) finish();
          } else {
            S.bombArmed = false;
          }
          paintHud();
          return;
        }

        for (var t = 0; t < 3; t++) {
          var piece = S.tray[t];
          if (!piece || piece.placed) continue;
          var r = traySlotRect(t);
          if (A.Input.x >= r.x && A.Input.x <= r.x + r.w && A.Input.y >= r.y - 20) {
            S.drag = { piece: piece, slot: t, x: A.Input.x, y: A.Input.y };
            A.Audio.sfx("select");
            break;
          }
        }
      }

      if (S.drag) {
        S.drag.x = A.Input.x;
        S.drag.y = A.Input.y;

        // Lift the piece above the finger so it is never hidden by the hand.
        var target = pointToCell(S.drag.x, S.drag.y - layout.cell * 1.5, S.drag.piece);
        S.drag.gx = target ? target.x : -1;
        S.drag.gy = target ? target.y : -1;
        S.drag.valid = target ? fits(S.drag.piece, target.x, target.y) : false;

        if (A.Input.released) {
          if (S.drag.valid) place(S.drag.piece, S.drag.gx, S.drag.gy);
          else A.Audio.sfx("bad");
          S.drag = null;
        }
      }
    }

    /** Maps a screen point to the top-left grid cell a piece would occupy. */
    function pointToCell(px, py, piece) {
      var gx = Math.floor((px - layout.x) / layout.cell);
      var gy = Math.floor((py - layout.y) / layout.cell);

      if (piece) {
        gx -= Math.floor((piece.shape.w - 1) / 2);
        gy -= Math.floor((piece.shape.h - 1) / 2);
        gx = A.clamp(gx, 0, N - piece.shape.w);
        gy = A.clamp(gy, 0, N - piece.shape.h);
        return { x: gx, y: gy };
      }

      if (gx < 0 || gy < 0 || gx >= N || gy >= N) return null;
      return { x: gx, y: gy };
    }

    /* ---------------------------------------------------------- render -- */
    function render(g) {
      var w = A.View.w, h = A.View.h;

      var bg = g.createLinearGradient(0, 0, 0, h);
      bg.addColorStop(0, "#1B1438");
      bg.addColorStop(1, "#0E0B1E");
      g.fillStyle = bg;
      g.fillRect(0, 0, w, h);

      A.Fx.applyShake(g);

      drawBoard(g);
      drawGhost(g);
      drawTray(g);
      drawHero(g);
      drawDragged(g);

      A.Fx.drawParticles(g, null);
      A.Fx.drawTexts(g, null);

      g.restore();
      A.Fx.drawFlash(g, w, h);
    }

    function roundCell(g, x, y, size, radius) {
      A.roundRect(g, x, y, size, size, radius);
    }

    function drawBoard(g) {
      var l = layout;

      A.roundRect(g, l.x - 8, l.y - 8, l.board + 16, l.board + 16, 18);
      g.fillStyle = "#241A45";
      g.fill();
      g.strokeStyle = "#3A2C68";
      g.lineWidth = 3;
      g.stroke();

      var pad = l.cell * 0.06;
      for (var y = 0; y < N; y++) {
        for (var x = 0; x < N; x++) {
          var i = idx(x, y);
          var value = S.grid[i];
          var pop = S.cellPop[i];
          var cx = l.x + x * l.cell + pad;
          var cy = l.y + y * l.cell + pad;
          var size = l.cell - pad * 2;

          if (!value) {
            roundCell(g, cx, cy, size, size * 0.22);
            g.fillStyle = "rgba(255,255,255,0.045)";
            g.fill();
            continue;
          }

          var grow = pop > 0 ? 1 + Math.sin(A.clamp01(pop / 0.32) * Math.PI) * 0.18 : 1;
          var s2 = size * grow;
          var off = (size - s2) / 2;
          drawBlock(g, cx + off, cy + off, s2, COLORS[(value - 1) % COLORS.length]);
        }
      }
    }

    function drawBlock(g, x, y, size, color) {
      var r = size * 0.22;
      roundCell(g, x, y, size, r);
      g.fillStyle = A.rgb(A.shade(color, -0.35));
      g.fill();

      roundCell(g, x, y, size, r);
      var grad = g.createLinearGradient(0, y, 0, y + size);
      grad.addColorStop(0, A.rgb(A.shade(color, 0.28)));
      grad.addColorStop(0.55, A.rgb(color));
      grad.addColorStop(1, A.rgb(A.shade(color, -0.18)));
      g.fillStyle = grad;
      g.fill();

      A.roundRect(g, x + size * 0.14, y + size * 0.12, size * 0.72, size * 0.22, size * 0.11);
      g.fillStyle = "rgba(255,255,255,0.28)";
      g.fill();
    }

    function drawGhost(g) {
      if (!S.drag || S.drag.gx < 0) return;
      var l = layout;
      var c = S.drag.piece.shape.c;
      var pad = l.cell * 0.06;

      for (var i = 0; i < c.length; i++) {
        var x = S.drag.gx + c[i][0], y = S.drag.gy + c[i][1];
        if (x < 0 || y < 0 || x >= N || y >= N) continue;
        roundCell(g, l.x + x * l.cell + pad, l.y + y * l.cell + pad, l.cell - pad * 2, l.cell * 0.2);
        g.fillStyle = S.drag.valid ? "rgba(63,217,138,0.34)" : "rgba(255,77,109,0.28)";
        g.fill();
        g.strokeStyle = S.drag.valid ? "rgba(63,217,138,0.9)" : "rgba(255,77,109,0.8)";
        g.lineWidth = 2;
        g.stroke();
      }
    }

    function drawTray(g) {
      var l = layout;
      for (var t = 0; t < 3; t++) {
        var piece = S.tray[t];
        if (!piece || piece.placed) continue;
        if (S.drag && S.drag.slot === t) continue;

        var r = traySlotRect(t);
        var scale = S.trayPop > 0 ? 1 + Math.sin(A.clamp01(S.trayPop / 0.3) * Math.PI) * 0.14 : 1;
        var cell = l.trayCell * scale;
        var usable = pieceFitsAnywhere(piece);

        var ox = r.x + r.w / 2 - (piece.shape.w * cell) / 2;
        var oy = r.y + cell * 0.5;

        g.globalAlpha = usable ? 1 : 0.35;
        for (var i = 0; i < piece.shape.c.length; i++) {
          drawBlock(g,
            ox + piece.shape.c[i][0] * cell + cell * 0.06,
            oy + piece.shape.c[i][1] * cell + cell * 0.06,
            cell * 0.88, COLORS[piece.color]);
        }
        g.globalAlpha = 1;
      }
    }

    function drawDragged(g) {
      if (!S.drag) return;
      var cell = layout.cell;
      var piece = S.drag.piece;
      var ox = S.drag.x - (piece.shape.w * cell) / 2;
      var oy = S.drag.y - layout.cell * 1.5 - (piece.shape.h * cell) / 2;

      g.globalAlpha = 0.95;
      for (var i = 0; i < piece.shape.c.length; i++) {
        drawBlock(g,
          ox + piece.shape.c[i][0] * cell + cell * 0.06,
          oy + piece.shape.c[i][1] * cell + cell * 0.06,
          cell * 0.88, COLORS[piece.color]);
      }
      g.globalAlpha = 1;
    }

    function drawHero(g) {
      if (!heroStyle) return;
      var l = layout;
      var size = Math.min(64, l.cell * 1.5);
      var x = l.x + l.board - size * 0.5;
      var y = l.y - 10;

      var cheering = S.heroCheer > 0;
      var pose = A.Rig.pose(cheering ? "cheer" : "idle",
        A.wrap(S.t * (cheering ? 3 : 0.5), 1));
      A.Rig.draw(g, heroStyle, pose, x, y, size, -1, { noShadow: true });
    }

    /* -------------------------------------------------------------- ui -- */
    function mount(root) {
      var hud = host.el("div", "hud");

      var top = host.el("div", "row");
      ui.score = host.el("div", "chip", "0");
      ui.combo = host.el("div", "chip gold", "");
      ui.coins = host.el("div", "chip gold", "0");
      top.appendChild(ui.score);
      top.appendChild(ui.combo);
      top.appendChild(ui.coins);
      hud.appendChild(top);

      var tools = host.el("div", "row");
      tools.style.marginTop = "8px";
      tools.style.pointerEvents = "auto";

      ui.undo = host.el("button", "chip");
      ui.undo.style.pointerEvents = "auto";
      ui.undo.style.cursor = "pointer";
      ui.undo.addEventListener("click", undo);
      tools.appendChild(ui.undo);

      ui.bomb = host.el("button", "chip");
      ui.bomb.style.pointerEvents = "auto";
      ui.bomb.style.cursor = "pointer";
      ui.bomb.addEventListener("click", function () {
        if (S.bombs > 0) {
          S.bombArmed = !S.bombArmed;
          A.Audio.sfx("select");
          paintHud();
        }
      });
      tools.appendChild(ui.bomb);
      hud.appendChild(tools);

      ui.hint = host.el("div", "hint", "Drag a block onto the grid");
      hud.appendChild(ui.hint);

      root.appendChild(hud);
    }

    function paintHud() {
      if (!ui.score) return;
      ui.score.textContent = A.formatNumber(S.score);
      ui.coins.textContent = A.formatNumber(S.coins);
      ui.combo.textContent = S.combo > 1 ? "COMBO x" + S.combo : "";
      ui.combo.style.opacity = S.combo > 1 ? "1" : "0";
      ui.undo.textContent = "UNDO " + S.undos;
      ui.undo.style.opacity = S.undos > 0 && S.history ? "1" : "0.4";
      ui.bomb.textContent = "BOMB " + S.bombs;
      ui.bomb.style.opacity = S.bombs > 0 ? "1" : "0.4";
      ui.bomb.style.borderColor = S.bombArmed ? "#FFC24B" : "";
      ui.hint.textContent = S.bombArmed ? "Tap a cell to detonate"
        : (S.placements < 2 ? "Drag a block onto the grid" : "");
    }

    return {
      mount: mount,
      start: function () {
        if (!heroStyle) {
          heroStyle = A.Rig.style(6060, {
            hue: host.hue, helmet: "crest", cape: true, weapon: "none", bulk: 1.0
          });
        }
        computeLayout();
        begin();
      },
      stop: function () { S = null; },
      update: function (dt) { if (S) update(dt); },
      render: function (g) { if (S && layout) render(g); },
      onResize: function () { computeLayout(); }
    };
  }

  /* ------------------------------------------------------ registration -- */
  A.games.push({
    id: "blocks",
    name: "Block Storm",
    tagline: "Eight-by-eight block puzzle. Three pieces at a time, no rotation, chain the combos.",
    accent: "#5DA8FF",
    unlock: 1,
    template: { coins: 0, runs: 0, best: 0, bestCombo: 0,
                upUndo: 0, upBomb: 0, upClean: 0, upCoin: 0 },
    bestLine: function (s) {
      return s.runs ? "Best " + A.formatNumber(s.best) + "  ·  combo x" + s.bestCombo : "New";
    },
    thumb: function (g, w, h, t) {
      g.fillStyle = "#1B1438";
      g.fillRect(0, 0, w, h);

      var n = 5, pad = 6;
      var cell = Math.min((w - pad * 2) / n, (h - pad * 2) / n);
      var ox = (w - cell * n) / 2, oy = (h - cell * n) / 2;
      var wave = Math.floor(t * 2) % n;

      for (var y = 0; y < n; y++) {
        for (var x = 0; x < n; x++) {
          var on = (x + y * 2 + Math.floor(t)) % 3 !== 0;
          var lit = y === wave;
          A.roundRect(g, ox + x * cell + 1.5, oy + y * cell + 1.5, cell - 3, cell - 3, 4);
          if (!on) { g.fillStyle = "rgba(255,255,255,0.07)"; g.fill(); continue; }
          var c = COLORS[(x + y) % COLORS.length];
          g.fillStyle = A.rgb(lit ? A.shade(c, 0.35) : c);
          g.fill();
        }
      }
    },
    create: create
  });
})(window.A);
