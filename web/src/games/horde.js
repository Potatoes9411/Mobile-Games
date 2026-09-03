/* ===========================================================================
   HORDE ARENA
   Survivor-style auto-battler. You only steer; the weapons fire themselves.
   Kills drop XP, XP levels you mid-run, and every level you draft one of three
   upgrades. Weapons evolve when paired with the right perk. 20 waves, a boss
   every fifth, then a meta shop between runs.
   =========================================================================== */
(function (A) {
  "use strict";
  A.games = A.games || [];

  var TAU = A.TAU;
  var ARENA = { w: 1700, h: 1200 };
  var DEPTH = 0.62;             // vertical foreshortening of the 3/4 view

  /* ------------------------------------------------------- definitions -- */
  var WEAPONS = {
    blaster: {
      name: "Blaster", icon: "◈", color: "#5AC8FF", max: 5,
      desc: function (l) { return "Fires " + (1 + Math.floor(l / 3)) + " bolt(s) at the nearest enemy"; },
      cooldown: function (l) { return 0.62 - l * 0.055; },
      damage: function (l) { return 9 + l * 5; }
    },
    orbit: {
      name: "Orbit Blades", icon: "✦", color: "#FFC24B", max: 5,
      desc: function (l) { return (1 + Math.ceil(l / 2)) + " blades circling you"; },
      damage: function (l) { return 7 + l * 4; }
    },
    nova: {
      name: "Shock Nova", icon: "◎", color: "#B98BFF", max: 5,
      desc: function (l) { return "Pulse hits everything within " + (90 + l * 26) + " px"; },
      cooldown: function (l) { return 3.0 - l * 0.28; },
      damage: function (l) { return 14 + l * 9; }
    },
    chain: {
      name: "Arc Chain", icon: "⌁", color: "#7BE0FF", max: 5,
      desc: function (l) { return "Lightning jumps to " + (2 + l) + " enemies"; },
      cooldown: function (l) { return 1.7 - l * 0.16; },
      damage: function (l) { return 11 + l * 6; }
    },
    drone: {
      name: "Sentry Drone", icon: "⧫", color: "#3FD98A", max: 5,
      desc: function (l) { return (l >= 4 ? "Two drones" : "A drone") + " that fires on its own"; },
      cooldown: function (l) { return 0.95 - l * 0.09; },
      damage: function (l) { return 8 + l * 5; }
    },
    mines: {
      name: "Scorch Mines", icon: "⬢", color: "#FF7A45", max: 5,
      desc: function (l) { return "Drops burning ground behind you"; },
      cooldown: function (l) { return 1.5 - l * 0.13; },
      damage: function (l) { return 5 + l * 4; }
    }
  };

  var PERKS = {
    might:    { name: "Might", icon: "▲", color: "#FF6B8A", max: 5, desc: function (l) { return "+18% weapon damage"; } },
    haste:    { name: "Haste", icon: "»", color: "#FFC24B", max: 5, desc: function (l) { return "+14% fire rate"; } },
    boots:    { name: "Swift Boots", icon: "⇒", color: "#3FD98A", max: 5, desc: function (l) { return "+12% move speed"; } },
    vitality: { name: "Vitality", icon: "✚", color: "#FF4D6D", max: 5, desc: function (l) { return "+22 max HP, heals 22"; } },
    magnet:   { name: "Magnet", icon: "◉", color: "#7BE0FF", max: 4, desc: function (l) { return "+35% pickup range"; } },
    fortune:  { name: "Fortune", icon: "◆", color: "#FFE08A", max: 4, desc: function (l) { return "+20% coins"; } },
    crit:     { name: "Focus", icon: "✧", color: "#B98BFF", max: 4, desc: function (l) { return "+8% crit chance"; } },
    regen:    { name: "Regen", icon: "❖", color: "#8BF3BE", max: 3, desc: function (l) { return "Recover 1.2 HP/s"; } }
  };

  /** Weapon at max plus its partner perk unlocks a single, much stronger form. */
  var EVOLUTIONS = {
    blaster: { perk: "haste", name: "Storm Repeater", icon: "≡", color: "#5AC8FF",
      desc: "Continuous fire, three bolts, pierces" },
    orbit: { perk: "might", name: "Ruin Halo", icon: "✷", color: "#FFC24B",
      desc: "Six heavy blades on a wider orbit" },
    nova: { perk: "vitality", name: "Aegis Pulse", icon: "◍", color: "#B98BFF",
      desc: "Bigger pulse that heals you per hit" },
    chain: { perk: "crit", name: "Tesla Cascade", icon: "⚡", color: "#7BE0FF",
      desc: "Arcs to eight enemies and stuns" }
  };

  var ENEMY_TYPES = {
    grunt:   { hp: 22, speed: 46, dmg: 8,  size: 0.86, xp: 3,  coins: 1, hue: 0.02, contact: true },
    runner:  { hp: 14, speed: 96, dmg: 6,  size: 0.74, xp: 3,  coins: 1, hue: 0.30, contact: true },
    brute:   { hp: 88, speed: 34, dmg: 16, size: 1.22, xp: 9,  coins: 3, hue: 0.78, contact: true },
    shooter: { hp: 30, speed: 40, dmg: 10, size: 0.90, xp: 6,  coins: 2, hue: 0.55, contact: false, range: 320 },
    boss:    { hp: 900, speed: 32, dmg: 26, size: 1.9,  xp: 90, coins: 40, hue: 0.90, contact: true, boss: true }
  };

  /* ------------------------------------------------------------- game --- */
  function create(host) {
    var save = host.save;
    var g2d = null;
    var atlases = {};
    var playerStyle = null;
    var S = null;

    var ui = {};

    /* ---------------------------------------------------- meta helpers -- */
    function metaDamage() { return 1 + save.upDamage * 0.10; }
    function metaHp() { return 100 + save.upHp * 20; }
    function metaMagnet() { return 1 + save.upMagnet * 0.15; }

    /* -------------------------------------------------------- baking ---- */
    function bakeAll() {
      playerStyle = A.Rig.style(9001, {
        hue: host.hue, sat: 0.72, helmet: "crest", cape: true,
        weapon: "blaster", bulk: 1.02, shoulderPads: true
      });

      Object.keys(ENEMY_TYPES).forEach(function (key) {
        var t = ENEMY_TYPES[key];
        var style = A.Rig.style(key.length * 7717 + 13, {
          hue: t.hue, sat: 0.68,
          helmet: t.boss ? "horned" : (key === "brute" ? "horned" : "dome"),
          cape: !!t.boss, bulk: t.boss ? 1.5 : (key === "brute" ? 1.35 : 0.95),
          weapon: key === "shooter" ? "staff" : (key === "brute" ? "axe" : "none")
        });
        atlases[key] = A.Rig.bake(style, key === "runner" ? "sprint" : "march", 10, {
          cell: 84, ss: 2, height: 62
        });
      });
    }

    /* ---------------------------------------------------------- state --- */
    function newRun() {
      S = {
        t: 0,
        wave: 1,
        waveT: 0,
        waveLength: 22,
        spawnT: 0,
        phase: "run",
        kills: 0,
        coins: 0,
        level: 1,
        xp: 0,
        xpNeed: 9,
        pendingLevels: 0,
        revives: host.reviveCharges() + (save.upRevive || 0),
        drafts: 0,
        player: {
          x: ARENA.w / 2, y: ARENA.h / 2,
          vx: 0, vy: 0,
          hp: metaHp(), maxHp: metaHp(),
          facing: 1,
          hurt: 0, fireAnim: 0, invuln: 0
        },
        weapons: { blaster: 1 },
        evolved: {},
        perks: {},
        cooldowns: {},
        enemies: [],
        bullets: [],
        foeBullets: [],
        orbs: [],
        zones: [],
        orbitAngle: 0,
        drones: [],
        cam: { x: ARENA.w / 2, y: ARENA.h / 2 }
      };
      A.Fx.reset();
    }

    /* --------------------------------------------------------- stats ---- */
    function damageMul() {
      var m = metaDamage() * (1 + (S.perks.might || 0) * 0.18);
      return m;
    }
    function rateMul() { return 1 / (1 + (S.perks.haste || 0) * 0.14); }
    function speedMul() { return 1 + (S.perks.boots || 0) * 0.12; }
    function pickupRange() { return 78 * metaMagnet() * (1 + (S.perks.magnet || 0) * 0.35); }
    function coinMul() { return host.coinMultiplier() * (1 + (S.perks.fortune || 0) * 0.2) * (1 + save.upCoin * 0.1); }
    function critChance() { return (S.perks.crit || 0) * 0.08; }

    function weaponLevel(key) { return S.weapons[key] || 0; }

    /* -------------------------------------------------------- spawning -- */
    function waveBudget() {
      return 4 + S.wave * 2.6;
    }

    function enemyScale() {
      return 1 + (S.wave - 1) * 0.28;
    }

    function spawnEnemy(typeKey, angleHint) {
      var t = ENEMY_TYPES[typeKey];
      var scale = t.boss ? 1 + (S.wave - 1) * 0.18 : enemyScale();
      var a = angleHint !== undefined ? angleHint : Math.random() * TAU;
      var radius = 470 + Math.random() * 130;

      var x = A.clamp(S.player.x + Math.cos(a) * radius, 40, ARENA.w - 40);
      var y = A.clamp(S.player.y + Math.sin(a) * radius, 40, ARENA.h - 40);

      S.enemies.push({
        type: typeKey,
        x: x, y: y,
        hp: t.hp * scale, maxHp: t.hp * scale,
        speed: t.speed * (1 + (S.wave - 1) * 0.012),
        dmg: t.dmg * (1 + (S.wave - 1) * 0.09),
        size: t.size,
        facing: 1,
        hitFlash: 0,
        attackT: Math.random() * 2,
        stun: 0,
        boss: !!t.boss,
        frame: Math.random()
      });
    }

    function updateWave(dt) {
      S.waveT += dt;

      if (S.waveT >= S.waveLength) {
        S.wave++;
        S.waveT = 0;
        host.progress("wave", S.wave);

        if (S.wave > 20) { finish(true); return; }

        A.Audio.sfx("levelup");
        A.Fx.text(A.View.w / 2, A.View.h * 0.34, 0, "WAVE " + S.wave, [255, 194, 75],
          { screen: true, scale: 1.5, life: 1.4, rise: -10 });

        if (S.wave % 5 === 0) {
          spawnEnemy("boss");
          A.Fx.text(A.View.w / 2, A.View.h * 0.42, 0, "BOSS", [255, 77, 109],
            { screen: true, scale: 1.3, life: 1.6, rise: -8 });
        }
      }

      S.spawnT -= dt;
      if (S.spawnT <= 0) {
        S.spawnT = Math.max(0.22, 1.0 - S.wave * 0.04);

        var alive = S.enemies.length;
        if (alive < waveBudget() * 3) {
          var roll = Math.random();
          var key = "grunt";
          if (S.wave >= 3 && roll < 0.28) key = "runner";
          if (S.wave >= 5 && roll > 0.82) key = "shooter";
          if (S.wave >= 7 && roll > 0.93) key = "brute";
          var count = 2 + Math.floor(S.wave / 4);
          for (var i = 0; i < count; i++) spawnEnemy(key);
        }
      }
    }

    /* --------------------------------------------------------- combat --- */
    function hurtEnemy(e, amount, knockX, knockY) {
      var crit = Math.random() < critChance();
      var dealt = amount * (crit ? 2.1 : 1);
      e.hp -= dealt;
      e.hitFlash = 0.14;

      if (knockX || knockY) {
        e.x += (knockX || 0) * (e.boss ? 0.15 : 1);
        e.y += (knockY || 0) * (e.boss ? 0.15 : 1);
      }

      if (crit) {
        A.Fx.text(e.x, 72 * e.size, e.y, Math.round(dealt).toString(), [255, 224, 138],
          { scale: 1.1, life: 0.6, rise: 70 });
      }

      if (e.hp <= 0) killEnemy(e);
    }

    function killEnemy(e) {
      var index = S.enemies.indexOf(e);
      if (index < 0) return;
      S.enemies.splice(index, 1);

      var t = ENEMY_TYPES[e.type];
      S.kills++;
      host.progress("kill", 1);

      var coins = Math.round(t.coins * coinMul() * (1 + S.wave * 0.06));
      S.coins += coins;

      S.orbs.push({ x: e.x, y: e.y, xp: t.xp, vx: 0, vy: 0, t: 0 });

      A.Fx.burst(e.x, 34 * e.size, e.y, e.boss ? 36 : 9, {
        color: e.boss ? [255, 194, 75] : [255, 90, 110],
        speed: e.boss ? 240 : 130, up: e.boss ? 220 : 120, gravity: 520,
        life: e.boss ? 0.9 : 0.5, size: e.boss ? 7 : 4.5, bounce: false
      });

      A.Audio.sfx(e.boss ? "win" : "hit");
      if (e.boss) { A.Fx.kick(0.7); A.Fx.hitStop(0.08); }
    }

    function nearestEnemy(x, y, maxDist) {
      var best = null, bestD = (maxDist || 1e9) * (maxDist || 1e9);
      for (var i = 0; i < S.enemies.length; i++) {
        var e = S.enemies[i];
        var d = A.dist2(x, y, e.x, e.y);
        if (d < bestD) { bestD = d; best = e; }
      }
      return best;
    }

    function fireBullet(x, y, tx, ty, damage, speed, opts) {
      opts = opts || {};
      var dx = tx - x, dy = ty - y;
      var len = Math.hypot(dx, dy) || 1;
      S.bullets.push({
        x: x, y: y,
        vx: dx / len * speed, vy: dy / len * speed,
        dmg: damage, life: opts.life || 1.4,
        r: opts.r || 7,
        color: opts.color || [122, 224, 255],
        pierce: opts.pierce || 0,
        hits: []
      });
    }

    function tickWeapons(dt) {
      var p = S.player;
      var cd = S.cooldowns;
      var dmg = damageMul();

      // Blaster / Storm Repeater
      var bl = weaponLevel("blaster");
      if (bl > 0) {
        cd.blaster = (cd.blaster || 0) - dt;
        if (cd.blaster <= 0) {
          var target = nearestEnemy(p.x, p.y, 620);
          if (target) {
            var evolved = S.evolved.blaster;
            var shots = evolved ? 3 : 1 + Math.floor(bl / 3);
            var base = WEAPONS.blaster.damage(bl) * dmg * (evolved ? 1.5 : 1);
            for (var i = 0; i < shots; i++) {
              var spread = (i - (shots - 1) / 2) * 0.16;
              var a = Math.atan2(target.y - p.y, target.x - p.x) + spread;
              fireBullet(p.x, p.y - 26, p.x + Math.cos(a) * 100, p.y + Math.sin(a) * 100,
                base, 640, { color: [122, 224, 255], pierce: evolved ? 2 : 0, r: evolved ? 9 : 7 });
            }
            cd.blaster = WEAPONS.blaster.cooldown(bl) * rateMul() * (evolved ? 0.45 : 1);
            p.fireAnim = 0.22;
            p.facing = target.x >= p.x ? 1 : -1;
            A.Audio.sfx("shoot");
          }
        }
      }

      // Orbit blades / Ruin Halo
      var ol = weaponLevel("orbit");
      if (ol > 0) {
        var evolvedOrbit = S.evolved.orbit;
        var blades = evolvedOrbit ? 6 : 1 + Math.ceil(ol / 2);
        var radius = evolvedOrbit ? 140 : 92 + ol * 6;
        S.orbitAngle += dt * (2.0 + ol * 0.18);

        for (var b = 0; b < blades; b++) {
          var ang = S.orbitAngle + (b / blades) * TAU;
          var bx = p.x + Math.cos(ang) * radius;
          var by = p.y + Math.sin(ang) * radius;
          for (var e = S.enemies.length - 1; e >= 0; e--) {
            var en = S.enemies[e];
            if (A.dist2(bx, by, en.x, en.y) < 900 && !en._orbitLock) {
              hurtEnemy(en, WEAPONS.orbit.damage(ol) * dmg * (evolvedOrbit ? 1.8 : 1) * dt * 6,
                Math.cos(ang) * 3, Math.sin(ang) * 3);
              en._orbitLock = 0.12;
            }
          }
        }
      }

      for (var k = 0; k < S.enemies.length; k++) {
        if (S.enemies[k]._orbitLock) S.enemies[k]._orbitLock = Math.max(0, S.enemies[k]._orbitLock - dt);
      }

      // Shock Nova / Aegis Pulse
      var nl = weaponLevel("nova");
      if (nl > 0) {
        cd.nova = (cd.nova || 0) - dt;
        if (cd.nova <= 0) {
          var evolvedNova = S.evolved.nova;
          var r = (90 + nl * 26) * (evolvedNova ? 1.6 : 1);
          var healed = 0;
          for (var n = S.enemies.length - 1; n >= 0; n--) {
            var target2 = S.enemies[n];
            if (A.dist2(p.x, p.y, target2.x, target2.y) < r * r) {
              var dx2 = target2.x - p.x, dy2 = target2.y - p.y;
              var l2 = Math.hypot(dx2, dy2) || 1;
              hurtEnemy(target2, WEAPONS.nova.damage(nl) * dmg * (evolvedNova ? 1.7 : 1),
                dx2 / l2 * 26, dy2 / l2 * 26);
              healed++;
            }
          }
          if (evolvedNova && healed) heal(healed * 1.4);
          S.zones.push({ x: p.x, y: p.y, r: r, t: 0, life: 0.4, ring: true, color: [185, 139, 255] });
          cd.nova = WEAPONS.nova.cooldown(nl) * rateMul();
          A.Fx.kick(0.22);
        }
      }

      // Arc Chain / Tesla Cascade
      var cl = weaponLevel("chain");
      if (cl > 0) {
        cd.chain = (cd.chain || 0) - dt;
        if (cd.chain <= 0) {
          var evolvedChain = S.evolved.chain;
          var jumps = evolvedChain ? 8 : 2 + cl;
          var from = p;
          var chainDmg = WEAPONS.chain.damage(cl) * dmg * (evolvedChain ? 1.6 : 1);
          var visited = [];
          for (var j = 0; j < jumps; j++) {
            var next = null, nd = 340 * 340;
            for (var q = 0; q < S.enemies.length; q++) {
              var cand = S.enemies[q];
              if (visited.indexOf(cand) >= 0) continue;
              var d2 = A.dist2(from.x, from.y, cand.x, cand.y);
              if (d2 < nd) { nd = d2; next = cand; }
            }
            if (!next) break;
            visited.push(next);
            S.zones.push({ line: true, x: from.x, y: from.y, x2: next.x, y2: next.y, t: 0, life: 0.18,
              color: [123, 224, 255] });
            if (evolvedChain) next.stun = 0.5;
            hurtEnemy(next, chainDmg);
            from = next;
          }
          if (visited.length) { cd.chain = WEAPONS.chain.cooldown(cl) * rateMul(); A.Audio.sfx("shoot"); }
          else cd.chain = 0.3;
        }
      }

      // Sentry drones
      var dl = weaponLevel("drone");
      if (dl > 0) {
        var wanted = dl >= 4 ? 2 : 1;
        while (S.drones.length < wanted) S.drones.push({ a: S.drones.length * Math.PI, cd: 0 });
        for (var dI = 0; dI < S.drones.length; dI++) {
          var drone = S.drones[dI];
          drone.a += dt * 1.5;
          drone.cd -= dt;
          var dxp = p.x + Math.cos(drone.a) * 74;
          var dyp = p.y + Math.sin(drone.a) * 74;
          if (drone.cd <= 0) {
            var dt2 = nearestEnemy(dxp, dyp, 520);
            if (dt2) {
              fireBullet(dxp, dyp, dt2.x, dt2.y, WEAPONS.drone.damage(dl) * dmg, 560,
                { color: [63, 217, 138], r: 6 });
              drone.cd = WEAPONS.drone.cooldown(dl) * rateMul();
            }
          }
          drone.x = dxp; drone.y = dyp;
        }
      }

      // Scorch mines
      var ml = weaponLevel("mines");
      if (ml > 0) {
        cd.mines = (cd.mines || 0) - dt;
        if (cd.mines <= 0) {
          S.zones.push({ x: p.x, y: p.y, r: 62 + ml * 8, t: 0, life: 3.2, burn: true,
            dps: WEAPONS.mines.damage(ml) * dmg, color: [255, 122, 69] });
          cd.mines = WEAPONS.mines.cooldown(ml) * rateMul();
        }
      }
    }

    function heal(amount) {
      var p = S.player;
      var before = p.hp;
      p.hp = Math.min(p.maxHp, p.hp + amount);
      if (p.hp - before > 1) {
        A.Fx.text(p.x, 96, p.y, "+" + Math.round(p.hp - before), [139, 243, 190],
          { scale: 0.8, life: 0.7, rise: 70 });
      }
    }

    /* ---------------------------------------------------- level & draft -- */
    function gainXp(amount) {
      S.xp += amount;
      while (S.xp >= S.xpNeed) {
        S.xp -= S.xpNeed;
        S.level++;
        S.xpNeed = Math.round(9 + S.level * 4 + Math.pow(S.level, 1.55));
        S.pendingLevels++;
      }
    }

    function draftOptions() {
      var options = [];

      // Evolutions first: they are the run's payoff moment.
      Object.keys(EVOLUTIONS).forEach(function (key) {
        var evo = EVOLUTIONS[key];
        if (S.evolved[key]) return;
        if ((S.weapons[key] || 0) < WEAPONS[key].max) return;
        if ((S.perks[evo.perk] || 0) < 3) return;
        options.push({
          kind: "evolve", key: key, name: evo.name, icon: evo.icon,
          color: evo.color, desc: evo.desc, tag: "EVOLUTION"
        });
      });

      var pool = [];
      Object.keys(WEAPONS).forEach(function (key) {
        var level = S.weapons[key] || 0;
        if (S.evolved[key] || level >= WEAPONS[key].max) return;
        if (level === 0 && Object.keys(S.weapons).length >= 5) return;
        pool.push({
          kind: "weapon", key: key, name: WEAPONS[key].name, icon: WEAPONS[key].icon,
          color: WEAPONS[key].color, desc: WEAPONS[key].desc(level + 1),
          tag: level === 0 ? "NEW" : "LV " + (level + 1)
        });
      });
      Object.keys(PERKS).forEach(function (key) {
        var level = S.perks[key] || 0;
        if (level >= PERKS[key].max) return;
        pool.push({
          kind: "perk", key: key, name: PERKS[key].name, icon: PERKS[key].icon,
          color: PERKS[key].color, desc: PERKS[key].desc(level + 1),
          tag: level === 0 ? "NEW" : "LV " + (level + 1)
        });
      });

      A.rng((S.level * 7919 + S.kills) | 0).shuffle(pool);
      while (options.length < 3 && pool.length) options.push(pool.shift());
      return options.slice(0, 3);
    }

    function showDraft() {
      var options = draftOptions();
      if (!options.length) { S.pendingLevels--; return; }

      var wrap = host.el("div", "draft");
      var head = host.el("div", "card");
      head.style.marginBottom = "10px";
      head.appendChild(host.el("h2", null, "LEVEL " + S.level));
      head.appendChild(host.el("p", "sub", "Choose one"));
      wrap.appendChild(head);

      options.forEach(function (opt) {
        var pick = host.el("button", "pick" + (opt.kind === "evolve" ? " evolve" : ""));
        var ico = host.el("div", "ico", opt.icon);
        ico.style.background = opt.color;
        pick.appendChild(ico);

        var text = host.el("div");
        text.appendChild(host.el("h3", null, opt.name));
        text.appendChild(host.el("div", "lvl", opt.tag));
        text.appendChild(host.el("p", null, opt.desc));
        pick.appendChild(text);

        pick.addEventListener("click", function () {
          applyDraft(opt);
          host.modal.hide();
          S.pendingLevels--;
          S.drafts++;
          host.progress("draft", 1);
          A.Audio.sfx("levelup");
          if (S.pendingLevels > 0) setTimeout(showDraft, 60);
        });
        wrap.appendChild(pick);
      });

      host.modal.show(wrap);
    }

    function applyDraft(opt) {
      if (opt.kind === "weapon") {
        S.weapons[opt.key] = (S.weapons[opt.key] || 0) + 1;
      } else if (opt.kind === "perk") {
        S.perks[opt.key] = (S.perks[opt.key] || 0) + 1;
        if (opt.key === "vitality") {
          S.player.maxHp += 22;
          heal(22);
        }
      } else if (opt.kind === "evolve") {
        S.evolved[opt.key] = true;
        A.Fx.flashScreen(0.6, [255, 224, 138]);
        A.Fx.kick(0.6);
        host.toast(opt.name.toUpperCase() + " UNLOCKED");
      }
    }

    /* --------------------------------------------------------- update --- */
    function update(dt) {
      if (S.phase !== "run") return;

      if (S.pendingLevels > 0 && !host.modal.visible()) { showDraft(); return; }

      S.t += dt;
      updateWave(dt);
      updatePlayer(dt);
      tickWeapons(dt);
      updateEnemies(dt);
      updateBullets(dt);
      updateOrbs(dt);
      updateZones(dt);

      // Follow, but never show past the arena edge.
      var halfW = A.View.w / 2, halfH = A.View.h / 2 / DEPTH;
      var tx = ARENA.w > halfW * 2 ? A.clamp(S.player.x, halfW, ARENA.w - halfW) : ARENA.w / 2;
      var ty = ARENA.h > halfH * 2 ? A.clamp(S.player.y, halfH, ARENA.h - halfH) : ARENA.h / 2;
      S.cam.x = A.approach(S.cam.x, tx, 6, dt);
      S.cam.y = A.approach(S.cam.y, ty, 6, dt);

      paintHud();
    }

    function updatePlayer(dt) {
      var p = S.player;
      var speed = 205 * speedMul();
      var mx = 0, my = 0;

      if (A.Input.down) {
        var dx = A.Input.x - A.Input.startX;
        var dy = A.Input.y - A.Input.startY;
        var len = Math.hypot(dx, dy);
        if (len > 6) {
          var k = Math.min(1, len / 70);
          mx = dx / len * k;
          my = dy / len * k;
        }
      }
      if (A.Input.axis) mx = A.Input.axis;
      if (A.Input.keys["w"] || A.Input.keys["arrowup"]) my = -1;
      if (A.Input.keys["s"] || A.Input.keys["arrowdown"]) my = 1;

      p.vx = A.approach(p.vx, mx * speed, 14, dt);
      p.vy = A.approach(p.vy, my * speed, 14, dt);
      p.x = A.clamp(p.x + p.vx * dt, 30, ARENA.w - 30);
      p.y = A.clamp(p.y + p.vy * dt, 30, ARENA.h - 30);

      if (Math.abs(p.vx) > 12) p.facing = p.vx > 0 ? 1 : -1;
      if (p.hurt > 0) p.hurt -= dt;
      if (p.fireAnim > 0) p.fireAnim -= dt;
      if (p.invuln > 0) p.invuln -= dt;

      if (S.perks.regen) heal(1.2 * S.perks.regen * dt);
    }

    function damagePlayer(amount) {
      var p = S.player;
      if (p.invuln > 0) return;

      p.hp -= amount;
      p.hurt = 0.35;
      p.invuln = 0.45;
      A.Fx.kick(0.4);
      A.Fx.flashScreen(0.3, [255, 77, 109]);
      A.Audio.sfx("bad");
      A.vibrate(30);

      if (p.hp <= 0) {
        if (S.revives > 0) {
          S.revives--;
          p.hp = p.maxHp * 0.6;
          p.invuln = 2.2;
          host.toast("SECOND WIND");
          A.Fx.flashScreen(0.7, [139, 243, 190]);
          for (var i = S.enemies.length - 1; i >= 0; i--) {
            if (!S.enemies[i].boss) killEnemy(S.enemies[i]);
          }
        } else {
          finish(false);
        }
      }
    }

    function updateEnemies(dt) {
      var p = S.player;

      for (var i = S.enemies.length - 1; i >= 0; i--) {
        var e = S.enemies[i];
        var t = ENEMY_TYPES[e.type];

        if (e.hitFlash > 0) e.hitFlash -= dt;
        if (e.stun > 0) { e.stun -= dt; continue; }

        var dx = p.x - e.x, dy = p.y - e.y;
        var d = Math.hypot(dx, dy) || 1;
        e.facing = dx >= 0 ? 1 : -1;
        e.frame += dt * 1.4;

        var wantsClose = t.contact || d > (t.range || 0);
        if (wantsClose) {
          e.x += dx / d * e.speed * dt;
          e.y += dy / d * e.speed * dt;
        }

        // Soft separation so a horde does not collapse into one pixel.
        for (var j = i - 1; j >= 0 && j > i - 7; j--) {
          var o = S.enemies[j];
          var sx = e.x - o.x, sy = e.y - o.y;
          var sd = sx * sx + sy * sy;
          var minD = 34 * (e.size + o.size) * 0.5;
          if (sd < minD * minD && sd > 0.01) {
            var f = (minD - Math.sqrt(sd)) * 0.5;
            var inv = 1 / Math.sqrt(sd);
            e.x += sx * inv * f; e.y += sy * inv * f;
            o.x -= sx * inv * f; o.y -= sy * inv * f;
          }
        }

        if (t.contact) {
          if (d < 30 + e.size * 22) {
            e.attackT -= dt;
            if (e.attackT <= 0) {
              e.attackT = 0.85;
              damagePlayer(e.dmg);
            }
          }
        } else {
          e.attackT -= dt;
          if (e.attackT <= 0 && d < t.range * 1.2) {
            e.attackT = 2.0;
            var len = d;
            S.foeBullets.push({
              x: e.x, y: e.y - 30,
              vx: dx / len * 260, vy: dy / len * 260,
              dmg: e.dmg, life: 2.4, r: 8
            });
          }
        }

        if (e.boss && Math.random() < dt * 0.5) {
          spawnEnemy("runner", Math.random() * TAU);
        }
      }
    }

    function updateBullets(dt) {
      for (var i = S.bullets.length - 1; i >= 0; i--) {
        var b = S.bullets[i];
        b.t = (b.t || 0) + dt;
        b.life -= dt;
        b.x += b.vx * dt;
        b.y += b.vy * dt;

        if (b.life <= 0) { S.bullets.splice(i, 1); continue; }

        for (var j = S.enemies.length - 1; j >= 0; j--) {
          var e = S.enemies[j];
          if (b.hits.indexOf(e) >= 0) continue;
          var rr = (b.r + 22 * e.size);
          if (A.dist2(b.x, b.y, e.x, e.y) < rr * rr) {
            hurtEnemy(e, b.dmg, b.vx * 0.02, b.vy * 0.02);
            A.Fx.burst(b.x, 26, b.y, 4, { color: b.color, speed: 90, up: 70, gravity: 480,
              life: 0.28, size: 4, bounce: false });
            b.hits.push(e);
            if (b.pierce > 0) { b.pierce--; }
            else { S.bullets.splice(i, 1); }
            break;
          }
        }
      }

      for (var k = S.foeBullets.length - 1; k >= 0; k--) {
        var f = S.foeBullets[k];
        f.life -= dt;
        f.x += f.vx * dt;
        f.y += f.vy * dt;
        if (f.life <= 0) { S.foeBullets.splice(k, 1); continue; }
        if (A.dist2(f.x, f.y, S.player.x, S.player.y) < 30 * 30) {
          damagePlayer(f.dmg);
          S.foeBullets.splice(k, 1);
        }
      }
    }

    function updateOrbs(dt) {
      var p = S.player;
      var range = pickupRange();

      for (var i = S.orbs.length - 1; i >= 0; i--) {
        var o = S.orbs[i];
        o.t += dt;
        var dx = p.x - o.x, dy = p.y - o.y;
        var d = Math.hypot(dx, dy) || 1;

        if (d < range) {
          var pull = 300 + (range - d) * 5;
          o.x += dx / d * pull * dt;
          o.y += dy / d * pull * dt;
        }

        if (d < 26) {
          gainXp(o.xp);
          S.orbs.splice(i, 1);
          A.Audio.sfx("coin");
        }
      }
    }

    function updateZones(dt) {
      for (var i = S.zones.length - 1; i >= 0; i--) {
        var z = S.zones[i];
        z.t += dt;
        if (z.t >= z.life) { S.zones.splice(i, 1); continue; }

        if (z.burn) {
          for (var j = S.enemies.length - 1; j >= 0; j--) {
            var e = S.enemies[j];
            if (A.dist2(z.x, z.y, e.x, e.y) < z.r * z.r) hurtEnemy(e, z.dps * dt);
          }
        }
      }
    }

    /* ---------------------------------------------------------- finish -- */
    function finish(win) {
      if (S.phase !== "run") return;
      S.phase = win ? "win" : "dead";

      var coins = Math.round(S.coins);
      save.coins += coins;
      save.runs++;
      save.bestWave = Math.max(save.bestWave, S.wave);
      save.bestKills = Math.max(save.bestKills, S.kills);
      host.commit();

      var xp = Math.round((S.kills * 0.8 + S.wave * 14) * host.xpMultiplier());
      host.addXp(xp);
      host.progress("run", 1);
      host.progress("coins", coins);
      host.progress("wave", S.wave);

      A.Audio.sfx(win ? "win" : "lose");
      A.vibrate(win ? [20, 40, 30] : 120);

      host.results({
        win: win,
        title: win ? "ARENA CLEARED" : "OVERRUN",
        subtitle: win ? "Twenty waves, no ground given" : "Wave " + S.wave,
        stats: [
          ["Wave", String(S.wave)],
          ["Kills", A.formatNumber(S.kills)],
          ["Coins", "+" + A.formatNumber(coins)],
          ["Account XP", "+" + A.formatNumber(xp)]
        ],
        buttons: [
          { label: "RUN AGAIN", className: "go", onClick: function () { host.modal.hide(); begin(); } },
          { label: "ARMOURY", className: "gold", onClick: openShop },
          { label: "MENU", className: "ghost", onClick: host.exit }
        ]
      });
    }

    /* ------------------------------------------------------------ shop -- */
    function openShop() {
      var rows = [
        { key: "upDamage", name: "Weapon Tuning", desc: "+10% damage from the first shot", max: 15,
          cost: function (l) { return Math.round(120 * Math.pow(1.28, l)); } },
        { key: "upHp", name: "Plating", desc: "+20 starting HP", max: 15,
          cost: function (l) { return Math.round(140 * Math.pow(1.3, l)); } },
        { key: "upMagnet", name: "Collector", desc: "+15% pickup range", max: 8,
          cost: function (l) { return Math.round(180 * Math.pow(1.35, l)); } },
        { key: "upCoin", name: "Scavenger", desc: "+10% coins", max: 10,
          cost: function (l) { return Math.round(200 * Math.pow(1.34, l)); } },
        { key: "upRevive", name: "Field Medic", desc: "Start with an extra revive", max: 2,
          cost: function (l) { return 1500 + l * 2500; } }
      ];

      var card = host.el("div", "card");
      card.appendChild(host.el("h2", null, "ARMOURY"));
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
    function toScreen(x, y) {
      return {
        x: A.View.w / 2 + (x - S.cam.x),
        y: A.View.h / 2 + (y - S.cam.y) * DEPTH
      };
    }

    function render(g) {
      var w = A.View.w, h = A.View.h;

      g.fillStyle = "#120C24";
      g.fillRect(0, 0, w, h);

      A.Fx.applyShake(g);
      drawFloor(g);
      drawZones(g);

      var drawables = [];
      for (var i = 0; i < S.orbs.length; i++) drawables.push({ y: S.orbs[i].y, kind: "orb", ref: S.orbs[i] });
      for (var j = 0; j < S.enemies.length; j++) drawables.push({ y: S.enemies[j].y, kind: "enemy", ref: S.enemies[j] });
      drawables.push({ y: S.player.y, kind: "player", ref: S.player });
      for (var d = 0; d < S.drones.length; d++) drawables.push({ y: S.drones[d].y || 0, kind: "drone", ref: S.drones[d] });

      drawables.sort(function (a, b) { return a.y - b.y; });

      for (var k = 0; k < drawables.length; k++) {
        var item = drawables[k];
        if (item.kind === "orb") drawOrb(g, item.ref);
        else if (item.kind === "enemy") drawEnemy(g, item.ref);
        else if (item.kind === "player") drawPlayer(g);
        else drawDrone(g, item.ref);
      }

      drawOrbitBlades(g);
      drawBullets(g);

      A.Fx.drawParticles(g, function (x, y, z) {
        var q = toScreen(x, z);
        return { x: q.x, y: q.y - y, s: 1 };
      });
      A.Fx.drawTexts(g, function (x, y, z) {
        var q = toScreen(x, z);
        return { x: q.x, y: q.y - y, s: 34 };
      });

      g.restore();
      A.Fx.drawFlash(g, w, h);
    }

    function drawFloor(g) {
      var w = A.View.w, h = A.View.h;
      var origin = toScreen(0, 0);
      var far = toScreen(ARENA.w, ARENA.h);

      var grad = g.createLinearGradient(0, origin.y, 0, far.y);
      grad.addColorStop(0, "#241B45");
      grad.addColorStop(1, "#150F2E");
      g.fillStyle = grad;
      g.fillRect(origin.x, origin.y, far.x - origin.x, far.y - origin.y);

      g.strokeStyle = "rgba(120,100,190,0.16)";
      g.lineWidth = 1;
      var step = 100;
      for (var x = 0; x <= ARENA.w; x += step) {
        var a = toScreen(x, 0), b = toScreen(x, ARENA.h);
        if (a.x < -40 || a.x > w + 40) continue;
        g.beginPath(); g.moveTo(a.x, a.y); g.lineTo(b.x, b.y); g.stroke();
      }
      for (var y = 0; y <= ARENA.h; y += step) {
        var c = toScreen(0, y), d = toScreen(ARENA.w, y);
        if (c.y < -40 || c.y > h + 40) continue;
        g.beginPath(); g.moveTo(c.x, c.y); g.lineTo(d.x, d.y); g.stroke();
      }

      // Floor decals break up the grid without costing a texture.
      if (!S.decals) {
        var rand = A.rng(31337);
        S.decals = [];
        for (var d = 0; d < 120; d++) {
          S.decals.push({
            x: rand.range(40, ARENA.w - 40), y: rand.range(40, ARENA.h - 40),
            r: rand.range(9, 26), a: rand.range(0.05, 0.13), rot: rand.range(0, TAU)
          });
        }
      }
      for (var di = 0; di < S.decals.length; di++) {
        var dec = S.decals[di];
        var dp = toScreen(dec.x, dec.y);
        if (dp.x < -80 || dp.x > w + 80 || dp.y < -60 || dp.y > h + 60) continue;
        g.globalAlpha = dec.a;
        g.fillStyle = "#8E7BD8";
        g.beginPath();
        g.ellipse(dp.x, dp.y, dec.r, dec.r * DEPTH, dec.rot, 0, TAU);
        g.fill();
      }
      g.globalAlpha = 1;

      // Arena wall: a lit lip all the way round so the edge is unmistakable.
      var lip = 26;
      g.fillStyle = "#0D0920";
      g.fillRect(origin.x - lip, origin.y - lip, far.x - origin.x + lip * 2, lip);
      g.fillRect(origin.x - lip, far.y, far.x - origin.x + lip * 2, lip);
      g.fillRect(origin.x - lip, origin.y - lip, lip, far.y - origin.y + lip * 2);
      g.fillRect(far.x, origin.y - lip, lip, far.y - origin.y + lip * 2);

      g.strokeStyle = "rgba(190,150,255,0.65)";
      g.lineWidth = 4;
      g.strokeRect(origin.x, origin.y, far.x - origin.x, far.y - origin.y);

      var vig = g.createRadialGradient(w / 2, h / 2, h * 0.28, w / 2, h / 2, h * 0.8);
      vig.addColorStop(0, "rgba(10,7,22,0)");
      vig.addColorStop(1, "rgba(10,7,22,0.55)");
      g.fillStyle = vig;
      g.fillRect(0, 0, w, h);
    }

    function drawZones(g) {
      for (var i = 0; i < S.zones.length; i++) {
        var z = S.zones[i];
        var k = 1 - z.t / z.life;

        if (z.line) {
          var a = toScreen(z.x, z.y), b = toScreen(z.x2, z.y2);
          g.globalAlpha = k;
          g.strokeStyle = A.rgb(z.color);
          g.lineWidth = 5 * k + 1;
          g.beginPath();
          g.moveTo(a.x, a.y - 30);
          var midX = (a.x + b.x) / 2 + (Math.random() - 0.5) * 22;
          var midY = (a.y + b.y) / 2 - 30 + (Math.random() - 0.5) * 22;
          g.lineTo(midX, midY);
          g.lineTo(b.x, b.y - 30);
          g.stroke();
          g.globalAlpha = 1;
          continue;
        }

        var p = toScreen(z.x, z.y);
        g.globalAlpha = z.ring ? k * 0.8 : 0.35 + k * 0.2;
        if (z.ring) {
          g.strokeStyle = A.rgb(z.color);
          g.lineWidth = 6 * k + 2;
          g.beginPath();
          g.ellipse(p.x, p.y, z.r * (1.2 - k * 0.2), z.r * DEPTH * (1.2 - k * 0.2), 0, 0, TAU);
          g.stroke();
        } else {
          g.fillStyle = A.rgb(z.color);
          g.beginPath();
          g.ellipse(p.x, p.y, z.r, z.r * DEPTH, 0, 0, TAU);
          g.fill();
        }
        g.globalAlpha = 1;
      }
    }

    function drawEnemy(g, e) {
      var atlas = atlases[e.type];
      var p = toScreen(e.x, e.y);
      if (p.x < -90 || p.x > A.View.w + 90 || p.y < -120 || p.y > A.View.h + 120) return;

      g.globalAlpha = 0.3;
      g.fillStyle = "#0B0818";
      g.beginPath();
      g.ellipse(p.x, p.y, 20 * e.size, 8 * e.size, 0, 0, TAU);
      g.fill();
      g.globalAlpha = 1;

      var scale = (58 * e.size) / atlas.height;
      var frame = Math.floor(e.frame * atlas.frames) % atlas.frames;

      if (e.hitFlash > 0) {
        g.save();
        g.globalCompositeOperation = "lighter";
        A.Rig.blit(g, atlas, frame, p.x, p.y, scale, e.facing);
        g.restore();
      }
      A.Rig.blit(g, atlas, frame, p.x, p.y, scale, e.facing);

      if (e.boss || e.hp < e.maxHp) {
        var barW = 44 * e.size;
        var y = p.y - 78 * e.size;
        g.fillStyle = "rgba(10,8,20,0.8)";
        g.fillRect(p.x - barW / 2, y, barW, 6);
        g.fillStyle = e.boss ? "#FFC24B" : "#FF4D6D";
        g.fillRect(p.x - barW / 2 + 1, y + 1, (barW - 2) * A.clamp01(e.hp / e.maxHp), 4);
      }
    }

    function drawPlayer(g) {
      var p = S.player;
      var s = toScreen(p.x, p.y);

      g.globalAlpha = 0.32;
      g.fillStyle = "#0B0818";
      g.beginPath();
      g.ellipse(s.x, s.y, 22, 9, 0, 0, TAU);
      g.fill();
      g.globalAlpha = 1;

      var moving = Math.hypot(p.vx, p.vy) > 24;
      var clip = p.hurt > 0.15 ? "hurt" : (p.fireAnim > 0 ? "shoot" : (moving ? "run" : "idle"));
      var t = clip === "shoot" ? (0.22 - p.fireAnim) * 4
            : clip === "hurt" ? (0.35 - p.hurt) * 3
            : A.wrap(S.t * (moving ? 2.1 : 0.5), 1);

      if (p.invuln > 0 && Math.floor(p.invuln * 20) % 2 === 0) g.globalAlpha = 0.45;
      var pose = A.Rig.pose(clip, t);
      A.Rig.draw(g, playerStyle, pose, s.x, s.y, 64, p.facing, { noShadow: true });
      g.globalAlpha = 1;
    }

    function drawDrone(g, drone) {
      if (drone.x === undefined) return;
      var p = toScreen(drone.x, drone.y);
      var bob = Math.sin(S.t * 6 + drone.a) * 3;

      g.fillStyle = "#3FD98A";
      g.beginPath();
      g.moveTo(p.x, p.y - 8 + bob);
      g.lineTo(p.x + 11, p.y + bob);
      g.lineTo(p.x, p.y + 8 + bob);
      g.lineTo(p.x - 11, p.y + bob);
      g.closePath();
      g.fill();

      g.fillStyle = "#0B0818";
      g.beginPath();
      g.arc(p.x, p.y + bob, 3.4, 0, TAU);
      g.fill();
    }

    function drawOrbitBlades(g) {
      var ol = weaponLevel("orbit");
      if (!ol) return;

      var evolved = S.evolved.orbit;
      var blades = evolved ? 6 : 1 + Math.ceil(ol / 2);
      var radius = evolved ? 140 : 92 + ol * 6;

      for (var b = 0; b < blades; b++) {
        var ang = S.orbitAngle + (b / blades) * TAU;
        var p = toScreen(S.player.x + Math.cos(ang) * radius, S.player.y + Math.sin(ang) * radius);
        g.save();
        g.translate(p.x, p.y - 26);
        g.rotate(S.orbitAngle * 3);
        g.fillStyle = evolved ? "#FFE08A" : "#FFC24B";
        g.beginPath();
        g.moveTo(0, -(evolved ? 17 : 12));
        g.lineTo(evolved ? 8 : 6, 0);
        g.lineTo(0, evolved ? 17 : 12);
        g.lineTo(evolved ? -8 : -6, 0);
        g.closePath();
        g.fill();
        g.restore();
      }
    }

    function drawBullets(g) {
      for (var i = 0; i < S.bullets.length; i++) {
        var b = S.bullets[i];
        var p = toScreen(b.x, b.y);
        g.fillStyle = A.rgb(b.color);
        g.beginPath();
        g.ellipse(p.x, p.y - 26, b.r, b.r * 0.7, Math.atan2(b.vy, b.vx), 0, TAU);
        g.fill();
      }

      for (var j = 0; j < S.foeBullets.length; j++) {
        var f = S.foeBullets[j];
        var q = toScreen(f.x, f.y);
        g.fillStyle = "#FF4D6D";
        g.beginPath();
        g.arc(q.x, q.y - 26, f.r, 0, TAU);
        g.fill();
      }
    }

    function drawOrb(g, o) {
      var p = toScreen(o.x, o.y);
      var pulse = 1 + Math.sin(S.t * 8 + o.x) * 0.12;
      g.fillStyle = "#7BE0FF";
      g.beginPath();
      g.moveTo(p.x, p.y - 9 * pulse);
      g.lineTo(p.x + 6 * pulse, p.y);
      g.lineTo(p.x, p.y + 9 * pulse);
      g.lineTo(p.x - 6 * pulse, p.y);
      g.closePath();
      g.fill();
    }

    /* -------------------------------------------------------------- ui -- */
    function mount(root) {
      var hud = host.el("div", "hud");

      var top = host.el("div", "row");
      ui.wave = host.el("div", "chip", "WAVE 1");
      ui.hp = host.el("div", "meter hp");
      ui.hpFill = host.el("i");
      ui.hp.appendChild(ui.hpFill);
      ui.coins = host.el("div", "chip gold", "0");
      top.appendChild(ui.wave);
      top.appendChild(ui.hp);
      top.appendChild(ui.coins);
      hud.appendChild(top);

      var xpRow = host.el("div", "row");
      xpRow.style.marginTop = "8px";
      ui.lvl = host.el("div", "chip", "LV 1");
      ui.xp = host.el("div", "meter xp");
      ui.xpFill = host.el("i");
      ui.xp.appendChild(ui.xpFill);
      ui.timer = host.el("div", "chip", "0:22");
      xpRow.appendChild(ui.lvl);
      xpRow.appendChild(ui.xp);
      xpRow.appendChild(ui.timer);
      hud.appendChild(xpRow);

      ui.hint = host.el("div", "hint");
      ui.hint.textContent = "Drag to move  ·  weapons fire themselves";
      hud.appendChild(ui.hint);

      root.appendChild(hud);
    }

    function paintHud() {
      ui.wave.textContent = "WAVE " + S.wave;
      ui.coins.textContent = A.formatNumber(S.coins);
      ui.lvl.textContent = "LV " + S.level;
      ui.hpFill.style.width = A.clamp01(S.player.hp / S.player.maxHp) * 100 + "%";
      ui.xpFill.style.width = A.clamp01(S.xp / S.xpNeed) * 100 + "%";
      ui.timer.textContent = A.formatTime(Math.max(0, S.waveLength - S.waveT));
      if (S.t > 4 && ui.hint.textContent) ui.hint.textContent = "";
    }

    /* ------------------------------------------------------- lifecycle -- */
    function begin() {
      newRun();
      paintHud();
    }

    return {
      mount: mount,
      start: function () {
        g2d = A.View.ctx;
        if (!playerStyle) bakeAll();
        begin();
      },
      stop: function () { S = null; },
      update: function (dt) { if (S) update(dt); },
      render: function (g) { if (S) render(g); },
      onResize: function () {}
    };
  }

  /* ------------------------------------------------------ registration -- */
  var thumbAtlas = null;
  var thumbStyle = null;

  A.games.push({
    id: "horde",
    name: "Horde Arena",
    tagline: "Auto-battler. Survive 20 waves, draft upgrades, evolve your weapons.",
    accent: "#B98BFF",
    unlock: 2,
    template: { coins: 0, runs: 0, bestWave: 0, bestKills: 0,
                upDamage: 0, upHp: 0, upMagnet: 0, upCoin: 0, upRevive: 0 },
    bestLine: function (s) {
      return s.runs ? "Best wave " + s.bestWave + "  ·  " + A.formatNumber(s.coins) + " coins" : "New";
    },
    thumb: function (g, w, h, t) {
      g.fillStyle = "#1A1136";
      g.fillRect(0, 0, w, h);
      g.strokeStyle = "rgba(185,139,255,0.25)";
      g.lineWidth = 1;
      for (var i = 1; i < 4; i++) {
        g.beginPath(); g.moveTo(0, i * h / 4); g.lineTo(w, i * h / 4); g.stroke();
      }
      if (!thumbStyle) {
        thumbStyle = A.Rig.style(9001, { hue: 0.58, helmet: "crest", cape: true, weapon: "blaster" });
        thumbAtlas = A.Rig.bake(A.Rig.style(7717, { hue: 0.02, helmet: "dome", bulk: 0.95 }),
          "march", 8, { cell: 70, ss: 2, height: 46 });
      }
      for (var e = 0; e < 3; e++) {
        var ex = 16 + e * 26 + Math.sin(t * 1.5 + e) * 4;
        A.Rig.blit(g, thumbAtlas, Math.floor((t * 6 + e * 3)) % thumbAtlas.frames, ex, h - 12, 0.42, 1);
      }
      var pose = A.Rig.pose("shoot", A.wrap(t * 2, 1));
      A.Rig.draw(g, thumbStyle, pose, w - 24, h - 10, 46, -1, { noShadow: true });
    },
    create: create
  });
})(window.A);
