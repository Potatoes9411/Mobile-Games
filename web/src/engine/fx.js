/* ===========================================================================
   ARCADE ENGINE - effects
   Particles, floating combat text, screen shake, hit stop and flashes.
   World-space and screen-space entries live in the same pools; games supply a
   projector when their world is not already in screen coordinates.
   =========================================================================== */
(function (A) {
  "use strict";

  var TAU = A.TAU;
  var CAP = 520;

  var Fx = {
    particles: [],
    texts: [],
    shake: 0,
    shakeTime: 0,
    shakeDuration: 0.28,
    flash: 0,
    flashColor: [255, 255, 255],
    timeScale: 1,
    _hitStop: 0
  };
  A.Fx = Fx;

  Fx.reset = function () {
    Fx.particles.length = 0;
    Fx.texts.length = 0;
    Fx.shake = 0;
    Fx.shakeTime = 0;
    Fx.flash = 0;
    Fx.timeScale = 1;
    Fx._hitStop = 0;
  };

  Fx.burst = function (x, y, z, count, opts) {
    opts = opts || {};
    for (var i = 0; i < count && Fx.particles.length < CAP; i++) {
      var a = opts.angle !== undefined
        ? opts.angle + (Math.random() - 0.5) * (opts.spread || 0.6)
        : Math.random() * TAU;
      var speed = (opts.speed || 6) * (0.35 + Math.random() * 0.9);

      Fx.particles.push({
        x: x, y: y, z: z || 0,
        vx: Math.cos(a) * speed,
        vy: (opts.up !== undefined ? opts.up : 7) * (0.4 + Math.random()),
        vz: Math.sin(a) * speed * (opts.flat ? 0 : 0.55),
        g: opts.gravity === undefined ? 22 : opts.gravity,
        life: (opts.life || 0.7) * (0.7 + Math.random() * 0.6),
        t: 0,
        size: (opts.size || 0.2) * (0.6 + Math.random() * 0.9),
        color: opts.color || [255, 194, 75],
        square: !!opts.square,
        bounce: opts.bounce !== false,
        screen: !!opts.screen
      });
    }
  };

  Fx.text = function (x, y, z, content, color, opts) {
    opts = opts || {};
    Fx.texts.push({
      x: x, y: y, z: z || 0,
      text: content,
      color: color || [255, 255, 255],
      t: 0,
      life: opts.life || 0.95,
      scale: opts.scale || 1,
      rise: opts.rise !== undefined ? opts.rise : 2.4,
      screen: !!opts.screen,
      drift: (Math.random() - 0.5) * (opts.drift || 0)
    });
  };

  Fx.kick = function (amount) {
    Fx.shake = Math.max(Fx.shake, amount);
    Fx.shakeTime = Fx.shakeDuration;
  };

  Fx.flashScreen = function (amount, color) {
    Fx.flash = Math.max(Fx.flash, amount);
    if (color) Fx.flashColor = color;
  };

  /** Brief slow motion. Sells a big impact without touching the frame loop. */
  Fx.hitStop = function (duration) {
    Fx._hitStop = Math.max(Fx._hitStop, duration);
  };

  Fx.update = function (dt) {
    if (Fx._hitStop > 0) {
      Fx._hitStop -= dt;
      Fx.timeScale = 0.25;
    } else {
      Fx.timeScale = 1;
    }

    if (Fx.shakeTime > 0) {
      Fx.shakeTime -= dt;
      if (Fx.shakeTime <= 0) Fx.shake = 0;
    }
    if (Fx.flash > 0) Fx.flash = Math.max(0, Fx.flash - dt * 2.6);

    for (var i = Fx.particles.length - 1; i >= 0; i--) {
      var p = Fx.particles[i];
      p.t += dt;
      if (p.t >= p.life) { Fx.particles.splice(i, 1); continue; }

      p.vy -= p.g * dt;
      p.x += p.vx * dt;
      p.y += p.vy * dt;
      p.z += p.vz * dt;

      if (p.bounce && p.y < 0) {
        p.y = 0;
        p.vy *= -0.35;
        p.vx *= 0.6;
        p.vz *= 0.6;
      }
    }

    for (var j = Fx.texts.length - 1; j >= 0; j--) {
      var t = Fx.texts[j];
      t.t += dt;
      if (t.t >= t.life) { Fx.texts.splice(j, 1); continue; }
      t.y += t.rise * dt;
      t.x += t.drift * dt;
    }
  };

  /**
   * Applies the shake transform. Games call this before drawing the world and
   * restore afterwards.
   */
  Fx.applyShake = function (g) {
    g.save();
    if (Fx.shake > 0) {
      var k = Fx.shake * A.clamp01(Fx.shakeTime / Fx.shakeDuration);
      g.translate((Math.random() - 0.5) * 22 * k, (Math.random() - 0.5) * 16 * k);
    }
  };

  /**
   * project(x, y, z) must return {x, y, s} in screen space, or null when the
   * point is behind the camera. Screen-space entries bypass it.
   */
  Fx.drawParticles = function (g, project) {
    for (var i = 0; i < Fx.particles.length; i++) {
      var p = Fx.particles[i];
      var sx, sy, scale;

      if (p.screen || !project) {
        sx = p.x; sy = -p.y; scale = 1;
        if (p.screen) sy = p.y;
      } else {
        var q = project(p.x, p.y, p.z);
        if (!q) continue;
        sx = q.x; sy = q.y; scale = q.s;
      }

      var k = 1 - p.t / p.life;
      var r = Math.max(1, p.size * scale * (0.45 + k * 0.75));

      g.globalAlpha = A.clamp01(k * 1.6);
      g.fillStyle = A.rgb(p.color);

      if (p.square) {
        g.save();
        g.translate(sx, sy);
        g.rotate(p.t * 9 + p.size * 10);
        g.fillRect(-r, -r * 0.7, r * 2, r * 1.4);
        g.restore();
      } else {
        g.beginPath();
        g.arc(sx, sy, r, 0, TAU);
        g.fill();
      }
    }
    g.globalAlpha = 1;
  };

  Fx.drawTexts = function (g, project, font) {
    g.textAlign = "center";
    g.textBaseline = "middle";
    g.lineJoin = "round";

    for (var i = 0; i < Fx.texts.length; i++) {
      var t = Fx.texts[i];
      var sx, sy, size;

      if (t.screen || !project) {
        sx = t.x; sy = t.y; size = 28 * t.scale;
      } else {
        var q = project(t.x, t.y, t.z);
        if (!q) continue;
        sx = q.x; sy = q.y;
        size = A.clamp(q.s * 0.85 * t.scale, 12, 56);
      }

      var k = A.clamp01(t.t / t.life);
      var alpha = 1 - Math.max(0, (k - 0.55) / 0.45);
      var pop = 1 + (1 - Math.min(1, k * 4)) * 0.35;

      g.globalAlpha = alpha;
      g.font = Math.round(size * pop) + "px " + (font || "'Titan One', 'Arial Black', sans-serif");
      g.lineWidth = Math.max(4, size * 0.26);
      g.strokeStyle = "rgba(26,20,40,0.95)";
      g.strokeText(t.text, sx, sy);
      g.fillStyle = A.rgb(t.color);
      g.fillText(t.text, sx, sy);
    }
    g.globalAlpha = 1;
  };

  Fx.drawFlash = function (g, w, h) {
    if (Fx.flash <= 0) return;
    g.globalAlpha = A.clamp01(Fx.flash * 0.55);
    g.fillStyle = A.rgb(Fx.flashColor);
    g.fillRect(0, 0, w, h);
    g.globalAlpha = 1;
  };
})(window.A);
