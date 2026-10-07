/* ===========================================================================
   VOID MUNCHER - native port, modeled on Hole.io
   Drag anywhere to steer a hole through a pastel city. It swallows anything
   smaller than its mouth, grows a size with every few bites, and four rival
   holes are racing it for the same district. Two minutes, then the ranking.

   What makes the format read is the moment of swallowing, so that is where
   the work goes: props near the lip tip toward the mouth, slide in and fall
   inside the pit (drawn through the opening, not stamped over it), the rim
   squashes, debris and dust kick up, stars stream to the progress bar and a
   "+N" floats off. Size-ups pop the rim with an overshoot and the camera eases
   out so the hole stays the hero of the frame.

   Rendering is an oblique three-quarter projection turned off the street grid.
   Every prop is drawn in its own local frame through one affine transform, so
   a tipping car is the same code as a parked one; faces are lit by their
   world normal (warm key light, cool shadow tint). The software rasterizer
   pays per scanline across its clip width, so every fill here clips to the
   shape's own horizontal extent first and props drop detail with screen size.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define MAX_PROPS  4600
#define MAX_VOIDS  5
#define MAX_BLOCKS 144
#define MAX_PARTS  260
#define MAX_POPS   20
#define MAX_FLY    28

/* Camera: grid turned VIEW_ROT off the screen axes, ground depth squashed by
   GROUND_K and heights raised at HEIGHT_K - a steep, close three-quarter view. */
#define VIEW_ROT  0.62f
#define GROUND_K  0.74f
#define HEIGHT_K  0.86f

#define ROUND_TIME 120.0f
#define SPAN       260.0f   /* block pitch */
#define ROAD       80.0f    /* road width */
#define WALK       18.0f    /* sidewalk width */
#define KERB       3.0f
#define GRAVITY    760.0f

enum { W_STREET, W_ROAD, W_LOT, W_PARK, W_SPECIAL };
enum {
    ST_CONE, ST_BIN, ST_HYDRANT, ST_LAMP, ST_PERSON, ST_MAILBOX, ST_BUSH, ST_BARRIER,
    ST_BENCH, ST_TREE, ST_BOOTH, ST_SCOOTER, ST_CART,
    ST_CAR, ST_CAB, ST_POLICE, ST_VAN, ST_BUS, ST_TRUCK,
    ST_GARAGE, ST_BROWN, ST_SHOP, ST_APART, ST_OFFICE, ST_SPIRE,
    ST_COIN, ST_GEM, ST_COUNT
};

typedef struct {
    int   where;
    float r;               /* mouth radius needed to swallow it */
    float len, wid, h;     /* footprint along local x, y; height */
    int   value;
    uint32_t colour;
} PropType;

static const PropType TYPES[ST_COUNT] = {
    [ST_CONE]    = { W_STREET,  6.f,   9.f,   9.f,  13.f,   1, 0xFF7A2E },
    [ST_BIN]     = { W_STREET,  7.f,  10.f,  10.f,  14.f,   1, 0x37B26C },
    [ST_HYDRANT] = { W_STREET,  6.f,   7.f,   7.f,  12.f,   1, 0xEE3B4E },
    [ST_LAMP]    = { W_STREET,  6.f,   4.f,   4.f,  36.f,   1, 0x5A6488 },
    [ST_PERSON]  = { W_STREET,  8.f,   7.f,   8.f,  20.f,   2, 0xE8455F },
    [ST_MAILBOX] = { W_STREET,  7.f,   8.f,   7.f,  14.f,   1, 0x3C6FE8 },
    [ST_BUSH]    = { W_PARK,    9.f,  14.f,  14.f,  12.f,   2, 0x4FC25A },
    [ST_BARRIER] = { W_STREET,  9.f,  18.f,   5.f,  10.f,   2, 0xFFFFFF },
    [ST_BENCH]   = { W_PARK,   11.f,  24.f,   9.f,  11.f,   3, 0xC9864E },
    [ST_TREE]    = { W_PARK,   13.f,  26.f,  26.f,  40.f,   4, 0x5CCB52 },
    [ST_BOOTH]   = { W_STREET, 12.f,  12.f,  12.f,  30.f,   4, 0xE8455F },
    [ST_SCOOTER] = { W_STREET, 15.f,  24.f,   9.f,  15.f,   6, 0xFF5D8F },
    [ST_CART]    = { W_PARK,   17.f,  26.f,  16.f,  32.f,   8, 0xFFC93C },
    [ST_CAR]     = { W_ROAD,   21.f,  44.f,  22.f,  18.f,  12, 0x3E8BFF },
    [ST_CAB]     = { W_ROAD,   21.f,  44.f,  22.f,  18.f,  12, 0xFFC52E },
    [ST_POLICE]  = { W_ROAD,   22.f,  46.f,  22.f,  19.f,  14, 0xF4F6FF },
    [ST_VAN]     = { W_ROAD,   25.f,  50.f,  24.f,  28.f,  18, 0xF4F6FF },
    [ST_BUS]     = { W_ROAD,   34.f,  82.f,  27.f,  31.f,  34, 0xF0475A },
    [ST_TRUCK]   = { W_ROAD,   35.f,  84.f,  28.f,  35.f,  36, 0x4F7BE8 },
    [ST_GARAGE]  = { W_LOT,    37.f,  52.f,  46.f,  30.f,  40, 0xF6D9A8 },
    [ST_BROWN]   = { W_LOT,    52.f,  64.f,  60.f,  96.f,  90, 0xD06A4E },
    [ST_SHOP]    = { W_LOT,    52.f,  66.f,  60.f,  58.f,  90, 0x9FDCC8 },
    [ST_APART]   = { W_LOT,    56.f,  64.f,  64.f, 124.f, 120, 0xF4A6B8 },
    [ST_OFFICE]  = { W_LOT,    78.f, 112.f, 108.f, 232.f, 260, 0x76A4F0 },
    [ST_SPIRE]   = { W_LOT,    74.f, 104.f, 104.f, 292.f, 280, 0xB7A6E8 },
    [ST_COIN]    = { W_SPECIAL, 0.f,  12.f,  12.f,  10.f,   0, 0xFFC93C },
    [ST_GEM]     = { W_SPECIAL, 0.f,  12.f,  12.f,  12.f,   0, 0xC04BFF },
};

static const char *TYPE_NAMES[] = {
    "CONES", "BINS", "HYDRANTS", "LAMPS", "PEOPLE", "MAILBOXES", "BUSHES", "BARRIERS",
    "BENCHES", "TREES", "BOOTHS", "SCOOTERS", "FOOD CARTS", "CARS", "CABS", "POLICE CARS",
    "VANS", "BUSES", "TRUCKS", "GARAGES", "HOUSES", "SHOPS", "APARTMENTS", "OFFICES", "TOWERS",
    "COINS", "GEMS"
};

static const uint32_t BROWN_TINTS[] = { 0xD06A4E, 0xE0855C, 0xC45A5A, 0xE8A070 };
static const uint32_t PASTELS[]     = { 0xF4A6B8, 0xB7A6E8, 0x8FB8F0, 0xF7B98B, 0xF2D8A6, 0x9FDCC8 };
static const uint32_t GLASS_TINTS[] = { 0x76A4F0, 0x6C8FE8, 0x8AB4F4, 0x9C8FE8 };
static const uint32_t CAR_TINTS[]   = { 0x3E8BFF, 0xF0475A, 0x37C47A, 0xFF8A3D, 0x9B6BFF, 0xF4F6FF };
static const uint32_t SHIRTS[]      = { 0xF0475A, 0x3E8BFF, 0xFFC52E, 0x37C47A, 0x9B6BFF, 0xFF8A3D, 0xF4F6FF };
static const uint32_t PANTS[]       = { 0x2E3866, 0x4A5890, 0x5B4636, 0x2B2F45 };
static const uint32_t SKINS_TONE[]  = { 0xF6CBA4, 0xE3A87C, 0xB97A55, 0x8A5A3C };
#define COUNT_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Hole skins: rim top, rim side, highlight, label colour. */
typedef struct { uint32_t top, side, light, label; } Skin;
static const Skin SKINS[] = {
    { 0x4FB3FF, 0x1E5FD0, 0xC8ECFF, 0x9CD8FF },   /* player */
    { 0xFF5C78, 0xC0213F, 0xFFD0D8, 0xFF8EA2 },
    { 0xB07CFF, 0x6A34C8, 0xE6D6FF, 0xC9A6FF },
    { 0x5BE08A, 0x1F9E55, 0xD2FFE0, 0x7CF0A6 },
    { 0xFFB04A, 0xD86A12, 0xFFE6C2, 0xFFC673 },
};
static const char *RIVAL_NAMES[] = {
    "Abyss", "Gulp", "Disco Potato", "Chasm", "Maw", "Sinkhole", "Katz", "Nom Nom",
    "Big Dipper", "Void King"
};

/* ------------------------------------------------------------------ state -- */
enum { PS_STAND, PS_TIP, PS_FALL, PS_GONE };

typedef struct {
    float x, y, z;
    float vx, vy, vz;      /* sliding / falling */
    float cvx, cvy;        /* cruise velocity: traffic and walkers */
    float rot, tilt, tdir, spin;
    float t;               /* walk phase, fall timer */
    float panic;
    float lo, hi;          /* walker range along its line */
    PA_Color col, col2;
    unsigned short seed;
    unsigned char style, state, eater, axis;
} Prop;

typedef struct {
    int   player, alive;
    char  name[16];
    float x, y, vx, vy;
    float r, rv;           /* drawn radius and its spring velocity */
    int   level, xp, score, kills, coins;
    float squash, pop, grace, respawn, dying;
    float dx0, dy0;        /* where a dying hole started sliding from */
    int   skin, killer;
    float think, skill;
    int   target, chase;
    float streak_t;
    int   streak;
    float hx, hy;
} Void;

typedef struct { float x, y, half; int kind; unsigned short seed; } Block;
enum { BK_PARK, BK_LOTS, BK_TOWER, BK_PARKING, BK_PLAZA };

enum { PK_DEBRIS, PK_DUST, PK_STAR, PK_SPARK, PK_RING };
typedef struct {
    float x, y, z, vx, vy, vz, life, max, size, rot;
    PA_Color col;
    int kind;
} Part;

typedef struct { float x, y, z, t, born; int value, kind, hole; } Pop;   /* kind 0 points, 1 coin, 2 skull */
typedef struct { float sx, sy, t, dur, bend; int live; } Fly;

enum { PH_MENU, PH_PLAY, PH_TIMEUP, PH_RESULTS };

typedef struct {
    Prop   props[MAX_PROPS];
    int    prop_count;
    Block  blocks[MAX_BLOCKS];
    int    block_count;
    int    cells_x, cells_y;
    float  half_x, half_y;

    Void   voids[MAX_VOIDS];
    int    void_count;

    Part   parts[MAX_PARTS];
    int    part_next;
    Pop    pops[MAX_POPS];
    Fly    fly[MAX_FLY];

    int    phase;
    float  phase_t, timer, time;
    int    level, total_value;
    float  cam_x, cam_y, cam_scale, shake;
    float  bar_pulse, kill_bump, last_tick;
    char   banner[32];
    float  banner_t;
    PA_Color banner_col;
    char   toast[40];
    float  toast_t;
    int    order[MAX_VOIDS];
    int    place, coins_round, new_best;
    float  sfx_gap;
    PA_Rng rng;
    float  joy_x, joy_y;
    int    joy_on;
    int    unlocked;       /* highest style index announced */
} Muncher;

static Muncher V;
static int     g_best_pct = -1;   /* best share x10 */
static int     g_coins = -1;

static struct { int w, h; float unit, cy; } L;
static float g_cr, g_sr;
static float g_LX, g_LY, g_LZ;    /* key light, world space */
static float g_DX, g_DY, g_DZ;    /* toward the camera, world space */

/* ---------------------------------------------------------------- helpers -- */
/* Twenty percent wider per size up to the towers, then a slower creep so a
   runaway hole still leaves a city on screen. */
static float level_radius(int lv) {
    if (lv <= 10) return 15.0f * powf(1.2f, (float)(lv - 1));
    return 15.0f * powf(1.2f, 9.0f) * powf(1.07f, (float)(lv - 10));
}
static int   level_need(int lv)   { return (int)(7.0f * powf(1.62f, (float)(lv - 1)) + 0.5f); }
static float ext_of(int style)    { const PropType *t = &TYPES[style]; return (t->len > t->wid ? t->len : t->wid) * 0.5f; }

static PA_Vec2 P3(float x, float y, float z) {
    float dx = x - V.cam_x, dy = y - V.cam_y;
    float vx = dx * g_cr - dy * g_sr, vy = dx * g_sr + dy * g_cr;
    PA_Vec2 o;
    o.x = (float)L.w * 0.5f + vx * V.cam_scale;
    o.y = L.cy + vy * V.cam_scale * GROUND_K - z * V.cam_scale * HEIGHT_K;
    return o;
}

static void setup_view(void) {
    g_cr = cosf(VIEW_ROT);
    g_sr = sinf(VIEW_ROT);
    /* Key light from screen-left, slightly toward the viewer, mostly above. */
    float lvx = -0.52f, lvy = 0.30f, lz = 0.80f;
    float n = sqrtf(lvx * lvx + lvy * lvy + lz * lz);
    lvx /= n; lvy /= n; lz /= n;
    g_LX = lvx * g_cr + lvy * g_sr;
    g_LY = -lvx * g_sr + lvy * g_cr;
    g_LZ = lz;
    g_DX = HEIGHT_K * g_sr;
    g_DY = HEIGHT_K * g_cr;
    g_DZ = GROUND_K;
}

static void layout(int w, int h) {
    L.w = w; L.h = h;
    L.unit = (float)(w < h ? w : h);
    L.cy = (float)h * (h > w ? 0.56f : 0.58f);
}

/* ------------------------------------------------------------- generation -- */
static Prop *add_prop(PA_Rng *r, int style, float x, float y, float rot) {
    if (V.prop_count >= MAX_PROPS) return NULL;
    Prop *p = &V.props[V.prop_count++];
    memset(p, 0, sizeof(*p));
    p->x = x; p->y = y; p->rot = rot;
    p->style = (unsigned char)style;
    p->eater = 255;
    p->seed = (unsigned short)pa_rng_int(r, 0, 0xFFFF);
    p->col = pa_hex(TYPES[style].colour);
    p->col2 = pa_hex(0xFFFFFF);
    switch (style) {
    case ST_BROWN:  p->col = pa_hex(BROWN_TINTS[pa_rng_int(r, 0, COUNT_OF(BROWN_TINTS) - 1)]); break;
    case ST_SHOP: case ST_APART: case ST_GARAGE:
        p->col = pa_hex(PASTELS[pa_rng_int(r, 0, COUNT_OF(PASTELS) - 1)]);
        p->col2 = pa_hex(PASTELS[pa_rng_int(r, 0, COUNT_OF(PASTELS) - 1)]);
        break;
    case ST_OFFICE: case ST_SPIRE:
        p->col = pa_hex(GLASS_TINTS[pa_rng_int(r, 0, COUNT_OF(GLASS_TINTS) - 1)]);
        p->col2 = pa_hex(PASTELS[pa_rng_int(r, 0, COUNT_OF(PASTELS) - 1)]);
        break;
    case ST_CAR: case ST_VAN:
        p->col = pa_hex(CAR_TINTS[pa_rng_int(r, 0, COUNT_OF(CAR_TINTS) - 1)]);
        p->col2 = pa_rng_chance(r, 0.5f) ? pa_hex(0xFFFFFF) : pa_shade(p->col, -0.25f);
        break;
    case ST_PERSON:
        p->col = pa_hex(SHIRTS[pa_rng_int(r, 0, COUNT_OF(SHIRTS) - 1)]);
        p->col2 = pa_hex(PANTS[pa_rng_int(r, 0, COUNT_OF(PANTS) - 1)]);
        p->t = pa_rng_range(r, 0.0f, 6.0f);
        break;
    case ST_CART: p->col2 = pa_hex(pa_rng_chance(r, 0.5f) ? 0xF0475A : 0x3E8BFF); break;
    case ST_TREE: case ST_BUSH:
        p->col = pa_mix(pa_hex(TYPES[style].colour), pa_hex(pa_rng_chance(r, 0.3f) ? 0x9AD94A : 0x3FB06A),
                        pa_rng_range(r, 0.0f, 0.5f));
        break;
    default: break;
    }
    return p;
}

static int pick_lot_style(PA_Rng *r, float ring, int level) {
    float big = pa_clamp01(ring * 1.2f + (float)level * 0.05f);
    float roll = pa_rng_next(r);
    if (roll < 0.22f - big * 0.14f) return ST_GARAGE;
    if (roll < 0.48f - big * 0.10f) return ST_SHOP;
    if (roll < 0.76f) return ST_BROWN;
    return ST_APART;
}

static void walker(PA_Rng *r, float x, float y, int axis, float lo, float hi) {
    Prop *p = add_prop(r, ST_PERSON, x, y, 0.0f);
    if (!p) return;
    p->axis = (unsigned char)axis;
    p->lo = lo; p->hi = hi;
    float sp = pa_rng_range(r, 16.0f, 26.0f) * (pa_rng_chance(r, 0.5f) ? 1.0f : -1.0f);
    p->cvx = axis == 0 ? sp : 0.0f;
    p->cvy = axis == 0 ? 0.0f : sp;
    p->rot = atan2f(p->cvy, p->cvx);
}

static void build_city(int level) {
    PA_Rng r;
    pa_rng_seed(&r, 0x5EED1u ^ ((uint32_t)level * 7919u));
    V.prop_count = 0;
    V.block_count = 0;

    V.cells_x = 7 + (level - 1) / 2;
    if (V.cells_x > 9) V.cells_x = 9;
    V.cells_y = V.cells_x + 2;
    V.half_x = SPAN * (float)V.cells_x * 0.5f;
    V.half_y = SPAN * (float)V.cells_y * 0.5f;
    float half = V.half_x > V.half_y ? V.half_x : V.half_y;
    float bw = SPAN - ROAD;
    float inner = bw * 0.5f - WALK;     /* 72: the lot inside the sidewalk ring */

    for (int cy = 0; cy < V.cells_y; cy++) {
        for (int cx = 0; cx < V.cells_x; cx++) {
            float bx = -V.half_x + SPAN * ((float)cx + 0.5f);
            float by = -V.half_y + SPAN * ((float)cy + 0.5f);
            float ring = pa_clamp01(sqrtf(bx * bx + by * by) / (half * 0.95f));
            int near_start = fabsf(bx) < SPAN && fabsf(by) < SPAN;
            int kind;
            float roll = pa_rng_next(&r);
            if (near_start) kind = (cx + cy) % 2 == 0 ? BK_PARK : (cx % 2 ? BK_PARKING : BK_LOTS);
            else if (roll < 0.12f) kind = BK_PARK;
            else if (roll < 0.22f) kind = BK_PARKING;
            else if (roll < 0.30f) kind = BK_PLAZA;
            else if (roll < 0.30f + 0.10f + ring * 0.38f + (float)level * 0.02f) kind = BK_TOWER;
            else kind = BK_LOTS;

            if (V.block_count >= MAX_BLOCKS) continue;
            Block *b = &V.blocks[V.block_count++];
            b->x = bx; b->y = by; b->half = bw * 0.5f; b->kind = kind;
            b->seed = (unsigned short)pa_rng_int(&r, 0, 0xFFFF);

            switch (kind) {
            case BK_LOTS: {
                float lh = inner * 0.5f;
                for (int q = 0; q < 4; q++) {
                    float lx = bx + ((q & 1) ? lh : -lh), ly = by + ((q & 2) ? lh : -lh);
                    if (pa_rng_chance(&r, 0.12f)) {
                        add_prop(&r, ST_TREE, lx - 12.0f, ly - 8.0f, 0.0f);
                        add_prop(&r, ST_BENCH, lx + 10.0f, ly + 14.0f, 0.0f);
                        add_prop(&r, ST_BIN, lx + 22.0f, ly - 16.0f, 0.0f);
                        continue;
                    }
                    int st = pick_lot_style(&r, ring, level);
                    float rot = pa_rng_chance(&r, 0.5f) ? 0.0f : PA_PI * 0.5f;
                    float jx = pa_rng_range(&r, -2.0f, 2.0f), jy = pa_rng_range(&r, -2.0f, 2.0f);
                    add_prop(&r, st, lx + jx, ly + jy, rot);
                }
                break;
            }
            case BK_TOWER: {
                int st = pa_rng_chance(&r, 0.55f) ? ST_OFFICE : ST_SPIRE;
                add_prop(&r, st, bx, by, 0.0f);
                break;
            }
            case BK_PLAZA: {
                /* A shop and an apartment on one side, a fountain plaza with
                   trees, benches and a food cart on the other. */
                add_prop(&r, ST_SHOP, bx - inner * 0.5f, by - inner * 0.5f, 0.0f);
                add_prop(&r, ST_APART, bx + inner * 0.5f, by - inner * 0.5f, 0.0f);
                add_prop(&r, ST_CART, bx - 18.0f, by + 40.0f, 0.3f);
                for (int k = 0; k < 4; k++) add_prop(&r, ST_TREE, bx - 52.0f + 34.0f * (float)k, by + 60.0f, 0.0f);
                add_prop(&r, ST_BENCH, bx + 30.0f, by + 30.0f, 0.0f);
                add_prop(&r, ST_BENCH, bx - 46.0f, by + 24.0f, 0.0f);
                walker(&r, bx, by + 18.0f, 0, bx - 60.0f, bx + 60.0f);
                walker(&r, bx + 20.0f, by + 44.0f, 0, bx - 60.0f, bx + 60.0f);
                break;
            }
            case BK_PARKING: {
                for (int row = 0; row < 2; row++) {
                    for (int k = 0; k < 5; k++) {
                        if (pa_rng_chance(&r, 0.25f)) continue;
                        float px = bx - inner + 18.0f + (float)k * 27.0f;
                        float py = by + (row ? 30.0f : -30.0f);
                        int st = pa_rng_chance(&r, 0.2f) ? ST_VAN : (pa_rng_chance(&r, 0.2f) ? ST_CAB : ST_CAR);
                        add_prop(&r, st, px, py, PA_PI * 0.5f + (row ? PA_PI : 0.0f));
                    }
                }
                add_prop(&r, ST_BOOTH, bx + inner - 10.0f, by - inner + 10.0f, 0.0f);
                walker(&r, bx, by, 0, bx - 60.0f, bx + 60.0f);
                break;
            }
            default: { /* park */
                int n = 9 + pa_rng_int(&r, 0, 4);
                for (int k = 0; k < n; k++) {
                    float x = bx + pa_rng_range(&r, -inner + 12.0f, inner - 12.0f);
                    float y = by + pa_rng_range(&r, -inner + 12.0f, inner - 12.0f);
                    if (fabsf(x - bx) < 8.0f || fabsf(y - by) < 8.0f) continue;   /* paths */
                    float roll2 = pa_rng_next(&r);
                    int st = roll2 < 0.48f ? ST_TREE : roll2 < 0.70f ? ST_BUSH : roll2 < 0.86f ? ST_BENCH : ST_CART;
                    add_prop(&r, st, x, y, pa_rng_chance(&r, 0.5f) ? 0.0f : PA_PI * 0.5f);
                }
                walker(&r, bx, by - 30.0f, 1, by - 60.0f, by + 60.0f);
                walker(&r, bx - 30.0f, by, 0, bx - 60.0f, bx + 60.0f);
                walker(&r, bx + 4.0f, by + 30.0f, 1, by - 60.0f, by + 60.0f);
                break;
            }
            }

            /* Sidewalk ring: lamps on a fixed pitch, the rest of the street
               furniture scattered between, and people walking the kerb. */
            float edge = bw * 0.5f - WALK * 0.5f;
            for (int side = 0; side < 4; side++) {
                int axis = (side & 1) ? 1 : 0;
                float sgn = side < 2 ? -1.0f : 1.0f;
                for (float s = -edge + 14.0f; s < edge - 10.0f; s += 22.0f) {
                    float x = axis == 0 ? bx + s : bx + sgn * edge;
                    float y = axis == 0 ? by + sgn * edge : by + s;
                    int k = (int)((s + edge) / 22.0f);
                    int st = -1;
                    if (k % 4 == 1) st = ST_LAMP;
                    else {
                        float roll3 = pa_rng_next(&r);
                        if (roll3 < 0.10f) st = ST_HYDRANT;
                        else if (roll3 < 0.20f) st = ST_BIN;
                        else if (roll3 < 0.26f) st = ST_MAILBOX;
                        else if (roll3 < 0.34f) st = ST_TREE;
                        else if (roll3 < 0.38f) st = ST_BOOTH;
                        else if (roll3 < 0.43f) st = ST_SCOOTER;
                        else if (roll3 < 0.48f) st = ST_CONE;
                        else if (roll3 < 0.51f) st = ST_BENCH;
                    }
                    if (st < 0) continue;
                    if (st == ST_TREE && kind == BK_TOWER) st = ST_BUSH;
                    add_prop(&r, st, x, y, axis == 0 ? 0.0f : PA_PI * 0.5f);
                }
                int walkers = 1 + pa_rng_int(&r, 0, 1);
                for (int k = 0; k < walkers; k++) {
                    float s = pa_rng_range(&r, -edge, edge);
                    float off = pa_rng_range(&r, -4.0f, 4.0f);
                    if (axis == 0) walker(&r, bx + s, by + sgn * (edge + off), 0, bx - edge, bx + edge);
                    else walker(&r, bx + sgn * (edge + off), by + s, 1, by - edge, by + edge);
                }
            }
        }
    }

    /* Traffic: two lanes per road, mixed vehicles, a heavier mix at higher levels. */
    for (int axis = 0; axis < 2; axis++) {
        int lines = axis == 0 ? V.cells_y + 1 : V.cells_x + 1;
        float len = axis == 0 ? V.half_x : V.half_y;
        for (int i = 0; i < lines; i++) {
            float line = axis == 0 ? -V.half_y + SPAN * (float)i : -V.half_x + SPAN * (float)i;
            for (int lane = 0; lane < 2; lane++) {
                float dir = lane == 0 ? 1.0f : -1.0f;
                float off = ROAD * 0.24f * dir;
                float speed = pa_rng_range(&r, 46.0f, 64.0f) * dir;
                int n = 2 + pa_rng_int(&r, 0, 2);
                for (int k = 0; k < n; k++) {
                    float roll = pa_rng_next(&r);
                    int st = roll < 0.40f ? ST_CAR : roll < 0.58f ? ST_CAB : roll < 0.68f ? ST_POLICE
                           : roll < 0.82f ? ST_VAN : roll < 0.92f ? ST_BUS : ST_TRUCK;
                    float s = -len + (2.0f * len) * ((float)k + pa_rng_range(&r, 0.1f, 0.5f)) / (float)n;
                    float x = axis == 0 ? s : line + off;
                    float y = axis == 0 ? line + off : s;
                    float rot = axis == 0 ? (dir > 0 ? 0.0f : PA_PI) : (dir > 0 ? PA_PI * 0.5f : -PA_PI * 0.5f);
                    Prop *p = add_prop(&r, st, x, y, rot);
                    if (!p) continue;
                    p->cvx = axis == 0 ? speed : 0.0f;
                    p->cvy = axis == 0 ? 0.0f : speed;
                }
            }
        }
    }

    /* Clear a landing pad, then dress it as a road works site: food that a
       brand-new hole can actually take in its first seconds. */
    for (int i = V.prop_count - 1; i >= 0; i--) {
        const Prop *p = &V.props[i];
        if (TYPES[p->style].r > 13.0f && p->x * p->x + p->y * p->y < 150.0f * 150.0f)
            V.props[i] = V.props[--V.prop_count];
    }
    for (int k = 0; k < 9; k++) {
        float a = PA_TAU * (float)k / 9.0f + 0.3f;
        float rr = 46.0f + (float)(k % 3) * 9.0f;
        add_prop(&r, k % 4 == 3 ? ST_BARRIER : ST_CONE, cosf(a) * rr, sinf(a) * rr, a + PA_PI * 0.5f);
    }
    walker(&r, -30.0f, 70.0f, 0, -90.0f, 60.0f);
    walker(&r, 60.0f, -34.0f, 1, -90.0f, 60.0f);

    /* Rewards: coins along the streets and a few gems in the parks. */
    for (int k = 0; k < 44; k++) {
        float x, y;
        if (k % 2 == 0) {
            int ln = pa_rng_int(&r, 0, V.cells_y);
            x = pa_rng_range(&r, -V.half_x, V.half_x);
            y = -V.half_y + SPAN * (float)ln + pa_rng_range(&r, -10.0f, 10.0f);
        } else {
            int ln = pa_rng_int(&r, 0, V.cells_x);
            y = pa_rng_range(&r, -V.half_y, V.half_y);
            x = -V.half_x + SPAN * (float)ln + pa_rng_range(&r, -10.0f, 10.0f);
        }
        Prop *p = add_prop(&r, k % 9 == 4 ? ST_GEM : ST_COIN, x, y, 0.0f);
        if (p) p->t = pa_rng_range(&r, 0.0f, 6.0f);
    }

    V.total_value = 0;
    for (int i = 0; i < V.prop_count; i++) V.total_value += TYPES[V.props[i].style].value;
    if (V.total_value < 1) V.total_value = 1;
}

/* ------------------------------------------------------------------ setup -- */
static float target_scale(float r) {
    /* The hole is the hero: it fills about a quarter of the width at the start
       and a third once it has grown, with the city zooming out around it. */
    float frac = 0.34f + 0.08f * pa_clamp01((r - 15.0f) / 28.0f);
    float s = frac * L.unit / (2.0f * 1.24f * r);
    float lo = L.unit / 1100.0f;
    return pa_clampf(s, lo, 4.2f);
}

static void reset_void(Void *v) {
    v->level = 1;
    v->xp = 0;
    v->r = level_radius(1);
    v->rv = 0.0f;
    v->vx = v->vy = 0.0f;
    v->alive = 1;
    v->dying = 0.0f;
    v->target = -1;
    v->chase = -1;
}

static void start_round(void) {
    if (g_best_pct < 0) g_best_pct = pa_save_get("voidmuncher.bestpct", 0);
    if (g_coins < 0) g_coins = pa_save_get("voidmuncher.coins", 0);
    if (L.w == 0) layout(540, 1170);
    setup_view();

    int level = pa_demo_mode() ? 3 : pa_save_get("voidmuncher.level", 1);
    if (level < 1) level = 1;

    memset(&V, 0, sizeof(V));
    V.level = level;
    pa_rng_seed(&V.rng, 0xBEEFu ^ (uint32_t)level * 31u);
    build_city(level);

    V.void_count = 0;
    Void *me = &V.voids[V.void_count++];
    me->player = 1;
    snprintf(me->name, sizeof(me->name), "Player");
    reset_void(me);
    me->skin = 0;
    me->grace = 5.0f;

    PA_Rng r;
    pa_rng_seed(&r, 0xC0FFEEu ^ ((uint32_t)level * 131u));
    for (int i = 0; i < 4 && V.void_count < MAX_VOIDS; i++) {
        float ang = PA_TAU * ((float)i + 0.5f) / 4.0f + 0.4f;
        Void *v = &V.voids[V.void_count++];
        reset_void(v);
        snprintf(v->name, sizeof(v->name), "%s", RIVAL_NAMES[(level * 3 + i * 7) % COUNT_OF(RIVAL_NAMES)]);
        v->x = cosf(ang) * V.half_x * 0.55f;
        v->y = sinf(ang) * V.half_y * 0.55f;
        v->skin = 1 + i;
        v->skill = pa_clamp01(0.5f + (float)level * 0.05f + pa_rng_range(&r, -0.10f, 0.10f));
        v->think = pa_rng_range(&r, 0.0f, 0.4f);
    }
    /* In review captures one rival works the blocks next to the player, so a
       rival hole is actually in shot. */
    if (pa_demo_mode()) { V.voids[1].x = 150.0f; V.voids[1].y = -250.0f; }

    V.timer = ROUND_TIME;
    V.phase = PH_MENU;
    V.cam_scale = target_scale(me->r) * 0.82f;
    V.unlocked = ST_TREE;
}

static void muncher_start(void) { start_round(); }
static void muncher_stop(void) { }

/* ------------------------------------------------------------- feedback -- */
static void spawn_part(int kind, float x, float y, float z, float vx, float vy, float vz,
                       float life, float size, PA_Color col) {
    Part *q = &V.parts[V.part_next];
    V.part_next = (V.part_next + 1) % MAX_PARTS;
    q->kind = kind;
    q->x = x; q->y = y; q->z = z;
    q->vx = vx; q->vy = vy; q->vz = vz;
    q->life = q->max = life;
    q->size = size;
    q->rot = pa_rng_range(&V.rng, 0.0f, PA_TAU);
    q->col = col;
}

static void add_pop(const Void *v, int hole, int value, int kind) {
    /* Bites inside a quarter second merge into one number, so a hole chewing
       through a row of cones counts up instead of stacking a column of text. */
    for (int i = 0; i < MAX_POPS; i++) {
        Pop *p = &V.pops[i];
        if (p->t > 0.0f && p->hole == hole && p->kind == kind && kind != 2 && p->t > 0.45f && V.time - p->born < 0.6f) {
            p->value += value;
            p->t = p->t > 0.9f ? p->t : 0.9f;
            return;
        }
    }
    int slot = 0;
    float least = 9.0f;
    for (int i = 0; i < MAX_POPS; i++) if (V.pops[i].t < least) { least = V.pops[i].t; slot = i; }
    /* A fresh number retires the previous one for this hole, so two never
       stack on top of each other. */
    for (int i = 0; i < MAX_POPS; i++)
        if (V.pops[i].t > 0.15f && V.pops[i].hole == hole && V.pops[i].kind == kind) V.pops[i].t = 0.15f;
    Pop *p = &V.pops[slot];
    p->x = v->x; p->y = v->y; p->z = 0.0f;
    p->t = 1.0f;
    p->born = V.time;
    p->value = value;
    p->kind = kind;
    p->hole = hole;
}

static void fly_star(const Void *v) {
    for (int i = 0; i < MAX_FLY; i++) {
        Fly *f = &V.fly[i];
        if (f->live) continue;
        PA_Vec2 s = P3(v->x, v->y, v->r * 0.3f);
        f->sx = s.x + pa_rng_range(&V.rng, -v->r, v->r) * V.cam_scale * 0.5f;
        f->sy = s.y;
        f->t = 0.0f;
        f->dur = pa_rng_range(&V.rng, 0.55f, 0.8f);
        f->bend = pa_rng_range(&V.rng, -160.0f, 160.0f);
        f->live = 1;
        return;
    }
}

static void announce(const char *text, PA_Color col) {
    snprintf(V.banner, sizeof(V.banner), "%s", text);
    V.banner_t = 1.6f;
    V.banner_col = col;
}

static void gain(Void *v, int hole, int pts, int scored) {
    if (scored) v->score += pts;
    v->xp += pts;
    int ups = 0;
    while (v->xp >= level_need(v->level)) {
        v->xp -= level_need(v->level);
        v->level++;
        ups++;
    }
    if (!ups) return;
    v->pop = 1.0f;
    v->rv += level_radius(v->level) * 2.6f;     /* overshoot kick */
    for (int k = 0; k < 10; k++) {
        float a = PA_TAU * (float)k / 10.0f;
        spawn_part(PK_SPARK, v->x + cosf(a) * v->r * 1.2f, v->y + sinf(a) * v->r * 1.2f, 4.0f,
                   cosf(a) * v->r * 1.4f, sinf(a) * v->r * 1.4f, 60.0f, 0.5f, 0.14f * v->r,
                   pa_hex(SKINS[v->skin].light));
    }
    spawn_part(PK_RING, v->x, v->y, 0.0f, 0, 0, 0, 0.45f, v->r, PA_RGB(255, 255, 255));
    if (v->player) {
        pa_tone(520.0f, 1040.0f, 0.18f, 1, 0.10f);
        pa_tone(780.0f, 1560.0f, 0.22f, 0, 0.06f);
        V.shake = 0.25f;
        /* Name the newest thing that now fits, the way size tiers are
           announced in the reference. */
        float mouth = level_radius(v->level) * 0.95f;
        int best = -1;
        for (int s = 0; s < ST_COIN; s++)
            if (TYPES[s].r <= mouth && s > V.unlocked && (best < 0 || TYPES[s].r > TYPES[best].r)) best = s;
        if (best >= 0) {
            V.unlocked = best;
            snprintf(V.toast, sizeof(V.toast), "NOW EAT %s!", TYPE_NAMES[best]);
            V.toast_t = 1.8f;
        }
    }
    (void)hole;
}

static void credit(Void *v, int hole, Prop *p) {
    const PropType *t = &TYPES[p->style];
    v->squash = 0.15f;
    if (p->style == ST_COIN || p->style == ST_GEM) {
        int n = p->style == ST_GEM ? 5 : 1;
        v->coins += n;
        if (v->player) {
            V.coins_round += n;
            add_pop(v, hole, n, 1);
            pa_sfx("coin");
        }
        return;
    }
    gain(v, hole, t->value, 1);

    /* Debris in the prop's own colour and a ring of dust off the lip. */
    float e = ext_of(p->style);
    int n = 8 + (int)pa_clampf(e / 10.0f, 0.0f, 4.0f);
    for (int k = 0; k < n; k++) {
        float a = pa_rng_range(&V.rng, 0.0f, PA_TAU);
        float sp = pa_rng_range(&V.rng, 30.0f, 70.0f) + e * 1.2f;
        if (k < n / 2)
            spawn_part(PK_DEBRIS, p->x, p->y, 4.0f, cosf(a) * sp, sinf(a) * sp,
                       pa_rng_range(&V.rng, 90.0f, 200.0f) + e * 2.0f, pa_rng_range(&V.rng, 0.5f, 0.8f),
                       pa_rng_range(&V.rng, 2.5f, 4.0f) + e * 0.08f, k & 1 ? p->col : pa_hex(0x8E95B8));
        else
            spawn_part(PK_DUST, v->x + cosf(a) * v->r, v->y + sinf(a) * v->r, 2.0f,
                       cosf(a) * sp * 0.4f, sinf(a) * sp * 0.4f, 20.0f, pa_rng_range(&V.rng, 0.45f, 0.7f),
                       4.0f + e * 0.25f, pa_hex(0xEEF0FA));
    }
    if (v->player) {
        int stars = t->value >= 30 ? 3 : t->value >= 8 ? 2 : 1;
        for (int k = 0; k < stars; k++) {
            float a = pa_rng_range(&V.rng, 0.0f, PA_TAU);
            spawn_part(PK_STAR, v->x, v->y, v->r * 0.2f, cosf(a) * v->r * 1.6f, sinf(a) * v->r * 1.6f,
                       220.0f + v->r * 4.0f, 0.7f, 0.0f, pa_hex(0xFFD23A));
            fly_star(v);
        }
        add_pop(v, hole, t->value, 0);
        if (V.sfx_gap <= 0.0f) {
            float f = 620.0f - pa_clampf(t->r * 9.0f, 0.0f, 440.0f);
            pa_tone(f, f * 0.45f, 0.10f, 0, 0.08f);
            if (t->r >= 30.0f) { pa_noise(0.28f, 0.16f); pa_tone(150.0f, 55.0f, 0.3f, 3, 0.10f); }
            V.sfx_gap = 0.05f;
        }
        if (t->r >= 30.0f) V.shake = pa_clampf(V.shake + 0.3f, 0.0f, 0.6f);
    }
}

/* ------------------------------------------------------------------ rivals */
static int can_eat(const Void *v, const Prop *p) {
    return p->state < PS_FALL && TYPES[p->style].r <= level_radius(v->level) * 0.95f;
}

static void steer_toward(Void *v, float tx, float ty, float speed, float dt) {
    float dx = tx - v->x, dy = ty - v->y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float slow = pa_clamp01(len / (v->r * 0.6f + 8.0f));
    float wx = dx / len * speed * slow, wy = dy / len * speed * slow;
    float k = 1.0f - expf(-9.0f * dt);
    v->vx += (wx - v->vx) * k;
    v->vy += (wy - v->vy) * k;
}

static float hole_speed(const Void *v) { return 70.0f + level_radius(v->level) * 1.6f; }

/** Picks a prop worth chasing. `ax, ay, range` bound the search, which is
    how a demo rival is kept working the blocks beside the player. */
static int pick_target(const Void *v, float ax, float ay, float range, int samples, int prefer_cars) {
    int best = -1;
    float best_score = -1.0f;
    int step = V.prop_count / (samples > 0 ? samples : 1);
    if (step < 1) step = 1;
    int off = (int)(v->think * 977.0f) % step;
    for (int i = off; i < V.prop_count; i += step) {
        const Prop *p = &V.props[i];
        if (!can_eat(v, p)) continue;
        float ex = p->x - ax, ey = p->y - ay;
        if (ex * ex + ey * ey > range * range) continue;
        float dx = v->x - p->x, dy = v->y - p->y;
        float d = sqrtf(dx * dx + dy * dy) + 30.0f;
        float val = (float)TYPES[p->style].value + (p->style == ST_COIN ? 2.0f : 0.0f);
        if (prefer_cars && p->style >= ST_CAR && p->style <= ST_TRUCK) val *= 3.0f;
        float s = val / d;
        if (s > best_score) { best_score = s; best = i; }
    }
    return best;
}

static void think_hole(Void *v, int index, float dt, int is_bot) {
    v->think -= dt;
    Void *me = &V.voids[0];
    if (v->think <= 0.0f || v->target < 0 || V.props[v->target].state >= PS_FALL ||
        (v->target >= 0 && !can_eat(v, &V.props[v->target]))) {
        v->think = is_bot ? 0.22f : pa_lerpf(0.6f, 0.22f, v->skill);
        v->chase = -1;

        /* Bigger hole close by: run. Smaller one close by: hunt it. */
        float flee_x = 0.0f, flee_y = 0.0f;
        for (int k = 0; k < V.void_count; k++) {
            const Void *o = &V.voids[k];
            if (k == index || !o->alive || o->dying > 0.0f) continue;
            float dx = v->x - o->x, dy = v->y - o->y;
            float d = sqrtf(dx * dx + dy * dy) + 1.0f;
            if (level_radius(o->level) >= level_radius(v->level) * 1.38f && d < o->r * 3.0f + 140.0f) {
                flee_x += dx / d; flee_y += dy / d;
            } else if (level_radius(v->level) >= level_radius(o->level) * 1.38f && d < 340.0f && (is_bot || v->skill > 0.35f) &&
                       !(o->player && o->grace > 0.0f) && o->grace <= 0.0f) {
                v->chase = k;
            }
        }
        if (flee_x != 0.0f || flee_y != 0.0f) {
            v->chase = -1;
            v->target = -2;
            v->hx = flee_x; v->hy = flee_y;
        } else if (v->chase < 0) {
            float ax = v->x, ay = v->y, range = 700.0f;
            if (pa_demo_mode() && index == 1 && me->alive) { ax = me->x; ay = me->y + 120.0f; range = 340.0f; }
            v->target = pick_target(v, ax, ay, range, is_bot ? 900 : (int)pa_lerpf(60.0f, 260.0f, v->skill),
                                    is_bot && v->level >= 4 && v->level <= 7);
            /* Local blocks picked clean: look across the whole district. */
            if (v->target < 0) v->target = pick_target(v, v->x, v->y, 1e6f, 400, 0);
        }
    }

    float speed = hole_speed(v) * (v->player ? 1.0f : pa_lerpf(0.84f, 0.98f, v->skill));
    if (v->chase >= 0) {
        const Void *o = &V.voids[v->chase];
        if (!o->alive) { v->chase = -1; return; }
        steer_toward(v, o->x, o->y, speed, dt);
    } else if (v->target == -2) {
        float l = sqrtf(v->hx * v->hx + v->hy * v->hy) + 0.001f;
        steer_toward(v, v->x + v->hx / l * 200.0f, v->y + v->hy / l * 200.0f, speed, dt);
    } else if (v->target >= 0) {
        const Prop *p = &V.props[v->target];
        steer_toward(v, p->x, p->y, speed, dt);
    } else {
        /* Nothing worth eating in range: cruise toward the middle. */
        steer_toward(v, 0.0f, 0.0f, speed * 0.6f, dt);
    }
}

/* ------------------------------------------------------------------ props */
static void update_walker(Prop *p, float dt) {
    /* Panic: anything that could eat this person and is close sends them
       running directly away with their arms up. */
    float fx = 0.0f, fy = 0.0f;
    for (int k = 0; k < V.void_count; k++) {
        const Void *v = &V.voids[k];
        if (!v->alive || !can_eat(v, p)) continue;
        float dx = p->x - v->x, dy = p->y - v->y;
        float d2 = dx * dx + dy * dy;
        float reach = v->r * 3.2f + 40.0f;
        if (d2 < reach * reach) {
            float d = sqrtf(d2) + 0.1f;
            fx += dx / d; fy += dy / d;
        }
    }
    if (fx != 0.0f || fy != 0.0f) {
        float l = sqrtf(fx * fx + fy * fy);
        p->panic = 1.4f;
        p->vx = fx / l * 62.0f;
        p->vy = fy / l * 62.0f;
    }
    if (p->panic > 0.0f) {
        p->panic -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->rot = atan2f(p->vy, p->vx);
        p->t += dt * 15.0f;
        if (p->panic <= 0.0f) {
            /* Calm again: walk a fresh line from here along the faster axis. */
            p->axis = fabsf(p->vx) > fabsf(p->vy) ? 0 : 1;
            float c = p->axis == 0 ? p->x : p->y;
            p->lo = c - 50.0f; p->hi = c + 50.0f;
            float sp = 20.0f * (p->axis == 0 ? (p->vx > 0 ? 1.0f : -1.0f) : (p->vy > 0 ? 1.0f : -1.0f));
            p->cvx = p->axis == 0 ? sp : 0.0f;
            p->cvy = p->axis == 0 ? 0.0f : sp;
        }
    } else {
        p->x += p->cvx * dt;
        p->y += p->cvy * dt;
        float c = p->axis == 0 ? p->x : p->y;
        if ((c > p->hi && (p->cvx + p->cvy) > 0.0f) || (c < p->lo && (p->cvx + p->cvy) < 0.0f)) {
            p->cvx = -p->cvx; p->cvy = -p->cvy;
        }
        p->rot = atan2f(p->cvy, p->cvx);
        p->t += dt * 8.0f;
    }
    p->x = pa_clampf(p->x, -V.half_x - ROAD * 0.4f, V.half_x + ROAD * 0.4f);
    p->y = pa_clampf(p->y, -V.half_y - ROAD * 0.4f, V.half_y + ROAD * 0.4f);
}

static void update_props(float dt) {
    float wrap_x = V.half_x + ROAD * 0.5f, wrap_y = V.half_y + ROAD * 0.5f;
    for (int i = 0; i < V.prop_count; i++) {
        Prop *p = &V.props[i];
        if (p->state == PS_GONE) continue;

        if (p->state == PS_FALL) {
            const Void *h = &V.voids[p->eater];
            p->t += dt;
            p->vz -= GRAVITY * dt;
            p->z += p->vz * dt;
            float k = 1.0f - expf(-5.0f * dt);
            p->x += (h->x - p->x) * k;
            p->y += (h->y - p->y) * k;
            p->tilt = pa_approach(p->tilt, 1.9f, 3.2f, dt);
            p->rot += p->spin * dt;
            float depth = TYPES[p->style].h + h->r * 2.6f + 40.0f;
            if (-p->z > depth || p->t > 2.4f) p->state = PS_GONE;
            continue;
        }
        if (p->style == ST_COIN || p->style == ST_GEM) p->t += dt;

        /* Lip test against every hole, nearest first claim. */
        float e = ext_of(p->style);
        int claim = -1;
        float claim_d = 1e9f;
        int wobble = -1;
        for (int k = 0; k < V.void_count; k++) {
            const Void *v = &V.voids[k];
            if (!v->alive || v->dying > 0.0f) continue;
            float dx = v->x - p->x, dy = v->y - p->y;
            float lip = v->r + e * 0.55f;
            float d2 = dx * dx + dy * dy;
            if (d2 > lip * lip) continue;
            float d = sqrtf(d2);
            if (can_eat(v, p)) { if (d < claim_d) { claim_d = d; claim = k; } }
            else if (d < v->r + e * 0.2f) wobble = k;
        }

        if (claim >= 0) {
            Void *v = &V.voids[claim];
            p->state = PS_TIP;
            p->eater = (unsigned char)claim;
            float dx = v->x - p->x, dy = v->y - p->y;
            float d = claim_d + 0.001f;
            p->tdir = atan2f(dy, dx);
            float lip = v->r + e * 0.55f;
            float want = 1.25f * pa_clamp01((lip - d) / (e * 1.1f + v->r * 0.25f));
            p->tilt = pa_approach(p->tilt, want, 16.0f, dt);
            float slide = (50.0f + 330.0f * sinf(p->tilt)) * dt;
            p->x += dx / d * slide;
            p->y += dy / d * slide;
            float fall_at = v->r - e * 0.45f;
            if (fall_at < v->r * 0.5f) fall_at = v->r * 0.5f;
            if (d < fall_at || TYPES[p->style].r == 0.0f) {
                p->state = PS_FALL;
                p->vz = 0.0f;
                p->t = 0.0f;
                p->spin = ((p->seed & 1) ? 1.0f : -1.0f) * (1.0f + (float)(p->seed % 7) * 0.3f);
                credit(v, claim, p);
            }
            continue;
        }
        if (p->state == PS_TIP) {
            /* The hole moved off: settle back onto the road. */
            p->tilt = pa_approach(p->tilt, 0.0f, 10.0f, dt);
            if (p->tilt < 0.01f) { p->tilt = 0.0f; p->state = PS_STAND; }
            continue;
        }
        if (wobble >= 0) {
            /* Too big to fit: it rocks on the lip instead. */
            const Void *v = &V.voids[wobble];
            p->tdir = atan2f(v->y - p->y, v->x - p->x);
            p->tilt = 0.05f + 0.04f * sinf(V.time * 17.0f + (float)(p->seed & 15));
        } else if (p->tilt != 0.0f) {
            p->tilt = 0.0f;
        }

        if (p->style == ST_PERSON) { update_walker(p, dt); continue; }
        if (p->cvx != 0.0f || p->cvy != 0.0f) {
            p->x += p->cvx * dt;
            p->y += p->cvy * dt;
            if (p->x > wrap_x) p->x -= wrap_x * 2.0f;
            if (p->x < -wrap_x) p->x += wrap_x * 2.0f;
            if (p->y > wrap_y) p->y -= wrap_y * 2.0f;
            if (p->y < -wrap_y) p->y += wrap_y * 2.0f;
        }
    }
}

/* ------------------------------------------------------------- hole vs hole */
static void respawn(Void *v, int index) {
    reset_void(v);
    v->grace = 3.0f;
    /* Somewhere away from every other hole. */
    float bx = 0.0f, by = 0.0f, best = -1.0f;
    for (int k = 0; k < 12; k++) {
        float x = pa_rng_range(&V.rng, -V.half_x * 0.8f, V.half_x * 0.8f);
        float y = pa_rng_range(&V.rng, -V.half_y * 0.8f, V.half_y * 0.8f);
        float m = 1e9f;
        for (int j = 0; j < V.void_count; j++) {
            if (j == index || !V.voids[j].alive) continue;
            float dx = x - V.voids[j].x, dy = y - V.voids[j].y;
            float d = dx * dx + dy * dy;
            if (d < m) m = d;
        }
        if (m > best) { best = m; bx = x; by = y; }
    }
    v->x = bx; v->y = by;
}

static void devour(Void *a, int ai, Void *b, int bi) {
    b->dying = 0.45f;
    b->killer = ai;
    b->dx0 = b->x; b->dy0 = b->y;
    a->kills++;
    a->squash = 0.15f;
    gain(a, ai, 10 + b->level * 6, 0);
    for (int k = 0; k < 12; k++) {
        float ang = PA_TAU * (float)k / 12.0f;
        spawn_part(PK_SPARK, b->x, b->y, 6.0f, cosf(ang) * b->r * 3.0f, sinf(ang) * b->r * 3.0f, 80.0f,
                   0.6f, b->r * 0.2f, pa_hex(SKINS[b->skin].top));
    }
    if (a->player) {
        add_pop(a, ai, 1, 2);
        a->streak = a->streak_t > 0.0f ? a->streak + 1 : 1;
        a->streak_t = 7.0f;
        V.kill_bump = 1.0f;
        static const char *calls[] = { "KILL!", "DOUBLE KILL!", "TRIPLE KILL!", "RAMPAGE!" };
        announce(calls[a->streak > 4 ? 3 : a->streak - 1], pa_hex(0xFFFFFF));
        pa_sfx("boom");
        pa_tone(300.0f, 900.0f, 0.25f, 2, 0.07f);
        V.shake = 0.5f;
    } else if (b->player) {
        pa_sfx("lose");
        V.shake = 0.6f;
    }
    (void)bi;
}

static void holes_vs_holes(float dt) {
    for (int i = 0; i < V.void_count; i++) {
        Void *a = &V.voids[i];
        if (!a->alive || a->dying > 0.0f) continue;
        for (int j = 0; j < V.void_count; j++) {
            if (i == j) continue;
            Void *b = &V.voids[j];
            if (!b->alive || b->dying > 0.0f || b->grace > 0.0f) continue;
            if (level_radius(a->level) < level_radius(b->level) * 1.38f) continue;
            float dx = a->x - b->x, dy = a->y - b->y;
            float bite = a->r - b->r * 0.4f;
            if (dx * dx + dy * dy > bite * bite) continue;
            devour(a, i, b, j);
        }
    }
    for (int i = 0; i < V.void_count; i++) {
        Void *v = &V.voids[i];
        if (v->dying > 0.0f) {
            v->dying -= dt;
            const Void *k = &V.voids[v->killer];
            float t = 1.0f - pa_clamp01(v->dying / 0.45f);
            v->x = pa_lerpf(v->dx0, k->x, t);
            v->y = pa_lerpf(v->dy0, k->y, t);
            if (v->dying <= 0.0f) {
                v->dying = 0.0f;
                v->alive = 0;
                v->respawn = 3.0f;
            }
        } else if (!v->alive) {
            v->respawn -= dt;
            if (v->respawn <= 0.0f) respawn(v, i);
        }
    }
}

/* ----------------------------------------------------------------- update */
static void screen_dir_to_world(float sx, float sy, float *wx, float *wy) {
    float vx = sx, vy = sy / GROUND_K;
    *wx = vx * g_cr + vy * g_sr;
    *wy = -vx * g_sr + vy * g_cr;
}

static void standings(void) {
    for (int i = 0; i < V.void_count; i++) V.order[i] = i;
    for (int i = 0; i < V.void_count; i++)
        for (int j = i + 1; j < V.void_count; j++)
            if (V.voids[V.order[j]].score > V.voids[V.order[i]].score) {
                int t = V.order[i]; V.order[i] = V.order[j]; V.order[j] = t;
            }
    for (int i = 0; i < V.void_count; i++) if (V.order[i] == 0) V.place = i + 1;
}

static void finish_round(void) {
    standings();
    const Void *me = &V.voids[0];
    int pct = (int)(1000.0f * (float)me->score / (float)V.total_value + 0.5f);
    V.new_best = pct > g_best_pct;
    if (!pa_demo_mode()) {
        if (V.new_best) { g_best_pct = pct; pa_save_set("voidmuncher.bestpct", g_best_pct); }
        g_coins += V.coins_round;
        pa_save_set("voidmuncher.coins", g_coins);
        if (V.place == 1) pa_save_set("voidmuncher.level", V.level + 1);
        pa_save_flush();
    } else if (V.new_best) {
        g_best_pct = pct;
    }
    pa_sfx(V.place == 1 ? "win" : "lose");
}

static void player_input(Void *me, const PA_Input *in, float dt) {
    float speed = hole_speed(me);
    /* A floating stick: the drag direction from where the finger went down,
       the anchor trailing the finger so reversing is instant. */
    if (in->pressed) { V.joy_x = in->x; V.joy_y = in->y; V.joy_on = 1; }
    if (!in->down) V.joy_on = 0;
    float wx = 0.0f, wy = 0.0f, mag = 0.0f;
    if (V.joy_on) {
        float dx = in->x - V.joy_x, dy = in->y - V.joy_y;
        float len = sqrtf(dx * dx + dy * dy);
        float reach = L.unit * 0.12f;
        if (len > reach) {
            V.joy_x = in->x - dx / len * reach;
            V.joy_y = in->y - dy / len * reach;
            len = reach;
        }
        if (len > 3.0f) {
            screen_dir_to_world(dx, dy, &wx, &wy);
            mag = pa_clamp01(len / (reach * 0.55f));
        }
    } else {
        float kx = 0.0f, ky = 0.0f;
        if (in->keys[PA_KEY_LEFT])  kx -= 1.0f;
        if (in->keys[PA_KEY_RIGHT]) kx += 1.0f;
        if (in->keys[PA_KEY_UP])    ky -= 1.0f;
        if (in->keys[PA_KEY_DOWN])  ky += 1.0f;
        if (kx != 0.0f || ky != 0.0f) { screen_dir_to_world(kx, ky * GROUND_K, &wx, &wy); mag = 1.0f; }
    }
    float wl = sqrtf(wx * wx + wy * wy);
    float tvx = 0.0f, tvy = 0.0f;
    if (wl > 0.0001f) { tvx = wx / wl * speed * mag; tvy = wy / wl * speed * mag; }
    float k = 1.0f - expf(-12.0f * dt);
    me->vx += (tvx - me->vx) * k;
    me->vy += (tvy - me->vy) * k;
}

static void update_parts(float dt) {
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *q = &V.parts[i];
        if (q->life <= 0.0f) continue;
        q->life -= dt;
        q->x += q->vx * dt;
        q->y += q->vy * dt;
        q->z += q->vz * dt;
        q->rot += dt * 7.0f;
        if (q->kind == PK_DEBRIS || q->kind == PK_STAR) {
            q->vz -= GRAVITY * 0.9f * dt;
            if (q->z < 0.0f && q->kind == PK_DEBRIS) { q->z = 0.0f; q->vz *= -0.3f; q->vx *= 0.6f; q->vy *= 0.6f; }
        } else {
            q->vx *= expf(-3.0f * dt);
            q->vy *= expf(-3.0f * dt);
        }
    }
    for (int i = 0; i < MAX_POPS; i++) if (V.pops[i].t > 0.0f) {
        V.pops[i].t -= dt * 0.9f;
        float rr = V.voids[V.pops[i].hole].r;
        V.pops[i].z += dt * (20.0f + rr * 0.5f);
        if (V.pops[i].z > rr * 0.6f + 8.0f) V.pops[i].z = rr * 0.6f + 8.0f;
    }
    for (int i = 0; i < MAX_FLY; i++) {
        Fly *f = &V.fly[i];
        if (!f->live) continue;
        f->t += dt;
        if (f->t >= f->dur) { f->live = 0; V.bar_pulse = 1.0f; }
    }
}

static void update_camera(float dt) {
    const Void *me = &V.voids[0];
    float target = target_scale(level_radius(me->level));
    if (V.phase == PH_MENU) target *= 0.82f;
    V.cam_x = pa_approach(V.cam_x, me->x, 6.0f, dt);
    V.cam_y = pa_approach(V.cam_y, me->y, 6.0f, dt);
    V.cam_scale = pa_approach(V.cam_scale, target, 2.2f, dt);
    /* Keep the frame mostly city: the camera stops short of the sea wall
       while the hole itself can still reach the promenade. */
    float sc = V.cam_scale > 0.1f ? V.cam_scale : 0.1f;
    float keep = (float)L.w * 0.42f / sc;
    float keep_deep = (float)L.h * 0.30f / (sc * GROUND_K);
    if (keep_deep > keep) keep = keep_deep;
    float lim_x = V.half_x - keep > 0.0f ? V.half_x - keep : 0.0f;
    float lim_y = V.half_y - keep > 0.0f ? V.half_y - keep : 0.0f;
    V.cam_x = pa_clampf(V.cam_x, -lim_x, lim_x);
    V.cam_y = pa_clampf(V.cam_y, -lim_y, lim_y);
    /* ...but never so far that the hole leaves the middle of the frame. */
    float dx = me->x - V.cam_x, dy = me->y - V.cam_y;
    float vx = dx * g_cr - dy * g_sr, vy = dx * g_sr + dy * g_cr;
    float mx = (float)L.w * 0.26f / sc, my = (float)L.h * 0.16f / (sc * GROUND_K);
    float cvx = vx > mx ? vx - mx : vx < -mx ? vx + mx : 0.0f;
    float cvy = vy > my ? vy - my : vy < -my ? vy + my : 0.0f;
    V.cam_x += cvx * g_cr + cvy * g_sr;
    V.cam_y += -cvx * g_sr + cvy * g_cr;
}

static int results_button_hit(float x, float y);
static float share(const Void *v);

static void muncher_update(float dt, const PA_Input *in) {
    if (L.w == 0) layout(540, 1170);
    setup_view();
    V.time += dt;
    V.phase_t += dt;
    if (V.banner_t > 0.0f) V.banner_t -= dt;
    if (V.toast_t > 0.0f) V.toast_t -= dt;
    if (V.sfx_gap > 0.0f) V.sfx_gap -= dt;
    V.shake = V.shake > 0.0f ? V.shake - dt * 1.6f : 0.0f;
    V.bar_pulse = V.bar_pulse > 0.0f ? V.bar_pulse - dt * 4.0f : 0.0f;
    V.kill_bump = V.kill_bump > 0.0f ? V.kill_bump - dt * 3.0f : 0.0f;

    Void *me = &V.voids[0];

    if (V.phase == PH_MENU) {
        update_props(dt);
        update_parts(dt);
        update_camera(dt);
        if (in->pressed || (pa_demo_mode() && V.phase_t > 1.4f)) {
            V.phase = PH_PLAY;
            V.phase_t = 0.0f;
            pa_sfx("select");
        }
        if (V.phase != PH_PLAY) return;
    }

    if (V.phase == PH_RESULTS) {
        update_parts(dt);
        if (V.phase_t > 0.9f && in->tapped && results_button_hit(in->x, in->y)) {
            pa_sfx("select");
            start_round();
            V.phase = PH_PLAY;
        }
        return;
    }
    if (V.phase == PH_TIMEUP) {
        update_props(dt);
        update_parts(dt);
        for (int i = 0; i < V.void_count; i++) { V.voids[i].vx *= 0.9f; V.voids[i].vy *= 0.9f; }
        if (V.phase_t > 1.6f) { V.phase = PH_RESULTS; V.phase_t = 0.0f; }
        return;
    }

    /* PH_PLAY */
    float prev = V.timer;
    V.timer -= dt;
    if (V.timer <= 10.0f && floorf(prev) != floorf(V.timer) && V.timer > 0.0f)
        pa_tone(1250.0f, 1250.0f, 0.05f, 2, 0.05f);
    if (V.timer <= 0.0f) {
        V.timer = 0.0f;
        V.phase = PH_TIMEUP;
        V.phase_t = 0.0f;
        announce("TIME'S UP!", pa_hex(0xFFD23A));
        finish_round();
        return;
    }

    for (int i = 0; i < V.void_count; i++) {
        Void *v = &V.voids[i];
        if (v->grace > 0.0f) v->grace -= dt;
        if (v->squash > 0.0f) v->squash -= dt;
        if (v->pop > 0.0f) v->pop -= dt * 2.5f;
        if (v->streak_t > 0.0f) v->streak_t -= dt;
        /* Radius spring toward the size this level allows: the growth pop. */
        float target = level_radius(v->level);
        v->rv += ((target - v->r) * 140.0f - v->rv * 13.0f) * dt;
        v->r += v->rv * dt;
        if (v->r < 4.0f) v->r = 4.0f;
        if (!v->alive || v->dying > 0.0f) continue;
        if (i == 0) {
            if (pa_demo_mode()) think_hole(v, 0, dt, 1);
            else player_input(v, in, dt);
        } else {
            think_hole(v, i, dt, 0);
        }
    }

    float bx = V.half_x + ROAD * 0.45f, by = V.half_y + ROAD * 0.45f;
    for (int i = 0; i < V.void_count; i++) {
        Void *v = &V.voids[i];
        if (!v->alive || v->dying > 0.0f) continue;
        v->x = pa_clampf(v->x + v->vx * dt, -bx, bx);
        v->y = pa_clampf(v->y + v->vy * dt, -by, by);
    }

    update_props(dt);
    holes_vs_holes(dt);
    update_parts(dt);
    if (me->alive || me->dying > 0.0f) update_camera(dt);
}

/* ======================================================================= */
/* ------------------------------------------------------------- drawing -- */
/* ======================================================================= */

/* The scanline filler walks its whole clip width on every row it touches, so
   a 6px window on a 540px canvas costs as much as a 540px band. Narrowing the
   horizontal clip to each shape's own extent first is the single biggest win
   for a scene of many small polygons. */
static int clip_x(PA_Canvas *c, float x0, float x1, int *s0, int *s1) {
    *s0 = c->clip_x0; *s1 = c->clip_x1;
    int a = (int)floorf(x0) - 1, b = (int)ceilf(x1) + 2;
    if (a < c->clip_x0) a = c->clip_x0;
    if (b > c->clip_x1) b = c->clip_x1;
    if (b <= a) return 0;
    c->clip_x0 = a; c->clip_x1 = b;
    return 1;
}
#define CLIP_END(c) ((c)->clip_x0 = s0, (c)->clip_x1 = s1)

static void bounds(const PA_Vec2 *p, int n, float *x0, float *x1, float *y0, float *y1) {
    *x0 = *x1 = p[0].x; *y0 = *y1 = p[0].y;
    for (int i = 1; i < n; i++) {
        if (p[i].x < *x0) *x0 = p[i].x;
        if (p[i].x > *x1) *x1 = p[i].x;
        if (p[i].y < *y0) *y0 = p[i].y;
        if (p[i].y > *y1) *y1 = p[i].y;
    }
}

static void fpoly(PA_Canvas *c, const PA_Vec2 *p, int n, PA_Color col) {
    if (n < 3 || PA_A(col) == 0) return;
    float x0, x1, y0, y1;
    bounds(p, n, &x0, &x1, &y0, &y1);
    if (y1 < (float)c->clip_y0 || y0 > (float)c->clip_y1 || x1 - x0 < 0.05f || y1 - y0 < 0.05f) return;
    int s0, s1;
    if (!clip_x(c, x0, x1, &s0, &s1)) return;
    pa_fill_poly(c, p, n, col);
    CLIP_END(c);
}

static void fpoly_paint(PA_Canvas *c, const PA_Vec2 *p, int n, const PA_Paint *pt) {
    if (n < 3) return;
    float x0, x1, y0, y1;
    bounds(p, n, &x0, &x1, &y0, &y1);
    if (y1 < (float)c->clip_y0 || y0 > (float)c->clip_y1) return;
    int s0, s1;
    if (!clip_x(c, x0, x1, &s0, &s1)) return;
    pa_fill_poly_paint(c, p, n, pt);
    CLIP_END(c);
}

static void fellipse(PA_Canvas *c, float cx, float cy, float rx, float ry, PA_Color col) {
    if (rx < 0.3f || ry < 0.3f || cy + ry < (float)c->clip_y0 || cy - ry > (float)c->clip_y1) return;
    int s0, s1;
    if (!clip_x(c, cx - rx, cx + rx, &s0, &s1)) return;
    pa_fill_ellipse(c, cx, cy, rx, ry, col);
    CLIP_END(c);
}

static void fellipse_paint(PA_Canvas *c, float cx, float cy, float rx, float ry, const PA_Paint *pt) {
    if (rx < 0.3f || ry < 0.3f || cy + ry < (float)c->clip_y0 || cy - ry > (float)c->clip_y1) return;
    int s0, s1;
    if (!clip_x(c, cx - rx, cx + rx, &s0, &s1)) return;
    pa_fill_ellipse_paint(c, cx, cy, rx, ry, pt);
    CLIP_END(c);
}

static void fcircle(PA_Canvas *c, float cx, float cy, float r, PA_Color col) { fellipse(c, cx, cy, r, r, col); }

static void rrect(PA_Canvas *c, float x, float y, float w, float h, float r, PA_Color col) {
    int s0, s1;
    if (!clip_x(c, x, x + w, &s0, &s1)) return;
    pa_round_rect(c, x, y, w, h, r, col);
    CLIP_END(c);
}

static void rrect_paint(PA_Canvas *c, float x, float y, float w, float h, float r, const PA_Paint *pt) {
    int s0, s1;
    if (!clip_x(c, x, x + w, &s0, &s1)) return;
    pa_round_rect_paint(c, x, y, w, h, r, pt);
    CLIP_END(c);
}

static void soft_shadow(PA_Canvas *c, float cx, float cy, float rx, float ry, float k) {
    if (rx < 1.0f) return;
    int s0, s1;
    if (!clip_x(c, cx - rx * 1.2f, cx + rx * 1.2f, &s0, &s1)) return;
    pa_shadow(c, cx, cy, rx, ry, k);
    CLIP_END(c);
}

static float text_x0(const char *s, float x, float size, PA_Align al, float tr, float *w) {
    *w = pa_text_width(s, size, tr);
    return al == PA_ALIGN_CENTER ? x - *w * 0.5f : al == PA_ALIGN_RIGHT ? x - *w : x;
}

/* ------------------------------------------------------------ text cache --
   The stroke font draws every segment and every joint as its own filled
   shape, three times over for an outlined label - a five-letter name costs
   more than the building under it. Labels barely change frame to frame, so
   each string is drawn once through pa_text / pa_text_bold into a scratch
   canvas, on black and again on white; the difference between the two gives
   exact coverage, and the black pass is already the premultiplied colour.
   Every later frame is a straight alpha blit of a few thousand pixels. */
#define TC_N 72
typedef struct {
    char key[64];
    int w, h, used;
    float mx, my;          /* where the text origin sits inside the image */
    uint32_t *px;          /* premultiplied RGB in the low bytes, coverage on top */
    unsigned last;
} TextImg;
static TextImg   g_tc[TC_N];
static unsigned  g_tc_clock;
static PA_Canvas g_tc_canvas;

static const TextImg *text_image(const char *s, float size, PA_Color fill, PA_Color out, float tr,
                                 float weight, int bold) {
    char key[64];
    snprintf(key, sizeof(key), "%.30s|%d|%x|%x|%d|%d|%d", s, (int)(size * 4.0f), (unsigned)fill,
             (unsigned)out, (int)(tr * 4.0f), (int)(weight * 8.0f), bold);
    g_tc_clock++;
    int slot = -1;
    unsigned oldest = 0xFFFFFFFFu;
    for (int i = 0; i < TC_N; i++) {
        if (g_tc[i].used && !strcmp(g_tc[i].key, key)) { g_tc[i].last = g_tc_clock; return &g_tc[i]; }
        unsigned age = g_tc[i].used ? g_tc[i].last : 0;
        if (age < oldest) { oldest = age; slot = i; }
    }
    TextImg *t = &g_tc[slot];
    float w = pa_text_width(s, size, tr);
    float m = size * (bold ? 0.12f * (weight + 1.6f) : 0.12f) + 3.0f;
    int iw = (int)(w + m * 2.0f + 4.0f), ih = (int)(size * 1.12f + m * 2.0f + 4.0f);
    if (iw < 2 || ih < 2 || iw > 1600 || ih > 400) return NULL;
    if (!pa_canvas_resize(&g_tc_canvas, iw, ih)) return NULL;
    uint32_t *buf = (uint32_t *)realloc(t->px, (size_t)iw * (size_t)ih * sizeof(uint32_t));
    if (!buf) return NULL;
    t->px = buf;
    t->w = iw; t->h = ih; t->mx = m + 2.0f; t->my = m + 2.0f;
    for (int pass = 0; pass < 2; pass++) {
        pa_clip_reset(&g_tc_canvas);
        pa_clear(&g_tc_canvas, pass ? PA_RGB(255, 255, 255) : PA_RGB(0, 0, 0));
        if (bold) pa_text_bold(&g_tc_canvas, s, t->mx, t->my, size, fill, out, PA_ALIGN_LEFT, tr, weight);
        else pa_text(&g_tc_canvas, s, t->mx, t->my, size, fill, PA_ALIGN_LEFT, tr);
        const uint32_t *src = g_tc_canvas.px;
        int n = iw * ih;
        if (!pass) { for (int i = 0; i < n; i++) buf[i] = src[i] & 0x00FFFFFFu; continue; }
        for (int i = 0; i < n; i++) {
            int bw = (int)((src[i] >> 8) & 0xFF) - (int)((buf[i] >> 8) & 0xFF);
            int a = 255 - bw;
            if (a < 0) a = 0;
            if (a > 255) a = 255;
            buf[i] = (buf[i] & 0x00FFFFFFu) | ((uint32_t)a << 24);
        }
    }
    snprintf(t->key, sizeof(t->key), "%s", key);
    t->used = 1;
    t->last = g_tc_clock;
    return t;
}

static void text_blit(PA_Canvas *c, const TextImg *t, float x, float y) {
    int dx = (int)floorf(x - t->mx + 0.5f), dy = (int)floorf(y - t->my + 0.5f);
    int x0 = dx < c->clip_x0 ? c->clip_x0 : dx, x1 = dx + t->w > c->clip_x1 ? c->clip_x1 : dx + t->w;
    int y0 = dy < c->clip_y0 ? c->clip_y0 : dy, y1 = dy + t->h > c->clip_y1 ? c->clip_y1 : dy + t->h;
    for (int yy = y0; yy < y1; yy++) {
        const uint32_t *s = t->px + (size_t)(yy - dy) * (size_t)t->w + (x0 - dx);
        uint32_t *d = c->px + (size_t)yy * (size_t)c->w + x0;
        for (int xx = x0; xx < x1; xx++, s++, d++) {
            uint32_t a = *s >> 24;
            if (!a) continue;
            if (a == 255) { *d = *s & 0x00FFFFFFu; continue; }
            uint32_t ia = 255 - a, v = *d;
            uint32_t r = ((*s >> 16) & 0xFF) + (((v >> 16) & 0xFF) * ia + 127) / 255;
            uint32_t g = ((*s >> 8) & 0xFF) + (((v >> 8) & 0xFF) * ia + 127) / 255;
            uint32_t b = (*s & 0xFF) + ((v & 0xFF) * ia + 127) / 255;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            *d = (r << 16) | (g << 8) | b;
        }
    }
}

static void txt(PA_Canvas *c, const char *s, float x, float y, float size, PA_Color col, PA_Align al, float tr) {
    size = floorf(size + 0.5f);
    float w, x0 = text_x0(s, x, size, al, tr, &w);
    const TextImg *t = text_image(s, size, col, 0, tr, 1.0f, 0);
    if (t) { text_blit(c, t, x0, y); return; }
    int s0, s1;
    if (!clip_x(c, x0 - size * 0.6f, x0 + w + size * 0.6f, &s0, &s1)) return;
    pa_text(c, s, x, y, size, col, al, tr);
    CLIP_END(c);
}

static void txtb(PA_Canvas *c, const char *s, float x, float y, float size, PA_Color fill, PA_Color out,
                 PA_Align al, float tr, float weight) {
    size = floorf(size + 0.5f);
    float w, x0 = text_x0(s, x, size, al, tr, &w);
    const TextImg *t = text_image(s, size, fill, out, tr, weight, 1);
    if (t) { text_blit(c, t, x0, y); return; }
    int s0, s1;
    if (!clip_x(c, x0 - size * 0.7f, x0 + w + size * 0.7f, &s0, &s1)) return;
    pa_text_bold(c, s, x, y, size, fill, out, al, tr, weight);
    CLIP_END(c);
}
/* ------------------------------------------------------- polygon clipping */
/** One Sutherland-Hodgman pass: against the plane z = 0 (by_z, keeping the
    half above or below) or against the screen edge a->b. */
static int clip_half(const PA_Vec2 *in, const float *zin, int n, PA_Vec2 *out, float *zout,
                     float ax, float ay, float bx, float by, float sign, int by_z, int keep_above) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float di, dj;
        if (by_z) { di = keep_above ? zin[i] : -zin[i]; dj = keep_above ? zin[j] : -zin[j]; }
        else {
            di = sign * ((bx - ax) * (in[i].y - ay) - (by - ay) * (in[i].x - ax));
            dj = sign * ((bx - ax) * (in[j].y - ay) - (by - ay) * (in[j].x - ax));
        }
        if (di >= 0.0f) { out[m] = in[i]; if (zout) zout[m] = zin ? zin[i] : 0.0f; m++; }
        if ((di >= 0.0f) != (dj >= 0.0f)) {
            float t = di / (di - dj);
            out[m].x = in[i].x + (in[j].x - in[i].x) * t;
            out[m].y = in[i].y + (in[j].y - in[i].y) * t;
            if (zout) zout[m] = zin ? zin[i] + (zin[j] - zin[i]) * t : 0.0f;
            m++;
        }
        if (m >= 62) break;
    }
    return m;
}

#define OPEN_N 28
static PA_Vec2 g_open[MAX_VOIDS][OPEN_N];

static int clip_convex(const PA_Vec2 *in, int n, const PA_Vec2 *o, int on, PA_Vec2 *out) {
    PA_Vec2 a[64], b[64];
    int m = n < 60 ? n : 60;
    memcpy(a, in, sizeof(PA_Vec2) * (size_t)m);
    /* The opening is built clockwise on screen (y down): inside is where the
       cross product is positive. */
    for (int e = 0; e < on && m >= 3; e++) {
        const PA_Vec2 *p0 = &o[e], *p1 = &o[(e + 1) % on];
        m = clip_half(a, NULL, m, b, NULL, p0->x, p0->y, p1->x, p1->y, 1.0f, 0, 0);
        memcpy(a, b, sizeof(PA_Vec2) * (size_t)m);
    }
    memcpy(out, a, sizeof(PA_Vec2) * (size_t)m);
    return m;
}

static void clip_to_opening(PA_Canvas *c, const PA_Vec2 *in, int n, int hole, PA_Color col) {
    PA_Vec2 o[64];
    int m = clip_convex(in, n, g_open[hole], OPEN_N, o);
    if (m >= 3) fpoly(c, o, m, col);
}

/* --------------------------------------------------------- the transform */
typedef struct { float x, y, z; } V3;
static struct {
    V3 o, ux, uy, uz;             /* origin, unit local axes (world) */
    float k;                      /* local scale */
    PA_Vec2 so, sx, sy, sz;       /* the same on screen, scaled */
    float zx, zy, zz;             /* world z per local unit */
    float px;                     /* screen px per local unit */
    int clip, hole;
} X;

static PA_Vec2 plin(float x, float y, float z) {
    float vx = x * g_cr - y * g_sr, vy = x * g_sr + y * g_cr;
    PA_Vec2 o = { vx * V.cam_scale, vy * V.cam_scale * GROUND_K - z * V.cam_scale * HEIGHT_K };
    return o;
}

static V3 rodrigues(V3 v, V3 a, float c, float s) {
    float d = a.x * v.x + a.y * v.y + a.z * v.z;
    V3 x = { a.y * v.z - a.z * v.y, a.z * v.x - a.x * v.z, a.x * v.y - a.y * v.x };
    V3 o = { v.x * c + x.x * s + a.x * d * (1.0f - c),
             v.y * c + x.y * s + a.y * d * (1.0f - c),
             v.z * c + x.z * s + a.z * d * (1.0f - c) };
    return o;
}

static void xf_set(float x, float y, float z, float rot, float tilt, float tdir, float k) {
    float cr = cosf(rot), sr = sinf(rot);
    V3 ex = { cr, sr, 0.0f }, ey = { -sr, cr, 0.0f }, ez = { 0.0f, 0.0f, 1.0f };
    if (tilt != 0.0f) {
        V3 a = { -sinf(tdir), cosf(tdir), 0.0f };
        float c = cosf(tilt), s = sinf(tilt);
        ex = rodrigues(ex, a, c, s);
        ey = rodrigues(ey, a, c, s);
        ez = rodrigues(ez, a, c, s);
    }
    X.o.x = x; X.o.y = y; X.o.z = z;
    X.ux = ex; X.uy = ey; X.uz = ez;
    X.k = k;
    X.so = P3(x, y, z);
    X.sx = plin(ex.x * k, ex.y * k, ex.z * k);
    X.sy = plin(ey.x * k, ey.y * k, ey.z * k);
    X.sz = plin(ez.x * k, ez.y * k, ez.z * k);
    X.zx = ex.z * k; X.zy = ey.z * k; X.zz = ez.z * k;
    X.px = k * V.cam_scale;
}

static inline PA_Vec2 xp(float lx, float ly, float lz) {
    PA_Vec2 o = { X.so.x + X.sx.x * lx + X.sy.x * ly + X.sz.x * lz,
                  X.so.y + X.sx.y * lx + X.sy.y * ly + X.sz.y * lz };
    return o;
}
static inline float xz(float lx, float ly, float lz) { return X.o.z + X.zx * lx + X.zy * ly + X.zz * lz; }

/** A polygon in the current local frame: split at ground level when it may be
    going into a hole, the underground part seen only through the opening. */
static void emit(PA_Canvas *c, const PA_Vec2 *s, const float *z, int n, PA_Color col) {
    if (!X.clip) { fpoly(c, s, n, col); return; }
    float zmin = z[0], zmax = z[0];
    for (int i = 1; i < n; i++) { if (z[i] < zmin) zmin = z[i]; if (z[i] > zmax) zmax = z[i]; }
    if (zmin >= 0.0f) { fpoly(c, s, n, col); return; }
    PA_Vec2 a[64]; float az[64];
    if (zmax > 0.0f) {
        int m = clip_half(s, z, n, a, az, 0, 0, 0, 0, 1.0f, 1, 1);
        if (m >= 3) fpoly(c, a, m, col);
        m = clip_half(s, z, n, a, az, 0, 0, 0, 0, 1.0f, 1, 0);
        if (m >= 3) clip_to_opening(c, a, m, X.hole, col);
    } else {
        clip_to_opening(c, s, n, X.hole, col);
    }
}

static PA_Color COOL, WARM;

/** Face colour from its world normal: warm key light on what faces it, a cool
    blue-violet tint (not black) on what turns away. */
static PA_Color lit(PA_Color base, float d) {
    PA_Color c = pa_shade(base, (d - 0.60f) * 0.34f);
    if (d < 0.35f) c = pa_mix(c, COOL, 0.38f * pa_clamp01((0.35f - d) / 0.65f));
    if (d > 0.62f) c = pa_mix(c, WARM, 0.30f * pa_clamp01((d - 0.62f) / 0.25f));
    return c;
}

/* Lit colour of a local normal, or 0 (transparent) when it faces away. */
static PA_Color face_col(PA_Color base, float nx, float ny, float nz) {
    float wx = X.ux.x * nx + X.uy.x * ny + X.uz.x * nz;
    float wy = X.ux.y * nx + X.uy.y * ny + X.uz.y * nz;
    float wz = X.ux.z * nx + X.uy.z * ny + X.uz.z * nz;
    if (wx * g_DX + wy * g_DY + wz * g_DZ <= 0.002f) return 0;
    PA_Color c = lit(base, wx * g_LX + wy * g_LY + wz * g_LZ);
    return c | 0xFF000000u;
}

/** Polygon in local coordinates, lit by a supplied local normal. */
static void lpoly(PA_Canvas *c, const float (*q)[3], int n, float nx, float ny, float nz, PA_Color base) {
    PA_Color col = face_col(base, nx, ny, nz);
    if (!col) return;
    PA_Vec2 s[12]; float z[12];
    if (n > 12) n = 12;
    for (int i = 0; i < n; i++) { s[i] = xp(q[i][0], q[i][1], q[i][2]); z[i] = xz(q[i][0], q[i][1], q[i][2]); }
    emit(c, s, z, n, col);
}

/* Visible side faces of the last box, for windows and trim. */
typedef struct { PA_Vec2 o, u, v; float zo, zu, zv, len, d; int vis; } Face;
static Face g_face[4];

/** Axis-aligned local box; lit faces, back faces culled. Records its side
    faces in g_face when `keep` is set. */
static void box(PA_Canvas *c, float x0, float y0, float z0, float x1, float y1, float z1,
                PA_Color side, PA_Color top, int keep) {
    PA_Vec2 s[8]; float z[8];
    for (int i = 0; i < 8; i++) {
        float lx = (i & 1) ? x1 : x0, ly = (i & 2) ? y1 : y0, lz = (i & 4) ? z1 : z0;
        s[i] = xp(lx, ly, lz);
        z[i] = xz(lx, ly, lz);
    }
    static const int F[6][4] = { { 4, 5, 7, 6 }, { 0, 1, 3, 2 }, { 1, 3, 7, 5 }, { 0, 2, 6, 4 },
                                 { 2, 3, 7, 6 }, { 0, 1, 5, 4 } };
    static const float N[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    for (int f = 0; f < 6; f++) {
        PA_Color col = face_col(f == 0 ? top : side, N[f][0], N[f][1], N[f][2]);
        if (!col) continue;
        PA_Vec2 q[4]; float qz[4];
        for (int k = 0; k < 4; k++) { q[k] = s[F[f][k]]; qz[k] = z[F[f][k]]; }
        emit(c, q, qz, 4, col);
    }
    if (!keep) return;
    /* Sides in a fixed order: -y, +x, +y, -x, each from bottom-left to
       bottom-right with v up the wall. */
    static const int SF[4][3] = { { 0, 1, 4 }, { 1, 3, 5 }, { 3, 2, 7 }, { 2, 0, 6 } };
    static const float SN[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    for (int k = 0; k < 4; k++) {
        Face *fc = &g_face[k];
        float wx = X.ux.x * SN[k][0] + X.uy.x * SN[k][1];
        float wy = X.ux.y * SN[k][0] + X.uy.y * SN[k][1];
        float wz = X.ux.z * SN[k][0] + X.uy.z * SN[k][1];
        fc->vis = wx * g_DX + wy * g_DY + wz * g_DZ > 0.002f;
        fc->d = wx * g_LX + wy * g_LY + wz * g_LZ;
        int a = SF[k][0], b = SF[k][1], t = SF[k][2];
        fc->o = s[a];
        fc->u.x = s[b].x - s[a].x; fc->u.y = s[b].y - s[a].y;
        fc->v.x = s[t].x - s[a].x; fc->v.y = s[t].y - s[a].y;
        fc->zo = z[a]; fc->zu = z[b] - z[a]; fc->zv = z[t] - z[a];
        fc->len = (k & 1) ? (y1 - y0) : (x1 - x0);
    }
}

/** A sub-rectangle of a recorded face, in face fractions (u across, v up). */
static void face_rect(PA_Canvas *c, const Face *f, float u0, float u1, float v0, float v1, PA_Color col) {
    PA_Vec2 s[4]; float z[4];
    float us[4] = { u0, u1, u1, u0 }, vs[4] = { v0, v0, v1, v1 };
    for (int i = 0; i < 4; i++) {
        s[i].x = f->o.x + f->u.x * us[i] + f->v.x * vs[i];
        s[i].y = f->o.y + f->u.y * us[i] + f->v.y * vs[i];
        z[i] = f->zo + f->zu * us[i] + f->zv * vs[i];
    }
    emit(c, s, z, 4, col);
}

/** Inset two-tone windows: a pale frame, sky-blue upper glass, deeper lower
    glass. Bands instead of panes once a pane would be under ~5px. */
static void windows(PA_Canvas *c, const Face *f, int cols, int rows, float v0, float v1,
                    PA_Color frame, float fw, float fh) {
    if (!f->vis || rows < 1 || cols < 1) return;
    float face_px = sqrtf(f->u.x * f->u.x + f->u.y * f->u.y);
    float face_py = sqrtf(f->v.x * f->v.x + f->v.y * f->v.y) * (v1 - v0);
    if (face_py / (float)rows < 3.0f) return;
    PA_Color g_hi = lit(pa_hex(0x9FD8FF), f->d), g_lo = lit(pa_hex(0x4A7BC8), f->d);
    PA_Color fr = lit(frame, f->d);
    float pane = face_px / (float)cols * fw;
    for (int r = 0; r < rows; r++) {
        float a = v0 + (v1 - v0) * ((float)r + 0.5f - fh * 0.5f) / (float)rows;
        float b = v0 + (v1 - v0) * ((float)r + 0.5f + fh * 0.5f) / (float)rows;
        float m = a + (b - a) * 0.45f;
        if (pane < 8.0f) {
            face_rect(c, f, 0.06f, 0.94f, a, b, g_lo);
            continue;
        }
        /* One pale sill under the whole row reads as the window bevel at a
           third of the cost of framing every pane. */
        if (pane > 12.0f) face_rect(c, f, 0.03f, 0.97f, a - (b - a) * 0.2f, a, fr);
        for (int k = 0; k < cols; k++) {
            float u0 = ((float)k + 0.5f - fw * 0.5f) / (float)cols, u1 = ((float)k + 0.5f + fw * 0.5f) / (float)cols;
            face_rect(c, f, u0, u1, a, pane > 10.0f ? m : b, g_lo);
            if (pane > 10.0f) face_rect(c, f, u0, u1, m, b, g_hi);
        }
    }
}

/** A disc in the local frame (heads, leaves): a polygon when it may be going
    under the lip, a cheap circle otherwise. */
static void ldisc(PA_Canvas *c, float lx, float ly, float lz, float rad, PA_Color col) {
    PA_Vec2 ctr = xp(lx, ly, lz);
    float r = rad * X.px;
    if (r < 0.6f) return;
    float z = xz(lx, ly, lz);
    if (!X.clip || z - rad > 0.0f) { fcircle(c, ctr.x, ctr.y, r, col); return; }
    PA_Vec2 s[12]; float zz[12];
    for (int i = 0; i < 12; i++) {
        float a = PA_TAU * (float)i / 12.0f;
        s[i].x = ctr.x + cosf(a) * r;
        s[i].y = ctr.y + sinf(a) * r;
        zz[i] = z - sinf(a) * rad * 0.5f;
    }
    emit(c, s, zz, 12, col);
}

/* --------------------------------------------------------------- props --- */
/** Square frustum centred on (cx, cy): cones, water-tank caps, umbrellas. */
static void pyramid(PA_Canvas *c, float cx, float cy, float r0, float r1, float z0, float z1, PA_Color col) {
    static const float SX[4] = { -1, 1, 1, -1 }, SY[4] = { -1, -1, 1, 1 };
    float slope = (r0 - r1) / (z1 - z0 + 0.001f);
    for (int k = 0; k < 4; k++) {
        int n = (k + 1) & 3;
        float q[4][3] = { { cx + SX[k] * r0, cy + SY[k] * r0, z0 }, { cx + SX[n] * r0, cy + SY[n] * r0, z0 },
                          { cx + SX[n] * r1, cy + SY[n] * r1, z1 }, { cx + SX[k] * r1, cy + SY[k] * r1, z1 } };
        float mx = (SX[k] + SX[n]) * 0.5f, my = (SY[k] + SY[n]) * 0.5f;
        float nl = sqrtf(mx * mx + my * my + slope * slope);
        lpoly(c, (const float (*)[3])q, 4, mx / nl, my / nl, slope / nl, col);
    }
    if (r1 > 0.01f) {
        float t[4][3] = { { cx - r1, cy - r1, z1 }, { cx + r1, cy - r1, z1 }, { cx + r1, cy + r1, z1 }, { cx - r1, cy + r1, z1 } };
        lpoly(c, (const float (*)[3])t, 4, 0, 0, 1, col);
    }
}

static void wheels(PA_Canvas *c, float hl, float hw, float xs, float rad) {
    /* Only the pair on the camera side shows; the far pair is under the body. */
    float wy = X.uy.x * g_DX + X.uy.y * g_DY + X.uy.z * g_DZ;
    float side = wy > 0.0f ? 1.0f : -1.0f;
    PA_Color tyre = pa_hex(0x262A40);
    for (int k = -1; k <= 1; k += 2) {
        float x = (float)k * hl * xs;
        float y = side * (hw - 1.4f);
        box(c, x - rad, y - 1.8f, 0.0f, x + rad, y + 1.8f, rad * 1.7f, tyre, tyre, 0);
        if (X.px * rad > 3.0f && !X.clip) {
            PA_Vec2 h = xp(x, side * (hw + 0.5f), rad * 0.85f);
            fcircle(c, h.x, h.y, rad * X.px * 0.42f, pa_hex(0xC9CEE0));
        }
    }
}

static void draw_vehicle(PA_Canvas *c, const Prop *p) {
    const PropType *t = &TYPES[p->style];
    float hl = t->len * 0.5f, hw = t->wid * 0.5f, h = t->h;
    int detail = X.px > 0.9f;
    PA_Color glass = pa_hex(0x3D5A9E), glass_hi = pa_hex(0x8FCBFF);
    switch (p->style) {
    case ST_BUS: {
        if (detail) wheels(c, hl, hw, 0.66f, 5.0f);
        box(c, -hl, -hw, 3.0f, hl, hw, h, p->col, pa_hex(0xF4F6FF), 1);
        for (int k = 0; k < 4; k++) {
            const Face *f = &g_face[k];
            if (!f->vis) continue;
            if (k & 1) face_rect(c, f, 0.12f, 0.88f, 0.48f, 0.84f, lit(glass, f->d));
            else {
                face_rect(c, f, 0.04f, 0.96f, 0.50f, 0.82f, lit(glass, f->d));
                if (detail) for (int w = 1; w < 8; w++)
                    face_rect(c, f, 0.04f + 0.115f * (float)w - 0.008f, 0.04f + 0.115f * (float)w + 0.008f,
                              0.50f, 0.82f, lit(p->col, f->d));
                face_rect(c, f, 0.0f, 1.0f, 0.18f, 0.26f, lit(pa_hex(0xFFFFFF), f->d));
            }
        }
        if (detail) box(c, -hl * 0.5f, -hw * 0.6f, h, hl * 0.2f, hw * 0.6f, h + 3.0f, pa_hex(0xD8DCEC),
                        pa_hex(0xE8EBF6), 0);
        break;
    }
    case ST_TRUCK: {
        if (detail) wheels(c, hl, hw, 0.7f, 5.4f);
        box(c, -hl, -hw, 4.0f, hl * 0.40f, hw, h, pa_hex(0xF2F3F8), pa_hex(0xFFFFFF), 1);
        for (int k = 0; k < 4; k += 2) if (g_face[k].vis)
            face_rect(c, &g_face[k], 0.06f, 0.94f, 0.38f, 0.62f, lit(p->col, g_face[k].d));
        box(c, hl * 0.44f, -hw * 0.94f, 3.0f, hl, hw * 0.94f, h * 0.74f, p->col, pa_shade(p->col, 0.15f), 1);
        if (g_face[1].vis) face_rect(c, &g_face[1], 0.1f, 0.9f, 0.55f, 0.88f, lit(glass, g_face[1].d));
        for (int k = 0; k < 4; k += 2) if (g_face[k].vis)
            face_rect(c, &g_face[k], k == 0 ? 0.55f : 0.08f, k == 0 ? 0.92f : 0.45f, 0.55f, 0.88f,
                      lit(glass, g_face[k].d));
        break;
    }
    case ST_VAN: {
        if (detail) wheels(c, hl, hw, 0.62f, 4.4f);
        box(c, -hl, -hw, 3.0f, hl, hw, h, p->col, p->col2, 1);
        for (int k = 0; k < 4; k++) {
            const Face *f = &g_face[k];
            if (!f->vis) continue;
            if (k == 1) face_rect(c, f, 0.08f, 0.92f, 0.52f, 0.86f, lit(glass, f->d));
            else if (!(k & 1)) {
                face_rect(c, f, k == 0 ? 0.72f : 0.04f, k == 0 ? 0.96f : 0.28f, 0.52f, 0.86f, lit(glass, f->d));
                face_rect(c, f, 0.0f, 1.0f, 0.12f, 0.22f, lit(pa_shade(p->col, -0.3f), f->d));
            }
        }
        break;
    }
    default: {   /* car, cab, police */
        float body = h * 0.52f;
        if (detail) wheels(c, hl, hw, 0.6f, 4.2f);
        PA_Color lower = p->style == ST_POLICE ? pa_hex(0x24305E) : pa_shade(p->col, -0.12f);
        PA_Color upper = p->style == ST_POLICE ? pa_hex(0xF4F6FF) : p->col;
        float split = 3.0f + (body - 3.0f) * 0.42f;
        if (detail) {
            box(c, -hl, -hw, 3.0f, hl, hw, split, lower, lower, 0);
            box(c, -hl, -hw, split, hl, hw, body, upper, upper, 1);
            if (g_face[1].vis) {
                face_rect(c, &g_face[1], 0.08f, 0.26f, 0.45f, 0.85f, pa_hex(0xFFF4C8));
                face_rect(c, &g_face[1], 0.74f, 0.92f, 0.45f, 0.85f, pa_hex(0xFFF4C8));
            }
            if (g_face[3].vis) {
                face_rect(c, &g_face[3], 0.08f, 0.26f, 0.45f, 0.85f, pa_hex(0xFF4A5C));
                face_rect(c, &g_face[3], 0.74f, 0.92f, 0.45f, 0.85f, pa_hex(0xFF4A5C));
            }
        } else {
            box(c, -hl, -hw, 3.0f, hl, hw, body, upper, upper, 0);
        }
        float x0 = -hl * 0.50f, x1 = hl * 0.30f;
        PA_Color roof = p->style == ST_CAR ? p->col2 : upper;
        box(c, x0, -hw * 0.84f, body, x1, hw * 0.84f, h, glass, roof, 1);
        if (detail) for (int k = 0; k < 4; k++) if (g_face[k].vis)
            face_rect(c, &g_face[k], 0.08f, 0.92f, 0.40f, 0.86f, lit(glass_hi, g_face[k].d));
        if (p->style == ST_CAB) {
            box(c, -3.0f, -5.0f, h, 3.0f, 5.0f, h + 3.5f, pa_hex(0x2A2E44), pa_hex(0xFFFFFF), 0);
        }
        if (p->style == ST_POLICE) {
            box(c, -2.5f, -7.0f, h, 2.5f, 0.0f, h + 2.8f, pa_hex(0xFF3B4E), pa_hex(0xFF6B7A), 0);
            box(c, -2.5f, 0.0f, h, 2.5f, 7.0f, h + 2.8f, pa_hex(0x2F6BFF), pa_hex(0x6F9BFF), 0);
        }
        break;
    }
    }
}

static void draw_person(PA_Canvas *c, const Prop *p) {
    PA_Color skin = pa_hex(SKINS_TONE[p->seed % COUNT_OF(SKINS_TONE)]);
    int panic = p->panic > 0.0f || p->state == PS_TIP;
    float sw = sinf(p->t) * (panic ? 3.4f : 2.2f);
    int detail = X.px > 1.5f;
    if (X.px < 0.9f) {
        /* Far away: one body block and a head reads as a person. */
        box(c, -1.8f, -3.0f, 0.0f, 1.8f, 3.0f, 15.0f, p->col, p->col, 0);
        ldisc(c, 0.0f, 0.0f, 18.0f, 3.8f, skin);
        return;
    }
    if (detail) {
        box(c, sw - 1.4f, -2.8f, 0.0f, sw + 1.4f, -0.4f, 8.5f, p->col2, p->col2, 0);
        box(c, -sw - 1.4f, 0.4f, 0.0f, -sw + 1.4f, 2.8f, 8.5f, p->col2, p->col2, 0);
    } else {
        box(c, -1.4f, -2.6f, 0.0f, 1.4f, 2.6f, 8.5f, p->col2, p->col2, 0);
    }
    box(c, -2.0f, -3.4f, 8.0f, 2.0f, 3.4f, 15.5f, p->col, p->col, 0);
    if (detail) {
        if (panic) {
            float wv = sinf(p->t * 1.7f) * 1.5f;
            box(c, -1.0f + wv, -5.4f, 13.0f, 1.0f + wv, -3.4f, 21.5f, skin, skin, 0);
            box(c, -1.0f - wv, 3.4f, 13.0f, 1.0f - wv, 5.4f, 21.5f, skin, skin, 0);
        } else {
            box(c, -sw * 0.6f - 1.0f, -5.2f, 8.6f, -sw * 0.6f + 1.0f, -3.4f, 15.0f, p->col, p->col, 0);
            box(c, sw * 0.6f - 1.0f, 3.4f, 8.6f, sw * 0.6f + 1.0f, 5.2f, 15.0f, p->col, p->col, 0);
        }
    }
    ldisc(c, 0.0f, 0.0f, 18.4f, 3.6f, skin);
    if (detail) {
        PA_Color hair = pa_hex((p->seed & 4) ? 0x3A2A22 : (p->seed & 8) ? 0xE8C060 : 0x6A3E26);
        ldisc(c, -0.9f, 0.0f, 20.0f, 3.0f, hair);
        if (panic && !X.clip) {
            /* Mouth open in a scream. */
            PA_Vec2 m = xp(3.0f, 0.0f, 17.4f);
            fcircle(c, m.x, m.y, X.px * 1.1f, pa_hex(0x5A1A22));
        }
    }
}

static void draw_tree(PA_Canvas *c, const Prop *p, float h, float rad) {
    box(c, -1.8f, -1.8f, 0.0f, 1.8f, 1.8f, h * 0.45f, pa_hex(0x8A5A3C), pa_hex(0x8A5A3C), 0);
    ldisc(c, 0.0f, 0.0f, h * 0.62f, rad, pa_shade(p->col, -0.30f));
    ldisc(c, -rad * 0.08f, rad * 0.06f, h * 0.68f, rad * 0.92f, p->col);
    if (X.px * rad > 5.0f) {
        ldisc(c, -rad * 0.34f, rad * 0.32f, h * 0.78f, rad * 0.5f, pa_shade(pa_mix(p->col, WARM, 0.3f), 0.16f));
        ldisc(c, rad * 0.4f, -rad * 0.2f, h * 0.56f, rad * 0.42f, pa_shade(p->col, -0.12f));
    }
}

static void roof_clutter(PA_Canvas *c, const Prop *p, float hx, float hy, float z, int big) {
    if (X.px * hx < 10.0f) return;
    unsigned s = p->seed;
    PA_Color metal = pa_hex(0xC9CEDF), metal_top = pa_hex(0xE6E9F3);
    int ac = 1 + (int)(s & 1) + big;
    for (int k = 0; k < ac; k++) {
        float ax = -hx * 0.55f + (float)k * hx * 0.38f, ay = (s & 2) ? hy * 0.45f : -hy * 0.45f;
        box(c, ax - 4.5f, ay - 3.5f, z, ax + 4.5f, ay + 3.5f, z + 5.0f, metal, metal_top, 0);
        if (X.px > 0.9f) ldisc(c, ax, ay, z + 5.1f, 2.4f, pa_hex(0x7A8098));
    }
    if (s & 4) {
        /* Water tank on stilts with a pointed cap, the plates' brownstone mark. */
        float tx = hx * 0.45f, ty = -hy * 0.3f;
        PA_Color wood = pa_hex(0x9A6A48);
        if (X.px > 0.8f) {
            box(c, tx - 5.0f, ty - 5.0f, z, tx - 3.8f, ty - 3.8f, z + 7.0f, wood, wood, 0);
            box(c, tx + 3.8f, ty + 3.8f, z, tx + 5.0f, ty + 5.0f, z + 7.0f, wood, wood, 0);
            box(c, tx + 3.8f, ty - 5.0f, z, tx + 5.0f, ty - 3.8f, z + 7.0f, wood, wood, 0);
        }
        box(c, tx - 6.0f, ty - 6.0f, z + 7.0f, tx + 6.0f, ty + 6.0f, z + 19.0f, wood, wood, 0);
        pyramid(c, tx, ty, 7.0f, 0.5f, z + 19.0f, z + 25.0f, pa_hex(0x6E7898));
    } else if (s & 8) {
        box(c, hx * 0.05f, -hy * 0.6f, z, hx * 0.75f, -hy * 0.05f, z + 1.6f, pa_hex(0x2B3C8A), pa_hex(0x3550B8), 0);
    }
    if (s & 16) {
        float ax = hx * 0.6f, ay = hy * 0.55f;
        box(c, ax - 0.7f, ay - 0.7f, z, ax + 0.7f, ay + 0.7f, z + 22.0f, pa_hex(0x8A90A8), pa_hex(0xB0B6CA), 0);
        ldisc(c, ax, ay, z + 22.5f, 1.4f, pa_hex(0xFF4A5C));
    }
    if (big && (s & 32) && !X.clip) {
        PA_Vec2 d = xp(-hx * 0.4f, hy * 0.4f, z + 6.0f);
        fellipse(c, d.x, d.y, 5.5f * X.px, 3.6f * X.px, pa_hex(0xE6E9F3));
        fellipse(c, d.x + 0.8f * X.px, d.y + 0.6f * X.px, 3.6f * X.px, 2.2f * X.px, pa_hex(0xB8BED2));
    }
}

/** The darker inset deck inside a roof parapet, as every roof in the plates. */
static void roof_deck(PA_Canvas *c, float hx, float hy, float z, PA_Color deck) {
    float in = 3.2f;
    float q[4][3] = { { -hx + in, -hy + in, z + 0.05f }, { hx - in, -hy + in, z + 0.05f },
                      { hx - in, hy - in, z + 0.05f }, { -hx + in, hy - in, z + 0.05f } };
    lpoly(c, (const float (*)[3])q, 4, 0, 0, 1, deck);
}

static void draw_building(PA_Canvas *c, const Prop *p) {
    const PropType *t = &TYPES[p->style];
    float hx = t->len * 0.5f, hy = t->wid * 0.5f, h = t->h;
    int detail = X.px * hx > 16.0f;
    int fine = X.px * hx > 34.0f;
    PA_Color cream = pa_hex(0xFBF2E2);

    switch (p->style) {
    case ST_GARAGE: {
        PA_Color roof = pa_mix(p->col2, pa_hex(0xE0506A), 0.55f);
        box(c, -hx, -hy, 0.0f, hx, hy, h, p->col, p->col, 1);
        if (detail) {
            for (int k = 0; k < 4; k++) {
                const Face *f = &g_face[k];
                if (!f->vis) continue;
                if (k == 1 || k == 2) {
                    face_rect(c, f, 0.14f, 0.60f, 0.0f, 0.66f, lit(pa_hex(0xE7EAF4), f->d));
                    for (int b = 1; b < 4; b++)
                        face_rect(c, f, 0.14f, 0.60f, 0.16f * (float)b, 0.16f * (float)b + 0.03f,
                                  lit(pa_hex(0xB8BED2), f->d));
                }
                face_rect(c, f, 0.70f, 0.88f, 0.35f, 0.72f, lit(cream, f->d));
                face_rect(c, f, 0.72f, 0.86f, 0.38f, 0.69f, lit(pa_hex(0x6FA6E8), f->d));
            }
        }
        float rh = 16.0f, eave = 3.0f;
        float a[4][3] = { { -hx - eave, -hy - eave, h }, { hx + eave, -hy - eave, h },
                          { hx + eave, 0.0f, h + rh }, { -hx - eave, 0.0f, h + rh } };
        float b[4][3] = { { -hx - eave, hy + eave, h }, { -hx - eave, 0.0f, h + rh },
                          { hx + eave, 0.0f, h + rh }, { hx + eave, hy + eave, h } };
        float nl = sqrtf(rh * rh + (hy + eave) * (hy + eave));
        float g0[3][3] = { { -hx, -hy, h }, { -hx, 0.0f, h + rh }, { -hx, hy, h } };
        float g1[3][3] = { { hx, -hy, h }, { hx, hy, h }, { hx, 0.0f, h + rh } };
        lpoly(c, (const float (*)[3])g0, 3, -1, 0, 0, p->col);
        lpoly(c, (const float (*)[3])g1, 3, 1, 0, 0, p->col);
        lpoly(c, (const float (*)[3])a, 4, 0.0f, -rh / nl, (hy + eave) / nl, roof);
        lpoly(c, (const float (*)[3])b, 4, 0.0f, rh / nl, (hy + eave) / nl, roof);
        if (detail && (p->seed & 1))
            box(c, hx * 0.3f, -4.0f, h + rh * 0.4f, hx * 0.3f + 5.0f, 0.0f, h + rh + 6.0f,
                pa_hex(0xB0584A), pa_hex(0x8A4038), 0);
        break;
    }
    case ST_BROWN: {
        box(c, -hx, -hy, 0.0f, hx, hy, h - 6.0f, p->col, p->col, 1);
        if (detail) for (int k = 0; k < 4; k++) {
            const Face *f = &g_face[k];
            if (!f->vis) continue;
            windows(c, f, (int)(f->len / 16.0f), 5, 0.2f, 0.97f, cream, 0.5f, 0.66f);
            if (fine) {
                face_rect(c, f, 0.0f, 1.0f, 0.185f, 0.2f, lit(cream, f->d));
                face_rect(c, f, 0.40f, 0.60f, 0.0f, 0.16f, lit(pa_hex(0x5A3A32), f->d));
                face_rect(c, f, 0.36f, 0.64f, 0.155f, 0.18f, lit(cream, f->d));
            }
        }
        box(c, -hx - 1.8f, -hy - 1.8f, h - 6.0f, hx + 1.8f, hy + 1.8f, h, cream, pa_shade(p->col, 0.12f), 0);
        roof_deck(c, hx + 1.8f, hy + 1.8f, h, pa_hex(0xA89CB8));
        roof_clutter(c, p, hx, hy, h, 0);
        break;
    }
    case ST_SHOP: {
        box(c, -hx, -hy, 0.0f, hx, hy, h, p->col, p->col, 1);
        if (detail) for (int k = 0; k < 4; k++) {
            const Face *f = &g_face[k];
            if (!f->vis) continue;
            face_rect(c, f, 0.06f, 0.94f, 0.04f, 0.36f, lit(pa_hex(0x5A8FD8), f->d));
            if (fine) face_rect(c, f, 0.06f, 0.94f, 0.22f, 0.36f, lit(pa_hex(0xA8DCFF), f->d));
            face_rect(c, f, 0.12f, 0.88f, 0.46f, 0.56f, lit(pa_shade(p->col2, -0.25f), f->d));
            windows(c, f, (int)(f->len / 18.0f), 2, 0.60f, 0.96f, cream, 0.56f, 0.62f);
        }
        if (detail) {
            /* Striped awnings over the visible storefronts. */
            static const float SN[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
            int vis[4];
            for (int k = 0; k < 4; k++) vis[k] = g_face[k].vis;
            for (int k = 0; k < 4; k++) {
                if (!vis[k]) continue;
                float nx = SN[k][0], ny = SN[k][1];
                float tx = -ny, ty = nx;
                float half = (k & 1) ? hy : hx;
                float fx = nx * hx, fy = ny * hy;
                int stripes = fine ? 6 : 2;
                for (int s = 0; s < stripes; s++) {
                    float a = -half * 0.92f + half * 1.84f * (float)s / (float)stripes;
                    float b = -half * 0.92f + half * 1.84f * (float)(s + 1) / (float)stripes;
                    float q[4][3] = { { fx + tx * a, fy + ty * a, h * 0.42f },
                                      { fx + tx * b, fy + ty * b, h * 0.42f },
                                      { fx + tx * b + nx * 9.0f, fy + ty * b + ny * 9.0f, h * 0.32f },
                                      { fx + tx * a + nx * 9.0f, fy + ty * a + ny * 9.0f, h * 0.32f } };
                    lpoly(c, (const float (*)[3])q, 4, nx * 0.6f, ny * 0.6f, 0.8f,
                          (s & 1) ? pa_hex(0xFFFFFF) : p->col2);
                }
            }
        }
        box(c, -hx - 1.0f, -hy - 1.0f, h, hx + 1.0f, hy + 1.0f, h + 3.0f, pa_shade(p->col, 0.2f), pa_shade(p->col, 0.25f), 0);
        roof_deck(c, hx + 1.0f, hy + 1.0f, h + 3.0f, pa_hex(0xB4AECB));
        roof_clutter(c, p, hx, hy, h + 3.0f, 0);
        break;
    }
    case ST_APART: {
        float h0 = h * 0.62f;
        box(c, -hx, -hy, 0.0f, hx, hy, h0, p->col, pa_shade(p->col2, 0.1f), 1);
        if (detail) for (int k = 0; k < 4; k++)
            windows(c, &g_face[k], (int)(g_face[k].len / 15.0f), 4, 0.08f, 0.96f, cream, 0.58f, 0.6f);
        float ix = hx * 0.78f, iy = hy * 0.78f;
        box(c, -ix, -iy, h0, ix, iy, h, p->col2, pa_shade(p->col2, 0.1f), 1);
        if (detail) for (int k = 0; k < 4; k++)
            windows(c, &g_face[k], (int)(g_face[k].len / 15.0f), 3, 0.06f, 0.94f, cream, 0.58f, 0.6f);
        box(c, -ix - 1.0f, -iy - 1.0f, h, ix + 1.0f, iy + 1.0f, h + 2.5f, cream, cream, 0);
        roof_deck(c, ix + 1.0f, iy + 1.0f, h + 2.5f, pa_hex(0xC4BCD8));
        roof_clutter(c, p, ix, iy, h + 2.5f, 1);
        break;
    }
    case ST_OFFICE: case ST_SPIRE: {
        int tiers = p->style == ST_SPIRE ? 3 : 1;
        float z0 = 0.0f;
        for (int tr = 0; tr < tiers; tr++) {
            float k = tr == 0 ? 1.0f : tr == 1 ? 0.78f : 0.56f;
            float z1 = tiers == 1 ? h : tr == 0 ? h * 0.52f : tr == 1 ? h * 0.80f : h;
            float ix = hx * k, iy = hy * k;
            box(c, -ix, -iy, z0, ix, iy, z1, p->col, pa_shade(p->col, 0.2f), 1);
            if (detail) for (int f = 0; f < 4; f++) {
                Face *fc = &g_face[f];
                if (!fc->vis) continue;
                /* Curtain wall: sky-lit at the top, deep blue at the base,
                   mullions and floor lines over it. */
                if (!X.clip) {
                    PA_Vec2 q[4] = { fc->o, { fc->o.x + fc->u.x, fc->o.y + fc->u.y },
                                     { fc->o.x + fc->u.x + fc->v.x, fc->o.y + fc->u.y + fc->v.y },
                                     { fc->o.x + fc->v.x, fc->o.y + fc->v.y } };
                    PA_Paint g = pa_linear(0, fc->o.y + fc->v.y, 0, fc->o.y);
                    pa_stop(&g, 0.0f, lit(pa_mix(pa_hex(0xA8DEFF), p->col, 0.30f), fc->d));
                    pa_stop(&g, 1.0f, lit(pa_mix(pa_hex(0x4A7BC8), p->col, 0.30f), fc->d));
                    fpoly_paint(c, q, 4, &g);
                }
                PA_Color mul = lit(pa_mix(p->col, pa_hex(0xFFFFFF), 0.45f), fc->d);
                int cols = (int)(fc->len / 14.0f);
                float colpx = sqrtf(fc->u.x * fc->u.x + fc->u.y * fc->u.y) / (float)(cols > 0 ? cols : 1);
                if (colpx > 4.0f) for (int m = 1; m < cols; m++) {
                    float u = (float)m / (float)cols;
                    face_rect(c, fc, u - 0.012f, u + 0.012f, 0.0f, 1.0f, mul);
                }
                int rows = (int)((z1 - z0) / 16.0f);
                float rowpx = sqrtf(fc->v.x * fc->v.x + fc->v.y * fc->v.y) / (float)(rows > 0 ? rows : 1);
                if (rowpx > 4.0f) for (int m = 1; m < rows; m++) {
                    float v = (float)m / (float)rows;
                    face_rect(c, fc, 0.0f, 1.0f, v - 0.01f, v + 0.01f, mul);
                }
                if (tr == 0) face_rect(c, fc, 0.0f, 1.0f, 0.0f, 12.0f / (z1 - z0), lit(pa_hex(0x3A4470), fc->d));
            }
            box(c, -ix - 1.0f, -iy - 1.0f, z1, ix + 1.0f, iy + 1.0f, z1 + 3.0f, pa_hex(0xE4E7F2), pa_hex(0xF2F4FA), 0);
            roof_deck(c, ix + 1.0f, iy + 1.0f, z1 + 3.0f, pa_hex(0xA8B0CC));
            z0 = z1;
        }
        float k = tiers == 1 ? 1.0f : 0.56f;
        if (p->style == ST_OFFICE && (p->seed & 1)) {
            if (detail && !X.clip) {
                PA_Vec2 hc = xp(0.0f, 0.0f, h + 3.2f);
                float rr = 30.0f * X.px;
                fellipse(c, hc.x, hc.y, rr, rr * GROUND_K, pa_hex(0x4A5070));
                fellipse(c, hc.x, hc.y, rr * 0.86f, rr * 0.86f * GROUND_K, pa_hex(0x5C6488));
                float hw = 9.0f * X.px;
                if (hw > 3.0f) txtb(c, "H", hc.x, hc.y - hw * 0.9f, hw * 1.8f, pa_hex(0xFFD23A), pa_hex(0x4A5070),
                                    PA_ALIGN_CENTER, 0.0f, 1.0f);
            }
        } else {
            box(c, -hx * k * 0.4f, -hy * k * 0.3f, h + 3.0f, hx * k * 0.3f, hy * k * 0.35f, h + 14.0f,
                pa_hex(0xD2D6E6), pa_hex(0xE8EBF4), 0);
            roof_clutter(c, p, hx * k, hy * k, h + 3.0f, 1);
        }
        if (p->style == ST_SPIRE)
            box(c, -1.0f, -1.0f, h + 14.0f, 1.0f, 1.0f, h + 50.0f, pa_hex(0xC9CEDF), pa_hex(0xE6E9F3), 0);
        break;
    }
    default: break;
    }
}

static void draw_small(PA_Canvas *c, const Prop *p) {
    const PropType *t = &TYPES[p->style];
    float hl = t->len * 0.5f, hw = t->wid * 0.5f, h = t->h;
    int detail = X.px > 1.0f;
    if (X.px * (hl > hw ? hl : hw) < 5.0f && p->style != ST_TREE && p->style != ST_BUSH) {
        /* A few pixels across: one block in the prop's colour is all that
           survives at this size, and it costs one fill instead of six. */
        if (p->style == ST_LAMP) box(c, -1.2f, -1.2f, 0.0f, 1.2f, 1.2f, h, p->col, pa_hex(0xFFF0B0), 0);
        else box(c, -hl * 0.8f, -hw * 0.8f, 0.0f, hl * 0.8f, hw * 0.8f, h * 0.8f, p->col, pa_shade(p->col, 0.15f), 0);
        return;
    }
    switch (p->style) {
    case ST_CONE:
        box(c, -hl * 0.62f, -hl * 0.62f, 0.0f, hl * 0.62f, hl * 0.62f, 1.6f, pa_shade(p->col, -0.2f), p->col, 0);
        pyramid(c, 0, 0, hl * 0.5f, hl * 0.36f, 1.6f, 5.2f, p->col);
        pyramid(c, 0, 0, hl * 0.36f, hl * 0.27f, 5.2f, 7.6f, pa_hex(0xFFFFFF));
        pyramid(c, 0, 0, hl * 0.27f, 0.4f, 7.6f, h, p->col);
        break;
    case ST_BARRIER:
        box(c, -hl, -1.2f, 0.0f, -hl + 2.0f, 1.2f, h * 0.7f, pa_hex(0x5A6488), pa_hex(0x5A6488), 0);
        box(c, hl - 2.0f, -1.2f, 0.0f, hl, 1.2f, h * 0.7f, pa_hex(0x5A6488), pa_hex(0x5A6488), 0);
        for (int s = 0; s < 4; s++) {
            float a = -hl + hl * 0.5f * (float)s, b = a + hl * 0.5f;
            PA_Color sc = (s & 1) ? pa_hex(0xFFFFFF) : pa_hex(0xF0475A);
            box(c, a, -1.6f, h * 0.55f, b, 1.6f, h, sc, sc, 0);
        }
        break;
    case ST_BIN:
        box(c, -hl * 0.85f, -hw * 0.85f, 0.0f, hl * 0.85f, hw * 0.85f, h - 2.0f, p->col, p->col, 0);
        box(c, -hl, -hw, h - 2.0f, hl, hw, h, pa_shade(p->col, -0.3f), pa_shade(p->col, -0.15f), 0);
        break;
    case ST_HYDRANT:
        box(c, -2.6f, -2.6f, 0.0f, 2.6f, 2.6f, h - 3.0f, p->col, p->col, 0);
        if (detail) box(c, -4.0f, -1.2f, h * 0.45f, 4.0f, 1.2f, h * 0.65f, pa_shade(p->col, -0.1f), p->col, 0);
        ldisc(c, 0.0f, 0.0f, h - 2.0f, 3.0f, pa_shade(p->col, 0.15f));
        break;
    case ST_MAILBOX:
        box(c, -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 6.0f, pa_hex(0x3A4060), pa_hex(0x3A4060), 0);
        box(c, -hl * 0.6f, -hw * 0.6f, 6.0f, hl * 0.6f, hw * 0.6f, h, p->col, pa_shade(p->col, 0.2f), 0);
        break;
    case ST_LAMP: {
        PA_Color pole = p->col;
        box(c, -1.8f, -1.8f, 0.0f, 1.8f, 1.8f, 2.0f, pole, pole, 0);
        box(c, -0.9f, -0.9f, 2.0f, 0.9f, 0.9f, h, pole, pole, 0);
        if (detail) box(c, -0.7f, -0.7f, h - 1.4f, 6.0f, 0.7f, h, pole, pole, 0);
        box(c, 3.4f, -2.2f, h - 3.0f, 7.4f, 2.2f, h - 1.0f, pa_hex(0xFFF0B0), pa_hex(0x6A7290), 0);
        break;
    }
    case ST_BUSH:
        ldisc(c, -2.0f, 1.0f, 5.0f, 6.4f, pa_shade(p->col, -0.25f));
        ldisc(c, 2.5f, -1.5f, 6.0f, 5.6f, p->col);
        ldisc(c, -1.0f, -2.0f, 8.6f, 4.0f, pa_shade(p->col, 0.15f));
        break;
    case ST_BENCH: {
        PA_Color wood = p->col, iron = pa_hex(0x4A5070);
        if (detail) {
            box(c, -hl + 1.0f, -hw, 0.0f, -hl + 2.6f, hw, 5.0f, iron, iron, 0);
            box(c, hl - 2.6f, -hw, 0.0f, hl - 1.0f, hw, 5.0f, iron, iron, 0);
        }
        box(c, -hl, -hw * 0.9f, 4.5f, hl, hw * 0.5f, 6.2f, wood, pa_shade(wood, 0.1f), 0);
        box(c, -hl, hw * 0.55f, 6.0f, hl, hw * 0.95f, h, wood, pa_shade(wood, 0.1f), 0);
        break;
    }
    case ST_TREE:
        draw_tree(c, p, h, hl * 0.62f);
        break;
    case ST_BOOTH:
        box(c, -hl * 0.5f, -hw * 0.5f, 0.0f, hl * 0.5f, hw * 0.5f, h, p->col, pa_shade(p->col, 0.12f), 1);
        if (detail) for (int k = 0; k < 4; k++) if (g_face[k].vis)
            face_rect(c, &g_face[k], 0.18f, 0.82f, 0.18f, 0.78f, lit(pa_hex(0xBFE6FF), g_face[k].d));
        box(c, -hl * 0.55f, -hw * 0.55f, h, hl * 0.55f, hw * 0.55f, h + 2.0f, pa_shade(p->col, -0.2f), p->col, 0);
        break;
    case ST_SCOOTER:
        if (detail) {
            box(c, -hl * 0.8f, -1.0f, 0.0f, -hl * 0.4f, 1.0f, 4.4f, pa_hex(0x262A40), pa_hex(0x262A40), 0);
            box(c, hl * 0.4f, -1.0f, 0.0f, hl * 0.8f, 1.0f, 4.4f, pa_hex(0x262A40), pa_hex(0x262A40), 0);
        }
        box(c, -hl * 0.7f, -hw * 0.5f, 3.0f, hl * 0.5f, hw * 0.5f, 7.0f, p->col, pa_shade(p->col, 0.15f), 0);
        box(c, -hl * 0.55f, -hw * 0.45f, 7.0f, -hl * 0.05f, hw * 0.45f, 9.5f, pa_hex(0x2E3450), pa_hex(0x3A4060), 0);
        box(c, hl * 0.42f, -0.8f, 3.0f, hl * 0.58f, 0.8f, h, pa_hex(0x6A7290), pa_hex(0x6A7290), 0);
        box(c, hl * 0.4f, -hw * 0.7f, h - 1.4f, hl * 0.6f, hw * 0.7f, h, pa_hex(0x2E3450), pa_hex(0x2E3450), 0);
        break;
    case ST_CART: {
        box(c, -hl * 0.8f, -hw * 0.8f, 3.0f, hl * 0.8f, hw * 0.8f, 15.0f, p->col2, pa_hex(0xF4F6FF), 1);
        if (detail) for (int k = 0; k < 4; k += 2) if (g_face[k].vis)
            face_rect(c, &g_face[k], 0.1f, 0.9f, 0.6f, 0.85f, lit(pa_hex(0xFFFFFF), g_face[k].d));
        box(c, -0.8f, -0.8f, 15.0f, 0.8f, 0.8f, h - 4.0f, pa_hex(0xB0B6CA), pa_hex(0xB0B6CA), 0);
        pyramid(c, 0, 0, hl * 0.95f, 1.0f, h - 6.0f, h, p->col);
        if (detail) ldisc(c, 0.0f, 0.0f, h - 0.5f, hl * 0.3f, pa_hex(0xFFFFFF));
        break;
    }
    default: break;
    }
}

static void draw_pickup(PA_Canvas *c, const Prop *p) {
    if (p->state == PS_FALL) return;
    float bob = 9.0f + sinf(p->t * 3.0f) * 2.0f;
    PA_Vec2 b = P3(p->x, p->y, 0.0f);
    PA_Vec2 s = P3(p->x, p->y, bob);
    float k = V.cam_scale;
    soft_shadow(c, b.x, b.y, 6.0f * k, 3.0f * k, 0.22f);
    if (p->style == ST_COIN) {
        float w = fabsf(cosf(p->t * 2.6f));
        float rx = 7.0f * k * (0.2f + 0.8f * w), ry = 7.0f * k * HEIGHT_K;
        fellipse(c, s.x + 1.4f * k * (1.0f - w), s.y, rx + 1.0f * k * (1.0f - w), ry, pa_hex(0xC98A10));
        fellipse(c, s.x, s.y, rx * 0.86f, ry * 0.9f, pa_hex(0xFFC93C));
        if (w > 0.4f) fellipse(c, s.x - rx * 0.12f, s.y - ry * 0.1f, rx * 0.5f, ry * 0.55f, pa_hex(0xFFE88A));
    } else {
        float r = 7.5f * k;
        PA_Vec2 top[4] = { { s.x, s.y - r * 1.2f }, { s.x + r, s.y - r * 0.2f }, { s.x, s.y + r }, { s.x - r, s.y - r * 0.2f } };
        fpoly(c, top, 4, pa_hex(0xB13CF0));
        PA_Vec2 hi[3] = { { s.x, s.y - r * 1.2f }, { s.x - r, s.y - r * 0.2f }, { s.x, s.y } };
        fpoly(c, hi, 3, pa_hex(0xE2A6FF));
        PA_Vec2 lo[3] = { { s.x, s.y }, { s.x + r, s.y - r * 0.2f }, { s.x, s.y + r } };
        fpoly(c, lo, 3, pa_hex(0x7A1FB8));
    }
}

static void draw_prop(PA_Canvas *c, const Prop *p) {
    if (p->style == ST_COIN || p->style == ST_GEM) { draw_pickup(c, p); return; }
    xf_set(p->x, p->y, p->z, p->rot, p->tilt, p->tdir, 1.0f);
    X.clip = p->state == PS_TIP || p->state == PS_FALL;
    X.hole = p->eater < MAX_VOIDS ? p->eater : 0;
    if (p->style == ST_PERSON) draw_person(c, p);
    else if (p->style >= ST_CAR && p->style <= ST_TRUCK) draw_vehicle(c, p);
    else if (p->style >= ST_GARAGE) draw_building(c, p);
    else draw_small(c, p);
    X.clip = 0;
}

/* --------------------------------------------------------------- shadows */
static void hull_fill(PA_Canvas *c, PA_Vec2 *pts, int n, PA_Color col) {
    /* Monotone chain over a handful of points. */
    for (int i = 1; i < n; i++) {
        PA_Vec2 v = pts[i];
        int j = i - 1;
        while (j >= 0 && (pts[j].x > v.x || (pts[j].x == v.x && pts[j].y > v.y))) { pts[j + 1] = pts[j]; j--; }
        pts[j + 1] = v;
    }
    PA_Vec2 h[20];
    int k = 0;
    for (int i = 0; i < n; i++) {
        while (k >= 2 && (h[k - 1].x - h[k - 2].x) * (pts[i].y - h[k - 2].y) -
                         (h[k - 1].y - h[k - 2].y) * (pts[i].x - h[k - 2].x) <= 0.0f) k--;
        h[k++] = pts[i];
    }
    for (int i = n - 2, t = k + 1; i >= 0; i--) {
        while (k >= t && (h[k - 1].x - h[k - 2].x) * (pts[i].y - h[k - 2].y) -
                         (h[k - 1].y - h[k - 2].y) * (pts[i].x - h[k - 2].x) <= 0.0f) k--;
        h[k++] = pts[i];
    }
    if (k > 3) fpoly(c, h, k - 1, col);
}

static float g_shx, g_shy;   /* world shadow offset per unit of height */

/** Ground shadow for a standing prop: a cast shadow away from the key light
    for buildings, an occlusion pad under vehicles, a soft pool under the rest. */
static void draw_shadow(PA_Canvas *c, const Prop *p) {
    const PropType *t = &TYPES[p->style];
    if (p->style == ST_COIN || p->style == ST_GEM || p->state != PS_STAND) return;
    float s = V.cam_scale;
    static const float SX[4] = { -1, 1, 1, -1 }, SY[4] = { -1, -1, 1, 1 };
    float ca = cosf(p->rot), sa = sinf(p->rot);
    if (t->where == W_LOT) {
        float hl = t->len * 0.5f + 1.5f, hw = t->wid * 0.5f + 1.5f;
        PA_Vec2 q[8], base[4];
        float hh = t->h * (p->style == ST_SPIRE ? 0.75f : 1.0f);
        for (int k = 0; k < 4; k++) {
            float wx = p->x + (SX[k] * hl) * ca - (SY[k] * hw) * sa;
            float wy = p->y + (SX[k] * hl) * sa + (SY[k] * hw) * ca;
            q[k] = P3(wx, wy, 0.0f);
            q[k + 4] = P3(wx + g_shx * hh, wy + g_shy * hh, 0.0f);
        }
        hull_fill(c, q, 8, PA_RGBA(58, 70, 140, 78));
        if (s > 1.2f) {
            /* Occlusion skirt at the foot of the walls, only up close. */
            for (int k = 0; k < 4; k++) {
                float wx = p->x + (SX[k] * (hl + 4.0f)) * ca - (SY[k] * (hw + 4.0f)) * sa;
                float wy = p->y + (SX[k] * (hl + 4.0f)) * sa + (SY[k] * (hw + 4.0f)) * ca;
                base[k] = P3(wx, wy, 0.0f);
            }
            fpoly(c, base, 4, PA_RGBA(40, 44, 100, 50));
        }
        return;
    }
    if (p->style >= ST_CAR && p->style <= ST_TRUCK) {
        float hl = t->len * 0.5f + 2.0f, hw = t->wid * 0.5f + 2.0f;
        PA_Vec2 q[4];
        for (int k = 0; k < 4; k++)
            q[k] = P3(p->x + g_shx * 6.0f + SX[k] * hl * ca - SY[k] * hw * sa,
                      p->y + g_shy * 6.0f + SX[k] * hl * sa + SY[k] * hw * ca, 0.0f);
        fpoly(c, q, 4, PA_RGBA(40, 44, 100, 80));
        return;
    }
    PA_Vec2 g = P3(p->x + g_shx * t->h * 0.2f, p->y + g_shy * t->h * 0.2f, 0.0f);
    float e = ext_of(p->style) * (p->style == ST_TREE ? 0.8f : 1.0f);
    if (p->style == ST_LAMP) e = 4.0f;
    soft_shadow(c, g.x, g.y, e * s * 1.1f, e * s * 0.62f, p->style == ST_TREE ? 0.32f : 0.26f);
}

/* --------------------------------------------------------------- ground -- */
static const uint32_t C_ROAD = 0x6A76A8, C_WALK = 0xCDD3EA, C_KERB = 0x98A0C8, C_LOT = 0xDDE1F2,
                      C_GRASS = 0x7BD36A, C_PATH = 0xF3E3B5, C_WATER = 0x3FD0E0, C_PARK_LOT = 0x5E6A9C,
                      C_TILE = 0xE6E0F2;

static int on_screen_pt(PA_Canvas *c, PA_Vec2 p, float m) {
    return p.x > -m && p.x < (float)c->w + m && p.y > -m && p.y < (float)c->h + m;
}

static void ground_quad(PA_Canvas *c, float x0, float y0, float x1, float y1, float z, PA_Color col) {
    PA_Vec2 q[4] = { P3(x0, y0, z), P3(x1, y0, z), P3(x1, y1, z), P3(x0, y1, z) };
    fpoly(c, q, 4, col);
}

static void ground_strip(PA_Canvas *c, float x, float y, float hl, float hw, int vertical, float z, PA_Color col) {
    if (vertical) ground_quad(c, x - hw, y - hl, x + hw, y + hl, z, col);
    else ground_quad(c, x - hl, y - hw, x + hl, y + hw, z, col);
}

/** A rectangular ring as one polygon: outer contour, a bridge, the inner
    contour reversed. Non-zero winding cancels the bridge, and the ring costs
    one fill with no overdraw. */
static void ground_ring(PA_Canvas *c, float cx, float cy, float ho, float hi, float z, PA_Color col) {
    PA_Vec2 q[10] = {
        P3(cx - ho, cy - ho, z), P3(cx + ho, cy - ho, z), P3(cx + ho, cy + ho, z), P3(cx - ho, cy + ho, z),
        P3(cx - ho, cy - ho, z),
        P3(cx - hi, cy - hi, z), P3(cx - hi, cy + hi, z), P3(cx + hi, cy + hi, z), P3(cx + hi, cy - hi, z),
        P3(cx - hi, cy - hi, z) };
    fpoly(c, q, 10, col);
}

static void draw_ground(PA_Canvas *c) {
    float s = V.cam_scale;
    float ex = V.half_x + ROAD * 0.5f, ey = V.half_y + ROAD * 0.5f;
    float prom = 26.0f;

    /* Sea beyond the district, a promenade, foam at the wall - skipped when
       all four screen corners are over the city, since the water polygon
       spans the whole frame and costs a full-screen pass even when hidden. */
    int sea = 0;
    for (int k = 0; k < 4 && !sea; k++) {
        float sx = (k & 1) ? (float)c->w : 0.0f, sy = (k & 2) ? (float)c->h : 0.0f;
        float vx = (sx - (float)L.w * 0.5f) / s, vy = (sy - L.cy) / (s * GROUND_K);
        float wx = V.cam_x + vx * g_cr + vy * g_sr, wy = V.cam_y - vx * g_sr + vy * g_cr;
        if (fabsf(wx) > ex - 4.0f || fabsf(wy) > ey - 4.0f) sea = 1;
    }
    if (sea) {
        float big = 6000.0f, X0 = ex + prom, Y0 = ey + prom;
        PA_Vec2 q[10] = {
            P3(-big, -big, 0), P3(big, -big, 0), P3(big, big, 0), P3(-big, big, 0), P3(-big, -big, 0),
            P3(-X0, -Y0, 0), P3(-X0, Y0, 0), P3(X0, Y0, 0), P3(X0, -Y0, 0), P3(-X0, -Y0, 0) };
        fpoly(c, q, 10, pa_hex(C_WATER));
        ground_ring(c, 0, 0, X0 + 14.0f, X0, 0.0f, pa_hex(0x8FEAF2));
        /* Promenade is a rectangle, not a square: two passes of strips. */
        ground_quad(c, -X0, -Y0, X0, -ey, 0.0f, pa_hex(C_WALK));
        ground_quad(c, -X0, ey, X0, Y0, 0.0f, pa_hex(C_WALK));
        ground_quad(c, -X0, -ey, -ex, ey, 0.0f, pa_hex(C_WALK));
        ground_quad(c, ex, -ey, X0, ey, 0.0f, pa_hex(C_WALK));
        /* Sea wall faces toward the camera. */
        PA_Vec2 w1[4] = { P3(-X0, Y0, 0), P3(X0, Y0, 0), P3(X0, Y0, -14), P3(-X0, Y0, -14) };
        fpoly(c, w1, 4, pa_hex(0x9AA0C8));
        PA_Vec2 w2[4] = { P3(X0, -Y0, 0), P3(X0, Y0, 0), P3(X0, Y0, -14), P3(X0, -Y0, -14) };
        fpoly(c, w2, 4, pa_hex(0x7E86B4));
    }

    /* Blocks: raised sidewalk ring with a kerb, the lot inside it. */
    float cull = (SPAN * 0.7f) * s + 40.0f;
    for (int i = 0; i < V.block_count; i++) {
        const Block *b = &V.blocks[i];
        PA_Vec2 ctr = P3(b->x, b->y, 0.0f);
        if (!on_screen_pt(c, ctr, cull)) continue;
        float ho = b->half, hi = b->half - WALK;
        xf_set(b->x, b->y, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        X.clip = 0;
        /* Kerb faces only (the top is the ring and the lot). */
        {
            PA_Vec2 a = P3(b->x - ho, b->y + ho, 0), bb = P3(b->x + ho, b->y + ho, 0);
            PA_Vec2 a2 = P3(b->x - ho, b->y + ho, KERB), bb2 = P3(b->x + ho, b->y + ho, KERB);
            PA_Vec2 k1[4] = { a, bb, bb2, a2 };
            fpoly(c, k1, 4, pa_hex(C_KERB));
            PA_Vec2 d = P3(b->x + ho, b->y - ho, 0), d2 = P3(b->x + ho, b->y - ho, KERB);
            PA_Vec2 k2[4] = { d, bb, bb2, d2 };
            fpoly(c, k2, 4, pa_shade(pa_hex(C_KERB), -0.12f));
        }
        ground_ring(c, b->x, b->y, ho, hi, KERB, pa_hex(C_WALK));
        if (s > 1.5f) {
            /* Paving joints across the sidewalk. */
            for (float t = -ho + 30.0f; t < ho - 4.0f; t += 30.0f) {
                ground_strip(c, b->x + t, b->y - ho + WALK * 0.5f, WALK * 0.5f, 0.6f, 1, KERB, pa_hex(0xBCC3DF));
                ground_strip(c, b->x + t, b->y + ho - WALK * 0.5f, WALK * 0.5f, 0.6f, 1, KERB, pa_hex(0xBCC3DF));
                ground_strip(c, b->x - ho + WALK * 0.5f, b->y + t, WALK * 0.5f, 0.6f, 0, KERB, pa_hex(0xBCC3DF));
                ground_strip(c, b->x + ho - WALK * 0.5f, b->y + t, WALK * 0.5f, 0.6f, 0, KERB, pa_hex(0xBCC3DF));
            }
        }
        switch (b->kind) {
        case BK_PARK:
            ground_quad(c, b->x - hi, b->y - hi, b->x + hi, b->y + hi, KERB, pa_hex(C_GRASS));
            ground_quad(c, b->x - hi, b->y - 7.0f, b->x + hi, b->y + 7.0f, KERB, pa_hex(C_PATH));
            ground_quad(c, b->x - 7.0f, b->y - hi, b->x + 7.0f, b->y + hi, KERB, pa_hex(C_PATH));
            if (b->seed & 1) {
                PA_Vec2 pc = P3(b->x + hi * 0.5f, b->y + hi * 0.5f, KERB);
                fellipse(c, pc.x, pc.y, 26.0f * s, 26.0f * s * GROUND_K, pa_hex(0xE9DDB0));
                fellipse(c, pc.x, pc.y, 21.0f * s, 21.0f * s * GROUND_K, pa_hex(C_WATER));
                fellipse(c, pc.x - 5.0f * s, pc.y - 3.0f * s, 8.0f * s, 4.0f * s, pa_hex(0x9BF0F6));
            }
            break;
        case BK_PARKING:
            ground_quad(c, b->x - hi, b->y - hi, b->x + hi, b->y + hi, KERB, pa_hex(C_PARK_LOT));
            if (s > 0.5f) for (int k = 0; k <= 5; k++) {
                float x = b->x - hi + 4.5f + (float)k * 27.0f;
                ground_strip(c, x, b->y - 30.0f, 14.0f, 0.9f, 1, KERB, pa_hex(0xF2F4FF));
                ground_strip(c, x, b->y + 30.0f, 14.0f, 0.9f, 1, KERB, pa_hex(0xF2F4FF));
            }
            break;
        case BK_TOWER: case BK_PLAZA:
            ground_quad(c, b->x - hi, b->y - hi, b->x + hi, b->y + hi, KERB, pa_hex(C_TILE));
            if (s > 0.6f) for (int k = -2; k <= 2; k++) {
                ground_strip(c, b->x + (float)k * 26.0f, b->y, hi, 0.7f, 1, KERB, pa_hex(0xD4CDE6));
                ground_strip(c, b->x, b->y + (float)k * 26.0f, hi, 0.7f, 0, KERB, pa_hex(0xD4CDE6));
            }
            break;
        default:
            ground_quad(c, b->x - hi, b->y - hi, b->x + hi, b->y + hi, KERB, pa_hex(C_LOT));
            break;
        }
    }

    /* Lane dashes and crosswalks, culled per segment. */
    PA_Color white = PA_RGB(246, 247, 255);
    if (s > 0.42f) {
        for (int axis = 0; axis < 2; axis++) {
            int lines = axis == 0 ? V.cells_y + 1 : V.cells_x + 1;
            int segs = axis == 0 ? V.cells_x : V.cells_y;
            for (int i = 0; i < lines; i++) {
                float line = axis == 0 ? -V.half_y + SPAN * (float)i : -V.half_x + SPAN * (float)i;
                for (int g = 0; g < segs; g++) {
                    float mid = (axis == 0 ? -V.half_x : -V.half_y) + SPAN * ((float)g + 0.5f);
                    PA_Vec2 mp = axis == 0 ? P3(mid, line, 0) : P3(line, mid, 0);
                    if (!on_screen_pt(c, mp, SPAN * 0.6f * s)) continue;
                    float from = mid - (SPAN - ROAD) * 0.5f + 34.0f, to = mid + (SPAN - ROAD) * 0.5f - 30.0f;
                    for (float d = from; d < to; d += 32.0f) {
                        if (axis == 0) ground_strip(c, d + 8.0f, line, 8.0f, 1.3f, 0, 0.0f, white);
                        else ground_strip(c, line, d + 8.0f, 8.0f, 1.3f, 1, 0.0f, white);
                    }
                }
            }
        }
    }
    for (int j = 0; j <= V.cells_y; j++) {
        for (int i = 0; i <= V.cells_x; i++) {
            float jx = -V.half_x + SPAN * (float)i, jy = -V.half_y + SPAN * (float)j;
            if (!on_screen_pt(c, P3(jx, jy, 0), ROAD * 1.4f * s + 20.0f)) continue;
            for (int side = 0; side < 4; side++) {
                float dx = side == 0 ? 1.0f : side == 1 ? -1.0f : 0.0f;
                float dy = side == 2 ? 1.0f : side == 3 ? -1.0f : 0.0f;
                float ax = jx + dx * (ROAD * 0.5f + 13.0f), ay = jy + dy * (ROAD * 0.5f + 13.0f);
                if (fabsf(ax) > ex || fabsf(ay) > ey) continue;
                int n = s > 0.5f ? 6 : 3;
                for (int k = 0; k < n; k++) {
                    float o = -ROAD * 0.42f + ROAD * 0.84f * ((float)k + 0.5f) / (float)n;
                    float sx = ax + (dx == 0.0f ? o : 0.0f), sy = ay + (dy == 0.0f ? o : 0.0f);
                    ground_strip(c, sx, sy, 10.0f, ROAD * 0.42f / (float)n, dx == 0.0f, 0.0f, white);
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- holes -- */
static float rim_w(const Void *v) { return v->r * 0.24f + 1.5f; }

static float squash_k(const Void *v) {
    float k = 1.0f;
    if (v->squash > 0.0f) k += 0.07f * sinf(PA_PI * (1.0f - v->squash / 0.15f));
    return k;
}

static void hole_center(const Void *v, PA_Vec2 *ctr, float *lift_px) {
    float rw = rim_w(v);
    *ctr = P3(v->x, v->y, 0.0f);
    *lift_px = rw * 0.42f * V.cam_scale * HEIGHT_K;
}

static void build_opening(const Void *v, int index) {
    PA_Vec2 ctr; float lift;
    hole_center(v, &ctr, &lift);
    float k = squash_k(v);
    float rx = v->r * k * V.cam_scale, ry = rx * GROUND_K;
    for (int i = 0; i < OPEN_N; i++) {
        float a = PA_TAU * (float)i / (float)OPEN_N;
        g_open[index][i].x = ctr.x + cosf(a) * rx;
        g_open[index][i].y = ctr.y - lift + sinf(a) * ry;
    }
}

static void ellipse_pts(PA_Vec2 *out, int n, float cx, float cy, float rx, float ry, float a0, float a1) {
    for (int i = 0; i < n; i++) {
        float a = a0 + (a1 - a0) * (float)i / (float)(n - 1);
        out[i].x = cx + cosf(a) * rx;
        out[i].y = cy + sinf(a) * ry;
    }
}

static void draw_hole(PA_Canvas *c, const Void *v, int index) {
    const Skin *sk = &SKINS[v->skin];
    float s = V.cam_scale;
    float k = squash_k(v);
    float r = v->r * k, rw = rim_w(v) * (1.0f + (k - 1.0f) * 2.5f);
    float fade = v->dying > 0.0f ? pa_clamp01(v->dying / 0.45f) : 1.0f;
    r *= fade; rw *= fade;
    PA_Vec2 ctr; float lift;
    hole_center(v, &ctr, &lift);
    float ro = (r + rw) * s;
    if (ctr.x + ro < -20.0f || ctr.x - ro > (float)c->w + 20.0f || ctr.y + ro < -20.0f || ctr.y - ro - lift > (float)c->h + 20.0f)
        return;
    PA_Color top = pa_hex(sk->top), side = pa_hex(sk->side), light = pa_hex(sk->light);

    /* Contact shadow round the lip, then the lip's outer wall. */
    fellipse(c, ctr.x + ro * 0.04f, ctr.y + ro * 0.06f, ro * 1.08f, ro * GROUND_K * 1.1f, PA_RGBA(40, 44, 110, 50));
    fellipse(c, ctr.x, ctr.y, ro, ro * GROUND_K, side);
    /* Rim top: lit from above-left, a touch darker toward the camera. */
    float ty = ctr.y - lift;
    PA_Paint rp = pa_linear(ctr.x - ro, ty - ro * GROUND_K, ctr.x + ro * 0.5f, ty + ro * GROUND_K);
    pa_stop(&rp, 0.0f, pa_mix(top, light, 0.30f));
    pa_stop(&rp, 0.55f, top);
    pa_stop(&rp, 1.0f, pa_mix(top, side, 0.30f));
    fellipse_paint(c, ctr.x, ty, ro, ro * GROUND_K, &rp);
    /* Specular arc across the back-left of the lip. */
    {
        PA_Vec2 arc[28];
        float a0 = PA_PI * 1.08f, a1 = PA_PI * 1.62f;
        float r1 = (r + rw * 0.78f) * s, r2 = (r + rw * 0.48f) * s;
        ellipse_pts(arc, 14, ctr.x, ty, r1, r1 * GROUND_K, a0, a1);
        PA_Vec2 back[14];
        ellipse_pts(back, 14, ctr.x, ty, r2, r2 * GROUND_K, a1, a0);
        memcpy(arc + 14, back, sizeof(back));
        fpoly(c, arc, 28, pa_alpha(light, 0.75f));
    }

    /* The pit: the far inner wall catches a little of the rim colour, then a
       radial fall into black, then a slow swirl. */
    float rx = r * s, ry = rx * GROUND_K;
    PA_Paint wall = pa_linear(0, ty - ry, 0, ty + ry * 0.2f);
    pa_stop(&wall, 0.0f, pa_mix(side, pa_hex(0x0A0C2A), 0.25f));
    pa_stop(&wall, 1.0f, pa_hex(0x141838));
    fellipse_paint(c, ctr.x, ty, rx, ry, &wall);
    float drop = r * 0.42f * s * HEIGHT_K;
    {
        PA_Vec2 deep[28], clipped[64];
        ellipse_pts(deep, 28, ctr.x, ty + drop, rx, ry, 0.0f, PA_TAU * 27.0f / 28.0f);
        int m = clip_convex(deep, 28, g_open[index], OPEN_N, clipped);
        PA_Paint pit = pa_radial(ctr.x, ty + drop * 0.6f, rx * 0.15f, rx * 1.05f);
        pa_stop(&pit, 0.0f, pa_hex(0x000000));
        pa_stop(&pit, 0.55f, pa_hex(0x05061A));
        pa_stop(&pit, 1.0f, pa_mix(pa_hex(0x1A1F4A), side, 0.18f));
        if (m >= 3) fpoly_paint(c, clipped, m, &pit);
    }
    if (rx > 14.0f) {
        /* Swirl: three arms turning at half a revolution a second. */
        float ph = V.time * PA_PI * (index & 1 ? -1.0f : 1.0f);
        PA_Color arm = pa_alpha(pa_mix(pa_hex(0x2C3480), side, 0.35f), 0.55f);
        for (int a = 0; a < 3; a++) {
            PA_Vec2 pts[24], clipped[64];
            int n = 12;
            for (int i = 0; i < n; i++) {
                float t = (float)i / (float)(n - 1);
                float ang = ph + (float)a * PA_TAU / 3.0f + t * 2.6f;
                float rad = (0.12f + 0.84f * t) * rx;
                float w = 0.07f * rx * sinf(t * PA_PI) + 0.5f;
                float px = ctr.x + cosf(ang) * rad, py = ty + drop * 0.35f * (1.0f - t) + sinf(ang) * rad * GROUND_K;
                float nx = cosf(ang), ny = sinf(ang) * GROUND_K;
                pts[i].x = px - nx * w; pts[i].y = py - ny * w;
                pts[2 * n - 1 - i].x = px + nx * w; pts[2 * n - 1 - i].y = py + ny * w;
            }
            int m = clip_convex(pts, 2 * n, g_open[index], OPEN_N, clipped);
            if (m >= 3) fpoly(c, clipped, m, arm);
        }
    }
    /* Inner edge shadow under the far lip. */
    {
        PA_Vec2 arc[24];
        ellipse_pts(arc, 12, ctr.x, ty, rx, ry, PA_PI, PA_TAU);
        ellipse_pts(arc + 12, 12, ctr.x, ty + ry * 0.16f, rx * 0.97f, ry * 0.9f, PA_TAU, PA_PI);
        fpoly(c, arc, 24, PA_RGBA(0, 0, 10, 90));
    }

    if (v->grace > 0.0f && v->alive && V.phase == PH_PLAY) {
        float a = 0.35f + 0.25f * sinf(V.time * 8.0f);
        float r1 = (r + rw * 1.5f) * s, r2 = (r + rw * 1.2f) * s;
        PA_Vec2 ring[40];
        ellipse_pts(ring, 20, ctr.x, ty, r1, r1 * GROUND_K, 0.0f, PA_TAU);
        ellipse_pts(ring + 20, 20, ctr.x, ty, r2, r2 * GROUND_K, PA_TAU, 0.0f);
        fpoly(c, ring, 40, PA_RGBA(255, 255, 255, (int)(a * 255.0f)));
    }
}

/* ------------------------------------------------------------- effects -- */
static void star_shape(PA_Canvas *c, float x, float y, float r, float rot, PA_Color fill, PA_Color edge) {
    PA_Vec2 o[10], in[10];
    for (int i = 0; i < 10; i++) {
        float a = rot - PA_PI * 0.5f + PA_PI * (float)i / 5.0f;
        float rr = (i & 1) ? r * 0.48f : r;
        o[i].x = x + cosf(a) * (rr + 1.6f); o[i].y = y + sinf(a) * (rr + 1.6f);
        in[i].x = x + cosf(a) * rr; in[i].y = y + sinf(a) * rr;
    }
    fpoly(c, o, 10, edge);
    fpoly(c, in, 10, fill);
}

static void draw_parts(PA_Canvas *c) {
    float s = V.cam_scale;
    for (int i = 0; i < MAX_PARTS; i++) {
        const Part *q = &V.parts[i];
        if (q->life <= 0.0f) continue;
        float t = 1.0f - q->life / q->max;
        PA_Vec2 p = P3(q->x, q->y, q->z);
        switch (q->kind) {
        case PK_DEBRIS: {
            float r = q->size * s * (1.0f - t * 0.4f);
            if (r < 0.8f) break;
            PA_Vec2 d[4];
            for (int k = 0; k < 4; k++) {
                float a = q->rot + PA_PI * 0.5f * (float)k;
                d[k].x = p.x + cosf(a) * r; d[k].y = p.y + sinf(a) * r * 0.8f;
            }
            fpoly(c, d, 4, q->col);
            PA_Vec2 h[3] = { d[0], d[1], p };
            fpoly(c, h, 3, pa_shade(q->col, 0.25f));
            break;
        }
        case PK_DUST: {
            float r = q->size * s * (0.6f + t * 1.1f);
            fellipse(c, p.x, p.y, r, r * 0.8f, pa_alpha(q->col, 0.75f * (1.0f - t)));
            break;
        }
        case PK_STAR: {
            float r = (5.0f + 9.0f * (1.0f - t)) * (0.6f + s * 0.25f);
            if (t > 0.75f) r *= (1.0f - t) * 4.0f;
            star_shape(c, p.x, p.y, r, q->rot * 0.3f, q->col, pa_hex(0xC9761A));
            break;
        }
        case PK_SPARK: {
            float r = q->size * s * (1.0f - t);
            if (r < 0.8f) break;
            PA_Vec2 d[4] = { { p.x, p.y - r * 1.6f }, { p.x + r * 0.5f, p.y }, { p.x, p.y + r * 1.6f }, { p.x - r * 0.5f, p.y } };
            fpoly(c, d, 4, q->col);
            break;
        }
        case PK_RING: {
            /* The size-up shockwave: a white band racing out from the lip. */
            float rr = q->size * (1.25f + t * 1.0f) * s, w = (1.0f - t) * q->size * 0.16f * s + 1.0f;
            PA_Vec2 ring[48];
            ellipse_pts(ring, 24, p.x, p.y, rr + w, (rr + w) * GROUND_K, 0.0f, PA_TAU);
            ellipse_pts(ring + 24, 24, p.x, p.y, rr, rr * GROUND_K, PA_TAU, 0.0f);
            fpoly(c, ring, 48, PA_RGBA(255, 255, 255, (int)(200.0f * (1.0f - t))));
            break;
        }
        default: break;
        }
    }
}

static void skull_icon(PA_Canvas *c, float x, float y, float r, PA_Color col, PA_Color eye) {
    fcircle(c, x, y - r * 0.12f, r, col);
    rrect(c, x - r * 0.56f, y + r * 0.42f, r * 1.12f, r * 0.62f, r * 0.2f, col);
    fcircle(c, x - r * 0.38f, y - r * 0.06f, r * 0.27f, eye);
    fcircle(c, x + r * 0.38f, y - r * 0.06f, r * 0.27f, eye);
    PA_Vec2 n[3] = { { x, y + r * 0.22f }, { x - r * 0.12f, y + r * 0.42f }, { x + r * 0.12f, y + r * 0.42f } };
    fpoly(c, n, 3, eye);
}

static void coin_icon(PA_Canvas *c, float x, float y, float r) {
    fcircle(c, x, y + r * 0.12f, r, pa_hex(0xC98A10));
    fcircle(c, x, y, r, pa_hex(0xFFC93C));
    fcircle(c, x, y, r * 0.64f, pa_hex(0xFFDB5C));
    star_shape(c, x, y, r * 0.45f, 0.0f, pa_hex(0xFFF2B0), pa_hex(0xE8A820));
}

static void draw_pops(PA_Canvas *c) {
    char buf[24];
    for (int i = 0; i < MAX_POPS; i++) {
        const Pop *p = &V.pops[i];
        if (p->t <= 0.0f) continue;
        const Void *v = &V.voids[p->hole];
        PA_Vec2 s = P3(v->x, v->y, p->z);
        float age = 1.0f - p->t;
        float pop = age < 0.12f ? 0.6f + age / 0.12f * 0.55f : age < 0.22f ? 1.15f - (age - 0.12f) * 1.5f : 1.0f;
        if (p->t < 0.15f) pop *= p->t / 0.15f;
        /* Three steps of scale, not a continuous one: each size is a cached
           text image, and a smooth ramp would re-rasterise every frame. */
        pop = pop < 0.5f ? 0.0f : pop < 0.85f ? 0.7f : pop > 1.08f ? 1.15f : 1.0f;
        float size = (p->kind == 0 ? 26.0f + (float)((int)pa_clampf((float)p->value * 0.25f, 0.0f, 12.0f) / 4 * 4) : 26.0f) * pop;
        if (size < 6.0f) continue;
        if (p->kind == 2) {
            skull_icon(c, s.x - size * 0.6f, s.y, size * 0.5f, PA_RGB(255, 255, 255), pa_hex(0x2A1F5A));
            txtb(c, "+1", s.x + size * 0.1f, s.y - size * 0.5f, size, PA_RGB(255, 255, 255), pa_hex(0x2A1F5A),
                 PA_ALIGN_LEFT, 1.0f, 1.5f);
            continue;
        }
        snprintf(buf, sizeof(buf), "+%d", p->value);
        if (p->kind == 1) {
            s.x += v->r * V.cam_scale * 0.55f;
            s.y += v->r * V.cam_scale * 0.25f;
            coin_icon(c, s.x - size * 0.75f, s.y, size * 0.42f);
            txtb(c, buf, s.x - size * 0.2f, s.y - size * 0.5f, size, pa_hex(0xFFE36A), pa_hex(0x6A3A08),
                 PA_ALIGN_LEFT, 1.0f, 1.5f);
        } else {
            txtb(c, buf, s.x, s.y - size * 0.5f, size, pa_hex(0xFFD23A), pa_hex(0x5A2A08), PA_ALIGN_CENTER, 1.0f, 1.6f);
        }
    }
}

/* ---------------------------------------------------------- hole labels -- */
static void flag_badge(PA_Canvas *c, float x, float y, float r, const Skin *sk) {
    fcircle(c, x, y, r + 1.5f, pa_hex(0x0E1230));
    fcircle(c, x, y, r, pa_hex(sk->side));
    rrect(c, x - r, y - r * 0.34f, r * 2.0f, r * 0.68f, 1.0f, pa_hex(sk->light));
    fcircle(c, x - r * 0.3f, y - r * 0.38f, r * 0.28f, PA_RGBA(255, 255, 255, 140));
}

static void draw_label(PA_Canvas *c, const Void *v) {
    const Skin *sk = &SKINS[v->skin];
    PA_Vec2 ctr; float lift;
    hole_center(v, &ctr, &lift);
    float s = V.cam_scale;
    float topy = ctr.y - lift - (v->r + rim_w(v)) * s * GROUND_K;
    if (ctr.x < -60.0f || ctr.x > (float)c->w + 60.0f || topy < 40.0f || topy > (float)c->h + 80.0f) return;
    char buf[32];
    PA_Color ink = pa_hex(0x101536);
    if (v->player) {
        float pw = 128.0f, ph = 26.0f;
        float py = topy - ph - 12.0f;
        float px = ctr.x - pw * 0.5f;
        rrect(c, px - 2.0f, py - 2.0f, pw + 4.0f, ph + 4.0f, (ph + 4.0f) * 0.5f, ink);
        rrect(c, px, py, pw, ph, ph * 0.5f, pa_hex(0x2A3266));
        float prog = pa_clamp01((float)v->xp / (float)level_need(v->level));
        if (prog > 0.02f) {
            float fw = (pw - 4.0f) * prog;
            if (fw < ph - 4.0f) fw = ph - 4.0f;
            rrect(c, px + 2.0f, py + 2.0f, fw, ph - 4.0f, (ph - 4.0f) * 0.5f, pa_hex(0x4FB3FF));
            rrect(c, px + 6.0f, py + 4.0f, fw - 8.0f > 0.0f ? fw - 8.0f : 0.0f, 4.0f, 2.0f, PA_RGBA(255, 255, 255, 90));
        }
        flag_badge(c, px + 4.0f, py + ph * 0.5f, 13.0f, sk);
        snprintf(buf, sizeof(buf), "%d/%d", v->xp, level_need(v->level));
        txtb(c, buf, ctr.x + 8.0f, py + 5.0f, 16.0f, PA_RGB(255, 255, 255), ink, PA_ALIGN_CENTER, 1.0f, 1.2f);
        txtb(c, v->name, ctr.x, py - 31.0f, 21.0f, pa_hex(sk->label), ink, PA_ALIGN_CENTER, 1.0f, 1.0f);
        snprintf(buf, sizeof(buf), "LVL %d", v->level);
        txtb(c, buf, ctr.x, py - 62.0f, 19.0f, pa_hex(sk->label), ink, PA_ALIGN_CENTER, 1.0f, 1.0f);
        if (v->streak > 1 && v->streak_t > 0.0f) {
            int n = v->streak > 6 ? 6 : v->streak;
            for (int k = 0; k < n; k++)
                skull_icon(c, ctr.x + ((float)k - (float)(n - 1) * 0.5f) * 24.0f, py - 82.0f, 9.0f,
                           PA_RGB(255, 255, 255), ink);
        }
    } else {
        float y = topy - 30.0f;
        /* Never print over the player's own tag: drop below the hole instead. */
        const Void *me = &V.voids[0];
        if (me->alive) {
            PA_Vec2 mc; float ml;
            hole_center(me, &mc, &ml);
            float my = mc.y - ml - (me->r + rim_w(me)) * s * GROUND_K - 70.0f;
            if (fabsf(ctr.x - mc.x) < 170.0f && fabsf(y - my) < 90.0f)
                y = ctr.y + (v->r + rim_w(v)) * s * GROUND_K + 34.0f;
        }
        txtb(c, v->name, ctr.x, y, 19.0f, pa_hex(sk->label), ink, PA_ALIGN_CENTER, 1.0f, 1.0f);
        snprintf(buf, sizeof(buf), "LVL %d", v->level);
        txtb(c, buf, ctr.x, y - 28.0f, 18.0f, pa_hex(sk->label), ink, PA_ALIGN_CENTER, 1.0f, 1.0f);
    }
}

static void draw_arrows(PA_Canvas *c, float top) {
    const Void *me = &V.voids[0];
    PA_Vec2 ms = P3(me->x, me->y, 0.0f);
    char buf[32];
    float px_[MAX_VOIDS], py_[MAX_VOIDS];
    int placed = 0;
    for (int j = 1; j < V.void_count; j++) {
        const Void *rv = &V.voids[j];
        if (!rv->alive || rv->dying > 0.0f) continue;
        PA_Vec2 sp = P3(rv->x, rv->y, 0.0f);
        if (sp.x > -10.0f && sp.x < (float)c->w + 10.0f && sp.y > top && sp.y < (float)c->h + 10.0f) continue;
        float ang = atan2f(sp.y - ms.y, sp.x - ms.x);
        float ca = cosf(ang), sa = sinf(ang);
        /* Push out to the screen edge along the bearing. */
        float tx = ca > 0.0f ? ((float)c->w - 30.0f - ms.x) / ca : ca < 0.0f ? (30.0f - ms.x) / ca : 1e9f;
        float ty = sa > 0.0f ? ((float)c->h - 40.0f - ms.y) / sa : sa < 0.0f ? (top + 30.0f - ms.y) / sa : 1e9f;
        float t = tx < ty ? tx : ty;
        float ex = ms.x + ca * t, ey = ms.y + sa * t;
        /* Two rivals off the same edge would print over each other. */
        for (int k = 0; k < placed; k++) {
            if (fabsf(ex - px_[k]) < 60.0f && fabsf(ey - py_[k]) < 46.0f) {
                if (fabsf(ca) > fabsf(sa)) ey = py_[k] + (ey >= py_[k] ? 48.0f : -48.0f);
                else ex = px_[k] + (ex >= px_[k] ? 110.0f : -110.0f);
            }
        }
        if (placed < MAX_VOIDS) { px_[placed] = ex; py_[placed] = ey; placed++; }
        int danger = level_radius(rv->level) >= level_radius(me->level) * 1.38f;
        const Skin *sk = &SKINS[rv->skin];
        PA_Color col = pa_hex(sk->top);
        PA_Vec2 tri[3] = { { ex + ca * 16.0f, ey + sa * 16.0f }, { ex + cosf(ang + 2.3f) * 14.0f, ey + sinf(ang + 2.3f) * 14.0f },
                           { ex + cosf(ang - 2.3f) * 14.0f, ey + sinf(ang - 2.3f) * 14.0f } };
        PA_Vec2 tro[3];
        for (int k = 0; k < 3; k++) { tro[k].x = ex + (tri[k].x - ex) * 1.3f; tro[k].y = ey + (tri[k].y - ey) * 1.3f; }
        fpoly(c, tro, 3, pa_hex(0x101536));
        fpoly(c, tri, 3, col);
        float lx = ex - ca * 30.0f, ly = ey - sa * 30.0f - 22.0f;
        PA_Align al = ex < (float)c->w * 0.3f ? PA_ALIGN_LEFT : ex > (float)c->w * 0.7f ? PA_ALIGN_RIGHT : PA_ALIGN_CENTER;
        if (al == PA_ALIGN_LEFT) lx = ex + 4.0f;
        if (al == PA_ALIGN_RIGHT) lx = ex - 4.0f;
        snprintf(buf, sizeof(buf), "LVL %d", rv->level);
        txtb(c, buf, lx, ly - 4.0f, 19.0f, danger ? pa_hex(0xFF6B7A) : pa_hex(sk->label), pa_hex(0x101536), al, 1.0f, 1.6f);
        txtb(c, rv->name, lx, ly + 18.0f, 19.0f, pa_hex(sk->label), pa_hex(0x101536), al, 1.0f, 1.6f);
    }
}

/* ------------------------------------------------------------------- HUD */
static float hud_top(PA_Canvas *c) { return (float)c->h * 0.014f + 8.0f; }

static void clock_icon(PA_Canvas *c, float x, float y, float r) {
    fcircle(c, x - r * 0.62f, y - r * 0.78f, r * 0.34f, pa_hex(0xE07A10));
    fcircle(c, x + r * 0.62f, y - r * 0.78f, r * 0.34f, pa_hex(0xE07A10));
    fcircle(c, x, y + r * 0.08f, r * 1.04f, pa_hex(0x7A3A08));
    fcircle(c, x, y, r, pa_hex(0xFF9A1E));
    fcircle(c, x, y, r * 0.72f, pa_hex(0xFFF6E6));
    pa_line(c, x, y, x, y - r * 0.5f, 2.6f, pa_hex(0x3A2410));
    pa_line(c, x, y, x + r * 0.36f, y + r * 0.1f, 2.6f, pa_hex(0x3A2410));
}

static float share(const Void *v) { return 100.0f * (float)v->score / (float)V.total_value; }

static void progress_bar(PA_Canvas *c, float cx, float y, float w, float h) {
    PA_Color ink = pa_hex(0x0B0F2A);
    float pulse = V.bar_pulse > 0.0f ? V.bar_pulse : 0.0f;
    rrect(c, cx - w * 0.5f - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f, (h + 6.0f) * 0.5f, ink);
    rrect(c, cx - w * 0.5f, y, w, h, h * 0.5f, pa_hex(0x1A2050));
    float pct = share(&V.voids[0]);
    float f = pa_clamp01(pct / 100.0f);
    float fw = (w - 6.0f) * f;
    if (fw > 0.5f) {
        if (fw < h - 6.0f) fw = h - 6.0f;
        PA_Paint g = pa_linear(0, y + 3.0f, 0, y + h - 3.0f);
        pa_stop(&g, 0.0f, pa_mix(pa_hex(0xFFE066), pa_hex(0xFFFFFF), pulse * 0.5f));
        pa_stop(&g, 0.5f, pa_hex(0xFFC400));
        pa_stop(&g, 1.0f, pa_hex(0xF0A000));
        rrect_paint(c, cx - w * 0.5f + 3.0f, y + 3.0f, fw, h - 6.0f, (h - 6.0f) * 0.5f, &g);
        rrect(c, cx - w * 0.5f + 8.0f, y + 6.0f, fw - 10.0f > 0.0f ? fw - 10.0f : 0.0f, (h - 6.0f) * 0.22f,
              (h - 6.0f) * 0.11f, PA_RGBA(255, 255, 255, 110));
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f%%", (double)pct);
    float ts = h * 0.62f * (1.0f + pulse * 0.12f);
    float tx = cx - w * 0.5f + 3.0f + fw + 10.0f;
    if (tx > cx + w * 0.5f - 90.0f) tx = cx - w * 0.5f + 16.0f;
    txtb(c, buf, tx, y + (h - ts) * 0.5f, ts, PA_RGB(255, 255, 255), ink, PA_ALIGN_LEFT, 1.0f, 1.5f);
}

static void draw_hud(PA_Canvas *c) {
    char buf[32];
    float W = (float)c->w;
    float top = hud_top(c);
    PA_Color ink = pa_hex(0x0B0F2A);
    float ph = 50.0f;
    pa_hub_pause_anchor(38.0f, top + ph * 0.5f, 22.0f);

    /* Timer pill with the alarm clock riding its left end. */
    int secs = (int)ceilf(V.timer);
    snprintf(buf, sizeof(buf), "%02d:%02d", secs / 60, secs % 60);
    float pw = 168.0f, px = W * 0.5f - pw * 0.5f + 14.0f;
    rrect(c, px, top + 2.0f, pw, ph - 4.0f, (ph - 4.0f) * 0.5f, PA_RGBA(20, 26, 70, 190));
    clock_icon(c, px + 4.0f, top + ph * 0.5f, 21.0f);
    int hurry = V.timer <= 10.0f && V.phase == PH_PLAY;
    float tsz = 28.0f * (hurry ? 1.0f + 0.12f * (V.timer - floorf(V.timer)) : 1.0f);
    txtb(c, buf, px + pw * 0.5f + 14.0f, top + ph * 0.5f - tsz * 0.5f, tsz,
         hurry ? pa_hex(0xFF5C70) : PA_RGB(255, 255, 255), ink, PA_ALIGN_CENTER, 1.5f, 1.6f);

    /* Kill counter. */
    float kw = 84.0f, kx = W - kw - 14.0f;
    float bump = V.kill_bump > 0.0f ? V.kill_bump : 0.0f;
    rrect(c, kx, top + 2.0f, kw, ph - 4.0f, (ph - 4.0f) * 0.5f, PA_RGBA(20, 26, 70, 190));
    skull_icon(c, kx + 24.0f, top + ph * 0.5f - 2.0f, 11.0f * (1.0f + bump * 0.3f), PA_RGB(255, 255, 255), pa_hex(0x1A2050));
    snprintf(buf, sizeof(buf), "%d", V.voids[0].kills);
    float ks = 28.0f * (1.0f + bump * 0.25f);
    txtb(c, buf, kx + 58.0f, top + ph * 0.5f - ks * 0.5f, ks, PA_RGB(255, 255, 255), ink, PA_ALIGN_CENTER, 1.0f, 1.6f);

    /* Progress: the player's share of the district. */
    float bw = W * 0.78f > 480.0f ? 480.0f : W * 0.78f, bh = pa_clampf((float)c->h * 0.04f, 34.0f, 48.0f);
    progress_bar(c, W * 0.5f, top + ph + 14.0f, bw, bh);

    /* Stars streaming into the bar. */
    float target_y = top + ph + 14.0f + bh * 0.5f;
    float f = pa_clamp01(share(&V.voids[0]) / 100.0f);
    float target_x = W * 0.5f - bw * 0.5f + 6.0f + (bw - 12.0f) * f;
    for (int i = 0; i < MAX_FLY; i++) {
        const Fly *fl = &V.fly[i];
        if (!fl->live) continue;
        float t = pa_clamp01(fl->t / fl->dur);
        float e = t * t * (3.0f - 2.0f * t);
        float mx = (fl->sx + target_x) * 0.5f + fl->bend, my = (fl->sy + target_y) * 0.5f;
        float a = (1.0f - e) * (1.0f - e), b = 2.0f * (1.0f - e) * e, d = e * e;
        float x = a * fl->sx + b * mx + d * target_x, y = a * fl->sy + b * my + d * target_y;
        star_shape(c, x, y, 11.0f * (1.0f - t * 0.4f), t * 6.0f, pa_hex(0xFFD23A), pa_hex(0xC9761A));
    }
}

static void banner_text(PA_Canvas *c, const char *s, float y, float size, PA_Color fill, float t) {
    float pop = t < 0.12f ? 0.5f + t / 0.12f * 0.7f : t < 0.25f ? 1.2f - (t - 0.12f) * 1.5f : 1.0f;
    txtb(c, s, (float)c->w * 0.5f, y - size * pop * 0.5f, size * pop, fill, pa_hex(0x1A1240), PA_ALIGN_CENTER, 2.0f, 1.9f);
}

/* ----------------------------------------------------------- menu screen */
static void hand_icon(PA_Canvas *c, float x, float y, float s) {
    PA_Color skin = pa_hex(0xFFE0C8), edge = pa_hex(0x2A1F5A);
    rrect(c, x - 3.0f * s - 2.0f, y - 22.0f * s - 2.0f, 6.0f * s + 4.0f, 26.0f * s, 3.0f * s + 2.0f, edge);
    rrect(c, x - 9.0f * s - 2.0f, y - 2.0f, 22.0f * s + 4.0f, 20.0f * s + 4.0f, 8.0f * s, edge);
    rrect(c, x - 3.0f * s, y - 22.0f * s, 6.0f * s, 26.0f * s, 3.0f * s, skin);
    rrect(c, x - 9.0f * s, y, 22.0f * s, 20.0f * s, 7.0f * s, skin);
}

static void draw_menu(PA_Canvas *c) {
    float W = (float)c->w, H = (float)c->h;
    pa_hub_pause_anchor(38.0f, hud_top(c) + 25.0f, 22.0f);
    char buf[48];
    float t = V.phase_t;
    float bob = sinf(t * 2.2f) * 4.0f;
    PA_Paint sc = pa_linear(0, 0, 0, H * 0.30f);
    pa_stop(&sc, 0.0f, PA_RGBA(40, 30, 110, 170));
    pa_stop(&sc, 1.0f, PA_RGBA(40, 30, 110, 0));
    pa_fill_rect_paint(c, 0, 0, W, H * 0.30f, &sc);
    float ty = H * 0.075f + bob;
    if (W > H) {
        /* Landscape: the title sits in the left margin, clear of the hole. */
        float k = pa_clampf(H / 1000.0f, 0.55f, 1.0f);
        txtb(c, "VOID", W * 0.2f, H * 0.12f, 62.0f * k, pa_hex(0xFFD23A), pa_hex(0x3A2380), PA_ALIGN_CENTER, 5.0f, 1.3f);
        txtb(c, "MUNCHER", W * 0.2f, H * 0.12f + 84.0f * k, 62.0f * k, pa_hex(0xFFD23A), pa_hex(0x3A2380),
             PA_ALIGN_CENTER, 5.0f, 1.3f);
        txtb(c, "DRAG TO EAT THE CITY", W * 0.5f, H * 0.88f, 24.0f, PA_RGB(255, 255, 255), pa_hex(0x1A1240),
             PA_ALIGN_CENTER, 1.5f, 1.6f);
        coin_icon(c, W - 112.0f, hud_top(c) + 24.0f, 13.0f);
        snprintf(buf, sizeof(buf), "%d", g_coins < 0 ? 0 : g_coins);
        txtb(c, buf, W - 92.0f, hud_top(c) + 12.0f, 24.0f, PA_RGB(255, 255, 255), pa_hex(0x0B0F2A), PA_ALIGN_LEFT, 1.0f, 1.5f);
        return;
    }
    txtb(c, "VOID", W * 0.5f, ty, 62.0f, pa_hex(0xFFD23A), pa_hex(0x3A2380), PA_ALIGN_CENTER, 5.0f, 1.3f);
    txtb(c, "MUNCHER", W * 0.5f, ty + 84.0f, 62.0f, pa_hex(0xFFD23A), pa_hex(0x3A2380), PA_ALIGN_CENTER, 5.0f, 1.3f);
    /* Mode chip. */
    snprintf(buf, sizeof(buf), "CITY %d  -  CLASSIC 2:00", V.level);
    float cw = pa_text_width(buf, 17.0f, 1.0f) + 40.0f;
    rrect(c, W * 0.5f - cw * 0.5f, ty + 172.0f, cw, 34.0f, 17.0f, PA_RGBA(20, 26, 70, 200));
    txtb(c, buf, W * 0.5f, ty + 180.0f, 17.0f, PA_RGB(255, 255, 255), pa_hex(0x0B0F2A), PA_ALIGN_CENTER, 1.0f, 1.2f);

    /* Wallet and best. */
    coin_icon(c, W - 112.0f, hud_top(c) + 24.0f, 13.0f);
    snprintf(buf, sizeof(buf), "%d", g_coins < 0 ? 0 : g_coins);
    txtb(c, buf, W - 92.0f, hud_top(c) + 12.0f, 24.0f, PA_RGB(255, 255, 255), pa_hex(0x0B0F2A), PA_ALIGN_LEFT, 1.0f, 1.5f);

    /* Drag hint: a hand tracing a figure of eight under the hole. */
    float hx = W * 0.5f + sinf(t * 2.0f) * W * 0.16f, hy = H * 0.80f + sinf(t * 4.0f) * 18.0f;
    PA_Vec2 trail[33];
    for (int i = 0; i <= 32; i++) {
        float a = PA_TAU * (float)i / 32.0f;
        trail[i].x = W * 0.5f + sinf(a) * W * 0.16f;
        trail[i].y = H * 0.80f + sinf(a * 2.0f) * 18.0f + 6.0f;
    }
    pa_stroke_poly(c, trail, 33, 1, 7.0f, PA_RGBA(255, 255, 255, 120));
    hand_icon(c, hx + 8.0f, hy + 10.0f, 1.5f);
    txtb(c, "DRAG TO EAT THE CITY", W * 0.5f, H * 0.87f, 24.0f, PA_RGB(255, 255, 255), pa_hex(0x1A1240),
         PA_ALIGN_CENTER, 1.5f, 1.6f);
    if (g_best_pct > 0) {
        snprintf(buf, sizeof(buf), "BEST %.1f%%", (double)g_best_pct / 10.0);
        txtb(c, buf, W * 0.5f, H * 0.87f + 36.0f, 18.0f, pa_hex(0xFFD23A), pa_hex(0x1A1240), PA_ALIGN_CENTER, 1.5f, 1.3f);
    }
}

/* -------------------------------------------------------- results screen */
static struct { float x, y, w, h; } g_btn;

static int results_button_hit(float x, float y) {
    if (g_btn.w <= 0.0f) return 1;
    return x >= g_btn.x - 10.0f && x <= g_btn.x + g_btn.w + 10.0f && y >= g_btn.y - 10.0f && y <= g_btn.y + g_btn.h + 10.0f;
}

static void hole_icon(PA_Canvas *c, float x, float y, float r, const Skin *sk) {
    fellipse(c, x, y + r * 0.18f, r, r * 0.74f, pa_hex(sk->side));
    fellipse(c, x, y, r, r * 0.74f, pa_hex(sk->top));
    fellipse(c, x - r * 0.1f, y - r * 0.12f, r * 0.8f, r * 0.52f, pa_mix(pa_hex(sk->top), pa_hex(sk->light), 0.5f));
    fellipse(c, x, y, r * 0.66f, r * 0.46f, pa_hex(0x05061A));
}

static void crown(PA_Canvas *c, float x, float y, float s) {
    PA_Vec2 q[7] = { { x - 16 * s, y + 10 * s }, { x - 18 * s, y - 8 * s }, { x - 8 * s, y + 1 * s }, { x, y - 13 * s },
                     { x + 8 * s, y + 1 * s }, { x + 18 * s, y - 8 * s }, { x + 16 * s, y + 10 * s } };
    PA_Vec2 o[7];
    for (int i = 0; i < 7; i++) { o[i].x = x + (q[i].x - x) * 1.18f; o[i].y = y + (q[i].y - y) * 1.18f + 1.0f; }
    fpoly(c, o, 7, pa_hex(0x6A3A08));
    fpoly(c, q, 7, pa_hex(0xFFC93C));
    fcircle(c, x, y - 13 * s, 3.2f * s, pa_hex(0xFF5C78));
    fcircle(c, x - 18 * s, y - 8 * s, 2.6f * s, pa_hex(0x4FB3FF));
    fcircle(c, x + 18 * s, y - 8 * s, 2.6f * s, pa_hex(0x4FB3FF));
}

static void draw_results(PA_Canvas *c) {
    float W = (float)c->w, H = (float)c->h;
    float t = V.phase_t;
    float a = pa_clamp01(t * 3.0f);
    char buf[48];
    PA_Color ink = pa_hex(0x1A1240);
    pa_fill_rect(c, 0, 0, W, H, PA_RGBA(22, 18, 70, (int)(a * 175.0f)));
    pa_hub_hide_pause();

    float slide = (1.0f - pa_smooth(pa_clamp01(t * 2.5f))) * H * 0.4f;
    float cw = W * 0.88f > 470.0f ? 470.0f : W * 0.88f;
    float cx = W * 0.5f - cw * 0.5f;
    float rows = (float)V.void_count;
    float ch = 96.0f + rows * 66.0f + 88.0f;
    float cy = H * 0.5f - ch * 0.5f + 40.0f + slide;

    /* Placement headline with a crown for the win. */
    static const char *ord[] = { "1ST", "2ND", "3RD", "4TH", "5TH" };
    float hy = cy - 140.0f;
    if (V.place == 1) crown(c, W * 0.5f, hy - 4.0f, 2.2f);
    snprintf(buf, sizeof(buf), "%s PLACE!", ord[V.place - 1 < 5 ? V.place - 1 : 4]);
    float pop = t < 0.35f ? 0.6f + t / 0.35f * 0.55f : t < 0.5f ? 1.15f - (t - 0.35f) : 1.0f;
    txtb(c, buf, W * 0.5f, hy + 36.0f - 22.0f * (pop - 1.0f), 50.0f * pop,
         V.place == 1 ? pa_hex(0xFFD23A) : PA_RGB(255, 255, 255), ink, PA_ALIGN_CENTER, 3.0f, 2.1f);

    /* Card. */
    rrect(c, cx, cy + 8.0f, cw, ch, 28.0f, PA_RGBA(10, 8, 40, 120));
    rrect(c, cx, cy, cw, ch, 28.0f, pa_hex(0xFFFFFF));
    rrect(c, cx, cy, cw, 64.0f, 28.0f, pa_hex(0x5B4BE0));
    pa_fill_rect(c, cx, cy + 40.0f, cw, 24.0f, pa_hex(0x5B4BE0));
    txtb(c, "RANKING", W * 0.5f, cy + 18.0f, 28.0f, PA_RGB(255, 255, 255), ink, PA_ALIGN_CENTER, 3.0f, 1.6f);

    for (int i = 0; i < V.void_count; i++) {
        if (t < 0.35f + 0.1f * (float)i) break;
        const Void *v = &V.voids[V.order[i]];
        const Skin *sk = &SKINS[v->skin];
        float ry = cy + 80.0f + 66.0f * (float)i;
        if (v->player) rrect(c, cx + 10.0f, ry - 4.0f, cw - 20.0f, 60.0f, 18.0f, pa_hex(0xFFF1B8));
        static const uint32_t medal[] = { 0xFFC93C, 0xC9D2E8, 0xE89A5C, 0xB8BED8, 0xB8BED8 };
        fcircle(c, cx + 40.0f, ry + 26.0f, 18.0f, pa_shade(pa_hex(medal[i]), -0.3f));
        fcircle(c, cx + 40.0f, ry + 24.0f, 18.0f, pa_hex(medal[i]));
        snprintf(buf, sizeof(buf), "%d", i + 1);
        txtb(c, buf, cx + 40.0f, ry + 14.0f, 20.0f, PA_RGB(255, 255, 255), pa_shade(pa_hex(medal[i]), -0.5f),
             PA_ALIGN_CENTER, 1.0f, 1.6f);
        hole_icon(c, cx + 90.0f, ry + 26.0f, 20.0f, sk);
        txtb(c, v->name, cx + 122.0f, ry + 6.0f, 21.0f, v->player ? pa_hex(0x2F7BEA) : pa_hex(0x2A2F55),
             PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 1.0f, 1.0f);
        snprintf(buf, sizeof(buf), "LVL %d   %d KILLS", v->level, v->kills);
        txt(c, buf, cx + 122.0f, ry + 34.0f, 14.0f, pa_hex(0x8A8FB0), PA_ALIGN_LEFT, 1.0f);
        snprintf(buf, sizeof(buf), "%.1f%%", (double)share(v));
        txtb(c, buf, cx + cw - 22.0f, ry + 12.0f, 26.0f, i == 0 ? pa_hex(0xFFB800) : pa_hex(0x2A2F55),
             i == 0 ? ink : PA_RGB(255, 255, 255), PA_ALIGN_RIGHT, 1.0f, i == 0 ? 2.0f : 1.0f);
        if (i == 0) crown(c, cx + 90.0f, ry + 2.0f, 0.55f);
    }

    /* Round stats. */
    float sy = cy + 80.0f + 66.0f * rows + 12.0f;
    pa_fill_rect(c, cx + 24.0f, sy - 6.0f, cw - 48.0f, 2.0f, pa_hex(0xE4E6F2));
    skull_icon(c, cx + 48.0f, sy + 26.0f, 11.0f, pa_hex(0x2A2F55), pa_hex(0xFFFFFF));
    snprintf(buf, sizeof(buf), "%d", V.voids[0].kills);
    txtb(c, buf, cx + 66.0f, sy + 14.0f, 24.0f, pa_hex(0x2A2F55), PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 1.0f, 1.0f);
    coin_icon(c, cx + cw * 0.5f - 30.0f, sy + 26.0f, 13.0f);
    snprintf(buf, sizeof(buf), "+%d", V.coins_round);
    txtb(c, buf, cx + cw * 0.5f - 12.0f, sy + 14.0f, 24.0f, pa_hex(0xE89A10), PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 1.0f, 1.0f);
    snprintf(buf, sizeof(buf), V.new_best ? "NEW BEST!" : "BEST %.1f%%", (double)g_best_pct / 10.0);
    txtb(c, buf, cx + cw - 22.0f, sy + 17.0f, 18.0f, V.new_best ? pa_hex(0xFF5C78) : pa_hex(0x8A8FB0),
         PA_RGB(255, 255, 255), PA_ALIGN_RIGHT, 1.0f, 1.0f);

    /* Continue button. */
    float bw = cw * 0.72f, bh = 72.0f;
    float bx = W * 0.5f - bw * 0.5f, by = cy + ch + 34.0f;
    float pulse = 1.0f + 0.03f * sinf(t * 5.0f);
    g_btn.x = bx; g_btn.y = by; g_btn.w = bw; g_btn.h = bh;
    if (t > 0.8f) {
        float pw = bw * pulse, px = W * 0.5f - pw * 0.5f;
        rrect(c, px, by + 7.0f, pw, bh, bh * 0.5f, pa_hex(0x1E8A44));
        rrect(c, px, by, pw, bh, bh * 0.5f, pa_hex(0x3DCB6C));
        rrect(c, px + 16.0f, by + 8.0f, pw - 32.0f, bh * 0.3f, bh * 0.15f, PA_RGBA(255, 255, 255, 70));
        txtb(c, V.place == 1 ? "NEXT CITY" : "PLAY AGAIN", W * 0.5f, by + bh * 0.5f - 15.0f, 30.0f,
             PA_RGB(255, 255, 255), pa_hex(0x14602E), PA_ALIGN_CENTER, 2.0f, 1.6f);
    }
}

/* --------------------------------------------------------------- render -- */
#define BUCKETS 256
static int g_head[BUCKETS], g_next[MAX_PROPS];

static void muncher_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) layout(c->w, c->h);
    setup_view();
    COOL = pa_hex(0x4A5890);
    WARM = pa_hex(0xFFE9C4);
    {
        /* Shadow direction: away from the light, in world units per height. */
        float vx = 0.52f * 0.42f, vy = -0.30f * 0.42f;
        g_shx = vx * g_cr + vy * g_sr;
        g_shy = -vx * g_sr + vy * g_cr;
    }
    float sh_x = 0.0f, sh_y = 0.0f;
    if (V.shake > 0.0f) {
        sh_x = sinf(V.time * 61.0f) * V.shake * 7.0f;
        sh_y = cosf(V.time * 53.0f) * V.shake * 5.0f;
    }
    float save_cy = L.cy;
    int save_w = L.w;
    L.cy += sh_y;
    (void)save_w;
    float cam_x = V.cam_x;
    V.cam_x -= sh_x / (V.cam_scale > 0.1f ? V.cam_scale : 0.1f) * g_cr;

    pa_clear(c, pa_hex(C_ROAD));
    draw_ground(c);

    /* Visible props, bucketed by depth. */
    float s = V.cam_scale;
    for (int i = 0; i < BUCKETS; i++) g_head[i] = -1;
    float vy_lo = 1e9f, vy_hi = -1e9f;
    {
        /* Depth range of the screen at ground level, with room below for
           tall things whose bases are off the bottom edge. */
        float top_vy = (-L.cy) / (s * GROUND_K);
        float bot_vy = ((float)c->h + 320.0f * s * HEIGHT_K - L.cy) / (s * GROUND_K);
        float cvy = V.cam_x * g_sr + V.cam_y * g_cr;
        vy_lo = cvy + top_vy - 80.0f;
        vy_hi = cvy + bot_vy + 80.0f;
    }
    float span = vy_hi - vy_lo;
    for (int i = 0; i < V.void_count; i++) if (V.voids[i].alive) build_opening(&V.voids[i], i);
    for (int i = V.prop_count - 1; i >= 0; i--) {
        const Prop *p = &V.props[i];
        if (p->state == PS_GONE) continue;
        float pvy = p->x * g_sr + p->y * g_cr;
        if (pvy < vy_lo || pvy > vy_hi) continue;
        PA_Vec2 b = P3(p->x, p->y, 0.0f);
        float e = ext_of(p->style) * s + 6.0f;
        if (b.x + e < 0.0f || b.x - e > (float)c->w) continue;
        if (b.y - TYPES[p->style].h * s * HEIGHT_K - e > (float)c->h + 40.0f) continue;
        if (b.y + e < -10.0f && p->state != PS_FALL) continue;
        int bk = (int)((pvy - vy_lo) / span * (float)(BUCKETS - 1));
        if (bk < 0) bk = 0;
        if (bk >= BUCKETS) bk = BUCKETS - 1;
        g_next[i] = g_head[bk];
        g_head[bk] = i;
    }

    /* Shadows go down first so nothing stands in another's shadow wrongly. */
    for (int b = 0; b < BUCKETS; b++)
        for (int i = g_head[b]; i >= 0; i = g_next[i]) draw_shadow(c, &V.props[i]);

    for (int j = 0; j < V.void_count; j++)
        if (V.voids[j].alive) draw_hole(c, &V.voids[j], j);

    for (int b = 0; b < BUCKETS; b++)
        for (int i = g_head[b]; i >= 0; i = g_next[i]) draw_prop(c, &V.props[i]);

    draw_parts(c);

    /* Depth haze at the top of the frame. */
    {
        /* Flat stacked bands: a gradient here would evaluate a paint for
           every pixel of the top quarter of the screen. */
        float hh = (float)c->h * 0.26f;
        for (int k = 0; k < 6; k++)
            pa_fill_rect(c, 0, floorf(hh * (float)k / 6.0f), (float)c->w, ceilf(hh / 6.0f),
                         PA_RGBA(214, 222, 255, 22 - k * 3));
    }

    if (V.phase != PH_RESULTS) {
        for (int j = 0; j < V.void_count; j++)
            if (V.voids[j].alive && V.voids[j].dying <= 0.0f) draw_label(c, &V.voids[j]);
        draw_pops(c);
    }

    L.cy = save_cy;
    V.cam_x = cam_x;

    if (V.phase == PH_MENU) { draw_menu(c); return; }

    float top = hud_top(c);
    if (V.phase == PH_PLAY || V.phase == PH_TIMEUP) {
        draw_arrows(c, top + 120.0f);
        draw_hud(c);
    }

    const Void *me = &V.voids[0];
    if (V.phase == PH_PLAY && !me->alive) {
        pa_fill_rect(c, 0, (float)c->h * 0.36f, (float)c->w, 150.0f, PA_RGBA(20, 14, 60, 170));
        char buf[48];
        snprintf(buf, sizeof(buf), "EATEN BY %s!", V.voids[me->killer].name);
        txtb(c, buf, (float)c->w * 0.5f, (float)c->h * 0.36f + 30.0f, 30.0f, pa_hex(SKINS[V.voids[me->killer].skin].label),
             pa_hex(0x1A1240), PA_ALIGN_CENTER, 2.0f, 1.6f);
        snprintf(buf, sizeof(buf), "RESPAWN IN %d", (int)ceilf(me->respawn));
        txtb(c, buf, (float)c->w * 0.5f, (float)c->h * 0.36f + 84.0f, 24.0f, PA_RGB(255, 255, 255),
             pa_hex(0x1A1240), PA_ALIGN_CENTER, 2.0f, 1.5f);
    }

    if (V.toast_t > 0.0f && V.phase == PH_PLAY) {
        float age = 1.8f - V.toast_t;
        float y = top + 150.0f;
        float w = pa_text_width(V.toast, 20.0f, 1.5f) + 44.0f;
        float in = pa_smooth(pa_clamp01(age * 5.0f)) * pa_clamp01(V.toast_t * 5.0f);
        if (in > 0.05f) {
            rrect(c, (float)c->w * 0.5f - w * 0.5f, y, w, 38.0f, 19.0f, PA_RGBA(255, 255, 255, (int)(230.0f * in)));
            if (in > 0.6f) txtb(c, V.toast, (float)c->w * 0.5f, y + 9.0f, 20.0f, pa_hex(0x2F7BEA), PA_RGB(255, 255, 255),
                                PA_ALIGN_CENTER, 1.5f, 1.0f);
        }
    }

    if (V.banner_t > 0.0f && V.banner_t < 1.6f) {
        float age = 1.6f - V.banner_t;
        if (V.banner_t > 0.15f)
            banner_text(c, V.banner, (float)c->h * 0.27f, 44.0f, V.banner_col, age);
    }

    if (V.phase == PH_RESULTS) draw_results(c);
}

/* ------------------------------------------------------------ hub thumb -- */
static void muncher_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    pa_fill_rect(c, x, y, w, h, pa_hex(C_ROAD));
    /* Diagonal blocks with pastel buildings, as in the plates. */
    for (int k = 0; k < 4; k++) {
        float bx = x + w * ((k & 1) ? 0.58f : 0.04f), by = y + h * ((k & 2) ? 0.60f : 0.04f);
        pa_round_rect(c, bx, by + 3.0f, w * 0.38f, h * 0.34f, 4.0f, pa_hex(C_KERB));
        pa_round_rect(c, bx, by, w * 0.38f, h * 0.34f, 4.0f, pa_hex(C_WALK));
    }
    static const uint32_t cols[4] = { 0xF4A6B8, 0x76A4F0, 0xB7A6E8, 0xD06A4E };
    for (int k = 0; k < 4; k++) {
        float bx = x + w * (0.08f + 0.56f * (float)(k & 1)), by = y + h * (0.02f + 0.56f * (float)(k >> 1));
        float bh = h * (0.14f + 0.05f * (float)(k & 1));
        pa_fill_rect(c, bx, by + h * 0.06f, w * 0.13f, bh, pa_hex(cols[k]));
        pa_fill_rect(c, bx + w * 0.13f, by + h * 0.06f, w * 0.08f, bh, pa_mix(pa_hex(cols[k]), pa_hex(0x4A5890), 0.4f));
        pa_fill_rect(c, bx, by + h * 0.02f, w * 0.21f, h * 0.04f, pa_mix(pa_hex(cols[k]), pa_hex(0xFFE9C4), 0.4f));
    }
    float cx = x + w * 0.5f, cy = y + h * 0.54f;
    float grow_t = 0.5f + 0.5f * sinf(t * 1.1f);
    float r = (w < h ? w : h) * (0.17f + grow_t * 0.07f);
    pa_fill_ellipse(c, cx, cy + 3.0f, r * 1.26f, r * 0.94f, pa_hex(0x1E5FD0));
    pa_fill_ellipse(c, cx, cy, r * 1.26f, r * 0.94f, pa_hex(0x4FB3FF));
    PA_Paint pit = pa_radial(cx, cy + r * 0.2f, r * 0.1f, r);
    pa_stop(&pit, 0.0f, pa_hex(0x000000));
    pa_stop(&pit, 1.0f, pa_hex(0x1A1F4A));
    pa_fill_ellipse_paint(c, cx, cy, r, r * 0.72f, &pit);
    /* A car tipping in. */
    float tip = 0.5f + 0.5f * sinf(t * 2.0f);
    PA_Vec2 car[4] = { { cx - r * 0.6f, cy - r * 0.1f + tip * 4.0f }, { cx + r * 0.2f, cy - r * 0.5f + tip * 6.0f },
                       { cx + r * 0.45f, cy - r * 0.2f + tip * 6.0f }, { cx - r * 0.35f, cy + r * 0.2f + tip * 4.0f } };
    pa_fill_poly(c, car, 4, pa_hex(0xFFC52E));
    pa_fill_ellipse(c, cx + r * 0.65f, cy - r * 0.95f, r * 0.18f, r * 0.18f, pa_hex(0xFFD23A));
}

const PA_Game PA_GAME_VOIDMUNCHER = {
    "voidmuncher", "Void Muncher", "Arena",
    "You are a hole. Swallow the city, out-grow four rival holes, top the ranking.",
    PA_RGB(47, 123, 234),
    muncher_start, muncher_stop, muncher_update, muncher_render, muncher_thumb
};
