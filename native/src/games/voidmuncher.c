/* ===========================================================================
   VOID MUNCHER - native port
   Drag to steer a hole across a generated district. It swallows anything
   smaller than its mouth, grows with every bite, and rival voids are doing the
   same to the same city. Ninety seconds decides who took the block.

   The district is deliberately not square: on a portrait screen a square one
   leaves fat empty bands top and bottom once the camera has pulled back far
   enough to show all of it. The camera's zoom-out is clamped at exactly the
   point where the whole district fits, for the same reason.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define MAX_PROPS  2600
#define MAX_VOIDS  5
#define MAX_BLOCKS 200

typedef struct {
    const char *key;
    int   tier;
    float r, h, w;
    int   value;
    uint32_t colour;
    int   round;
    float weight;
} PropType;

/* Tier gaps are wide on purpose: each one is a visible change in what the city
   looks like from inside, and crossing one is the moment a run gets loud. */
static const PropType TYPES[] = {
    { "cone",    0,  6.f,   9.f,  11.f,   1, 0xF2803C, 1, 12.f },
    { "bin",     0,  7.f,  12.f,  13.f,   2, 0x4E7A63, 1,  9.f },
    { "sign",    0,  6.f,  17.f,   8.f,   2, 0xC9D3DE, 1,  8.f },
    { "bench",   1, 11.f,   8.f,  26.f,   4, 0x9A6A3F, 0,  8.f },
    { "tree",    1, 12.f,  30.f,  24.f,   5, 0x3E8F5A, 1, 11.f },
    { "walker",  1,  9.f,  20.f,  12.f,   6, 0xE8B48A, 1, 10.f },
    { "scooter", 2, 15.f,  14.f,  30.f,  10, 0xE2455F, 0,  7.f },
    { "kiosk",   2, 17.f,  26.f,  34.f,  14, 0xE9C33F, 0,  5.f },
    { "car",     3, 21.f,  18.f,  46.f,  26, 0x3D8BFF, 0,  9.f },
    { "cab",     3, 21.f,  18.f,  46.f,  28, 0xF6C24B, 0,  5.f },
    { "van",     3, 24.f,  26.f,  52.f,  34, 0xEDEDF2, 0,  4.f },
    { "bus",     4, 33.f,  32.f,  78.f,  70, 0xD9483C, 0,  4.f },
    { "truck",   4, 35.f,  34.f,  82.f,  78, 0x5F6B8A, 0,  3.f },
    { "hut",     4, 36.f,  44.f,  74.f,  84, 0xB0663F, 0,  3.f },
    { "house",   5, 52.f,  78.f, 106.f, 190, 0xD5B48A, 0,  4.f },
    { "shop",    5, 56.f,  92.f, 114.f, 210, 0x7A6DC4, 0,  3.f },
    { "tower",   6, 78.f, 168.f, 156.f, 640, 0x8C9AB4, 0,  2.f },
    { "spire",   6, 70.f, 210.f, 138.f, 720, 0xB6C2D4, 0,  1.f }
};
#define TYPE_COUNT ((int)(sizeof(TYPES) / sizeof(TYPES[0])))

typedef struct {
    float x, y;
    int   bx, by;
    const PropType *type;
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
    int   score;
    PA_Color tint;
    float vx, vy;
    float think, skill;
    int   target;          /* prop index, or -1 */
    int   chase;           /* void index, or -1 */
} Void;

typedef struct { float x, y, w, h; } Block;

static const char *RIVAL_NAMES[] = { "ABYSS", "GULP", "NIL", "CHASM", "MAW", "SINK" };
static const uint32_t RIVAL_TINTS[] = { 0xE2455F, 0x8C5BFF, 0x2FD6A4, 0xF6A33F };

static const struct { float r; const char *text; } MILESTONES[] = {
    { 20.f, "BENCHES" }, { 26.f, "TREES" }, { 32.f, "SCOOTERS" }, { 40.f, "CARS" },
    { 52.f, "VANS" },    { 66.f, "BUSES" }, { 84.f, "HOUSES" },   { 108.f, "SHOPS" },
    { 140.f, "TOWERS" }
};
#define MILESTONE_COUNT ((int)(sizeof(MILESTONES) / sizeof(MILESTONES[0])))

typedef struct {
    Prop   props[MAX_PROPS];
    int    prop_count;
    Block  blocks[MAX_BLOCKS];
    int    block_count;
    float  half_x, half_y, span;
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

static struct { int w, h; float unit; } L;

/* ------------------------------------------------------------- generation -- */
/** Weighted pick, biased so outer rings hold the big stuff and early growth
    happens near the middle. */
static const PropType *roll_prop(PA_Rng *r, int level, float ring) {
    float total = 0.0f;
    float weights[TYPE_COUNT];
    float want = ring * 6.4f;

    for (int i = 0; i < TYPE_COUNT; i++) {
        float gap = fabsf((float)TYPES[i].tier - want);
        float w = TYPES[i].weight * expf(-gap * gap * 0.55f);
        if (TYPES[i].tier >= 5 && level < 2) w *= 0.35f;
        weights[i] = w > 0.02f ? w : 0.0f;
        total += weights[i];
    }
    if (total <= 0.0f) return &TYPES[0];

    float roll = pa_rng_next(r) * total;
    for (int i = 0; i < TYPE_COUNT; i++) {
        roll -= weights[i];
        if (roll <= 0.0f) return &TYPES[i];
    }
    return &TYPES[TYPE_COUNT - 1];
}

static void build_city(int level) {
    PA_Rng r;
    pa_rng_seed(&r, 0x5EEDu ^ ((uint32_t)level * 7919u));

    V.prop_count = 0;
    V.block_count = 0;

    V.cells_x = 6 + (level / 3 > 3 ? 3 : level / 3);
    float aspect = pa_clampf((float)L.h / (float)(L.w > 0 ? L.w : 1), 1.0f, 2.4f);
    V.cells_y = (int)(V.cells_x * aspect + 0.5f);
    if (V.cells_y < V.cells_x) V.cells_y = V.cells_x;
    if (V.cells_y > 16) V.cells_y = 16;

    V.span = (1240.0f + (float)level * 60.0f) / (float)V.cells_x;
    float road = V.span * 0.28f;
    V.half_x = V.span * (float)V.cells_x * 0.5f;
    V.half_y = V.span * (float)V.cells_y * 0.5f;
    float half = V.half_x > V.half_y ? V.half_x : V.half_y;

    for (int cy = 0; cy < V.cells_y; cy++) {
        for (int cx = 0; cx < V.cells_x; cx++) {
            float bx = -V.half_x + V.span * ((float)cx + 0.5f);
            float by = -V.half_y + V.span * ((float)cy + 0.5f);
            float bw = V.span - road, bh = V.span - road;

            if (V.block_count < MAX_BLOCKS) {
                Block *b = &V.blocks[V.block_count++];
                b->x = bx; b->y = by; b->w = bw; b->h = bh;
            }

            float ring = pa_clamp01(sqrtf(bx * bx + by * by) / (half * 1.05f));
            int count = (int)(pa_lerpf(30.0f, 13.0f, ring) * (1.0f + (float)level * 0.04f));
            int placed = 0;

            /* Fixed attempt budget rather than retrying until success: a dense
               block can genuinely run out of room, and a retry loop spins on
               that. A missing prop is invisible; a hang is not. */
            for (int n = 0; n < count * 4 && placed < count && V.prop_count < MAX_PROPS; n++) {
                const PropType *proto = roll_prop(&r, level, ring);
                float px = bx + pa_rng_range(&r, -bw * 0.44f, bw * 0.44f);
                float py = by + pa_rng_range(&r, -bh * 0.44f, bh * 0.44f);

                int ok = 1;
                for (int t = V.prop_count - 1; t >= 0; t--) {
                    Prop *q = &V.props[t];
                    if (q->bx != cx || q->by != cy) continue;
                    float need = (q->type->r + proto->r) * 0.88f;
                    float dx = px - q->x, dy = py - q->y;
                    if (dx * dx + dy * dy < need * need) { ok = 0; break; }
                }
                if (!ok) continue;
                placed++;

                Prop *p = &V.props[V.prop_count++];
                memset(p, 0, sizeof(*p));
                p->x = px; p->y = py;
                p->bx = cx; p->by = cy;
                p->type = proto;
                p->eater = -1;
                p->mask = (uint32_t)pa_rng_int(&r, 0, 0xFFFF);
            }
        }
    }

    /* Clear a landing pad so the opening seconds are never a wall of trucks. */
    for (int i = V.prop_count - 1; i >= 0; i--) {
        if (V.props[i].type->tier >= 2 &&
            V.props[i].x * V.props[i].x + V.props[i].y * V.props[i].y < 165.0f * 165.0f) {
            V.props[i] = V.props[--V.prop_count];
        }
    }
}

/* ------------------------------------------------------------------ setup -- */
static float start_radius(void) { return 16.0f; }

static float target_scale(float r) {
    /* Never zoom out past the point where the whole district fits: beyond that
       the city becomes a postage stamp in a sea of background, which is the
       opposite of what the zoom is for. */
    float floor_x = (float)L.w / (V.half_x * 2.0f + 150.0f);
    float floor_y = (float)L.h / (V.half_y * 2.0f + 150.0f);
    float floor_scale = floor_x > floor_y ? floor_x : floor_y;
    return pa_clampf(L.unit / (r * 22.0f + 330.0f), floor_scale, 1.1f);
}

static void muncher_start(void) {
    if (!g_best_loaded) {
        g_best = pa_save_get("voidmuncher.best", 0);
        g_best_loaded = 1;
    }
    if (L.w == 0) { L.w = 540; L.h = 960; L.unit = 540.0f; }

    int level = pa_save_get("voidmuncher.level", 1);
    if (level < 1) level = 1;

    memset(&V, 0, sizeof(V));
    V.level = level;
    build_city(level);

    V.void_count = 0;
    Void *me = &V.voids[V.void_count++];
    me->player = 1; me->alive = 1; me->name = "YOU";
    me->r = start_radius();
    me->tint = pa_hex(0x5DE0FF);
    me->target = -1; me->chase = -1;

    int rivals = pa_clampf(2.0f + (float)(level / 2), 2.0f, 4.0f);
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
            grow(a, PA_PI * b->r * b->r * 0.5f);
            a->score += b->score / 2;
            if (a->player) banner("SWALLOWED");
            else if (b->player) { finish(0); return; }
        }
    }
}

static void muncher_update(float dt, const PA_Input *in) {
    V.time += dt;
    if (V.banner_t > 0.0f) V.banner_t -= dt;

    if (V.over) {
        V.over_t += dt;
        if (V.over_t > 3.0f) muncher_start();
        return;
    }

    V.timer -= dt;
    if (V.timer <= 0.0f) { V.timer = 0.0f; finish(-1); return; }
    if (V.grace > 0.0f) V.grace -= dt;
    V.shockwave = V.shockwave > 0.0f ? V.shockwave - dt * 2.4f : 0.0f;

    Void *me = &V.voids[0];
    float speed = 250.0f;

    if (in->down) {
        float dx = in->x - (float)L.w * 0.5f;
        float dy = in->y - (float)L.h * 0.5f;
        float len = sqrtf(dx * dx + dy * dy);
        float reach = L.unit * 0.22f;
        float mag = pa_clamp01(len / reach);
        if (len > 4.0f) {
            me->vx = dx / len * speed * mag;
            me->vy = dy / len * speed * mag;
        }
    } else {
        float kx = 0.0f, ky = 0.0f;
        if (in->keys[PA_KEY_LEFT])  kx -= 1.0f;
        if (in->keys[PA_KEY_RIGHT]) kx += 1.0f;
        if (in->keys[PA_KEY_UP])    ky -= 1.0f;
        if (in->keys[PA_KEY_DOWN])  ky += 1.0f;
        if (kx != 0.0f || ky != 0.0f) {
            float kl = sqrtf(kx * kx + ky * ky);
            me->vx = kx / kl * speed;
            me->vy = ky / kl * speed;
        } else {
            me->vx *= expf(-7.0f * dt);
            me->vy *= expf(-7.0f * dt);
        }
    }

    for (int i = 1; i < V.void_count; i++) {
        if (V.voids[i].alive) steer_rival(&V.voids[i], dt);
    }

    float bx = V.half_x + 40.0f, by = V.half_y + 40.0f;
    for (int i = 0; i < V.void_count; i++) {
        Void *v = &V.voids[i];
        if (!v->alive) continue;
        v->x = pa_clampf(v->x + v->vx * dt, -bx, bx);
        v->y = pa_clampf(v->y + v->vy * dt, -by, by);
    }

    /* Eating pass. Props are static, so a plain distance test is enough. */
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
            V.props[i].sink = pa_clampf(V.props[i].sink + dt * 2.6f, 0.0f, 1.0f);
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

    /* Keep the district filling the frame: once the view is wider than the city
       on an axis, the camera locks to the centre on that axis. */
    float lim_x = V.half_x + 30.0f, lim_y = V.half_y + 30.0f;
    float half_view_x = ((float)L.w * 0.5f) / V.cam_scale;
    float half_view_y = ((float)L.h * 0.5f) / V.cam_scale;
    V.cam_x = half_view_x >= lim_x ? 0.0f : pa_clampf(V.cam_x, -(lim_x - half_view_x), lim_x - half_view_x);
    V.cam_y = half_view_y >= lim_y ? 0.0f : pa_clampf(V.cam_y, -(lim_y - half_view_y), lim_y - half_view_y);
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
static PA_Vec2 to_screen(float wx, float wy) {
    PA_Vec2 p;
    p.x = (float)L.w * 0.5f + (wx - V.cam_x) * V.cam_scale;
    p.y = (float)L.h * 0.5f + (wy - V.cam_y) * V.cam_scale;
    return p;
}

static void draw_prop(PA_Canvas *c, Prop *p) {
    float shrink = p->taken ? (1.0f - pa_smooth(p->sink)) : 1.0f;
    if (shrink <= 0.02f) return;

    PA_Vec2 s = to_screen(p->x, p->y);
    float px = s.x, py = s.y;
    float sc = V.cam_scale;

    if (p->taken && p->eater >= 0) {
        /* Spiral into the eater's mouth while shrinking. */
        Void *e = &V.voids[p->eater];
        PA_Vec2 mouth = to_screen(e->x, e->y);
        float t = pa_smooth(p->sink);
        float ang = p->ang + t * 3.1f;
        float rad = (1.0f - t) * sqrtf((px - mouth.x) * (px - mouth.x) +
                                       (py - mouth.y) * (py - mouth.y));
        px = mouth.x + cosf(ang) * rad;
        py = mouth.y + sinf(ang) * rad + t * 18.0f * sc;
    }

    float w = p->type->w * sc * shrink;
    if (w < 1.2f) return;
    float d = w * 0.60f;
    float hh = p->type->h * sc * shrink;
    PA_Color col = pa_hex(p->type->colour);

    /* Soft contact shadow. Flat-shaded art has no lighting to read depth from,
       so this is carrying all of it - a hard ellipse reads as a sticker. */
    pa_shadow(c, px, py + d * 0.14f, w * 0.56f, d * 0.50f, 0.34f);

    float radius = w * 0.14f < 5.0f ? w * 0.14f : 5.0f;
    PA_Color side = pa_shade(col, -0.34f);
    PA_Color skirt = pa_shade(col, -0.48f);

    if (p->type->round) {
        pa_fill_rect(c, px - w * 0.5f, py - hh, w, hh, side);
        pa_fill_ellipse(c, px, py, w * 0.5f, d * 0.5f, side);
        /* A darker band at the base grounds a cylinder that would otherwise be
           one flat column of colour. */
        pa_fill_ellipse(c, px, py - hh * 0.14f, w * 0.5f, d * 0.5f, skirt);
        pa_fill_ellipse(c, px, py - hh, w * 0.5f, d * 0.5f, col);
    } else {
        pa_round_rect(c, px - w * 0.5f, py - hh, w, hh + d * 0.5f, radius, side);
        pa_round_rect(c, px - w * 0.5f, py - hh * 0.22f, w, hh * 0.22f + d * 0.5f,
                      radius, skirt);
        pa_round_rect(c, px - w * 0.5f, py - hh - d * 0.5f, w, d, radius, col);
        /* A lighter parapet inset on the roof: the single cheapest thing that
           separates a building from a coloured brick. */
        if (p->type->tier >= 4 && w > 14.0f) {
            pa_round_rect(c, px - w * 0.36f, py - hh - d * 0.34f, w * 0.72f, d * 0.68f,
                          radius * 0.6f, pa_shade(col, 0.18f));
        }
    }

    /* Windows on anything tall enough to read as a building. */
    if (!p->type->round && p->type->tier >= 4 && hh > 22.0f) {
        int cols = (int)(w / (7.0f * sc + 4.0f));
        int rows = (int)(hh / (11.0f * sc + 5.0f));
        if (cols < 2) cols = 2;
        if (rows < 1) rows = 1;
        float ww = w / (float)cols * 0.44f, wh = hh / (float)rows * 0.42f;
        for (int cx = 0; cx < cols; cx++) {
            for (int cy = 0; cy < rows; cy++) {
                if ((p->mask >> ((cx * 3 + cy) % 16)) & 1) continue;
                /* Two window tints rather than one, so a tower is not a
                   perfectly regular grid of identical dots. */
                int warm = ((cx * 5 + cy * 3 + (int)(p->mask & 7)) % 3) != 0;
                pa_fill_rect(c,
                    px - w * 0.5f + ((float)cx + 0.5f) * (w / (float)cols) - ww * 0.5f,
                    py - hh + ((float)cy + 0.4f) * (hh / (float)rows) - wh * 0.5f,
                    ww, wh,
                    warm ? PA_RGBA(255, 240, 190, 165) : PA_RGBA(150, 205, 255, 120));
            }
        }
    }
}

static void draw_void(PA_Canvas *c, Void *v) {
    if (!v->alive) return;
    PA_Vec2 s = to_screen(v->x, v->y);
    float r = v->r * V.cam_scale;
    float d = r * 0.66f;

    /* Rim glow in the void's tint - the only way to tell four black holes apart
       at a glance. */
    PA_Paint glow = pa_radial(s.x, s.y, r * 0.6f, r * 1.8f);
    pa_stop(&glow, 0.0f, pa_alpha(v->tint, v->player ? 0.62f : 0.40f));
    pa_stop(&glow, 0.55f, pa_alpha(v->tint, v->player ? 0.24f : 0.14f));
    pa_stop(&glow, 1.0f, pa_alpha(v->tint, 0.0f));
    pa_fill_ellipse_paint(c, s.x, s.y, r * 1.8f, d * 1.8f, &glow);

    PA_Paint core = pa_radial(s.x, s.y - d * 0.2f, r * 0.1f, r);
    pa_stop(&core, 0.0f, pa_hex(0x000000));
    pa_stop(&core, 0.7f, pa_hex(0x05040E));
    pa_stop(&core, 1.0f, pa_hex(0x100D24));
    pa_fill_ellipse_paint(c, s.x, s.y, r, d, &core);

    float lw = r * (v->player ? 0.10f : 0.06f);
    pa_stroke_circle(c, s.x, s.y, r, lw < 2.4f ? 2.4f : lw, v->tint);

    if (v->player && V.grace > 0.0f) {
        /* Grace ring, so both "why did nothing eat me" and "why did that eat
           me" are answered on screen rather than in a tooltip. */
        float a = 0.14f + 0.14f * sinf(V.time * 6.0f);
        pa_stroke_circle(c, s.x, s.y, r * 1.3f, r * 0.08f < 2.0f ? 2.0f : r * 0.08f,
                         PA_RGBA(255, 255, 255, (int)(a * 255.0f)));
    }

    if (!v->player) {
        pa_text(c, v->name, s.x, s.y - d - 20.0f, 12.0f, v->tint, PA_ALIGN_CENTER, 1.5f);
    }
}

static void muncher_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) {
        L.w = c->w; L.h = c->h;
        L.unit = (float)(c->w < c->h ? c->w : c->h);
    }

    pa_clear(c, pa_hex(0x181D2C));

    PA_Vec2 tl = to_screen(-V.half_x, -V.half_y);
    PA_Vec2 br = to_screen(V.half_x, V.half_y);
    pa_fill_rect(c, tl.x, tl.y, br.x - tl.x, br.y - tl.y, pa_hex(0x333A50));

    for (int i = 0; i < V.block_count; i++) {
        Block *b = &V.blocks[i];
        PA_Vec2 p = to_screen(b->x - b->w * 0.5f, b->y - b->h * 0.5f);
        float bw = b->w * V.cam_scale, bh = b->h * V.cam_scale;
        /* A kerb under each block: the grid was blocks floating on tarmac with
           nothing marking where one ends. */
        pa_round_rect(c, p.x - 2.0f, p.y - 2.0f, bw + 4.0f, bh + 4.0f,
                      9.0f * V.cam_scale, pa_hex(0x8E97A8));
        pa_round_rect(c, p.x, p.y, bw, bh, 8.0f * V.cam_scale, pa_hex(0x49A05F));
    }

    /* Painter's algorithm: sort by world y so a hole sits in front of the bus
       it is about to take. Voids go down first - they are a floor feature. */
    for (int j = 0; j < V.void_count; j++) draw_void(c, &V.voids[j]);

    /* Insertion into a bucketed order keeps this linear: props barely move, so
       a full sort every frame would be paying for a stability we already have. */
    float cull_x = ((float)c->w * 0.5f) / V.cam_scale + 200.0f;
    float cull_y = ((float)c->h * 0.5f) / V.cam_scale + 260.0f;
    #define BUCKETS 96
    static int head[BUCKETS], next[MAX_PROPS];
    for (int i = 0; i < BUCKETS; i++) head[i] = -1;
    for (int i = V.prop_count - 1; i >= 0; i--) {
        Prop *p = &V.props[i];
        if (p->taken && p->sink >= 1.0f) continue;
        if (fabsf(p->x - V.cam_x) > cull_x || fabsf(p->y - V.cam_y) > cull_y) continue;
        float t = pa_clamp01((p->y + V.half_y) / (V.half_y * 2.0f + 1.0f));
        int b = (int)(t * (BUCKETS - 1));
        next[i] = head[b];
        head[b] = i;
    }
    for (int b = 0; b < BUCKETS; b++) {
        for (int i = head[b]; i >= 0; i = next[i]) draw_prop(c, &V.props[i]);
    }
    #undef BUCKETS

    if (V.shockwave > 0.0f) {
        PA_Vec2 m = to_screen(V.voids[0].x, V.voids[0].y);
        float rr = V.voids[0].r * V.cam_scale * (1.0f + (1.0f - V.shockwave) * 2.4f);
        pa_stroke_circle(c, m.x, m.y, rr,
                         6.0f * V.cam_scale * V.shockwave < 2.0f ? 2.0f : 6.0f * V.cam_scale * V.shockwave,
                         PA_RGBA(93, 224, 255, (int)(V.shockwave * 128.0f)));
    }

    /* Off-screen rival arrows, so a hunting void is never a surprise. */
    for (int j = 1; j < V.void_count; j++) {
        Void *rv = &V.voids[j];
        if (!rv->alive) continue;
        PA_Vec2 sp = to_screen(rv->x, rv->y);
        if (sp.x > -40.0f && sp.x < (float)c->w + 40.0f &&
            sp.y > -40.0f && sp.y < (float)c->h + 40.0f) continue;
        float ang = atan2f(rv->y - V.cam_y, rv->x - V.cam_x);
        float ex = (float)c->w * 0.5f + cosf(ang) * (float)(c->w < c->h ? c->w : c->h) * 0.40f;
        float ey = (float)c->h * 0.5f + sinf(ang) * (float)(c->w < c->h ? c->w : c->h) * 0.40f;
        int danger = rv->r > V.voids[0].r * 1.1f;
        PA_Vec2 tri[3] = {
            { ex + cosf(ang) * 13.0f, ey + sinf(ang) * 13.0f },
            { ex + cosf(ang + 2.4f) * 12.0f, ey + sinf(ang + 2.4f) * 12.0f },
            { ex + cosf(ang - 2.4f) * 12.0f, ey + sinf(ang - 2.4f) * 12.0f }
        };
        pa_fill_poly(c, tri, 3, danger ? PA_RGBA(226, 69, 95, 230) : pa_alpha(rv->tint, 0.6f));
    }

    pa_vignette(c, 0.5f);

    pa_hud_scrim(c, 96.0f);

    /* HUD */
    char buf[64];
    int mins = (int)V.timer / 60, secs = (int)V.timer % 60;
    snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
    pa_text(c, buf, 104.0f, 34.0f, 20.0f,
            V.timer < 15.0f ? PA_RGB(255, 107, 122) : PA_RGB(255, 255, 255),
            PA_ALIGN_LEFT, 2.0f);

    snprintf(buf, sizeof(buf), "SIZE %d", (int)V.voids[0].r);
    pa_text(c, buf, (float)c->w * 0.5f + 40.0f, 34.0f, 14.0f,
            PA_RGBA(255, 255, 255, 180), PA_ALIGN_CENTER, 2.0f);

    snprintf(buf, sizeof(buf), "%d", V.voids[0].score);
    pa_text(c, buf, (float)c->w - 20.0f, 34.0f, 20.0f,
            PA_RGB(255, 201, 60), PA_ALIGN_RIGHT, 2.0f);

    float bx2 = 20.0f;
    for (int j = 0; j < V.void_count; j++) {
        Void *v = &V.voids[j];
        snprintf(buf, sizeof(buf), "%s %d", v->name, v->score);
        pa_text(c, buf, bx2, 66.0f, 11.0f,
                v->alive ? (v->player ? PA_RGB(255, 255, 255) : pa_alpha(v->tint, 0.85f))
                         : PA_RGBA(255, 255, 255, 70),
                PA_ALIGN_LEFT, 2.0f);
        bx2 += pa_text_width(buf, 11.0f, 2.0f) + 18.0f;
    }

    if (V.banner_t > 0.0f) {
        pa_text(c, V.banner, (float)c->w * 0.5f, (float)c->h * 0.30f, 24.0f,
                PA_RGBA(255, 255, 255, (int)(pa_clamp01(V.banner_t) * 220.0f)),
                PA_ALIGN_CENTER, 5.0f);
    }

    if (V.eaten < 6 && !V.over) {
        pa_text(c, "DRAG TO STEER", (float)c->w * 0.5f, (float)c->h - 46.0f, 12.0f,
                PA_RGBA(255, 255, 255, 150), PA_ALIGN_CENTER, 4.0f);
    }

    if (V.over) {
        float a = pa_clamp01(V.over_t * 2.4f);
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(10, 8, 24, (int)(a * 180.0f)));
        pa_text(c, V.won ? "DISTRICT TAKEN" : "OUT OF TIME",
                (float)c->w * 0.5f, (float)c->h * 0.42f, 30.0f,
                V.won ? PA_RGB(126, 240, 160) : PA_RGB(255, 107, 122),
                PA_ALIGN_CENTER, 5.0f);
        snprintf(buf, sizeof(buf), "SCORE %d - BEST %d", V.voids[0].score, g_best);
        pa_text(c, buf, (float)c->w * 0.5f, (float)c->h * 0.50f, 13.0f,
                PA_RGBA(255, 255, 255, 180), PA_ALIGN_CENTER, 3.0f);
    }
}

static void muncher_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    pa_fill_rect(c, x, y, w, h, pa_hex(0x2B3145));
    pa_round_rect(c, x + w * 0.06f, y + h * 0.10f, w * 0.40f, h * 0.34f, 5.0f, pa_hex(0x3A6E52));
    pa_round_rect(c, x + w * 0.55f, y + h * 0.10f, w * 0.39f, h * 0.34f, 5.0f, pa_hex(0x3A6E52));
    pa_round_rect(c, x + w * 0.06f, y + h * 0.55f, w * 0.40f, h * 0.35f, 5.0f, pa_hex(0x3A6E52));
    pa_round_rect(c, x + w * 0.55f, y + h * 0.55f, w * 0.39f, h * 0.35f, 5.0f, pa_hex(0x3A6E52));

    float cx = x + w * 0.5f, cy = y + h * 0.56f;
    float grow_t = 0.5f + 0.5f * sinf(t * 1.1f);
    float r = (w < h ? w : h) * (0.14f + grow_t * 0.10f);

    struct { float px, py; uint32_t col; } boxes[3] = {
        { 0.22f, 0.24f, 0x3D8BFF }, { 0.80f, 0.30f, 0xE2455F }, { 0.30f, 0.82f, 0xE9C33F }
    };
    for (int i = 0; i < 3; i++) {
        float ang = t * 1.6f + (float)i * 2.1f;
        float pull = pa_wrapf(t * 0.5f + (float)i * 0.33f, 1.0f);
        float px = pa_lerpf(x + boxes[i].px * w, cx, pull) + cosf(ang) * (1.0f - pull) * 6.0f;
        float py = pa_lerpf(y + boxes[i].py * h, cy, pull) + sinf(ang) * (1.0f - pull) * 6.0f;
        float sz = (w < h ? w : h) * 0.13f * (1.0f - pull * 0.85f);
        pa_round_rect(c, px - sz * 0.5f, py - sz * 0.5f, sz, sz, 2.0f, pa_hex(boxes[i].col));
    }

    PA_Paint glow = pa_radial(cx, cy, r * 0.7f, r * 1.6f);
    pa_stop(&glow, 0.0f, PA_RGBA(93, 224, 255, 115));
    pa_stop(&glow, 1.0f, PA_RGBA(93, 224, 255, 0));
    pa_fill_ellipse_paint(c, cx, cy, r * 1.6f, r * 1.05f, &glow);
    pa_fill_ellipse(c, cx, cy, r, r * 0.66f, pa_hex(0x05040E));
    pa_stroke_circle(c, cx, cy, r, 2.0f, pa_hex(0x5DE0FF));
}

const PA_Game PA_GAME_VOIDMUNCHER = {
    "voidmuncher", "Void Muncher", "Arena",
    "You are a hole. Swallow the district before three rival voids eat it out from under you.",
    PA_RGB(93, 224, 255),
    muncher_start, muncher_stop, muncher_update, muncher_render, muncher_thumb
};
