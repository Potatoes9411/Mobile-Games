/* ===========================================================================
   HORDE ARENA - native port
   A Survivor!.io style auto-battler. A joystick appears wherever you touch;
   steering is the only input and every weapon fires itself. Zombies close in
   from every side in hundreds, kills drop XP gems, a full bar pauses for a
   three-card skill draft, and a weapon at five stars plus its partner item
   evolves into its super form. Elites drop lucky chests, two bosses lock you
   in a cage, and eight minutes ends the chapter.

   Drawn the way the plates show it: straight-down camera over a lavender-grey
   city of tiled pavement and darker asphalt with crosswalks, chibi monsters
   with heavy black outlines, glowing diamond gems, white damage numerals on
   everything, a segmented green XP bar along the top and slate cards with
   yellow headers and stars for the draft.

   Hundreds of enemies on a phone software renderer: every character is drawn
   once at load into an alpha sprite (rendered over black and over white and
   solved for coverage), so a frame costs a blit per monster rather than a
   dozen antialiased polygons, and a spatial grid keeps the sim linear.
   =========================================================================== */
#include "../pa.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- tunables -- */
#define RUN_LEN       480.0f     /* eight minutes, the final boss lands here */
#define MAX_ENEMIES   960
#define MAX_PROJ      360
#define MAX_ZONES     96
#define MAX_GEMS      1100
#define MAX_PARTS     900
#define MAX_NUMS      96
#define MAX_DECALS    220
#define MAX_EBUL      160
#define GRID_CELL     40.0f
#define GRID_W        64
#define GRID_H        80

#define OUTLINE_COL   0xFF1A1A2Eu

enum { PH_TITLE, PH_PLAY, PH_DRAFT, PH_CHEST, PH_DYING, PH_OVER };

/* Weapons and their partner items share an index: item i evolves weapon i. */
enum { W_KUNAI, W_GUARD, W_DRONE, W_BOLT, W_BRICK, W_BALL, W_FIRE, W_FIELD, W_COUNT };
enum { P_POWER, P_BRACER, P_FUEL, P_CUBE, P_BOOK, P_SHOES, P_OIL, P_RAGE, P_COUNT };
#define MAX_LV 5
#define SLOTS  6

/* Icon ids: weapons, then items, then evolutions, then the fallbacks. */
#define IC_ITEM(i) (W_COUNT + (i))
#define IC_EVO(i)  (W_COUNT + P_COUNT + (i))
#define IC_MEAT    (W_COUNT + P_COUNT + W_COUNT)
#define IC_COINS   (IC_MEAT + 1)

static const char *W_NAME[W_COUNT] = {
    "KUNAI", "GUARDIAN", "DRONE", "LIGHTNING", "BRICK", "SOCCER BALL", "MOLOTOV", "FORCEFIELD"
};
static const char *EVO_NAME[W_COUNT] = {
    "SHADOW KUNAI", "DEFENDER", "DESTROYER", "THUNDERSTORM", "DUMBBELL", "QUANTUM BALL", "NAPALM", "INFERNO RING"
};
static const char *P_NAME[P_COUNT] = {
    "POWER ROUND", "EXO BRACER", "HE FUEL", "ENERGY CUBE", "FITNESS GUIDE", "SPORT SHOES", "OIL BONDS", "RAGE TONIC"
};
static const char *W_DESC[W_COUNT][MAX_LV] = {
    { "THROWS A KUNAI AT THE NEAREST ENEMY", "ADDS 1 KUNAI", "KUNAI DAMAGE UP", "ADDS 1 KUNAI", "ADDS 1 KUNAI, DAMAGE UP" },
    { "TWO GUARDIANS ORBIT YOU", "GUARDIAN DAMAGE UP", "ADDS 1 GUARDIAN", "SPINS FASTER, DAMAGE UP", "ADDS 1 GUARDIAN" },
    { "A DRONE FIRES ROCKETS AT FOES", "FIRES FASTER", "ADDS A SECOND DRONE", "ROCKET DAMAGE UP", "BIGGER BLASTS, FASTER" },
    { "LIGHTNING STRIKES A RANDOM FOE", "ADDS 1 STRIKE", "STRIKE DAMAGE UP", "ADDS 1 STRIKE", "ADDS 1 STRIKE, FASTER" },
    { "LOBS A HEAVY BRICK THAT PIERCES", "BRICK DAMAGE UP", "ADDS 1 BRICK", "BRICK DAMAGE UP", "ADDS 1 BRICK" },
    { "A BALL THAT BOUNCES OFF THE SCREEN", "BALL DAMAGE UP", "ADDS 1 BALL", "BALLS LAST LONGER", "ADDS 1 BALL" },
    { "SETS THE GROUND ABLAZE", "BURN DAMAGE UP", "ADDS 1 BOTTLE", "BIGGER FIRES", "ADDS 1 BOTTLE" },
    { "A BURNING RING SCORCHES ALL NEAR YOU", "FIELD DAMAGE UP", "BIGGER FIELD", "BURNS FASTER", "BIGGER FIELD, DAMAGE UP" }
};
static const char *EVO_DESC[W_COUNT] = {
    "A STORM OF PIERCING BLADES",
    "SIX GUARDIANS THAT NEVER STOP",
    "HEAVY ROCKET SALVOS",
    "A STORM OF CHAINED STRIKES",
    "CRUSHING DUMBBELLS RAIN DOWN",
    "GLOWING BALLS THAT NEVER TIRE",
    "A SEA OF FIRE",
    "A HUGE RING OF HELLFIRE"
};
static const char *P_DESC[P_COUNT] = {
    "+10% DAMAGE",
    "+10% AREA",
    "+12% DURATION, +15% PICKUP RANGE",
    "-7% COOLDOWN",
    "+20 MAX HP AND HEALS",
    "+8% MOVE SPEED",
    "+0.6 HP REGEN EVERY SECOND",
    "+6% CRITICAL HIT CHANCE"
};

/* ---------------------------------------------------------------- enemies -- */
enum { E_ZOMBIE, E_BEETLE, E_FLY, E_BRUTE, E_SPITTER, E_BLUEBUG, E_DOG, E_ELITE, E_BOSS, E_KING, E_TYPES };

typedef struct {
    float hp, speed, dmg, r;
    int   xp;
    int   ranged;
} EnemyDef;

static const EnemyDef EDEF[E_TYPES] = {
    /* hp     speed  dmg   r      xp ranged */
    {  11.f,  44.f,  6.f, 12.5f, 1, 0 },   /* zombie */
    {   9.f,  52.f,  5.f, 12.5f, 1, 0 },   /* orange shell bug */
    {   6.f,  78.f,  4.f, 11.5f, 1, 0 },   /* fly */
    {  48.f,  36.f, 12.f, 17.f,  3, 0 },   /* hard hat brute */
    {  20.f,  38.f,  8.f, 15.f,  3, 1 },   /* flower spitter */
    {  20.f,  56.f,  7.f, 12.5f, 2, 0 },   /* blue shell bug */
    {   8.f,  80.f,  5.f, 12.f,  1, 0 },   /* zombie dog, the runner */
    { 420.f,  40.f, 18.f, 28.f, 25, 0 },   /* elite */
    {4400.f,  44.f, 15.f, 56.f, 60, 0 },   /* boss */
    {15000.f, 48.f, 20.f, 62.f, 90, 0 },   /* final boss */
};

typedef struct {
    float x, y;
    float hp, maxhp;
    float speed, dmg, r;
    float kx, ky;           /* knockback velocity */
    float flash, anim, atk, stun;
    float imm[W_COUNT];     /* per-weapon re-hit timers */
    uint8_t type, alive, face, elite;
    /* boss behaviour */
    float bt;               /* state timer */
    int   bstate;
    float tx, ty;           /* dash heading */
} Enemy;

/* -------------------------------------------------------------- projectiles -- */
enum { PJ_KUNAI, PJ_ROCKET, PJ_BRICK, PJ_BALL, PJ_BOTTLE };

typedef struct {
    float x, y, vx, vy, z, vz;
    float life, dmg, r, ang, spin, age;
    int   kind, pierce, evo, alive;
    float tx, ty;
} Proj;

enum { Z_FIRE, Z_BLAST, Z_BOLT, Z_RING, Z_SLAM };

typedef struct {
    float x, y, r, life, max, dps, tick;
    int   kind, alive, evo;
    float x2, y2;
} Zone;

enum { G_BLUE, G_GREEN, G_GOLD, G_COIN, G_MEAT, G_MAGNET, G_BOMB, G_CHEST, G_KINDS };

typedef struct {
    float x, y, vx, vy;
    int   value, kind, alive, pulled;
    float t;
} Gem;

typedef struct { float x, y, vx, vy, life, max, size; uint32_t col; int kind; } Part;
typedef struct { float x, y, life; int value, crit; } Num;
typedef struct { float x, y, r; int seed; float age; } Decal;
typedef struct { float x, y, vx, vy, life, r; int alive; } EBullet;

/* ---------------------------------------------------------------- sprites -- */
typedef struct { int w, h; float ox, oy; uint32_t *px; } Sprite;

#define ANIM_FRAMES 4
enum { SPR_HERO, SPR_ZOMBIE, SPR_BEETLE, SPR_FLY, SPR_BRUTE, SPR_SPITTER, SPR_BLUEBUG, SPR_DOG,
       SPR_ELITE, SPR_BOSS, SPR_KING, SPR_SETS };

static Sprite g_spr[SPR_SETS][ANIM_FRAMES];
static Sprite g_gem_spr[G_KINDS];
static Sprite g_flame[ANIM_FRAMES];
static Sprite g_spark;
static void spr_blit(PA_Canvas *c, const Sprite *s, float x, float y, int flip, uint32_t tint, int amt, int alpha);
static void flame_ring(PA_Canvas *c, float x, float y, float r, int n, float spin, int alpha);
/* Additive glow falloff, sampled at any radius. Cheaper than a radial paint
   and it lights rather than paints over. */
#define GLOW_N 64
static uint8_t g_glow_lut[GLOW_N * GLOW_N];
static float  g_baked_scale = -1.0f;

/* ------------------------------------------------------------------ state -- */
typedef struct { int kind, id; } Pick;   /* kind: 0 weapon, 1 item, 2 evolution, 3 meat, 4 coins */

typedef struct {
    int    phase;
    float  t, clock;        /* run time, wall animation time */
    float  phase_t;
    PA_Rng rng;

    /* hero */
    float  px, py, pvx, pvy;
    float  hp, maxhp, invuln, hurt, regen_acc;
    int    face;
    float  run_anim, moving;
    float  cam_x, cam_y, shake, flash;
    uint32_t flash_col;

    /* joystick */
    int    joy_on;
    float  joy_ox, joy_oy, joy_kx, joy_ky;
    float  mvx, mvy;

    /* progression */
    int    level, xp, xpneed, pending;
    int    kills, coins;
    int    wlv[W_COUNT], evo[W_COUNT], plv[P_COUNT];
    int    wslot[SLOTS], wslots, pslot[SLOTS], pslots;
    float  cd[W_COUNT];
    float  guard_ang, guard_cycle;
    float  drone_ang;
    int    picks;

    /* waves */
    int    next_event;
    float  spawn_acc;
    int    boss_idx;        /* enemy index, or -1 */
    int    boss_kind;
    int    bosses_down;
    int    cage;
    float  cage_x, cage_y, cage_r, cage_t;
    char   banner[32];
    int    banner_kind;     /* 0 horde, 1 elite, 2 boss */
    float  banner_t;
    float  combo_t; int combo;
    float  levelup_t;

    /* draft */
    Pick   opt[3];
    int    nopt, chosen;
    float  pick_t;

    /* chest */
    Pick   chest_tiles[16];
    int    chest_win[3], chest_nwin, chest_coins;
    float  chest_spin;      /* position of the runner, in tiles */
    float  chest_speed;
    int    chest_stage;

    /* results */
    int    won, new_best, reward;
    float  over_t;

    /* bot */
    float  bot_wander, bot_pick;
    float  field_pulse, muzzle, muzzle_ang;

    /* sfx throttles */
    float  sfx_hit, sfx_gem, sfx_throw;
    int    gem_streak;
} Run;

static Run   S;
static Enemy g_en[MAX_ENEMIES];
static int   g_en_count;          /* high-water mark */
static Proj  g_pj[MAX_PROJ];
static Zone  g_zn[MAX_ZONES];
static Gem   g_gm[MAX_GEMS];
static int   g_gem_count;
static Part  g_pt[MAX_PARTS];
static int   g_pt_next;
static Num   g_num[MAX_NUMS];
static int   g_num_next;
static Decal g_dc[MAX_DECALS];
static int   g_dc_next;
static EBullet g_eb[MAX_EBUL];

static int   g_grid_head[GRID_W * GRID_H];
static int   g_grid_next[MAX_ENEMIES];
static float g_grid_x0, g_grid_y0;

static int   g_best, g_best_kills, g_bank, g_meta_atk, g_meta_hp, g_runs, g_loaded;

static struct { int w, h; float s; } L = { 540, 1170, 1.0f };

static void begin_run(void);
static void gc_reset(void);
static const Sprite *ball_sprite(int evo, float r, float ang);
static void open_draft(void);
static void open_chest(void);
static void finish(int won);
static void hurt_enemy(int i, float dmg, float kx, float ky, int wid);
static void draw_icon(PA_Canvas *c, int id, float cx, float cy, float size, float t);

/* =========================================================== sprite baking == */
static void spr_free(Sprite *s) { free(s->px); s->px = NULL; s->w = s->h = 0; }

typedef void (*DrawFn)(PA_Canvas *c, float x, float y, float k, int frame);

/** Render `fn` twice, over black and over white, and solve each pixel for the
    colour and coverage that produce both. Exact for source-over blending, so
    the antialiased outline survives into the sprite. */
static void spr_bake(Sprite *s, int w, int h, float ox, float oy, float k, int frame, DrawFn fn) {
    PA_Canvas a, b;
    spr_free(s);
    if (!pa_canvas_init(&a, w, h)) return;
    if (!pa_canvas_init(&b, w, h)) { pa_canvas_free(&a); return; }
    pa_clear(&a, PA_RGB(0, 0, 0));
    pa_clear(&b, PA_RGB(255, 255, 255));
    fn(&a, ox, oy, k, frame);
    fn(&b, ox, oy, k, frame);
    s->px = (uint32_t *)malloc((size_t)w * (size_t)h * 4u);
    if (s->px) {
        s->w = w; s->h = h; s->ox = ox; s->oy = oy;
        for (int i = 0; i < w * h; i++) {
            uint32_t pb = a.px[i], pw = b.px[i];
            int rb = (int)((pb >> 16) & 255), gb = (int)((pb >> 8) & 255), bb = (int)(pb & 255);
            int rw = (int)((pw >> 16) & 255), gw = (int)((pw >> 8) & 255), bw = (int)(pw & 255);
            int al = 255 - ((rw - rb) + (gw - gb) + (bw - bb)) / 3;
            if (al <= 2) { s->px[i] = 0; continue; }
            if (al > 255) al = 255;
            int r = rb * 255 / al, g = gb * 255 / al, bl = bb * 255 / al;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (bl > 255) bl = 255;
            s->px[i] = ((uint32_t)al << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
        }
    }
    pa_canvas_free(&a);
    pa_canvas_free(&b);
}

/** Alpha blit with optional mirror, a tint pulled toward `tint` by `amt`
    (hit flashes) and a global alpha. Clipped to the canvas scissor. */
static void spr_blit(PA_Canvas *c, const Sprite *s, float x, float y, int flip,
                     uint32_t tint, int amt, int alpha) {
    if (!s->px) return;
    int x0 = (int)floorf(x - (flip ? (float)s->w - s->ox : s->ox) + 0.5f);
    int y0 = (int)floorf(y - s->oy + 0.5f);
    int sx0 = 0, sy0 = 0, sx1 = s->w, sy1 = s->h;
    if (x0 + sx0 < c->clip_x0) sx0 = c->clip_x0 - x0;
    if (y0 + sy0 < c->clip_y0) sy0 = c->clip_y0 - y0;
    if (x0 + sx1 > c->clip_x1) sx1 = c->clip_x1 - x0;
    if (y0 + sy1 > c->clip_y1) sy1 = c->clip_y1 - y0;
    if (sx0 >= sx1 || sy0 >= sy1) return;
    int tr = (int)((tint >> 16) & 255), tg = (int)((tint >> 8) & 255), tb = (int)(tint & 255);
    for (int yy = sy0; yy < sy1; yy++) {
        const uint32_t *src = s->px + (size_t)yy * (size_t)s->w;
        uint32_t *dst = c->px + (size_t)(y0 + yy) * (size_t)c->w + x0;
        for (int xx = sx0; xx < sx1; xx++) {
            uint32_t p = src[flip ? s->w - 1 - xx : xx];
            int a = (int)(p >> 24);
            if (!a) continue;
            int r = (int)((p >> 16) & 255), g = (int)((p >> 8) & 255), b = (int)(p & 255);
            if (amt) {
                r += (tr - r) * amt / 255;
                g += (tg - g) * amt / 255;
                b += (tb - b) * amt / 255;
            }
            if (alpha < 255) a = a * alpha / 255;
            if (a >= 255) { dst[xx] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; continue; }
            uint32_t d = dst[xx];
            int inv = 255 - a;
            int dr = (int)((d >> 16) & 255), dg = (int)((d >> 8) & 255), db = (int)(d & 255);
            dst[xx] = ((uint32_t)((r * a + dr * inv) / 255) << 16) |
                      ((uint32_t)((g * a + dg * inv) / 255) << 8) |
                      (uint32_t)((b * a + db * inv) / 255);
        }
    }
}

/* Axis-aligned fills that skip the antialiased polygon path. The ground is a
   few hundred rectangles and the gore a few hundred dots every frame; through
   the scanline filler they cost more than the whole swarm of sprites. */
static void fast_rect(PA_Canvas *c, float fx, float fy, float fw, float fh, uint32_t rgb, int alpha) {
    int x0 = (int)floorf(fx + 0.5f), y0 = (int)floorf(fy + 0.5f);
    int x1 = (int)floorf(fx + fw + 0.5f), y1 = (int)floorf(fy + fh + 0.5f);
    if (x0 < c->clip_x0) x0 = c->clip_x0;
    if (y0 < c->clip_y0) y0 = c->clip_y0;
    if (x1 > c->clip_x1) x1 = c->clip_x1;
    if (y1 > c->clip_y1) y1 = c->clip_y1;
    if (x0 >= x1 || y0 >= y1 || alpha <= 0) return;
    rgb &= 0xFFFFFFu;
    if (alpha >= 255) {
        for (int y = y0; y < y1; y++) {
            uint32_t *d = c->px + (size_t)y * (size_t)c->w;
            for (int x = x0; x < x1; x++) d[x] = rgb;
        }
        return;
    }
    int sr = (int)((rgb >> 16) & 255) * alpha, sg = (int)((rgb >> 8) & 255) * alpha, sb = (int)(rgb & 255) * alpha;
    int inv = 255 - alpha;
    for (int y = y0; y < y1; y++) {
        uint32_t *d = c->px + (size_t)y * (size_t)c->w;
        for (int x = x0; x < x1; x++) {
            uint32_t p = d[x];
            d[x] = ((uint32_t)((sr + (int)((p >> 16) & 255) * inv) / 255) << 16) |
                   ((uint32_t)((sg + (int)((p >> 8) & 255) * inv) / 255) << 8) |
                   (uint32_t)((sb + (int)(p & 255) * inv) / 255);
        }
    }
}

static void fast_disc(PA_Canvas *c, float cx, float cy, float r, uint32_t rgb, int alpha) {
    if (r < 0.5f) r = 0.5f;
    int y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
    for (int y = y0; y <= y1; y++) {
        float dy = (float)y + 0.5f - cy;
        float span = r * r - dy * dy;
        if (span <= 0.0f) continue;
        float hw = sqrtf(span);
        fast_rect(c, cx - hw, (float)y, hw * 2.0f, 1.0f, rgb, alpha);
    }
}

static void flame_ring(PA_Canvas *c, float x, float y, float r, int n, float spin, int alpha) {
    int fl = (int)(S.clock * 30.0f) & 1;   /* two-frame flicker */
    for (int i = 0; i < n; i++) {
        float a = (float)i / (float)n * PA_TAU + spin;
        float fx = x + cosf(a) * r, fy = y + sinf(a) * r + 8.0f * L.s;
        spr_blit(c, &g_flame[(i + fl * 2 + (int)(S.clock * 12.0f)) & 3], fx, fy, (i + fl) & 1, 0, 0, alpha);
    }
}

/* ======================================================= outlined shapes == */
/* Characters are drawn in two passes: every part expanded in the outline
   colour, then every part in its fill. The parts merge into one silhouette
   with a single heavy outline, which is the comic look of the plates. */
static int   g_pass;
static float g_ow;

static PA_Color OUTC(void) { return OUTLINE_COL; }

/* Pass -1 (the hero only) lays a white rim outside the black outline so the
   player reads against the swarm. */
static float g_wo;
static float P_grow(void) { return g_pass < 0 ? g_ow + g_wo : g_ow; }
static PA_Color P_edge(void) { return g_pass < 0 ? PA_RGB(255, 255, 255) : OUTC(); }

static void P_circle(PA_Canvas *c, float x, float y, float r, PA_Color col) {
    if (g_pass <= 0) pa_fill_circle(c, x, y, r + P_grow(), P_edge());
    else pa_fill_circle(c, x, y, r, col);
}
static void P_ellipse(PA_Canvas *c, float x, float y, float rx, float ry, PA_Color col) {
    if (g_pass <= 0) pa_fill_ellipse(c, x, y, rx + P_grow(), ry + P_grow(), P_edge());
    else pa_fill_ellipse(c, x, y, rx, ry, col);
}
static void P_rrect(PA_Canvas *c, float x, float y, float w, float h, float r, PA_Color col) {
    float g = P_grow();
    if (g_pass <= 0) pa_round_rect(c, x - g, y - g, w + g * 2.0f, h + g * 2.0f, r + g, P_edge());
    else pa_round_rect(c, x, y, w, h, r, col);
}
static void P_poly(PA_Canvas *c, const PA_Vec2 *p, int n, PA_Color col) {
    if (g_pass <= 0) { pa_fill_poly(c, p, n, P_edge()); pa_stroke_poly(c, p, n, 1, P_grow() * 2.0f, P_edge()); }
    else pa_fill_poly(c, p, n, col);
}
static void P_line(PA_Canvas *c, float x0, float y0, float x1, float y1, float w, PA_Color col) {
    if (g_pass <= 0) pa_line(c, x0, y0, x1, y1, w + P_grow() * 2.0f, P_edge());
    else pa_line(c, x0, y0, x1, y1, w, col);
}

/* One-off outlined shapes for icons and effects. */
static void o_circle(PA_Canvas *c, float x, float y, float r, PA_Color col, float ow) {
    pa_fill_circle(c, x, y, r + ow, OUTC());
    pa_fill_circle(c, x, y, r, col);
}
static void o_rrect(PA_Canvas *c, float x, float y, float w, float h, float r, PA_Color col, float ow) {
    pa_round_rect(c, x - ow, y - ow, w + ow * 2.0f, h + ow * 2.0f, r + ow, OUTC());
    pa_round_rect(c, x, y, w, h, r, col);
}
static void o_poly(PA_Canvas *c, const PA_Vec2 *p, int n, PA_Color col, float ow) {
    pa_fill_poly(c, p, n, OUTC());
    pa_stroke_poly(c, p, n, 1, ow * 2.0f, OUTC());
    pa_fill_poly(c, p, n, col);
}

static void eye(PA_Canvas *c, float x, float y, float r, float look) {
    pa_fill_circle(c, x, y, r + 0.9f, OUTC());
    pa_fill_circle(c, x, y, r, PA_RGB(255, 255, 255));
    pa_fill_circle(c, x + r * 0.32f * look, y + r * 0.1f, r * 0.5f, PA_RGB(20, 18, 26));
}

static void soft_shadow(PA_Canvas *c, float x, float y, float rx, float ry) {
    pa_fill_ellipse(c, x, y, rx, ry, PA_RGBA(20, 16, 40, 70));
    pa_fill_ellipse(c, x, y, rx * 0.72f, ry * 0.7f, PA_RGBA(20, 16, 40, 60));
}

/* ======================================================== character art == */
/* All art faces right at scale k; the blitter mirrors it. (x, y) is the point
   between the feet. */
static void art_hero(PA_Canvas *c, float x, float y, float k, int f) {
    float ph = (float)f * PA_TAU / (float)ANIM_FRAMES;
    float bob = fabsf(sinf(ph)) * 2.2f * k;
    float step = sinf(ph) * 3.5f * k;
    float by = y - bob;
    soft_shadow(c, x, y, 15.0f * k, 5.0f * k);
    g_ow = 2.3f * k;
    g_wo = 2.6f * k;
    for (g_pass = -1; g_pass < 2; g_pass++) {
        P_rrect(c, x - 9.0f * k + step, y - 12.0f * k, 8.0f * k, 12.0f * k, 3.5f * k, pa_hex(0x30323F));
        P_rrect(c, x + 1.0f * k - step, y - 12.0f * k, 8.0f * k, 12.0f * k, 3.5f * k, pa_hex(0x30323F));
        /* scarf tail streaming behind */
        PA_Vec2 tail[4] = {
            { x - 6.0f * k, by - 30.0f * k }, { x - 19.0f * k, by - 28.0f * k + step * 0.6f },
            { x - 18.0f * k, by - 22.0f * k + step * 0.6f }, { x - 6.0f * k, by - 24.0f * k }
        };
        P_poly(c, tail, 4, pa_hex(0xC93424));
        P_rrect(c, x - 11.0f * k, by - 29.0f * k, 22.0f * k, 19.0f * k, 6.0f * k, pa_hex(0x545A6E));
        /* gun held across the body */
        P_rrect(c, x + 2.0f * k, by - 22.0f * k, 22.0f * k, 7.0f * k, 2.0f * k, pa_hex(0x666C7C));
        P_rrect(c, x + 6.0f * k, by - 17.0f * k, 6.0f * k, 7.0f * k, 2.0f * k, pa_hex(0xE8862A));
        P_rrect(c, x - 12.0f * k, by - 31.0f * k, 25.0f * k, 7.5f * k, 3.5f * k, pa_hex(0xE2442F));
        P_rrect(c, x - 14.0f * k, by - 56.0f * k, 30.0f * k, 28.0f * k, 9.0f * k, pa_hex(0x7A3B2C));
    }
    /* face */
    pa_round_rect(c, x - 8.0f * k, by - 44.0f * k, 22.0f * k, 14.0f * k, 4.5f * k, pa_hex(0xF7CFAA));
    pa_fill_circle(c, x + 0.5f * k, by - 37.5f * k, 2.4f * k, pa_hex(0x1A1620));
    pa_fill_circle(c, x + 8.5f * k, by - 37.5f * k, 2.4f * k, pa_hex(0x1A1620));
    /* goggles */
    o_rrect(c, x - 13.0f * k, by - 55.0f * k, 29.0f * k, 11.0f * k, 5.5f * k, pa_hex(0x8ED2F4), 1.8f * k);
    pa_round_rect(c, x - 10.0f * k, by - 53.0f * k, 23.0f * k, 3.5f * k, 1.7f * k, pa_hex(0xD8F2FF));
    /* gun highlight and scarf fold */
    pa_fill_rect(c, x + 4.0f * k, by - 21.0f * k, 17.0f * k, 1.8f * k, pa_hex(0x9AA0B0));
    pa_fill_rect(c, x - 10.0f * k, by - 26.5f * k, 21.0f * k, 1.6f * k, pa_hex(0xA82A1E));
}

static void zombie_core(PA_Canvas *c, float x, float y, float k, int f, uint32_t skin,
                        uint32_t shirt, uint32_t eyes, int big) {
    float ph = (float)f * PA_TAU / (float)ANIM_FRAMES;
    float bob = fabsf(sinf(ph)) * 1.6f * k;
    float step = sinf(ph) * 3.0f * k;
    float by = y - bob;
    soft_shadow(c, x, y, 13.0f * k, 4.5f * k);
    g_ow = (big ? 2.4f : 2.5f) * k;
    for (g_pass = 0; g_pass < 2; g_pass++) {
        P_rrect(c, x - 7.0f * k + step, y - 10.0f * k, 6.0f * k, 10.0f * k, 2.5f * k, pa_hex(0x2B3552));
        P_rrect(c, x + 1.0f * k - step, y - 10.0f * k, 6.0f * k, 10.0f * k, 2.5f * k, pa_hex(0x2B3552));
        P_rrect(c, x - 9.0f * k, by - 23.0f * k, 18.0f * k, 15.0f * k, 4.5f * k, pa_hex(shirt));
        /* arms reaching forward */
        P_line(c, x + 2.0f * k, by - 19.0f * k, x + 13.0f * k, by - 18.0f * k - step * 0.4f, 4.6f * k, pa_hex(skin));
        P_rrect(c, x - 11.0f * k, by - 42.0f * k, 22.0f * k, 21.0f * k, 7.0f * k, pa_hex(skin));
        if (big) {
            for (int i = 0; i < 3; i++) {
                float sx = x - 7.0f * k + (float)i * 6.5f * k;
                PA_Vec2 sp[3] = { { sx - 3.0f * k, by - 40.0f * k }, { sx + 0.5f * k, by - 48.0f * k },
                                  { sx + 3.5f * k, by - 40.0f * k } };
                P_poly(c, sp, 3, pa_hex(0x3A2A40));
            }
        }
    }
    pa_round_rect(c, x - 8.0f * k, by - 22.0f * k, 16.0f * k, 3.0f * k, 1.5f * k, pa_shade(pa_hex(shirt), 0.25f));
    eye(c, x + 1.5f * k, by - 33.0f * k, 3.4f * k, 1.0f);
    eye(c, x + 8.0f * k, by - 33.5f * k, 2.8f * k, 1.0f);
    if (eyes) {
        pa_fill_circle(c, x + 2.0f * k, by - 33.0f * k, 1.8f * k, pa_hex(eyes));
        pa_fill_circle(c, x + 8.5f * k, by - 33.5f * k, 1.5f * k, pa_hex(eyes));
    }
    pa_line(c, x + 1.0f * k, by - 26.5f * k, x + 8.0f * k, by - 27.5f * k, 1.6f * k, pa_hex(0x1A1620));
    pa_line(c, x - 7.0f * k, by - 38.0f * k, x - 3.0f * k, by - 36.0f * k, 1.2f * k, pa_shade(pa_hex(skin), -0.35f));
}

static void art_zombie(PA_Canvas *c, float x, float y, float k, int f) {
    zombie_core(c, x, y, k, f, 0x8CC24C, 0x3D72C8, 0, 0);
}
static void art_elite(PA_Canvas *c, float x, float y, float k, int f) {
    zombie_core(c, x, y, k, f, 0xA27BC8, 0x2F2F3E, 0xFF2E3A, 1);
}

static void shell_core(PA_Canvas *c, float x, float y, float k, int f, uint32_t hi, uint32_t mid,
                       uint32_t lo) {
    float ph = (float)f * PA_TAU / (float)ANIM_FRAMES;
    float wob = sinf(ph) * 1.5f * k;
    soft_shadow(c, x, y, 13.0f * k, 4.5f * k);
    g_ow = 2.4f * k;
    for (g_pass = 0; g_pass < 2; g_pass++) {
        for (int i = 0; i < 3; i++) {
            float lx = x - 5.0f * k + (float)i * 6.0f * k;
            float sw = (i & 1) ? wob : -wob;
            P_line(c, lx, y - 7.0f * k, lx + sw + 1.5f * k, y - 1.0f * k, 2.4f * k, pa_hex(0xC8707C));
        }
        P_circle(c, x + 6.5f * k, y - 9.5f * k, 7.5f * k, pa_hex(0x3D2030));
        P_circle(c, x - 3.0f * k, y - 15.0f * k + wob * 0.3f, 11.5f * k, pa_hex(mid));
    }
    PA_Paint p = pa_radial(x - 6.0f * k, y - 19.0f * k, 1.0f * k, 13.0f * k);
    pa_stop(&p, 0.0f, pa_hex(hi));
    pa_stop(&p, 0.45f, pa_hex(mid));
    pa_stop(&p, 1.0f, pa_hex(lo));
    pa_fill_ellipse_paint(c, x - 3.0f * k, y - 15.0f * k + wob * 0.3f, 11.5f * k, 11.5f * k, &p);
    pa_fill_circle(c, x - 7.0f * k, y - 20.0f * k + wob * 0.3f, 2.6f * k, PA_RGBA(255, 255, 255, 170));
    pa_fill_circle(c, x + 4.5f * k, y - 11.0f * k, 2.6f * k, PA_RGB(240, 240, 236));
    pa_fill_circle(c, x + 10.0f * k, y - 10.5f * k, 2.2f * k, PA_RGB(240, 240, 236));
    pa_fill_circle(c, x + 5.0f * k, y - 11.0f * k, 1.1f * k, PA_RGB(20, 18, 26));
    pa_fill_circle(c, x + 10.4f * k, y - 10.5f * k, 1.0f * k, PA_RGB(20, 18, 26));
}
static void art_beetle(PA_Canvas *c, float x, float y, float k, int f) {
    shell_core(c, x, y, k, f, 0xFFE27A, 0xFF8F1C, 0xD9401A);
}
static void art_bluebug(PA_Canvas *c, float x, float y, float k, int f) {
    shell_core(c, x, y, k, f, 0xB4F4FF, 0x34A6F2, 0x1C4FC4);
}

static void art_fly(PA_Canvas *c, float x, float y, float k, int f) {
    float ph = (float)f * PA_TAU / (float)ANIM_FRAMES;
    float hov = sinf(ph) * 1.5f * k;
    float flap = (f & 1) ? 0.55f : 1.0f;
    float cy = y - 19.0f * k - hov;
    soft_shadow(c, x, y, 10.0f * k, 3.5f * k);
    g_ow = 2.4f * k;
    for (g_pass = 0; g_pass < 2; g_pass++) {
        P_ellipse(c, x - 4.0f * k, cy - 12.0f * k, 4.5f * k, 8.0f * k * flap, PA_RGB(250, 250, 255));
        P_ellipse(c, x + 2.0f * k, cy - 13.0f * k, 4.5f * k, 8.5f * k * flap, PA_RGB(250, 250, 255));
        P_ellipse(c, x - 1.0f * k, cy + 1.0f * k, 9.5f * k, 10.5f * k, pa_hex(0xDE2E98));
        P_circle(c, x + 7.0f * k, cy - 6.0f * k, 6.5f * k, pa_hex(0xD3DDB2));
        PA_Vec2 beak[3] = { { x + 12.0f * k, cy - 4.0f * k }, { x + 17.0f * k, cy - 1.5f * k },
                            { x + 11.0f * k, cy - 0.5f * k } };
        P_poly(c, beak, 3, pa_hex(0x9C9AA8));
    }
    pa_fill_ellipse(c, x - 4.0f * k, cy - 2.0f * k, 3.5f * k, 4.5f * k, pa_hex(0xF27AC2));
    pa_fill_circle(c, x + 6.0f * k, cy - 7.5f * k, 1.6f * k, PA_RGB(30, 26, 34));
    pa_fill_circle(c, x + 10.0f * k, cy - 7.0f * k, 1.4f * k, PA_RGB(30, 26, 34));
    pa_line(c, x - 3.0f * k, cy - 12.0f * k, x - 3.0f * k, cy - 16.0f * k, 0.8f * k, pa_hex(0xC8CCE0));
}

static void art_brute(PA_Canvas *c, float x, float y, float k, int f) {
    float ph = (float)f * PA_TAU / (float)ANIM_FRAMES;
    float bob = fabsf(sinf(ph)) * 1.8f * k;
    float step = sinf(ph) * 3.5f * k;
    float by = y - bob;
    soft_shadow(c, x, y, 16.0f * k, 5.0f * k);
    g_ow = 2.6f * k;
    for (g_pass = 0; g_pass < 2; g_pass++) {
        P_rrect(c, x - 9.0f * k + step, y - 12.0f * k, 8.0f * k, 12.0f * k, 3.0f * k, pa_hex(0x2C3350));
        P_rrect(c, x + 1.0f * k - step, y - 12.0f * k, 8.0f * k, 12.0f * k, 3.0f * k, pa_hex(0x2C3350));
        P_line(c, x + 4.0f * k, by - 24.0f * k, x + 16.0f * k, by - 22.0f * k - step * 0.4f, 6.0f * k, pa_hex(0x9AC254));
        P_rrect(c, x - 12.0f * k, by - 31.0f * k, 24.0f * k, 21.0f * k, 6.0f * k, pa_hex(0xF27A26));
        P_rrect(c, x - 11.0f * k, by - 48.0f * k, 22.0f * k, 20.0f * k, 7.0f * k, pa_hex(0x9AC254));
        P_ellipse(c, x, by - 46.0f * k, 13.0f * k, 8.5f * k, pa_hex(0xF6C02E));
        P_rrect(c, x - 15.0f * k, by - 46.0f * k, 30.0f * k, 4.5f * k, 2.0f * k, pa_hex(0xF6C02E));
    }
    pa_fill_ellipse(c, x - 3.0f * k, by - 49.0f * k, 5.0f * k, 3.0f * k, pa_hex(0xFFE58A));
    pa_fill_rect(c, x - 11.0f * k, by - 22.0f * k, 22.0f * k, 3.5f * k, pa_hex(0xE8EEF2));
    pa_fill_rect(c, x - 3.0f * k, by - 30.0f * k, 3.0f * k, 18.0f * k, pa_hex(0xE8EEF2));
    eye(c, x + 2.5f * k, by - 36.0f * k, 3.0f * k, 1.0f);
    eye(c, x + 8.5f * k, by - 36.0f * k, 2.6f * k, 1.0f);
    pa_line(c, x + 1.5f * k, by - 30.5f * k, x + 8.5f * k, by - 31.0f * k, 1.6f * k, pa_hex(0x1A1620));
}

static void art_spitter(PA_Canvas *c, float x, float y, float k, int f) {
    float ph = (float)f * PA_TAU / (float)ANIM_FRAMES;
    float pulse = 1.0f + sinf(ph) * 0.08f;
    float hx = x + 1.0f * k, hy = y - 28.0f * k;
    soft_shadow(c, x, y, 13.0f * k, 4.5f * k);
    g_ow = 2.5f * k;
    for (g_pass = 0; g_pass < 2; g_pass++) {
        P_ellipse(c, x, y - 10.0f * k, 10.0f * k, 10.0f * k, pa_hex(0x76B045));
        P_ellipse(c, x - 9.0f * k, y - 9.0f * k, 6.0f * k, 3.5f * k, pa_hex(0x5E9A38));
        for (int i = 0; i < 5; i++) {
            float a = -PA_PI * 0.5f + (float)i * PA_TAU / 5.0f;
            float pr = 10.5f * k * pulse;
            P_ellipse(c, hx + cosf(a) * pr, hy + sinf(a) * pr, 7.5f * k, 7.5f * k, pa_hex(0xF2649A));
        }
        P_circle(c, hx, hy, 9.5f * k, pa_hex(0xFFF3E2));
    }
    pa_fill_circle(c, hx, hy, 5.8f * k, pa_hex(0x8C1F3A));
    for (int i = 0; i < 4; i++) {
        float a = (float)i * PA_TAU / 4.0f + PA_PI * 0.25f;
        PA_Vec2 t[3] = { { hx + cosf(a) * 6.0f * k, hy + sinf(a) * 6.0f * k },
                         { hx + cosf(a + 0.5f) * 6.0f * k, hy + sinf(a + 0.5f) * 6.0f * k },
                         { hx + cosf(a + 0.25f) * 3.0f * k, hy + sinf(a + 0.25f) * 3.0f * k } };
        pa_fill_poly(c, t, 3, PA_RGB(255, 255, 255));
    }
    for (int i = 0; i < 5; i++) {
        float a = -PA_PI * 0.5f + (float)i * PA_TAU / 5.0f;
        pa_fill_circle(c, hx + cosf(a) * 11.5f * k, hy + sinf(a) * 11.5f * k, 2.0f * k, pa_hex(0xF79BC0));
    }
}

static void boss_core(PA_Canvas *c, float x, float y, float k, int f, int king) {
    float ph = (float)(f & 3) * PA_TAU / 4.0f;
    float bob = fabsf(sinf(ph)) * 3.0f * k;
    float step = sinf(ph) * 5.0f * k;
    float by = y - bob;
    uint32_t skin = king ? 0xD0566A : 0x86B04E;
    PA_Color sk = pa_hex(skin);
    soft_shadow(c, x, y, 40.0f * k, 12.0f * k);
    g_ow = 3.2f * k;
    for (g_pass = 0; g_pass < 2; g_pass++) {
        P_rrect(c, x - 26.0f * k + step, y - 26.0f * k, 18.0f * k, 26.0f * k, 6.0f * k, pa_hex(0x4E5664));
        P_rrect(c, x + 8.0f * k - step, y - 26.0f * k, 18.0f * k, 26.0f * k, 6.0f * k, pa_hex(0x4E5664));
        /* raised arm with the steel gauntlet */
        P_rrect(c, x + 24.0f * k, by - 92.0f * k + step, 16.0f * k, 34.0f * k, 7.0f * k, sk);
        P_rrect(c, x + 19.0f * k, by - 112.0f * k + step, 26.0f * k, 24.0f * k, 6.0f * k, pa_hex(0x8C92A2));
        /* hanging arm */
        P_rrect(c, x - 44.0f * k, by - 72.0f * k - step, 17.0f * k, 40.0f * k, 8.0f * k, sk);
        P_circle(c, x - 36.0f * k, by - 32.0f * k - step, 11.0f * k, sk);
        P_rrect(c, x - 34.0f * k, by - 84.0f * k, 66.0f * k, 62.0f * k, 18.0f * k, sk);
        P_rrect(c, x - 22.0f * k, by - 106.0f * k, 44.0f * k, 34.0f * k, 12.0f * k, sk);
        if (king) {
            PA_Vec2 cr[7] = { { x - 16.0f * k, by - 104.0f * k }, { x - 16.0f * k, by - 120.0f * k },
                              { x - 8.0f * k, by - 111.0f * k },  { x, by - 124.0f * k },
                              { x + 8.0f * k, by - 111.0f * k },  { x + 16.0f * k, by - 120.0f * k },
                              { x + 16.0f * k, by - 104.0f * k } };
            P_poly(c, cr, 7, pa_hex(0xFFC21C));
        }
    }
    /* belt, jaw plate and teeth, goggle eye, stitches */
    pa_round_rect(c, x - 34.0f * k, by - 40.0f * k, 66.0f * k, 9.0f * k, 3.0f * k, pa_hex(0x4A505E));
    for (int i = 0; i < 4; i++)
        pa_fill_circle(c, x - 24.0f * k + (float)i * 15.0f * k, by - 35.5f * k, 2.0f * k, pa_hex(0xB8BECB));
    o_rrect(c, x - 16.0f * k, by - 82.0f * k, 34.0f * k, 18.0f * k, 4.0f * k, pa_hex(0x9AA0AE), 2.2f * k);
    for (int i = 0; i < 5; i++) {
        float tx = x - 13.0f * k + (float)i * 6.8f * k;
        PA_Vec2 t[3] = { { tx, by - 82.0f * k }, { tx + 6.0f * k, by - 82.0f * k }, { tx + 3.0f * k, by - 74.0f * k } };
        pa_fill_poly(c, t, 3, PA_RGB(250, 250, 246));
    }
    pa_fill_rect(c, x - 14.0f * k, by - 70.0f * k, 30.0f * k, 2.5f * k, pa_hex(0x6E7482));
    eye(c, x - 6.0f * k, by - 94.0f * k, 7.0f * k, 1.0f);
    pa_stroke_circle(c, x - 6.0f * k, by - 94.0f * k, 8.5f * k, 2.0f * k, pa_hex(0x6E7482));
    eye(c, x + 12.0f * k, by - 93.0f * k, 4.0f * k, 1.0f);
    if (king) {
        pa_fill_circle(c, x - 4.5f * k, by - 93.5f * k, 2.5f * k, pa_hex(0xFF2E3A));
        pa_fill_circle(c, x + 13.0f * k, by - 92.5f * k, 1.6f * k, pa_hex(0xFF2E3A));
    }
    PA_Color dark = pa_shade(sk, -0.4f);
    pa_line(c, x + 4.0f * k, by - 106.0f * k, x + 12.0f * k, by - 100.0f * k, 2.0f * k, dark);
    pa_line(c, x + 6.0f * k, by - 105.0f * k, x + 9.0f * k, by - 108.0f * k, 1.6f * k, dark);
    pa_line(c, x + 9.0f * k, by - 103.0f * k, x + 12.0f * k, by - 106.0f * k, 1.6f * k, dark);
    pa_fill_ellipse(c, x - 18.0f * k, by - 66.0f * k, 9.0f * k, 5.0f * k, pa_shade(sk, 0.18f));
    pa_round_rect(c, x + 22.0f * k, by - 108.0f * k + step, 22.0f * k, 4.0f * k, 2.0f * k, pa_hex(0xC4C9D4));
    pa_fill_rect(c, x + 26.0f * k, by - 99.0f * k + step, 3.0f * k, 9.0f * k, pa_hex(0x6E7482));
    pa_fill_rect(c, x + 33.0f * k, by - 99.0f * k + step, 3.0f * k, 9.0f * k, pa_hex(0x6E7482));
}
static void art_boss(PA_Canvas *c, float x, float y, float k, int f) { boss_core(c, x, y, k, f, 0); }
static void art_king(PA_Canvas *c, float x, float y, float k, int f) { boss_core(c, x, y, k, f, 1); }

static void art_dog(PA_Canvas *c, float x, float y, float k, int f) {
    float ph = (float)f * PA_TAU / (float)ANIM_FRAMES;
    float run = sinf(ph) * 3.5f * k, bob = fabsf(cosf(ph)) * 1.5f * k;
    PA_Color fur = pa_hex(0x8E8EA2), dark = pa_hex(0x666678);
    soft_shadow(c, x, y, 14.0f * k, 4.5f * k);
    g_ow = 2.4f * k;
    for (g_pass = 0; g_pass < 2; g_pass++) {
        P_line(c, x - 9.0f * k, y - 10.0f * k, x - 9.0f * k - run, y - 1.0f * k, 3.4f * k, dark);
        P_line(c, x + 7.0f * k, y - 10.0f * k, x + 7.0f * k + run, y - 1.0f * k, 3.4f * k, dark);
        P_line(c, x - 11.0f * k, y - 16.0f * k - bob, x - 18.0f * k, y - 22.0f * k + run * 0.4f, 3.0f * k, fur);
        P_line(c, x - 5.0f * k, y - 10.0f * k, x - 5.0f * k + run, y - 1.0f * k, 3.4f * k, fur);
        P_line(c, x + 3.0f * k, y - 10.0f * k, x + 3.0f * k - run, y - 1.0f * k, 3.4f * k, fur);
        P_ellipse(c, x - 1.0f * k, y - 13.5f * k - bob, 12.5f * k, 7.0f * k, fur);
        PA_Vec2 ear[3] = { { x + 6.0f * k, y - 23.0f * k - bob }, { x + 8.0f * k, y - 31.0f * k - bob },
                           { x + 12.0f * k, y - 24.0f * k - bob } };
        P_poly(c, ear, 3, dark);
        P_circle(c, x + 10.0f * k, y - 19.0f * k - bob, 7.0f * k, fur);
        P_ellipse(c, x + 16.0f * k, y - 16.5f * k - bob, 5.0f * k, 3.6f * k, pa_hex(0xA4A4B6));
    }
    pa_fill_ellipse(c, x - 4.0f * k, y - 15.0f * k - bob, 5.0f * k, 3.5f * k, dark);
    pa_line(c, x + 5.0f * k, y - 14.0f * k - bob, x + 8.0f * k, y - 22.0f * k - bob, 2.2f * k, pa_hex(0xE8303A));
    pa_fill_circle(c, x + 11.5f * k, y - 21.0f * k - bob, 2.6f * k, PA_RGB(250, 250, 246));
    pa_fill_circle(c, x + 12.2f * k, y - 21.0f * k - bob, 1.3f * k, pa_hex(0xD8202E));
    pa_fill_circle(c, x + 20.5f * k, y - 17.5f * k - bob, 1.6f * k, OUTC());
    pa_line(c, x + 13.0f * k, y - 14.0f * k - bob, x + 19.0f * k, y - 14.5f * k - bob, 1.3f * k, OUTC());
}

/* Flame tongue: red rim, orange body, #FFB347 core. Four frames of flicker. */
static void flame_poly(PA_Canvas *c, float x, float y, float w, float h, float lean, PA_Color col) {
    PA_Vec2 p[13];
    for (int i = 0; i < 9; i++) {
        float a = (float)i / 8.0f * PA_PI;
        p[i].x = x + cosf(a) * w; p[i].y = y + sinf(a) * w * 0.75f;
    }
    p[9].x = x - w * 0.85f; p[9].y = y - h * 0.32f;
    p[10].x = x - w * 0.3f + lean * 0.5f; p[10].y = y - h * 0.72f;
    p[11].x = x + lean; p[11].y = y - h;
    p[12].x = x + w * 0.8f + lean * 0.3f; p[12].y = y - h * 0.38f;
    pa_fill_poly(c, p, 13, col);
}
static void art_flame(PA_Canvas *c, float x, float y, float k, int f) {
    float lean = sinf((float)f * 1.7f) * 3.0f * k;
    float h = (28.0f + (float)((f * 5) % 4)) * k;
    flame_poly(c, x, y, 9.0f * k, h, lean, pa_hex(0xE2401C));
    flame_poly(c, x, y + 0.5f * k, 6.6f * k, h * 0.74f, lean * 0.8f, pa_hex(0xFF8A1E));
    flame_poly(c, x, y + 1.0f * k, 4.2f * k, h * 0.46f, lean * 0.6f, pa_hex(0xFFB347));
    pa_fill_ellipse(c, x, y + 1.0f * k, 2.4f * k, 1.8f * k, pa_hex(0xFFF0B0));
}

/* White four-point impact star. */
static void art_spark(PA_Canvas *c, float x, float y, float k, int f) {
    (void)f;
    pa_fill_circle(c, x, y, 7.0f * k, PA_RGBA(255, 255, 255, 90));
    PA_Vec2 p[8];
    for (int i = 0; i < 8; i++) {
        float a = (float)i * PA_PI / 4.0f;
        float r = (i & 1) ? 3.0f * k : 11.0f * k;
        p[i].x = x + cosf(a) * r; p[i].y = y + sinf(a) * r;
    }
    pa_fill_poly(c, p, 8, PA_RGB(255, 255, 255));
}

/* Pickups. `kind` arrives in the frame slot. */
static void art_pickup(PA_Canvas *c, float x, float y, float k, int kind) {
    if (kind <= G_GOLD) {
        static const uint32_t hi[3] = { 0xC8F4FF, 0xC9FFB0, 0xFFF0A0 };
        static const uint32_t md[3] = { 0x34B8F8, 0x4CCB3A, 0xFFA81C };
        static const uint32_t lo[3] = { 0x1A6CD0, 0x238A2A, 0xD9661A };
        float s = (kind == G_GOLD ? 1.35f : kind == G_GREEN ? 1.12f : 1.0f) * k;
        PA_Vec2 d[4] = { { x, y - 7.5f * s }, { x + 5.5f * s, y }, { x, y + 7.5f * s }, { x - 5.5f * s, y } };
        o_poly(c, d, 4, pa_hex(md[kind]), 1.4f * k);
        PA_Vec2 tl[3] = { { x, y - 7.5f * s }, { x, y }, { x - 5.5f * s, y } };
        pa_fill_poly(c, tl, 3, pa_hex(hi[kind]));
        PA_Vec2 br[3] = { { x, y + 7.5f * s }, { x, y }, { x + 5.5f * s, y } };
        pa_fill_poly(c, br, 3, pa_hex(lo[kind]));
        return;
    }
    if (kind == G_COIN) {
        o_circle(c, x, y, 7.0f * k, pa_hex(0xFFC21C), 1.5f * k);
        pa_stroke_circle(c, x, y, 4.6f * k, 1.4f * k, pa_hex(0xE08A10));
        pa_fill_circle(c, x - 2.0f * k, y - 2.5f * k, 1.6f * k, pa_hex(0xFFF0B0));
    } else if (kind == G_MEAT) {
        g_ow = 1.6f * k;
        for (g_pass = 0; g_pass < 2; g_pass++) {
            P_line(c, x + 2.0f * k, y + 2.0f * k, x + 8.0f * k, y + 7.0f * k, 3.0f * k, pa_hex(0xF4EEE0));
            P_circle(c, x + 8.5f * k, y + 7.5f * k, 2.6f * k, pa_hex(0xF4EEE0));
            P_ellipse(c, x - 2.0f * k, y - 2.0f * k, 7.5f * k, 6.5f * k, pa_hex(0xC4583A));
        }
        pa_fill_ellipse(c, x - 4.0f * k, y - 4.0f * k, 3.0f * k, 2.2f * k, pa_hex(0xE88A68));
    } else if (kind == G_MAGNET) {
        g_ow = 1.5f * k;
        for (g_pass = 0; g_pass < 2; g_pass++) {
            PA_Vec2 arc[9];
            for (int i = 0; i < 9; i++) {
                float a = (float)i / 8.0f * PA_PI;
                arc[i].x = x + cosf(a) * 6.0f * k;
                arc[i].y = y + 1.0f * k + sinf(a) * 6.0f * k;
            }
            if (g_pass == 0) pa_stroke_poly(c, arc, 9, 0, 4.5f * k + g_ow * 2.0f, OUTC());
            else pa_stroke_poly(c, arc, 9, 0, 4.5f * k, pa_hex(0xE8303A));
            P_line(c, x - 6.0f * k, y - 7.0f * k, x - 6.0f * k, y + 1.0f * k, 4.5f * k, pa_hex(0xE8303A));
            P_line(c, x + 6.0f * k, y - 7.0f * k, x + 6.0f * k, y + 1.0f * k, 4.5f * k, pa_hex(0x2E6CE8));
        }
        pa_fill_rect(c, x - 8.2f * k, y - 8.0f * k, 4.4f * k, 3.0f * k, pa_hex(0xF2F2F2));
        pa_fill_rect(c, x + 3.8f * k, y - 8.0f * k, 4.4f * k, 3.0f * k, pa_hex(0xF2F2F2));
    } else if (kind == G_BOMB) {
        o_circle(c, x, y + 1.0f * k, 7.5f * k, pa_hex(0x34343E), 1.5f * k);
        pa_fill_circle(c, x - 2.5f * k, y - 1.5f * k, 2.0f * k, pa_hex(0x7A7A88));
        pa_line(c, x + 4.0f * k, y - 5.0f * k, x + 7.0f * k, y - 9.0f * k, 1.8f * k, pa_hex(0xC8A070));
        pa_fill_circle(c, x + 7.5f * k, y - 9.5f * k, 2.4f * k, pa_hex(0xFFC21C));
        pa_fill_circle(c, x + 7.5f * k, y - 9.5f * k, 1.2f * k, PA_RGB(255, 255, 255));
    } else if (kind == G_CHEST) {
        soft_shadow(c, x, y + 10.0f * k, 15.0f * k, 4.0f * k);
        o_rrect(c, x - 13.0f * k, y - 4.0f * k, 26.0f * k, 14.0f * k, 3.0f * k, pa_hex(0xE8A21C), 2.0f * k);
        o_rrect(c, x - 13.0f * k, y - 12.0f * k, 26.0f * k, 9.0f * k, 4.0f * k, pa_hex(0xFFC83A), 2.0f * k);
        pa_fill_rect(c, x - 3.0f * k, y - 12.0f * k, 6.0f * k, 22.0f * k, pa_hex(0xD8342E));
        o_rrect(c, x - 3.5f * k, y - 5.0f * k, 7.0f * k, 7.0f * k, 1.5f * k, pa_hex(0xFFE27A), 1.2f * k);
        pa_fill_rect(c, x - 11.0f * k, y - 10.5f * k, 6.0f * k, 2.0f * k, pa_hex(0xFFF0B0));
    }
}

/* Damage numerals are baked too: the swarm takes dozens of hits a second and
   stroking each popup's glyphs three times would cost more than the swarm. */
/* Styles: 0 hit, 1 crit, 2 HUD counter, 3 HUD timer, 4 HUD level. Glyph 10
   is the colon. */
enum { NUM_HIT, NUM_CRIT, NUM_HUD, NUM_TIMER, NUM_LEVEL, NUM_STYLES };
#define NUM_GLYPHS 11
static Sprite g_digit[NUM_STYLES][NUM_GLYPHS];
static const float NUM_SIZE[NUM_STYLES] = { 13.0f, 18.0f, 17.0f, 24.0f, 20.0f };
static const float NUM_WEIGHT[NUM_STYLES] = { 0.95f, 1.0f, 1.2f, 1.4f, 1.2f };
#define DIGIT_SIZE(style) (NUM_SIZE[style])

static void art_digit(PA_Canvas *c, float x, float y, float k, int f) {
    int st = f / NUM_GLYPHS, g = f % NUM_GLYPHS;
    char ch[2] = { g == 10 ? ':' : (char)('0' + g), 0 };
    pa_text_bold(c, ch, x, y, NUM_SIZE[st] * k, st == NUM_CRIT ? pa_hex(0xFFC21C) : PA_RGB(255, 255, 255), OUTC(),
                 PA_ALIGN_CENTER, 0.0f, NUM_WEIGHT[st]);
}

/** A numeral string from the baked glyphs. (x, y) is the top of the text. */
static float num_width(int st, const char *t) {
    float adv = NUM_SIZE[st] * L.s * 0.80f;
    int n = (int)strlen(t);
    return n > 0 ? adv * (float)n : 0.0f;
}
static void num_text(PA_Canvas *c, int st, const char *t, float x, float y, PA_Align al, uint32_t tint, int amt) {
    float adv = NUM_SIZE[st] * L.s * 0.80f;
    float w = num_width(st, t);
    float x0 = al == PA_ALIGN_LEFT ? x : al == PA_ALIGN_RIGHT ? x - w : x - w * 0.5f;
    for (int k = 0; t[k]; k++) {
        int g = t[k] == ':' ? 10 : t[k] - '0';
        if (g < 0 || g > 10) continue;
        float gx = x0 + adv * ((float)k + 0.5f);
        if (g == 10) gx = x0 + adv * ((float)k + 0.5f);
        spr_blit(c, &g_digit[st][g], gx, y, 0, tint, amt, 255);
    }
}

/* Sprite boxes in pixels at UI scale 1, the anchor, and the art scale. */
typedef struct { DrawFn fn; float w, h, ox, oy, k; } SprSpec;

static void bake_all(float s) {
    static const SprSpec specs[SPR_SETS] = {
        { art_hero,     84,  98, 42,  86, 1.25f },
        { art_zombie,   54,  64, 24,  58, 1.2f },
        { art_beetle,   52,  50, 23,  44, 1.2f },
        { art_fly,      56,  64, 25,  58, 1.2f },
        { art_brute,    62,  80, 28,  75, 1.2f },
        { art_spitter,  64,  72, 32,  66, 1.2f },
        { art_bluebug,  52,  50, 23,  44, 1.2f },
        { art_dog,      60,  54, 26,  46, 1.2f },
        { art_elite,   108, 126, 47, 116, 2.2f },
        { art_boss,    250, 230, 116, 218, 1.5f },
        { art_king,    270, 254, 126, 238, 1.6f },
    };
    for (int i = 0; i < SPR_SETS; i++) {
        const SprSpec *p = &specs[i];
        for (int f = 0; f < ANIM_FRAMES; f++)
            spr_bake(&g_spr[i][f], (int)ceilf(p->w * s), (int)ceilf(p->h * s), p->ox * s, p->oy * s,
                     p->k * s, f, p->fn);
    }
    for (int g = 0; g < G_KINDS; g++) {
        int sz = (int)ceilf((g == G_CHEST ? 40.0f : 26.0f) * s);
        spr_bake(&g_gem_spr[g], sz, sz, (float)sz * 0.5f, (float)sz * 0.5f, s, g, art_pickup);
    }
    for (int st = 0; st < NUM_STYLES; st++)
        for (int d = 0; d < NUM_GLYPHS; d++) {
            float size = DIGIT_SIZE(st) * s;
            int w = (int)ceilf(size * 1.5f), h = (int)ceilf(size * 1.8f);
            spr_bake(&g_digit[st][d], w, h, (float)w * 0.5f, size * 0.38f, s, d + st * NUM_GLYPHS, art_digit);
        }
    for (int f = 0; f < ANIM_FRAMES; f++) {
        int fw = (int)ceilf(24.0f * s), fh = (int)ceilf(40.0f * s);
        spr_bake(&g_flame[f], fw, fh, (float)fw * 0.5f, 33.0f * s, s, f, art_flame);
    }
    {
        int sz = (int)ceilf(26.0f * s);
        spr_bake(&g_spark, sz, sz, (float)sz * 0.5f, (float)sz * 0.5f, s, 0, art_spark);
    }
    for (int yy = 0; yy < GLOW_N; yy++)
        for (int xx = 0; xx < GLOW_N; xx++) {
            float dx = ((float)xx + 0.5f) / (float)GLOW_N * 2.0f - 1.0f, dy = ((float)yy + 0.5f) / (float)GLOW_N * 2.0f - 1.0f;
            float d = 1.0f - sqrtf(dx * dx + dy * dy);
            g_glow_lut[yy * GLOW_N + xx] = (uint8_t)(d > 0.0f ? d * d * 255.0f : 0.0f);
        }
    g_baked_scale = s;
}

/* ============================================================ helpers ==== */
static float frand(void) { return pa_rng_next(&S.rng); }
static float frange(float a, float b) { return pa_rng_range(&S.rng, a, b); }

static float view_hw(void) { return (float)L.w * 0.5f / L.s; }
static float view_hh(void) { return (float)L.h * 0.5f / L.s; }

static float dmg_mul(void)   { return (1.0f + 0.10f * (float)S.plv[P_POWER]) * (1.0f + 0.06f * (float)g_meta_atk); }
static float area_mul(void)  { return 1.0f + 0.10f * (float)S.plv[P_BRACER]; }
static float dur_mul(void)   { return 1.0f + 0.12f * (float)S.plv[P_FUEL]; }
static float cd_mul(void)    { return 1.0f - 0.07f * (float)S.plv[P_CUBE]; }
static float speed_mul(void) { return 1.0f + 0.08f * (float)S.plv[P_SHOES]; }
static float pickup_r(void)  { return 62.0f * (1.0f + 0.15f * (float)S.plv[P_FUEL]); }

static int xp_need(int lv) { return 4 + lv * 4 + lv * lv / 3; }

static float hp_mul(float t) {
    float q = t / 240.0f;
    return 1.0f + t / 60.0f * 0.7f + q * q * 2.3f;
}

/* ---------------------------------------------------------------- audio -- */
static void sfx_throw(void) {
    if (S.sfx_throw > 0.0f) return;
    S.sfx_throw = 0.07f;
    pa_tone(1500.0f, 700.0f, 0.04f, 2, 0.022f);
}
static void sfx_hit(void) {
    if (S.sfx_hit > 0.0f) return;
    S.sfx_hit = 0.05f;
    pa_noise(0.03f, 0.05f);
    pa_tone(240.0f, 110.0f, 0.05f, 0, 0.05f);
}
static void sfx_gem(void) {
    if (S.sfx_gem > 0.0f) return;
    S.sfx_gem = 0.035f;
    float f = 900.0f + (float)(S.gem_streak % 24) * 40.0f;
    pa_tone(f, f * 1.3f, 0.05f, 0, 0.035f);
    S.gem_streak++;
}

/* ------------------------------------------------------------ particles -- */
enum { PT_SPARK, PT_SMOKE, PT_FLAME, PT_BLOOD, PT_STAR, PT_HIT };

static void part(float x, float y, float vx, float vy, float life, float size, uint32_t col, int kind) {
    Part *p = &g_pt[g_pt_next];
    g_pt_next = (g_pt_next + 1) % MAX_PARTS;
    p->x = x; p->y = y; p->vx = vx; p->vy = vy;
    p->life = p->max = life; p->size = size; p->col = col; p->kind = kind;
}

static void burst(float x, float y, int n, float speed, float life, float size, uint32_t col, int kind) {
    for (int i = 0; i < n; i++) {
        float a = frand() * PA_TAU, s = speed * frange(0.35f, 1.0f);
        part(x, y, cosf(a) * s, sinf(a) * s, life * frange(0.6f, 1.0f), size * frange(0.7f, 1.2f), col, kind);
    }
}

static void num(float x, float y, int v, int crit) {
    Num *n = &g_num[g_num_next];
    g_num_next = (g_num_next + 1) % MAX_NUMS;
    n->x = x + frange(-6.0f, 6.0f); n->y = y; n->life = 0.7f; n->value = v; n->crit = crit;
}

static void decal(float x, float y, float r) {
    Decal *d = &g_dc[g_dc_next];
    g_dc_next = (g_dc_next + 1) % MAX_DECALS;
    d->x = x; d->y = y; d->r = r; d->seed = (int)(frand() * 1000.0f); d->age = 0.0f;
}

static void zone(int kind, float x, float y, float r, float life, float dps, int evo) {
    for (int i = 0; i < MAX_ZONES; i++) {
        Zone *z = &g_zn[i];
        if (z->alive) continue;
        memset(z, 0, sizeof(*z));
        z->alive = 1; z->kind = kind; z->x = x; z->y = y; z->r = r;
        z->life = z->max = life; z->dps = dps; z->evo = evo;
        return;
    }
}

static void banner(const char *text, int kind) {
    snprintf(S.banner, sizeof(S.banner), "%s", text);
    S.banner_kind = kind;
    S.banner_t = 3.0f;
    pa_tone(620.0f, 980.0f, 0.32f, 2, 0.06f);
}

/* --------------------------------------------------------------- grid ---- */
static void grid_build(void) {
    for (int i = 0; i < GRID_W * GRID_H; i++) g_grid_head[i] = -1;
    g_grid_x0 = S.px - GRID_CELL * (float)GRID_W * 0.5f;
    g_grid_y0 = S.py - GRID_CELL * (float)GRID_H * 0.5f;
    for (int i = 0; i < g_en_count; i++) {
        if (!g_en[i].alive) continue;
        int cx = (int)floorf((g_en[i].x - g_grid_x0) / GRID_CELL);
        int cy = (int)floorf((g_en[i].y - g_grid_y0) / GRID_CELL);
        if (cx < 0 || cy < 0 || cx >= GRID_W || cy >= GRID_H) { g_grid_next[i] = -2; continue; }
        int k = cy * GRID_W + cx;
        g_grid_next[i] = g_grid_head[k];
        g_grid_head[k] = i;
    }
}

/** Enemies whose body overlaps a circle at (x, y) of radius r. */
static int query(float x, float y, float r, int *out, int max) {
    int n = 0;
    float reach = r + 50.0f;
    int x0 = (int)floorf((x - reach - g_grid_x0) / GRID_CELL), x1 = (int)floorf((x + reach - g_grid_x0) / GRID_CELL);
    int y0 = (int)floorf((y - reach - g_grid_y0) / GRID_CELL), y1 = (int)floorf((y + reach - g_grid_y0) / GRID_CELL);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= GRID_W) x1 = GRID_W - 1;
    if (y1 >= GRID_H) y1 = GRID_H - 1;
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++)
            for (int i = g_grid_head[cy * GRID_W + cx]; i >= 0; i = g_grid_next[i]) {
                const Enemy *e = &g_en[i];
                if (!e->alive) continue;
                float dx = e->x - x, dy = e->y - y, rr = r + e->r;
                if (dx * dx + dy * dy < rr * rr && n < max) out[n++] = i;
            }
    return n;
}

static int nearest_enemy(float x, float y, float maxd, int skip_a, int skip_b) {
    int best = -1;
    float bd = maxd * maxd;
    for (int i = 0; i < g_en_count; i++) {
        const Enemy *e = &g_en[i];
        if (!e->alive || i == skip_a || i == skip_b) continue;
        float dx = e->x - x, dy = e->y - y, d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static int on_screen(float x, float y, float m) {
    return fabsf(x - S.cam_x) < view_hw() + m && fabsf(y - S.cam_y) < view_hh() + m;
}

/* -------------------------------------------------------------- enemies -- */
static int spawn_enemy(int type, float x, float y) {
    int i = -1;
    for (int k = 0; k < MAX_ENEMIES; k++) if (!g_en[k].alive) { i = k; break; }
    if (i < 0) return -1;
    Enemy *e = &g_en[i];
    memset(e, 0, sizeof(*e));
    const EnemyDef *d = &EDEF[type];
    e->alive = 1; e->type = (uint8_t)type;
    e->x = x; e->y = y;
    float m = (type >= E_BOSS) ? 1.0f : hp_mul(S.t);
    if (type == E_ELITE) m = 1.0f + S.t / 90.0f;
    e->hp = e->maxhp = d->hp * m;
    e->speed = d->speed * (type >= E_ELITE ? 1.0f : frange(0.88f, 1.12f)) * (1.0f + S.t / 1600.0f);
    e->dmg = d->dmg * (1.0f + S.t / 300.0f);
    e->r = d->r;
    e->anim = frand() * 4.0f;
    e->atk = frange(0.2f, 1.0f);
    e->elite = type >= E_ELITE;
    if (i >= g_en_count) g_en_count = i + 1;
    return i;
}

static void offscreen_point(float *x, float *y, float margin) {
    float hw = view_hw() + margin, hh = view_hh() + margin;
    float per = frand() * (hw + hh) * 4.0f;
    if (per < hw * 2.0f)            { *x = S.px - hw + per;            *y = S.py - hh; }
    else if ((per -= hw * 2.0f) < hh * 2.0f) { *x = S.px + hw;          *y = S.py - hh + per; }
    else if ((per -= hh * 2.0f) < hw * 2.0f) { *x = S.px + hw - per;    *y = S.py + hh; }
    else { per -= hw * 2.0f;          *x = S.px - hw;                   *y = S.py + hh - per; }
}

static int pick_type(void) {
    float t = S.t;
    float w[E_ELITE] = { 0 };
    /* Four silhouettes from the first wave: walker, shell bug, runner, flyer. */
    w[E_ZOMBIE]  = t < 240.0f ? 1.0f : 0.5f;
    w[E_BEETLE]  = 0.9f;
    w[E_DOG]     = t > 6.0f ? 0.55f : 0.0f;
    w[E_FLY]     = t > 16.0f ? (t > 70.0f ? 0.8f : 0.4f) : 0.0f;
    w[E_BRUTE]   = t > 110.0f ? 0.22f + t / 2400.0f : 0.0f;
    w[E_SPITTER] = t > 150.0f ? 0.14f : 0.0f;
    w[E_BLUEBUG] = t > 210.0f ? 0.9f : 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < E_ELITE; i++) sum += w[i];
    float r = frand() * sum;
    for (int i = 0; i < E_ELITE; i++) { r -= w[i]; if (r <= 0.0f && w[i] > 0.0f) return i; }
    return E_ZOMBIE;
}

static int alive_count(void) {
    int n = 0;
    for (int i = 0; i < g_en_count; i++) n += g_en[i].alive;
    return n;
}

/** A tight ring hugging the screen edge that closes on the hero from every
    side at once: the Survivor!.io encirclement. `rows` concentric bands. */
static void ring_wave(int type, int count, int rows, float speedup) {
    float rx = view_hw() + 30.0f, ry = view_hh() + 30.0f;
    for (int r = 0; r < rows; r++) {
        float k = 1.0f + 0.07f * (float)r;
        for (int i = 0; i < count; i++) {
            float a = ((float)i + 0.5f * (float)(r & 1)) / (float)count * PA_TAU;
            int e = spawn_enemy(type, S.px + cosf(a) * rx * k, S.py + sinf(a) * ry * k);
            if (e >= 0) g_en[e].speed *= speedup;
        }
    }
}

static void surge(int type, int count) {
    ring_wave(type, count / 2, 2, 1.1f);
    ring_wave(E_ZOMBIE, count / 3, 1, 1.0f);
}

static void spawn_boss(int king) {
    float x = S.px, y = S.py - 210.0f;
    S.cage = 1;
    S.cage_x = S.px; S.cage_y = S.py - 40.0f;
    S.cage_r = view_hw() * 0.96f;
    if (S.cage_r > 300.0f) S.cage_r = 300.0f;
    S.cage_t = 0.0f;
    /* The cage clears its own floor: anything inside goes up in smoke so the
       fight starts on an empty ring. */
    for (int i = 0; i < g_en_count; i++) {
        Enemy *e = &g_en[i];
        if (!e->alive || e->type >= E_ELITE) continue;
        float dx = e->x - S.cage_x, dy = e->y - S.cage_y;
        if (dx * dx + dy * dy < (S.cage_r + 30.0f) * (S.cage_r + 30.0f)) {
            e->alive = 0;
            burst(e->x, e->y - 10.0f, 4, 70.0f, 0.5f, 7.0f, 0xFFB8B4C8, PT_SMOKE);
        }
    }
    int b = spawn_enemy(king ? E_KING : E_BOSS, x, y);
    /* review capture of the win: the final boss arrives already beaten down */
    if (b >= 0 && king && pa_demo_mode() == 6) g_en[b].hp = g_en[b].maxhp * 0.08f;
    S.boss_idx = b;
    S.boss_kind = king ? E_KING : E_BOSS;
    if (b >= 0) { g_en[b].bstate = 4; g_en[b].bt = 0.9f; }
    S.shake = 0.6f;
    pa_tone(140.0f, 50.0f, 0.9f, 3, 0.14f);
    pa_noise(0.5f, 0.12f);
    burst(x, y - 40.0f, 30, 260.0f, 0.8f, 10.0f, 0xFFB8B4C8, PT_SMOKE);
}

typedef struct { float t; int kind, arg; } Event;
enum { EV_SURGE, EV_ELITE, EV_BOSS_WARN, EV_BOSS, EV_KING_WARN, EV_KING };
static const Event EVENTS[] = {
    {  55.0f, EV_SURGE, E_BEETLE },  {  95.0f, EV_ELITE, 1 },  { 150.0f, EV_SURGE, E_FLY },
    { 195.0f, EV_ELITE, 1 },         { 236.0f, EV_BOSS_WARN, 0 }, { 240.0f, EV_BOSS, 0 },
    { 300.0f, EV_SURGE, E_BLUEBUG }, { 335.0f, EV_ELITE, 2 },  { 380.0f, EV_SURGE, E_FLY },
    { 420.0f, EV_ELITE, 2 },         { 474.0f, EV_KING_WARN, 0 }, { 478.0f, EV_KING, 0 },
};
#define EVENT_COUNT ((int)(sizeof(EVENTS) / sizeof(EVENTS[0])))

static void run_events(void) {
    while (S.next_event < EVENT_COUNT && S.t >= EVENTS[S.next_event].t) {
        const Event *ev = &EVENTS[S.next_event++];
        float x, y;
        switch (ev->kind) {
        case EV_SURGE:
            banner("ZOMBIES INCOMING", 0);
            surge(ev->arg, 120 + (int)(S.t / 3.0f));
            break;
        case EV_ELITE:
            banner("ELITE INCOMING", 1);
            for (int i = 0; i < ev->arg; i++) {
                offscreen_point(&x, &y, 30.0f);
                spawn_enemy(E_ELITE, x, y);
            }
            break;
        case EV_BOSS_WARN: banner("BOSS INCOMING", 2); break;
        case EV_KING_WARN: banner("FINAL BOSS", 2); break;
        case EV_BOSS: if (S.boss_idx < 0) spawn_boss(0); break;
        case EV_KING:
            if (S.boss_idx >= 0 && g_en[S.boss_idx].alive) g_en[S.boss_idx].alive = 0;
            spawn_boss(1);
            break;
        }
    }
}

static void spawner(float dt) {
    float target = 60.0f + S.t * 1.5f;
    if (target > 720.0f) target = 720.0f;
    if (S.cage) target *= 0.45f;
    float rate = 9.0f + S.t * 0.16f;
    /* Every 16 s a closing ring of the current mix, on top of the trickle. */
    if (!S.cage && S.t > 24.0f && (int)(S.t / 16.0f) != (int)((S.t - dt) / 16.0f) && alive_count() < 820) {
        int n = 28 + (int)(S.t * 0.22f);
        if (n > 110) n = 110;
        ring_wave(pick_type(), n, S.t > 150.0f ? 2 : 1, 1.05f);
    }
    S.spawn_acc += dt * rate;
    int alive = alive_count();
    while (S.spawn_acc >= 1.0f) {
        S.spawn_acc -= 1.0f;
        if ((float)alive >= target) continue;
        float x, y;
        offscreen_point(&x, &y, 36.0f);
        if (S.cage) {
            float cx = x - S.cage_x, cy = y - S.cage_y;
            if (cx * cx + cy * cy < (S.cage_r + 40.0f) * (S.cage_r + 40.0f)) continue;
        }
        if (spawn_enemy(pick_type(), x, y) >= 0) alive++;
    }
}

/* ------------------------------------------------------------ pickups ---- */
static void drop_gem(float x, float y, int kind, int value) {
    int slot = -1;
    for (int i = 0; i < MAX_GEMS; i++) if (!g_gm[i].alive) { slot = i; break; }
    if (slot < 0) {
        /* Full: fold the value into the oldest plain gem so nothing is lost. */
        for (int i = 0; i < MAX_GEMS; i++) if (g_gm[i].kind <= G_GOLD && kind <= G_GOLD) {
            g_gm[i].value += value;
            if (g_gm[i].value >= 10) g_gm[i].kind = G_GOLD;
            else if (g_gm[i].value >= 3) g_gm[i].kind = G_GREEN;
            return;
        }
        return;
    }
    Gem *g = &g_gm[slot];
    memset(g, 0, sizeof(*g));
    g->alive = 1; g->x = x; g->y = y; g->kind = kind; g->value = value;
    float a = frand() * PA_TAU;
    g->vx = cosf(a) * 40.0f; g->vy = sinf(a) * 40.0f;
    if (slot >= g_gem_count) g_gem_count = slot + 1;
}

/* --------------------------------------------------------------- combat -- */
static void heal(float amount) {
    float before = S.hp;
    S.hp = S.hp + amount > S.maxhp ? S.maxhp : S.hp + amount;
    if (S.hp - before >= 1.0f) {
        burst(S.px, S.py - 30.0f, 8, 70.0f, 0.6f, 4.0f, 0xFF6EF08C, PT_STAR);
    }
}

static void hurt_player(float dmg) {
    if (S.invuln > 0.0f || S.phase != PH_PLAY) return;
    S.hp -= dmg;
    S.hurt = 0.25f;
    S.invuln = 0.10f;
    S.shake = S.shake > 0.18f ? S.shake : 0.18f;
    if (S.sfx_hit <= 0.0f) pa_tone(320.0f, 120.0f, 0.12f, 3, 0.07f);
    if (S.hp <= 0.0f) {
        S.hp = 0.0f;
        S.phase = PH_DYING;
        S.phase_t = 0.0f;
        S.flash = 0.6f; S.flash_col = 0xFFFF3040;
        burst(S.px, S.py - 24.0f, 26, 200.0f, 0.9f, 6.0f, 0xFFD01828, PT_BLOOD);
        pa_sfx("lose");
    }
}

static void boss_down(Enemy *e) {
    S.bosses_down++;
    S.cage = 0;
    S.boss_idx = -1;
    S.shake = 0.9f;
    S.flash = 0.7f; S.flash_col = 0xFFFFFFFF;
    burst(e->x, e->y - 50.0f, 60, 380.0f, 1.2f, 9.0f, 0xFFFFB21E, PT_SPARK);
    burst(e->x, e->y - 50.0f, 30, 220.0f, 1.4f, 16.0f, 0xFFB8B4C8, PT_SMOKE);
    zone(Z_RING, e->x, e->y - 40.0f, 260.0f, 0.6f, 0.0f, 1);
    pa_sfx("boom");
    pa_sfx("win");
    if (e->type == E_KING) {
        S.won = 1;
        S.phase = PH_DYING;
        S.phase_t = 0.0f;
        return;
    }
    for (int i = 0; i < 14; i++) drop_gem(e->x + frange(-50, 50), e->y + frange(-40, 40), G_GOLD, 12);
    drop_gem(e->x, e->y, G_CHEST, 3);
    banner("BOSS DEFEATED", 3);
}

static void kill_enemy(int i) {
    Enemy *e = &g_en[i];
    if (!e->alive) return;
    e->alive = 0;
    S.kills++;
    float cy = e->y - e->r;
    burst(e->x, cy, e->elite ? 22 : 6, e->elite ? 220.0f : 120.0f, 0.45f, e->elite ? 6.0f : 4.0f, 0xFFD8202E, PT_BLOOD);
    decal(e->x, e->y - 2.0f, e->r * (e->elite ? 1.6f : 1.0f));
    if (e->type == E_BOSS || e->type == E_KING) { boss_down(e); return; }
    if (e->type == E_ELITE) {
        for (int k = 0; k < 6; k++) drop_gem(e->x + frange(-26, 26), e->y + frange(-20, 20), G_GOLD, 10);
        drop_gem(e->x, e->y, G_CHEST, 1);
        pa_sfx("boom");
        S.shake = 0.35f;
        return;
    }
    int xp = EDEF[e->type].xp;
    drop_gem(e->x, e->y - 4.0f, xp >= 10 ? G_GOLD : xp >= 3 ? G_GREEN : G_BLUE, xp);
    float r = frand();
    if (r < 0.0035f) drop_gem(e->x + 8.0f, e->y, G_MAGNET, 0);
    else if (r < 0.0060f) drop_gem(e->x + 8.0f, e->y, G_BOMB, 0);
    else if (r < 0.0120f) drop_gem(e->x + 8.0f, e->y, G_MEAT, 0);
    else if (r < 0.0300f) drop_gem(e->x + 8.0f, e->y, G_COIN, 5);
}

static void hurt_enemy(int i, float dmg, float kx, float ky, int wid) {
    Enemy *e = &g_en[i];
    if (!e->alive) return;
    int crit = wid >= 0 && frand() < 0.08f + 0.06f * (float)S.plv[P_RAGE];
    if (crit) dmg *= 2.0f;
    e->hp -= dmg;
    e->flash = 0.12f;
    if (frand() < 0.5f) part(e->x + frange(-6, 6), e->y - e->r * 1.3f, 0, 0, 0.12f, 1.0f, 0xFFFFFFFF, PT_HIT);
    float kb = e->type >= E_BOSS ? 0.0f : e->type == E_ELITE ? 0.25f : e->type == E_BRUTE ? 0.6f : 1.0f;
    e->kx += kx * kb; e->ky += ky * kb;
    if (wid >= 0 || e->type >= E_ELITE)
        num(e->x, e->y - e->r * 2.6f - (e->type >= E_ELITE ? 30.0f : 0.0f), (int)(dmg + 0.5f), crit);
    sfx_hit();
    if (e->hp <= 0.0f) kill_enemy(i);
}

static void blast(float x, float y, float r, float dmg, int wid) {
    int hits[256];
    int n = query(x, y, r, hits, 256);
    for (int k = 0; k < n; k++) {
        Enemy *e = &g_en[hits[k]];
        float dx = e->x - x, dy = e->y - y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
        hurt_enemy(hits[k], dmg, dx / d * 120.0f, dy / d * 120.0f, wid);
    }
}

/* -------------------------------------------------------------- weapons -- */
static Proj *new_proj(int kind) {
    for (int i = 0; i < MAX_PROJ; i++) {
        if (g_pj[i].alive) continue;
        memset(&g_pj[i], 0, sizeof(Proj));
        g_pj[i].alive = 1;
        g_pj[i].kind = kind;
        return &g_pj[i];
    }
    return NULL;
}

static void guardian_pos(int i, int n, float *x, float *y) {
    float rad = (S.evo[W_GUARD] ? 84.0f : 66.0f) * area_mul();
    float a = S.guard_ang + (float)i * PA_TAU / (float)n;
    *x = S.px + cosf(a) * rad;
    *y = S.py - 20.0f + sinf(a) * rad;
}
static int guardian_count(void) {
    static const int n[MAX_LV] = { 2, 2, 3, 3, 4 };
    if (!S.wlv[W_GUARD]) return 0;
    return S.evo[W_GUARD] ? 6 : n[S.wlv[W_GUARD] - 1];
}
static int guardians_active(void) {
    if (S.evo[W_GUARD]) return 1;
    return S.guard_cycle < 4.0f * dur_mul();
}

static float field_radius(void) {
    static const float r[MAX_LV] = { 128, 128, 148, 148, 168 };
    if (!S.wlv[W_FIELD]) return 0.0f;
    return (S.evo[W_FIELD] ? 190.0f : r[S.wlv[W_FIELD] - 1]) * area_mul();
}

static int drone_count(void) {
    if (!S.wlv[W_DRONE]) return 0;
    return (S.evo[W_DRONE] || S.wlv[W_DRONE] >= 3) ? 2 : 1;
}
static void drone_pos(int i, float *x, float *y) {
    float a = S.drone_ang + (float)i * PA_PI;
    *x = S.px + cosf(a) * 46.0f;
    *y = S.py - 52.0f + sinf(a) * 16.0f;
}

static void fire_weapons(float dt) {
    float cdm = cd_mul(), dm = dmg_mul(), am = area_mul(), du = dur_mul();
    for (int w = 0; w < W_COUNT; w++) if (S.wlv[w] && S.cd[w] > 0.0f) S.cd[w] -= dt;

    /* Kunai: a fan at the nearest enemy. */
    int lv = S.wlv[W_KUNAI];
    if (lv && S.cd[W_KUNAI] <= 0.0f) {
        int t = nearest_enemy(S.px, S.py - 20.0f, 460.0f, -1, -1);
        if (t >= 0) {
            static const int cnt[MAX_LV] = { 1, 2, 2, 3, 4 };
            static const float dmg[MAX_LV] = { 14, 16, 21, 23, 27 };
            int ev = S.evo[W_KUNAI];
            int n = ev ? 5 : cnt[lv - 1];
            float base = atan2f(g_en[t].y - g_en[t].r - (S.py - 20.0f), g_en[t].x - S.px);
            for (int k = 0; k < n; k++) {
                Proj *p = new_proj(PJ_KUNAI);
                if (!p) break;
                float a = base + ((float)k - (float)(n - 1) * 0.5f) * (ev ? 0.16f : 0.11f);
                float sp = ev ? 640.0f : 560.0f;
                p->x = S.px + cosf(a) * 10.0f; p->y = S.py - 20.0f + sinf(a) * 10.0f;
                p->vx = cosf(a) * sp; p->vy = sinf(a) * sp;
                p->life = 0.9f; p->dmg = (ev ? 36.0f : dmg[lv - 1]) * dm; p->r = 8.0f;
                p->pierce = ev ? 3 : 0; p->evo = ev; p->ang = a;
            }
            S.cd[W_KUNAI] = (ev ? 0.34f : 0.80f) * cdm;
            S.face = g_en[t].x >= S.px ? 1 : -1;
            S.muzzle = 0.08f;
            S.muzzle_ang = base;
            sfx_throw();
        } else S.cd[W_KUNAI] = 0.1f;
    }

    /* Guardians orbit; contact is resolved in the projectile pass. */
    lv = S.wlv[W_GUARD];
    if (lv) {
        S.guard_ang += dt * (S.evo[W_GUARD] ? 4.2f : (lv >= 4 ? 3.8f : 3.1f));
        S.guard_cycle += dt;
        if (S.guard_cycle > 4.0f * du + 2.4f * cdm) S.guard_cycle = 0.0f;
        if (guardians_active()) {
            static const float dmg[MAX_LV] = { 12, 16, 17, 22, 25 };
            float d = (S.evo[W_GUARD] ? 40.0f : dmg[lv - 1]) * dm;
            float gr = (S.evo[W_GUARD] ? 17.0f : 13.0f) * am;
            int n = guardian_count();
            for (int k = 0; k < n; k++) {
                float gx, gy, hits_n;
                int hits[64];
                guardian_pos(k, n, &gx, &gy);
                hits_n = (float)query(gx, gy + 12.0f, gr, hits, 64);
                for (int h = 0; h < (int)hits_n; h++) {
                    Enemy *e = &g_en[hits[h]];
                    if (e->imm[W_GUARD] > 0.0f) continue;
                    e->imm[W_GUARD] = 0.4f;
                    float dx = e->x - S.px, dy = e->y - S.py, dl = sqrtf(dx * dx + dy * dy) + 0.01f;
                    hurt_enemy(hits[h], d, dx / dl * 220.0f, dy / dl * 220.0f, W_GUARD);
                    part(gx, gy, frange(-80, 80), frange(-80, 80), 0.2f, 3.0f, 0xFFFFE070, PT_SPARK);
                }
            }
        }
    }

    /* Drones fire homing rockets. */
    lv = S.wlv[W_DRONE];
    if (lv) {
        S.drone_ang += dt * 1.4f;
        if (S.cd[W_DRONE] <= 0.0f) {
            static const float dmg[MAX_LV] = { 22, 26, 28, 36, 40 };
            static const float cd[MAX_LV] = { 1.5f, 1.25f, 1.25f, 1.1f, 0.95f };
            int ev = S.evo[W_DRONE];
            int fired = 0;
            for (int d = 0; d < drone_count(); d++) {
                float dx, dy;
                drone_pos(d, &dx, &dy);
                int salvo = ev ? 3 : 1;
                for (int s = 0; s < salvo; s++) {
                    int t = nearest_enemy(dx, dy, 480.0f, -1, -1);
                    if (t < 0) break;
                    Proj *p = new_proj(PJ_ROCKET);
                    if (!p) break;
                    float a = atan2f(g_en[t].y - dy, g_en[t].x - dx) + frange(-0.9f, 0.9f);
                    p->x = dx; p->y = dy; p->vx = cosf(a) * 200.0f; p->vy = sinf(a) * 200.0f;
                    p->life = 2.0f; p->dmg = (ev ? 58.0f : dmg[lv - 1]) * dm;
                    p->r = (ev ? 84.0f : (lv >= 5 ? 58.0f : 48.0f)) * am;
                    p->evo = ev; p->pierce = t;
                    fired = 1;
                }
            }
            S.cd[W_DRONE] = fired ? (ev ? 0.8f : cd[lv - 1]) * cdm : 0.2f;
            if (fired) pa_tone(500.0f, 900.0f, 0.08f, 3, 0.03f);
        }
    }

    /* Lightning: strikes random enemies on screen. */
    lv = S.wlv[W_BOLT];
    if (lv && S.cd[W_BOLT] <= 0.0f) {
        static const int cnt[MAX_LV] = { 1, 2, 2, 3, 4 };
        static const float dmg[MAX_LV] = { 30, 34, 44, 48, 54 };
        static const float cd[MAX_LV] = { 1.8f, 1.7f, 1.6f, 1.5f, 1.25f };
        int ev = S.evo[W_BOLT];
        int n = ev ? 7 : cnt[lv - 1];
        int struck = 0;
        for (int k = 0; k < n; k++) {
            int pick = -1;
            for (int tries = 0; tries < 24 && pick < 0; tries++) {
                int i = (int)(frand() * (float)g_en_count);
                if (i < g_en_count && g_en[i].alive && on_screen(g_en[i].x, g_en[i].y, -20.0f)) pick = i;
            }
            if (pick < 0) break;
            float x = g_en[pick].x, y = g_en[pick].y;
            float r = (ev ? 64.0f : 42.0f) * am;
            zone(Z_BOLT, x, y, r, 0.32f, 0.0f, ev);
            blast(x, y - 8.0f, r, (ev ? 82.0f : dmg[lv - 1]) * dm, W_BOLT);
            burst(x, y - 6.0f, 6, 160.0f, 0.3f, 3.0f, 0xFFB8F0FF, PT_SPARK);
            struck++;
        }
        S.cd[W_BOLT] = struck ? (ev ? 0.85f : cd[lv - 1]) * cdm : 0.25f;
        if (struck) { pa_noise(0.10f, 0.08f); pa_tone(1900.0f, 260.0f, 0.14f, 3, 0.04f); }
    }

    /* Bricks: lobbed up, fall through everything. */
    lv = S.wlv[W_BRICK];
    if (lv && S.cd[W_BRICK] <= 0.0f) {
        static const int cnt[MAX_LV] = { 1, 1, 2, 2, 3 };
        static const float dmg[MAX_LV] = { 45, 60, 64, 80, 92 };
        int ev = S.evo[W_BRICK];
        int n = ev ? 4 : cnt[lv - 1];
        for (int k = 0; k < n; k++) {
            Proj *p = new_proj(PJ_BRICK);
            if (!p) break;
            p->x = S.px; p->y = S.py - 30.0f;
            p->vx = frange(-150.0f, 150.0f); p->vy = frange(-560.0f, -470.0f);
            p->life = 1.7f; p->dmg = (ev ? 150.0f : dmg[lv - 1]) * dm;
            p->r = (ev ? 18.0f : 12.0f) * am; p->evo = ev;
            p->spin = frange(-10.0f, 10.0f);
        }
        S.cd[W_BRICK] = (ev ? 1.2f : 1.6f) * cdm;
        pa_tone(300.0f, 520.0f, 0.06f, 1, 0.04f);
    }

    /* Soccer balls bounce off the edges of the screen. */
    lv = S.wlv[W_BALL];
    if (lv && S.cd[W_BALL] <= 0.0f) {
        static const int cnt[MAX_LV] = { 1, 1, 2, 2, 3 };
        static const float dmg[MAX_LV] = { 16, 21, 22, 26, 30 };
        int ev = S.evo[W_BALL];
        int n = ev ? 3 : cnt[lv - 1];
        for (int k = 0; k < n; k++) {
            Proj *p = new_proj(PJ_BALL);
            if (!p) break;
            float a = frand() * PA_TAU;
            float sp = ev ? 400.0f : 320.0f;
            p->x = S.px; p->y = S.py - 16.0f;
            p->vx = cosf(a) * sp; p->vy = sinf(a) * sp;
            p->life = (ev ? 6.0f : (lv >= 4 ? 4.2f : 3.2f)) * du;
            p->dmg = (ev ? 46.0f : dmg[lv - 1]) * dm; p->r = (ev ? 15.0f : 11.0f) * am; p->evo = ev;
        }
        S.cd[W_BALL] = 2.7f * cdm;
        pa_tone(220.0f, 330.0f, 0.06f, 0, 0.05f);
    }

    /* Forcefield: a ring of fire round the hero that scorches everything in it. */
    lv = S.wlv[W_FIELD];
    if (S.field_pulse > 0.0f) S.field_pulse -= dt * 3.0f;
    if (lv && S.cd[W_FIELD] <= 0.0f) {
        static const float dmg[MAX_LV] = { 6, 8, 9, 11, 14 };
        int ev = S.evo[W_FIELD];
        float r = field_radius();
        int hits[320];
        int n = query(S.px, S.py - 10.0f, r, hits, 320);
        for (int k = 0; k < n; k++) {
            Enemy *e = &g_en[hits[k]];
            float dx = e->x - S.px, dy = e->y - S.py, dl = sqrtf(dx * dx + dy * dy) + 0.01f;
            hurt_enemy(hits[k], (ev ? 28.0f : dmg[lv - 1]) * dm, dx / dl * 60.0f, dy / dl * 60.0f, W_FIELD);
            if (k < 10) part(e->x, e->y - 12.0f, frange(-30, 30), -60.0f, 0.45f, 6.0f, 0xFFFF8A1E, PT_FLAME);
        }
        S.cd[W_FIELD] = (ev ? 0.38f : (lv >= 4 ? 0.5f : 0.65f)) * cdm;
        S.field_pulse = 1.0f;
    }

    /* Molotovs: arcing bottles that leave fire on the ground. */
    lv = S.wlv[W_FIRE];
    if (lv && S.cd[W_FIRE] <= 0.0f) {
        static const int cnt[MAX_LV] = { 1, 1, 2, 2, 3 };
        int ev = S.evo[W_FIRE];
        int n = ev ? 3 : cnt[lv - 1];
        for (int k = 0; k < n; k++) {
            Proj *p = new_proj(PJ_BOTTLE);
            if (!p) break;
            int t = -1;
            for (int tries = 0; tries < 16 && t < 0; tries++) {
                int i = (int)(frand() * (float)g_en_count);
                if (i < g_en_count && g_en[i].alive && on_screen(g_en[i].x, g_en[i].y, -60.0f)) t = i;
            }
            float tx = t >= 0 ? g_en[t].x : S.px + frange(-160, 160);
            float ty = t >= 0 ? g_en[t].y : S.py + frange(-200, 200);
            p->x = S.px; p->y = S.py - 20.0f; p->tx = tx; p->ty = ty;
            p->life = 0.6f; p->age = 0.0f; p->evo = ev; p->spin = 12.0f;
        }
        S.cd[W_FIRE] = (ev ? 2.4f : 2.8f) * cdm;
    }
}

static void update_projectiles(float dt) {
    float hw = view_hw(), hh = view_hh();
    int hits[64];
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *p = &g_pj[i];
        if (!p->alive) continue;
        p->age += dt;
        p->life -= dt;
        switch (p->kind) {
        case PJ_KUNAI: {
            p->x += p->vx * dt; p->y += p->vy * dt;
            if (p->evo && ((int)(p->age * 60.0f) & 1))
                part(p->x, p->y, 0, 0, 0.18f, 4.0f, 0xFF9A5CF0, PT_SMOKE);
            int n = query(p->x, p->y + 10.0f, p->r, hits, 64);
            for (int h = 0; h < n && p->alive; h++) {
                Enemy *e = &g_en[hits[h]];
                if (e->imm[W_KUNAI] > 0.0f) continue;
                e->imm[W_KUNAI] = 0.15f;
                float sp = sqrtf(p->vx * p->vx + p->vy * p->vy) + 0.01f;
                hurt_enemy(hits[h], p->dmg, p->vx / sp * 70.0f, p->vy / sp * 70.0f, W_KUNAI);
                part(p->x, p->y, -p->vx * 0.1f, -p->vy * 0.1f, 0.18f, 3.0f, 0xFFFFFFFF, PT_SPARK);
                if (p->pierce-- <= 0) p->alive = 0;
            }
            break;
        }
        case PJ_ROCKET: {
            int t = p->pierce;
            if (t < 0 || t >= g_en_count || !g_en[t].alive) { t = nearest_enemy(p->x, p->y, 400.0f, -1, -1); p->pierce = t; }
            float sp = 200.0f + p->age * 700.0f;
            if (sp > 520.0f) sp = 520.0f;
            if (t >= 0) {
                float dx = g_en[t].x - p->x, dy = g_en[t].y - g_en[t].r - p->y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
                float steer = 1.0f - expf(-7.0f * dt);
                p->vx += (dx / d * sp - p->vx) * steer;
                p->vy += (dy / d * sp - p->vy) * steer;
            }
            p->x += p->vx * dt; p->y += p->vy * dt;
            if (((int)(p->age * 90.0f) & 1)) part(p->x, p->y, frange(-10, 10), frange(-10, 10), 0.35f, 4.5f, 0xFFF4F2F8, PT_SMOKE);
            int n = query(p->x, p->y + 8.0f, 6.0f, hits, 4);
            if (n > 0 || p->life <= 0.0f) {
                blast(p->x, p->y + 8.0f, p->r, p->dmg, W_DRONE);
                zone(Z_BLAST, p->x, p->y + 8.0f, p->r, 0.35f, 0.0f, p->evo);
                burst(p->x, p->y, 8, 150.0f, 0.4f, 6.0f, 0xFFFFB21E, PT_FLAME);
                if (S.sfx_throw <= 0.0f) { pa_noise(0.12f, 0.07f); S.sfx_throw = 0.05f; }
                p->alive = 0;
            }
            break;
        }
        case PJ_BRICK: {
            p->vy += 1050.0f * dt;
            p->x += p->vx * dt; p->y += p->vy * dt;
            p->ang += p->spin * dt;
            int n = query(p->x, p->y + 10.0f, p->r, hits, 64);
            for (int h = 0; h < n; h++) {
                Enemy *e = &g_en[hits[h]];
                if (e->imm[W_BRICK] > 0.0f) continue;
                e->imm[W_BRICK] = 0.6f;
                hurt_enemy(hits[h], p->dmg, p->vx * 0.2f, 60.0f, W_BRICK);
                burst(p->x, p->y, 3, 120.0f, 0.3f, 3.5f, 0xFFC8643C, PT_SPARK);
            }
            break;
        }
        case PJ_BALL: {
            p->x += p->vx * dt; p->y += p->vy * dt;
            p->ang += dt * 9.0f;
            float lx = S.cam_x - hw + p->r, rx = S.cam_x + hw - p->r;
            float ty = S.cam_y - hh + p->r + 120.0f / L.s, by = S.cam_y + hh - p->r;
            if (p->x < lx) { p->x = lx; p->vx = fabsf(p->vx); }
            if (p->x > rx) { p->x = rx; p->vx = -fabsf(p->vx); }
            if (p->y < ty) { p->y = ty; p->vy = fabsf(p->vy); }
            if (p->y > by) { p->y = by; p->vy = -fabsf(p->vy); }
            if (p->evo && ((int)(p->age * 40.0f) & 1)) part(p->x, p->y, 0, 0, 0.25f, p->r * 0.8f, 0xFF7AD8FF, PT_SMOKE);
            int n = query(p->x, p->y + 8.0f, p->r, hits, 64);
            for (int h = 0; h < n; h++) {
                Enemy *e = &g_en[hits[h]];
                if (e->imm[W_BALL] > 0.0f) continue;
                e->imm[W_BALL] = 0.45f;
                float sp = sqrtf(p->vx * p->vx + p->vy * p->vy) + 0.01f;
                hurt_enemy(hits[h], p->dmg, p->vx / sp * 160.0f, p->vy / sp * 160.0f, W_BALL);
            }
            break;
        }
        case PJ_BOTTLE: {
            float k = pa_clamp01(p->age / 0.6f);
            float sx = S.px, sy = S.py - 20.0f;
            (void)sx; (void)sy;
            p->ang += p->spin * dt;
            if (k >= 1.0f) {
                float r = (p->evo ? 74.0f : 34.0f + 4.0f * (float)S.wlv[W_FIRE]) * area_mul();
                static const float dps[MAX_LV] = { 15, 19, 21, 25, 29 };
                float d = (p->evo ? 50.0f : dps[S.wlv[W_FIRE] - 1]) * dmg_mul();
                zone(Z_FIRE, p->tx, p->ty, r, (p->evo ? 5.0f : 3.2f) * dur_mul(), d, p->evo);
                burst(p->tx, p->ty, 10, 120.0f, 0.4f, 5.0f, 0xFF2EA84A, PT_SPARK);
                pa_noise(0.18f, 0.06f);
                p->alive = 0;
            }
            break;
        }
        }
        if (p->life <= 0.0f && p->kind != PJ_BOTTLE) p->alive = 0;
    }
}

static void update_zones(float dt) {
    int hits[256];
    for (int i = 0; i < MAX_ZONES; i++) {
        Zone *z = &g_zn[i];
        if (!z->alive) continue;
        z->life -= dt;
        if (z->life <= 0.0f) { z->alive = 0; continue; }
        if (z->kind == Z_FIRE) {
            z->tick -= dt;
            if (z->tick <= 0.0f) {
                z->tick = 0.3f;
                int n = query(z->x, z->y, z->r * 0.9f, hits, 256);
                for (int k = 0; k < n; k++) hurt_enemy(hits[k], z->dps * 0.3f, 0.0f, 0.0f, W_FIRE);
            }
            if (frand() < dt * 18.0f) {
                float a = frand() * PA_TAU, rr = frand() * z->r * 0.8f;
                part(z->x + cosf(a) * rr, z->y + sinf(a) * rr * 0.8f, 0, -40.0f, 0.5f, 6.0f, 0xFFFF8A1E, PT_FLAME);
            }
        } else if (z->kind == Z_SLAM) {
            /* Boss shockwave: hurts when the ring sweeps over you. */
            float k = 1.0f - z->life / z->max;
            float rr = z->r * k;
            float dx = S.px - z->x, dy = S.py - z->y, d = sqrtf(dx * dx + dy * dy);
            if (!z->evo && fabsf(d - rr) < 18.0f) { hurt_player(z->dps); z->evo = 1; }
        }
    }
}

/* --------------------------------------------------------- enemy update -- */
static void boss_think(Enemy *e, float dt, float *mx, float *my) {
    e->bt -= dt;
    float dx = S.px - e->x, dy = S.py - e->y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
    int king = e->type == E_KING;
    switch (e->bstate) {
    case 0: /* walk */
        *mx = dx / d; *my = dy / d;
        if (e->bt <= 0.0f) {
            e->bstate = (S.rng.state & 1) ? 1 : 3;
            e->bt = e->bstate == 1 ? 0.75f : 0.7f;
            e->tx = dx / d; e->ty = dy / d;
        }
        break;
    case 1: /* wind up the charge */
        *mx = *my = 0.0f;
        e->tx = dx / d; e->ty = dy / d;
        if (e->bt <= 0.0f) { e->bstate = 2; e->bt = 0.55f; pa_tone(200.0f, 90.0f, 0.3f, 3, 0.07f); }
        break;
    case 2: /* charge */
        e->x += e->tx * (king ? 470.0f : 400.0f) * dt;
        e->y += e->ty * (king ? 470.0f : 400.0f) * dt;
        *mx = *my = 0.0f;
        if (((int)(e->bt * 60.0f) & 1)) part(e->x, e->y, 0, 0, 0.35f, 10.0f, 0xFFB8B4C8, PT_SMOKE);
        if (e->bt <= 0.0f) { e->bstate = 0; e->bt = king ? 1.6f : 2.2f; }
        break;
    case 4: /* dropping in from above; lands with a quake */
        *mx = *my = 0.0f;
        if (e->bt <= 0.0f) {
            zone(Z_SLAM, e->x, e->y, 280.0f, 0.7f, 0.0f, 1);
            zone(Z_RING, e->x, e->y - 30.0f, 200.0f, 0.5f, 0.0f, 1);
            burst(e->x, e->y, 26, 300.0f, 0.9f, 12.0f, 0xFFB8B4C8, PT_SMOKE);
            burst(e->x, e->y, 18, 260.0f, 0.6f, 5.0f, 0xFF8A8A9A, PT_SPARK);
            S.shake = 1.0f;
            S.flash = 0.35f; S.flash_col = 0xFFFFFFFF;
            pa_sfx("boom");
            pa_tone(90.0f, 40.0f, 0.6f, 3, 0.14f);
            e->bstate = 0; e->bt = 1.4f;
        }
        break;
    case 3: /* slam */
        *mx = *my = 0.0f;
        if (e->bt <= 0.0f) {
            zone(Z_SLAM, e->x, e->y, 230.0f, 0.75f, e->dmg * 0.7f, 0);
            S.shake = 0.45f;
            pa_sfx("boom");
            int shots = king ? 16 : 10;
            for (int k = 0; k < shots; k++) {
                for (int b = 0; b < MAX_EBUL; b++) {
                    if (g_eb[b].alive) continue;
                    float a = (float)k / (float)shots * PA_TAU + S.t;
                    g_eb[b].alive = 1; g_eb[b].x = e->x; g_eb[b].y = e->y - 40.0f;
                    g_eb[b].vx = cosf(a) * 190.0f; g_eb[b].vy = sinf(a) * 190.0f;
                    g_eb[b].life = 2.6f; g_eb[b].r = 8.0f;
                    break;
                }
            }
            if (king) for (int k = 0; k < 6; k++) {
                float a = (float)k / 6.0f * PA_TAU;
                spawn_enemy(E_FLY, e->x + cosf(a) * 70.0f, e->y + sinf(a) * 50.0f);
            }
            e->bstate = 0; e->bt = king ? 1.8f : 2.4f;
        }
        break;
    }
}

static void update_enemies(float dt) {
    float hw = view_hw(), hh = view_hh();
    for (int i = 0; i < g_en_count; i++) {
        Enemy *e = &g_en[i];
        if (!e->alive) continue;
        for (int w = 0; w < W_COUNT; w++) if (e->imm[w] > 0.0f) e->imm[w] -= dt;
        if (e->flash > 0.0f) e->flash -= dt;
        e->anim += dt * (e->type == E_FLY ? 9.0f : 6.0f) * (e->speed / 50.0f);

        float dx = S.px - e->x, dy = S.py - e->y;
        float d = sqrtf(dx * dx + dy * dy) + 0.01f;
        float mx = dx / d, my = dy / d;
        if (e->type == E_BOSS || e->type == E_KING) boss_think(e, dt, &mx, &my);
        if (e->type == E_SPITTER) {
            if (d < 230.0f) { mx = -mx * 0.3f; my = -my * 0.3f; }
            e->atk -= dt;
            if (e->atk <= 0.0f && d < 340.0f && on_screen(e->x, e->y, 0.0f)) {
                e->atk = 2.6f;
                for (int b = 0; b < MAX_EBUL; b++) {
                    if (g_eb[b].alive) continue;
                    g_eb[b].alive = 1; g_eb[b].x = e->x; g_eb[b].y = e->y - 28.0f;
                    g_eb[b].vx = dx / d * 170.0f; g_eb[b].vy = dy / d * 170.0f;
                    g_eb[b].life = 3.0f; g_eb[b].r = 7.0f;
                    break;
                }
            }
        }
        if (fabsf(dx) > 2.0f) e->face = dx > 0.0f ? 0 : 1;
        e->x += (mx * e->speed + e->kx) * dt;
        e->y += (my * e->speed + e->ky) * dt;
        float kd = expf(-9.0f * dt);
        e->kx *= kd; e->ky *= kd;

        /* The cage fence holds both sides of it. */
        if (S.cage) {
            float cx = e->x - S.cage_x, cy = e->y - S.cage_y, cd = sqrtf(cx * cx + cy * cy) + 0.01f;
            int inside_was = (e->type >= E_BOSS) || e->bt < 0.0f;
            (void)inside_was;
            if (e->type >= E_BOSS || cd < S.cage_r) {
                if (cd > S.cage_r - e->r) { e->x = S.cage_x + cx / cd * (S.cage_r - e->r); e->y = S.cage_y + cy / cd * (S.cage_r - e->r); }
            } else if (cd < S.cage_r + e->r) {
                e->x = S.cage_x + cx / cd * (S.cage_r + e->r); e->y = S.cage_y + cy / cd * (S.cage_r + e->r);
            }
        }

        /* Contact damage. */
        float reach = e->r + 12.0f;
        if (fabsf(dx) < reach && fabsf(dy + 6.0f) < reach) {
            e->atk -= dt;
            if (e->atk <= 0.0f && e->type != E_SPITTER && !(e->type >= E_BOSS && e->bstate == 4)) {
                e->atk = 0.9f;
                hurt_player(e->dmg);
            }
        }

        /* Left far behind: walk back in from the side the hero is heading. */
        if (e->type < E_BOSS && (fabsf(dx) > hw + 220.0f || fabsf(dy) > hh + 220.0f)) {
            float x, y;
            offscreen_point(&x, &y, 40.0f);
            if (fabsf(S.pvx) + fabsf(S.pvy) > 20.0f) {
                float sp = sqrtf(S.pvx * S.pvx + S.pvy * S.pvy);
                x = S.px + S.pvx / sp * (hw + 40.0f) + frange(-hw, hw) * fabsf(S.pvy) / sp;
                y = S.py + S.pvy / sp * (hh + 40.0f) + frange(-hh, hh) * fabsf(S.pvx) / sp * 0.5f;
            }
            e->x = x; e->y = y;
        }
    }

    /* Separation through the grid: a swarm, not a single stacked sprite. */
    for (int cy = 0; cy < GRID_H; cy++)
        for (int cx = 0; cx < GRID_W; cx++)
            for (int i = g_grid_head[cy * GRID_W + cx]; i >= 0; i = g_grid_next[i]) {
                Enemy *a = &g_en[i];
                if (!a->alive) continue;
                for (int oy = 0; oy <= 1; oy++)
                    for (int ox = (oy ? -1 : 0); ox <= 1; ox++) {
                        int nx = cx + ox, ny = cy + oy;
                        if (nx < 0 || nx >= GRID_W || ny >= GRID_H) continue;
                        int j = (ox == 0 && oy == 0) ? g_grid_next[i] : g_grid_head[ny * GRID_W + nx];
                        for (; j >= 0; j = g_grid_next[j]) {
                            Enemy *b = &g_en[j];
                            if (!b->alive) continue;
                            float sx = a->x - b->x, sy = a->y - b->y;
                            float md = (a->r + b->r) * 0.82f;
                            float d2 = sx * sx + sy * sy;
                            if (d2 >= md * md || d2 < 0.0001f) continue;
                            float dd = sqrtf(d2), push = (md - dd) * 0.5f / dd;
                            float wa = a->type >= E_ELITE ? 0.1f : 1.0f, wb = b->type >= E_ELITE ? 0.1f : 1.0f;
                            a->x += sx * push * wa; a->y += sy * push * wa;
                            b->x -= sx * push * wb; b->y -= sy * push * wb;
                        }
                    }
            }

    for (int b = 0; b < MAX_EBUL; b++) {
        EBullet *q = &g_eb[b];
        if (!q->alive) continue;
        q->x += q->vx * dt; q->y += q->vy * dt;
        q->life -= dt;
        if (q->life <= 0.0f) { q->alive = 0; continue; }
        float dx = q->x - S.px, dy = q->y - (S.py - 22.0f);
        if (dx * dx + dy * dy < (q->r + 12.0f) * (q->r + 12.0f)) {
            hurt_player(8.0f * (1.0f + S.t / 300.0f));
            q->alive = 0;
            burst(q->x, q->y, 6, 90.0f, 0.3f, 4.0f, 0xFFC04AE0, PT_SPARK);
        }
    }
}

/* ------------------------------------------------------------- pickups --- */
static void update_gems(float dt) {
    float pr = pickup_r();
    for (int i = 0; i < g_gem_count; i++) {
        Gem *g = &g_gm[i];
        if (!g->alive) continue;
        g->t += dt;
        g->x += g->vx * dt; g->y += g->vy * dt;
        float dx = S.px - g->x, dy = (S.py - 16.0f) - g->y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
        if (!g->pulled && d < (g->kind == G_CHEST ? 40.0f : pr)) g->pulled = 1;
        if (g->pulled) {
            float sp = 260.0f + g->t * 120.0f;
            if (g->kind == G_CHEST) sp = 200.0f;
            g->vx += (dx / d * sp - g->vx) * (1.0f - expf(-10.0f * dt));
            g->vy += (dy / d * sp - g->vy) * (1.0f - expf(-10.0f * dt));
            if (d < 16.0f) {
                g->alive = 0;
                switch (g->kind) {
                case G_BLUE: case G_GREEN: case G_GOLD:
                    S.xp += g->value;
                    sfx_gem();
                    break;
                case G_COIN: S.coins += g->value; pa_sfx("coin"); break;
                case G_MEAT: heal(S.maxhp * 0.3f); pa_sfx("good"); break;
                case G_MAGNET:
                    for (int k = 0; k < g_gem_count; k++)
                        if (g_gm[k].alive && g_gm[k].kind <= G_GOLD) { g_gm[k].pulled = 1; g_gm[k].t = 0.6f; }
                    pa_tone(400.0f, 1400.0f, 0.4f, 1, 0.08f);
                    break;
                case G_BOMB:
                    S.flash = 0.5f; S.flash_col = 0xFFFFFFFF; S.shake = 0.5f;
                    for (int k = 0; k < g_en_count; k++)
                        if (g_en[k].alive && on_screen(g_en[k].x, g_en[k].y, 20.0f))
                            hurt_enemy(k, g_en[k].type >= E_ELITE ? 300.0f : 99999.0f, 0, 0, -1);
                    pa_sfx("boom");
                    break;
                case G_CHEST:
                    open_chest();
                    break;
                }
            }
        } else {
            g->vx *= expf(-6.0f * dt); g->vy *= expf(-6.0f * dt);
        }
    }
    while (S.xp >= S.xpneed) {
        S.xp -= S.xpneed;
        S.level++;
        S.xpneed = xp_need(S.level);
        S.pending++;
    }
}

static void update_fx(float dt) {
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *p = &g_pt[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        p->x += p->vx * dt; p->y += p->vy * dt;
        float drag = expf(-(p->kind == PT_SMOKE ? 3.0f : 5.0f) * dt);
        p->vx *= drag; p->vy *= drag;
        if (p->kind == PT_BLOOD) p->vy += 300.0f * dt;
        if (p->kind == PT_FLAME) p->vy -= 60.0f * dt;
    }
    for (int i = 0; i < MAX_NUMS; i++) if (g_num[i].life > 0.0f) { g_num[i].life -= dt; g_num[i].y -= 34.0f * dt; }
    for (int i = 0; i < MAX_DECALS; i++) g_dc[i].age += dt;
}

/* ========================================================= run lifecycle == */
static void load_meta(void) {
    if (g_loaded) return;
    g_loaded = 1;
    if (pa_demo_mode()) {
        /* Review captures run from a fixed profile so every frame is the same
           on every machine, whatever save file sits in the working directory. */
        g_best = 312; g_best_kills = 1840; g_bank = 1265; g_meta_atk = 2; g_meta_hp = 1; g_runs = 7;
        return;
    }
    g_best       = pa_save_get("horde.best", 0);
    g_best_kills = pa_save_get("horde.kills", 0);
    g_bank       = pa_save_get("horde.coins", 0);
    g_meta_atk   = pa_save_get("horde.atk", 0);
    g_meta_hp    = pa_save_get("horde.hp", 0);
    g_runs       = pa_save_get("horde.runs", 0);
}

static void save_meta(void) {
    if (pa_demo_mode()) return;
    pa_save_set("horde.best", g_best);
    pa_save_set("horde.kills", g_best_kills);
    pa_save_set("horde.coins", g_bank);
    pa_save_set("horde.atk", g_meta_atk);
    pa_save_set("horde.hp", g_meta_hp);
    pa_save_set("horde.runs", g_runs);
    pa_save_flush();
}

static int meta_cost(int lv) { return 120 + lv * 90; }

static void add_weapon(int w) {
    if (!S.wlv[w]) { if (S.wslots >= SLOTS) return; S.wslot[S.wslots++] = w; }
    if (S.wlv[w] < MAX_LV) S.wlv[w]++;
    S.cd[w] = 0.2f;
}
static void add_item(int p) {
    if (!S.plv[p]) { if (S.pslots >= SLOTS) return; S.pslot[S.pslots++] = p; }
    if (S.plv[p] < MAX_LV) S.plv[p]++;
    if (p == P_BOOK) { S.maxhp += 20.0f; S.hp += 20.0f; }
}

static void clear_world(void) {
    memset(g_en, 0, sizeof(g_en)); g_en_count = 0;
    memset(g_pj, 0, sizeof(g_pj));
    memset(g_zn, 0, sizeof(g_zn));
    memset(g_gm, 0, sizeof(g_gm)); g_gem_count = 0;
    memset(g_pt, 0, sizeof(g_pt)); g_pt_next = 0;
    memset(g_num, 0, sizeof(g_num)); g_num_next = 0;
    memset(g_dc, 0, sizeof(g_dc)); g_dc_next = 0;
    memset(g_eb, 0, sizeof(g_eb));
}

static void begin_run(void) {
    clear_world();
    memset(&S, 0, sizeof(S));
    pa_rng_seed(&S.rng, pa_demo_mode() ? 0x5EEDu : 0x9E3779B9u ^ ((uint32_t)g_runs * 2654435761u));
    S.phase = PH_PLAY;
    S.maxhp = S.hp = 100.0f + 10.0f * (float)g_meta_hp;
    S.level = 1;
    S.xpneed = xp_need(1);
    S.face = 1;
    S.boss_idx = -1;
    add_weapon(W_KUNAI);
    pa_sfx("good");
}

/* ---------------------------------------------------------------- draft -- */
static int build_picks(Pick *out, int max) {
    Pick cand[W_COUNT * 2 + P_COUNT];
    float wt[W_COUNT * 2 + P_COUNT];
    int n = 0, got = 0;
    for (int w = 0; w < W_COUNT; w++)
        if (S.wlv[w] == MAX_LV && !S.evo[w] && S.plv[w] > 0 && got < max) {
            out[got].kind = 2; out[got].id = w; got++;
        }
    for (int w = 0; w < W_COUNT; w++) {
        if (S.wlv[w] > 0 && S.wlv[w] < MAX_LV) { cand[n].kind = 0; cand[n].id = w; wt[n++] = 3.0f; }
        else if (!S.wlv[w] && S.wslots < SLOTS) { cand[n].kind = 0; cand[n].id = w; wt[n++] = 2.0f; }
    }
    for (int p = 0; p < P_COUNT; p++) {
        float bonus = S.wlv[p] ? 0.6f : 0.0f;   /* partner items come up more often */
        if (S.plv[p] > 0 && S.plv[p] < MAX_LV) { cand[n].kind = 1; cand[n].id = p; wt[n++] = 1.8f + bonus; }
        else if (!S.plv[p] && S.pslots < SLOTS) { cand[n].kind = 1; cand[n].id = p; wt[n++] = 1.4f + bonus; }
    }
    while (got < max && n > 0) {
        float sum = 0.0f;
        for (int i = 0; i < n; i++) sum += wt[i];
        float r = frand() * sum;
        int k = n - 1;
        for (int i = 0; i < n; i++) { r -= wt[i]; if (r <= 0.0f) { k = i; break; } }
        out[got++] = cand[k];
        cand[k] = cand[n - 1]; wt[k] = wt[n - 1]; n--;
    }
    if (got == 0 && max > 0) { out[got].kind = 3; out[got].id = 0; got++; }
    if (got == 1 && max > 1 && out[0].kind == 3) { out[got].kind = 4; out[got].id = 0; got++; }
    return got;
}

static void apply_pick(const Pick *p) {
    switch (p->kind) {
    case 0: add_weapon(p->id); break;
    case 1: add_item(p->id); break;
    case 2:
        S.evo[p->id] = 1;
        S.flash = 0.5f; S.flash_col = 0xFFFFE070;
        zone(Z_RING, S.px, S.py - 20.0f, 220.0f, 0.6f, 0.0f, 1);
        break;
    case 3: heal(S.maxhp * 0.5f); break;
    case 4: S.coins += 60; break;
    }
}

static void open_draft(void) {
    S.nopt = build_picks(S.opt, 3);
    S.phase = PH_DRAFT;
    S.phase_t = 0.0f;
    S.chosen = -1;
    S.pick_t = 0.0f;
    S.levelup_t = 1.0f;
    pa_sfx("levelup");
}

/** How the review bot ranks a card: finish evolutions, then deepen weapons it
    owns, then partner items, then anything new. */
static float pick_score(const Pick *p) {
    switch (p->kind) {
    case 2: return 100.0f;
    case 0: return S.wlv[p->id] ? 50.0f + (float)S.wlv[p->id] : (S.wslots < 4 ? 46.0f : 20.0f);
    case 1: return (S.wlv[p->id] ? 40.0f : 18.0f) + (p->id == P_POWER ? 4.0f : 0.0f);
    default: return 1.0f;
    }
}

static void card_rect(int i, float *x, float *y, float *w, float *h) {
    float s = L.s;
    float gap = 10.0f * s;
    float cw = ((float)L.w - gap * 4.0f) / 3.0f;
    if (cw > 210.0f * s) cw = 210.0f * s;
    float ch = cw * 2.05f;
    if (ch > (float)L.h * 0.46f) ch = (float)L.h * 0.46f;
    float total = cw * 3.0f + gap * 2.0f;
    *x = ((float)L.w - total) * 0.5f + (float)i * (cw + gap);
    *y = (float)L.h * 0.36f;
    /* Keep clear of the owned-skill bars above, shrinking on short screens. */
    float top = (float)L.h * 0.36f - 210.0f * s;
    if (top < 70.0f * s) top = 70.0f * s;
    float min_y = top + 124.0f * s + 30.0f * s;
    if (*y < min_y) *y = min_y;
    if (*y + ch > (float)L.h - 36.0f * s) ch = (float)L.h - 36.0f * s - *y;
    *w = cw; *h = ch;
}

static void update_draft(float dt, const PA_Input *in) {
    if (S.chosen < 0) {
        if (S.phase_t > 0.35f && in->tapped) {
            for (int i = 0; i < S.nopt; i++) {
                float x, y, w, h;
                card_rect(i, &x, &y, &w, &h);
                if (in->x >= x && in->x <= x + w && in->y >= y - 20.0f && in->y <= y + h) {
                    S.chosen = i; pa_sfx("good");
                }
            }
        }
        if (pa_demo_mode() && S.phase_t > 1.6f) {
            int best = 0;
            for (int i = 1; i < S.nopt; i++) if (pick_score(&S.opt[i]) > pick_score(&S.opt[best])) best = i;
            S.chosen = best;
            pa_sfx("good");
        }
        return;
    }
    S.pick_t += dt;
    if (S.pick_t >= 0.32f) {
        apply_pick(&S.opt[S.chosen]);
        S.pending--;
        S.picks++;
        if (S.pending > 0) open_draft();
        else { S.phase = PH_PLAY; S.phase_t = 0.0f; S.invuln = 0.6f; }
    }
}

/* ---------------------------------------------------------------- chest -- */
static void open_chest(void) {
    static const int ring_n = 16;
    int count = (S.bosses_down > 0 && S.cage == 0 && S.boss_idx < 0 && frand() < 0.5f) ? 3 : (frand() < 0.3f ? 3 : 1);
    if (pa_demo_mode() == 5) count = 3;
    Pick wins[3];
    int n = build_picks(wins, count);
    S.chest_nwin = n;
    for (int i = 0; i < ring_n; i++) {
        int id = (int)(frand() * (float)(W_COUNT + P_COUNT));
        if (id >= W_COUNT + P_COUNT) id = 0;
        S.chest_tiles[i].kind = id < W_COUNT ? 0 : 1;
        S.chest_tiles[i].id = id < W_COUNT ? id : id - W_COUNT;
    }
    int w0 = (int)(frand() * (float)ring_n) % ring_n;
    for (int i = 0; i < n; i++) {
        int at = (w0 + i * 5) % ring_n;
        S.chest_win[i] = at;
        S.chest_tiles[at] = wins[i];
    }
    S.chest_spin = 0.0f;
    S.chest_speed = (float)(ring_n * 2 + w0);
    S.chest_stage = 0;
    S.chest_coins = 20 + (int)(frand() * 40.0f);
    S.phase = PH_CHEST;
    S.phase_t = 0.0f;
    pa_sfx("good");
}

#define CHEST_SPIN_T 2.4f

static void update_chest(float dt, const PA_Input *in) {
    (void)dt;
    if (S.chest_stage == 0) {
        float k = pa_clamp01(S.phase_t / CHEST_SPIN_T);
        float e = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);
        float pos = S.chest_speed * e;
        if ((int)pos != (int)S.chest_spin) pa_tone(880.0f, 880.0f, 0.03f, 2, 0.035f);
        S.chest_spin = pos;
        if (k >= 1.0f) {
            S.chest_stage = 1;
            S.phase_t = 0.0f;
            for (int i = 0; i < S.chest_nwin; i++) apply_pick(&S.chest_tiles[S.chest_win[i]]);
            S.coins += S.chest_coins;
            pa_sfx("win");
        }
        return;
    }
    if ((S.phase_t > 0.6f && in->tapped) || (pa_demo_mode() && S.phase_t > 2.2f)) {
        S.phase = S.pending > 0 ? PH_PLAY : PH_PLAY;
        S.phase_t = 0.0f;
        S.invuln = 0.6f;
        pa_sfx("select");
    }
}

/* -------------------------------------------------------------- results -- */
static void finish(int won) {
    S.phase = PH_OVER;
    S.over_t = 0.0f;
    S.won = won;
    int secs = (int)S.t;
    S.reward = S.coins + S.kills / 6 + S.bosses_down * 80 + (won ? 300 : 0) + secs / 4;
    S.new_best = secs > g_best || (won && secs >= g_best);
    if (secs > g_best) g_best = secs;
    if (S.kills > g_best_kills) g_best_kills = S.kills;
    g_bank += S.reward;
    g_runs++;
    save_meta();
    PA_RunReport rep = { 0 };
    rep.score = secs;
    rep.coins = S.reward;
    rep.won = won;
    rep.level = 1;
    rep.stars = won ? 3 : secs >= 240 ? 2 : secs >= 120 ? 1 : 0;
    pa_meta_report("horde", &rep);
}

static void over_buttons(float *rx, float *ry, float *rw, float *rh, float *hx, float *hy, float *hw, float *hh) {
    float s = L.s;
    float pw = (float)L.w - 48.0f * s;
    if (pw > 460.0f * s) pw = 460.0f * s;
    float px = ((float)L.w - pw) * 0.5f;
    float by = (float)L.h * 0.5f + 300.0f * s;
    if (by > (float)L.h - 90.0f * s) by = (float)L.h - 90.0f * s;
    *hx = px; *hy = by; *hw = pw * 0.36f; *hh = 64.0f * s;
    *rx = px + pw * 0.40f; *ry = by; *rw = pw * 0.60f; *rh = 64.0f * s;
}

static void title_buttons(float *sx, float *sy, float *sw, float *sh, float *ax, float *ay, float *hx, float *hy, float *cw, float *chh) {
    float s = L.s;
    *sw = 260.0f * s; *sh = 76.0f * s;
    *sx = ((float)L.w - *sw) * 0.5f;
    *sy = (float)L.h - 170.0f * s;
    *cw = 150.0f * s; *chh = 52.0f * s;
    *ay = *sy - 78.0f * s; *hy = *ay;
    *ax = (float)L.w * 0.5f - *cw - 8.0f * s;
    *hx = (float)L.w * 0.5f + 8.0f * s;
}

static int in_box(const PA_Input *in, float x, float y, float w, float h) {
    return in->x >= x && in->x <= x + w && in->y >= y && in->y <= y + h;
}

/* ------------------------------------------------------------------ bot -- */
static void bot_steer(float dt, float *mx, float *my) {
    float rx = 0.0f, ry = 0.0f, cxs = 0.0f, cys = 0.0f;
    int near = 0;
    for (int i = 0; i < g_en_count; i++) {
        const Enemy *e = &g_en[i];
        if (!e->alive) continue;
        float dx = S.px - e->x, dy = S.py - e->y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
        float range = e->type >= E_BOSS ? 260.0f : e->type == E_ELITE ? 150.0f : 120.0f;
        if (d > range) continue;
        float w = (range - d) / range;
        w *= w * (e->type >= E_BOSS ? 6.0f : e->type == E_ELITE ? 2.5f : 1.0f);
        rx += dx / d * w; ry += dy / d * w;
        cxs += dx / d; cys += dy / d;
        near++;
    }
    for (int b = 0; b < MAX_EBUL; b++) {
        if (!g_eb[b].alive) continue;
        float dx = S.px - g_eb[b].x, dy = S.py - 20.0f - g_eb[b].y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
        if (d < 90.0f) { rx += dx / d * 2.0f; ry += dy / d * 2.0f; }
    }
    /* Boss slam ring: step out of its path. */
    for (int i = 0; i < MAX_ZONES; i++) {
        const Zone *z = &g_zn[i];
        if (!z->alive || z->kind != Z_SLAM) continue;
        float dx = S.px - z->x, dy = S.py - z->y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
        rx += dx / d * 3.0f; ry += dy / d * 3.0f;
    }
    float threat = sqrtf(rx * rx + ry * ry);

    /* Gems and chests when it is safe enough to go and get them. */
    float gx = 0.0f, gy = 0.0f, gd = 300.0f * 300.0f;
    for (int i = 0; i < g_gem_count; i++) {
        const Gem *g = &g_gm[i];
        if (!g->alive || g->pulled) continue;
        float dx = g->x - S.px, dy = g->y - S.py, d = dx * dx + dy * dy;
        if (g->kind >= G_MEAT) d *= 0.25f;
        if (d < gd) { gd = d; gx = dx; gy = dy; }
    }
    float gl = sqrtf(gx * gx + gy * gy) + 0.01f;

    S.bot_wander += dt * 0.45f;
    float wx = cosf(S.bot_wander), wy = sinf(S.bot_wander) * 0.8f;

    float ox = rx * 2.4f, oy = ry * 2.4f;
    /* Circle-strafe: slide sideways round the pack rather than straight away. */
    if (threat > 0.05f) { ox += -ry / threat * 0.9f; oy += rx / threat * 0.9f; }
    if (gl > 1.0f) { float gw = threat < 0.7f ? 1.1f : 0.25f; ox += gx / gl * gw; oy += gy / gl * gw; }
    ox += wx * 0.45f; oy += wy * 0.45f;
    if (S.cage) {
        float dx = S.cage_x - S.px, dy = S.cage_y - S.py, d = sqrtf(dx * dx + dy * dy) + 0.01f;
        if (d > S.cage_r * 0.55f) { float k = (d - S.cage_r * 0.55f) / (S.cage_r * 0.45f) * 3.0f; ox += dx / d * k; oy += dy / d * k; }
    }
    (void)near; (void)cxs; (void)cys;
    float l = sqrtf(ox * ox + oy * oy);
    if (l < 0.05f) { *mx = *my = 0.0f; return; }
    *mx = ox / l; *my = oy / l;
}

/* ---------------------------------------------------------------- update -- */
static void sim_play(float dt, const PA_Input *in) {
    S.t += dt;
    run_events();
    if (S.cage) S.cage_t += dt;

    /* Floating joystick: born where the finger lands, follows it past the rim. */
    float mx = 0.0f, my = 0.0f;
    float R = 54.0f * L.s;
    if (pa_demo_mode()) {
        if (pa_demo_mode() != 4) bot_steer(dt, &mx, &my);
        S.joy_on = 1;
        S.joy_ox = (float)L.w * 0.5f; S.joy_oy = (float)L.h * 0.80f;
        S.joy_kx = pa_approach(S.joy_kx, mx * R * 0.9f, 12.0f, dt);
        S.joy_ky = pa_approach(S.joy_ky, my * R * 0.9f, 12.0f, dt);
    } else {
        if (in->pressed) { S.joy_on = 1; S.joy_ox = in->x; S.joy_oy = in->y; S.joy_kx = S.joy_ky = 0.0f; }
        if (in->down && S.joy_on) {
            float dx = in->x - S.joy_ox, dy = in->y - S.joy_oy, d = sqrtf(dx * dx + dy * dy);
            if (d > R) { S.joy_ox += dx * (d - R) / d; S.joy_oy += dy * (d - R) / d; dx = dx * R / d; dy = dy * R / d; d = R; }
            S.joy_kx = dx; S.joy_ky = dy;
            if (d > 5.0f * L.s) { float m = pa_clamp01(d / (R * 0.6f)); mx = dx / d * m; my = dy / d * m; }
        } else S.joy_on = 0;
        float kx = (float)(in->keys[PA_KEY_RIGHT] - in->keys[PA_KEY_LEFT]);
        float ky = (float)(in->keys[PA_KEY_DOWN] - in->keys[PA_KEY_UP]);
        if (kx != 0.0f || ky != 0.0f) { float kl = sqrtf(kx * kx + ky * ky); mx = kx / kl; my = ky / kl; }
    }
    S.mvx = mx; S.mvy = my;

    float sp = 152.0f * speed_mul();
    float ease = 1.0f - expf(-16.0f * dt);
    S.pvx += (mx * sp - S.pvx) * ease;
    S.pvy += (my * sp - S.pvy) * ease;
    S.px += S.pvx * dt; S.py += S.pvy * dt;
    float spd = sqrtf(S.pvx * S.pvx + S.pvy * S.pvy);
    S.moving = spd > 20.0f ? 1.0f : 0.0f;
    S.run_anim += dt * (spd > 20.0f ? spd / 16.0f : 0.0f);
    if (fabsf(S.pvx) > 25.0f && S.cd[W_KUNAI] > 0.25f) S.face = S.pvx > 0.0f ? 1 : -1;
    if (S.cage) {
        float dx = S.px - S.cage_x, dy = S.py - S.cage_y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
        float lim = S.cage_r - 18.0f;
        if (d > lim) { S.px = S.cage_x + dx / d * lim; S.py = S.cage_y + dy / d * lim; }
    }
    if (S.plv[P_OIL]) {
        S.regen_acc += dt;
        if (S.regen_acc >= 1.0f) { S.regen_acc -= 1.0f; if (S.hp < S.maxhp) S.hp = S.hp + 0.6f * (float)S.plv[P_OIL] > S.maxhp ? S.maxhp : S.hp + 0.6f * (float)S.plv[P_OIL]; }
    }
    if (S.invuln > 0.0f) S.invuln -= dt;

    grid_build();
    if (!(S.boss_idx >= 0 && S.boss_kind == E_KING)) spawner(dt);
    update_enemies(dt);
    grid_build();
    fire_weapons(dt);
    update_projectiles(dt);
    update_zones(dt);
    update_gems(dt);
    update_fx(dt);

    S.cam_x += (S.px - S.cam_x) * (1.0f - expf(-10.0f * dt));
    S.cam_y += (S.py - 20.0f - S.cam_y) * (1.0f - expf(-10.0f * dt));

    if (S.phase == PH_PLAY && S.pending > 0) open_draft();
}

static void demo_preload(int mode) {
    begin_run();
    static const int kit2[W_COUNT] = { 4, 3, 2, 3, 0, 2, 0, 3 };
    static const int itm2[P_COUNT] = { 2, 1, 1, 2, 0, 1, 0, 0 };
    static const int kit3[W_COUNT] = { 5, 4, 3, 3, 0, 0, 2, 4 };
    static const int itm3[P_COUNT] = { 2, 2, 1, 2, 1, 0, 0, 1 };
    const int *kit = mode == 3 || mode == 6 ? kit3 : kit2;
    const int *itm = mode == 3 || mode == 6 ? itm3 : itm2;
    float t = mode == 2 ? 140.0f : mode == 3 ? 228.0f : mode == 4 ? 300.0f : mode == 5 ? 100.0f : 470.0f;
    int level = mode == 2 ? 15 : mode == 3 ? 21 : mode == 4 ? 18 : mode == 5 ? 11 : 31;
    if (mode == 4 || mode == 5) { kit = kit2; itm = itm2; }
    for (int w = 0; w < W_COUNT; w++) for (int l = (w == W_KUNAI); l < kit[w]; l++) add_weapon(w);
    for (int p = 0; p < P_COUNT; p++) for (int l = 0; l < itm[p]; l++) add_item(p);
    if (mode == 3 || mode == 6) S.evo[W_KUNAI] = 1;
    if (mode == 6) {
        S.evo[W_GUARD] = 1; for (int l = S.wlv[W_GUARD]; l < MAX_LV; l++) add_weapon(W_GUARD);
        S.evo[W_FIELD] = 1; for (int l = S.wlv[W_FIELD]; l < MAX_LV; l++) add_weapon(W_FIELD);
    }
    S.hp = S.maxhp;
    S.t = t;
    S.level = level; S.xpneed = xp_need(level); S.xp = S.xpneed * 3 / 5;
    S.kills = (int)(t * 7.5f);
    S.coins = (int)(t * 0.4f);
    if (mode >= 3) S.bosses_down = mode == 6 ? 1 : 0;
    while (S.next_event < EVENT_COUNT && EVENTS[S.next_event].t <= t) S.next_event++;
    if (mode == 6) S.bosses_down = 1;
    /* A crowd already closing in. */
    int crowd = mode == 2 ? 330 : mode == 3 ? 260 : mode == 4 ? 300 : mode == 5 ? 60 : 260;
    for (int i = 0; i < crowd; i++) {
        float a = frand() * PA_TAU, r = mode == 4 ? frange(70.0f, 330.0f) : frange(170.0f, 640.0f);
        spawn_enemy(pick_type(), S.px + cosf(a) * r, S.py + sinf(a) * r * 1.3f);
    }
    for (int i = 0; i < 40; i++) {
        float a = frand() * PA_TAU, r = frange(80.0f, 400.0f);
        drop_gem(S.px + cosf(a) * r, S.py + sinf(a) * r, i % 7 == 0 ? G_GREEN : G_BLUE, i % 7 == 0 ? 3 : 1);
    }
    for (int i = 0; i < 40; i++) decal(S.px + frange(-260, 260), S.py + frange(-500, 500), frange(8.0f, 14.0f));
    if (mode == 4) { S.hp = 26.0f; S.maxhp = 140.0f; }
    if (mode == 5) drop_gem(S.px + 70.0f, S.py - 20.0f, G_CHEST, 3);
    S.cam_x = S.px; S.cam_y = S.py - 20.0f;
}

static void s_start(void) {
    load_meta();
    clear_world();
    memset(&S, 0, sizeof(S));
    pa_rng_seed(&S.rng, 0x7177u);
    S.boss_idx = -1;
    S.phase = PH_TITLE;
    int dm = pa_demo_mode();
    if (dm >= 2) demo_preload(dm);
}

static void s_stop(void) {
    for (int i = 0; i < SPR_SETS; i++) for (int f = 0; f < ANIM_FRAMES; f++) spr_free(&g_spr[i][f]);
    for (int g = 0; g < G_KINDS; g++) spr_free(&g_gem_spr[g]);
    for (int st = 0; st < NUM_STYLES; st++) for (int d = 0; d < NUM_GLYPHS; d++) spr_free(&g_digit[st][d]);
    g_baked_scale = -1.0f;
    gc_reset();
}

static void s_update(float dt, const PA_Input *in) {
    S.clock += dt;
    S.phase_t += dt;
    if (S.banner_t > 0.0f) {
        float before = S.banner_t;
        S.banner_t -= dt;
        if (S.banner_t > 1.2f && (int)(before / 0.45f) != (int)(S.banner_t / 0.45f))
            pa_tone(((int)(S.banner_t / 0.45f) & 1) ? 660.0f : 880.0f, ((int)(S.banner_t / 0.45f) & 1) ? 880.0f : 660.0f,
                    0.3f, 2, 0.05f);
    }
    if (S.flash > 0.0f) S.flash -= dt * 1.6f;
    if (S.shake > 0.0f) S.shake -= dt * 1.8f;
    if (S.hurt > 0.0f) S.hurt -= dt;
    if (S.levelup_t > 0.0f) S.levelup_t -= dt;
    if (S.sfx_hit > 0.0f) S.sfx_hit -= dt;
    if (S.sfx_throw > 0.0f) S.sfx_throw -= dt;
    if (S.sfx_gem > 0.0f) S.sfx_gem -= dt; else if (S.sfx_gem < -0.5f) S.gem_streak = 0; else S.sfx_gem -= dt;

    switch (S.phase) {
    case PH_TITLE: {
        float sx, sy, sw, sh, ax, ay, hx, hy, cw, ch;
        title_buttons(&sx, &sy, &sw, &sh, &ax, &ay, &hx, &hy, &cw, &ch);
        if (in->tapped) {
            if (in_box(in, sx, sy, sw, sh)) begin_run();
            else if (in_box(in, ax, ay, cw, ch)) {
                int cost = meta_cost(g_meta_atk);
                if (g_bank >= cost && g_meta_atk < 10) { g_bank -= cost; g_meta_atk++; save_meta(); pa_sfx("coin"); }
                else pa_sfx("bad");
            } else if (in_box(in, hx, hy, cw, ch)) {
                int cost = meta_cost(g_meta_hp);
                if (g_bank >= cost && g_meta_hp < 10) { g_bank -= cost; g_meta_hp++; save_meta(); pa_sfx("coin"); }
                else pa_sfx("bad");
            }
        }
        if (in->key_pressed[PA_KEY_ENTER] || in->key_pressed[PA_KEY_SPACE]) begin_run();
        break;
    }
    case PH_PLAY: sim_play(dt, in); break;
    case PH_DRAFT: update_draft(dt, in); break;
    case PH_CHEST: update_chest(dt, in); break;
    case PH_DYING:
        update_fx(dt);
        for (int i = 0; i < g_en_count; i++) if (g_en[i].alive) g_en[i].anim += dt * 3.0f;
        if (S.phase_t > (S.won ? 2.4f : 1.6f)) finish(S.won);
        break;
    case PH_OVER: {
        S.over_t += dt;
        update_fx(dt);
        float rx, ry, rw, rh, hx, hy, hw, hh;
        over_buttons(&rx, &ry, &rw, &rh, &hx, &hy, &hw, &hh);
        if (S.over_t > 0.8f && in->tapped) {
            if (in_box(in, rx, ry, rw, rh)) begin_run();
            else if (in_box(in, hx, hy, hw, hh)) { clear_world(); S.phase = PH_TITLE; S.phase_t = 0.0f; pa_sfx("select"); }
        }
        if (S.over_t > 0.8f && (in->key_pressed[PA_KEY_ENTER] || in->key_pressed[PA_KEY_SPACE])) begin_run();
        break;
    }
    }
}

/* ============================================================= drawing ==== */
static float g_shx, g_shy;

/* World to screen. The view is set once per frame; the ground cache points it
   at a block while it paints one. */
static float g_vx, g_vy, g_vcx, g_vcy;
static float SX(float x) { return (x - g_vx) * L.s + g_vcx; }
static float SY(float y) { return (y - g_vy) * L.s + g_vcy; }
static void set_view(void) {
    g_vx = S.cam_x; g_vy = S.cam_y;
    g_vcx = (float)L.w * 0.5f + g_shx; g_vcy = (float)L.h * 0.5f + g_shy;
}

static void time_str(char *buf, size_t n, float t) {
    int s = (int)t;
    snprintf(buf, n, "%02d:%02d", s / 60, s % 60);
}

static float fit_size(const char *text, float maxw, float size, float tracking) {
    float w = pa_text_width(text, size, tracking);
    return w > maxw ? size * maxw / w : size;
}

/** Word-wrapped text, centred on x. Returns the height used. */
static float wrap_text(PA_Canvas *c, const char *text, float x, float y, float maxw, float size,
                       PA_Color col, float lh) {
    char line[224], word[48];
    int ln = 0, lines = 0;
    const char *p = text;
    line[0] = 0;
    while (*p) {
        int wn = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && wn < 47) word[wn++] = *p++;
        word[wn] = 0;
        if (!wn) break;
        char trial[220];
        snprintf(trial, sizeof(trial), "%s%s%s", line, ln ? " " : "", word);
        if (ln && pa_text_width(trial, size, size * 0.08f) > maxw) {
            pa_text(c, line, x, y + (float)lines * lh, size, col, PA_ALIGN_CENTER, size * 0.08f);
            lines++;
            snprintf(line, sizeof(line), "%s", word);
            ln = wn;
        } else {
            snprintf(line, sizeof(line), "%s", trial);
            ln = (int)strlen(line);
        }
    }
    if (ln) { pa_text(c, line, x, y + (float)lines * lh, size, col, PA_ALIGN_CENTER, size * 0.08f); lines++; }
    return (float)lines * lh;
}

static void star(PA_Canvas *c, float x, float y, float r, PA_Color fill, float ow) {
    PA_Vec2 p[10];
    for (int i = 0; i < 10; i++) {
        float a = -PA_PI * 0.5f + (float)i * PA_PI / 5.0f;
        float rr = (i & 1) ? r * 0.46f : r;
        p[i].x = x + cosf(a) * rr; p[i].y = y + sinf(a) * rr;
    }
    o_poly(c, p, 10, fill, ow);
}

static void skull_icon(PA_Canvas *c, float x, float y, float r) {
    o_circle(c, x, y - r * 0.15f, r, PA_RGB(240, 238, 232), r * 0.22f);
    pa_round_rect(c, x - r * 0.55f, y + r * 0.4f, r * 1.1f, r * 0.6f, r * 0.2f, PA_RGB(240, 238, 232));
    pa_fill_circle(c, x - r * 0.38f, y - r * 0.1f, r * 0.28f, OUTC());
    pa_fill_circle(c, x + r * 0.38f, y - r * 0.1f, r * 0.28f, OUTC());
}

static void coin_icon(PA_Canvas *c, float x, float y, float r) {
    o_circle(c, x, y, r, pa_hex(0xFFC21C), r * 0.2f);
    pa_stroke_circle(c, x, y, r * 0.62f, r * 0.18f, pa_hex(0xE08A10));
    pa_fill_circle(c, x - r * 0.3f, y - r * 0.35f, r * 0.22f, pa_hex(0xFFF0B0));
}

/* ---------------------------------------------------------------- icons -- */
static void icon_kunai(PA_Canvas *c, float x, float y, float s, PA_Color blade, PA_Color wrap) {
    float ow = s * 0.035f;
    PA_Vec2 b[4] = { { x + s * 0.34f, y - s * 0.34f }, { x + s * 0.02f, y - s * 0.12f },
                     { x - s * 0.06f, y - s * 0.02f }, { x + s * 0.12f, y + s * 0.02f } };
    PA_Vec2 bl[4] = { { x + s * 0.34f, y - s * 0.34f }, { x + s * 0.12f, y + s * 0.02f },
                      { x + s * 0.02f, y + s * 0.06f }, { x + s * 0.02f, y - s * 0.12f } };
    pa_line(c, x - s * 0.06f, y + s * 0.06f, x - s * 0.24f, y + s * 0.24f, s * 0.08f + ow * 2.0f, OUTC());
    o_poly(c, b, 4, blade, ow);
    o_poly(c, bl, 4, pa_shade(blade, -0.25f), ow);
    pa_line(c, x - s * 0.06f, y + s * 0.06f, x - s * 0.22f, y + s * 0.22f, s * 0.08f, wrap);
    pa_stroke_circle(c, x - s * 0.28f, y + s * 0.28f, s * 0.07f, ow * 1.4f + s * 0.025f, OUTC());
    pa_stroke_circle(c, x - s * 0.28f, y + s * 0.28f, s * 0.07f, s * 0.025f, blade);
}

static void icon_saw(PA_Canvas *c, float x, float y, float r, PA_Color a, PA_Color b, float t) {
    PA_Vec2 p[24];
    for (int i = 0; i < 24; i++) {
        float ang = t + (float)i * PA_TAU / 24.0f;
        float rr = (i & 1) ? r * 0.82f : r;
        p[i].x = x + cosf(ang) * rr; p[i].y = y + sinf(ang) * rr;
    }
    o_poly(c, p, 24, a, r * 0.1f);
    pa_fill_circle(c, x, y, r * 0.58f, b);
    for (int i = 0; i < 3; i++) {
        float ang = t * 1.0f + (float)i * PA_TAU / 3.0f;
        PA_Vec2 w[3] = { { x, y }, { x + cosf(ang) * r * 0.58f, y + sinf(ang) * r * 0.58f },
                         { x + cosf(ang + 0.7f) * r * 0.58f, y + sinf(ang + 0.7f) * r * 0.58f } };
        pa_fill_poly(c, w, 3, a);
    }
    o_circle(c, x, y, r * 0.2f, pa_hex(0xD8DCE6), r * 0.06f);
}

static void icon_drone(PA_Canvas *c, float x, float y, float s, int evo) {
    float ow = s * 0.04f;
    PA_Color body = evo ? pa_hex(0x3A3C48) : pa_hex(0xF4F5F8);
    pa_line(c, x - s * 0.3f, y - s * 0.12f, x + s * 0.3f, y - s * 0.12f, s * 0.06f + ow * 2.0f, OUTC());
    pa_line(c, x - s * 0.3f, y - s * 0.12f, x + s * 0.3f, y - s * 0.12f, s * 0.06f, pa_hex(0x8C92A2));
    pa_fill_ellipse(c, x - s * 0.3f, y - s * 0.18f, s * 0.12f + ow, s * 0.04f + ow, OUTC());
    pa_fill_ellipse(c, x + s * 0.3f, y - s * 0.18f, s * 0.12f + ow, s * 0.04f + ow, OUTC());
    pa_fill_ellipse(c, x - s * 0.3f, y - s * 0.18f, s * 0.12f, s * 0.04f, pa_hex(0xC8CCD8));
    pa_fill_ellipse(c, x + s * 0.3f, y - s * 0.18f, s * 0.12f, s * 0.04f, pa_hex(0xC8CCD8));
    o_rrect(c, x - s * 0.2f, y - s * 0.16f, s * 0.4f, s * 0.34f, s * 0.12f, body, ow);
    o_rrect(c, x - s * 0.06f, y - s * 0.06f, s * 0.12f, s * 0.16f, s * 0.05f, evo ? pa_hex(0xFF3A3A) : pa_hex(0x4CE06A), ow * 0.7f);
}

static void icon_bolt(PA_Canvas *c, float x, float y, float s, PA_Color col) {
    PA_Vec2 p[7] = { { x + s * 0.08f, y - s * 0.40f }, { x - s * 0.22f, y + s * 0.04f }, { x - s * 0.02f, y + s * 0.04f },
                     { x - s * 0.10f, y + s * 0.40f }, { x + s * 0.24f, y - s * 0.08f }, { x + s * 0.04f, y - s * 0.08f },
                     { x + s * 0.16f, y - s * 0.40f } };
    o_poly(c, p, 7, col, s * 0.04f);
}

static void icon_brick(PA_Canvas *c, float x, float y, float s, int evo) {
    float ow = s * 0.04f;
    if (evo) {
        pa_line(c, x - s * 0.3f, y, x + s * 0.3f, y, s * 0.09f + ow * 2.0f, OUTC());
        pa_line(c, x - s * 0.3f, y, x + s * 0.3f, y, s * 0.09f, pa_hex(0xB8BECB));
        o_rrect(c, x - s * 0.36f, y - s * 0.2f, s * 0.14f, s * 0.4f, s * 0.04f, pa_hex(0x4A505E), ow);
        o_rrect(c, x + s * 0.22f, y - s * 0.2f, s * 0.14f, s * 0.4f, s * 0.04f, pa_hex(0x4A505E), ow);
        return;
    }
    PA_Vec2 top[4] = { { x - s * 0.32f, y - s * 0.06f }, { x - s * 0.14f, y - s * 0.22f }, { x + s * 0.34f, y - s * 0.22f },
                       { x + s * 0.16f, y - s * 0.06f } };
    PA_Vec2 side[4] = { { x + s * 0.16f, y - s * 0.06f }, { x + s * 0.34f, y - s * 0.22f }, { x + s * 0.34f, y + s * 0.06f },
                        { x + s * 0.16f, y + s * 0.22f } };
    pa_round_rect(c, x - s * 0.32f - ow, y - s * 0.22f - ow, s * 0.66f + ow * 2.0f, s * 0.44f + ow * 2.0f, ow, OUTC());
    pa_fill_rect(c, x - s * 0.32f, y - s * 0.06f, s * 0.48f, s * 0.28f, pa_hex(0xC4523A));
    pa_fill_poly(c, top, 4, pa_hex(0xE07A5C));
    pa_fill_poly(c, side, 4, pa_hex(0x96382A));
    pa_fill_rect(c, x - s * 0.32f, y + s * 0.07f, s * 0.48f, s * 0.025f, pa_hex(0x7A2A20));
    pa_fill_rect(c, x - s * 0.1f, y - s * 0.06f, s * 0.025f, s * 0.13f, pa_hex(0x7A2A20));
    pa_fill_rect(c, x + s * 0.05f, y + s * 0.09f, s * 0.025f, s * 0.13f, pa_hex(0x7A2A20));
}

static void icon_ball(PA_Canvas *c, float x, float y, float r, float t, int evo) {
    o_circle(c, x, y, r, evo ? pa_hex(0x9EEBFF) : PA_RGB(250, 250, 250), r * 0.12f);
    PA_Color dark = evo ? pa_hex(0x2A6CE0) : pa_hex(0x24242C);
    PA_Vec2 pent[5];
    for (int i = 0; i < 5; i++) {
        float a = t + (float)i * PA_TAU / 5.0f;
        pent[i].x = x + cosf(a) * r * 0.32f; pent[i].y = y + sinf(a) * r * 0.32f;
    }
    pa_fill_poly(c, pent, 5, dark);
    for (int i = 0; i < 5; i++) {
        float a = t + (float)i * PA_TAU / 5.0f;
        float bx = x + cosf(a) * r * 0.86f, by = y + sinf(a) * r * 0.86f;
        pa_line(c, pent[i].x, pent[i].y, bx, by, r * 0.08f, dark);
        pa_fill_circle(c, bx, by, r * 0.2f, dark);
    }
}

static void icon_field(PA_Canvas *c, float x, float y, float s, int evo, float t) {
    float r = s * (evo ? 0.36f : 0.32f);
    pa_fill_circle(c, x, y, r, PA_RGBA(255, 59, 47, 110));
    pa_stroke_circle(c, x, y, r, s * 0.1f, OUTC());
    pa_stroke_circle(c, x, y, r, s * 0.06f, evo ? pa_hex(0xFFC21C) : pa_hex(0xFF7A3D));
    for (int i = 0; i < 8; i++) {
        float a = (float)i / 8.0f * PA_TAU + t * 0.5f;
        float fx = x + cosf(a) * r, fy = y + sinf(a) * r;
        PA_Vec2 f[3] = { { fx - s * 0.05f, fy }, { fx + s * 0.01f, fy - s * 0.15f }, { fx + s * 0.05f, fy } };
        pa_fill_poly(c, f, 3, evo ? pa_hex(0xFFE27A) : pa_hex(0xFF9A2E));
    }
    o_circle(c, x, y, s * 0.1f, evo ? pa_hex(0xFFC21C) : pa_hex(0xE8303A), s * 0.035f);
}

static void icon_bottle(PA_Canvas *c, float x, float y, float s, int evo) {
    float ow = s * 0.04f;
    if (evo) {
        o_rrect(c, x - s * 0.24f, y - s * 0.22f, s * 0.48f, s * 0.56f, s * 0.08f, pa_hex(0xD8342E), ow);
        pa_fill_rect(c, x - s * 0.24f, y - s * 0.08f, s * 0.48f, s * 0.05f, pa_hex(0x6E1A16));
        pa_fill_rect(c, x - s * 0.24f, y + s * 0.14f, s * 0.48f, s * 0.05f, pa_hex(0x6E1A16));
        PA_Vec2 f[5] = { { x - s * 0.16f, y - s * 0.24f }, { x - s * 0.08f, y - s * 0.44f }, { x, y - s * 0.32f },
                         { x + s * 0.1f, y - s * 0.48f }, { x + s * 0.18f, y - s * 0.24f } };
        o_poly(c, f, 5, pa_hex(0xFFA21E), ow * 0.8f);
        return;
    }
    o_rrect(c, x - s * 0.18f, y - s * 0.06f, s * 0.36f, s * 0.42f, s * 0.12f, pa_hex(0x38B04A), ow);
    o_rrect(c, x - s * 0.07f, y - s * 0.28f, s * 0.14f, s * 0.26f, s * 0.04f, pa_hex(0x38B04A), ow);
    pa_fill_rect(c, x - s * 0.12f, y + s * 0.02f, s * 0.07f, s * 0.24f, pa_hex(0x9AF0A0));
    PA_Vec2 f[4] = { { x - s * 0.06f, y - s * 0.3f }, { x + s * 0.02f, y - s * 0.48f }, { x + s * 0.12f, y - s * 0.34f },
                     { x + s * 0.05f, y - s * 0.26f } };
    o_poly(c, f, 4, pa_hex(0xFF8A1E), ow * 0.7f);
}

/** Skill icon centred in a box of `size`. */
static void draw_icon(PA_Canvas *c, int id, float x, float y, float s, float t) {
    float ow = s * 0.04f;
    if (id >= IC_EVO(0) && id < IC_MEAT) {
        int w = id - IC_EVO(0);
        PA_Paint g = pa_radial(x, y, s * 0.05f, s * 0.5f);
        pa_stop(&g, 0.0f, PA_RGBA(255, 230, 120, 200));
        pa_stop(&g, 1.0f, PA_RGBA(255, 160, 40, 0));
        pa_fill_ellipse_paint(c, x, y, s * 0.5f, s * 0.5f, &g);
        switch (w) {
        case W_KUNAI: icon_kunai(c, x, y, s, pa_hex(0x9A6CF0), pa_hex(0x3A1E70)); break;
        case W_GUARD: icon_saw(c, x, y, s * 0.34f, pa_hex(0xFFC21C), pa_hex(0x9A5A10), t); break;
        case W_DRONE: icon_drone(c, x, y, s, 1); break;
        case W_BOLT:
            o_circle(c, x - s * 0.1f, y - s * 0.16f, s * 0.16f, pa_hex(0x6A5A98), ow);
            o_circle(c, x + s * 0.12f, y - s * 0.18f, s * 0.19f, pa_hex(0x7A6AA8), ow);
            icon_bolt(c, x, y + s * 0.08f, s * 0.75f, pa_hex(0xFFE24A));
            break;
        case W_BRICK: icon_brick(c, x, y, s, 1); break;
        case W_BALL: icon_ball(c, x, y, s * 0.3f, t, 1); break;
        case W_FIRE: icon_bottle(c, x, y, s, 1); break;
        case W_FIELD: icon_field(c, x, y, s, 1, t); break;
        }
        return;
    }
    if (id < W_COUNT) {
        switch (id) {
        case W_KUNAI: icon_kunai(c, x, y, s, pa_hex(0xC8CCD8), pa_hex(0xD8342E)); break;
        case W_GUARD: icon_saw(c, x, y, s * 0.32f, pa_hex(0xE8303A), pa_hex(0x2A2A34), t); break;
        case W_DRONE: icon_drone(c, x, y, s, 0); break;
        case W_BOLT: icon_bolt(c, x, y, s, pa_hex(0x6EE0FF)); break;
        case W_BRICK: icon_brick(c, x, y, s, 0); break;
        case W_BALL: icon_ball(c, x, y, s * 0.3f, t, 0); break;
        case W_FIRE: icon_bottle(c, x, y, s, 0); break;
        case W_FIELD: icon_field(c, x, y, s, 0, t); break;
        }
        return;
    }
    if (id < IC_EVO(0)) {
        switch (id - W_COUNT) {
        case P_POWER:
            o_rrect(c, x - s * 0.12f, y - s * 0.04f, s * 0.24f, s * 0.36f, s * 0.03f, pa_hex(0xE8A21C), ow);
            {
                PA_Vec2 tip[5] = { { x - s * 0.12f, y - s * 0.04f }, { x - s * 0.12f, y - s * 0.14f }, { x, y - s * 0.38f },
                                   { x + s * 0.12f, y - s * 0.14f }, { x + s * 0.12f, y - s * 0.04f } };
                o_poly(c, tip, 5, pa_hex(0xD86A3A), ow);
            }
            pa_fill_rect(c, x - s * 0.08f, y, s * 0.05f, s * 0.28f, pa_hex(0xFFE27A));
            break;
        case P_BRACER:
            o_rrect(c, x - s * 0.26f, y - s * 0.2f, s * 0.52f, s * 0.4f, s * 0.12f, pa_hex(0x8C92A2), ow);
            pa_fill_rect(c, x - s * 0.26f, y - s * 0.05f, s * 0.52f, s * 0.04f, pa_hex(0x5A6070));
            o_circle(c, x, y - s * 0.02f, s * 0.09f, pa_hex(0x4AB8FF), ow * 0.8f);
            break;
        case P_FUEL:
            o_rrect(c, x - s * 0.22f, y - s * 0.2f, s * 0.44f, s * 0.52f, s * 0.06f, pa_hex(0xE8303A), ow);
            o_rrect(c, x + s * 0.02f, y - s * 0.34f, s * 0.14f, s * 0.14f, s * 0.03f, pa_hex(0x4A505E), ow);
            pa_line(c, x - s * 0.14f, y - s * 0.1f, x + s * 0.14f, y + s * 0.22f, s * 0.05f, pa_hex(0xA81E24));
            pa_line(c, x + s * 0.14f, y - s * 0.1f, x - s * 0.14f, y + s * 0.22f, s * 0.05f, pa_hex(0xA81E24));
            break;
        case P_CUBE: {
            PA_Vec2 top[4] = { { x, y - s * 0.34f }, { x + s * 0.28f, y - s * 0.18f }, { x, y - s * 0.02f }, { x - s * 0.28f, y - s * 0.18f } };
            PA_Vec2 lf[4] = { { x - s * 0.28f, y - s * 0.18f }, { x, y - s * 0.02f }, { x, y + s * 0.32f }, { x - s * 0.28f, y + s * 0.16f } };
            PA_Vec2 rt[4] = { { x + s * 0.28f, y - s * 0.18f }, { x, y - s * 0.02f }, { x, y + s * 0.32f }, { x + s * 0.28f, y + s * 0.16f } };
            PA_Vec2 all[6] = { top[0], top[1], rt[3], rt[2], lf[3], top[3] };
            o_poly(c, all, 6, pa_hex(0x3ACCF0), ow);
            pa_fill_poly(c, top, 4, pa_hex(0xA8F0FF));
            pa_fill_poly(c, lf, 4, pa_hex(0x38B8E8));
            pa_fill_poly(c, rt, 4, pa_hex(0x2280C8));
            break;
        }
        case P_BOOK:
            o_rrect(c, x - s * 0.24f, y - s * 0.3f, s * 0.48f, s * 0.6f, s * 0.05f, pa_hex(0x3A9A4A), ow);
            pa_fill_rect(c, x - s * 0.24f, y + s * 0.18f, s * 0.48f, s * 0.08f, pa_hex(0xF4F0E4));
            pa_fill_circle(c, x - s * 0.06f, y - s * 0.06f, s * 0.08f, pa_hex(0xFF5A6A));
            pa_fill_circle(c, x + s * 0.06f, y - s * 0.06f, s * 0.08f, pa_hex(0xFF5A6A));
            {
                PA_Vec2 h[3] = { { x - s * 0.135f, y - s * 0.03f }, { x + s * 0.135f, y - s * 0.03f }, { x, y + s * 0.12f } };
                pa_fill_poly(c, h, 3, pa_hex(0xFF5A6A));
            }
            break;
        case P_SHOES: {
            PA_Vec2 sh[6] = { { x - s * 0.3f, y + s * 0.14f }, { x - s * 0.26f, y - s * 0.18f }, { x - s * 0.04f, y - s * 0.16f },
                              { x + s * 0.06f, y - s * 0.02f }, { x + s * 0.32f, y + s * 0.04f }, { x + s * 0.32f, y + s * 0.14f } };
            o_poly(c, sh, 6, pa_hex(0xE8303A), ow);
            pa_fill_rect(c, x - s * 0.3f, y + s * 0.1f, s * 0.62f, s * 0.06f, PA_RGB(250, 250, 250));
            pa_line(c, x - s * 0.18f, y - s * 0.08f, x - s * 0.02f, y - s * 0.02f, s * 0.04f, PA_RGB(250, 250, 250));
            break;
        }
        case P_RAGE:
            o_rrect(c, x - s * 0.06f, y - s * 0.36f, s * 0.12f, s * 0.16f, s * 0.03f, pa_hex(0xA86A3A), ow);
            o_circle(c, x, y + s * 0.06f, s * 0.24f, pa_hex(0xE8303A), ow);
            pa_fill_ellipse(c, x, y + s * 0.1f, s * 0.2f, s * 0.14f, pa_hex(0xB81E28));
            pa_fill_circle(c, x - s * 0.08f, y - s * 0.03f, s * 0.06f, pa_hex(0xFFB0B0));
            break;
        case P_OIL:
            o_rrect(c, x - s * 0.22f, y - s * 0.28f, s * 0.44f, s * 0.58f, s * 0.08f, pa_hex(0x2E7A5A), ow);
            pa_fill_rect(c, x - s * 0.22f, y - s * 0.14f, s * 0.44f, s * 0.05f, pa_hex(0x1C4A38));
            pa_fill_rect(c, x - s * 0.22f, y + s * 0.12f, s * 0.44f, s * 0.05f, pa_hex(0x1C4A38));
            {
                PA_Vec2 d[4] = { { x, y - s * 0.1f }, { x + s * 0.08f, y + s * 0.03f }, { x, y + s * 0.09f }, { x - s * 0.08f, y + s * 0.03f } };
                pa_fill_poly(c, d, 4, pa_hex(0xFFC21C));
            }
            break;
        }
        return;
    }
    if (id == IC_MEAT) { art_pickup(c, x, y, s / 22.0f, G_MEAT); return; }
    if (id == IC_COINS) {
        coin_icon(c, x - s * 0.12f, y + s * 0.08f, s * 0.2f);
        coin_icon(c, x + s * 0.12f, y + s * 0.04f, s * 0.2f);
        coin_icon(c, x, y - s * 0.12f, s * 0.2f);
    }
}

static int pick_icon(const Pick *p) {
    switch (p->kind) {
    case 0: return p->id;
    case 1: return IC_ITEM(p->id);
    case 2: return IC_EVO(p->id);
    case 3: return IC_MEAT;
    default: return IC_COINS;
    }
}

/* --------------------------------------------------------------- ground -- */
/* ------------------------------------------------------------ street props */
/* Props are drawn once into the cached ground blocks, so a busy street costs
   nothing per frame. Everything is world units mapped through SX/SY. */
static void prop_shadow(PA_Canvas *c, float x, float y, float w, float h, float r) {
    pa_round_rect(c, SX(x + 5.0f), SY(y + 6.0f), w * L.s, h * L.s, r * L.s, PA_RGBA(14, 12, 28, 120));
}

/** Axis-aligned rect in a car's local frame: x along its length. */
static void car_rect(PA_Canvas *c, float cx, float cy, int vert, float lx, float ly, float lw, float lh, float r,
                     PA_Color col, float ow) {
    float x = vert ? cx + ly : cx + lx, y = vert ? cy + lx : cy + ly;
    float w = vert ? lh : lw, h = vert ? lw : lh;
    if (ow > 0.0f) o_rrect(c, SX(x), SY(y), w * L.s, h * L.s, r * L.s, col, ow * L.s);
    else pa_round_rect(c, SX(x), SY(y), w * L.s, h * L.s, r * L.s, col);
}

static void prop_car(PA_Canvas *c, float cx, float cy, int vert, uint32_t paint, int burnt, int flip) {
    float L2 = 48.0f, W2 = 23.0f;
    float sgn = flip ? -1.0f : 1.0f;
    PA_Color body = burnt ? pa_hex(0x4A4448) : pa_hex(paint);
    PA_Color glass = burnt ? pa_hex(0x1E1C22) : pa_hex(0x3A4C70);
    if (vert) prop_shadow(c, cx - W2, cy - L2, W2 * 2.0f, L2 * 2.0f, 10.0f);
    else prop_shadow(c, cx - L2, cy - W2, L2 * 2.0f, W2 * 2.0f, 10.0f);
    /* wheels peeking out */
    for (int i = 0; i < 4; i++) {
        float lx = (i & 1) ? 22.0f : -36.0f, ly = (i & 2) ? W2 - 3.0f : -W2 - 4.0f;
        car_rect(c, cx, cy, vert, lx, ly, 15.0f, 7.0f, 2.0f, pa_hex(0x22222A), 0.0f);
    }
    car_rect(c, cx, cy, vert, -L2, -W2, L2 * 2.0f, W2 * 2.0f, 11.0f, body, 3.0f);
    car_rect(c, cx, cy, vert, -L2 + 4.0f, -W2 + 3.0f, L2 * 2.0f - 8.0f, 5.0f, 3.0f, pa_shade(body, 0.25f), 0.0f);
    /* glass and roof */
    car_rect(c, cx, cy, vert, sgn > 0 ? 6.0f : -26.0f, -W2 + 5.0f, 20.0f, W2 * 2.0f - 10.0f, 5.0f, glass, 1.5f);
    car_rect(c, cx, cy, vert, sgn > 0 ? -30.0f : 16.0f, -W2 + 6.0f, 14.0f, W2 * 2.0f - 12.0f, 4.0f, glass, 1.5f);
    car_rect(c, cx, cy, vert, -14.0f, -W2 + 5.0f, 20.0f, W2 * 2.0f - 10.0f, 4.0f, pa_shade(body, burnt ? 0.1f : 0.18f), 0.0f);
    if (!burnt) {
        car_rect(c, cx, cy, vert, sgn > 0 ? 6.0f : -26.0f, -W2 + 7.0f, 4.0f, W2 * 2.0f - 14.0f, 2.0f, pa_hex(0x8FB0E0), 0.0f);
        float fx = sgn > 0 ? L2 - 6.0f : -L2 + 1.0f, bx = sgn > 0 ? -L2 + 1.0f : L2 - 6.0f;
        car_rect(c, cx, cy, vert, fx, -W2 + 3.0f, 5.0f, 8.0f, 2.0f, pa_hex(0xFFF2A0), 0.0f);
        car_rect(c, cx, cy, vert, fx, W2 - 11.0f, 5.0f, 8.0f, 2.0f, pa_hex(0xFFF2A0), 0.0f);
        car_rect(c, cx, cy, vert, bx, -W2 + 3.0f, 5.0f, 7.0f, 2.0f, pa_hex(0xE8303A), 0.0f);
        car_rect(c, cx, cy, vert, bx, W2 - 10.0f, 5.0f, 7.0f, 2.0f, pa_hex(0xE8303A), 0.0f);
        if (paint == 0xF6C340) car_rect(c, cx, cy, vert, -12.0f, -5.0f, 12.0f, 10.0f, 2.0f, pa_hex(0x22222A), 0.0f);
    } else {
        for (int i = 0; i < 4; i++) {
            float lx = -30.0f + (float)i * 17.0f, ly = (float)((i * 7) % 11) - 8.0f;
            car_rect(c, cx, cy, vert, lx, ly, 9.0f, 6.0f, 3.0f, pa_hex(0x2A2628), 0.0f);
        }
        car_rect(c, cx, cy, vert, 8.0f, -3.0f, 5.0f, 5.0f, 2.5f, pa_hex(0xFF8A1E), 0.0f);
    }
}

static void prop_barrier(PA_Canvas *c, float x, float y) {
    prop_shadow(c, x, y, 72.0f, 16.0f, 4.0f);
    o_rrect(c, SX(x), SY(y), 72.0f * L.s, 16.0f * L.s, 4.0f * L.s, pa_hex(0xF2F2F2), 2.6f * L.s);
    for (int i = 0; i < 4; i++) {
        float sx = x + 4.0f + (float)i * 18.0f;
        PA_Vec2 st[4] = { { SX(sx), SY(y + 16.0f) }, { SX(sx + 7.0f), SY(y) }, { SX(sx + 15.0f), SY(y) }, { SX(sx + 8.0f), SY(y + 16.0f) } };
        pa_fill_poly(c, st, 4, pa_hex(0xE8303A));
    }
    pa_fill_rect(c, SX(x + 2.0f), SY(y + 2.0f), 68.0f * L.s, 2.5f * L.s, PA_RGBA(255, 255, 255, 120));
}

static void prop_dumpster(PA_Canvas *c, float x, float y) {
    prop_shadow(c, x, y, 54.0f, 34.0f, 5.0f);
    o_rrect(c, SX(x), SY(y), 54.0f * L.s, 34.0f * L.s, 5.0f * L.s, pa_hex(0x3E8A4E), 3.0f * L.s);
    pa_round_rect(c, SX(x + 3.0f), SY(y + 3.0f), 48.0f * L.s, 12.0f * L.s, 3.0f * L.s, pa_hex(0x56A866));
    pa_fill_rect(c, SX(x), SY(y + 16.0f), 54.0f * L.s, 2.5f * L.s, OUTC());
    pa_fill_rect(c, SX(x + 24.0f), SY(y + 20.0f), 8.0f * L.s, 9.0f * L.s, pa_hex(0x2C6A3A));
}

static void prop_cone(PA_Canvas *c, float x, float y) {
    pa_fill_ellipse(c, SX(x + 4.0f), SY(y + 5.0f), 9.0f * L.s, 8.0f * L.s, PA_RGBA(14, 12, 28, 110));
    o_rrect(c, SX(x - 8.0f), SY(y - 8.0f), 16.0f * L.s, 16.0f * L.s, 3.0f * L.s, pa_hex(0xC8501A), 2.0f * L.s);
    o_circle(c, SX(x), SY(y), 6.0f * L.s, pa_hex(0xFF7A26), 1.6f * L.s);
    pa_stroke_circle(c, SX(x), SY(y), 3.6f * L.s, 1.8f * L.s, PA_RGB(250, 250, 250));
    pa_fill_circle(c, SX(x), SY(y), 1.6f * L.s, pa_hex(0xFF9A4A));
}

static void prop_tires(PA_Canvas *c, float x, float y, int n) {
    for (int i = 0; i < n; i++) {
        float tx = x + (float)i * 15.0f, ty = y + (float)((i & 1) * 8);
        pa_fill_circle(c, SX(tx + 4.0f), SY(ty + 5.0f), 12.0f * L.s, PA_RGBA(14, 12, 28, 110));
        o_circle(c, SX(tx), SY(ty), 11.0f * L.s, pa_hex(0x2C2C36), 2.0f * L.s);
        pa_stroke_circle(c, SX(tx), SY(ty), 7.5f * L.s, 1.5f * L.s, pa_hex(0x45454F));
        pa_fill_circle(c, SX(tx), SY(ty), 4.5f * L.s, pa_hex(0x5E5E70));
    }
}

static void prop_crate(PA_Canvas *c, float x, float y, float sz) {
    prop_shadow(c, x, y, sz, sz, 3.0f);
    o_rrect(c, SX(x), SY(y), sz * L.s, sz * L.s, 3.0f * L.s, pa_hex(0xB8803E), 2.6f * L.s);
    pa_stroke_rect(c, SX(x + 4.0f), SY(y + 4.0f), (sz - 8.0f) * L.s, (sz - 8.0f) * L.s, 2.0f * L.s, pa_hex(0x8A5A28));
    pa_line(c, SX(x + 4.0f), SY(y + 4.0f), SX(x + sz - 4.0f), SY(y + sz - 4.0f), 2.4f * L.s, pa_hex(0x8A5A28));
    pa_fill_rect(c, SX(x + 2.0f), SY(y + 2.0f), (sz - 4.0f) * L.s, 2.0f * L.s, pa_hex(0xD8A060));
}

static void prop_rubble(PA_Canvas *c, float x, float y, uint32_t h) {
    for (int i = 0; i < 4; i++) {
        float rx = x + (float)((h >> (i * 4)) & 15) * 2.2f - 16.0f, ry = y + (float)((h >> (i * 4 + 2)) & 15) * 1.6f - 12.0f;
        float r = 5.0f + (float)((h >> (i * 3)) & 3) * 2.0f;
        PA_Vec2 p[5];
        for (int k = 0; k < 5; k++) {
            float a = (float)k * PA_TAU / 5.0f + (float)i;
            float rr = r * (0.7f + 0.3f * (float)((h >> (k + i)) & 1));
            p[k].x = SX(rx + cosf(a) * rr); p[k].y = SY(ry + sinf(a) * rr * 0.8f);
        }
        o_poly(c, p, 5, (i & 1) ? pa_hex(0x8A8A9A) : pa_hex(0x6A6A7C), 1.6f * L.s);
    }
}

static void prop_hydrant(PA_Canvas *c, float x, float y) {
    pa_fill_circle(c, SX(x + 4.0f), SY(y + 5.0f), 9.0f * L.s, PA_RGBA(14, 12, 28, 110));
    o_rrect(c, SX(x - 11.0f), SY(y - 3.0f), 22.0f * L.s, 6.0f * L.s, 3.0f * L.s, pa_hex(0xC42A2A), 1.8f * L.s);
    o_circle(c, SX(x), SY(y), 7.5f * L.s, pa_hex(0xE8383A), 1.8f * L.s);
    pa_fill_circle(c, SX(x), SY(y), 3.5f * L.s, pa_hex(0xFF8A80));
}

static void prop_bags(PA_Canvas *c, float x, float y) {
    for (int i = 0; i < 3; i++) {
        float bx = x + (float)i * 12.0f, by = y + (float)((i * 5) % 9);
        pa_fill_ellipse(c, SX(bx + 4.0f), SY(by + 5.0f), 10.0f * L.s, 8.0f * L.s, PA_RGBA(14, 12, 28, 110));
        pa_fill_ellipse(c, SX(bx), SY(by), 11.5f * L.s, 9.5f * L.s, OUTC());
        pa_fill_ellipse(c, SX(bx), SY(by), 9.5f * L.s, 7.5f * L.s, pa_hex(0x34343F));
        pa_fill_ellipse(c, SX(bx - 3.0f), SY(by - 3.0f), 3.5f * L.s, 2.5f * L.s, pa_hex(0x5A5A6A));
    }
}

static void crack(PA_Canvas *c, float x, float y, uint32_t h, int alpha) {
    PA_Vec2 p[6];
    float a = (float)(h & 255) / 255.0f * PA_TAU;
    p[0].x = SX(x); p[0].y = SY(y);
    for (int i = 1; i < 6; i++) {
        a += ((float)((h >> (i * 3)) & 7) - 3.5f) * 0.25f;
        x += cosf(a) * 12.0f; y += sinf(a) * 12.0f;
        p[i].x = SX(x); p[i].y = SY(y);
    }
    pa_stroke_poly(c, p, 6, 0, 2.0f * L.s, PA_RGBA(20, 18, 34, alpha));
    pa_line(c, p[2].x, p[2].y, p[2].x + 8.0f * L.s, p[2].y - 6.0f * L.s, 1.4f * L.s, PA_RGBA(20, 18, 34, alpha));
}

/* ------------------------------------------------------------ ground ----- */
#define BLOCK 640.0f
#define ROAD  150.0f
#define TILE  64.0f

static uint32_t hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void wrect(PA_Canvas *c, float x, float y, float w, float h, PA_Color col) {
    fast_rect(c, SX(x), SY(y), w * L.s, h * L.s, col, 255);
}

static uint32_t g_lcg;
static float lrand(void) { g_lcg = g_lcg * 1664525u + 1013904223u; return (float)(g_lcg >> 8) / 16777216.0f; }

/** One 640-unit city block: crossing roads on its top and left, a raised
    pavement square, paint, props, grime. Drawn once into the cache. */
static void draw_block(PA_Canvas *c, int bx, int by) {
    float ox = (float)bx * BLOCK, oy = (float)by * BLOCK;
    uint32_t h = hash2(bx, by);
    g_lcg = hash2(bx * 7 + 3, by * 13 + 5) | 1u;
    PA_Color asphalt = pa_hex(0x5A5560), pave = pa_hex(0x8E8898), seam = pa_hex(0x787284), dark_tile = pa_hex(0x857F90);
    PA_Color curb = pa_hex(0xB6B0BE), curb_d = pa_hex(0x3A3440), paint = PA_RGBA(232, 226, 208, 235);
    pa_clear(c, asphalt);
    float px0 = ox + ROAD, py0 = oy + ROAD, ps = BLOCK - ROAD;
    /* oil and grime on the road */
    for (int i = 0; i < 4; i++) {
        float sx = ox + lrand() * BLOCK, sy = oy + lrand() * ROAD;
        if (i & 1) { sx = ox + lrand() * ROAD; sy = oy + lrand() * BLOCK; }
        pa_fill_ellipse(c, SX(sx), SY(sy), (14.0f + lrand() * 22.0f) * L.s, (9.0f + lrand() * 12.0f) * L.s, PA_RGBA(26, 24, 40, 70));
    }
    /* curb shadow, curb, pavement */
    wrect(c, px0 - 6.0f, py0 - 6.0f, ps + 18.0f, ps + 18.0f, pa_hex(0x2C2C3E));
    wrect(c, px0 - 8.0f, py0 - 8.0f, ps + 12.0f, ps + 12.0f, curb_d);
    wrect(c, px0 - 6.0f, py0 - 6.0f, ps + 8.0f, ps + 8.0f, curb);
    wrect(c, px0 - 6.0f, py0 + ps - 1.0f, ps + 8.0f, 3.0f, pa_hex(0x8C8BA2));
    wrect(c, px0, py0, ps - 4.0f, ps - 4.0f, pave);
    for (int ty = 0; ty < (int)(ps / TILE) + 1; ty++)
        for (int tx = 0; tx < (int)(ps / TILE) + 1; tx++) {
            uint32_t th = hash2(bx * 31 + tx, by * 17 + ty);
            if ((th & 7) == 0) {
                float tw = TILE, tht = TILE;
                if (px0 + (float)tx * TILE + tw > px0 + ps - 4.0f) tw = px0 + ps - 4.0f - (px0 + (float)tx * TILE);
                if (py0 + (float)ty * TILE + tht > py0 + ps - 4.0f) tht = py0 + ps - 4.0f - (py0 + (float)ty * TILE);
                if (tw > 0.0f && tht > 0.0f) wrect(c, px0 + (float)tx * TILE, py0 + (float)ty * TILE, tw, tht, dark_tile);
            }
        }
    for (float t = TILE; t < ps - 4.0f; t += TILE) {
        wrect(c, px0 + t - 1.0f, py0, 2.5f, ps - 4.0f, seam);
        wrect(c, px0, py0 + t - 1.0f, ps - 4.0f, 2.5f, seam);
    }
    if ((h & 3) == 1) {
        float cx = px0 + ps * 0.5f, cy = py0 + ps * 0.5f, r = ps * 0.3f;
        pa_stroke_circle(c, SX(cx), SY(cy), r * L.s, 4.0f * L.s, PA_RGBA(214, 212, 228, 160));
        pa_fill_rect(c, SX(cx - r), SY(cy - 2.0f), r * 2.0f * L.s, 4.0f * L.s, PA_RGBA(214, 212, 228, 160));
    } else if ((h & 3) == 2) {
        float cx = px0 + 120.0f + (float)((h >> 4) % 160), cy = py0 + 140.0f + (float)((h >> 12) % 160);
        pa_round_rect(c, SX(cx + 5.0f), SY(cy + 7.0f), 124.0f * L.s, 64.0f * L.s, 10.0f * L.s, PA_RGBA(14, 12, 28, 110));
        o_rrect(c, SX(cx), SY(cy), 120.0f * L.s, 60.0f * L.s, 9.0f * L.s, pa_hex(0xA4A3B8), 2.6f * L.s);
        pa_round_rect(c, SX(cx) + 7.0f * L.s, SY(cy) + 7.0f * L.s, 106.0f * L.s, 46.0f * L.s, 6.0f * L.s, pa_hex(0x5E8A4E));
        for (int k = 0; k < 3; k++) {
            float bxp = SX(cx + 24.0f + (float)k * 36.0f), byp = SY(cy + 30.0f);
            pa_fill_circle(c, bxp, byp + 3.0f * L.s, 15.0f * L.s, OUTC());
            pa_fill_circle(c, bxp, byp, 15.0f * L.s, pa_hex(0x6FB04A));
            pa_fill_circle(c, bxp - 4.0f * L.s, byp - 5.0f * L.s, 6.0f * L.s, pa_hex(0x8FD060));
        }
    }
    /* cracks on both surfaces, about 15% ink */
    for (int i = 0; i < 6; i++) {
        float cx = ox + lrand() * BLOCK, cy = oy + lrand() * BLOCK;
        crack(c, cx, cy, hash2(bx * 5 + i, by * 3 - i), 40);
    }
    {
        float mx = ox + ROAD + 200.0f + (float)(h % 200), my = oy + ROAD * 0.5f;
        pa_fill_circle(c, SX(mx), SY(my), 16.0f * L.s, pa_hex(0x34344A));
        pa_fill_circle(c, SX(mx), SY(my), 14.0f * L.s, pa_hex(0x56566C));
        pa_stroke_circle(c, SX(mx), SY(my), 10.0f * L.s, 2.0f * L.s, pa_hex(0x3E3E52));
        wrect(c, mx - 9.0f, my - 1.0f, 18.0f, 2.0f, pa_hex(0x3E3E52));
    }
    for (float t = 0.0f; t < BLOCK; t += 64.0f) {
        if (t > ROAD + 64.0f && t < BLOCK - 20.0f) {
            pa_fill_rect(c, SX(ox + t), SY(oy + ROAD * 0.5f - 2.5f), 34.0f * L.s, 5.0f * L.s, paint);
            pa_fill_rect(c, SX(ox + ROAD * 0.5f - 2.5f), SY(oy + t), 5.0f * L.s, 34.0f * L.s, paint);
        }
    }
    for (int k = 0; k < 7; k++) {
        float s0 = 12.0f + (float)k * 19.0f;
        pa_fill_rect(c, SX(ox + ROAD + 8.0f), SY(oy + s0), 46.0f * L.s, 10.0f * L.s, paint);
        pa_fill_rect(c, SX(ox + s0), SY(oy + ROAD + 8.0f), 10.0f * L.s, 46.0f * L.s, paint);
    }

    /* props: parked and wrecked cars, barriers, then kerbside clutter */
    static const uint32_t paints[5] = { 0xE8303A, 0x3E7FF0, 0xF6C340, 0xF1F2F7, 0x3EA860 };
    int ncar = 1 + (lrand() < 0.6f);
    for (int i = 0; i < ncar; i++) {
        float cx = ox + 270.0f + (float)i * 190.0f + lrand() * 90.0f;
        float cy = oy + (lrand() < 0.5f ? 34.0f : 116.0f);
        prop_car(c, cx, cy, 0, paints[(int)(lrand() * 5.0f) % 5], lrand() < 0.28f, cy > oy + 75.0f);
    }
    if (lrand() < 0.85f) {
        float cy = oy + 270.0f + lrand() * 300.0f, cx = ox + (lrand() < 0.5f ? 34.0f : 116.0f);
        prop_car(c, cx, cy, 1, paints[(int)(lrand() * 5.0f) % 5], lrand() < 0.28f, cx > ox + 75.0f);
    }
    if (lrand() < 0.7f) prop_barrier(c, ox + 40.0f + lrand() * 40.0f, oy + 230.0f + lrand() * 40.0f);
    if (lrand() < 0.5f) prop_rubble(c, ox + 300.0f + lrand() * 250.0f, oy + 70.0f, hash2(bx, by * 9));
    for (int i = 0; i < 6; i++) {
        float along = 30.0f + lrand() * (ps - 90.0f);
        int top = lrand() < 0.5f;
        float x = top ? px0 + along : px0 + 18.0f;
        float y = top ? py0 + 18.0f : py0 + along;
        float pick = lrand();
        if (pick < 0.16f) prop_dumpster(c, x, y);
        else if (pick < 0.34f) { prop_cone(c, x + 6.0f, y + 6.0f); prop_cone(c, x + 30.0f, y + 10.0f); if (pick < 0.25f) prop_cone(c, x + 16.0f, y + 26.0f); }
        else if (pick < 0.46f) prop_tires(c, x + 10.0f, y + 10.0f, 2 + (pick < 0.40f));
        else if (pick < 0.60f) { prop_crate(c, x, y, 28.0f); if (pick < 0.53f) prop_crate(c, x + 30.0f, y + 6.0f, 22.0f); }
        else if (pick < 0.72f) prop_rubble(c, x + 16.0f, y + 12.0f, hash2(bx * 11 + i, by));
        else if (pick < 0.82f) prop_hydrant(c, x + 6.0f, y + 6.0f);
        else if (pick < 0.92f) prop_bags(c, x + 10.0f, y + 10.0f);
        else icon_ball(c, SX(x + 10.0f), SY(y + 10.0f), 8.0f * L.s, 0.4f, 0);
    }

    /* grain: per-pixel jitter and sparse specks, so no surface reads flat */
    for (int yy = 0; yy < c->h; yy++) {
        uint32_t *row = c->px + (size_t)yy * (size_t)c->w;
        for (int xx = 0; xx < c->w; xx++) {
            uint32_t hv = ((uint32_t)(xx + bx * 4096) * 73856093u) ^ ((uint32_t)(yy + by * 4096) * 19349663u);
            hv *= 2654435761u;
            int n = (int)((hv >> 24) & 7) - 4;
            if (((hv >> 8) & 63) == 0) n -= 12;
            else if (((hv >> 8) & 127) == 1) n += 9;
            uint32_t p = row[xx];
            int r = (int)((p >> 16) & 255) + n, g = (int)((p >> 8) & 255) + n, b = (int)(p & 255) + n;
            r = r < 0 ? 0 : r > 255 ? 255 : r;
            g = g < 0 ? 0 : g > 255 ? 255 : g;
            b = b < 0 ? 0 : b > 255 ? 255 : b;
            row[xx] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

/* Ground blocks are rendered once and kept: the street costs one copy per
   frame however much is painted on it. */
#define GC_N 9
typedef struct { int bx, by, valid; unsigned stamp; PA_Canvas cv; } GBlock;
static GBlock g_gc[GC_N];
static unsigned g_gc_clock;
static float g_gc_scale = -1.0f;

static void gc_reset(void) {
    for (int i = 0; i < GC_N; i++) {
        if (g_gc[i].cv.px) pa_canvas_free(&g_gc[i].cv);
        g_gc[i].valid = 0;
    }
    g_gc_scale = L.s;
}

static GBlock *gc_get(int bx, int by) {
    int lru = -1;
    for (int i = 0; i < GC_N; i++)
        if (g_gc[i].valid && g_gc[i].bx == bx && g_gc[i].by == by) { g_gc[i].stamp = ++g_gc_clock; return &g_gc[i]; }
    for (int i = 0; i < GC_N && lru < 0; i++) if (!g_gc[i].valid) lru = i;
    if (lru < 0) { lru = 0; for (int i = 1; i < GC_N; i++) if (g_gc[i].stamp < g_gc[lru].stamp) lru = i; }
    GBlock *b = &g_gc[lru];
    int sz = (int)ceilf(BLOCK * L.s) + 2;
    if (!b->cv.px) { if (!pa_canvas_init(&b->cv, sz, sz)) return NULL; }
    else if (b->cv.w != sz) { if (!pa_canvas_resize(&b->cv, sz, sz)) return NULL; }
    float sv[4] = { g_vx, g_vy, g_vcx, g_vcy };
    g_vx = (float)bx * BLOCK; g_vy = (float)by * BLOCK; g_vcx = 0.0f; g_vcy = 0.0f;
    pa_clip_reset(&b->cv);
    draw_block(&b->cv, bx, by);
    g_vx = sv[0]; g_vy = sv[1]; g_vcx = sv[2]; g_vcy = sv[3];
    b->bx = bx; b->by = by; b->valid = 1; b->stamp = ++g_gc_clock;
    return b;
}

static void draw_ground(PA_Canvas *c) {
    if (fabsf(g_gc_scale - L.s) > 0.001f) gc_reset();
    float hw = (float)L.w * 0.5f / L.s + 40.0f, hh = (float)L.h * 0.5f / L.s + 40.0f;
    float x0 = g_vx - hw, x1 = g_vx + hw, y0 = g_vy - hh, y1 = g_vy + hh;
    int bx0 = (int)floorf(x0 / BLOCK), bx1 = (int)floorf(x1 / BLOCK);
    int by0 = (int)floorf(y0 / BLOCK), by1 = (int)floorf(y1 / BLOCK);
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++) {
            GBlock *b = gc_get(bx, by);
            if (!b) continue;
            int sx0 = (int)floorf(SX((float)bx * BLOCK)), sy0 = (int)floorf(SY((float)by * BLOCK));
            int sx1 = (int)floorf(SX((float)(bx + 1) * BLOCK)), sy1 = (int)floorf(SY((float)(by + 1) * BLOCK));
            int cx0 = sx0 < c->clip_x0 ? c->clip_x0 : sx0, cx1 = sx1 > c->clip_x1 ? c->clip_x1 : sx1;
            int cy0 = sy0 < c->clip_y0 ? c->clip_y0 : sy0, cy1 = sy1 > c->clip_y1 ? c->clip_y1 : sy1;
            if (cx1 - cx0 > b->cv.w - (cx0 - sx0)) cx1 = cx0 + b->cv.w - (cx0 - sx0);
            if (cx0 >= cx1) continue;
            for (int y = cy0; y < cy1; y++) {
                int ly = y - sy0;
                if (ly >= b->cv.h) break;
                memcpy(c->px + (size_t)y * (size_t)c->w + cx0, b->cv.px + (size_t)ly * (size_t)b->cv.w + (cx0 - sx0),
                       (size_t)(cx1 - cx0) * 4u);
            }
        }
}

/* Additive light from the cached falloff. */
static void glow(PA_Canvas *c, float cx, float cy, float rx, float ry, uint32_t rgb, float k) {
    if (rx < 1.0f || ry < 1.0f || k <= 0.0f) return;
    int x0 = (int)floorf(cx - rx), x1 = (int)ceilf(cx + rx), y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
    if (x0 < c->clip_x0) x0 = c->clip_x0;
    if (y0 < c->clip_y0) y0 = c->clip_y0;
    if (x1 > c->clip_x1) x1 = c->clip_x1;
    if (y1 > c->clip_y1) y1 = c->clip_y1;
    if (x0 >= x1 || y0 >= y1) return;
    static int lx[4096];
    if (x1 - x0 > 4096) x1 = x0 + 4096;
    float sx = (float)GLOW_N / (rx * 2.0f), sy = (float)GLOW_N / (ry * 2.0f);
    for (int x = x0; x < x1; x++) {
        int v = (int)(((float)x + 0.5f - (cx - rx)) * sx);
        lx[x - x0] = v < 0 ? 0 : v >= GLOW_N ? GLOW_N - 1 : v;
    }
    int kr = (int)((float)((rgb >> 16) & 255) * k), kg = (int)((float)((rgb >> 8) & 255) * k), kb = (int)((float)(rgb & 255) * k);
    for (int y = y0; y < y1; y++) {
        int ly = (int)(((float)y + 0.5f - (cy - ry)) * sy);
        ly = ly < 0 ? 0 : ly >= GLOW_N ? GLOW_N - 1 : ly;
        const uint8_t *row = g_glow_lut + ly * GLOW_N;
        uint32_t *d = c->px + (size_t)y * (size_t)c->w;
        for (int x = x0; x < x1; x++) {
            int v = row[lx[x - x0]];
            if (!v) continue;
            uint32_t p = d[x];
            int r = (int)((p >> 16) & 255) + ((kr * v) >> 8);
            int g = (int)((p >> 8) & 255) + ((kg * v) >> 8);
            int b = (int)(p & 255) + ((kb * v) >> 8);
            d[x] = ((uint32_t)(r > 255 ? 255 : r) << 16) | ((uint32_t)(g > 255 ? 255 : g) << 8) | (uint32_t)(b > 255 ? 255 : b);
        }
    }
}

/* ------------------------------------------------------------ world fx --- */
/* Gore splats are baked: eight dot patterns, blitted with a fade. Three
   times the kills means three times the splats, and per-dot fills cost more
   than the swarm. */
#define SPLATS 8
static Sprite g_splat[SPLATS];
static float  g_splat_s = -1.0f;
static void art_splat(PA_Canvas *c, float x, float y, float k, int f) {
    uint32_t h = (uint32_t)(f * 7919 + 13) * 2654435761u;
    float r0 = 12.0f;
    for (int i = 0; i < 6; i++) {
        float ox = ((float)((h >> (i * 5)) & 31) - 15.5f) / 15.5f * r0 * 1.3f;
        float oy = ((float)((h >> (i * 5 + 2)) & 31) - 15.5f) / 15.5f * r0 * 0.9f;
        float r = (i == 0 ? 3.4f : 1.4f + (float)((h >> (i * 3)) & 3) * 0.6f) * k;
        pa_fill_circle(c, x + ox * k, y + oy * k, r, pa_hex(0xCE1C2C));
    }
}
static void draw_decals(PA_Canvas *c) {
    if (fabsf(g_splat_s - L.s) > 0.001f) {
        int sz = (int)ceilf(44.0f * L.s);
        for (int f = 0; f < SPLATS; f++) spr_bake(&g_splat[f], sz, sz, (float)sz * 0.5f, (float)sz * 0.5f, L.s, f, art_splat);
        g_splat_s = L.s;
    }
    for (int i = 0; i < MAX_DECALS; i++) {
        const Decal *d = &g_dc[i];
        if (d->r <= 0.0f || d->age > 14.0f) continue;
        if (!on_screen(d->x, d->y, 30.0f)) continue;
        float a = d->age < 8.0f ? 1.0f : 1.0f - (d->age - 8.0f) / 6.0f;
        spr_blit(c, &g_splat[d->seed % SPLATS], SX(d->x), SY(d->y), d->seed & 1, 0, 0, (int)(200.0f * a));
    }
}

static void draw_zones_under(PA_Canvas *c) {
    for (int i = 0; i < MAX_ZONES; i++) {
        const Zone *z = &g_zn[i];
        if (!z->alive) continue;
        float x = SX(z->x), y = SY(z->y), r = z->r * L.s;
        if (z->kind == Z_FIRE) {
            float k = z->life / z->max;
            float grow = pa_clamp01((z->max - z->life) * 6.0f);
            float fade = pa_clamp01(z->life * 2.0f);
            PA_Paint p = pa_radial(x, y, r * 0.1f, r * grow);
            pa_stop(&p, 0.0f, PA_RGBA(255, 220, 90, (int)(200 * fade)));
            pa_stop(&p, 0.55f, PA_RGBA(255, 120, 30, (int)(170 * fade)));
            pa_stop(&p, 1.0f, PA_RGBA(220, 50, 20, 0));
            pa_fill_ellipse_paint(c, x, y, r * grow, r * 0.8f * grow, &p);
            (void)k;
        } else if (z->kind == Z_SLAM) {
            float k = 1.0f - z->life / z->max;
            float rr = r * k;
            pa_stroke_circle(c, x, y, rr, 10.0f * L.s, PA_RGBA(255, 90, 60, (int)(220 * (1.0f - k))));
            pa_stroke_circle(c, x, y, rr, 4.0f * L.s, PA_RGBA(255, 230, 200, (int)(230 * (1.0f - k))));
        }
    }
    /* boss telegraphs: charge lane */
    if (S.boss_idx >= 0 && g_en[S.boss_idx].alive) {
        const Enemy *b = &g_en[S.boss_idx];
        if (b->bstate == 1) {
            float bx = SX(b->x), by = SY(b->y);
            float ex = bx + b->tx * 260.0f * L.s, ey = by + b->ty * 260.0f * L.s;
            float a = 0.4f + 0.3f * sinf(S.clock * 30.0f);
            pa_line(c, bx, by, ex, ey, 46.0f * L.s, PA_RGBA(255, 40, 40, (int)(a * 120.0f)));
            pa_line(c, bx, by, ex, ey, 8.0f * L.s, PA_RGBA(255, 120, 100, (int)(a * 200.0f)));
        }
        if (b->bstate == 3) {
            float a = 0.3f + 0.3f * sinf(S.clock * 30.0f);
            pa_fill_ellipse(c, SX(b->x), SY(b->y), 120.0f * L.s, 110.0f * L.s, PA_RGBA(255, 60, 40, (int)(a * 110.0f)));
        }
    }
}

static void draw_cage(PA_Canvas *c, int front) {
    if (!S.cage) return;
    float grow = pa_clamp01(S.cage_t * 3.0f);
    float r = S.cage_r * L.s * (1.6f - 0.6f * grow);
    float cx = SX(S.cage_x), cy = SY(S.cage_y);
    if (!front) {
        pa_stroke_circle(c, cx, cy, r, 16.0f * L.s, PA_RGBA(255, 60, 40, 70));
        pa_stroke_circle(c, cx, cy, r, 5.0f * L.s, PA_RGBA(255, 120, 80, 200));
    }
    /* spiked posts: the back half before actors, the front half after */
    int n = 40;
    for (int i = 0; i < n; i++) {
        float a = (float)i / (float)n * PA_TAU + S.cage_t * 0.05f;
        float s = sinf(a);
        if ((s > 0.0f) != (front != 0)) continue;
        float px = cx + cosf(a) * r, py = cy + s * r;
        float h = 22.0f * L.s;
        pa_round_rect(c, px - 5.5f * L.s, py - h - 1.5f * L.s, 11.0f * L.s, h + 5.0f * L.s, 3.0f * L.s, OUTC());
        pa_round_rect(c, px - 4.0f * L.s, py - h, 8.0f * L.s, h + 2.0f * L.s, 2.0f * L.s, pa_hex(0x8C92A2));
        pa_fill_rect(c, px - 4.0f * L.s, py - h * 0.6f, 8.0f * L.s, 3.0f * L.s, pa_hex(0xE8303A));
        PA_Vec2 sp[3] = { { px - 4.5f * L.s, py - h }, { px, py - h - 9.0f * L.s }, { px + 4.5f * L.s, py - h } };
        o_poly(c, sp, 3, pa_hex(0xC4C9D4), 1.3f * L.s);
    }
}

static void draw_gems(PA_Canvas *c) {
    for (int i = 0; i < g_gem_count; i++) {
        const Gem *g = &g_gm[i];
        if (!g->alive || !on_screen(g->x, g->y, 20.0f)) continue;
        float x = SX(g->x), y = SY(g->y);
        if (g->kind == G_CHEST) {
            float pulse = 0.5f + 0.5f * sinf(S.clock * 6.0f);
            PA_Paint p = pa_radial(x, y, 4.0f * L.s, 44.0f * L.s);
            pa_stop(&p, 0.0f, PA_RGBA(255, 230, 120, (int)(150 + pulse * 80)));
            pa_stop(&p, 1.0f, PA_RGBA(255, 200, 60, 0));
            pa_fill_ellipse_paint(c, x, y, 44.0f * L.s, 44.0f * L.s, &p);
            spr_blit(c, &g_gem_spr[G_CHEST], x, y - fabsf(sinf(S.clock * 4.0f)) * 5.0f * L.s, 0, 0, 0, 255);
            continue;
        }
        float bob = g->kind >= G_COIN ? sinf(S.clock * 4.0f + (float)i) * 2.0f * L.s : 0.0f;
        spr_blit(c, &g_gem_spr[g->kind], x, y + bob, 0, 0, 0, 255);
    }
}

static const Sprite *enemy_sprite(const Enemy *e) {
    static const int map[E_TYPES] = { SPR_ZOMBIE, SPR_BEETLE, SPR_FLY, SPR_BRUTE, SPR_SPITTER, SPR_BLUEBUG,
                                      SPR_DOG, SPR_ELITE, SPR_BOSS, SPR_KING };
    int f = ((int)e->anim) & (ANIM_FRAMES - 1);
    return &g_spr[map[e->type]][f];
}

static void ring_ellipse(PA_Canvas *c, float x, float y, float rx, float ry, float w, PA_Color col) {
    PA_Vec2 p[32];
    for (int i = 0; i < 32; i++) {
        float a = (float)i / 32.0f * PA_TAU;
        p[i].x = x + cosf(a) * rx; p[i].y = y + sinf(a) * ry;
    }
    pa_stroke_poly(c, p, 32, 1, w, col);
}

static void draw_hero_ring(PA_Canvas *c) {
    float x = SX(S.px), y = SY(S.py), s = L.s;
    pa_fill_ellipse(c, x, y, 27.0f * s, 11.0f * s, PA_RGBA(255, 77, 77, 50));
    ring_ellipse(c, x, y, 27.0f * s, 11.0f * s, 3.0f * s, PA_RGBA(255, 77, 77, 153));
}

static void draw_hero(PA_Canvas *c) {
    float x = SX(S.px), y = SY(S.py);
    int f = S.moving > 0.5f ? (((int)S.run_anim) & 3) : 0;
    int tint = 0;
    uint32_t tc = 0xFFFFFF;
    if (S.hurt > 0.0f) { tint = (int)(S.hurt / 0.25f * 200.0f); tc = 0xFF3040; }
    if (S.phase == PH_DYING && !S.won) {
        tint = 255; tc = 0xFFFFFF;
        if (((int)(S.phase_t * 14.0f)) & 1) tc = 0xFF3040;
    }
    spr_blit(c, &g_spr[SPR_HERO][f], x, y, S.face < 0, tc, tint, 255);
}

static void draw_muzzle(PA_Canvas *c) {
    if (S.muzzle <= 0.0f || S.phase != PH_PLAY) return;
    float s = L.s, k = S.muzzle / 0.08f;
    float ca = cosf(S.muzzle_ang), sa = sinf(S.muzzle_ang);
    float x = SX(S.px) + ca * 26.0f * s, y = SY(S.py) - 26.0f * s + sa * 20.0f * s;
    glow(c, x, y, 34.0f * s, 34.0f * s, 0xFFC860, 0.9f * k);
    PA_Vec2 st[8];
    for (int i = 0; i < 8; i++) {
        float a = S.muzzle_ang + (float)i * PA_PI / 4.0f;
        float r = (i & 1) ? 5.0f * s : (i == 0 ? 20.0f : 12.0f) * s * (0.6f + 0.4f * k);
        st[i].x = x + cosf(a) * r; st[i].y = y + sinf(a) * r;
    }
    pa_fill_poly(c, st, 8, pa_hex(0xFFE27A));
    pa_fill_circle(c, x, y, 5.0f * s * k, PA_RGB(255, 255, 255));
}

static void draw_enemy(PA_Canvas *c, const Enemy *e) {
    float x = SX(e->x), y = SY(e->y), s = L.s;
    int boss = e->type >= E_BOSS;
    float lift = 0.0f;
    if (boss) {
        float land = e->bstate == 4 ? 1.0f - pa_clamp01(e->bt / 0.9f) : 1.0f;
        if (e->bstate == 4) lift = (1.0f - land) * (1.0f - land) * 420.0f * s;
        /* ground shadow 0.25 sw at 40%, then the pulsing red telegraph ring */
        pa_fill_ellipse(c, x, y, 68.0f * s * (0.4f + 0.6f * land), 24.0f * s * (0.4f + 0.6f * land), PA_RGBA(10, 8, 22, 102));
        float pulse = 0.5f + 0.5f * sinf(S.clock * 8.0f);
        float rr = (e->type == E_KING ? 84.0f : 76.0f) * s;
        int hot = e->bstate == 1 || e->bstate == 3;
        glow(c, x, y, rr * 1.2f, rr * 0.55f, 0xFF3020, (hot ? 0.55f : 0.25f) + 0.2f * pulse);
        glow(c, x, y - 60.0f * s, 150.0f * s, 120.0f * s, 0xFF7A1F, 0.22f + 0.1f * pulse);
        if (e->bstate == 3) flame_ring(c, x, y, 190.0f * s * (0.6f + 0.4f * land), 22, S.clock, 200);
        ring_ellipse(c, x, y, rr * (0.92f + 0.08f * pulse), rr * 0.4f * (0.92f + 0.08f * pulse), (hot ? 5.0f : 3.0f) * s,
                     PA_RGBA(255, 59, 47, (int)(150 + 90 * pulse)));
    } else if (e->type == E_ELITE) {
        float rr = 32.0f * s;
        ring_ellipse(c, x, y, rr, rr * 0.4f, 3.0f * s, PA_RGBA(255, 50, 50, 190));
        pa_fill_ellipse(c, x, y, rr, rr * 0.4f, PA_RGBA(255, 40, 40, 60));
    }
    /* Two frames of pure white on every hit; big bodies get a softer flash. */
    int amt = 0;
    if (e->flash > 0.07f) amt = e->type >= E_ELITE ? 150 : 255;
    else if (e->flash > 0.0f && e->type < E_ELITE) amt = 90;
    spr_blit(c, enemy_sprite(e), x, y - lift, e->face, 0xFFFFFF, amt, 255);
    if (e->type == E_ELITE) {
        float w = 58.0f * s, bx = x - w * 0.5f, by = y - 128.0f * s;
        pa_round_rect(c, bx - 2.0f * s, by - 2.0f * s, w + 4.0f * s, 10.0f * s, 4.0f * s, OUTC());
        pa_round_rect(c, bx, by, w * pa_clamp01(e->hp / e->maxhp), 6.0f * s, 3.0f * s, pa_hex(0xFF3A3A));
    }
}

/* Directional projectiles baked at 32 headings: kind 0 kunai, 1 rocket,
   2 enemy spit (heading ignored). */
#define DIRS 32
static Sprite g_dir[3][2][DIRS];
static float  g_dir_s = -1.0f;
static void art_dir(PA_Canvas *c, float x, float y, float s, int f) {
    int kind = f / (2 * DIRS), evo = (f / DIRS) & 1;
    float a = (float)(f % DIRS) / (float)DIRS * PA_TAU, ca = cosf(a), sa = sinf(a);
    if (kind == 0) {
        float len = (evo ? 22.0f : 17.0f) * s, wid = (evo ? 6.0f : 4.5f) * s;
        PA_Vec2 b[4] = { { x + ca * len * 0.6f, y + sa * len * 0.6f }, { x - sa * wid, y + ca * wid },
                         { x - ca * len * 0.15f, y - sa * len * 0.15f }, { x + sa * wid, y - ca * wid } };
        pa_line(c, x - ca * len * 0.15f, y - sa * len * 0.15f, x - ca * len * 0.55f, y - sa * len * 0.55f, 4.5f * s, OUTC());
        pa_line(c, x - ca * len * 0.15f, y - sa * len * 0.15f, x - ca * len * 0.5f, y - sa * len * 0.5f, 2.2f * s,
                evo ? pa_hex(0x3A1E70) : pa_hex(0xD8342E));
        o_poly(c, b, 4, evo ? pa_hex(0xA67CFF) : pa_hex(0xD8DCE6), 1.4f * s);
    } else if (kind == 1) {
        float len = (evo ? 13.0f : 10.0f) * s;
        pa_line(c, x - ca * len, y - sa * len, x + ca * len, y + sa * len, (evo ? 9.0f : 7.0f) * s, OUTC());
        pa_line(c, x - ca * len * 0.8f, y - sa * len * 0.8f, x + ca * len * 0.6f, y + sa * len * 0.6f,
                (evo ? 5.5f : 4.0f) * s, PA_RGB(246, 246, 250));
        pa_fill_circle(c, x + ca * len * 0.7f, y + sa * len * 0.7f, 2.4f * s, pa_hex(0xE8303A));
        pa_fill_circle(c, x - ca * len * 1.1f, y - sa * len * 1.1f, 3.0f * s, pa_hex(0xFFB21E));
    } else {
        o_circle(c, x, y, 7.5f * s, pa_hex(0xC04AE0), 1.6f * s);
        pa_fill_circle(c, x - 2.0f * s, y - 2.0f * s, 2.6f * s, pa_hex(0xF0B0FF));
    }
}
static const Sprite *dir_sprite(int kind, int evo, float ang) {
    if (fabsf(g_dir_s - L.s) > 0.001f) {
        int sz = (int)ceilf(40.0f * L.s);
        for (int k = 0; k < 3; k++)
            for (int e = 0; e < 2; e++)
                for (int d = 0; d < DIRS; d++) {
                    if (k == 2 && (e || d)) continue;
                    spr_bake(&g_dir[k][e][d], sz, sz, (float)sz * 0.5f, (float)sz * 0.5f, L.s, k * 2 * DIRS + e * DIRS + d, art_dir);
                }
        g_dir_s = L.s;
    }
    if (kind == 2) return &g_dir[2][0][0];
    int d = (int)floorf(pa_wrapf(ang, PA_TAU) / PA_TAU * (float)DIRS + 0.5f) % DIRS;
    return &g_dir[kind][evo][d];
}

static void draw_projectiles(PA_Canvas *c) {
    for (int i = 0; i < MAX_PROJ; i++) {
        const Proj *p = &g_pj[i];
        if (!p->alive || !on_screen(p->x, p->y, 40.0f)) continue;
        float x = SX(p->x), y = SY(p->y), s = L.s;
        switch (p->kind) {
        case PJ_KUNAI: {
            /* ~0.04 sw cyan tracer with a white core, and a three-step trail */
            pa_line(c, x - p->vx * 0.066f * s, y - p->vy * 0.066f * s, x - p->vx * 0.03f * s, y - p->vy * 0.03f * s, 3.5f * s,
                    p->evo ? PA_RGBA(178, 120, 255, 90) : PA_RGBA(95, 227, 255, 90));
            pa_line(c, x - p->vx * 0.035f * s, y - p->vy * 0.035f * s, x, y, 6.0f * s,
                    p->evo ? PA_RGBA(178, 120, 255, 210) : PA_RGBA(95, 227, 255, 220));
            pa_line(c, x - p->vx * 0.035f * s, y - p->vy * 0.035f * s, x, y, 2.2f * s, PA_RGBA(255, 255, 255, 230));
            spr_blit(c, dir_sprite(0, p->evo, atan2f(p->vy, p->vx)), x, y, 0, 0, 0, 255);
            break;
        }
        case PJ_ROCKET:
            glow(c, x - p->vx * 0.03f * s, y - p->vy * 0.03f * s, 9.0f * s, 9.0f * s, 0xFFA030, 0.8f);
            spr_blit(c, dir_sprite(1, p->evo, atan2f(p->vy, p->vx)), x, y, 0, 0, 0, 255);
            break;
        case PJ_BRICK:
            pa_fill_ellipse(c, x, y + 26.0f * s, 10.0f * s, 3.5f * s, PA_RGBA(20, 16, 40, 60));
            {
                float ca = cosf(p->ang), sa = sinf(p->ang);
                float hwid = (p->evo ? 16.0f : 11.0f) * s, hh = (p->evo ? 7.0f : 6.0f) * s;
                if (p->evo) {
                    pa_line(c, x - ca * hwid, y - sa * hwid, x + ca * hwid, y + sa * hwid, 4.0f * s + 3.0f * s, OUTC());
                    pa_line(c, x - ca * hwid, y - sa * hwid, x + ca * hwid, y + sa * hwid, 4.0f * s, pa_hex(0xB8BECB));
                    o_circle(c, x - ca * hwid, y - sa * hwid, 8.0f * s, pa_hex(0x4A505E), 1.5f * s);
                    o_circle(c, x + ca * hwid, y + sa * hwid, 8.0f * s, pa_hex(0x4A505E), 1.5f * s);
                } else {
                    PA_Vec2 q[4] = { { x - ca * hwid + sa * hh, y - sa * hwid - ca * hh }, { x + ca * hwid + sa * hh, y + sa * hwid - ca * hh },
                                     { x + ca * hwid - sa * hh, y + sa * hwid + ca * hh }, { x - ca * hwid - sa * hh, y - sa * hwid + ca * hh } };
                    o_poly(c, q, 4, pa_hex(0xC4523A), 1.6f * s);
                    pa_line(c, x - ca * hwid * 0.8f, y - sa * hwid * 0.8f, x + ca * hwid * 0.8f, y + sa * hwid * 0.8f, 1.2f * s, pa_hex(0x7A2A20));
                }
            }
            break;
        case PJ_BALL:
            pa_fill_ellipse(c, x, y + p->r * s, p->r * s, p->r * 0.35f * s, PA_RGBA(20, 16, 40, 60));
            if (p->evo) {
                PA_Paint g = pa_radial(x, y, p->r * 0.5f * s, p->r * 2.0f * s);
                pa_stop(&g, 0.0f, PA_RGBA(120, 220, 255, 160));
                pa_stop(&g, 1.0f, PA_RGBA(120, 220, 255, 0));
                pa_fill_ellipse_paint(c, x, y, p->r * 2.0f * s, p->r * 2.0f * s, &g);
            }
            spr_blit(c, ball_sprite(p->evo, p->r * s, p->ang), x, y, 0, 0, 0, 255);
            break;
        case PJ_BOTTLE: {
            float k = pa_clamp01(p->age / 0.6f);
            float sx = SX(S.px), sy = SY(S.py - 20.0f);
            float tx = SX(p->tx), ty = SY(p->ty);
            float bx = sx + (tx - sx) * k, byy = sy + (ty - sy) * k - sinf(k * PA_PI) * 90.0f * s;
            pa_fill_ellipse(c, sx + (tx - sx) * k, sy + (ty - sy) * k + 20.0f * s, 6.0f * s, 2.5f * s, PA_RGBA(20, 16, 40, 60));
            icon_bottle(c, bx, byy, 26.0f * s, p->evo);
            break;
        }
        }
    }
    for (int b = 0; b < MAX_EBUL; b++) {
        const EBullet *q = &g_eb[b];
        if (!q->alive) continue;
        float x = SX(q->x), y = SY(q->y);
        spr_blit(c, dir_sprite(2, 0, 0.0f), x, y, 0, 0, 0, 255);
    }
}

/* Spinning weapons as cached frames: guardians and balls rebake only when
   their radius changes (an EXO BRACER pick), never per frame. */
#define SPIN_FRAMES 6
static Sprite g_saw[2][SPIN_FRAMES], g_ball_spr[2][SPIN_FRAMES], g_drone_spr[2];
static float  g_saw_r[2] = { -1.0f, -1.0f }, g_ball_r[2] = { -1.0f, -1.0f }, g_drone_k = -1.0f;

static void art_saw(PA_Canvas *c, float x, float y, float r, int f) {
    int evo = f >= SPIN_FRAMES;
    float t = (float)(f % SPIN_FRAMES) / (float)SPIN_FRAMES * (PA_TAU / 12.0f);
    if (evo) icon_saw(c, x, y, r, pa_hex(0xFFC21C), pa_hex(0x9A5A10), t);
    else icon_saw(c, x, y, r, pa_hex(0xE8303A), pa_hex(0x2A2A34), t);
}
static void art_ballspr(PA_Canvas *c, float x, float y, float r, int f) {
    int evo = f >= SPIN_FRAMES;
    icon_ball(c, x, y, r, (float)(f % SPIN_FRAMES) / (float)SPIN_FRAMES * (PA_TAU / 5.0f), evo);
}
static void art_dronespr(PA_Canvas *c, float x, float y, float k, int f) { icon_drone(c, x, y, k, f); }

static const Sprite *saw_sprite(int evo, float r, float t) {
    if (fabsf(g_saw_r[evo] - r) > 0.25f) {
        int sz = (int)ceilf(r * 2.4f) + 4;
        for (int f = 0; f < SPIN_FRAMES; f++)
            spr_bake(&g_saw[evo][f], sz, sz, (float)sz * 0.5f, (float)sz * 0.5f, r, f + evo * SPIN_FRAMES, art_saw);
        g_saw_r[evo] = r;
    }
    return &g_saw[evo][((int)(t * 30.0f)) % SPIN_FRAMES];
}
static const Sprite *ball_sprite(int evo, float r, float ang) {
    if (fabsf(g_ball_r[evo] - r) > 0.25f) {
        int sz = (int)ceilf(r * 2.4f) + 4;
        for (int f = 0; f < SPIN_FRAMES; f++)
            spr_bake(&g_ball_spr[evo][f], sz, sz, (float)sz * 0.5f, (float)sz * 0.5f, r, f + evo * SPIN_FRAMES, art_ballspr);
        g_ball_r[evo] = r;
    }
    int f = (int)(pa_wrapf(ang, PA_TAU / 5.0f) / (PA_TAU / 5.0f) * (float)SPIN_FRAMES) % SPIN_FRAMES;
    return &g_ball_spr[evo][f];
}
static const Sprite *drone_sprite(int evo) {
    float k = 40.0f * L.s;
    if (fabsf(g_drone_k - k) > 0.25f) {
        int sz = (int)ceilf(k * 1.1f);
        for (int e = 0; e < 2; e++) spr_bake(&g_drone_spr[e], sz, sz, (float)sz * 0.5f, (float)sz * 0.5f, k, e, art_dronespr);
        g_drone_k = k;
    }
    return &g_drone_spr[evo];
}

static void draw_orbitals(PA_Canvas *c) {
    int n = guardian_count();
    if (n && guardians_active()) {
        float r = (S.evo[W_GUARD] ? 17.0f : 13.0f) * area_mul() * L.s;
        for (int i = 0; i < n; i++) {
            float gx, gy;
            guardian_pos(i, n, &gx, &gy);
            float x = SX(gx), y = SY(gy);
            pa_fill_ellipse(c, x, y + 18.0f * L.s, r * 0.8f, r * 0.3f, PA_RGBA(20, 16, 40, 50));
            spr_blit(c, saw_sprite(S.evo[W_GUARD], r, S.clock), x, y, 0, 0, 0, 255);
        }
    }
    for (int d = 0; d < drone_count(); d++) {
        float dx, dy;
        drone_pos(d, &dx, &dy);
        float x = SX(dx), y = SY(dy) + sinf(S.clock * 5.0f + (float)d) * 3.0f * L.s;
        pa_fill_ellipse(c, x, SY(S.py + 4.0f), 10.0f * L.s, 3.5f * L.s, PA_RGBA(20, 16, 40, 50));
        spr_blit(c, drone_sprite(S.evo[W_DRONE]), x, y, 0, 0, 0, 255);
    }
}

static void draw_zones_over(PA_Canvas *c) {
    for (int i = 0; i < MAX_ZONES; i++) {
        const Zone *z = &g_zn[i];
        if (!z->alive) continue;
        float x = SX(z->x), y = SY(z->y), r = z->r * L.s, s = L.s;
        float k = 1.0f - z->life / z->max;
        float a = 1.0f - k;
        if (z->kind == Z_BOLT) {
            /* A jagged bolt from the top of the screen with a blown-out strike. */
            PA_Vec2 pts[9];
            uint32_t h = (uint32_t)(i * 7919 + (int)(z->x * 3.0f));
            float top = y - 520.0f * s;
            for (int k2 = 0; k2 < 9; k2++) {
                float f = (float)k2 / 8.0f;
                float jx = k2 == 0 || k2 == 8 ? 0.0f : ((float)((h >> (k2 * 3)) & 15) - 7.5f) * 4.0f * s;
                pts[k2].x = x + jx; pts[k2].y = top + (y - 10.0f * s - top) * f;
            }
            glow(c, x, y - 6.0f * s, r * 2.0f, r * 1.4f, 0x5AB8FF, 1.1f * a);
            pa_stroke_poly(c, pts, 9, 0, (z->evo ? 16.0f : 11.0f) * s, PA_RGBA(50, 130, 255, (int)(a * 170)));
            pa_stroke_poly(c, pts, 9, 0, (z->evo ? 7.0f : 4.5f) * s, PA_RGBA(235, 250, 255, (int)(a * 255)));
            pa_fill_ellipse(c, x, y, r * (0.6f + k * 0.6f), r * (0.45f + k * 0.45f), PA_RGBA(170, 235, 255, (int)(a * 160)));
            pa_stroke_circle(c, x, y, r * (0.5f + k * 1.1f), 3.5f * s, PA_RGBA(255, 255, 255, (int)(a * 230)));
        } else if (z->kind == Z_BLAST) {
            float rr = r * (0.55f + 0.75f * k);
            glow(c, x, y, r * 1.8f, r * 1.6f, 0xFF8A2A, 1.0f * a);
            pa_fill_circle(c, x, y, rr, PA_RGBA(255, 120, 40, (int)(a * 200)));
            pa_fill_circle(c, x, y, rr * 0.62f, PA_RGBA(255, 210, 100, (int)(a * 230)));
            pa_fill_circle(c, x, y, rr * 0.3f, PA_RGBA(255, 255, 230, (int)(a * 255)));
            pa_stroke_circle(c, x, y, r * (0.8f + 0.9f * k), 4.0f * s * a + 1.0f, PA_RGBA(255, 240, 200, (int)(a * 220)));
            if (k < 0.7f) for (int f = 0; f < 5; f++) {
                float fa = (float)f * 1.2566f + (float)i;
                spr_blit(c, &g_flame[(f + (int)(S.clock * 14.0f)) & 3], x + cosf(fa) * rr * 0.7f, y + sinf(fa) * rr * 0.55f + 10.0f * s,
                         0, 0, 0, (int)(255 * a));
            }
        } else if (z->kind == Z_RING) {
            glow(c, x, y, r * k * 1.15f, r * k * 1.15f, 0xFFE07A, 0.35f * a);
            pa_stroke_circle(c, x, y, r * k, 9.0f * s * a + 1.0f, PA_RGBA(255, 240, 160, (int)(a * 230)));
        } else if (z->kind == Z_SLAM) {
            float rr = r * k;
            glow(c, x, y, rr * 1.1f + 1.0f, rr * 0.9f + 1.0f, 0xFFD36B, 0.35f * a);
            glow(c, x, y, rr * 1.3f + 1.0f, rr * 1.1f + 1.0f, 0xFF4A2A, 0.45f * a);
            if (k > 0.1f) flame_ring(c, x, y, rr, 24, 0.0f, (int)(255 * a));
            pa_stroke_circle(c, x, y, rr, 14.0f * s * a + 2.0f, PA_RGBA(255, 90, 50, (int)(200 * a)));
            pa_stroke_circle(c, x, y, rr, 5.0f * s, PA_RGBA(255, 235, 200, (int)(240 * a)));
        } else if (z->kind == Z_FIRE) {
            float fade = pa_clamp01(z->life * 2.0f) * pa_clamp01((z->max - z->life) * 6.0f);
            glow(c, x, y, r * 1.25f, r * 1.0f, 0xFF6A1A, 0.6f * fade);
            int n = (int)(z->r / 8.0f) + 4;
            for (int f = 0; f < n; f++) {
                float fa = (float)f * 2.39996f;
                float rr2 = sqrtf(((float)f + 0.5f) / (float)n) * r * 0.85f;
                float fx = x + cosf(fa) * rr2, fy = y + sinf(fa) * rr2 * 0.75f + 10.0f * s;
                spr_blit(c, &g_flame[(f + (int)(S.clock * 12.0f)) & 3], fx, fy, f & 1, 0, 0, (int)(255 * fade));
            }
        }
    }
}

/* The forcefield: a ring of hellfire round the hero, the screen-scale AoE of
   the reference. #FF3B2F at 35% inside, a 4px #FF7A3D rim, additive glow and
   flames riding the rim. */
/* The forcefield at ~0.7 sw: three layers like the reference's hellfire aura.
   #FF3B1F rim, #FF8A1F flame sprites riding it, #FFD36B additive core, with
   a two-frame flicker in the light. */
static void draw_field(PA_Canvas *c) {
    if (!S.wlv[W_FIELD] || S.phase == PH_TITLE) return;
    float s = L.s, r = field_radius() * s;
    float x = SX(S.px), y = SY(S.py - 10.0f);
    float pulse = pa_clamp01(S.field_pulse);
    float flick = ((int)(S.clock * 30.0f) & 1) ? 1.0f : 0.82f;
    fast_disc(c, x, y, r, 0xFF3B1F, 64);
    glow(c, x, y, r * 0.95f, r * 0.95f, 0xFFD36B, (0.30f + 0.16f * pulse) * flick);
    glow(c, x, y, r * 1.22f, r * 1.22f, 0xFF6A1F, (0.26f + 0.12f * pulse) * flick);
    pa_stroke_circle(c, x, y, r, 6.0f * s, pa_hex(0xFF3B1F));
    pa_stroke_circle(c, x, y, r - 4.0f * s, 2.0f * s, PA_RGBA(255, 211, 107, 200));
    pa_stroke_circle(c, x, y, r * (1.0f - 0.4f * pulse), 3.0f * s, PA_RGBA(255, 211, 107, (int)(200 * pulse)));
    flame_ring(c, x, y, r, S.evo[W_FIELD] ? 34 : 26, S.clock * 0.7f, 240);
    flame_ring(c, x, y, r * 0.86f, 10, -S.clock * 0.5f, 150);
}

static void draw_particles(PA_Canvas *c) {
    for (int i = 0; i < MAX_PARTS; i++) {
        const Part *p = &g_pt[i];
        if (p->life <= 0.0f) continue;
        float k = p->life / p->max;
        float x = SX(p->x), y = SY(p->y), s = p->size * L.s;
        uint32_t rgb = p->col & 0xFFFFFFu;
        int r = (int)((rgb >> 16) & 255), g = (int)((rgb >> 8) & 255), b = (int)(rgb & 255);
        switch (p->kind) {
        case PT_SPARK: fast_rect(c, x - s * 0.5f, y - s * 0.5f, s * k + 1.0f, s * k + 1.0f, rgb, 255); break;
        case PT_BLOOD: fast_disc(c, x, y, s * (0.5f + 0.5f * k), rgb, (int)(255 * pa_clamp01(k * 2.0f))); break;
        case PT_SMOKE: fast_disc(c, x, y, s * (1.6f - k * 0.8f), rgb, (int)(150 * k)); break;
        case PT_FLAME:
            fast_disc(c, x, y, s * k, ((uint32_t)r << 16) | ((uint32_t)(int)((float)g * k + 60.0f * (1.0f - k)) << 8) | (uint32_t)b,
                      (int)(230 * k));
            break;
        case PT_STAR: star(c, x, y, s * (0.6f + k * 0.6f), PA_RGBA(r, g, b, 255), 1.0f * L.s); break;
        case PT_HIT: spr_blit(c, &g_spark, x, y, 0, 0, 0, (int)(255 * k)); break;
        }
    }
}

static void draw_numbers(PA_Canvas *c) {
    char buf[16];
    for (int i = 0; i < MAX_NUMS; i++) {
        const Num *n = &g_num[i];
        if (n->life <= 0.0f) continue;
        int st = n->crit ? 1 : 0;
        float adv = DIGIT_SIZE(st) * L.s * 0.78f;
        int len = snprintf(buf, sizeof(buf), "%d", n->value);
        float x = SX(n->x) - adv * (float)(len - 1) * 0.5f;
        float y = SY(n->y) - (n->life > 0.6f ? (n->life - 0.6f) * 60.0f * L.s : 0.0f);
        int alpha = n->life < 0.15f ? (int)(n->life / 0.15f * 255.0f) : 255;
        for (int k = 0; k < len; k++) {
            int d = buf[k] - '0';
            if (d < 0 || d > 9) continue;
            spr_blit(c, &g_digit[st][d], x + adv * (float)k, y, 0, 0, 0, alpha);
        }
    }
}

static void draw_actors(PA_Canvas *c) {
    /* Bucket by screen row so the swarm overlaps front to back without a sort. */
    #define ROWS 256
    static int head[ROWS], next[MAX_ENEMIES + 1];
    for (int i = 0; i < ROWS; i++) head[i] = -1;
    float top = -80.0f, span = (float)L.h + 240.0f;
    for (int i = 0; i <= g_en_count; i++) {
        float y;
        if (i == g_en_count) y = SY(S.py);
        else {
            const Enemy *e = &g_en[i];
            if (!e->alive || !on_screen(e->x, e->y, 110.0f)) continue;
            y = SY(e->y);
        }
        int row = (int)((y - top) / span * (float)ROWS);
        if (row < 0) row = 0;
        if (row >= ROWS) row = ROWS - 1;
        next[i] = head[row];
        head[row] = i;
    }
    for (int r = 0; r < ROWS; r++)
        for (int i = head[r]; i >= 0; i = next[i]) {
            if (i != g_en_count) draw_enemy(c, &g_en[i]);
        }
    /* The hero always reads on top of the pile, as in the reference. */
    draw_hero(c);
    #undef ROWS
}

/* ------------------------------------------------------------------ HUD -- */
static void draw_xp_bar(PA_Canvas *c) {
    float s = L.s;
    float x = 12.0f * s, y = 14.0f * s, w = (float)L.w - 24.0f * s, h = 36.0f * s;
    o_rrect(c, x, y, w, h, 10.0f * s, pa_hex(0xC9CEDA), 2.5f * s);
    pa_round_rect(c, x + 3.0f * s, y + 3.0f * s, w - 6.0f * s, h - 6.0f * s, 8.0f * s, pa_hex(0x283040));
    float tab = 64.0f * s;
    float bx = x + 9.0f * s, bw = w - tab - 14.0f * s, by = y + 8.0f * s, bh = h - 16.0f * s;
    float seg = 22.0f * s, gap = 5.0f * s, skew = 6.0f * s;
    int nseg = (int)((bw + gap) / (seg + gap));
    float frac = S.xpneed > 0 ? pa_clamp01((float)S.xp / (float)S.xpneed) : 0.0f;
    float filled = frac * (float)nseg;
    /* Slanted segments, filled row by row: the parallelograms of the plates
       without a polygon per segment. */
    int rows = (int)bh;
    for (int i = 0; i < nseg; i++) {
        float sx = bx + (float)i * (seg + gap);
        float t = (float)i / (float)(nseg > 1 ? nseg - 1 : 1);
        uint32_t on = pa_mix(pa_hex(0x22A83A), pa_hex(0xA6E22A), t) & 0xFFFFFFu;
        uint32_t hi = pa_shade(pa_mix(pa_hex(0x22A83A), pa_hex(0xA6E22A), t), 0.35f) & 0xFFFFFFu;
        float part = (float)i < filled ? pa_clamp01(filled - (float)i) : 0.0f;
        for (int r = 0; r < rows; r++) {
            float off = skew * (1.0f - ((float)r + 0.5f) / (float)rows);
            fast_rect(c, sx + off, by + (float)r, seg, 1.0f, 0x3A4456, 255);
            if (part > 0.0f) fast_rect(c, sx + off, by + (float)r, seg * part, 1.0f, r < (int)(4.0f * s) && r >= (int)(1.5f * s) ? hi : on, 255);
        }
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", S.level);
    pa_text(c, "LV", x + w - tab + 4.0f * s, y + 13.0f * s, 9.0f * s, pa_hex(0x9AA3B8), PA_ALIGN_LEFT, 1.0f * s);
    num_text(c, NUM_LEVEL, buf, x + w - 12.0f * s, y + 8.0f * s, PA_ALIGN_RIGHT, 0, 0);
}

/* The scrim behind the HUD, in flat bands rather than a per-pixel gradient. */
static void hud_scrim(PA_Canvas *c, float height) {
    int bands = 14;
    for (int i = 0; i < bands; i++) {
        float t = (float)i / (float)bands;
        int a = (int)(170.0f * (1.0f - t) * (1.0f - t));
        fast_rect(c, 0.0f, height * t, (float)L.w, height / (float)bands + 1.0f, 0x080A16, a);
    }
}

static void draw_hud(PA_Canvas *c) {
    float s = L.s;
    char buf[32];
    hud_scrim(c, 140.0f * s);
    draw_xp_bar(c);
    float rowy = 62.0f * s;
    /* pause sits at the left of the second row, where the reference keeps it */
    pa_hub_pause_anchor(34.0f * s, rowy + 18.0f * s, 20.0f * s);
    skull_icon(c, 74.0f * s, rowy + 18.0f * s, 10.0f * s);
    snprintf(buf, sizeof(buf), "%d", S.kills);
    num_text(c, NUM_HUD, buf, 90.0f * s, rowy + 10.0f * s, PA_ALIGN_LEFT, 0, 0);
    time_str(buf, sizeof(buf), S.t);
    num_text(c, NUM_TIMER, buf, (float)L.w * 0.5f, rowy + 6.0f * s, PA_ALIGN_CENTER, 0xFF5A4A, S.t > RUN_LEN - 20.0f ? 200 : 0);
    snprintf(buf, sizeof(buf), "%d", S.coins);
    coin_icon(c, (float)L.w - 30.0f * s, rowy + 18.0f * s, 10.0f * s);
    num_text(c, NUM_HUD, buf, (float)L.w - 46.0f * s, rowy + 10.0f * s, PA_ALIGN_RIGHT, 0, 0);

    if (S.boss_idx >= 0 && g_en[S.boss_idx].alive) {
        const Enemy *b = &g_en[S.boss_idx];
        float bw = (float)L.w * 0.72f, bx = ((float)L.w - bw) * 0.5f, by = 126.0f * s;
        pa_text_bold(c, b->type == E_KING ? "MUTANT KING" : "GIANT MUTANT", (float)L.w * 0.5f, by - 20.0f * s, 13.0f * s,
                     pa_hex(0xFFD8D0), OUTC(), PA_ALIGN_CENTER, 2.0f * s, 1.0f);
        o_rrect(c, bx, by, bw, 16.0f * s, 5.0f * s, pa_hex(0x3A1A20), 2.5f * s);
        float f = pa_clamp01(b->hp / b->maxhp);
        if (f > 0.0f) {
            pa_round_rect(c, bx + 2.0f * s, by + 2.0f * s, (bw - 4.0f * s) * f, 12.0f * s, 4.0f * s, pa_hex(0xE8303A));
            pa_fill_rect(c, bx + 4.0f * s, by + 3.0f * s, (bw - 8.0f * s) * f, 3.0f * s, pa_hex(0xFF8A8A));
        }
    }
}

/* Outer 15% of the screen darkened by up to 25%, in flat bands. */
static void edge_vignette(PA_Canvas *c) {
    float mw = (float)L.w * 0.15f, mh = (float)L.h * 0.15f;
    int bands = 8;
    for (int i = 0; i < bands; i++) {
        float t = (float)i / (float)bands;
        int a = (int)(64.0f * (1.0f - t) * (1.0f - t));
        float bw = mw / (float)bands + 1.0f, bh = mh / (float)bands + 1.0f;
        fast_rect(c, mw * t, 0.0f, bw, (float)L.h, 0x100A18, a);
        fast_rect(c, (float)L.w - mw * t - bw, 0.0f, bw, (float)L.h, 0x100A18, a);
        fast_rect(c, 0.0f, mh * t, (float)L.w, bh, 0x100A18, a);
        fast_rect(c, 0.0f, (float)L.h - mh * t - bh, (float)L.w, bh, 0x100A18, a);
    }
}

static void draw_hero_hp(PA_Canvas *c) {
    float s = L.s;
    float x = SX(S.px), y = SY(S.py) + 8.0f * s;
    float w = 44.0f * s;
    pa_round_rect(c, x - w * 0.5f - 2.0f * s, y - 2.0f * s, w + 4.0f * s, 10.0f * s, 4.0f * s, OUTC());
    pa_round_rect(c, x - w * 0.5f, y, w, 6.0f * s, 3.0f * s, pa_hex(0x4A1E24));
    float f = pa_clamp01(S.hp / S.maxhp);
    if (f > 0.0f) pa_round_rect(c, x - w * 0.5f, y, w * f, 6.0f * s, 3.0f * s, f < 0.3f ? pa_hex(0xFF3A3A) : pa_hex(0xF04A4A));
}

static void draw_joystick(PA_Canvas *c) {
    if (S.phase != PH_PLAY) return;
    float s = L.s, R = 54.0f * s;
    if (!S.joy_on) {
        /* resting ring where the thumb usually lands */
        float ox = (float)L.w * 0.5f, oy = (float)L.h * 0.80f;
        pa_stroke_circle(c, ox, oy, R, 3.0f * s, PA_RGBA(255, 255, 255, 90));
        pa_fill_circle(c, ox, oy, 22.0f * s, PA_RGBA(255, 255, 255, 70));
        return;
    }
    pa_fill_circle(c, S.joy_ox, S.joy_oy, R + 6.0f * s, PA_RGBA(20, 20, 40, 60));
    pa_fill_circle(c, S.joy_ox, S.joy_oy, R, PA_RGBA(255, 255, 255, 46));
    pa_stroke_circle(c, S.joy_ox, S.joy_oy, R, 3.0f * s, PA_RGBA(255, 255, 255, 150));
    float kx = S.joy_ox + S.joy_kx, ky = S.joy_oy + S.joy_ky;
    pa_fill_circle(c, kx, ky + 3.0f * s, 25.0f * s, PA_RGBA(20, 20, 40, 90));
    pa_fill_circle(c, kx, ky, 25.0f * s, PA_RGBA(255, 255, 255, 220));
    pa_fill_circle(c, kx - 6.0f * s, ky - 7.0f * s, 8.0f * s, PA_RGBA(255, 255, 255, 255));
    pa_stroke_circle(c, kx, ky, 25.0f * s, 2.5f * s, PA_RGBA(40, 44, 64, 160));
}

static void draw_banner(PA_Canvas *c) {
    if (S.banner_t <= 0.0f) return;
    float s = L.s;
    float t = 3.0f - S.banner_t;
    float in = pa_clamp01(t * 5.0f), out = pa_clamp01(S.banner_t * 3.0f);
    float a = in * out;
    float cy = (float)L.h * 0.30f;
    float bh = 62.0f * s;
    int gold = S.banner_kind == 3;
    PA_Color band = gold ? PA_RGBA(230, 150, 20, (int)(a * 190)) : PA_RGBA(200, 20, 30, (int)(a * 170));
    float slide = (1.0f - in) * (float)L.w;
    pa_fill_rect(c, slide, cy - bh * 0.5f, (float)L.w, bh, band);
    /* halftone dots along the band edges */
    for (int row = 0; row < 2; row++) {
        float yy = row ? cy + bh * 0.5f - 7.0f * s : cy - bh * 0.5f + 7.0f * s;
        for (float x = slide + 6.0f * s; x < (float)L.w; x += 12.0f * s)
            pa_fill_circle(c, x, yy, 2.4f * s, gold ? PA_RGBA(255, 220, 120, (int)(a * 200)) : PA_RGBA(255, 90, 90, (int)(a * 200)));
    }
    pa_fill_rect(c, slide, cy - bh * 0.5f - 3.0f * s, (float)L.w, 3.0f * s, PA_RGBA(255, 255, 255, (int)(a * 120)));
    pa_fill_rect(c, slide, cy + bh * 0.5f, (float)L.w, 3.0f * s, PA_RGBA(255, 255, 255, (int)(a * 120)));
    if (a > 0.3f) {
        float size = fit_size(S.banner, (float)L.w - 40.0f * s, 30.0f * s, 2.0f * s);
        pa_text_bold(c, S.banner, (float)L.w * 0.5f + slide, cy - size * 0.5f, size, PA_RGB(255, 255, 255), OUTC(),
                     PA_ALIGN_CENTER, 2.0f * s, 1.6f);
    }
    if (!gold && a > 0.2f) {
        float pulse = 1.0f + 0.08f * sinf(t * 14.0f);
        float r = 54.0f * s * pulse, tx = (float)L.w * 0.5f, ty = cy - bh * 0.5f - 64.0f * s;
        PA_Vec2 tri[3] = { { tx, ty - r * 0.85f }, { tx + r, ty + r * 0.65f }, { tx - r, ty + r * 0.65f } };
        o_poly(c, tri, 3, pa_hex(0xE8202E), 3.0f * s);
        PA_Vec2 inn[3] = { { tx, ty - r * 0.5f }, { tx + r * 0.66f, ty + r * 0.46f }, { tx - r * 0.66f, ty + r * 0.46f } };
        pa_fill_poly(c, inn, 3, PA_RGB(255, 255, 255));
        pa_round_rect(c, tx - 5.5f * s * pulse, ty - r * 0.22f, 11.0f * s * pulse, r * 0.46f, 4.0f * s, OUTC());
        pa_fill_circle(c, tx, ty + r * 0.34f, 6.0f * s * pulse, OUTC());
    }
}

/* ---------------------------------------------------------- overlays ----- */
static void ribbon(PA_Canvas *c, const char *text, float cx, float cy, float w, float h, PA_Color fill, PA_Color edge) {
    float s = L.s;
    PA_Vec2 p[6] = { { cx - w * 0.5f, cy }, { cx - w * 0.5f + h * 0.5f, cy - h * 0.5f }, { cx + w * 0.5f - h * 0.5f, cy - h * 0.5f },
                     { cx + w * 0.5f, cy }, { cx + w * 0.5f - h * 0.5f, cy + h * 0.5f }, { cx - w * 0.5f + h * 0.5f, cy + h * 0.5f } };
    PA_Vec2 sh[6];
    for (int i = 0; i < 6; i++) { sh[i] = p[i]; sh[i].y += 5.0f * s; }
    o_poly(c, sh, 6, edge, 3.0f * s);
    o_poly(c, p, 6, fill, 3.0f * s);
    PA_Vec2 hi[4] = { p[1], p[2], { p[2].x + h * 0.12f, cy - h * 0.3f }, { p[1].x - h * 0.12f, cy - h * 0.3f } };
    pa_fill_poly(c, hi, 4, pa_shade(fill, 0.35f));
    float size = fit_size(text, w - h * 1.4f, h * 0.44f, 1.5f * s);
    pa_text_bold(c, text, cx, cy - size * 0.5f, size, PA_RGB(255, 255, 255), OUTC(), PA_ALIGN_CENTER, 1.5f * s, 1.4f);
}

static void slot_bar(PA_Canvas *c, float x, float y, float w, float h, const int *ids, int n, int items) {
    float s = L.s;
    PA_Vec2 p[4] = { { x, y }, { x + w, y }, { x + w - h * 0.3f, y + h }, { x, y + h } };
    o_poly(c, p, 4, pa_hex(0x343A4C), 2.5f * s);
    o_rrect(c, x + 4.0f * s, y + 4.0f * s, h - 8.0f * s, h - 8.0f * s, 6.0f * s, pa_hex(0xFFC21C), 1.8f * s);
    float bx = x + h * 0.5f, by = y + h * 0.5f;
    if (!items) {
        pa_stroke_circle(c, bx, by, h * 0.22f, 2.5f * s, OUTC());
        pa_line(c, bx - h * 0.32f, by, bx + h * 0.32f, by, 2.5f * s, OUTC());
        pa_line(c, bx, by - h * 0.32f, bx, by + h * 0.32f, 2.5f * s, OUTC());
    } else {
        o_rrect(c, bx - h * 0.2f, by - h * 0.14f, h * 0.4f, h * 0.3f, 3.0f * s, pa_hex(0xFFF0B0), 1.5f * s);
    }
    float slot = (w - h - 16.0f * s) / 6.0f;
    for (int i = 0; i < 6; i++) {
        float sx = x + h + 4.0f * s + (float)i * slot;
        pa_round_rect(c, sx, y + 6.0f * s, slot - 4.0f * s, h - 12.0f * s, 4.0f * s, pa_hex(0x262B38));
        if (i < n) {
            int id = items ? IC_ITEM(ids[i]) : (S.evo[ids[i]] ? IC_EVO(ids[i]) : ids[i]);
            draw_icon(c, id, sx + (slot - 4.0f * s) * 0.5f, y + h * 0.5f, (slot - 4.0f * s) * 0.95f, 0.0f);
        }
    }
}

static void draw_card(PA_Canvas *c, int i, float appear) {
    float s = L.s;
    float x, y, w, h;
    card_rect(i, &x, &y, &w, &h);
    const Pick *p = &S.opt[i];
    float rise = (1.0f - pa_smooth(appear)) * 220.0f * s;
    y += rise;
    if (S.chosen >= 0) {
        if (S.chosen == i) { float k = pa_clamp01(S.pick_t / 0.32f); y -= 16.0f * s * sinf(k * PA_PI); }
        else y += pa_clamp01(S.pick_t / 0.32f) * 30.0f * s;
    }
    int is_new = (p->kind == 0 && !S.wlv[p->id]) || (p->kind == 1 && !S.plv[p->id]);
    /* rarity: evolution red-orange, new weapon pink, weapon gold, item blue */
    PA_Color head = p->kind == 2 ? pa_hex(0x9B5BFF) : (is_new && p->kind == 0) ? pa_hex(0xFF4F8B)
                  : p->kind == 1 ? pa_hex(0x4FA3FF) : p->kind >= 3 ? pa_hex(0x5ACB4A) : pa_hex(0xFFC21A);
    float pulse = 0.5f + 0.5f * sinf(S.clock * 6.0f);
    if (p->kind == 2 || is_new) {
        /* a pulsing gold halo, added rather than painted */
        glow(c, x + w * 0.5f, y + h * 0.5f, w * 0.8f, h * 0.66f, p->kind == 2 ? 0xFF8A2A : 0xFFC21C, 0.35f + 0.3f * pulse);
        pa_round_rect(c, x - 5.0f * s, y - 5.0f * s, w + 10.0f * s, h + 10.0f * s, 14.0f * s,
                      PA_RGBA(255, 200, 60, (int)(90 + 110 * pulse)));
    }
    if (S.chosen == i) pa_round_rect(c, x - 6.0f * s, y - 6.0f * s, w + 12.0f * s, h + 12.0f * s, 14.0f * s, PA_RGBA(255, 255, 255, 200));
    o_rrect(c, x, y, w, h, 10.0f * s, pa_hex(0x3D4357), 3.0f * s);
    float hh = 40.0f * s;
    pa_round_rect(c, x, y, w, hh, 10.0f * s, head);
    pa_fill_rect(c, x, y + hh - 10.0f * s, w, 10.0f * s, head);
    pa_fill_rect(c, x + 6.0f * s, y + 5.0f * s, w - 12.0f * s, 4.0f * s, pa_shade(head, 0.35f));
    pa_fill_rect(c, x, y + hh, w, 3.0f * s, OUTC());
    /* 2px inner bevel: lit top and left, shaded bottom and right */
    pa_fill_rect(c, x + 3.0f * s, y + hh + 3.0f * s, w - 6.0f * s, 2.0f * s, pa_hex(0x5A6280));
    pa_fill_rect(c, x + 3.0f * s, y + hh + 3.0f * s, 2.0f * s, h - hh - 10.0f * s, pa_hex(0x535A76));
    pa_fill_rect(c, x + w - 5.0f * s, y + hh + 3.0f * s, 2.0f * s, h - hh - 10.0f * s, pa_hex(0x2A2E40));
    pa_fill_rect(c, x + 3.0f * s, y + h - 5.0f * s, w - 6.0f * s, 2.0f * s, pa_hex(0x262A3A));
    const char *name = p->kind == 0 ? W_NAME[p->id] : p->kind == 1 ? P_NAME[p->id] : p->kind == 2 ? EVO_NAME[p->id]
                     : p->kind == 3 ? "MEAT" : "GOLD";
    float ns = fit_size(name, w - 16.0f * s, 16.0f * s, 1.0f * s);
    pa_text_bold(c, name, x + w * 0.5f, y + hh * 0.5f - ns * 0.5f, ns, PA_RGB(255, 255, 255), OUTC(), PA_ALIGN_CENTER, 1.0f * s, 1.1f);
    if (is_new) {
        float sc = 1.0f + 0.12f * pulse;
        float bw = 56.0f * s * sc, bh = 24.0f * s * sc, bx = x + w - bw + 6.0f * s, by = y - bh * 0.6f;
        glow(c, bx + bw * 0.5f, by + bh * 0.5f, bw * 0.9f, bh * 1.1f, 0xFFD23A, 0.4f + 0.4f * pulse);
        o_rrect(c, bx, by, bw, bh, bh * 0.5f, pa_hex(0xFF4F8B), 2.5f * s);
        pa_text_bold(c, "NEW!", bx + bw * 0.5f, by + bh * 0.5f - 7.0f * s * sc, 14.0f * s * sc, PA_RGB(255, 255, 255), OUTC(),
                     PA_ALIGN_CENTER, 1.0f * s, 1.1f);
    }
    if (p->kind == 2) pa_text_bold(c, "EVOLVE!", x + w * 0.5f, y - 22.0f * s, 15.0f * s, pa_hex(0xFFD23A), OUTC(), PA_ALIGN_CENTER, 1.0f * s, 1.2f);
    /* icon over a faint crosshair */
    float icy = y + hh + (h - hh) * 0.23f;
    {
        float pr = w * 0.36f, cx = x + w * 0.5f;
        PA_Paint plate = pa_radial(cx, icy - pr * 0.25f, pr * 0.1f, pr * 1.1f);
        pa_stop(&plate, 0.0f, pa_hex(0x4A5070));
        pa_stop(&plate, 0.45f, pa_hex(0x3A3F5C));
        pa_stop(&plate, 1.0f, pa_hex(0x1E2236));
        pa_fill_circle(c, cx, icy, pr + 2.5f * s, OUTC());
        pa_fill_ellipse_paint(c, cx, icy, pr, pr, &plate);
        pa_stroke_circle(c, cx, icy, pr - 2.0f * s, 2.0f * s, pa_hex(0x5A6284));
        glow(c, cx, icy, pr * 0.9f, pr * 0.9f, head & 0xFFFFFFu, 0.32f);
    }
    draw_icon(c, pick_icon(p), x + w * 0.5f, icy, w * 0.62f, S.clock * 2.0f);
    /* description */
    float dy = y + hh + (h - hh) * 0.50f, dh = (h - hh) * 0.30f;
    pa_round_rect(c, x + 8.0f * s, dy, w - 16.0f * s, dh, 8.0f * s, pa_hex(0x2C3142));
    const char *desc = "";
    int lv = 0;
    if (p->kind == 0) { lv = S.wlv[p->id]; desc = W_DESC[p->id][lv < MAX_LV ? lv : MAX_LV - 1]; }
    else if (p->kind == 1) { lv = S.plv[p->id]; desc = P_DESC[p->id]; }
    else if (p->kind == 2) desc = EVO_DESC[p->id];
    else if (p->kind == 3) desc = "RESTORES HALF YOUR HEALTH";
    else desc = "+60 GOLD";
    float ts = 12.5f * s;
    if (w < 150.0f * s) ts = 11.0f * s;
    wrap_text(c, desc, x + w * 0.5f, dy + 10.0f * s, w - 28.0f * s, ts, pa_hex(0xE8ECF4), ts * 1.6f);
    /* stars strip */
    float sy = y + h - 34.0f * s;
    pa_round_rect(c, x + 3.0f * s, sy, w - 6.0f * s, 31.0f * s, 8.0f * s, pa_hex(0x2A2E3C));
    if (p->kind <= 1) {
        float sr = 9.0f * s;
        if (w < 150.0f * s) sr = 8.0f * s;
        float gap = sr * 2.3f;
        float sx0 = x + w * 0.5f - gap * 2.0f;
        for (int k = 0; k < MAX_LV; k++) {
            PA_Color col = pa_hex(0x1C2030);
            if (k < lv) col = pa_hex(0xFFC21C);
            else if (k == lv) col = (((int)(S.clock * 4.0f)) & 1) ? pa_hex(0xF23C78) : pa_hex(0xFF7AA8);
            star(c, sx0 + gap * (float)k, sy + 15.5f * s, sr, col, 1.6f * s);
        }
    } else if (p->kind == 2) {
        star(c, x + w * 0.5f, sy + 15.5f * s, 12.0f * s, pa_hex(0xFF6A1E), 1.8f * s);
    }
}

static void draw_draft(PA_Canvas *c) {
    float s = L.s;
    float a = pa_clamp01(S.phase_t * 4.0f);
    pa_fill_rect(c, 0, 0, (float)L.w, (float)L.h, PA_RGBA(12, 14, 26, (int)(a * 170)));
    float top = (float)L.h * 0.36f - 210.0f * s;
    if (top < 70.0f * s) top = 70.0f * s;
    float pop = 1.0f + (1.0f - pa_clamp01(S.phase_t * 5.0f)) * 0.25f;
    ribbon(c, "SKILL SELECTION", (float)L.w * 0.5f, top + 30.0f * s, 300.0f * s * pop, 58.0f * s * pop, pa_hex(0xFFC21C), pa_hex(0xC97A10));
    pa_text_bold(c, "LEVEL UP!", (float)L.w * 0.5f, top - 24.0f * s, 18.0f * s, pa_hex(0x9BE22A), OUTC(), PA_ALIGN_CENTER, 2.0f * s, 1.3f);
    float bw = ((float)L.w - 30.0f * s) * 0.5f;
    if (bw > 300.0f * s) bw = 300.0f * s;
    float bx = (float)L.w * 0.5f - bw - 4.0f * s, by = top + 84.0f * s;
    slot_bar(c, bx, by, bw, 40.0f * s, S.wslot, S.wslots, 0);
    slot_bar(c, (float)L.w * 0.5f + 4.0f * s, by, bw, 40.0f * s, S.pslot, S.pslots, 1);
    for (int i = 0; i < S.nopt; i++) draw_card(c, i, pa_clamp01((S.phase_t - 0.08f * (float)i) * 3.5f));
    float cx, cy, cw, ch;
    card_rect(0, &cx, &cy, &cw, &ch);
    if (S.chosen < 0 && ((int)(S.clock * 2.0f) & 1))
        pa_text_bold(c, "SELECT A SKILL", (float)L.w * 0.5f, cy + ch + 18.0f * s, 14.0f * s, PA_RGB(255, 255, 255), OUTC(),
                     PA_ALIGN_CENTER, 2.0f * s, 1.0f);
}

static void draw_chest(PA_Canvas *c) {
    float s = L.s;
    pa_fill_rect(c, 0, 0, (float)L.w, (float)L.h, PA_RGBA(12, 14, 26, 170));
    float pw = (float)L.w - 50.0f * s;
    if (pw > 440.0f * s) pw = 440.0f * s;
    float tile = (pw - 30.0f * s) / 5.0f;
    float ph = tile * 5.0f + 110.0f * s;
    float px = ((float)L.w - pw) * 0.5f, py = (float)L.h * 0.5f - ph * 0.5f;
    o_rrect(c, px, py, pw, ph, 14.0f * s, pa_hex(0x3D4357), 3.5f * s);
    /* marquee bulbs chasing round the frame */
    {
        float per = 2.0f * (pw + ph);
        int count = (int)(per / (26.0f * s));
        for (int b = 0; b < count; b++) {
            float d = (float)b / (float)count * per, bx, by;
            if (d < pw) { bx = px + d; by = py; }
            else if ((d -= pw) < ph) { bx = px + pw; by = py + d; }
            else if ((d -= ph) < pw) { bx = px + pw - d; by = py + ph; }
            else { d -= pw; bx = px; by = py + ph - d; }
            int on = ((b + (int)(S.clock * (S.chest_stage == 0 ? 14.0f : 6.0f))) % 3) == 0;
            PA_Color col = (b & 1) ? pa_hex(0xFF4F8B) : pa_hex(0xFFD23A);
            if (on) glow(c, bx, by, 13.0f * s, 13.0f * s, col & 0xFFFFFFu, 0.9f);
            o_circle(c, bx, by, 5.0f * s, on ? col : pa_shade(col, -0.55f), 1.5f * s);
            if (on) pa_fill_circle(c, bx - 1.5f * s, by - 1.5f * s, 1.8f * s, PA_RGB(255, 255, 255));
        }
    }
    ribbon(c, "LUCKY CHEST", (float)L.w * 0.5f, py + 4.0f * s, pw * 0.72f, 54.0f * s, pa_hex(0xFFC21C), pa_hex(0xC97A10));
    float gx = px + 15.0f * s, gy = py + 42.0f * s;
    /* the ring of tiles walks clockwise round a 5x5 border */
    int order[16][2];
    int k = 0;
    for (int i = 0; i < 5; i++) { order[k][0] = i; order[k][1] = 0; k++; }
    for (int i = 1; i < 5; i++) { order[k][0] = 4; order[k][1] = i; k++; }
    for (int i = 3; i >= 0; i--) { order[k][0] = i; order[k][1] = 4; k++; }
    for (int i = 3; i >= 1; i--) { order[k][0] = 0; order[k][1] = i; k++; }
    int cur = ((int)S.chest_spin) % 16;
    for (int i = 0; i < 16; i++) {
        float tx = gx + (float)order[i][0] * tile, ty = gy + (float)order[i][1] * tile;
        int lit = (S.chest_stage == 0 && i == cur);
        int won = 0;
        if (S.chest_stage == 1)
            for (int w = 0; w < S.chest_nwin; w++) if (S.chest_win[w] == i && S.phase_t > 0.3f * (float)w) won = 1;
        PA_Color bg = won ? pa_hex(0xFFE04A) : lit ? pa_hex(0xFFF4B0) : pa_hex(0x2A2F3D);
        if (won) pa_round_rect(c, tx - 2.0f * s, ty - 2.0f * s, tile, tile, 10.0f * s, PA_RGBA(255, 230, 100, 160));
        o_rrect(c, tx + 3.0f * s, ty + 3.0f * s, tile - 6.0f * s, tile - 6.0f * s, 8.0f * s, bg, 2.0f * s);
        float pop = 1.0f;
        if (won) {
            pa_round_rect(c, tx + 8.0f * s, ty + 8.0f * s, tile - 16.0f * s, tile - 16.0f * s, 6.0f * s, pa_hex(0x9BE22A));
            for (int w = 0; w < S.chest_nwin; w++)
                if (S.chest_win[w] == i) {
                    float since = S.phase_t - 0.3f * (float)w;
                    pop = 1.0f + 0.35f * pa_clamp01(1.0f - since * 4.0f);
                    if (since < 0.3f) glow(c, tx + tile * 0.5f, ty + tile * 0.5f, tile, tile, 0xFFE07A, 1.0f - since * 3.0f);
                }
        }
        draw_icon(c, pick_icon(&S.chest_tiles[i]), tx + tile * 0.5f, ty + tile * 0.5f, tile * 0.7f * pop, S.clock);
    }
    /* centre window: the hero on a pile of gold */
    float wx = gx + tile + 3.0f * s, wy = gy + tile + 3.0f * s, ww = tile * 3.0f - 6.0f * s;
    PA_Paint bgp = pa_linear(0, wy, 0, wy + ww);
    pa_stop(&bgp, 0.0f, pa_hex(0x2A1E4A));
    pa_stop(&bgp, 1.0f, pa_hex(0xC23A8A));
    pa_round_rect(c, wx - 2.5f * s, wy - 2.5f * s, ww + 5.0f * s, ww + 5.0f * s, 10.0f * s, OUTC());
    pa_round_rect_paint(c, wx, wy, ww, ww, 8.0f * s, &bgp);
    for (int i = 0; i < 9; i++) {
        float cx = wx + ww * (0.12f + 0.095f * (float)i), cy = wy + ww * (0.86f - 0.05f * (float)((i * 5) % 3));
        coin_icon(c, cx, cy, ww * 0.075f);
    }
    int clip0 = c->clip_x0, clip1 = c->clip_y0, clip2 = c->clip_x1, clip3 = c->clip_y1;
    pa_clip_rect(c, (int)wx, (int)wy, (int)ww, (int)ww);
    float bounce = S.chest_stage == 1 ? fabsf(sinf(S.phase_t * 8.0f)) * 6.0f * s * pa_clamp01(1.0f - S.phase_t) : 0.0f;
    art_hero(c, wx + ww * 0.5f, wy + ww * 0.98f - bounce, ww / 72.0f, 0);
    c->clip_x0 = clip0; c->clip_y0 = clip1; c->clip_x1 = clip2; c->clip_y1 = clip3;
    if (S.chest_stage == 1) {
        PA_Vec2 cr[7];
        float hx = wx + ww * 0.5f + 2.0f * s, hy = wy + ww * 0.98f - bounce - 58.0f * ww / 72.0f;
        float cw = ww * 0.18f;
        cr[0].x = hx - cw; cr[0].y = hy; cr[1].x = hx - cw; cr[1].y = hy - cw * 0.8f; cr[2].x = hx - cw * 0.5f; cr[2].y = hy - cw * 0.4f;
        cr[3].x = hx; cr[3].y = hy - cw; cr[4].x = hx + cw * 0.5f; cr[4].y = hy - cw * 0.4f; cr[5].x = hx + cw; cr[5].y = hy - cw * 0.8f;
        cr[6].x = hx + cw; cr[6].y = hy;
        o_poly(c, cr, 7, pa_hex(0xFFC21C), 2.0f * s);
    }
    /* footer: gold won */
    float fy = gy + tile * 5.0f + 14.0f * s;
    pa_round_rect(c, px + pw * 0.25f, fy, pw * 0.5f, 36.0f * s, 18.0f * s, pa_hex(0x262B38));
    coin_icon(c, px + pw * 0.25f + 22.0f * s, fy + 18.0f * s, 12.0f * s);
    char buf[24];
    snprintf(buf, sizeof(buf), "+%d", S.chest_stage == 1 ? S.chest_coins : (int)(S.chest_spin * 7.0f) % 90);
    pa_text_bold(c, buf, px + pw * 0.5f + 16.0f * s, fy + 8.0f * s, 20.0f * s, PA_RGB(255, 255, 255), OUTC(), PA_ALIGN_CENTER, 1.5f * s, 1.2f);
    if (S.chest_stage == 1) {
        /* rewards land one by one, 0.3 s apart, above the machine */
        float cw = 96.0f * s, gap = 10.0f * s;
        float total = cw * (float)S.chest_nwin + gap * (float)(S.chest_nwin - 1);
        for (int w = 0; w < S.chest_nwin; w++) {
            float since = S.phase_t - 0.3f * (float)w;
            if (since <= 0.0f) continue;
            float pop = 1.0f + 0.4f * pa_clamp01(1.0f - since * 5.0f);
            const Pick *pk = &S.chest_tiles[S.chest_win[w]];
            const char *nm = pk->kind == 0 ? W_NAME[pk->id] : pk->kind == 1 ? P_NAME[pk->id] : pk->kind == 2 ? EVO_NAME[pk->id] : "GOLD";
            float cx = ((float)L.w - total) * 0.5f + (float)w * (cw + gap) + cw * 0.5f, cy = py - 70.0f * s;
            glow(c, cx, cy, cw * 0.7f * pop, cw * 0.6f * pop, 0xFFD36B, 0.5f);
            o_rrect(c, cx - cw * 0.5f * pop, cy - 40.0f * s * pop, cw * pop, 80.0f * s * pop, 10.0f * s, pa_hex(0x2A2F3D), 2.5f * s);
            draw_icon(c, pick_icon(pk), cx, cy - 10.0f * s * pop, 46.0f * s * pop, S.clock);
            float ns = fit_size(nm, cw - 10.0f * s, 11.0f * s, 0.5f * s);
            pa_text_bold(c, nm, cx, cy + 20.0f * s * pop, ns, pa_hex(0x9BE22A), OUTC(), PA_ALIGN_CENTER, 0.5f * s, 1.0f);
        }
    }
    if (S.chest_stage == 1 && S.phase_t > 0.6f && ((int)(S.clock * 2.0f) & 1))
        pa_text_bold(c, "TAP TO CONTINUE", (float)L.w * 0.5f, py + ph + 18.0f * s, 14.0f * s, PA_RGB(255, 255, 255), OUTC(),
                     PA_ALIGN_CENTER, 2.0f * s, 1.0f);
}

static void button(PA_Canvas *c, float x, float y, float w, float h, const char *text, PA_Color fill, PA_Color edge, float size) {
    float s = L.s;
    o_rrect(c, x, y + 6.0f * s, w, h, 14.0f * s, edge, 3.0f * s);
    o_rrect(c, x, y, w, h, 14.0f * s, fill, 3.0f * s);
    pa_round_rect(c, x + 8.0f * s, y + 6.0f * s, w - 16.0f * s, h * 0.28f, 8.0f * s, pa_shade(fill, 0.3f));
    float ts = fit_size(text, w - 24.0f * s, size, 2.0f * s);
    pa_text_bold(c, text, x + w * 0.5f, y + h * 0.5f - ts * 0.5f, ts, PA_RGB(255, 255, 255), OUTC(), PA_ALIGN_CENTER, 2.0f * s, 1.4f);
}

static void draw_results(PA_Canvas *c) {
    float s = L.s;
    float a = pa_clamp01(S.over_t * 3.0f);
    pa_fill_rect(c, 0, 0, (float)L.w, (float)L.h, PA_RGBA(12, 14, 26, (int)(a * 190)));
    pa_hub_hide_pause();
    float pw = (float)L.w - 48.0f * s;
    if (pw > 460.0f * s) pw = 460.0f * s;
    float px = ((float)L.w - pw) * 0.5f;
    float ph = 470.0f * s;
    float py = (float)L.h * 0.5f - 210.0f * s + (1.0f - pa_smooth(a)) * 60.0f * s;
    float rx, ry, rw, rh, hx, hy, hw, hh;
    over_buttons(&rx, &ry, &rw, &rh, &hx, &hy, &hw, &hh);
    if (py + ph > ry - 20.0f * s) py = ry - 20.0f * s - ph;
    o_rrect(c, px, py, pw, ph, 16.0f * s, pa_hex(0x3D4357), 3.5f * s);
    pa_round_rect(c, px + 6.0f * s, py + 44.0f * s, pw - 12.0f * s, 120.0f * s, 12.0f * s, pa_hex(0x2C3142));
    ribbon(c, S.won ? "VICTORY!" : "DEFEATED", (float)L.w * 0.5f, py + 2.0f * s, pw * 0.7f, 60.0f * s,
           S.won ? pa_hex(0xFFC21C) : pa_hex(0x8C92A8), S.won ? pa_hex(0xC97A10) : pa_hex(0x4A5066));
    char buf[48];
    pa_text(c, "CHAPTER 1 - DEAD CITY", (float)L.w * 0.5f, py + 54.0f * s, 11.0f * s, pa_hex(0x9AA3B8), PA_ALIGN_CENTER, 2.0f * s);
    pa_text(c, "TIME SURVIVED", (float)L.w * 0.5f, py + 76.0f * s, 12.0f * s, pa_hex(0xE8ECF4), PA_ALIGN_CENTER, 2.0f * s);
    time_str(buf, sizeof(buf), S.t);
    float pop = 1.0f + (1.0f - pa_clamp01((S.over_t - 0.3f) * 4.0f)) * 0.4f;
    pa_text_bold(c, buf, (float)L.w * 0.5f, py + 98.0f * s, 40.0f * s * pop, S.won ? pa_hex(0xFFD23A) : PA_RGB(255, 255, 255),
                 OUTC(), PA_ALIGN_CENTER, 3.0f * s, 1.8f);
    if (S.new_best && S.over_t > 0.6f) {
        float bxp = (float)L.w * 0.5f + 100.0f * s, byp = py + 100.0f * s;
        PA_Vec2 tag[4] = { { bxp - 4.0f * s, byp - 6.0f * s }, { bxp + 78.0f * s, byp - 14.0f * s },
                           { bxp + 82.0f * s, byp + 14.0f * s }, { bxp, byp + 22.0f * s } };
        o_poly(c, tag, 4, pa_hex(0xF23C78), 2.0f * s);
        pa_text_bold(c, "NEW BEST", bxp + 39.0f * s, byp - 2.0f * s, 11.0f * s, PA_RGB(255, 255, 255), OUTC(), PA_ALIGN_CENTER, 1.0f * s, 1.0f);
    } else {
        time_str(buf, sizeof(buf), (float)g_best);
        char b2[64];
        snprintf(b2, sizeof(b2), "BEST %s", buf);
        pa_text(c, b2, (float)L.w * 0.5f, py + 146.0f * s, 11.0f * s, pa_hex(0x9AA3B8), PA_ALIGN_CENTER, 2.0f * s);
    }
    /* stat chips */
    float cy = py + 178.0f * s, cw = (pw - 36.0f * s) / 3.0f;
    for (int i = 0; i < 3; i++) {
        float cx = px + 12.0f * s + (float)i * (cw + 6.0f * s);
        pa_round_rect(c, cx, cy, cw, 62.0f * s, 10.0f * s, pa_hex(0x2C3142));
        const char *label = i == 0 ? "KILLS" : i == 1 ? "LEVEL" : "BOSSES";
        snprintf(buf, sizeof(buf), "%d", i == 0 ? S.kills : i == 1 ? S.level : S.bosses_down);
        if (i == 0) skull_icon(c, cx + 20.0f * s, cy + 22.0f * s, 9.0f * s);
        else if (i == 1) star(c, cx + 20.0f * s, cy + 22.0f * s, 10.0f * s, pa_hex(0xFFC21C), 1.6f * s);
        else draw_icon(c, IC_EVO(W_GUARD), cx + 20.0f * s, cy + 22.0f * s, 24.0f * s, 0.0f);
        pa_text_bold(c, buf, cx + cw * 0.5f + 12.0f * s, cy + 12.0f * s, 18.0f * s, PA_RGB(255, 255, 255), OUTC(), PA_ALIGN_CENTER, 1.0f * s, 1.2f);
        pa_text(c, label, cx + cw * 0.5f, cy + 42.0f * s, 10.0f * s, pa_hex(0x9AA3B8), PA_ALIGN_CENTER, 1.5f * s);
    }
    /* rewards */
    float ry2 = cy + 78.0f * s;
    pa_text_bold(c, "REWARDS", px + 16.0f * s, ry2, 13.0f * s, pa_hex(0xFFC21C), OUTC(), PA_ALIGN_LEFT, 2.0f * s, 1.0f);
    float tile = 62.0f * s;
    for (int i = 0; i < 3; i++) {
        float tx = px + 16.0f * s + (float)i * (tile + 10.0f * s), ty = ry2 + 22.0f * s;
        float reveal = pa_clamp01((S.over_t - 0.5f - 0.3f * (float)i) * 4.0f);
        if (reveal <= 0.0f) continue;
        if (reveal < 1.0f) glow(c, tx + tile * 0.5f, ty + tile * 0.5f, tile, tile, 0xFFD36B, 1.0f - reveal);
        PA_Color bg = i == 0 ? pa_hex(0x6A4AC8) : i == 1 ? pa_hex(0x3A8AE8) : pa_hex(0x38A858);
        o_rrect(c, tx, ty, tile, tile, 8.0f * s, bg, 2.0f * s);
        if (i == 0) {
            draw_icon(c, IC_COINS, tx + tile * 0.5f, ty + tile * 0.42f, tile * 0.7f, 0.0f);
            snprintf(buf, sizeof(buf), "%d", S.reward);
        } else if (i == 1) {
            art_pickup(c, tx + tile * 0.5f, ty + tile * 0.42f, 1.6f * s, G_BLUE);
            snprintf(buf, sizeof(buf), "%d", S.kills / 2 + S.level * 10);
        } else {
            art_pickup(c, tx + tile * 0.5f, ty + tile * 0.42f, 1.5f * s, G_CHEST);
            snprintf(buf, sizeof(buf), "%d", 1 + S.bosses_down);
        }
        pa_text_bold(c, buf, tx + tile - 5.0f * s, ty + tile - 18.0f * s, 12.0f * s, PA_RGB(255, 255, 255), OUTC(), PA_ALIGN_RIGHT, 1.0f * s, 1.0f);
    }
    /* the build that got you here */
    float buildy = ry2 + 22.0f * s + tile + 14.0f * s;
    float slot = (pw - 32.0f * s) / 6.0f;
    for (int i = 0; i < S.wslots; i++) {
        int w = S.wslot[i];
        float sx = px + 16.0f * s + (float)i * slot;
        pa_round_rect(c, sx, buildy, slot - 6.0f * s, slot - 6.0f * s, 6.0f * s, pa_hex(0x262B38));
        draw_icon(c, S.evo[w] ? IC_EVO(w) : w, sx + (slot - 6.0f * s) * 0.5f, buildy + (slot - 6.0f * s) * 0.45f, slot * 0.7f, 0.0f);
        for (int k = 0; k < MAX_LV; k++)
            pa_fill_circle(c, sx + (slot - 6.0f * s) * 0.5f + ((float)k - 2.0f) * 6.0f * s, buildy + slot - 14.0f * s, 2.2f * s,
                           k < S.wlv[w] ? pa_hex(0xFFC21C) : pa_hex(0x4A5066));
    }
    if (S.over_t > 0.5f) {
        button(c, hx, hy, hw, hh, "HOME", pa_hex(0x6A7290), pa_hex(0x3A4058), 20.0f * s);
        button(c, rx, ry, rw, rh, "RETRY", pa_hex(0xFFC21C), pa_hex(0xC97A10), 26.0f * s);
    }
}

static void draw_title(PA_Canvas *c) {
    float s = L.s;
    pa_hub_hide_pause();
    S.cam_x = S.clock * 18.0f;
    S.cam_y = S.clock * 8.0f;
    g_shx = g_shy = 0.0f;
    set_view();
    draw_ground(c);
    /* ambient horde walking across the lower half */
    for (int i = 0; i < 26; i++) {
        uint32_t h = hash2(i, 77);
        int set = SPR_ZOMBIE + (int)(h % 7);
        float lane = (float)L.h * (0.60f + 0.34f * (float)((h >> 8) & 255) / 255.0f);
        float x = pa_wrapf((float)((h >> 16) & 1023) + S.clock * (24.0f + (float)(h % 20)), (float)L.w + 120.0f) - 60.0f;
        int f = ((int)(S.clock * 6.0f + (float)i)) & 3;
        spr_blit(c, &g_spr[set][f], x, lane, 0, 0, 0, 255);
    }
    PA_Paint dim = pa_linear(0, 0, 0, (float)L.h);
    pa_stop(&dim, 0.0f, PA_RGBA(16, 18, 34, 235));
    pa_stop(&dim, 0.45f, PA_RGBA(16, 18, 34, 150));
    pa_stop(&dim, 1.0f, PA_RGBA(16, 18, 34, 90));
    pa_fill_rect_paint(c, 0, 0, (float)L.w, (float)L.h, &dim);

    /* bank */
    char buf[48];
    snprintf(buf, sizeof(buf), "%d", g_bank);
    pa_round_rect(c, (float)L.w - 150.0f * s, 18.0f * s, 134.0f * s, 34.0f * s, 17.0f * s, PA_RGBA(10, 12, 22, 200));
    coin_icon(c, (float)L.w - 132.0f * s, 35.0f * s, 12.0f * s);
    pa_text_bold(c, buf, (float)L.w - 26.0f * s, 26.0f * s, 17.0f * s, PA_RGB(255, 255, 255), OUTC(), PA_ALIGN_RIGHT, 1.5f * s, 1.2f);

    /* logo */
    float ly = (float)L.h * 0.09f + 30.0f * s;
    float ls = fit_size("HORDE", (float)L.w - 60.0f * s, 76.0f * s, 4.0f * s);
    pa_text_bold(c, "HORDE", (float)L.w * 0.5f + 4.0f * s, ly + 6.0f * s, ls, pa_hex(0xC2560E), OUTC(), PA_ALIGN_CENTER, 4.0f * s, 1.7f);
    pa_text_bold(c, "HORDE", (float)L.w * 0.5f, ly, ls, pa_hex(0xFFC21C), OUTC(), PA_ALIGN_CENTER, 4.0f * s, 1.7f);
    ribbon(c, "ARENA", (float)L.w * 0.5f, ly + ls + 32.0f * s, 220.0f * s, 46.0f * s, pa_hex(0xE8303A), pa_hex(0x8A1A20));

    /* chapter card with the hero */
    float cw = (float)L.w - 70.0f * s;
    if (cw > 420.0f * s) cw = 420.0f * s;
    float ch = cw * 0.92f;
    float cx = ((float)L.w - cw) * 0.5f, cy = ly + ls + 72.0f * s;
    float sx, sy, sw, sh, ax, ay, hx, hy, bw, bh;
    title_buttons(&sx, &sy, &sw, &sh, &ax, &ay, &hx, &hy, &bw, &bh);
    if (cy + ch + 70.0f * s > ay) ch = ay - 70.0f * s - cy;
    o_rrect(c, cx, cy, cw, ch, 16.0f * s, pa_hex(0x3D4357), 3.5f * s);
    PA_Paint sky = pa_linear(0, cy, 0, cy + ch);
    pa_stop(&sky, 0.0f, pa_hex(0x5B3A8C));
    pa_stop(&sky, 0.6f, pa_hex(0xE0567A));
    pa_stop(&sky, 1.0f, pa_hex(0xFF9A3A));
    pa_round_rect_paint(c, cx + 8.0f * s, cy + 40.0f * s, cw - 16.0f * s, ch - 48.0f * s, 10.0f * s, &sky);
    int c0 = c->clip_x0, c1 = c->clip_y0, c2 = c->clip_x1, c3 = c->clip_y1;
    pa_clip_rect(c, (int)(cx + 8.0f * s), (int)(cy + 40.0f * s), (int)(cw - 16.0f * s), (int)(ch - 48.0f * s));
    /* skyline */
    for (int i = 0; i < 9; i++) {
        uint32_t h = hash2(i, 5);
        float bwid = cw * 0.13f, bht = ch * (0.25f + 0.3f * (float)(h & 255) / 255.0f);
        float bx = cx + 8.0f * s + (float)i * bwid * 0.95f;
        pa_fill_rect(c, bx, cy + ch * 0.72f - bht, bwid * 0.9f, bht, pa_hex(0x3A2860));
        for (int w = 0; w < 4; w++)
            pa_fill_rect(c, bx + bwid * 0.2f + (float)(w & 1) * bwid * 0.35f, cy + ch * 0.72f - bht + 10.0f * s + (float)(w >> 1) * 18.0f * s,
                         bwid * 0.18f, 8.0f * s, ((h >> w) & 1) ? pa_hex(0xFFD86A) : pa_hex(0x4A3A78));
    }
    pa_fill_rect(c, cx, cy + ch * 0.72f, cw, ch, pa_hex(0x5D5C77));
    pa_fill_rect(c, cx, cy + ch * 0.72f, cw, 4.0f * s, pa_hex(0xAAA9BE));
    float floor_y = cy + ch - 18.0f * s;
    for (int i = 0; i < 7; i++) {
        uint32_t h = hash2(i, 9);
        int set = SPR_ZOMBIE + (int)(h % 7);
        float zx = cx + cw * (0.08f + 0.14f * (float)i);
        float zy = floor_y - 26.0f * s - (float)(h % 3) * 8.0f * s;
        spr_blit(c, &g_spr[set][((int)(S.clock * 6.0f) + i) & 3], zx, zy, zx > cx + cw * 0.5f, 0, 0, 255);
    }
    float hk = (ch * 0.62f) / 64.0f;
    float bob = fabsf(sinf(S.clock * 3.0f)) * 3.0f * s;
    art_hero(c, cx + cw * 0.5f, floor_y + 4.0f * s - bob, hk, 0);
    c->clip_x0 = c0; c->clip_y0 = c1; c->clip_x1 = c2; c->clip_y1 = c3;
    pa_text_bold(c, "CHAPTER 1", cx + 18.0f * s, cy + 10.0f * s, 18.0f * s, PA_RGB(255, 255, 255), OUTC(), PA_ALIGN_LEFT, 1.5f * s, 1.2f);
    pa_text_bold(c, "DEAD CITY", cx + cw - 18.0f * s, cy + 12.0f * s, 15.0f * s, pa_hex(0xFFC21C), OUTC(), PA_ALIGN_RIGHT, 1.5f * s, 1.1f);
    char tb[16];
    time_str(tb, sizeof(tb), (float)g_best);
    snprintf(buf, sizeof(buf), "BEST %s   GOAL 08:00", tb);
    pa_round_rect(c, cx + cw * 0.12f, cy + ch - 2.0f * s, cw * 0.76f, 30.0f * s, 15.0f * s, OUTC());
    pa_text(c, buf, cx + cw * 0.5f, cy + ch + 7.0f * s, 12.0f * s, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 2.0f * s);

    /* upgrades */
    for (int i = 0; i < 2; i++) {
        float bx = i ? hx : ax, byy = i ? hy : ay;
        int lv = i ? g_meta_hp : g_meta_atk;
        int cost = meta_cost(lv);
        int can = g_bank >= cost && lv < 10;
        o_rrect(c, bx, byy, bw, bh, 12.0f * s, pa_hex(0x343A4C), 2.5f * s);
        o_rrect(c, bx + 6.0f * s, byy + 6.0f * s, bh - 12.0f * s, bh - 12.0f * s, 8.0f * s, i ? pa_hex(0xE8303A) : pa_hex(0xFF8A1E), 1.5f * s);
        if (i) {
            float hx2 = bx + bh * 0.5f, hy2 = byy + bh * 0.5f, r = bh * 0.14f;
            pa_fill_circle(c, hx2 - r * 0.9f, hy2 - r * 0.4f, r, PA_RGB(255, 255, 255));
            pa_fill_circle(c, hx2 + r * 0.9f, hy2 - r * 0.4f, r, PA_RGB(255, 255, 255));
            PA_Vec2 hp[3] = { { hx2 - r * 1.85f, hy2 - r * 0.1f }, { hx2 + r * 1.85f, hy2 - r * 0.1f }, { hx2, hy2 + r * 1.9f } };
            pa_fill_poly(c, hp, 3, PA_RGB(255, 255, 255));
        } else {
            icon_kunai(c, bx + bh * 0.5f, byy + bh * 0.5f, bh * 0.8f, pa_hex(0xF4F4F8), pa_hex(0x3A3A48));
        }
        snprintf(buf, sizeof(buf), "%s LV%d", i ? "HEALTH" : "ATTACK", lv);
        pa_text_bold(c, buf, bx + bh + 2.0f * s, byy + 8.0f * s, 11.0f * s, PA_RGB(255, 255, 255), OUTC(), PA_ALIGN_LEFT, 1.0f * s, 1.0f);
        coin_icon(c, bx + bh + 9.0f * s, byy + bh - 15.0f * s, 7.0f * s);
        snprintf(buf, sizeof(buf), lv >= 10 ? "MAX" : "%d", cost);
        pa_text(c, buf, bx + bh + 20.0f * s, byy + bh - 21.0f * s, 11.0f * s, can ? pa_hex(0xFFD23A) : pa_hex(0x9AA3B8), PA_ALIGN_LEFT, 1.0f * s);
    }
    float pulse = 1.0f + 0.04f * sinf(S.clock * 5.0f);
    float pw2 = sw * pulse, ph2 = sh * pulse;
    button(c, sx - (pw2 - sw) * 0.5f, sy - (ph2 - sh) * 0.5f, pw2, ph2, "START", pa_hex(0xFFC21C), pa_hex(0xC97A10), 34.0f * s);
    pa_text(c, "DRAG ANYWHERE TO MOVE - WEAPONS FIRE ON THEIR OWN", (float)L.w * 0.5f, sy + sh + 26.0f * s,
            fit_size("DRAG ANYWHERE TO MOVE - WEAPONS FIRE ON THEIR OWN", (float)L.w - 30.0f * s, 11.0f * s, 1.0f * s),
            pa_hex(0xC8CCE0), PA_ALIGN_CENTER, 1.0f * s);
}

static void s_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) {
        L.w = c->w; L.h = c->h;
        L.s = (float)(c->w < c->h ? c->w : c->h) / 540.0f;
    }
    if (fabsf(g_baked_scale - L.s) > 0.001f) bake_all(L.s);
    if (S.phase == PH_TITLE) { draw_title(c); return; }

    g_shx = g_shy = 0.0f;
    if (S.shake > 0.0f) {
        g_shx = sinf(S.clock * 93.0f) * S.shake * 9.0f * L.s;
        g_shy = cosf(S.clock * 71.0f) * S.shake * 7.0f * L.s;
    }
    set_view();
    draw_ground(c);
    draw_decals(c);
    draw_zones_under(c);
    draw_field(c);
    draw_cage(c, 0);
    draw_gems(c);
    if (S.phase != PH_TITLE) draw_hero_ring(c);
    draw_actors(c);
    draw_cage(c, 1);
    draw_zones_over(c);
    draw_orbitals(c);
    draw_projectiles(c);
    draw_particles(c);
    draw_muzzle(c);
    draw_numbers(c);
    if (S.phase == PH_PLAY || S.phase == PH_DYING) draw_hero_hp(c);
    edge_vignette(c);
    draw_joystick(c);
    if (S.flash > 0.0f) {
        uint32_t fc = S.flash_col;
        pa_fill_rect(c, 0, 0, (float)L.w, (float)L.h,
                     PA_RGBA((fc >> 16) & 255, (fc >> 8) & 255, fc & 255, (int)(pa_clamp01(S.flash) * 150.0f)));
    }
    if (S.hp < S.maxhp * 0.3f && S.phase == PH_PLAY) {
        float a = 0.5f + 0.5f * sinf(S.clock * 6.0f);
        PA_Paint v = pa_radial((float)L.w * 0.5f, (float)L.h * 0.5f, (float)L.h * 0.3f, (float)L.h * 0.62f);
        pa_stop(&v, 0.0f, PA_RGBA(200, 0, 0, 0));
        pa_stop(&v, 1.0f, PA_RGBA(200, 0, 0, (int)(90 + 60 * a)));
        pa_fill_rect_paint(c, 0, 0, (float)L.w, (float)L.h, &v);
    }
    draw_hud(c);
    draw_banner(c);
    if (S.phase == PH_PLAY && S.t < 4.0f && !pa_demo_mode()) {
        pa_text_bold(c, "DRAG ANYWHERE TO MOVE", (float)L.w * 0.5f, (float)L.h * 0.70f, 16.0f * L.s, PA_RGB(255, 255, 255), OUTC(),
                     PA_ALIGN_CENTER, 2.0f * L.s, 1.2f);
    }
    if (S.phase == PH_DRAFT) { pa_hub_hide_pause(); draw_draft(c); }
    if (S.phase == PH_CHEST) { pa_hub_hide_pause(); draw_chest(c); }
    if (S.phase == PH_DYING && S.won) {
        float k = pa_clamp01(S.phase_t * 3.0f);
        pa_text_bold(c, "CHAPTER CLEAR!", (float)L.w * 0.5f, (float)L.h * 0.36f, 34.0f * L.s * (0.8f + 0.2f * k), pa_hex(0xFFD23A),
                     OUTC(), PA_ALIGN_CENTER, 2.0f * L.s, 2.0f);
    }
    if (S.phase == PH_OVER) draw_results(c);
}

/* ---------------------------------------------------------------- thumb -- */
static void s_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    pa_fill_rect(c, x, y, w, h, pa_hex(0x5D5C77));
    float u = (w < h ? w : h) / 100.0f;
    pa_fill_rect(c, x + w * 0.3f, y, w * 0.7f, h * 0.62f, pa_hex(0x83829A));
    for (int i = 1; i < 6; i++) {
        pa_fill_rect(c, x + w * 0.3f + (float)i * 14.0f * u, y, 1.5f * u, h * 0.62f, pa_hex(0x6F6E86));
        pa_fill_rect(c, x + w * 0.3f, y + (float)i * 14.0f * u, w * 0.7f, 1.5f * u, pa_hex(0x6F6E86));
    }
    for (int k = 0; k < 4; k++) pa_fill_rect(c, x + w * 0.04f, y + h * (0.7f + 0.06f * (float)k), w * 0.2f, 3.0f * u, pa_hex(0xC9C8DA));
    float cx = x + w * 0.5f, cy = y + h * 0.56f;
    for (int i = 0; i < 18; i++) {
        float a = (float)i * 0.349f + t * 0.3f;
        float r = (34.0f + (float)(i % 3) * 7.0f) * u;
        float ex = cx + cosf(a) * r, ey = cy + sinf(a) * r * 0.9f;
        PA_Color col = (i % 3 == 0) ? pa_hex(0xFF8F1C) : (i % 3 == 1) ? pa_hex(0xDE2E98) : pa_hex(0x8CC24C);
        pa_fill_circle(c, ex, ey, 6.0f * u, OUTC());
        pa_fill_circle(c, ex, ey, 4.6f * u, col);
    }
    for (int i = 0; i < 3; i++) {
        float a = t * 3.0f + (float)i * PA_TAU / 3.0f;
        icon_saw(c, cx + cosf(a) * 20.0f * u, cy + sinf(a) * 20.0f * u, 5.0f * u, pa_hex(0xE8303A), pa_hex(0x2A2A34), t * 8.0f);
    }
    o_rrect(c, cx - 6.0f * u, cy - 14.0f * u, 12.0f * u, 11.0f * u, 3.0f * u, pa_hex(0x7A3B2C), 1.4f * u);
    pa_round_rect(c, cx - 5.5f * u, cy - 13.0f * u, 11.0f * u, 4.0f * u, 2.0f * u, pa_hex(0x8ED2F4));
    o_rrect(c, cx - 4.5f * u, cy - 3.0f * u, 9.0f * u, 7.0f * u, 2.0f * u, pa_hex(0x545A6E), 1.4f * u);
    pa_fill_rect(c, x + w * 0.06f, y + h * 0.06f, w * 0.88f, 6.0f * u, OUTC());
    pa_fill_rect(c, x + w * 0.06f + u, y + h * 0.06f + u, w * 0.88f * (0.3f + 0.5f * pa_wrapf(t * 0.2f, 1.0f)), 4.0f * u, pa_hex(0x9BE22A));
}

const PA_Game PA_GAME_HORDE = {
    "horde", "Horde Arena", "Survivor",
    "Steer through the swarm, draft upgrades, survive the waves.",
    PA_RGB(150, 90, 255),
    s_start, s_stop, s_update, s_render, s_thumb
};
