/* ===========================================================================
   ARCADE ENGINE - core
   Shared math, deterministic RNG, persistent save, audio, input and the frame
   loop. Every game in the hub is built on this. Classic script, no modules, so
   the whole thing still runs from a file:// double-click.
   =========================================================================== */
window.A = window.A || {};

(function (A) {
  "use strict";

  /* ------------------------------------------------------------- math ---- */
  A.TAU = Math.PI * 2;
  A.clamp = function (v, a, b) { return v < a ? a : (v > b ? b : v); };
  A.clamp01 = function (v) { return A.clamp(v, 0, 1); };
  A.lerp = function (a, b, t) { return a + (b - a) * t; };
  A.invLerp = function (a, b, v) { return a === b ? 0 : A.clamp01((v - a) / (b - a)); };
  A.smooth = function (t) { return t * t * (3 - 2 * t); };
  A.approach = function (current, target, rate, dt) {
    return current + (target - current) * (1 - Math.exp(-rate * dt));
  };
  A.wrap = function (v, m) { return ((v % m) + m) % m; };
  A.dist2 = function (ax, ay, bx, by) {
    var dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
  };

  /** Unity's SmoothDamp, scalar. Returns [value, velocity]. */
  A.smoothDamp = function (current, target, velocity, smoothTime, maxSpeed, dt) {
    smoothTime = Math.max(0.0001, smoothTime);
    var omega = 2 / smoothTime;
    var x = omega * dt;
    var exp = 1 / (1 + x + 0.48 * x * x + 0.235 * x * x * x);
    var change = A.clamp(current - target, -maxSpeed * smoothTime, maxSpeed * smoothTime);
    var temp = (velocity + omega * change) * dt;
    var newVel = (velocity - omega * temp) * exp;
    var output = (current - change) + (change + temp) * exp;
    if ((target - current > 0) === (output > target)) {
      output = target;
      newVel = (output - target) / dt;
    }
    return [output, newVel];
  };

  /** Deterministic RNG. Same seed, same world, every time. */
  A.rng = function (seed) {
    var a = (seed >>> 0) || 1;
    var next = function () {
      a = (a + 0x6D2B79F5) | 0;
      var t = Math.imul(a ^ (a >>> 15), 1 | a);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
    next.range = function (lo, hi) { return lo + next() * (hi - lo); };
    next.int = function (lo, hi) { return Math.floor(next.range(lo, hi + 1)); };
    next.chance = function (p) { return next() < p; };
    next.pick = function (list) { return list[Math.floor(next() * list.length) % list.length]; };
    next.shuffle = function (list) {
      for (var i = list.length - 1; i > 0; i--) {
        var j = Math.floor(next() * (i + 1));
        var t = list[i]; list[i] = list[j]; list[j] = t;
      }
      return list;
    };
    return next;
  };

  /* ------------------------------------------------------------ colour --- */
  A.rgb = function (c) { return "rgb(" + (c[0] | 0) + "," + (c[1] | 0) + "," + (c[2] | 0) + ")"; };
  A.rgba = function (c, alpha) {
    return "rgba(" + (c[0] | 0) + "," + (c[1] | 0) + "," + (c[2] | 0) + "," + alpha + ")";
  };
  A.mix = function (a, b, t) {
    return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
  };
  A.shade = function (c, amount) {
    return amount < 0 ? A.mix(c, [0, 0, 0], -amount) : A.mix(c, [255, 255, 255], amount);
  };
  A.hex = function (value) {
    return [(value >> 16) & 255, (value >> 8) & 255, value & 255];
  };
  A.toHex = function (c) {
    var s = "#";
    for (var i = 0; i < 3; i++) s += ("0" + Math.round(A.clamp(c[i], 0, 255)).toString(16)).slice(-2);
    return s;
  };
  /** HSL in 0..1 to an rgb triplet. Used to generate character palettes. */
  A.hsl = function (h, s, l) {
    h = A.wrap(h, 1);
    var q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    var p = 2 * l - q;
    var f = function (t) {
      t = A.wrap(t, 1);
      if (t < 1 / 6) return p + (q - p) * 6 * t;
      if (t < 1 / 2) return q;
      if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
      return p;
    };
    return [f(h + 1 / 3) * 255, f(h) * 255, f(h - 1 / 3) * 255];
  };

  A.formatNumber = function (n) {
    n = Math.round(n);
    if (n < 10000) return n.toLocaleString();
    if (n < 1000000) return (n / 1000).toFixed(n < 100000 ? 1 : 0) + "K";
    if (n < 1000000000) return (n / 1000000).toFixed(1) + "M";
    return (n / 1000000000).toFixed(2) + "B";
  };

  A.formatTime = function (seconds) {
    seconds = Math.max(0, Math.floor(seconds));
    var m = Math.floor(seconds / 60), s = seconds % 60;
    return m + ":" + (s < 10 ? "0" : "") + s;
  };

  /* -------------------------------------------------------------- save --- */
  var SAVE_KEY = "arcade.save.v1";

  A.Save = {
    data: null,

    defaults: function () {
      return {
        version: 1,
        gems: 0,
        accountXp: 0,
        accountLevel: 1,
        soundEnabled: true,
        lastDay: "",
        streak: 0,
        bestStreak: 0,
        missions: [],
        missionDay: "",
        dailyBonusDay: "",
        totalRuns: 0,
        games: {}
      };
    },

    load: function () {
      var parsed = null;
      try {
        var raw = localStorage.getItem(SAVE_KEY);
        if (raw) parsed = JSON.parse(raw);
      } catch (e) { parsed = null; }

      A.Save.data = Object.assign(A.Save.defaults(), parsed || {});
      if (!A.Save.data.games) A.Save.data.games = {};
      return A.Save.data;
    },

    write: function () {
      try { localStorage.setItem(SAVE_KEY, JSON.stringify(A.Save.data)); } catch (e) { /* private mode */ }
    },

    /** Per-game save slot, created from a template on first access. */
    game: function (id, template) {
      var games = A.Save.data.games;
      if (!games[id]) games[id] = {};
      if (template) {
        for (var key in template) {
          if (games[id][key] === undefined) games[id][key] = template[key];
        }
      }
      return games[id];
    },

    reset: function () {
      A.Save.data = A.Save.defaults();
      A.Save.write();
    }
  };

  /* ------------------------------------------------------------- audio --- */
  A.Audio = {
    ctx: null,

    resume: function () {
      if (!A.Audio.ctx) {
        try {
          A.Audio.ctx = new (window.AudioContext || window.webkitAudioContext)();
        } catch (e) { A.Audio.ctx = null; }
      }
      if (A.Audio.ctx && A.Audio.ctx.state === "suspended") A.Audio.ctx.resume();
      return A.Audio.ctx;
    },

    tone: function (from, to, duration, type, gain) {
      var ctx = A.Audio.ctx;
      if (!ctx || !A.Save.data || !A.Save.data.soundEnabled) return;

      var osc = ctx.createOscillator();
      var amp = ctx.createGain();
      osc.type = type || "triangle";
      osc.frequency.setValueAtTime(Math.max(30, from), ctx.currentTime);
      osc.frequency.exponentialRampToValueAtTime(Math.max(30, to), ctx.currentTime + duration);
      amp.gain.setValueAtTime(0.0001, ctx.currentTime);
      amp.gain.exponentialRampToValueAtTime(gain || 0.14, ctx.currentTime + 0.012);
      amp.gain.exponentialRampToValueAtTime(0.0001, ctx.currentTime + duration);
      osc.connect(amp).connect(ctx.destination);
      osc.start();
      osc.stop(ctx.currentTime + duration + 0.02);
    },

    noise: function (duration, gain) {
      var ctx = A.Audio.ctx;
      if (!ctx || !A.Save.data || !A.Save.data.soundEnabled) return;

      var count = Math.floor(ctx.sampleRate * duration);
      var buffer = ctx.createBuffer(1, count, ctx.sampleRate);
      var channel = buffer.getChannelData(0);
      var low = 0;
      for (var i = 0; i < count; i++) {
        low = low * 0.72 + (Math.random() * 2 - 1) * 0.28;
        channel[i] = low * Math.pow(1 - i / count, 2.2);
      }

      var source = ctx.createBufferSource();
      var amp = ctx.createGain();
      amp.gain.value = gain || 0.2;
      source.buffer = buffer;
      source.connect(amp).connect(ctx.destination);
      source.start();
    },

    sfx: function (name) {
      switch (name) {
        case "good":    A.Audio.tone(520, 1180, 0.16, "triangle", 0.14); break;
        case "bad":     A.Audio.tone(420, 150, 0.22, "sawtooth", 0.12); break;
        case "hit":     A.Audio.noise(0.14, 0.20); break;
        case "shoot":   A.Audio.tone(880, 320, 0.07, "square", 0.06); break;
        case "coin":    A.Audio.tone(880, 1760, 0.10, "triangle", 0.11); break;
        case "gem":     A.Audio.tone(1046, 1568, 0.16, "triangle", 0.12); break;
        case "select":  A.Audio.tone(660, 720, 0.06, "sine", 0.09); break;
        case "levelup": [523, 784, 1046].forEach(function (f, i) {
                          setTimeout(function () { A.Audio.tone(f, f, 0.13, "triangle", 0.12); }, i * 80);
                        }); break;
        case "win":     [523, 659, 784, 1047].forEach(function (f, i) {
                          setTimeout(function () { A.Audio.tone(f, f, 0.13, "triangle", 0.12); }, i * 95);
                        }); break;
        case "lose":    [440, 349, 262].forEach(function (f, i) {
                          setTimeout(function () { A.Audio.tone(f, f, 0.17, "sawtooth", 0.10); }, i * 130);
                        }); break;
        case "jump":    A.Audio.tone(320, 620, 0.10, "triangle", 0.10); break;
        case "slide":   A.Audio.noise(0.20, 0.10); break;
      }
    }
  };

  A.vibrate = function (pattern) {
    if (!navigator.vibrate) return;
    try { navigator.vibrate(pattern); } catch (e) { /* blocked */ }
  };

  /* ------------------------------------------------------------- input --- */
  A.Input = {
    down: false,
    pressed: false,
    released: false,
    x: 0, y: 0,
    dx: 0, dy: 0,
    startX: 0, startY: 0,
    startTime: 0,
    swipe: "",
    tapped: false,
    keys: {},
    axis: 0,

    _lastX: 0, _lastY: 0, _downThisFrame: false, _upThisFrame: false, _swipeThisFrame: "",

    attach: function (element) {
      var self = A.Input;

      element.addEventListener("pointerdown", function (e) {
        A.Audio.resume();
        element.setPointerCapture && element.setPointerCapture(e.pointerId);
        self.down = true;
        self._downThisFrame = true;
        self.x = self._lastX = self.startX = e.clientX;
        self.y = self._lastY = self.startY = e.clientY;
        self.startTime = performance.now();
      });

      element.addEventListener("pointermove", function (e) {
        self.x = e.clientX;
        self.y = e.clientY;
      });

      var up = function (e) {
        if (!self.down) return;
        self.down = false;
        self._upThisFrame = true;

        var dx = self.x - self.startX;
        var dy = self.y - self.startY;
        var elapsed = performance.now() - self.startTime;
        var far = Math.abs(dx) > 42 || Math.abs(dy) > 42;

        if (far && elapsed < 500) {
          if (Math.abs(dx) > Math.abs(dy)) self._swipeThisFrame = dx > 0 ? "right" : "left";
          else self._swipeThisFrame = dy > 0 ? "down" : "up";
        } else if (!far && elapsed < 350) {
          self._swipeThisFrame = "tap";
        }
      };

      element.addEventListener("pointerup", up);
      element.addEventListener("pointercancel", up);

      window.addEventListener("keydown", function (e) {
        self.keys[e.key.toLowerCase()] = true;
        if ([" ", "arrowup", "arrowdown", "arrowleft", "arrowright"].indexOf(e.key.toLowerCase()) >= 0) {
          e.preventDefault();
        }
        A.Audio.resume();
      });

      window.addEventListener("keyup", function (e) {
        self.keys[e.key.toLowerCase()] = false;
      });
    },

    /** Called once per frame by the loop, before any game update. */
    beginFrame: function () {
      var self = A.Input;

      self.dx = self.down ? (self.x - self._lastX) : 0;
      self.dy = self.down ? (self.y - self._lastY) : 0;
      self._lastX = self.x;
      self._lastY = self.y;

      self.pressed = self._downThisFrame;
      self.released = self._upThisFrame;
      self.swipe = self._swipeThisFrame;
      self.tapped = self._swipeThisFrame === "tap";

      self._downThisFrame = false;
      self._upThisFrame = false;
      self._swipeThisFrame = "";

      var left = self.keys["a"] || self.keys["arrowleft"];
      var right = self.keys["d"] || self.keys["arrowright"];
      self.axis = (right ? 1 : 0) - (left ? 1 : 0);

      if (self.keys["arrowup"] || self.keys["w"]) self.swipe = self.swipe || "up";
      if (self.keys["arrowdown"] || self.keys["s"]) self.swipe = self.swipe || "down";
      if (self.keys[" "]) self.tapped = true;
    },

    consumeSwipe: function () {
      var s = A.Input.swipe;
      A.Input.swipe = "";
      return s;
    },

    keyPressed: function (key) {
      return !!A.Input.keys[key];
    }
  };

  /* -------------------------------------------------------------- loop --- */
  A.Loop = {
    running: false,
    time: 0,
    dt: 0,
    fps: 60,
    _accum: 0,
    _frames: 0,
    _last: 0,
    onFrame: null,

    start: function (callback) {
      A.Loop.onFrame = callback;
      A.Loop.running = true;
      A.Loop._last = performance.now();
      requestAnimationFrame(A.Loop._step);
    },

    _step: function (now) {
      var dt = Math.min(0.05, (now - A.Loop._last) / 1000);
      A.Loop._last = now;
      A.Loop.dt = dt;
      A.Loop.time += dt;

      A.Loop._accum += dt;
      A.Loop._frames++;
      if (A.Loop._accum >= 0.5) {
        A.Loop.fps = Math.round(A.Loop._frames / A.Loop._accum);
        A.Loop._accum = 0;
        A.Loop._frames = 0;
      }

      A.Input.beginFrame();
      if (A.Loop.onFrame) A.Loop.onFrame(dt);

      requestAnimationFrame(A.Loop._step);
    }
  };

  /* ------------------------------------------------------------ canvas --- */
  A.View = {
    canvas: null,
    ctx: null,
    w: 0, h: 0, dpr: 1,

    attach: function (canvas) {
      A.View.canvas = canvas;
      A.View.ctx = canvas.getContext("2d");
      A.View.resize();
      window.addEventListener("resize", A.View.resize);
      window.addEventListener("orientationchange", A.View.resize);
    },

    resize: function () {
      var v = A.View;
      v.dpr = Math.min(2, window.devicePixelRatio || 1);
      v.w = Math.max(280, window.innerWidth);
      v.h = Math.max(420, window.innerHeight);
      v.canvas.width = Math.round(v.w * v.dpr);
      v.canvas.height = Math.round(v.h * v.dpr);
      v.ctx.setTransform(v.dpr, 0, 0, v.dpr, 0, 0);
      if (A.View.onResize) A.View.onResize();
    },

    /** Shorter screen dimension, the honest basis for UI scale. */
    unit: function () { return Math.min(A.View.w, A.View.h); }
  };

  A.newCanvas = function (w, h) {
    var canvas = document.createElement("canvas");
    canvas.width = Math.max(1, Math.ceil(w));
    canvas.height = Math.max(1, Math.ceil(h));
    return canvas;
  };

  A.roundRect = function (g, x, y, w, h, r) {
    if (g.roundRect) { g.beginPath(); g.roundRect(x, y, w, h, r); return; }
    r = Math.min(r, Math.abs(w) / 2, Math.abs(h) / 2);
    g.beginPath();
    g.moveTo(x + r, y);
    g.arcTo(x + w, y, x + w, y + h, r);
    g.arcTo(x + w, y + h, x, y + h, r);
    g.arcTo(x, y + h, x, y, r);
    g.arcTo(x, y, x + w, y, r);
    g.closePath();
  };
})(window.A);
