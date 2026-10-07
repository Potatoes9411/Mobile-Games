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
#define TRACK_LEN    40.0f       /* castle front */
#define CAM_D        20.0f       /* perspective distance: a low, close chase camera */
#define VK           0.95f       /* vertical foreshortening of heights */
#define CANNON_Z     1.0f
#define DEFENCE_Z    2.6f        /* hazard stripe: reds crossing it hurt the cannon */
#define UNIT_H       1.05f
#define BLUE_SPEED   6.4f
#define HIT_R        0.50f
#define MAX_BLUE     3000
#define MAX_RED      2200
#define MAX_BRUTES   10
#define MAX_CHAMPS   8
#define MAX_GATES    12
#define MAX_LADDER   40
#define SEP_R        0.42f
#define MAX_CANNONS  7
#define MAX_PARTS    900
#define MAX_FLOATS   40
#define MAX_DECOR    90
#define MAX_SCUFF    40
#define CELL         0.7f
#define GX           18          /* x in [-6, 6.6) */
#define GZ           72          /* z in [-4, 46.4) */
#define ZBINS        GZ

enum { S_READY, S_PLAY, S_WIN, S_FAIL, S_RESULT, S_SHOP };
enum { G_MUL, G_ADD, G_SUB };
enum { PK_PUFF, PK_BIT, PK_RING, PK_SPARK, PK_FLASH, PK_DUST };
enum { T_BLUE, T_RED, T_GLOW, T_COUNT };

typedef struct { float x, z, vx, ph, fresh; uint64_t lad; uint16_t gates; uint8_t dead; } Mob;
typedef struct { float x, z, hp, maxhp, flash, ph, size, speed, dmg; uint16_t gates; int boss; } Brute;
typedef struct { int type, val; float x0, x, z, w, amp, spd, ph, pulse, cool; int hunger; } Gate;
typedef struct { float x, z, y, vx, vz, vy, life, max, size; PA_Color col; int kind; } Part;
typedef struct { float x, z, y, t, size; PA_Color col; char text[16]; } Float;
typedef struct { float x, z, s; int kind; } Decor;
typedef struct { float x, y, w, h; } Rect;
typedef struct { float x, z, w, cool, pulse; } Ladder;

/* ------------------------------------------------------------- state -- */
static Mob   g_blue[MAX_BLUE];  static int g_nblue;
static Mob   g_red[MAX_RED];    static int g_nred;
static Brute g_brute[MAX_BRUTES]; static int g_nbrute;
static Brute g_champ[MAX_CHAMPS]; static int g_nchamp;
static Gate  g_gate[MAX_GATES]; static int g_ngate;
static Ladder g_lad[MAX_LADDER]; static int g_nlad;
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
    int   boss_level, theme, built_level;
    float cannon_x, cannon_tx, recoil, cannon_flash, crecoil[MAX_CANNONS];
    int   shot_i;
    float dmg_castle, dmg_timer;
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
static float g_zoom = 1.0f, g_zpx, g_zpy;   /* render-only camera push-in */

/* ------------------------------------------------------------ palette -- */
#define C_INK      PA_RGB(38, 30, 74)
#define C_BLUE     0x2F8BFF
#define C_RED      0xF0373A

/* ---------------------------------------------------------- utilities -- */
static float frand(float a, float b) { return pa_rng_range(&G.rng, a, b); }

static void layout(float w, float h) {
    g_w = w; g_h = h;
    int portrait = h >= w;
    /* A low chase camera: the lane fills the screen at the cannon (~80% down)
       and converges on a vanishing point 10% from the top. */
    g_k = fminf(w * 0.50f, h * (portrait ? 0.62f : 0.40f)) / TRACK_HALF;
    g_y0 = h * (portrait ? 0.86f : 0.90f);
    g_yh = h * (portrait ? 0.10f : 0.12f);
    g_u = pa_clampf(fminf(w / 540.0f, h / 900.0f), 0.62f, 2.2f);
}

static float persp(float z) { if (z < -CAM_D + 5.0f) z = -CAM_D + 5.0f; return CAM_D / (z + CAM_D); }
static float ss(float z) { return g_k * persp(z) * g_zoom; }
static float sx(float x, float z) { return g_zpx + (g_w * 0.5f + x * g_k * persp(z) - g_zpx) * g_zoom + g_shx; }
static float sy(float z) { return g_zpy + (g_yh + (g_y0 - g_yh) * persp(z) - g_zpy) * g_zoom + g_shy; }
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
/* mobs per second out of the whole fleet */
static float fire_rate(int lvl)  { return 27.0f + 1.8f * (float)lvl; }
static int   champ_hp(int lvl)   { return 80 + 36 * lvl; }
/* the fleet grows a cannon every second fire-rate level */
static int   fleet_size(int lvl) { int n = 1 + lvl / 2; return n > MAX_CANNONS ? MAX_CANNONS : n; }
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
        p->life = p->max = frand(0.7f, 1.3f); p->size = frand(0.16f, 0.36f); p->col = col;
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

static void flash_ring(float x, float y, float z, float px) {
    Part *p = part_new();
    p->kind = PK_FLASH; p->x = x; p->y = y; p->z = z; p->life = p->max = 0.32f; p->size = px;
}

static void dust(float x, float z, int n, float spread, float size) {
    for (int i = 0; i < n; i++) {
        Part *p = part_new();
        p->kind = PK_DUST; p->x = x + frand(-spread, spread); p->z = z + frand(-0.6f, 1.6f); p->y = frand(0.2f, 2.0f);
        p->vx = frand(-1.2f, 1.2f); p->vz = frand(-0.6f, 0.6f); p->vy = frand(0.3f, 1.4f);
        p->life = p->max = frand(1.0f, 1.8f); p->size = size * frand(0.6f, 1.2f); p->col = pa_hex(0xD9CFC4);
    }
}

/* A heavy hit: chunks, a white flash ring 0.15 of the screen, a short shake. */
static void impact(float x, float y, float z, PA_Color col, int n, float shake) {
    bits(x, y, z, col, n, 5.0f, 6.0f);
    bits(x, y, z, PA_RGB(250, 246, 240), n / 3, 5.0f, 6.0f);
    flash_ring(x, y, z, 0.15f * 540.0f);
    kick(shake);
}

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
    g_nblue = g_nred = g_nbrute = g_nchamp = g_ngate = g_nlad = 0;
    for (int i = 0; i < MAX_PARTS; i++) g_part[i].life = 0.0f;
    for (int i = 0; i < MAX_FLOATS; i++) g_float[i].t = 9.0f;

    float m = (float)(n - 1);
    G.boss_level = n % 5 == 0;
    G.theme = (n - 1) % 3;
    G.built_level = n;

    /* Gate schedule: the product of the best gate in each row. One row of x2
       to teach, then the multipliers stack as the levels climb. */
    int rows, mul[3] = { 2, 2, 2 };
    if (n == 1) { rows = 1; mul[0] = 3; }
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
    G.cannon_max = G.cannon_hp = 30;
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
    G.boss_timer = 0.0f; G.boss_spawned = !G.boss_level;
    G.kills = 0;
    G.t = 0.0f;

    /* Gate rows. The first levels teach with gifts only; traps and moving
       panels arrive once the player knows what a multiplier does. */
    static const float ROWZ[3][3] = { { 11.0f, 0, 0 }, { 8.5f, 17.5f, 0 }, { 7.5f, 14.0f, 21.0f } };
    for (int r = 0; r < rows; r++) {
        float z = ROWZ[rows - 1][r];
        int good = mul[r];
        int add = 5 * (int)((fire_rate(n - 1) * 1.6f + 4.0f) / 5.0f);
        int sub = 3 * (4 + n / 2);
        if (n == 1) { add_gate(G_MUL, 3, -1.0f, z, 4.4f); continue; }
        if (n == 2) { add_gate(G_MUL, 3, 1.95f, z, 3.6f); add_gate(G_ADD, add, -1.95f, z, 3.6f); continue; }
        int pick = pa_rng_int(&G.rng, 0, n >= 7 ? 4 : (n >= 4 ? 3 : 1));
        float side = pa_rng_chance(&G.rng, 0.5f) ? 1.0f : -1.0f;
        switch (pick) {
            case 0: add_gate(G_MUL, good, frand(-1.6f, 1.6f), z, 4.2f); break;
            case 1: add_gate(G_MUL, good, -1.95f * side, z, 3.6f); add_gate(G_ADD, add, 1.95f * side, z, 3.6f); break;
            case 2: add_gate(G_MUL, good, -1.95f * side, z, 3.6f); add_gate(G_SUB, sub, 1.95f * side, z, 3.6f); break;
            case 3: if (good == 3) { add_gate(G_MUL, 3, -1.95f * side, z, 3.6f); add_gate(G_MUL, 2, 1.95f * side, z, 3.6f); }
                    else { add_gate(G_MUL, 2, -1.95f * side, z, 3.6f); add_gate(G_ADD, add, 1.95f * side, z, 3.6f); }
                    break;
            default: add_gate(G_MUL, good, 0.0f, z, 2.6f); add_gate(G_SUB, sub, -2.75f, z, 2.0f);
                     add_gate(G_SUB, sub, 2.75f, z, 2.0f); break;
        }
    }
    /* moving panels from level 4 */
    for (int i = 0; i < g_ngate; i++) {
        Gate *g = &g_gate[i];
        int lone = i + 1 >= g_ngate || g_gate[i + 1].z != g->z;
        if (i > 0 && g_gate[i - 1].z == g->z) lone = 0;
        if (n >= 4 && lone && pa_rng_chance(&G.rng, n >= 9 ? 0.75f : 0.5f)) {
            g->amp = fminf(3.8f - g->w * 0.5f, 1.4f + 0.06f * m);
            g->x0 = 0.0f;
            g->spd = frand(0.7f, 1.1f);
            g->ph = frand(0.0f, PA_TAU);
        }
    }

    /* Rails of stacked +1 panels up both edges, the reference's long +1
       walls: a side route that pays out steadily, and they frame the lane. */
    for (int sd = -1; sd <= 1; sd += 2) {
        for (float z = -2.2f; z < 29.0f && g_nlad < MAX_LADDER; z += 1.85f) {
            Ladder *l = &g_lad[g_nlad++];
            l->x = (float)sd * (TRACK_HALF - 0.62f); l->z = z; l->w = 1.05f; l->cool = 0.0f; l->pulse = 0.0f;
        }
    }

    /* scenery either side of the track */
    g_ndecor = 0;
    for (int i = 0; i < MAX_DECOR; i++) {
        Decor *d = &g_decor[g_ndecor++];
        float side = (i & 1) ? 1.0f : -1.0f;
        d->x = side * frand(TRACK_HALF + 1.6f, TRACK_HALF + 16.0f);
        d->z = frand(-4.0f, TRACK_LEN + 40.0f);
        d->kind = pa_rng_chance(&G.rng, G.theme == 2 ? 0.7f : 0.75f) ? 0 : 1;
        d->s = frand(0.7f, 1.35f);
        if (G.theme == 2 && d->kind == 0) d->x = side * (TRACK_HALF + 2.0f + 3.0f * (float)pa_rng_int(&G.rng, 0, 3));
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
    b->size = boss ? 8.0f : 2.0f; b->speed = boss ? 0.3f : 1.4f;
    if (boss) b->z = 30.0f;
    b->ph = frand(0.0f, PA_TAU);
    pa_tone(110.0f, 70.0f, 0.35f, 3, 0.10f);
}

static void spawn_champ(float x, float z, uint16_t gates, float hp) {
    if (g_nchamp >= MAX_CHAMPS) return;
    Brute *b = &g_champ[g_nchamp++];
    memset(b, 0, sizeof(*b));
    b->x = x; b->z = z; b->hp = b->maxhp = hp; b->size = 1.9f; b->speed = 3.2f; b->gates = gates;
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
        for (int i = 0; i < g_nlad; i++) if (g_lad[i].z > CANNON_Z && fabsf(x - g_lad[i].x) < g_lad[i].w * 0.5f) { v *= 1.0f + 0.6f / flow; flow += 0.6f; }
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
                float off = (float)((k + 1) / 2) * 0.58f * ((k & 1) ? 1.0f : -1.0f);
                Mob *c = blue_new(pa_clampf(x + off, g->x - g->w * 0.5f + 0.1f, g->x + g->w * 0.5f - 0.1f), z + frand(0.0f, 0.25f));
                if (!c) break;
                c->gates = gates; c->lad = m->lad; c->fresh = 0.5f; c->vx = off * 1.8f;
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
                    c->gates = gates; c->lad = m->lad; c->fresh = 0.5f; c->vx = frand(-1.0f, 1.0f);
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

/* Each +1 panel hands out one extra mob to a passer, then needs a moment. */
static void ladder_pass(Mob *m, float pz) {
    for (int i = 0; i < g_nlad; i++) {
        Ladder *l = &g_lad[i];
        if (!(pz < l->z && m->z >= l->z)) continue;
        if (fabsf(m->x - l->x) > l->w * 0.5f) continue;
        uint64_t bit = (uint64_t)1 << i;
        if (m->lad & bit) continue;
        m->lad |= bit;
        if (l->cool > 0.0f) continue;
        l->cool = 0.45f; l->pulse = 1.0f;
        Mob *c = blue_new(m->x + frand(-0.3f, 0.3f), m->z + 0.1f);
        if (c) { c->gates = m->gates; c->lad = m->lad; c->fresh = 0.4f; c->vx = -l->x * 0.12f; }
        m->fresh = 0.4f;
        if (G.sfx_gate <= 0.0f) { G.sfx_gate = 0.07f; pa_tone(880.0f + 40.0f * (float)i, 1320.0f, 0.04f, 1, 0.035f); }
    }
}

/* Fleet layout: up to four abreast at the rail, the rest a step behind. */
static void fleet_slot(int n, int i, float *x, float *z) {
    int front = n < 4 ? n : 4, row = i < front ? 0 : 1;
    int cnt = row ? n - front : front, k = row ? i - front : i;
    *x = ((float)k - (float)(cnt - 1) * 0.5f) * 1.24f;
    *z = row ? -1.15f : 0.0f;
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
        {
            int nf = fleet_size(G.up_fire), front = nf < 4 ? nf : 4;
            float half = (float)(front - 1) * 0.62f;
            G.cannon_tx = pa_clampf(G.cannon_tx, -TRACK_HALF + 0.75f + half, TRACK_HALF - 0.75f - half);
        }
        G.cannon_x = pa_approach(G.cannon_x, G.cannon_tx, 24.0f, dt);
        G.firing = fire;

        /* ---- fire ---- */
        if (fire) {
            G.fire_acc += dt * fire_rate(G.up_fire);
            while (G.fire_acc >= 1.0f) {
                G.fire_acc -= 1.0f;
                /* round robin across the fleet */
                int nf = fleet_size(G.up_fire), ci = G.shot_i++ % nf;
                float cx, cz; fleet_slot(nf, ci, &cx, &cz);
                Mob *m = blue_new(G.cannon_x + cx + frand(-0.2f, 0.2f), CANNON_Z + 0.55f + frand(0.0f, 0.3f));
                if (m) m->vx = frand(-1.6f, 1.6f) + (G.cannon_tx - G.cannon_x) * 0.6f;
                G.recoil = 1.0f; G.crecoil[ci] = 1.0f;
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
            G.wave_acc += dt * G.red_speed / 0.5f;
            /* the wave pours out as a carpet: full-width ranks, shoulder to shoulder */
            while (G.wave_acc >= 1.0f && G.wave_left > 0 && g_nred < G.red_cap) {
                G.wave_acc -= 1.0f;
                int per = 15;
                for (int k = 0; k < per && G.wave_left > 0; k++) {
                    float x = -TRACK_HALF + 0.5f + (float)k * ((2.0f * TRACK_HALF - 1.0f) / (float)(per - 1));
                    red_new(x + frand(-0.12f, 0.12f), TRACK_LEN - 0.4f + frand(-0.15f, 0.15f));
                    G.wave_left--;
                }
            }
        }
        if (!G.boss_spawned && G.t >= G.boss_timer) {
            G.boss_spawned = 1;
            spawn_brute(0.0f, floorf(G.brute_hp * 25.0f), 1);
            float_text(0.0f, 12.0f, 30.0f, "BOSS!", PA_RGB(255, 90, 80), 1.6f);
            kick(5.0f);
        }
    } else {
        G.firing = 0;
    }

    G.recoil = pa_approach(G.recoil, 0.0f, 14.0f, dt);
    for (int i = 0; i < MAX_CANNONS; i++) G.crecoil[i] = pa_approach(G.crecoil[i], 0.0f, 12.0f, dt);

    /* ---- gates ---- */
    for (int i = 0; i < g_ngate; i++) {
        Gate *g = &g_gate[i];
        if (g->amp > 0.0f) g->x = g->x0 + g->amp * sinf(G.t * g->spd + g->ph);
        g->pulse = pa_approach(g->pulse, 0.0f, 7.0f, dt);
        if (g->cool > 0.0f) { g->cool -= dt; if (g->cool <= 0.0f) g->hunger = g->val; }
    }
    for (int i = 0; i < g_nlad; i++) {
        g_lad[i].cool -= dt;
        g_lad[i].pulse = pa_approach(g_lad[i].pulse, 0.0f, 6.0f, dt);
    }

    build_grids();

    /* ---- blue crowd ---- */
    for (int i = 0; i < g_nblue; i++) {
        Mob *m = &g_blue[i];
        float pz = m->z;
        int r0 = row_of(m->z);
        float want = 0.0f, rate = 0.7f;
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
        if (!m->dead && g_nlad) ladder_pass(m, pz);
        if (!m->dead && m->z >= TRACK_LEN - 0.3f) {
            m->dead = 1;
            if (G.base_hp > 0.0f) {
                G.base_hp -= 1.0f;
                G.dmg_castle += 1.0f;
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
        for (int r = r0; r > r0 - 5 && r >= 0; r--) {
            if (g_brow_n[r] > 0) { want = pa_clampf((g_brow_x[r] / (float)g_brow_n[r] - m->x) * 0.5f, -0.8f, 0.8f); break; }
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
            if (d2 < SEP_R * SEP_R && d2 > 1e-6f) {
                float d = sqrtf(d2), push = (SEP_R - d) * 0.5f / d;
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
            if (d2 < SEP_R * SEP_R && d2 > 1e-6f) {
                float d = sqrtf(d2), push = (SEP_R - d) * 0.5f / d;
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
        b->flash = pa_approach(b->flash, 0.0f, 14.0f, dt);
        float rad = 0.55f * b->size;
        int c0 = cell_of(b->x - rad, b->z - rad), c1 = cell_of(b->x + rad, b->z + rad);
        int x0 = c0 % GX, z0 = c0 / GX, x1 = c1 % GX, z1 = c1 / GX;
        for (int zz = z0; zz <= z1; zz++) for (int xx = x0; xx <= x1; xx++) {
            for (int j = g_bhead[zz * GX + xx]; j >= 0; j = g_bnext[j]) {
                Mob *m = &g_blue[j];
                if (m->dead) continue;
                float ddx = m->x - b->x, ddz = m->z - b->z;
                if (ddx * ddx + ddz * ddz < rad * rad && b->hp > 0.0f) {
                    m->dead = 1; b->hp -= 1.0f; b->dmg += 1.0f; if (b->flash < 0.15f) b->flash = 1.0f;
                    puff(m->x, m->z, PA_RGB(140, 200, 255), 1, 1.0f);
                    if (pa_rng_chance(&G.rng, 0.25f)) bits(m->x, 0.8f * b->size, b->z - 0.3f * b->size, PA_RGB(255, 206, 60), 2, 3.0f, 3.0f);
                    snd_kill();
                }
            }
        }
        if (b->hp <= 0.0f) {
            impact(b->x, 1.2f * b->size, b->z, PA_RGB(255, 206, 60), b->boss ? 26 : 16, b->boss ? 12.0f : 6.0f);
            bits(b->x, 1.2f * b->size, b->z, PA_RGB(230, 50, 50), 8, 4.0f, 5.0f);
            ring(b->x, b->z, PA_RGB(255, 230, 140), 1.2f * b->size);
            if (b->boss) dust(b->x, b->z, 10, 2.0f, 1.4f);
            pa_sfx("boom");
            G.kills += 5;
        } else if (b->z < DEFENCE_Z + 0.2f && live) {
            G.cannon_hp -= b->boss ? 999 : 15;
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
            float reach = 0.45f * (e->size + b->size);
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
        float rad = 0.5f * b->size;
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
            impact(b->x, 2.0f, TRACK_LEN - 0.2f, PA_RGB(226, 64, 64), 18, 9.0f);
            dust(b->x, TRACK_LEN, 5, 1.5f, 1.2f);
            pa_sfx("boom");
            b->hp = 0.0f;
        } else if (b->hp <= 0.0f) {
            impact(b->x, 1.2f, b->z, PA_RGB(90, 170, 255), 12, 4.0f);
            puff(b->x, b->z, PA_RGB(200, 230, 255), 4, 2.0f);
        }
    }

    /* outlined damage numbers, batched so they stay readable */
    G.dmg_timer -= dt;
    if (G.dmg_timer <= 0.0f) {
        G.dmg_timer = 0.32f;
        char buf[16];
        if (G.dmg_castle >= 1.0f && G.base_hp > 0.0f) {
            snprintf(buf, sizeof(buf), "-%d", (int)G.dmg_castle);
            float_text(frand(-2.5f, 2.5f), 6.5f, TRACK_LEN, buf, PA_RGB(255, 255, 255), 1.2f);
            G.dmg_castle = 0.0f;
        }
        for (int i = 0; i < g_nbrute; i++) if (g_brute[i].dmg >= 1.0f && g_brute[i].hp > 0.0f) {
            Brute *b = &g_brute[i];
            snprintf(buf, sizeof(buf), "-%d", (int)b->dmg);
            float_text(b->x + frand(-0.3f, 0.3f) * b->size, 1.6f * b->size, b->z - 0.5f, buf, PA_RGB(255, 236, 90), 1.25f);
            b->dmg = 0.0f;
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
            /* the castle comes down: chunks, a flash, a rolling dust cloud */
            for (int k = 0; k < 7; k++) {
                bits(frand(-5.0f, 5.0f), frand(1.0f, 5.0f), TRACK_LEN + frand(0.0f, 3.0f), PA_RGB(226, 64, 64), 8, 7.0f, 8.0f);
                bits(frand(-5.0f, 5.0f), frand(1.0f, 5.0f), TRACK_LEN + frand(0.0f, 3.0f), PA_RGB(246, 240, 236), 5, 7.0f, 8.0f);
            }
            flash_ring(0.0f, 3.0f, TRACK_LEN, 0.3f * 540.0f);
            dust(0.0f, TRACK_LEN + 0.5f, 26, 6.0f, 2.2f);
            kick(16.0f);
            pa_sfx("boom");
            pa_sfx("win");
            finish(1);
        } else if (G.cannon_hp <= 0) {
            G.cannon_hp = 0;
            puff(G.cannon_x, CANNON_Z, PA_RGB(80, 80, 90), 10, 2.5f);
            impact(G.cannon_x, 0.8f, CANNON_Z, PA_RGB(60, 140, 255), 18, 14.0f);
            dust(G.cannon_x, CANNON_Z, 8, 1.0f, 0.9f);
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
        } else if (p->kind == PK_DUST) {
            p->vx *= 1.0f - 1.5f * dt; p->vy *= 1.0f - 1.2f * dt;
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
#define NSPR 29
typedef struct { int w, h, ax, ay; uint8_t *a; uint32_t *c; } Spr;
static Spr   g_spr[T_COUNT][NSPR][2];
static float g_spr_k = -1.0f;

static float spr_height(int s) { return 8.0f + (float)s * 3.0f; }

static void team_cols(int team, PA_Color *mid, PA_Color *lite, PA_Color *dark, PA_Color *rim) {
    if (team == T_RED) { *mid = pa_hex(0xE8231E); *lite = pa_hex(0xFF8C7C); *dark = pa_hex(0x98121C); *rim = pa_hex(0xFFC9BE); }
    else if (team == T_GLOW) { *mid = pa_hex(0x8CCBFF); *lite = pa_hex(0xF2FBFF); *dark = pa_hex(0x3F8CF0); *rim = pa_hex(0xFFFFFF); }
    else { *mid = pa_hex(0x2F8BFF); *lite = pa_hex(0x9FD4FF); *dark = pa_hex(0x1452C8); *rim = pa_hex(0xBFE6FF); }
}

/* One little mob person, foot at (fx, fy), H pixels tall, lit by a key light
   from the upper left with a cool rim on the right. Blue run away from the
   camera so we see their backs; red run at it and show their eyes. */
static void draw_mob(PA_Canvas *c, float fx, float fy, float H, int team, int frame) {
    PA_Color mid, lite, dark, rim;
    team_cols(team, &mid, &lite, &dark, &rim);
    float st = frame ? 1.0f : -1.0f;
    /* blob contact shadow, 30% black */
    pa_fill_ellipse(c, fx + H * 0.04f, fy - H * 0.03f, H * 0.33f, H * 0.12f, PA_RGBA(0, 0, 0, 77));
    /* feet */
    pa_fill_ellipse(c, fx - H * 0.13f, fy - H * 0.07f - st * H * 0.04f, H * 0.10f, H * 0.08f, dark);
    pa_fill_ellipse(c, fx + H * 0.13f, fy - H * 0.07f + st * H * 0.04f, H * 0.10f, H * 0.08f, dark);
    /* arms swing against the feet */
    pa_fill_ellipse(c, fx - H * 0.26f, fy - H * 0.40f + st * H * 0.06f, H * 0.08f, H * 0.13f, pa_mix(mid, lite, 0.2f));
    pa_fill_ellipse(c, fx + H * 0.26f, fy - H * 0.40f - st * H * 0.06f, H * 0.08f, H * 0.13f, pa_mix(mid, dark, 0.45f));
    pa_fill_ellipse(c, fx + H * 0.29f, fy - H * 0.43f - st * H * 0.06f, H * 0.03f, H * 0.08f, pa_alpha(rim, 0.8f));
    /* bean body: rim pass first, the lit body over it nudged left */
    pa_round_rect(c, fx - H * 0.20f, fy - H * 0.63f, H * 0.44f, H * 0.57f, H * 0.21f, rim);
    PA_Paint p = pa_linear(fx - H * 0.25f, fy - H * 0.66f, fx + H * 0.20f, fy - H * 0.06f);
    pa_stop(&p, 0.0f, lite); pa_stop(&p, 0.42f, mid); pa_stop(&p, 1.0f, dark);
    pa_round_rect_paint(c, fx - H * 0.23f, fy - H * 0.61f, H * 0.43f, H * 0.56f, H * 0.21f, &p);
    pa_fill_ellipse(c, fx - H * 0.13f, fy - H * 0.42f, H * 0.04f, H * 0.11f, pa_alpha(PA_RGB(255, 255, 255), 0.45f));
    /* head, same treatment */
    pa_fill_circle(c, fx + H * 0.02f, fy - H * 0.735f, H * 0.235f, rim);
    PA_Paint ph = pa_radial(fx - H * 0.09f, fy - H * 0.82f, 0.0f, H * 0.32f);
    pa_stop(&ph, 0.0f, lite); pa_stop(&ph, 0.5f, mid); pa_stop(&ph, 1.0f, dark);
    pa_fill_ellipse_paint(c, fx - H * 0.01f, fy - H * 0.725f, H * 0.225f, H * 0.225f, &ph);
    /* little ears, the mob's silhouette */
    pa_fill_circle(c, fx - H * 0.17f, fy - H * 0.91f, H * 0.075f, pa_mix(mid, lite, 0.3f));
    pa_fill_circle(c, fx + H * 0.16f, fy - H * 0.91f, H * 0.075f, pa_mix(mid, dark, 0.3f));
    /* specular */
    pa_fill_ellipse(c, fx - H * 0.09f, fy - H * 0.83f, H * 0.08f, H * 0.05f, pa_alpha(PA_RGB(255, 255, 255), 0.85f));
    if (team == T_RED) {
        pa_fill_circle(c, fx - H * 0.08f, fy - H * 0.70f, H * 0.06f, PA_RGB(255, 255, 255));
        pa_fill_circle(c, fx + H * 0.08f, fy - H * 0.70f, H * 0.06f, PA_RGB(255, 255, 255));
        pa_fill_circle(c, fx - H * 0.075f, fy - H * 0.69f, H * 0.03f, PA_RGB(40, 10, 20));
        pa_fill_circle(c, fx + H * 0.085f, fy - H * 0.69f, H * 0.03f, PA_RGB(40, 10, 20));
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

/* Paint something once into a premultiplied sprite: drawn on black and on
   white so the exact coverage falls out, the same trick as the mob sprites. */
typedef void (*CaptureFn)(PA_Canvas *c, const void *arg);
static void capture(Spr *sp, float x0, float y0, float x1, float y1, float ax, float ay, CaptureFn fn, const void *arg) {
    free(sp->a); free(sp->c); sp->a = NULL; sp->c = NULL;
    int w = (int)ceilf(x1 - x0), h = (int)ceilf(y1 - y0);
    if (w < 1 || h < 1 || w > 4096 || h > 4096) return;
    PA_Canvas blk, wht;
    if (!pa_canvas_init(&blk, w, h)) return;
    if (!pa_canvas_init(&wht, w, h)) { pa_canvas_free(&blk); return; }
    pa_clear(&blk, PA_RGB(0, 0, 0)); pa_clear(&wht, PA_RGB(255, 255, 255));
    float sx_ = g_shx, sy_ = g_shy;
    g_shx = -floorf(x0); g_shy = -floorf(y0);
    fn(&blk, arg); fn(&wht, arg);
    g_shx = sx_; g_shy = sy_;
    sp->w = w; sp->h = h; sp->ax = (int)(ax - floorf(x0)); sp->ay = (int)(ay - floorf(y0));
    sp->a = (uint8_t *)malloc((size_t)w * (size_t)h);
    sp->c = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
    if (sp->a && sp->c) for (int i = 0; i < w * h; i++) {
        uint32_t b = blk.px[i], wv = wht.px[i];
        int d = (int)((wv >> 16) & 255) - (int)((b >> 16) & 255) + (int)((wv >> 8) & 255) - (int)((b >> 8) & 255)
              + (int)(wv & 255) - (int)(b & 255);
        int a = 255 - d / 3;
        sp->a[i] = (uint8_t)(a < 0 ? 0 : (a > 255 ? 255 : a));
        sp->c[i] = b & 0x00FFFFFFu;
    }
    pa_canvas_free(&blk); pa_canvas_free(&wht);
}

/* Soft discs for particles: a tinted coverage mask is far cheaper than an
   antialiased polygon for the hundreds of puffs a clash throws up. */
#define NMASK 48
static uint8_t *g_mask[NMASK + 1];
static void mask_build(void) {
    for (int r = 1; r <= NMASK; r++) {
        if (g_mask[r]) continue;
        int d = 2 * r + 2;
        g_mask[r] = (uint8_t *)malloc((size_t)d * (size_t)d);
        if (!g_mask[r]) continue;
        for (int y = 0; y < d; y++) for (int x = 0; x < d; x++) {
            float dx = (float)x + 0.5f - (float)(r + 1), dy = (float)y + 0.5f - (float)(r + 1);
            float a = pa_clamp01((float)r - sqrtf(dx * dx + dy * dy) + 0.5f);
            g_mask[r][y * d + x] = (uint8_t)(a * 255.0f);
        }
    }
}
static void disc(PA_Canvas *c, float cx, float cy, float rad, PA_Color col, float alpha) {
    int r = (int)(rad + 0.5f);
    int al = (int)(alpha * (float)PA_A(col));
    if (al <= 2) return;
    if (r > NMASK) { pa_fill_circle(c, cx, cy, rad, pa_alpha(col, alpha * (float)PA_A(col) / 255.0f)); return; }
    if (r < 1) r = 1;
    if (!g_mask[r]) return;
    int d = 2 * r + 2, x0 = (int)cx - (r + 1), y0 = (int)cy - (r + 1);
    uint32_t cr = PA_R(col), cg = PA_G(col), cb = PA_B(col);
    for (int y = 0; y < d; y++) {
        int yy = y0 + y;
        if (yy < c->clip_y0 || yy >= c->clip_y1) continue;
        uint32_t *row = c->px + (size_t)yy * (size_t)c->w;
        const uint8_t *m = g_mask[r] + y * d;
        for (int x = 0; x < d; x++) {
            int xx = x0 + x;
            if (xx < c->clip_x0 || xx >= c->clip_x1 || !m[x]) continue;
            uint32_t a = (uint32_t)(m[x] * al) >> 8, ia = 255 - a, v = row[xx];
            uint32_t rr = (cr * a + ((v >> 16) & 255) * ia) >> 8, gg = (cg * a + ((v >> 8) & 255) * ia) >> 8, bb = (cb * a + (v & 255) * ia) >> 8;
            row[xx] = (rr << 16) | (gg << 8) | bb;
        }
    }
}

/* Cached art under the push-in camera: the sprite was painted at zoom 1 with
   its anchor at (ax0, ay0); place it about the pivot and scale, nearest. */
static void blit_zoomed(PA_Canvas *c, const Spr *s, float ax0, float ay0) {
    if (!s->a) return;
    float Z = g_zoom;
    float ox = g_zpx + (ax0 - g_zpx) * Z + g_shx - (float)s->ax * Z;
    float oy = g_zpy + (ay0 - g_zpy) * Z + g_shy - (float)s->ay * Z;
    int dw = (int)((float)s->w * Z), dh = (int)((float)s->h * Z);
    int x0 = (int)ox, y0 = (int)oy;
    for (int y = 0; y < dh; y++) {
        int yy = y0 + y;
        if (yy < c->clip_y0 || yy >= c->clip_y1) continue;
        int sy_ = (int)((float)y / Z); if (sy_ >= s->h) sy_ = s->h - 1;
        const uint8_t *ar = s->a + sy_ * s->w;
        const uint32_t *cr = s->c + sy_ * s->w;
        uint32_t *dst = c->px + (size_t)yy * (size_t)c->w;
        for (int x = 0; x < dw; x++) {
            int xx = x0 + x;
            if (xx < c->clip_x0 || xx >= c->clip_x1) continue;
            int sx_ = (int)((float)x / Z); if (sx_ >= s->w) sx_ = s->w - 1;
            int a = ar[sx_];
            if (!a) continue;
            if (a == 255) { dst[xx] = cr[sx_]; continue; }
            uint32_t d = dst[xx], inv = (uint32_t)(255 - a);
            uint32_t rb = ((d & 0xFF00FFu) * inv >> 8) & 0xFF00FFu, g = ((d & 0x00FF00u) * inv >> 8) & 0x00FF00u;
            dst[xx] = ((rb | g) + cr[sx_]) & 0xFFFFFFu;
        }
    }
}

/* the zoom-1 screen position of a world point, for anchoring cached art */
static PA_Vec2 proj1(float x, float y, float z) {
    float p = persp(z);
    PA_Vec2 v = { g_w * 0.5f + x * g_k * p, g_yh + (g_y0 - g_yh) * p - y * g_k * p * VK };
    return v;
}

static void frect(PA_Canvas *c, float fx, float fy, float fw, float fh, PA_Color col, float alpha) {
    int x0 = (int)fx, y0 = (int)fy, x1 = (int)(fx + fw + 0.5f), y1 = (int)(fy + fh + 0.5f);
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    x0 = x0 < c->clip_x0 ? c->clip_x0 : x0; y0 = y0 < c->clip_y0 ? c->clip_y0 : y0;
    x1 = x1 > c->clip_x1 ? c->clip_x1 : x1; y1 = y1 > c->clip_y1 ? c->clip_y1 : y1;
    uint32_t a = (uint32_t)(pa_clamp01(alpha) * (float)PA_A(col)), ia = 255 - a;
    uint32_t cr = PA_R(col) * a, cg = PA_G(col) * a, cb = PA_B(col) * a;
    for (int y = y0; y < y1; y++) {
        uint32_t *row = c->px + (size_t)y * (size_t)c->w;
        for (int x = x0; x < x1; x++) {
            uint32_t v = row[x];
            row[x] = (((cr + ((v >> 16) & 255) * ia) >> 8) << 16) | (((cg + ((v >> 8) & 255) * ia) >> 8) << 8) | ((cb + (v & 255) * ia) >> 8);
        }
    }
}

static const Spr *spr_for(int team, float z, int frame) {
    float H = UNIT_H * ss(z);
    int s = (int)((H - 8.0f) / 3.0f + 0.5f);
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

/* ---- the scenery ----
   Everything that never moves (sky, horizon, ground, props, the textured road
   and its paint) is painted once per level into a cached canvas and copied in
   each frame, so it can afford per-pixel grain and dozens of decals. */
typedef struct {
    uint32_t sky0, sky1, haze, gfar, gnear, rfar, rnear, curb, curb_side, speck, hill, hill2;
} Theme;
static const Theme THEMES[3] = {
    /* snow field */
    { 0xB9C6E4, 0xE8EDF7, 0xF2F4FA, 0xE4E8F2, 0xC9CFE0, 0xE3E4EA, 0xC6C8D2, 0xF5F6FA, 0xAEB2C4, 0xFFFFFF, 0xCDD5EA, 0xB6C0DC },
    /* desert road */
    { 0xF0BE7E, 0xFBE3BC, 0xFBEAD0, 0xF1D7A8, 0xDDB27A, 0xB3ACA7, 0x948C86, 0xEDE6DC, 0x8A8178, 0xC0904F, 0xE7AE74, 0xD8955C },
    /* harbour yard */
    { 0x86B0E2, 0xD7E6F5, 0xE4EDF7, 0xAAB3C7, 0x8D96AC, 0xD4D6DC, 0xB9BBC5, 0xF1F2F6, 0x9599AA, 0x6F788E, 0xA7B8D2, 0x91A4C2 },
};

static PA_Canvas g_bg;
static int g_bg_key = -1, g_bg_w, g_bg_h;

static uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}
static float hashf(int x, int y) { return (float)(hash32((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u) & 0xFFFF) / 65535.0f; }
/* smooth value noise for blotches on the asphalt */
static float vnoise(float x, float y) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    fx = fx * fx * (3.0f - 2.0f * fx); fy = fy * fy * (3.0f - 2.0f * fy);
    float a = hashf(ix, iy), b = hashf(ix + 1, iy), c = hashf(ix, iy + 1), d = hashf(ix + 1, iy + 1);
    return pa_lerpf(pa_lerpf(a, b, fx), pa_lerpf(c, d, fx), fy);
}

static void draw_pine(PA_Canvas *c, float x, float z, float s) {
    float k = ss(z);
    PA_Vec2 b = proj(x, 0.0f, z);
    float h = 3.4f * s * k * VK, w = 1.15f * s * k;
    pa_shadow(c, b.x + w * 0.5f, b.y, w * 1.2f, w * 0.32f, 0.45f);
    pa_fill_rect(c, b.x - w * 0.1f, b.y - h * 0.22f, w * 0.2f, h * 0.22f, PA_RGB(120, 108, 120));
    for (int t = 0; t < 3; t++) {
        float ty = b.y - h * (0.18f + 0.26f * (float)t);
        float tw = w * (1.0f - 0.24f * (float)t);
        float th = h * 0.42f;
        PA_Vec2 l[3] = { { b.x, ty - th }, { b.x - tw, ty }, { b.x, ty + th * 0.08f } };
        PA_Vec2 r[3] = { { b.x, ty - th }, { b.x, ty + th * 0.08f }, { b.x + tw, ty } };
        pa_fill_poly(c, l, 3, PA_RGB(132, 166, 160));
        pa_fill_poly(c, r, 3, PA_RGB(92, 124, 130));
        /* snow on each tier */
        PA_Vec2 sn[4] = { { b.x, ty - th }, { b.x - tw * 0.55f, ty - th * 0.42f }, { b.x, ty - th * 0.55f }, { b.x + tw * 0.5f, ty - th * 0.45f } };
        pa_fill_poly(c, sn, 4, PA_RGB(236, 242, 248));
    }
}

static void draw_rock(PA_Canvas *c, float x, float z, float s, PA_Color lit, PA_Color shade) {
    float k = ss(z);
    PA_Vec2 b = proj(x, 0.0f, z);
    float w = 0.9f * s * k, h = 0.6f * s * k * VK;
    pa_shadow(c, b.x + w * 0.2f, b.y, w * 1.2f, w * 0.3f, 0.4f);
    PA_Vec2 r[6] = { { b.x - w, b.y }, { b.x - w * 0.7f, b.y - h * 0.8f }, { b.x - w * 0.1f, b.y - h * 1.1f },
                     { b.x + w * 0.6f, b.y - h * 0.8f }, { b.x + w, b.y }, { b.x, b.y + h * 0.1f } };
    pa_fill_poly(c, r, 6, shade);
    PA_Vec2 hi[4] = { { b.x - w * 0.7f, b.y - h * 0.8f }, { b.x - w * 0.1f, b.y - h * 1.1f }, { b.x + w * 0.2f, b.y - h * 0.5f }, { b.x - w * 0.5f, b.y - h * 0.35f } };
    pa_fill_poly(c, hi, 4, lit);
}

static void draw_cactus(PA_Canvas *c, float x, float z, float s) {
    float k = ss(z);
    PA_Vec2 b = proj(x, 0.0f, z);
    float h = 2.6f * s * k * VK, w = 0.36f * s * k;
    pa_shadow(c, b.x + w * 1.5f, b.y, w * 2.6f, w * 0.6f, 0.45f);
    PA_Color g0 = PA_RGB(92, 168, 92), g1 = PA_RGB(56, 122, 70);
    pa_round_rect(c, b.x - w * 0.5f, b.y - h, w, h, w * 0.5f, g1);
    pa_round_rect(c, b.x - w * 0.5f, b.y - h, w * 0.55f, h, w * 0.3f, g0);
    for (int sd = -1; sd <= 1; sd += 2) {
        float ay = b.y - h * (sd < 0 ? 0.55f : 0.68f), ax = b.x + (float)sd * w * 1.15f;
        pa_round_rect(c, ax - w * 0.32f, ay - h * 0.28f, w * 0.64f, h * 0.32f, w * 0.32f, sd < 0 ? g0 : g1);
        pa_round_rect(c, fminf(ax, b.x), ay - w * 0.3f, fabsf(ax - b.x), w * 0.6f, w * 0.3f, sd < 0 ? g0 : g1);
    }
}

static void draw_container(PA_Canvas *c, float x, float z, float s, int tint) {
    static const uint32_t TINT[5] = { 0xD9473E, 0x2F6FD0, 0xF09A2A, 0x2AA39A, 0x8A57C8 };
    PA_Color base = pa_hex(TINT[tint % 5]);
    float len = 2.6f * s, wid = 1.1f, hgt = 1.15f;
    float stack = (tint % 3 == 0) ? 2.0f : 1.0f;
    pa_shadow(c, sx(x, z), sy(z), ss(z) * len * 0.8f, ss(z) * 0.5f, 0.5f);
    for (int i = 0; i < (int)stack; i++) {
        PA_Color b2 = i ? pa_hex(TINT[(tint + 2) % 5]) : base;
        float y0 = hgt * (float)i, y1 = y0 + hgt;
        float x0 = x - wid * 0.5f, x1 = x + wid * 0.5f;
        if (x1 < 0.0f) quad(c, proj(x1, y0, z), proj(x1, y0, z + len), proj(x1, y1, z + len), proj(x1, y1, z), pa_shade(b2, -0.05f));
        if (x0 > 0.0f) quad(c, proj(x0, y0, z), proj(x0, y0, z + len), proj(x0, y1, z + len), proj(x0, y1, z), pa_shade(b2, -0.3f));
        quad(c, proj(x0, y1, z), proj(x1, y1, z), proj(x1, y1, z + len), proj(x0, y1, z + len), pa_shade(b2, 0.3f));
        quad(c, proj(x0, y0, z), proj(x1, y0, z), proj(x1, y1, z), proj(x0, y1, z), pa_shade(b2, -0.15f));
        /* corrugation */
        for (int r = 1; r < 6; r++) {
            float zz = z + len * (float)r / 6.0f;
            float xs = x1 < 0.0f ? x1 : x0;
            PA_Vec2 a = proj(xs, y0 + 0.08f, zz), bb = proj(xs, y1 - 0.08f, zz);
            pa_line(c, a.x, a.y, bb.x, bb.y, fmaxf(1.0f, ss(zz) * 0.05f), PA_RGBA(0, 0, 0, 40));
        }
    }
}

static void draw_lamp(PA_Canvas *c, float x, float z, float s) {
    float k = ss(z);
    PA_Vec2 b = proj(x, 0.0f, z);
    float h = 4.2f * s * k * VK;
    pa_shadow(c, b.x, b.y, k * 0.5f, k * 0.15f, 0.4f);
    pa_line(c, b.x, b.y, b.x, b.y - h, fmaxf(1.5f, k * 0.12f), PA_RGB(84, 90, 110));
    float dir = x < 0.0f ? 1.0f : -1.0f;
    pa_line(c, b.x, b.y - h, b.x + dir * k * 0.7f, b.y - h + k * 0.1f, fmaxf(1.5f, k * 0.1f), PA_RGB(84, 90, 110));
    pa_fill_ellipse(c, b.x + dir * k * 0.75f, b.y - h + k * 0.18f, k * 0.28f, k * 0.12f, PA_RGB(250, 240, 200));
}

static void draw_horizon(PA_Canvas *c, const Theme *th, int theme) {
    float hy = g_yh, w = g_w;
    PA_Rng r; pa_rng_seed(&r, 777u + (uint32_t)theme);
    if (theme == 0) {
        /* two ranges of snowy mountains */
        for (int layer = 0; layer < 2; layer++) {
            PA_Color col = pa_hex(layer ? th->hill2 : th->hill);
            float base = hy + (float)layer * g_h * 0.012f;
            float x = -40.0f;
            while (x < w + 40.0f) {
                float pw = pa_rng_range(&r, 70.0f, 150.0f) * g_u, ph = pa_rng_range(&r, 0.035f, 0.075f) * g_h * (layer ? 0.75f : 1.0f);
                PA_Vec2 m[3] = { { x, base + 2.0f }, { x + pw * 0.5f, base - ph }, { x + pw, base + 2.0f } };
                pa_fill_poly(c, m, 3, col);
                PA_Vec2 cap[3] = { { x + pw * 0.5f, base - ph }, { x + pw * 0.36f, base - ph * 0.62f }, { x + pw * 0.62f, base - ph * 0.66f } };
                pa_fill_poly(c, cap, 3, PA_RGB(250, 252, 255));
                x += pw * 0.62f;
            }
        }
    } else if (theme == 1) {
        /* mesas in the haze */
        for (int layer = 0; layer < 2; layer++) {
            PA_Color col = pa_hex(layer ? th->hill2 : th->hill);
            float x = pa_rng_range(&r, -60.0f, 0.0f);
            while (x < w + 40.0f) {
                float pw = pa_rng_range(&r, 60.0f, 160.0f) * g_u, ph = pa_rng_range(&r, 0.025f, 0.06f) * g_h;
                float base = hy + (float)layer * g_h * 0.01f + 2.0f;
                PA_Vec2 m[4] = { { x, base }, { x + pw * 0.15f, base - ph }, { x + pw * 0.85f, base - ph }, { x + pw, base } };
                pa_fill_poly(c, m, 4, col);
                pa_fill_rect(c, x + pw * 0.15f, base - ph, pw * 0.7f, ph * 0.12f, pa_shade(col, 0.15f));
                x += pw + pa_rng_range(&r, 20.0f, 120.0f) * g_u;
            }
        }
    } else {
        /* a hazy skyline with cranes */
        float x = -20.0f;
        while (x < w + 20.0f) {
            float bw = pa_rng_range(&r, 24.0f, 60.0f) * g_u, bh = pa_rng_range(&r, 0.02f, 0.08f) * g_h;
            PA_Color col = pa_mix(pa_hex(th->hill), pa_hex(th->hill2), pa_rng_next(&r));
            pa_fill_rect(c, x, hy - bh, bw, bh + 3.0f, col);
            for (float wy = hy - bh + 6.0f; wy < hy - 4.0f; wy += 8.0f * g_u)
                pa_fill_rect(c, x + 4.0f, wy, bw - 8.0f, 2.0f * g_u, PA_RGBA(255, 255, 255, 40));
            x += bw + pa_rng_range(&r, 0.0f, 10.0f);
        }
        for (int i = 0; i < 3; i++) {
            float cx = w * (0.15f + 0.35f * (float)i), ch = g_h * 0.09f;
            PA_Color cc = PA_RGB(214, 120, 60);
            pa_line(c, cx, hy, cx, hy - ch, 3.0f * g_u, cc);
            pa_line(c, cx - ch * 0.3f, hy - ch, cx + ch * 0.6f, hy - ch, 3.0f * g_u, cc);
            pa_line(c, cx + ch * 0.5f, hy - ch, cx + ch * 0.5f, hy - ch * 0.6f, 1.0f, cc);
        }
    }
}

static void bg_build(void) {
    PA_Canvas *c = &g_bg;
    const Theme *th = &THEMES[G.theme];
    float H = TRACK_HALF;
    /* sky down to the vanishing line, ground from there */
    PA_Paint sky = pa_linear(0, 0, 0, g_yh);
    pa_stop(&sky, 0.0f, pa_hex(th->sky0)); pa_stop(&sky, 1.0f, pa_hex(th->sky1));
    pa_fill_rect_paint(c, 0, 0, g_w, g_yh + 1.0f, &sky);
    PA_Paint gr = pa_linear(0, g_yh, 0, g_h);
    pa_stop(&gr, 0.0f, pa_hex(th->gfar)); pa_stop(&gr, 1.0f, pa_hex(th->gnear));
    pa_fill_rect_paint(c, 0, g_yh, g_w, g_h - g_yh, &gr);
    draw_horizon(c, th, G.theme);
    PA_Paint hz = pa_linear(0, g_yh - g_h * 0.02f, 0, g_yh + g_h * 0.07f);
    pa_stop(&hz, 0.0f, pa_alpha(pa_hex(th->haze), 0.0f)); pa_stop(&hz, 0.35f, pa_alpha(pa_hex(th->haze), 0.75f));
    pa_stop(&hz, 1.0f, pa_alpha(pa_hex(th->haze), 0.0f));
    pa_fill_rect_paint(c, 0, g_yh - g_h * 0.02f, g_w, g_h * 0.09f, &hz);

    /* ground grain: sparkles, sand ripples or yard seams */
    PA_Rng r; pa_rng_seed(&r, 9001u + (uint32_t)G.built_level);
    for (int i = 0; i < 700; i++) {
        float z = pa_rng_range(&r, -6.0f, 120.0f), x = pa_rng_range(&r, -34.0f, 34.0f);
        if (fabsf(x) < H + 0.6f) continue;
        PA_Vec2 p = proj(x, 0, z);
        float k = ss(z);
        if (G.theme == 1) pa_fill_ellipse(c, p.x, p.y, k * 0.9f, k * 0.08f, PA_RGBA(PA_R(pa_hex(th->speck)), PA_G(pa_hex(th->speck)), PA_B(pa_hex(th->speck)), 50));
        else if (G.theme == 0) pa_fill_ellipse(c, p.x, p.y, fmaxf(0.8f, k * 0.08f), fmaxf(0.5f, k * 0.04f), PA_RGBA(255, 255, 255, 170));
        else pa_fill_ellipse(c, p.x, p.y, k * 0.5f, k * 0.12f, PA_RGBA(40, 46, 60, 26));
    }
    if (G.theme == 2) {
        /* concrete slab seams */
        for (float z = -6.0f; z < 90.0f; z += 4.0f) {
            for (int sd = -1; sd <= 1; sd += 2) {
                PA_Vec2 a = proj((float)sd * (H + 0.6f), 0, z), b = proj((float)sd * 40.0f, 0, z);
                pa_line(c, a.x, a.y, b.x, b.y, fmaxf(1.0f, ss(z) * 0.05f), PA_RGBA(40, 46, 60, 40));
            }
        }
        for (float x = H + 4.0f; x < 40.0f; x += 4.0f) for (int sd = -1; sd <= 1; sd += 2) {
            PA_Vec2 a = proj((float)sd * x, 0, -6.0f), b = proj((float)sd * x, 0, 90.0f);
            pa_line(c, a.x, a.y, b.x, b.y, 1.0f, PA_RGBA(40, 46, 60, 40));
        }
    }

    /* props, far to near */
    for (int i = 0; i < g_ndecor; i++) {
        Decor *d = &g_decor[i];
        if (G.theme == 0) { if (d->kind == 0) draw_pine(c, d->x, d->z, d->s); else draw_rock(c, d->x, d->z, d->s, PA_RGB(206, 210, 224), PA_RGB(150, 154, 174)); }
        else if (G.theme == 1) { if (d->kind == 0) draw_cactus(c, d->x, d->z, d->s); else draw_rock(c, d->x, d->z, d->s * 1.4f, PA_RGB(232, 160, 104), PA_RGB(184, 108, 70)); }
        else { if (d->kind == 0) draw_container(c, d->x, d->z, d->s, (int)(d->s * 97.0f)); else draw_lamp(c, d->x, d->z, d->s); }
    }

    /* the road slab */
    float zn = -12.0f, zf = TRACK_LEN + 6.0f;
    quad(c, proj(-H - 1.4f, 0, zn), proj(-H, 0, zn), proj(-H, 0, zf), proj(-H - 1.4f, 0, zf), PA_RGBA(30, 30, 60, 46));
    quad(c, proj(H, 0, zn), proj(H + 1.4f, 0, zn), proj(H + 1.4f, 0, zf), proj(H, 0, zf), PA_RGBA(30, 30, 60, 46));
    PA_Vec2 a = proj(-H, 0, zn), b = proj(H, 0, zn), cc = proj(H, 0, zf), d = proj(-H, 0, zf);
    PA_Vec2 poly[4] = { a, b, cc, d };
    PA_Paint rp = pa_linear(0, d.y, 0, a.y);
    pa_stop(&rp, 0.0f, pa_hex(th->rfar)); pa_stop(&rp, 1.0f, pa_hex(th->rnear));
    pa_fill_poly_paint(c, poly, 4, &rp);
    /* asphalt grain and blotches, per pixel, in road space so they recede */
    int y0 = (int)fmaxf(0.0f, d.y), y1 = (int)fminf(g_h, g_h);
    for (int y = y0; y < y1; y++) {
        float pz = ((float)y + 0.5f - g_yh) / (g_y0 - g_yh);
        if (pz <= 0.01f) continue;
        float z = CAM_D / pz - CAM_D;
        float k = ss(z);
        int xa = (int)fmaxf(0.0f, sx(-H, z)), xb = (int)fminf(g_w, sx(H, z));
        uint32_t *row = c->px + (size_t)y * (size_t)c->w;
        for (int x = xa; x < xb; x++) {
            float wx = ((float)x - g_w * 0.5f) / k;
            float blot = vnoise(wx * 0.7f + 3.0f, z * 0.7f) * 0.6f + vnoise(wx * 2.1f, z * 2.1f + 9.0f) * 0.4f;
            int n = (int)((hashf(x, y) - 0.5f) * 14.0f + (blot - 0.5f) * 26.0f);
            uint32_t v = row[x];
            int rr = (int)((v >> 16) & 255) + n, gg = (int)((v >> 8) & 255) + n, bb = (int)(v & 255) + n;
            rr = rr < 0 ? 0 : (rr > 255 ? 255 : rr); gg = gg < 0 ? 0 : (gg > 255 ? 255 : gg); bb = bb < 0 ? 0 : (bb > 255 ? 255 : bb);
            row[x] = ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
        }
    }
    /* patches and cracks */
    for (int i = 0; i < 9; i++) {
        float z = pa_rng_range(&r, -2.0f, TRACK_LEN - 4.0f), x = pa_rng_range(&r, -H + 1.0f, H - 2.5f);
        float l = pa_rng_range(&r, 1.0f, 3.0f), wd = pa_rng_range(&r, 0.8f, 1.8f);
        quad(c, proj(x, 0, z), proj(x + wd, 0, z), proj(x + wd, 0, z + l), proj(x, 0, z + l), PA_RGBA(40, 40, 60, 22));
    }
    for (int i = 0; i < 16; i++) {
        float z = pa_rng_range(&r, -3.0f, TRACK_LEN - 2.0f), x = pa_rng_range(&r, -H + 0.5f, H - 0.5f);
        PA_Vec2 pts[6]; int n = 0;
        for (int j = 0; j < 5; j++) {
            pts[n++] = proj(x, 0, z);
            x += pa_rng_range(&r, -0.5f, 0.5f); z += pa_rng_range(&r, 0.2f, 0.7f);
            if (fabsf(x) > H - 0.3f) break;
        }
        if (n > 1) pa_stroke_poly(c, pts, n, 0, fmaxf(1.0f, ss(z) * 0.035f), PA_RGBA(50, 50, 70, 70));
    }
    /* paint: centre dashes, lane edges, forward chevrons */
    for (float z = -8.0f; z < TRACK_LEN - 2.0f; z += 3.0f)
        quad(c, proj(-0.09f, 0, z), proj(0.09f, 0, z), proj(0.09f, 0, z + 1.4f), proj(-0.09f, 0, z + 1.4f), PA_RGBA(255, 255, 255, 190));
    for (int sd = -1; sd <= 1; sd += 2) {
        float x = (float)sd * (H - 0.5f);
        quad(c, proj(x - 0.07f, 0, zn), proj(x + 0.07f, 0, zn), proj(x + 0.07f, 0, zf - 6.0f), proj(x - 0.07f, 0, zf - 6.0f), PA_RGBA(255, 255, 255, 150));
    }
    for (int i = 0; i < 2; i++) for (int sd = -1; sd <= 1; sd += 2) {
        float z = 26.5f + 4.0f * (float)i, x = (float)sd * 2.5f;
        PA_Vec2 ch[6] = { proj(x - 0.9f, 0, z), proj(x, 0, z + 0.9f), proj(x + 0.9f, 0, z), proj(x + 0.9f, 0, z + 0.5f),
                          proj(x, 0, z + 1.4f), proj(x - 0.9f, 0, z + 0.5f) };
        pa_fill_poly(c, ch, 6, PA_RGBA(255, 255, 255, 120));
    }
    /* curbs, and the soft occlusion they throw on the road */
    for (int sd = -1; sd <= 1; sd += 2) {
        float xi = (float)sd * H, xo = (float)sd * (H - 0.45f);
        PA_Vec2 ao[4] = { proj(xi, 0, zn), proj(xo, 0, zn), proj(xo, 0, zf), proj(xi, 0, zf) };
        PA_Paint ap = pa_linear(proj(xi, 0, 4.0f).x, 0, proj(xo, 0, 4.0f).x, 0);
        pa_stop(&ap, 0.0f, PA_RGBA(20, 20, 50, 60)); pa_stop(&ap, 1.0f, PA_RGBA(20, 20, 50, 0));
        pa_fill_poly_paint(c, ao, 4, &ap);
    }
    box(c, -H - 0.45f, -H + 0.05f, zn, zf, 0.0f, 0.38f, pa_hex(th->curb), pa_shade(pa_hex(th->curb), -0.12f), pa_hex(th->curb_side));
    box(c, H - 0.05f, H + 0.45f, zn, zf, 0.0f, 0.38f, pa_hex(th->curb), pa_shade(pa_hex(th->curb), -0.12f), pa_hex(th->curb_side));
    /* defence stripe and cannon rail */
    float z0 = DEFENCE_Z - 0.22f, z1 = DEFENCE_Z + 0.22f;
    quad(c, proj(-H, 0, z0), proj(H, 0, z0), proj(H, 0, z1), proj(-H, 0, z1), PA_RGB(250, 204, 36));
    for (float x = -H; x < H - 0.1f; x += 0.8f) {
        float xb = fminf(x + 0.4f, H);
        quad(c, proj(x, 0, z0), proj(xb, 0, z0), proj(fminf(xb + 0.25f, H), 0, z1), proj(fminf(x + 0.25f, H), 0, z1), PA_RGB(38, 38, 50));
    }
    quad(c, proj(-H + 0.6f, 0, CANNON_Z - 0.05f), proj(H - 0.6f, 0, CANNON_Z - 0.05f), proj(H - 0.6f, 0, CANNON_Z + 0.05f), proj(-H + 0.6f, 0, CANNON_Z + 0.05f), PA_RGBA(255, 255, 255, 230));
    /* the castle's footprint shadow */
    pa_shadow(c, sx(0, TRACK_LEN + 1.5f), sy(TRACK_LEN + 0.6f), ss(TRACK_LEN) * 8.0f, ss(TRACK_LEN) * 1.6f, 0.6f);
    pa_vignette(c, 0.18f);
}

static void bg_blit(PA_Canvas *c, int dx, int dy) {
    int key = G.built_level * 4 + G.theme;
    if (!g_bg.px || g_bg_key != key || g_bg_w != c->w || g_bg_h != c->h) {
        if (!g_bg.px) pa_canvas_init(&g_bg, c->w, c->h); else pa_canvas_resize(&g_bg, c->w, c->h);
        float sx_ = g_shx, sy_ = g_shy, zs = g_zoom;
        g_shx = g_shy = 0.0f; g_zoom = 1.0f;
        bg_build();
        g_shx = sx_; g_shy = sy_; g_zoom = zs;
        g_bg_key = key; g_bg_w = c->w; g_bg_h = c->h;
    }
    if (g_zoom != 1.0f) {
        /* push-in: a nearest-neighbour scale of the cached scenery about the pivot */
        static int xs[4096];
        int w = c->w < 4096 ? c->w : 4096;
        for (int x = 0; x < w; x++) {
            int v = (int)(g_zpx + ((float)(x - dx) - g_zpx) / g_zoom);
            xs[x] = v < 0 ? 0 : (v >= c->w ? c->w - 1 : v);
        }
        for (int y = 0; y < c->h; y++) {
            int v = (int)(g_zpy + ((float)(y - dy) - g_zpy) / g_zoom);
            v = v < 0 ? 0 : (v >= c->h ? c->h - 1 : v);
            uint32_t *dst = c->px + (size_t)y * (size_t)c->w;
            const uint32_t *src = g_bg.px + (size_t)v * (size_t)c->w;
            for (int x = 0; x < w; x++) dst[x] = src[xs[x]];
        }
        return;
    }
    for (int y = 0; y < c->h; y++) {
        int syy = y - dy; syy = syy < 0 ? 0 : (syy >= c->h ? c->h - 1 : syy);
        uint32_t *dst = c->px + (size_t)y * (size_t)c->w;
        const uint32_t *src = g_bg.px + (size_t)syy * (size_t)c->w;
        if (dx >= 0) {
            memcpy(dst + dx, src, (size_t)(c->w - dx) * 4);
            for (int x = 0; x < dx; x++) dst[x] = src[0];
        } else {
            memcpy(dst, src - dx, (size_t)(c->w + dx) * 4);
            for (int x = c->w + dx; x < c->w; x++) dst[x] = src[c->w - 1];
        }
    }
}

/* ---- the enemy castle: lit from the upper left, sitting in its own shadow ---- */
static void lbox(PA_Canvas *c, float x0, float x1, float z0, float z1, float y0, float y1, PA_Color base) {
    box(c, x0, x1, z0, z1, y0, y1, pa_shade(base, 0.28f), base, pa_shade(base, -0.32f));
    /* occlusion where it meets whatever it stands on */
    float ao = fminf(0.6f, (y1 - y0) * 0.25f);
    PA_Vec2 a = proj(x0, y0, z0), b = proj(x1, y0 + ao, z0);
    PA_Paint p = pa_linear(0, b.y, 0, a.y);
    pa_stop(&p, 0.0f, PA_RGBA(20, 10, 30, 0)); pa_stop(&p, 1.0f, PA_RGBA(20, 10, 30, 90));
    pa_fill_rect_paint(c, a.x, b.y, b.x - a.x, a.y - b.y, &p);
    /* bevel highlight along the top front edge */
    PA_Vec2 e0 = proj(x0, y1, z0), e1 = proj(x1, y1, z0);
    pa_line(c, e0.x, e0.y, e1.x, e1.y, fmaxf(1.0f, ss(z0) * 0.06f), PA_RGBA(255, 255, 255, 90));
}

static void draw_castle_look(PA_Canvas *c, float fl, float sh, float drop) {
    float L = TRACK_LEN;
    float hk = 1.0f - 0.85f * pa_smooth(drop);
    PA_Color red = pa_mix(PA_RGB(222, 58, 60), PA_RGB(255, 255, 255), fl);
    PA_Color wht = pa_mix(PA_RGB(242, 236, 232), PA_RGB(255, 255, 255), fl);
    PA_Color roof = PA_RGB(70, 58, 108);

    /* keep */
    lbox(c, -3.0f + sh, 3.0f + sh, L + 1.7f, L + 4.6f, 0.0f, 6.4f * hk, red);
    lbox(c, -3.2f + sh, 3.2f + sh, L + 1.6f, L + 4.7f, 6.4f * hk, 7.0f * hk, wht);
    if (drop < 0.3f) {
        PA_Vec2 r0 = proj(-3.0f + sh, 7.0f, L + 1.8f), r1 = proj(3.0f + sh, 7.0f, L + 1.8f), r2 = proj(sh, 9.6f, L + 3.2f);
        PA_Vec2 tri[3] = { r0, r1, r2 };
        pa_fill_poly(c, tri, 3, roof);
        PA_Vec2 tl[3] = { r0, r2, proj(sh, 7.0f, L + 1.8f) };
        pa_fill_poly(c, tl, 3, PA_RGB(104, 90, 150));
        PA_Vec2 p0 = proj(sh, 9.5f, L + 3.2f), p1 = proj(sh, 11.8f, L + 3.2f);
        pa_line(c, p0.x, p0.y, p1.x, p1.y, fmaxf(1.5f, ss(L) * 0.1f), PA_RGB(70, 60, 80));
        float wv = sinf(G.clock * 6.0f) * 0.15f;
        PA_Vec2 f3[3] = { p1, proj(1.7f + sh, 11.3f + wv, L + 3.2f), proj(sh, 10.7f, L + 3.2f) };
        pa_fill_poly(c, f3, 3, PA_RGB(240, 50, 60));
        /* lit windows */
        for (int i = -1; i <= 1; i++) {
            PA_Vec2 w0 = proj((float)i * 1.6f - 0.35f + sh, 4.6f * hk, L + 1.7f), w1 = proj((float)i * 1.6f + 0.35f + sh, 5.7f * hk, L + 1.7f);
            pa_round_rect(c, w0.x, w1.y, w1.x - w0.x, w0.y - w1.y, (w1.x - w0.x) * 0.5f, PA_RGB(255, 214, 120));
        }
    }
    /* front wall with battlements */
    lbox(c, -5.4f + sh, 5.4f + sh, L, L + 1.7f, 0.0f, 3.4f * hk, red);
    if (drop < 0.5f) for (int i = 0; i < 9; i++) {
        float x = -5.4f + (float)i * 1.3f + sh;
        lbox(c, x, x + 0.7f, L, L + 0.7f, 3.4f * hk, 4.1f * hk, wht);
    }
    quad(c, proj(-5.4f + sh, 2.5f * hk, L), proj(5.4f + sh, 2.5f * hk, L), proj(5.4f + sh, 2.85f * hk, L), proj(-5.4f + sh, 2.85f * hk, L), wht);
    {
        PA_Vec2 d0 = proj(-2.3f + sh, 0.0f, L), d1 = proj(2.3f + sh, 2.1f * hk, L);
        pa_round_rect(c, d0.x, d1.y, d1.x - d0.x, d0.y - d1.y + 1.0f, (d1.x - d0.x) * 0.4f, PA_RGB(56, 18, 32));
        pa_round_rect(c, d0.x + 3.0f, d1.y + 3.0f, d1.x - d0.x - 6.0f, (d0.y - d1.y) * 0.35f, (d1.x - d0.x) * 0.3f, PA_RGB(96, 30, 50));
    }
    /* towers */
    for (int sd = -1; sd <= 1; sd += 2) {
        float x0 = sd < 0 ? -6.9f : 4.7f, x1 = x0 + 2.2f;
        lbox(c, x0 + sh, x1 + sh, L - 0.5f, L + 1.9f, 0.0f, 5.4f * hk, red);
        lbox(c, x0 - 0.2f + sh, x1 + 0.2f + sh, L - 0.7f, L + 2.1f, 5.4f * hk, 6.0f * hk, wht);
        if (drop < 0.3f) {
            PA_Vec2 a = proj(x0 - 0.1f + sh, 6.0f, L - 0.6f), b = proj(x1 + 0.1f + sh, 6.0f, L - 0.6f), t = proj((x0 + x1) * 0.5f + sh, 8.6f, L + 0.7f);
            PA_Vec2 tri[3] = { a, b, t };
            pa_fill_poly(c, tri, 3, roof);
            PA_Vec2 hl[3] = { a, t, proj((x0 + x1) * 0.5f + sh, 6.0f, L - 0.6f) };
            pa_fill_poly(c, hl, 3, PA_RGB(104, 90, 150));
        }
        PA_Vec2 w0 = proj(x0 + 0.75f + sh, 3.3f * hk, L - 0.5f), w1 = proj(x1 - 0.75f + sh, 4.4f * hk, L - 0.5f);
        pa_round_rect(c, w0.x, w1.y, w1.x - w0.x, w0.y - w1.y, (w1.x - w0.x) * 0.5f, drop < 0.3f ? PA_RGB(255, 214, 120) : PA_RGB(70, 24, 40));
    }
    if (drop > 0.0f) {
        for (int i = 0; i < 16; i++) {
            float rx = -6.6f + (float)i * 0.82f, rz = L + 0.2f + (float)(i % 3) * 0.7f;
            lbox(c, rx, rx + 0.7f, rz, rz + 0.6f, 0.0f, 0.5f + 0.4f * (float)(i % 2), i % 4 == 0 ? wht : red);
        }
    }
}

/* ---- static art caches: castle and gate panels are painted once per level ---- */
static Spr g_castle_spr[2];
static Spr g_gate_spr[MAX_GATES][2];
static Spr g_lad_spr[MAX_LADDER];
static float g_gate_xb[MAX_GATES];
static int g_cache_key = -1;
static float g_cache_k = -1.0f, g_cache_w = -1.0f, g_cache_h = -1.0f;

static void cap_castle(PA_Canvas *c, const void *arg) { draw_castle_look(c, *(const float *)arg, 0.0f, 0.0f); }

static void draw_castle(PA_Canvas *c) {
    float fl = G.base_flash > 0.35f ? 0.22f : 0.0f;
    float sh = G.base_shake * 0.15f * sinf(G.clock * 60.0f);
    if (G.collapse > 0.0f || !g_castle_spr[0].a) { draw_castle_look(c, G.base_flash * 0.22f, sh, G.collapse); return; }
    if (g_zoom != 1.0f) { PA_Vec2 a = proj1(0.0f, 0.0f, TRACK_LEN); blit_zoomed(c, &g_castle_spr[fl > 0.0f ? 1 : 0], a.x, a.y); return; }
    PA_Vec2 a = proj(0.0f, 0.0f, TRACK_LEN);
    blit(c, &g_castle_spr[fl > 0.0f ? 1 : 0], (int)(a.x + sh * ss(TRACK_LEN)), (int)a.y);
}

static void draw_castle_hp(PA_Canvas *c) {
    if (G.state == S_WIN || G.state == S_RESULT) return;
    float L = TRACK_LEN;
    PA_Vec2 p = proj(0.0f, 5.3f, L + 1.7f);
    char buf[16]; snprintf(buf, sizeof(buf), "%d", (int)ceilf(G.base_hp));
    float size = fminf(30.0f * g_u, ss(L) * 1.6f) * (1.0f + G.base_flash * 0.12f);
    float bw = fminf(130.0f * g_u, ss(L) * 5.4f), bh = 11.0f * g_u;
    float by = p.y + size * 0.62f;
    pa_round_rect(c, p.x - bw * 0.5f - 3.0f, by - 3.0f, bw + 6.0f, bh + 6.0f, (bh + 6.0f) * 0.5f, C_INK);
    pa_round_rect(c, p.x - bw * 0.5f, by, bw, bh, bh * 0.5f, PA_RGB(255, 255, 255));
    float f = pa_clamp01(G.base_hp / fmaxf(1.0f, G.base_max));
    if (f > 0.0f) pa_round_rect(c, p.x - bw * 0.5f, by, fmaxf(bh, bw * f), bh, bh * 0.5f, PA_RGB(236, 50, 56));
    txt(c, buf, p.x, p.y, size, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.6f);
}

/* ---- gates: chunky bevelled frames round a tinted glass panel ---- */
enum { PANEL_BLUE, PANEL_MAGENTA, PANEL_RED };

static void draw_panel(PA_Canvas *c, float x0, float x1, float z, float ht, int kind, const char *label,
                       float pulse, int dim, float label_scale) {
    PA_Color glass, glassB, frame;
    if (kind == PANEL_MAGENTA) { glass = pa_hex(0xE58BF0); glassB = pa_hex(0xD23CE0); frame = pa_hex(0xA21FB8); }
    else if (kind == PANEL_RED) { glass = pa_hex(0xFF9AA6); glassB = pa_hex(0xF0354E); frame = pa_hex(0xC21C36); }
    else { glass = pa_hex(0xA9D6FF); glassB = pa_hex(0x6FB8FF); frame = pa_hex(0x1E5BD8); }
    float k = ss(z);
    float pw = 0.30f, depth = 0.3f;
    /* contact occlusion and a coloured glow on the road */
    pa_shadow(c, sx((x0 + x1) * 0.5f, z), sy(z), (x1 - x0) * 0.62f * k, k * 0.45f, 0.5f);
    quad(c, proj(x0, 0, z - 0.25f), proj(x1, 0, z - 0.25f), proj(x1, 0, z + 0.25f), proj(x0, 0, z + 0.25f), pa_alpha(glassB, 0.35f));
    /* glass */
    PA_Vec2 g0 = proj(x0, 0.12f, z), g1 = proj(x1, ht, z);
    float ga = dim ? 0.28f : 0.62f;
    PA_Paint gp = pa_linear(0, g1.y, 0, g0.y);
    pa_stop(&gp, 0.0f, pa_alpha(pa_mix(glass, PA_RGB(255, 255, 255), pulse * 0.5f), ga));
    pa_stop(&gp, 1.0f, pa_alpha(pa_mix(glassB, PA_RGB(255, 255, 255), pulse * 0.4f), ga * 0.8f));
    pa_fill_rect_paint(c, g0.x, g1.y, g1.x - g0.x, g0.y - g1.y, &gp);
    /* diagonal sheen */
    float gw = g1.x - g0.x, gh = g0.y - g1.y;
    PA_Vec2 sh4[4] = { { g0.x + gw * 0.08f, g0.y }, { g0.x + gw * 0.22f, g0.y }, { g0.x + gw * 0.42f, g1.y }, { g0.x + gw * 0.28f, g1.y } };
    pa_fill_poly(c, sh4, 4, PA_RGBA(255, 255, 255, dim ? 16 : 46));
    (void)gh;
    /* frame: posts, sill and a heavy top beam, each a lit box */
    PA_Color fT = pa_shade(frame, 0.45f), fS = pa_shade(frame, -0.4f);
    box(c, x0 - pw, x0 + pw * 0.2f, z - depth, z + depth, 0.0f, ht + 0.1f, fT, frame, fS);
    box(c, x1 - pw * 0.2f, x1 + pw, z - depth, z + depth, 0.0f, ht + 0.1f, fT, frame, fS);
    box(c, x0 - pw, x1 + pw, z - depth, z + depth, 0.0f, 0.14f, fT, fS, fS);
    box(c, x0 - pw - 0.05f, x1 + pw + 0.05f, z - depth - 0.05f, z + depth, ht - 0.05f, ht + 0.42f, fT, frame, fS);
    /* bevel lights on the front faces */
    PA_Vec2 b0 = proj(x0 - pw, ht + 0.40f, z - depth - 0.05f), b1 = proj(x1 + pw, ht + 0.40f, z - depth - 0.05f);
    pa_line(c, b0.x, b0.y, b1.x, b1.y, fmaxf(1.5f, k * 0.07f), pa_alpha(PA_RGB(255, 255, 255), 0.55f));
    PA_Vec2 v0 = proj(x0 - pw + 0.04f, 0.15f, z - depth), v1 = proj(x0 - pw + 0.04f, ht, z - depth);
    pa_line(c, v0.x, v0.y, v1.x, v1.y, fmaxf(1.0f, k * 0.05f), pa_alpha(PA_RGB(255, 255, 255), 0.45f));
    PA_Vec2 e0 = proj(x0 - pw, 0.0f, z - depth), e1 = proj(x1 + pw, 0.0f, z - depth);
    pa_line(c, e0.x, e0.y, e1.x, e1.y, fmaxf(1.0f, k * 0.05f), PA_RGBA(0, 0, 0, 70));
    if (pulse > 0.05f) {
        PA_Vec2 q0 = proj(x0, 0.0f, z), q1 = proj(x1, ht, z);
        pa_stroke_rect(c, q0.x, q1.y, q1.x - q0.x, q0.y - q1.y, fmaxf(2.0f, k * 0.12f * pulse), PA_RGBA(255, 255, 255, (int)(200.0f * pulse)));
    }
    /* numerals */
    float size = k * 1.05f * label_scale * (1.0f + pulse * 0.14f);
    PA_TextStyle ts = pa_text_style(PA_FACE_DISPLAY, dim ? PA_RGB(225, 228, 240) : PA_RGB(255, 255, 255));
    ts.fill_bottom = dim ? PA_RGB(200, 204, 220) : pa_mix(glass, PA_RGB(255, 255, 255), 0.55f);
    ts.outline = fmaxf(1.5f, size * 0.10f); ts.outline_col = pa_shade(frame, -0.55f);
    ts.shadow_dy = fmaxf(1.0f, size * 0.08f); ts.shadow_col = pa_alpha(pa_shade(frame, -0.6f), 0.9f);
    ts.align = PA_ALIGN_CENTER;
    PA_Vec2 m = proj((x0 + x1) * 0.5f, ht * 0.5f, z);
    pa_text_ex(c, label, m.x, m.y - size * 0.5f, size, &ts);
}

static void gate_label(const Gate *g, char *buf, size_t n) {
    if (g->type == G_MUL) snprintf(buf, n, "x%d", g->val);
    else if (g->type == G_ADD) snprintf(buf, n, "+%d", g->val);
    else snprintf(buf, n, "-%d", g->val);
}

#define GATE_HT 2.2f
#define LAD_HT  1.1f
static void gate_live(PA_Canvas *c, const Gate *g, float pulse, int dim) {
    char buf[12]; gate_label(g, buf, sizeof(buf));
    int kind = g->type == G_SUB ? PANEL_RED : (g->type == G_MUL && g->val >= 3 ? PANEL_MAGENTA : PANEL_BLUE);
    draw_panel(c, g->x - g->w * 0.5f, g->x + g->w * 0.5f, g->z, GATE_HT, kind, buf, pulse, dim, 1.0f);
}
typedef struct { const Gate *g; int dim; } GateCap;
static void cap_gate(PA_Canvas *c, const void *arg) { const GateCap *gc = (const GateCap *)arg; gate_live(c, gc->g, 0.0f, gc->dim); }
static void cap_lad(PA_Canvas *c, const void *arg) {
    const Ladder *l = (const Ladder *)arg;
    draw_panel(c, l->x - l->w * 0.5f, l->x + l->w * 0.5f, l->z, LAD_HT, PANEL_BLUE, "+1", 0.0f, 0, 0.62f);
}

/* the pulse when a crowd goes through: a white rim and a lift of the glass */
static void panel_pulse(PA_Canvas *c, float x0, float x1, float z, float ht, float pulse) {
    if (pulse <= 0.05f) return;
    PA_Vec2 q0 = proj(x0, 0.0f, z), q1 = proj(x1, ht, z);
    float w = q1.x - q0.x, h = q0.y - q1.y, t = fmaxf(2.0f, ss(z) * 0.12f * pulse);
    PA_Color wc = PA_RGB(255, 255, 255);
    frect(c, q0.x + t, q1.y + t, w - 2.0f * t, h - 2.0f * t, wc, 0.35f * pulse);
    frect(c, q0.x, q1.y, w, t, wc, 0.85f * pulse);
    frect(c, q0.x, q0.y - t, w, t, wc, 0.85f * pulse);
    frect(c, q0.x, q1.y + t, t, h - 2.0f * t, wc, 0.85f * pulse);
    frect(c, q1.x - t, q1.y + t, t, h - 2.0f * t, wc, 0.85f * pulse);
}

static void panel_bounds(float x0, float x1, float z, float ht, float *bx0, float *by0, float *bx1, float *by1) {
    float k = ss(z);
    *bx0 = sx(x0 - 0.7f, z) - 6.0f; *bx1 = sx(x1 + 0.7f, z) + 6.0f;
    *by0 = sy(z) - (ht + 0.9f) * k - 6.0f; *by1 = sy(z) + 0.8f * k + 6.0f;
}

static void caches_refresh(void) {
    int key = G.built_level;
    if (key == g_cache_key && g_k == g_cache_k && g_w == g_cache_w && g_h == g_cache_h) return;
    g_cache_key = key; g_cache_k = g_k; g_cache_w = g_w; g_cache_h = g_h;
    float zs = g_zoom; g_zoom = 1.0f;
    float L = TRACK_LEN, kl = ss(L);
    PA_Vec2 a = proj(0.0f, 0.0f, L);
    for (int v = 0; v < 2; v++) {
        float fl = v ? 0.22f : 0.0f;
        capture(&g_castle_spr[v], sx(-7.6f, L) - 4.0f, a.y - 13.0f * kl, sx(7.6f, L) + 4.0f, a.y + 2.0f * kl, a.x, a.y, cap_castle, &fl);
    }
    for (int i = 0; i < g_ngate; i++) {
        const Gate *g = &g_gate[i];
        float bx0, by0, bx1, by1;
        panel_bounds(g->x - g->w * 0.5f, g->x + g->w * 0.5f, g->z, GATE_HT, &bx0, &by0, &bx1, &by1);
        g_gate_xb[i] = g->x;
        for (int v = 0; v < 2; v++) {
            if (v && g->type == G_MUL) { free(g_gate_spr[i][1].a); free(g_gate_spr[i][1].c); g_gate_spr[i][1].a = NULL; g_gate_spr[i][1].c = NULL; continue; }
            GateCap gc = { g, v };
            capture(&g_gate_spr[i][v], bx0, by0, bx1, by1, sx(g->x, g->z), sy(g->z), cap_gate, &gc);
        }
    }
    for (int i = 0; i < g_nlad; i++) {
        const Ladder *l = &g_lad[i];
        float bx0, by0, bx1, by1;
        panel_bounds(l->x - l->w * 0.5f, l->x + l->w * 0.5f, l->z, LAD_HT, &bx0, &by0, &bx1, &by1);
        capture(&g_lad_spr[i], bx0, by0, bx1, by1, sx(l->x, l->z), sy(l->z), cap_lad, l);
    }
    g_zoom = zs;
}

static void draw_gate(PA_Canvas *c, const Gate *g) {
    int gi = (int)(g - g_gate), dim = g->cool > 0.0f;
    const Spr *sp = &g_gate_spr[gi][dim];
    if (!sp->a || (g->x != g_gate_xb[gi] && g->amp == 0.0f)) { gate_live(c, g, g->pulse, dim); return; }
    if (g_zoom != 1.0f) { PA_Vec2 a = proj1(g->x, 0.0f, g->z); blit_zoomed(c, sp, a.x, a.y); return; }
    blit(c, sp, (int)sx(g->x, g->z), (int)sy(g->z));
    panel_pulse(c, g->x - g->w * 0.5f, g->x + g->w * 0.5f, g->z, GATE_HT, g->pulse);
}

static void draw_ladder(PA_Canvas *c, const Ladder *l) {
    const Spr *sp = &g_lad_spr[(int)(l - g_lad)];
    if (!sp->a) { cap_lad(c, l); panel_pulse(c, l->x - l->w * 0.5f, l->x + l->w * 0.5f, l->z, LAD_HT, l->pulse); return; }
    if (g_zoom != 1.0f) { PA_Vec2 a = proj1(l->x, 0.0f, l->z); blit_zoomed(c, sp, a.x, a.y); return; }
    blit(c, sp, (int)sx(l->x, l->z), (int)sy(l->z));
    panel_pulse(c, l->x - l->w * 0.5f, l->x + l->w * 0.5f, l->z, LAD_HT, l->pulse);
}

/* ---- brutes: enemy giants (yellow, red bands, facing us) and our champion ---- */
static void draw_brute(PA_Canvas *c, const Brute *b, int enemy) {
    float k = ss(b->z) * b->size;
    PA_Vec2 f = proj(b->x, 0.0f, b->z);
    float u = k * VK;
    float sw = sinf(b->ph);
    PA_Color body = enemy ? PA_RGB(255, 206, 36) : PA_RGB(64, 160, 255);
    PA_Color bodyL = enemy ? PA_RGB(255, 240, 150) : PA_RGB(170, 224, 255);
    PA_Color bodyD = enemy ? PA_RGB(214, 136, 16) : PA_RGB(24, 86, 210);
    PA_Color rim = enemy ? PA_RGB(255, 250, 220) : PA_RGB(214, 242, 255);
    PA_Color band = enemy ? PA_RGB(228, 36, 40) : PA_RGB(255, 206, 40);
    if (b->flash > 0.0f) {
        float t = b->flash * 0.45f;
        body = pa_mix(body, PA_RGB(255, 255, 255), t); bodyL = pa_mix(bodyL, PA_RGB(255, 255, 255), t);
        bodyD = pa_mix(bodyD, PA_RGB(255, 255, 255), t * 0.8f); band = pa_mix(band, PA_RGB(255, 255, 255), t * 0.6f);
    }
    /* idle breathing on top of the stride */
    float bob = fabsf(sw) * u * 0.06f + sinf(G.clock * 2.4f + b->x) * u * 0.03f;
    pa_shadow(c, f.x + k * 0.15f, f.y, k * 1.25f, k * 0.36f, 0.7f);
    pa_fill_ellipse(c, f.x, f.y - k * 0.02f, k * 0.8f, k * 0.18f, PA_RGBA(0, 0, 0, 70));
    /* legs */
    pa_round_rect(c, f.x - k * 0.42f, f.y - u * 0.55f + sw * u * 0.05f, k * 0.32f, u * 0.55f, k * 0.14f, bodyD);
    pa_round_rect(c, f.x + k * 0.10f, f.y - u * 0.55f - sw * u * 0.05f, k * 0.32f, u * 0.55f, k * 0.14f, bodyD);
    float ty = f.y - u * 0.45f - bob;
    /* arms behind the torso */
    for (int s = -1; s <= 1; s += 2) {
        float ax = f.x + (float)s * k * 0.78f, ay = ty - u * 1.15f + (float)s * sw * u * 0.08f;
        pa_round_rect(c, ax - k * 0.25f, ay - k * 0.02f, k * 0.54f, u * 1.05f, k * 0.26f, rim);
        pa_round_rect(c, ax - k * 0.29f, ay, k * 0.54f, u * 1.05f, k * 0.26f, s < 0 ? body : pa_mix(body, bodyD, 0.55f));
        pa_round_rect(c, ax - k * 0.25f, ay + k * 0.05f, k * 0.2f, u * 0.9f, k * 0.1f, s < 0 ? bodyL : body);
        pa_round_rect(c, ax - k * 0.31f, ay + u * 0.62f, k * 0.58f, u * 0.17f, k * 0.06f, band);
        pa_fill_circle(c, ax - k * 0.02f, ay + u * 1.0f, k * 0.27f, s < 0 ? body : pa_mix(body, bodyD, 0.4f));
    }
    /* torso with a rim on the right and a glossy key light */
    pa_round_rect(c, f.x - k * 0.58f, ty - u * 1.45f, k * 1.24f, u * 1.32f, k * 0.42f, rim);
    pa_round_rect(c, f.x - k * 0.64f, ty - u * 1.42f, k * 1.24f, u * 1.32f, k * 0.42f, bodyD);
    pa_round_rect(c, f.x - k * 0.64f, ty - u * 1.42f, k * 1.08f, u * 1.22f, k * 0.40f, body);
    pa_fill_ellipse(c, f.x - k * 0.22f, ty - u * 1.0f, k * 0.36f, u * 0.38f, pa_mix(body, bodyL, 0.55f));
    pa_fill_circle(c, f.x - k * 0.58f, ty - u * 1.18f, k * 0.3f, body);
    pa_fill_circle(c, f.x + k * 0.52f, ty - u * 1.18f, k * 0.3f, pa_mix(body, bodyD, 0.5f));
    pa_fill_ellipse(c, f.x - k * 0.34f, ty - u * 1.22f, k * 0.16f, k * 0.08f, PA_RGBA(255, 255, 255, 150));
    if (enemy) {
        PA_Vec2 st[10];
        float cx = f.x, cy = ty - u * 0.72f, R = k * 0.32f;
        for (int i = 0; i < 10; i++) {
            float a = -PA_PI * 0.5f + (float)i * PA_PI / 5.0f, r = (i & 1) ? R * 0.45f : R;
            st[i].x = cx + cosf(a) * r; st[i].y = cy + sinf(a) * r * 0.9f;
        }
        pa_fill_poly(c, st, 10, band);
        float hx = f.x, hy = ty - u * 1.48f;
        pa_fill_circle(c, hx, hy, k * 0.26f, body);
        PA_Vec2 cr[3] = { { hx - k * 0.12f, hy - k * 0.18f }, { hx + k * 0.05f, hy - k * 0.48f }, { hx + k * 0.16f, hy - k * 0.14f } };
        pa_fill_poly(c, cr, 3, band);
        pa_fill_circle(c, hx - k * 0.09f, hy, k * 0.05f, PA_RGB(40, 20, 20));
        pa_fill_circle(c, hx + k * 0.09f, hy, k * 0.05f, PA_RGB(40, 20, 20));
        pa_line(c, hx - k * 0.16f, hy - k * 0.10f, hx - k * 0.03f, hy - k * 0.05f, fmaxf(1.0f, k * 0.04f), PA_RGB(40, 20, 20));
        pa_line(c, hx + k * 0.16f, hy - k * 0.10f, hx + k * 0.03f, hy - k * 0.05f, fmaxf(1.0f, k * 0.04f), PA_RGB(40, 20, 20));
    } else {
        float hx = f.x, hy = ty - u * 1.46f;
        pa_fill_circle(c, hx, hy, k * 0.25f, pa_mix(body, bodyD, 0.25f));
        PA_Vec2 bolt[6] = { { f.x + k * 0.06f, ty - u * 1.05f }, { f.x - k * 0.14f, ty - u * 0.68f }, { f.x - k * 0.01f, ty - u * 0.68f },
                            { f.x - k * 0.08f, ty - u * 0.32f }, { f.x + k * 0.15f, ty - u * 0.76f }, { f.x + k * 0.02f, ty - u * 0.76f } };
        pa_fill_poly(c, bolt, 6, band);
        pa_fill_ellipse(c, hx - k * 0.08f, hy - k * 0.1f, k * 0.09f, k * 0.05f, PA_RGBA(255, 255, 255, 160));
    }
    /* HP: the boss gets a broad bar, 0.3 of the screen, over its head */
    char buf[12]; snprintf(buf, sizeof(buf), "%d", (int)ceilf(b->hp));
    float ts = b->boss ? 36.0f * g_u : fmaxf(16.0f * g_u, fminf(28.0f * g_u, k * 0.42f));
    float hy = ty - u * 1.95f - ts * 0.4f;
    if (hy < (b->boss ? 128.0f : 96.0f) * g_u) hy = (b->boss ? 128.0f : 96.0f) * g_u;
    float bw = b->boss ? g_w * 0.50f : fmaxf(46.0f * g_u, fminf(120.0f * g_u, k * 1.3f));
    float bh = b->boss ? 18.0f * g_u : fmaxf(6.0f, fminf(10.0f * g_u, k * 0.1f));
    float by = hy + ts * 0.62f;
    pa_round_rect(c, f.x - bw * 0.5f - 3.0f, by - 3.0f, bw + 6.0f, bh + 6.0f, bh, C_INK);
    pa_round_rect(c, f.x - bw * 0.5f, by, bw, bh, bh * 0.5f, PA_RGB(255, 255, 255));
    float fr = pa_clamp01(b->hp / b->maxhp);
    pa_round_rect(c, f.x - bw * 0.5f, by, fmaxf(bh, bw * fr), bh, bh * 0.5f, enemy ? PA_RGB(236, 50, 56) : PA_RGB(60, 210, 90));
    txt(c, buf, f.x, hy, ts, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.3f);
}

/* ---- the cannon ---- */
static void draw_cannon_body(PA_Canvas *c, float x, float z, float rec, int lead) {
    float k = ss(z) * 1.0f;
    PA_Vec2 f = proj(x, 0, z);
    pa_shadow(c, f.x + k * 0.1f, f.y, k * 0.85f, k * 0.3f, 0.7f);
    /* wheels */
    for (int s = -1; s <= 1; s += 2) {
        for (int r = 0; r < 2; r++) {
            float wx = f.x + (float)s * k * 0.56f, wy = f.y - k * 0.12f - (float)r * k * 0.34f;
            pa_round_rect(c, wx - k * 0.14f, wy - k * 0.22f, k * 0.28f, k * 0.36f, k * 0.1f, PA_RGB(36, 36, 46));
            pa_round_rect(c, wx - k * 0.07f, wy - k * 0.12f, k * 0.14f, k * 0.16f, k * 0.05f, PA_RGB(150, 152, 168));
        }
    }
    /* chassis */
    pa_round_rect(c, f.x - k * 0.5f, f.y - k * 0.6f, k * 1.0f, k * 0.42f, k * 0.12f, PA_RGB(60, 64, 84));
    pa_round_rect(c, f.x - k * 0.5f, f.y - k * 0.64f, k * 1.0f, k * 0.14f, k * 0.07f, PA_RGB(104, 108, 132));
    /* barrel: a fat glossy capsule tipped toward the track */
    float by = f.y - k * 0.52f + rec * k * 0.12f;
    float bl = k * (1.15f - rec * 0.12f), bw = k * (0.72f + rec * 0.06f);
    PA_Color b0 = PA_RGB(150, 214, 255), b1 = PA_RGB(44, 140, 255), b2 = PA_RGB(18, 76, 200);
    if (G.cannon_flash > 0.0f) { b0 = pa_mix(b0, PA_RGB(255, 80, 80), G.cannon_flash); b1 = pa_mix(b1, PA_RGB(255, 60, 60), G.cannon_flash); }
    pa_round_rect(c, f.x - bw * 0.46f, by - bl - k * 0.02f, bw, bl, bw * 0.48f, PA_RGB(191, 230, 255));
    PA_Paint pb = pa_linear(f.x - bw * 0.5f, 0, f.x + bw * 0.5f, 0);
    pa_stop(&pb, 0.0f, b0); pa_stop(&pb, 0.4f, b1); pa_stop(&pb, 1.0f, b2);
    pa_round_rect_paint(c, f.x - bw * 0.5f, by - bl, bw, bl, bw * 0.48f, &pb);
    pa_round_rect(c, f.x - bw * 0.52f, by - bl * 0.45f, bw * 1.04f, k * 0.12f, k * 0.05f, lead ? PA_RGB(255, 196, 40) : PA_RGB(20, 70, 180));
    float mx = f.x, my = by - bl + bw * 0.28f;
    pa_fill_ellipse(c, mx, my, bw * 0.46f, bw * 0.26f, b2);
    pa_fill_ellipse(c, mx, my + 1.0f, bw * 0.32f, bw * 0.17f, PA_RGB(14, 30, 80));
    pa_fill_ellipse(c, f.x - bw * 0.22f, by - bl * 0.62f, bw * 0.08f, bl * 0.2f, PA_RGBA(255, 255, 255, 150));
    if (rec > 0.5f && G.firing) {
        float r = bw * 0.55f * rec;
        disc(c, mx, my - r * 0.4f, r, PA_RGB(255, 236, 160), 0.8f);
        disc(c, mx, my - r * 0.4f, r * 0.55f, PA_RGB(255, 255, 255), 1.0f);
    }
}

typedef struct { float z, rec; int lead; } CanCap;
static void cap_cannon(PA_Canvas *c, const void *arg) {
    const CanCap *cc = (const CanCap *)arg;
    int fs = G.firing; G.firing = 1;
    float cf = G.cannon_flash; G.cannon_flash = 0.0f;
    draw_cannon_body(c, 0.0f, cc->z, cc->rec, cc->lead);
    G.firing = fs; G.cannon_flash = cf;
}
static Spr g_can_spr[2][2][2];     /* row, recoiled, lead */
static float g_can_k = -1.0f, g_can_h = -1.0f;
static void cannon_body(PA_Canvas *c, float x, float z, float rec, int lead, int row) {
    if (G.cannon_flash > 0.0f) { draw_cannon_body(c, x, z, rec, lead); return; }
    if (g_can_k != g_k || g_can_h != g_h) {
        g_can_k = g_k; g_can_h = g_h;
        for (int r = 0; r < 2; r++) for (int v = 0; v < 2; v++) for (int l = 0; l < 2; l++) {
            float cz = CANNON_Z + (r ? -1.15f : 0.0f), k = ss(cz);
            CanCap cc = { cz, v ? 1.0f : 0.0f, l };
            PA_Vec2 f = proj(0.0f, 0.0f, cz);
            capture(&g_can_spr[r][v][l], f.x - k * 1.2f, f.y - k * 2.2f, f.x + k * 1.2f, f.y + k * 0.6f, f.x, f.y, cap_cannon, &cc);
        }
    }
    const Spr *sp = &g_can_spr[row][(rec > 0.5f && G.firing) ? 1 : 0][lead];
    if (!sp->a) { draw_cannon_body(c, x, z, rec, lead); return; }
    if (g_zoom != 1.0f) { PA_Vec2 a = proj1(x, 0.0f, z); blit_zoomed(c, sp, a.x, a.y); return; }
    PA_Vec2 f = proj(x, 0.0f, z);
    blit(c, sp, (int)f.x, (int)f.y);
}

/* The cannon fleet: one gun at the start, up to seven as fire rate is bought. */
static void draw_cannon(PA_Canvas *c) {
    int n = fleet_size(G.up_fire), front = n < 4 ? n : 4;
    float z = CANNON_Z, k = ss(z);
    PA_Vec2 f = proj(G.cannon_x, 0, z);
    float half = (float)(front - 1) * 0.62f + 0.9f;
    /* green aim ring round the whole squad */
    {
        PA_Vec2 pts[48];
        float rz = n > 4 ? 0.75f : 0.45f;
        PA_Vec2 cc = proj(G.cannon_x, 0, z - (n > 4 ? 0.55f : 0.0f));
        for (int i = 0; i < 48; i++) {
            float a = PA_TAU * (float)i / 48.0f;
            pts[i].x = cc.x + cosf(a) * k * (half + 0.2f); pts[i].y = cc.y + sinf(a) * k * rz;
        }
        pa_stroke_poly(c, pts, 48, 1, fmaxf(3.0f, k * 0.1f), PA_RGBA(80, 236, 90, 220));
    }
    /* far row first */
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < n; i++) {
        float cx, cz; fleet_slot(n, i, &cx, &cz);
        if ((cz < 0.0f) != (pass == 1)) continue;
        cannon_body(c, G.cannon_x + cx, z + cz, G.crecoil[i], i == 0, cz < 0.0f);
    }
    /* charge meter: a vertical capsule beside the fleet */
    float kk = k * 1.1f;
    float mh = kk * 1.7f, mw = kk * 0.38f;
    float gx = sx(G.cannon_x - half - 0.3f, z) - mw, gy = f.y - mh;
    if (gx < 8.0f) gx = sx(G.cannon_x + half + 0.3f, z);
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
    float bx = gx + mw * 0.5f, bty = gy + mh - mw * 0.9f;
    PA_Vec2 bolt[6] = { { bx + mw * 0.10f, bty - mw * 0.45f }, { bx - mw * 0.22f, bty + mw * 0.05f }, { bx - mw * 0.02f, bty + mw * 0.05f },
                        { bx - mw * 0.12f, bty + mw * 0.45f }, { bx + mw * 0.22f, bty - mw * 0.06f }, { bx + mw * 0.02f, bty - mw * 0.06f } };
    pa_fill_poly(c, bolt, 6, PA_RGB(255, 255, 255));
    /* defence health under the squad */
    if (G.cannon_hp < G.cannon_max || G.state == S_PLAY) {
        float hw = k * 1.6f, hh = fmaxf(7.0f, k * 0.15f);
        float hx = f.x - hw * 0.5f, hy = sy(z - (n > 4 ? 1.15f : 0.0f)) + k * 0.4f;
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
            disc(c, s.x, s.y, r, p->col, 0.85f * t);
            disc(c, s.x - r * 0.2f, s.y - r * 0.2f, r * 0.5f, PA_RGB(255, 255, 255), 0.6f * t);
            break; }
        case PK_BIT: {
            /* a tumbling chunk: lit top face over a shaded side, squashing as it spins */
            float r = p->size * k, a = p->life * 9.0f + (float)i;
            float wv = r * (0.55f + 0.45f * fabsf(cosf(a))), hv = r * (0.55f + 0.45f * fabsf(sinf(a)));
            float al = fminf(1.0f, t * 2.5f);
            frect(c, s.x - wv * 0.5f, s.y - hv * 0.5f, wv, hv, pa_shade(p->col, -0.25f), al);
            frect(c, s.x - wv * 0.5f, s.y - hv * 0.5f, wv, hv * 0.45f, pa_shade(p->col, 0.3f), al);
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
        case PK_FLASH: {
            /* screen-space white ring, out to p->size pixels */
            float e = 1.0f - t, r = p->size * g_u * (0.25f + 0.75f * pa_smooth(e));
            if (e < 0.5f) {
                /* white-hot core going gold, 0.25 of the screen across */
                float cr = 0.125f * g_w * (0.5f + e), ca = 1.0f - e * 2.0f;
                PA_Paint cp = pa_radial(s.x, s.y, 0.0f, cr);
                pa_stop(&cp, 0.0f, PA_RGBA(255, 255, 255, (int)(255.0f * ca)));
                pa_stop(&cp, 0.45f, pa_alpha(pa_hex(0xFFD23F), 0.85f * ca));
                pa_stop(&cp, 1.0f, PA_RGBA(255, 210, 63, 0));
                pa_fill_ellipse_paint(c, s.x, s.y, cr, cr, &cp);
            }
            pa_stroke_circle(c, s.x, s.y, r, fmaxf(2.0f, 9.0f * g_u * t), pa_alpha(PA_RGB(255, 255, 255), t));
            break; }
        case PK_DUST: {
            float e = 1.0f - t, r = p->size * k * (0.6f + 0.9f * e);
            disc(c, s.x, s.y, r, p->col, 0.75f * t);
            disc(c, s.x - r * 0.25f, s.y - r * 0.3f, r * 0.55f, pa_shade(p->col, 0.3f), 0.6f * t);
            break; }
        default: {
            float r = fmaxf(1.5f, p->size * k);
            disc(c, s.x, s.y, r, p->col, t);
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
        float size = fminf(fmaxf(18.0f * g_u, ss(f->z) * 0.75f), 36.0f * g_u) * f->size * pop;
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
    bg_blit(c, (int)g_shx, (int)g_shy);
    draw_castle(c);
    draw_castle_hp(c);

    /* bucket the crowds by depth */
    for (int b = 0; b < ZBINS; b++) { g_zb_head[0][b] = -1; g_zb_head[1][b] = -1; }
    for (int i = 0; i < g_nblue; i++) { int b = zbin(g_blue[i].z); g_zb_next[0][i] = g_zb_head[0][b]; g_zb_head[0][b] = (short)i; }
    for (int i = 0; i < g_nred; i++) { int b = zbin(g_red[i].z); g_zb_next[1][i] = g_zb_head[1][b]; g_zb_head[1][b] = (short)i; }

    /* the large things, in depth order */
    int done_g[MAX_GATES] = { 0 }, done_b[MAX_BRUTES] = { 0 }, done_c[MAX_CHAMPS] = { 0 }, done_l[MAX_LADDER] = { 0 };
    int cannon_done = 0;
    for (int b = ZBINS - 1; b >= 0; b--) {
        float zlo = -4.0f + (float)b * CELL;
        for (;;) {
            /* the farthest undrawn big object at or beyond this bin */
            float bz = -1e9f; int kind = -1, idx = -1;
            for (int i = 0; i < g_ngate; i++) if (!done_g[i] && g_gate[i].z >= zlo && g_gate[i].z > bz) { bz = g_gate[i].z; kind = 0; idx = i; }
            for (int i = 0; i < g_nlad; i++) if (!done_l[i] && g_lad[i].z >= zlo && g_lad[i].z > bz) { bz = g_lad[i].z; kind = 4; idx = i; }
            for (int i = 0; i < g_nbrute; i++) if (!done_b[i] && g_brute[i].z >= zlo && g_brute[i].z > bz) { bz = g_brute[i].z; kind = 1; idx = i; }
            for (int i = 0; i < g_nchamp; i++) if (!done_c[i] && g_champ[i].z >= zlo && g_champ[i].z > bz) { bz = g_champ[i].z; kind = 2; idx = i; }
            if (!cannon_done && CANNON_Z >= zlo && CANNON_Z > bz) { bz = CANNON_Z; kind = 3; }
            if (kind < 0) break;
            if (kind == 0) { draw_gate(c, &g_gate[idx]); done_g[idx] = 1; }
            else if (kind == 4) { draw_ladder(c, &g_lad[idx]); done_l[idx] = 1; }
            else if (kind == 1) { draw_brute(c, &g_brute[idx], 1); done_b[idx] = 1; }
            else if (kind == 2) { draw_brute(c, &g_champ[idx], 0); done_c[idx] = 1; }
            else { draw_cannon(c); cannon_done = 1; }
        }
        draw_bin_units(c, b);
    }
    if (!cannon_done) draw_cannon(c);
    draw_parts(c);
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
        int idx = (int)((H - 8.0f) / 3.0f);
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
    mask_build();
    g_zoom = 1.0f;
    if (G.state == S_SHOP) { draw_shop(c); pa_hub_hide_pause(); return; }
    caches_refresh();
    /* Boss levels open with the camera pushing in on the beast, then easing back. */
    for (int i = 0; i < g_nbrute; i++) if (g_brute[i].boss && G.state == S_PLAY) {
        float t = G.t;
        float bump = pa_smooth(pa_clamp01(t / 0.7f)) * (1.0f - pa_smooth(pa_clamp01((t - 2.0f) / 1.0f)));
        if (bump > 0.001f) {
            const Brute *b = &g_brute[i];
            g_zpx = sx(b->x, b->z); g_zpy = sy(b->z) - ss(b->z) * b->size * 1.0f;
            g_zoom = 1.0f + 0.32f * bump;
        }
        break;
    }
    /* screen shake on the world only */
    float sh = G.shake * g_u;
    g_shx = sh * sinf(G.clock * 53.0f);
    g_shy = sh * 0.6f * cosf(G.clock * 41.0f);
    draw_world(c);
    g_shx = g_shy = 0.0f;
    g_zoom = 1.0f;
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
