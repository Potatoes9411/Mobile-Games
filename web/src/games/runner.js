/* ===========================================================================
   ROOFTOP RUN
   Three-lane endless runner. Swipe across to switch lanes, up to vault, down to
   slide. Speed climbs with distance, power-ups stack, and the meta shop turns
   coins into longer magnets, stronger shields and a head start.
   =========================================================================== */
(function (A) {
  "use strict";
  A.games = A.games || [];

  var TAU = A.TAU;
  var LANES = [-3.3, 0, 3.3];
  var CHUNK = 60;
  var GRAVITY = 46;
  var JUMP_V = 15.5;

  var PAL = {
    skyTop: [22, 16, 46], skyMid: [86, 44, 104], skyLow: [246, 128, 96],
    sun: [255, 214, 150], fog: [232, 146, 120],
    far: [58, 40, 84], near: [36, 26, 60],
    roof: [92, 84, 116], roofDark: [66, 60, 88], lane: [226, 214, 240],
    edge: [46, 40, 66], gap: [16, 12, 30],
    gold: [255, 194, 75], jade: [63, 217, 138], red: [240, 77, 90],
    cyan: [123, 224, 255], violet: [185, 139, 255], ink: [26, 20, 40]
  };

  var OBSTACLES = {
    barrier: { h: 1.15, w: 2.3, need: "air", color: [240, 77, 90] },
    beam:    { h: 1.1, w: 2.6, need: "slide", color: [255, 194, 75], y: 1.55 },
    block:   { h: 3.0, w: 2.4, need: "dodge", color: [120, 96, 160] },
    gap:     { h: 0, w: 3.0, need: "air", color: [16, 12, 30] }
  };

  function create(host) {
    var save = host.save;
    var cam = A.Camera3D();
    var style = null;
    var S = null;
    var ui = {};

    function bake() {
      style = A.Rig.style(5150, {
        hue: host.hue, sat: 0.74, helmet: "hood", cape: true, weapon: "none", bulk: 0.98
      });
    }

    /* --------------------------------------------------------- state ---- */
    function begin() {
      S = {
        t: 0,
        z: 0,
        speed: 14,
        lane: 1,
        x: 0, targetX: 0, vx: 0,
        y: 0, vy: 0,
        air: false,
        slide: 0,
        roll: 0,
        alive: true,
        started: false,
        coins: 0,
        dist: 0,
        best: save.best,
        shield: save.upShield > 0 ? 1 : 0,
        magnet: 0,
        boost: 0,
        obstacles: [],
        pickups: [],
        props: [],
        nextChunk: 0,
        revives: host.reviveCharges() + (save.upRevive || 0),
        hitFlash: 0
      };

      S.z = save.upHeadStart * 60;
      S.dist = S.z;
      for (var i = 0; i < 5; i++) buildChunk();
      A.Fx.reset();
      paintHud();
    }

    /* ------------------------------------------------- level streaming -- */
    function buildChunk() {
      var index = S.nextChunk++;
      var rand = A.rng(index * 7919 + 31);
      var z0 = index * CHUNK;

      // The first stretch is empty so the player is never killed before they move.
      if (index === 0) {
        for (var p = 0; p < 6; p++) addProps(rand, z0 + p * 10);
        return;
      }

      var difficulty = A.clamp01(index / 26);
      var rows = 3 + Math.round(difficulty * 3);

      for (var r = 0; r < rows; r++) {
        var z = z0 + 12 + r * ((CHUNK - 16) / rows) + rand.range(-2, 2);
        var kinds = ["barrier", "beam", "block"];
        if (index > 4) kinds.push("gap");

        var blocked = [];
        var count = rand() < 0.3 + difficulty * 0.4 ? 2 : 1;

        for (var c = 0; c < count; c++) {
          var lane = rand.int(0, 2);
          if (blocked.indexOf(lane) >= 0) continue;
          blocked.push(lane);

          var kind = rand.pick(kinds);
          if (kind === "gap" && count > 1) kind = "barrier";
          S.obstacles.push({ kind: kind, lane: lane, z: z, hit: false });
        }

        // Never seal all three lanes with something that cannot be beaten.
        if (blocked.length >= 3) S.obstacles.pop();

        // Coins reward the open lane.
        var free = [0, 1, 2].filter(function (l) { return blocked.indexOf(l) < 0; });
        if (free.length) {
          var coinLane = rand.pick(free);
          var arc = rand() < 0.35;
          for (var k = 0; k < 5; k++) {
            S.pickups.push({
              kind: "coin", lane: coinLane, z: z + 2 + k * 1.6,
              y: arc ? 0.6 + Math.sin((k / 4) * Math.PI) * 2.4 : 0.9,
              taken: false
            });
          }
        }
      }

      if (rand() < 0.45) {
        var kinds2 = ["magnet", "shield", "boost"];
        S.pickups.push({
          kind: rand.pick(kinds2), lane: rand.int(0, 2),
          z: z0 + rand.range(20, CHUNK - 10), y: 1.2, taken: false
        });
      }

      for (var q = 0; q < 6; q++) addProps(rand, z0 + q * 10 + rand.range(0, 6));
    }

    function addProps(rand, z) {
      for (var side = -1; side <= 1; side += 2) {
        if (rand() < 0.4) continue;
        S.props.push({
          x: side * (6.5 + rand.range(0, 9)),
          z: z,
          kind: rand() < 0.55 ? "unit" : (rand() < 0.7 ? "vent" : "antenna"),
          h: rand.range(0.8, 2.6),
          w: rand.range(1.0, 2.2)
        });
      }
    }

    function prune() {
      var behind = S.z - 24;
      S.obstacles = S.obstacles.filter(function (o) { return o.z > behind; });
      S.pickups = S.pickups.filter(function (p) { return p.z > behind && !p.taken; });
      S.props = S.props.filter(function (p) { return p.z > behind; });
      while (S.nextChunk * CHUNK < S.z + 260) buildChunk();
    }

    /* -------------------------------------------------------- actions --- */
    function moveLane(delta) {
      var next = A.clamp(S.lane + delta, 0, 2);
      if (next === S.lane) return;
      S.lane = next;
      S.targetX = LANES[S.lane];
      A.Audio.sfx("select");
    }

    function jump() {
      if (S.air || S.slide > 0) return;
      S.vy = JUMP_V;
      S.air = true;
      A.Audio.sfx("jump");
      A.Fx.burst(S.x, 0.2, S.z, 8, { color: PAL.roof, speed: 3, up: 4, life: 0.4, size: 0.12 });
    }

    function slide() {
      if (S.slide > 0) return;
      if (S.air) { S.vy = -JUMP_V * 0.9; }
      S.slide = 0.7;
      A.Audio.sfx("slide");
    }

    /* --------------------------------------------------------- update --- */
    function update(dt) {
      S.t += dt;

      if (!S.alive) return;

      if (!S.started) {
        if (A.Input.pressed || A.Input.tapped || A.Input.swipe) {
          S.started = true;
          save.runs++;
          host.commit();
          ui.hint.textContent = "";
        } else {
          moveCamera(dt);
          return;
        }
      }

      var swipe = A.Input.consumeSwipe();
      if (swipe === "left") moveLane(-1);
      else if (swipe === "right") moveLane(1);
      else if (swipe === "up") jump();
      else if (swipe === "down") slide();

      if (A.Input.keys["a"] || A.Input.keys["arrowleft"]) { if (!S._lk) { moveLane(-1); S._lk = 1; } } else S._lk = 0;
      if (A.Input.keys["d"] || A.Input.keys["arrowright"]) { if (!S._rk) { moveLane(1); S._rk = 1; } } else S._rk = 0;

      var boosting = S.boost > 0;
      var target = 14 + S.dist / 240 + (boosting ? 12 : 0);
      S.speed = A.approach(S.speed, Math.min(38, target), 2.5, dt);

      S.z += S.speed * dt;
      S.dist = S.z;

      var step = A.smoothDamp(S.x, S.targetX, S.vx, 0.11, 40, dt);
      S.x = step[0]; S.vx = step[1];

      if (S.air) {
        S.vy -= GRAVITY * dt;
        S.y += S.vy * dt;
        if (S.y <= 0) { S.y = 0; S.vy = 0; S.air = false; }
      }
      if (S.slide > 0) S.slide -= dt;
      if (S.roll > 0) S.roll -= dt;
      if (S.magnet > 0) S.magnet -= dt;
      if (S.boost > 0) S.boost -= dt;
      if (S.hitFlash > 0) S.hitFlash -= dt;

      collide();
      prune();
      moveCamera(dt);
      paintHud();
    }

    function collide() {
      var laneX = LANES[S.lane];

      for (var i = 0; i < S.obstacles.length; i++) {
        var o = S.obstacles[i];
        if (o.hit || Math.abs(o.z - S.z) > 1.1) continue;
        if (o.lane !== S.lane) continue;

        var def = OBSTACLES[o.kind];
        var cleared = false;

        if (def.need === "air") cleared = S.y > 0.85;
        else if (def.need === "slide") cleared = S.slide > 0;
        else cleared = false;

        if (cleared) {
          o.hit = true;
          continue;
        }

        o.hit = true;
        takeHit(o);
        return;
      }

      var pull = S.magnet > 0 ? 7 : 1.6;
      for (var j = 0; j < S.pickups.length; j++) {
        var p = S.pickups[j];
        if (p.taken) continue;
        var dz = p.z - S.z;
        if (dz < -2 || dz > 6) continue;

        var px = LANES[p.lane];
        var near = Math.abs(px - S.x) < pull && Math.abs(dz) < (S.magnet > 0 ? 5 : 1.4);
        if (!near) continue;
        if (p.kind === "coin" && S.magnet <= 0 && Math.abs(p.y - (S.y + 0.9)) > 1.9) continue;

        p.taken = true;
        collect(p);
      }
    }

    function collect(p) {
      if (p.kind === "coin") {
        var value = 1 + save.upCoinValue;
        S.coins += value;
        A.Audio.sfx("coin");
        A.Fx.burst(LANES[p.lane], p.y, p.z, 4, { color: PAL.gold, speed: 2.5, up: 4, life: 0.4, size: 0.14 });
        return;
      }

      if (p.kind === "magnet") {
        S.magnet = 5 + save.upMagnet * 1.6;
        host.toast("MAGNET");
      } else if (p.kind === "shield") {
        S.shield = 1;
        host.toast("SHIELD");
      } else if (p.kind === "boost") {
        S.boost = 3.2 + save.upBoost * 0.9;
        S.roll = 0.5;
        host.toast("BOOST");
      }

      A.Audio.sfx("gem");
      A.Fx.flashScreen(0.25, PAL.cyan);
      A.Fx.burst(LANES[p.lane], p.y, p.z, 14, { color: PAL.cyan, speed: 5, up: 7, life: 0.6, size: 0.2 });
    }

    function takeHit(o) {
      if (S.boost > 0) {
        A.Fx.burst(LANES[o.lane], 1, o.z, 16, { color: PAL.violet, speed: 7, up: 8, life: 0.5, size: 0.22, square: true });
        return;
      }

      if (S.shield > 0) {
        S.shield = 0;
        S.hitFlash = 0.4;
        A.Fx.kick(0.5);
        A.Fx.flashScreen(0.4, PAL.cyan);
        A.Audio.sfx("hit");
        A.vibrate(40);
        host.toast("SHIELD BROKEN");
        return;
      }

      S.alive = false;
      A.Fx.kick(0.8);
      A.Fx.flashScreen(0.5, PAL.red);
      A.Fx.hitStop(0.1);
      A.Audio.sfx("lose");
      A.vibrate(120);
      A.Fx.burst(S.x, 1, S.z, 22, { color: PAL.red, speed: 6, up: 8, life: 0.8, size: 0.24, square: true });
      setTimeout(finish, 700);
    }

    function moveCamera(dt, snap) {
      cam.fit(A.View.w, A.View.h, 0.34, 0.60);
      var y = 6.9 + S.y * 0.35;
      var z = S.z - 10 - (S.boost > 0 ? 1.4 : 0);
      if (snap) cam.moveTo(S.x * 0.4, y, z);
      else cam.moveTo(S.x * 0.4, y, z, 9, dt);
    }

    /* --------------------------------------------------------- finish --- */
    function finish() {
      if (!S || S.phase === "done") return;
      S.phase = "done";

      var distance = Math.round(S.dist);
      var coins = Math.round(S.coins * host.coinMultiplier() * (1 + save.upCoinValue * 0.1));
      var score = distance + S.coins * 5;

      save.coins += coins;
      save.best = Math.max(save.best, distance);
      save.bestScore = Math.max(save.bestScore, score);
      host.commit();

      var xp = Math.round((distance / 22 + S.coins * 0.4) * host.xpMultiplier());
      host.addXp(xp);
      host.progress("run", 1);
      host.progress("coins", coins);
      host.progress("distance", distance);

      var buttons = [];
      if (S.revives > 0) {
        buttons.push({
          label: "SECOND WIND", className: "gold", sub: "Keep the run going",
          onClick: function () {
            S.revives--;
            S.alive = true;
            S.phase = "run";
            S.shield = 1;
            S.obstacles = S.obstacles.filter(function (o) { return o.z > S.z + 22; });
            host.modal.hide();
          }
        });
      }
      buttons.push({ label: "RUN AGAIN", className: "go", onClick: function () { host.modal.hide(); begin(); } });
      buttons.push({ label: "UPGRADES", className: "gold", onClick: openShop });
      buttons.push({ label: "MENU", className: "ghost", onClick: host.exit });

      host.results({
        win: false,
        title: "WIPEOUT",
        subtitle: distance > S.best ? "New personal best" : "Best " + A.formatNumber(S.best) + " m",
        stats: [
          ["Distance", A.formatNumber(distance) + " m"],
          ["Coins", "+" + A.formatNumber(coins)],
          ["Score", A.formatNumber(score)],
          ["Account XP", "+" + A.formatNumber(xp)]
        ],
        buttons: buttons
      });
    }

    function openShop() {
      var rows = [
        { key: "upMagnet", name: "Magnet Coil", desc: "+1.6s magnet duration", max: 10,
          cost: function (l) { return Math.round(150 * Math.pow(1.3, l)); } },
        { key: "upBoost", name: "Jet Charge", desc: "+0.9s boost duration", max: 10,
          cost: function (l) { return Math.round(180 * Math.pow(1.32, l)); } },
        { key: "upShield", name: "Start Shield", desc: "Begin every run shielded", max: 1,
          cost: function () { return 900; } },
        { key: "upHeadStart", name: "Head Start", desc: "+60 m before the first obstacle", max: 8,
          cost: function (l) { return Math.round(260 * Math.pow(1.4, l)); } },
        { key: "upCoinValue", name: "Gilded Coins", desc: "Coins are worth more", max: 8,
          cost: function (l) { return Math.round(320 * Math.pow(1.38, l)); } },
        { key: "upRevive", name: "Guardian", desc: "Carry an extra second wind", max: 2,
          cost: function (l) { return 1800 + l * 2600; } }
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
    function fogAt(z) { return cam.fog(z, 30, 150) * 0.9; }
    function F(color, z) { return A.rgb(A.mix(color, PAL.fog, fogAt(z))); }

    function render(g) {
      var w = A.View.w, h = A.View.h;
      A.Fx.applyShake(g);

      drawSky(g, w, h);
      drawSkyline(g, w, h);
      drawRoof(g);

      var items = [];
      S.props.forEach(function (p) { items.push({ z: p.z, fn: drawProp, a: p }); });
      S.obstacles.forEach(function (o) { items.push({ z: o.z, fn: drawObstacle, a: o }); });
      S.pickups.forEach(function (p) { if (!p.taken) items.push({ z: p.z, fn: drawPickup, a: p }); });
      items.push({ z: S.z, fn: drawPlayer });

      items.sort(function (a, b) { return b.z - a.z; });
      for (var i = 0; i < items.length; i++) items[i].fn(g, items[i].a);

      A.Fx.drawParticles(g, cam.project);
      A.Fx.drawTexts(g, cam.project);

      if (S.boost > 0) drawSpeedLines(g, w, h);

      g.restore();
      A.Fx.drawFlash(g, w, h);
    }

    function drawSky(g, w, h) {
      var bottom = cam.horizon + 2;
      var grad = g.createLinearGradient(0, 0, 0, bottom);
      grad.addColorStop(0, A.rgb(PAL.skyTop));
      grad.addColorStop(0.55, A.rgb(PAL.skyMid));
      grad.addColorStop(1, A.rgb(PAL.skyLow));
      g.fillStyle = grad;
      g.fillRect(0, 0, w, bottom);

      var sunX = w * 0.5 - cam.x * 3;
      var sunY = cam.horizon - h * 0.03;
      var glow = g.createRadialGradient(sunX, sunY, 0, sunX, sunY, h * 0.36);
      glow.addColorStop(0, A.rgba(PAL.sun, 0.7));
      glow.addColorStop(1, A.rgba(PAL.skyLow, 0));
      g.fillStyle = glow;
      g.fillRect(0, 0, w, bottom);
    }

    function drawSkyline(g, w, h) {
      for (var layer = 0; layer < 2; layer++) {
        var color = layer === 0 ? A.mix(PAL.far, PAL.fog, 0.4) : PAL.near;
        var parallax = layer === 0 ? 4 : 9;
        var scale = layer === 0 ? 0.055 : 0.085;
        var offset = -cam.x * parallax - cam.z * parallax * 0.6;

        g.fillStyle = A.rgb(color);
        var step = layer === 0 ? 46 : 62;
        for (var x = -step; x < w + step; x += step) {
          var seed = Math.floor((x + offset) / step);
          var rand = A.rng(seed * 2654435761 + layer * 91);
          var bh = h * scale * (0.6 + rand() * 1.6);
          var bx = x - A.wrap(offset, step);
          g.fillRect(bx, cam.horizon - bh, step - 5, bh + 4);

          if (layer === 1 && rand() < 0.7) {
            g.fillStyle = "rgba(255,214,150,0.35)";
            for (var wy = 0; wy < 4; wy++) {
              for (var wx = 0; wx < 3; wx++) {
                if (rand() < 0.5) continue;
                g.fillRect(bx + 8 + wx * 14, cam.horizon - bh + 10 + wy * 15, 6, 8);
              }
            }
            g.fillStyle = A.rgb(color);
          }
        }
      }
    }

    function drawRoof(g) {
      var nearZ = cam.z + 3, farZ = cam.z + 190;
      var h = A.View.h, w = A.View.w;
      var hw = 4.9;

      g.fillStyle = A.rgb(PAL.gap);
      g.fillRect(0, cam.horizon, w, h - cam.horizon);

      var roof = g.createLinearGradient(0, cam.horizon, 0, h);
      roof.addColorStop(0, A.rgb(PAL.fog));
      roof.addColorStop(0.08, A.rgb(A.mix(PAL.roof, PAL.fog, 0.55)));
      roof.addColorStop(0.4, A.rgb(PAL.roof));
      roof.addColorStop(1, A.rgb(PAL.roofDark));
      A.strip(g, cam, -hw, hw, 0, 0, nearZ, farZ, roof);

      for (var side = -1; side <= 1; side += 2) {
        A.strip(g, cam, side * hw, side * (hw + 0.6), 0, 0, nearZ, farZ, A.rgb(PAL.edge));
        A.strip(g, cam, side * (hw + 0.6), side * (hw + 0.6), 0, 0.55, nearZ, farZ,
          A.rgb(A.shade(PAL.edge, 0.2)));
      }

      var start = Math.floor(nearZ / 5) * 5;
      for (var z = start; z < Math.min(farZ, nearZ + 120); z += 5) {
        var fade = 1 - fogAt(z);
        if (fade <= 0.04) continue;
        g.globalAlpha = 0.22 * fade;
        for (var l = 0; l < 2; l++) {
          var lx = (LANES[l] + LANES[l + 1]) / 2;
          A.strip(g, cam, lx - 0.07, lx + 0.07, 0.01, 0.01, z, z + 2.4, A.rgb(PAL.lane));
        }
        g.globalAlpha = 1;
      }
    }

    function drawProp(g, p) {
      var f = cam.plane(p.z);
      if (!f || f.s < 0.7) return;

      var x = f.px(p.x), y0 = f.py(0), y1 = f.py(p.h);
      var wpx = p.w * f.s;

      if (p.kind === "antenna") {
        g.strokeStyle = F(PAL.edge, p.z);
        g.lineWidth = Math.max(1, f.s * 0.06);
        g.beginPath(); g.moveTo(x, y0); g.lineTo(x, f.py(p.h * 2.2)); g.stroke();
        g.fillStyle = F(PAL.red, p.z);
        g.beginPath(); g.arc(x, f.py(p.h * 2.2), Math.max(1.5, f.s * 0.09), 0, TAU); g.fill();
        return;
      }

      g.fillStyle = F(p.kind === "vent" ? PAL.roofDark : PAL.edge, p.z);
      g.fillRect(x - wpx / 2, y1, wpx, y0 - y1);
      g.fillStyle = F(A.shade(PAL.roofDark, 0.25), p.z);
      g.fillRect(x - wpx / 2, y1, wpx, Math.max(1, f.s * 0.09));
    }

    function drawObstacle(g, o) {
      var f = cam.plane(o.z);
      if (!f || f.s < 0.7) return;

      var def = OBSTACLES[o.kind];
      var x = f.px(LANES[o.lane]);
      var wpx = def.w * f.s;

      if (o.kind === "gap") {
        var near = cam.plane(o.z - 1.6), far = cam.plane(o.z + 1.6);
        if (!near || !far) return;
        A.quad(g,
          [near.px(LANES[o.lane] - 1.5), near.py(0)], [near.px(LANES[o.lane] + 1.5), near.py(0)],
          [far.px(LANES[o.lane] + 1.5), far.py(0)], [far.px(LANES[o.lane] - 1.5), far.py(0)],
          A.rgb(PAL.gap));
        g.strokeStyle = F(PAL.red, o.z);
        g.lineWidth = Math.max(1.5, f.s * 0.05);
        g.stroke();
        return;
      }

      var y0 = f.py(def.y || 0);
      var y1 = f.py((def.y || 0) + def.h);

      g.fillStyle = F(def.color, o.z);
      g.fillRect(x - wpx / 2, y1, wpx, y0 - y1);
      g.fillStyle = F(A.shade(def.color, 0.28), o.z);
      g.fillRect(x - wpx / 2, y1, wpx, Math.max(2, f.s * 0.1));
      g.strokeStyle = F(PAL.ink, o.z);
      g.lineWidth = Math.max(1, f.s * 0.035);
      g.strokeRect(x - wpx / 2, y1, wpx, y0 - y1);

      // A chevron tells you what the obstacle wants before you reach it.
      if (f.s > 6) {
        g.fillStyle = "rgba(255,255,255,0.85)";
        g.font = Math.max(10, f.s * 0.5) + "px 'Titan One', sans-serif";
        g.textAlign = "center";
        g.textBaseline = "middle";
        var glyph = def.need === "air" ? "▲" : (def.need === "slide" ? "▼" : "✖");
        g.fillText(glyph, x, (y0 + y1) / 2);
      }
    }

    function drawPickup(g, p) {
      var f = cam.plane(p.z);
      if (!f || f.s < 0.7) return;

      var x = f.px(LANES[p.lane]);
      var y = f.py(p.y + Math.sin(S.t * 4 + p.z) * 0.08);
      var r = f.s * (p.kind === "coin" ? 0.22 : 0.3);

      if (p.kind === "coin") {
        var squash = Math.abs(Math.cos(S.t * 5 + p.z * 0.4));
        g.fillStyle = A.rgb(PAL.gold);
        g.beginPath();
        g.ellipse(x, y, r * (0.35 + squash * 0.65), r, 0, 0, TAU);
        g.fill();
        g.strokeStyle = "#8A5A0C";
        g.lineWidth = Math.max(1, f.s * 0.035);
        g.stroke();
        return;
      }

      var color = p.kind === "magnet" ? PAL.cyan : (p.kind === "shield" ? PAL.jade : PAL.violet);
      g.fillStyle = A.rgba(color, 0.28);
      g.beginPath(); g.arc(x, y, r * 1.5, 0, TAU); g.fill();
      g.fillStyle = A.rgb(color);
      g.beginPath();
      g.moveTo(x, y - r);
      g.lineTo(x + r, y);
      g.lineTo(x, y + r);
      g.lineTo(x - r, y);
      g.closePath();
      g.fill();
    }

    function drawPlayer(g) {
      var p = cam.project(S.x, 0, S.z);
      if (!p) return;

      g.globalAlpha = 0.3 * (1 - A.clamp01(S.y / 5));
      g.fillStyle = A.rgb(PAL.ink);
      g.beginPath();
      g.ellipse(p.x, p.y, p.s * 0.42, p.s * 0.16, 0, 0, TAU);
      g.fill();
      g.globalAlpha = 1;

      var feet = cam.project(S.x, S.y, S.z);
      if (!feet) return;

      var clip, t;
      if (!S.alive) { clip = "die"; t = A.clamp01((0.7 - Math.max(0, S.roll)) / 0.7); }
      else if (S.roll > 0) { clip = "roll"; t = 1 - S.roll / 0.5; }
      else if (S.slide > 0) { clip = "slide"; t = 0; }
      else if (S.air) { clip = S.vy > 0 ? "jump" : "fall"; t = A.clamp01(0.5 - S.vy / 30); }
      else { clip = S.boost > 0 ? "sprint" : "run"; t = A.wrap(S.t * (S.speed / 7), 1); }

      if (S.shield > 0) {
        g.globalAlpha = 0.30 + Math.sin(S.t * 6) * 0.1;
        g.fillStyle = A.rgb(PAL.jade);
        g.beginPath();
        g.ellipse(feet.x, feet.y - p.s * 0.95, p.s * 0.95, p.s * 1.25, 0, 0, TAU);
        g.fill();
        g.globalAlpha = 1;
      }

      if (S.hitFlash > 0 && Math.floor(S.hitFlash * 20) % 2 === 0) g.globalAlpha = 0.4;
      var pose = A.Rig.pose(clip, t);
      A.Rig.draw(g, style, pose, feet.x, feet.y, 1.95 * p.s, 1, { noShadow: true });
      g.globalAlpha = 1;

      if (S.magnet > 0) {
        g.strokeStyle = A.rgba(PAL.cyan, 0.5);
        g.lineWidth = 2;
        g.beginPath();
        g.ellipse(feet.x, feet.y, p.s * 1.9, p.s * 0.7, 0, 0, TAU);
        g.stroke();
      }
    }

    function drawSpeedLines(g, w, h) {
      g.strokeStyle = "rgba(255,255,255,0.22)";
      g.lineWidth = 2;
      for (var i = 0; i < 14; i++) {
        var rand = A.rng(i * 977 + Math.floor(S.t * 20));
        var x = rand() * w;
        var y = rand() * h;
        var len = 40 + rand() * 90;
        g.beginPath();
        g.moveTo(x, y);
        g.lineTo(x + (x - w / 2) * 0.12, y + len);
        g.stroke();
      }
    }

    /* ------------------------------------------------------------ ui ---- */
    function mount(root) {
      var hud = host.el("div", "hud");

      var top = host.el("div", "row");
      ui.dist = host.el("div", "chip", "0 m");
      ui.power = host.el("div", "meter");
      ui.powerFill = host.el("i");
      ui.power.appendChild(ui.powerFill);
      ui.coins = host.el("div", "chip gold", "0");
      top.appendChild(ui.dist);
      top.appendChild(ui.power);
      top.appendChild(ui.coins);
      hud.appendChild(top);

      ui.speed = host.el("div", "cap", "");
      hud.appendChild(ui.speed);

      ui.hint = host.el("div", "hint", "Swipe to move  ·  up to vault  ·  down to slide");
      hud.appendChild(ui.hint);

      root.appendChild(hud);
    }

    function paintHud() {
      if (!ui.dist) return;
      ui.dist.textContent = A.formatNumber(Math.round(S.dist)) + " m";
      ui.coins.textContent = A.formatNumber(S.coins);

      var power = Math.max(S.magnet, S.boost);
      ui.powerFill.style.width = A.clamp01(power / 6) * 100 + "%";
      ui.power.style.opacity = power > 0 ? "1" : "0.25";

      ui.speed.textContent = S.boost > 0 ? "BOOST" : (S.shield > 0 ? "SHIELDED" : "");
    }

    return {
      mount: mount,
      start: function () {
        if (!style) bake();
        begin();
        moveCamera(0, true);
      },
      stop: function () { S = null; },
      update: function (dt) { if (S) update(dt); },
      render: function (g) { if (S) render(g); },
      onResize: function () { if (S) moveCamera(0, true); }
    };
  }

  /* ------------------------------------------------------ registration -- */
  var thumbStyle = null;

  A.games.push({
    id: "runner",
    name: "Rooftop Run",
    tagline: "Endless three-lane sprint. Vault, slide, grab boosts, never stop.",
    accent: "#3FD98A",
    unlock: 3,
    template: { coins: 0, runs: 0, best: 0, bestScore: 0,
                upMagnet: 0, upBoost: 0, upShield: 0, upHeadStart: 0, upCoinValue: 0, upRevive: 0 },
    bestLine: function (s) {
      return s.runs ? "Best " + A.formatNumber(s.best) + " m  ·  " + A.formatNumber(s.coins) + " coins" : "New";
    },
    thumb: function (g, w, h, t) {
      var sky = g.createLinearGradient(0, 0, 0, h * 0.6);
      sky.addColorStop(0, "#16102E");
      sky.addColorStop(1, "#F68060");
      g.fillStyle = sky;
      g.fillRect(0, 0, w, h * 0.6);

      g.fillStyle = "#241A3C";
      for (var i = 0; i < 7; i++) {
        var bh = 12 + ((i * 37) % 26);
        g.fillRect(i * (w / 7), h * 0.6 - bh, w / 7 - 2, bh);
      }

      g.fillStyle = "#5C5474";
      g.beginPath();
      g.moveTo(w * 0.3, h * 0.6);
      g.lineTo(w * 0.7, h * 0.6);
      g.lineTo(w, h);
      g.lineTo(0, h);
      g.closePath();
      g.fill();

      if (!thumbStyle) thumbStyle = A.Rig.style(5150, { hue: 0.36, helmet: "hood", cape: true });
      var pose = A.Rig.pose("run", A.wrap(t * 1.8, 1));
      A.Rig.draw(g, thumbStyle, pose, w * 0.5, h * 0.94, 54, 1, { noShadow: true });
    },
    create: create
  });
})(window.A);
