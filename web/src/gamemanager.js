/* ===========================================================================
   POCKET ARCADE - GameManager
   Owns the part of the hub that is easy to get wrong: loading a game's script
   on demand, handing it a scope it can register its resources with, and then
   tearing every one of those resources down when the player walks away.

   The teardown is the whole reason this file exists. A browser will hand out a
   limited number of live WebGL contexts - hot-swapping thirty games without
   disposing geometries, materials, textures, renderers, physics engines,
   event listeners and animation frames will exhaust them and take the tab with
   it. Every resource a game creates has to be reachable from its scope, and
   every scope is emptied on unload.
   =========================================================================== */
(function (A) {
  "use strict";

  var GM = {};
  A.GameManager = GM;

  var $ = function (id) { return document.getElementById(id); };

  var loading = {};        // id -> Promise, so a double click loads once
  var currentScope = null;

  /* ====================================================== script loading == */

  /** True once a game's own script has registered it with A.games. */
  GM.isLoaded = function (id) {
    for (var i = 0; i < A.games.length; i++) {
      if (A.games[i].id === id) return true;
    }
    return false;
  };

  GM.definition = function (id) {
    for (var i = 0; i < A.games.length; i++) {
      if (A.games[i].id === id) return A.games[i];
    }
    return null;
  };

  /**
   * Injects a manifest row's script and resolves once it has registered.
   *
   * The single-file build inlines every game, so by the time this runs the
   * definition already exists and nothing is fetched - which is what keeps the
   * bundle working from a file:// double click, where injecting a relative
   * script would fail silently.
   */
  GM.ensure = function (id) {
    if (GM.isLoaded(id)) return Promise.resolve(GM.definition(id));
    if (loading[id]) return loading[id];

    var row = A.manifestRow(id);
    if (!row) return Promise.reject(new Error("No manifest row for game: " + id));

    loading[id] = new Promise(function (resolve, reject) {
      var el = document.createElement("script");
      el.src = row.script;
      el.async = false;
      el.setAttribute("data-game", id);
      el.onload = function () {
        var def = GM.definition(id);
        if (def) resolve(def);
        else reject(new Error("Script loaded but did not register: " + id));
      };
      el.onerror = function () { reject(new Error("Failed to load " + row.script)); };
      document.head.appendChild(el);
    });

    /* A failed load must not poison the cache - the player may well have just
       lost connectivity for a moment and will try the tile again. */
    loading[id]["catch"](function () { delete loading[id]; });
    return loading[id];
  };

  /** Kicks off every manifest script. Tiles appear as each one registers. */
  GM.preloadAll = function (onEach) {
    var pending = 0;
    A.MANIFEST.forEach(function (row) {
      if (GM.isLoaded(row.id)) return;
      pending++;
      GM.ensure(row.id).then(function () {
        if (onEach) onEach(row.id);
      })["catch"](function (err) {
        if (window.console) console.warn("[arcade] " + err.message);
      });
    });
    return pending;
  };

  /** Manifest metadata for a loaded definition (genre tag, canonical title). */
  GM.meta = function (id) {
    return A.manifestRow(id) || { id: id, title: id, genre: "" };
  };

  /** Manifest order, so the grid does not reshuffle as scripts land. */
  GM.orderedDefinitions = function () {
    var out = [];
    for (var i = 0; i < A.MANIFEST.length; i++) {
      var def = GM.definition(A.MANIFEST[i].id);
      if (def) out.push(def);
    }
    /* Anything registered without a manifest row still shows, at the end. */
    for (var j = 0; j < A.games.length; j++) {
      if (!A.manifestRow(A.games[j].id)) out.push(A.games[j]);
    }
    return out;
  };

  /* ============================================================== scope === */

  /**
   * A game's resource scope. Anything registered here is released on unload,
   * in reverse order of registration so a renderer is disposed before the
   * scene it drew, and a listener is detached before the element it was on is
   * removed.
   */
  function Scope(id) {
    this.id = id;
    this.cleanups = [];
    this.frames = [];
    this.timers = [];
    this.controller = (typeof AbortController === "function") ? new AbortController() : null;
    this.disposed = false;
  }

  /** Runs `fn` on unload. Returns fn so it can be chained. */
  Scope.prototype.onCleanup = function (fn) {
    if (typeof fn === "function") this.cleanups.push(fn);
    return fn;
  };

  /**
   * Scoped addEventListener. Games must use this rather than the raw API:
   * a listener left behind by the previous game double-fires in the next one,
   * and that class of bug is invisible until two games disagree about a tap.
   */
  Scope.prototype.on = function (target, type, handler, options) {
    var opts = options || {};
    if (this.controller) {
      opts = Object.assign({}, opts, { signal: this.controller.signal });
      target.addEventListener(type, handler, opts);
    } else {
      /* No AbortController (old Safari): fall back to explicit removal. */
      target.addEventListener(type, handler, opts);
      this.cleanups.push(function () {
        target.removeEventListener(type, handler, opts);
      });
    }
    return handler;
  };

  /** Scoped requestAnimationFrame loop. Cancelled on unload. */
  Scope.prototype.raf = function (step) {
    var self = this;
    var id = 0;
    var tick = function (t) {
      if (self.disposed) return;
      id = requestAnimationFrame(tick);
      self.frames[self.frames.indexOf(id)] = id;
      step(t);
    };
    id = requestAnimationFrame(tick);
    this.frames.push(id);
    var slot = this.frames.length - 1;
    this.cleanups.push(function () { cancelAnimationFrame(self.frames[slot]); });
    return id;
  };

  Scope.prototype.timeout = function (fn, ms) {
    var id = setTimeout(fn, ms);
    this.timers.push(id);
    return id;
  };

  Scope.prototype.interval = function (fn, ms) {
    var id = setInterval(fn, ms);
    this.timers.push(id);
    return id;
  };

  /**
   * Registers a Three.js renderer (and optionally its scene) for disposal.
   * Disposing the renderer alone is not enough: geometries, materials and
   * textures each hold GPU memory that only their own dispose() releases.
   */
  Scope.prototype.three = function (renderer, scene) {
    var self = this;
    this.cleanups.push(function () { GM.disposeThree(renderer, scene); });
    if (renderer && renderer.domElement) {
      this.cleanups.push(function () {
        if (renderer.domElement.parentNode) {
          renderer.domElement.parentNode.removeChild(renderer.domElement);
        }
      });
    }
    return self;
  };

  /** Registers a Matter.js engine/runner/render trio for teardown. */
  Scope.prototype.matter = function (engine, runner, render) {
    this.cleanups.push(function () { GM.disposeMatter(engine, runner, render); });
    return this;
  };

  /** Registers a DOM node to remove on unload (an injected canvas, say). */
  Scope.prototype.node = function (el) {
    this.cleanups.push(function () {
      if (el && el.parentNode) el.parentNode.removeChild(el);
    });
    return el;
  };

  Scope.prototype.dispose = function () {
    if (this.disposed) return;
    this.disposed = true;

    var i;
    for (i = 0; i < this.frames.length; i++) cancelAnimationFrame(this.frames[i]);
    for (i = 0; i < this.timers.length; i++) {
      clearTimeout(this.timers[i]);
      clearInterval(this.timers[i]);
    }
    if (this.controller) {
      try { this.controller.abort(); } catch (e) { /* already aborted */ }
    }

    /* Reverse order, and one failure must not strand the rest. */
    for (i = this.cleanups.length - 1; i >= 0; i--) {
      try { this.cleanups[i](); } catch (e) {
        if (window.console) console.warn("[arcade] cleanup failed", e);
      }
    }

    this.cleanups.length = 0;
    this.frames.length = 0;
    this.timers.length = 0;
  };

  GM.Scope = Scope;

  /* ========================================================== disposers === */

  function disposeMaterial(material) {
    if (!material) return;
    /* Every texture-ish slot a standard material can carry. Walking the object
       generically also catches custom uniforms on shader materials. */
    for (var key in material) {
      var value = material[key];
      if (value && value.isTexture && typeof value.dispose === "function") {
        value.dispose();
      }
    }
    if (material.uniforms) {
      for (var u in material.uniforms) {
        var uv = material.uniforms[u] && material.uniforms[u].value;
        if (uv && uv.isTexture && typeof uv.dispose === "function") uv.dispose();
      }
    }
    if (typeof material.dispose === "function") material.dispose();
  }

  /**
   * Walks a Three.js scene graph and releases every GPU resource on it, then
   * drops the renderer and forces its context loss.
   */
  GM.disposeThree = function (renderer, scene) {
    if (scene && typeof scene.traverse === "function") {
      var doomed = [];
      scene.traverse(function (obj) {
        if (obj.geometry && typeof obj.geometry.dispose === "function") {
          obj.geometry.dispose();
        }
        if (obj.material) {
          if (Array.isArray(obj.material)) obj.material.forEach(disposeMaterial);
          else disposeMaterial(obj.material);
        }
        if (obj.isInstancedMesh && obj.instanceColor && obj.instanceColor.dispose) {
          obj.instanceColor.dispose();
        }
        if (obj !== scene) doomed.push(obj);
      });
      for (var i = doomed.length - 1; i >= 0; i--) {
        if (doomed[i].parent) doomed[i].parent.remove(doomed[i]);
      }
      if (scene.environment && scene.environment.dispose) scene.environment.dispose();
      if (scene.background && scene.background.isTexture && scene.background.dispose) {
        scene.background.dispose();
      }
      if (typeof scene.clear === "function") scene.clear();
    }

    if (!renderer) return;
    try {
      if (renderer.renderLists && renderer.renderLists.dispose) renderer.renderLists.dispose();
      if (renderer.info && renderer.info.programs) {
        /* Programs are released by dispose(); reading them first makes leaks
           visible in the console during development. */
        renderer.info.autoReset = true;
      }
      if (typeof renderer.dispose === "function") renderer.dispose();
      if (typeof renderer.forceContextLoss === "function") renderer.forceContextLoss();
      renderer.domElement = null;
    } catch (e) {
      if (window.console) console.warn("[arcade] renderer dispose failed", e);
    }
  };

  /** Stops and empties a Matter.js world. */
  GM.disposeMatter = function (engine, runner, render) {
    var M = window.Matter;
    if (!M) return;
    try {
      if (render && M.Render && M.Render.stop) {
        M.Render.stop(render);
        if (render.canvas && render.canvas.parentNode) {
          render.canvas.parentNode.removeChild(render.canvas);
        }
        render.canvas = null;
        render.context = null;
        render.textures = {};
      }
      if (runner && M.Runner && M.Runner.stop) M.Runner.stop(runner);
      if (engine) {
        if (M.Events && M.Events.off) M.Events.off(engine);
        if (engine.world && M.Composite && M.Composite.clear) {
          M.Composite.clear(engine.world, false, true);
        } else if (engine.world && M.World && M.World.clear) {
          M.World.clear(engine.world, false);
        }
        if (M.Engine && M.Engine.clear) M.Engine.clear(engine);
      }
    } catch (e) {
      if (window.console) console.warn("[arcade] matter teardown failed", e);
    }
  };

  /* ========================================================== lifecycle === */

  GM.currentScope = function () { return currentScope; };

  /** Fresh scope for a game about to mount. */
  GM.beginScope = function (id) {
    GM.endScope();
    currentScope = new Scope(id);
    return currentScope;
  };

  /** Tears the running game's scope down. Safe to call when nothing is running. */
  GM.endScope = function () {
    if (!currentScope) return;
    var scope = currentScope;
    currentScope = null;
    scope.dispose();
  };

  /**
   * Full unload. Beyond the scope, this resets the shared state a game may
   * have left dirty: the effects layer, the pointer state, and the 2D stage.
   */
  GM.unloadCurrentGame = function () {
    GM.endScope();

    A.Fx.reset();
    A.Input.reset();

    var stage = $("stage");
    if (stage) {
      var ctx = stage.getContext("2d");
      if (ctx) {
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.clearRect(0, 0, stage.width, stage.height);
        ctx.setTransform(A.View.dpr, 0, 0, A.View.dpr, 0, 0);
      }
    }

    /* Anything a game injected next to the stage. Games that use the scope's
       node()/three() helpers are already covered; this is the safety net for
       one that appended a canvas and forgot. */
    var layer = $("gameLayer");
    if (layer) {
      var extra = layer.querySelectorAll("canvas:not(#stage)");
      for (var i = 0; i < extra.length; i++) {
        if (extra[i].parentNode) extra[i].parentNode.removeChild(extra[i]);
      }
    }
  };

  /* =========================================================== scoring === */

  /**
   * Records a run's score against the game's personal best. Returns true when
   * it is a new record, so the caller can flash the banner.
   */
  GM.reportScore = function (id, score) {
    if (typeof score !== "number" || !isFinite(score)) return false;
    var slot = A.Save.game(id, {});
    var rounded = Math.round(score);
    if (slot.hiScore === undefined) slot.hiScore = 0;
    if (rounded <= slot.hiScore) return false;
    slot.hiScore = rounded;
    A.Save.write();
    return true;
  };

  GM.hiScore = function (id) {
    var slot = A.Save.game(id, {});
    return slot.hiScore || 0;
  };

  /* ========================================================== settings === */

  var SETTINGS_DEFAULTS = { volume: 0.8, quality: "high", haptics: true };

  GM.settings = function () {
    var d = A.Save.data;
    if (!d.settings) d.settings = {};
    for (var key in SETTINGS_DEFAULTS) {
      if (d.settings[key] === undefined) d.settings[key] = SETTINGS_DEFAULTS[key];
    }
    return d.settings;
  };

  /** Pushes saved settings into the subsystems that read them. */
  GM.applySettings = function () {
    var s = GM.settings();
    A.Audio.volume = A.Save.data.soundEnabled === false ? 0 : A.clamp01(s.volume);
    A.View.qualityCap = s.quality === "low" ? 1 : (s.quality === "medium" ? 1.5 : 2);
    A.View.resize();
    A.hapticsEnabled = !!s.haptics;
  };

  GM.setSetting = function (key, value) {
    var s = GM.settings();
    s[key] = value;
    A.Save.write();
    GM.applySettings();
  };

  /* ============================================================= boot ===== */

  GM.boot = function (onGameReady) {
    GM.applySettings();
    return GM.preloadAll(onGameReady);
  };
})(window.A);
