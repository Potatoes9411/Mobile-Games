/* ===========================================================================
   VOID MUNCHER - native port
   Drag to steer a hole across a generated district. It swallows anything
   smaller than its mouth, grows with every bite, and rival voids are doing the
   same to the same city. Ninety seconds decides who took the block.

   Drawn the way the Hole.io plates show a city: a close three-quarter camera
   turned off the street grid so the roads run diagonally, pale lavender-grey
   tarmac with white crosswalks and lane dashes, raised sidewalks, pastel
   buildings with window grids, traffic that actually drives, and the hole as
   a black pit with a thick coloured rim and a name tag. Every prop is a real
   box under an oblique projection, so the two faces toward the camera shade
   differently and nothing reads as a flat sticker.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define MAX_PROPS  4200
#define MAX_VOIDS  5
#define MAX_BLOCKS 160

/* Camera. The grid is turned VIEW_ROT off the screen axes, ground depth is
   foreshortened by GROUND_K and heights rise at HEIGHT_K: a steep, close
   three-quarter view like the plates'. */
#define VIEW_ROT  0.62f
#define GROUND_K  0.74f
#define HEIGHT_K  0.86f

enum { ST_CONE, ST_BIN, ST_LAMP, ST_BENCH, ST_TREE, ST_WALKER, ST_SCOOTER, ST_KIOSK,
       ST_CAR, ST_CAB, ST_VAN, ST_BUS, ST_TRUCK, ST_HUT, ST_HOUSE, ST_SHOP, ST_TOWER,
       ST_SPIRE };
enum { W_STREET, W_ROAD, W_LOT, W_PARK };

typedef struct {
    const char *key;
    int   style, where, tier;
    float r;               /* mouth radius needed to swallow it */
    float len, wid, h;     /* drawn footprint and height */
    int   value;
    uint32_t colour;
    float weight;
} PropType;

/* Tier gaps are wide on purpose: each one is a visible change in what the city
   looks like from inside, and crossing one is the moment a run gets loud. */
static const PropType TYPES[] = {
    { "cone",    ST_CONE,    W_STREET, 0,  6.f,  10.f, 10.f,  13.f,   1, 0xFF7A2E, 12.f },
    { "bin",     ST_BIN,     W_STREET, 0,  7.f,  10.f, 10.f,  14.f,   2, 0x3E9E6A,  9.f },
    { "lamp",    ST_LAMP,    W_STREET, 0,  6.f,   4.f,  4.f,  30.f,   2, 0x6E7890,  8.f },
    { "bench",   ST_BENCH,   W_PARK,   1, 11.f,  24.f,  9.f,   9.f,   4, 0xB07A48,  8.f },
    { "tree",    ST_TREE,    W_PARK,   1, 12.f,  24.f, 24.f,  34.f,   5, 0x55B84A, 11.f },
    { "walker",  ST_WALKER,  W_STREET, 1,  9.f,   8.f,  7.f,  19.f,   6, 0xE8B48A, 10.f },
    { "scooter", ST_SCOOTER, W_ROAD,   2, 15.f,  24.f,  9.f,  14.f,  10, 0xE8455F,  7.f },
    { "kiosk",   ST_KIOSK,   W_PARK,   2, 17.f,  30.f, 22.f,  26.f,  14, 0xF4C53F,  5.f },
    { "car",     ST_CAR,     W_ROAD,   3, 21.f,  44.f, 22.f,  17.f,  26, 0x3E7FF0,  9.f },
    { "cab",     ST_CAB,     W_ROAD,   3, 21.f,  44.f, 22.f,  17.f,  28, 0xF6C340,  5.f },
    { "van",     ST_VAN,     W_ROAD,   3, 24.f,  50.f, 24.f,  26.f,  34, 0xF1F2F7,  4.f },
    { "bus",     ST_BUS,     W_ROAD,   4, 33.f,  80.f, 27.f,  30.f,  70, 0xE5533F,  4.f },
    { "truck",   ST_TRUCK,   W_ROAD,   4, 35.f,  82.f, 28.f,  34.f,  78, 0x5F77B0,  3.f },
    { "hut",     ST_HUT,     W_LOT,    4, 36.f,  62.f, 56.f,  44.f,  84, 0xD9785A,  3.f },
    { "house",   ST_HOUSE,   W_LOT,    5, 52.f,  90.f, 84.f,  82.f, 190, 0xF4A6B8,  4.f },
    { "shop",    ST_SHOP,    W_LOT,    5, 56.f, 100.f, 92.f,  72.f, 210, 0x9FDCC8,  3.f },
    { "tower",   ST_TOWER,   W_LOT,    6, 78.f, 136.f, 130.f, 230.f, 640, 0x8FB8F0,  2.f },
    { "spire",   ST_SPIRE,   W_LOT,    6, 70.f, 124.f, 124.f, 290.f, 720, 0xB7A6E8,  1.f }
};
#define TYPE_COUNT ((int)(sizeof(TYPES) / sizeof(TYPES[0])))

/* Building paint, measured off the plates: pastel pinks, blues, lavenders,
   mint, cream and a warm brick. */
static const uint32_t BUILDING_TINTS[] = {
    0xF4A6B8, 0x8FB8F0, 0xB7A6E8, 0x9FDCC8, 0xF2D8A6, 0xD9785A, 0x7FA0E0, 0xF0C090
};
static const uint32_t SHIRTS[] = { 0xE8455F, 0x3E7FF0, 0xF6C340, 0x55B84A, 0x9B5BE8, 0xF1F2F7 };

typedef struct {
    float x, y;
    float vx, vy;          /* traffic only */
    float rot;
    const PropType *type;
    PA_Color col;
    float sink;
    int   taken;
    float ang;
    int   eater;
    uint32_t mask;
} Prop;

typedef struct {
    int   player, alive;
    const char *name;
    float x, y, r;
    int   score, kills;
    PA_Color tint;
    float vx, vy;
    float think, skill;
    int   target;          /* prop index, or -1 */
    int   chase;           /* void index, or -1 */
} Void;

typedef struct { float x, y, w, h; int park; } Block;

static const char *RIVAL_NAMES[] = { "ABYSS", "GULP", "NIL", "CHASM", "MAW", "SINK" };
static const uint32_t RIVAL_TINTS[] = { 0xE8455F, 0x9B5BE8, 0x2FC98E, 0xF79A2E };

static const struct { float r; const char *text; } MILESTONES[] = {
    { 20.f, "BENCHES" }, { 26.f, "TREES" }, { 32.f, "SCOOTERS" }, { 40.f, "CARS" },
    { 52.f, "VANS" },    { 66.f, "BUSES" }, { 84.f, "HOUSES" },   { 108.f, "SHOPS" },
    { 140.f, "TOWERS" }
};
#define MILESTONE_COUNT ((int)(sizeof(MILESTONES) / sizeof(MILESTONES[0])))

typedef struct {
    Prop   props[MAX_PROPS];
    int    prop_count, player_eaten;
    Block  blocks[MAX_BLOCKS];
    int    block_count;
    float  half_x, half_y, span, road;
    int    cells_x, cells_y;

    Void   voids[MAX_VOIDS];
    int    void_count;

    float  timer, grace;
    int    level, eaten, milestone;
    float  cam_x, cam_y, cam_scale;
    float  shockwave, time;
    int    over, won;
    float  over_t;
    char   banner[40];
    float  banner_t;
} Muncher;

static Muncher V;
static int     g_best;
static int     g_best_loaded;

static struct { int w, h; float unit, cy; } L;
static float g_cr, g_sr;

/* ------------------------------------------------------------- generation -- */
/** Weighted pick among types that live in `where`, biased so outer rings hold
    the big stuff and early growth happens near the middle. */
static const PropType *roll_prop(PA_Rng *r, int level, float ring, int where) {
    float total = 0.0f;
    float weights[TYPE_COUNT];
    float want = ring * 6.4f;

    for (int i = 0; i < TYPE_COUNT; i++) {
        int ok = TYPES[i].where == where ||
                 (where == W_PARK && TYPES[i].where == W_STREET && TYPES[i].style == ST_WALKER) ||
                 (where == W_STREET && TYPES[i].where == W_PARK && TYPES[i].style != ST_KIOSK);
        if (!ok) { weights[i] = 0.0f; continue; }
        float gap = fabsf((float)TYPES[i].tier - want);
        float w = TYPES[i].weight * expf(-gap * gap * 0.35f);
        if (TYPES[i].tier >= 5 && level < 2) w *= 0.35f;
        weights[i] = w > 0.01f ? w : 0.0f;
        total += weights[i];
    }
    if (total <= 0.0f) return &TYPES[0];

    float roll = pa_rng_next(r) * total;
    for (int i = 0; i < TYPE_COUNT; i++) {
        roll -= weights[i];
        if (roll <= 0.0f && weights[i] > 0.0f) return &TYPES[i];
    }
    for (int i = TYPE_COUNT - 1; i >= 0; i--) if (weights[i] > 0.0f) return &TYPES[i];
    return &TYPES[0];
}

static Prop *add_prop(PA_Rng *r, const PropType *t, float x, float y, float rot) {
    if (V.prop_count >= MAX_PROPS) return NULL;
    Prop *p = &V.props[V.prop_count++];
    memset(p, 0, sizeof(*p));
    p->x = x; p->y = y; p->rot = rot;
    p->type = t;
    p->eater = -1;
    p->mask = (uint32_t)pa_rng_int(r, 0, 0xFFFF);
    p->col = pa_hex(t->colour);
    if (t->where == W_LOT)
        p->col = pa_hex(BUILDING_TINTS[pa_rng_int(r, 0, (int)(sizeof(BUILDING_TINTS) / 4) - 1)]);
    if (t->style == ST_WALKER)
        p->col = pa_hex(SHIRTS[pa_rng_int(r, 0, (int)(sizeof(SHIRTS) / 4) - 1)]);
    if (t->style == ST_CAR && pa_rng_chance(r, 0.5f))
        p->col = pa_hex(pa_rng_chance(r, 0.5f) ? 0xE8455F : 0x55B84A);
    return p;
}

/** Axis-aligned overlap test against props already placed in a block. */
static int lot_free(int from, float x, float y, float hx, float hy) {
    for (int t = from; t < V.prop_count; t++) {
        const Prop *q = &V.props[t];
        float qx = (fabsf(sinf(q->rot)) > 0.5f ? q->type->wid : q->type->len) * 0.5f;
        float qy = (fabsf(sinf(q->rot)) > 0.5f ? q->type->len : q->type->wid) * 0.5f;
        if (q->type->where != W_LOT && q->type->style != ST_KIOSK) { qx = qy = q->type->r * 0.7f; }
        if (fabsf(x - q->x) < hx + qx + 6.0f && fabsf(y - q->y) < hy + qy + 6.0f) return 0;
    }
    return 1;
}

static void build_city(int level) {
    PA_Rng r;
    pa_rng_seed(&r, 0x5EEDu ^ ((uint32_t)level * 7919u));

    V.prop_count = 0;
    V.block_count = 0;

    V.cells_x = 5 + (level / 3 > 3 ? 3 : level / 3);
    float aspect = pa_clampf((float)L.h / (float)(L.w > 0 ? L.w : 1), 1.0f, 2.0f);
    V.cells_y = (int)((float)V.cells_x * aspect * 0.85f + 0.5f);
    if (V.cells_y < V.cells_x) V.cells_y = V.cells_x;
    if (V.cells_y > 13) V.cells_y = 13;

    V.span = 260.0f;
    V.road = V.span * 0.30f;
    V.half_x = V.span * (float)V.cells_x * 0.5f;
    V.half_y = V.span * (float)V.cells_y * 0.5f;
    float half = V.half_x > V.half_y ? V.half_x : V.half_y;
    float bw = V.span - V.road;
    float walk = 16.0f;                 /* sidewalk ring inside each block */

    for (int cy = 0; cy < V.cells_y; cy++) {
        for (int cx = 0; cx < V.cells_x; cx++) {
            float bx = -V.half_x + V.span * ((float)cx + 0.5f);
            float by = -V.half_y + V.span * ((float)cy + 0.5f);
            float ring = pa_clamp01(sqrtf(bx * bx + by * by) / (half * 1.05f));
            int centre = fabsf(bx) < V.span * 0.5f && fabsf(by) < V.span * 0.5f;
            int park = centre || pa_rng_chance(&r, 0.10f);

            if (V.block_count < MAX_BLOCKS) {
                Block *b = &V.blocks[V.block_count++];
                b->x = bx; b->y = by; b->w = bw; b->h = bw; b->park = park;
            }

            int first = V.prop_count;
            float inner = bw * 0.5f - walk - 4.0f;

            if (park) {
                /* Trees in loose rows, benches and strollers between them. */
                int n = 10 + pa_rng_int(&r, 0, 6);
                for (int k = 0; k < n * 3 && n > 0; k++) {
                    const PropType *t = roll_prop(&r, level, ring * 0.5f, W_PARK);
                    float x = bx + pa_rng_range(&r, -inner, inner);
                    float y = by + pa_rng_range(&r, -inner, inner);
                    float hx = t->len * 0.5f, hy = t->wid * 0.5f;
                    if (!lot_free(first, x, y, hx, hy)) continue;
                    add_prop(&r, t, x, y, pa_rng_chance(&r, 0.5f) ? 0.0f : PA_PI * 0.5f);
                    n--;
                }
            } else {
                /* Buildings first, biggest that fit, then small fill. */
                for (int k = 0; k < 14; k++) {
                    const PropType *t = roll_prop(&r, level, ring, W_LOT);
                    float rot = pa_rng_chance(&r, 0.5f) ? 0.0f : PA_PI * 0.5f;
                    float hx = (rot == 0.0f ? t->len : t->wid) * 0.5f;
                    float hy = (rot == 0.0f ? t->wid : t->len) * 0.5f;
                    if (hx > inner || hy > inner) continue;
                    float x = bx + pa_rng_range(&r, -(inner - hx), inner - hx);
                    float y = by + pa_rng_range(&r, -(inner - hy), inner - hy);
                    if (!lot_free(first, x, y, hx, hy)) continue;
                    add_prop(&r, t, x, y, rot);
                }
            }

            /* Street furniture round the sidewalk ring. */
            float edge = bw * 0.5f - walk * 0.5f;
            for (int side = 0; side < 4; side++) {
                for (float s = -edge + 10.0f; s < edge - 10.0f; s += 26.0f) {
                    if (!pa_rng_chance(&r, 0.42f)) continue;
                    const PropType *t = roll_prop(&r, level, ring * 0.3f, W_STREET);
                    if (t->r > 13.0f) continue;
                    float x = bx + (side == 0 ? s : side == 1 ? edge : side == 2 ? -s : -edge);
                    float y = by + (side == 0 ? -edge : side == 1 ? s : side == 2 ? edge : -s);
                    add_prop(&r, t, x, y, pa_rng_range(&r, 0.0f, PA_TAU));
                }
            }
        }
    }

    /* Traffic: vehicles along every road line, two lanes each way of travel. */
    for (int axis = 0; axis < 2; axis++) {
        int lines = axis == 0 ? V.cells_y + 1 : V.cells_x + 1;
        float len = axis == 0 ? V.half_x : V.half_y;
        for (int i = 0; i < lines; i++) {
            float line = axis == 0 ? -V.half_y + V.span * (float)i : -V.half_x + V.span * (float)i;
            for (int lane = 0; lane < 2; lane++) {
                float dir = lane == 0 ? 1.0f : -1.0f;
                float off = V.road * 0.22f * (lane == 0 ? 1.0f : -1.0f);
                float speed = pa_rng_range(&r, 34.0f, 52.0f) * dir;
                int n = pa_rng_int(&r, 1, 3);
                for (int k = 0; k < n; k++) {
                    const PropType *t = roll_prop(&r, level, 0.35f + 0.3f * pa_rng_next(&r), W_ROAD);
                    float s = -len + (2.0f * len) * ((float)k + pa_rng_range(&r, 0.1f, 0.6f)) / (float)n;
                    float x = axis == 0 ? s : line + off;
                    float y = axis == 0 ? line + off : s;
                    float rot = axis == 0 ? (dir > 0 ? 0.0f : PA_PI) : (dir > 0 ? PA_PI * 0.5f : -PA_PI * 0.5f);
                    Prop *p = add_prop(&r, t, x, y, rot);
                    if (!p) continue;
                    p->vx = axis == 0 ? speed : 0.0f;
                    p->vy = axis == 0 ? 0.0f : speed;
                }
            }
        }
    }

    /* Clear a landing pad so the opening seconds are never a wall of trucks. */
    for (int i = V.prop_count - 1; i >= 0; i--) {
        if (V.props[i].type->tier >= 2 &&
            V.props[i].x * V.props[i].x + V.props[i].y * V.props[i].y < 150.0f * 150.0f) {
            V.props[i] = V.props[--V.prop_count];
        }
    }
}

/* ------------------------------------------------------------------ setup -- */
static float start_radius(void) { return 16.0f; }

static float target_scale(float r) {
    /* Close in, as the plates are: the mouth fills a good fifth of the width
       at the start and the camera pulls back as it grows. */
    return pa_clampf(L.unit / (r * 6.0f + 96.0f), 0.30f, 3.0f);
}

static void muncher_start(void) {
    if (!g_best_loaded) {
        g_best = pa_save_get("voidmuncher.best", 0);
        g_best_loaded = 1;
    }
    if (L.w == 0) { L.w = 540; L.h = 960; L.unit = 540.0f; L.cy = 960.0f * 0.56f; }
    g_cr = cosf(VIEW_ROT);
    g_sr = sinf(VIEW_ROT);

    int level = pa_save_get("voidmuncher.level", 1);
    if (level < 1) level = 1;

    memset(&V, 0, sizeof(V));
    V.level = level;
    build_city(level);

    V.void_count = 0;
    Void *me = &V.voids[V.void_count++];
    me->player = 1; me->alive = 1; me->name = "YOU";
    me->r = start_radius();
    me->tint = pa_hex(0x2F7BEA);
    me->target = -1; me->chase = -1;

    int rivals = (int)pa_clampf(2.0f + (float)(level / 2), 2.0f, 4.0f);
    PA_Rng r;
    pa_rng_seed(&r, 0xC0FFEEu ^ ((uint32_t)level * 131u));
    for (int i = 0; i < rivals && V.void_count < MAX_VOIDS; i++) {
        float ang = PA_TAU * ((float)i + 0.5f) / (float)rivals;
        Void *v = &V.voids[V.void_count++];
        v->alive = 1;
        v->name = RIVAL_NAMES[(level * 3 + i) % (int)(sizeof(RIVAL_NAMES) / sizeof(RIVAL_NAMES[0]))];
        v->x = cosf(ang) * V.half_x * 0.60f;
        v->y = sinf(ang) * V.half_y * 0.60f;
        /* Rivals start a touch behind, so the first half minute feels like a
           lead that was earned. */
        v->r = start_radius() * 0.9f;
        v->tint = pa_hex(RIVAL_TINTS[i % 4]);
        v->skill = pa_clamp01(0.42f + (float)level * 0.05f + pa_rng_range(&r, -0.06f, 0.06f));
        v->think = pa_rng_range(&r, 0.0f, 0.4f);
        v->target = -1; v->chase = -1;
    }

    V.timer = 90.0f;
    V.grace = 9.0f;
    V.cam_scale = target_scale(me->r);
    V.milestone = 0;
}

static void muncher_stop(void) { }

/* ------------------------------------------------------------------ eating */
/** Radius after absorbing `area`. Areas add, radii do not: doubling the mouth
    needs four times the city, which is what keeps the curve honest. */
static void grow(Void *v, float area) {
    /* Rivals bank less of what they eat. Without the handicap a bot that spawns
       on a dense block snowballs past the player inside three seconds, which
       reads as the game cheating rather than as a race. */
    float gain = area * (v->player ? 1.0f : 0.62f);
    float a = PA_PI * v->r * v->r + gain;
    v->r = sqrtf(a / PA_PI);
}

static void banner(const char *text) {
    snprintf(V.banner, sizeof(V.banner), "%s", text);
    V.banner_t = 2.0f;
}

static void swallow(Void *v, Prop *p, int void_index) {
    p->taken = 1;
    p->sink = 0.0001f;
    p->ang = atan2f(p->y - v->y, p->x - v->x);
    p->eater = void_index;

    grow(v, PA_PI * p->type->r * p->type->r * 0.62f);
    v->score += p->type->value;

    if (v->player) {
        V.eaten++;
        V.player_eaten++;
        pa_sfx(p->type->tier >= 4 ? "boom" : "pop");
        if (p->type->tier >= 4) V.shockwave = 1.0f;
    }
}

static int can_eat(const Void *v, const Prop *p) {
    return !p->taken && p->type->r <= v->r * 0.94f;
}

/* ------------------------------------------------------------------ update */
static void steer_rival(Void *v, float dt) {
    v->think -= dt;

    if (v->think <= 0.0f || v->target < 0 || V.props[v->target].taken) {
        v->think = pa_lerpf(0.55f, 0.16f, v->skill);
        v->chase = -1;

        /* Score every candidate by value over travel time and take the best.
           Low-skill rivals sample a sparse slice instead, which makes them
           wander plausibly rather than looking broken. */
        int best = -1;
        float best_score = -1.0f;
        int samples = (int)pa_lerpf(26.0f, 150.0f, v->skill);
        int step = V.prop_count / (samples > 0 ? samples : 1);
        if (step < 1) step = 1;

        for (int i = 0; i < V.prop_count; i += step) {
            Prop *p = &V.props[i];
            if (!can_eat(v, p)) continue;
            float dx = v->x - p->x, dy = v->y - p->y;
            float d = sqrtf(dx * dx + dy * dy) + 1.0f;
            float s = (float)p->type->value / d;
            if (s > best_score) { best_score = s; best = i; }
        }

        /* A big rival that has run out of food goes hunting smaller voids. */
        if (best < 0) {
            for (int k = 0; k < V.void_count; k++) {
                Void *o = &V.voids[k];
                if (o == v || !o->alive || v->r < o->r * 1.3f) continue;
                v->chase = k;
                break;
            }
        }
        v->target = best;
    }

    float tx, ty;
    if (v->chase >= 0) { tx = V.voids[v->chase].x; ty = V.voids[v->chase].y; }
    else if (v->target >= 0) { tx = V.props[v->target].x; ty = V.props[v->target].y; }
    else return;

    float dx = tx - v->x, dy = ty - v->y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float speed = (148.0f + (float)V.level * 7.0f) * pa_lerpf(0.80f, 1.04f, v->skill);
    v->vx = dx / len * speed;
    v->vy = dy / len * speed;
}

static void finish(int won);

static void try_devour(void) {
    for (int i = 0; i < V.void_count; i++) {
        Void *a = &V.voids[i];
        if (!a->alive) continue;
        for (int j = 0; j < V.void_count; j++) {
            if (i == j) continue;
            Void *b = &V.voids[j];
            /* A high bar and a centre well inside the mouth, so a narrow lead
               never becomes an instant unrecoverable loss. */
            if (!b->alive || a->r < b->r * 1.35f) continue;
            if (b->player && V.grace > 0.0f) continue;
            float bite = a->r * 0.70f;
            float dx = a->x - b->x, dy = a->y - b->y;
            if (dx * dx + dy * dy > bite * bite) continue;

            b->alive = 0;
            a->kills++;
            grow(a, PA_PI * b->r * b->r * 0.5f);
            a->score += b->score / 2;
            if (a->player) { banner("SWALLOWED!"); pa_sfx("levelup"); }
            else if (b->player) { finish(0); return; }
        }
    }
}

/** Screen-space direction to world-space direction, undoing the camera turn
    and the ground foreshortening so steering goes where the finger points. */
static void screen_dir_to_world(float sx, float sy, float *wx, float *wy) {
    float vx = sx, vy = sy / GROUND_K;
    *wx = vx * g_cr + vy * g_sr;
    *wy = -vx * g_sr + vy * g_cr;
}

static PA_Vec2 P3(float x, float y, float z);

static void muncher_update(float dt, const PA_Input *in) {
    V.time += dt;
    if (V.banner_t > 0.0f) V.banner_t -= dt;

    if (V.over) {
        V.over_t += dt;
        if (V.over_t > 4.0f) muncher_start();
        return;
    }

    V.timer -= dt;
    if (V.timer <= 0.0f) { V.timer = 0.0f; finish(-1); return; }
    if (V.grace > 0.0f) V.grace -= dt;
    V.shockwave = V.shockwave > 0.0f ? V.shockwave - dt * 2.4f : 0.0f;

    Void *me = &V.voids[0];
    float speed = 250.0f;

    if (in->down) {
        /* Relative to where the hole is drawn, not the screen centre: the
           camera stops at the district edge and the hole walks off-centre. */
        PA_Vec2 hs = P3(me->x, me->y, 0.0f);
        float dx = in->x - hs.x, dy = in->y - hs.y;
        float len = sqrtf(dx * dx + dy * dy);
        float reach = L.unit * 0.22f;
        float mag = pa_clamp01(len / reach);
        if (len > 4.0f) {
            float wx, wy;
            screen_dir_to_world(dx, dy, &wx, &wy);
            float wl = sqrtf(wx * wx + wy * wy);
            me->vx = wx / wl * speed * mag;
            me->vy = wy / wl * speed * mag;
        }
    } else {
        float kx = 0.0f, ky = 0.0f;
        if (in->keys[PA_KEY_LEFT])  kx -= 1.0f;
        if (in->keys[PA_KEY_RIGHT]) kx += 1.0f;
        if (in->keys[PA_KEY_UP])    ky -= 1.0f;
        if (in->keys[PA_KEY_DOWN])  ky += 1.0f;
        if (kx != 0.0f || ky != 0.0f) {
            float wx, wy;
            screen_dir_to_world(kx, ky * GROUND_K, &wx, &wy);
            float kl = sqrtf(wx * wx + wy * wy);
            me->vx = wx / kl * speed;
            me->vy = wy / kl * speed;
        } else {
            me->vx *= expf(-7.0f * dt);
            me->vy *= expf(-7.0f * dt);
        }
    }

    for (int i = 1; i < V.void_count; i++) {
        if (V.voids[i].alive) steer_rival(&V.voids[i], dt);
    }

    float bx = V.half_x + V.road * 0.5f, by = V.half_y + V.road * 0.5f;
    for (int i = 0; i < V.void_count; i++) {
        Void *v = &V.voids[i];
        if (!v->alive) continue;
        v->x = pa_clampf(v->x + v->vx * dt, -bx, bx);
        v->y = pa_clampf(v->y + v->vy * dt, -by, by);
    }

    /* Traffic drives its lane and wraps at the district edge. */
    float wrap_x = V.half_x + V.road * 0.5f, wrap_y = V.half_y + V.road * 0.5f;
    for (int i = 0; i < V.prop_count; i++) {
        Prop *p = &V.props[i];
        if (p->taken || (p->vx == 0.0f && p->vy == 0.0f)) continue;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        if (p->x > wrap_x) p->x -= wrap_x * 2.0f;
        if (p->x < -wrap_x) p->x += wrap_x * 2.0f;
        if (p->y > wrap_y) p->y -= wrap_y * 2.0f;
        if (p->y < -wrap_y) p->y += wrap_y * 2.0f;
    }

    /* Eating pass. */
    for (int i = 0; i < V.prop_count; i++) {
        Prop *p = &V.props[i];
        if (p->taken) continue;
        for (int j = 0; j < V.void_count; j++) {
            Void *v = &V.voids[j];
            if (!v->alive || !can_eat(v, p)) continue;
            /* Bite when the centre is inside the mouth, minus a little, so
               things visibly reach the lip before they tip in. */
            float reach = v->r * 0.86f;
            float dx = v->x - p->x, dy = v->y - p->y;
            if (dx * dx + dy * dy < reach * reach) { swallow(v, p, j); break; }
        }
    }

    for (int i = 0; i < V.prop_count; i++) {
        if (V.props[i].taken && V.props[i].sink < 1.0f) {
            V.props[i].sink = pa_clampf(V.props[i].sink + dt * 2.2f, 0.0f, 1.0f);
        }
    }

    try_devour();
    if (V.over) return;

    while (V.milestone < MILESTONE_COUNT && me->r >= MILESTONES[V.milestone].r) {
        banner(MILESTONES[V.milestone].text);
        pa_sfx("levelup");
        V.milestone++;
    }

    V.cam_x = pa_approach(V.cam_x, me->x, 7.0f, dt);
    V.cam_y = pa_approach(V.cam_y, me->y, 7.0f, dt);
    V.cam_scale = pa_approach(V.cam_scale, target_scale(me->r), 2.4f, dt);
    /* Stop short of the district edge so the view stays mostly city rather
       than open sea; the hole itself can still reach the kerb. */
    float keep = 260.0f / (V.cam_scale > 0.1f ? V.cam_scale : 0.1f);
    float lim_x = V.half_x - keep > 0.0f ? V.half_x - keep : 0.0f;
    float lim_y = V.half_y - keep > 0.0f ? V.half_y - keep : 0.0f;
    V.cam_x = pa_clampf(V.cam_x, -lim_x, lim_x);
    V.cam_y = pa_clampf(V.cam_y, -lim_y, lim_y);
}

static int standing(const Void *v) {
    int place = 1;
    for (int i = 0; i < V.void_count; i++) {
        const Void *o = &V.voids[i];
        if (o == v) continue;
        if (!v->alive && o->alive) place++;
        else if (v->alive == o->alive && o->score > v->score) place++;
    }
    return place;
}

static void finish(int won) {
    if (V.over) return;
    V.over = 1;
    V.over_t = 0.0f;

    Void *me = &V.voids[0];
    int place = standing(me);
    V.won = (won == 1) || (won == -1 && place == 1 && me->alive);

    if (me->score > g_best) {
        g_best = me->score;
        pa_save_set("voidmuncher.best", g_best);
    }
    if (V.won) pa_save_set("voidmuncher.level", V.level + 1);
    pa_save_flush();
    pa_sfx(V.won ? "win" : "lose");
}

/* ---------------------------------------------------------------- drawing -- */
static PA_Vec2 P3(float x, float y, float z) {
    float dx = x - V.cam_x, dy = y - V.cam_y;
    float vx = dx * g_cr - dy * g_sr, vy = dx * g_sr + dy * g_cr;
    PA_Vec2 o;
    o.x = (float)L.w * 0.5f + vx * V.cam_scale;
    o.y = L.cy + vy * V.cam_scale * GROUND_K - z * V.cam_scale * HEIGHT_K;
    return o;
}

static float view_depth(float x, float y) { return x * g_sr + y * g_cr; }

static void quad(PA_Canvas *c, PA_Vec2 a, PA_Vec2 b, PA_Vec2 d, PA_Vec2 e, PA_Color col) {
    PA_Vec2 q[4] = { a, b, d, e };
    pa_fill_poly(c, q, 4, col);
}

/** World-axis rectangle on the ground at height z. */
static void ground_rect(PA_Canvas *c, float x0, float y0, float x1, float y1, float z, PA_Color col) {
    quad(c, P3(x0, y0, z), P3(x1, y0, z), P3(x1, y1, z), P3(x0, y1, z), col);
}

/** Rotated rectangle on the ground (a stripe, a dash, a shadow). */
static void ground_strip(PA_Canvas *c, float x, float y, float hl, float hw, float rot, PA_Color col) {
    float ca = cosf(rot), sa = sinf(rot);
    PA_Vec2 q[4];
    static const float SX[4] = { -1, 1, 1, -1 }, SY[4] = { -1, -1, 1, 1 };
    for (int k = 0; k < 4; k++) {
        float lx = SX[k] * hl, ly = SY[k] * hw;
        q[k] = P3(x + lx * ca - ly * sa, y + lx * sa + ly * ca, 0.0f);
    }
    pa_fill_poly(c, q, 4, col);
}

typedef struct { PA_Vec2 b[4], t[4]; int vis[4]; float nvx[4]; } BoxFaces;

/**
 * An oriented box. Side faces are drawn only when they face the camera, and
 * shaded by which way they turn: left-facing sides catch the light, right-
 * facing ones fall into shade, the top is brightest - the plates' three tones.
 */
static void box3(PA_Canvas *c, float x, float y, float z0, float hl, float hw, float h,
                 float rot, PA_Color side, PA_Color top, BoxFaces *out) {
    float ca = cosf(rot), sa = sinf(rot);
    static const float LX[4] = { -1, 1, 1, -1 }, LY[4] = { -1, -1, 1, 1 };
    static const float NX[4] = { 0, 1, 0, -1 }, NY[4] = { -1, 0, 1, 0 };
    BoxFaces f;
    for (int k = 0; k < 4; k++) {
        float lx = LX[k] * hl, ly = LY[k] * hw;
        float wx = x + lx * ca - ly * sa, wy = y + lx * sa + ly * ca;
        f.b[k] = P3(wx, wy, z0);
        f.t[k] = P3(wx, wy, z0 + h);
    }
    for (int k = 0; k < 4; k++) {
        float nx = NX[k] * ca - NY[k] * sa, ny = NX[k] * sa + NY[k] * ca;
        float nvx = nx * g_cr - ny * g_sr, nvy = nx * g_sr + ny * g_cr;
        f.vis[k] = nvy > 0.001f;
        f.nvx[k] = nvx;
        if (!f.vis[k]) continue;
        int n = (k + 1) & 3;
        quad(c, f.b[k], f.b[n], f.t[n], f.t[k], pa_shade(side, -0.10f - 0.20f * nvx));
    }
    quad(c, f.t[0], f.t[1], f.t[2], f.t[3], top);
    if (out) *out = f;
}

/** Windows on the visible faces of a box, as a grid or, when small on screen,
    as ribbon bands - the software rasterizer pays per polygon. */
static void windows(PA_Canvas *c, const BoxFaces *f, float face_h, float len, float wid,
                    PA_Color glass, float z_from) {
    for (int k = 0; k < 4; k++) {
        if (!f->vis[k]) continue;
        int n = (k + 1) & 3;
        PA_Vec2 o = f->b[k];
        float ux = f->b[n].x - o.x, uy = f->b[n].y - o.y;
        float vx = f->t[k].x - o.x, vy = f->t[k].y - o.y;
        float face_len = (k & 1) ? wid : len;
        int rows = (int)(face_h * (1.0f - z_from) / 24.0f);
        int cols = (int)(face_len / 20.0f);
        if (rows < 1) rows = 1;
        if (rows > 12) rows = 12;
        if (cols < 1) cols = 1;
        if (cols > 8) cols = 8;
        PA_Color g = pa_shade(glass, -0.08f - 0.16f * f->nvx[k]);
        float px_w = sqrtf(ux * ux + uy * uy) / (float)cols;
        for (int rr = 0; rr < rows; rr++) {
            float v0 = z_from + (1.0f - z_from) * ((float)rr + 0.28f) / (float)rows;
            float v1 = z_from + (1.0f - z_from) * ((float)rr + 0.72f) / (float)rows;
            if (px_w < 7.0f) {
                float u0 = 0.08f, u1 = 0.92f;
                PA_Vec2 q[4] = {
                    { o.x + ux * u0 + vx * v0, o.y + uy * u0 + vy * v0 },
                    { o.x + ux * u1 + vx * v0, o.y + uy * u1 + vy * v0 },
                    { o.x + ux * u1 + vx * v1, o.y + uy * u1 + vy * v1 },
                    { o.x + ux * u0 + vx * v1, o.y + uy * u0 + vy * v1 } };
                pa_fill_poly(c, q, 4, g);
                continue;
            }
            for (int cc = 0; cc < cols; cc++) {
                float u0 = ((float)cc + 0.22f) / (float)cols, u1 = ((float)cc + 0.78f) / (float)cols;
                PA_Vec2 q[4] = {
                    { o.x + ux * u0 + vx * v0, o.y + uy * u0 + vy * v0 },
                    { o.x + ux * u1 + vx * v0, o.y + uy * u1 + vy * v0 },
                    { o.x + ux * u1 + vx * v1, o.y + uy * u1 + vy * v1 },
                    { o.x + ux * u0 + vx * v1, o.y + uy * u0 + vy * v1 } };
                pa_fill_poly(c, q, 4, g);
            }
        }
    }
}

static void draw_prop_at(PA_Canvas *c, const Prop *p, float x, float y, float z0, float k) {
    const PropType *t = p->type;
    float s = V.cam_scale;
    float hl = t->len * 0.5f * k, hw = t->wid * 0.5f * k, h = t->h * k;
    if (t->len * k * s < 1.5f) return;
    PA_Color col = p->col;
    PA_Color top = pa_shade(col, 0.10f);
    BoxFaces f;

    if (z0 >= 0.0f) {
        PA_Vec2 g = P3(x + 3.0f * k, y + 4.0f * k, 0.0f);
        float rad = (hl > hw ? hl : hw) * s;
        pa_shadow(c, g.x, g.y, rad * 1.05f, rad * 0.55f, t->where == W_LOT ? 0.26f : 0.20f);
    }

    switch (t->style) {
    case ST_CONE: {
        PA_Vec2 a = P3(x - hl, y, z0), b = P3(x + hl, y, z0), tip = P3(x, y, z0 + h);
        PA_Vec2 tri[3] = { a, b, tip };
        pa_fill_poly(c, tri, 3, col);
        PA_Vec2 m0 = P3(x - hl * 0.62f, y, z0 + h * 0.38f), m1 = P3(x + hl * 0.62f, y, z0 + h * 0.38f);
        PA_Vec2 n0 = P3(x - hl * 0.44f, y, z0 + h * 0.56f), n1 = P3(x + hl * 0.44f, y, z0 + h * 0.56f);
        quad(c, m0, m1, n1, n0, PA_RGB(255, 255, 255));
        break;
    }
    case ST_BIN:
        box3(c, x, y, z0, hl, hw, h, p->rot, col, pa_shade(col, -0.30f), NULL);
        break;
    case ST_LAMP: {
        box3(c, x, y, z0, 1.6f * k, 1.6f * k, h, 0.0f, col, col, NULL);
        box3(c, x, y, z0 + h, 4.0f * k, 4.0f * k, 4.0f * k, 0.0f, pa_hex(0xFFE9A0), pa_hex(0xFFF4C8), NULL);
        break;
    }
    case ST_BENCH:
        box3(c, x, y, z0 + h * 0.4f, hl, hw, h * 0.25f, p->rot, col, top, NULL);
        box3(c, x - sinf(p->rot) * hw * 0.7f, y + cosf(p->rot) * hw * 0.7f, z0 + h * 0.4f,
             hl, hw * 0.25f, h * 0.7f, p->rot, col, top, NULL);
        break;
    case ST_TREE: {
        box3(c, x, y, z0, 2.4f * k, 2.4f * k, h * 0.45f, 0.0f, pa_hex(0x8A5A34), pa_hex(0x8A5A34), NULL);
        PA_Vec2 cn = P3(x, y, z0 + h * 0.66f);
        float rr = t->len * 0.5f * k * s;
        pa_fill_circle(c, cn.x, cn.y + rr * 0.12f, rr, pa_shade(col, -0.28f));
        pa_fill_circle(c, cn.x, cn.y, rr * 0.94f, col);
        pa_fill_circle(c, cn.x - rr * 0.28f, cn.y - rr * 0.30f, rr * 0.50f, pa_shade(col, 0.18f));
        break;
    }
    case ST_WALKER: {
        box3(c, x, y, z0, hl * 0.5f, hw * 0.5f, h * 0.66f, p->rot, col, top, NULL);
        PA_Vec2 hd = P3(x, y, z0 + h * 0.84f);
        pa_fill_circle(c, hd.x, hd.y, 3.2f * k * s, pa_hex(0xF2C29A));
        break;
    }
    case ST_SCOOTER:
        box3(c, x, y, z0, hl, hw * 0.6f, h * 0.35f, p->rot, col, top, NULL);
        box3(c, x - cosf(p->rot) * hl * 0.2f, y - sinf(p->rot) * hl * 0.2f, z0 + h * 0.35f,
             3.0f * k, 3.0f * k, h * 0.5f, p->rot, pa_hex(0x3E4560), pa_hex(0x3E4560), NULL);
        break;
    case ST_KIOSK:
        box3(c, x, y, z0, hl, hw, h * 0.72f, p->rot, col, top, NULL);
        box3(c, x, y, z0 + h * 0.72f, hl * 1.12f, hw * 1.12f, h * 0.16f, p->rot,
             pa_hex(0xE8455F), pa_hex(0xFFFFFF), NULL);
        break;
    case ST_CAR: case ST_CAB: case ST_VAN: {
        float body = t->style == ST_VAN ? 0.62f : 0.52f;
        box3(c, x, y, z0, hl, hw, h * body, p->rot, col, top, NULL);
        float cab = t->style == ST_VAN ? 0.80f : 0.52f;
        float shift = t->style == ST_VAN ? 0.12f : -0.05f;
        PA_Color glass = pa_hex(0x9CC8F2);
        box3(c, x + cosf(p->rot) * hl * shift, y + sinf(p->rot) * hl * shift, z0 + h * body,
             hl * cab, hw * 0.88f, h * (1.0f - body), p->rot, glass, top, NULL);
        if (t->style == ST_CAB)
            box3(c, x, y, z0 + h, 4.0f * k, 2.0f * k, 3.0f * k, p->rot,
                 pa_hex(0x303440), pa_hex(0xF1F2F7), NULL);
        break;
    }
    case ST_BUS:
        box3(c, x, y, z0, hl, hw, h, p->rot, col, pa_shade(col, 0.22f), &f);
        windows(c, &f, h, t->len, t->wid, pa_hex(0xBFE0FF), 0.45f);
        break;
    case ST_TRUCK: {
        float ca = cosf(p->rot), sa = sinf(p->rot);
        box3(c, x - ca * hl * 0.26f, y - sa * hl * 0.26f, z0, hl * 0.74f, hw, h,
             p->rot, pa_hex(0xE4E6EE), pa_hex(0xF4F5FA), NULL);
        box3(c, x + ca * hl * 0.76f, y + sa * hl * 0.76f, z0, hl * 0.24f, hw * 0.95f, h * 0.72f,
             p->rot, col, top, NULL);
        break;
    }
    default: {
        /* Buildings: body, window grid, then a roof parapet and details. */
        box3(c, x, y, z0, hl, hw, h, p->rot, col, pa_shade(col, 0.16f), &f);
        windows(c, &f, h, t->len * k, t->wid * k, pa_hex(0xCFE6FF), t->style == ST_SHOP ? 0.30f : 0.08f);
        if (t->style == ST_SHOP) {
            /* Awning band over the shop floor. */
            box3(c, x, y, z0 + h * 0.20f, hl * 1.04f, hw * 1.04f, h * 0.07f, p->rot,
                 pa_hex(0xE8455F), pa_hex(0xFFFFFF), NULL);
        }
        box3(c, x, y, z0 + h, hl * 0.86f, hw * 0.86f, 0.6f, p->rot, pa_shade(col, 0.08f),
             pa_shade(col, -0.06f), NULL);
        if (t->style == ST_SPIRE) {
            box3(c, x, y, z0 + h, hl * 0.55f, hw * 0.55f, h * 0.16f, p->rot, col,
                 pa_shade(col, 0.16f), NULL);
            box3(c, x, y, z0 + h * 1.16f, 1.8f * k, 1.8f * k, h * 0.14f, 0.0f,
                 pa_hex(0xD0D4E0), pa_hex(0xD0D4E0), NULL);
        } else if (t->style == ST_TOWER || t->style == ST_HOUSE) {
            float ca = cosf(p->rot), sa = sinf(p->rot);
            box3(c, x + ca * hl * 0.35f - sa * hw * 0.3f, y + sa * hl * 0.35f + ca * hw * 0.3f,
                 z0 + h, hl * 0.18f, hw * 0.18f, 10.0f * k, p->rot, pa_hex(0xC8CCD8),
                 pa_hex(0xE4E7EE), NULL);
        }
        break;
    }
    }
}

static void draw_prop(PA_Canvas *c, const Prop *p) {
    if (!p->taken) { draw_prop_at(c, p, p->x, p->y, 0.0f, 1.0f); return; }
    if (p->eater < 0) return;
    /* Tipping in: pulled toward the centre on a short spiral while sinking
       and shrinking, clipped at the lip by the caller. */
    const Void *e = &V.voids[p->eater];
    float t = pa_smooth(p->sink);
    float ang = p->ang + t * 2.4f;
    float dx = p->x - e->x, dy = p->y - e->y;
    float rad = (1.0f - t) * sqrtf(dx * dx + dy * dy);
    float x = e->x + cosf(ang) * rad, y = e->y + sinf(ang) * rad;
    float k = 1.0f - t * 0.55f;
    draw_prop_at(c, p, x, y, -t * (p->type->h * 1.1f + e->r * 0.6f), k);
}

/* --- the hole ------------------------------------------------------------ */
static void ring_band(PA_Canvas *c, float x, float y, float r0, float r1, float a0, float a1,
                      float z, PA_Color col) {
    PA_Vec2 pts[68];
    int n = 0;
    const int ST = 32;
    for (int i = 0; i <= ST; i++) {
        float a = a0 + (a1 - a0) * (float)i / (float)ST;
        pts[n++] = P3(x + cosf(a) * r1, y + sinf(a) * r1, z);
    }
    for (int i = ST; i >= 0; i--) {
        float a = a0 + (a1 - a0) * (float)i / (float)ST;
        pts[n++] = P3(x + cosf(a) * r0, y + sinf(a) * r0, z);
    }
    pa_fill_poly(c, pts, n, col);
}

/* The rim is a raised lip, so its front half is drawn a second time after any
   sinking props to hide them where they go under the edge. The screen's
   "front" is world angle VIEW_ROT-adjusted: +view-y maps to world angle
   (PI/2 - VIEW_ROT). */
static float front_angle(void) { return PA_PI * 0.5f - VIEW_ROT; }

static void draw_void_back(PA_Canvas *c, const Void *v) {
    float s = V.cam_scale;
    float rim = v->r * 0.26f + 2.0f / s;
    PA_Vec2 ctr = P3(v->x, v->y, 0.0f);

    /* Soft shadow the lip throws on the road. */
    pa_shadow(c, ctr.x, ctr.y + 3.0f, (v->r + rim) * s * 1.08f,
              (v->r + rim) * s * GROUND_K * 1.08f, 0.30f);

    /* The lip: darker outer edge, body in the void's colour, a highlight on
       its upper surface. */
    float lift = rim * 0.35f;
    ring_band(c, v->x, v->y, v->r, v->r + rim, 0.0f, PA_TAU, 0.0f, pa_shade(v->tint, -0.34f));
    ring_band(c, v->x, v->y, v->r, v->r + rim, 0.0f, PA_TAU, lift, v->tint);
    ring_band(c, v->x, v->y, v->r + rim * 0.25f, v->r + rim * 0.62f, 0.0f, PA_TAU, lift,
              pa_shade(v->tint, 0.22f));

    /* The pit: its far inner wall lit faintly in the void's colour, falling to
       black toward the front. */
    PA_Vec2 top = P3(v->x, v->y, lift);
    float rx = v->r * s, ry = v->r * s * GROUND_K;
    PA_Paint pit = pa_linear(0, top.y - ry, 0, top.y + ry);
    pa_stop(&pit, 0.0f, pa_mix(pa_hex(0x0A0820), v->tint, 0.35f));
    pa_stop(&pit, 0.40f, pa_hex(0x07061A));
    pa_stop(&pit, 1.0f, pa_hex(0x000000));
    pa_fill_ellipse_paint(c, top.x, top.y, rx, ry, &pit);
}

static void draw_void_front(PA_Canvas *c, const Void *v) {
    float s = V.cam_scale;
    float rim = v->r * 0.26f + 2.0f / s;
    float lift = rim * 0.35f;
    float fa = front_angle();
    ring_band(c, v->x, v->y, v->r, v->r + rim, fa - PA_PI * 0.5f, fa + PA_PI * 0.5f, 0.0f,
              pa_shade(v->tint, -0.34f));
    ring_band(c, v->x, v->y, v->r, v->r + rim, fa - PA_PI * 0.5f, fa + PA_PI * 0.5f, lift, v->tint);
    ring_band(c, v->x, v->y, v->r + rim * 0.25f, v->r + rim * 0.62f, fa - PA_PI * 0.5f,
              fa + PA_PI * 0.5f, lift, pa_shade(v->tint, 0.22f));

    if (v->player && V.grace > 0.0f) {
        /* Grace ring, so both "why did nothing eat me" and "why did that eat
           me" are answered on screen rather than in a tooltip. */
        float a = 0.25f + 0.2f * sinf(V.time * 6.0f);
        ring_band(c, v->x, v->y, v->r + rim * 1.25f, v->r + rim * 1.55f, 0.0f, PA_TAU, lift,
                  PA_RGBA(255, 255, 255, (int)(a * 255.0f)));
    }
}

static int void_level(const Void *v) {
    int lv = 1;
    for (int i = 0; i < MILESTONE_COUNT; i++) if (v->r >= MILESTONES[i].r) lv++;
    return lv;
}

static void draw_void_label(PA_Canvas *c, const Void *v) {
    float s = V.cam_scale;
    float rim = v->r * 0.26f + 2.0f / s;
    PA_Vec2 top = P3(v->x, v->y, 0.0f);
    float y = top.y - (v->r + rim) * s * GROUND_K - 46.0f;
    char buf[32];
    snprintf(buf, sizeof(buf), "%s", v->player ? "PLAYER" : v->name);
    pa_text_bold(c, buf, top.x, y, 15.0f, v->player ? pa_hex(0x9CD0FF) : pa_shade(v->tint, 0.25f),
                 pa_hex(0x1C2040), PA_ALIGN_CENTER, 1.5f, 1.3f);

    /* Level pill with progress to the next size tier, as the plates' "5/10". */
    int lv = void_level(v);
    float r0 = lv >= 2 ? MILESTONES[lv - 2].r : start_radius();
    float r1 = lv - 1 < MILESTONE_COUNT ? MILESTONES[lv - 1].r : r0 + 40.0f;
    float prog = pa_clamp01((v->r - r0) / (r1 - r0));
    float pw = 78.0f, ph = 20.0f, px = top.x - pw * 0.5f, py = y + 22.0f;
    pa_round_rect(c, px, py, pw, ph, ph * 0.5f, pa_hex(0x1C2040));
    if (prog > 0.05f)
        pa_round_rect(c, px + 2.0f, py + 2.0f, (pw - 4.0f) * prog, ph - 4.0f, (ph - 4.0f) * 0.5f,
                      v->tint);
    snprintf(buf, sizeof(buf), "LVL %d", lv);
    pa_text(c, buf, top.x, py + 4.0f, 11.0f, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.0f);
}

/* --- the ground ---------------------------------------------------------- */
static int on_screen(PA_Canvas *c, float x, float y, float margin) {
    PA_Vec2 p = P3(x, y, 0.0f);
    return p.x > -margin && p.x < (float)c->w + margin && p.y > -margin && p.y < (float)c->h + margin;
}

static void draw_ground(PA_Canvas *c) {
    float R = V.road;
    float ex = V.half_x + R * 0.5f, ey = V.half_y + R * 0.5f;
    float s = V.cam_scale;

    /* Beyond the district: a sandy verge, then sea. */
    ground_rect(c, -ex - 60.0f, -ey - 60.0f, ex + 60.0f, ey + 60.0f, 0.0f, pa_hex(0xF2DCA2));
    ground_rect(c, -ex, -ey, ex, ey, 0.0f, pa_hex(0xA7A2BF));

    /* Lane dashes down the middle of every road, skipping the junctions. */
    float cull = V.span * s + 40.0f;
    if (s > 0.35f) {
        PA_Color dash = PA_RGBA(255, 255, 255, 200);
        for (int axis = 0; axis < 2; axis++) {
            int lines = axis == 0 ? V.cells_y + 1 : V.cells_x + 1;
            int segs = axis == 0 ? V.cells_x : V.cells_y;
            for (int i = 0; i < lines; i++) {
                float line = axis == 0 ? -V.half_y + V.span * (float)i : -V.half_x + V.span * (float)i;
                for (int g = 0; g < segs; g++) {
                    float mid = (axis == 0 ? -V.half_x : -V.half_y) + V.span * ((float)g + 0.5f);
                    if (!on_screen(c, axis == 0 ? mid : line, axis == 0 ? line : mid, cull)) continue;
                    float from = mid - (V.span - R) * 0.5f + 10.0f, to = mid + (V.span - R) * 0.5f - 10.0f;
                    for (float d = from; d < to; d += 30.0f) {
                        float cx = axis == 0 ? d + 7.0f : line, cy = axis == 0 ? line : d + 7.0f;
                        ground_strip(c, cx, cy, 7.0f, 1.4f, axis == 0 ? 0.0f : PA_PI * 0.5f, dash);
                    }
                }
            }
        }
    }

    /* Crosswalks on every approach to every junction. */
    for (int j = 0; j <= V.cells_y; j++) {
        for (int i = 0; i <= V.cells_x; i++) {
            float jx = -V.half_x + V.span * (float)i, jy = -V.half_y + V.span * (float)j;
            if (!on_screen(c, jx, jy, cull)) continue;
            for (int side = 0; side < 4; side++) {
                float dx = side == 0 ? 1.0f : side == 1 ? -1.0f : 0.0f;
                float dy = side == 2 ? 1.0f : side == 3 ? -1.0f : 0.0f;
                float ax = jx + dx * (R * 0.5f + 12.0f), ay = jy + dy * (R * 0.5f + 12.0f);
                if (fabsf(ax) > ex || fabsf(ay) > ey) continue;
                for (int k = 0; k < 6; k++) {
                    float o = -R * 0.40f + R * 0.80f * ((float)k + 0.5f) / 6.0f;
                    float sx = ax + (dx == 0.0f ? o : 0.0f), sy = ay + (dy == 0.0f ? o : 0.0f);
                    ground_strip(c, sx, sy, 10.0f, R * 0.04f, dx != 0.0f ? 0.0f : PA_PI * 0.5f,
                                 PA_RGBA(255, 255, 255, 225));
                }
            }
        }
    }

    /* Blocks: a raised sidewalk slab with a visible kerb, park lawns inset. */
    for (int i = 0; i < V.block_count; i++) {
        const Block *b = &V.blocks[i];
        if (!on_screen(c, b->x, b->y, V.span * s + 60.0f)) continue;
        box3(c, b->x, b->y, 0.0f, b->w * 0.5f, b->h * 0.5f, 3.0f, 0.0f, pa_hex(0xBDB8D0),
             pa_hex(0xDCD8E8), NULL);
        if (b->park) {
            float in = b->w * 0.5f - 16.0f;
            ground_rect(c, b->x - in, b->y - in, b->x + in, b->y + in, 3.0f, pa_hex(0x8ED468));
            ground_rect(c, b->x - in + 5.0f, b->y - 2.0f, b->x + in - 5.0f, b->y + 2.0f, 3.0f,
                        pa_hex(0xE8DDBA));
            ground_rect(c, b->x - 2.0f, b->y - in + 5.0f, b->x + 2.0f, b->y + in - 5.0f, 3.0f,
                        pa_hex(0xE8DDBA));
        } else {
            float in = b->w * 0.5f - 16.0f;
            ground_rect(c, b->x - in, b->y - in, b->x + in, b->y + in, 3.0f, pa_hex(0xCBC6DB));
        }
    }
}

/* --- HUD ----------------------------------------------------------------- */
static void clock_icon(PA_Canvas *c, float x, float y, float r) {
    pa_fill_circle(c, x, y, r, pa_hex(0xF79A2E));
    pa_fill_circle(c, x, y, r * 0.72f, PA_RGB(255, 255, 255));
    pa_line(c, x, y, x, y - r * 0.5f, 2.0f, pa_hex(0x303440));
    pa_line(c, x, y, x + r * 0.38f, y, 2.0f, pa_hex(0x303440));
}

static void skull_icon(PA_Canvas *c, float x, float y, float r) {
    pa_fill_circle(c, x, y - r * 0.15f, r, PA_RGB(255, 255, 255));
    pa_round_rect(c, x - r * 0.55f, y + r * 0.35f, r * 1.1f, r * 0.6f, r * 0.2f, PA_RGB(255, 255, 255));
    pa_fill_circle(c, x - r * 0.38f, y - r * 0.12f, r * 0.26f, pa_hex(0x1C2040));
    pa_fill_circle(c, x + r * 0.38f, y - r * 0.12f, r * 0.26f, pa_hex(0x1C2040));
}

static void draw_hud(PA_Canvas *c) {
    char buf[64];
    float cx = (float)c->w * 0.5f;

    /* Timer pill with the orange clock, centred. */
    int mins = (int)V.timer / 60, secs = (int)V.timer % 60;
    snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
    pa_round_rect(c, cx - 62.0f, 16.0f, 124.0f, 36.0f, 18.0f, PA_RGBA(28, 32, 64, 170));
    clock_icon(c, cx - 40.0f, 34.0f, 15.0f);
    pa_text_bold(c, buf, cx + 12.0f, 24.0f, 18.0f,
                 V.timer < 15.0f ? pa_hex(0xFF6B7A) : PA_RGB(255, 255, 255),
                 pa_hex(0x1C2040), PA_ALIGN_CENTER, 2.0f, 1.3f);

    /* Kills, right. */
    skull_icon(c, (float)c->w - 54.0f, 32.0f, 10.0f);
    snprintf(buf, sizeof(buf), "%d", V.voids[0].kills);
    pa_text_bold(c, buf, (float)c->w - 36.0f, 24.0f, 18.0f, PA_RGB(255, 255, 255),
                 pa_hex(0x1C2040), PA_ALIGN_LEFT, 2.0f, 1.3f);

    /* Share of the district eaten, a yellow bar with the percentage. */
    float pct = V.prop_count > 0 ? (float)V.player_eaten / (float)V.prop_count : 0.0f;
    float bw = (float)c->w * 0.62f < 420.0f ? (float)c->w * 0.62f : 420.0f;
    float bx = cx - bw * 0.5f, by = 64.0f;
    if (bx < 112.0f) { bw -= (112.0f - bx) * 2.0f; bx = 112.0f; }
    pa_round_rect(c, bx, by, bw, 24.0f, 12.0f, pa_hex(0x1C2040));
    if (pct > 0.0f)
        pa_round_rect(c, bx + 3.0f, by + 3.0f, (bw - 6.0f) * pa_clamp01(pct) < 18.0f ? 18.0f
                      : (bw - 6.0f) * pa_clamp01(pct), 18.0f, 9.0f, pa_hex(0xFFC21C));
    snprintf(buf, sizeof(buf), "%.1f%%", (double)(pct * 100.0f));
    pa_text_bold(c, buf, bx + 26.0f, by + 5.0f, 13.0f, PA_RGB(255, 255, 255), pa_hex(0x1C2040),
                 PA_ALIGN_LEFT, 1.5f, 1.2f);
}

static void muncher_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) {
        L.w = c->w; L.h = c->h;
        L.unit = (float)(c->w < c->h ? c->w : c->h);
        L.cy = (float)c->h * 0.56f;
    }
    g_cr = cosf(VIEW_ROT);
    g_sr = sinf(VIEW_ROT);

    pa_clear(c, pa_hex(0x5CC8F2));
    draw_ground(c);

    /* Sorted by depth into buckets; standing props and the sinking ones are
       kept apart so the sinking ones can go down between the pit and its lip. */
    #define BUCKETS 160
    static int head[BUCKETS], sink_head[BUCKETS], next[MAX_PROPS];
    for (int i = 0; i < BUCKETS; i++) { head[i] = -1; sink_head[i] = -1; }
    float span_v = (V.half_x + V.half_y) * 1.2f + 200.0f;
    float margin = 320.0f / V.cam_scale;
    float vis_x = ((float)c->w * 0.5f) / V.cam_scale + margin;
    float vis_y = ((float)c->h * 0.6f) / (V.cam_scale * GROUND_K) + margin;
    float cam_vx = V.cam_x * g_cr - V.cam_y * g_sr, cam_vy = view_depth(V.cam_x, V.cam_y);
    for (int i = V.prop_count - 1; i >= 0; i--) {
        Prop *p = &V.props[i];
        if (p->taken && p->sink >= 1.0f) continue;
        float pvx = p->x * g_cr - p->y * g_sr, pvy = view_depth(p->x, p->y);
        if (fabsf(pvx - cam_vx) > vis_x || fabsf(pvy - cam_vy) > vis_y) continue;
        int b = (int)((pvy + span_v) / (span_v * 2.0f) * (float)(BUCKETS - 1));
        if (b < 0) b = 0;
        if (b >= BUCKETS) b = BUCKETS - 1;
        if (p->taken) { next[i] = sink_head[b]; sink_head[b] = i; }
        else          { next[i] = head[b];      head[b] = i; }
    }

    for (int j = 0; j < V.void_count; j++) if (V.voids[j].alive) draw_void_back(c, &V.voids[j]);

    /* Sinking props, clipped below each pit's front lip so nothing hangs out
       under the road. */
    int cx0 = c->clip_x0, cy0 = c->clip_y0, cx1 = c->clip_x1, cy1 = c->clip_y1;
    for (int j = 0; j < V.void_count; j++) {
        const Void *v = &V.voids[j];
        PA_Vec2 ctr = P3(v->x, v->y, 0.0f);
        int lip = (int)(ctr.y + v->r * V.cam_scale * GROUND_K);
        pa_clip_rect(c, cx0, cy0, cx1 - cx0, (lip < cy1 ? lip : cy1) - cy0);
        for (int b = 0; b < BUCKETS; b++)
            for (int i = sink_head[b]; i >= 0; i = next[i])
                if (V.props[i].eater == j) draw_prop(c, &V.props[i]);
        c->clip_x0 = cx0; c->clip_y0 = cy0; c->clip_x1 = cx1; c->clip_y1 = cy1;
    }

    for (int j = 0; j < V.void_count; j++) if (V.voids[j].alive) draw_void_front(c, &V.voids[j]);

    for (int b = 0; b < BUCKETS; b++)
        for (int i = head[b]; i >= 0; i = next[i]) draw_prop(c, &V.props[i]);
    #undef BUCKETS

    if (V.shockwave > 0.0f) {
        const Void *me = &V.voids[0];
        float rr = me->r * (1.0f + (1.0f - V.shockwave) * 1.6f);
        ring_band(c, me->x, me->y, rr, rr + me->r * 0.10f * V.shockwave + 2.0f, 0.0f, PA_TAU, 0.0f,
                  PA_RGBA(255, 255, 255, (int)(V.shockwave * 150.0f)));
    }

    for (int j = 0; j < V.void_count; j++) if (V.voids[j].alive) draw_void_label(c, &V.voids[j]);

    /* Off-screen rivals: tinted arrows at the edge with their name. */
    for (int j = 1; j < V.void_count; j++) {
        Void *rv = &V.voids[j];
        if (!rv->alive) continue;
        PA_Vec2 sp = P3(rv->x, rv->y, 0.0f);
        if (sp.x > -20.0f && sp.x < (float)c->w + 20.0f && sp.y > 90.0f && sp.y < (float)c->h + 20.0f)
            continue;
        PA_Vec2 me = P3(V.voids[0].x, V.voids[0].y, 0.0f);
        float ang = atan2f(sp.y - me.y, sp.x - me.x);
        float ex = pa_clampf(me.x + cosf(ang) * 2000.0f, 26.0f, (float)c->w - 26.0f);
        float ey = pa_clampf(me.y + sinf(ang) * 2000.0f, 120.0f, (float)c->h - 30.0f);
        int danger = rv->r > V.voids[0].r * 1.1f;
        PA_Vec2 tri[3] = {
            { ex + cosf(ang) * 14.0f, ey + sinf(ang) * 14.0f },
            { ex + cosf(ang + 2.4f) * 12.0f, ey + sinf(ang + 2.4f) * 12.0f },
            { ex + cosf(ang - 2.4f) * 12.0f, ey + sinf(ang - 2.4f) * 12.0f }
        };
        pa_fill_poly(c, tri, 3, danger ? pa_hex(0xE8455F) : rv->tint);
        char buf[32];
        snprintf(buf, sizeof(buf), "LVL%d %s", void_level(rv), rv->name);
        float tx = ex - cosf(ang) * 22.0f, ty = ey - sinf(ang) * 22.0f - 6.0f;
        pa_text_bold(c, buf, tx, ty, 10.0f, rv->tint, pa_hex(0x1C2040),
                     ex < (float)c->w * 0.3f ? PA_ALIGN_LEFT : ex > (float)c->w * 0.7f ? PA_ALIGN_RIGHT
                                                                           : PA_ALIGN_CENTER,
                     1.0f, 1.0f);
    }

    draw_hud(c);

    if (V.banner_t > 0.0f) {
        /* Popped in and cut, never faded: the bold outline is overlapping
           strokes, and at partial alpha every joint shows as a dark knot. */
        float pop = 1.0f + (1.0f - pa_clamp01((2.0f - V.banner_t) * 6.0f)) * 0.3f;
        if (V.banner_t > 0.25f)
            pa_text_bold(c, V.banner, (float)c->w * 0.5f, (float)c->h * 0.19f, 28.0f * pop,
                         PA_RGB(255, 255, 255), pa_hex(0x1C2040), PA_ALIGN_CENTER, 3.0f, 2.0f);
    }

    if (V.eaten < 6 && !V.over) {
        pa_text_bold(c, "DRAG TO STEER", (float)c->w * 0.5f, (float)c->h - 52.0f, 14.0f,
                     PA_RGB(255, 255, 255), pa_hex(0x1C2040), PA_ALIGN_CENTER, 3.0f, 1.2f);
    }

    if (V.over) {
        /* Results card: the standings, as the plates' end screen lists them. */
        float a = pa_clamp01(V.over_t * 2.4f);
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(28, 32, 64, (int)(a * 150.0f)));
        float pw = (float)c->w * 0.82f, ph = 110.0f + 44.0f * (float)V.void_count;
        float px = ((float)c->w - pw) * 0.5f, py = (float)c->h * 0.30f + (1.0f - a) * 40.0f;
        pa_round_rect(c, px, py + 6.0f, pw, ph, 22.0f, PA_RGBA(20, 22, 50, (int)(a * 120.0f)));
        pa_round_rect(c, px, py, pw, ph, 22.0f, PA_RGBA(255, 255, 255, (int)(a * 255.0f)));
        const char *title = V.won ? "DISTRICT TAKEN!" : V.voids[0].alive ? "TIME'S UP" : "SWALLOWED";
        pa_text_bold(c, title, (float)c->w * 0.5f, py - 20.0f, 26.0f,
                     V.won ? pa_hex(0xFFC21C) : PA_RGB(255, 255, 255), pa_hex(0x1C2040),
                     PA_ALIGN_CENTER, 3.0f, 2.0f);

        int order[MAX_VOIDS];
        for (int i = 0; i < V.void_count; i++) order[i] = i;
        for (int i = 0; i < V.void_count; i++)
            for (int j = i + 1; j < V.void_count; j++)
                if (standing(&V.voids[order[j]]) < standing(&V.voids[order[i]])) {
                    int t = order[i]; order[i] = order[j]; order[j] = t;
                }
        char buf[64];
        for (int i = 0; i < V.void_count; i++) {
            const Void *v = &V.voids[order[i]];
            float ry = py + 34.0f + 44.0f * (float)i;
            if (v->player)
                pa_round_rect(c, px + 12.0f, ry - 8.0f, pw - 24.0f, 38.0f, 12.0f,
                              PA_RGBA(47, 123, 234, (int)(a * 40.0f)));
            snprintf(buf, sizeof(buf), "%d", i + 1);
            pa_fill_circle(c, px + 38.0f, ry + 11.0f, 14.0f, pa_alpha(v->tint, a));
            pa_text(c, buf, px + 38.0f, ry + 4.0f, 13.0f, PA_RGBA(255, 255, 255, (int)(a * 255.0f)),
                    PA_ALIGN_CENTER, 1.0f);
            pa_text(c, v->player ? "YOU" : v->name, px + 64.0f, ry + 2.0f, 16.0f,
                    PA_RGBA(28, 32, 64, (int)(a * (v->alive ? 255.0f : 110.0f))), PA_ALIGN_LEFT, 2.0f);
            snprintf(buf, sizeof(buf), "%d", v->score);
            pa_text(c, buf, px + pw - 24.0f, ry + 2.0f, 16.0f,
                    PA_RGBA(28, 32, 64, (int)(a * 255.0f)), PA_ALIGN_RIGHT, 2.0f);
        }
        snprintf(buf, sizeof(buf), "BEST %d", g_best);
        pa_text(c, buf, (float)c->w * 0.5f, py + ph - 40.0f, 13.0f,
                PA_RGBA(28, 32, 64, (int)(a * 170.0f)), PA_ALIGN_CENTER, 3.0f);
    }
}

static void muncher_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    pa_fill_rect(c, x, y, w, h, pa_hex(0xA7A2BF));
    /* Two diagonal roads' worth of blocks in the plates' palette. */
    for (int k = 0; k < 4; k++) {
        float bx = x + w * ((k & 1) ? 0.56f : 0.06f), by = y + h * ((k & 2) ? 0.56f : 0.08f);
        pa_round_rect(c, bx, by + 3.0f, w * 0.38f, h * 0.36f, 4.0f, pa_hex(0xBDB8D0));
        pa_round_rect(c, bx, by, w * 0.38f, h * 0.36f, 4.0f, pa_hex(0xDCD8E8));
    }
    static const uint32_t cols[3] = { 0xF4A6B8, 0x8FB8F0, 0xB7A6E8 };
    for (int k = 0; k < 3; k++) {
        float bx = x + w * (0.12f + 0.62f * (float)(k & 1)), by = y + h * (0.12f + 0.52f * (float)(k >> 1));
        pa_fill_rect(c, bx, by, w * 0.14f, h * 0.20f, pa_shade(pa_hex(cols[k]), -0.2f));
        pa_fill_rect(c, bx, by - h * 0.05f, w * 0.14f, h * 0.06f, pa_hex(cols[k]));
    }
    float cx = x + w * 0.5f, cy = y + h * 0.52f;
    float grow_t = 0.5f + 0.5f * sinf(t * 1.1f);
    float r = (w < h ? w : h) * (0.16f + grow_t * 0.08f);
    pa_fill_ellipse(c, cx, cy, r * 1.22f, r * 0.9f, pa_hex(0x1D56B0));
    pa_fill_ellipse(c, cx, cy - 2.0f, r * 1.22f, r * 0.9f, pa_hex(0x2F7BEA));
    PA_Paint pit = pa_linear(0, cy - r * 0.7f, 0, cy + r * 0.7f);
    pa_stop(&pit, 0.0f, pa_hex(0x1A1850));
    pa_stop(&pit, 1.0f, pa_hex(0x000000));
    pa_fill_ellipse_paint(c, cx, cy - 2.0f, r, r * 0.72f, &pit);
}

const PA_Game PA_GAME_VOIDMUNCHER = {
    "voidmuncher", "Void Muncher", "Arena",
    "You are a hole. Swallow the district before three rival voids eat it out from under you.",
    PA_RGB(47, 123, 234),
    muncher_start, muncher_stop, muncher_update, muncher_render, muncher_thumb
};
