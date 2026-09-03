/* ===========================================================================
   ARCADE ENGINE - procedural character rig
   
   A character is never authored. A seed produces proportions, a palette and
   gear; a named clip produces a pose (joint angles) for any point in time; the
   renderer walks the skeleton and draws tapered capsules with an ink outline.
   
   Two consumption paths:
     Rig.draw(...)  live skeletal draw - heroes, bosses, menu characters.
     Rig.bake(...)  renders a clip into a sprite strip once at boot, so a crowd
                    of hundreds costs one drawImage each instead of a skeleton.
   =========================================================================== */
(function (A) {
  "use strict";

  var TAU = A.TAU;

  var Rig = {};
  A.Rig = Rig;

  /* ==================================================== 1. STYLE ========= */

  var HELMETS = ["none", "dome", "horned", "hood", "crest"];
  var WEAPONS = ["none", "sword", "axe", "bow", "blaster", "staff"];

  /**
   * Builds a character style from a seed. Everything a character looks like -
   * height, bulk, limb length, colours, headgear - falls out of this, so two
   * seeds are two visibly different fighters with no art authored.
   */
  Rig.style = function (seed, opts) {
    opts = opts || {};
    var rand = A.rng(seed || 1);

    var bulk = opts.bulk !== undefined ? opts.bulk : rand.range(0.85, 1.2);
    var height = opts.height !== undefined ? opts.height : rand.range(0.92, 1.08);

    var hue = opts.hue !== undefined ? opts.hue : rand();
    var sat = opts.sat !== undefined ? opts.sat : rand.range(0.55, 0.8);

    var armor = opts.armor || A.hsl(hue, sat, rand.range(0.50, 0.60));
    var armorDark = A.shade(armor, -0.42);
    var armorLight = A.shade(armor, 0.30);
    var accent = opts.accent || A.hsl(A.wrap(hue + rand.range(0.12, 0.22), 1), 0.85, 0.60);
    var skin = opts.skin || A.hsl(A.wrap(0.07 + rand.range(-0.02, 0.02), 1), 0.42, rand.range(0.55, 0.78));
    var ink = opts.ink || [26, 20, 40];

    var s = {
      seed: seed,
      height: height,
      // Segment lengths in character units. A character is 1.0 tall at scale 1.
      hipY: 0.46 * height,
      spineLen: 0.26 * height,
      neckLen: 0.055 * height,
      headR: 0.098 * height * (opts.headScale || rand.range(0.95, 1.12)),
      upperArm: 0.155 * height,
      lowerArm: 0.145 * height,
      upperLeg: 0.215 * height,
      lowerLeg: 0.205 * height,
      shoulderW: 0.085 * height * bulk,
      hipW: 0.052 * height * bulk,

      // Limb radii, tapered from joint to joint. Chunky on purpose: thin limbs
      // read as a stick figure once a character is 40px tall on a phone.
      armR0: 0.055 * bulk, armR1: 0.042 * bulk,
      legR0: 0.070 * bulk, legR1: 0.052 * bulk,
      torsoR0: 0.105 * bulk, torsoR1: 0.086 * bulk,

      outline: 0.020,
      bulk: bulk,

      helmet: opts.helmet || (opts.helmet === "" ? "none" : rand.pick(HELMETS)),
      weapon: opts.weapon || (opts.weapon === "" ? "none" : "none"),
      cape: opts.cape !== undefined ? opts.cape : rand.chance(0.25),
      shoulderPads: opts.shoulderPads !== undefined ? opts.shoulderPads : rand.chance(0.6),

      colors: {
        armor: armor,
        armorDark: armorDark,
        armorLight: armorLight,
        accent: accent,
        skin: skin,
        ink: ink,
        boot: A.shade(armor, -0.55),
        visor: A.shade(ink, 0.12)
      },

      css: {}
    };

    // Pre-stringify: colour conversion in a per-frame draw is pure waste.
    s.css.armor = A.rgb(s.colors.armor);
    s.css.armorDark = A.rgb(s.colors.armorDark);
    s.css.armorLight = A.rgb(s.colors.armorLight);
    s.css.accent = A.rgb(s.colors.accent);
    s.css.skin = A.rgb(s.colors.skin);
    s.css.ink = A.rgb(s.colors.ink);
    s.css.boot = A.rgb(s.colors.boot);
    s.css.visor = A.rgb(s.colors.visor);
    s.css.backLimb = A.rgb(A.shade(s.colors.armor, -0.28));
    s.css.backSkin = A.rgb(A.shade(s.colors.skin, -0.25));

    return s;
  };

  /* ==================================================== 2. POSE ========== */

  Rig.newPose = function () {
    return {
      rootX: 0, rootY: 0, rootRot: 0,
      spine: 0, neck: 0,
      armUB: 0, armLB: 0,   // back arm: upper, lower(elbow bend)
      armUF: 0, armLF: 0,   // front arm
      legUB: 0, legLB: 0,   // back leg: thigh, knee bend
      legUF: 0, legLF: 0,   // front leg
      sway: 0,              // secondary motion for plume, cape, hair
      squash: 1
    };
  };

  var scratchPose = Rig.newPose();

  /**
   * Clip library. Each writes joint angles for a normalised time.
   * Angles: 0 points straight down for limbs, straight up for the spine.
   * Positive swings the limb toward the facing direction.
   */
  var CLIPS = {
    idle: function (p, t) {
      var s = Math.sin(t * TAU);
      p.spine = 0.05 + s * 0.018;
      p.neck = -0.02 - s * 0.012;
      p.armUB = 0.05 + s * 0.03; p.armLB = 0.22;
      p.armUF = -0.05 - s * 0.03; p.armLF = 0.22;
      p.legUB = 0.03; p.legLB = 0.06;
      p.legUF = -0.03; p.legLF = 0.06;
      p.rootY = s * 0.012;
      p.sway = s * 0.10;
    },

    run: function (p, t) {
      var s = Math.sin(t * TAU), c = Math.cos(t * TAU);
      p.spine = 0.17 + Math.abs(s) * 0.03;
      p.neck = -0.14;
      p.legUB = -s * 0.92; p.legLB = A.clamp(0.18 + s * 1.05, 0.05, 1.9);
      p.legUF = s * 0.92; p.legLF = A.clamp(0.18 - s * 1.05, 0.05, 1.9);
      p.armUB = s * 0.78; p.armLB = 0.5 + Math.max(0, -s) * 0.55;
      p.armUF = -s * 0.78; p.armLF = 0.5 + Math.max(0, s) * 0.55;
      p.rootY = Math.abs(c) * 0.042;
      p.sway = -s * 0.55;
      p.squash = 1 + c * 0.03;
    },

    sprint: function (p, t) {
      CLIPS.run(p, t);
      var s = Math.sin(t * TAU);
      p.spine = 0.36;
      p.neck = -0.24;
      p.legUB *= 1.2; p.legUF *= 1.2;
      p.armUB = s * 1.0; p.armUF = -s * 1.0;
      p.armLB = 0.85 + Math.max(0, -s) * 0.5;
      p.armLF = 0.85 + Math.max(0, s) * 0.5;
      p.rootY *= 1.3;
    },

    walk: function (p, t) {
      var s = Math.sin(t * TAU), c = Math.cos(t * TAU);
      p.spine = 0.08;
      p.neck = -0.05;
      p.legUB = -s * 0.5; p.legLB = A.clamp(0.10 + s * 0.6, 0.03, 1.4);
      p.legUF = s * 0.5; p.legLF = A.clamp(0.10 - s * 0.6, 0.03, 1.4);
      p.armUB = s * 0.42; p.armLB = 0.3;
      p.armUF = -s * 0.42; p.armLF = 0.3;
      p.rootY = Math.abs(c) * 0.018;
      p.sway = -s * 0.3;
    },

    jump: function (p, t) {
      // t: 0 at takeoff, 1 at landing.
      var tuck = Math.sin(A.clamp01(t) * Math.PI);
      p.spine = 0.10 - tuck * 0.06;
      p.neck = -0.06;
      p.legUB = 0.35 + tuck * 0.55; p.legLB = 0.5 + tuck * 0.9;
      p.legUF = -0.30 + tuck * 0.75; p.legLF = 0.35 + tuck * 0.7;
      p.armUB = -1.35 - tuck * 0.4; p.armLB = 0.25;
      p.armUF = -1.15 - tuck * 0.4; p.armLF = 0.25;
      p.rootY = 0;
      p.sway = -0.9;
      p.squash = 1.06;
    },

    fall: function (p, t) {
      p.spine = -0.06;
      p.neck = 0.05;
      p.legUB = 0.5; p.legLB = 0.35;
      p.legUF = -0.35; p.legLF = 0.5;
      p.armUB = -1.5; p.armLB = 0.5;
      p.armUF = -1.2; p.armLF = 0.6;
      p.sway = -1.2;
      p.squash = 0.96;
    },

    slide: function (p, t) {
      p.spine = -0.55;
      p.neck = 0.55;
      p.legUB = 0.95; p.legLB = 1.35;
      p.legUF = -0.85; p.legLF = 0.15;
      p.armUB = 1.1; p.armLB = 0.3;
      p.armUF = -0.5; p.armLF = 0.9;
      p.rootY = -0.20;
      p.sway = 1.1;
      p.squash = 0.9;
    },

    roll: function (p, t) {
      p.rootRot = A.clamp01(t) * TAU;
      p.spine = -0.9;
      p.neck = 0.8;
      p.legUB = 1.2; p.legLB = 1.7;
      p.legUF = 1.0; p.legLF = 1.7;
      p.armUB = 1.3; p.armLB = 1.4;
      p.armUF = 1.1; p.armLF = 1.4;
      p.rootY = -0.16;
    },

    attack: function (p, t) {
      t = A.clamp01(t);
      var windup = A.clamp01(t / 0.34);
      var swing = A.clamp01((t - 0.34) / 0.26);
      var recover = A.clamp01((t - 0.60) / 0.40);
      var arm = A.lerp(A.lerp(0, -2.3, windup), 1.5, swing);

      p.spine = 0.10 + windup * 0.16 - swing * 0.34 + recover * 0.10;
      p.neck = -0.10;
      p.armUF = arm;
      p.armLF = 0.35 + windup * 0.9 - swing * 0.9;
      p.armUB = 0.35 - windup * 0.5 + swing * 0.6;
      p.armLB = 0.55;
      p.legUB = -0.28; p.legLB = 0.22;
      p.legUF = 0.34; p.legLF = 0.18;
      p.sway = -swing * 1.4;
      p.squash = 1 + swing * 0.05;
    },

    shoot: function (p, t) {
      var kick = Math.exp(-A.clamp01(t) * 9) * 0.35;
      p.spine = 0.06 - kick * 0.2;
      p.neck = -0.05;
      p.armUF = -1.52 + kick; p.armLF = 0.05;
      p.armUB = -1.22 + kick * 0.6; p.armLB = 0.55;
      p.legUB = -0.30; p.legLB = 0.20;
      p.legUF = 0.32; p.legLF = 0.16;
      p.sway = kick * 2;
    },

    cast: function (p, t) {
      var pulse = Math.sin(A.clamp01(t) * Math.PI);
      p.spine = -0.08 - pulse * 0.1;
      p.neck = 0.08;
      p.armUF = -2.2 - pulse * 0.3; p.armLF = 0.3;
      p.armUB = -2.0 - pulse * 0.3; p.armLB = 0.3;
      p.legUB = -0.2; p.legLB = 0.15;
      p.legUF = 0.2; p.legLF = 0.15;
      p.sway = -pulse * 0.8;
      p.squash = 1 + pulse * 0.06;
    },

    hurt: function (p, t) {
      var k = Math.exp(-A.clamp01(t) * 6);
      p.spine = -0.35 * k;
      p.neck = 0.3 * k;
      p.armUB = -0.9 * k; p.armLB = 0.4;
      p.armUF = -1.1 * k; p.armLF = 0.4;
      p.legUB = -0.35 * k; p.legLB = 0.3;
      p.legUF = 0.4 * k; p.legLF = 0.3;
      p.rootX = -0.06 * k;
      p.sway = 1.5 * k;
    },

    die: function (p, t) {
      t = A.clamp01(t);
      var e = A.smooth(t);
      p.rootRot = e * 1.45;
      p.rootY = -e * 0.34;
      p.spine = -0.5 * e;
      p.neck = 0.4 * e;
      p.armUB = -1.4 * e; p.armLB = 0.5 * (1 - e);
      p.armUF = -1.1 * e; p.armLF = 0.5 * (1 - e);
      p.legUB = 0.7 * e; p.legLB = 0.6 * e;
      p.legUF = -0.5 * e; p.legLF = 0.5 * e;
      p.squash = 1 - e * 0.12;
    },

    cheer: function (p, t) {
      var s = Math.sin(t * TAU);
      p.spine = -0.06;
      p.neck = 0.10;
      p.armUB = -2.5 - s * 0.25; p.armLB = 0.2;
      p.armUF = -2.7 + s * 0.25; p.armLF = 0.2;
      p.legUB = 0.06; p.legLB = 0.05;
      p.legUF = -0.06; p.legLF = 0.05;
      p.rootY = Math.abs(s) * 0.07;
      p.sway = s * 0.5;
      p.squash = 1 + Math.abs(s) * 0.05;
    },

    march: function (p, t) {
      var s = Math.sin(t * TAU);
      p.spine = 0.06;
      p.neck = -0.04;
      p.legUB = -s * 0.62; p.legLB = A.clamp(0.12 + s * 0.8, 0.04, 1.5);
      p.legUF = s * 0.62; p.legLF = A.clamp(0.12 - s * 0.8, 0.04, 1.5);
      p.armUB = s * 0.30; p.armLB = 0.9;
      p.armUF = -s * 0.30; p.armLF = 0.9;
      p.rootY = Math.abs(Math.cos(t * TAU)) * 0.03;
      p.sway = -s * 0.35;
    }
  };

  Rig.clips = CLIPS;

  Rig.pose = function (clip, t, out) {
    var p = out || scratchPose;
    p.rootX = 0; p.rootY = 0; p.rootRot = 0;
    p.spine = 0; p.neck = 0;
    p.armUB = 0; p.armLB = 0; p.armUF = 0; p.armLF = 0;
    p.legUB = 0; p.legLB = 0; p.legUF = 0; p.legLF = 0;
    p.sway = 0; p.squash = 1;

    var fn = CLIPS[clip] || CLIPS.idle;
    fn(p, t);
    return p;
  };

  /** Blends two poses. Used for hit reactions over locomotion. */
  Rig.blend = function (a, b, t, out) {
    var p = out || Rig.newPose();
    for (var key in p) {
      if (typeof a[key] === "number") p[key] = A.lerp(a[key], b[key], t);
    }
    return p;
  };

  /* ==================================================== 3. DRAW ========== */

  /** Convex hull of two circles: a tapered capsule. The whole body is made of these. */
  function capsule(g, x0, y0, x1, y1, r0, r1) {
    var dx = x1 - x0, dy = y1 - y0;
    var d = Math.sqrt(dx * dx + dy * dy);

    if (d < 0.0001 || d <= Math.abs(r0 - r1)) {
      var r = Math.max(r0, r1);
      var cx = r0 > r1 ? x0 : x1, cy = r0 > r1 ? y0 : y1;
      g.beginPath();
      g.arc(cx, cy, r, 0, TAU);
      return;
    }

    var a = Math.atan2(dy, dx);
    var t = Math.acos(A.clamp((r0 - r1) / d, -1, 1));
    g.beginPath();
    g.arc(x0, y0, r0, a + t, a - t);
    g.arc(x1, y1, r1, a - t, a + t);
    g.closePath();
  }

  function limb(g, x0, y0, x1, y1, r0, r1, fill, ink, inkWidth) {
    capsule(g, x0, y0, x1, y1, r0, r1);
    if (ink) { g.lineWidth = inkWidth; g.strokeStyle = ink; g.lineJoin = "round"; g.stroke(); }
    g.fillStyle = fill;
    g.fill();
  }

  function disc(g, x, y, r, fill, ink, inkWidth) {
    g.beginPath();
    g.arc(x, y, r, 0, TAU);
    if (ink) { g.lineWidth = inkWidth; g.strokeStyle = ink; g.stroke(); }
    g.fillStyle = fill;
    g.fill();
  }

  /**
   * Walks the skeleton and draws the character.
   * (x, y) is the point between the feet; scale is the character's height in px.
   * facing is 1 or -1.
   */
  Rig.draw = function (g, style, pose, x, y, scale, facing, opts) {
    opts = opts || {};
    var c = style.css;
    var ink = opts.silhouette ? opts.silhouette : c.ink;
    var inkW = Math.max(0.6, style.outline * scale);
    var f = facing >= 0 ? 1 : -1;

    g.save();
    g.translate(x, y);
    if (pose.rootRot) {
      g.translate(0, -style.hipY * scale);
      g.rotate(pose.rootRot * f);
      g.translate(0, style.hipY * scale);
    }
    g.scale(f * scale, scale * (pose.squash || 1));
    g.translate(pose.rootX, -pose.rootY);

    // Skeleton solve. Angles are absolute from straight down (limbs) and the
    // torso lean accumulates into the arms, which is what sells the run cycle.
    var hipX = 0, hipY = -style.hipY;
    var spine = pose.spine;
    var chestX = hipX + Math.sin(spine) * style.spineLen;
    var chestY = hipY - Math.cos(spine) * style.spineLen;
    var neckA = spine + pose.neck;
    var headX = chestX + Math.sin(neckA) * (style.neckLen + style.headR);
    var headY = chestY - Math.cos(neckA) * (style.neckLen + style.headR);

    function chain(ax, ay, a0, len0, a1, len1) {
      var jx = ax + Math.sin(a0) * len0;
      var jy = ay + Math.cos(a0) * len0;
      var ex = jx + Math.sin(a0 + a1) * len1;
      var ey = jy + Math.cos(a0 + a1) * len1;
      return [jx, jy, ex, ey];
    }

    var shoulderBX = chestX - Math.cos(spine) * style.shoulderW * 0.55;
    var shoulderFX = chestX + Math.cos(spine) * style.shoulderW * 0.55;
    var shoulderY = chestY + style.torsoR0 * 0.15;

    var armB = chain(shoulderBX, shoulderY, pose.armUB + spine * 0.5, style.upperArm, pose.armLB, style.lowerArm);
    var armF = chain(shoulderFX, shoulderY, pose.armUF + spine * 0.5, style.upperArm, pose.armLF, style.lowerArm);
    var legB = chain(hipX - style.hipW, hipY, pose.legUB, style.upperLeg, -pose.legLB, style.lowerLeg);
    var legF = chain(hipX + style.hipW, hipY, pose.legUF, style.upperLeg, -pose.legLF, style.lowerLeg);

    // 1. contact shadow
    if (!opts.noShadow) {
      g.save();
      g.globalAlpha = 0.26;
      g.beginPath();
      g.ellipse(0, 0.005, 0.20, 0.055, 0, 0, TAU);
      g.fillStyle = c.ink;
      g.fill();
      g.restore();
    }

    // 2. cape behind everything
    if (style.cape) {
      var swayX = pose.sway * 0.07;
      g.beginPath();
      g.moveTo(chestX - 0.06, chestY + 0.02);
      g.quadraticCurveTo(chestX - 0.16 + swayX, chestY - 0.16, chestX - 0.10 + swayX * 2.2, hipY + 0.19);
      g.lineTo(chestX + 0.09 + swayX * 2.2, hipY + 0.20);
      g.quadraticCurveTo(chestX + 0.13 + swayX, chestY - 0.14, chestX + 0.06, chestY + 0.02);
      g.closePath();
      g.fillStyle = c.accent;
      g.lineWidth = inkW / scale;
      g.strokeStyle = ink;
      g.stroke();
      g.fill();
    }

    var lw = inkW / scale;

    // 3. back limbs, shaded so the silhouette reads in depth
    limb(g, legB[0], legB[1], legB[2], legB[3], style.legR1, style.legR1 * 0.85, c.backLimb, ink, lw);
    limb(g, hipX - style.hipW, hipY, legB[0], legB[1], style.legR0 * 0.9, style.legR1, c.backLimb, ink, lw);
    disc(g, legB[2], legB[3], style.legR1 * 1.05, A.rgb(A.shade(style.colors.boot, -0.15)), ink, lw);

    limb(g, armB[0], armB[1], armB[2], armB[3], style.armR1, style.armR1 * 0.85, c.backLimb, ink, lw);
    limb(g, shoulderBX, shoulderY, armB[0], armB[1], style.armR0 * 0.9, style.armR1, c.backLimb, ink, lw);
    disc(g, armB[2], armB[3], style.armR1 * 1.1, c.backSkin, ink, lw);

    // 4. front leg
    limb(g, legF[0], legF[1], legF[2], legF[3], style.legR1, style.legR1 * 0.9, c.armorDark, ink, lw);
    limb(g, hipX + style.hipW, hipY, legF[0], legF[1], style.legR0, style.legR1, c.armor, ink, lw);
    disc(g, legF[2], legF[3], style.legR1 * 1.15, c.boot, ink, lw);

    // 5. torso and belt
    limb(g, hipX, hipY, chestX, chestY, style.torsoR1, style.torsoR0, c.armor, ink, lw);
    g.save();
    g.translate(hipX, hipY);
    g.rotate(-spine);
    g.fillStyle = c.armorDark;
    g.fillRect(-style.torsoR1 * 1.05, -0.012, style.torsoR1 * 2.1, 0.030);
    g.restore();

    if (style.shoulderPads) {
      disc(g, shoulderFX, shoulderY, style.torsoR0 * 0.52, c.armorLight, ink, lw);
      disc(g, shoulderBX, shoulderY, style.torsoR0 * 0.46, A.rgb(A.shade(style.colors.armorLight, -0.2)), ink, lw);
    }

    // 6. head, helmet, face
    disc(g, headX, headY, style.headR, c.skin, ink, lw);
    drawHelmet(g, style, headX, headY, neckA, pose, ink, lw);

    if (style.helmet !== "hood") {
      g.save();
      g.translate(headX, headY);
      g.rotate(-neckA);
      g.fillStyle = c.visor;
      A.roundRect(g, -style.headR * 0.72, -style.headR * 0.10, style.headR * 1.5, style.headR * 0.40,
        style.headR * 0.18);
      g.fill();
      g.restore();
    }

    // 7. front arm and whatever it is holding
    limb(g, armF[0], armF[1], armF[2], armF[3], style.armR1, style.armR1 * 0.9, c.armorDark, ink, lw);
    limb(g, shoulderFX, shoulderY, armF[0], armF[1], style.armR0, style.armR1, c.armor, ink, lw);
    disc(g, armF[2], armF[3], style.armR1 * 1.15, c.skin, ink, lw);

    if (style.weapon && style.weapon !== "none") {
      drawWeapon(g, style, armF[2], armF[3], pose.armUF + pose.armLF + spine * 0.5, ink, lw);
    }

    g.restore();
  };

  function drawHelmet(g, style, hx, hy, angle, pose, ink, lw) {
    var c = style.css;
    var r = style.headR;
    if (style.helmet === "none") return;

    g.save();
    g.translate(hx, hy);
    g.rotate(-angle);

    if (style.helmet === "hood") {
      g.beginPath();
      g.arc(0, 0, r * 1.22, Math.PI * 0.86, Math.PI * 2.14);
      g.closePath();
      g.fillStyle = c.armorDark;
      g.lineWidth = lw; g.strokeStyle = ink; g.stroke();
      g.fill();
    } else {
      g.beginPath();
      g.arc(0, -r * 0.10, r * 1.16, Math.PI, TAU);
      g.closePath();
      g.fillStyle = c.armor;
      g.lineWidth = lw; g.strokeStyle = ink; g.stroke();
      g.fill();
    }

    if (style.helmet === "horned") {
      for (var side = -1; side <= 1; side += 2) {
        g.beginPath();
        g.moveTo(side * r * 0.9, -r * 0.30);
        g.quadraticCurveTo(side * r * 1.9, -r * 0.85, side * r * 1.5, -r * 1.5);
        g.quadraticCurveTo(side * r * 1.25, -r * 0.75, side * r * 0.75, -r * 0.55);
        g.closePath();
        g.fillStyle = c.armorLight;
        g.lineWidth = lw; g.strokeStyle = ink; g.stroke();
        g.fill();
      }
    } else if (style.helmet === "crest" || style.helmet === "dome") {
      var lean = (pose.sway || 0) * 0.10;
      g.beginPath();
      g.moveTo(-r * 0.10, -r * 1.05);
      g.quadraticCurveTo(r * 0.25 + lean, -r * 2.10, r * 1.05 + lean * 2, -r * 1.75);
      g.quadraticCurveTo(r * 0.45 + lean, -r * 1.45, r * 0.22, -r * 0.95);
      g.closePath();
      g.fillStyle = c.accent;
      g.lineWidth = lw; g.strokeStyle = ink; g.stroke();
      g.fill();
    }

    g.restore();
  }

  function drawWeapon(g, style, hx, hy, angle, ink, lw) {
    var c = style.css;
    g.save();
    g.translate(hx, hy);
    g.rotate(angle);

    switch (style.weapon) {
      case "sword":
        g.fillStyle = c.armorDark;
        g.fillRect(-0.014, -0.02, 0.028, 0.10);
        g.beginPath();
        g.moveTo(-0.020, -0.02);
        g.lineTo(0.020, -0.02);
        g.lineTo(0.012, -0.30);
        g.lineTo(0, -0.34);
        g.lineTo(-0.012, -0.30);
        g.closePath();
        g.fillStyle = "#E8EEF7";
        g.lineWidth = lw; g.strokeStyle = ink; g.stroke();
        g.fill();
        g.fillStyle = c.accent;
        g.fillRect(-0.048, -0.035, 0.096, 0.022);
        break;

      case "axe":
        g.fillStyle = c.armorDark;
        g.fillRect(-0.013, -0.02, 0.026, 0.12);
        g.beginPath();
        g.moveTo(0, -0.02);
        g.quadraticCurveTo(0.13, -0.10, 0.09, -0.24);
        g.quadraticCurveTo(-0.02, -0.17, -0.09, -0.24);
        g.quadraticCurveTo(-0.13, -0.10, 0, -0.02);
        g.closePath();
        g.fillStyle = "#D8DEE9";
        g.lineWidth = lw; g.strokeStyle = ink; g.stroke();
        g.fill();
        break;

      case "bow":
        g.beginPath();
        g.arc(0, 0, 0.15, -Math.PI * 0.62, Math.PI * 0.62);
        g.lineWidth = 0.022; g.strokeStyle = c.accent; g.stroke();
        g.beginPath();
        g.moveTo(Math.cos(-Math.PI * 0.62) * 0.15, Math.sin(-Math.PI * 0.62) * 0.15);
        g.lineTo(Math.cos(Math.PI * 0.62) * 0.15, Math.sin(Math.PI * 0.62) * 0.15);
        g.lineWidth = 0.006; g.strokeStyle = "#EDEDED"; g.stroke();
        break;

      case "blaster":
        A.roundRect(g, -0.03, -0.05, 0.20, 0.07, 0.02);
        g.fillStyle = c.armorDark;
        g.lineWidth = lw; g.strokeStyle = ink; g.stroke();
        g.fill();
        A.roundRect(g, 0.13, -0.038, 0.09, 0.045, 0.015);
        g.fillStyle = c.accent;
        g.fill();
        break;

      case "staff":
        g.fillStyle = c.armorDark;
        g.fillRect(-0.011, -0.30, 0.022, 0.46);
        disc(g, 0, -0.32, 0.045, c.accent, ink, lw);
        break;
    }

    g.restore();
  }

  /* ==================================================== 4. BAKE ========== */

  /**
   * Renders a clip into a horizontal sprite strip. Crowds draw from this, which
   * turns a skeleton solve plus ~20 path fills into a single drawImage.
   */
  Rig.bake = function (style, clip, frameCount, options) {
    options = options || {};
    var cell = options.cell || 96;
    var ss = options.ss || 2;
    var height = options.height || cell * 0.78;
    var footRatio = options.footRatio !== undefined ? options.footRatio : 0.94;

    var cw = cell, ch = Math.round(cell * 1.25);
    var canvas = A.newCanvas(cw * ss * frameCount, ch * ss);
    var g = canvas.getContext("2d");
    g.scale(ss, ss);
    g.lineJoin = "round";
    g.lineCap = "round";

    var pose = Rig.newPose();
    for (var i = 0; i < frameCount; i++) {
      g.save();
      g.translate(i * cw, 0);
      Rig.pose(clip, i / frameCount, pose);
      Rig.draw(g, style, pose, cw * 0.5, ch * footRatio, height, 1, options);
      g.restore();
    }

    return {
      canvas: canvas,
      frames: frameCount,
      cw: cw, ch: ch, ss: ss,
      footRatio: footRatio,
      height: height,
      worldHeight: options.worldHeight || 1
    };
  };

  /** Draws one baked frame with its feet at (x, y). */
  Rig.blit = function (g, atlas, frame, x, y, scale, facing) {
    var i = ((frame % atlas.frames) + atlas.frames) % atlas.frames;
    var w = atlas.cw * scale, h = atlas.ch * scale;
    var dx = x - w * 0.5;
    var dy = y - h * atlas.footRatio;

    if (facing < 0) {
      g.save();
      g.translate(x, 0);
      g.scale(-1, 1);
      g.drawImage(atlas.canvas,
        i * atlas.cw * atlas.ss, 0, atlas.cw * atlas.ss, atlas.ch * atlas.ss,
        -w * 0.5, dy, w, h);
      g.restore();
      return;
    }

    g.drawImage(atlas.canvas,
      i * atlas.cw * atlas.ss, 0, atlas.cw * atlas.ss, atlas.ch * atlas.ss,
      dx, dy, w, h);
  };

  /** Frame index for a looping clip at a given speed. */
  Rig.frameAt = function (atlas, time, cyclesPerSecond, offset) {
    return Math.floor(A.wrap((time * cyclesPerSecond + (offset || 0)), 1) * atlas.frames);
  };
})(window.A);
