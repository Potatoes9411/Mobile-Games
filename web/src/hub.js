/* ===========================================================================
   POCKET ARCADE - hub
   Owns everything outside the games: account level, gems, daily missions and
   streak, the prestige shop, the modal stack, and the lifecycle that swaps one
   game's HUD and update loop for another's.
   =========================================================================== */
(function (A) {
  "use strict";

  var Hub = {};
  A.Hub = Hub;

  var $ = function (id) { return document.getElementById(id); };
  var active = null;      // running game instance
  var activeDef = null;
  var homeVisible = true;
  var avatarStyle = null;
  var avatarCtx = null;
  var tileCanvases = [];

  /* ------------------------------------------------------ progression --- */
  Hub.xpForLevel = function (level) { return Math.round(110 * Math.pow(level, 1.32)); };

  Hub.addXp = function (amount) {
    var d = A.Save.data;
    d.accountXp += Math.max(0, Math.round(amount));

    var levelled = 0;
    while (d.accountXp >= Hub.xpForLevel(d.accountLevel)) {
      d.accountXp -= Hub.xpForLevel(d.accountLevel);
      d.accountLevel++;
      levelled++;
      d.gems += 5;
    }

    if (levelled > 0) {
      Hub.toast("ACCOUNT LEVEL " + d.accountLevel + "  +" + (levelled * 5) + " gems");
      A.Audio.sfx("levelup");
    }
    A.Save.write();
    Hub.paintAccount();
    return levelled;
  };

  Hub.addGems = function (amount) {
    A.Save.data.gems += Math.max(0, Math.round(amount));
    A.Save.write();
    Hub.paintAccount();
  };

  Hub.spendGems = function (amount) {
    if (A.Save.data.gems < amount) return false;
    A.Save.data.gems -= amount;
    A.Save.write();
    Hub.paintAccount();
    return true;
  };

  /** Permanent multipliers bought with gems, applied by every game. */
  Hub.prestige = function () {
    var d = A.Save.data;
    if (!d.prestige) {
      d.prestige = { coin: 0, xp: 0, revive: 0, hue: 0.58 };
    }
    return d.prestige;
  };

  Hub.coinMultiplier = function () { return 1 + Hub.prestige().coin * 0.12; };
  Hub.xpMultiplier = function () { return 1 + Hub.prestige().xp * 0.15; };
  Hub.reviveCharges = function () { return Hub.prestige().revive; };

  /* --------------------------------------------------------- missions --- */
  var MISSION_POOL = [
    { key: "runs", type: "run", lo: 3, hi: 5, text: function (n) { return "Play " + n + " runs"; } },
    { key: "coins", type: "coins", lo: 500, hi: 1200, text: function (n) { return "Collect " + n + " coins"; } },
    { key: "kills", type: "kill", lo: 150, hi: 320, text: function (n) { return "Defeat " + n + " enemies"; } },
    { key: "wave", type: "wave", best: true, lo: 6, hi: 10, text: function (n) { return "Reach wave " + n + " in Horde Arena"; } },
    { key: "dist", type: "distance", best: true, lo: 900, hi: 1800, text: function (n) { return "Run " + n + " m in Rooftop Run"; } },
    { key: "tower", type: "tower", lo: 2, hi: 4, text: function (n) { return "Storm " + n + " towers in Mob Clash"; } },
    { key: "draft", type: "draft", lo: 8, hi: 16, text: function (n) { return "Take " + n + " level-up upgrades"; } },
    { key: "crowd", type: "crowd", best: true, lo: 400, hi: 900, text: function (n) { return "Reach a mob of " + n; } }
  ];

  function today() {
    var d = new Date();
    return d.getFullYear() + "-" + (d.getMonth() + 1) + "-" + d.getDate();
  }

  Hub.refreshDaily = function () {
    var d = A.Save.data;
    var day = today();
    if (d.missionDay === day && d.missions && d.missions.length) return;

    var seed = 0;
    for (var i = 0; i < day.length; i++) seed = (seed * 31 + day.charCodeAt(i)) | 0;
    var rand = A.rng(Math.abs(seed) + 7);

    var pool = MISSION_POOL.slice();
    rand.shuffle(pool);

    d.missions = pool.slice(0, 3).map(function (m) {
      var target = Math.round(rand.range(m.lo, m.hi) / (m.hi > 100 ? 10 : 1)) * (m.hi > 100 ? 10 : 1);
      return {
        key: m.key, type: m.type, best: !!m.best,
        target: target, progress: 0, claimed: false,
        label: m.text(A.formatNumber(target)),
        reward: 8 + Math.round(rand.range(0, 12))
      };
    });
    d.missionDay = day;

    // Daily streak: consecutive days with at least one launch.
    if (d.lastDay !== day) {
      var yesterday = new Date();
      yesterday.setDate(yesterday.getDate() - 1);
      var yKey = yesterday.getFullYear() + "-" + (yesterday.getMonth() + 1) + "-" + yesterday.getDate();
      d.streak = (d.lastDay === yKey) ? d.streak + 1 : 1;
      d.bestStreak = Math.max(d.bestStreak || 0, d.streak);
      d.lastDay = day;
      d.gems += 3 + Math.min(7, d.streak);
      setTimeout(function () {
        Hub.toast("DAILY BONUS  +" + (3 + Math.min(7, d.streak)) + " gems  (streak " + d.streak + ")");
      }, 600);
    }

    A.Save.write();
  };

  /** Games report progress here; the hub decides what counts toward what. */
  Hub.progress = function (type, amount) {
    var missions = A.Save.data.missions || [];
    var changed = false;

    for (var i = 0; i < missions.length; i++) {
      var m = missions[i];
      if (m.type !== type || m.claimed) continue;

      m.progress = m.best ? Math.max(m.progress, amount) : m.progress + amount;

      if (m.progress >= m.target) {
        m.claimed = true;
        A.Save.data.gems += m.reward;
        Hub.toast("MISSION COMPLETE  +" + m.reward + " gems");
        A.Audio.sfx("gem");
      }
      changed = true;
    }

    if (changed) {
      A.Save.write();
      if (homeVisible) Hub.paintMissions();
      Hub.paintAccount();
    }
  };

  /* ------------------------------------------------------------ toasts -- */
  Hub.toast = function (text) {
    var node = document.createElement("div");
    node.className = "toast";
    node.textContent = text;
    $("toasts").appendChild(node);
    setTimeout(function () { node.remove(); }, 2400);
  };

  /* ------------------------------------------------------------ modal --- */
  Hub.modal = {
    show: function (node) {
      var host = $("modal");
      host.innerHTML = "";
      host.appendChild(node);
      host.hidden = false;
    },
    hide: function () {
      var host = $("modal");
      host.hidden = true;
      host.innerHTML = "";
    },
    visible: function () { return !$("modal").hidden; }
  };

  Hub.el = function (tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };

  Hub.button = function (label, className, onClick, sub) {
    var b = Hub.el("button", "btn " + (className || ""));
    b.textContent = label;
    if (sub) {
      var s = Hub.el("small", null, sub);
      b.appendChild(s);
    }
    b.addEventListener("click", function () {
      A.Audio.sfx("select");
      onClick();
    });
    return b;
  };

  /**
   * Standard end-of-run card. Games hand over their own stat lines.
   *
   * Two shapes are accepted because the arcade grew two conventions: the
   * original positional `stats: [[label, value]]` / `buttons: []`, and the
   * later object form `rows: [{label, value}]` / `actions: []`. Normalising
   * here is cheaper - and far less error prone - than rewriting every caller,
   * and a game that passes the wrong one silently rendered an empty card.
   */
  Hub.results = function (opts) {
    var card = Hub.el("div", "card");
    var title = Hub.el("h2", null, opts.title);
    title.style.color = opts.win ? "var(--jade)" : "var(--rose)";
    card.appendChild(title);
    card.appendChild(Hub.el("p", "sub", opts.subtitle || ""));

    /* A run that beat the stored personal best gets its own banner. */
    if (activeDef && typeof opts.score === "number") {
      if (A.GameManager.reportScore(activeDef.id, opts.score)) {
        var flash = Hub.el("p", "sub", "NEW RECORD");
        flash.style.color = "var(--gold)";
        flash.style.letterSpacing = ".28em";
        card.appendChild(flash);
      }
    }

    var lines = [];
    (opts.stats || []).forEach(function (s) {
      lines.push({ label: s[0], value: s[1] });
    });
    (opts.rows || []).forEach(function (r) {
      lines.push({ label: r.label, value: r.value });
    });

    if (lines.length) {
      var stats = Hub.el("div", "stats");
      lines.forEach(function (line) {
        var box = Hub.el("div", "stat");
        box.appendChild(Hub.el("b", null, String(line.value)));
        box.appendChild(Hub.el("span", null, line.label));
        stats.appendChild(box);
      });
      card.appendChild(stats);
    }

    var buttons = (opts.buttons || []).concat(opts.actions || []);
    buttons.forEach(function (b) {
      card.appendChild(Hub.button(b.label, b.className, b.onClick, b.sub));
    });

    Hub.modal.show(card);
    return card;
  };

  /* ------------------------------------------------------ home screen --- */
  Hub.paintAccount = function () {
    var d = A.Save.data;
    var need = Hub.xpForLevel(d.accountLevel);
    $("acctLevel").textContent = "LV " + d.accountLevel;
    $("acctXp").textContent = A.formatNumber(d.accountXp) + " / " + A.formatNumber(need) + " XP";
    $("acctBar").style.width = A.clamp01(d.accountXp / need) * 100 + "%";
    $("gemCount").textContent = A.formatNumber(d.gems);
  };

  Hub.paintMissions = function () {
    var host = $("missions");
    host.innerHTML = "";

    (A.Save.data.missions || []).forEach(function (m) {
      var row = Hub.el("div", "mission" + (m.claimed ? " done" : ""));
      var left = Hub.el("div");
      left.appendChild(Hub.el("h4", null, m.label));
      var bar = Hub.el("div", "bar");
      var fill = Hub.el("i");
      fill.style.width = A.clamp01(m.progress / m.target) * 100 + "%";
      bar.appendChild(fill);
      left.appendChild(bar);
      row.appendChild(left);

      var reward = Hub.el("div", "rw");
      reward.appendChild(Hub.el("b"));
      reward.appendChild(Hub.el("span", null, m.claimed ? "DONE" : String(m.reward)));
      row.appendChild(reward);

      host.appendChild(row);
    });

    var streak = $("streak");
    streak.innerHTML = "";
    for (var i = 0; i < 7; i++) {
      var pip = Hub.el("i");
      if (i < Math.min(7, A.Save.data.streak || 0)) pip.className = "on";
      streak.appendChild(pip);
    }
  };

  Hub.paintTiles = function () {
    var host = $("tiles");
    host.innerHTML = "";
    tileCanvases = [];

    A.GameManager.orderedDefinitions().forEach(function (def) {
      var unlocked = A.Save.data.accountLevel >= (def.unlock || 1);
      var meta = A.GameManager.meta(def.id);
      var tile = Hub.el("button", "tile" + (unlocked ? "" : " locked"));
      tile.style.setProperty("--accent", def.accent);

      var canvas = document.createElement("canvas");
      canvas.width = 320; canvas.height = 220;
      tile.appendChild(canvas);
      var tileCtx = canvas.getContext("2d");
      tileCtx.scale(2, 2);
      tileCanvases.push({ def: def, ctx: tileCtx, w: 160, h: 110 });

      var slot = A.Save.game(def.id, def.template || {});
      var fresh = !slot.runs;

      var flag = Hub.el("div", "flag", unlocked ? (fresh ? "NEW" : "PLAY") : "LV " + def.unlock);
      if (unlocked && !fresh) flag.style.background = def.accent;
      tile.appendChild(flag);

      var body = Hub.el("div", "body");
      body.appendChild(Hub.el("h3", null, def.name));
      if (meta.genre) {
        var genre = Hub.el("div", "genre", meta.genre);
        genre.style.color = def.accent;
        body.appendChild(genre);
      }
      body.appendChild(Hub.el("p", null, def.tagline));

      if (unlocked && def.bestLine) {
        var line = def.bestLine(slot);
        var hi = A.GameManager.hiScore(def.id);
        if (hi > 0) line += "  ·  HI " + A.formatNumber(hi);
        body.appendChild(Hub.el("div", "best", line));
      } else if (!unlocked) {
        body.appendChild(Hub.el("div", "best", "Reach account level " + def.unlock));
      }
      tile.appendChild(body);

      if (unlocked) {
        tile.addEventListener("click", function () {
          A.Audio.resume();
          A.Audio.sfx("select");
          Hub.launch(def);
        });
      }

      host.appendChild(tile);
    });
  };

  /* ------------------------------------------------------- prestige ----- */
  Hub.openShop = function () {
    var p = Hub.prestige();
    var card = Hub.el("div", "card");
    card.appendChild(Hub.el("h2", null, "PRESTIGE"));
    card.appendChild(Hub.el("p", "sub", "Permanent, across every game"));

    var rows = [
      { key: "coin", name: "Coin Rush", desc: "+12% coins in every game", max: 12,
        cost: function (l) { return 20 + l * 14; } },
      { key: "xp", name: "Fast Learner", desc: "+15% account XP from runs", max: 10,
        cost: function (l) { return 25 + l * 18; } },
      { key: "revive", name: "Second Wind", desc: "Carry one extra revive into a run", max: 3,
        cost: function (l) { return 60 + l * 70; } }
    ];

    rows.forEach(function (r) {
      var row = Hub.el("div", "shopRow");
      var left = Hub.el("div");
      left.appendChild(Hub.el("h4", null, r.name + "  ·  LV " + p[r.key]));
      left.appendChild(Hub.el("p", null, r.desc));
      row.appendChild(left);

      var maxed = p[r.key] >= r.max;
      var cost = r.cost(p[r.key]);
      var buy = Hub.el("button", "buy");
      buy.innerHTML = maxed ? "MAX" : cost + "<small>GEMS</small>";
      buy.disabled = maxed || A.Save.data.gems < cost;
      buy.addEventListener("click", function () {
        if (Hub.spendGems(cost)) {
          p[r.key]++;
          A.Save.write();
          A.Audio.sfx("gem");
          Hub.openShop();
        }
      });
      row.appendChild(buy);
      card.appendChild(row);
    });

    var skinRow = Hub.el("div", "shopRow");
    var skinLeft = Hub.el("div");
    skinLeft.appendChild(Hub.el("h4", null, "Recruit colours"));
    skinLeft.appendChild(Hub.el("p", null, "Reroll your squad's palette"));
    skinRow.appendChild(skinLeft);

    var skinBuy = Hub.el("button", "buy");
    skinBuy.innerHTML = "15<small>GEMS</small>";
    skinBuy.disabled = A.Save.data.gems < 15;
    skinBuy.addEventListener("click", function () {
      if (Hub.spendGems(15)) {
        p.hue = Math.random();
        A.Save.write();
        Hub.buildAvatar();
        A.Audio.sfx("gem");
        Hub.openShop();
      }
    });
    skinRow.appendChild(skinBuy);
    card.appendChild(skinRow);

    card.appendChild(Hub.button("BACK", "ghost", Hub.modal.hide));
    Hub.modal.show(card);
  };

  /* -------------------------------------------------------- lifecycle --- */
  /** Loads a game by id if needed, then launches it. */
  Hub.launchById = function (id) {
    var known = A.GameManager.definition(id);
    if (known) { Hub.launch(known); return Promise.resolve(known); }

    Hub.toast("LOADING...");
    return A.GameManager.ensure(id).then(function (def) {
      Hub.paintTiles();
      Hub.launch(def);
      return def;
    })["catch"](function (err) {
      Hub.toast("COULD NOT LOAD THAT GAME");
      if (window.console) console.warn(err);
    });
  };

  Hub.launch = function (def) {
    if (active) Hub.exit();

    activeDef = def;
    /* Fresh resource scope. Everything the game registers on it - listeners,
       frames, renderers, physics worlds - is released when we unload. */
    var scope = A.GameManager.beginScope(def.id);
    homeVisible = false;
    $("home").hidden = true;
    $("gameUi").hidden = false;
    $("gameUi").innerHTML = "";

    var host = {
      id: def.id,
      accent: def.accent,
      ui: $("gameUi"),
      save: A.Save.game(def.id, def.template || {}),
      exit: Hub.exit,
      toast: Hub.toast,
      modal: Hub.modal,
      results: Hub.results,
      el: Hub.el,
      button: Hub.button,
      progress: Hub.progress,
      coinMultiplier: Hub.coinMultiplier,
      xpMultiplier: Hub.xpMultiplier,
      reviveCharges: Hub.reviveCharges,
      hue: Hub.prestige().hue,
      addXp: Hub.addXp,
      addGems: Hub.addGems,
      commit: function () { A.Save.write(); },
      /* Games that build their own renderer or physics world register it here
         so the hub can take it apart again. */
      scope: scope,
      reportScore: function (score) { return A.GameManager.reportScore(def.id, score); },
      hiScore: function () { return A.GameManager.hiScore(def.id); }
    };

    A.Fx.reset();
    active = def.create(host);
    if (active.mount) active.mount($("gameUi"));

    var back = Hub.el("button", "backBtn", "MENU");
    back.addEventListener("click", function () {
      A.Audio.sfx("select");
      Hub.exit();
    });
    $("gameUi").appendChild(back);

    var pause = Hub.el("button", "pauseBtn", "II");
    pause.addEventListener("click", function () {
      A.Audio.sfx("select");
      Hub.pause.show();
    });
    $("gameUi").appendChild(pause);

    if (active.start) active.start();
    A.Save.data.totalRuns = A.Save.data.totalRuns || 0;
  };

  Hub.exit = function () {
    if (active && active.stop) {
      try { active.stop(); } catch (e) {
        if (window.console) console.warn("[arcade] game stop failed", e);
      }
    }
    active = null;
    activeDef = null;
    homeVisible = true;

    /* Release the game's scope and reset the shared state it touched, before
       anything repaints. */
    A.GameManager.unloadCurrentGame();
    Hub.pause.hide();
    Hub.modal.hide();
    $("gameUi").hidden = true;
    $("gameUi").innerHTML = "";
    $("home").hidden = false;
    Hub.paintAccount();
    Hub.paintMissions();
    Hub.paintTiles();
  };

  /* ----------------------------------------------------------- pause ---- */
  /**
   * Global pause. The frame loop keeps running so the scene stays painted, but
   * the game's update is skipped and its input is dropped - a paused game that
   * still reads the pointer would move the moment the overlay is dismissed.
   */
  Hub.pause = {
    visible: function () { return !$("pause").hidden; },

    show: function () {
      if (!active || Hub.pause.visible()) return;
      $("pauseName").textContent = activeDef ? activeDef.name : "";
      $("pause").hidden = false;
      A.Input.reset();
    },

    hide: function () {
      if ($("pause").hidden) return;
      $("pause").hidden = true;
      A.Input.reset();
    },

    toggle: function () {
      if (Hub.pause.visible()) Hub.pause.hide();
      else Hub.pause.show();
    }
  };

  /* -------------------------------------------------------- settings ---- */
  Hub.openSettings = function () {
    var GM = A.GameManager;
    var s = GM.settings();
    var card = Hub.el("div", "card");
    card.appendChild(Hub.el("h2", null, "SETTINGS"));
    card.appendChild(Hub.el("p", "sub", "Applies to every game"));

    /* Volume */
    var volRow = Hub.el("div", "setRow");
    var volLeft = Hub.el("div");
    volLeft.appendChild(Hub.el("h4", null, "VOLUME"));
    var volRead = Hub.el("p", null, Math.round(s.volume * 100) + "%");
    volLeft.appendChild(volRead);
    volRow.appendChild(volLeft);

    var slider = document.createElement("input");
    slider.type = "range";
    slider.min = "0";
    slider.max = "100";
    slider.step = "5";
    slider.value = String(Math.round(s.volume * 100));
    slider.addEventListener("input", function () {
      var v = parseInt(slider.value, 10) / 100;
      volRead.textContent = Math.round(v * 100) + "%";
      A.Save.data.soundEnabled = v > 0;
      GM.setSetting("volume", v);
    });
    /* One confirmation blip on release, so the level is audible while setting it. */
    slider.addEventListener("change", function () { A.Audio.resume(); A.Audio.sfx("select"); });
    volRow.appendChild(slider);
    card.appendChild(volRow);

    /* Graphics */
    var gfxRow = Hub.el("div", "setRow");
    var gfxLeft = Hub.el("div");
    gfxLeft.appendChild(Hub.el("h4", null, "GRAPHICS"));
    gfxLeft.appendChild(Hub.el("p", null, "Lower this if the frame rate dips"));
    gfxRow.appendChild(gfxLeft);

    var seg = Hub.el("div", "segmented");
    [["low", "LOW"], ["medium", "MED"], ["high", "HIGH"]].forEach(function (opt) {
      var b = Hub.el("button", s.quality === opt[0] ? "on" : null, opt[1]);
      b.addEventListener("click", function () {
        GM.setSetting("quality", opt[0]);
        A.Audio.sfx("select");
        Hub.modal.hide();
        Hub.openSettings();
      });
      seg.appendChild(b);
    });
    gfxRow.appendChild(seg);
    card.appendChild(gfxRow);

    /* Haptics */
    var hapRow = Hub.el("div", "setRow");
    var hapLeft = Hub.el("div");
    hapLeft.appendChild(Hub.el("h4", null, "VIBRATION"));
    hapLeft.appendChild(Hub.el("p", null, "Phones only"));
    hapRow.appendChild(hapLeft);

    var hapSeg = Hub.el("div", "segmented");
    [[true, "ON"], [false, "OFF"]].forEach(function (opt) {
      var b = Hub.el("button", s.haptics === opt[0] ? "on" : null, opt[1]);
      b.addEventListener("click", function () {
        GM.setSetting("haptics", opt[0]);
        A.Audio.sfx("select");
        Hub.modal.hide();
        Hub.openSettings();
      });
      hapSeg.appendChild(b);
    });
    hapRow.appendChild(hapSeg);
    card.appendChild(hapRow);

    card.appendChild(Hub.button("DONE", "go", Hub.modal.hide));
    Hub.modal.show(card);
  };

  /* ---------------------------------------------------------- avatar ---- */
  Hub.buildAvatar = function () {
    avatarStyle = A.Rig.style(4242, {
      hue: Hub.prestige().hue,
      helmet: "crest",
      cape: true,
      weapon: "sword",
      bulk: 1.05
    });
  };

  function drawAvatar(time) {
    if (!avatarCtx || !avatarStyle) return;
    var g = avatarCtx;
    g.clearRect(0, 0, 104, 104);

    var grad = g.createLinearGradient(0, 0, 0, 104);
    grad.addColorStop(0, "#2A1F55");
    grad.addColorStop(1, "#150F2E");
    g.fillStyle = grad;
    g.fillRect(0, 0, 104, 104);

    var pose = A.Rig.pose("idle", A.wrap(time * 0.4, 1));
    A.Rig.draw(g, avatarStyle, pose, 52, 96, 86, 1, { noShadow: false });
  }

  function drawTiles(time) {
    for (var i = 0; i < tileCanvases.length; i++) {
      var entry = tileCanvases[i];
      if (entry.def.thumb) entry.def.thumb(entry.ctx, entry.w, entry.h, time + i * 1.7);
    }
  }

  /* ------------------------------------------------------------- boot --- */
  Hub.boot = function () {
    A.Save.load();
    Hub.prestige();
    Hub.refreshDaily();
    Hub.buildAvatar();

    A.View.attach($("stage"));
    A.Input.attach($("stage"));

    avatarCtx = $("avatarCanvas").getContext("2d");

    A.View.onResize = function () {
      if (active && active.onResize) active.onResize();
    };

    $("settingsBtn").addEventListener("click", function () {
      A.Audio.resume();
      Hub.openSettings();
    });

    $("pauseResume").addEventListener("click", function () {
      A.Audio.sfx("select");
      Hub.pause.hide();
    });
    $("pauseSettings").addEventListener("click", function () {
      A.Audio.sfx("select");
      Hub.openSettings();
    });
    $("pauseQuit").addEventListener("click", function () {
      A.Audio.sfx("select");
      Hub.exit();
    });

    /* Escape pauses a running game and closes an open card, in that order. */
    window.addEventListener("keydown", function (e) {
      if (e.key !== "Escape") return;
      if (Hub.modal.visible()) { Hub.modal.hide(); return; }
      if (active) Hub.pause.toggle();
    });

    $("gemShopBtn").addEventListener("click", function () {
      A.Audio.resume();
      Hub.openShop();
    });

    $("resetBtn").addEventListener("click", function () {
      var card = Hub.el("div", "card");
      card.appendChild(Hub.el("h2", null, "RESET EVERYTHING?"));
      card.appendChild(Hub.el("p", "sub", "Every game, level and gem is wiped"));
      card.appendChild(Hub.button("YES, WIPE IT", "gold", function () {
        A.Save.reset();
        Hub.refreshDaily();
        Hub.buildAvatar();
        Hub.modal.hide();
        Hub.exit();
      }));
      card.appendChild(Hub.button("CANCEL", "ghost", Hub.modal.hide));
      Hub.modal.show(card);
    });

    Hub.paintAccount();
    Hub.paintMissions();

    /* The manifest drives the grid. Tiles appear as each script registers, so
       a slow game file never blocks the rest of the hub from rendering. */
    A.GameManager.boot(function () { Hub.paintTiles(); });
    Hub.paintTiles();

    /*
     * Fixed timestep. requestAnimationFrame fires at the display's refresh
     * rate, so feeding its delta straight into the games made every one of them
     * run fast on a 120Hz or 144Hz panel: the arithmetic looked delta-correct,
     * but anything with per-frame damping, an integer counter, or an event that
     * fires on a threshold behaves differently at a different cadence. The
     * simulation now always advances in FIXED chunks and a fast display simply
     * renders more often between them.
     */
    var FIXED = 1 / 120;
    var MAX_STEPS = 8;
    var accumulator = 0;

    A.Loop.start(function (dt) {
      var g = A.View.ctx;

      if (active) {
        var halted = Hub.modal.visible() || Hub.pause.visible();

        accumulator += dt * A.Fx.timeScale;
        /* A long stall must not queue hundreds of catch-up steps. */
        if (accumulator > 0.25) accumulator = 0.25;

        var steps = 0;
        while (accumulator >= FIXED && steps < MAX_STEPS) {
          A.Fx.beginStep();
          if (!halted && active.update) active.update(FIXED);
          A.Fx.update(FIXED);
          accumulator -= FIXED;
          steps++;
          A.Input.clearEdges();
        }
        /* Hit the ceiling: the machine cannot keep up, so drop the debt rather
           than spiral. */
        if (steps >= MAX_STEPS) accumulator = 0;

        if (active.render) active.render(g);
      } else {
        accumulator = 0;
        g.clearRect(0, 0, A.View.w, A.View.h);
        drawAvatar(A.Loop.time);
        drawTiles(A.Loop.time);
      }
    });
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", Hub.boot);
  } else {
    Hub.boot();
  }
})(window.A);
