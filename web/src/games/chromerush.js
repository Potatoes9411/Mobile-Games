/* ===========================================================================
   CHROME RUSH
   The endless-highway weaver. You hold a lane, traffic thickens, the speedo
   climbs and never comes back down. Scoring rewards the thing that scares you:
   shaving past a truck at speed banks a near-miss multiplier, so the safe line
   down an empty shoulder is also the poorest line.

   Every vehicle is a stack of extruded boxes generated from its stat block, and
   each one carries a horn ability on a cooldown - that is the meta hook. You are
   not buying a faster car, you are buying a different way to survive.
   =========================================================================== */
(function (A) {
  "use strict";
  A.games = A.games || [];

  var LANES = 5;
  var LANE_W = 2.4;                     // world units
  var ROAD_W = LANES * LANE_W;
  var VIEW_AHEAD = 42;                  // world units of road drawn ahead
  var VIEW_BEHIND = 10;

  /* Vehicles. `top` is the cruising speed the throttle climbs toward, `grip` how
     fast you can change lanes, `bulk` how much traffic you shrug off. Abilities
     are all on the same button so the control scheme never changes. */
  var CARS = [
    {
      id: "hatch", name: "City Hatch", cost: 0,
      top: 30, accel: 7.0, grip: 9.0, bulk: 0, len: 1.9, wide: 1.05,
      body: 0x3D8BFF, roof: 0x9FCBFF,
      ability: "boost", abilityName: "Nitro", cool: 9,
      blurb: "A short nitro burst. Nothing fancy, always useful."
    },
    {
      id: "sport", name: "Wedge GT", cost: 900,
      top: 38, accel: 8.6, grip: 11.0, bulk: 0, len: 2.0, wide: 1.02,
      body: 0xE2455F, roof: 0xFF9AA8,
      ability: "boost", abilityName: "Overdrive", cool: 7,
      blurb: "Faster everything. Punishing if your lane reads are late."
    },
    {
      id: "police", name: "Interceptor", cost: 2200,
      top: 35, accel: 8.0, grip: 12.0, bulk: 1, len: 2.1, wide: 1.10,
      body: 0x1E2740, roof: 0xEDEDF2,
      ability: "siren", abilityName: "Siren", cool: 11,
      blurb: "Traffic pulls aside for four seconds. The panic button."
    },
    {
      id: "truck", name: "Haul Rig", cost: 4200,
      top: 31, accel: 5.4, grip: 6.6, bulk: 2, len: 3.4, wide: 1.24,
      body: 0xE9A03F, roof: 0xFFD79A,
      ability: "ram", abilityName: "Bull Bar", cool: 10,
      blurb: "Shove three cars out of the way instead of dying."
    },
    {
      id: "hover", name: "Skiff", cost: 7500,
      top: 41, accel: 9.4, grip: 14.0, bulk: 0, len: 2.0, wide: 0.98,
      body: 0x2FD6A4, roof: 0xC4FFEA,
      ability: "phase", abilityName: "Phase", cool: 12,
      blurb: "Three seconds of passing straight through traffic."
    }
  ];

  /* Biomes rotate every stretch so a long run keeps changing colour. */
  var BIOMES = [
    /* Each biome has to separate three planes at a glance: verge, tarmac and the
       stuff standing beside it. The first pass had all three within a few points
       of the same navy and the road simply vanished. */
    { name: "DOWNTOWN",     sky: 0x1B2545, ground: 0x223054, road: 0x474E60, line: 0xF7ECC6, prop: 0x33406B, propTall: true,  lit: true },
    { name: "SUNSET FLATS", sky: 0xE8823F, ground: 0xC4763F, road: 0x6A5B4E, line: 0xFFF0C8, prop: 0x8F5A36, propTall: false, lit: false },
    { name: "PINE PASS",    sky: 0x3D7E8C, ground: 0x2C6A4C, road: 0x4A525C, line: 0xEEF6E0, prop: 0x1D4630, propTall: true,  lit: false },
    { name: "SALT NIGHT",   sky: 0x120F26, ground: 0x1E1C3E, road: 0x353552, line: 0x8FF0FF, prop: 0x34305F, propTall: false, lit: true }
  ];

  var TRAFFIC_TINTS = [0xD9483C, 0xE9C33F, 0x5F6B8A, 0xEDEDF2, 0x7A6DC4, 0x3E8F5A, 0xE2803C];

  function create(host) {
    var save = host.save;
    var S = null;
    var ui = {};
    var layout = null;

    function activeCar() {
      for (var i = 0; i < CARS.length; i++) if (CARS[i].id === save.carId) return CARS[i];
      return CARS[0];
    }

    /* --------------------------------------------------------------- run -- */
    function begin() {
      var car = activeCar();
      S = {
        car: car,
        rand: A.rng((Math.random() * 0xFFFFFF) >>> 0),
        x: 0,                       // lateral position, world units from centre
        vx: 0,
        z: 0,                       // distance travelled
        speed: 12,
        throttle: 1,
        traffic: [],
        coins: [],
        props: [],
        nextSpawn: 0,
        nextProp: 0,
        score: 0,
        picked: 0,
        nearMiss: 0, nearTimer: 0,
        best: save.best,
        cooldown: 0,
        abilityT: 0,
        crashed: false, crashT: 0,
        shakeLean: 0,
        biome: 0, biomeMix: 0,
        revived: false
      };
      A.Fx.reset();
      computeLayout();
      paintHud();
      host.toast(car.name + "  ·  " + car.abilityName + " on the button");
    }

    function computeLayout() {
      var w = A.View.w, h = A.View.h;
      /* One world unit in pixels. The road has to fit across the narrow axis
         with shoulders, and the whole visible stretch has to fit vertically. */
      var byWidth = w / (ROAD_W + 5.0);
      var byHeight = h / (VIEW_AHEAD + VIEW_BEHIND) * 2.35;
      var scale = Math.min(byWidth, byHeight);
      layout = {
        w: w, h: h,
        scale: scale,
        cx: w * 0.5,
        /* Player sits low so most of the screen is the road you are about to
           reach. Reaction time is the whole game. */
        baseY: h * 0.80,
        depth: scale * 0.86,
        rise: scale * 0.52
      };
      if (S) S.shakeLean = 0;
    }

    /** World (lateral x, distance z, height y) to screen. Straight orthographic
        with a vertical squash - readable at a glance, and cheap enough to draw
        two hundred boxes a frame on a phone. */
    function project(x, z, y) {
      var dz = z - S.z;
      return {
        x: layout.cx + x * layout.scale - S.shakeLean * layout.scale * 0.2,
        y: layout.baseY - dz * layout.depth - (y || 0) * layout.rise
      };
    }

    /* ---------------------------------------------------------- spawning -- */
    function laneCentre(i) { return (i - (LANES - 1) / 2) * LANE_W; }

    function difficulty() { return A.clamp01(S.score / 4200); }

    function spawnTraffic() {
      var d = difficulty();
      var rand = S.rand;

      /* Pick a lane, but never seal the road: we track which lanes already have
         something close to the spawn line and always leave at least one gap. */
      var free = [];
      for (var i = 0; i < LANES; i++) {
        var blocked = false;
        for (var t = 0; t < S.traffic.length; t++) {
          var c = S.traffic[t];
          if (c.lane === i && Math.abs(c.z - (S.z + VIEW_AHEAD)) < 9) { blocked = true; break; }
        }
        if (!blocked) free.push(i);
      }
      if (free.length <= 1) return;

      var lane = free[rand.int(0, free.length - 1)];
      var oncoming = lane >= LANES - 2 && rand.chance(0.50 + d * 0.18);
      var big = rand.chance(0.16 + d * 0.12);

      S.traffic.push({
        lane: lane,
        x: laneCentre(lane) + rand.range(-0.16, 0.16),
        z: S.z + VIEW_AHEAD + rand.range(0, 8),
        /* Same-direction traffic is slower than you; oncoming closes fast. */
        speed: oncoming ? -(rand.range(16, 24) + d * 8) : rand.range(7, 15) + d * 6,
        len: big ? rand.range(3.1, 3.9) : rand.range(1.8, 2.2),
        wide: big ? 1.22 : rand.range(0.98, 1.10),
        tint: A.hex(TRAFFIC_TINTS[rand.int(0, TRAFFIC_TINTS.length - 1)]),
        big: big,
        oncoming: oncoming,
        drift: rand.range(-0.20, 0.20),
        yielded: 0,
        shoved: 0,
        counted: false
      });

      /* Coins ride in the gaps, which nudges the player toward the risky line. */
      if (rand.chance(0.55)) {
        var cl = free[rand.int(0, free.length - 1)];
        var n = rand.int(3, 6);
        for (var k = 0; k < n; k++) {
          S.coins.push({ x: laneCentre(cl), z: S.z + VIEW_AHEAD + 4 + k * 1.6, got: false, spin: k });
        }
      }
    }

    function spawnProp() {
      var b = BIOMES[S.biome];
      var side = S.rand.chance(0.5) ? -1 : 1;
      S.props.push({
        x: side * (ROAD_W / 2 + S.rand.range(1.0, 2.6)),
        z: S.z + VIEW_AHEAD + S.rand.range(0, 10),
        w: S.rand.range(0.8, 1.9),
        h: b.propTall ? S.rand.range(2.6, 7.5) : S.rand.range(0.6, 1.6),
        tint: A.shade(A.hex(b.prop), S.rand.range(-0.14, 0.14)),
        lit: b.lit && S.rand.chance(0.75),
        mask: S.rand.int(0, 0xFFFF)
      });
    }

    /* -------------------------------------------------------------- step -- */
    function update(dt) {
      A.Fx.update(dt);

      if (S.crashed) {
        S.crashT += dt;
        S.speed = A.approach(S.speed, 0, 3.4, dt);
        S.z += S.speed * dt;
        return;
      }

      var car = S.car;

      /* Throttle. Speed climbs toward the car's top and keeps a slow global
         creep past it, so a perfect run still eventually ends. */
      var creep = 1 + A.clamp01(S.score / 6000) * 0.30;
      var target = car.top * creep * (S.abilityT > 0 && car.ability === "boost" ? 1.45 : 1);
      S.speed = A.approach(S.speed, target, car.accel * 0.18, dt);
      S.z += S.speed * dt;
      S.score = Math.floor(S.z);

      /* Steering: drag left/right, or arrow keys. The car has weight - `grip`
         sets how fast lateral velocity converges on the input. */
      var want = 0;
      if (A.Input.down && layout) {
        var dxPix = A.Input.x - layout.cx;
        want = A.clamp(dxPix / (layout.scale * (ROAD_W / 2)), -1, 1);
        want = want * (ROAD_W / 2 + 0.6);
      } else {
        var k = 0;
        if (A.Input.keyPressed("arrowleft") || A.Input.keyPressed("a")) k -= 1;
        if (A.Input.keyPressed("arrowright") || A.Input.keyPressed("d")) k += 1;
        want = S.x + k * 4.5;
      }
      want = A.clamp(want, -(ROAD_W / 2 + 0.45), ROAD_W / 2 + 0.45);

      var prevX = S.x;
      S.x = A.approach(S.x, want, car.grip, dt);
      S.vx = (S.x - prevX) / Math.max(dt, 0.0001);
      S.shakeLean = A.approach(S.shakeLean, A.clamp(S.vx * 0.05, -1, 1), 8, dt);

      /* Off the tarmac is survivable but slow - a soft wall, not a hard one. */
      var edge = ROAD_W / 2 - car.wide * 0.5;
      if (Math.abs(S.x) > edge) {
        S.speed *= Math.exp(-2.2 * dt);
        if (S.rand.chance(dt * 30)) {
          A.Fx.burst(S.x, S.z + 1, 0, 2, { color: [190, 170, 140], speed: 60, life: 0.3 });
        }
      }

      /* Ability. */
      if (S.cooldown > 0) S.cooldown = Math.max(0, S.cooldown - dt);
      if (S.abilityT > 0) S.abilityT = Math.max(0, S.abilityT - dt);
      if (A.Input.keyPressed(" ") && S.cooldown <= 0) fireAbility();

      /* Traffic. */
      var i;
      for (i = S.traffic.length - 1; i >= 0; i--) {
        var c = S.traffic[i];
        c.z += c.speed * dt;
        c.x += c.drift * dt * 0.4;

        if (c.shoved !== 0) {
          c.x += c.shoved * 9 * dt;
          c.shoved *= Math.exp(-1.6 * dt);
        }

        /* Siren: nearby traffic slides toward the nearest shoulder. */
        if (S.abilityT > 0 && car.ability === "siren" && Math.abs(c.z - S.z) < 26) {
          var side = c.x >= 0 ? 1 : -1;
          c.x = A.approach(c.x, side * (ROAD_W / 2 + 0.4), 3.2, dt);
          c.yielded = 1;
        }

        if (c.z < S.z - VIEW_BEHIND - 6 || c.z > S.z + VIEW_AHEAD + 30) {
          S.traffic.splice(i, 1);
          continue;
        }

        /* Collision + near miss, tested on the same pass. */
        var dz = Math.abs(c.z - S.z);
        var dx = Math.abs(c.x - S.x);
        var hitZ = (c.len + car.len) * 0.5;
        var hitX = (c.wide + car.wide) * 0.5;

        if (dz < hitZ && dx < hitX) {
          var phased = S.abilityT > 0 && car.ability === "phase";
          var rammed = S.abilityT > 0 && car.ability === "ram";
          if (phased) {
            /* pass straight through, no penalty */
          } else if (rammed && !c.big) {
            c.shoved = c.x >= S.x ? 1 : -1;
            S.speed *= 0.94;
            A.Fx.burst(c.x, c.z, 0.4, 8, { color: c.tint, speed: 150, life: 0.4 });
            A.Audio.sfx("thud");
          } else if (car.bulk > 0 && !c.big && car.bulk >= 2) {
            c.shoved = c.x >= S.x ? 1 : -1;
            S.speed *= 0.88;
            A.Audio.sfx("thud");
          } else {
            crash(c);
            return;
          }
        } else if (!c.counted && dz < hitZ + 0.6 && dx < hitX + 0.75) {
          /* Near miss: close enough to scare, far enough to live. */
          c.counted = true;
          S.nearMiss++;
          S.nearTimer = 1.8;
          S.picked += 2;
          A.Fx.text(c.x, c.z, 0.9, "NEAR MISS", [255, 214, 84], { life: 0.6, rise: 50 });
          A.Audio.sfx("pop");
        }
      }

      /* Coins. */
      for (i = S.coins.length - 1; i >= 0; i--) {
        var co = S.coins[i];
        if (co.z < S.z - 6) { S.coins.splice(i, 1); continue; }
        if (Math.abs(co.z - S.z) < 1.1 && Math.abs(co.x - S.x) < 0.9) {
          S.coins.splice(i, 1);
          S.picked += 1;
          A.Audio.sfx("coin");
          A.Fx.burst(co.x, co.z, 0.5, 5, { color: [255, 214, 84], speed: 110, life: 0.32 });
        }
      }

      /* Roadside props. */
      for (i = S.props.length - 1; i >= 0; i--) {
        if (S.props[i].z < S.z - VIEW_BEHIND - 4) S.props.splice(i, 1);
      }

      if (S.nearTimer > 0) {
        S.nearTimer -= dt;
        if (S.nearTimer <= 0) S.nearMiss = 0;
      }

      /* Spawn pacing tightens with difficulty but has a hard floor, otherwise
         late runs become a solid wall rather than a hard one. */
      S.nextSpawn -= dt;
      if (S.nextSpawn <= 0) {
        spawnTraffic();
        S.nextSpawn = A.lerp(0.30, 0.11, difficulty()) * S.rand.range(0.75, 1.3);
      }
      S.nextProp -= dt;
      if (S.nextProp <= 0) {
        spawnProp();
        S.nextProp = S.rand.range(0.10, 0.34);
      }

      /* Biome every 900 units, cross-faded so the change is felt not jarring. */
      var stage = Math.floor(S.z / 900);
      var wantBiome = stage % BIOMES.length;
      if (wantBiome !== S.biome) {
        S.biomeMix += dt * 0.6;
        if (S.biomeMix >= 1) {
          S.biome = wantBiome;
          S.biomeMix = 0;
          host.toast(BIOMES[S.biome].name);
        }
      } else {
        S.biomeMix = Math.max(0, S.biomeMix - dt * 0.6);
      }

      paintHud();
    }

    function fireAbility() {
      var car = S.car;
      S.cooldown = car.cool;
      A.Audio.sfx(car.ability === "siren" ? "horn" : "boom");
      A.Fx.kick(5);

      if (car.ability === "boost") S.abilityT = 2.4;
      else if (car.ability === "siren") S.abilityT = 4.0;
      else if (car.ability === "ram") S.abilityT = 3.0;
      else if (car.ability === "phase") S.abilityT = 3.0;

      A.Fx.text(S.x, S.z + 3, 1.2, car.abilityName.toUpperCase(), [93, 224, 255], { life: 0.7, rise: 60 });
    }

    /* ------------------------------------------------------------- crash -- */
    function crash(c) {
      S.crashed = true;
      S.crashT = 0;
      A.Fx.kick(18);
      A.Fx.flashScreen(0.42, [255, 210, 170]);
      A.Fx.burst(S.x, S.z, 0.6, 26, { color: [255, 168, 70], speed: 260, life: 0.7 });
      if (c) A.Fx.burst(c.x, c.z, 0.6, 16, { color: c.tint, speed: 220, life: 0.6 });
      A.Audio.sfx("boom");
      A.vibrate([40, 40, 90]);
      setTimeout(finish, 1100);
    }

    function finish() {
      if (!S || !S.crashed) return;

      var coins = Math.round((S.picked * 3 + S.score * 0.10) * host.coinMultiplier());
      save.coins += coins;
      save.runs++;
      save.totalDistance += S.score;
      var isBest = S.score > save.best;
      if (isBest) save.best = S.score;
      host.commit();

      var xp = Math.round((16 + S.score * 0.02) * host.xpMultiplier());
      host.addXp(xp);
      host.progress("run", 1);
      host.progress("coins", coins);

      host.results({
        title: isBest ? "NEW RECORD" : "WRECKED",
        subtitle: BIOMES[S.biome].name + "  ·  " + S.car.name,
        rows: [
          { label: "Distance", value: A.formatNumber(S.score) + " m" },
          { label: "Best", value: A.formatNumber(save.best) + " m" },
          { label: "Top speed", value: Math.round(S.speed * 3.6) + " km/h" },
          { label: "Pickups", value: S.picked },
          { label: "Coins", value: "+" + A.formatNumber(coins) },
          { label: "XP", value: "+" + xp }
        ],
        actions: [
          { label: "DRIVE AGAIN", className: "go", onClick: function () { host.modal.hide(); begin(); } },
          { label: "GARAGE", className: "gold", onClick: function () { host.modal.hide(); openShop(); } },
          { label: "MENU", className: "ghost", onClick: host.exit }
        ]
      });
    }

    /* ------------------------------------------------------------ garage -- */
    function openShop() {
      var card = host.el("div", "card");
      card.appendChild(host.el("h2", null, "GARAGE"));
      card.appendChild(host.el("p", "sub", A.formatNumber(save.coins) + " coins"));

      CARS.forEach(function (c) {
        var owned = save.owned.indexOf(c.id) >= 0;
        var row = host.el("div", "shopRow");
        var left = host.el("div");
        left.appendChild(host.el("h4", null, c.name + "  ·  " + c.abilityName));
        left.appendChild(host.el("p", null, c.blurb));
        row.appendChild(left);

        var btn = host.el("button", "buy");
        if (save.carId === c.id) {
          btn.textContent = "DRIVING";
          btn.disabled = true;
        } else if (owned) {
          btn.textContent = "SELECT";
          btn.addEventListener("click", function () {
            save.carId = c.id;
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
            save.carId = c.id;
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
    /** Extruded box in road space. Same construction as the rest of the arcade:
        a top face, a front face and one side, so height reads without lighting. */
    function box(g, x, z, y, w, len, h, color, alpha) {
      var top = project(x, z, y + h);
      var tw = w * layout.scale;
      var tl = len * layout.depth;
      var th = h * layout.rise;

      if (top.y + tl > layout.h + 80 || top.y - th < -120) return;
      g.globalAlpha = alpha === undefined ? 1 : alpha;

      g.fillStyle = A.rgb(A.shade(color, -0.30));
      g.fillRect(top.x - tw / 2, top.y + tl / 2, tw, th);

      g.fillStyle = A.rgb(A.shade(color, -0.46));
      g.fillRect(top.x + tw / 2 - 1, top.y - tl / 2, Math.max(1.5, tw * 0.10), th + tl);

      g.fillStyle = A.rgb(color);
      g.fillRect(top.x - tw / 2, top.y - tl / 2, tw, tl);

      g.globalAlpha = 1;
    }

    function drawVehicle(g, x, z, len, wide, body, roof, opts) {
      opts = opts || {};
      var s = project(x, z, 0);

      g.fillStyle = "rgba(0,0,0,0.26)";
      g.fillRect(s.x - wide * layout.scale * 0.52, s.y - len * layout.depth * 0.48,
        wide * layout.scale * 1.04, len * layout.depth * 0.96);

      box(g, x, z, 0.04, wide, len, 0.30, body, opts.alpha);
      box(g, x, z + len * 0.04, 0.34, wide * 0.80, len * 0.52, 0.20, roof, opts.alpha);

      /* Windscreen slab, then lights at both ends. */
      var wp = project(x, z + len * 0.20, 0.55);
      g.globalAlpha = opts.alpha === undefined ? 1 : opts.alpha;
      g.fillStyle = "rgba(24,32,52,0.75)";
      g.fillRect(wp.x - wide * layout.scale * 0.34, wp.y - len * layout.depth * 0.10,
        wide * layout.scale * 0.68, len * layout.depth * 0.20);

      var front = project(x, z + len * 0.5, 0.18);
      var back = project(x, z - len * 0.5, 0.18);
      var lw = wide * layout.scale * 0.16, lh = Math.max(2, layout.depth * 0.10);
      g.fillStyle = opts.oncoming ? "rgba(255,248,214,0.95)" : "rgba(255,244,200,0.85)";
      g.fillRect(front.x - wide * layout.scale * 0.36, front.y - lh / 2, lw, lh);
      g.fillRect(front.x + wide * layout.scale * 0.36 - lw, front.y - lh / 2, lw, lh);
      g.fillStyle = "rgba(255,80,80,0.85)";
      g.fillRect(back.x - wide * layout.scale * 0.36, back.y - lh / 2, lw, lh);
      g.fillRect(back.x + wide * layout.scale * 0.36 - lw, back.y - lh / 2, lw, lh);
      g.globalAlpha = 1;
    }

    function render(g) {
      var vw = layout.w, vh = layout.h;
      var a = BIOMES[S.biome];
      var b = BIOMES[(S.biome + 1) % BIOMES.length];
      var mix = S.biomeMix;
      var skyC = A.mix(A.hex(a.sky), A.hex(b.sky), mix);
      var groundC = A.mix(A.hex(a.ground), A.hex(b.ground), mix);
      var roadC = A.mix(A.hex(a.road), A.hex(b.road), mix);
      var lineC = A.mix(A.hex(a.line), A.hex(b.line), mix);

      var sky = g.createLinearGradient(0, 0, 0, vh * 0.5);
      sky.addColorStop(0, A.rgb(A.shade(skyC, 0.22)));
      sky.addColorStop(1, A.rgb(skyC));
      g.fillStyle = sky;
      g.fillRect(0, 0, vw, vh);

      g.save();
      A.Fx.applyShake(g);

      /* Ground plate. */
      var horizon = project(0, S.z + VIEW_AHEAD + 12, 0).y;
      g.fillStyle = A.rgb(groundC);
      g.fillRect(0, horizon, vw, vh - horizon);

      /* Road slab plus shoulder stripes. */
      var lx = project(-ROAD_W / 2, 0, 0).x;
      var rx = project(ROAD_W / 2, 0, 0).x;
      g.fillStyle = A.rgb(roadC);
      g.fillRect(lx, horizon, rx - lx, vh - horizon);
      g.fillStyle = A.rgb(A.shade(roadC, 0.18));
      g.fillRect(lx - layout.scale * 0.30, horizon, layout.scale * 0.30, vh - horizon);
      g.fillRect(rx, horizon, layout.scale * 0.30, vh - horizon);

      /* Lane dashes, drawn as world-space rungs so they scroll with distance. */
      g.fillStyle = A.rgba(lineC, 0.72);
      var first = Math.floor(S.z / 4) * 4;
      for (var d = first - VIEW_BEHIND; d < S.z + VIEW_AHEAD + 8; d += 4) {
        for (var l = 1; l < LANES; l++) {
          var px = project(laneCentre(l) - LANE_W / 2, d, 0);
          if (px.y < horizon - 20 || px.y > vh + 20) continue;
          g.fillRect(px.x - layout.scale * 0.05, px.y - layout.depth * 0.9,
            layout.scale * 0.10, layout.depth * 1.8);
        }
      }

      /* Roadside props behind everything else on the tarmac. */
      var i;
      for (i = 0; i < S.props.length; i++) {
        var p = S.props[i];
        box(g, p.x, p.z, 0, p.w, p.w, p.h, p.tint);
        if (!p.lit || p.h < 1.6) continue;
        /* Window grid on the face toward the camera. The bit mask keeps the same
           windows dark every frame instead of flickering. */
        var top = project(p.x, p.z, p.h);
        var pw = p.w * layout.scale, ph = p.h * layout.rise;
        if (top.y > layout.h + 40 || top.y + ph < -40) continue;
        var cols = Math.max(2, Math.round(p.w * 2.2));
        var rows = Math.max(2, Math.round(p.h * 1.1));
        var cw = pw / cols * 0.42, ch = ph / rows * 0.40;
        g.fillStyle = "rgba(255,238,178,0.72)";
        for (var wx = 0; wx < cols; wx++) {
          for (var wy = 0; wy < rows; wy++) {
            if ((p.mask >> ((wx * 3 + wy) % 16)) & 1) continue;
            g.fillRect(
              top.x - pw / 2 + (wx + 0.5) * (pw / cols) - cw / 2,
              top.y + p.w * layout.depth * 0.5 + (wy + 0.35) * (ph / rows) - ch / 2,
              cw, ch);
          }
        }
      }

      /* Coins. */
      for (i = 0; i < S.coins.length; i++) {
        var co = S.coins[i];
        var cp = project(co.x, co.z, 0.42);
        if (cp.y < horizon - 20 || cp.y > vh + 30) continue;
        var rr = layout.scale * 0.24;
        var squash = Math.abs(Math.cos(A.Loop.time * 3 + co.spin));
        g.fillStyle = "rgba(0,0,0,0.20)";
        g.beginPath();
        g.ellipse(cp.x, project(co.x, co.z, 0).y, rr * 0.8, rr * 0.30, 0, 0, A.TAU);
        g.fill();
        var grad = g.createLinearGradient(cp.x, cp.y - rr, cp.x, cp.y + rr);
        grad.addColorStop(0, "#FFE9A0");
        grad.addColorStop(0.55, "#FFC93C");
        grad.addColorStop(1, "#C98A12");
        g.fillStyle = grad;
        g.beginPath();
        g.ellipse(cp.x, cp.y, Math.max(1.5, rr * squash), rr, 0, 0, A.TAU);
        g.fill();
      }

      /* Traffic, far to near so nearer cars overlap correctly. */
      var order = S.traffic.slice().sort(function (m, n) { return n.z - m.z; });
      for (i = 0; i < order.length; i++) {
        var c = order[i];
        drawVehicle(g, c.x, c.z, c.len, c.wide, c.tint,
          A.shade(c.tint, 0.30), { oncoming: c.oncoming });
        if (c.yielded && S.abilityT > 0) {
          var yp = project(c.x, c.z, 1.1);
          g.fillStyle = "rgba(93,224,255,0.5)";
          g.beginPath();
          g.arc(yp.x, yp.y, layout.scale * 0.16, 0, A.TAU);
          g.fill();
        }
      }

      /* Player. Phase draws translucent so the state is unmistakable. */
      var car = S.car;
      var phasing = S.abilityT > 0 && car.ability === "phase";
      if (!S.crashed || S.crashT < 0.35) {
        drawVehicle(g, S.x, S.z, car.len, car.wide, A.hex(car.body), A.hex(car.roof),
          { alpha: phasing ? 0.55 : 1 });
      }

      /* Nitro flame out the back. */
      if (S.abilityT > 0 && car.ability === "boost") {
        var fp = project(S.x, S.z - car.len * 0.55, 0.20);
        var flame = g.createLinearGradient(fp.x, fp.y, fp.x, fp.y + layout.depth * 2.4);
        flame.addColorStop(0, "rgba(255,236,150,0.9)");
        flame.addColorStop(1, "rgba(255,90,40,0)");
        g.fillStyle = flame;
        g.beginPath();
        g.moveTo(fp.x - car.wide * layout.scale * 0.30, fp.y);
        g.lineTo(fp.x + car.wide * layout.scale * 0.30, fp.y);
        g.lineTo(fp.x, fp.y + layout.depth * (2.0 + Math.random() * 0.8));
        g.closePath();
        g.fill();
      }

      /* Siren bar. */
      if (S.abilityT > 0 && car.ability === "siren") {
        var sp = project(S.x, S.z, 0.60);
        var on = (Math.floor(A.Loop.time * 9) % 2) === 0;
        g.fillStyle = on ? "#4B8BFF" : "#FF4B5C";
        g.fillRect(sp.x - car.wide * layout.scale * 0.34, sp.y - 3, car.wide * layout.scale * 0.68, 6);
      }

      A.Fx.drawParticles(g, function (x, z, y) {
        var p = project(x, z, y);
        return { x: p.x, y: p.y, s: layout.scale / 30 };
      });
      A.Fx.drawTexts(g, function (x, z, y) {
        var p = project(x, z, y);
        return { x: p.x, y: p.y, s: layout.scale / 30 };
      });

      g.restore();

      /* Speed streaks at the screen edges - the cheapest possible sense of pace. */
      var pace = A.clamp01((S.speed - 18) / 26);
      if (pace > 0.02) {
        g.strokeStyle = "rgba(255,255,255," + (pace * 0.20).toFixed(3) + ")";
        g.lineWidth = 2;
        for (var k = 0; k < 8; k++) {
          var t = A.wrap(A.Loop.time * (2.2 + pace * 3) + k * 0.37, 1);
          var side = k % 2 ? 1 : -1;
          var sx2 = vw / 2 + side * vw * (0.36 + (k % 4) * 0.04);
          var sy = t * vh;
          g.beginPath();
          g.moveTo(sx2, sy);
          g.lineTo(sx2, sy + vh * 0.10 * (0.4 + pace));
          g.stroke();
        }
      }

      A.Fx.drawFlash(g, vw, vh);
    }

    /* ----------------------------------------------------------------- ui -- */
    function mount(root) {
      var hud = host.el("div", "hud");
      var top = host.el("div", "row");
      ui.dist = host.el("div", "chip", "0 m");
      ui.speed = host.el("div", "chip", "0 km/h");
      ui.coins = host.el("div", "chip gold", "0");
      top.appendChild(ui.dist);
      top.appendChild(ui.speed);
      top.appendChild(ui.coins);
      hud.appendChild(top);

      ui.cap = host.el("div", "cap", "");
      hud.appendChild(ui.cap);

      ui.hint = host.el("div", "hint", "Drag to steer  ·  hit the button for your ability");
      hud.appendChild(ui.hint);
      root.appendChild(hud);

      /* The ability lives on its own button. Firing it from a screen tap would
         also yank the steering target to wherever the thumb landed. */
      ui.fire = host.el("button", "fireBtn", "GO");
      ui.fire.addEventListener("click", function (e) {
        e.stopPropagation();
        if (S && !S.crashed && S.cooldown <= 0) fireAbility();
      });
      ui.fire.addEventListener("pointerdown", function (e) { e.stopPropagation(); });
      root.appendChild(ui.fire);
    }

    function paintHud() {
      if (!ui.dist || !S) return;
      ui.dist.textContent = A.formatNumber(S.score) + " m";
      ui.speed.textContent = Math.round(S.speed * 3.6) + " km/h";
      ui.coins.textContent = A.formatNumber(save.coins + S.picked);

      var ready = S.cooldown <= 0;
      var label = ready ? S.car.abilityName.toUpperCase() + " READY"
        : S.car.abilityName.toUpperCase() + "  " + S.cooldown.toFixed(1) + "s";
      if (S.nearMiss > 1) label += "     ·     NEAR MISS x" + S.nearMiss;
      ui.cap.textContent = label;
      ui.cap.style.color = ready ? "#5DE0FF" : "";
      if (ui.fire) {
        ui.fire.textContent = ready ? S.car.abilityName.toUpperCase() : Math.ceil(S.cooldown) + "";
        ui.fire.classList.toggle("ready", ready);
      }
      if (S.score > 60) ui.hint.textContent = "";
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
    id: "chromerush",
    name: "Chrome Rush",
    tagline: "Endless highway. Thread the traffic, bank near misses, and unlock five very different rides.",
    accent: "#FF7A4B",
    unlock: 2,
    template: { coins: 0, runs: 0, best: 0, totalDistance: 0, carId: "hatch", owned: ["hatch"] },
    bestLine: function (s) {
      return s.runs ? "Best " + A.formatNumber(s.best) + " m  ·  " + s.owned.length + " cars" : "New";
    },
    thumb: function (g, w, h, t) {
      g.fillStyle = "#2A3352";
      g.fillRect(0, 0, w, h);
      g.fillStyle = "#3C424E";
      g.fillRect(w * 0.16, 0, w * 0.68, h);
      g.fillStyle = "#2F3A4C";
      g.fillRect(0, 0, w * 0.16, h);
      g.fillRect(w * 0.84, 0, w * 0.16, h);

      g.fillStyle = "rgba(242,230,192,0.7)";
      for (var l = 1; l < 3; l++) {
        var lx = w * (0.16 + 0.68 * l / 3);
        for (var d = 0; d < 6; d++) {
          var y = A.wrap(t * 0.9 + d / 6, 1) * h;
          g.fillRect(lx - 1.5, y, 3, h * 0.09);
        }
      }

      function car(cx, cy, ww, hh, body, roof) {
        g.fillStyle = "rgba(0,0,0,0.25)";
        g.fillRect(cx - ww / 2, cy - hh / 2 + 2, ww, hh);
        g.fillStyle = body;
        A.roundRect(g, cx - ww / 2, cy - hh / 2, ww, hh, 2); g.fill();
        g.fillStyle = roof;
        A.roundRect(g, cx - ww * 0.34, cy - hh * 0.26, ww * 0.68, hh * 0.44, 2); g.fill();
      }

      car(w * 0.34, A.wrap(t * 0.55 + 0.1, 1) * (h + 40) - 20, w * 0.15, h * 0.20, "#D9483C", "#FF8E84");
      car(w * 0.66, A.wrap(t * 0.42 + 0.6, 1) * (h + 40) - 20, w * 0.15, h * 0.20, "#E9C33F", "#FFEBA0");
      car(w * 0.50, h * 0.76, w * 0.16, h * 0.22, "#3D8BFF", "#9FCBFF");

      var flame = g.createLinearGradient(0, h * 0.87, 0, h);
      flame.addColorStop(0, "rgba(255,236,150,0.85)");
      flame.addColorStop(1, "rgba(255,90,40,0)");
      g.fillStyle = flame;
      g.beginPath();
      g.moveTo(w * 0.47, h * 0.87); g.lineTo(w * 0.53, h * 0.87); g.lineTo(w * 0.50, h);
      g.closePath(); g.fill();
    },
    create: create
  });
})(window.A);
