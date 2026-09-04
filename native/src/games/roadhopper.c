/* ===========================================================================
   ROAD HOPPER - native port
   The endless lane-hopper. Tap to hop forward, swipe to sidestep, thread a
   blocky animal through traffic, log rivers and express trains. The camera
   creeps forward the whole time, so standing still is its own way of dying.

   Rows are generated on demand from a hash of the row index and recycled once
   they fall behind, so an endless track costs a fixed amount of memory. The
   board is drawn as a finite skewed slab: seeing where it ends is what sells
   the tilt, because a full-width band slides onto itself under a shear and
   reads as a flat stripe no matter how much lean is applied.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define HALF        5                 /* playfield spans -HALF..HALF columns */
#define COLS        (HALF * 2 + 1)
#define EDGE        (HALF + 0.5f)     /* where the slab actually ends */
#define SAFE_ROWS   2
#define HOP_TIME    0.13f
#define IDLE_LIMIT  6.0f
#define ROW_CAP     64                /* ring capacity; the view needs ~24 */
#define MAX_MOVERS  10

enum { ROW_GRASS, ROW_ROAD, ROW_WATER, ROW_RAIL };

typedef struct {
    float x, len;
    float hue;
    int   big;
} Mover;

typedef struct {
    int   index;
    int   live;
    int   type;
    int   band;                       /* alternating grass shade */

    int   has_tree[COLS];
    float tree_h[COLS], tree_s[COLS];
    int   coin;                       /* column, or -99 */

    int   dir;
    float speed, span;
    int   markings;
    Mover movers[MAX_MOVERS];
    int   mover_count;

    int   warn;
    int   train_on;
    float train_x, next_train;
} Row;

/* --------------------------------------------------------------- character */
typedef struct {
    float x, z, w, h, d;
    PA_Color col;
    int  front;
} Part;

typedef struct {
    Part  parts[12];
    int   count;
    float eye_z, head_w;
} Critter;

typedef struct {
    const char *name;
    uint32_t    seed;
    int         cost;
} CritterDef;

static const CritterDef CRITTERS[] = {
    { "CHICK",     101,    0 },
    { "DUCK",      214,  120 },
    { "PIGLET",    337,  260 },
    { "FROG",      452,  420 },
    { "ALLEY CAT", 578,  700 },
    { "TIN BOT",   691, 1100 }
};
#define CRITTER_COUNT ((int)(sizeof(CRITTERS) / sizeof(CRITTERS[0])))

static void build_critter(Critter *c, uint32_t seed) {
    PA_Rng r;
    pa_rng_seed(&r, seed);
    memset(c, 0, sizeof(*c));

    float hue = pa_rng_next(&r);
    PA_Color body  = pa_hsl(hue, pa_rng_range(&r, 0.45f, 0.78f), pa_rng_range(&r, 0.52f, 0.68f));
    PA_Color belly = pa_shade(body, pa_rng_range(&r, 0.20f, 0.38f));
    PA_Color beak  = pa_hsl(pa_wrapf(hue + pa_rng_range(&r, 0.08f, 0.18f), 1.0f), 0.85f, 0.58f);
    PA_Color feet  = pa_shade(beak, -0.18f);

    float body_w = pa_rng_range(&r, 0.60f, 0.76f);
    float body_h = pa_rng_range(&r, 0.38f, 0.54f);
    float head_w = body_w * pa_rng_range(&r, 0.72f, 0.94f);
    float head_h = pa_rng_range(&r, 0.34f, 0.48f);

    #define PART(px, pz, pw, ph, pd, pc, pf) \
        do { Part *p = &c->parts[c->count++]; \
             p->x = (px); p->z = (pz); p->w = (pw); p->h = (ph); p->d = (pd); \
             p->col = (pc); p->front = (pf); } while (0)

    PART(-body_w * 0.26f, 0.0f, 0.11f, 0.16f, 0.11f, feet, 0);
    PART( body_w * 0.26f, 0.0f, 0.11f, 0.16f, 0.11f, feet, 0);
    PART(0.0f, 0.14f, body_w, body_h, body_w * 0.82f, body, 0);
    PART(0.0f, 0.17f, body_w * 0.62f, body_h * 0.55f, body_w * 0.90f, belly, 0);
    PART(0.0f, 0.14f + body_h, head_w, head_h, head_w * 0.86f, body, 0);
    PART(0.0f, 0.14f + body_h + head_h * 0.30f, head_w * 0.30f, head_h * 0.30f,
         head_w * 0.95f, beak, 1);

    int crest = pa_rng_int(&r, 0, 3);
    if (crest == 1) {
        PART(0.0f, 0.14f + body_h + head_h, head_w * 0.24f, 0.14f, head_w * 0.24f, beak, 0);
    } else if (crest == 2) {
        PART(-head_w * 0.34f, 0.14f + body_h + head_h * 0.92f, 0.10f, 0.16f, 0.08f, body, 0);
        PART( head_w * 0.34f, 0.14f + body_h + head_h * 0.92f, 0.10f, 0.16f, 0.08f, body, 0);
    } else if (crest == 3) {
        PART(0.0f, 0.14f + body_h + head_h, head_w * 0.55f, 0.08f, head_w * 0.30f,
             pa_shade(beak, -0.25f), 0);
    }

    if (pa_rng_chance(&r, 0.55f)) {
        PART(-body_w * 0.54f, 0.20f, 0.09f, body_h * 0.66f, body_w * 0.58f,
             pa_shade(body, -0.16f), 0);
        PART( body_w * 0.54f, 0.20f, 0.09f, body_h * 0.66f, body_w * 0.58f,
             pa_shade(body, -0.16f), 0);
    }
    #undef PART

    c->eye_z = 0.14f + body_h + head_h * 0.62f;
    c->head_w = head_w;
}

/* ------------------------------------------------------------------ state -- */
typedef struct {
    uint32_t run_seed;
    Row      rows[ROW_CAP];
    int      first, built;
    int      prev_type, streak;

    float    px, py;           /* logical tile position */
    float    draw_x, draw_y;
    float    hop_z;
    int      hopping;
    float    hop_t, hop_fx, hop_fy;
    int      hop_tx, hop_ty;

    int      on_log;           /* index into the row's movers, -1 when not */
    float    log_offset;

    float    cam_y;
    int      score, coins;
    float    idle;
    int      started;
    int      over;
    int      death;            /* 0 car 1 train 2 water 3 eagle */
    float    death_t, eagle;

    Critter  critter;
    int      critter_index;
    float    time;
} Hopper;

static Hopper H;
static int    g_best;
static int    g_best_loaded;

/* Layout, recomputed whenever the canvas changes size. */
static struct {
    int   w, h;
    float tile, depth, rise, shear, mid_dy, edge_drop, cx, cy;
    int   ahead, behind;
} L;

static void compute_layout(int w, int h) {
    /*
     * Column width comes from the narrow axis. Row depth is deliberately much
     * larger than a tile is wide: the real thing is viewed down a rotated
     * diagonal, so bands read far apart on screen even though the world grid is
     * square. Matching that spacing matters more than matching the geometry.
     */
    L.w = w; L.h = h;
    L.tile = (float)w / (float)(HALF * 2 + 3.4f);
    L.depth = L.tile * 1.52f;
    L.rise = L.tile * 1.02f;
    L.shear = -L.tile * 0.15f;
    L.cx = (float)w * 0.5f;
    L.cy = (float)h * 0.76f;
    L.edge_drop = L.tile * 0.55f;
    L.ahead = (int)ceilf((L.cy + 90.0f) / L.depth) + 2;
    L.behind = (int)ceilf(((float)h - L.cy + 90.0f) / L.depth) + 2;
    if (L.ahead > ROW_CAP - 8) L.ahead = ROW_CAP - 8;
    if (L.behind > 16) L.behind = 16;
    L.mid_dy = (float)(L.ahead - L.behind) * 0.5f;
}

static PA_Vec2 project(float col, float row, float z) {
    float dy = row - H.cam_y;
    PA_Vec2 p;
    p.x = L.cx + col * L.tile + (dy - L.mid_dy) * L.shear;
    p.y = L.cy - dy * L.depth - z * L.rise;
    return p;
}

/* ---------------------------------------------------------------- palette -- */
static PA_Color pal_sky(void)    { return pa_hex(0x8FD4E8); }
static PA_Color pal_grass_a(void){ return pa_hex(0x4FAE63); }
static PA_Color pal_grass_b(void){ return pa_shade(pa_hex(0x4FAE63), -0.10f); }
static PA_Color pal_road(void)   { return pa_hex(0x4A4F5A); }
static PA_Color pal_water(void)  { return pa_hex(0x2E7BD6); }
static PA_Color pal_rail(void)   { return pa_hex(0x6B6257); }

/* ------------------------------------------------------------- generation -- */
static uint32_t row_seed(int index) {
    return (uint32_t)(0x9E3779B1u ^ ((uint32_t)index * 2654435761u));
}

static int max_streak(int type, PA_Rng *r, float d) {
    if (type == ROW_GRASS) return pa_rng_int(r, 1, 3);
    if (type == ROW_ROAD)  return pa_rng_int(r, 1, 2 + (int)(d * 2.0f));
    if (type == ROW_WATER) return pa_rng_int(r, 1, 3);
    return pa_rng_int(r, 1, 2);
}

static Row *row_slot(int index) { return &H.rows[((index % ROW_CAP) + ROW_CAP) % ROW_CAP]; }

static Row *row_at(int index) {
    Row *r = row_slot(index);
    return (r->live && r->index == index) ? r : NULL;
}

static void build_row(int index) {
    Row *row = row_slot(index);
    PA_Rng r;
    pa_rng_seed(&r, row_seed(index) ^ H.run_seed);

    memset(row, 0, sizeof(*row));
    row->index = index;
    row->live = 1;
    row->coin = -99;
    row->band = ((index % 2) + 2) % 2;

    if (index <= SAFE_ROWS) {
        row->type = ROW_GRASS;
        /* Rows behind the start line exist only to fill the bottom of the
           screen. Nobody hops back there, so they get dense scenery - an empty
           green field under the player looks like the level failed to load. */
        if (index < 0) {
            int n = pa_rng_int(&r, 2, 5);
            for (int i = 0; i < n; i++) {
                int c = pa_rng_int(&r, 0, COLS - 1);
                if (row->has_tree[c]) continue;
                row->has_tree[c] = 1;
                row->tree_h[c] = pa_rng_range(&r, 0.55f, 1.45f);
                row->tree_s[c] = pa_rng_range(&r, 0.62f, 0.90f);
            }
        }
        H.streak = (row->type == H.prev_type) ? H.streak + 1 : 1;
        H.prev_type = row->type;
        return;
    }

    /* Difficulty ramps by row then flattens: past row 220 the game is about
       nerve, not about ever-faster cars. */
    float d = pa_clamp01((float)index / 220.0f);

    int type = H.prev_type;
    if (H.streak >= max_streak(H.prev_type, &r, d)) {
        float roll = pa_rng_next(&r);
        if (H.prev_type == ROW_GRASS)
            type = roll < 0.52f ? ROW_ROAD : (roll < 0.80f ? ROW_WATER : ROW_RAIL);
        else if (H.prev_type == ROW_ROAD)
            type = roll < 0.55f ? ROW_GRASS : (roll < 0.85f ? ROW_WATER : ROW_RAIL);
        else if (H.prev_type == ROW_WATER)
            type = roll < 0.62f ? ROW_GRASS : ROW_ROAD;
        else
            type = roll < 0.60f ? ROW_GRASS : ROW_ROAD;
    }
    row->type = type;

    if (type == ROW_GRASS) {
        /* Trees block tiles. Density climbs but never seals a row - a sealed
           row would be an unwinnable board. */
        int count = pa_rng_int(&r, 1, 2 + (int)(d * 4.0f));
        int placed = 0;
        for (int i = 0; i < count * 2 && placed < count; i++) {
            int c = pa_rng_int(&r, 0, COLS - 1);
            if (row->has_tree[c]) continue;
            row->has_tree[c] = 1;
            row->tree_h[c] = pa_rng_range(&r, 0.55f, 1.35f);
            row->tree_s[c] = pa_rng_range(&r, 0.62f, 0.86f);
            placed++;
        }
        if (pa_rng_chance(&r, 0.30f)) {
            for (int t = 0; t < 8; t++) {
                int cc = pa_rng_int(&r, 1, COLS - 2);
                if (!row->has_tree[cc]) { row->coin = cc; break; }
            }
        }
    } else if (type == ROW_ROAD || type == ROW_WATER) {
        int water = (type == ROW_WATER);
        row->dir = pa_rng_chance(&r, 0.5f) ? 1 : -1;
        row->speed = water ? pa_lerpf(1.3f, 3.1f, d) * pa_rng_range(&r, 0.85f, 1.15f)
                           : pa_lerpf(1.9f, 5.6f, d) * pa_rng_range(&r, 0.82f, 1.20f);
        row->markings = !water && pa_rng_chance(&r, 0.5f);
        row->span = water ? (HALF * 2 + 6) : (HALF * 2 + 4);

        float gap = water ? pa_lerpf(2.4f, 3.4f, d) * pa_rng_range(&r, 0.9f, 1.2f)
                          : pa_lerpf(6.2f, 3.3f, d) * pa_rng_range(&r, 0.9f, 1.25f);
        float x = pa_rng_range(&r, -row->span * 0.5f, row->span * 0.5f);
        while (x < row->span * 0.5f && row->mover_count < MAX_MOVERS) {
            Mover *m = &row->movers[row->mover_count++];
            m->big = !water && pa_rng_chance(&r, 0.22f);
            m->len = water ? pa_rng_range(&r, 1.6f, 3.4f)
                           : (m->big ? pa_rng_range(&r, 1.7f, 2.3f) : pa_rng_range(&r, 0.95f, 1.35f));
            m->x = x;
            m->hue = water ? 0.09f : pa_rng_next(&r);
            x += m->len + gap * pa_rng_range(&r, 0.85f, 1.15f);
        }
        if (!water && pa_rng_chance(&r, 0.18f)) row->coin = pa_rng_int(&r, 1, COLS - 2);
    } else {
        row->dir = pa_rng_chance(&r, 0.5f) ? 1 : -1;
        row->speed = pa_lerpf(14.0f, 22.0f, d);
        row->span = HALF * 2 + 8;
        row->train_on = 0;
        row->next_train = pa_rng_range(&r, 1.6f, 4.2f);
    }

    H.streak = (row->type == H.prev_type) ? H.streak + 1 : 1;
    H.prev_type = row->type;
}

static void ensure_rows(float ahead) {
    int target = (int)ceilf(ahead) + L.ahead + 4;
    /* The ring only holds ROW_CAP rows, so never build further ahead than it
       can carry or the oldest visible row is overwritten under the player. */
    int limit = H.first + ROW_CAP - 1;
    if (target > limit) target = limit;
    while (H.built < target) {
        H.built++;
        build_row(H.built);
    }
    int cut = (int)floorf(H.cam_y) - (L.behind + 4) - 2;
    while (H.first < cut) {
        Row *r = row_slot(H.first);
        if (r->index == H.first) r->live = 0;
        H.first++;
    }
}

/* ------------------------------------------------------------------ input -- */
static int walkable(int col, int row_index) {
    if (col < -HALF || col > HALF) return 0;
    Row *r = row_at(row_index);
    if (r && r->type == ROW_GRASS && r->has_tree[col + HALF]) return 0;
    return 1;
}

static Mover *find_log(Row *row, float x) {
    for (int i = 0; i < row->mover_count; i++) {
        Mover *m = &row->movers[i];
        if (x > m->x - m->len * 0.5f - 0.30f && x < m->x + m->len * 0.5f + 0.30f) return m;
    }
    return NULL;
}

static void try_hop(int dx, int dy) {
    if (H.over || H.hopping) return;

    float from_x = (H.on_log >= 0) ? H.draw_x : H.px;
    int tx = (int)floorf(from_x + 0.5f) + dx;
    int ty = (int)H.py + dy;
    if (ty < H.first) return;

    ensure_rows(H.cam_y);
    if (!walkable(tx, ty)) {
        if (tx >= -HALF && tx <= HALF) pa_sfx("thud");
        return;
    }

    H.hop_fx = from_x;
    H.hop_fy = H.py;
    H.hop_tx = tx;
    H.hop_ty = ty;
    H.hopping = 1;
    H.hop_t = 0.0f;
    H.on_log = -1;
    H.started = 1;
    pa_sfx("hop");
}

/* ------------------------------------------------------------------- life -- */
static void die(int kind) {
    if (H.over) return;
    H.over = 1;
    H.death = kind;
    H.death_t = 0.0f;
    pa_sfx("lose");
    if (H.score > g_best) {
        g_best = H.score;
        pa_save_set("roadhopper.best", g_best);
        pa_save_flush();
    }
}

static void landed(void) {
    Row *r = row_at((int)H.py);
    if (!r) return;
    if (r->type == ROW_WATER) {
        Mover *m = find_log(r, H.draw_x);
        if (!m) { pa_sfx("hit"); die(2); return; }
        H.on_log = (int)(m - r->movers);
        H.log_offset = pa_clampf(H.draw_x - m->x, -m->len * 0.5f + 0.2f, m->len * 0.5f - 0.2f);
        H.draw_x = m->x + H.log_offset;
    }
}

static void check_hazards(void) {
    Row *test = H.hopping ? row_at(H.hop_ty) : row_at((int)H.py);
    if (!test) return;
    float x = H.draw_x;

    if (test->type == ROW_ROAD) {
        for (int i = 0; i < test->mover_count; i++) {
            Mover *m = &test->movers[i];
            /* Mid-hop the player is airborne over the destination row, so the
               test runs against wherever they will land. Being clipped by a car
               already cleared is the most infuriating failure in this format. */
            if (fabsf(m->x - x) < m->len * 0.5f + 0.34f) {
                if (!H.hopping || H.hop_t > 0.55f) { die(0); return; }
            }
        }
    } else if (test->type == ROW_RAIL && test->train_on) {
        if (fabsf(test->train_x - x) < 5.0f) {
            if (!H.hopping || H.hop_t > 0.40f) { die(1); return; }
        }
    }
}

/* ------------------------------------------------------------------ frame -- */
static void hopper_start(void) {
    if (!g_best_loaded) { g_best = pa_save_get("roadhopper.best", 0); g_best_loaded = 1; }
    memset(&H, 0, sizeof(H));
    H.run_seed = 0x5EED1234u ^ (uint32_t)(pa_wrapf(1.0f, 1.0f) * 1000.0f);
    /* A per-run seed without a clock source: the row hash already decorrelates
       neighbouring rows, so mixing the best score in is enough variety. */
    H.run_seed ^= (uint32_t)(g_best * 2654435761u) + 0x9E3779B9u;

    H.critter_index = 0;
    build_critter(&H.critter, CRITTERS[0].seed);

    H.cam_y = -2.6f;
    H.on_log = -1;
    H.prev_type = ROW_GRASS;
    H.streak = 0;

    if (L.w == 0) compute_layout(540, 960);
    H.first = -(L.behind + 4);
    H.built = H.first - 1;
    ensure_rows(0.0f);
}

static void hopper_stop(void) { }

static void hopper_update(float dt, const PA_Input *in) {
    H.time += dt;

    if (H.over) {
        H.death_t += dt;
        if (H.death == 3) H.eagle = pa_clampf(H.eagle + dt * 1.8f, 0.0f, 1.0f);
        if (H.death_t > 2.2f) hopper_start();
        return;
    }

    if (in->swipe == PA_SWIPE_UP || in->swipe == PA_SWIPE_TAP || in->key_pressed[PA_KEY_UP])
        try_hop(0, 1);
    else if (in->swipe == PA_SWIPE_DOWN || in->key_pressed[PA_KEY_DOWN]) try_hop(0, -1);
    else if (in->swipe == PA_SWIPE_LEFT || in->key_pressed[PA_KEY_LEFT]) try_hop(-1, 0);
    else if (in->swipe == PA_SWIPE_RIGHT || in->key_pressed[PA_KEY_RIGHT]) try_hop(1, 0);

    if (H.hopping) {
        H.hop_t += dt / HOP_TIME;
        float t = pa_clamp01(H.hop_t);
        H.draw_x = pa_lerpf(H.hop_fx, (float)H.hop_tx, t);
        H.draw_y = pa_lerpf(H.hop_fy, (float)H.hop_ty, t);
        /* A plain sine arc peaks exactly halfway, which is what puts landings
           on the beat. */
        H.hop_z = sinf(t * PA_PI) * 0.42f;
        if (t >= 1.0f) {
            if (H.hop_ty > (int)H.py) {
                if (H.hop_ty > H.score) H.score = H.hop_ty;
                H.idle = 0.0f;
            }
            H.px = (float)H.hop_tx;
            H.py = (float)H.hop_ty;
            H.draw_x = H.px;
            H.draw_y = H.py;
            H.hop_z = 0.0f;
            H.hopping = 0;
            landed();
            if (H.over) return;
        }
    } else {
        H.hop_z = 0.0f;
        H.draw_y = H.py;
    }

    ensure_rows(H.cam_y > H.py ? H.cam_y : H.py);

    /* Traffic, logs and trains. */
    for (int idx = H.first; idx <= H.built; idx++) {
        Row *r = row_at(idx);
        if (!r) continue;
        if ((float)idx < H.cam_y - 10.0f || (float)idx > H.cam_y + 24.0f) continue;

        if (r->type == ROW_ROAD || r->type == ROW_WATER) {
            float lim = r->span * 0.5f + 4.0f;
            for (int i = 0; i < r->mover_count; i++) {
                Mover *m = &r->movers[i];
                m->x += (float)r->dir * r->speed * dt;
                if (r->dir > 0 && m->x - m->len * 0.5f > lim) m->x -= r->span + 8.0f;
                if (r->dir < 0 && m->x + m->len * 0.5f < -lim) m->x += r->span + 8.0f;
            }
        } else if (r->type == ROW_RAIL) {
            if (!r->train_on) {
                r->next_train -= dt;
                r->warn = r->next_train < 1.4f;
                if (r->next_train <= 0.0f) {
                    r->train_on = 1;
                    r->train_x = r->dir > 0 ? -(r->span * 0.5f + 9.0f) : (r->span * 0.5f + 9.0f);
                    pa_sfx("horn");
                }
            } else {
                r->train_x += (float)r->dir * r->speed * dt;
                if (fabsf(r->train_x) > r->span * 0.5f + 10.0f) {
                    r->train_on = 0;
                    r->warn = 0;
                    r->next_train = 2.2f + pa_wrapf(H.time * 1.7f, 3.4f);
                }
            }
        }
    }

    /* Ride the log underfoot. */
    Row *here = row_at((int)H.py);
    if (!H.hopping && here && here->type == ROW_WATER) {
        if (H.on_log < 0) {
            Mover *m = find_log(here, H.draw_x);
            if (!m) { die(2); return; }
            H.on_log = (int)(m - here->movers);
            H.log_offset = H.draw_x - m->x;
        }
        H.draw_x = here->movers[H.on_log].x + H.log_offset;
        H.px = floorf(H.draw_x + 0.5f);
        if (H.draw_x < -EDGE - 0.6f || H.draw_x > EDGE + 0.6f) { die(2); return; }
    } else if (!H.hopping) {
        H.on_log = -1;
        H.draw_x = H.px;
    }

    /* Camera creep, and a catch-up if the player sprints ahead of it. */
    float creep = pa_lerpf(0.55f, 1.35f, pa_clamp01((float)H.score / 200.0f));
    if (H.started) H.cam_y += creep * dt;
    float want = H.draw_y - 2.4f;
    if (want > H.cam_y) H.cam_y = pa_approach(H.cam_y, want, 9.0f, dt);

    if (H.draw_y < H.cam_y - 3.2f) { die(3); return; }

    H.idle += dt;
    if (H.idle > IDLE_LIMIT && H.started) { die(3); return; }

    check_hazards();
    if (H.over) return;

    /* Coins. */
    Row *coin_row = row_at((int)H.py);
    if (coin_row && coin_row->coin != -99 && !H.hopping) {
        if (fabsf((float)(coin_row->coin - HALF) - H.draw_x) < 0.5f) {
            coin_row->coin = -99;
            H.coins++;
            pa_sfx("coin");
        }
    }
}

/* ---------------------------------------------------------------- drawing -- */
/**
 * One extruded box in world space: a top face, a front face and one sheared
 * side. Everything in the world is made of these, which is why the scene stays
 * consistent without a single texture.
 */
static void box(PA_Canvas *c, float col, float row, float z,
                float w, float h, float d, PA_Color colour) {
    PA_Vec2 p = project(col, row, z + h);
    float tw = w * L.tile;
    float td = d * L.depth;
    float th = h * L.rise;
    float sx = L.shear * d;

    if (p.y - th > (float)L.h + 80.0f || p.y + td < -120.0f) return;

    PA_Vec2 front[4] = {
        { p.x - tw * 0.5f, p.y + td * 0.5f },
        { p.x + tw * 0.5f, p.y + td * 0.5f },
        { p.x + tw * 0.5f, p.y + td * 0.5f + th },
        { p.x - tw * 0.5f, p.y + td * 0.5f + th }
    };
    pa_fill_poly(c, front, 4, pa_shade(colour, -0.26f));

    PA_Vec2 side[4] = {
        { p.x + tw * 0.5f + sx, p.y - td * 0.5f + sx },
        { p.x + tw * 0.5f,      p.y + td * 0.5f },
        { p.x + tw * 0.5f,      p.y + td * 0.5f + th },
        { p.x + tw * 0.5f + sx, p.y - td * 0.5f + th + sx }
    };
    pa_fill_poly(c, side, 4, pa_shade(colour, -0.42f));

    PA_Vec2 top[4] = {
        { p.x - tw * 0.5f + sx, p.y - td * 0.5f + sx },
        { p.x + tw * 0.5f + sx, p.y - td * 0.5f + sx },
        { p.x + tw * 0.5f,      p.y + td * 0.5f },
        { p.x - tw * 0.5f,      p.y + td * 0.5f }
    };
    pa_fill_poly(c, top, 4, colour);
}

static void draw_row(PA_Canvas *c, Row *r) {
    PA_Color col;
    switch (r->type) {
        case ROW_ROAD:  col = pal_road();  break;
        case ROW_WATER: col = pal_water(); break;
        case ROW_RAIL:  col = pal_rail();  break;
        default:        col = r->band ? pal_grass_b() : pal_grass_a(); break;
    }

    PA_Vec2 a = project(-EDGE, (float)r->index - 0.5f, 0.0f);
    PA_Vec2 b = project( EDGE, (float)r->index - 0.5f, 0.0f);
    PA_Vec2 cc = project( EDGE, (float)r->index + 0.5f, 0.0f);
    PA_Vec2 d = project(-EDGE, (float)r->index + 0.5f, 0.0f);

    if (a.y < -160.0f && d.y < -160.0f) return;
    if (a.y > (float)L.h + 160.0f && d.y > (float)L.h + 160.0f) return;

    /* Cut edges first, so the row surface lands on top of its own thickness.
       The slab being visibly finite is what makes the lean readable. */
    float drop = L.edge_drop;
    PA_Color cut = pa_shade(col, -0.44f);
    PA_Vec2 left[4]  = { a, d, { d.x, d.y + drop }, { a.x, a.y + drop } };
    PA_Vec2 right[4] = { b, cc, { cc.x, cc.y + drop }, { b.x, b.y + drop } };
    pa_fill_poly(c, left, 4, cut);
    pa_fill_poly(c, right, 4, cut);

    PA_Vec2 surface[4] = { a, b, cc, d };
    pa_fill_poly(c, surface, 4, col);

    if (r->type == ROW_ROAD && r->markings) {
        PA_Vec2 m1 = project(-EDGE, (float)r->index, 0.0f);
        PA_Vec2 m2 = project( EDGE, (float)r->index, 0.0f);
        int dashes = 9;
        for (int i = 0; i < dashes; i++) {
            float t0 = (float)i / (float)dashes;
            float t1 = t0 + 0.5f / (float)dashes;
            pa_line(c, pa_lerpf(m1.x, m2.x, t0), pa_lerpf(m1.y, m2.y, t0),
                       pa_lerpf(m1.x, m2.x, t1), pa_lerpf(m1.y, m2.y, t1),
                    L.tile * 0.06f, PA_RGBA(255, 255, 255, 115));
        }
    }

    if (r->type == ROW_WATER) {
        /* Two drifting highlight bands do the job of an animated water shader. */
        for (int k = 0; k < 2; k++) {
            float off = pa_wrapf(H.time * 0.5f + (float)k * 0.5f + (float)r->index * 0.13f, 1.0f)
                        * 2.0f - 1.0f;
            PA_Vec2 w1 = project(-EDGE, (float)r->index + off * 0.3f, 0.0f);
            PA_Vec2 w2 = project( EDGE, (float)r->index + off * 0.3f, 0.0f);
            pa_line(c, w1.x, w1.y, w2.x, w2.y, L.tile * 0.06f, PA_RGBA(255, 255, 255, 36));
        }
    }

    if (r->type == ROW_RAIL) {
        for (int s = -1; s <= 1; s += 2) {
            PA_Vec2 r1 = project(-EDGE, (float)r->index + (float)s * 0.16f, 0.0f);
            PA_Vec2 r2 = project( EDGE, (float)r->index + (float)s * 0.16f, 0.0f);
            pa_line(c, r1.x, r1.y, r2.x, r2.y, L.tile * 0.08f, PA_RGBA(30, 26, 20, 140));
        }
        if (r->warn) {
            int blink = ((int)(H.time * 6.0f) % 2) == 0;
            for (int side = -1; side <= 1; side += 2) {
                PA_Vec2 lp = project((float)side * (EDGE + 0.6f), (float)r->index, 0.85f);
                pa_fill_circle(c, lp.x, lp.y, L.tile * 0.17f,
                               blink ? pa_hex(0xFF4B5C) : pa_hex(0x5A2028));
            }
        }
    }

    if (r->coin != -99) {
        float cx = (float)(r->coin - HALF);
        float bob = 0.30f + sinf(H.time * 4.0f + (float)r->index) * 0.06f;
        PA_Vec2 cp = project(cx, (float)r->index, bob);
        PA_Vec2 gp = project(cx, (float)r->index, 0.0f);
        float rr = L.tile * 0.20f;
        pa_fill_ellipse(c, cp.x, gp.y, rr * 0.9f, rr * 0.42f, PA_RGBA(0, 0, 0, 50));
        PA_Paint coin = pa_linear(cp.x - rr, cp.y - rr, cp.x + rr, cp.y + rr);
        pa_stop(&coin, 0.0f, pa_hex(0xFFE9A0));
        pa_stop(&coin, 0.5f, pa_hex(0xFFC93C));
        pa_stop(&coin, 1.0f, pa_hex(0xC98A12));
        pa_fill_ellipse_paint(c, cp.x, cp.y, rr * 0.62f, rr, &coin);
    }
}

static void draw_row_props(PA_Canvas *c, Row *r) {
    if (r->type == ROW_GRASS) {
        for (int i = 0; i < COLS; i++) {
            if (!r->has_tree[i]) continue;
            float cx = (float)(i - HALF);
            float s = r->tree_s[i], h = r->tree_h[i];
            /* Narrow trunk, wide crown, lighter cap. Matching the crown to the
               trunk width turned every tree into a green domino. */
            box(c, cx, (float)r->index, 0.0f, 0.26f, 0.34f, 0.26f, pa_hex(0x6B4A2E));
            box(c, cx, (float)r->index, 0.30f, s, h * 0.62f, s, pa_hex(0x2E7A47));
            box(c, cx, (float)r->index - 0.04f, 0.30f + h * 0.42f,
                s * 0.68f, h * 0.34f, s * 0.68f, pa_hex(0x3EA063));
        }
    } else if (r->type == ROW_ROAD) {
        for (int i = 0; i < r->mover_count; i++) {
            Mover *m = &r->movers[i];
            if (fabsf(m->x) > EDGE + 4.0f) continue;
            PA_Color body = pa_hsl(m->hue, 0.72f, 0.56f);
            float flip = (float)r->dir;

            box(c, m->x, (float)r->index, 0.05f, m->len, 0.30f, 0.62f, body);
            if (m->big) {
                box(c, m->x - flip * m->len * 0.28f, (float)r->index, 0.35f,
                    m->len * 0.40f, 0.26f, 0.56f, pa_shade(body, 0.22f));
                box(c, m->x + flip * m->len * 0.24f, (float)r->index, 0.35f,
                    m->len * 0.48f, 0.16f, 0.54f, pa_shade(body, -0.30f));
            } else {
                box(c, m->x - flip * m->len * 0.06f, (float)r->index, 0.30f,
                    m->len * 0.46f, 0.18f, 0.46f, pa_shade(body, 0.30f));
                PA_Vec2 scr = project(m->x + flip * m->len * 0.14f, (float)r->index, 0.48f);
                pa_fill_rect(c, scr.x - m->len * 0.05f * L.tile, scr.y - L.depth * 0.22f,
                             m->len * 0.10f * L.tile, L.depth * 0.44f, PA_RGBA(26, 34, 54, 158));
            }
            /* Headlights on the leading edge, so oncoming traffic reads at once. */
            PA_Vec2 lp = project(m->x + flip * (m->len * 0.5f), (float)r->index, 0.16f);
            pa_fill_ellipse(c, lp.x, lp.y, L.tile * 0.06f, L.tile * 0.05f,
                            PA_RGBA(255, 246, 200, 235));
        }
    } else if (r->type == ROW_WATER) {
        for (int i = 0; i < r->mover_count; i++) {
            Mover *m = &r->movers[i];
            if (fabsf(m->x) > EDGE + 5.0f) continue;
            box(c, m->x, (float)r->index, 0.0f, m->len, 0.20f, 0.66f, pa_hex(0x7A5433));
            box(c, m->x, (float)r->index - 0.02f, 0.20f, m->len * 0.98f, 0.05f, 0.60f,
                pa_hex(0x8E653F));
        }
    } else if (r->type == ROW_RAIL && r->train_on) {
        box(c, r->train_x, (float)r->index, 0.05f, 9.0f, 0.78f, 0.78f, pa_hex(0xC2453D));
        box(c, r->train_x, (float)r->index - 0.04f, 0.83f, 8.4f, 0.10f, 0.70f, pa_hex(0xE8E2D6));
        for (int w = -3; w <= 3; w++) {
            PA_Vec2 wp = project(r->train_x + (float)w * 1.25f, (float)r->index - 0.34f, 0.42f);
            pa_fill_rect(c, wp.x - L.tile * 0.16f, wp.y - L.tile * 0.13f,
                         L.tile * 0.32f, L.tile * 0.26f, PA_RGBA(180, 225, 255, 215));
        }
    }
}

static void draw_player(PA_Canvas *c) {
    Critter *ch = &H.critter;
    float z = H.hop_z;
    float squash = H.hopping ? 1.0f : (1.0f + sinf(H.time * 6.0f) * 0.02f);

    PA_Vec2 sp = project(H.draw_x, H.draw_y, 0.0f);
    float shrink = 1.0f - z * 0.5f;
    pa_fill_ellipse(c, sp.x, sp.y, L.tile * 0.30f * shrink, L.depth * 0.30f * shrink,
                    PA_RGBA(0, 0, 0, 60));

    if (H.over && H.death == 0) {
        /* Pancaked: flatten the whole stack in place. */
        float flat = pa_clamp01(H.death_t * 6.0f);
        for (int i = 0; i < ch->count; i++) {
            Part *p = &ch->parts[i];
            box(c, H.draw_x + p->x, H.draw_y, p->z * (1.0f - flat * 0.9f),
                p->w * (1.0f + flat * 0.5f), p->h * (1.0f - flat * 0.85f),
                p->d * (1.0f + flat * 0.5f), p->col);
        }
        return;
    }

    for (int i = 0; i < ch->count; i++) {
        Part *p = &ch->parts[i];
        box(c, H.draw_x + p->x, H.draw_y - (p->front ? 0.16f : 0.0f),
            (p->z + z) * squash, p->w, p->h * squash, p->d, p->col);
    }

    /* Eyes are drawn flat on top so they always face the camera. */
    PA_Vec2 ep = project(H.draw_x, H.draw_y - 0.20f, (ch->eye_z + z) * squash);
    float er = L.tile * 0.055f;
    for (int e = -1; e <= 1; e += 2) {
        pa_fill_circle(c, ep.x + (float)e * ch->head_w * L.tile * 0.26f, ep.y, er,
                       pa_hex(0x141024));
    }
}

static void hopper_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) compute_layout(c->w, c->h);

    /* Sky up top, a deeper tone underneath. The slab floats over the darker
       half, which is what gives it a cut-out silhouette. */
    PA_Paint sky = pa_linear(0, 0, 0, (float)c->h);
    pa_stop(&sky, 0.00f, pa_shade(pal_sky(), 0.20f));
    pa_stop(&sky, 0.42f, pal_sky());
    pa_stop(&sky, 0.62f, pa_shade(pal_sky(), -0.34f));
    pa_stop(&sky, 1.00f, pa_shade(pal_sky(), -0.52f));
    pa_fill_rect_paint(c, 0, 0, (float)c->w, (float)c->h, &sky);

    /* Painter's algorithm along the row axis: far rows first. */
    int far = (int)ceilf(H.cam_y) + L.ahead;
    int near = (int)floorf(H.cam_y) - L.behind;
    int player_row = (int)floorf(H.draw_y + 0.5f);

    for (int i = far; i >= near; i--) {
        Row *r = row_at(i);
        if (r) draw_row(c, r);
    }
    for (int i = far; i >= near; i--) {
        Row *r = row_at(i);
        if (!r) continue;
        draw_row_props(c, r);
        /* The player draws inside the row loop so props in front occlude them. */
        if (player_row == i) draw_player(c);
    }
    if (player_row > far || player_row < near) draw_player(c);

    /* Eagle, on the idle death. */
    if (H.eagle > 0.0f) {
        PA_Vec2 p = project(H.draw_x, H.draw_y, pa_lerpf(4.5f, 0.35f, pa_smooth(H.eagle)));
        float w = L.tile * 1.5f;
        float flap = sinf(H.time * 14.0f) * w * 0.16f;
        PA_Vec2 wing[5] = {
            { p.x, p.y },
            { p.x - w, p.y - w * 0.35f + flap },
            { p.x - w * 0.3f, p.y + w * 0.12f },
            { p.x + w * 0.3f, p.y + w * 0.12f },
            { p.x + w, p.y - w * 0.35f - flap }
        };
        pa_fill_poly(c, wing, 5, pa_hex(0x2A2438));
        pa_fill_ellipse(c, p.x, p.y + w * 0.06f, w * 0.28f, w * 0.20f, pa_hex(0x5A4B6E));
    }

    /* HUD. Left of x=104 belongs to the hub's MENU button. */
    char buf[48];
    snprintf(buf, sizeof(buf), "%d", H.score);
    pa_text(c, buf, 104.0f, 34.0f, 22.0f, PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 2.0f);

    snprintf(buf, sizeof(buf), "BEST %d", g_best > H.score ? g_best : H.score);
    pa_text(c, buf, (float)c->w * 0.5f + 40.0f, 36.0f, 13.0f,
            PA_RGBA(255, 255, 255, 170), PA_ALIGN_CENTER, 3.0f);

    snprintf(buf, sizeof(buf), "%d", H.coins);
    pa_text(c, buf, (float)c->w - 20.0f, 34.0f, 18.0f,
            PA_RGB(255, 201, 60), PA_ALIGN_RIGHT, 2.0f);

    if (H.score < 4) {
        pa_text(c, "TAP TO HOP - SWIPE TO SIDESTEP", (float)c->w * 0.5f,
                (float)c->h - 46.0f, 12.0f, PA_RGBA(255, 255, 255, 150),
                PA_ALIGN_CENTER, 3.0f);
    }

    /* Idle warning vignette - the eagle's tell. */
    if (H.idle > IDLE_LIMIT - 2.0f && !H.over) {
        float warn = pa_clamp01((H.idle - (IDLE_LIMIT - 2.0f)) * 0.5f);
        PA_Paint vg = pa_radial((float)c->w * 0.5f, (float)c->h * 0.5f,
                                (float)c->h * 0.25f, (float)c->h * 0.75f);
        pa_stop(&vg, 0.0f, PA_RGBA(120, 20, 30, 0));
        pa_stop(&vg, 1.0f, PA_RGBA(120, 20, 30, (int)(warn * 140.0f)));
        pa_fill_rect_paint(c, 0, 0, (float)c->w, (float)c->h, &vg);
    }

    if (H.over) {
        static const char *REASONS[4] = {
            "FLATTENED BY TRAFFIC", "THE EXPRESS DOES NOT BRAKE",
            "INTO THE DRINK", "THE EAGLE GOT BORED OF WAITING"
        };
        float a = pa_clamp01(H.death_t * 2.4f);
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h,
                     PA_RGBA(10, 8, 24, (int)(a * 170.0f)));
        pa_text(c, H.score >= g_best ? "NEW BEST" : "SQUASHED",
                (float)c->w * 0.5f, (float)c->h * 0.42f, 38.0f,
                H.score >= g_best ? PA_RGB(126, 240, 160) : PA_RGB(255, 107, 122),
                PA_ALIGN_CENTER, 6.0f);
        pa_text(c, REASONS[H.death], (float)c->w * 0.5f, (float)c->h * 0.50f, 12.0f,
                PA_RGBA(255, 255, 255, 170), PA_ALIGN_CENTER, 3.0f);
    }
}

static void hopper_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    struct { PA_Color col; float a, b; } bands[5] = {
        { pa_hex(0x8FD4E8), 0.00f, 0.16f },
        { pa_hex(0x4FAE63), 0.16f, 0.34f },
        { pa_hex(0x4A4F5A), 0.34f, 0.54f },
        { pa_hex(0x2E7BD6), 0.54f, 0.74f },
        { pa_hex(0x47A25C), 0.74f, 1.00f }
    };
    for (int i = 0; i < 5; i++) {
        pa_fill_rect(c, x, y + h * bands[i].a, w, h * (bands[i].b - bands[i].a) + 1.0f,
                     bands[i].col);
    }

    float cx = pa_wrapf(t * 0.32f, 1.0f) * (w + 40.0f) - 20.0f;
    pa_round_rect(c, x + cx, y + h * 0.39f, w * 0.22f, h * 0.10f, 3.0f, pa_hex(0xE2455F));
    pa_round_rect(c, x + cx + w * 0.05f, y + h * 0.365f, w * 0.12f, h * 0.05f, 2.0f,
                  pa_hex(0xFF8A9B));

    float lx = w - pa_wrapf(t * 0.22f + 0.4f, 1.0f) * (w + 50.0f);
    pa_round_rect(c, x + lx, y + h * 0.60f, w * 0.34f, h * 0.09f, 4.0f, pa_hex(0x7A5433));

    float bob = fabsf(sinf(t * 3.0f)) * h * 0.05f;
    float px = x + w * 0.5f, py = y + h * 0.80f - bob;
    pa_fill_ellipse(c, px, y + h * 0.845f, w * 0.09f, h * 0.022f, PA_RGBA(0, 0, 0, 50));
    pa_round_rect(c, px - w * 0.08f, py - h * 0.07f, w * 0.16f, h * 0.09f, 3.0f, pa_hex(0xF5D53F));
    pa_round_rect(c, px - w * 0.065f, py - h * 0.125f, w * 0.13f, h * 0.065f, 3.0f, pa_hex(0xFFE87A));
    pa_round_rect(c, px - w * 0.018f, py - h * 0.105f, w * 0.036f, h * 0.024f, 1.0f, pa_hex(0xE8892F));
    pa_fill_circle(c, px - w * 0.032f, py - h * 0.108f, w * 0.014f, pa_hex(0x141024));
    pa_fill_circle(c, px + w * 0.032f, py - h * 0.108f, w * 0.014f, pa_hex(0x141024));
}

const PA_Game PA_GAME_ROADHOPPER = {
    "roadhopper", "Road Hopper", "Endless Hopper",
    "Hop across traffic, log rivers and express rails. The camera never stops creeping.",
    PA_RGB(123, 216, 79),
    hopper_start, hopper_stop, hopper_update, hopper_render, hopper_thumb
};
