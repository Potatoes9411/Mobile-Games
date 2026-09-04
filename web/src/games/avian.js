/* ===========================================================================
   AVIAN ARTILLERY
   A slingshot artillery puzzle on Matter.js. Drag the pouch back against a
   tension limit, read the predicted arc, and bring a structure down on the
   targets sheltering inside it.

   Two decisions shape everything below.

   The launch sets velocity rather than accumulating force. A trajectory preview
   is a promise to the player, and the only way to keep that promise exactly is
   to make the launch deterministic: one impulse, integrated by the same maths
   the preview draws. An accumulating force spread over frames would drift from
   the dashed line by exactly as much as the frame timing wobbled.

   Sleeping is on. A stack of boxes that never quite settles jitters forever,
   bleeds frame time, and reads as a bug. Matter parks a body once it has been
   near-motionless long enough, and an impact wakes the region back up.
   =========================================================================== */
(function (A) {
  "use strict";
  A.games = A.games || [];

  /* World is authored in these units and the camera scales it to the screen. */
  var WORLD_W = 1400;
  var GROUND_Y = 640;
  /* Fork height above the ground is a real proportion: 80 units of stem under
     a 26-unit fork spread. The first pass put the anchor 210 units up, which
     drew the slingshot as a bare flagpole. */
  var ANCHOR = { x: 250, y: 542 };
  var MAX_PULL = 104;             // pouch cannot leave this radius
  var LAUNCH_POWER = 0.235;       // pull distance -> per-step displacement
  var GRAVITY_Y = 1;
  var GRAVITY_SCALE = 0.0016;     // heavier than Matter's default; nothing floats
  var STEP_MS = 1000 / 60;

  /*
   * Material tiers. `density` and `hp` are the two dials that matter: density
   * decides what a block does to whatever is under it, hp decides how much of
   * an impact it survives. Restitution stays low across the board on purpose -
   * bouncy debris is the single fastest way to make a demolition read as a
   * balloon fight rather than a collapse.
   */
  var MATERIALS = {
    glass: {
      name: "glass",
      density: 0.0009, friction: 0.06, frictionStatic: 0.2, restitution: 0.02,
      hp: 16, threshold: 2.0,
      fill: [154, 216, 232], edge: [206, 244, 255], shard: [188, 232, 246],
      alpha: 0.72
    },
    wood: {
      name: "wood",
      density: 0.0021, friction: 0.62, frictionStatic: 0.9, restitution: 0.05,
      hp: 52, threshold: 3.4,
      fill: [173, 118, 62], edge: [206, 152, 92], shard: [150, 100, 52],
      alpha: 1
    },
    stone: {
      name: "stone",
      density: 0.0058, friction: 0.86, frictionStatic: 1.1, restitution: 0.02,
      hp: 168, threshold: 5.6,
      fill: [131, 137, 149], edge: [168, 175, 188], shard: [110, 116, 128],
      alpha: 1
    }
  };

  /* Birds. Each carries one in-flight ability, spent with a single tap. */
  var BIRDS = [
    {
      id: "dart", name: "Dart", ability: "dash",
      radius: 17, density: 0.0034, restitution: 0.30, frictionAir: 0.004,
      body: 0xE2455F, belly: 0xFFD2D8, brow: 0x8E1E30,
      blurb: "Tap in flight for a hard horizontal kick."
    },
    {
      id: "boulder", name: "Boulder", ability: "slam",
      radius: 22, density: 0.0092, restitution: 0.06, frictionAir: 0.006,
      body: 0x6E7686, belly: 0xC3CAD6, brow: 0x3D4450,
      blurb: "Heavy. Tap to slam straight down through a roof."
    },
    {
      id: "splitter", name: "Splitter", ability: "split",
      radius: 15, density: 0.0030, restitution: 0.24, frictionAir: 0.004,
      body: 0xE9C33F, belly: 0xFFF0B4, brow: 0x8A6E12,
      blurb: "Tap to fan into three. Good against wide roofs."
    }
  ];

  function create(host) {
    var save = host.save;
    var M = window.Matter;
    var S = null;
    var ui = {};
    var layout = null;

    /* ----------------------------------------------------------- helpers -- */
    function mat(name) { return MATERIALS[name] || MATERIALS.wood; }

    /** Per-step displacement is Matter's velocity unit; this is world px/sec. */
    function speedOf(body) {
      return Math.hypot(body.velocity.x, body.velocity.y);
    }

    /* -------------------------------------------------------- generation -- */
    /**
     * Builds one fort. Levels cycle through three silhouettes and get taller,
     * denser and stonier as they climb, so the same read - "where is the load
     * bearing piece" - keeps paying off with a harder answer.
     */
    function buildLevel(level) {
      var rand = A.rng(0xA71A5 ^ (level * 6151));
      var bodies = [];
      var enemies = [];

      var tier = Math.min(3, 1 + Math.floor((level - 1) / 3));
      var shape = (level - 1) % 3;
      /* Close enough that a portrait phone can hold the slingshot and the fort
         in one frame without shrinking the fort to a smudge. */
      var baseX = 790 + Math.min(190, (level - 1) * 18);

      function block(x, y, w, h, name) {
        var m = mat(name);
        var b = M.Bodies.rectangle(x, y, w, h, {
          density: m.density,
          friction: m.friction,
          frictionStatic: m.frictionStatic,
          restitution: m.restitution,
          /* Slightly rounded corners stop stacks from catching on each other's
             exact vertices, which is what starts a stack rocking. */
          chamfer: { radius: Math.min(3, Math.min(w, h) * 0.12) },
          label: "block"
        });
        b.game = { kind: "block", mat: m, hp: m.hp, maxHp: m.hp, w: w, h: h, flash: 0 };
        bodies.push(b);
        return b;
      }

      function enemy(x, y, r) {
        var e = M.Bodies.circle(x, y, r, {
          density: 0.0016, friction: 0.5, frictionStatic: 0.7, restitution: 0.20,
          label: "enemy"
        });
        e.game = { kind: "enemy", r: r, hp: 16, maxHp: 16, flash: 0, blink: rand.range(0, 6) };
        bodies.push(e);
        enemies.push(e);
        return e;
      }

      /* Pick the material palette for this tier. Early forts are wood and glass
         so the first collapses are legible; stone arrives once the player has
         learned what a load-bearing column looks like. */
      function pick(preferStone) {
        if (tier >= 3 && preferStone) return rand.chance(0.62) ? "stone" : "wood";
        if (tier >= 2 && preferStone) return rand.chance(0.34) ? "stone" : "wood";
        return rand.chance(0.5) ? "wood" : "glass";
      }

      /*
       * Everything is stacked from a running surface height rather than from
       * absolute coordinates. The first pass placed bodies by eye, which left
       * them floating a few dozen pixels up - they then fell on spawn, and the
       * landing impact alone was enough to kill a target before the player had
       * fired a single bird.
       */
      function column(x, surface, w, h, name) {
        block(x, surface - h / 2, w, h, name);
        return surface - h;
      }

      function slab(x, surface, w, h, name) {
        block(x, surface - h / 2, w, h, name);
        return surface - h;
      }

      function sit(x, surface, r) {
        enemy(x, surface - r, r);
        return surface - r * 2;
      }

      var floors = 1 + tier;
      var i, f;

      if (shape === 0) {
        /* Tower: paired columns under a lintel, a target on alternating decks. */
        var span = 132;
        var surface = GROUND_Y;
        for (f = 0; f < floors; f++) {
          var colH = 92;
          column(baseX - span / 2, surface, 22, colH, pick(f === 0));
          column(baseX + span / 2, surface, 22, colH, pick(f === 0));
          if (f % 2 === 0) sit(baseX, surface, 18);
          surface = slab(baseX, surface - colH, span + 54, 20, pick(false));
        }
        sit(baseX, surface, 16);
      } else if (shape === 1) {
        /* Bunker: a stone shell over a glass core - the greedy shot bounces. */
        var deck = slab(baseX, GROUND_Y, 262, 22, pick(true));
        for (i = -1; i <= 1; i++) {
          column(baseX + i * 98, deck, 20, 126, pick(true));
        }
        sit(baseX - 49, deck, 18);
        sit(baseX + 49, deck, 18);

        var roof = slab(baseX, deck - 126, 262, 24, pick(true));
        column(baseX - 48, roof, 18, 62, "glass");
        column(baseX + 48, roof, 18, 62, "glass");
        var cap = slab(baseX, roof - 62, 156, 18, "wood");
        sit(baseX, cap, 16);
      } else {
        /* Stepped terrace: three shelves, each one a separate problem. */
        for (i = 0; i < 3; i++) {
          var sx = baseX - 136 + i * 136;
          var height = 2 + i;
          var top = GROUND_Y;
          for (f = 0; f < height; f++) {
            column(sx - 44, top, 18, 60, pick(f === 0));
            column(sx + 44, top, 18, 60, pick(f === 0));
            top = slab(sx, top - 60, 122, 16, pick(false));
          }
          sit(sx, top, 16);
        }
      }

      return { bodies: bodies, enemies: enemies };
    }

    /* --------------------------------------------------------- world ----- */
    function makeWorld() {
      var engine = M.Engine.create({
        /* Sleeping is what keeps a settled fort from vibrating for the rest of
           the level. Matter parks a body once it has been near-motionless for
           long enough, and any impact wakes its neighbours again. */
        enableSleeping: true
      });
      engine.gravity.y = GRAVITY_Y;
      engine.gravity.scale = GRAVITY_SCALE;
      /* More solver work per step than the default. A stack of stone under a
         direct hit is exactly the case where too few iterations shows up as
         blocks sinking into each other. */
      engine.positionIterations = 10;
      engine.velocityIterations = 8;

      var ground = M.Bodies.rectangle(WORLD_W / 2, GROUND_Y + 60, WORLD_W + 800, 120, {
        isStatic: true, friction: 0.9, frictionStatic: 1.0, restitution: 0.05,
        label: "ground"
      });
      ground.game = { kind: "ground" };

      var leftWall = M.Bodies.rectangle(-120, GROUND_Y - 400, 60, 1400, { isStatic: true });
      leftWall.game = { kind: "wall" };

      M.Composite.add(engine.world, [ground, leftWall]);
      return { engine: engine, ground: ground };
    }

    /* ------------------------------------------------------------ begin -- */
    function begin() {
      teardownWorld();

      var world = makeWorld();
      var level = buildLevel(save.level);
      M.Composite.add(world.engine.world, level.bodies);

      S = {
        engine: world.engine,
        ground: world.ground,
        blocks: level.bodies,
        enemies: level.enemies,
        birdsLeft: 3,
        birdIndex: 0,
        bird: null,
        state: "aim",              // aim | flying | settling | done
        pulling: false,
        pouch: { x: ANCHOR.x, y: ANCHOR.y },
        grabOffset: { x: 0, y: 0 },
        preview: [],
        abilityUsed: false,
        settleTimer: 0,
        /* Damage stays off until the fort has settled once. Even perfectly
           stacked bodies shuffle a pixel or two as the solver resolves the
           initial overlap, and an armed world would score that as an impact. */
        armed: false,
        armTimer: 0,
        camX: ANCHOR.x + 300, camY: GROUND_Y - 220, camZoom: 1,
        score: 0,
        popped: 0,
        shots: 0,
        debris: [],
        over: false,
        won: false
      };

      installCollisions();
      computeLayout();
      S.camZoom = layout.baseZoom;
      S.camY = groundFramedY(S.camZoom);
      loadBird();
      paintHud();
      host.toast("LEVEL " + save.level + "  ·  clear every target");
    }

    function teardownWorld() {
      if (!S || !S.engine) return;
      /* Explicit teardown between levels, so a restart never leaves the old
         world's bodies and events attached to a new one. */
      M.Events.off(S.engine);
      M.Composite.clear(S.engine.world, false, true);
      M.Engine.clear(S.engine);
      S.engine = null;
    }

    /* ------------------------------------------------------- collisions -- */
    /**
     * Damage from an impact. Using relative speed alone would let a feather
     * touching a wall at speed do as much as a boulder; scaling by the square
     * root of the other body's mass keeps the ordering intuitive without
     * letting a stone block one-shot everything it brushes.
     */
    function impactStrength(a, b) {
      var rvx = a.velocity.x - b.velocity.x;
      var rvy = a.velocity.y - b.velocity.y;
      var rel = Math.hypot(rvx, rvy);
      var other = b.isStatic ? 4 : b.mass;
      return rel * Math.sqrt(Math.max(0.05, other));
    }

    function damage(body, amount, atX, atY) {
      if (!S.armed) return;
      var g = body.game;
      if (!g || g.dead) return;
      g.hp -= amount;
      g.flash = 1;

      if (g.hp > 0) {
        if (amount > 4) {
          A.Fx.burst(atX, atY, 0, 3,
            { color: g.kind === "enemy" ? [126, 214, 122] : g.mat.shard, speed: 90, life: 0.3 });
        }
        return;
      }

      g.dead = true;
      var colour = g.kind === "enemy" ? [126, 214, 122] : g.mat.shard;
      A.Fx.burst(body.position.x, body.position.y, 0, g.kind === "enemy" ? 18 : 11,
        { color: colour, speed: g.kind === "enemy" ? 210 : 150, life: 0.55 });

      if (g.kind === "enemy") {
        S.popped++;
        S.score += 1200;
        A.Audio.sfx("pop");
        A.Fx.kick(5);
        A.Fx.text(body.position.x, body.position.y, 0, "+1200", [255, 214, 84],
          { life: 0.8, rise: 60 });
      } else {
        S.score += Math.round(g.maxHp * 2.4);
        A.Audio.sfx(g.mat.name === "glass" ? "pop" : "thud");
      }

      /* Debris shards live only in the renderer - spawning real bodies for
         every fragment is what turns a satisfying collapse into a slideshow. */
      spawnDebris(body, colour);
      M.Composite.remove(S.engine.world, body);
    }

    function spawnDebris(body, colour) {
      var g = body.game;
      var count = g.kind === "enemy" ? 7 : 5;
      for (var i = 0; i < count; i++) {
        S.debris.push({
          x: body.position.x, y: body.position.y,
          vx: body.velocity.x * 0.6 + (Math.random() - 0.5) * 7,
          vy: body.velocity.y * 0.6 - Math.random() * 5,
          spin: (Math.random() - 0.5) * 0.4,
          angle: Math.random() * A.TAU,
          size: (g.kind === "enemy" ? g.r : Math.min(g.w, g.h)) * A.lerp(0.18, 0.42, Math.random()),
          life: 1,
          colour: colour
        });
      }
    }

    function installCollisions() {
      M.Events.on(S.engine, "collisionStart", function (evt) {
        for (var i = 0; i < evt.pairs.length; i++) {
          var pair = evt.pairs[i];
          resolveHit(pair.bodyA, pair.bodyB, pair);
          resolveHit(pair.bodyB, pair.bodyA, pair);
        }
      });
    }

    function resolveHit(victim, striker, pair) {
      var g = victim.game;
      if (!g || g.dead) return;
      if (g.kind !== "block" && g.kind !== "enemy") return;

      var strength = impactStrength(victim, striker);
      var threshold = g.kind === "enemy" ? 2.8 : g.mat.threshold;
      if (strength < threshold) return;

      /* A bird strikes far harder than the same impact between two blocks -
         otherwise the fun shot, the direct hit, is the weakest one available. */
      var scale = (striker.game && striker.game.kind === "bird") ? 3.2 : 1.25;
      var contact = pair.collision && pair.collision.supports && pair.collision.supports[0];
      damage(victim, (strength - threshold) * scale,
        contact ? contact.x : victim.position.x,
        contact ? contact.y : victim.position.y);
    }

    /* ------------------------------------------------------------ birds -- */
    function currentBirdSpec() { return BIRDS[S.birdIndex % BIRDS.length]; }

    function loadBird() {
      if (S.birdsLeft <= 0) { finish(false); return; }
      var spec = currentBirdSpec();
      S.bird = null;
      S.pouch.x = ANCHOR.x;
      S.pouch.y = ANCHOR.y;
      S.abilityUsed = false;
      S.state = "aim";
      S.preview = [];
      S.spec = spec;
      paintHud();
    }

    function launch(vx, vy) {
      var spec = S.spec;
      var bird = M.Bodies.circle(S.pouch.x, S.pouch.y, spec.radius, {
        density: spec.density,
        restitution: spec.restitution,
        friction: 0.55,
        frictionAir: spec.frictionAir,
        label: "bird"
      });
      bird.game = { kind: "bird", spec: spec, r: spec.radius, flash: 0, born: A.Loop.time };

      M.Composite.add(S.engine.world, bird);
      /* One deterministic impulse, so the flight matches the preview exactly. */
      M.Body.setVelocity(bird, { x: vx, y: vy });
      M.Body.setAngularVelocity(bird, vx * 0.02);

      S.bird = bird;
      S.state = "flying";
      S.shots++;
      S.birdsLeft--;
      A.Audio.sfx("boom");
      A.Fx.kick(4);
      A.vibrate(24);
      paintHud();
    }

    function fireAbility() {
      if (S.state !== "flying" || !S.bird || S.abilityUsed) return;
      S.abilityUsed = true;
      var bird = S.bird;
      var v = bird.velocity;
      var spec = S.spec;

      if (spec.ability === "dash") {
        M.Body.setVelocity(bird, { x: (v.x >= 0 ? 1 : -1) * (Math.abs(v.x) * 1.85 + 4), y: v.y * 0.35 });
      } else if (spec.ability === "slam") {
        M.Body.setVelocity(bird, { x: v.x * 0.35, y: Math.abs(v.y) + 13 });
      } else if (spec.ability === "split") {
        for (var i = -1; i <= 1; i += 2) {
          var shard = M.Bodies.circle(bird.position.x, bird.position.y, spec.radius * 0.66, {
            density: spec.density * 0.8, restitution: spec.restitution,
            friction: 0.5, frictionAir: spec.frictionAir, label: "bird"
          });
          shard.game = { kind: "bird", spec: spec, r: spec.radius * 0.66, flash: 0, shard: true };
          M.Composite.add(S.engine.world, shard);
          M.Body.setVelocity(shard, { x: v.x, y: v.y + i * 3.1 });
        }
      }

      A.Fx.burst(bird.position.x, bird.position.y, 0, 12,
        { color: A.hex(spec.body), speed: 200, life: 0.4 });
      A.Fx.text(bird.position.x, bird.position.y - 26, 0, spec.ability.toUpperCase(),
        [93, 224, 255], { life: 0.6, rise: 50 });
      A.Audio.sfx("horn");
      paintHud();
    }

    /* ------------------------------------------------------- prediction -- */
    /**
     * Forward Euler over Matter's own integration, so the dashed arc is the
     * flight rather than an approximation of it. Matter stores velocity as a
     * per-step displacement and adds gravity as force/mass * dt^2 each step,
     * which reduces to a constant increment per step.
     */
    function predict(vx, vy, steps) {
      var pts = [];
      var x = S.pouch.x, y = S.pouch.y;
      var gy = GRAVITY_Y * GRAVITY_SCALE * STEP_MS * STEP_MS;
      var drag = 1 - S.spec.frictionAir;

      for (var i = 0; i < steps; i++) {
        vy += gy;
        vx *= drag;
        vy *= drag;
        x += vx;
        y += vy;
        if (y > GROUND_Y + 20) { pts.push({ x: x, y: GROUND_Y }); break; }
        if (i % 3 === 0) pts.push({ x: x, y: y });
      }
      return pts;
    }

    function pullVector() {
      return { x: ANCHOR.x - S.pouch.x, y: ANCHOR.y - S.pouch.y };
    }

    function refreshPreview() {
      var pull = pullVector();
      /* Two seconds of flight at sixty steps a second, as specified. */
      S.preview = predict(pull.x * LAUNCH_POWER, pull.y * LAUNCH_POWER, 120);
    }

    /* ------------------------------------------------------------ input -- */
    function screenToWorld(sx, sy) {
      return {
        x: S.camX + (sx - layout.w / 2) / S.camZoom,
        y: S.camY + (sy - layout.h / 2) / S.camZoom
      };
    }

    function handleInput() {
      if (S.over) return;

      if (S.state === "aim") {
        if (!S.armed) return;
        if (A.Input.pressed) {
          var w = screenToWorld(A.Input.x, A.Input.y);
          /*
           * Deliberately forgiving. On a phone the pouch is smaller than a
           * thumb, so a press anywhere near the sling - or anywhere in the left
           * half of the screen, which is where a thumb naturally lands to aim -
           * takes hold of it. Demanding a pixel-accurate press on the leather
           * is pure friction and buys nothing.
           */
          var grab = MAX_PULL * 2.1;
          var nearSling = A.dist2(w.x, w.y, ANCHOR.x, ANCHOR.y) < grab * grab;
          var aimingSide = A.Input.x < layout.w * 0.62;
          if (nearSling || aimingSide) {
            S.pulling = true;
            /*
             * Drag is relative, not absolute. Because the grab region is much
             * larger than the pouch, snapping the pouch to the press point
             * would teleport it - and with it the aim - the instant a thumb
             * landed anywhere on the left of the screen. Holding the offset
             * means the pouch moves exactly as far as the thumb does.
             */
            S.grabOffset = { x: S.pouch.x - w.x, y: S.pouch.y - w.y };
            A.Audio.sfx("select");
          }
        }

        if (S.pulling && A.Input.down) {
          var p = screenToWorld(A.Input.x, A.Input.y);
          var dx = p.x + S.grabOffset.x - ANCHOR.x;
          var dy = p.y + S.grabOffset.y - ANCHOR.y;
          var len = Math.hypot(dx, dy);
          if (len > MAX_PULL) {
            /* Past the limit the pouch stays on the rim, along the same angle,
               so the tension caps without the aim jumping. */
            dx = dx / len * MAX_PULL;
            dy = dy / len * MAX_PULL;
          }
          S.pouch.x = ANCHOR.x + dx;
          S.pouch.y = ANCHOR.y + dy;
          refreshPreview();
        }

        if (S.pulling && A.Input.released) {
          S.pulling = false;
          var pull = pullVector();
          if (Math.hypot(pull.x, pull.y) < 16) {
            S.pouch.x = ANCHOR.x;
            S.pouch.y = ANCHOR.y;
            S.preview = [];
            return;
          }
          launch(pull.x * LAUNCH_POWER, pull.y * LAUNCH_POWER);
        }
        return;
      }

      if (S.state === "flying" && A.Input.pressed && !S.abilityUsed) fireAbility();
    }

    /* ------------------------------------------------------------- step -- */
    function update(dt) {
      A.Fx.update(dt);
      if (!S.engine) return;

      handleInput();

      /* Fixed-step physics. Matter is far happier with a constant delta than
         with whatever the display handed us, and a variable step turns a
         settled stack back into a jittering one. */
      var steps = A.clamp(Math.round(dt / (STEP_MS / 1000)), 1, 3);
      for (var i = 0; i < steps; i++) M.Engine.update(S.engine, STEP_MS);

      if (!S.armed) {
        S.armTimer += dt;
        /* Armed as soon as the world stops moving, with a hard ceiling so a
           permanently restless fort cannot leave the level unplayable. */
        if ((S.armTimer > 0.35 && worldAtRest()) || S.armTimer > 2.5) S.armed = true;
      }

      stepDebris(dt);
      decayFlashes(dt);
      updateCamera(dt);

      if (S.state === "flying") {
        var bird = S.bird;
        var lost = !bird || bird.position.y > GROUND_Y + 260 ||
          bird.position.x > WORLD_W + 200 || bird.position.x < -260;
        var stopped = bird && speedOf(bird) < 0.55;

        if (lost) {
          S.state = "settling";
          S.settleTimer = 0;
        } else if (stopped) {
          S.settleTimer += dt;
          if (S.settleTimer > 0.7) { S.state = "settling"; S.settleTimer = 0; }
        } else {
          S.settleTimer = 0;
        }
      } else if (S.state === "settling") {
        S.settleTimer += dt;
        /* Wait for the world to stop moving before handing the next bird over,
           otherwise a collapse still in progress gets cut short. */
        if (S.settleTimer > 0.45 && worldAtRest()) advance();
        else if (S.settleTimer > 4.5) advance();
      }

      if (!S.over && S.enemies.length && allTargetsDown()) finish(true);
      paintHud();
    }

    function allTargetsDown() {
      for (var i = 0; i < S.enemies.length; i++) {
        if (!S.enemies[i].game.dead) return false;
      }
      return true;
    }

    function worldAtRest() {
      var bodies = M.Composite.allBodies(S.engine.world);
      for (var i = 0; i < bodies.length; i++) {
        var b = bodies[i];
        if (b.isStatic || b.isSleeping) continue;
        if (speedOf(b) > 0.42 || Math.abs(b.angularVelocity) > 0.045) return false;
      }
      return true;
    }

    function advance() {
      if (S.over) return;
      if (allTargetsDown()) { finish(true); return; }

      /* Retire the spent bird and any split shards. */
      var bodies = M.Composite.allBodies(S.engine.world);
      for (var i = 0; i < bodies.length; i++) {
        if (bodies[i].game && bodies[i].game.kind === "bird") {
          M.Composite.remove(S.engine.world, bodies[i]);
        }
      }
      S.bird = null;
      S.birdIndex++;

      if (S.birdsLeft <= 0) { finish(false); return; }
      loadBird();
    }

    function stepDebris(dt) {
      for (var i = S.debris.length - 1; i >= 0; i--) {
        var d = S.debris[i];
        d.vy += 34 * dt;
        d.x += d.vx * dt * 60;
        d.y += d.vy * dt * 60;
        d.angle += d.spin;
        if (d.y > GROUND_Y - 2) {
          d.y = GROUND_Y - 2;
          d.vy *= -0.24;
          d.vx *= 0.7;
        }
        d.life -= dt * 0.62;
        if (d.life <= 0) S.debris.splice(i, 1);
      }
    }

    function decayFlashes(dt) {
      var bodies = M.Composite.allBodies(S.engine.world);
      for (var i = 0; i < bodies.length; i++) {
        var g = bodies[i].game;
        if (g && g.flash > 0) g.flash = Math.max(0, g.flash - dt * 4.5);
      }
    }

    /* ----------------------------------------------------------- camera -- */
    function updateCamera(dt) {
      var wantX, wantY, wantZoom;

      if (S.state === "flying" && S.bird) {
        /* Frame the slingshot and the bird together, so the shot stays legible
           as a whole rather than becoming a close-up of one bird. */
        var minX = Math.min(ANCHOR.x, S.bird.position.x) - 190;
        var maxX = Math.max(ANCHOR.x, S.bird.position.x) + 260;
        var minY = Math.min(GROUND_Y, S.bird.position.y) - 240;
        var maxY = GROUND_Y + 90;

        wantX = (minX + maxX) / 2;
        wantZoom = Math.min(
          layout.w / Math.max(420, maxX - minX),
          layout.h / Math.max(420, maxY - minY)
        );
        /* Bias toward the ground anchor, then lift as the bird climbs so a high
           arc still has headroom. */
        var anchored = GROUND_Y - (0.28 * layout.h) / wantZoom;
        var lift = Math.min(0, (minY + maxY) / 2 - anchored);
        wantY = anchored + lift * 0.75;
      } else if (S.state === "aim") {
        wantX = ANCHOR.x + 300;
        wantZoom = layout.baseZoom;
        wantY = groundFramedY(wantZoom);
      } else {
        wantX = 780;
        wantZoom = layout.baseZoom * 0.94;
        wantY = groundFramedY(wantZoom);
      }

      wantZoom = A.clamp(wantZoom, layout.baseZoom * 0.46, layout.baseZoom * 1.22);

      var rate = S.state === "flying" ? 5.2 : 3.0;
      S.camX = A.approach(S.camX, wantX, rate, dt);
      S.camY = A.approach(S.camY, wantY, rate, dt);
      S.camZoom = A.approach(S.camZoom, wantZoom, rate * 0.8, dt);
    }

    function computeLayout() {
      var w = A.View.w, h = A.View.h;
      /*
       * Fit, not fill. The first pass took the larger of the two ratios, which
       * zooms in until the narrow axis is satisfied - on a portrait phone that
       * pushed the slingshot clean off the left edge. Taking the smaller ratio
       * guarantees the slingshot and the fort are both inside the frame.
       */
      layout = {
        w: w, h: h,
        baseZoom: Math.min(w / 780, h / 620)
      };
      if (S) S.camZoom = layout.baseZoom;
    }

    /**
     * Camera Y that puts the ground line at 78% of the screen height, so the
     * playfield sits on the lower third and the arc has sky to travel through.
     * Centring on the action instead leaves half the screen as bare grass.
     */
    function groundFramedY(zoom) {
      return GROUND_Y - (0.28 * layout.h) / zoom;
    }

    /* ---------------------------------------------------------- results -- */
    function finish(won) {
      if (S.over) return;
      S.over = true;
      S.won = won;
      S.state = "done";

      var stars = 0;
      if (won) {
        S.score += S.birdsLeft * 2400;
        stars = S.birdsLeft >= 2 ? 3 : (S.birdsLeft === 1 ? 2 : 1);
      }

      var coins = Math.round((S.score * 0.02 + (won ? 60 : 12)) * host.coinMultiplier());
      save.coins += coins;
      save.runs++;
      if (won) {
        save.level++;
        save.stars += stars;
        A.Audio.sfx("win");
      } else {
        A.Audio.sfx("lose");
      }
      host.commit();

      var xp = Math.round((won ? 34 : 12) * host.xpMultiplier());
      host.addXp(xp);
      host.progress("run", 1);
      host.progress("coins", coins);
      if (won) host.progress("win", 1);

      host.results({
        title: won ? "FORT DOWN" : "OUT OF BIRDS",
        subtitle: won
          ? (stars === 3 ? "Three stars" : stars + " star" + (stars === 1 ? "" : "s"))
          : (S.enemies.length - S.popped) + " target" +
            ((S.enemies.length - S.popped) === 1 ? "" : "s") + " still standing",
        win: won,
        score: S.score,
        rows: [
          { label: "Score", value: A.formatNumber(S.score) },
          { label: "Targets", value: S.popped + " / " + S.enemies.length },
          { label: "Birds left", value: S.birdsLeft },
          { label: "Coins", value: "+" + A.formatNumber(coins) },
          { label: "XP", value: "+" + xp }
        ],
        actions: [
          { label: won ? "NEXT LEVEL" : "RETRY", className: "go",
            onClick: function () { host.modal.hide(); begin(); } },
          { label: "MENU", className: "ghost", onClick: host.exit }
        ]
      });
    }

    /* ---------------------------------------------------------- drawing -- */
    function toScreen(x, y) {
      return {
        x: layout.w / 2 + (x - S.camX) * S.camZoom,
        y: layout.h / 2 + (y - S.camY) * S.camZoom
      };
    }

    function drawBackdrop(g) {
      var vw = layout.w, vh = layout.h;
      var sky = g.createLinearGradient(0, 0, 0, vh);
      sky.addColorStop(0, "#67B7E8");
      sky.addColorStop(0.55, "#A8D8EE");
      sky.addColorStop(1, "#DCEBD6");
      g.fillStyle = sky;
      g.fillRect(0, 0, vw, vh);

      /* Two parallax hill bands. They move at a fraction of the camera, which
         is the cheapest possible sense of a world larger than the fort. */
      for (var band = 0; band < 2; band++) {
        var depth = band === 0 ? 0.18 : 0.34;
        var baseline = toScreen(0, GROUND_Y - (band === 0 ? 130 : 66)).y;
        g.fillStyle = band === 0 ? "rgba(122,168,138,0.55)" : "rgba(96,146,112,0.75)";
        g.beginPath();
        g.moveTo(-40, vh);
        var offset = -S.camX * depth * S.camZoom;
        for (var x = -40; x <= vw + 40; x += 22) {
          var t = (x - offset) * 0.0042 + band * 3.1;
          var y = baseline - (Math.sin(t) * 34 + Math.sin(t * 2.3) * 17);
          g.lineTo(x, y);
        }
        g.lineTo(vw + 40, vh);
        g.closePath();
        g.fill();
      }

      var gy = toScreen(0, GROUND_Y).y;
      g.fillStyle = "#5B9A55";
      g.fillRect(0, gy, vw, vh - gy);
      g.fillStyle = "#79B762";
      g.fillRect(0, gy, vw, Math.max(3, 9 * S.camZoom));
      g.fillStyle = "rgba(60,90,54,0.30)";
      g.fillRect(0, gy + Math.max(3, 9 * S.camZoom), vw, Math.max(2, 4 * S.camZoom));
    }

    function drawBlock(g, body) {
      var gm = body.game;
      var m = gm.mat;
      var v = body.vertices;
      var s0 = toScreen(v[0].x, v[0].y);

      g.beginPath();
      g.moveTo(s0.x, s0.y);
      for (var i = 1; i < v.length; i++) {
        var p = toScreen(v[i].x, v[i].y);
        g.lineTo(p.x, p.y);
      }
      g.closePath();

      var hurt = 1 - A.clamp01(gm.hp / gm.maxHp);
      g.globalAlpha = m.alpha;
      g.fillStyle = A.rgb(A.mix(m.fill, [70, 52, 44], hurt * 0.42));
      g.fill();
      g.globalAlpha = 1;

      g.strokeStyle = A.rgb(m.edge);
      g.lineWidth = Math.max(1, 2 * S.camZoom);
      g.stroke();

      /* Cracks scale with accumulated damage, so a block about to go is
         readable before it goes. */
      if (hurt > 0.22) {
        var c = toScreen(body.position.x, body.position.y);
        var reach = Math.min(gm.w, gm.h) * 0.42 * S.camZoom;
        g.strokeStyle = "rgba(28,20,16," + (hurt * 0.65).toFixed(3) + ")";
        g.lineWidth = Math.max(1, 1.6 * S.camZoom);
        g.beginPath();
        for (var k = 0; k < 3; k++) {
          var a = body.angle + k * 2.1;
          g.moveTo(c.x, c.y);
          g.lineTo(c.x + Math.cos(a) * reach * hurt, c.y + Math.sin(a) * reach * hurt);
        }
        g.stroke();
      }

      if (gm.flash > 0) {
        g.globalAlpha = gm.flash * 0.75;
        g.fillStyle = "#FFFFFF";
        g.fill();
        g.globalAlpha = 1;
      }
    }

    function drawEnemy(g, body) {
      var gm = body.game;
      var c = toScreen(body.position.x, body.position.y);
      var r = gm.r * S.camZoom;
      var hurt = 1 - A.clamp01(gm.hp / gm.maxHp);

      g.fillStyle = "rgba(24,44,24,0.22)";
      g.beginPath();
      g.ellipse(c.x, c.y + r * 0.9, r * 0.9, r * 0.32, 0, 0, A.TAU);
      g.fill();

      var grad = g.createRadialGradient(c.x - r * 0.35, c.y - r * 0.4, r * 0.15, c.x, c.y, r * 1.1);
      grad.addColorStop(0, "#A9E88C");
      grad.addColorStop(0.65, A.rgb(A.mix([106, 190, 96], [176, 96, 72], hurt)));
      grad.addColorStop(1, "#3F7A38");
      g.fillStyle = grad;
      g.beginPath();
      g.arc(c.x, c.y, r, 0, A.TAU);
      g.fill();
      g.strokeStyle = "rgba(28,58,26,0.7)";
      g.lineWidth = Math.max(1, 2 * S.camZoom);
      g.stroke();

      /* Eyes and a brow. Blink on a per-target offset so a row of them is not
         one organism. */
      var blink = Math.sin(A.Loop.time * 2.2 + gm.blink) > 0.94;
      var er = r * 0.24;
      for (var e = -1; e <= 1; e += 2) {
        var ex = c.x + e * r * 0.34, ey = c.y - r * 0.18;
        g.fillStyle = "#FFFFFF";
        g.beginPath();
        g.arc(ex, ey, er, 0, A.TAU);
        g.fill();
        if (!blink) {
          g.fillStyle = "#1A1428";
          g.beginPath();
          g.arc(ex + e * er * 0.22, ey, er * 0.48, 0, A.TAU);
          g.fill();
        }
      }
      g.strokeStyle = "#2C5C28";
      g.lineWidth = Math.max(1, 2.2 * S.camZoom);
      g.beginPath();
      g.moveTo(c.x - r * 0.58, c.y - r * 0.52);
      g.lineTo(c.x - r * 0.14, c.y - r * 0.36);
      g.moveTo(c.x + r * 0.58, c.y - r * 0.52);
      g.lineTo(c.x + r * 0.14, c.y - r * 0.36);
      g.stroke();

      if (gm.flash > 0) {
        g.globalAlpha = gm.flash * 0.7;
        g.fillStyle = "#FFFFFF";
        g.beginPath();
        g.arc(c.x, c.y, r, 0, A.TAU);
        g.fill();
        g.globalAlpha = 1;
      }
    }

    function drawBird(g, x, y, angle, radius, spec) {
      var c = toScreen(x, y);
      var r = radius * S.camZoom;

      g.save();
      g.translate(c.x, c.y);
      g.rotate(angle);

      g.fillStyle = A.rgb(A.hex(spec.body));
      g.beginPath();
      g.arc(0, 0, r, 0, A.TAU);
      g.fill();

      g.fillStyle = A.rgb(A.hex(spec.belly));
      g.beginPath();
      g.ellipse(0, r * 0.26, r * 0.62, r * 0.52, 0, 0, A.TAU);
      g.fill();

      g.fillStyle = "#F2A93B";
      g.beginPath();
      g.moveTo(r * 0.82, -r * 0.06);
      g.lineTo(r * 1.42, r * 0.14);
      g.lineTo(r * 0.80, r * 0.34);
      g.closePath();
      g.fill();

      g.fillStyle = "#FFFFFF";
      g.beginPath();
      g.arc(r * 0.34, -r * 0.30, r * 0.30, 0, A.TAU);
      g.fill();
      g.fillStyle = "#1A1428";
      g.beginPath();
      g.arc(r * 0.42, -r * 0.30, r * 0.14, 0, A.TAU);
      g.fill();

      g.strokeStyle = A.rgb(A.hex(spec.brow));
      g.lineWidth = Math.max(1.4, r * 0.16);
      g.lineCap = "round";
      g.beginPath();
      g.moveTo(r * 0.06, -r * 0.62);
      g.lineTo(r * 0.60, -r * 0.44);
      g.stroke();
      g.lineCap = "butt";

      g.restore();
    }

    function drawSlingshot(g, front) {
      var scale = S.camZoom;
      var forkBack = toScreen(ANCHOR.x - 26, ANCHOR.y - 16);
      var forkFront = toScreen(ANCHOR.x + 26, ANCHOR.y - 16);
      var pouch = toScreen(S.pouch.x, S.pouch.y);
      var stemTop = toScreen(ANCHOR.x, ANCHOR.y + 14);
      var stemBase = toScreen(ANCHOR.x, GROUND_Y);

      if (!front) {
        /* Y-frame behind the pouch: stem, then the two arms. */
        g.strokeStyle = "#6B4A2C";
        g.lineWidth = Math.max(3, 19 * scale);
        g.lineCap = "round";
        g.beginPath();
        g.moveTo(stemBase.x, stemBase.y);
        g.lineTo(stemTop.x, stemTop.y);
        g.stroke();

        g.lineWidth = Math.max(2.5, 14 * scale);
        g.beginPath();
        g.moveTo(stemTop.x, stemTop.y);
        g.lineTo(forkBack.x, forkBack.y);
        g.moveTo(stemTop.x, stemTop.y);
        g.lineTo(forkFront.x, forkFront.y);
        g.stroke();

        /* Back band, drawn before the pouch so the pouch sits on top of it. */
        band(g, forkBack, pouch, scale);
        g.lineCap = "butt";
        return;
      }

      /* Front band and the leather, over the top of the projectile. */
      band(g, forkFront, pouch, scale);

      var stretch = Math.hypot(S.pouch.x - ANCHOR.x, S.pouch.y - ANCHOR.y);
      if (stretch > 3 || S.state === "aim") {
        var angle = Math.atan2(ANCHOR.y - S.pouch.y, ANCHOR.x - S.pouch.x);
        g.save();
        g.translate(pouch.x, pouch.y);
        g.rotate(angle + Math.PI / 2);
        g.fillStyle = "#4A2F1B";
        A.roundRect(g, -5 * scale, -13 * scale, 10 * scale, 26 * scale, 4 * scale);
        g.fill();
        g.restore();
      }
    }

    /** One rubber band: a quadratic curve that sags less the harder it is pulled. */
    function band(g, fork, pouch, scale) {
      var slack = A.clamp01(1 - Math.hypot(S.pouch.x - ANCHOR.x, S.pouch.y - ANCHOR.y) / MAX_PULL);
      var midX = (fork.x + pouch.x) / 2;
      var midY = (fork.y + pouch.y) / 2 + slack * 16 * scale;

      g.strokeStyle = "#2F1E12";
      g.lineWidth = Math.max(2, (6 - slack * 1.6) * scale);
      g.lineCap = "round";
      g.beginPath();
      g.moveTo(fork.x, fork.y);
      g.quadraticCurveTo(midX, midY, pouch.x, pouch.y);
      g.stroke();
      g.lineCap = "butt";
    }

    function drawPreview(g) {
      if (!S.preview.length) return;
      g.save();
      g.setLineDash([Math.max(3, 5 * S.camZoom), Math.max(5, 9 * S.camZoom)]);
      g.strokeStyle = "rgba(255,255,255,0.78)";
      g.lineWidth = Math.max(1.5, 3 * S.camZoom);
      g.beginPath();
      var first = toScreen(S.preview[0].x, S.preview[0].y);
      g.moveTo(first.x, first.y);
      for (var i = 1; i < S.preview.length; i++) {
        var p = toScreen(S.preview[i].x, S.preview[i].y);
        g.lineTo(p.x, p.y);
      }
      g.stroke();
      g.setLineDash([]);
      g.restore();

      /* Impact marker at the end of the arc. */
      var last = S.preview[S.preview.length - 1];
      var l = toScreen(last.x, last.y);
      g.strokeStyle = "rgba(255,255,255,0.55)";
      g.lineWidth = Math.max(1.5, 2.5 * S.camZoom);
      g.beginPath();
      g.arc(l.x, l.y, Math.max(4, 8 * S.camZoom), 0, A.TAU);
      g.stroke();
    }

    function drawDebris(g) {
      for (var i = 0; i < S.debris.length; i++) {
        var d = S.debris[i];
        var p = toScreen(d.x, d.y);
        var sz = d.size * S.camZoom;
        g.save();
        g.translate(p.x, p.y);
        g.rotate(d.angle);
        g.globalAlpha = A.clamp01(d.life);
        g.fillStyle = A.rgb(d.colour);
        g.fillRect(-sz / 2, -sz / 2, sz, sz);
        g.restore();
      }
      g.globalAlpha = 1;
    }

    function render(g) {
      if (!S.engine) return;
      drawBackdrop(g);

      g.save();
      A.Fx.applyShake(g);

      drawSlingshot(g, false);

      var bodies = M.Composite.allBodies(S.engine.world);
      var i;
      for (i = 0; i < bodies.length; i++) {
        var gm = bodies[i].game;
        if (!gm) continue;
        if (gm.kind === "block") drawBlock(g, bodies[i]);
      }
      for (i = 0; i < bodies.length; i++) {
        var ge = bodies[i].game;
        if (ge && ge.kind === "enemy") drawEnemy(g, bodies[i]);
      }
      for (i = 0; i < bodies.length; i++) {
        var gb = bodies[i].game;
        if (gb && gb.kind === "bird") {
          drawBird(g, bodies[i].position.x, bodies[i].position.y, bodies[i].angle, gb.r, gb.spec);
        }
      }

      drawDebris(g);

      /* The nocked bird rides the pouch until it is launched. */
      if (S.state === "aim" && S.spec) {
        var aim = Math.atan2(ANCHOR.y - S.pouch.y, ANCHOR.x - S.pouch.x) + Math.PI;
        drawBird(g, S.pouch.x, S.pouch.y, aim, S.spec.radius, S.spec);
      }

      drawSlingshot(g, true);
      if (S.state === "aim" && S.pulling) drawPreview(g);

      A.Fx.drawParticles(g, function (x, y) {
        var p = toScreen(x, y);
        return { x: p.x, y: p.y, s: S.camZoom };
      });
      A.Fx.drawTexts(g, function (x, y) {
        var p = toScreen(x, y);
        return { x: p.x, y: p.y, s: S.camZoom };
      });

      g.restore();
      A.Fx.drawFlash(g, layout.w, layout.h);
    }

    /* --------------------------------------------------------------- ui -- */
    function mount(root) {
      var hud = host.el("div", "hud");
      var top = host.el("div", "row");
      ui.level = host.el("div", "chip", "LEVEL 1");
      ui.score = host.el("div", "chip", "0");
      ui.birds = host.el("div", "chip gold", "3");
      top.appendChild(ui.level);
      top.appendChild(ui.score);
      top.appendChild(ui.birds);
      hud.appendChild(top);

      ui.cap = host.el("div", "cap", "");
      hud.appendChild(ui.cap);

      ui.hint = host.el("div", "hint", "Drag the pouch back, release to fire");
      hud.appendChild(ui.hint);
      root.appendChild(hud);
    }

    function paintHud() {
      if (!ui.level || !S) return;
      ui.level.textContent = "LEVEL " + save.level;
      ui.score.textContent = A.formatNumber(S.score);
      ui.birds.textContent = "BIRDS " + S.birdsLeft;

      var left = S.enemies.length - S.popped;
      if (S.state === "flying" && !S.abilityUsed) {
        ui.cap.textContent = "TAP FOR " + S.spec.ability.toUpperCase();
        ui.cap.style.color = "#5DE0FF";
      } else {
        ui.cap.textContent = (S.spec ? S.spec.name.toUpperCase() + "   ·   " : "") +
          left + " target" + (left === 1 ? "" : "s") + " left";
        ui.cap.style.color = "";
      }
      if (S.shots > 0) ui.hint.textContent = "";
    }

    return {
      mount: mount,
      start: begin,
      stop: function () {
        teardownWorld();
        S = null;
      },
      update: function (dt) { if (S && layout) update(dt); },
      render: function (g) { if (S && layout) render(g); },
      onResize: computeLayout
    };
  }

  A.games.push({
    id: "avian",
    name: "Avian Artillery",
    tagline: "Slingshot artillery. Read the arc, find the load-bearing block, bring the fort down.",
    accent: "#E2455F",
    unlock: 1,
    template: { coins: 0, runs: 0, level: 1, stars: 0 },
    bestLine: function (s) { return s.runs ? "Level " + s.level + "  ·  " + s.stars + " stars" : "New"; },
    thumb: function (g, w, h, t) {
      var sky = g.createLinearGradient(0, 0, 0, h);
      sky.addColorStop(0, "#67B7E8");
      sky.addColorStop(1, "#C5E3E8");
      g.fillStyle = sky;
      g.fillRect(0, 0, w, h);

      g.fillStyle = "#5B9A55";
      g.fillRect(0, h * 0.78, w, h * 0.22);

      /* A small fort with a target on top. */
      g.fillStyle = "#AD763E";
      g.fillRect(w * 0.60, h * 0.50, w * 0.05, h * 0.28);
      g.fillRect(w * 0.80, h * 0.50, w * 0.05, h * 0.28);
      g.fillStyle = "#9AD8E8";
      g.fillRect(w * 0.58, h * 0.43, w * 0.29, h * 0.06);
      g.fillStyle = "#6ABE60";
      g.beginPath();
      g.arc(w * 0.725, h * 0.36, Math.min(w, h) * 0.075, 0, A.TAU);
      g.fill();

      /* Slingshot and a bird arcing across. */
      g.strokeStyle = "#6B4A2C";
      g.lineWidth = Math.max(2, w * 0.022);
      g.lineCap = "round";
      g.beginPath();
      g.moveTo(w * 0.16, h * 0.78);
      g.lineTo(w * 0.16, h * 0.60);
      g.moveTo(w * 0.16, h * 0.60);
      g.lineTo(w * 0.11, h * 0.50);
      g.moveTo(w * 0.16, h * 0.60);
      g.lineTo(w * 0.21, h * 0.50);
      g.stroke();

      var p = A.wrap(t * 0.45, 1);
      var bx = A.lerp(w * 0.17, w * 0.70, p);
      var by = h * 0.56 - Math.sin(p * Math.PI) * h * 0.30;

      g.setLineDash([3, 4]);
      g.strokeStyle = "rgba(255,255,255,0.7)";
      g.lineWidth = 1.5;
      g.beginPath();
      for (var i = 0; i <= 16; i++) {
        var q = i / 16;
        var qx = A.lerp(w * 0.17, w * 0.70, q);
        var qy = h * 0.56 - Math.sin(q * Math.PI) * h * 0.30;
        if (i === 0) g.moveTo(qx, qy); else g.lineTo(qx, qy);
      }
      g.stroke();
      g.setLineDash([]);
      g.lineCap = "butt";

      g.fillStyle = "#E2455F";
      g.beginPath();
      g.arc(bx, by, Math.min(w, h) * 0.062, 0, A.TAU);
      g.fill();
      g.fillStyle = "#F2A93B";
      g.beginPath();
      g.moveTo(bx + w * 0.035, by - h * 0.008);
      g.lineTo(bx + w * 0.072, by + h * 0.006);
      g.lineTo(bx + w * 0.035, by + h * 0.022);
      g.closePath();
      g.fill();
    },
    create: create
  });
})(window.A);
