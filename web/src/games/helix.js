/* ===========================================================================
   HELIX DROP
   The Voodoo helix tower. Drag to spin the tower, drop the ball through the
   gaps, and never land on red. Three platforms cleared without a bounce turns
   the ball into a smasher that punches straight through everything, which is
   the entire reason the format is compulsive.
   =========================================================================== */
(function (A) {
  "use strict";
  A.games = A.games || [];

  var TAU = A.TAU;
  var SEGMENTS = 12;
  var SEG = TAU / SEGMENTS;

  var SOLID = 0, GAP = 1, DEADLY = 2;

  var PALETTES = [
    { top: "#2B1B4D", bot: "#150C29", ring: [124, 92, 255], ring2: [92, 66, 200], ball: [255, 196, 74] },
    { top: "#0E3346", bot: "#071A26", ring: [64, 200, 190], ring2: [40, 150, 150], ball: [255, 120, 90] },
    { top: "#3A1430", bot: "#1C0918", ring: [255, 122, 160], ring2: [200, 80, 120], ball: [120, 230, 255] },
    { top: "#14331F", bot: "#081A10", ring: [104, 214, 120], ring2: [66, 160, 88], ball: [255, 214, 92] }
  ];

  function create(host) {
    var save = host.save;
    var S = null;
    var ui = {};
    var heroStyle = null;

    /* ----------------------------------------------------------- level -- */
    function buildTower(level) {
      var rand = A.rng(level * 48271 + 7);
      var count = 12 + Math.min(26, level * 2);
      var platforms = [];

      for (var i = 0; i < count; i++) {
        var segs = new Uint8Array(SEGMENTS);
        var difficulty = A.clamp01(i / count) * A.clamp01(level / 12);

        // Every platform gets one guaranteed run of gaps, so there is always a way through.
        var gapWidth = Math.max(2, 4 - Math.floor(level / 6));
        var gapStart = rand.int(0, SEGMENTS - 1);
        for (var gsi = 0; gsi < gapWidth; gsi++) {
          segs[(gapStart + gsi) % SEGMENTS] = GAP;
        }

        var extraGaps = rand.int(0, 2);
        for (var e = 0; e < extraGaps; e++) segs[rand.int(0, SEGMENTS - 1)] = GAP;

        var deadly = Math.round(A.lerp(1, 5, difficulty)) + (i === 0 ? -1 : 0);
        for (var d = 0; d < deadly; d++) {
          var slot = rand.int(0, SEGMENTS - 1);
          if (segs[slot] === GAP) continue;
          segs[slot] = DEADLY;
        }

        platforms.push({ segs: segs, y: i + 1, broken: 0, hit: 0 });
      }

      return platforms;
    }

    function begin(keepLevel) {
      var level = keepLevel ? save.level : save.level;
      var palette = PALETTES[(level - 1) % PALETTES.length];

      S = {
        level: level,
        palette: palette,
        platforms: buildTower(level),
        rot: 0,
        rotVel: 0,
        ball: { y: 0, vy: 0, squash: 1 },
        camY: 0,
        combo: 0,
        smashing: false,
        passed: 0,
        coins: 0,
        over: false,
        won: false,
        t: 0,
        shield: save.upShield,
        started: false,
        heroCheer: 0
      };
      A.Fx.reset();
      paintHud();
    }

    /* -------------------------------------------------------- mechanics -- */
    /** Which segment is currently facing the player, at the bottom of the ellipse. */
    function frontSegment() {
      return Math.floor(A.wrap(Math.PI / 2 - S.rot, TAU) / SEG) % SEGMENTS;
    }

    function update(dt) {
      S.t += dt;
      if (S.heroCheer > 0) S.heroCheer -= dt;
      if (S.over) return;

      if (!S.started) {
        if (A.Input.pressed || A.Input.tapped) {
          S.started = true;
          save.runs++;
          host.commit();
          if (ui.hint) ui.hint.textContent = "";
        }
      }

      // Spin: drag anywhere, or arrow keys.
      if (A.Input.down) S.rotVel -= (A.Input.dx / A.View.w) * 9;
      if (A.Input.axis) S.rotVel -= A.Input.axis * 3.4 * dt;
      S.rot += S.rotVel * dt;
      S.rotVel *= Math.pow(0.0008, dt);

      if (!S.started) return;

      var previous = S.ball.y;
      S.ball.vy += 34 * dt;
      S.ball.y += S.ball.vy * dt;
      S.ball.squash = A.approach(S.ball.squash, 1, 12, dt);

      for (var i = 0; i < S.platforms.length; i++) {
        var platform = S.platforms[i];
        if (platform.broken > 0) continue;
        if (!(previous < platform.y && S.ball.y >= platform.y)) continue;

        var kind = platform.segs[frontSegment()];

        if (kind === GAP) {
          S.combo++;
          S.passed++;
          S.coins += 1;
          if (S.combo >= 3 && !S.smashing) {
            S.smashing = true;
            host.toast("SMASH");
            A.Audio.sfx("levelup");
            A.Fx.flashScreen(0.35, [255, 196, 74]);
          }
          A.Audio.sfx("select");
          continue;
        }

        if (S.smashing) {
          platform.broken = 0.001;
          S.passed++;
          S.coins += 2;
          A.Fx.kick(0.3);
          A.Audio.sfx("hit");
          A.vibrate(16);
          burstRing(platform, S.palette.ring);
          continue;
        }

        if (kind === DEADLY) {
          if (S.shield > 0) {
            S.shield--;
            platform.broken = 0.001;
            host.toast("SHIELD USED");
            A.Fx.flashScreen(0.4, [120, 220, 255]);
            burstRing(platform, [120, 220, 255]);
            paintHud();
            continue;
          }
          S.ball.y = platform.y;
          finish(false);
          return;
        }

        // Solid: bounce, lose the streak.
        S.ball.y = platform.y - 0.001;
        S.ball.vy = -11;
        S.ball.squash = 0.62;
        S.combo = 0;
        S.smashing = false;
        platform.hit = 0.22;
        A.Fx.kick(0.14);
        A.Audio.sfx("jump");
        break;
      }

      for (var b = 0; b < S.platforms.length; b++) {
        if (S.platforms[b].broken > 0) S.platforms[b].broken += dt * 2.4;
        if (S.platforms[b].hit > 0) S.platforms[b].hit -= dt;
      }

      if (S.ball.y > S.platforms.length + 0.6) { finish(true); return; }

      S.camY = A.approach(S.camY, S.ball.y, 9, dt);
      paintHud();
    }

    function burstRing(platform, color) {
      var geo = geometry();
      var y = geo.screenY(platform.y);
      A.Fx.burst(A.View.w / 2, y, 0, 16, {
        color: color, speed: 260, up: 60, gravity: 500, life: 0.6,
        size: 5, screen: true, bounce: false, square: true
      });
    }

    /* ---------------------------------------------------------- finish -- */
    function finish(won) {
      if (S.over) return;
      S.over = true;
      S.won = won;
      S.heroCheer = won ? 2 : 0;

      var coins = Math.round((S.coins + (won ? 25 + S.level * 5 : 0)) *
        host.coinMultiplier() * (1 + save.upCoin * 0.15));
      save.coins += coins;
      save.bestDepth = Math.max(save.bestDepth, S.passed);
      if (won) { save.level++; save.wins++; }
      host.commit();

      var xp = Math.round((S.passed * 2 + (won ? 20 : 4)) * host.xpMultiplier());
      host.addXp(xp);
      host.progress("run", 1);
      host.progress("coins", coins);

      if (won) {
        A.Audio.sfx("win");
        A.Fx.flashScreen(0.45, [255, 214, 92]);
        A.Fx.kick(0.5);
        A.vibrate([20, 40, 30]);
      } else {
        A.Audio.sfx("lose");
        A.Fx.flashScreen(0.55, [255, 80, 80]);
        A.Fx.kick(0.7);
        A.vibrate(120);
      }

      setTimeout(function () {
        host.results({
          win: won,
          title: won ? "TOWER CLEARED" : "SPLAT",
          subtitle: won ? "Level " + (save.level - 1) + " done" : "Landed on red",
          stats: [
            ["Level", String(S.level)],
            ["Platforms", String(S.passed)],
            ["Best depth", String(save.bestDepth)],
            ["Coins", "+" + A.formatNumber(coins)]
          ],
          buttons: [
            { label: won ? "NEXT TOWER" : "TRY AGAIN", className: "go",
              onClick: function () { host.modal.hide(); begin(); } },
            { label: "UPGRADES", className: "gold", onClick: openShop },
            { label: "MENU", className: "ghost", onClick: host.exit }
          ]
        });
      }, 620);
    }

    function openShop() {
      var rows = [
        { key: "upShield", name: "Bumper", desc: "Survive one red platform per tower", max: 3,
          cost: function (l) { return Math.round(350 * Math.pow(1.7, l)); } },
        { key: "upCoin", name: "Jackpot", desc: "+15% coins", max: 10,
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

    /* ---------------------------------------------------------- render -- */
    function geometry() {
      var w = A.View.w, h = A.View.h;
      var radius = Math.min(w * 0.40, h * 0.22);
      var spacing = Math.max(52, h * 0.115);
      var ballY = h * 0.34;

      return {
        cx: w / 2,
        radius: radius,
        inner: radius * 0.30,
        squash: 0.34,
        spacing: spacing,
        thickness: Math.max(9, spacing * 0.20),
        screenY: function (y) { return ballY + (y - S.camY) * spacing; }
      };
    }

    function render(g) {
      var w = A.View.w, h = A.View.h;
      var geo = geometry();

      var bg = g.createLinearGradient(0, 0, 0, h);
      bg.addColorStop(0, S.palette.top);
      bg.addColorStop(1, S.palette.bot);
      g.fillStyle = bg;
      g.fillRect(0, 0, w, h);

      A.Fx.applyShake(g);

      drawPillar(g, geo);

      for (var i = 0; i < S.platforms.length; i++) {
        var platform = S.platforms[i];
        var y = geo.screenY(platform.y);
        if (y < -80 || y > h + 80) continue;
        drawPlatform(g, geo, platform, y);
      }

      drawGoal(g, geo);
      drawBall(g, geo);

      A.Fx.drawParticles(g, null);
      A.Fx.drawTexts(g, null);

      g.restore();
      A.Fx.drawFlash(g, w, h);
    }

    function drawPillar(g, geo) {
      var h = A.View.h;
      var grad = g.createLinearGradient(geo.cx - geo.inner, 0, geo.cx + geo.inner, 0);
      grad.addColorStop(0, "rgba(0,0,0,0.55)");
      grad.addColorStop(0.45, "rgba(255,255,255,0.10)");
      grad.addColorStop(1, "rgba(0,0,0,0.55)");
      g.fillStyle = "#1A1230";
      g.fillRect(geo.cx - geo.inner, 0, geo.inner * 2, h);
      g.fillStyle = grad;
      g.fillRect(geo.cx - geo.inner, 0, geo.inner * 2, h);
    }

    function ringBand(g, geo, y, a0, a1, outer, inner) {
      g.beginPath();
      g.ellipse(geo.cx, y, outer, outer * geo.squash, 0, a0, a1);
      g.ellipse(geo.cx, y, inner, inner * geo.squash, 0, a1, a0, true);
      g.closePath();
    }

    function drawPlatform(g, geo, platform, y) {
      var broken = platform.broken;
      var lift = broken > 0 ? broken * 26 : 0;
      var alpha = broken > 0 ? Math.max(0, 1 - broken) : 1;
      if (alpha <= 0) return;

      var hitDrop = platform.hit > 0 ? Math.sin(A.clamp01(platform.hit / 0.22) * Math.PI) * 4 : 0;
      var top = y + hitDrop - lift;

      g.globalAlpha = alpha;

      for (var pass = 0; pass < 2; pass++) {
        var yy = pass === 0 ? top + geo.thickness : top;

        for (var i = 0; i < SEGMENTS; i++) {
          var kind = platform.segs[i];
          if (kind === GAP) continue;

          var a0 = i * SEG + S.rot;
          var a1 = a0 + SEG;

          ringBand(g, geo, yy, a0 + 0.008, a1 - 0.008, geo.radius, geo.inner);

          var base = kind === DEADLY ? [235, 62, 74] : S.palette.ring;
          g.fillStyle = pass === 0
            ? A.rgb(A.shade(base, -0.45))
            : A.rgb(kind === DEADLY ? base : A.shade(base, (i % 2) * 0.12 - 0.02));
          g.fill();
        }
      }

      g.globalAlpha = 1;
    }

    function drawGoal(g, geo) {
      var y = geo.screenY(S.platforms.length + 1);
      if (y < -60 || y > A.View.h + 120) return;

      g.beginPath();
      g.ellipse(geo.cx, y, geo.radius * 1.05, geo.radius * 1.05 * geo.squash, 0, 0, TAU);
      g.fillStyle = "rgba(63,217,138,0.30)";
      g.fill();
      g.strokeStyle = "#3FD98A";
      g.lineWidth = 4;
      g.stroke();

      if (heroStyle) {
        var cheering = S.heroCheer > 0;
        var pose = A.Rig.pose(cheering ? "cheer" : "idle", A.wrap(S.t * (cheering ? 3 : 0.5), 1));
        A.Rig.draw(g, heroStyle, pose, geo.cx, y + 6, Math.min(70, geo.radius * 0.7), 1, { noShadow: true });
      }
    }

    function drawBall(g, geo) {
      var y = geo.screenY(S.ball.y);
      var r = Math.max(11, geo.radius * 0.17);
      var sx = r / S.ball.squash, sy = r * S.ball.squash;

      if (S.smashing) {
        g.globalAlpha = 0.4;
        g.fillStyle = "#FFC24B";
        g.beginPath();
        g.ellipse(geo.cx, y, sx * 1.7, sy * 1.7, 0, 0, TAU);
        g.fill();
        g.globalAlpha = 1;
      }

      var grad = g.createRadialGradient(geo.cx - sx * 0.35, y - sy * 0.4, r * 0.15, geo.cx, y, r * 1.2);
      var ball = S.smashing ? [255, 236, 150] : S.palette.ball;
      grad.addColorStop(0, A.rgb(A.shade(ball, 0.5)));
      grad.addColorStop(0.6, A.rgb(ball));
      grad.addColorStop(1, A.rgb(A.shade(ball, -0.4)));

      g.fillStyle = grad;
      g.beginPath();
      g.ellipse(geo.cx, y, sx, sy, 0, 0, TAU);
      g.fill();
      g.strokeStyle = "rgba(20,14,34,0.6)";
      g.lineWidth = 2.5;
      g.stroke();
    }

    /* -------------------------------------------------------------- ui -- */
    function mount(root) {
      var hud = host.el("div", "hud");

      var top = host.el("div", "row");
      ui.level = host.el("div", "chip", "TOWER 1");
      ui.depth = host.el("div", "meter");
      ui.depthFill = host.el("i");
      ui.depth.appendChild(ui.depthFill);
      ui.coins = host.el("div", "chip gold", "0");
      top.appendChild(ui.level);
      top.appendChild(ui.depth);
      top.appendChild(ui.coins);
      hud.appendChild(top);

      ui.combo = host.el("div", "bigNum", "");
      hud.appendChild(ui.combo);

      ui.hint = host.el("div", "hint", "Drag to spin  ·  find the gaps");
      hud.appendChild(ui.hint);

      root.appendChild(hud);
    }

    function paintHud() {
      if (!ui.level) return;
      ui.level.textContent = "TOWER " + S.level;
      ui.coins.textContent = A.formatNumber(S.coins);
      ui.depthFill.style.width = A.clamp01(S.ball.y / (S.platforms.length + 1)) * 100 + "%";
      ui.combo.textContent = S.smashing ? "SMASH" : (S.combo > 1 ? "x" + S.combo : "");
      ui.combo.style.color = S.smashing ? "#FFC24B" : "#fff";
      ui.combo.style.fontSize = S.smashing ? "44px" : "";
    }

    return {
      mount: mount,
      start: function () {
        if (!heroStyle) {
          heroStyle = A.Rig.style(8800, {
            hue: host.hue, helmet: "crest", cape: true, weapon: "none"
          });
        }
        begin();
      },
      stop: function () { S = null; },
      update: function (dt) { if (S) update(dt); },
      render: function (g) { if (S) render(g); },
      onResize: function () {}
    };
  }

  /* ------------------------------------------------------ registration -- */
  A.games.push({
    id: "helix",
    name: "Helix Drop",
    tagline: "Spin the tower, drop through the gaps, never touch red. Three in a row and you smash.",
    accent: "#7C5CFF",
    unlock: 2,
    template: { coins: 0, runs: 0, wins: 0, level: 1, bestDepth: 0, upShield: 0, upCoin: 0 },
    bestLine: function (s) {
      return s.runs ? "Tower " + s.level + "  ·  best depth " + s.bestDepth : "New";
    },
    thumb: function (g, w, h, t) {
      var bg = g.createLinearGradient(0, 0, 0, h);
      bg.addColorStop(0, "#2B1B4D");
      bg.addColorStop(1, "#150C29");
      g.fillStyle = bg;
      g.fillRect(0, 0, w, h);

      var cx = w / 2, radius = w * 0.36, inner = radius * 0.3, squash = 0.34;
      g.fillStyle = "#1A1230";
      g.fillRect(cx - inner, 0, inner * 2, h);

      for (var p = 0; p < 3; p++) {
        var y = h * (0.32 + p * 0.24) + Math.sin(t * 1.4 + p) * 2;
        for (var i = 0; i < 12; i++) {
          if ((i + p * 4 + Math.floor(t)) % 5 === 0) continue;
          var a0 = i * (A.TAU / 12) + t * 0.6;
          g.beginPath();
          g.ellipse(cx, y, radius, radius * squash, 0, a0 + 0.02, a0 + A.TAU / 12 - 0.02);
          g.ellipse(cx, y, inner, inner * squash, 0, a0 + A.TAU / 12 - 0.02, a0 + 0.02, true);
          g.closePath();
          g.fillStyle = (i + p) % 7 === 0 ? "#EB3E4A" : "#7C5CFF";
          g.fill();
        }
      }

      g.fillStyle = "#FFC44A";
      g.beginPath();
      g.arc(cx, h * 0.2 + Math.abs(Math.sin(t * 3)) * 8, w * 0.07, 0, A.TAU);
      g.fill();
    },
    create: create
  });
})(window.A);
