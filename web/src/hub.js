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

  /** Standard end-of-run card. Games hand over their own stat lines. */
  Hub.results = function (opts) {
    var card = Hub.el("div", "card");
    var title = Hub.el("h2", null, opts.title);
    title.style.color = opts.win ? "var(--jade)" : "var(--rose)";
    card.appendChild(title);
    card.appendChild(Hub.el("p", "sub", opts.subtitle || ""));

    var stats = Hub.el("div", "stats");
    (opts.stats || []).forEach(function (s) {
      var box = Hub.el("div", "stat");
      box.appendChild(Hub.el("b", null, s[1]));
      box.appendChild(Hub.el("span", null, s[0]));
      stats.appendChild(box);
    });
    card.appendChild(stats);

    (opts.buttons || []).forEach(function (b) {
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

    A.games.forEach(function (def) {
      var unlocked = A.Save.data.accountLevel >= (def.unlock || 1);
      var tile = Hub.el("button", "tile" + (unlocked ? "" : " locked"));
      tile.style.setProperty("--accent", def.accent);

      var canvas = document.createElement("canvas");
      canvas.width = 208; canvas.height = 176;
      tile.appendChild(canvas);
      tileCanvases.push({ def: def, ctx: canvas.getContext("2d"), w: 104, h: 88 });
      canvas.getContext("2d").scale(2, 2);

      var mid = Hub.el("div");
      mid.appendChild(Hub.el("h3", null, def.name));
      mid.appendChild(Hub.el("p", null, def.tagline));

      var slot = A.Save.game(def.id, def.template || {});
      if (unlocked && def.bestLine) {
        mid.appendChild(Hub.el("div", "best", def.bestLine(slot)));
      } else if (!unlocked) {
        mid.appendChild(Hub.el("div", "best", "Unlocks at account level " + def.unlock));
      }
      tile.appendChild(mid);

      var go = Hub.el("div", "go", unlocked ? "PLAY" : "LOCKED");
      go.style.background = unlocked ? def.accent : "";
      tile.appendChild(go);

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
  Hub.launch = function (def) {
    if (active) Hub.exit();

    activeDef = def;
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
      commit: function () { A.Save.write(); }
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

    if (active.start) active.start();
    A.Save.data.totalRuns = A.Save.data.totalRuns || 0;
  };

  Hub.exit = function () {
    if (active && active.stop) active.stop();
    active = null;
    activeDef = null;
    homeVisible = true;
    A.Fx.reset();
    Hub.modal.hide();
    $("gameUi").hidden = true;
    $("gameUi").innerHTML = "";
    $("home").hidden = false;
    Hub.paintAccount();
    Hub.paintMissions();
    Hub.paintTiles();
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

    $("soundToggle").addEventListener("click", function () {
      A.Save.data.soundEnabled = !A.Save.data.soundEnabled;
      A.Save.write();
      $("soundToggle").textContent = "Sound: " + (A.Save.data.soundEnabled ? "on" : "off");
    });
    $("soundToggle").textContent = "Sound: " + (A.Save.data.soundEnabled ? "on" : "off");

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
    Hub.paintTiles();

    A.Loop.start(function (dt) {
      var g = A.View.ctx;
      var scaled = dt * A.Fx.timeScale;

      if (active) {
        if (!Hub.modal.visible() && active.update) active.update(scaled);
        A.Fx.update(scaled);
        if (active.render) active.render(g);
      } else {
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
