/* ===========================================================================
   BLOCK STORM - native port
   Eight by eight board, three pieces in the tray, drag one onto the grid.
   Fill a row or a column and it clears. No gravity, no timer: the only thing
   that ends a run is having nowhere left to put any of the three.

   The art follows the same rules as the rest of the native suite - flat shaded
   and vivid, with a bevel and a contact shadow doing the depth work, since
   there is no lighting in the scene to do it.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define N          8
#define TRAY       3
#define MAX_CELLS  9

typedef struct {
    int w, h, count;
    signed char cx[MAX_CELLS], cy[MAX_CELLS];
    int weight;
} Shape;

/* Standard block-puzzle set: lines, squares, corners and rectangles. */
static const Shape SHAPES[] = {
    { 1,1,1, {0},{0}, 6 },
    { 2,1,2, {0,1},{0,0}, 8 },
    { 1,2,2, {0,0},{0,1}, 8 },
    { 3,1,3, {0,1,2},{0,0,0}, 8 },
    { 1,3,3, {0,0,0},{0,1,2}, 8 },
    { 4,1,4, {0,1,2,3},{0,0,0,0}, 5 },
    { 1,4,4, {0,0,0,0},{0,1,2,3}, 5 },
    { 5,1,5, {0,1,2,3,4},{0,0,0,0,0}, 2 },
    { 1,5,5, {0,0,0,0,0},{0,1,2,3,4}, 2 },
    { 2,2,4, {0,1,0,1},{0,0,1,1}, 7 },
    { 3,3,9, {0,1,2,0,1,2,0,1,2},{0,0,0,1,1,1,2,2,2}, 1 },
    { 2,2,3, {0,0,1},{0,1,1}, 5 },
    { 2,2,3, {1,0,1},{0,1,1}, 5 },
    { 2,2,3, {0,1,0},{0,0,1}, 5 },
    { 2,2,3, {0,1,1},{0,0,1}, 5 },
    { 3,2,6, {0,1,2,0,1,2},{0,0,0,1,1,1}, 3 },
    { 2,3,6, {0,1,0,1,0,1},{0,0,1,1,2,2}, 3 },
    { 3,3,5, {0,0,0,1,2},{0,1,2,2,2}, 3 },
    { 3,3,5, {2,2,0,1,2},{0,1,2,2,2}, 3 },
    { 3,3,5, {0,1,2,0,0},{0,0,0,1,2}, 3 },
    { 3,3,5, {0,1,2,2,2},{0,0,0,1,2}, 3 },
    { 3,2,4, {0,1,2,1},{0,0,0,1}, 4 },
    { 2,3,4, {0,0,1,0},{0,1,1,2}, 4 }
};
#define SHAPE_COUNT ((int)(sizeof(SHAPES) / sizeof(SHAPES[0])))

/* Block colours, matched to the reference's seven hue families: fully saturated
   primaries, because the board is read as pattern and two blocks a few points
   apart in hue would read as one colour. */
static const uint32_t TINTS[] = {
    0xE8323C, 0xF46A20, 0xF8C41C, 0x3CC44A, 0x28C8E8, 0x3E6CF0, 0xA050E0
};
#define TINT_COUNT ((int)(sizeof(TINTS) / sizeof(TINTS[0])))

typedef struct {
    int shape;          /* index into SHAPES, -1 when the slot is spent */
    int tint;
} Slot;

typedef struct {
    PA_Rng rand;
    int    cell[N * N];        /* -1 empty, else tint index */
    float  pop[N * N];         /* clear animation, 1 -> 0 */
    Slot   tray[TRAY];
    int    score, combo, placed;
    int    dragging;           /* tray index, -1 when idle */
    float  drag_x, drag_y;
    int    over;
    float  over_t, time;
    char   banner[24];
    float  banner_t;
} Storm;

static Storm B;
static int   g_best, g_best_loaded;

static struct { int w, h; float cell, ox, oy, tray_y, tray_cell; } L;

static int idx(int x, int y) { return y * N + x; }

static void compute_layout(int w, int h) {
    L.w = w; L.h = h;
    /* Proportions measured off the plates: the board spans ~72% of the width,
       its top sits ~17% down, and the tray pieces are ~0.6 of a board cell. */
    float side = (float)w * 0.80f;
    float oy = (float)h * 0.17f;
    if (oy < 132.0f) oy = 132.0f;
    /* Height must also hold the tray: its pieces are up to 4 tray cells tall. */
    float avail_h = ((float)h - oy - 24.0f) / (1.0f + 0.62f * 4.6f / (float)N);
    if (side > avail_h) side = avail_h;
    L.cell = side / (float)N;
    L.ox = ((float)w - L.cell * N) * 0.5f;
    L.oy = oy;
    /* Tray centred in the space under the board, as on the plates, instead of
       hugging the board and leaving a dead strip at the bottom of tall phones. */
    float below = L.oy + L.cell * N;
    L.tray_cell = L.cell * 0.62f;
    L.tray_y = below + ((float)h * 0.94f - below) * 0.5f - L.tray_cell * 2.5f;
    if (L.tray_y < below + L.cell * 0.4f) L.tray_y = below + L.cell * 0.4f;
}

/* ------------------------------------------------------------- generation */
static int roll_shape(PA_Rng *r) {
    int total = 0;
    for (int i = 0; i < SHAPE_COUNT; i++) total += SHAPES[i].weight;
    int roll = pa_rng_int(r, 0, total - 1);
    for (int i = 0; i < SHAPE_COUNT; i++) {
        roll -= SHAPES[i].weight;
        if (roll < 0) return i;
    }
    return 0;
}

static void refill_tray(void) {
    for (int i = 0; i < TRAY; i++) {
        B.tray[i].shape = roll_shape(&B.rand);
        B.tray[i].tint = pa_rng_int(&B.rand, 0, TINT_COUNT - 1);
    }
}

static int fits(int shape, int gx, int gy) {
    const Shape *s = &SHAPES[shape];
    if (gx < 0 || gy < 0 || gx + s->w > N || gy + s->h > N) return 0;
    for (int i = 0; i < s->count; i++) {
        if (B.cell[idx(gx + s->cx[i], gy + s->cy[i])] >= 0) return 0;
    }
    return 1;
}

static int any_fits(int shape) {
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) if (fits(shape, x, y)) return 1;
    }
    return 0;
}

static int tray_playable(void) {
    for (int i = 0; i < TRAY; i++) {
        if (B.tray[i].shape >= 0 && any_fits(B.tray[i].shape)) return 1;
    }
    return 0;
}

static void banner(const char *t) {
    snprintf(B.banner, sizeof(B.banner), "%s", t);
    B.banner_t = 1.4f;
}

/* ------------------------------------------------------------- placement */
static void resolve_clears(void) {
    int full_row[N] = { 0 }, full_col[N] = { 0 };
    int lines = 0;

    for (int y = 0; y < N; y++) {
        int full = 1;
        for (int x = 0; x < N; x++) if (B.cell[idx(x, y)] < 0) { full = 0; break; }
        if (full) { full_row[y] = 1; lines++; }
    }
    for (int x = 0; x < N; x++) {
        int full = 1;
        for (int y = 0; y < N; y++) if (B.cell[idx(x, y)] < 0) { full = 0; break; }
        if (full) { full_col[x] = 1; lines++; }
    }
    if (lines == 0) { B.combo = 0; return; }

    /*
     * Rows and columns are marked before any of them are emptied. Clearing as
     * each one is found would let an earlier clear un-fill a line that was
     * complete at the moment the piece landed, and a cross of two full lines
     * would only ever score one.
     */
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            if (!full_row[y] && !full_col[x]) continue;
            if (B.cell[idx(x, y)] >= 0) B.pop[idx(x, y)] = 1.0f;
            B.cell[idx(x, y)] = -1;
        }
    }

    B.combo++;
    int gain = lines * 100 * (lines > 1 ? lines : 1) * (B.combo > 1 ? B.combo : 1);
    B.score += gain;
    pa_sfx(lines > 1 ? "levelup" : "good");

    if (lines > 1) {
        char msg[24];
        snprintf(msg, sizeof(msg), "%d LINES", lines);
        banner(msg);
    } else if (B.combo > 1) {
        char msg[24];
        snprintf(msg, sizeof(msg), "COMBO %d", B.combo);
        banner(msg);
    }
}

static void place(int slot, int gx, int gy) {
    const Shape *s = &SHAPES[B.tray[slot].shape];
    for (int i = 0; i < s->count; i++) {
        B.cell[idx(gx + s->cx[i], gy + s->cy[i])] = B.tray[slot].tint;
    }
    B.score += s->count;
    B.placed++;
    B.tray[slot].shape = -1;
    pa_sfx("thud");

    resolve_clears();

    int empty = 1;
    for (int i = 0; i < TRAY; i++) if (B.tray[i].shape >= 0) empty = 0;
    if (empty) refill_tray();

    if (!tray_playable()) {
        B.over = 1;
        B.over_t = 0.0f;
        pa_sfx("lose");
        if (B.score > g_best) {
            g_best = B.score;
            pa_save_set("blockstorm.best", g_best);
            pa_save_flush();
        }
    }
}

/* ------------------------------------------------------------------ frame */
static void storm_start(void) {
    if (!g_best_loaded) { g_best = pa_save_get("blockstorm.best", 0); g_best_loaded = 1; }
    if (L.w == 0) compute_layout(540, 960);

    memset(&B, 0, sizeof(B));
    for (int i = 0; i < N * N; i++) B.cell[i] = -1;
    B.dragging = -1;
    pa_rng_seed(&B.rand, 0xB10Cu ^ ((uint32_t)(g_best * 2654435761u) + 0x9E3779B9u));
    refill_tray();

    /* Capture aid: PA_DEMO_BOARD prefills a mid-game board so a headless
       screenshot can be compared against a reference plate, which never shows
       an empty board. Inert in normal play - nothing sets the variable. */
    if (getenv("PA_DEMO_BOARD")) {
        PA_Rng r;
        pa_rng_seed(&r, 7);
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                if (pa_rng_chance(&r, 0.42f) && !(y == 4 && x < 5)) {
                    B.cell[idx(x, y)] = (x / 2 + y / 3) % TINT_COUNT;
                }
            }
        }
        B.score = 480;
    }
}

static void storm_stop(void) { }

/** Tray slot rectangle, resolved once so hit testing and drawing agree. */
static void tray_rect(int i, float *x, float *y, float *w) {
    /* Slots gather under the board rather than spreading across a wide window. */
    float span = (float)L.w - 36.0f;
    if (span > L.cell * N * 1.15f) span = L.cell * N * 1.15f;
    float slot_w = span / (float)TRAY;
    *x = ((float)L.w - span) * 0.5f + (float)i * slot_w;
    *y = L.tray_y;
    *w = slot_w;
}

/** Grid cell under a dragged piece, aligned so the piece's top-left lands
    where the cursor implies rather than where the finger literally is. */
static void drag_target(int slot, int *gx, int *gy) {
    const Shape *s = &SHAPES[B.tray[slot].shape];
    float px = B.drag_x - (float)s->w * L.cell * 0.5f;
    /* Lifted a row above the finger: on a phone the hand covers the drop site
       otherwise, and the player is placing blind. */
    float py = B.drag_y - (float)s->h * L.cell * 0.5f - L.cell * 0.9f;
    *gx = (int)floorf((px - L.ox) / L.cell + 0.5f);
    *gy = (int)floorf((py - L.oy) / L.cell + 0.5f);
}

static void storm_update(float dt, const PA_Input *in) {
    B.time += dt;
    if (B.banner_t > 0.0f) B.banner_t -= dt;
    for (int i = 0; i < N * N; i++) {
        if (B.pop[i] > 0.0f) B.pop[i] = B.pop[i] > dt * 3.0f ? B.pop[i] - dt * 3.0f : 0.0f;
    }

    if (B.over) {
        B.over_t += dt;
        if (B.over_t > 3.0f) storm_start();
        return;
    }

    if (in->pressed && B.dragging < 0) {
        for (int i = 0; i < TRAY; i++) {
            if (B.tray[i].shape < 0) continue;
            float x, y, w;
            tray_rect(i, &x, &y, &w);
            if (in->x >= x && in->x <= x + w && in->y >= y && in->y <= y + L.tray_cell * 5.0f) {
                B.dragging = i;
                pa_sfx("select");
                break;
            }
        }
    }

    if (B.dragging >= 0) {
        B.drag_x = in->x;
        B.drag_y = in->y;
        if (in->released) {
            int gx, gy;
            drag_target(B.dragging, &gx, &gy);
            if (fits(B.tray[B.dragging].shape, gx, gy)) place(B.dragging, gx, gy);
            else pa_sfx("bad");
            B.dragging = -1;
        }
    }
}

/* ---------------------------------------------------------------- drawing */
/**
 * One block, built the way the reference builds it: a flat face inside four
 * bevel trapezoids - lightest along the top, light on the left, darker on the
 * right, darkest along the bottom - with a thin dark outline and sharp corners.
 * The first version was rounded with a cap highlight, which reads as a
 * different game entirely; the four-way bevel is the format's signature.
 */
static void block(PA_Canvas *c, float x, float y, float size, PA_Color tint, float scale) {
    if (scale <= 0.02f) return;
    float inset = size * (1.0f - scale) * 0.5f;
    x += inset; y += inset; size *= scale;

    float x1 = x + 1.0f, y1 = y + 1.0f;
    float x2 = x + size - 1.0f, y2 = y + size - 1.0f;
    float b = size * 0.13f;

    pa_fill_rect(c, x, y, size, size, pa_shade(tint, -0.55f));

    PA_Vec2 top[4]    = { { x1, y1 }, { x2, y1 }, { x2 - b, y1 + b }, { x1 + b, y1 + b } };
    PA_Vec2 left[4]   = { { x1, y1 }, { x1 + b, y1 + b }, { x1 + b, y2 - b }, { x1, y2 } };
    PA_Vec2 right[4]  = { { x2, y1 }, { x2, y2 }, { x2 - b, y2 - b }, { x2 - b, y1 + b } };
    PA_Vec2 bottom[4] = { { x1, y2 }, { x1 + b, y2 - b }, { x2 - b, y2 - b }, { x2, y2 } };
    pa_fill_poly(c, top, 4, pa_shade(tint, 0.44f));
    pa_fill_poly(c, left, 4, pa_shade(tint, 0.16f));
    pa_fill_poly(c, right, 4, pa_shade(tint, -0.22f));
    pa_fill_poly(c, bottom, 4, pa_shade(tint, -0.38f));

    /* The face carries a faint top-to-bottom lift, as the reference's does. */
    PA_Paint face = pa_linear(0, y1 + b, 0, y2 - b);
    pa_stop(&face, 0.0f, pa_shade(tint, 0.10f));
    pa_stop(&face, 1.0f, tint);
    pa_fill_rect_paint(c, x1 + b, y1 + b, (x2 - x1) - b * 2.0f, (y2 - y1) - b * 2.0f, &face);
}

static void draw_shape(PA_Canvas *c, int shape, int tint, float x, float y,
                       float cell, float alpha) {
    const Shape *s = &SHAPES[shape];
    PA_Color col = pa_alpha(pa_hex(TINTS[tint]), alpha);
    for (int i = 0; i < s->count; i++) {
        block(c, x + (float)s->cx[i] * cell, y + (float)s->cy[i] * cell, cell, col, 1.0f);
    }
}

/* Measured from the reference plates: a flat medium blue that fills 62% of
   every screenshot, and a dark navy board inset into it. */
#define BG_BLUE    0x4860BC
#define BOARD_NAVY 0x252B53
#define GRID_LINE  0x323A68

static void draw_crown(PA_Canvas *c, float x, float y, float s) {
    PA_Vec2 crown[7] = {
        { x, y + s * 0.80f }, { x, y + s * 0.20f }, { x + s * 0.28f, y + s * 0.50f },
        { x + s * 0.50f, y }, { x + s * 0.72f, y + s * 0.50f }, { x + s, y + s * 0.20f },
        { x + s, y + s * 0.80f }
    };
    pa_fill_poly(c, crown, 7, pa_hex(0xF8C41C));
    pa_fill_rect(c, x, y + s * 0.80f, s, s * 0.16f, pa_hex(0xE0A410));
}

static void storm_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) compute_layout(c->w, c->h);

    /* The reference field lifts slightly toward the bottom of the screen. */
    PA_Paint bg = pa_linear(0, 0, 0, (float)c->h);
    pa_stop(&bg, 0.0f, pa_shade(pa_hex(BG_BLUE), -0.04f));
    pa_stop(&bg, 1.0f, pa_shade(pa_hex(BG_BLUE), 0.10f));
    pa_fill_rect_paint(c, 0, 0, (float)c->w, (float)c->h, &bg);

    /* Board: a thin darker frame, then navy, then faint grid lines. No per-cell
       rounded boxes - the reference's empty cells are just the navy. */
    float bw = L.cell * N;
    pa_fill_rect(c, L.ox - 6.0f, L.oy - 6.0f, bw + 12.0f, bw + 12.0f, pa_hex(0x3A4FA0));
    pa_fill_rect(c, L.ox, L.oy, bw, bw, pa_hex(BOARD_NAVY));
    for (int k = 1; k < N; k++) {
        pa_fill_rect(c, L.ox + (float)k * L.cell - 0.75f, L.oy, 1.5f, bw, pa_hex(GRID_LINE));
        pa_fill_rect(c, L.ox, L.oy + (float)k * L.cell - 0.75f, bw, 1.5f, pa_hex(GRID_LINE));
    }

    int ghost_x = -99, ghost_y = -99, ghost_ok = 0;
    if (B.dragging >= 0) {
        drag_target(B.dragging, &ghost_x, &ghost_y);
        ghost_ok = fits(B.tray[B.dragging].shape, ghost_x, ghost_y);
    }

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            float px = L.ox + (float)x * L.cell;
            float py = L.oy + (float)y * L.cell;
            int v = B.cell[idx(x, y)];
            if (v >= 0) block(c, px, py, L.cell, pa_hex(TINTS[v]), 1.0f);

            if (B.pop[idx(x, y)] > 0.0f) {
                /* Clearing blocks flash white and shrink out. */
                float t = B.pop[idx(x, y)];
                block(c, px, py, L.cell, PA_RGB(255, 255, 255), t);
            }
        }
    }

    /* Placement ghost, as the reference shows it: translucent pale cells with a
       light outline where the piece would land, only when it fits. */
    if (B.dragging >= 0 && ghost_ok) {
        const Shape *sh = &SHAPES[B.tray[B.dragging].shape];
        for (int i = 0; i < sh->count; i++) {
            int gx = ghost_x + sh->cx[i], gy = ghost_y + sh->cy[i];
            float px = L.ox + (float)gx * L.cell, py = L.oy + (float)gy * L.cell;
            pa_fill_rect(c, px + 1.0f, py + 1.0f, L.cell - 2.0f, L.cell - 2.0f,
                         PA_RGBA(255, 255, 255, 60));
            pa_stroke_rect(c, px + 1.5f, py + 1.5f, L.cell - 3.0f, L.cell - 3.0f, 2.0f,
                           PA_RGBA(255, 255, 255, 150));
        }
    }

    /* Tray: three pieces at reduced scale, no slot boxes. */
    for (int i = 0; i < TRAY; i++) {
        if (B.tray[i].shape < 0 || B.dragging == i) continue;
        float x, y, w;
        tray_rect(i, &x, &y, &w);
        const Shape *sh = &SHAPES[B.tray[i].shape];
        float sx = x + (w - (float)sh->w * L.tray_cell) * 0.5f;
        float sy = y + (L.tray_cell * 5.0f - (float)sh->h * L.tray_cell) * 0.5f;
        /* A spent-out tray piece that no longer fits anywhere is dimmed, so the
           player can see the end coming instead of discovering it. */
        int playable = any_fits(B.tray[i].shape);
        const Shape *s2 = sh;
        for (int k = 0; k < s2->count; k++) {
            PA_Color tint = pa_hex(TINTS[B.tray[i].tint]);
            if (!playable) tint = pa_mix(tint, pa_hex(0x5A6488), 0.7f);
            block(c, sx + (float)s2->cx[k] * L.tray_cell, sy + (float)s2->cy[k] * L.tray_cell,
                  L.tray_cell, tint, 1.0f);
        }
    }

    if (B.dragging >= 0) {
        const Shape *sh = &SHAPES[B.tray[B.dragging].shape];
        draw_shape(c, B.tray[B.dragging].shape, B.tray[B.dragging].tint,
                   B.drag_x - (float)sh->w * L.cell * 0.5f,
                   B.drag_y - (float)sh->h * L.cell * 0.5f - L.cell * 0.9f,
                   L.cell, 1.0f);
    }

    /* HUD in the reference's layout: crown and best in gold top-left, the live
       score large and centred, nothing else. Left of x=100 is the hub's MENU. */
    char buf[64];
    int best = g_best > B.score ? g_best : B.score;
    float hud_y = L.oy * 0.30f > 24.0f ? L.oy * 0.30f : 24.0f;
    draw_crown(c, 106.0f, hud_y, 22.0f);
    snprintf(buf, sizeof(buf), "%d", best);
    pa_text_bold(c, buf, 136.0f, hud_y + 2.0f, 18.0f, pa_hex(0xF8C41C), PA_RGB(22, 26, 60),
                 PA_ALIGN_LEFT, 2.0f, 1.6f);
    snprintf(buf, sizeof(buf), "%d", B.score);
    float score_y = L.oy - 76.0f < 44.0f ? 44.0f : L.oy - 76.0f;
    pa_text_bold(c, buf, (float)c->w * 0.5f, score_y, 44.0f, PA_RGB(255, 255, 255),
                 PA_RGB(38, 52, 130), PA_ALIGN_CENTER, 3.0f, 2.2f);

    if (B.banner_t > 0.0f) {
        float a = pa_clamp01(B.banner_t);
        float pop = 1.0f + (1.0f - pa_clamp01((1.4f - B.banner_t) * 6.0f)) * 0.25f;
        pa_text_bold(c, B.banner, (float)c->w * 0.5f, L.oy + L.cell * 3.4f, 30.0f * pop,
                     PA_RGBA(255, 214, 60, (int)(a * 255.0f)), PA_RGBA(80, 40, 0, (int)(a * 255.0f)),
                     PA_ALIGN_CENTER, 3.0f, 2.2f);
    }

    if (B.over) {
        float a = pa_clamp01(B.over_t * 2.2f);
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(20, 24, 60, (int)(a * 170.0f)));
        pa_text_bold(c, "NO MORE MOVES", (float)c->w * 0.5f, (float)c->h * 0.40f, 30.0f,
                     PA_RGB(255, 255, 255), PA_RGB(22, 26, 60), PA_ALIGN_CENTER, 3.0f, 2.2f);
        snprintf(buf, sizeof(buf), "%d", B.score);
        pa_text_bold(c, buf, (float)c->w * 0.5f, (float)c->h * 0.48f, 34.0f,
                     pa_hex(0xF8C41C), PA_RGB(22, 26, 60), PA_ALIGN_CENTER, 3.0f, 2.2f);
    }
}

static void storm_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    pa_fill_rect(c, x, y, w, h, pa_hex(0x18133A));
    int n = 6;
    float cell = (w < h ? w : h) / (float)n;
    float ox = x + (w - cell * (float)n) * 0.5f;
    float oy = y + (h - cell * (float)n) * 0.5f;

    /* A row filling and flashing out, so the tile shows the payoff. */
    float phase = pa_wrapf(t * 0.42f, 1.0f);
    int fill_row = 3;
    for (int gy = 0; gy < n; gy++) {
        for (int gx = 0; gx < n; gx++) {
            float px = ox + (float)gx * cell, py = oy + (float)gy * cell;
            int pattern = ((gx * 3 + gy * 5) % 7) < 3;
            if (gy == fill_row) {
                float lead = (float)gx / (float)n;
                if (phase > lead && phase < 0.82f) {
                    block(c, px, py, cell, pa_hex(TINTS[(gx + 2) % TINT_COUNT]), 1.0f);
                    continue;
                }
                if (phase >= 0.82f) {
                    pa_round_rect(c, px + 1.0f, py + 1.0f, cell - 2.0f, cell - 2.0f,
                                  cell * 0.2f,
                                  PA_RGBA(255, 255, 255, (int)((1.0f - phase) / 0.18f * 190.0f)));
                    continue;
                }
            } else if (pattern) {
                block(c, px, py, cell, pa_hex(TINTS[(gx + gy) % TINT_COUNT]), 1.0f);
                continue;
            }
            pa_round_rect(c, px + 1.0f, py + 1.0f, cell - 2.0f, cell - 2.0f, cell * 0.2f,
                          PA_RGBA(255, 255, 255, 16));
        }
    }
}

const PA_Game PA_GAME_BLOCKSTORM = {
    "blockstorm", "Block Storm", "Puzzle",
    "Eight by eight. Drop pieces, clear rows and columns, and never run out of room.",
    PA_RGB(93, 224, 255),
    storm_start, storm_stop, storm_update, storm_render, storm_thumb
};
