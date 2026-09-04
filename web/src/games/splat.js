/* ===========================================================================
   ROLLER SPLAT
   Voodoo's paint-the-maze puzzle. Swipe a direction, the ball rolls until it
   hits a wall, painting everything it crosses. Cover every tile to clear.
   One rule, no timer, and a satisfying "oh, THAT order" moment per level.
   =========================================================================== */
(function (A) {
  "use strict";
  A.games = A.games || [];

  /* '#' wall, '.' floor, 'o' ball start. Hand built so each has one tidy solution. */
  var LEVELS = [
    ["#######",
     "#o....#",
     "#.###.#",
     "#.....#",
     "#.###.#",
     "#.....#",
     "#######"],

    ["#########",
     "#o......#",
     "#.#####.#",
     "#.#...#.#",
     "#.#.#.#.#",
     "#...#...#",
     "#########"],

    ["#########",
     "#.......#",
     "#.#####.#",
     "#o#...#.#",
     "#.#.#.#.#",
     "#...#...#",
     "#.#####.#",
     "#.......#",
     "#########"],

    ["##########",
     "#o.......#",
     "#.##..##.#",
     "#.#....#.#",
     "#....##..#",
     "#.##....##",
     "#....##..#",
     "#.######.#",
     "#........#",
     "##########"],

    ["###########",
     "#o........#",
     "#.###.###.#",
     "#...#...#.#",
     "###.###.#.#",
     "#.......#.#",
     "#.#####.#.#",
     "#.#...#...#",
     "#.#.#.#####",
     "#...#.....#",
     "###########"],

    ["###########",
     "#....#....#",
     "#.##.#.##.#",
     "#.#o....#.#",
     "#.#.###.#.#",
     "#...#.#...#",
     "###.#.#.###",
     "#.....#...#",
     "#.#######.#",
     "#.........#",
     "###########"],

    ["############",
     "#o.........#",
     "#.####.###.#",
     "#.#......#.#",
     "#.#.####.#.#",
     "#.#.#..#.#.#",
     "#.#.#..#.#.#",
     "#.#.####.#.#",
     "#.#......#.#",
     "#.########.#",
     "#..........#",
     "############"],

    ["############",
     "#....##....#",
     "#.##.##.##.#",
     "#.#......#.#",
     "#.#.####.#.#",
     "#o..#..#...#",
     "####.#.#.###",
     "#....#.#...#",
     "#.####.####.",
     "#..........#",
     "############"]
  ];

  function create(host) {
    var save = host.save;
    var S = null;
    var layout = null;
    var ui = {};

    var WALL = 1, FLOOR = 0;

    function begin() {
      var spec = LEVELS[(save.level - 1) % LEVELS.length];
      var h = spec.length, w = spec[0].length;

      S = {
        w: w, h: h,
        cell: new Uint8Array(w * h),
        painted: new Uint8Array(w * h),
        pop: new Float32Array(w * h),
        total: 0,
        done: 0,
        ball: { x: 0, y: 0, sx: 0, sy: 0 },
        moving: null,
        moves: 0,
        over: false,
        t: 0,
        trail: [],
        hue: A.wrap((save.level * 0.13), 1)
      };

      for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
          var ch = spec[y][x] || "#";
          var i = y * w + x;
          if (ch === "#") { S.cell[i] = WALL; continue; }
          S.cell[i] = FLOOR;
          S.total++;
          if (ch === "o") { S.ball.x = x; S.ball.y = y; }
        }
      }

      S.ball.sx = S.ball.x;
      S.ball.sy = S.ball.y;
      paint(S.ball.x, S.ball.y);

      A.Fx.reset();
      computeLayout();
      paintHud();
    }

    function paint(x, y) {
      var i = y * S.w + x;
      if (S.cell[i] === WALL || S.painted[i]) return;
      S.painted[i] = 1;
      S.pop[i] = 0.26;
      S.done++;
    }

    function computeLayout() {
      if (!S) return;
      var vw = A.View.w, vh = A.View.h;
      var top = Math.max(112, vh * 0.16);
      var bottom = Math.max(80, vh * 0.12);
      var cell = Math.min((vw - 34) / S.w, (vh - top - bottom) / S.h);

      layout = {
        cell: cell,
        x: (vw - cell * S.w) / 2,
        y: top + ((vh - top - bottom) - cell * S.h) / 2
      };
    }

    /* ---------------------------------------------------------- rolling -- */
    function tryRoll(dx, dy) {
      if (S.moving || S.over) return;

      var x = S.ball.x, y = S.ball.y;
      var path = [];
      while (true) {
        var nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= S.w || ny >= S.h) break;
        if (S.cell[ny * S.w + nx] === WALL) break;
        x = nx; y = ny;
        path.push([x, y]);
      }

      if (!path.length) { A.Audio.sfx("bad"); return; }

      S.moving = { path: path, index: 0, timer: 0, step: 0.035 };
      S.moves++;
      A.Audio.sfx("select");
      A.vibrate(10);
    }

    function update(dt) {
      S.t += dt;

      for (var i = 0; i < S.pop.length; i++) {
        if (S.pop[i] > 0) S.pop[i] = Math.max(0, S.pop[i] - dt);
      }

      if (S.moving) {
        var m = S.moving;
        m.timer += dt;
        while (m.timer >= m.step && m.index < m.path.length) {
          m.timer -= m.step;
          var cellPos = m.path[m.index++];
          S.ball.x = cellPos[0];
          S.ball.y = cellPos[1];
          paint(S.ball.x, S.ball.y);

          if (layout) {
            S.trail.push({ x: S.ball.x, y: S.ball.y, t: 0 });
          }
        }

        S.ball.sx = A.approach(S.ball.sx, S.ball.x, 26, dt);
        S.ball.sy = A.approach(S.ball.sy, S.ball.y, 26, dt);

        if (m.index >= m.path.length &&
            Math.abs(S.ball.sx - S.ball.x) < 0.03 && Math.abs(S.ball.sy - S.ball.y) < 0.03) {
          S.moving = null;
          A.Fx.kick(0.12);
          A.Audio.sfx("hit");
          if (S.done >= S.total) finish();
        }
      } else {
        S.ball.sx = A.approach(S.ball.sx, S.ball.x, 26, dt);
        S.ball.sy = A.approach(S.ball.sy, S.ball.y, 26, dt);
      }

      for (var t = S.trail.length - 1; t >= 0; t--) {
        S.trail[t].t += dt;
        if (S.trail[t].t > 0.5) S.trail.splice(t, 1);
      }

      var swipe = A.Input.consumeSwipe();
      if (swipe === "left") tryRoll(-1, 0);
      else if (swipe === "right") tryRoll(1, 0);
      else if (swipe === "up") tryRoll(0, -1);
      else if (swipe === "down") tryRoll(0, 1);

      paintHud();
    }

    function finish() {
      if (S.over) return;
      S.over = true;

      var par = Math.max(4, Math.round(S.total / 7));
      var stars = S.moves <= par ? 3 : (S.moves <= par * 1.5 ? 2 : 1);
      var coins = Math.round((25 + save.level * 6 + stars * 15) *
        host.coinMultiplier() * (1 + save.upCoin * 0.15));

      save.coins += coins;
      save.runs++;
      save.level++;
      save.stars += stars;
      host.commit();

      var xp = Math.round((16 + save.level * 2) * host.xpMultiplier());
      host.addXp(xp);
      host.progress("run", 1);
      host.progress("coins", coins);

      A.Audio.sfx("win");
      A.Fx.flashScreen(0.4, [120, 230, 255]);
      A.Fx.kick(0.4);
      A.vibrate([18, 36, 24]);

      setTimeout(function () {
        host.results({
          win: true,
          title: "ALL PAINTED",
          subtitle: "★".repeat(stars) + "☆".repeat(3 - stars),
          stats: [
            ["Level", String(save.level - 1)],
            ["Moves", S.moves + " / " + par],
            ["Stars", String(save.stars)],
            ["Coins", "+" + A.formatNumber(coins)]
          ],
          buttons: [
            { label: "NEXT LEVEL", className: "go", onClick: function () { host.modal.hide(); begin(); } },
            { label: "UPGRADES", className: "gold", onClick: openShop },
            { label: "MENU", className: "ghost", onClick: host.exit }
          ]
        });
      }, 520);
    }

    function openShop() {
      var card = host.el("div", "card");
      card.appendChild(host.el("h2", null, "UPGRADES"));
      card.appendChild(host.el("p", "sub", A.formatNumber(save.coins) + " coins"));

      var rows = [
        { key: "upCoin", name: "Pigment", desc: "+15% coins per level", max: 10,
          cost: function (l) { return Math.round(160 * Math.pow(1.35, l)); } },
        { key: "upSkip", name: "Skip Token", desc: "Jump a level you are stuck on", max: 5,
          cost: function (l) { return Math.round(500 * Math.pow(1.4, l)); } }
      ];

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

      if (save.upSkip > 0) {
        card.appendChild(host.button("SKIP THIS LEVEL", "gold", function () {
          save.upSkip--;
          save.level++;
          host.commit();
          host.modal.hide();
          begin();
        }, save.upSkip + " tokens"));
      }

      card.appendChild(host.button("BACK", "ghost", function () { host.modal.hide(); begin(); }));
      host.modal.show(card);
    }

    /* ---------------------------------------------------------- render -- */
    function render(g) {
      var vw = A.View.w, vh = A.View.h;
      var l = layout;

      var paintColor = A.hsl(S.hue, 0.72, 0.58);
      var bg = g.createLinearGradient(0, 0, 0, vh);
      bg.addColorStop(0, "#15102C");
      bg.addColorStop(1, "#0B0818");
      g.fillStyle = bg;
      g.fillRect(0, 0, vw, vh);

      A.Fx.applyShake(g);

      A.roundRect(g, l.x - 12, l.y - 12, l.cell * S.w + 24, l.cell * S.h + 24, 20);
      g.fillStyle = "#1D1638";
      g.fill();
      g.strokeStyle = "#332A5C";
      g.lineWidth = 3;
      g.stroke();

      for (var y = 0; y < S.h; y++) {
        for (var x = 0; x < S.w; x++) {
          var i = y * S.w + x;
          var px = l.x + x * l.cell, py = l.y + y * l.cell;

          if (S.cell[i] === WALL) {
            A.roundRect(g, px + 1, py + 1, l.cell - 2, l.cell - 2, l.cell * 0.16);
            g.fillStyle = "#2C2450";
            g.fill();
            continue;
          }

          A.roundRect(g, px + 2, py + 2, l.cell - 4, l.cell - 4, l.cell * 0.18);
          g.fillStyle = "rgba(255,255,255,0.05)";
          g.fill();

          if (!S.painted[i]) continue;

          var grow = S.pop[i] > 0 ? 1 + Math.sin(A.clamp01(S.pop[i] / 0.26) * Math.PI) * 0.25 : 1;
          var size = (l.cell - 3) * grow;
          var off = (l.cell - size) / 2;
          A.roundRect(g, px + off, py + off, size, size, size * 0.22);
          g.fillStyle = A.rgb(paintColor);
          g.fill();
        }
      }

      // Ball
      var bx = l.x + (S.ball.sx + 0.5) * l.cell;
      var by = l.y + (S.ball.sy + 0.5) * l.cell;
      var r = l.cell * 0.34;

      g.fillStyle = "rgba(0,0,0,0.35)";
      g.beginPath();
      g.ellipse(bx, by + r * 0.55, r * 0.9, r * 0.35, 0, 0, A.TAU);
      g.fill();

      var grad = g.createRadialGradient(bx - r * 0.35, by - r * 0.4, r * 0.1, bx, by, r * 1.2);
      grad.addColorStop(0, A.rgb(A.shade(paintColor, 0.55)));
      grad.addColorStop(0.6, A.rgb(paintColor));
      grad.addColorStop(1, A.rgb(A.shade(paintColor, -0.4)));
      g.fillStyle = grad;
      g.beginPath();
      g.arc(bx, by, r, 0, A.TAU);
      g.fill();
      g.strokeStyle = "rgba(15,11,28,0.65)";
      g.lineWidth = 2.5;
      g.stroke();

      A.Fx.drawParticles(g, null);
      A.Fx.drawTexts(g, null);

      g.restore();
      A.Fx.drawFlash(g, vw, vh);
    }

    /* -------------------------------------------------------------- ui -- */
    function mount(root) {
      var hud = host.el("div", "hud");

      var top = host.el("div", "row");
      ui.level = host.el("div", "chip", "LEVEL 1");
      ui.meter = host.el("div", "meter");
      ui.fill = host.el("i");
      ui.meter.appendChild(ui.fill);
      ui.coins = host.el("div", "chip gold", "0");
      top.appendChild(ui.level);
      top.appendChild(ui.meter);
      top.appendChild(ui.coins);
      hud.appendChild(top);

      ui.cap = host.el("div", "cap", "");
      hud.appendChild(ui.cap);

      ui.hint = host.el("div", "hint", "Swipe to roll  ·  paint every tile");
      hud.appendChild(ui.hint);

      root.appendChild(hud);
    }

    function paintHud() {
      if (!ui.level) return;
      ui.level.textContent = "LEVEL " + save.level;
      ui.coins.textContent = A.formatNumber(save.coins);
      ui.fill.style.width = A.clamp01(S.done / S.total) * 100 + "%";
      ui.cap.textContent = S.done + " / " + S.total + " painted   ·   " + S.moves + " moves";
      if (S.moves > 0) ui.hint.textContent = "";
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
    id: "splat",
    name: "Roller Splat",
    tagline: "Swipe the ball, it rolls until it hits a wall. Paint every tile in the maze.",
    accent: "#5DE0FF",
    unlock: 1,
    template: { coins: 0, runs: 0, level: 1, stars: 0, upCoin: 0, upSkip: 0 },
    bestLine: function (s) { return s.runs ? "Level " + s.level + "  ·  " + s.stars + " stars" : "New"; },
    thumb: function (g, w, h, t) {
      g.fillStyle = "#15102C";
      g.fillRect(0, 0, w, h);

      var n = 6, cell = Math.min(w, h) / n;
      var ox = (w - cell * n) / 2, oy = (h - cell * n) / 2;
      var progress = A.wrap(t * 0.35, 1);

      for (var y = 0; y < n; y++) {
        for (var x = 0; x < n; x++) {
          var wall = (x === 2 && y > 0 && y < 4) || (x === 4 && y < 3);
          A.roundRect(g, ox + x * cell + 1, oy + y * cell + 1, cell - 2, cell - 2, 3);
          if (wall) { g.fillStyle = "#2C2450"; g.fill(); continue; }
          var order = (y * n + x) / (n * n);
          g.fillStyle = order < progress ? "#5DE0FF" : "rgba(255,255,255,0.06)";
          g.fill();
        }
      }

      var bi = Math.floor(progress * n * n);
      g.fillStyle = "#BFF3FF";
      g.beginPath();
      g.arc(ox + ((bi % n) + 0.5) * cell, oy + (Math.floor(bi / n) + 0.5) * cell, cell * 0.34, 0, A.TAU);
      g.fill();
    },
    create: create
  });
})(window.A);
