/* ===========================================================================
   POCKET ARCADE - cross-game meta layer
   Ported in spirit from web/src/hub.js: account XP and level, gems, daily
   missions drawn from a pool by the date, a consecutive-day login streak.
   The economy numbers live here and only here.
   =========================================================================== */
#include "meta.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

static MetaState            M;
static MetaPending          P;
static const PA_Game *const *g_games;
static int                  g_ready;
static int                  g_demo;

/* ------------------------------------------------------------- economy -- */
int meta_xp_need(int level) {
    if (level < 1) level = 1;
    /* 60, 140, 230 ... ~1,100 at level 10: a few runs early, a session later. */
    float need = 60.0f * powf((float)level, 1.28f);
    return ((int)(need / 5.0f + 0.5f)) * 5;
}

int meta_levelup_gems(int new_level) { return 10 + 2 * new_level; }

static const int STREAK_GEMS[META_STREAK_CYCLE] = { 5, 8, 10, 15, 20, 25, 60 };
int meta_streak_reward(int day) {
    if (day < 1) day = 1;
    return STREAK_GEMS[(day - 1) % META_STREAK_CYCLE];
}
int meta_streak_cycle_day(void) { return ((M.streak < 1 ? 1 : M.streak) - 1) % META_STREAK_CYCLE + 1; }

static const int MASTERY[3] = { 60, 260, 800 };
int meta_game_stars(int g) {
    if (g < 0 || g >= M.game_count) return 0;
    int s = 0;
    for (int i = 0; i < 3; i++) if (M.game[g].xp >= MASTERY[i]) s = i + 1;
    return s;
}

void pa_fmt_int(int v, char *buf, size_t n) {
    char raw[24];
    int neg = v < 0;
    snprintf(raw, sizeof(raw), "%d", neg ? -v : v);
    int len = (int)strlen(raw), o = 0;
    char out[40];
    if (neg) out[o++] = '-';
    for (int i = 0; i < len; i++) {
        out[o++] = raw[i];
        int left = len - 1 - i;
        if (left > 0 && left % 3 == 0) out[o++] = ',';
    }
    out[o] = 0;
    snprintf(buf, n, "%s", out);
}

/* ---------------------------------------------------------- persistence -- */
static int  sget(const char *k, int fb) { return g_demo ? fb : pa_save_get(k, fb); }
static void sset(const char *k, int v) {
    if (g_demo) return;
    /* Zeros are the default; writing them for every game would spend save
       slots on nothing. */
    if (v == 0 && pa_save_get(k, 0) == 0) return;
    pa_save_set(k, v);
}

static void game_key(char *buf, size_t n, int g, const char *field) {
    snprintf(buf, n, "meta.%s.%s", g_games[g]->id, field);
}

static void save_all(void) {
    if (g_demo) return;
    char k[40];
    sset("meta.level", M.level);
    sset("meta.xp", M.xp);
    sset("meta.gems", M.gems);
    sset("meta.coins", M.coins);
    sset("meta.played", M.played_mask);
    sset("meta.bonus", M.bonus_state);
    for (int i = 0; i < META_MISSIONS; i++) {
        snprintf(k, sizeof(k), "meta.m%d.prog", i); sset(k, M.missions[i].progress);
        snprintf(k, sizeof(k), "meta.m%d.state", i); sset(k, M.missions[i].state);
    }
    for (int g = 0; g < M.game_count; g++) {
        game_key(k, sizeof(k), g, "best"); sset(k, M.game[g].best);
        game_key(k, sizeof(k), g, "runs"); sset(k, M.game[g].runs);
        game_key(k, sizeof(k), g, "xp");   sset(k, M.game[g].xp);
        game_key(k, sizeof(k), g, "lvl");  sset(k, M.game[g].level);
    }
    pa_save_flush();
}

/* ---------------------------------------------------------------- clock -- */
static int days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void read_clock(int *day, int *secs_left) {
    if (g_demo) { *day = 20734; *secs_left = 5 * 3600 + 12 * 60 + 40; return; }
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (!lt) { *day = (int)(now / 86400); *secs_left = 86400 - (int)(now % 86400); return; }
    *day = days_from_civil(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    *secs_left = 86400 - (lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);
}

/* ------------------------------------------------------------- missions -- */
static void mission_label(MetaMission *m) {
    char n[24];
    pa_fmt_int(m->target, n, sizeof(n));
    switch (m->type) {
    case MIS_RUNS:      snprintf(m->label, sizeof(m->label), "Play %s runs", n); break;
    case MIS_COINS:     snprintf(m->label, sizeof(m->label), "Collect %s coins", n); break;
    case MIS_GAMES:     snprintf(m->label, sizeof(m->label), "Play %s different games", n); break;
    case MIS_GAME_RUNS: snprintf(m->label, sizeof(m->label), "Play %s %s times",
                                 g_games[m->game]->name, n); break;
    case MIS_WINS:      snprintf(m->label, sizeof(m->label), "Win or clear %s levels", n); break;
    default:            snprintf(m->label, sizeof(m->label), "Beat a personal best"); break;
    }
}

/* The day's three missions are a pure function of the date, so only the
   progress needs saving and every device agrees on what "today" asks. */
static void build_missions(int day) {
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)day * 2654435761u ^ 0x5EEDu);
    M.featured = M.game_count ? (int)(((unsigned)day * 7u + 3u) % (unsigned)M.game_count) : 0;
    if (g_demo) {
        for (int g = 0; g < M.game_count; g++) if (!strcmp(g_games[g]->id, "helix")) M.featured = g;
    }

    int pool[MIS_TYPE_COUNT];
    for (int i = 0; i < MIS_TYPE_COUNT; i++) pool[i] = i;
    for (int i = MIS_TYPE_COUNT - 1; i > 0; i--) {
        int j = pa_rng_int(&r, 0, i);
        int t = pool[i]; pool[i] = pool[j]; pool[j] = t;
    }
    /* Always lead with "play today's featured game": it is the one mission
       that points the player somewhere specific. */
    int order[META_MISSIONS] = { MIS_GAME_RUNS, -1, -1 };
    int k = 1;
    for (int i = 0; i < MIS_TYPE_COUNT && k < META_MISSIONS; i++)
        if (pool[i] != MIS_GAME_RUNS) order[k++] = pool[i];

    for (int i = 0; i < META_MISSIONS; i++) {
        MetaMission *m = &M.missions[i];
        memset(m, 0, sizeof(*m));
        m->type = order[i];
        m->game = -1;
        switch (m->type) {
        case MIS_RUNS:      m->target = pa_rng_int(&r, 4, 6);         m->reward = 12; break;
        case MIS_COINS:     m->target = pa_rng_int(&r, 3, 10) * 50;   m->reward = 18; break;
        case MIS_GAMES:     m->target = 3;                            m->reward = 15; break;
        case MIS_GAME_RUNS: m->target = 2; m->game = M.featured;      m->reward = 12; break;
        case MIS_WINS:      m->target = pa_rng_int(&r, 2, 4);         m->reward = 20; break;
        default:            m->target = 1;                            m->reward = 20; break;
        }
        m->reward += pa_rng_int(&r, 0, 2) * 2;
        mission_label(m);
    }
    M.bonus_reward = 40;
}

static void load_missions(int day) {
    build_missions(day);
    int saved_day = sget("meta.m.day", -1);
    if (saved_day == day) {
        char k[32];
        for (int i = 0; i < META_MISSIONS; i++) {
            snprintf(k, sizeof(k), "meta.m%d.prog", i);  M.missions[i].progress = sget(k, 0);
            snprintf(k, sizeof(k), "meta.m%d.state", i); M.missions[i].state = sget(k, 0);
        }
        M.bonus_state = sget("meta.bonus", 0);
        M.played_mask = sget("meta.played", 0);
    } else {
        M.bonus_state = 0;
        M.played_mask = 0;
        sset("meta.m.day", day);
    }
}

/* ---------------------------------------------------------------- streak -- */
static void roll_day(int day) {
    int last = sget("meta.day", -1);
    if (last == day) return;
    int streak = sget("meta.streak", 0);
    streak = (last == day - 1) ? streak + 1 : 1;
    M.streak = streak;
    M.streak_claimed = 0;
    sset("meta.streak", streak);
    sset("meta.day", day);
}

static void apply_day(int day) {
    M.day = day;
    roll_day(day);
    load_missions(day);
    M.streak = sget("meta.streak", M.streak ? M.streak : 1);
    M.streak_claimed = sget("meta.strk.claim", -1) == day;
    save_all();
}

void meta_refresh_clock(void) {
    if (!g_ready) return;
    int day, left;
    read_clock(&day, &left);
    M.secs_to_reset = left;
    if (day != M.day) apply_day(day);
}

/* ------------------------------------------------------------------ demo -- */
static int game_index(const char *id) {
    if (!id) return -1;
    for (int g = 0; g < M.game_count; g++) if (!strcmp(g_games[g]->id, id)) return g;
    return -1;
}

static void seed_demo(void) {
    int demo = g_demo;
    M.level = 7;
    M.xp = demo == 3 ? meta_xp_need(7) - 18 : (int)((float)meta_xp_need(7) * 0.62f);
    M.gems = 1240;
    M.coins = 18450;
    M.streak = 3;
    M.streak_claimed = demo == 2 ? 0 : 1;
    static const struct { const char *id; int best, runs, xp, level; } seed[] = {
        { "roadhopper", 214, 31, 905, 0 }, { "voidmuncher", 0, 12, 300, 14 },
        { "chromerush", 18420, 9, 270, 0 }, { "blockstorm", 6230, 22, 640, 0 },
        { "helix", 0, 40, 1200, 23 },      { "splat", 0, 18, 420, 31 },
        { "mobclash", 0, 6, 180, 9 },      { "paper", 41, 3, 70, 0 },
        { "pins", 0, 0, 0, 0 },            { "runner", 3120, 4, 90, 0 },
        { "horde", 0, 0, 0, 0 },           { "avian", 0, 2, 40, 5 },
    };
    for (size_t i = 0; i < sizeof(seed) / sizeof(seed[0]); i++) {
        int g = game_index(seed[i].id);
        if (g < 0) continue;
        M.game[g].best = seed[i].best;
        M.game[g].runs = seed[i].runs;
        M.game[g].xp = seed[i].xp;
        M.game[g].level = seed[i].level;
    }
    /* One mission ready to claim, one under way, one fresh. */
    M.missions[0].progress = M.missions[0].target;
    M.missions[0].state = MIS_DONE;
    M.missions[1].progress = M.missions[1].target * 2 / 3;
    if (M.missions[1].progress >= M.missions[1].target) M.missions[1].progress = M.missions[1].target - 1;
    M.missions[2].progress = 0;
    M.played_mask = 0;
}

/* ------------------------------------------------------------------ init -- */
void meta_init(const PA_Game *const *games, int count) {
    memset(&M, 0, sizeof(M));
    memset(&P, 0, sizeof(P));
    g_games = games;
    M.game_count = count > META_MAX_GAMES ? META_MAX_GAMES : count;
    g_demo = pa_demo_mode();
    g_ready = 1;

    M.level = sget("meta.level", 1);
    if (M.level < 1) M.level = 1;
    M.xp = sget("meta.xp", 0);
    M.gems = sget("meta.gems", 0);
    M.coins = sget("meta.coins", 0);
    char k[40];
    for (int g = 0; g < M.game_count; g++) {
        game_key(k, sizeof(k), g, "best"); M.game[g].best = sget(k, 0);
        game_key(k, sizeof(k), g, "runs"); M.game[g].runs = sget(k, 0);
        game_key(k, sizeof(k), g, "xp");   M.game[g].xp = sget(k, 0);
        game_key(k, sizeof(k), g, "lvl");  M.game[g].level = sget(k, 0);
    }
    int day, left;
    read_clock(&day, &left);
    M.secs_to_reset = left;
    apply_day(day);
    if (g_demo) seed_demo();
    P.level_from = P.level_to = M.level;
}

const MetaState *meta_state(void) { return &M; }

int meta_total_xp(void) {
    int t = M.xp;
    for (int l = 1; l < M.level; l++) t += meta_xp_need(l);
    return t;
}

void meta_level_from_total(int total, int *level, int *into, int *need) {
    int l = 1;
    while (total >= meta_xp_need(l) && l < 999) { total -= meta_xp_need(l); l++; }
    *level = l; *into = total; *need = meta_xp_need(l);
}

void meta_game_progress(int g, char *buf, size_t n) {
    buf[0] = 0;
    if (g < 0 || g >= M.game_count) return;
    char num[24];
    int lvl = M.game[g].level, best = M.game[g].best;
    if (!g_demo) {
        /* Before a game is wired into the meta layer its own save still knows
           how far the player got; read it rather than show a blank tile. */
        char k[40];
        snprintf(k, sizeof(k), "%s.level", g_games[g]->id);
        int own = pa_save_get(k, 0);
        if (own > lvl) lvl = own;
        snprintf(k, sizeof(k), "%s.best", g_games[g]->id);
        own = pa_save_get(k, 0);
        if (own > best) best = own;
    }
    if (lvl > 0) { snprintf(buf, n, "LEVEL %d", lvl); return; }
    if (best > 0) { pa_fmt_int(best, num, sizeof(num)); snprintf(buf, n, "BEST %s", num); }
}

int meta_claimable(void) {
    int n = 0;
    for (int i = 0; i < META_MISSIONS; i++) if (M.missions[i].state == MIS_DONE) n++;
    if (M.bonus_state == MIS_DONE) n++;
    return n;
}

/* --------------------------------------------------------------- reports -- */
static void advance(int type, int amount, int is_max, int game) {
    for (int i = 0; i < META_MISSIONS; i++) {
        MetaMission *m = &M.missions[i];
        if (m->type != type || m->state != MIS_ACTIVE) continue;
        if (type == MIS_GAME_RUNS && m->game != game) continue;
        m->progress = is_max ? (amount > m->progress ? amount : m->progress) : m->progress + amount;
        if (m->progress >= m->target) {
            m->progress = m->target;
            m->state = MIS_DONE;
            P.missions_done++;
        }
    }
}

void pa_meta_report(const char *game_id, const PA_RunReport *r) {
    if (!g_ready || !r) return;
    meta_refresh_clock();
    int g = game_index(game_id);
    int score = r->score > 0 ? r->score : 0;
    int coins = r->coins > 0 ? r->coins : 0;
    int stars = r->stars < 0 ? 0 : (r->stars > 3 ? 3 : r->stars);
    int new_best = 0;

    if (g >= 0) {
        MetaGameStats *s = &M.game[g];
        new_best = s->runs > 0 && score > s->best && s->best > 0;
        if (score > s->best) s->best = score;
        if (r->level > s->level) s->level = r->level;
        s->runs++;
        M.played_mask |= 1 << g;
    }

    int xp = 15 + (coins / 8 > 25 ? 25 : coins / 8) + (r->won ? 10 : 0) + stars * 4 + (new_best ? 15 : 0);
    if (g >= 0 && g == M.featured) xp *= 2;
    if (g >= 0) M.game[g].xp += xp;

    M.xp += xp;
    M.coins += coins;
    P.xp += xp;
    P.coins += coins;
    P.runs++;
    while (M.xp >= meta_xp_need(M.level)) {
        M.xp -= meta_xp_need(M.level);
        M.level++;
        int gems = meta_levelup_gems(M.level);
        M.gems += gems;
        P.level_gems += gems;
    }
    P.level_to = M.level;

    advance(MIS_RUNS, 1, 0, g);
    advance(MIS_COINS, coins, 0, g);
    if (g >= 0) advance(MIS_GAME_RUNS, 1, 0, g);
    if (r->won) advance(MIS_WINS, 1, 0, g);
    if (new_best) advance(MIS_BEST, 1, 0, g);
    {
        int distinct = 0;
        for (int i = 0; i < M.game_count; i++) if (M.played_mask & (1 << i)) distinct++;
        advance(MIS_GAMES, distinct, 1, g);
    }
    save_all();
}

void pa_meta_run_end(const char *game_id, int score, int coins_earned) {
    PA_RunReport r;
    memset(&r, 0, sizeof(r));
    r.score = score;
    r.coins = coins_earned;
    pa_meta_report(game_id, &r);
}

int pa_meta_account_level(void) { return M.level; }
int pa_meta_gems(void) { return M.gems; }

void meta_take_pending(MetaPending *out) {
    *out = P;
    memset(&P, 0, sizeof(P));
    P.level_from = P.level_to = M.level;
}

/* ---------------------------------------------------------------- claims -- */
int meta_claim_streak(void) {
    if (M.streak_claimed) return 0;
    int gems = meta_streak_reward(meta_streak_cycle_day());
    M.gems += gems;
    M.streak_claimed = 1;
    sset("meta.strk.claim", M.day);
    save_all();
    return gems;
}

int meta_claim_mission(int i) {
    if (i < 0 || i >= META_MISSIONS || M.missions[i].state != MIS_DONE) return 0;
    M.missions[i].state = MIS_CLAIMED;
    M.gems += M.missions[i].reward;
    int all = 1;
    for (int k = 0; k < META_MISSIONS; k++) if (M.missions[k].state != MIS_CLAIMED) all = 0;
    if (all && M.bonus_state == MIS_ACTIVE) M.bonus_state = MIS_DONE;
    save_all();
    return M.missions[i].reward;
}

int meta_claim_bonus(void) {
    if (M.bonus_state != MIS_DONE) return 0;
    M.bonus_state = MIS_CLAIMED;
    M.gems += M.bonus_reward;
    save_all();
    return M.bonus_reward;
}
