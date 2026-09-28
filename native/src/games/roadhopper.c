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
#include <stdlib.h>

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
    float x, dy, z;            /* lateral, forward (negative = toward camera), up */
    float w, h, d;
    PA_Color col;
} Part;

typedef struct {
    Part  parts[16];
    int   count;
    float eye_z, head_w, head_front;
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

#define PART(px, pdy, pz, pw, pd, ph, pc) \
    do { if (c->count < 16) { Part *p_ = &c->parts[c->count++]; \
         p_->x = (px); p_->dy = (pdy); p_->z = (pz); \
         p_->w = (pw); p_->d = (pd); p_->h = (ph); p_->col = (pc); } } while (0)

/*
 * The default character is authored, not rolled: a white voxel chicken with an
 * orange beak and a red comb. A randomly tinted blob is what makes a mascot
 * read as placeholder. Proportions come from the plates - tall and narrow,
 * about half a tile wide, standing a little under a tile high.
 *
 * Every part carries its own forward offset. The first version gave the beak a
 * depth of nearly the whole head and marked it "front" with a fixed nudge; the
 * old camera hid that, and the rotated one showed it as a stripe running the
 * full length of the head.
 */
static void build_chicken(Critter *c) {
    PA_Color white = pa_hex(0xF6F6F0), shade = pa_hex(0xE2E2DA);
    PA_Color orange = pa_hex(0xF0962A), red = pa_hex(0xE23B3B);

    PART(-0.10f,  0.02f, 0.00f, 0.07f, 0.07f, 0.16f, orange);   /* legs   */
    PART( 0.10f,  0.02f, 0.00f, 0.07f, 0.07f, 0.16f, orange);
    PART( 0.00f,  0.04f, 0.14f, 0.50f, 0.48f, 0.40f, white);    /* body   */
    PART( 0.00f,  0.30f, 0.42f, 0.30f, 0.12f, 0.18f, shade);    /* tail   */
    PART(-0.27f,  0.06f, 0.24f, 0.05f, 0.30f, 0.20f, shade);    /* wings  */
    PART( 0.27f,  0.06f, 0.24f, 0.05f, 0.30f, 0.20f, shade);
    PART( 0.00f, -0.08f, 0.54f, 0.38f, 0.32f, 0.30f, white);    /* head   */
    PART( 0.00f, -0.29f, 0.62f, 0.14f, 0.10f, 0.09f, orange);   /* beak   */
    PART( 0.00f, -0.27f, 0.52f, 0.08f, 0.06f, 0.12f, red);      /* wattle */
    PART( 0.00f, -0.08f, 0.84f, 0.09f, 0.20f, 0.12f, red);      /* comb   */

    c->eye_z = 0.74f;
    c->head_w = 0.38f;
    c->head_front = -0.08f - 0.16f;
}

static void build_critter(Critter *c, uint32_t seed) {
    memset(c, 0, sizeof(*c));
    if (seed == CRITTERS[0].seed) { build_chicken(c); return; }

    PA_Rng r;
    pa_rng_seed(&r, seed);

    float hue = pa_rng_next(&r);
    PA_Color body  = pa_hsl(hue, pa_rng_range(&r, 0.45f, 0.78f), pa_rng_range(&r, 0.55f, 0.70f));
    PA_Color belly = pa_shade(body, pa_rng_range(&r, 0.22f, 0.40f));
    PA_Color beak  = pa_hsl(pa_wrapf(hue + pa_rng_range(&r, 0.08f, 0.18f), 1.0f), 0.85f, 0.58f);
    PA_Color feet  = pa_shade(beak, -0.18f);

    float bw = pa_rng_range(&r, 0.46f, 0.58f);
    float bd = bw * 0.90f;
    float bh = pa_rng_range(&r, 0.34f, 0.46f);
    float hw = bw * pa_rng_range(&r, 0.74f, 0.92f);
    float hd = hw * 0.86f;
    float hh = pa_rng_range(&r, 0.28f, 0.38f);
    float head_z = 0.14f + bh;

    PART(-bw * 0.24f, 0.02f, 0.0f, 0.08f, 0.08f, 0.16f, feet);
    PART( bw * 0.24f, 0.02f, 0.0f, 0.08f, 0.08f, 0.16f, feet);
    PART(0.0f, 0.03f, 0.14f, bw, bd, bh, body);
    /* Belly is a thin patch on the front face, not a slab through the body. */
    PART(0.0f, 0.03f - bd * 0.5f - 0.02f, 0.18f, bw * 0.60f, 0.04f, bh * 0.55f, belly);
    PART(0.0f, -0.06f, head_z, hw, hd, hh, body);
    PART(0.0f, -0.06f - hd * 0.5f - 0.05f, head_z + hh * 0.28f, hw * 0.32f, 0.10f, hh * 0.28f, beak);

    int crest = pa_rng_int(&r, 0, 3);
    if (crest == 1) {
        PART(0.0f, -0.06f, head_z + hh, hw * 0.22f, hd * 0.50f, 0.13f, beak);
    } else if (crest == 2) {
        PART(-hw * 0.34f, -0.06f, head_z + hh * 0.90f, 0.10f, 0.08f, 0.17f, body);
        PART( hw * 0.34f, -0.06f, head_z + hh * 0.90f, 0.10f, 0.08f, 0.17f, body);
    } else if (crest == 3) {
        PART(0.0f, -0.06f, head_z + hh, hw * 0.56f, hd * 0.30f, 0.08f, pa_shade(beak, -0.25f));
    }

    c->eye_z = head_z + hh * 0.62f;
    c->head_w = hw;
    c->head_front = -0.06f - hd * 0.5f;
}
#undef PART

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
    float    cam_col;          /* lateral camera follow, in columns */
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
    float tile, rowlen, rise;
    float ax, ay;              /* screen step for +1 column (along the lane)   */
    float fx, fy;              /* screen step for +1 row (forward)             */
    float cx, cy;
    int   ahead, behind, wide;
} L;

/*
 * Measured from the reference plates, not chosen: lanes run downhill to the
 * right at 15 degrees (a line fitted through a water edge across 26 samples),
 * and about seven tiles span the width of a portrait phone. The whole world is
 * rotated, which is the single thing that most separates this look from a
 * top-down grid - a horizontal lane under a shear reads as a flat stripe.
 */
#define LANE_ANGLE_DEG 15.0f

static void compute_layout(int w, int h) {
    L.w = w; L.h = h;
    float th = LANE_ANGLE_DEG * PA_PI / 180.0f;
    L.tile = (float)w / 7.0f;
    L.rowlen = L.tile * 1.08f;
    /* Verticals are tall relative to the ground: the plates show a great deal of
       every car's and building's side face, and a shorter rise reads as a
       top-down map with extruded icons on it. */
    L.rise = L.tile * 1.14f;
    L.ax = cosf(th) * L.tile;
    L.ay = sinf(th) * L.tile;
    L.fx = sinf(th) * L.rowlen;
    L.fy = -cosf(th) * L.rowlen;
    L.cx = (float)w * 0.50f;
    L.cy = (float)h * 0.64f;

    /* A row's band tilts, so its left end sits higher on screen than its
       centre. Coverage has to allow for that or the top-left corner of the
       screen shows sky where there should be road. */
    L.wide = (int)(((float)w * 0.5f) / L.ax) + 5;
    float lift = (float)L.wide * L.ay;
    L.ahead  = (int)ceilf((L.cy + lift + 120.0f) / -L.fy) + 2;
    L.behind = (int)ceilf(((float)h - L.cy + lift + 120.0f) / -L.fy) + 2;
    if (L.ahead > ROW_CAP - 10) L.ahead = ROW_CAP - 10;
    if (L.behind > 18) L.behind = 18;
}

static PA_Vec2 project(float col, float row, float z) {
    float dc = col - H.cam_col;
    float dr = row - H.cam_y;
    PA_Vec2 p;
    p.x = L.cx + dc * L.ax + dr * L.fx;
    p.y = L.cy + dc * L.ay + dr * L.fy - z * L.rise;
    return p;
}

/* ---------------------------------------------------------------- palette -- */
/*
 * Sampled from the reference plates, classifying pixels by hue band across all
 * twenty screenshots. Classic grass is a yellow-green at hue 86 - the first
 * pass used a bluer green around 118, which is most of why it read as generic.
 * The road is a violet grey, not a neutral one.
 */
static PA_Color pal_sky(void)     { return pa_hex(0x8ED8F0); }
static PA_Color pal_grass_a(void) { return pa_hex(0x8AC63C); }
static PA_Color pal_grass_b(void) { return pa_hex(0x7EB430); }
static PA_Color pal_road(void)    { return pa_hex(0x4E4E66); }
static PA_Color pal_water(void)   { return pa_hex(0x309CF6); }
static PA_Color pal_rail(void)    { return pa_hex(0x6A5E7A); }

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
                row->tree_h[c] = pa_rng_range(&r, 0.85f, 1.75f);
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
            row->tree_h[c] = pa_rng_range(&r, 0.80f, 1.60f);
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
    /* Partial follow: the camera leans toward the player without centring on
       them, so the darkened verge comes into view near the edge of play. */
    H.cam_col = pa_approach(H.cam_col, H.draw_x * 0.55f, 5.0f, dt);

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
/*
 * Everything in the world is a box, drawn the way the reference draws it: three
 * flat tones - a light top, a mid-tone front, a dark end - and a hard shadow.
 * No gradients, no soft blobs. The reference is flat shaded; the depth comes
 * from the tones being consistent and the shadows being crisp.
 */

static float signed_area(const PA_Vec2 *q, int n) {
    float a = 0.0f;
    for (int i = 0; i < n; i++) {
        const PA_Vec2 *p0 = &q[i], *p1 = &q[(i + 1) % n];
        a += p0->x * p1->y - p1->x * p0->y;
    }
    return a;
}

/**
 * Box of footprint w (along the lane) by d (forward) and height h, standing at
 * (col, row) with its base at z. Side faces are culled by screen winding: a face
 * is visible exactly when it winds the same way as the top, since all four are
 * listed outward-counter-clockwise. That is robust to the camera angle, where a
 * hard-coded "draw the south and east faces" would silently break the moment
 * the angle changed.
 */
static void box3(PA_Canvas *c, float col, float row, float z,
                 float w, float d, float h, PA_Color colour) {
    float hw = w * 0.5f, hd = d * 0.5f;
    PA_Vec2 bot[4] = {
        project(col - hw, row - hd, z), project(col + hw, row - hd, z),
        project(col + hw, row + hd, z), project(col - hw, row + hd, z)
    };
    PA_Vec2 top[4] = {
        project(col - hw, row - hd, z + h), project(col + hw, row - hd, z + h),
        project(col + hw, row + hd, z + h), project(col - hw, row + hd, z + h)
    };

    float minx = top[0].x, maxx = top[0].x, miny = top[0].y, maxy = bot[0].y;
    for (int i = 0; i < 4; i++) {
        if (top[i].x < minx) minx = top[i].x;
        if (top[i].x > maxx) maxx = top[i].x;
        if (bot[i].x < minx) minx = bot[i].x;
        if (bot[i].x > maxx) maxx = bot[i].x;
        if (top[i].y < miny) miny = top[i].y;
        if (bot[i].y > maxy) maxy = bot[i].y;
    }
    if (maxx < -20.0f || minx > (float)L.w + 20.0f || maxy < -20.0f || miny > (float)L.h + 20.0f)
        return;

    float top_sign = signed_area(top, 4);
    /* South and north faces (the long sides along the lane) take the mid tone;
       the two ends take the dark tone. */
    static const float tone[4] = { -0.24f, -0.44f, -0.24f, -0.44f };

    for (int f = 0; f < 4; f++) {
        int i = f, j = (f + 1) % 4;
        PA_Vec2 face[4] = { bot[i], bot[j], top[j], top[i] };
        float s = signed_area(face, 4);
        if ((s > 0.0f) != (top_sign > 0.0f) || fabsf(s) < 0.5f) continue;
        pa_fill_poly(c, face, 4, pa_shade(colour, tone[f]));
    }
    pa_fill_poly(c, top, 4, colour);
}

/* Andrew's monotone chain, for the swept shadow footprint. */
static int cross_z(PA_Vec2 o, PA_Vec2 a, PA_Vec2 b) {
    float v = (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    return v > 0.0f ? 1 : (v < 0.0f ? -1 : 0);
}

static int vec_cmp(const void *pa, const void *pb) {
    const PA_Vec2 *a = (const PA_Vec2 *)pa, *b = (const PA_Vec2 *)pb;
    if (a->x != b->x) return a->x < b->x ? -1 : 1;
    if (a->y != b->y) return a->y < b->y ? -1 : 1;
    return 0;
}

static int hull(PA_Vec2 *pts, int n, PA_Vec2 *out) {
    qsort(pts, (size_t)n, sizeof(PA_Vec2), vec_cmp);
    int k = 0;
    for (int i = 0; i < n; i++) {
        while (k >= 2 && cross_z(out[k - 2], out[k - 1], pts[i]) <= 0) k--;
        out[k++] = pts[i];
    }
    for (int i = n - 2, t = k + 1; i >= 0; i--) {
        while (k >= t && cross_z(out[k - 2], out[k - 1], pts[i]) <= 0) k--;
        out[k++] = pts[i];
    }
    return k - 1;
}

/**
 * Hard shadow: the footprint swept toward the light's far side by an amount
 * proportional to height. The reference casts every shadow down and to the
 * right as a crisp flat shape about a third darker than the ground under it;
 * the first native pass used soft radial blobs, which is a different idiom.
 */
static void hard_shadow(PA_Canvas *c, float col, float row, float w, float d, float h) {
    float hw = w * 0.5f, hd = d * 0.5f;
    float ox = h * 0.62f, oy = -h * 0.30f;
    PA_Vec2 pts[8] = {
        project(col - hw, row - hd, 0), project(col + hw, row - hd, 0),
        project(col + hw, row + hd, 0), project(col - hw, row + hd, 0),
        project(col - hw + ox, row - hd + oy, 0), project(col + hw + ox, row - hd + oy, 0),
        project(col + hw + ox, row + hd + oy, 0), project(col - hw + ox, row + hd + oy, 0)
    };
    PA_Vec2 out[18];
    int n = hull(pts, 8, out);
    if (n >= 3) pa_fill_poly(c, out, n, PA_RGBA(20, 18, 48, 92));
}

/* ------------------------------------------------------------ ground rows -- */
static PA_Color row_colour(const Row *r) {
    switch (r->type) {
        case ROW_ROAD:  return pal_road();
        case ROW_WATER: return pal_water();
        case ROW_RAIL:  return pal_rail();
        default:        return r->band ? pal_grass_b() : pal_grass_a();
    }
}

static void band(PA_Canvas *c, float c0, float c1, float row0, float row1, PA_Color col) {
    PA_Vec2 q[4] = {
        project(c0, row0, 0), project(c1, row0, 0),
        project(c1, row1, 0), project(c0, row1, 0)
    };
    pa_fill_poly(c, q, 4, col);
}

static void draw_ground(PA_Canvas *c, Row *r) {
    float idx = (float)r->index;
    float lo = H.cam_col - (float)L.wide, hi = H.cam_col + (float)L.wide;
    PA_Color col = row_colour(r);

    /* Full-width lanes, as in the reference - the finite slab was an invention.
       Outside the playable columns the ground darkens, which is how the
       reference marks the edge without drawing a wall. */
    PA_Color outside = pa_shade(col, -0.16f);
    band(c, lo, -EDGE, idx - 0.5f, idx + 0.5f, outside);
    band(c, EDGE, hi, idx - 0.5f, idx + 0.5f, outside);

    if (r->type == ROW_GRASS) {
        /* Checker within the row, not just alternating rows: a flat field of
           one green is the clearest tell of an unfinished scene. */
        for (int t = -HALF; t <= HALF; t++) {
            int odd = ((t + r->index) & 1) != 0;
            band(c, (float)t - 0.5f, (float)t + 0.5f, idx - 0.5f, idx + 0.5f,
                 odd ? pal_grass_b() : pal_grass_a());
        }
    } else {
        band(c, -EDGE, EDGE, idx - 0.5f, idx + 0.5f, col);
    }

    if (r->type == ROW_ROAD) {
        /* Lane dashes on the far edge of the lane, pale and short. */
        for (float t = lo; t < hi; t += 1.0f) {
            float x0 = floorf(t) + 0.15f;
            PA_Vec2 q[4] = {
                project(x0, idx + 0.46f, 0.0f), project(x0 + 0.55f, idx + 0.46f, 0.0f),
                project(x0 + 0.55f, idx + 0.52f, 0.0f), project(x0, idx + 0.52f, 0.0f)
            };
            pa_fill_poly(c, q, 4, PA_RGBA(196, 196, 214, 150));
        }
    } else if (r->type == ROW_WATER) {
        /* Foam specks drifting with the current. Seeded per row and scrolled by
           time, so they move without shimmering. */
        uint32_t bits = row_seed(r->index);
        for (int k = 0; k < 7; k++) {
            float t = pa_wrapf((float)((bits >> (k * 4)) & 15) * 1.7f
                               + H.time * 0.6f * (float)(r->dir ? r->dir : 1), (float)(L.wide * 2));
            float fx = lo + t;
            float fr = idx - 0.3f + (float)((bits >> (k * 3 + 1)) & 3) * 0.2f;
            PA_Vec2 q[4] = {
                project(fx, fr, 0), project(fx + 0.30f, fr, 0),
                project(fx + 0.30f, fr + 0.06f, 0), project(fx, fr + 0.06f, 0)
            };
            pa_fill_poly(c, q, 4, PA_RGBA(255, 255, 255, 120));
        }
    } else if (r->type == ROW_RAIL) {
        /* Sleepers, then two rails over them. */
        for (float t = floorf(lo); t < hi; t += 0.5f) {
            PA_Vec2 q[4] = {
                project(t, idx - 0.34f, 0), project(t + 0.20f, idx - 0.34f, 0),
                project(t + 0.20f, idx + 0.34f, 0), project(t, idx + 0.34f, 0)
            };
            pa_fill_poly(c, q, 4, pa_hex(0x5B4636));
        }
        for (int s2 = -1; s2 <= 1; s2 += 2) {
            float rr = idx + (float)s2 * 0.20f;
            band(c, lo, hi, rr - 0.035f, rr + 0.035f, pa_hex(0x9C98B0));
        }
    }
}

/* ------------------------------------------------------------- drawables -- */
enum { DR_TREE, DR_DECO, DR_CAR, DR_LOG, DR_TRAIN, DR_COIN, DR_SIGNAL, DR_PLAYER };

typedef struct { float key; int kind; int row; int idx; float x; } Drawable;

#define MAX_DRAW 900
static Drawable g_draw[MAX_DRAW];
static int      g_draw_n;

static void push(int kind, int row, int idx, float x, float z_bias) {
    if (g_draw_n >= MAX_DRAW) return;
    PA_Vec2 g = project(x, (float)row, 0.0f);
    Drawable *d = &g_draw[g_draw_n++];
    d->key = g.y + z_bias;
    d->kind = kind; d->row = row; d->idx = idx; d->x = x;
}

static int draw_cmp(const void *a, const void *b) {
    float d = ((const Drawable *)a)->key - ((const Drawable *)b)->key;
    return d < 0.0f ? -1 : (d > 0.0f ? 1 : 0);
}

/** Decorative scenery outside the playable columns, derived from the row hash
    so it costs no simulation state. The reference packs its edges with trees;
    empty verges were half of why ours read as a test level. */
static int deco_at(const Row *r, int col, float *h_out, float *s_out) {
    if (r->type != ROW_GRASS) return 0;
    uint32_t h = row_seed(r->index * 131 + col * 7919);
    if ((h & 7) > 3) return 0;
    *h_out = 0.8f + (float)((h >> 4) & 15) / 15.0f * 1.2f;
    *s_out = 0.66f + (float)((h >> 8) & 7) / 7.0f * 0.22f;
    return 1;
}

static void draw_tree(PA_Canvas *c, float col, float row, float h, float s) {
    if (h < 1.0f) {
        /* Low bush: two stacked blocks, the second set back and lighter. */
        hard_shadow(c, col, row, s, s, h * 0.7f);
        box3(c, col, row, 0.0f, s, s, h * 0.45f, pa_hex(0x4E9A3A));
        box3(c, col - 0.04f, row + 0.04f, h * 0.45f, s * 0.70f, s * 0.70f, h * 0.30f, pa_hex(0x62B048));
        return;
    }
    /* Tree: trunk clear of the crown, crown stepping inward as it rises. */
    hard_shadow(c, col, row, s, s, h + 0.8f);
    box3(c, col, row, 0.0f, 0.26f, 0.26f, 0.70f, pa_hex(0x7A5333));
    box3(c, col, row, 0.66f, s, s, h * 0.55f, pa_hex(0x3F8F3C));
    box3(c, col - 0.02f, row + 0.02f, 0.66f + h * 0.55f, s * 0.76f, s * 0.76f, h * 0.36f,
         pa_hex(0x55A844));
    box3(c, col - 0.04f, row + 0.04f, 0.66f + h * 0.91f, s * 0.46f, s * 0.46f, h * 0.22f,
         pa_hex(0x6DBE52));
}

static void draw_car(PA_Canvas *c, const Row *r, const Mover *m) {
    PA_Color body = pa_hsl(m->hue, 0.72f, 0.56f);
    float flip = (float)r->dir;
    float row = (float)r->index;
    float len = m->len;

    hard_shadow(c, m->x, row, len, 0.66f, 0.70f);

    /* Wheels wider than the body in depth, so they show below it - the
       projection's recurring trap. */
    for (int q = -1; q <= 1; q += 2) {
        box3(c, m->x + (float)q * len * 0.30f, row, 0.0f, len * 0.20f, 0.74f, 0.16f,
             pa_hex(0x24222E));
    }

    if (m->big) {
        /* Truck: tall pale box behind a coloured cab. */
        box3(c, m->x - flip * len * 0.14f, row, 0.12f, len * 0.70f, 0.64f, 0.66f, pa_hex(0xE4E4EE));
        box3(c, m->x + flip * len * 0.34f, row, 0.12f, len * 0.30f, 0.62f, 0.42f, body);
        box3(c, m->x + flip * len * 0.34f, row, 0.54f, len * 0.22f, 0.56f, 0.10f, pa_hex(0xDDE8F4));
    } else {
        box3(c, m->x, row, 0.12f, len, 0.62f, 0.26f, body);
        /* Cabin: white roof block, set back, with a dark glass band all round -
           the reference's cars are all built this way. */
        box3(c, m->x - flip * len * 0.06f, row, 0.38f, len * 0.54f, 0.56f, 0.20f, pa_hex(0x2A2D44));
        box3(c, m->x - flip * len * 0.06f, row, 0.54f, len * 0.50f, 0.52f, 0.08f, pa_hex(0xF2F4F8));
    }

    PA_Vec2 lp = project(m->x + flip * len * 0.50f, row - 0.20f, 0.24f);
    pa_fill_rect(c, lp.x - 3.0f, lp.y - 2.0f, 6.0f, 4.0f, PA_RGBA(255, 246, 200, 255));
}

static void draw_log(PA_Canvas *c, const Row *r, const Mover *m) {
    float row = (float)r->index;
    /* Logs sit in the water, so the shadow is the water darkening, not a cast
       shape on the ground. */
    band(c, m->x - m->len * 0.5f + 0.1f, m->x + m->len * 0.5f + 0.25f, row - 0.42f, row + 0.30f,
         PA_RGBA(20, 60, 120, 70));
    box3(c, m->x, row, -0.04f, m->len, 0.62f, 0.26f, pa_hex(0x8A5A3A));
    for (int seg = 0; seg < 4; seg++) {
        float t = -0.36f + (float)seg * 0.24f;
        box3(c, m->x + m->len * t, row, 0.22f, m->len * 0.03f, 0.60f, 0.012f, pa_hex(0x6A4228));
    }
}

static void draw_train(PA_Canvas *c, const Row *r) {
    float row = (float)r->index;
    hard_shadow(c, r->train_x, row, 9.0f, 0.78f, 0.9f);
    for (int car = -1; car <= 1; car++) {
        float x = r->train_x + (float)car * 3.05f;
        box3(c, x, row, 0.02f, 2.90f, 0.78f, 0.86f, pa_hex(0xD84A3E));
        box3(c, x, row, 0.36f, 2.70f, 0.80f, 0.22f, pa_hex(0x2A2D44));
        box3(c, x, row, 0.88f, 2.80f, 0.70f, 0.08f, pa_hex(0xE8E6F0));
    }
}

/* Close enough in to be on screen at the default camera; one per rail row,
   alternating sides so a run of tracks does not line them up like fence posts. */
#define SIGNAL_COL 3.4f

static void draw_signal(PA_Canvas *c, const Row *r, float side) {
    float row = (float)r->index + 0.45f;
    float col = side * SIGNAL_COL;
    hard_shadow(c, col, row, 0.14f, 0.14f, 1.4f);
    /* Red and white striped post, black crossbar, a lamp that glows before a
       train - lifted straight from the plates because it is how the player
       learns to read a rail row. */
    for (int k = 0; k < 5; k++) {
        box3(c, col, row, (float)k * 0.26f, 0.12f, 0.12f, 0.26f,
             (k & 1) ? pa_hex(0xF2F2F2) : pa_hex(0xD8363A));
    }
    box3(c, col, row, 1.26f, 0.62f, 0.10f, 0.14f, pa_hex(0x1C1A24));
    int blink = r->warn && ((int)(H.time * 6.0f) % 2) == 0;
    PA_Vec2 lp = project(col, row - 0.07f, 1.10f);
    if (blink) {
        PA_Paint glow = pa_radial(lp.x, lp.y, 0.0f, L.tile * 0.9f);
        pa_stop(&glow, 0.0f, PA_RGBA(255, 60, 60, 150));
        pa_stop(&glow, 1.0f, PA_RGBA(255, 60, 60, 0));
        pa_fill_ellipse_paint(c, lp.x, lp.y, L.tile * 0.9f, L.tile * 0.9f, &glow);
    }
    pa_fill_circle(c, lp.x, lp.y, L.tile * 0.07f, blink ? pa_hex(0xFF4040) : pa_hex(0x5A1C20));
}

static void draw_coin(PA_Canvas *c, const Row *r) {
    float cx = (float)(r->coin - HALF);
    float bob = 0.30f + sinf(H.time * 4.0f + (float)r->index) * 0.06f;
    hard_shadow(c, cx, (float)r->index, 0.26f, 0.26f, 0.3f);
    box3(c, cx, (float)r->index, bob, 0.30f, 0.08f, 0.30f, pa_hex(0xFFC93C));
}

static void draw_player(PA_Canvas *c) {
    Critter *ch = &H.critter;
    float z = H.hop_z;
    float squash = H.hopping ? 1.0f : (1.0f + sinf(H.time * 6.0f) * 0.02f);

    /* The shadow stays on the ground while the body rises, so a hop reads as
       height rather than as the whole character sliding up the screen. */
    hard_shadow(c, H.draw_x, H.draw_y, 0.62f, 0.56f, 0.9f - z * 0.5f);

    if (H.over && H.death == 0) {
        float flat = pa_clamp01(H.death_t * 6.0f);
        for (int i = 0; i < ch->count; i++) {
            Part *p = &ch->parts[i];
            box3(c, H.draw_x + p->x, H.draw_y + p->dy, p->z * (1.0f - flat * 0.9f),
                 p->w * (1.0f + flat * 0.5f), p->d * (1.0f + flat * 0.5f),
                 p->h * (1.0f - flat * 0.85f), p->col);
        }
        return;
    }

    for (int i = 0; i < ch->count; i++) {
        Part *p = &ch->parts[i];
        box3(c, H.draw_x + p->x, H.draw_y + p->dy,
             (p->z + z) * squash, p->w, p->d, p->h * squash, p->col);
    }
    /* Eyes on the front face of the head, near its outer corners, as square
       voxels like everything else. */
    for (int e = -1; e <= 1; e += 2) {
        PA_Vec2 ep = project(H.draw_x + (float)e * ch->head_w * 0.30f,
                             H.draw_y + ch->head_front - 0.005f, (ch->eye_z + z) * squash);
        float sz = L.tile * 0.065f;
        pa_fill_rect(c, ep.x - sz * 0.5f, ep.y - sz * 0.5f, sz, sz, pa_hex(0x141024));
    }
}

static void hopper_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) compute_layout(c->w, c->h);

    pa_clear(c, pal_sky());

    int far = (int)ceilf(H.cam_y) + L.ahead;
    int near = (int)floorf(H.cam_y) - L.behind;

    /* 1. Ground, far to near. */
    for (int i = far; i >= near; i--) {
        Row *r = row_at(i);
        if (r) draw_ground(c, r);
    }

    /* 2. Everything standing on the ground, sorted by where it touches the
          ground on screen. Sorting per object rather than per row is what the
          rotated camera requires: within one row, a prop further right is
          nearer the camera, so row order alone gets overlaps wrong. */
    g_draw_n = 0;
    for (int i = far; i >= near; i--) {
        Row *r = row_at(i);
        if (!r) continue;
        if (r->type == ROW_GRASS) {
            for (int t = 0; t < COLS; t++) {
                if (r->has_tree[t]) push(DR_TREE, i, t, (float)(t - HALF), 0.0f);
            }
            for (int t = HALF + 1; t <= L.wide + HALF + 2; t++) {
                float hh, ss;
                if (deco_at(r, t, &hh, &ss)) push(DR_DECO, i, t, (float)t, 0.0f);
                if (deco_at(r, -t, &hh, &ss)) push(DR_DECO, i, -t, (float)(-t), 0.0f);
            }
        } else if (r->type == ROW_ROAD) {
            for (int m = 0; m < r->mover_count; m++) push(DR_CAR, i, m, r->movers[m].x, 0.0f);
        } else if (r->type == ROW_WATER) {
            /* Logs sort well behind anything at the same spot, so a rider is
               always drawn on top of the log it is standing on. */
            for (int m = 0; m < r->mover_count; m++) push(DR_LOG, i, m, r->movers[m].x, -L.tile);
        } else if (r->type == ROW_RAIL) {
            push(DR_SIGNAL, i, (r->index & 1) ? 1 : -1,
                 (r->index & 1) ? SIGNAL_COL : -SIGNAL_COL, 0.0f);
            if (r->train_on) push(DR_TRAIN, i, 0, r->train_x, 0.0f);
        }
        if (r->coin != -99) push(DR_COIN, i, 0, (float)(r->coin - HALF), 0.0f);
    }
    push(DR_PLAYER, (int)floorf(H.draw_y + 0.5f), 0, H.draw_x, 2.0f);
    qsort(g_draw, (size_t)g_draw_n, sizeof(Drawable), draw_cmp);

    for (int k = 0; k < g_draw_n; k++) {
        Drawable *d = &g_draw[k];
        Row *r = row_at(d->row);
        switch (d->kind) {
            case DR_TREE:
                if (r) draw_tree(c, d->x, (float)d->row, r->tree_h[d->idx], r->tree_s[d->idx]);
                break;
            case DR_DECO: {
                float hh, ss;
                if (r && deco_at(r, d->idx, &hh, &ss)) draw_tree(c, d->x, (float)d->row, hh, ss);
                break;
            }
            case DR_CAR:    if (r) draw_car(c, r, &r->movers[d->idx]); break;
            case DR_LOG:    if (r) draw_log(c, r, &r->movers[d->idx]); break;
            case DR_TRAIN:  if (r) draw_train(c, r); break;
            case DR_SIGNAL: if (r) draw_signal(c, r, (float)d->idx); break;
            case DR_COIN:   if (r) draw_coin(c, r); break;
            case DR_PLAYER: draw_player(c); break;
        }
    }

    /* Eagle, on the idle death. */
    if (H.eagle > 0.0f) {
        PA_Vec2 p = project(H.draw_x, H.draw_y, pa_lerpf(4.5f, 0.35f, pa_smooth(H.eagle)));
        float w = L.tile * 1.5f;
        float flap = sinf(H.time * 14.0f) * w * 0.16f;
        PA_Vec2 wing[5] = {
            { p.x, p.y }, { p.x - w, p.y - w * 0.35f + flap },
            { p.x - w * 0.3f, p.y + w * 0.12f }, { p.x + w * 0.3f, p.y + w * 0.12f },
            { p.x + w, p.y - w * 0.35f - flap }
        };
        pa_fill_poly(c, wing, 5, pa_hex(0x2A2438));
    }

    /* HUD, in the reference's idiom: chunky outlined numerals in the two top
       corners and almost nothing else. Left of x=100 is the hub's MENU. */
    char buf[48];
    snprintf(buf, sizeof(buf), "%d", H.score);
    pa_text_bold(c, buf, 106.0f, 22.0f, 40.0f, PA_RGB(255, 255, 255), PA_RGB(20, 18, 28),
                 PA_ALIGN_LEFT, 3.0f, 2.3f);
    /* Coin count with a coin glyph to its right, as in the reference: a small
       outlined square in red carrying a pale mark. */
    float glyph = 20.0f;
    float gx = (float)c->w - 22.0f - glyph, gy = 30.0f;
    pa_round_rect(c, gx - 3.0f, gy - 3.0f, glyph + 6.0f, glyph + 6.0f, 5.0f, PA_RGB(20, 18, 28));
    pa_round_rect(c, gx, gy, glyph, glyph, 3.0f, pa_hex(0xE8453C));
    pa_fill_rect(c, gx + glyph * 0.30f, gy + glyph * 0.24f, glyph * 0.40f, glyph * 0.18f, pa_hex(0xFFE0A0));
    pa_fill_rect(c, gx + glyph * 0.30f, gy + glyph * 0.24f, glyph * 0.18f, glyph * 0.52f, pa_hex(0xFFE0A0));
    pa_fill_rect(c, gx + glyph * 0.30f, gy + glyph * 0.58f, glyph * 0.40f, glyph * 0.18f, pa_hex(0xFFE0A0));
    snprintf(buf, sizeof(buf), "%d", H.coins);
    pa_text_bold(c, buf, gx - 10.0f, 26.0f, 28.0f, PA_RGB(255, 222, 40),
                 PA_RGB(20, 18, 28), PA_ALIGN_RIGHT, 3.0f, 2.3f);
    snprintf(buf, sizeof(buf), "TOP %d", g_best > H.score ? g_best : H.score);
    pa_text(c, buf, 108.0f, 76.0f, 12.0f, PA_RGBA(255, 255, 255, 210), PA_ALIGN_LEFT, 2.0f);

    if (H.score < 1 && !H.over) {
        pa_text_bold(c, "TAP TO HOP", (float)c->w * 0.5f, (float)c->h * 0.86f, 20.0f,
                     PA_RGB(255, 255, 255), PA_RGB(20, 18, 28), PA_ALIGN_CENTER, 4.0f, 1.6f);
    }

    if (H.idle > IDLE_LIMIT - 2.0f && !H.over) {
        float warn = pa_clamp01((H.idle - (IDLE_LIMIT - 2.0f)) * 0.5f);
        pa_vignette(c, warn * 0.9f);
    }

    if (H.over) {
        static const char *REASONS[4] = {
            "FLATTENED", "HIT BY A TRAIN", "SPLASH", "THE EAGLE GOT YOU"
        };
        float a = pa_clamp01(H.death_t * 2.4f);
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(10, 8, 24, (int)(a * 150.0f)));
        pa_text_bold(c, REASONS[H.death], (float)c->w * 0.5f, (float)c->h * 0.40f, 30.0f,
                     PA_RGB(255, 255, 255), PA_RGB(20, 18, 28), PA_ALIGN_CENTER, 4.0f, 2.0f);
        snprintf(buf, sizeof(buf), "SCORE %d", H.score);
        pa_text_bold(c, buf, (float)c->w * 0.5f, (float)c->h * 0.48f, 20.0f,
                     PA_RGB(255, 222, 40), PA_RGB(20, 18, 28), PA_ALIGN_CENTER, 3.0f, 1.8f);
    }
}

static void hopper_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    /* The rebuilt game's palette: checker grass, slate road, bright water. */
    struct { uint32_t col; float a, b; } bands[5] = {
        { 0x8AC63C, 0.00f, 0.22f },
        { 0x4E4E66, 0.22f, 0.44f },
        { 0x7EB430, 0.44f, 0.56f },
        { 0x309CF6, 0.56f, 0.76f },
        { 0x8AC63C, 0.76f, 1.00f }
    };
    for (int i = 0; i < 5; i++)
        pa_fill_rect(c, x, y + h * bands[i].a, w, h * (bands[i].b - bands[i].a) + 1.0f,
                     pa_hex(bands[i].col));
    for (float dx = 0.08f; dx < 1.0f; dx += 0.24f)
        pa_fill_rect(c, x + w * dx, y + h * 0.325f, w * 0.10f, h * 0.012f, PA_RGBA(255, 255, 255, 170));

    /* A car on the road with its three-tone sides, a log on the water. */
    float cx = pa_wrapf(t * 0.32f, 1.0f) * (w + 40.0f) - 20.0f;
    pa_fill_rect(c, x + cx, y + h * 0.36f, w * 0.24f, h * 0.05f, pa_hex(0xA8243A));
    pa_fill_rect(c, x + cx, y + h * 0.28f, w * 0.24f, h * 0.08f, pa_hex(0xE8455F));
    pa_fill_rect(c, x + cx + w * 0.05f, y + h * 0.25f, w * 0.13f, h * 0.04f, pa_hex(0x9FD2FF));
    float lx = w - pa_wrapf(t * 0.22f + 0.4f, 1.0f) * (w + 50.0f);
    pa_fill_rect(c, x + lx, y + h * 0.66f, w * 0.34f, h * 0.04f, pa_hex(0x5A3A22));
    pa_fill_rect(c, x + lx, y + h * 0.61f, w * 0.34f, h * 0.05f, pa_hex(0x8A5A34));

    /* The chicken: white body, red comb, orange beak, hard shadow. */
    float bob = fabsf(sinf(t * 3.0f)) * h * 0.05f;
    float px = x + w * 0.5f, py = y + h * 0.86f - bob;
    pa_fill_rect(c, px - w * 0.05f, y + h * 0.86f, w * 0.14f, h * 0.03f, PA_RGBA(20, 18, 48, 70));
    pa_fill_rect(c, px - w * 0.07f, py - h * 0.10f, w * 0.14f, h * 0.10f, pa_hex(0xDCDCE6));
    pa_fill_rect(c, px - w * 0.07f, py - h * 0.15f, w * 0.14f, h * 0.05f, PA_RGB(255, 255, 255));
    pa_fill_rect(c, px - w * 0.02f, py - h * 0.18f, w * 0.04f, h * 0.03f, pa_hex(0xE8283C));
    pa_fill_rect(c, px - w * 0.015f, py - h * 0.09f, w * 0.03f, h * 0.02f, pa_hex(0xF79A2E));
    pa_fill_rect(c, px - w * 0.045f, py - h * 0.12f, w * 0.015f, h * 0.02f, pa_hex(0x141024));
    pa_fill_rect(c, px + w * 0.03f, py - h * 0.12f, w * 0.015f, h * 0.02f, pa_hex(0x141024));
}

const PA_Game PA_GAME_ROADHOPPER = {
    "roadhopper", "Road Hopper", "Endless Hopper",
    "Hop across traffic, log rivers and express rails. The camera never stops creeping.",
    PA_RGB(123, 216, 79),
    hopper_start, hopper_stop, hopper_update, hopper_render, hopper_thumb
};
