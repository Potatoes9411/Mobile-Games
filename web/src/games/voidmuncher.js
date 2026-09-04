/* ===========================================================================
   VOID MUNCHER
   The "you are a hole, eat the city" format. Drag to steer a void across a
   generated district. It swallows anything smaller than its mouth, grows with
   every bite, and three rival voids are doing exactly the same thing to the
   same city. Ninety seconds, then whoever ate most wins the block.

   The whole appeal is the growth curve crossing thresholds: the moment your
   hole gets wide enough to take a bus is the moment the run gets loud, so the
   prop tiers below are spaced to guarantee three or four of those moments.
   =========================================================================== */
(function (A) {
  "use strict";
  A.games = A.games || [];

  /* Prop tiers. `r` is the footprint radius the void has to beat, `value` the
     score, `h` the extruded height in world units. Tier gaps are deliberate -
     each one is a visible upgrade in what the city looks like from inside. */
  var PROPS = [
    { key: "cone",    tier: 0, r: 6,  h: 9,  w: 11, value: 1,    color: 0xF2803C, round: true,  weight: 12 },
    { key: "bin",     tier: 0, r: 7,  h: 12, w: 13, value: 2,    color: 0x4E7A63, round: true,  weight: 9 },
    { key: "sign",    tier: 0, r: 6,  h: 17, w: 8,  value: 2,    color: 0xC9D3DE, round: true,  weight: 8 },
    { key: "bench",   tier: 1, r: 11, h: 8,  w: 26, value: 4,    color: 0x9A6A3F, round: false, weight: 8 },
    { key: "tree",    tier: 1, r: 12, h: 30, w: 24, value: 5,    color: 0x3E8F5A, round: true,  weight: 11 },
    { key: "walker",  tier: 1, r: 9,  h: 20, w: 12, value: 6,    color: 0xE8B48A, round: true,  weight: 10 },
    { key: "scooter", tier: 2, r: 15, h: 14, w: 30, value: 10,   color: 0xE2455F, round: false, weight: 7 },
    { key: "kiosk",   tier: 2, r: 17, h: 26, w: 34, value: 14,   color: 0xE9C33F, round: false, weight: 5 },
    { key: "car",     tier: 3, r: 21, h: 18, w: 46, value: 26,   color: 0x3D8BFF, round: false, weight: 9 },
    { key: "cab",     tier: 3, r: 21, h: 18, w: 46, value: 28,   color: 0xF6C24B, round: false, weight: 5 },
    { key: "van",     tier: 3, r: 24, h: 26, w: 52, value: 34,   color: 0xEDEDF2, round: false, weight: 4 },
    { key: "bus",     tier: 4, r: 33, h: 32, w: 78, value: 70,   color: 0xD9483C, round: false, weight: 4 },
    { key: "truck",   tier: 4, r: 35, h: 34, w: 82, value: 78,   color: 0x5F6B8A, round: false, weight: 3 },
    { key: "hut",     tier: 4, r: 36, h: 44, w: 74, value: 84,   color: 0xB0663F, round: false, weight: 3 },
    { key: "house",   tier: 5, r: 52, h: 78, w: 106, value: 190, color: 0xD5B48A, round: false, weight: 4 },
    { key: "shop",    tier: 5, r: 56, h: 92, w: 114, value: 210, color: 0x7A6DC4, round: false, weight: 3 },
    { key: "tower",   tier: 6, r: 78, h: 168, w: 156, value: 640, color: 0x8C9AB4, round: false, weight: 2 },
    { key: "spire",   tier: 6, r: 70, h: 210, w: 138, value: 720, color: 0xB6C2D4, round: false, weight: 1 }
  ];

  var RIVAL_NAMES = ["ABYSS", "GULP", "NIL", "CHASM", "MAW", "SINK", "PIT", "HOLLOW"];
  var RIVAL_TINTS = [0xE2455F, 0x8C5BFF, 0x2FD6A4, 0xF6A33F];

  /* Nine well spread swallow milestones. Crossing one is worth a callout. */
  var MILESTONES = [
    { r: 14, text: "BENCHES" }, { r: 18, text: "TREES" }, { r: 24, text: "SCOOTERS" },
    { r: 30, text: "CARS" }, { r: 40, text: "VANS" }, { r: 52, text: "BUSES" },
    { r: 70, text: "HOUSES" }, { r: 92, text: "SHOPS" }, { r: 120, text: "TOWERS" }
  ];

  function create(host) {
    var save = host.save;
    var S = null;
    var ui = {};
    var layout = null;

    /* ------------------------------------------------------------ tuning -- */
    function startRadius() { return 16 + save.upStart * 2.0; }
    function growthScale() { return 1 + save.upGrow * 0.12; }
    function moveSpeed() { return 250 + save.upSpeed * 26; }
    function runSeconds() { return 90 + save.upTime * 8; }

    /* --------------------------------------------------------- generation -- */
    /** Weighted prop pick, biased by how deep into the district we are. */
    function rollProp(rand, level, ring) {
      /* Outer rings hold the big stuff so early growth stays near the middle
         and the player has a reason to spiral outward as they get bigger. */
      var pool = [];
      for (var i = 0; i < PROPS.length; i++) {
        var p = PROPS[i];
        var want = ring * 6.4;                       // preferred tier for this ring
        var gap = Math.abs(p.tier - want);
        var w = p.weight * Math.exp(-gap * gap * 0.55);
        if (p.tier >= 5 && level < 2) w *= 0.35;     // no skyline on the first district
        if (w > 0.02) pool.push({ p: p, w: w });
      }
      var total = 0, k;
      for (k = 0; k < pool.length; k++) total += pool[k].w;
      var roll = rand() * total;
      for (k = 0; k < pool.length; k++) {
        roll -= pool[k].w;
        if (roll <= 0) return pool[k].p;
      }
      return pool[pool.length - 1].p;
    }

    function buildCity(level) {
      var rand = A.rng(0x5EED ^ (level * 7919));
      var props = [];
      var blocks = [];

      /* City is laid out on a road grid. Props sit inside blocks, never on the
         roads, so the void always has clean lanes to travel down - a solid
         wall of scenery would make steering feel like wading.

         The grid is deliberately not square: a square district on a portrait
         screen leaves fat empty bands top and bottom once the camera has zoomed
         out far enough to show all of it. */
      var cellsX = 6 + Math.min(3, Math.floor(level / 3));
      var aspect = A.clamp((A.View.h || 800) / (A.View.w || 460), 1, 2.4);
      var cellsY = A.clamp(Math.round(cellsX * aspect), cellsX, 16);
      var span = (1240 + level * 60) / cellsX;
      var road = span * 0.28;
      var halfX = span * cellsX * 0.5;
      var halfY = span * cellsY * 0.5;
      var half = Math.max(halfX, halfY);

      for (var cy = 0; cy < cellsY; cy++) {
        for (var cx = 0; cx < cellsX; cx++) {
          var bx = -halfX + span * (cx + 0.5);
          var by = -halfY + span * (cy + 0.5);
          var bw = span - road, bh = span - road;
          blocks.push({ x: bx, y: by, w: bw, h: bh });

          /* Ring is the normalised distance from the district centre. */
          var ring = A.clamp01(Math.sqrt(bx * bx + by * by) / (half * 1.05));
          var count = Math.round(A.lerp(30, 13, ring) * (1 + level * 0.04));
          var placed = 0;

          /* Fixed attempt budget rather than retrying until success: a dense
             block can genuinely run out of room, and the old retry loop could
             spin on that. A missing prop is invisible; a hang is not. */
          for (var n = 0; n < count * 4 && placed < count; n++) {
            var proto = rollProp(rand, level, ring);
            var px = bx + rand.range(-bw * 0.44, bw * 0.44);
            var py = by + rand.range(-bh * 0.44, bh * 0.44);

            var ok = true;
            for (var t = props.length - 1; t >= 0; t--) {
              var q = props[t];
              if (q.bx !== cx || q.by !== cy) continue;
              var need = (q.r + proto.r) * 0.88;
              if (A.dist2(px, py, q.x, q.y) < need * need) { ok = false; break; }
            }
            if (!ok) continue;
            placed++;

            props.push({
              x: px, y: py, bx: cx, by: cy,
              r: proto.r, h: proto.h, w: proto.w,
              value: proto.value, tier: proto.tier,
              round: proto.round, key: proto.key,
              color: A.hex(proto.color),
              spin: rand.range(0, A.TAU),
              sink: 0, taken: false, ang: 0, dropX: 0, dropY: 0
            });
          }
        }
      }

      /* Clear a landing pad so the opening seconds are never a wall of trucks. */
      for (var i = props.length - 1; i >= 0; i--) {
        if (props[i].tier >= 2 && A.dist2(props[i].x, props[i].y, 0, 0) < 165 * 165) {
          props.splice(i, 1);
        }
      }

      return {
        half: half, halfX: halfX, halfY: halfY,
        props: props, blocks: blocks, span: span, road: road,
        cellsX: cellsX, cellsY: cellsY
      };
    }

    /* -------------------------------------------------------------- run --- */
    function begin() {
      var level = save.level;
      var city = buildCity(level);

      var voids = [{
        player: true, name: "YOU", x: 0, y: 0, r: startRadius(),
        score: 0, tint: A.hex(0x5DE0FF), vx: 0, vy: 0, target: null, think: 0, alive: true
      }];

      var rivalCount = A.clamp(2 + Math.floor(level / 2), 2, 4);
      var rand = A.rng(0xC0FFEE ^ (level * 131));
      for (var i = 0; i < rivalCount; i++) {
        var ang = A.TAU * (i + 0.5) / rivalCount;
        voids.push({
          player: false,
          name: RIVAL_NAMES[(level * 3 + i) % RIVAL_NAMES.length],
          x: Math.cos(ang) * city.halfX * 0.60,
          y: Math.sin(ang) * city.halfY * 0.60,
          /* Rivals start a touch behind so the first thirty seconds feel like a
             lead you earned, then their growth rate catches up. */
          r: startRadius() * 0.9,
          score: 0, tint: A.hex(RIVAL_TINTS[i % RIVAL_TINTS.length]),
          vx: 0, vy: 0, target: null, think: rand.range(0, 0.4), alive: true,
          skill: A.clamp01(0.42 + level * 0.05 + rand.range(-0.06, 0.06))
        });
      }

      S = {
        level: level,
        city: city,
        voids: voids,
        me: voids[0],
        time: runSeconds(),
        over: false,
        camX: 0, camY: 0, camScale: 1,
        milestone: 0,
        eaten: 0,
        combo: 0, comboTimer: 0,
        shockwave: 0,
        /* Opening grace. Long enough to reach the first cluster of props. */
        grace: 9
      };

      A.Fx.reset();
      computeLayout();
      paintHud();
      host.toast("DISTRICT " + level + "  ·  swallow the block");
    }

    function computeLayout() {
      layout = { w: A.View.w, h: A.View.h, unit: A.View.unit() };
      if (S) {
        S.camX = S.me.x;
        S.camY = S.me.y;
        S.camScale = targetScale(S.me.r);
      }
    }

    /** Zoom out as the void grows, so it always occupies a similar screen share.
        The constant matters more than it looks: too tight and the district reads
        as two enormous blocks with a dot in the middle, and the whole point of
        the format - watching the city shrink around you - never lands. */
    function targetScale(r) {
      if (!layout) return 1;
      /* Never zoom out past the point where the whole district fits: past that
         the city becomes a postage stamp in a sea of background, which is the
         opposite of the "the world is shrinking" feeling the zoom is for. */
      var floorScale = S
        ? Math.max(layout.w / (S.city.halfX * 2 + 150), layout.h / (S.city.halfY * 2 + 150))
        : 0.2;
      return A.clamp(layout.unit / (r * 22 + 330), floorScale, 1.1);
    }

    /* ------------------------------------------------------------- eating -- */
    /** Radius after absorbing `area`. Areas add, radii do not - that keeps the
        curve honest: doubling the mouth needs four times the city. */
    function grow(v, area) {
      /* Rivals bank less of what they eat. Without the handicap a bot that
         happens to spawn on a dense block snowballs past the player inside the
         first three seconds, which reads as the game cheating rather than as a
         race. */
      var gain = area * (v.player ? growthScale() : 0.62);
      var a = Math.PI * v.r * v.r + gain;
      v.r = Math.sqrt(a / Math.PI);
    }

    function canEat(v, p) { return !p.taken && p.r <= v.r * 0.94; }

    function swallow(v, p) {
      p.taken = true;
      p.sink = 0.0001;
      p.ang = Math.atan2(p.y - v.y, p.x - v.x);
      p.dropX = v.x;
      p.dropY = v.y;
      p.eater = v;

      grow(v, Math.PI * p.r * p.r * 0.62);
      v.score += p.value;

      if (v.player) {
        S.eaten++;
        S.combo++;
        S.comboTimer = 1.5;
        A.Audio.sfx(p.tier >= 4 ? "boom" : "pop");

        if (p.tier >= 4) {
          A.Fx.kick(p.tier >= 5 ? 9 : 5);
          A.Fx.burst(v.x, v.y, 0, 14, { color: p.color, speed: 190, life: 0.5 });
          S.shockwave = 1;
        }
        if (S.combo >= 3 && S.combo % 5 === 0) {
          A.Fx.text(v.x, v.y - v.r - 22, 0, "x" + S.combo, [255, 214, 84], { life: 0.7, rise: 60 });
        }
      }
    }

    /* One void can swallow another. The bar is deliberately high (1.35x, centre
       well inside the mouth) so a narrow lead never turns into an instant
       unrecoverable loss, and the player gets a grace period on top. */
    function tryDevourVoids() {
      for (var i = 0; i < S.voids.length; i++) {
        var a = S.voids[i];
        if (!a.alive) continue;
        for (var j = 0; j < S.voids.length; j++) {
          if (i === j) continue;
          var b = S.voids[j];
          if (!b.alive || a.r < b.r * 1.35) continue;
          if (b.player && S.grace > 0) continue;
          var bite = a.r * 0.70;
          if (A.dist2(a.x, a.y, b.x, b.y) > bite * bite) continue;

          b.alive = false;
          grow(a, Math.PI * b.r * b.r * 0.5);
          a.score += Math.round(b.score * 0.5);

          if (a.player) {
            host.toast("SWALLOWED " + b.name);
            A.Fx.kick(11);
            A.Fx.flashScreen(0.3, [93, 224, 255]);
          } else if (b.player) {
            finish("eaten by " + a.name);
            return;
          }
        }
      }
    }

    /* ------------------------------------------------------------- rivals -- */
    function steerRival(v, dt) {
      v.think -= dt;

      if (v.think <= 0 || !v.target || v.target.taken) {
        v.think = A.lerp(0.55, 0.16, v.skill);

        /* Score every candidate by value over travel time, then take the best.
           Low skill rivals sample a small random slice instead, which makes
           them wander plausibly rather than looking broken. */
        var best = null, bestScore = -1;
        var props = S.city.props;
        var samples = Math.round(A.lerp(26, 150, v.skill));
        var step = Math.max(1, Math.floor(props.length / samples));
        var offset = Math.floor(Math.random() * step);

        for (var i = offset; i < props.length; i += step) {
          var p = props[i];
          if (!canEat(v, p)) continue;
          var d = Math.sqrt(A.dist2(v.x, v.y, p.x, p.y)) + 1;
          var s = p.value / d;
          if (s > bestScore) { bestScore = s; best = p; }
        }

        /* A big rival that has run out of food goes hunting smaller voids. */
        if (!best) {
          for (var k = 0; k < S.voids.length; k++) {
            var o = S.voids[k];
            if (o === v || !o.alive || v.r < o.r * 1.3) continue;
            best = { x: o.x, y: o.y, taken: false, chase: o };
            break;
          }
        }
        v.target = best;
      }

      if (!v.target) return;
      var tx = v.target.chase ? v.target.chase.x : v.target.x;
      var ty = v.target.chase ? v.target.chase.y : v.target.y;
      var dx = tx - v.x, dy = ty - v.y;
      var len = Math.hypot(dx, dy) || 1;
      var speed = (148 + S.level * 7) * A.lerp(0.80, 1.04, v.skill);
      v.vx = (dx / len) * speed;
      v.vy = (dy / len) * speed;
    }

    /* -------------------------------------------------------------- step -- */
    function update(dt) {
      A.Fx.update(dt);
      if (S.over) return;

      S.time -= dt;
      if (S.time <= 0) { S.time = 0; finish("time"); return; }

      S.shockwave = Math.max(0, S.shockwave - dt * 2.4);
      if (S.grace > 0) S.grace = Math.max(0, S.grace - dt);
      if (S.comboTimer > 0) {
        S.comboTimer -= dt;
        if (S.comboTimer <= 0) S.combo = 0;
      }

      /* Player steering: drag anywhere, the void chases the finger offset.
         Keyboard falls back to the arrow keys for the desktop build. */
      var me = S.me;
      var speed = moveSpeed();
      if (A.Input.down && layout) {
        var dx = A.Input.x - layout.w / 2;
        var dy = A.Input.y - layout.h / 2;
        var len = Math.hypot(dx, dy);
        var reach = layout.unit * 0.22;
        var mag = A.clamp01(len / reach);
        if (len > 4) {
          me.vx = (dx / len) * speed * mag;
          me.vy = (dy / len) * speed * mag;
        }
      } else {
        var kx = 0, ky = 0;
        if (A.Input.keyPressed("arrowleft") || A.Input.keyPressed("a")) kx -= 1;
        if (A.Input.keyPressed("arrowright") || A.Input.keyPressed("d")) kx += 1;
        if (A.Input.keyPressed("arrowup") || A.Input.keyPressed("w")) ky -= 1;
        if (A.Input.keyPressed("arrowdown") || A.Input.keyPressed("s")) ky += 1;
        if (kx || ky) {
          var kl = Math.hypot(kx, ky);
          me.vx = (kx / kl) * speed;
          me.vy = (ky / kl) * speed;
        } else {
          me.vx *= Math.exp(-7 * dt);
          me.vy *= Math.exp(-7 * dt);
        }
      }

      var i;
      for (i = 1; i < S.voids.length; i++) {
        if (S.voids[i].alive) steerRival(S.voids[i], dt);
      }

      var boundX = S.city.halfX + 40, boundY = S.city.halfY + 40;
      for (i = 0; i < S.voids.length; i++) {
        var v = S.voids[i];
        if (!v.alive) continue;
        v.x = A.clamp(v.x + v.vx * dt, -boundX, boundX);
        v.y = A.clamp(v.y + v.vy * dt, -boundY, boundY);
      }

      /* Eating pass. Props are static so a plain distance test is enough; at
         a few hundred props over four voids this stays comfortably cheap. */
      var props = S.city.props;
      for (i = 0; i < props.length; i++) {
        var p = props[i];
        if (p.taken) continue;
        for (var j = 0; j < S.voids.length; j++) {
          var vv = S.voids[j];
          if (!vv.alive || !canEat(vv, p)) continue;
          /* Bite when the prop centre is inside the mouth, minus a little so
             things visibly reach the lip before they tip in. */
          var reach = vv.r * 0.86;
          if (A.dist2(vv.x, vv.y, p.x, p.y) < reach * reach) { swallow(vv, p); break; }
        }
      }

      /* Sink animation: spiral in, shrink, then stop drawing. */
      for (i = 0; i < props.length; i++) {
        if (props[i].taken && props[i].sink < 1) {
          props[i].sink = Math.min(1, props[i].sink + dt * 2.6);
        }
      }

      tryDevourVoids();
      if (S.over) return;

      /* Milestone callouts. */
      while (S.milestone < MILESTONES.length && me.r >= MILESTONES[S.milestone].r) {
        host.toast("BIG ENOUGH FOR " + MILESTONES[S.milestone].text);
        A.Audio.sfx("levelup");
        A.Fx.flashScreen(0.16, [93, 224, 255]);
        S.milestone++;
      }

      /* Camera. */
      S.camX = A.approach(S.camX, me.x, 7, dt);
      S.camY = A.approach(S.camY, me.y, 7, dt);
      S.camScale = A.approach(S.camScale, targetScale(me.r), 2.4, dt);

      /* Keep the district filling the frame. Once the view is wider than the
         city on an axis the camera locks to the centre on that axis. */
      var limX = S.city.halfX + 30, limY = S.city.halfY + 30;
      var halfViewX = (layout.w * 0.5) / S.camScale;
      var halfViewY = (layout.h * 0.5) / S.camScale;
      S.camX = halfViewX >= limX ? 0 : A.clamp(S.camX, -(limX - halfViewX), limX - halfViewX);
      S.camY = halfViewY >= limY ? 0 : A.clamp(S.camY, -(limY - halfViewY), limY - halfViewY);

      paintHud();
    }

    /* ------------------------------------------------------------ results -- */
    function standings() {
      var list = S.voids.slice();
      list.sort(function (a, b) {
        if (a.alive !== b.alive) return a.alive ? -1 : 1;
        return b.score - a.score;
      });
      return list;
    }

    function finish(reason) {
      if (S.over) return;
      S.over = true;

      var order = standings();
      var place = order.indexOf(S.me) + 1;
      var won = place === 1 && S.me.alive;

      var coins = Math.round(S.me.score * 0.42 * host.coinMultiplier() * (won ? 1.6 : 1));
      save.coins += coins;
      save.runs++;
      if (S.me.score > save.best) save.best = S.me.score;
      if (won) {
        save.wins++;
        save.level++;
      }
      host.commit();

      var xp = Math.round((22 + S.me.score * 0.05) * host.xpMultiplier());
      host.addXp(xp);
      host.progress("run", 1);
      host.progress("coins", coins);
      if (won) host.progress("win", 1);

      A.Audio.sfx(won ? "win" : "lose");

      var lines = [];
      for (var i = 0; i < order.length; i++) {
        var v = order[i];
        lines.push({
          label: (i + 1) + ".  " + v.name + (v.alive ? "" : "  (out)"),
          value: A.formatNumber(v.score)
        });
      }

      host.results({
        title: won ? "DISTRICT TAKEN" : (reason === "time" ? "OUT OF TIME" : "SWALLOWED"),
        subtitle: won
          ? "District " + save.level + " unlocked"
          : (reason === "time" ? "Finished " + place + " of " + order.length : "A bigger void got you"),
        rows: [
          { label: "Score", value: A.formatNumber(S.me.score) },
          { label: "Props eaten", value: S.eaten },
          { label: "Final size", value: Math.round(S.me.r) },
          { label: "Coins", value: "+" + A.formatNumber(coins) },
          { label: "XP", value: "+" + xp }
        ].concat(lines),
        actions: [
          { label: won ? "NEXT DISTRICT" : "RETRY", className: "go", onClick: function () { host.modal.hide(); begin(); } },
          { label: "UPGRADES", className: "gold", onClick: function () { host.modal.hide(); openShop(); } },
          { label: "MENU", className: "ghost", onClick: host.exit }
        ]
      });
    }

    /* --------------------------------------------------------------- shop -- */
    function openShop() {
      var rows = [
        { key: "upStart", name: "Wider Mouth", desc: "Start each district bigger", base: 140 },
        { key: "upGrow",  name: "Deeper Void", desc: "Every bite grows you more",  base: 220 },
        { key: "upSpeed", name: "Slipstream",  desc: "Move faster across the block", base: 180 },
        { key: "upTime",  name: "Slow Clock",  desc: "+8 seconds on the timer",    base: 260 }
      ];

      var card = host.el("div", "card");
      card.appendChild(host.el("h2", null, "VOID UPGRADES"));
      card.appendChild(host.el("p", "sub", A.formatNumber(save.coins) + " coins"));

      rows.forEach(function (r) {
        var level = save[r.key];
        var cost = Math.round(r.base * Math.pow(1.55, level));
        var row = host.el("div", "shopRow");
        var left = host.el("div");
        left.appendChild(host.el("h4", null, r.name + "  ·  LV " + level));
        left.appendChild(host.el("p", null, r.desc));
        row.appendChild(left);

        var buy = host.el("button", "buy");
        if (level >= 12) {
          buy.textContent = "MAX";
          buy.disabled = true;
        } else {
          buy.textContent = A.formatNumber(cost);
          buy.disabled = save.coins < cost;
          buy.addEventListener("click", function () {
            if (save.coins < cost) return;
            save.coins -= cost;
            save[r.key]++;
            host.commit();
            A.Audio.sfx("buy");
            host.modal.hide();
            openShop();
          });
        }
        row.appendChild(buy);
        card.appendChild(row);
      });

      card.appendChild(host.button("BACK", "ghost", function () { host.modal.hide(); begin(); }));
      host.modal.show(card);
    }

    /* ------------------------------------------------------------- render -- */
    function toScreen(wx, wy) {
      return {
        x: layout.w / 2 + (wx - S.camX) * S.camScale,
        y: layout.h / 2 + (wy - S.camY) * S.camScale
      };
    }

    function drawProp(g, p, sc) {
      var s = toScreen(p.x, p.y);
      var shrink = p.taken ? (1 - A.smooth(p.sink)) : 1;
      if (shrink <= 0.02) return;

      var px = s.x, py = s.y;
      if (p.taken) {
        /* Spiral into the eater's mouth while shrinking. */
        var e = p.eater;
        var mouth = toScreen(e ? e.x : p.dropX, e ? e.y : p.dropY);
        var t = A.smooth(p.sink);
        var ang = p.ang + t * 3.1;
        var rad = (1 - t) * Math.hypot(px - mouth.x, py - mouth.y);
        px = mouth.x + Math.cos(ang) * rad;
        py = mouth.y + Math.sin(ang) * rad + t * 18 * sc;
      }

      var w = p.w * sc * shrink;
      var d = w * 0.60;
      var hh = p.h * sc * shrink;
      if (w < 1.2) return;

      var col = p.color;
      var round = p.round;

      g.fillStyle = "rgba(12,10,26,0.30)";
      g.beginPath();
      g.ellipse(px, py + d * 0.10, w * 0.60, d * 0.60, 0, 0, A.TAU);
      g.fill();

      /* Extruded side. */
      g.fillStyle = A.rgb(A.shade(col, -0.34));
      if (round) {
        g.beginPath();
        g.moveTo(px - w / 2, py);
        g.lineTo(px - w / 2, py - hh);
        g.lineTo(px + w / 2, py - hh);
        g.lineTo(px + w / 2, py);
        g.ellipse(px, py, w / 2, d / 2, 0, 0, Math.PI);
        g.closePath();
      } else {
        A.roundRect(g, px - w / 2, py - hh, w, hh + d / 2, Math.min(w * 0.14, 5));
      }
      g.fill();

      /* Top face. */
      g.fillStyle = A.rgb(col);
      if (round) {
        g.beginPath();
        g.ellipse(px, py - hh, w / 2, d / 2, 0, 0, A.TAU);
      } else {
        A.roundRect(g, px - w / 2, py - hh - d / 2, w, d, Math.min(w * 0.14, 5));
      }
      g.fill();

      /* Windows on anything tall enough to read as a building. */
      if (!round && p.tier >= 4 && hh > 22) {
        g.fillStyle = "rgba(255,242,196,0.55)";
        var cols = Math.max(2, Math.floor(w / (7 * sc + 4)));
        var rowsN = Math.max(1, Math.floor(hh / (11 * sc + 5)));
        var ww = w / cols * 0.44, wh = hh / rowsN * 0.42;
        for (var cx = 0; cx < cols; cx++) {
          for (var cy = 0; cy < rowsN; cy++) {
            if (((cx * 7 + cy * 3 + p.tier) % 5) === 0) continue;
            g.fillRect(
              px - w / 2 + (cx + 0.5) * (w / cols) - ww / 2,
              py - hh + (cy + 0.4) * (hh / rowsN) - wh / 2,
              ww, wh);
          }
        }
      }
    }

    function drawVoid(g, v, sc) {
      if (!v.alive) return;
      var s = toScreen(v.x, v.y);
      var r = v.r * sc;
      var d = r * 0.66;

      /* Rim glow in the void's tint - the only way to tell four black holes
         apart at a glance. */
      var glow = g.createRadialGradient(s.x, s.y, r * 0.6, s.x, s.y, r * 1.8);
      glow.addColorStop(0, A.rgba(v.tint, v.player ? 0.62 : 0.40));
      glow.addColorStop(0.55, A.rgba(v.tint, v.player ? 0.24 : 0.14));
      glow.addColorStop(1, A.rgba(v.tint, 0));
      g.fillStyle = glow;
      g.beginPath();
      g.ellipse(s.x, s.y, r * 1.8, d * 1.8, 0, 0, A.TAU);
      g.fill();

      var core = g.createRadialGradient(s.x, s.y - d * 0.2, r * 0.1, s.x, s.y, r);
      core.addColorStop(0, "#000000");
      core.addColorStop(0.7, "#05040E");
      core.addColorStop(1, "#100D24");
      g.fillStyle = core;
      g.beginPath();
      g.ellipse(s.x, s.y, r, d, 0, 0, A.TAU);
      g.fill();

      g.strokeStyle = A.rgba(v.tint, v.player ? 1 : 0.8);
      g.lineWidth = Math.max(2.4, r * (v.player ? 0.10 : 0.06));
      g.stroke();
      if (v.player && S.grace > 0) {
        /* Grace ring, so "why did nothing eat me" and "why did that eat me" are
           both answered on screen rather than in a tooltip. */
        g.strokeStyle = "rgba(255,255,255," + (0.14 + 0.14 * Math.sin(A.Loop.time * 6)).toFixed(3) + ")";
        g.lineWidth = Math.max(2, r * 0.08);
        g.beginPath();
        g.ellipse(s.x, s.y, r * 1.3, d * 1.3, 0, 0, A.TAU);
        g.stroke();
      }

      /* Two swirl arcs sell the "things are being pulled in" read. */
      var spin = A.Loop.time * (v.player ? 2.1 : 1.5);
      g.strokeStyle = A.rgba(v.tint, 0.28);
      g.lineWidth = Math.max(1, r * 0.03);
      for (var k = 0; k < 2; k++) {
        g.beginPath();
        g.ellipse(s.x, s.y, r * (0.48 + k * 0.24), d * (0.48 + k * 0.24),
          spin + k * 1.2, 0.4, 4.2);
        g.stroke();
      }

      if (!v.player) {
        g.fillStyle = A.rgba(v.tint, 0.92);
        g.font = "700 " + Math.max(10, Math.round(11 * Math.min(2, sc + 0.5))) + "px Barlow Semi Condensed, sans-serif";
        g.textAlign = "center";
        g.fillText(v.name, s.x, s.y - d - 8);
      }
    }

    function render(g) {
      var vw = layout.w, vh = layout.h, sc = S.camScale;
      var city = S.city;

      g.fillStyle = "#1D2233";
      g.fillRect(0, 0, vw, vh);

      g.save();
      A.Fx.applyShake(g);

      /* Ground plate plus road grid. Drawing the blocks rather than the roads
         means the roads are just the gaps, which is one fill instead of many. */
      var tl = toScreen(-city.halfX, -city.halfY);
      var br = toScreen(city.halfX, city.halfY);
      g.fillStyle = "#2B3145";
      g.fillRect(tl.x, tl.y, br.x - tl.x, br.y - tl.y);

      g.fillStyle = "#3A6E52";
      for (var b = 0; b < city.blocks.length; b++) {
        var blk = city.blocks[b];
        var p0 = toScreen(blk.x - blk.w / 2, blk.y - blk.h / 2);
        A.roundRect(g, p0.x, p0.y, blk.w * sc, blk.h * sc, 8 * sc);
        g.fill();
      }

      /* Lane dashes down the middle of every road. */
      g.strokeStyle = "rgba(255,255,255,0.16)";
      g.lineWidth = Math.max(1, 2 * sc);
      g.setLineDash([12 * sc, 14 * sc]);
      g.beginPath();
      var c;
      for (c = 1; c < city.cellsX; c++) {
        var vx = -city.halfX + city.span * c;
        var a1 = toScreen(vx, -city.halfY), a2 = toScreen(vx, city.halfY);
        g.moveTo(a1.x, a1.y); g.lineTo(a2.x, a2.y);
      }
      for (c = 1; c < city.cellsY; c++) {
        var vy = -city.halfY + city.span * c;
        var b1 = toScreen(-city.halfX, vy), b2 = toScreen(city.halfX, vy);
        g.moveTo(b1.x, b1.y); g.lineTo(b2.x, b2.y);
      }
      g.stroke();
      g.setLineDash([]);

      /* Boundary wall. */
      g.strokeStyle = "rgba(93,224,255,0.35)";
      g.lineWidth = Math.max(2, 4 * sc);
      g.strokeRect(tl.x, tl.y, br.x - tl.x, br.y - tl.y);

      /* Painter's algorithm: sort voids and props together by world y so a hole
         correctly sits in front of the bus it is about to take. */
      var draw = [];
      var props = city.props;
      var cullX = (vw / 2) / sc + 200, cullY = (vh / 2) / sc + 260;
      for (var i = 0; i < props.length; i++) {
        var p = props[i];
        if (p.taken && p.sink >= 1) continue;
        if (Math.abs(p.x - S.camX) > cullX || Math.abs(p.y - S.camY) > cullY) continue;
        draw.push(p);
      }
      var voidList = [];
      for (var j = 0; j < S.voids.length; j++) {
        if (S.voids[j].alive) voidList.push(S.voids[j]);
      }

      /* Voids are a floor feature: everything sits on top of them, so they go
         down first, sorted among themselves by size (small over large reads
         better when two overlap). */
      voidList.sort(function (a, b) { return b.r - a.r; });
      for (j = 0; j < voidList.length; j++) drawVoid(g, voidList[j], sc);

      draw.sort(function (a, b) { return a.y - b.y; });
      for (i = 0; i < draw.length; i++) drawProp(g, draw[i], sc);

      /* Shockwave ring after a big swallow. */
      if (S.shockwave > 0) {
        var me = toScreen(S.me.x, S.me.y);
        var rr = S.me.r * sc * (1 + (1 - S.shockwave) * 2.4);
        g.strokeStyle = "rgba(93,224,255," + (S.shockwave * 0.5).toFixed(3) + ")";
        g.lineWidth = Math.max(2, 6 * sc * S.shockwave);
        g.beginPath();
        g.ellipse(me.x, me.y, rr, rr * 0.66, 0, 0, A.TAU);
        g.stroke();
      }

      A.Fx.drawParticles(g, function (x, y) {
        var s = toScreen(x, y);
        return { x: s.x, y: s.y, s: sc };
      });
      A.Fx.drawTexts(g, function (x, y) {
        var s = toScreen(x, y);
        return { x: s.x, y: s.y, s: sc };
      });

      g.restore();

      /* Off-screen rival arrows so a hunting void is never a surprise. */
      for (j = 0; j < voidList.length; j++) {
        var rv = voidList[j];
        if (rv.player) continue;
        var sp = toScreen(rv.x, rv.y);
        if (sp.x > -40 && sp.x < vw + 40 && sp.y > -40 && sp.y < vh + 40) continue;
        var ang = Math.atan2(rv.y - S.camY, rv.x - S.camX);
        var ex = vw / 2 + Math.cos(ang) * Math.min(vw, vh) * 0.40;
        var ey = vh / 2 + Math.sin(ang) * Math.min(vw, vh) * 0.40;
        var danger = rv.r > S.me.r * 1.1;
        g.save();
        g.translate(ex, ey);
        g.rotate(ang);
        g.fillStyle = danger ? "rgba(226,69,95,0.9)" : A.rgba(rv.tint, 0.6);
        g.beginPath();
        g.moveTo(13, 0); g.lineTo(-9, 8); g.lineTo(-9, -8);
        g.closePath();
        g.fill();
        g.restore();
      }

      A.Fx.drawFlash(g, vw, vh);
    }

    /* ----------------------------------------------------------------- ui -- */
    function mount(root) {
      var hud = host.el("div", "hud");

      var top = host.el("div", "row");
      ui.timer = host.el("div", "chip", "1:30");
      ui.size = host.el("div", "chip", "SIZE 11");
      ui.coins = host.el("div", "chip gold", "0");
      top.appendChild(ui.timer);
      top.appendChild(ui.size);
      top.appendChild(ui.coins);
      hud.appendChild(top);

      ui.board = host.el("div", "cap", "");
      hud.appendChild(ui.board);

      ui.hint = host.el("div", "hint", "Drag to steer  ·  swallow anything smaller than you");
      hud.appendChild(ui.hint);

      root.appendChild(hud);
    }

    function paintHud() {
      if (!ui.timer || !S) return;
      ui.timer.textContent = A.formatTime(S.time);
      ui.timer.style.color = S.time < 15 ? "#FF6B7A" : "";
      ui.size.textContent = "SIZE " + Math.round(S.me.r);
      ui.coins.textContent = A.formatNumber(save.coins);

      var order = standings();
      var parts = [];
      for (var i = 0; i < order.length; i++) {
        var v = order[i];
        var tag = (i + 1) + " " + v.name + " " + A.formatNumber(v.score);
        parts.push(v.player ? "[ " + tag + " ]" : tag);
      }
      ui.board.textContent = parts.join("   ·   ");
      if (S.eaten > 5) ui.hint.textContent = "";
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
    id: "voidmuncher",
    name: "Void Muncher",
    tagline: "You are a hole. Swallow the district before three rival voids eat it out from under you.",
    accent: "#5DE0FF",
    unlock: 1,
    template: { coins: 0, runs: 0, wins: 0, best: 0, level: 1, upStart: 0, upGrow: 0, upSpeed: 0, upTime: 0 },
    bestLine: function (s) {
      return s.runs ? "District " + s.level + "  ·  best " + A.formatNumber(s.best) : "New";
    },
    thumb: function (g, w, h, t) {
      g.fillStyle = "#2B3145";
      g.fillRect(0, 0, w, h);
      g.fillStyle = "#3A6E52";
      A.roundRect(g, w * 0.06, h * 0.10, w * 0.40, h * 0.34, 5); g.fill();
      A.roundRect(g, w * 0.55, h * 0.10, w * 0.39, h * 0.34, 5); g.fill();
      A.roundRect(g, w * 0.06, h * 0.55, w * 0.40, h * 0.35, 5); g.fill();
      A.roundRect(g, w * 0.55, h * 0.55, w * 0.39, h * 0.35, 5); g.fill();

      /* A couple of props orbiting into the mouth, so the tile animates. */
      var cx = w * 0.5, cy = h * 0.56;
      var grow = 0.5 + 0.5 * Math.sin(t * 1.1);
      var r = Math.min(w, h) * (0.14 + grow * 0.10);

      var boxes = [[0.22, 0.24, 0x3D8BFF], [0.80, 0.30, 0xE2455F], [0.30, 0.82, 0xE9C33F]];
      for (var i = 0; i < boxes.length; i++) {
        var ang = t * 1.6 + i * 2.1;
        var pull = A.wrap(t * 0.5 + i * 0.33, 1);
        var px = A.lerp(boxes[i][0] * w, cx, pull) + Math.cos(ang) * (1 - pull) * 6;
        var py = A.lerp(boxes[i][1] * h, cy, pull) + Math.sin(ang) * (1 - pull) * 6;
        var sz = Math.min(w, h) * 0.13 * (1 - pull * 0.85);
        g.fillStyle = A.rgb(A.hex(boxes[i][2]));
        A.roundRect(g, px - sz / 2, py - sz / 2, sz, sz, 2);
        g.fill();
      }

      var glow = g.createRadialGradient(cx, cy, r * 0.7, cx, cy, r * 1.6);
      glow.addColorStop(0, "rgba(93,224,255,0.45)");
      glow.addColorStop(1, "rgba(93,224,255,0)");
      g.fillStyle = glow;
      g.beginPath(); g.ellipse(cx, cy, r * 1.6, r * 1.05, 0, 0, A.TAU); g.fill();

      g.fillStyle = "#05040E";
      g.beginPath(); g.ellipse(cx, cy, r, r * 0.66, 0, 0, A.TAU); g.fill();
      g.strokeStyle = "#5DE0FF";
      g.lineWidth = 2;
      g.stroke();
    },
    create: create
  });
})(window.A);
