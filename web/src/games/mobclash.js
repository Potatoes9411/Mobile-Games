/* ===========================================================================
   MOB CLASH: GATE SIEGE
   Swerve a crowd through math gates, then spend the horde storming a keep one
   room at a time. The pacing generator below is unchanged from the standalone
   build and is verified solvable for every level it produces; the runtime and
   renderer sit on the shared arcade engine.
   =========================================================================== */
(function (A) {
  "use strict";
  A.games = A.games || [];

  var TAU = A.TAU;
  var MAX_CROWD = 99999;
  var clamp = A.clamp, clamp01 = A.clamp01, lerp = A.lerp, invLerp = A.invLerp;
  var smoothDamp = A.smoothDamp;

  /* ------------------------------------------------------- gate maths --- */
  function applyGate(crowd, type, value) {
    var r;
    if (type === "add") r = crowd + value;
    else if (type === "sub") r = crowd - value;
    else if (type === "mul") r = value <= 0 ? crowd : crowd * value;
    else r = value <= 1 ? crowd : Math.round(crowd / value);
    return clamp(Math.round(r), 0, MAX_CROWD);
  }

  function gateLabel(type, value) {
    return type === "add" ? "+" + value :
           type === "sub" ? "\u2212" + value :
           type === "mul" ? "\u00D7" + value : "\u00F7" + value;
  }

  function gateIsGood(type, value) {
    return (type === "add" && value > 0) || (type === "mul" && value > 1);
  }

  const TUNE = {
    tutorialLevels: 3, masteryLevels: 10, feederInterval: 4, seed: 90211,
    baseTrackLength: 110, trackLengthPerLevel: 6, maxTrackLength: 300, trackHalfWidth: 6,
    baseRunSpeed: 12, runSpeedPerLevel: 0.11, maxRunSpeed: 18,
    tutorialRows: 3, masteryRows: 4, feederRows: 4, maxRows: 6,
    gateLane: 0.55, firstRowDistance: 26, finalRowPadding: 20,
    safeAddFraction: 0.65, trapSubtractFraction: 0.35,
    maxScatteredObstacles: 14, obstacleKillRadius: 1.15, attritionPerObstacle: 0.10,
    maxFloors: 6, tutorialDifficulty: 0.35, feederDifficulty: 0.30,
    coreStart: 0.55, coreEnd: 0.95, plateau: 40,
    floorWeightGrowth: 0.75, maxCasualty: 0.25,
    towerGap: 18
  };
  
  /** Greedy solver, mirrors LevelData.IsSolvable. */
  function isSolvable(nodes, crowd, casualtyRatio) {
    if (!nodes.length) return true;
    const cleared = nodes.map(() => false);
    const topFloor = nodes.reduce((m, n) => Math.max(m, n.floor), 0);
    let remaining = nodes.length, guard = 0;
  
    while (remaining > 0 && guard++ < 4096) {
      let unlocked = 0;
      for (let f = 0; f <= topFloor; f++) {
        const done = nodes.every((n, i) => n.floor !== f || cleared[i]);
        if (!done) { unlocked = f; break; }
        unlocked = f + 1;
      }
      let best = -1;
      nodes.forEach((n, i) => {
        if (cleared[i] || n.floor !== unlocked || n.power >= crowd) return;
        if (best < 0 || n.power < nodes[best].power) best = i;
      });
      if (best < 0) return false;
      const p = nodes[best].power;
      crowd += p - Math.round(p * casualtyRatio);
      cleared[best] = true;
      remaining--;
    }
    return remaining === 0;
  }
  
  function generateLevel(levelIndex, crowdAtStart, bias) {
    levelIndex = Math.max(1, levelIndex);
    crowdAtStart = clamp(crowdAtStart, 5, 500);
    bias = clamp(bias, 0.55, 1.25);
  
    const rand = A.rng(TUNE.seed + levelIndex * 7919);
    const rf = (a, b) => a + rand() * (b - a);
    const chance = p => rand() < p;
  
    const isTutorial = levelIndex <= TUNE.tutorialLevels;
    const isFeeder = !isTutorial && levelIndex % TUNE.feederInterval === 0;
  
    const level = {
      index: levelIndex, isTutorial, isFeeder,
      startingCrowd: crowdAtStart,
      halfWidth: TUNE.trackHalfWidth,
      speed: Math.min(TUNE.maxRunSpeed, TUNE.baseRunSpeed + TUNE.runSpeedPerLevel * (levelIndex - 1)),
      gates: [], obstacles: [], nodes: [], floorCount: 2,
      casualtyRatio: 0, goldPerUnit: 1, goldMultiplier: isFeeder ? 2 : 1,
      floorClearBonus: 20 + 5 * levelIndex,
      band: isTutorial ? "hook" : (isFeeder ? "gold rush" : (levelIndex <= TUNE.masteryLevels ? "mastery" : "core"))
    };
  
    const rawLength = TUNE.baseTrackLength + TUNE.trackLengthPerLevel * (levelIndex - 1);
    level.length = Math.min(TUNE.maxTrackLength, rawLength) * (isFeeder ? 0.92 : 1);
    level.towerDistance = level.length + TUNE.towerGap;
  
    /* --- hazards scattered along the track ------------------------------- */
    if (!isTutorial && !isFeeder) {
      const count = Math.round(lerp(1, TUNE.maxScatteredObstacles, invLerp(4, 30, levelIndex)));
      const from = TUNE.firstRowDistance - 8;
      const to = level.length - TUNE.finalRowPadding - 6;
      for (let i = 0; i < count && to > from; i++) {
        const o = { z: rf(from, to), lane: rf(-0.85, 0.85), r: TUNE.obstacleKillRadius, kind: "static", amp: 0, spd: 0, phase: rf(0, TAU) };
        if (levelIndex >= 16 && chance(0.30)) { o.kind = "spinner"; o.amp = rf(2.0, 3.2); o.spd = rf(0.45, 0.85); }
        else if (levelIndex >= 11 && chance(0.45)) { o.kind = "sweeper"; o.amp = rf(1.8, 3.4); o.spd = rf(0.35, 0.70); }
        level.obstacles.push(o);
      }
    }
  
    /* --- decision rows ---------------------------------------------------- */
    const rows = isTutorial ? TUNE.tutorialRows
      : isFeeder ? TUNE.feederRows
      : levelIndex <= TUNE.masteryLevels ? TUNE.masteryRows
      : clamp(TUNE.masteryRows + Math.floor((levelIndex - TUNE.masteryLevels) / 6), TUNE.masteryRows, TUNE.maxRows);
  
    const skill = isTutorial ? 0.99
      : isFeeder ? 0.95
      : levelIndex <= TUNE.masteryLevels ? 0.86
      : lerp(0.86, 0.70, invLerp(TUNE.masteryLevels, TUNE.plateau, levelIndex));
  
    const firstZ = TUNE.firstRowDistance;
    const lastZ = Math.max(firstZ + 12, level.length - TUNE.finalRowPadding);
    let expected = crowdAtStart;
    let previousZ = 0;
    let trapUsed = false;
  
    const riskyMotion = spec => {
      if (levelIndex >= 9 && chance(0.4)) { spec.motion = "rotate"; spec.amp = rf(2.2, 3.0); spec.spd = rf(0.30, 0.55); }
      else if (levelIndex >= 6) { spec.motion = "slide"; spec.amp = rf(1.8, 2.8); spec.spd = rf(0.35, 0.60); }
    };
  
    for (let row = 0; row < rows; row++) {
      const t = rows <= 1 ? 0.5 : row / (rows - 1);
      const z = lerp(firstZ, lastZ, t) + rf(-2, 2);
  
      let left = { type: "add", value: 10, motion: "static", amp: 0, spd: 0, risky: false };
      let right = { type: "mul", value: 2, motion: "static", amp: 0, spd: 0, risky: false };
  
      if (isTutorial) {
        /* The hook: both halves are gifts, so the win rate stays at 100%.
           The ladder ramps the finale crowd 600 -> 900 -> 1500 across levels 1-3. */
        const ladder = [[2, 3, 5], [3, 3, 5], [3, 5, 5]][clamp(levelIndex - 1, 0, 2)];
        let li = clamp(row, 0, 2);
        if (row === rows - 1) li = 2;
        left = { type: "mul", value: ladder[li], motion: "static", amp: 0, spd: 0, risky: false };
        right = { type: "add", value: Math.max(10, Math.round(expected * 0.6)), motion: "static", amp: 0, spd: 0, risky: false };
      } else if (isFeeder) {
        /* Gold rush. Multiplier rows alternate with additive rows: four stacked multipliers
           would compound into a five figure crowd and wreck the gold curve. */
        if (row % 2 === 0) {
          left = { type: "mul", value: 3, motion: "static", amp: 0, spd: 0, risky: false };
          right = { type: "mul", value: 2, motion: "static", amp: 0, spd: 0, risky: false };
        } else {
          left = { type: "add", value: Math.max(25, Math.round(expected * 1.2)), motion: "static", amp: 0, spd: 0, risky: false };
          right = { type: "add", value: Math.max(15, Math.round(expected * 0.7)), motion: "static", amp: 0, spd: 0, risky: false };
        }
      } else if (levelIndex <= TUNE.masteryLevels) {
        const safeAdd = Math.max(10, Math.round(expected * TUNE.safeAddFraction));
        const makeTrap = !trapUsed && levelIndex >= 7 && chance(0.35);
        if (makeTrap) { trapUsed = true; left = { type: "sub", value: Math.max(5, Math.round(expected * TUNE.trapSubtractFraction)), motion: "static", amp: 0, spd: 0, risky: false }; }
        else left = { type: "add", value: safeAdd, motion: "static", amp: 0, spd: 0, risky: false };
        right = { type: "mul", value: (levelIndex >= 8 && chance(0.4)) ? 3 : 2, motion: "static", amp: 0, spd: 0, risky: true };
        riskyMotion(right);
      } else {
        const roll = rand();
        if (roll < 0.45) {
          left = { type: "add", value: Math.max(12, Math.round(expected * TUNE.safeAddFraction)), motion: "static", amp: 0, spd: 0, risky: false };
          right = { type: "mul", value: chance(0.35) ? 3 : 2, motion: "static", amp: 0, spd: 0, risky: true };
          riskyMotion(right);
        } else if (roll < 0.75) {
          left = { type: "mul", value: 2, motion: "static", amp: 0, spd: 0, risky: false };
          right = { type: "mul", value: chance(0.30) ? 4 : 3, motion: "static", amp: 0, spd: 0, risky: true };
          riskyMotion(right);
        } else if (!trapUsed && expected > 40) {
          trapUsed = true;
          left = { type: "div", value: 2, motion: "static", amp: 0, spd: 0, risky: false };
          right = { type: "sub", value: Math.max(10, Math.round(expected * TUNE.trapSubtractFraction)), motion: chance(0.5) ? "slide" : "static", amp: 2.2, spd: 0.45, risky: false };
        } else {
          left = { type: "add", value: Math.max(15, Math.round(expected * 0.5)), motion: "slide", amp: 2.0, spd: 0.40, risky: false };
          right = { type: "mul", value: 2, motion: "static", amp: 0, spd: 0, risky: true };
          riskyMotion(right);
        }
      }
  
      if (chance(0.5)) { const s = left; left = right; right = s; }
  
      left.lane = -TUNE.gateLane; right.lane = TUNE.gateLane;
  
      [left, right].forEach(half => {
        half.z = z;
        half.row = row;
        half.phase = rf(0, TAU);
        if (half.risky && !isTutorial && !isFeeder) {
          const guards = levelIndex >= 12 ? 2 : 1;
          for (let g = 0; g < guards; g++) {
            const gz = z - 6 - g * 4.5;
            if (gz <= 8) continue;
            level.obstacles.push({
              z: gz, lane: half.lane + rf(-0.12, 0.12), r: TUNE.obstacleKillRadius, phase: rf(0, TAU),
              kind: levelIndex >= 10 ? "sweeper" : "static",
              amp: levelIndex >= 10 ? rf(1.2, 2.2) : 0,
              spd: levelIndex >= 10 ? rf(0.45, 0.80) : 0
            });
          }
        }
        level.gates.push(half);
      });
  
      /* simulate the reference run so the tower can be sized to the player */
      const a = applyGate(expected, left.type, left.value);
      const b = applyGate(expected, right.type, right.value);
      expected = Math.max(1, Math.round(lerp(Math.min(a, b), Math.max(a, b), skill)));
  
      const hazards = level.obstacles.filter(o => o.z > previousZ && o.z <= z).length;
      if (hazards > 0) expected = Math.max(1, Math.round(expected * Math.pow(1 - TUNE.attritionPerObstacle * (1 - skill), hazards)));
      previousZ = z;
    }
  
    const trailing = level.obstacles.filter(o => o.z > previousZ && o.z <= level.length).length;
    if (trailing > 0) expected = Math.max(1, Math.round(expected * Math.pow(1 - TUNE.attritionPerObstacle * (1 - skill), trailing)));
  
    level.expectedCrowd = clamp(expected, 5, MAX_CROWD);
  
    /* --- tower ------------------------------------------------------------ */
    const floors = (isTutorial || isFeeder) ? 2 : clamp(2 + Math.floor(levelIndex / 6), 2, TUNE.maxFloors);
    level.floorCount = floors;
  
    let ratio = isTutorial ? TUNE.tutorialDifficulty
      : isFeeder ? TUNE.feederDifficulty
      : lerp(TUNE.coreStart, TUNE.coreEnd, invLerp(4, TUNE.plateau, levelIndex));
    ratio *= bias;
    level.difficultyRatio = ratio;
    level.casualtyRatio = (isTutorial || isFeeder) ? 0 : lerp(0, TUNE.maxCasualty, invLerp(8, 30, levelIndex));
  
    const totalPower = Math.max(floors * 3, Math.round(level.expectedCrowd * ratio));
    const weights = [];
    let weightSum = 0;
    for (let f = 0; f < floors; f++) { weights[f] = 1 + TUNE.floorWeightGrowth * f; weightSum += weights[f]; }
  
    const nodes = [];
    for (let f = 0; f < floors; f++) {
      const floorPower = Math.max(2, Math.round(totalPower * (weights[f] / weightSum)));
      if (f === floors - 1) { nodes.push({ floor: f, slot: 0, power: floorPower, boss: true }); continue; }
  
      const slots = (levelIndex >= 15 && chance(0.45)) ? 3 : 2;
      const shares = [];
      let sum = 0;
      for (let s = 0; s < slots; s++) { shares[s] = (0.6 + s * 0.35) * rf(0.9, 1.1); sum += shares[s]; }
      for (let s = 0; s < slots; s++) {
        nodes.push({ floor: f, slot: s, power: Math.max(1, Math.round(floorPower * shares[s] / sum)), boss: false });
      }
    }
  
    /* Guarantee the tower is solvable by a reference crowd, and non trivial. */
    const scaleNodes = k => nodes.forEach(n => { n.power = clamp(Math.round(n.power * k), 1, MAX_CROWD); });
    const margin = (isTutorial || isFeeder) ? 0.70 : 0.90;
    const reference = Math.max(3, Math.round(level.expectedCrowd * margin));
    const trivial = Math.max(2, Math.round(level.expectedCrowd * 0.40));
  
    if (!isTutorial && !isFeeder) {
      let tighten = 0;
      while (tighten++ < 12 && isSolvable(nodes, trivial, level.casualtyRatio)) scaleNodes(1.08);
    }
    let relax = 0;
    while (relax++ < 32 && !isSolvable(nodes, reference, level.casualtyRatio)) scaleNodes(0.88);
    if (!isSolvable(nodes, reference, level.casualtyRatio)) {
      nodes.forEach(n => { n.power = Math.max(1, Math.round(reference * 0.35) + n.floor * 2); });
    }
  
    level.nodes = nodes;
    level.totalTowerPower = nodes.reduce((a, n) => a + n.power, 0);
    return level;
  }
  

  /* ======================================================== runtime ===== */
  var PAL = {
    skyTop: [27, 20, 54], skyMid: [122, 60, 104], skyLow: [244, 137, 75],
    sun: [255, 222, 150], fog: [236, 160, 116],
    hillFar: [92, 66, 116], hillNear: [56, 42, 82],
    grass: [46, 116, 88], grassDark: [30, 86, 66],
    road: [226, 199, 163], roadDark: [198, 169, 132], lane: [247, 232, 206],
    wall: [126, 96, 88], wallTop: [176, 140, 122],
    stone: [198, 182, 162], stoneDark: [168, 150, 132], stoneDeep: [112, 98, 88],
    gold: [255, 194, 75], jade: [63, 217, 138], red: [240, 77, 90], redDark: [130, 26, 38],
    blue: [61, 139, 255], ink: [36, 26, 56],
    tree: [38, 104, 78], treeDark: [24, 74, 56], trunk: [84, 58, 46]
  };

  var TOWER = { roomW: 3.5, roomH: 2.7, floorH: 3.7, baseY: 1.5, slot: 4.7, depth: 1.3 };
  var MAX_UNITS = 220;
  var SIEGE_UNITS = 130;

  function create(host) {
    var save = host.save;
    var cam = A.Camera3D();
    var mobAtlas = null, foeAtlas = null;
    var S = null;
    var ui = {};
    var clouds = null;
    var grads = null;

    function buildGradients(g) {
      var w = A.View.w, h = A.View.h;
      var bottom = cam.horizon + 2;

      var sky = g.createLinearGradient(0, 0, 0, bottom);
      sky.addColorStop(0, A.rgb(PAL.skyTop));
      sky.addColorStop(0.52, A.rgb(PAL.skyMid));
      sky.addColorStop(0.88, A.rgb(A.mix(PAL.skyMid, PAL.skyLow, 0.75)));
      sky.addColorStop(1, A.rgb(PAL.skyLow));

      var ground = g.createLinearGradient(0, cam.horizon, 0, h);
      ground.addColorStop(0, A.rgb(PAL.fog));
      ground.addColorStop(0.06, A.rgb(A.mix(PAL.grass, PAL.fog, 0.62)));
      ground.addColorStop(0.35, A.rgb(PAL.grass));
      ground.addColorStop(1, A.rgb(PAL.grassDark));

      var road = g.createLinearGradient(0, cam.horizon, 0, h);
      road.addColorStop(0, A.rgb(PAL.fog));
      road.addColorStop(0.08, A.rgb(A.mix(PAL.road, PAL.fog, 0.55)));
      road.addColorStop(0.4, A.rgb(PAL.road));
      road.addColorStop(1, A.rgb(PAL.roadDark));

      var vig = g.createRadialGradient(w / 2, h * 0.52, h * 0.28, w / 2, h * 0.52, h * 0.78);
      vig.addColorStop(0, "rgba(20,14,38,0)");
      vig.addColorStop(1, "rgba(20,14,38,0.45)");

      grads = { sky: sky, ground: ground, road: road, vig: vig, w: w, h: h, horizon: cam.horizon };
    }

    /* ------------------------------------------------------ economy ----- */
    function startingCrowd() { return 20 + save.upCrowd * 2; }
    function goldMultiplier() { return (1 + save.upGold * 0.15) * host.coinMultiplier(); }
    function gateFlat() { return save.upGate; }
    function gateMul() { return save.upGate * 0.05; }
    function ddsBias() { return clamp(1 - 0.08 * save.fails, 0.6, 1); }

    /* -------------------------------------------------------- assets ---- */
    function bake() {
      mobAtlas = A.Rig.bake(A.Rig.style(1201, {
        hue: host.hue, sat: 0.72, helmet: "crest", cape: false, weapon: "none", bulk: 1.0
      }), "run", 10, { cell: 82, ss: 2, height: 58 });

      foeAtlas = A.Rig.bake(A.Rig.style(7013, {
        hue: 0.02, sat: 0.7, helmet: "horned", cape: false, weapon: "axe", bulk: 1.08
      }), "march", 8, { cell: 82, ss: 2, height: 58 });

      var rand = A.rng(4242);
      clouds = [];
      for (var i = 0; i < 8; i++) {
        clouds.push({ x: rand(), y: 0.10 + rand() * 0.6, w: 0.07 + rand() * 0.12,
                      h: 0.012 + rand() * 0.02, a: 0.06 + rand() * 0.12, drift: 0.004 + rand() * 0.01 });
      }
    }

    /* --------------------------------------------------------- state ---- */
    function prepare() {
      var level = generateLevel(save.level, startingCrowd(), ddsBias());

      S = {
        phase: "ready",
        t: 0,
        level: level,
        player: { x: 0, z: 0, tx: 0, vx: 0 },
        crowd: 0,
        units: [],
        transitionT: 0,
        transitionFrom: 0,
        scenery: buildScenery(level),
        siege: null,
        reward: 0
      };

      level.gates.forEach(function (g) { g.consumed = false; });
      buildSiege();
      setCrowd(level.startingCrowd, true);
      moveCamera(0, true);
      paintHud();
    }

    function buildScenery(level) {
      var rand = A.rng(level.index * 104729 + 17);
      var out = [];
      var to = level.towerDistance + 60;
      for (var z = -20; z < to; z += 5.5 + rand() * 6) {
        for (var side = -1; side <= 1; side += 2) {
          if (rand() < 0.34) continue;
          var roll = rand();
          out.push({
            z: z + rand() * 3,
            x: side * (level.halfWidth + 3.5 + rand() * 26),
            kind: roll < 0.62 ? "tree" : (roll < 0.85 ? "rock" : "banner"),
            scale: 0.75 + rand() * 0.7
          });
        }
      }
      return out.sort(function (a, b) { return b.z - a.z; });
    }

    function visualCap() {
      return (S.phase === "siege" || S.phase === "transition" || S.phase === "done") ? SIEGE_UNITS : MAX_UNITS;
    }

    function setCrowd(n, quiet) {
      var before = S.crowd;
      S.crowd = clamp(Math.round(n), 0, MAX_CROWD);

      var want = Math.min(S.crowd, visualCap());
      while (S.units.length < want) {
        S.units.push({
          x: S.player.x + (Math.random() - 0.5) * 1.4,
          z: S.player.z + (Math.random() - 0.5) * 1.4,
          vx: 0, vz: 0,
          phase: Math.random(),
          damp: 0.11 + (Math.random() - 0.5) * 0.1,
          jx: (Math.random() - 0.5) * 0.4,
          jz: (Math.random() - 0.5) * 0.4,
          gait: 0.85 + Math.random() * 0.35
        });
      }
      if (S.units.length > want) S.units.length = want;

      if (!quiet && S.crowd !== before) paintCrowd(true);
      else paintCrowd(false);
    }

    function crowdRadius() {
      return Math.min(5.2, 0.44 * Math.sqrt(Math.max(1, S.units.length)));
    }

    /* --------------------------------------------------------- gates ---- */
    function gateHalfWidth() { return S.level.halfWidth * 0.5; }

    function gateX(g) {
      var base = (g.lane < 0 ? -1 : 1) * gateHalfWidth();
      var travel = Math.min(g.amp, gateHalfWidth() * 0.62);
      if (g.motion === "slide") return base + Math.sin(S.t * g.spd * TAU + g.phase) * travel;
      if (g.motion === "rotate") return base + Math.sin(S.t * g.spd * Math.PI + g.phase) * travel * 0.5;
      return base;
    }

    function obstaclePos(o) {
      var base = o.lane * (S.level.halfWidth - o.r);
      if (o.kind === "sweeper") return { x: base + Math.sin(S.t * o.spd * TAU + o.phase) * o.amp, z: o.z };
      if (o.kind === "spinner") {
        var a = (S.t * o.spd + o.phase) * TAU;
        return { x: base + Math.cos(a) * o.amp, z: o.z + Math.sin(a) * o.amp * 0.35 };
      }
      return { x: base, z: o.z };
    }

    function resolveGate(half) {
      var before = S.crowd;
      var after;
      if (half.type === "add") after = applyGate(before, "add", half.value + gateFlat());
      else if (half.type === "mul") after = clamp(Math.round(before * (half.value + gateMul())), 0, MAX_CROWD);
      else after = applyGate(before, half.type, half.value);

      setCrowd(after);

      var delta = after - before;
      var good = delta >= 0;
      var gx = gateX(half);

      A.Fx.text(gx, 4.2, half.z, (delta >= 0 ? "+" : "") + delta, good ? PAL.jade : PAL.red,
        { scale: 1.25, rise: 2.6 });
      A.Fx.burst(gx, 2.4, half.z, good ? 22 : 14, {
        color: good ? PAL.jade : PAL.red,
        speed: good ? 6.5 : 4.5, up: good ? 9 : 5, life: 0.55, size: 0.17, square: true
      });

      A.Fx.kick(good ? 0.3 : 0.6);
      if (!good) A.Fx.flashScreen(0.32, PAL.red);
      A.Audio.sfx(good ? "good" : "bad");
      A.vibrate(good ? 14 : 34);
    }

    function checkGates(prevZ, z) {
      var crossed = S.level.gates.filter(function (g) {
        return !g.consumed && g.z > prevZ && g.z <= z;
      });
      if (!crossed.length) return;

      var taken = null, best = Infinity;
      crossed.forEach(function (g) {
        var d = Math.abs(S.player.x - gateX(g));
        if (d < best) { best = d; taken = g; }
      });
      if (taken) resolveGate(taken);

      S.level.gates.forEach(function (g) {
        if (crossed.indexOf(g) >= 0 || (taken && g.row === taken.row)) g.consumed = true;
      });
    }

    function shaveObstacles() {
      var near = S.level.obstacles.filter(function (o) { return Math.abs(o.z - S.player.z) < 14; });
      if (!near.length || !S.units.length) return;

      var positions = near.map(obstaclePos);
      var killed = 0, lastX = 0, lastZ = 0;

      for (var i = S.units.length - 1; i >= 0; i--) {
        var u = S.units[i];
        for (var k = 0; k < positions.length; k++) {
          var dx = u.x - positions[k].x, dz = u.z - positions[k].z, r = near[k].r;
          if (dx * dx + dz * dz <= r * r) {
            lastX = u.x; lastZ = u.z;
            A.Fx.burst(u.x, 0.9, u.z, 4, { color: [176, 218, 255], speed: 3.5, up: 5, life: 0.5, size: 0.15 });
            S.units.splice(i, 1);
            killed++;
            break;
          }
        }
      }
      if (!killed) return;

      var perVisual = (S.units.length > 0 && S.crowd > S.units.length)
        ? Math.max(1, Math.round(S.crowd / S.units.length)) : 1;
      var loss = Math.min(S.crowd, killed * perVisual);
      S.crowd = Math.max(0, S.crowd - loss);
      paintCrowd(true);

      A.Fx.kick(0.3);
      A.Audio.sfx("hit");
      A.Fx.text(lastX, 2.8, lastZ, "-" + loss, PAL.red, { scale: 0.9, rise: 2.6 });
    }

    /* --------------------------------------------------------- siege ---- */
    function buildSiege() {
      S.siege = {
        nodes: S.level.nodes.map(function (n) {
          return { floor: n.floor, slot: n.slot, power: n.power, boss: n.boss,
                   state: n.floor === 0 ? "available" : "locked", rect: null, pulse: 0, reject: 0 };
        }),
        floor: 0, battle: null, deadlock: 0, live: false
      };
    }

    function roomWorld(node) {
      var slots = S.level.nodes.filter(function (n) { return n.floor === node.floor; }).length;
      return {
        x: (node.slot - (slots - 1) * 0.5) * TOWER.slot,
        y: TOWER.baseY + node.floor * TOWER.floorH,
        z: S.level.towerDistance - TOWER.depth
      };
    }

    function attackNode(node) {
      if (!S.siege.live || S.siege.battle || node.state !== "available") return;
      var w = roomWorld(node);

      if (S.crowd <= node.power) {
        node.reject = 0.34;
        A.Fx.kick(0.34);
        A.Fx.flashScreen(0.22, PAL.red);
        A.Audio.sfx("bad");
        A.vibrate(70);
        A.Fx.text(w.x, w.y + TOWER.roomH + 0.6, w.z, "TOO STRONG", PAL.red, { scale: 0.85 });
        return;
      }

      var casualties = Math.round(node.power * S.level.casualtyRatio);
      S.siege.battle = {
        node: node, t: 0, stage: "charge",
        start: S.crowd,
        trough: Math.max(1, S.crowd - node.power),
        final: Math.max(1, S.crowd + node.power - casualties),
        gain: node.power - casualties
      };
      A.Audio.sfx("select");
      A.vibrate(20);
    }

    function updateSiege(dt) {
      var s = S.siege;
      s.nodes.forEach(function (n) {
        if (n.pulse > 0) n.pulse -= dt;
        if (n.reject > 0) n.reject -= dt;
      });

      if (s.battle) {
        var b = s.battle;
        b.t += dt;

        if (b.stage === "charge") {
          var k = clamp01(b.t / 0.28);
          setCrowd(Math.round(lerp(b.start, b.trough, k)), true);
          if (k >= 1) {
            var w = roomWorld(b.node);
            b.stage = "absorb"; b.t = 0;
            b.node.pulse = 0.38;
            A.Fx.kick(0.62);
            A.Fx.flashScreen(0.3, [255, 255, 255]);
            A.Fx.hitStop(0.05);
            A.Audio.sfx("hit");
            A.vibrate(30);
            A.Fx.burst(w.x, w.y + TOWER.roomH * 0.5, w.z, 34,
              { color: PAL.stone, speed: 7, up: 9, life: 0.9, size: 0.26, square: true });
            A.Fx.text(w.x, w.y + TOWER.roomH + 0.4, w.z, "+" + b.gain, PAL.jade, { scale: 1.1 });
          }
        } else {
          var k2 = clamp01(b.t / 0.32);
          setCrowd(Math.round(lerp(b.trough, b.final, k2)), true);
          if (k2 >= 1) {
            setCrowd(b.final);
            b.node.state = "cleared";
            A.Audio.sfx("coin");

            var floorDone = s.nodes.every(function (n) { return n.floor !== s.floor || n.state === "cleared"; });
            if (floorDone && s.floor + 1 < S.level.floorCount) {
              s.floor++;
              s.nodes.forEach(function (n) { if (n.floor === s.floor && n.state === "locked") n.state = "available"; });
            }
            s.battle = null;
            if (s.nodes.every(function (n) { return n.state === "cleared"; })) finish(true);
          }
        }
        return;
      }

      var anyMove = s.nodes.some(function (n) { return n.state === "available" && S.crowd > n.power; });
      var allDone = s.nodes.every(function (n) { return n.state === "cleared"; });
      if (!anyMove && !allDone) {
        s.deadlock += dt;
        if (s.deadlock > 1.1) finish(false, "No room left that you outnumber");
      } else {
        s.deadlock = 0;
      }
    }

    /* -------------------------------------------------------- camera ---- */
    function moveCamera(dt, snap) {
      var siegeView = S.phase === "transition" || S.phase === "siege" || S.phase === "done";
      var target;

      if (siegeView) {
        var f = S.level.floorCount;
        target = { x: 0, y: 9 + f * 1.4, z: S.level.towerDistance - (16 + f * 2.4) };
      } else {
        target = { x: S.player.x * 0.55, y: 9.6, z: S.player.z - 13.5 };
      }

      cam.fit(A.View.w, A.View.h, 0.27, 0.56);
      if (snap) cam.moveTo(target.x, target.y, target.z);
      else cam.moveTo(target.x, target.y, target.z, S.phase === "run" ? 9 : 3.4, dt);
    }

    /* -------------------------------------------------------- update ---- */
    function update(dt) {
      S.t += dt;
      var level = S.level;

      if (S.phase === "ready") {
        if (A.Input.pressed || A.Input.tapped) {
          S.phase = "run";
          save.runs++;
          host.commit();
          if (ui.hint) ui.hint.textContent = "Drag to steer";
        }
      } else if (S.phase === "run") {
        if (A.Input.axis) S.player.tx += A.Input.axis * 11 * dt;
        if (A.Input.down) S.player.tx += (A.Input.dx / Math.min(A.View.w, 620)) * 13;

        var limit = Math.max(0.6, level.halfWidth - crowdRadius() * 0.42);
        S.player.tx = clamp(S.player.tx, -limit, limit);

        var prevZ = S.player.z;
        var step = smoothDamp(S.player.x, S.player.tx, S.player.vx, 0.085, 26, dt);
        S.player.x = step[0]; S.player.vx = step[1];
        S.player.z += level.speed * dt;

        checkGates(prevZ, S.player.z);
        shaveObstacles();

        if (S.crowd <= 0) finish(false, "The mob was wiped out on the track");
        else if (S.player.z >= level.length) {
          S.transitionFrom = S.player.z;
          S.transitionT = 0;
          S.phase = "transition";
          setCrowd(S.crowd, true);
        }
      } else if (S.phase === "transition") {
        S.transitionT += dt;
        var k = clamp01(S.transitionT / 1.15);
        S.player.z = lerp(S.transitionFrom, level.towerDistance - 8, A.smooth(k));
        S.player.x = A.approach(S.player.x, 0, 6, dt);
        S.player.tx = S.player.x;
        if (k >= 1) {
          S.phase = "siege";
          S.siege.live = true;
          if (ui.hint) ui.hint.textContent = "Tap a room you outnumber";
        }
      } else if (S.phase === "siege") {
        updateSiege(dt);
        if (A.Input.pressed) {
          for (var i = 0; i < S.siege.nodes.length; i++) {
            var n = S.siege.nodes[i];
            if (n.state === "cleared" || !n.rect) continue;
            var r = n.rect;
            if (A.Input.x >= r.x && A.Input.x <= r.x + r.w && A.Input.y >= r.y && A.Input.y <= r.y + r.h) {
              attackNode(n);
              break;
            }
          }
        }
      }

      updateUnits(dt);
      moveCamera(dt);
      paintProgress();
    }

    function updateUnits(dt) {
      var n = S.units.length;
      if (!n) return;

      var line = S.phase === "siege" || S.phase === "done";
      var radius = crowdRadius();
      var spreadX = line ? Math.min(12, 1.15 * Math.sqrt(n)) : radius;
      var spreadZ = line ? Math.min(3.6, 0.34 * Math.sqrt(n)) : radius;
      var cx = S.player.x, cz = S.player.z;

      for (var i = 0; i < n; i++) {
        var u = S.units[i];
        var slot = (i + 0.5) / n;
        var angle = i * 2.39996323;
        var r = Math.sqrt(slot);
        var a = smoothDamp(u.x, cx + Math.cos(angle) * r * spreadX + u.jx, u.vx, u.damp, 60, dt);
        var b = smoothDamp(u.z, cz + Math.sin(angle) * r * spreadZ + u.jz, u.vz, u.damp, 60, dt);
        u.x = a[0]; u.vx = a[1];
        u.z = b[0]; u.vz = b[1];
      }
    }

    /* -------------------------------------------------------- finish ---- */
    function finish(win, reason) {
      if (S.phase === "done") return;
      S.phase = "done";
      S.siege.live = false;

      var cleared = S.siege.nodes.filter(function (n) { return n.state === "cleared"; }).length;
      var raw = S.crowd * S.level.goldPerUnit + cleared * S.level.floorClearBonus;
      var gold = Math.round(raw * S.level.goldMultiplier * goldMultiplier() * (win ? 1 : 0.25));

      save.gold += gold;
      save.bestCrowd = Math.max(save.bestCrowd, S.crowd);
      if (win) { save.level++; save.wins++; save.fails = 0; }
      else save.fails++;
      host.commit();

      var xp = Math.round((cleared * 12 + S.level.index * 8) * host.xpMultiplier());
      host.addXp(xp);
      host.progress("run", 1);
      host.progress("coins", gold);
      host.progress("crowd", S.crowd);
      if (win) host.progress("tower", 1);

      A.Audio.sfx(win ? "win" : "lose");
      A.vibrate(win ? [20, 40, 30] : 110);
      if (win) {
        A.Fx.kick(0.7);
        A.Fx.flashScreen(0.4, PAL.gold);
        A.Fx.burst(0, TOWER.baseY + S.level.floorCount * TOWER.floorH, S.level.towerDistance, 80,
          { color: PAL.gold, speed: 11, up: 15, life: 1.5, size: 0.28, square: true, gravity: 16 });
      }

      host.results({
        win: win,
        title: win ? "TOWER TAKEN" : "SIEGE BROKEN",
        subtitle: win ? cleared + " rooms conquered" : (reason || "The mob fell short"),
        stats: [
          ["Level", String(S.level.index)],
          ["Final mob", A.formatNumber(S.crowd)],
          ["Gold", "+" + A.formatNumber(gold)],
          ["Account XP", "+" + A.formatNumber(xp)]
        ],
        buttons: [
          { label: win ? "NEXT LEVEL" : "TRY AGAIN", className: "go",
            onClick: function () { host.modal.hide(); prepare(); } },
          { label: "UPGRADES", className: "gold", onClick: openShop },
          { label: "MENU", className: "ghost", onClick: host.exit }
        ]
      });
    }

    function openShop() {
      var rows = [
        { key: "upCrowd", name: "Starting Mob", desc: "+2 runners at the start line", max: 40,
          cost: function (l) { return Math.round(120 * Math.pow(1.35, l)); } },
        { key: "upGold", name: "Gold Multiplier", desc: "+15% gold from every run", max: 30,
          cost: function (l) { return Math.round(300 * Math.pow(1.45, l)); } },
        { key: "upGate", name: "Gate Bonus", desc: "+1 on additive gates, +0.05x on multipliers", max: 20,
          cost: function (l) { return Math.round(500 * Math.pow(1.5, l)); } }
      ];

      var card = host.el("div", "card");
      card.appendChild(host.el("h2", null, "UPGRADES"));
      card.appendChild(host.el("p", "sub", A.formatNumber(save.gold) + " gold"));

      rows.forEach(function (r) {
        var row = host.el("div", "shopRow");
        var left = host.el("div");
        left.appendChild(host.el("h4", null, r.name + "  ·  LV " + (save[r.key] + 1)));
        left.appendChild(host.el("p", null, r.desc));
        row.appendChild(left);

        var maxed = save[r.key] >= r.max;
        var cost = r.cost(save[r.key]);
        var buy = host.el("button", "buy");
        buy.innerHTML = maxed ? "MAX" : A.formatNumber(cost) + "<small>GOLD</small>";
        buy.disabled = maxed || save.gold < cost;
        buy.addEventListener("click", function () {
          if (save.gold >= cost) {
            save.gold -= cost;
            save[r.key]++;
            host.commit();
            A.Audio.sfx("coin");
            openShop();
          }
        });
        row.appendChild(buy);
        card.appendChild(row);
      });

      card.appendChild(host.button("BACK", "ghost", function () { host.modal.hide(); prepare(); }));
      host.modal.show(card);
    }

    /* -------------------------------------------------------- render ---- */
    function fogAt(z) { return cam.fog(z, 34, 165) * 0.86; }
    function F(color, z) { return A.rgb(A.mix(color, PAL.fog, fogAt(z))); }

    function render(g) {
      var w = A.View.w, h = A.View.h;
      if (!grads || grads.w !== w || grads.h !== h || grads.horizon !== cam.horizon) buildGradients(g);
      A.Fx.applyShake(g);

      drawSky(g, w, h);
      drawHills(g, w);
      drawTerrain(g);

      var items = [];
      for (var i = 0; i < S.scenery.length; i++) {
        var item = S.scenery[i];
        if (item.z > cam.z + 2 && item.z < cam.z + 200) items.push({ z: item.z, fn: drawScenery, a: item });
      }
      if (S.level.towerDistance - cam.z < 230) items.push({ z: S.level.towerDistance, fn: drawKeep });
      S.level.gates.forEach(function (gate) { if (!gate.consumed) items.push({ z: gate.z, fn: drawGate, a: gate }); });
      S.level.obstacles.forEach(function (o) { items.push({ z: o.z, fn: drawObstacle, a: o }); });
      S.units.forEach(function (u) { items.push({ z: u.z, fn: drawUnit, a: u }); });
      items.push({ z: S.level.length, fn: drawFinish });

      items.sort(function (a, b) { return b.z - a.z; });
      for (var k = 0; k < items.length; k++) items[k].fn(g, items[k].a);

      A.Fx.drawParticles(g, cam.project);
      A.Fx.drawTexts(g, cam.project);

      g.fillStyle = grads.vig;
      g.fillRect(0, 0, w, h);

      g.restore();
      A.Fx.drawFlash(g, w, h);
    }

    function drawSky(g, w, h) {
      var bottom = cam.horizon + 2;
      g.fillStyle = grads.sky;
      g.fillRect(0, 0, w, bottom);

      var sunX = w * 0.5 - cam.x * 2.2;
      var sunY = cam.horizon - h * 0.055;
      var glow = g.createRadialGradient(sunX, sunY, 0, sunX, sunY, h * 0.42);
      glow.addColorStop(0, A.rgba(PAL.sun, 0.75));
      glow.addColorStop(0.35, A.rgba(PAL.skyLow, 0.3));
      glow.addColorStop(1, A.rgba(PAL.skyLow, 0));
      g.fillStyle = glow;
      g.fillRect(0, 0, w, bottom);

      g.fillStyle = A.rgba(PAL.sun, 0.95);
      g.beginPath();
      g.arc(sunX, sunY, h * 0.052, 0, TAU);
      g.fill();

      for (var i = 0; i < clouds.length; i++) {
        var c = clouds[i];
        var cx = A.wrap(c.x + S.t * c.drift - cam.x * 0.004, 1.4);
        var px = cx * w * 1.4 - w * 0.2;
        var py = c.y * cam.horizon;
        g.fillStyle = A.rgba(A.mix(PAL.sun, [255, 255, 255], 0.35), c.a);
        g.beginPath();
        g.ellipse(px, py, c.w * w, c.h * h, 0, 0, TAU);
        g.ellipse(px + c.w * w * 0.55, py + c.h * h * 0.28, c.w * w * 0.55, c.h * h * 0.72, 0, 0, TAU);
        g.fill();
      }
    }

    function hillLayer(g, w, color, amp, base, phase, parallax, step) {
      var offset = -cam.x * parallax - cam.z * parallax * 0.05;
      g.beginPath();
      g.moveTo(-10, cam.horizon + 4);
      for (var x = -10; x <= w + 10; x += step) {
        var t = (x + offset) * 0.0055;
        g.lineTo(x, cam.horizon - base - amp * (Math.sin(t + phase) * 0.62 + Math.sin(t * 2.17 + phase * 2.3) * 0.38));
      }
      g.lineTo(w + 10, cam.horizon + 4);
      g.closePath();
      g.fillStyle = color;
      g.fill();
    }

    function drawHills(g, w) {
      var h = A.View.h;
      hillLayer(g, w, A.rgb(A.mix(PAL.hillFar, PAL.fog, 0.45)), h * 0.055, h * 0.008, 0.7, 5, 16);
      hillLayer(g, w, A.rgb(A.mix(PAL.hillNear, PAL.fog, 0.22)), h * 0.036, 0, 2.4, 11, 12);
    }

    function drawTerrain(g) {
      var hw = S.level.halfWidth;
      var nearZ = cam.z + 3.2, farZ = cam.z + 210;
      var h = A.View.h, w = A.View.w;

      g.fillStyle = grads.ground;
      g.fillRect(0, cam.horizon, w, h - cam.horizon);

      A.strip(g, cam, -hw - 2.6, hw + 2.6, 0.02, 0.02, nearZ, farZ,
        A.rgb(A.mix(PAL.roadDark, PAL.grassDark, 0.35)));

      A.strip(g, cam, -hw, hw, 0.04, 0.04, nearZ, farZ, grads.road);

      var start = Math.floor(nearZ / 6) * 6;
      for (var z = start; z < Math.min(farZ, nearZ + 130); z += 6) {
        var fade = 1 - fogAt(z);
        if (fade <= 0.03) continue;
        g.globalAlpha = 0.34 * fade;
        A.strip(g, cam, -hw + 0.35, hw - 0.35, 0.05, 0.05, z, z + 1.6, A.rgb(PAL.lane));
        g.globalAlpha = 1;
      }

      for (var side = -1; side <= 1; side += 2) {
        var x = side * (hw + 0.5);
        A.strip(g, cam, x, x, 0, 1.75, nearZ, farZ, A.rgb(PAL.wall));
        A.strip(g, cam, x, x + side * 0.75, 1.75, 1.75, nearZ, farZ, A.rgb(PAL.wallTop));
      }
    }

    function drawScenery(g, item) {
      var base = cam.project(item.x, 0, item.z);
      if (!base) return;

      var s = base.s * item.scale;
      var fg = fogAt(item.z);

      if (item.kind === "tree") {
        g.fillStyle = A.rgba(PAL.ink, 0.22 * (1 - fg));
        g.beginPath(); g.ellipse(base.x, base.y, s, s * 0.3, 0, 0, TAU); g.fill();
        g.fillStyle = A.rgb(A.mix(PAL.trunk, PAL.fog, fg));
        g.fillRect(base.x - s * 0.14, base.y - s * 1.5, s * 0.28, s * 1.5);
        for (var i = 0; i < 3; i++) {
          var y = base.y - s * (1.4 + i * 0.85);
          var r = s * (1.25 - i * 0.28);
          g.fillStyle = A.rgb(A.mix(i === 0 ? PAL.treeDark : PAL.tree, PAL.fog, fg));
          g.beginPath();
          g.moveTo(base.x, y - r * 1.5);
          g.lineTo(base.x + r, y + r * 0.35);
          g.lineTo(base.x - r, y + r * 0.35);
          g.closePath();
          g.fill();
        }
      } else if (item.kind === "rock") {
        g.fillStyle = A.rgb(A.mix(PAL.stoneDeep, PAL.fog, fg));
        g.beginPath(); g.ellipse(base.x, base.y - s * 0.28, s * 0.62, s * 0.42, 0, 0, TAU); g.fill();
        g.fillStyle = A.rgb(A.mix(PAL.stoneDark, PAL.fog, fg));
        g.beginPath(); g.ellipse(base.x - s * 0.12, base.y - s * 0.4, s * 0.36, s * 0.24, 0, 0, TAU); g.fill();
      } else {
        var top = base.y - s * 2.6;
        g.strokeStyle = A.rgb(A.mix(PAL.trunk, PAL.fog, fg));
        g.lineWidth = Math.max(1, s * 0.13);
        g.beginPath(); g.moveTo(base.x, base.y); g.lineTo(base.x, top); g.stroke();
        var wave = Math.sin(S.t * 2.4 + item.z) * s * 0.18;
        g.fillStyle = A.rgb(A.mix(PAL.red, PAL.fog, fg));
        g.beginPath();
        g.moveTo(base.x, top);
        g.lineTo(base.x + s * 0.95, top + s * 0.3 + wave);
        g.lineTo(base.x, top + s * 0.78);
        g.closePath();
        g.fill();
      }
    }

    function drawGate(g, gate) {
      var gx = gateX(gate);
      var f = cam.plane(gate.z);
      if (!f || f.s < 0.9) return;

      var good = gateIsGood(gate.type, gate.value);
      var tint = good ? PAL.jade : PAL.red;
      var fg = fogAt(gate.z);
      var alpha = 1 - fg * 0.8;

      var halfW = gateHalfWidth() - 0.12;
      var lx = f.px(gx - halfW), rx = f.px(gx + halfW);
      var y0 = f.py(0.05), yField = f.py(3.7), yTop = f.py(4.35);

      g.globalAlpha = alpha;

      var cxs = (lx + rx) / 2;
      var glow = g.createRadialGradient(cxs, (y0 + yTop) / 2, 0, cxs, (y0 + yTop) / 2, (rx - lx) * 0.9);
      glow.addColorStop(0, A.rgba(tint, 0.34));
      glow.addColorStop(1, A.rgba(tint, 0));
      g.fillStyle = glow;
      g.fillRect(lx - (rx - lx) * 0.5, yTop - 20, (rx - lx) * 2, y0 - yTop + 40);

      var field = g.createLinearGradient(0, yField, 0, y0);
      field.addColorStop(0, A.rgba(tint, 0.14));
      field.addColorStop(1, A.rgba(tint, 0.42));
      g.fillStyle = field;
      g.fillRect(lx, yField, rx - lx, y0 - yField);

      g.globalAlpha = alpha * 0.5;
      g.fillStyle = "rgba(255,255,255,0.5)";
      for (var i = 0; i < 4; i++) {
        var t = A.wrap(S.t * 0.55 + i * 0.25 + gate.phase * 0.1, 1);
        g.fillRect(lx, yField + (y0 - yField) * t, rx - lx, Math.max(1, f.s * 0.035));
      }
      g.globalAlpha = alpha;

      var postW = f.s * 0.62;
      var stone = F(PAL.stone, gate.z), stoneD = F(PAL.stoneDark, gate.z);
      g.fillStyle = stone;
      g.fillRect(lx - postW * 0.5, yTop, postW, y0 - yTop);
      g.fillRect(rx - postW * 0.5, yTop, postW, y0 - yTop);
      g.fillStyle = stoneD;
      g.fillRect(lx - postW * 0.5, yTop, postW * 0.32, y0 - yTop);
      g.fillRect(rx - postW * 0.5, yTop, postW * 0.32, y0 - yTop);

      var lintelH = f.py(3.73) - yTop;
      g.fillStyle = stone;
      g.fillRect(lx - postW * 0.7, yTop, (rx - lx) + postW * 1.4, lintelH);
      g.fillStyle = A.rgba(tint, 0.85);
      g.fillRect(lx - postW * 0.7, yTop + lintelH * 0.26, (rx - lx) + postW * 1.4, lintelH * 0.34);

      var size = Math.max(12, f.s * 1.15);
      g.font = size + "px 'Titan One', 'Arial Black', sans-serif";
      g.textAlign = "center";
      g.textBaseline = "middle";
      g.lineJoin = "round";
      g.lineWidth = Math.max(3, size * 0.2);
      g.strokeStyle = A.rgb(PAL.ink);
      var label = gateLabel(gate.type, gate.value);
      g.strokeText(label, cxs, f.py(1.85));
      g.fillStyle = good ? "#FFFFFF" : "#FFB2AC";
      g.fillText(label, cxs, f.py(1.85));

      g.globalAlpha = 1;
    }

    function drawObstacle(g, o) {
      var pos = obstaclePos(o);
      var f = cam.plane(pos.z);
      if (!f || f.s < 0.9) return;

      var fg = fogAt(pos.z);
      var s = f.s;
      var cx = f.px(pos.x), gy = f.py(0);

      g.globalAlpha = (1 - fg) * 0.55;
      g.strokeStyle = A.rgb(PAL.red);
      g.lineWidth = Math.max(1.5, s * 0.055);
      g.setLineDash([s * 0.22, s * 0.18]);
      g.beginPath();
      g.ellipse(cx, gy, o.r * s, o.r * s * 0.34, 0, 0, TAU);
      g.stroke();
      g.setLineDash([]);
      g.globalAlpha = 1 - fg * 0.8;

      var hubY = f.py(1.05);
      var spin = S.t * (o.kind === "static" ? 1.6 : 4.2) + o.phase;
      var R = o.r * s * 0.95;

      g.fillStyle = F(PAL.stoneDeep, pos.z);
      g.fillRect(cx - s * 0.1, hubY, s * 0.2, gy - hubY);

      g.beginPath();
      for (var i = 0; i < 16; i++) {
        var a = spin + (i / 16) * TAU;
        var rr = R * (i % 2 === 0 ? 1 : 0.72);
        var px = cx + Math.cos(a) * rr, py = hubY + Math.sin(a) * rr * 0.42;
        if (i === 0) g.moveTo(px, py); else g.lineTo(px, py);
      }
      g.closePath();
      g.fillStyle = F(PAL.stoneDark, pos.z);
      g.fill();
      g.lineWidth = Math.max(1.5, s * 0.05);
      g.strokeStyle = F(PAL.ink, pos.z);
      g.stroke();

      g.beginPath();
      g.ellipse(cx, hubY, R * 0.34, R * 0.16, 0, 0, TAU);
      g.fillStyle = F(PAL.red, pos.z);
      g.fill();
      g.globalAlpha = 1;
    }

    function drawUnit(g, u) {
      var p = cam.project(u.x, 0, u.z);
      if (!p || p.s < 0.8) return;

      g.globalAlpha = 0.28;
      g.fillStyle = A.rgb(PAL.ink);
      g.beginPath();
      g.ellipse(p.x, p.y, p.s * 0.4, p.s * 0.15, 0, 0, TAU);
      g.fill();
      g.globalAlpha = 1;

      var moving = S.phase === "run" || S.phase === "transition";
      var cycle = moving ? S.t * 1.8 * u.gait + u.phase : u.phase + S.t * 0.25;
      var frame = Math.floor(A.wrap(cycle, 1) * mobAtlas.frames);
      A.Rig.blit(g, mobAtlas, frame, p.x, p.y, (1.95 * p.s) / mobAtlas.height, 1);
    }

    function drawFinish(g) {
      var hw = S.level.halfWidth;
      var f = cam.plane(S.level.length);
      if (!f || f.s < 0.6) return;

      for (var i = 0; i < 12; i++) {
        var x0 = f.px(-hw + (i * 2 * hw) / 12);
        var x1 = f.px(-hw + ((i + 1) * 2 * hw) / 12);
        g.fillStyle = i % 2 === 0 ? "#FFFFFF" : A.rgb(PAL.ink);
        g.fillRect(x0, f.py(0.06) - f.s * 0.06, x1 - x0, f.s * 1.1);
      }
    }

    function archPath(g, x, y, w, h) {
      var rr = Math.min(h * 0.52, w * 0.5);
      g.beginPath();
      g.moveTo(x, y + h);
      g.lineTo(x, y + rr);
      g.ellipse(x + w / 2, y + rr, w / 2, rr, 0, Math.PI, 0, false);
      g.lineTo(x + w, y + h);
      g.closePath();
    }

    function drawKeep(g) {
      var level = S.level, siege = S.siege;
      var floors = level.floorCount;
      var maxSlots = 1;
      for (var f0 = 0; f0 < floors; f0++) {
        maxSlots = Math.max(maxSlots, level.nodes.filter(function (n) { return n.floor === f0; }).length);
      }

      var shellW = maxSlots * TOWER.slot + 3.2;
      var shellH = TOWER.baseY + floors * TOWER.floorH + 1.4;
      var zF = level.towerDistance - TOWER.depth;
      var zB = level.towerDistance + TOWER.depth * 1.9;

      var front = cam.plane(zF), back = cam.plane(zB);
      if (!front || !back || front.s < 0.6) return;

      var z = level.towerDistance;
      var hw = shellW / 2;
      var fx0 = front.px(-hw), fx1 = front.px(hw);
      var fy0 = front.py(0), fyTop = front.py(shellH);

      A.quad(g, [back.px(-hw), back.py(0)], [back.px(hw), back.py(0)],
        [back.px(hw), back.py(shellH)], [back.px(-hw), back.py(shellH)], F(PAL.stoneDeep, z));
      A.quad(g, [fx0, fy0], [back.px(-hw), back.py(0)],
        [back.px(-hw), back.py(shellH)], [fx0, fyTop], F(PAL.stoneDark, z));
      A.quad(g, [fx1, fy0], [back.px(hw), back.py(0)],
        [back.px(hw), back.py(shellH)], [fx1, fyTop], F(PAL.stoneDark, z));

      var wall = g.createLinearGradient(0, fyTop, 0, fy0);
      wall.addColorStop(0, F(A.mix(PAL.stone, [255, 255, 255], 0.12), z));
      wall.addColorStop(1, F(A.mix(PAL.stone, PAL.stoneDeep, 0.3), z));
      g.fillStyle = wall;
      g.fillRect(fx0, fyTop, fx1 - fx0, fy0 - fyTop);

      g.fillStyle = A.rgba(PAL.stoneDeep, 0.14 * (1 - fogAt(z)));
      for (var y = 0.5; y < shellH; y += 0.62) {
        g.fillRect(fx0, front.py(y), fx1 - fx0, Math.max(1, front.s * 0.035));
      }

      var bw = front.s * 1.05;
      [fx0, fx1 - bw].forEach(function (bx) {
        g.fillStyle = F(PAL.stone, z);
        g.fillRect(bx, front.py(shellH + 0.5), bw, fy0 - front.py(shellH + 0.5));
        g.fillStyle = A.rgba(PAL.stoneDeep, 0.22 * (1 - fogAt(z)));
        g.fillRect(bx + bw * 0.72, front.py(shellH + 0.5), bw * 0.28, fy0 - front.py(shellH + 0.5));
      });

      var gw = front.s * TOWER.roomW * 0.85, gh = front.s * TOWER.baseY * 0.92;
      var gx = (fx0 + fx1) / 2 - gw / 2, gy = fy0 - gh;
      archPath(g, gx, gy, gw, gh);
      g.fillStyle = F(A.mix(PAL.stoneDeep, PAL.ink, 0.45), z);
      g.fill();
      g.strokeStyle = F(PAL.stoneDark, z);
      g.lineWidth = Math.max(1.5, front.s * 0.07);
      g.stroke();

      var merlonW = (fx1 - fx0) / 9;
      var merlonTop = front.py(shellH + 0.85);
      for (var m = 0; m < 9; m += 2) {
        g.fillStyle = F(PAL.stone, z);
        g.fillRect(fx0 + m * merlonW, merlonTop, merlonW, fyTop - merlonTop);
      }

      var poleX = (fx0 + fx1) / 2;
      var poleTop = front.py(shellH + 2.1);
      var allClear = siege.nodes.every(function (n) { return n.state === "cleared"; });
      g.strokeStyle = F(PAL.stoneDark, z);
      g.lineWidth = Math.max(1.5, front.s * 0.06);
      g.beginPath(); g.moveTo(poleX, merlonTop); g.lineTo(poleX, poleTop); g.stroke();
      var wave = Math.sin(S.t * 3.1) * front.s * 0.18;
      g.fillStyle = F(allClear ? PAL.blue : PAL.red, z);
      g.beginPath();
      g.moveTo(poleX, poleTop);
      g.lineTo(poleX + front.s * 1.5, poleTop + front.s * 0.42 + wave);
      g.lineTo(poleX, poleTop + front.s * 0.95);
      g.closePath();
      g.fill();

      var detail = front.s > 9;
      siege.nodes.forEach(function (node) {
        var world = roomWorld(node);
        var rx0 = front.px(world.x - TOWER.roomW / 2);
        var rx1 = front.px(world.x + TOWER.roomW / 2);
        var ry1 = front.py(world.y), ry0 = front.py(world.y + TOWER.roomH);
        var rw = rx1 - rx0, rh = ry1 - ry0;
        node.rect = { x: rx0, y: ry0, w: rw, h: rh };

        var beatable = S.crowd > node.power;
        var live = node.state === "available" && siege.live;
        var pulse = node.pulse > 0 ? Math.sin(clamp01(node.pulse / 0.38) * Math.PI) * 0.09 : 0;
        var jitter = node.reject > 0 ? Math.sin(S.t * 55) * rw * 0.05 * clamp01(node.reject / 0.34) : 0;

        var dx = rx0 + jitter - rw * pulse * 0.5;
        var dy = ry0 - rh * pulse * 0.5;
        var dw = rw * (1 + pulse), dh = rh * (1 + pulse);
        var cx = dx + dw / 2;

        var inner, edge, label;
        if (node.state === "cleared") { inner = A.mix(PAL.blue, PAL.ink, 0.6); edge = PAL.blue; label = "#BEE0FF"; }
        else if (node.state === "locked") { inner = A.mix(PAL.stoneDeep, PAL.ink, 0.62); edge = PAL.stoneDeep; label = "#A89684"; }
        else if (beatable) { inner = A.mix(PAL.redDark, PAL.ink, 0.2); edge = node.boss ? PAL.gold : PAL.jade; label = "#FFFFFF"; }
        else { inner = A.mix(PAL.redDark, PAL.ink, 0.55); edge = PAL.redDark; label = "#FFB2AC"; }

        archPath(g, dx - dw * 0.08, dy - dh * 0.06, dw * 1.16, dh * 1.06);
        g.fillStyle = F(A.mix(PAL.stone, PAL.stoneDark, 0.55), z);
        g.fill();

        archPath(g, dx, dy, dw, dh);
        g.fillStyle = F(inner, z);
        g.fill();
        g.save();
        g.clip();

        if (detail && node.state !== "cleared" && node.state !== "locked") {
          var show = Math.min(3, 1 + (node.power % 3));
          for (var i = 0; i < show; i++) {
            var spread = (i - (show - 1) / 2) * dw * 0.27;
            A.Rig.blit(g, foeAtlas, Math.floor((S.t * 2.6 + i * 0.31 + node.floor) * 4) % foeAtlas.frames,
              cx + spread, dy + dh * 0.96, (dh * 0.6) / foeAtlas.height, 1);
          }
        }

        if (node.state === "locked") {
          g.strokeStyle = "rgba(168,150,132,0.85)";
          g.lineWidth = Math.max(1.2, front.s * 0.06);
          for (var b = 1; b < 5; b++) {
            g.beginPath();
            g.moveTo(dx + (dw * b) / 5, dy + dh * 0.12);
            g.lineTo(dx + (dw * b) / 5, dy + dh);
            g.stroke();
          }
        }

        if (node.state === "cleared") {
          g.fillStyle = A.rgb(PAL.blue);
          var bwid = dw * 0.46, bh = dh * 0.52;
          g.beginPath();
          g.moveTo(cx - bwid / 2, dy + dh * 0.1);
          g.lineTo(cx + bwid / 2, dy + dh * 0.1);
          g.lineTo(cx + bwid / 2, dy + dh * 0.1 + bh);
          g.lineTo(cx, dy + dh * 0.1 + bh * 0.7);
          g.lineTo(cx - bwid / 2, dy + dh * 0.1 + bh);
          g.closePath();
          g.fill();
        }
        g.restore();

        archPath(g, dx, dy, dw, dh);
        g.strokeStyle = F(edge, z);
        g.lineWidth = Math.max(1.5, front.s * (live && beatable ? 0.1 : 0.05));
        g.stroke();
        if (live && beatable) {
          g.globalAlpha = 0.3 + Math.sin(S.t * 4 + node.floor) * 0.16;
          g.strokeStyle = A.rgb(edge);
          g.lineWidth = Math.max(2, front.s * 0.18);
          g.stroke();
          g.globalAlpha = 1;
        }

        if (front.s > 4) {
          var pw = dw * 0.68, ph = dh * 0.3;
          A.roundRect(g, cx - pw / 2, ry1 + dh * 0.06, pw, ph, ph * 0.3);
          g.fillStyle = F(A.mix(PAL.stoneDeep, PAL.ink, 0.35), z);
          g.fill();
          g.strokeStyle = F(edge, z);
          g.lineWidth = Math.max(1, front.s * 0.035);
          g.stroke();

          var size = Math.max(9, ph * 0.78);
          g.font = size + "px 'Titan One', 'Arial Black', sans-serif";
          g.textAlign = "center";
          g.textBaseline = "middle";
          g.fillStyle = label;
          g.fillText(node.state === "cleared" ? "✓" : String(node.power), cx, ry1 + dh * 0.06 + ph * 0.54);
        }
      });
    }

    /* ------------------------------------------------------------ ui ---- */
    function mount(root) {
      var hud = host.el("div", "hud");

      var top = host.el("div", "row");
      ui.level = host.el("div", "chip", "LVL 1");
      ui.meter = host.el("div", "meter");
      ui.meterFill = host.el("i");
      ui.meter.appendChild(ui.meterFill);
      ui.gold = host.el("div", "chip gold", "0");
      top.appendChild(ui.level);
      top.appendChild(ui.meter);
      top.appendChild(ui.gold);
      hud.appendChild(top);

      ui.crowd = host.el("div", "bigNum", "0");
      hud.appendChild(ui.crowd);
      hud.appendChild(host.el("div", "cap", "runners"));

      ui.hint = host.el("div", "hint", "Tap to start");
      hud.appendChild(ui.hint);

      root.appendChild(hud);
    }

    var popTimer = null;
    function paintCrowd(pop) {
      if (!ui.crowd) return;
      ui.crowd.textContent = A.formatNumber(S.crowd);
      if (!pop) return;
      ui.crowd.classList.add("pop");
      clearTimeout(popTimer);
      popTimer = setTimeout(function () { ui.crowd.classList.remove("pop"); }, 95);
    }

    function paintProgress() {
      if (!ui.meterFill) return;
      var done = (S.phase === "run" || S.phase === "ready")
        ? clamp01(S.player.z / S.level.length) : 1;
      ui.meterFill.style.width = Math.round(done * 100) + "%";
    }

    function paintHud() {
      if (!ui.level) return;
      ui.level.textContent = "LVL " + save.level;
      ui.gold.textContent = A.formatNumber(save.gold);
      ui.hint.textContent = "Tap to start";
      paintCrowd(false);
      paintProgress();
    }

    return {
      mount: mount,
      start: function () {
        if (!mobAtlas) bake();
        prepare();
      },
      stop: function () { S = null; },
      update: function (dt) { if (S) update(dt); },
      render: function (g) { if (S) render(g); },
      onResize: function () { if (S) moveCamera(0, true); }
    };
  }

  /* ------------------------------------------------------ registration -- */
  var thumbMob = null, thumbKeep = null;

  A.games.push({
    id: "mobclash",
    name: "Mob Clash",
    tagline: "Swerve a crowd through math gates, then storm the keep room by room.",
    accent: "#FFC24B",
    unlock: 1,
    template: { level: 1, gold: 0, runs: 0, wins: 0, fails: 0, bestCrowd: 0,
                upCrowd: 0, upGold: 0, upGate: 0 },
    bestLine: function (s) {
      return s.runs ? "Level " + s.level + "  ·  best mob " + A.formatNumber(s.bestCrowd) : "New";
    },
    thumb: function (g, w, h, t) {
      var sky = g.createLinearGradient(0, 0, 0, h);
      sky.addColorStop(0, "#3A2454");
      sky.addColorStop(0.55, "#F4894B");
      sky.addColorStop(0.56, "#2E7458");
      sky.addColorStop(1, "#1E5642");
      g.fillStyle = sky;
      g.fillRect(0, 0, w, h);

      g.fillStyle = "#E2C7A3";
      g.beginPath();
      g.moveTo(w * 0.34, h * 0.56);
      g.lineTo(w * 0.66, h * 0.56);
      g.lineTo(w * 0.95, h);
      g.lineTo(w * 0.05, h);
      g.closePath();
      g.fill();

      if (!thumbMob) {
        thumbMob = A.Rig.bake(A.Rig.style(1201, { hue: 0.58, helmet: "crest" }),
          "run", 8, { cell: 64, ss: 2, height: 40 });
      }
      for (var i = 0; i < 5; i++) {
        var x = w * 0.5 + Math.cos(i * 2.4) * 16;
        var y = h * 0.86 + Math.sin(i * 2.4) * 5;
        A.Rig.blit(g, thumbMob, Math.floor(t * 9 + i * 2) % thumbMob.frames, x, y, 0.34, 1);
      }
      g.fillStyle = "rgba(63,217,138,0.8)";
      g.fillRect(w * 0.36, h * 0.44, w * 0.12, h * 0.14);
      g.fillStyle = "rgba(255,77,90,0.8)";
      g.fillRect(w * 0.52, h * 0.44, w * 0.12, h * 0.14);
    },
    create: create
  });
})(window.A);
