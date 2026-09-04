/* ===========================================================================
   PAPER TERRITORY
   Voodoo's paper.io. Drive out of your own colour, loop back, and everything
   you enclosed becomes yours. Get clipped on your own trail, or anyone else's,
   and you lose the lot. Three rivals do the same thing to you.
   =========================================================================== */
(function (A) {
  "use strict";
  A.games = A.games || [];

  var W = 56, H = 56;
  var TAU = A.TAU;

  var COLORS = [
    [90, 170, 255],   // player
    [255, 110, 110],
    [110, 220, 150],
    [255, 190, 80]
  ];

  var DIRS = [[1, 0], [0, 1], [-1, 0], [0, -1]];

  function create(host) {
    var save = host.save;
    var S = null;
    var ui = {};

    function idx(x, y) { return y * W + x; }
    function inside(x, y) { return x >= 0 && y >= 0 && x < W && y < H; }

    /* ----------------------------------------------------------- setup -- */
    function begin() {
      S = {
        owner: new Uint8Array(W * H),      // 0 = neutral, else player index + 1
        trail: new Uint8Array(W * H),      // same encoding, marks live trails
        players: [],
        t: 0,
        over: false,
        won: false,
        coins: 0,
        peak: 0,
        kills: 0,
        goal: 0.32 + Math.min(0.2, save.level * 0.012),
        level: save.level,
        shield: save.upShield
      };

      var spots = [[12, 12], [W - 13, 12], [12, H - 13], [W - 13, H - 13]];
      A.rng(save.runs * 131 + 5).shuffle(spots);

      for (var p = 0; p < 4; p++) {
        var spot = spots[p];
        var player = {
          index: p,
          x: spot[0], y: spot[1],
          fx: spot[0], fy: spot[1],
          dir: p % 4,
          nextDir: p % 4,
          alive: true,
          out: 0,
          trailCells: [],
          ai: p > 0,
          plan: 0,
          share: 0,
          speed: p === 0 ? 7.2 + save.upSpeed * 0.35 : 6.6 + Math.random() * 0.6
        };
        S.players.push(player);

        for (var dy = -3; dy <= 3; dy++) {
          for (var dx = -3; dx <= 3; dx++) {
            if (inside(spot[0] + dx, spot[1] + dy)) {
              S.owner[idx(spot[0] + dx, spot[1] + dy)] = p + 1;
            }
          }
        }
      }

      recount();
      A.Fx.reset();
      paintHud();
    }

    function recount() {
      var counts = [0, 0, 0, 0];
      for (var i = 0; i < S.owner.length; i++) {
        var o = S.owner[i];
        if (o) counts[o - 1]++;
      }
      for (var p = 0; p < 4; p++) S.players[p].share = counts[p] / (W * H);
      S.peak = Math.max(S.peak, S.players[0].share);
    }

    /* -------------------------------------------------------- capturing -- */
    /**
     * Flood from the border across everything that is not mine and not my trail.
     * Whatever the flood cannot reach was enclosed, so it becomes mine.
     */
    function capture(player) {
      var mine = player.index + 1;
      var seen = new Uint8Array(W * H);
      var queue = [];

      for (var x = 0; x < W; x++) {
        if (!seen[idx(x, 0)]) { seen[idx(x, 0)] = 1; queue.push(idx(x, 0)); }
        if (!seen[idx(x, H - 1)]) { seen[idx(x, H - 1)] = 1; queue.push(idx(x, H - 1)); }
      }
      for (var y = 0; y < H; y++) {
        if (!seen[idx(0, y)]) { seen[idx(0, y)] = 1; queue.push(idx(0, y)); }
        if (!seen[idx(W - 1, y)]) { seen[idx(W - 1, y)] = 1; queue.push(idx(W - 1, y)); }
      }

      // Border cells that are already mine do not let the flood through.
      queue = queue.filter(function (i) {
        if (S.owner[i] === mine || S.trail[i] === mine) { seen[i] = 0; return false; }
        return true;
      });

      while (queue.length) {
        var current = queue.pop();
        var cx = current % W, cy = (current / W) | 0;
        for (var d = 0; d < 4; d++) {
          var nx = cx + DIRS[d][0], ny = cy + DIRS[d][1];
          if (!inside(nx, ny)) continue;
          var ni = idx(nx, ny);
          if (seen[ni]) continue;
          if (S.owner[ni] === mine || S.trail[ni] === mine) continue;
          seen[ni] = 1;
          queue.push(ni);
        }
      }

      var gained = 0;
      for (var i2 = 0; i2 < S.owner.length; i2++) {
        if (seen[i2]) continue;
        if (S.owner[i2] !== mine) gained++;
        S.owner[i2] = mine;
      }

      for (var t = 0; t < player.trailCells.length; t++) S.trail[player.trailCells[t]] = 0;
      player.trailCells.length = 0;
      player.out = 0;

      if (gained > 0 && player.index === 0) {
        S.coins += Math.round(gained * 0.35 * (1 + save.upCoin * 0.15));
        A.Audio.sfx("coin");
        A.Fx.kick(0.2);
        A.Fx.flashScreen(0.14, COLORS[0]);
      }

      recount();
      return gained;
    }

    function killPlayer(player, byPlayer) {
      if (!player.alive) return;

      if (player.index === 0 && S.shield > 0) {
        S.shield--;
        for (var s = 0; s < player.trailCells.length; s++) S.trail[player.trailCells[s]] = 0;
        player.trailCells.length = 0;
        player.out = 0;
        host.toast("SHIELD USED");
        A.Fx.flashScreen(0.4, [120, 220, 255]);
        paintHud();
        return;
      }

      player.alive = false;
      for (var i = 0; i < player.trailCells.length; i++) S.trail[player.trailCells[i]] = 0;
      player.trailCells.length = 0;

      // Their land goes neutral, which is what makes hunting rivals worth it.
      var mark = player.index + 1;
      for (var c = 0; c < S.owner.length; c++) if (S.owner[c] === mark) S.owner[c] = 0;

      A.Fx.burst(player.x, 0, player.y, 20, {
        color: COLORS[player.index], speed: 6, up: 6, life: 0.7, size: 0.6, bounce: false
      });

      if (player.index === 0) {
        A.Audio.sfx("lose");
        A.vibrate(120);
        finish(false);
      } else {
        if (byPlayer && byPlayer.index === 0) {
          S.kills++;
          S.coins += 60;
          host.toast("RIVAL ELIMINATED");
          A.Audio.sfx("levelup");
        }
        recount();
      }
    }

    /* -------------------------------------------------------- stepping -- */
    function stepInto(player, nx, ny) {
      if (!inside(nx, ny)) { killPlayer(player, null); return; }

      var target = idx(nx, ny);
      var mine = player.index + 1;

      // Running into any live trail kills its owner.
      var trailOwner = S.trail[target];
      if (trailOwner) {
        if (trailOwner === mine) { killPlayer(player, null); return; }
        killPlayer(S.players[trailOwner - 1], player);
      }

      player.x = nx;
      player.y = ny;

      if (S.owner[target] === mine) {
        if (player.trailCells.length) capture(player);
      } else {
        S.trail[target] = mine;
        player.trailCells.push(target);
        player.out++;
      }
    }

    function updatePlayer(player, dt) {
      if (!player.alive) return;

      var d = DIRS[player.dir];
      player.fx += d[0] * player.speed * dt;
      player.fy += d[1] * player.speed * dt;

      while (Math.abs(player.fx - player.x) >= 1 || Math.abs(player.fy - player.y) >= 1) {
        var nx = player.x + d[0], ny = player.y + d[1];
        stepInto(player, nx, ny);
        if (!player.alive) return;

        // Turns only land on cell boundaries, which keeps the trail on the grid.
        if (player.nextDir !== player.dir && (player.nextDir + 2) % 4 !== player.dir) {
          player.dir = player.nextDir;
          d = DIRS[player.dir];
          player.fx = player.x;
          player.fy = player.y;
        }
      }
    }

    function updateAi(player, dt) {
      if (!player.alive) return;
      player.plan -= dt;
      if (player.plan > 0) return;

      player.plan = 0.25 + Math.random() * 0.5;
      var mine = player.index + 1;

      // Head home once the excursion is long enough, otherwise wander outward.
      if (player.out > 14 + Math.random() * 20) {
        var best = -1, bestScore = Infinity;
        for (var d = 0; d < 4; d++) {
          if ((d + 2) % 4 === player.dir) continue;
          var nx = player.x + DIRS[d][0] * 3, ny = player.y + DIRS[d][1] * 3;
          if (!inside(nx, ny)) continue;
          if (S.trail[idx(nx, ny)] === mine) continue;
          var score = nearestOwnDistance(player, nx, ny);
          if (score < bestScore) { bestScore = score; best = d; }
        }
        if (best >= 0) player.nextDir = best;
      } else if (Math.random() < 0.5) {
        var options = [];
        for (var d2 = 0; d2 < 4; d2++) {
          if ((d2 + 2) % 4 === player.dir) continue;
          var tx = player.x + DIRS[d2][0] * 2, ty = player.y + DIRS[d2][1] * 2;
          if (!inside(tx, ty)) continue;
          if (S.trail[idx(tx, ty)] === mine) continue;
          options.push(d2);
        }
        if (options.length) player.nextDir = options[(Math.random() * options.length) | 0];
      }
    }

    function nearestOwnDistance(player, x, y) {
      var mine = player.index + 1;
      var best = 1e9;
      for (var r = 1; r < 14; r++) {
        for (var a = 0; a < 8; a++) {
          var px = x + Math.round(Math.cos(a / 8 * TAU) * r);
          var py = y + Math.round(Math.sin(a / 8 * TAU) * r);
          if (!inside(px, py)) continue;
          if (S.owner[idx(px, py)] === mine) return r;
        }
      }
      return best;
    }

    /* --------------------------------------------------------- update --- */
    function update(dt) {
      S.t += dt;
      if (S.over) return;

      var swipe = A.Input.consumeSwipe();
      var me = S.players[0];
      if (swipe === "right") me.nextDir = 0;
      else if (swipe === "down") me.nextDir = 1;
      else if (swipe === "left") me.nextDir = 2;
      else if (swipe === "up") me.nextDir = 3;

      if (A.Input.keys["arrowright"] || A.Input.keys["d"]) me.nextDir = 0;
      if (A.Input.keys["arrowdown"] || A.Input.keys["s"]) me.nextDir = 1;
      if (A.Input.keys["arrowleft"] || A.Input.keys["a"]) me.nextDir = 2;
      if (A.Input.keys["arrowup"] || A.Input.keys["w"]) me.nextDir = 3;

      for (var p = 0; p < S.players.length; p++) {
        if (S.players[p].ai) updateAi(S.players[p], dt);
        updatePlayer(S.players[p], dt);
      }

      if (!S.over && S.players[0].share >= S.goal) finish(true);

      var rivals = S.players.filter(function (x) { return x.ai && x.alive; });
      if (!S.over && rivals.length === 0) finish(true);

      paintHud();
    }

    /* --------------------------------------------------------- finish --- */
    function finish(won) {
      if (S.over) return;
      S.over = true;
      S.won = won;

      var share = Math.round(S.peak * 100);
      var coins = Math.round((S.coins + (won ? 60 + save.level * 8 : 0)) * host.coinMultiplier());
      save.coins += coins;
      save.runs++;
      save.bestShare = Math.max(save.bestShare, share);
      if (won) { save.level++; save.wins++; }
      host.commit();

      var xp = Math.round((share * 1.2 + S.kills * 12 + (won ? 20 : 0)) * host.xpMultiplier());
      host.addXp(xp);
      host.progress("run", 1);
      host.progress("coins", coins);

      if (won) { A.Audio.sfx("win"); A.Fx.flashScreen(0.4, COLORS[0]); A.vibrate([20, 40, 30]); }

      setTimeout(function () {
        host.results({
          win: won,
          title: won ? "TERRITORY HELD" : "CUT OFF",
          subtitle: won ? "Target reached" : "Something crossed your trail",
          stats: [
            ["Land", share + "%"],
            ["Target", Math.round(S.goal * 100) + "%"],
            ["Rivals out", String(S.kills)],
            ["Coins", "+" + A.formatNumber(coins)]
          ],
          buttons: [
            { label: won ? "NEXT MAP" : "TRY AGAIN", className: "go",
              onClick: function () { host.modal.hide(); begin(); } },
            { label: "UPGRADES", className: "gold", onClick: openShop },
            { label: "MENU", className: "ghost", onClick: host.exit }
          ]
        });
      }, 520);
    }

    function openShop() {
      var rows = [
        { key: "upSpeed", name: "Sprinter", desc: "Move faster than the rivals", max: 6,
          cost: function (l) { return Math.round(300 * Math.pow(1.45, l)); } },
        { key: "upShield", name: "Safety Line", desc: "Survive one cut per map", max: 3,
          cost: function (l) { return Math.round(450 * Math.pow(1.7, l)); } },
        { key: "upCoin", name: "Land Value", desc: "+15% coins per capture", max: 10,
          cost: function (l) { return Math.round(200 * Math.pow(1.35, l)); } }
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

    /* --------------------------------------------------------- render --- */
    function render(g) {
      var vw = A.View.w, vh = A.View.h;
      var me = S.players[0];

      var view = 26;
      var cell = Math.max(6, Math.min(vw, vh * 0.78) / view);
      var camX = me.fx, camY = me.fy;
      var ox = vw / 2 - camX * cell;
      var oy = vh * 0.52 - camY * cell;

      g.fillStyle = "#0D0B1A";
      g.fillRect(0, 0, vw, vh);

      A.Fx.applyShake(g);

      var x0 = Math.max(0, Math.floor(-ox / cell) - 1);
      var x1 = Math.min(W - 1, Math.ceil((vw - ox) / cell) + 1);
      var y0 = Math.max(0, Math.floor(-oy / cell) - 1);
      var y1 = Math.min(H - 1, Math.ceil((vh - oy) / cell) + 1);

      g.fillStyle = "#171233";
      g.fillRect(ox, oy, W * cell, H * cell);

      for (var y = y0; y <= y1; y++) {
        for (var x = x0; x <= x1; x++) {
          var i = idx(x, y);
          var owner = S.owner[i];
          var trail = S.trail[i];
          if (!owner && !trail) continue;

          var px = ox + x * cell, py = oy + y * cell;

          if (owner) {
            g.fillStyle = A.rgb(A.shade(COLORS[owner - 1], -0.18));
            g.fillRect(px, py, cell + 0.5, cell + 0.5);
          }
          if (trail) {
            g.fillStyle = A.rgb(A.shade(COLORS[trail - 1], 0.35));
            g.fillRect(px + cell * 0.16, py + cell * 0.16, cell * 0.68, cell * 0.68);
          }
        }
      }

      g.strokeStyle = "rgba(255,255,255,0.16)";
      g.lineWidth = 3;
      g.strokeRect(ox, oy, W * cell, H * cell);

      for (var p = 0; p < S.players.length; p++) {
        var player = S.players[p];
        if (!player.alive) continue;

        var bx = ox + (player.fx + 0.5) * cell;
        var by = oy + (player.fy + 0.5) * cell;
        var r = cell * 0.62;

        g.fillStyle = "rgba(0,0,0,0.35)";
        A.roundRect(g, bx - r, by - r + 2, r * 2, r * 2, r * 0.38);
        g.fill();

        A.roundRect(g, bx - r, by - r, r * 2, r * 2, r * 0.38);
        g.fillStyle = A.rgb(COLORS[p]);
        g.fill();
        g.strokeStyle = "rgba(12,9,24,0.75)";
        g.lineWidth = 2;
        g.stroke();

        g.fillStyle = "#12091F";
        var eye = r * 0.22;
        g.beginPath();
        g.arc(bx - r * 0.3, by - r * 0.12, eye, 0, TAU);
        g.arc(bx + r * 0.3, by - r * 0.12, eye, 0, TAU);
        g.fill();
      }

      drawMinimap(g, vw, vh);

      A.Fx.drawParticles(g, function (x, y, z) {
        return { x: ox + (x + 0.5) * cell, y: oy + (z + 0.5) * cell - y * cell, s: cell };
      });
      A.Fx.drawTexts(g, null);

      g.restore();
      A.Fx.drawFlash(g, vw, vh);
    }

    function drawMinimap(g, vw, vh) {
      var size = Math.min(96, vw * 0.26);
      var px = vw - size - 14;
      var py = vh - size - 18;
      var scale = size / W;

      A.roundRect(g, px - 4, py - 4, size + 8, size + 8, 10);
      g.fillStyle = "rgba(10,8,22,0.8)";
      g.fill();
      g.strokeStyle = "rgba(255,255,255,0.18)";
      g.lineWidth = 2;
      g.stroke();

      var step = 2;
      for (var y = 0; y < H; y += step) {
        for (var x = 0; x < W; x += step) {
          var owner = S.owner[idx(x, y)];
          if (!owner) continue;
          g.fillStyle = A.rgb(COLORS[owner - 1]);
          g.fillRect(px + x * scale, py + y * scale, scale * step, scale * step);
        }
      }

      var me = S.players[0];
      g.fillStyle = "#FFFFFF";
      g.fillRect(px + me.fx * scale - 1.5, py + me.fy * scale - 1.5, 3, 3);
    }

    /* -------------------------------------------------------------- ui -- */
    function mount(root) {
      var hud = host.el("div", "hud");

      var top = host.el("div", "row");
      ui.share = host.el("div", "chip", "0%");
      ui.meter = host.el("div", "meter");
      ui.fill = host.el("i");
      ui.meter.appendChild(ui.fill);
      ui.coins = host.el("div", "chip gold", "0");
      top.appendChild(ui.share);
      top.appendChild(ui.meter);
      top.appendChild(ui.coins);
      hud.appendChild(top);

      ui.cap = host.el("div", "cap", "");
      hud.appendChild(ui.cap);

      ui.hint = host.el("div", "hint", "Swipe to turn  ·  loop back to claim");
      hud.appendChild(ui.hint);

      root.appendChild(hud);
    }

    function paintHud() {
      if (!ui.share) return;
      var me = S.players[0];
      var pct = me.share * 100;
      ui.share.textContent = pct.toFixed(1) + "%";
      ui.coins.textContent = A.formatNumber(S.coins);
      ui.fill.style.width = A.clamp01(me.share / S.goal) * 100 + "%";
      ui.cap.textContent = "target " + Math.round(S.goal * 100) + "%" +
        (S.shield > 0 ? "   ·   " + S.shield + " safety line" : "");
      if (S.t > 3) ui.hint.textContent = "";
    }

    return {
      mount: mount,
      start: begin,
      stop: function () { S = null; },
      update: function (dt) { if (S) update(dt); },
      render: function (g) { if (S) render(g); },
      onResize: function () {}
    };
  }

  A.games.push({
    id: "paper",
    name: "Paper Territory",
    tagline: "Drive out, loop back, claim the ground. Three rivals want the same map.",
    accent: "#5AAAFF",
    unlock: 2,
    template: { coins: 0, runs: 0, wins: 0, level: 1, bestShare: 0,
                upSpeed: 0, upShield: 0, upCoin: 0 },
    bestLine: function (s) { return s.runs ? "Map " + s.level + "  ·  best " + s.bestShare + "%" : "New"; },
    thumb: function (g, w, h, t) {
      g.fillStyle = "#171233";
      g.fillRect(0, 0, w, h);

      var n = 12, cell = Math.max(w, h) / n;
      for (var y = 0; y < n; y++) {
        for (var x = 0; x < n; x++) {
          var d = Math.hypot(x - 4, y - 6);
          var d2 = Math.hypot(x - 9, y - 4);
          if (d < 3.4 + Math.sin(t) * 0.5) { g.fillStyle = "#3E77C4"; g.fillRect(x * cell, y * cell, cell, cell); }
          else if (d2 < 2.6) { g.fillStyle = "#C45A5A"; g.fillRect(x * cell, y * cell, cell, cell); }
        }
      }

      var ang = t * 1.2;
      var bx = w * 0.5 + Math.cos(ang) * w * 0.3;
      var by = h * 0.5 + Math.sin(ang) * h * 0.28;
      g.fillStyle = "#8CC6FF";
      for (var i = 0; i < 6; i++) {
        var a2 = ang - i * 0.16;
        g.fillRect(w * 0.5 + Math.cos(a2) * w * 0.3 - 2, h * 0.5 + Math.sin(a2) * h * 0.28 - 2, 4, 4);
      }
      A.roundRect(g, bx - 6, by - 6, 12, 12, 3);
      g.fillStyle = "#5AAAFF";
      g.fill();
    },
    create: create
  });
})(window.A);
