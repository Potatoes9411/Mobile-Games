/* ===========================================================================
   MOB CLASH: GATE SIEGE - native build, modelled on Mob Control (Voodoo)

   A cannon sits at the near end of a long concrete track. Hold to fire a
   stream of little blue mob people, drag to slide the cannon left and right.
   They run up the track through translucent gate panels (X2, X3, +10, -5)
   that multiply or thin the crowd, meet the red crowds pouring out of the
   enemy castle and annihilate one-for-one, and every blue that reaches the
   castle knocks a point off its health. A charge meter fills with every shot
   and launches a champion brute. Brutes come the other way too.

   Drawing: one perspective camera behind the cannon. The crowd is the whole
   spectacle, so each mob person is pre-rendered once per screen scale into a
   small premultiplied sprite (rendered on black and on white to recover exact
   coverage) and blitted with an integer blend, which keeps a thousand of them
   cheap on a phone's software renderer. Everything else is a handful of
   vector shapes: boxes under the same projection, gates as flat panels.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ------------------------------------------------------------- tuning -- */
#define TRACK_HALF   5.0f
#define TRACK_LEN    48.0f       /* castle front */
#define CAM_D        40.0f       /* perspective distance */
#define VK           0.86f       /* vertical foreshortening of heights */
#define CANNON_Z     1.0f
#define DEFENCE_Z    2.7f        /* hazard stripe: reds crossing it hurt the cannon */
#define UNIT_H       0.78f
#define BLUE_SPEED   6.8f
#define HIT_R        0.46f
#define MAX_BLUE     1500
#define MAX_RED      1100
#define MAX_BRUTES   10
#define MAX_CHAMPS   8
#define MAX_GATES    12
#define MAX_PARTS    900
#define MAX_FLOATS   40
#define MAX_DECOR    90
#define MAX_SCUFF    40
#define CELL         0.5f
#define GX           24          /* x in [-6, 6) */
#define GZ           144         /* z in [-4, 68) */
#define ZBINS        GZ

enum { S_READY, S_PLAY, S_WIN, S_FAIL, S_RESULT, S_SHOP };
enum { G_MUL, G_ADD, G_SUB };
enum { PK_PUFF, PK_BIT, PK_RING, PK_SPARK };
enum { T_BLUE, T_RED, T_GLOW, T_COUNT };

typedef struct { float x, z, vx, ph, fresh; uint16_t gates; uint8_t dead; } Mob;
typedef struct { float x, z, hp, maxhp, flash, ph, size, speed; uint16_t gates; int boss; } Brute;
typedef struct { int type, val; float x0, x, z, w, amp, spd, ph, pulse, cool; int hunger; } Gate;
typedef struct { float x, z, y, vx, vz, vy, life, max, size; PA_Color col; int kind; } Part;
typedef struct { float x, z, y, t, size; PA_Color col; char text[16]; } Float;
typedef struct { float x, z, s; int kind; } Decor;
typedef struct { float x, y, w, h; } Rect;

/* ------------------------------------------------------------- state -- */
static Mob   g_blue[MAX_BLUE];  static int g_nblue;
static Mob   g_red[MAX_RED];    static int g_nred;
static Brute g_brute[MAX_BRUTES]; static int g_nbrute;
static Brute g_champ[MAX_CHAMPS]; static int g_nchamp;
static Gate  g_gate[MAX_GATES]; static int g_ngate;
static Part  g_part[MAX_PARTS]; static int g_pi;
static Float g_float[MAX_FLOATS]; static int g_fi;
static Decor g_decor[MAX_DECOR]; static int g_ndecor;
static float g_scuff[MAX_SCUFF][3];

/* spatial grids, rebuilt every step */
static short g_rhead[GX * GZ], g_rnext[MAX_RED];
static short g_bhead[GX * GZ], g_bnext[MAX_BLUE];
static int   g_rrow_n[GZ], g_brow_n[GZ];
static float g_rrow_x[GZ], g_brow_x[GZ];

static struct {
    int   state;
    float st, t, clock;
    int   level, coins, gems, best;
    int   up_fire, up_champ, up_income;
    /* level */
    float base_hp, base_max, base_flash, base_shake, collapse;
    int   boss_level;
    float cannon_x, cannon_tx, recoil, cannon_flash;
    int   cannon_hp, cannon_max;
    float fire_acc, charge, charge_max, charge_flash;
    int   firing;
    int   wave_i, wave_left;
    float wave_timer, wave_gap, wave_acc, red_speed;
    int   brute_every, red_cap;
    float red_rate;
    float brute_hp, boss_timer; int boss_spawned;
    int   kills;
    /* results */
    int   result_win, result_level, reward_coins, reward_gems;
    float shown_coins;
    float shake;
    float sfx_kill, sfx_base, sfx_fire, sfx_gate;
    int   demo;
    PA_Rng rng;
    float bot_wob;
    float press_t; int press_id;
    float shop_flash[3];
} G;

/* layout (written by render, read by update for hit tests) */
static float g_w = 540.0f, g_h = 1170.0f, g_k = 50.8f, g_y0 = 936.0f, g_yh = -140.0f, g_u = 1.0f;
static float g_shx, g_shy;

/* ------------------------------------------------------------ palette -- */
#define C_INK      PA_RGB(38, 30, 74)
#define C_BLUE     0x2F8BFF
#define C_RED      0xF0373A

/* ---------------------------------------------------------- utilities -- */
static float frand(float a, float b) { return pa_rng_range(&G.rng, a, b); }

static void layout(float w, float h) {
    g_w = w; g_h = h;
    int portrait = h >= w;
    g_k = fminf(w * 0.47f, h * 0.40f) / TRACK_HALF;
    float y0 = h * (portrait ? 0.835f : 0.88f);
    float yf = h * (portrait ? (h / w < 2.0f ? 0.275f : 0.255f) : 0.36f);
    float pl = CAM_D / (TRACK_LEN + CAM_D);
    g_y0 = y0;
    g_yh = (yf - y0 * pl) / (1.0f - pl);
    g_u = pa_clampf(fminf(w / 540.0f, h / 900.0f), 0.62f, 2.2f);
}

static float persp(float z) { if (z < -CAM_D + 5.0f) z = -CAM_D + 5.0f; return CAM_D / (z + CAM_D); }
static float ss(float z) { return g_k * persp(z); }
static float sx(float x, float z) { return g_w * 0.5f + x * ss(z) + g_shx; }
static float sy(float z) { return g_yh + (g_y0 - g_yh) * persp(z) + g_shy; }
static PA_Vec2 proj(float x, float y, float z) {
    PA_Vec2 v = { sx(x, z), sy(z) - y * ss(z) * VK };
    return v;
}

static void fmt_num(int v, char *buf, size_t n) {
    if (v >= 100000) snprintf(buf, n, "%dK", v / 1000);
    else if (v >= 10000) snprintf(buf, n, "%d.%dK", v / 1000, (v % 1000) / 100);
    else snprintf(buf, n, "%d", v);
}

static int in_rect(Rect r, float x, float y) { return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h; }

/* Text centred vertically on y, in the chunky outlined HUD style. */
static void txt(PA_Canvas *c, const char *s, float x, float y, float size, PA_Color fill, PA_Align al, float weight) {
    pa_text_bold(c, s, x, y - size * 0.5f, size, fill, C_INK, al, size * 0.05f, weight);
}

/* ------------------------------------------------------------ economy -- */
static float fire_rate(int lvl)  { return 8.0f + 0.6f * (float)lvl; }
static int   champ_hp(int lvl)   { return 26 + 12 * lvl; }
static float income_mul(int lvl) { return 1.0f + 0.15f * (float)lvl; }
static int cost_fire(int lvl)   { return (int)(50.0f * powf(1.30f, (float)lvl) + 0.5f) / 5 * 5; }
static int cost_income(int lvl) { return (int)(90.0f * powf(1.42f, (float)lvl) + 0.5f) / 5 * 5; }
static int cost_champ(int lvl)  { return 2 + 2 * lvl; }   /* gems */
#define MAX_UP_FIRE   20
#define MAX_UP_CHAMP  12
#define MAX_UP_INCOME 15

static void save_all(void) {
    if (G.demo) return;
    pa_save_set("mobclash.level", G.level);
    pa_save_set("mobclash.coins", G.coins);
    pa_save_set("mobclash.gems", G.gems);
    pa_save_set("mobclash.best", G.best);
    pa_save_set("mobclash.up_fire", G.up_fire);
    pa_save_set("mobclash.up_champ", G.up_champ);
    pa_save_set("mobclash.up_income", G.up_income);
    pa_save_flush();
}

static int can_afford(int which) {
    if (which == 0) return G.up_fire < MAX_UP_FIRE && G.coins >= cost_fire(G.up_fire);
    if (which == 1) return G.up_champ < MAX_UP_CHAMP && G.gems >= cost_champ(G.up_champ);
    return G.up_income < MAX_UP_INCOME && G.coins >= cost_income(G.up_income);
}

/* ------------------------------------------------------------ effects -- */
static Part *part_new(void) {
    Part *p = &g_part[g_pi];
    g_pi = (g_pi + 1) % MAX_PARTS;
    memset(p, 0, sizeof(*p));
    return p;
}

static void puff(float x, float z, PA_Color col, int n, float spread) {
    for (int i = 0; i < n; i++) {
        Part *p = part_new();
        p->kind = PK_PUFF; p->x = x + frand(-0.1f, 0.1f); p->z = z + frand(-0.1f, 0.1f);
        p->y = frand(0.15f, 0.45f);
        p->vx = frand(-spread, spread); p->vz = frand(-spread, spread); p->vy = frand(0.4f, 1.6f);
        p->life = p->max = frand(0.25f, 0.45f); p->size = frand(0.10f, 0.18f); p->col = col;
    }
}

static void bits(float x, float y, float z, PA_Color col, int n, float speed, float up) {
    for (int i = 0; i < n; i++) {
        Part *p = part_new();
        p->kind = PK_BIT; p->x = x; p->z = z; p->y = y;
        float a = frand(0.0f, PA_TAU);
        float s = frand(0.3f, 1.0f) * speed;
        p->vx = cosf(a) * s; p->vz = sinf(a) * s * 0.7f; p->vy = frand(0.4f, 1.0f) * up;
        p->life = p->max = frand(0.6f, 1.2f); p->size = frand(0.08f, 0.20f); p->col = col;
    }
}

static void ring(float x, float z, PA_Color col, float size) {
    Part *p = part_new();
    p->kind = PK_RING; p->x = x; p->z = z; p->life = p->max = 0.45f; p->size = size; p->col = col;
}

static void sparks(float x, float y, float z, PA_Color col, int n) {
    for (int i = 0; i < n; i++) {
        Part *p = part_new();
        p->kind = PK_SPARK; p->x = x + frand(-0.3f, 0.3f); p->z = z; p->y = y + frand(-0.2f, 0.4f);
        p->vx = frand(-2.5f, 2.5f); p->vz = frand(-1.5f, 1.5f); p->vy = frand(1.0f, 3.5f);
        p->life = p->max = frand(0.25f, 0.5f); p->size = frand(0.05f, 0.09f); p->col = col;
    }
}

static void float_text(float x, float y, float z, const char *s, PA_Color col, float size) {
    Float *f = &g_float[g_fi];
    g_fi = (g_fi + 1) % MAX_FLOATS;
    f->x = x; f->y = y; f->z = z; f->t = 0.0f; f->size = size; f->col = col;
    snprintf(f->text, sizeof(f->text), "%s", s);
}

static void kick(float amount) { if (amount > G.shake) G.shake = amount; }

/* ------------------------------------------------------------- sounds -- */
static void snd_fire(void) {
    if (G.sfx_fire > 0.0f) return;
    G.sfx_fire = 0.07f;
    pa_tone(340.0f + frand(-30.0f, 30.0f), 160.0f, 0.05f, 2, 0.035f);
}
static void snd_kill(void) {
    if (G.sfx_kill > 0.0f) return;
    G.sfx_kill = 0.06f;
    pa_tone(900.0f + frand(-120.0f, 120.0f), 1500.0f, 0.04f, 0, 0.04f);
}
static void snd_gate(int val) {
    if (G.sfx_gate > 0.0f) return;
    G.sfx_gate = 0.09f;
    float f = 520.0f + 90.0f * (float)val;
    pa_tone(f, f * 1.5f, 0.07f, 1, 0.05f);
}
static void snd_base(void) {
    if (G.sfx_base > 0.0f) return;
    G.sfx_base = 0.11f;
    pa_tone(150.0f, 80.0f, 0.08f, 0, 0.09f);
}

/* ------------------------------------------------------------- levels -- */
static void add_gate(int type, int val, float x, float z, float w) {
    if (g_ngate >= MAX_GATES) return;
    Gate *g = &g_gate[g_ngate++];
    memset(g, 0, sizeof(*g));
    g->type = type; g->val = val; g->x0 = g->x = x; g->z = z; g->w = w;
    g->hunger = val;
}

static void level_build(int n) {
    pa_rng_seed(&G.rng, 4111u + (uint32_t)n * 7919u);
    g_nblue = g_nred = g_nbrute = g_nchamp = g_ngate = 0;
    for (int i = 0; i < MAX_PARTS; i++) g_part[i].life = 0.0f;
    for (int i = 0; i < MAX_FLOATS; i++) g_float[i].t = 9.0f;

    float m = (float)(n - 1);
    G.boss_level = n % 5 == 0;

    /* Gate schedule: the product of the best gate in each row. One row of x2
       to teach, then the multipliers stack as the levels climb. */
    int rows, mul[3] = { 2, 2, 2 };
    if (n == 1) { rows = 1; }
    else if (n == 2) { rows = 1; mul[0] = 3; }
    else if (n <= 5) { rows = 2; }
    else if (n <= 9) { rows = 2; mul[pa_rng_int(&G.rng, 0, 1)] = 3; }
    else if (n <= 14) { rows = 3; }
    else { rows = 3; mul[pa_rng_int(&G.rng, 0, 2)] = 3; }
    float best_mul = 1.0f;
    for (int r = 0; r < rows; r++) best_mul *= (float)mul[r];

    /* Difficulty is relative, as in docs/PACING_MATH.md: size the castle and
       the red pressure from what a fair player pushes up the track here (a
       cannon upgraded about once per level, three quarters of shots through
       the best gates). Reds eat a bit over half of that; the rest grinds the
       castle down in about half a minute. */
    float expect = fire_rate(n - 1) * best_mul * fmaxf(0.45f, 0.75f - 0.02f * m);
    G.base_max = 0.45f * expect * (24.0f + 0.6f * m);
    if (G.boss_level) G.base_max *= 1.2f;
    G.base_max = fmaxf(120.0f, floorf(G.base_max / 5.0f) * 5.0f);
    G.base_hp = G.base_max;
    G.base_flash = G.base_shake = G.collapse = 0.0f;
    G.cannon_max = G.cannon_hp = 12;
    G.cannon_x = G.cannon_tx = 0.0f;
    G.recoil = G.cannon_flash = 0.0f;
    G.fire_acc = 0.9f;
    G.charge = 0.0f; G.charge_max = 9.0f * fire_rate(G.up_fire); G.charge_flash = 0.0f;
    G.wave_i = 0; G.wave_left = 0;
    G.wave_timer = 2.4f;
    G.wave_gap = 8.0f;
    G.red_rate = expect * (0.56f + 0.012f * m);
    G.red_cap = (int)fminf((float)MAX_RED, G.red_rate * 9.0f + 40.0f);
    G.red_speed = fminf(4.6f, 3.3f + 0.08f * m);
    G.brute_every = n >= 8 ? 2 : 3;
    G.brute_hp = n >= 3 ? 0.9f * expect : 0.0f;
    G.boss_timer = 8.0f; G.boss_spawned = !G.boss_level;
    G.kills = 0;
    G.t = 0.0f;

    /* Gate rows. The first levels teach with gifts only; traps and moving
       panels arrive once the player knows what a multiplier does. */
    static const float ROWZ[3][3] = { { 17.0f, 0, 0 }, { 12.0f, 25.0f, 0 }, { 10.0f, 19.5f, 29.5f } };
    for (int r = 0; r < rows; r++) {
        float z = ROWZ[rows - 1][r];
        int good = mul[r];
        int add = 5 * (int)((fire_rate(n - 1) * 1.6f + 4.0f) / 5.0f);
        int sub = 4 + n / 2;
        if (n == 1) { add_gate(G_MUL, 2, -1.6f, z, 4.0f); continue; }
        if (n == 2) { add_gate(G_MUL, 3, 2.2f, z, 3.4f); add_gate(G_ADD, 10, -2.4f, z, 3.2f); continue; }
        int pick = pa_rng_int(&G.rng, 0, n >= 7 ? 4 : (n >= 4 ? 3 : 1));
        float side = pa_rng_chance(&G.rng, 0.5f) ? 1.0f : -1.0f;
        switch (pick) {
            case 0: add_gate(G_MUL, good, frand(-2.2f, 2.2f), z, 4.0f); break;
            case 1: add_gate(G_MUL, good, -2.5f * side, z, 3.4f); add_gate(G_ADD, add, 2.5f * side, z, 3.3f); break;
            case 2: add_gate(G_MUL, good, -2.5f * side, z, 3.4f); add_gate(G_SUB, sub, 2.5f * side, z, 3.4f); break;
            case 3: if (good == 3) { add_gate(G_MUL, 3, -2.6f * side, z, 3.0f); add_gate(G_MUL, 2, 2.4f * side, z, 3.6f); }
                    else { add_gate(G_MUL, 2, -2.5f * side, z, 3.4f); add_gate(G_ADD, add, 2.5f * side, z, 3.3f); }
                    break;
            default: add_gate(G_MUL, good, 0.0f, z, 2.8f); add_gate(G_SUB, sub, -3.6f, z, 2.6f);
                     add_gate(G_SUB, sub, 3.6f, z, 2.6f); break;
        }
    }
    /* moving panels from level 4 */
    for (int i = 0; i < g_ngate; i++) {
        Gate *g = &g_gate[i];
        int lone = i + 1 >= g_ngate || g_gate[i + 1].z != g->z;
        if (i > 0 && g_gate[i - 1].z == g->z) lone = 0;
        if (n >= 4 && lone && pa_rng_chance(&G.rng, n >= 9 ? 0.75f : 0.5f)) {
            g->amp = fminf(TRACK_HALF - g->w * 0.5f - 0.3f, 1.4f + 0.06f * m);
            g->x0 = 0.0f;
            g->spd = frand(0.7f, 1.1f);
            g->ph = frand(0.0f, PA_TAU);
        }
    }

    /* scenery: pale pines and rocks either side of the track */
    g_ndecor = 0;
    for (int i = 0; i < MAX_DECOR; i++) {
        Decor *d = &g_decor[g_ndecor++];
        float side = (i & 1) ? 1.0f : -1.0f;
        d->x = side * frand(TRACK_HALF + 1.0f, TRACK_HALF + 13.0f);
        d->z = frand(-6.0f, TRACK_LEN + 26.0f);
        d->kind = pa_rng_chance(&G.rng, 0.78f) ? 0 : 1;
        d->s = frand(0.7f, 1.35f);
    }
    /* far to near so the painter's order is right */
    for (int i = 1; i < g_ndecor; i++) {
        Decor d = g_decor[i]; int j = i - 1;
        while (j >= 0 && g_decor[j].z < d.z) { g_decor[j + 1] = g_decor[j]; j--; }
        g_decor[j + 1] = d;
    }
    for (int i = 0; i < MAX_SCUFF; i++) {
        g_scuff[i][0] = frand(-TRACK_HALF + 0.6f, TRACK_HALF - 0.6f);
        g_scuff[i][1] = frand(-2.0f, TRACK_LEN - 1.0f);
        g_scuff[i][2] = frand(0.3f, 1.2f);
    }
    pa_rng_seed(&G.rng, 99173u + (uint32_t)n * 31u);
}

static void begin_ready(void) {
    level_build(G.level);
    G.state = S_READY; G.st = 0.0f;
}

/* ------------------------------------------------------------ spawning -- */
static Mob *blue_new(float x, float z) {
    if (g_nblue >= MAX_BLUE) return NULL;
    Mob *m = &g_blue[g_nblue++];
    memset(m, 0, sizeof(*m));
    m->x = x; m->z = z; m->ph = frand(0.0f, 2.0f);
    return m;
}
static Mob *red_new(float x, float z) {
    if (g_nred >= MAX_RED) return NULL;
    Mob *m = &g_red[g_nred++];
    memset(m, 0, sizeof(*m));
    m->x = x; m->z = z; m->ph = frand(0.0f, 2.0f);
    return m;
}

static void spawn_brute(float x, float hp, int boss) {
    if (g_nbrute >= MAX_BRUTES) return;
    Brute *b = &g_brute[g_nbrute++];
    memset(b, 0, sizeof(*b));
    b->x = x; b->z = TRACK_LEN - 0.8f; b->hp = b->maxhp = hp; b->boss = boss;
    b->size = boss ? 2.3f : 1.45f; b->speed = boss ? 1.2f : 1.6f;
    b->ph = frand(0.0f, PA_TAU);
    pa_tone(110.0f, 70.0f, 0.35f, 3, 0.10f);
}

static void spawn_champ(float x, float z, uint16_t gates, float hp) {
    if (g_nchamp >= MAX_CHAMPS) return;
    Brute *b = &g_champ[g_nchamp++];
    memset(b, 0, sizeof(*b));
    b->x = x; b->z = z; b->hp = b->maxhp = hp; b->size = 1.3f; b->speed = 3.4f; b->gates = gates;
    b->ph = frand(0.0f, PA_TAU);
}

/* ---------------------------------------------------------------- grid -- */
static int cell_of(float x, float z) {
    int cx = (int)floorf((x + 6.0f) / CELL), cz = (int)floorf((z + 4.0f) / CELL);
    cx = cx < 0 ? 0 : (cx >= GX ? GX - 1 : cx);
    cz = cz < 0 ? 0 : (cz >= GZ ? GZ - 1 : cz);
    return cz * GX + cx;
}
static int row_of(float z) {
    int cz = (int)floorf((z + 4.0f) / CELL);
    return cz < 0 ? 0 : (cz >= GZ ? GZ - 1 : cz);
}

static void build_grids(void) {
    for (int i = 0; i < GX * GZ; i++) { g_rhead[i] = -1; g_bhead[i] = -1; }
    for (int i = 0; i < GZ; i++) { g_rrow_n[i] = g_brow_n[i] = 0; g_rrow_x[i] = g_brow_x[i] = 0.0f; }
    for (int i = 0; i < g_nred; i++) {
        int c = cell_of(g_red[i].x, g_red[i].z);
        g_rnext[i] = g_rhead[c]; g_rhead[c] = (short)i;
        int r = c / GX; g_rrow_n[r]++; g_rrow_x[r] += g_red[i].x;
    }
    for (int i = 0; i < g_nblue; i++) {
        int c = cell_of(g_blue[i].x, g_blue[i].z);
        g_bnext[i] = g_bhead[c]; g_bhead[c] = (short)i;
        int r = c / GX; g_brow_n[r]++; g_brow_x[r] += g_blue[i].x;
    }
}

static void compact(void) {
    int n = 0;
    for (int i = 0; i < g_nblue; i++) if (!g_blue[i].dead) g_blue[n++] = g_blue[i];
    g_nblue = n;
    n = 0;
    for (int i = 0; i < g_nred; i++) if (!g_red[i].dead) g_red[n++] = g_red[i];
    g_nred = n;
    n = 0;
    for (int i = 0; i < g_nbrute; i++) if (g_brute[i].hp > 0.0f) g_brute[n++] = g_brute[i];
    g_nbrute = n;
    n = 0;
    for (int i = 0; i < g_nchamp; i++) if (g_champ[i].hp > 0.0f) g_champ[n++] = g_champ[i];
    g_nchamp = n;
}

/* ------------------------------------------------------------- results -- */
static void finish(int win) {
    G.result_win = win;
    G.result_level = G.level;
    float base = 40.0f + 12.0f * (float)G.level + (float)G.kills * 0.12f;
    if (G.boss_level) base *= 2.0f;
    base *= income_mul(G.up_income);
    if (!win) base *= 0.3f;
    G.reward_coins = (int)(base + 0.5f);
    G.reward_gems = win ? (G.boss_level ? 5 : 1) : 0;
    G.coins += G.reward_coins;
    G.gems += G.reward_gems;
    G.shown_coins = 0.0f;
    if (win) {
        G.level++;
        if (G.level - 1 > G.best) G.best = G.level - 1;
    }
    save_all();
    G.state = win ? S_WIN : S_FAIL;
    G.st = 0.0f;
}

/* ------------------------------------------------------------- the bot -- */
/* Review captures only: a player who aims for the best gate and turns to
   meet whatever is about to reach the defence line. */
static float gate_factor(const Gate *g, float flow) {
    if (g->type == G_MUL) return (float)g->val;
    if (g->type == G_ADD) return 1.0f + (float)g->val / (1.1f * flow);
    return fmaxf(0.2f, 1.0f - (float)g->val / (1.3f * flow));
}

static float bot_target(void) {
    float sum = 0.0f; int n = 0;
    for (int i = 0; i < g_nred; i++) if (g_red[i].z < 15.0f) { sum += g_red[i].x; n++; }
    for (int i = 0; i < g_nbrute; i++) if (g_brute[i].z < 16.0f) { sum += g_brute[i].x * 12.0f; n += 12; }
    if (n >= 6) return sum / (float)n;
    /* the straight column through every row that grows the crowd most */
    float best = -1.0f, bx = 0.0f;
    for (float x = -TRACK_HALF + 0.8f; x <= TRACK_HALF - 0.8f; x += 0.2f) {
        float flow = fire_rate(G.up_fire), v = 1.0f;
        for (int i = 0; i < g_ngate; i++) {
            const Gate *g = &g_gate[i];
            /* where a moving panel will be when the stream gets there */
            float eta = (g->z - CANNON_Z) / BLUE_SPEED;
            float gx = g->amp > 0.0f ? g->x0 + g->amp * sinf((G.t + eta) * g->spd + g->ph) : g->x;
            if (fabsf(x - gx) < g->w * 0.5f - 0.3f) { float f = gate_factor(g, flow); v *= f; flow *= f; }
        }
        v -= fabsf(x - G.cannon_x) * 0.01f;
        if (v > best) { best = v; bx = x; }
    }
    return bx;
}

/* ---------------------------------------------------------- simulation -- */
static void gate_pass(Mob *m, float pz) {
    for (int gi = 0; gi < g_ngate; gi++) {
        Gate *g = &g_gate[gi];
        if (!(pz < g->z && m->z >= g->z)) continue;
        if (fabsf(m->x - g->x) > g->w * 0.5f) continue;
        uint16_t bit = (uint16_t)(1u << gi);
        if (m->gates & bit) continue;
        m->gates |= bit;
        if (g->type == G_MUL) {
            float x = m->x, z = m->z; uint16_t gates = m->gates;
            m->fresh = 0.5f;
            for (int k = 1; k < g->val; k++) {
                float off = (float)((k + 1) / 2) * 0.42f * ((k & 1) ? 1.0f : -1.0f);
                Mob *c = blue_new(pa_clampf(x + off, g->x - g->w * 0.5f + 0.1f, g->x + g->w * 0.5f - 0.1f), z + frand(0.0f, 0.25f));
                if (!c) break;
                c->gates = gates; c->fresh = 0.5f; c->vx = off * 2.4f;
            }
            g->pulse = 1.0f;
            snd_gate(g->val);
            if (pa_rng_chance(&G.rng, 0.25f)) sparks(x, 0.8f, g->z, PA_RGB(200, 235, 255), 2);
        } else if (g->type == G_ADD) {
            if (g->cool <= 0.0f) {
                float x = m->x, z = m->z; uint16_t gates = m->gates;
                for (int k = 0; k < g->val; k++) {
                    Mob *c = blue_new(pa_clampf(x + frand(-1.0f, 1.0f), -TRACK_HALF + 0.3f, TRACK_HALF - 0.3f), z + frand(0.0f, 0.8f));
                    if (!c) break;
                    c->gates = gates; c->fresh = 0.5f; c->vx = frand(-1.0f, 1.0f);
                }
                char buf[16]; snprintf(buf, sizeof(buf), "+%d", g->val);
                float_text(g->x, 1.9f, g->z, buf, PA_RGB(255, 255, 255), 1.0f);
                ring(x, z, PA_RGB(120, 200, 255), 1.6f);
                g->cool = 1.1f; g->pulse = 1.0f;
                pa_tone(600.0f, 1200.0f, 0.12f, 1, 0.08f);
            }
        } else {
            if (g->hunger > 0) {
                m->dead = 1;
                g->hunger--;
                g->pulse = 1.0f;
                puff(m->x, m->z, PA_RGB(255, 120, 140), 2, 1.2f);
                if (g->hunger == 0) g->cool = 1.3f;
                if (G.sfx_gate <= 0.0f) { G.sfx_gate = 0.08f; pa_tone(300.0f, 180.0f, 0.06f, 3, 0.05f); }
            }
        }
        if (m->dead) return;
    }
}

static void sim_step(float dt, const PA_Input *in, int live) {
    G.t += dt;
    if (live) {
        /* ---- input ---- */
        int fire = 0;
        float px_per = ss(CANNON_Z);
        if (in->down) { fire = 1; G.cannon_tx += in->dx / px_per * 1.1f; }
        if (in->keys[PA_KEY_SPACE] || in->keys[PA_KEY_UP]) fire = 1;
        if (in->keys[PA_KEY_LEFT]) { G.cannon_tx -= 9.0f * dt; fire = 1; }
        if (in->keys[PA_KEY_RIGHT]) { G.cannon_tx += 9.0f * dt; fire = 1; }
        if (G.demo) {
            fire = 1;
            float tx = bot_target();
            G.bot_wob += dt;
            tx += 0.25f * sinf(G.bot_wob * 1.9f);
            if (G.demo == 3) {
                /* the losing run: hesitant thumb, aims at the traps */
                fire = fmodf(G.t, 2.6f) < 0.7f;
                tx = -tx;
            }
            G.cannon_tx = pa_approach(G.cannon_tx, tx, 6.0f, dt);
        }
        G.cannon_tx = pa_clampf(G.cannon_tx, -TRACK_HALF + 0.75f, TRACK_HALF - 0.75f);
        G.cannon_x = pa_approach(G.cannon_x, G.cannon_tx, 24.0f, dt);
        G.firing = fire;

        /* ---- fire ---- */
        if (fire) {
            G.fire_acc += dt * fire_rate(G.up_fire);
            while (G.fire_acc >= 1.0f) {
                G.fire_acc -= 1.0f;
                Mob *m = blue_new(G.cannon_x + frand(-0.25f, 0.25f), CANNON_Z + 0.55f + frand(0.0f, 0.3f));
                if (m) m->vx = frand(-1.5f, 1.5f) + (G.cannon_tx - G.cannon_x) * 0.6f;
                G.recoil = 1.0f;
                snd_fire();
                G.charge += 1.0f;
                if (G.charge >= G.charge_max) {
                    G.charge = 0.0f;
                    G.charge_flash = 1.0f;
                    spawn_champ(G.cannon_x, CANNON_Z + 0.8f, 0, (float)champ_hp(G.up_champ));
                    float_text(G.cannon_x, 3.0f, CANNON_Z + 1.0f, "CHAMPION!", PA_RGB(255, 214, 64), 0.8f);
                    ring(G.cannon_x, CANNON_Z + 0.8f, PA_RGB(255, 220, 90), 2.6f);
                    kick(6.0f);
                    pa_tone(220.0f, 880.0f, 0.35f, 3, 0.10f);
                }
            }
        } else if (G.fire_acc < 0.85f) G.fire_acc = pa_approach(G.fire_acc, 0.85f, 6.0f, dt);

        /* ---- enemy waves ---- */
        G.wave_timer -= dt;
        if (G.wave_timer <= 0.0f) {
            G.wave_left += (int)(G.red_rate * G.wave_gap * (1.0f + 0.04f * (float)G.wave_i));
            if (G.wave_left > G.red_cap) G.wave_left = G.red_cap;
            G.wave_i++;
            G.wave_timer = G.wave_gap;
            if (G.brute_hp > 0.0f && G.wave_i % G.brute_every == 0)
                spawn_brute(frand(-2.0f, 2.0f), floorf(G.brute_hp), 0);
        }
        if (G.wave_left > 0) {
            G.wave_acc += dt * fmaxf(40.0f, G.red_rate * G.wave_gap / 0.6f);
            while (G.wave_acc >= 1.0f && G.wave_left > 0 && g_nred < G.red_cap) {
                G.wave_acc -= 1.0f; G.wave_left--;
                red_new(frand(-3.6f, 3.6f), TRACK_LEN - frand(0.3f, 3.2f));
            }
        }
        if (!G.boss_spawned && G.t >= G.boss_timer) {
            G.boss_spawned = 1;
            spawn_brute(0.0f, floorf(G.brute_hp * 5.0f), 1);
            float_text(0.0f, 4.5f, TRACK_LEN - 2.0f, "BOSS!", PA_RGB(255, 90, 80), 1.6f);
            kick(5.0f);
        }
    } else {
        G.firing = 0;
    }

    G.recoil = pa_approach(G.recoil, 0.0f, 14.0f, dt);

    /* ---- gates ---- */
    for (int i = 0; i < g_ngate; i++) {
        Gate *g = &g_gate[i];
        if (g->amp > 0.0f) g->x = g->x0 + g->amp * sinf(G.t * g->spd + g->ph);
        g->pulse = pa_approach(g->pulse, 0.0f, 7.0f, dt);
        if (g->cool > 0.0f) { g->cool -= dt; if (g->cool <= 0.0f) g->hunger = g->val; }
    }

    build_grids();

    /* ---- blue crowd ---- */
    for (int i = 0; i < g_nblue; i++) {
        Mob *m = &g_blue[i];
        float pz = m->z;
        int r0 = row_of(m->z);
        float want = 0.0f, rate = 1.1f;
        for (int r = r0; r < r0 + 10 && r < GZ; r++) {
            if (g_rrow_n[r] > 0) { want = pa_clampf((g_rrow_x[r] / (float)g_rrow_n[r] - m->x) * 1.6f, -2.4f, 2.4f); rate = 3.0f; break; }
        }
        m->vx = pa_approach(m->vx, want, rate, dt);
        m->x += m->vx * dt;
        m->z += BLUE_SPEED * dt;
        if (m->x < -TRACK_HALF + 0.3f) { m->x = -TRACK_HALF + 0.3f; m->vx = fabsf(m->vx); }
        if (m->x > TRACK_HALF - 0.3f) { m->x = TRACK_HALF - 0.3f; m->vx = -fabsf(m->vx); }
        m->ph += dt * 9.0f;
        if (m->fresh > 0.0f) m->fresh -= dt;
        gate_pass(m, pz);
        if (!m->dead && m->z >= TRACK_LEN - 0.3f) {
            m->dead = 1;
            if (G.base_hp > 0.0f) {
                G.base_hp -= 1.0f;
                G.base_flash = 1.0f;
                G.base_shake = fminf(1.0f, G.base_shake + 0.15f);
                if (pa_rng_chance(&G.rng, 0.4f)) sparks(m->x, 0.6f, TRACK_LEN, PA_RGB(255, 240, 200), 2);
                snd_base();
            }
        }
    }

    /* ---- red crowd ---- */
    for (int i = 0; i < g_nred; i++) {
        Mob *m = &g_red[i];
        int r0 = row_of(m->z);
        float want = 0.0f;
        for (int r = r0; r > r0 - 10 && r >= 0; r--) {
            if (g_brow_n[r] > 0) { want = pa_clampf((g_brow_x[r] / (float)g_brow_n[r] - m->x) * 1.4f, -2.0f, 2.0f); break; }
        }
        m->vx = pa_approach(m->vx, want, 3.0f, dt);
        m->x = pa_clampf(m->x + m->vx * dt, -TRACK_HALF + 0.3f, TRACK_HALF - 0.3f);
        m->z -= G.red_speed * dt;
        m->ph += dt * 7.5f;
        if (m->z < DEFENCE_Z && live) {
            m->dead = 1;
            G.cannon_hp--;
            G.cannon_flash = 1.0f;
            kick(4.0f);
            puff(m->x, m->z, PA_RGB(255, 90, 80), 4, 1.5f);
            pa_noise(0.10f, 0.12f);
        }
    }

    /* ---- crowd vs crowd: one for one ---- */
    for (int i = 0; i < g_nblue; i++) {
        Mob *b = &g_blue[i];
        if (b->dead) continue;
        int c = cell_of(b->x, b->z), cx = c % GX, cz = c / GX;
        for (int dz = -1; dz <= 1 && !b->dead; dz++) {
            int zz = cz + dz; if (zz < 0 || zz >= GZ) continue;
            for (int dx = -1; dx <= 1 && !b->dead; dx++) {
                int xx = cx + dx; if (xx < 0 || xx >= GX) continue;
                for (int j = g_rhead[zz * GX + xx]; j >= 0; j = g_rnext[j]) {
                    Mob *r = &g_red[j];
                    if (r->dead) continue;
                    float ddx = r->x - b->x, ddz = r->z - b->z;
                    if (ddx * ddx + ddz * ddz < HIT_R * HIT_R) {
                        r->dead = 1; b->dead = 1;
                        G.kills++;
                        float mx = (r->x + b->x) * 0.5f, mz = (r->z + b->z) * 0.5f;
                        puff(mx, mz, (G.kills & 1) ? PA_RGB(120, 190, 255) : PA_RGB(255, 120, 110), 1, 1.4f);
                        if ((G.kills & 3) == 0) sparks(mx, 0.4f, mz, PA_RGB(255, 255, 255), 1);
                        snd_kill();
                        break;
                    }
                }
            }
        }
    }

    /* ---- light separation so a crowd reads as a blob, not a stack ---- */
    for (int i = 0; i < g_nblue; i++) {
        Mob *b = &g_blue[i];
        if (b->dead) continue;
        int c = cell_of(b->x, b->z);
        for (int j = g_bhead[c]; j >= 0; j = g_bnext[j]) {
            if (j == i) continue;
            Mob *o = &g_blue[j];
            float ddx = b->x - o->x, ddz = b->z - o->z;
            float d2 = ddx * ddx + ddz * ddz;
            if (d2 < 0.36f * 0.36f && d2 > 1e-6f) {
                float d = sqrtf(d2), push = (0.36f - d) * 0.5f / d;
                b->x += ddx * push; b->z += ddz * push * 0.5f;
            } else if (d2 <= 1e-6f) b->x += (i & 1) ? 0.05f : -0.05f;
        }
        b->x = pa_clampf(b->x, -TRACK_HALF + 0.3f, TRACK_HALF - 0.3f);
    }
    for (int i = 0; i < g_nred; i++) {
        Mob *b = &g_red[i];
        if (b->dead) continue;
        int c = cell_of(b->x, b->z);
        for (int j = g_rhead[c]; j >= 0; j = g_rnext[j]) {
            if (j == i) continue;
            Mob *o = &g_red[j];
            float ddx = b->x - o->x, ddz = b->z - o->z;
            float d2 = ddx * ddx + ddz * ddz;
            if (d2 < 0.36f * 0.36f && d2 > 1e-6f) {
                float d = sqrtf(d2), push = (0.36f - d) * 0.5f / d;
                b->x += ddx * push; b->z += ddz * push * 0.5f;
            }
        }
        b->x = pa_clampf(b->x, -TRACK_HALF + 0.3f, TRACK_HALF - 0.3f);
    }

    /* ---- enemy brutes ---- */
    for (int i = 0; i < g_nbrute; i++) {
        Brute *b = &g_brute[i];
        b->z -= b->speed * dt;
        b->ph += dt * 4.0f;
        b->flash = pa_approach(b->flash, 0.0f, 10.0f, dt);
        float rad = 0.75f * b->size;
        int c0 = cell_of(b->x - rad, b->z - rad), c1 = cell_of(b->x + rad, b->z + rad);
        int x0 = c0 % GX, z0 = c0 / GX, x1 = c1 % GX, z1 = c1 / GX;
        for (int zz = z0; zz <= z1; zz++) for (int xx = x0; xx <= x1; xx++) {
            for (int j = g_bhead[zz * GX + xx]; j >= 0; j = g_bnext[j]) {
                Mob *m = &g_blue[j];
                if (m->dead) continue;
                float ddx = m->x - b->x, ddz = m->z - b->z;
                if (ddx * ddx + ddz * ddz < rad * rad && b->hp > 0.0f) {
                    m->dead = 1; b->hp -= 1.0f; b->flash = 1.0f;
                    puff(m->x, m->z, PA_RGB(140, 200, 255), 1, 1.0f);
                    snd_kill();
                }
            }
        }
        if (b->hp <= 0.0f) {
            bits(b->x, 1.2f * b->size, b->z, PA_RGB(255, 206, 60), 14, 4.0f, 5.0f);
            bits(b->x, 1.2f * b->size, b->z, PA_RGB(230, 50, 50), 8, 4.0f, 5.0f);
            ring(b->x, b->z, PA_RGB(255, 230, 140), 2.5f * b->size);
            kick(b->boss ? 10.0f : 5.0f);
            pa_sfx("boom");
            G.kills += 5;
        } else if (b->z < DEFENCE_Z + 0.2f && live) {
            G.cannon_hp -= b->boss ? 99 : 6;
            G.cannon_flash = 1.0f;
            b->hp = 0.0f;
            kick(12.0f);
            pa_sfx("boom");
        }
    }

    /* ---- champions ---- */
    for (int i = 0; i < g_nchamp; i++) {
        Brute *b = &g_champ[i];
        b->ph += dt * 5.0f;
        b->flash = pa_approach(b->flash, 0.0f, 10.0f, dt);
        /* walk at whatever brute is closest ahead, else up the middle of the reds */
        float want = 0.0f, best = 12.0f;
        int fighting = 0;
        for (int j = 0; j < g_nbrute; j++) {
            Brute *e = &g_brute[j];
            float dz = e->z - b->z;
            if (dz > -0.5f && dz < best) { best = dz; want = pa_clampf((e->x - b->x) * 1.2f, -2.0f, 2.0f); }
            float ddx = e->x - b->x;
            float reach = 0.8f * (e->size + b->size);
            if (fabsf(dz) < reach && fabsf(ddx) < reach && e->hp > 0.0f) {
                fighting = 1;
                float dmg = 30.0f * dt;
                e->hp -= dmg; b->hp -= dmg; e->flash = b->flash = 1.0f;
                if (pa_rng_chance(&G.rng, 0.3f)) sparks((e->x + b->x) * 0.5f, 1.4f, (e->z + b->z) * 0.5f, PA_RGB(255, 240, 160), 2);
            }
        }
        if (best >= 12.0f) {
            int r0 = row_of(b->z);
            for (int r = r0; r < r0 + 12 && r < GZ; r++)
                if (g_rrow_n[r] > 0) { want = pa_clampf((g_rrow_x[r] / (float)g_rrow_n[r] - b->x) * 1.2f, -1.8f, 1.8f); break; }
        }
        b->x = pa_clampf(b->x + want * dt, -TRACK_HALF + 0.7f, TRACK_HALF - 0.7f);
        if (!fighting) b->z += b->speed * dt;
        /* trample reds */
        float rad = 0.8f * b->size;
        int c0 = cell_of(b->x - rad, b->z - rad), c1 = cell_of(b->x + rad, b->z + rad);
        int x0 = c0 % GX, z0 = c0 / GX, x1 = c1 % GX, z1 = c1 / GX;
        for (int zz = z0; zz <= z1; zz++) for (int xx = x0; xx <= x1; xx++) {
            for (int j = g_rhead[zz * GX + xx]; j >= 0; j = g_rnext[j]) {
                Mob *m = &g_red[j];
                if (m->dead) continue;
                float ddx = m->x - b->x, ddz = m->z - b->z;
                if (ddx * ddx + ddz * ddz < rad * rad && b->hp > 0.0f) {
                    m->dead = 1; b->hp -= 1.0f; b->flash = 1.0f; G.kills++;
                    puff(m->x, m->z, PA_RGB(255, 120, 110), 2, 2.0f);
                    snd_kill();
                }
            }
        }
        if (b->z >= TRACK_LEN - 0.6f && b->hp > 0.0f) {
            float dmg = fmaxf(10.0f, b->hp * 1.5f);
            G.base_hp -= dmg; G.base_flash = 1.0f; G.base_shake = 1.0f;
            char buf[16]; snprintf(buf, sizeof(buf), "-%d", (int)dmg);
            float_text(b->x, 3.8f, TRACK_LEN - 0.5f, buf, PA_RGB(255, 236, 90), 1.5f);
            bits(b->x, 1.5f, TRACK_LEN - 0.2f, PA_RGB(240, 90, 80), 16, 4.0f, 5.0f);
            bits(b->x, 1.5f, TRACK_LEN - 0.2f, PA_RGB(250, 245, 240), 10, 4.0f, 5.0f);
            kick(9.0f);
            pa_sfx("boom");
            b->hp = 0.0f;
        } else if (b->hp <= 0.0f) {
            bits(b->x, 1.0f, b->z, PA_RGB(90, 170, 255), 10, 3.0f, 4.0f);
            puff(b->x, b->z, PA_RGB(200, 230, 255), 4, 2.0f);
        }
    }

    compact();

    /* ---- outcome ---- */
    if (live) {
        if (G.base_hp <= 0.0f) {
            G.base_hp = 0.0f;
            for (int i = 0; i < g_nred; i++) puff(g_red[i].x, g_red[i].z, PA_RGB(255, 140, 130), 1, 1.5f);
            for (int i = 0; i < g_nbrute; i++) bits(g_brute[i].x, 1.0f, g_brute[i].z, PA_RGB(255, 206, 60), 8, 3.0f, 4.0f);
            g_nred = 0; g_nbrute = 0;
            for (int k = 0; k < 6; k++) {
                bits(frand(-4.0f, 4.0f), frand(1.0f, 3.5f), TRACK_LEN + frand(0.0f, 2.5f), PA_RGB(236, 76, 70), 8, 6.0f, 7.0f);
                bits(frand(-4.0f, 4.0f), frand(1.0f, 3.5f), TRACK_LEN + frand(0.0f, 2.5f), PA_RGB(250, 246, 240), 6, 6.0f, 7.0f);
            }
            kick(16.0f);
            pa_sfx("boom");
            pa_sfx("win");
            finish(1);
        } else if (G.cannon_hp <= 0) {
            G.cannon_hp = 0;
            puff(G.cannon_x, CANNON_Z, PA_RGB(80, 80, 90), 10, 2.5f);
            bits(G.cannon_x, 0.8f, CANNON_Z, PA_RGB(60, 140, 255), 14, 4.0f, 6.0f);
            kick(14.0f);
            pa_sfx("lose");
            finish(0);
        }
    }
}

static void tick_effects(float dt) {
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *p = &g_part[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        p->x += p->vx * dt; p->z += p->vz * dt; p->y += p->vy * dt;
        if (p->kind == PK_BIT || p->kind == PK_SPARK) {
            p->vy -= 14.0f * dt;
            if (p->y < 0.0f) { p->y = 0.0f; p->vy *= -0.35f; p->vx *= 0.6f; p->vz *= 0.6f; }
        } else if (p->kind == PK_PUFF) {
            p->vx *= 1.0f - 4.0f * dt; p->vz *= 1.0f - 4.0f * dt; p->vy *= 1.0f - 3.0f * dt;
        }
    }
    for (int i = 0; i < MAX_FLOATS; i++) if (g_float[i].t < 9.0f) g_float[i].t += dt;
    G.shake = pa_approach(G.shake, 0.0f, 9.0f, dt);
    G.base_flash = pa_approach(G.base_flash, 0.0f, 10.0f, dt);
    G.base_shake = pa_approach(G.base_shake, 0.0f, 6.0f, dt);
    G.cannon_flash = pa_approach(G.cannon_flash, 0.0f, 5.0f, dt);
    G.charge_flash = pa_approach(G.charge_flash, 0.0f, 3.0f, dt);
    G.sfx_kill -= dt; G.sfx_base -= dt; G.sfx_fire -= dt; G.sfx_gate -= dt;
    for (int i = 0; i < 3; i++) G.shop_flash[i] = pa_approach(G.shop_flash[i], 0.0f, 4.0f, dt);
}

/* ------------------------------------------------------------ UI rects -- */
static Rect r_ready_upgrades(void) {
    float bw = 150.0f * g_u, bh = 58.0f * g_u;
    Rect r = { g_w - bw - 16.0f * g_u, g_h - bh - 22.0f * g_u, bw, bh };
    return r;
}
static Rect r_card(void) {
    float cw = fminf(g_w - 40.0f * g_u, 470.0f * g_u), ch = 430.0f * g_u;
    Rect r = { (g_w - cw) * 0.5f, g_h * 0.5f - ch * 0.42f, cw, ch };
    return r;
}
static Rect r_result_btn(int which) {
    Rect c = r_card();
    float bw = c.w * 0.74f, bh = 70.0f * g_u;
    Rect r = { c.x + (c.w - bw) * 0.5f, c.y + c.h - bh * (which == 0 ? 2.45f : 1.25f), bw, bh * (which == 0 ? 1.0f : 0.86f) };
    if (which == 1) { r.w *= 0.8f; r.x = c.x + (c.w - r.w) * 0.5f; }
    return r;
}
static float shop_top(void) { return 210.0f * g_u; }
static Rect r_shop_card(int i) {
    float cw = fminf(g_w - 32.0f * g_u, 500.0f * g_u), ch = 128.0f * g_u, gap = 18.0f * g_u;
    Rect r = { (g_w - cw) * 0.5f, shop_top() + (float)i * (ch + gap), cw, ch };
    return r;
}
static Rect r_shop_buy(int i) {
    Rect c = r_shop_card(i);
    float bw = 140.0f * g_u, bh = 62.0f * g_u;
    Rect r = { c.x + c.w - bw - 16.0f * g_u, c.y + (c.h - bh) * 0.5f, bw, bh };
    return r;
}
static Rect r_shop_play(void) {
    float bw = fminf(g_w * 0.7f, 340.0f * g_u), bh = 84.0f * g_u;
    Rect r = { (g_w - bw) * 0.5f, g_h - bh - 60.0f * g_u, bw, bh };
    return r;
}

static void buy(int which) {
    if (!can_afford(which)) { pa_sfx("bad"); return; }
    if (which == 0) { G.coins -= cost_fire(G.up_fire); G.up_fire++; }
    else if (which == 1) { G.gems -= cost_champ(G.up_champ); G.up_champ++; }
    else { G.coins -= cost_income(G.up_income); G.up_income++; }
    G.shop_flash[which] = 1.0f;
    pa_sfx("levelup");
    save_all();
}

static void go_shop(void) { G.state = S_SHOP; G.st = 0.0f; pa_sfx("select"); }

/* ---------------------------------------------------------- game hooks -- */
static void s_start(void) {
    memset(&G, 0, sizeof(G));
    G.demo = pa_demo_mode();
    if (G.demo) {
        G.level = 1;
        switch (G.demo) {
            case 2: G.level = 7; G.coins = 864; G.gems = 23; G.up_fire = 7; G.up_champ = 2; G.up_income = 2; G.best = 6; break;
            case 3: G.level = 9; G.coins = 412; G.gems = 11; G.up_fire = 2; G.best = 8; break;
            case 4: G.level = 5; G.coins = 690; G.gems = 14; G.up_fire = 9; G.up_champ = 3; G.up_income = 1; G.best = 4; break;
            case 5: G.level = 8; G.coins = 1460; G.gems = 19; G.up_fire = 5; G.up_champ = 1; G.up_income = 1; G.best = 7; break;
            case 6: G.level = 15; G.coins = 3920; G.gems = 41; G.up_fire = 15; G.up_champ = 6; G.up_income = 4; G.best = 14; break;
            default: break;
        }
    } else {
        G.level = pa_save_get("mobclash.level", 1);
        if (G.level < 1) G.level = 1;
        G.coins = pa_save_get("mobclash.coins", 0);
        G.gems = pa_save_get("mobclash.gems", 0);
        G.best = pa_save_get("mobclash.best", 0);
        G.up_fire = pa_save_get("mobclash.up_fire", 0);
        G.up_champ = pa_save_get("mobclash.up_champ", 0);
        G.up_income = pa_save_get("mobclash.up_income", 0);
    }
    begin_ready();
    if (G.demo == 5) { G.state = S_SHOP; }
}

static void s_stop(void) { save_all(); }

static void s_update(float dt, const PA_Input *in) {
    G.st += dt;
    G.clock += dt;
    tick_effects(dt);
    G.press_t = pa_approach(G.press_t, 0.0f, 8.0f, dt);

    switch (G.state) {
    case S_READY:
        if (G.demo) {
            if (G.st > (G.demo == 1 ? 1.4f : 0.3f)) { G.state = S_PLAY; G.st = 0.0f; }
        } else if (in->pressed) {
            if (in_rect(r_ready_upgrades(), in->x, in->y)) go_shop();
            else { G.state = S_PLAY; G.st = 0.0f; pa_sfx("select"); }
        } else if (in->key_pressed[PA_KEY_SPACE] || in->key_pressed[PA_KEY_ENTER]) { G.state = S_PLAY; G.st = 0.0f; }
        break;
    case S_PLAY:
        sim_step(dt, in, 1);
        break;
    case S_WIN: case S_FAIL:
        sim_step(dt, in, 0);
        if (G.state == S_WIN) G.collapse = pa_clamp01(G.collapse + dt * 1.2f);
        if (G.st > (G.state == S_WIN ? 1.9f : 1.5f)) { G.state = S_RESULT; G.st = 0.0f; }
        break;
    case S_RESULT: {
        sim_step(dt, in, 0);
        float target = (float)G.reward_coins;
        if (G.st > 0.5f && G.shown_coins < target) {
            float prev = G.shown_coins;
            G.shown_coins = fminf(target, G.shown_coins + dt * fmaxf(target, 40.0f) / 0.9f);
            if ((int)(prev / 7.0f) != (int)(G.shown_coins / 7.0f)) pa_tone(1200.0f, 1700.0f, 0.03f, 1, 0.03f);
        }
        int act = -1;
        if (G.demo) { if (G.demo == 4 && G.st > 3.2f) act = 1; }
        else if (in->tapped && G.st > 0.6f) {
            if (in_rect(r_result_btn(0), in->x, in->y)) act = 0;
            else if (in_rect(r_result_btn(1), in->x, in->y)) act = 1;
        } else if (G.st > 0.6f && (in->key_pressed[PA_KEY_ENTER] || in->key_pressed[PA_KEY_SPACE])) act = 0;
        if (act == 0) { pa_sfx("select"); begin_ready(); }
        else if (act == 1) go_shop();
        break;
    }
    case S_SHOP: {
        int hit = -1;
        if (G.demo == 5) {
            if (G.st > 0.9f && G.st - dt <= 0.9f) hit = 0;
            if (G.st > 1.7f && G.st - dt <= 1.7f) hit = 1;
        } else if (G.demo == 4) {
            if (G.st > 1.0f && G.st - dt <= 1.0f) hit = 0;
        } else if (in->tapped) {
            for (int i = 0; i < 3; i++) if (in_rect(r_shop_buy(i), in->x, in->y)) hit = i;
            if (in_rect(r_shop_play(), in->x, in->y)) hit = 3;
        } else if (in->key_pressed[PA_KEY_ENTER]) hit = 3;
        if (hit >= 0 && hit < 3) { buy(hit); G.press_t = 1.0f; G.press_id = hit; }
        else if (hit == 3) { pa_sfx("select"); begin_ready(); }
        break;
    }
    default: break;
    }
}

/* =========================================================== RENDERING == */

/* ---- mob sprites: one premultiplied bitmap per team, scale and stride ---- */
#define NSPR 16
typedef struct { int w, h, ax, ay; uint8_t *a; uint32_t *c; } Spr;
static Spr   g_spr[T_COUNT][NSPR][2];
static float g_spr_k = -1.0f;

static float spr_height(int s) { return 8.0f + (float)s * 2.0f; }

static void team_cols(int team, PA_Color *mid, PA_Color *lite, PA_Color *dark) {
    if (team == T_RED) { *mid = PA_RGB(236, 52, 52); *lite = PA_RGB(255, 128, 112); *dark = PA_RGB(150, 22, 34); }
    else if (team == T_GLOW) { *mid = PA_RGB(150, 214, 255); *lite = PA_RGB(240, 252, 255); *dark = PA_RGB(70, 150, 245); }
    else { *mid = PA_RGB(42, 140, 255); *lite = PA_RGB(130, 206, 255); *dark = PA_RGB(22, 76, 196); }
}

/* One little mob person, foot at (fx, fy), H pixels tall. Blue run away from
   the camera so we see their backs; red run at it and show their eyes. */
static void draw_mob(PA_Canvas *c, float fx, float fy, float H, int team, int frame) {
    PA_Color mid, lite, dark;
    team_cols(team, &mid, &lite, &dark);
    float st = frame ? 1.0f : -1.0f;
    pa_fill_ellipse(c, fx, fy - H * 0.02f, H * 0.30f, H * 0.11f, PA_RGBA(30, 34, 70, 70));
    /* feet */
    pa_fill_ellipse(c, fx - H * 0.13f, fy - H * 0.07f - st * H * 0.04f, H * 0.095f, H * 0.075f, dark);
    pa_fill_ellipse(c, fx + H * 0.13f, fy - H * 0.07f + st * H * 0.04f, H * 0.095f, H * 0.075f, dark);
    /* arms swing against the feet */
    pa_fill_ellipse(c, fx - H * 0.25f, fy - H * 0.40f + st * H * 0.05f, H * 0.075f, H * 0.12f, pa_mix(mid, dark, 0.35f));
    pa_fill_ellipse(c, fx + H * 0.25f, fy - H * 0.40f - st * H * 0.05f, H * 0.075f, H * 0.12f, pa_mix(mid, dark, 0.45f));
    /* bean body */
    PA_Paint p = pa_linear(fx - H * 0.25f, fy - H * 0.75f, fx + H * 0.22f, fy - H * 0.08f);
    pa_stop(&p, 0.0f, lite); pa_stop(&p, 0.45f, mid); pa_stop(&p, 1.0f, dark);
    pa_round_rect_paint(c, fx - H * 0.22f, fy - H * 0.62f, H * 0.44f, H * 0.56f, H * 0.2f, &p);
    /* head */
    PA_Paint ph = pa_radial(fx - H * 0.07f, fy - H * 0.80f, 0.0f, H * 0.30f);
    pa_stop(&ph, 0.0f, lite); pa_stop(&ph, 0.55f, mid); pa_stop(&ph, 1.0f, pa_mix(mid, dark, 0.6f));
    pa_fill_circle(c, fx, fy - H * 0.72f, H * 0.23f, PA_RGB(0, 0, 0));
    pa_fill_ellipse_paint(c, fx, fy - H * 0.72f, H * 0.23f, H * 0.23f, &ph);
    /* little ears, the mob's silhouette */
    pa_fill_circle(c, fx - H * 0.17f, fy - H * 0.90f, H * 0.075f, mid);
    pa_fill_circle(c, fx + H * 0.17f, fy - H * 0.90f, H * 0.075f, pa_mix(mid, dark, 0.3f));
    pa_fill_ellipse(c, fx - H * 0.08f, fy - H * 0.82f, H * 0.07f, H * 0.045f, pa_alpha(PA_RGB(255, 255, 255), 0.65f));
    if (team == T_RED) {
        pa_fill_circle(c, fx - H * 0.08f, fy - H * 0.70f, H * 0.055f, PA_RGB(255, 255, 255));
        pa_fill_circle(c, fx + H * 0.08f, fy - H * 0.70f, H * 0.055f, PA_RGB(255, 255, 255));
        pa_fill_circle(c, fx - H * 0.075f, fy - H * 0.69f, H * 0.028f, PA_RGB(40, 10, 20));
        pa_fill_circle(c, fx + H * 0.085f, fy - H * 0.69f, H * 0.028f, PA_RGB(40, 10, 20));
    }
}

static void spr_free(void) {
    for (int t = 0; t < T_COUNT; t++) for (int s = 0; s < NSPR; s++) for (int f = 0; f < 2; f++) {
        free(g_spr[t][s][f].a); free(g_spr[t][s][f].c);
        g_spr[t][s][f].a = NULL; g_spr[t][s][f].c = NULL;
    }
    g_spr_k = -1.0f;
}

static void spr_build(void) {
    spr_free();
    for (int t = 0; t < T_COUNT; t++) for (int s = 0; s < NSPR; s++) for (int f = 0; f < 2; f++) {
        float H = spr_height(s);
        int w = (int)ceilf(H * 0.80f) + 4, h = (int)ceilf(H * 1.05f) + 4;
        PA_Canvas blk, wht;
        if (!pa_canvas_init(&blk, w, h)) continue;
        if (!pa_canvas_init(&wht, w, h)) { pa_canvas_free(&blk); continue; }
        pa_clear(&blk, PA_RGB(0, 0, 0));
        pa_clear(&wht, PA_RGB(255, 255, 255));
        float fx = (float)w * 0.5f, fy = (float)h - 2.0f;
        draw_mob(&blk, fx, fy, H, t, f);
        draw_mob(&wht, fx, fy, H, t, f);
        Spr *sp = &g_spr[t][s][f];
        sp->w = w; sp->h = h; sp->ax = w / 2; sp->ay = h - 2;
        sp->a = (uint8_t *)malloc((size_t)w * (size_t)h);
        sp->c = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
        if (sp->a && sp->c) {
            for (int i = 0; i < w * h; i++) {
                uint32_t b = blk.px[i], wv = wht.px[i];
                int db = (int)((wv >> 16) & 255) - (int)((b >> 16) & 255);
                int dg = (int)((wv >> 8) & 255) - (int)((b >> 8) & 255);
                int dbb = (int)(wv & 255) - (int)(b & 255);
                int a = 255 - (db + dg + dbb) / 3;
                sp->a[i] = (uint8_t)(a < 0 ? 0 : (a > 255 ? 255 : a));
                sp->c[i] = b & 0x00FFFFFFu;
            }
        }
        pa_canvas_free(&blk); pa_canvas_free(&wht);
    }
    g_spr_k = g_k;
}

static void blit(PA_Canvas *c, const Spr *s, int x, int y) {
    if (!s->a) return;
    int x0 = x - s->ax, y0 = y - s->ay;
    int sx0 = 0, sy0 = 0, w = s->w, h = s->h;
    if (x0 < c->clip_x0) { sx0 = c->clip_x0 - x0; }
    if (y0 < c->clip_y0) { sy0 = c->clip_y0 - y0; }
    if (x0 + w > c->clip_x1) w = c->clip_x1 - x0;
    if (y0 + h > c->clip_y1) h = c->clip_y1 - y0;
    for (int yy = sy0; yy < h; yy++) {
        uint32_t *dst = c->px + (size_t)(y0 + yy) * (size_t)c->w + x0;
        const uint8_t *ar = s->a + yy * s->w;
        const uint32_t *cr = s->c + yy * s->w;
        for (int xx = sx0; xx < w; xx++) {
            int a = ar[xx];
            if (!a) continue;
            if (a == 255) { dst[xx] = cr[xx]; continue; }
            uint32_t d = dst[xx], sc = cr[xx];
            int inv = 255 - a;
            uint32_t rb = ((d & 0xFF00FFu) * (uint32_t)inv >> 8) & 0xFF00FFu;
            uint32_t g = ((d & 0x00FF00u) * (uint32_t)inv >> 8) & 0x00FF00u;
            uint32_t o = (rb | g) + sc;
            /* the premultiplied add can't overflow a channel but guard rounding */
            dst[xx] = o & 0xFFFFFFu;
        }
    }
}

static const Spr *spr_for(int team, float z, int frame) {
    float H = UNIT_H * ss(z) * 1.18f;
    int s = (int)((H - 8.0f) * 0.5f + 0.5f);
    s = s < 0 ? 0 : (s >= NSPR ? NSPR - 1 : s);
    return &g_spr[team][s][frame & 1];
}

/* ---- world primitives ---- */
static void quad(PA_Canvas *c, PA_Vec2 a, PA_Vec2 b, PA_Vec2 cc, PA_Vec2 d, PA_Color col) {
    PA_Vec2 p[4] = { a, b, cc, d };
    pa_fill_poly(c, p, 4, col);
}

/* An axis aligned box on the ground: top, front, and whichever side faces the
   camera. */
static void box(PA_Canvas *c, float x0, float x1, float z0, float z1, float y0, float y1,
                PA_Color top, PA_Color front, PA_Color side) {
    if (x1 < 0.0f) quad(c, proj(x1, y0, z0), proj(x1, y0, z1), proj(x1, y1, z1), proj(x1, y1, z0), side);
    if (x0 > 0.0f) quad(c, proj(x0, y0, z0), proj(x0, y0, z1), proj(x0, y1, z1), proj(x0, y1, z0), side);
    quad(c, proj(x0, y1, z0), proj(x1, y1, z0), proj(x1, y1, z1), proj(x0, y1, z1), top);
    quad(c, proj(x0, y0, z0), proj(x1, y0, z0), proj(x1, y1, z0), proj(x0, y1, z0), front);
}

static void draw_coin_icon(PA_Canvas *c, float x, float y, float r) {
    pa_fill_circle(c, x, y + r * 0.12f, r, PA_RGB(196, 110, 16));
    pa_fill_circle(c, x, y, r, PA_RGB(255, 196, 40));
    pa_fill_circle(c, x, y, r * 0.72f, PA_RGB(244, 160, 24));
    pa_fill_circle(c, x - r * 0.06f, y - r * 0.06f, r * 0.62f, PA_RGB(255, 206, 60));
    pa_round_rect(c, x - r * 0.12f, y - r * 0.42f, r * 0.24f, r * 0.84f, r * 0.1f, PA_RGB(214, 128, 18));
    pa_fill_ellipse(c, x - r * 0.38f, y - r * 0.42f, r * 0.2f, r * 0.12f, PA_RGBA(255, 255, 255, 170));
}

static void draw_gem_icon(PA_Canvas *c, float x, float y, float r) {
    PA_Vec2 hex[6];
    for (int i = 0; i < 6; i++) {
        float a = PA_TAU * (float)i / 6.0f + PA_PI / 6.0f;
        hex[i].x = x + cosf(a) * r; hex[i].y = y + sinf(a) * r;
    }
    PA_Vec2 o[6];
    for (int i = 0; i < 6; i++) { o[i].x = x + (hex[i].x - x) * 1.18f; o[i].y = y + (hex[i].y - y) * 1.18f; }
    pa_fill_poly(c, o, 6, PA_RGB(14, 40, 120));
    pa_fill_poly(c, hex, 6, PA_RGB(40, 110, 240));
    /* cube faces */
    PA_Vec2 topf[4] = { hex[4], hex[5], { x, y }, hex[3] };
    pa_fill_poly(c, topf, 4, PA_RGB(120, 200, 255));
    PA_Vec2 lf[4] = { hex[3], { x, y }, hex[1], hex[2] };
    pa_fill_poly(c, lf, 4, PA_RGB(56, 140, 250));
    PA_Vec2 rf[4] = { { x, y }, hex[5], hex[0], hex[1] };
    pa_fill_poly(c, rf, 4, PA_RGB(30, 86, 210));
}

/* Pill counter: gems top-left in blue, coins top-right in yellow. */
static void draw_pill(PA_Canvas *c, float x, float y, float w, float h, int gem, int value) {
    PA_Color a = gem ? PA_RGB(64, 210, 255) : PA_RGB(255, 222, 70);
    PA_Color b = gem ? PA_RGB(22, 140, 240) : PA_RGB(255, 172, 20);
    pa_round_rect(c, x - 3.0f, y - 3.0f + h * 0.08f, w + 6.0f, h + 6.0f, (h + 6.0f) * 0.5f, PA_RGBA(20, 16, 60, 120));
    pa_round_rect(c, x - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f, (h + 6.0f) * 0.5f, C_INK);
    PA_Paint p = pa_linear(0, y, 0, y + h);
    pa_stop(&p, 0.0f, a); pa_stop(&p, 1.0f, b);
    pa_round_rect_paint(c, x, y, w, h, h * 0.5f, &p);
    pa_round_rect(c, x + h * 0.4f, y + h * 0.1f, w - h * 0.8f, h * 0.22f, h * 0.11f, PA_RGBA(255, 255, 255, 80));
    float ix = gem ? x + h * 0.55f : x + w - h * 0.55f;
    if (gem) draw_gem_icon(c, ix, y + h * 0.5f, h * 0.34f);
    else draw_coin_icon(c, ix, y + h * 0.5f, h * 0.36f);
    char buf[24]; fmt_num(value, buf, sizeof(buf));
    float ts = h * 0.46f;
    float tx = gem ? x + h * 1.05f : x + w - h * 1.05f;
    txt(c, buf, tx, y + h * 0.52f, ts, PA_RGB(255, 255, 255), gem ? PA_ALIGN_LEFT : PA_ALIGN_RIGHT, 1.3f);
}

/* ---- the scenery ---- */
static void draw_ground(PA_Canvas *c) {
    PA_Paint p = pa_linear(0, 0, 0, g_h);
    pa_stop(&p, 0.0f, PA_RGB(214, 218, 232));
    pa_stop(&p, 0.45f, PA_RGB(196, 201, 220));
    pa_stop(&p, 1.0f, PA_RGB(176, 182, 204));
    pa_fill_rect_paint(c, 0, 0, g_w, g_h, &p);
    /* faint field stripes for depth */
    for (int i = 0; i < 18; i++) {
        float z0 = -6.0f + (float)i * 6.0f, z1 = z0 + 3.0f;
        float y0 = sy(z1), y1 = sy(z0);
        if (y1 < 0.0f || y0 > g_h) continue;
        pa_fill_rect(c, 0, y0, g_w, y1 - y0, PA_RGBA(255, 255, 255, 18));
    }
}

static void draw_pine(PA_Canvas *c, float x, float z, float s) {
    float k = ss(z);
    PA_Vec2 b = proj(x, 0.0f, z);
    float h = 2.6f * s * k * VK, w = 0.9f * s * k;
    pa_shadow(c, b.x + w * 0.3f, b.y, w * 0.9f, w * 0.28f, 0.35f);
    pa_fill_rect(c, b.x - w * 0.1f, b.y - h * 0.22f, w * 0.2f, h * 0.22f, PA_RGB(128, 120, 132));
    for (int t = 0; t < 3; t++) {
        float ty = b.y - h * (0.18f + 0.26f * (float)t);
        float tw = w * (1.0f - 0.24f * (float)t);
        float th = h * 0.42f;
        PA_Vec2 l[3] = { { b.x, ty - th }, { b.x - tw, ty }, { b.x, ty + th * 0.08f } };
        PA_Vec2 r[3] = { { b.x, ty - th }, { b.x, ty + th * 0.08f }, { b.x + tw, ty } };
        pa_fill_poly(c, l, 3, PA_RGB(150, 172, 170));
        pa_fill_poly(c, r, 3, PA_RGB(116, 138, 142));
    }
    /* snow-pale tips, the plates' misty pines */
    PA_Vec2 tip[3] = { { b.x, b.y - h * 0.94f }, { b.x - w * 0.22f, b.y - h * 0.80f }, { b.x + w * 0.22f, b.y - h * 0.80f } };
    pa_fill_poly(c, tip, 3, PA_RGB(214, 226, 230));
}

static void draw_rock(PA_Canvas *c, float x, float z, float s) {
    float k = ss(z);
    PA_Vec2 b = proj(x, 0.0f, z);
    float w = 0.7f * s * k, h = 0.45f * s * k * VK;
    pa_shadow(c, b.x, b.y, w * 1.1f, w * 0.3f, 0.3f);
    PA_Vec2 r[6] = { { b.x - w, b.y }, { b.x - w * 0.7f, b.y - h * 0.8f }, { b.x - w * 0.1f, b.y - h * 1.1f },
                     { b.x + w * 0.6f, b.y - h * 0.8f }, { b.x + w, b.y }, { b.x, b.y + h * 0.1f } };
    pa_fill_poly(c, r, 6, PA_RGB(156, 158, 176));
    PA_Vec2 hi[4] = { { b.x - w * 0.7f, b.y - h * 0.8f }, { b.x - w * 0.1f, b.y - h * 1.1f }, { b.x + w * 0.2f, b.y - h * 0.5f }, { b.x - w * 0.5f, b.y - h * 0.35f } };
    pa_fill_poly(c, hi, 4, PA_RGB(190, 192, 208));
}

static void draw_track(PA_Canvas *c) {
    float zn = -12.0f, zf = TRACK_LEN + 4.0f, H = TRACK_HALF;
    /* soft drop shadow either side so the slab sits on the ground */
    quad(c, proj(-H - 0.9f, 0, zn), proj(-H, 0, zn), proj(-H, 0, zf), proj(-H - 0.9f, 0, zf), PA_RGBA(80, 84, 120, 40));
    quad(c, proj(H, 0, zn), proj(H + 0.9f, 0, zn), proj(H + 0.9f, 0, zf), proj(H, 0, zf), PA_RGBA(80, 84, 120, 40));
    /* slab side faces */
    quad(c, proj(-H - 0.35f, 0.0f, zn), proj(-H - 0.35f, 0.0f, zf), proj(-H - 0.35f, 0.32f, zf), proj(-H - 0.35f, 0.32f, zn), PA_RGB(150, 152, 168));
    PA_Vec2 a = proj(-H, 0, zn), b = proj(H, 0, zn), cc = proj(H, 0, zf), d = proj(-H, 0, zf);
    PA_Vec2 poly[4] = { a, b, cc, d };
    PA_Paint p = pa_linear(0, d.y, 0, a.y);
    pa_stop(&p, 0.0f, PA_RGB(226, 227, 233));
    pa_stop(&p, 1.0f, PA_RGB(206, 207, 214));
    pa_fill_poly_paint(c, poly, 4, &p);
    /* scuffs */
    for (int i = 0; i < MAX_SCUFF; i++) {
        PA_Vec2 s = proj(g_scuff[i][0], 0, g_scuff[i][1]);
        float k = ss(g_scuff[i][1]);
        pa_fill_ellipse(c, s.x, s.y, g_scuff[i][2] * k * 0.6f, g_scuff[i][2] * k * 0.12f, PA_RGBA(120, 120, 140, 22));
    }
    /* centre dashes */
    for (float z = -6.0f; z < TRACK_LEN - 2.0f; z += 3.2f) {
        quad(c, proj(-0.07f, 0, z), proj(0.07f, 0, z), proj(0.07f, 0, z + 1.5f), proj(-0.07f, 0, z + 1.5f), PA_RGBA(255, 255, 255, 200));
    }
    /* lane lines */
    for (int s = -1; s <= 1; s += 2) {
        float x = (float)s * (H - 0.55f);
        quad(c, proj(x - 0.05f, 0, zn), proj(x + 0.05f, 0, zn), proj(x + 0.05f, 0, zf), proj(x - 0.05f, 0, zf), PA_RGBA(255, 255, 255, 150));
    }
    /* raised curbs */
    box(c, -H - 0.35f, -H + 0.05f, zn, zf, 0.0f, 0.32f, PA_RGB(240, 241, 246), PA_RGB(196, 198, 210), PA_RGB(176, 178, 194));
    box(c, H - 0.05f, H + 0.35f, zn, zf, 0.0f, 0.32f, PA_RGB(240, 241, 246), PA_RGB(196, 198, 210), PA_RGB(176, 178, 194));
    /* defence stripe: yellow and black chevrons across the track */
    float z0 = DEFENCE_Z - 0.18f, z1 = DEFENCE_Z + 0.18f;
    quad(c, proj(-H, 0, z0), proj(H, 0, z0), proj(H, 0, z1), proj(-H, 0, z1), PA_RGB(250, 206, 40));
    for (float x = -H; x < H - 0.1f; x += 0.7f) {
        float xb = fminf(x + 0.35f, H);
        quad(c, proj(x, 0, z0), proj(xb, 0, z0), proj(fminf(xb + 0.2f, H), 0, z1), proj(fminf(x + 0.2f, H), 0, z1), PA_RGB(40, 40, 52));
    }
    /* cannon rail */
    quad(c, proj(-H + 0.6f, 0, CANNON_Z - 0.04f), proj(H - 0.6f, 0, CANNON_Z - 0.04f), proj(H - 0.6f, 0, CANNON_Z + 0.04f), proj(-H + 0.6f, 0, CANNON_Z + 0.04f), PA_RGBA(255, 255, 255, 230));
}

/* ---- the enemy castle ---- */
static void draw_castle(PA_Canvas *c) {
    float L = TRACK_LEN;
    float fl = G.base_flash * 0.22f;
    float sh = G.base_shake * 0.12f * sinf(G.clock * 60.0f);
    float drop = G.collapse;            /* sinks into rubble after the win */
    float hk = 1.32f * (1.0f - 0.85f * pa_smooth(drop));
    PA_Color red = pa_mix(PA_RGB(226, 64, 64), PA_RGB(255, 255, 255), fl);
    PA_Color redT = pa_mix(PA_RGB(250, 110, 100), PA_RGB(255, 255, 255), fl);
    PA_Color redD = pa_mix(PA_RGB(168, 36, 48), PA_RGB(255, 255, 255), fl);
    PA_Color wht = pa_mix(PA_RGB(246, 240, 236), PA_RGB(255, 255, 255), fl);
    PA_Color whtD = PA_RGB(206, 196, 200);
    PA_Color roof = PA_RGB(64, 54, 96);

    pa_shadow(c, sx(0, L + 1.5f), sy(L + 1.0f), ss(L) * 6.0f, ss(L) * 1.0f, 0.5f);
    /* back keep */
    box(c, -2.4f + sh, 2.4f + sh, L + 1.3f, L + 3.6f, 0.0f, 4.2f * hk, redT, red, redD);
    box(c, -2.6f + sh, 2.6f + sh, L + 1.2f, L + 3.7f, 4.2f * hk, 4.6f * hk, wht, whtD, whtD);
    if (drop < 0.3f) {
        /* keep roof and flag */
        PA_Vec2 r0 = proj(-2.2f + sh, 4.6f * hk, L + 1.4f), r1 = proj(2.2f + sh, 4.6f * hk, L + 1.4f), r2 = proj(0.0f + sh, 6.4f * hk, L + 2.4f);
        PA_Vec2 tri[3] = { r0, r1, r2 };
        pa_fill_poly(c, tri, 3, roof);
        PA_Vec2 pole0 = proj(0.0f + sh, 6.3f * hk, L + 2.4f), pole1 = proj(0.0f + sh, 6.3f * hk + 1.6f, L + 2.4f);
        pa_line(c, pole0.x, pole0.y, pole1.x, pole1.y, fmaxf(1.5f, ss(L) * 0.08f), PA_RGB(70, 60, 80));
        float wv = sinf(G.clock * 6.0f) * 0.1f;
        PA_Vec2 fl3[3] = { pole1, proj(1.2f + sh, 6.3f * hk + 1.2f + wv, L + 2.4f), proj(0.0f + sh, 6.3f * hk + 0.8f, L + 2.4f) };
        pa_fill_poly(c, fl3, 3, PA_RGB(240, 50, 60));
    }
    /* front wall */
    box(c, -4.4f + sh, 4.4f + sh, L, L + 1.3f, 0.0f, 2.3f * hk, redT, red, redD);
    for (int i = 0; i < 9; i++) {
        float x = -4.4f + (float)i * 1.1f + sh;
        if (drop < 0.5f) box(c, x, x + 0.6f, L, L + 0.6f, 2.3f * hk, 2.75f * hk, wht, whtD, whtD);
    }
    /* white trim band */
    quad(c, proj(-4.4f + sh, 1.7f * hk, L), proj(4.4f + sh, 1.7f * hk, L), proj(4.4f + sh, 1.95f * hk, L), proj(-4.4f + sh, 1.95f * hk, L), wht);
    /* door the reds pour out of */
    {
        PA_Vec2 d0 = proj(-1.8f + sh, 0.0f, L), d1 = proj(1.8f + sh, 1.55f * hk, L);
        pa_round_rect(c, d0.x, d1.y, d1.x - d0.x, d0.y - d1.y + 1.0f, (d1.x - d0.x) * 0.35f, PA_RGB(60, 20, 34));
        pa_round_rect(c, d0.x + 3.0f, d1.y + 3.0f, d1.x - d0.x - 6.0f, (d0.y - d1.y) * 0.4f, (d1.x - d0.x) * 0.3f, PA_RGB(90, 30, 48));
    }
    /* towers */
    for (int s = -1; s <= 1; s += 2) {
        float x0 = s < 0 ? -5.2f : 3.6f, x1 = x0 + 1.6f;
        box(c, x0 + sh, x1 + sh, L - 0.4f, L + 1.4f, 0.0f, 3.5f * hk, redT, red, redD);
        box(c, x0 - 0.15f + sh, x1 + 0.15f + sh, L - 0.55f, L + 1.55f, 3.5f * hk, 3.9f * hk, wht, whtD, whtD);
        if (drop < 0.3f) {
            PA_Vec2 a = proj(x0 - 0.1f + sh, 3.9f * hk, L - 0.5f), b = proj(x1 + 0.1f + sh, 3.9f * hk, L - 0.5f), t = proj((x0 + x1) * 0.5f + sh, 5.6f * hk, L + 0.4f);
            PA_Vec2 tri[3] = { a, b, t };
            pa_fill_poly(c, tri, 3, roof);
            PA_Vec2 hl[3] = { a, t, proj((x0 + x1) * 0.5f + sh, 3.9f * hk, L - 0.5f) };
            pa_fill_poly(c, hl, 3, PA_RGB(96, 84, 136));
        }
        /* window */
        PA_Vec2 w0 = proj(x0 + 0.55f + sh, 2.2f * hk, L - 0.4f), w1 = proj(x1 - 0.55f + sh, 2.9f * hk, L - 0.4f);
        pa_round_rect(c, w0.x, w1.y, w1.x - w0.x, w0.y - w1.y, (w1.x - w0.x) * 0.5f, PA_RGB(70, 24, 40));
    }
    if (drop > 0.0f) {
        /* rubble and smoke */
        for (int i = 0; i < 14; i++) {
            float rx = -4.8f + (float)i * 0.72f, rz = L + 0.2f + (float)(i % 3) * 0.6f;
            box(c, rx, rx + 0.55f, rz, rz + 0.5f, 0.0f, 0.4f + 0.3f * (float)(i % 2), redT, red, redD);
        }
        for (int i = 0; i < 6; i++) {
            float t = fmodf(G.st * 0.6f + (float)i * 0.17f, 1.0f);
            PA_Vec2 p = proj(-3.0f + (float)i * 1.2f, 1.0f + t * 4.0f, L + 1.0f);
            pa_fill_circle(c, p.x, p.y, ss(L) * (0.7f + t * 1.1f), PA_RGBA(200, 200, 210, (int)(150.0f * (1.0f - t))));
        }
    }
}

static void draw_castle_hp(PA_Canvas *c) {
    if (G.state == S_WIN || G.state == S_RESULT) return;
    float L = TRACK_LEN;
    /* on the keep's face, above the battlements: clear of the HUD at any height */
    PA_Vec2 p = proj(0.0f, 4.3f, L + 1.3f);
    char buf[16]; snprintf(buf, sizeof(buf), "%d", (int)ceilf(G.base_hp));
    float size = fminf(34.0f * g_u, ss(L) * 1.5f) * (1.0f + G.base_flash * 0.12f);
    float bw = 120.0f * g_u, bh = 12.0f * g_u;
    pa_round_rect(c, p.x - bw * 0.5f - 3.0f, p.y + size * 0.62f - 3.0f, bw + 6.0f, bh + 6.0f, (bh + 6.0f) * 0.5f, C_INK);
    pa_round_rect(c, p.x - bw * 0.5f, p.y + size * 0.62f, bw, bh, bh * 0.5f, PA_RGB(255, 255, 255));
    float f = pa_clamp01(G.base_hp / fmaxf(1.0f, G.base_max));
    if (f > 0.0f) pa_round_rect(c, p.x - bw * 0.5f, p.y + size * 0.62f, fmaxf(bh, bw * f), bh, bh * 0.5f, PA_RGB(236, 50, 56));
    txt(c, buf, p.x, p.y, size, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.6f);
}

/* ---- gates ---- */
static void gate_label(const Gate *g, char *buf, size_t n) {
    if (g->type == G_MUL) snprintf(buf, n, "X%d", g->val);
    else if (g->type == G_ADD) snprintf(buf, n, "+%d", g->val);
    else snprintf(buf, n, "-%d", g->val);
}

static void draw_gate(PA_Canvas *c, const Gate *g) {
    float x0 = g->x - g->w * 0.5f, x1 = g->x + g->w * 0.5f, z = g->z;
    float ht = 1.45f + g->pulse * 0.12f;
    PA_Color fill, frame, frameD;
    if (g->type == G_SUB) { fill = PA_RGBA(255, 70, 96, 120); frame = PA_RGB(255, 96, 120); frameD = PA_RGB(186, 40, 70); }
    else if (g->type == G_MUL && g->val >= 3) { fill = PA_RGBA(176, 72, 244, 130); frame = PA_RGB(196, 110, 255); frameD = PA_RGB(120, 50, 196); }
    else { fill = PA_RGBA(60, 150, 255, 120); frame = PA_RGB(96, 180, 255); frameD = PA_RGB(30, 104, 220); }
    int dim = g->cool > 0.0f;
    if (dim) fill = pa_alpha(pa_mix(fill, PA_RGB(160, 160, 180), 0.6f), 0.25f);
    if (g->pulse > 0.0f) fill = pa_alpha(pa_mix(fill, PA_RGB(255, 255, 255), g->pulse * 0.45f), (float)PA_A(fill) / 255.0f + g->pulse * 0.2f);
    /* ground glow strip */
    quad(c, proj(x0, 0, z - 0.2f), proj(x1, 0, z - 0.2f), proj(x1, 0, z + 0.2f), proj(x0, 0, z + 0.2f), pa_alpha(frame, 0.45f));
    /* translucent panel */
    quad(c, proj(x0, 0.0f, z), proj(x1, 0.0f, z), proj(x1, ht, z), proj(x0, ht, z), fill);
    /* inner sheen */
    quad(c, proj(x0 + 0.15f, ht * 0.62f, z), proj(x1 - 0.15f, ht * 0.62f, z), proj(x1 - 0.15f, ht * 0.9f, z), proj(x0 + 0.15f, ht * 0.9f, z), PA_RGBA(255, 255, 255, dim ? 14 : 40));
    /* frame: posts and top rail */
    float pw = 0.16f;
    box(c, x0 - pw, x0 + pw * 0.3f, z - 0.12f, z + 0.12f, 0.0f, ht + 0.1f, frame, frameD, frameD);
    box(c, x1 - pw * 0.3f, x1 + pw, z - 0.12f, z + 0.12f, 0.0f, ht + 0.1f, frame, frameD, frameD);
    box(c, x0 - pw, x1 + pw, z - 0.12f, z + 0.12f, ht - 0.08f, ht + 0.14f, pa_shade(frame, 0.3f), frame, frameD);
    /* label */
    char buf[12]; gate_label(g, buf, sizeof(buf));
    float k = ss(z);
    float size = k * 0.82f * (1.0f + g->pulse * 0.15f);
    PA_Vec2 m = proj(g->x, ht * 0.48f, z);
    PA_Color tc = dim ? PA_RGB(220, 220, 236) : PA_RGB(255, 255, 255);
    if (buf[0] == '-') {
        /* a lone dash in the outlined face reads as '=', so it gets a fatter stroke */
        float tr = size * 0.05f, adv = size * 0.72f + tr;
        float w = pa_text_width(buf, size, tr), x0 = m.x - w * 0.5f;
        txt(c, "-", x0 + adv * 0.5f - tr, m.y, size * 1.35f, tc, PA_ALIGN_CENTER, 2.2f);
        txt(c, buf + 1, x0 + adv, m.y, size, tc, PA_ALIGN_LEFT, 1.5f);
    } else txt(c, buf, m.x, m.y, size, tc, PA_ALIGN_CENTER, 1.5f);
}

/* ---- brutes: enemy giants (yellow, red bands, facing us) and our champion ---- */
static void draw_brute(PA_Canvas *c, const Brute *b, int enemy) {
    float k = ss(b->z) * b->size;
    PA_Vec2 f = proj(b->x, 0.0f, b->z);
    float u = k * VK;
    float sw = sinf(b->ph);
    PA_Color body = enemy ? PA_RGB(255, 210, 40) : PA_RGB(70, 170, 255);
    PA_Color bodyL = enemy ? PA_RGB(255, 236, 130) : PA_RGB(160, 220, 255);
    PA_Color bodyD = enemy ? PA_RGB(220, 150, 20) : PA_RGB(30, 100, 220);
    PA_Color band = enemy ? PA_RGB(230, 40, 40) : PA_RGB(255, 210, 50);
    if (b->flash > 0.0f) {
        body = pa_mix(body, PA_RGB(255, 255, 255), b->flash * 0.6f);
        bodyL = pa_mix(bodyL, PA_RGB(255, 255, 255), b->flash * 0.6f);
        bodyD = pa_mix(bodyD, PA_RGB(255, 255, 255), b->flash * 0.5f);
    }
    float bob = fabsf(sw) * u * 0.06f;
    pa_shadow(c, f.x, f.y, k * 1.0f, k * 0.3f, 0.55f);
    /* legs */
    pa_round_rect(c, f.x - k * 0.42f, f.y - u * 0.55f + sw * u * 0.05f, k * 0.32f, u * 0.55f, k * 0.14f, bodyD);
    pa_round_rect(c, f.x + k * 0.10f, f.y - u * 0.55f - sw * u * 0.05f, k * 0.32f, u * 0.55f, k * 0.14f, bodyD);
    float ty = f.y - u * 0.45f - bob;
    /* arms behind the torso */
    for (int s = -1; s <= 1; s += 2) {
        float ax = f.x + (float)s * k * 0.78f, ay = ty - u * 1.15f + (float)s * sw * u * 0.08f;
        PA_Paint pa = pa_linear(ax - k * 0.25f, ay, ax + k * 0.25f, ay);
        pa_stop(&pa, 0.0f, s < 0 ? bodyL : body); pa_stop(&pa, 1.0f, s < 0 ? body : bodyD);
        pa_round_rect_paint(c, ax - k * 0.27f, ay, k * 0.54f, u * 1.05f, k * 0.26f, &pa);
        pa_round_rect(c, ax - k * 0.29f, ay + u * 0.62f, k * 0.58f, u * 0.17f, k * 0.06f, band);
        pa_fill_circle(c, ax, ay + u * 1.0f, k * 0.27f, body);
    }
    /* torso */
    PA_Paint pt = pa_linear(f.x - k * 0.6f, ty - u * 1.4f, f.x + k * 0.6f, ty);
    pa_stop(&pt, 0.0f, bodyL); pa_stop(&pt, 0.5f, body); pa_stop(&pt, 1.0f, bodyD);
    pa_round_rect_paint(c, f.x - k * 0.62f, ty - u * 1.42f, k * 1.24f, u * 1.32f, k * 0.42f, &pt);
    /* shoulders */
    pa_fill_circle(c, f.x - k * 0.55f, ty - u * 1.18f, k * 0.3f, body);
    pa_fill_circle(c, f.x + k * 0.55f, ty - u * 1.18f, k * 0.3f, pa_mix(body, bodyD, 0.5f));
    if (enemy) {
        /* red star on the chest */
        PA_Vec2 st[10];
        float cx = f.x, cy = ty - u * 0.72f, R = k * 0.32f;
        for (int i = 0; i < 10; i++) {
            float a = -PA_PI * 0.5f + (float)i * PA_PI / 5.0f, r = (i & 1) ? R * 0.45f : R;
            st[i].x = cx + cosf(a) * r; st[i].y = cy + sinf(a) * r * 0.9f;
        }
        pa_fill_poly(c, st, 10, band);
        /* head with a red crest and angry eyes */
        float hx = f.x, hy = ty - u * 1.48f;
        pa_fill_circle(c, hx, hy, k * 0.26f, body);
        PA_Vec2 cr[3] = { { hx - k * 0.12f, hy - k * 0.18f }, { hx + k * 0.05f, hy - k * 0.48f }, { hx + k * 0.16f, hy - k * 0.14f } };
        pa_fill_poly(c, cr, 3, band);
        pa_fill_circle(c, hx - k * 0.09f, hy, k * 0.05f, PA_RGB(40, 20, 20));
        pa_fill_circle(c, hx + k * 0.09f, hy, k * 0.05f, PA_RGB(40, 20, 20));
        pa_line(c, hx - k * 0.16f, hy - k * 0.10f, hx - k * 0.03f, hy - k * 0.05f, fmaxf(1.0f, k * 0.04f), PA_RGB(40, 20, 20));
        pa_line(c, hx + k * 0.16f, hy - k * 0.10f, hx + k * 0.03f, hy - k * 0.05f, fmaxf(1.0f, k * 0.04f), PA_RGB(40, 20, 20));
    } else {
        /* back view: spine shading, a gold lightning crest */
        float hx = f.x, hy = ty - u * 1.46f;
        pa_fill_circle(c, hx, hy, k * 0.25f, pa_mix(body, bodyD, 0.25f));
        PA_Vec2 bolt[6] = { { f.x + k * 0.06f, ty - u * 1.05f }, { f.x - k * 0.14f, ty - u * 0.68f }, { f.x - k * 0.01f, ty - u * 0.68f },
                            { f.x - k * 0.08f, ty - u * 0.32f }, { f.x + k * 0.15f, ty - u * 0.76f }, { f.x + k * 0.02f, ty - u * 0.76f } };
        pa_fill_poly(c, bolt, 6, band);
        pa_fill_ellipse(c, hx - k * 0.08f, hy - k * 0.1f, k * 0.09f, k * 0.05f, PA_RGBA(255, 255, 255, 140));
    }
    /* HP number */
    char buf[12]; snprintf(buf, sizeof(buf), "%d", (int)ceilf(b->hp));
    float ts = fmaxf(16.0f * g_u, k * 0.42f);
    float hy = ty - u * 1.95f;
    float bw = fmaxf(46.0f * g_u, k * 1.3f), bh = fmaxf(6.0f, k * 0.1f);
    pa_round_rect(c, f.x - bw * 0.5f - 2.0f, hy + ts * 0.62f - 2.0f, bw + 4.0f, bh + 4.0f, bh, C_INK);
    pa_round_rect(c, f.x - bw * 0.5f, hy + ts * 0.62f, bw, bh, bh * 0.5f, PA_RGB(255, 255, 255));
    float fr = pa_clamp01(b->hp / b->maxhp);
    pa_round_rect(c, f.x - bw * 0.5f, hy + ts * 0.62f, fmaxf(bh, bw * fr), bh, bh * 0.5f, enemy ? PA_RGB(236, 50, 56) : PA_RGB(60, 210, 90));
    txt(c, buf, f.x, hy, ts, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.3f);
}

/* ---- the cannon ---- */
static void draw_cannon(PA_Canvas *c) {
    float z = CANNON_Z, x = G.cannon_x;
    float k = ss(z) * 1.3f;
    PA_Vec2 f = proj(x, 0, z);
    float rec = G.recoil;
    /* green aim ring */
    pa_stroke_circle(c, f.x, f.y, 1.0f, 1.0f, PA_RGBA(0, 0, 0, 0));
    {
        PA_Vec2 pts[40];
        for (int i = 0; i < 40; i++) {
            float a = PA_TAU * (float)i / 40.0f;
            pts[i].x = f.x + cosf(a) * k * 1.15f; pts[i].y = f.y + sinf(a) * k * 0.42f;
        }
        pa_stroke_poly(c, pts, 40, 1, fmaxf(3.0f, k * 0.12f), PA_RGBA(80, 236, 90, 220));
    }
    pa_shadow(c, f.x, f.y, k * 1.0f, k * 0.34f, 0.6f);
    /* wheels */
    for (int s = -1; s <= 1; s += 2) {
        for (int r = 0; r < 2; r++) {
            float wx = f.x + (float)s * k * 0.62f, wy = f.y - k * 0.12f - (float)r * k * 0.36f;
            pa_round_rect(c, wx - k * 0.14f, wy - k * 0.22f, k * 0.28f, k * 0.36f, k * 0.1f, PA_RGB(36, 36, 46));
            pa_round_rect(c, wx - k * 0.07f, wy - k * 0.12f, k * 0.14f, k * 0.16f, k * 0.05f, PA_RGB(150, 152, 168));
        }
    }
    /* chassis */
    pa_round_rect(c, f.x - k * 0.55f, f.y - k * 0.62f, k * 1.1f, k * 0.42f, k * 0.12f, PA_RGB(60, 64, 84));
    pa_round_rect(c, f.x - k * 0.55f, f.y - k * 0.66f, k * 1.1f, k * 0.14f, k * 0.07f, PA_RGB(96, 100, 124));
    /* barrel: a fat capsule tipped toward the track */
    float by = f.y - k * 0.55f + rec * k * 0.12f;
    float bl = k * (1.25f - rec * 0.12f), bw = k * (0.78f + rec * 0.06f);
    PA_Color b0 = PA_RGB(120, 200, 255), b1 = PA_RGB(44, 140, 255), b2 = PA_RGB(20, 82, 210);
    if (G.cannon_flash > 0.0f) { b0 = pa_mix(b0, PA_RGB(255, 80, 80), G.cannon_flash); b1 = pa_mix(b1, PA_RGB(255, 60, 60), G.cannon_flash); }
    PA_Paint pb = pa_linear(f.x - bw * 0.5f, 0, f.x + bw * 0.5f, 0);
    pa_stop(&pb, 0.0f, b0); pa_stop(&pb, 0.45f, b1); pa_stop(&pb, 1.0f, b2);
    pa_round_rect_paint(c, f.x - bw * 0.5f, by - bl, bw, bl, bw * 0.48f, &pb);
    pa_round_rect(c, f.x - bw * 0.52f, by - bl * 0.45f, bw * 1.04f, k * 0.12f, k * 0.05f, PA_RGB(20, 70, 180));
    /* muzzle, seen end-on */
    float mx = f.x, my = by - bl + bw * 0.28f;
    pa_fill_ellipse(c, mx, my, bw * 0.46f, bw * 0.26f, b2);
    pa_fill_ellipse(c, mx, my + 1.0f, bw * 0.32f, bw * 0.17f, PA_RGB(14, 30, 80));
    pa_fill_ellipse(c, f.x - bw * 0.22f, by - bl * 0.62f, bw * 0.08f, bl * 0.18f, PA_RGBA(255, 255, 255, 120));
    if (rec > 0.5f && G.firing) {
        float r = bw * 0.55f * rec;
        pa_fill_circle(c, mx, my - r * 0.4f, r, PA_RGBA(255, 255, 220, 200));
        pa_fill_circle(c, mx, my - r * 0.4f, r * 0.55f, PA_RGBA(255, 255, 255, 255));
    }
    /* charge meter: a vertical capsule beside the cannon */
    float mh = k * 1.9f, mw = k * 0.42f;
    float gx = f.x - k * 1.55f, gy = f.y - mh - k * 0.1f;
    if (gx < 10.0f) gx = f.x + k * 1.55f - mw;
    pa_round_rect(c, gx - 3.0f, gy - 3.0f, mw + 6.0f, mh + 6.0f, (mw + 6.0f) * 0.5f, C_INK);
    pa_round_rect(c, gx, gy, mw, mh, mw * 0.5f, PA_RGB(236, 240, 252));
    float fr = pa_clamp01(G.charge / G.charge_max);
    if (fr > 0.02f) {
        float fh = fmaxf(mw, mh * fr);
        PA_Paint pm = pa_linear(0, gy + mh - fh, 0, gy + mh);
        pa_stop(&pm, 0.0f, PA_RGB(120, 230, 255)); pa_stop(&pm, 1.0f, PA_RGB(40, 130, 255));
        pa_round_rect_paint(c, gx, gy + mh - fh, mw, fh, mw * 0.5f, &pm);
    }
    if (G.charge_flash > 0.0f) pa_round_rect(c, gx, gy, mw, mh, mw * 0.5f, PA_RGBA(255, 240, 120, (int)(220.0f * G.charge_flash)));
    /* bolt */
    float bx = gx + mw * 0.5f, bty = gy + mh - mw * 0.9f;
    PA_Vec2 bolt[6] = { { bx + mw * 0.10f, bty - mw * 0.45f }, { bx - mw * 0.22f, bty + mw * 0.05f }, { bx - mw * 0.02f, bty + mw * 0.05f },
                        { bx - mw * 0.12f, bty + mw * 0.45f }, { bx + mw * 0.22f, bty - mw * 0.06f }, { bx + mw * 0.02f, bty - mw * 0.06f } };
    pa_fill_poly(c, bolt, 6, PA_RGB(255, 255, 255));
    /* defence health under the cannon */
    if (G.cannon_hp < G.cannon_max || G.state == S_PLAY) {
        float hw = k * 1.6f, hh = fmaxf(7.0f, k * 0.16f);
        float hx = f.x - hw * 0.5f, hy = f.y + k * 0.52f;
        pa_round_rect(c, hx - 2.0f, hy - 2.0f, hw + 4.0f, hh + 4.0f, hh, C_INK);
        pa_round_rect(c, hx, hy, hw, hh, hh * 0.5f, PA_RGB(90, 40, 60));
        float hf = pa_clamp01((float)G.cannon_hp / (float)G.cannon_max);
        if (hf > 0.0f) pa_round_rect(c, hx, hy, fmaxf(hh, hw * hf), hh, hh * 0.5f, hf > 0.4f ? PA_RGB(80, 220, 100) : PA_RGB(255, 80, 70));
    }
}

/* ---- particles & floating text ---- */
static void draw_parts(PA_Canvas *c) {
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *p = &g_part[i];
        if (p->life <= 0.0f) continue;
        float t = p->life / p->max;
        PA_Vec2 s = proj(p->x, p->y, p->z);
        float k = ss(p->z);
        switch (p->kind) {
        case PK_PUFF: {
            float r = p->size * k * (1.6f - t * 0.8f);
            pa_fill_circle(c, s.x, s.y, r, pa_alpha(p->col, 0.85f * t));
            pa_fill_circle(c, s.x - r * 0.2f, s.y - r * 0.2f, r * 0.5f, pa_alpha(PA_RGB(255, 255, 255), 0.6f * t));
            break; }
        case PK_BIT: {
            float r = p->size * k;
            pa_fill_rect(c, s.x - r * 0.5f, s.y - r * 0.5f, r, r * 0.8f, pa_alpha(p->col, fminf(1.0f, t * 2.0f)));
            break; }
        case PK_RING: {
            float e = 1.0f - t;
            PA_Vec2 pts[32];
            for (int j = 0; j < 32; j++) {
                float a = PA_TAU * (float)j / 32.0f;
                pts[j].x = s.x + cosf(a) * k * p->size * (0.3f + e); pts[j].y = s.y + sinf(a) * k * p->size * (0.3f + e) * 0.38f;
            }
            pa_stroke_poly(c, pts, 32, 1, fmaxf(2.0f, k * 0.14f * t), pa_alpha(p->col, t));
            break; }
        default: {
            float r = fmaxf(1.5f, p->size * k);
            pa_fill_circle(c, s.x, s.y, r, pa_alpha(p->col, t));
            break; }
        }
    }
}

static void draw_floats(PA_Canvas *c) {
    for (int i = 0; i < MAX_FLOATS; i++) {
        Float *f = &g_float[i];
        if (f->t >= 1.1f) continue;
        /* outlined strokes overlap, so a translucent one turns muddy: these
           pop in and shrink away instead of fading */
        float pop = f->t < 0.12f ? 0.6f + f->t / 0.12f * 0.5f : (f->t < 0.25f ? 1.1f - (f->t - 0.12f) : 1.0f);
        if (f->t > 0.8f) pop *= 1.0f - (f->t - 0.8f) / 0.3f;
        if (pop < 0.2f) continue;
        PA_Vec2 s = proj(f->x, f->y + f->t * 1.2f, f->z);
        float size = fmaxf(18.0f * g_u, ss(f->z) * 0.75f) * f->size * pop;
        pa_text_bold(c, f->text, s.x, s.y - size * 0.5f, size, f->col, C_INK, PA_ALIGN_CENTER, size * 0.05f, 1.5f);
    }
}

/* ---- the whole battlefield, painter's order far to near ---- */
static short g_zb_head[2][ZBINS], g_zb_next[2][MAX_BLUE > MAX_RED ? MAX_BLUE : MAX_RED];

static int zbin(float z) { return row_of(z); }

static void draw_bin_units(PA_Canvas *c, int b) {
    for (int j = g_zb_head[1][b]; j >= 0; j = g_zb_next[1][j]) {
        const Mob *m = &g_red[j];
        PA_Vec2 p = proj(m->x, 0, m->z);
        int fr = ((int)m->ph) & 1;
        blit(c, spr_for(T_RED, m->z, fr), (int)p.x, (int)p.y - fr);
    }
    for (int j = g_zb_head[0][b]; j >= 0; j = g_zb_next[0][j]) {
        const Mob *m = &g_blue[j];
        PA_Vec2 p = proj(m->x, 0, m->z);
        int fr = ((int)m->ph) & 1;
        blit(c, spr_for(m->fresh > 0.0f ? T_GLOW : T_BLUE, m->z, fr), (int)p.x, (int)p.y - fr);
    }
}

static void draw_world(PA_Canvas *c) {
    draw_ground(c);
    for (int i = 0; i < g_ndecor; i++) {
        if (g_decor[i].z < TRACK_LEN + 4.0f) continue;
        if (g_decor[i].kind == 0) draw_pine(c, g_decor[i].x, g_decor[i].z, g_decor[i].s);
        else draw_rock(c, g_decor[i].x, g_decor[i].z, g_decor[i].s);
    }
    draw_track(c);
    for (int i = 0; i < g_ndecor; i++) {
        if (g_decor[i].z >= TRACK_LEN + 4.0f) continue;
        if (g_decor[i].kind == 0) draw_pine(c, g_decor[i].x, g_decor[i].z, g_decor[i].s);
        else draw_rock(c, g_decor[i].x, g_decor[i].z, g_decor[i].s);
    }
    draw_castle(c);

    /* bucket the crowds by depth */
    for (int b = 0; b < ZBINS; b++) { g_zb_head[0][b] = -1; g_zb_head[1][b] = -1; }
    for (int i = 0; i < g_nblue; i++) { int b = zbin(g_blue[i].z); g_zb_next[0][i] = g_zb_head[0][b]; g_zb_head[0][b] = (short)i; }
    for (int i = 0; i < g_nred; i++) { int b = zbin(g_red[i].z); g_zb_next[1][i] = g_zb_head[1][b]; g_zb_head[1][b] = (short)i; }

    /* the large things, in depth order */
    int done_g[MAX_GATES] = { 0 }, done_b[MAX_BRUTES] = { 0 }, done_c[MAX_CHAMPS] = { 0 };
    int cannon_done = 0;
    for (int b = ZBINS - 1; b >= 0; b--) {
        float zlo = -4.0f + (float)b * CELL;
        for (;;) {
            /* the farthest undrawn big object at or beyond this bin */
            float bz = -1e9f; int kind = -1, idx = -1;
            for (int i = 0; i < g_ngate; i++) if (!done_g[i] && g_gate[i].z >= zlo && g_gate[i].z > bz) { bz = g_gate[i].z; kind = 0; idx = i; }
            for (int i = 0; i < g_nbrute; i++) if (!done_b[i] && g_brute[i].z >= zlo && g_brute[i].z > bz) { bz = g_brute[i].z; kind = 1; idx = i; }
            for (int i = 0; i < g_nchamp; i++) if (!done_c[i] && g_champ[i].z >= zlo && g_champ[i].z > bz) { bz = g_champ[i].z; kind = 2; idx = i; }
            if (!cannon_done && CANNON_Z >= zlo && CANNON_Z > bz) { bz = CANNON_Z; kind = 3; }
            if (kind < 0) break;
            if (kind == 0) { draw_gate(c, &g_gate[idx]); done_g[idx] = 1; }
            else if (kind == 1) { draw_brute(c, &g_brute[idx], 1); done_b[idx] = 1; }
            else if (kind == 2) { draw_brute(c, &g_champ[idx], 0); done_c[idx] = 1; }
            else { draw_cannon(c); cannon_done = 1; }
        }
        draw_bin_units(c, b);
    }
    if (!cannon_done) draw_cannon(c);
    draw_parts(c);
    draw_castle_hp(c);
    draw_floats(c);
}

/* ---- HUD ---- */
static void draw_hud(PA_Canvas *c) {
    float u = g_u;
    float ph = 54.0f * u, top = 26.0f * u;
    float pw = 160.0f * u;
    draw_pill(c, 12.0f * u, top, pw, ph, 1, G.gems);
    draw_pill(c, g_w - pw - 12.0f * u, top, pw, ph, 0, G.coins);
    /* level tag between the pills */
    char buf[32];
    snprintf(buf, sizeof(buf), "LEVEL %d", (G.state == S_RESULT || G.state == S_WIN) && G.result_win ? G.result_level : G.level);
    float lw = fminf(g_w - 2.0f * pw - 50.0f * u, 170.0f * u);
    if (lw > 90.0f * u) {
        float ly = top + ph * 0.5f;
        float ts = 22.0f * u;
        int boss = ((G.state == S_RESULT || G.state == S_WIN) && G.result_win ? G.result_level : G.level) % 5 == 0;
        txt(c, buf, g_w * 0.5f, ly - (boss ? 10.0f * u : 0.0f), ts, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.4f);
        if (boss) {
            float bw = 74.0f * u, bh = 22.0f * u;
            pa_round_rect(c, g_w * 0.5f - bw * 0.5f, ly + 6.0f * u, bw, bh, bh * 0.5f, PA_RGB(236, 50, 56));
            txt(c, "BOSS", g_w * 0.5f, ly + 6.0f * u + bh * 0.52f, 15.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.0f);
        }
    }
    pa_hub_pause_anchor(12.0f * u + 26.0f * u, top + ph + 38.0f * u, 24.0f * u);
}

static void draw_hand(PA_Canvas *c, float x, float y, float s) {
    /* a pointing hand, drawn from rounded pieces */
    pa_fill_circle(c, x + s * 0.06f, y + s * 0.10f, s * 0.42f, PA_RGBA(30, 20, 60, 60));
    pa_round_rect(c, x - s * 0.12f, y - s * 0.62f, s * 0.24f, s * 0.7f, s * 0.12f, C_INK);
    pa_round_rect(c, x - s * 0.36f, y - s * 0.12f, s * 0.78f, s * 0.62f, s * 0.24f, C_INK);
    pa_round_rect(c, x - s * 0.09f, y - s * 0.59f, s * 0.18f, s * 0.66f, s * 0.09f, PA_RGB(255, 255, 255));
    pa_round_rect(c, x - s * 0.33f, y - s * 0.09f, s * 0.72f, s * 0.56f, s * 0.22f, PA_RGB(255, 255, 255));
    for (int i = 0; i < 3; i++)
        pa_line(c, x + s * (0.02f + 0.12f * (float)i), y - s * 0.06f, x + s * (0.02f + 0.12f * (float)i), y + s * 0.10f, fmaxf(1.0f, s * 0.03f), PA_RGB(200, 200, 220));
}

static void button3d(PA_Canvas *c, Rect r, PA_Color col, const char *label, float size, float press) {
    float d = 7.0f * g_u * (1.0f - press);
    float y = r.y + 7.0f * g_u - d;
    pa_round_rect(c, r.x - 3.0f, y - 3.0f, r.w + 6.0f, r.h + d + 6.0f, r.h * 0.34f, C_INK);
    pa_round_rect(c, r.x, y + d, r.w, r.h, r.h * 0.3f, pa_shade(col, -0.35f));
    PA_Paint p = pa_linear(0, y, 0, y + r.h);
    pa_stop(&p, 0.0f, pa_shade(col, 0.25f)); pa_stop(&p, 1.0f, col);
    pa_round_rect_paint(c, r.x, y, r.w, r.h, r.h * 0.3f, &p);
    pa_round_rect(c, r.x + r.h * 0.25f, y + r.h * 0.1f, r.w - r.h * 0.5f, r.h * 0.2f, r.h * 0.1f, PA_RGBA(255, 255, 255, 70));
    if (label) txt(c, label, r.x + r.w * 0.5f, y + r.h * 0.52f, size, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.5f);
}

static void draw_ready(PA_Canvas *c) {
    float u = g_u;
    float a = pa_clamp01(G.st * 4.0f);
    /* level banner */
    char buf[32]; snprintf(buf, sizeof(buf), "LEVEL %d", G.level);
    float by = g_h * 0.40f;
    float bw = fminf(g_w * 0.86f, 420.0f * u), bh = 86.0f * u;
    float pop = 0.8f + 0.2f * pa_smooth(a);
    Rect rb = { g_w * 0.5f - bw * 0.5f * pop, by - bh * 0.5f * pop, bw * pop, bh * pop };
    PA_Vec2 rib[6] = { { rb.x - 26.0f * u, rb.y + rb.h * 0.15f }, { rb.x + rb.w + 26.0f * u, rb.y + rb.h * 0.15f },
                       { rb.x + rb.w + 8.0f * u, rb.y + rb.h * 0.6f }, { rb.x + rb.w + 26.0f * u, rb.y + rb.h * 1.05f },
                       { rb.x - 26.0f * u, rb.y + rb.h * 1.05f }, { rb.x - 8.0f * u, rb.y + rb.h * 0.6f } };
    pa_fill_poly(c, rib, 6, PA_RGB(84, 58, 190));
    pa_round_rect(c, rb.x - 3.0f, rb.y - 3.0f, rb.w + 6.0f, rb.h + 6.0f, 18.0f * u, C_INK);
    PA_Paint p = pa_linear(0, rb.y, 0, rb.y + rb.h);
    pa_stop(&p, 0.0f, PA_RGB(140, 100, 255)); pa_stop(&p, 1.0f, PA_RGB(96, 64, 220));
    pa_round_rect_paint(c, rb.x, rb.y, rb.w, rb.h, 16.0f * u, &p);
    txt(c, buf, g_w * 0.5f, rb.y + rb.h * 0.52f, 44.0f * u * pop, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 2.0f);
    if (G.boss_level) txt(c, "BOSS FIGHT", g_w * 0.5f, rb.y + rb.h + 34.0f * u, 26.0f * u, PA_RGB(255, 90, 80), PA_ALIGN_CENTER, 1.6f);

    /* the drag hint over the cannon */
    float k = ss(CANNON_Z);
    PA_Vec2 f = proj(G.cannon_x, 0, CANNON_Z);
    float hx = f.x + sinf(G.clock * 3.2f) * k * 1.8f;
    float hy = f.y + k * 0.9f;
    pa_line(c, f.x - k * 2.2f, hy - k * 0.25f, f.x + k * 2.2f, hy - k * 0.25f, 4.0f * u, PA_RGBA(255, 255, 255, 160));
    txt(c, "<", f.x - k * 2.5f, hy - k * 0.25f, 30.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.4f);
    txt(c, ">", f.x + k * 2.5f, hy - k * 0.25f, 30.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.4f);
    draw_hand(c, hx, hy + 18.0f * u, 64.0f * u);
    txt(c, "HOLD TO FIRE", g_w * 0.5f - 20.0f * u, g_h - 44.0f * u, 22.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.6f);

    /* upgrades shortcut */
    Rect r = r_ready_upgrades();
    button3d(c, r, PA_RGB(255, 176, 30), NULL, 0, 0);
    txt(c, "UPGRADE", r.x + r.w * 0.5f, r.y + r.h * 0.52f, 21.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.4f);
    if (can_afford(0) || can_afford(1) || can_afford(2)) {
        float bx = r.x + r.w - 4.0f * u, byy = r.y + 4.0f * u, rr = 13.0f * u * (1.0f + 0.1f * sinf(G.clock * 8.0f));
        pa_fill_circle(c, bx, byy, rr + 2.5f, C_INK);
        pa_fill_circle(c, bx, byy, rr, PA_RGB(240, 50, 60));
        txt(c, "!", bx, byy, 16.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.0f);
    }
}

static void draw_result(PA_Canvas *c) {
    float u = g_u;
    float a = pa_smooth(pa_clamp01(G.st * 3.0f));
    pa_fill_rect(c, 0, 0, g_w, g_h, PA_RGBA(26, 16, 70, (int)(150.0f * a)));
    pa_hub_hide_pause();
    Rect cd = r_card();
    float off = (1.0f - a) * 120.0f * u;
    cd.y += off;
    int win = G.result_win;
    /* card */
    pa_round_rect(c, cd.x - 4.0f, cd.y - 4.0f + 10.0f * u, cd.w + 8.0f, cd.h + 8.0f, 34.0f * u, PA_RGBA(10, 6, 40, 110));
    pa_round_rect(c, cd.x - 4.0f, cd.y - 4.0f, cd.w + 8.0f, cd.h + 8.0f, 34.0f * u, C_INK);
    PA_Paint p = pa_linear(0, cd.y, 0, cd.y + cd.h);
    pa_stop(&p, 0.0f, PA_RGB(250, 248, 255)); pa_stop(&p, 1.0f, PA_RGB(222, 218, 246));
    pa_round_rect_paint(c, cd.x, cd.y, cd.w, cd.h, 30.0f * u, &p);
    /* ribbon */
    float rw = cd.w * 1.08f, rh = 84.0f * u;
    float rx = g_w * 0.5f - rw * 0.5f, ry = cd.y - rh * 0.55f;
    PA_Color rc = win ? PA_RGB(255, 186, 30) : PA_RGB(236, 64, 72);
    PA_Vec2 tails[2][4] = {
        { { rx - 22.0f * u, ry + rh * 0.3f }, { rx + 30.0f * u, ry + rh * 0.3f }, { rx + 30.0f * u, ry + rh * 1.2f }, { rx - 6.0f * u, ry + rh * 1.2f } },
        { { rx + rw - 30.0f * u, ry + rh * 0.3f }, { rx + rw + 22.0f * u, ry + rh * 0.3f }, { rx + rw + 6.0f * u, ry + rh * 1.2f }, { rx + rw - 30.0f * u, ry + rh * 1.2f } } };
    pa_fill_poly(c, tails[0], 4, pa_shade(rc, -0.35f));
    pa_fill_poly(c, tails[1], 4, pa_shade(rc, -0.35f));
    pa_round_rect(c, rx - 3.0f, ry - 3.0f, rw + 6.0f, rh + 6.0f, 20.0f * u, C_INK);
    PA_Paint pr = pa_linear(0, ry, 0, ry + rh);
    pa_stop(&pr, 0.0f, pa_shade(rc, 0.25f)); pa_stop(&pr, 1.0f, rc);
    pa_round_rect_paint(c, rx, ry, rw, rh, 18.0f * u, &pr);
    txt(c, win ? "VICTORY!" : "DEFEATED", g_w * 0.5f, ry + rh * 0.52f, 46.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 2.2f);

    char buf[48];
    if (win) snprintf(buf, sizeof(buf), "LEVEL %d CLEARED", G.result_level);
    else snprintf(buf, sizeof(buf), "THE CASTLE HELD");
    pa_text(c, buf, g_w * 0.5f, cd.y + rh * 0.62f, 20.0f * u, PA_RGB(110, 96, 150), PA_ALIGN_CENTER, 2.0f * u);

    /* reward */
    float ry2 = cd.y + cd.h * 0.38f;
    float coin_r = 30.0f * u;
    char cb[24]; snprintf(cb, sizeof(cb), "+%d", (int)G.shown_coins);
    float tw = pa_text_width(cb, 50.0f * u, 2.5f * u);
    float total = coin_r * 2.0f + 14.0f * u + tw;
    float sx0 = g_w * 0.5f - total * 0.5f;
    /* glow behind the coin */
    pa_fill_circle(c, sx0 + coin_r, ry2, coin_r * (1.6f + 0.1f * sinf(G.clock * 4.0f)), PA_RGBA(255, 220, 100, 70));
    draw_coin_icon(c, sx0 + coin_r, ry2, coin_r);
    txt(c, cb, sx0 + coin_r * 2.0f + 14.0f * u, ry2, 50.0f * u, PA_RGB(255, 206, 40), PA_ALIGN_LEFT, 2.0f);
    if (G.reward_gems > 0) {
        char gb[16]; snprintf(gb, sizeof(gb), "+%d", G.reward_gems);
        float gy = ry2 + 60.0f * u;
        draw_gem_icon(c, g_w * 0.5f - 30.0f * u, gy, 16.0f * u);
        txt(c, gb, g_w * 0.5f - 6.0f * u, gy, 30.0f * u, PA_RGB(70, 170, 255), PA_ALIGN_LEFT, 1.6f);
    } else if (!win) {
        pa_text(c, "UPGRADE YOUR CANNON AND TRY AGAIN", g_w * 0.5f, ry2 + 50.0f * u, 14.0f * u, PA_RGB(120, 106, 160), PA_ALIGN_CENTER, 1.2f * u);
    }

    Rect b0 = r_result_btn(0), b1 = r_result_btn(1);
    b0.y += off; b1.y += off;
    button3d(c, b0, PA_RGB(76, 206, 80), win ? "NEXT LEVEL" : "RETRY", 30.0f * u, 0.0f);
    button3d(c, b1, PA_RGB(255, 170, 30), "UPGRADES", 22.0f * u, 0.0f);
    if (can_afford(0) || can_afford(1) || can_afford(2)) {
        float bx = b1.x + b1.w - 4.0f * u, byy = b1.y + 6.0f * u;
        pa_fill_circle(c, bx, byy, 15.5f * u, C_INK);
        pa_fill_circle(c, bx, byy, 13.0f * u, PA_RGB(240, 50, 60));
        txt(c, "!", bx, byy, 16.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.0f);
    }
}

/* ---- upgrade screen ---- */
static void icon_cannon(PA_Canvas *c, float x, float y, float s) {
    pa_round_rect(c, x - s * 0.42f, y + s * 0.12f, s * 0.84f, s * 0.26f, s * 0.08f, PA_RGB(60, 64, 84));
    pa_fill_circle(c, x - s * 0.28f, y + s * 0.40f, s * 0.13f, PA_RGB(36, 36, 46));
    pa_fill_circle(c, x + s * 0.28f, y + s * 0.40f, s * 0.13f, PA_RGB(36, 36, 46));
    PA_Paint p = pa_linear(x - s * 0.3f, 0, x + s * 0.3f, 0);
    pa_stop(&p, 0.0f, PA_RGB(130, 206, 255)); pa_stop(&p, 1.0f, PA_RGB(24, 96, 220));
    pa_round_rect_paint(c, x - s * 0.28f, y - s * 0.48f, s * 0.56f, s * 0.72f, s * 0.27f, &p);
    pa_fill_ellipse(c, x, y - s * 0.36f, s * 0.2f, s * 0.11f, PA_RGB(14, 30, 80));
}
static void icon_champ(PA_Canvas *c, float x, float y, float s) {
    Brute b; memset(&b, 0, sizeof(b));
    (void)b;
    PA_Color body = PA_RGB(70, 170, 255), d = PA_RGB(30, 100, 220);
    pa_round_rect(c, x - s * 0.5f, y - s * 0.28f, s * 0.22f, s * 0.5f, s * 0.11f, body);
    pa_round_rect(c, x + s * 0.28f, y - s * 0.28f, s * 0.22f, s * 0.5f, s * 0.11f, d);
    pa_round_rect(c, x - s * 0.34f, y - s * 0.34f, s * 0.68f, s * 0.66f, s * 0.22f, body);
    pa_fill_circle(c, x, y - s * 0.42f, s * 0.15f, body);
    PA_Vec2 bolt[6] = { { x + s * 0.04f, y - s * 0.2f }, { x - s * 0.1f, y + s * 0.02f }, { x, y + s * 0.02f },
                        { x - s * 0.05f, y + s * 0.24f }, { x + s * 0.1f, y - s * 0.03f }, { x + s * 0.01f, y - s * 0.03f } };
    pa_fill_poly(c, bolt, 6, PA_RGB(255, 210, 50));
}

static void draw_shop(PA_Canvas *c) {
    float u = g_u;
    PA_Paint bg = pa_linear(0, 0, 0, g_h);
    pa_stop(&bg, 0.0f, PA_RGB(122, 86, 236)); pa_stop(&bg, 1.0f, PA_RGB(74, 50, 170));
    pa_fill_rect_paint(c, 0, 0, g_w, g_h, &bg);
    /* diagonal pattern */
    for (int i = -10; i < 30; i++) {
        float x = (float)i * 60.0f * u - fmodf(G.clock * 12.0f, 60.0f * u);
        PA_Vec2 s[4] = { { x, 0 }, { x + 26.0f * u, 0 }, { x + 26.0f * u + g_h * 0.4f, g_h }, { x + g_h * 0.4f, g_h } };
        pa_fill_poly(c, s, 4, PA_RGBA(255, 255, 255, 10));
    }
    draw_hud(c);
    txt(c, "UPGRADES", g_w * 0.5f, 140.0f * u, 46.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 2.2f);

    for (int i = 0; i < 3; i++) {
        Rect r = r_shop_card(i);
        pa_round_rect(c, r.x - 3.0f, r.y - 3.0f + 8.0f * u, r.w + 6.0f, r.h + 6.0f, 26.0f * u, PA_RGBA(20, 10, 60, 90));
        pa_round_rect(c, r.x - 3.0f, r.y - 3.0f, r.w + 6.0f, r.h + 6.0f, 26.0f * u, C_INK);
        pa_round_rect(c, r.x, r.y, r.w, r.h, 23.0f * u, pa_mix(PA_RGB(250, 248, 255), PA_RGB(255, 246, 170), G.shop_flash[i]));
        /* icon tile */
        float ts = r.h - 28.0f * u;
        float ix = r.x + 14.0f * u, iy = r.y + 14.0f * u;
        PA_Color tile = i == 0 ? PA_RGB(80, 170, 255) : (i == 1 ? PA_RGB(150, 110, 255) : PA_RGB(255, 196, 60));
        PA_Paint tp = pa_linear(0, iy, 0, iy + ts);
        pa_stop(&tp, 0.0f, pa_shade(tile, 0.35f)); pa_stop(&tp, 1.0f, tile);
        pa_round_rect_paint(c, ix, iy, ts, ts, 18.0f * u, &tp);
        if (i == 0) icon_cannon(c, ix + ts * 0.5f, iy + ts * 0.5f, ts * 0.8f);
        else if (i == 1) icon_champ(c, ix + ts * 0.5f, iy + ts * 0.55f, ts * 0.8f);
        else draw_coin_icon(c, ix + ts * 0.5f, iy + ts * 0.5f, ts * 0.32f);

        const char *name = i == 0 ? "FIRE RATE" : (i == 1 ? "CHAMPION" : "INCOME");
        int lvl = i == 0 ? G.up_fire : (i == 1 ? G.up_champ : G.up_income);
        int maxl = i == 0 ? MAX_UP_FIRE : (i == 1 ? MAX_UP_CHAMP : MAX_UP_INCOME);
        float tx = ix + ts + 16.0f * u;
        txt(c, name, tx, r.y + 34.0f * u, 24.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 1.4f);
        char lb[24]; snprintf(lb, sizeof(lb), "LVL %d", lvl + 1);
        float lw = pa_text_width(lb, 15.0f * u, 1.0f * u) + 16.0f * u;
        pa_round_rect(c, tx, r.y + 54.0f * u, lw, 24.0f * u, 12.0f * u, PA_RGB(96, 64, 220));
        pa_text(c, lb, tx + 8.0f * u, r.y + 58.5f * u, 15.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 1.0f * u);
        char sb[40];
        if (i == 0) snprintf(sb, sizeof(sb), "%.1f/S > %.1f/S", (double)fire_rate(lvl), (double)fire_rate(lvl + 1));
        else if (i == 1) snprintf(sb, sizeof(sb), "HP %d > %d", champ_hp(lvl), champ_hp(lvl + 1));
        else snprintf(sb, sizeof(sb), "+%d%% > +%d%%", lvl * 15, (lvl + 1) * 15);
        if (lvl >= maxl) snprintf(sb, sizeof(sb), "MAXED");
        pa_text(c, sb, tx, r.y + 90.0f * u, 15.0f * u, PA_RGB(110, 96, 150), PA_ALIGN_LEFT, 1.0f * u);

        Rect b = r_shop_buy(i);
        int ok = can_afford(i);
        float press = (G.press_id == i) ? G.press_t : 0.0f;
        button3d(c, b, lvl >= maxl ? PA_RGB(150, 150, 170) : (ok ? PA_RGB(76, 206, 80) : PA_RGB(170, 168, 190)), NULL, 0, press);
        float by = b.y + 7.0f * u - 7.0f * u * (1.0f - press) + b.h * 0.5f;
        if (lvl >= maxl) txt(c, "MAX", b.x + b.w * 0.5f, by, 24.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.4f);
        else {
            int cost = i == 0 ? cost_fire(lvl) : (i == 1 ? cost_champ(lvl) : cost_income(lvl));
            char cb[16]; fmt_num(cost, cb, sizeof(cb));
            float cs = 24.0f * u;
            float w = pa_text_width(cb, cs, cs * 0.05f) + 30.0f * u;
            float x0 = b.x + (b.w - w) * 0.5f;
            if (i == 1) draw_gem_icon(c, x0 + 10.0f * u, by, 11.0f * u);
            else draw_coin_icon(c, x0 + 10.0f * u, by, 12.0f * u);
            txt(c, cb, x0 + 30.0f * u, by, cs, PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 1.4f);
        }
    }
    /* champion preview under the cards: a little crowd and the brute */
    Rect pl = r_shop_play();
    float last = r_shop_card(2).y + r_shop_card(2).h;
    float mid = (last + pl.y) * 0.5f;
    if (pl.y - last > 150.0f * u) {
        float H = 34.0f * u;
        int idx = (int)((H - 8.0f) * 0.5f);
        idx = idx < 0 ? 0 : (idx >= NSPR ? NSPR - 1 : idx);
        pa_fill_ellipse(c, g_w * 0.5f, mid + 40.0f * u, 200.0f * u, 34.0f * u, PA_RGBA(30, 16, 90, 90));
        for (int i = 0; i < 26; i++) {
            float a = (float)i * 2.4f;
            float rr = 30.0f * u + 140.0f * u * ((float)(i % 7) / 7.0f);
            float x = g_w * 0.5f + cosf(a) * rr, y = mid + 40.0f * u + sinf(a) * rr * 0.18f;
            int fr = ((int)(G.clock * 8.0f + (float)i)) & 1;
            blit(c, &g_spr[T_BLUE][idx][fr], (int)x, (int)y - fr);
        }
        Brute b; memset(&b, 0, sizeof(b));
        b.hp = b.maxhp = (float)champ_hp(G.up_champ); b.size = 1.0f; b.ph = G.clock * 3.0f; b.flash = G.shop_flash[1];
        /* draw the champion at a fixed screen size by borrowing the projection */
        float k = 70.0f * u;
        PA_Vec2 f = { g_w * 0.5f, mid + 52.0f * u };
        (void)k; (void)f;
        {
            float sv_k = g_k, sv_y0 = g_y0, sv_yh = g_yh, sv_w = g_w, sx_ = g_shx, sy_ = g_shy;
            g_shx = 0; g_shy = 0;
            /* a projection where z=0 lands on the preview spot at the wanted scale */
            g_k = 70.0f * u; g_y0 = mid + 52.0f * u; g_yh = g_y0 - 1000.0f;
            b.x = 0.0f; b.z = 0.0f;
            draw_brute(c, &b, 0);
            g_k = sv_k; g_y0 = sv_y0; g_yh = sv_yh; g_w = sv_w; g_shx = sx_; g_shy = sy_;
        }
    }
    char pb[32]; snprintf(pb, sizeof(pb), "PLAY");
    button3d(c, pl, PA_RGB(76, 206, 80), pb, 40.0f * u, 0.0f);
    char lb[24]; snprintf(lb, sizeof(lb), "LEVEL %d", G.level);
    txt(c, lb, g_w * 0.5f, pl.y + pl.h + 30.0f * u, 20.0f * u, PA_RGB(230, 220, 255), PA_ALIGN_CENTER, 1.2f);
}

static void s_render(PA_Canvas *c) {
    layout((float)c->w, (float)c->h);
    if (g_spr_k != g_k) spr_build();
    if (G.state == S_SHOP) { draw_shop(c); pa_hub_hide_pause(); return; }
    /* screen shake on the world only */
    float sh = G.shake * g_u;
    g_shx = sh * sinf(G.clock * 53.0f);
    g_shy = sh * 0.6f * cosf(G.clock * 41.0f);
    draw_world(c);
    g_shx = g_shy = 0.0f;
    draw_hud(c);
    if (G.state == S_READY) draw_ready(c);
    else if (G.state == S_WIN && G.st > 0.15f) {
        float a = pa_clamp01((G.st - 0.15f) * 4.0f);
        float pop = a < 1.0f ? 0.5f + 0.7f * a : 1.2f - 0.2f * pa_clamp01((G.st - 0.4f) * 5.0f);
        txt(c, "CASTLE DOWN!", g_w * 0.5f, g_h * 0.42f, 48.0f * g_u * pop, PA_RGB(255, 220, 60), PA_ALIGN_CENTER, 2.2f);
    } else if (G.state == S_FAIL && G.st > 0.15f) {
        float a = pa_smooth(pa_clamp01((G.st - 0.15f) * 3.0f));
        pa_fill_rect(c, 0, 0, g_w, g_h, PA_RGBA(120, 10, 20, (int)(60.0f * a)));
        txt(c, "BREACHED!", g_w * 0.5f, g_h * 0.42f, 52.0f * g_u * (0.5f + 0.5f * a), PA_RGB(255, 90, 80), PA_ALIGN_CENTER, 2.2f);
    } else if (G.state == S_RESULT) draw_result(c);
}

/* ---- hub tile ---- */
static void s_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    PA_Paint bg = pa_linear(0, y, 0, y + h);
    pa_stop(&bg, 0.0f, PA_RGB(214, 218, 232)); pa_stop(&bg, 1.0f, PA_RGB(182, 188, 208));
    pa_fill_rect_paint(c, x, y, w, h, &bg);
    float cx = x + w * 0.5f;
    PA_Vec2 tr[4] = { { cx - w * 0.46f, y + h }, { cx + w * 0.46f, y + h }, { cx + w * 0.22f, y + h * 0.18f }, { cx - w * 0.22f, y + h * 0.18f } };
    pa_fill_poly(c, tr, 4, PA_RGB(222, 223, 230));
    /* castle */
    pa_fill_rect(c, cx - w * 0.22f, y + h * 0.06f, w * 0.44f, h * 0.14f, PA_RGB(226, 64, 64));
    pa_fill_rect(c, cx - w * 0.22f, y + h * 0.06f, w * 0.44f, h * 0.03f, PA_RGB(250, 110, 100));
    pa_round_rect(c, cx - w * 0.06f, y + h * 0.12f, w * 0.12f, h * 0.08f, w * 0.03f, PA_RGB(60, 20, 34));
    /* reds */
    for (int i = 0; i < 36; i++) {
        float a = (float)i * 2.39f;
        float rx = cx + cosf(a) * w * 0.14f * ((float)(i % 6) / 6.0f + 0.2f);
        float ry = y + h * 0.36f + sinf(a) * h * 0.06f + sinf(t * 2.0f) * h * 0.01f;
        pa_fill_circle(c, rx, ry, w * 0.022f, PA_RGB(236, 52, 52));
    }
    /* gate */
    pa_fill_rect(c, cx - w * 0.30f, y + h * 0.48f, w * 0.30f, h * 0.10f, PA_RGBA(176, 72, 244, 160));
    pa_text_bold(c, "X3", cx - w * 0.15f, y + h * 0.49f, h * 0.075f, PA_RGB(255, 255, 255), C_INK, PA_ALIGN_CENTER, 1.0f, 1.2f);
    /* blues */
    for (int i = 0; i < 50; i++) {
        float a = (float)i * 2.39f;
        float rx = cx - w * 0.06f + cosf(a) * w * 0.16f * ((float)(i % 7) / 7.0f + 0.15f);
        float ry = y + h * 0.66f + sinf(a) * h * 0.07f;
        pa_fill_circle(c, rx, ry, w * 0.026f, PA_RGB(42, 140, 255));
        pa_fill_circle(c, rx - w * 0.006f, ry - w * 0.008f, w * 0.01f, PA_RGB(140, 210, 255));
    }
    /* cannon */
    float kx = cx + sinf(t * 1.5f) * w * 0.1f;
    pa_round_rect(c, kx - w * 0.07f, y + h * 0.80f, w * 0.14f, h * 0.14f, w * 0.06f, PA_RGB(44, 140, 255));
    pa_fill_ellipse(c, kx, y + h * 0.82f, w * 0.05f, h * 0.02f, PA_RGB(14, 30, 80));
}

const PA_Game PA_GAME_MOBCLASH = {
    "mobclash", "Mob Clash", "Crowd Battler",
    "Fire your mob through the gates and storm the enemy castle.",
    PA_RGB(66, 140, 255),
    s_start, s_stop, s_update, s_render, s_thumb
};
