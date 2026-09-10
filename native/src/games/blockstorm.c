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

/* Block colours. Vivid and clearly distinct at a glance; the board is read as
   pattern, so two blocks a few points apart in hue would be one colour. */
static const uint32_t TINTS[] = {
    0x5DE0FF, 0xFF6B7A, 0xFFC93C, 0x7EF0A0, 0xB985FF, 0xFF9A4B, 0x4BA8FF
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
    float side = (float)w - 36.0f;
    float avail_h = (float)h - 300.0f;
    if (side > avail_h) side = avail_h;
    L.cell = side / (float)N;
    L.ox = ((float)w - L.cell * N) * 0.5f;
    L.oy = 132.0f;
    L.tray_y = L.oy + L.cell * N + 42.0f;
    L.tray_cell = L.cell * 0.62f;
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
}

static void storm_stop(void) { }

/** Tray slot rectangle, resolved once so hit testing and drawing agree. */
static void tray_rect(int i, float *x, float *y, float *w) {
    float slot_w = ((float)L.w - 36.0f) / (float)TRAY;
    *x = 18.0f + (float)i * slot_w;
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
/** One board block: a bevelled face with a lighter top edge and a dark base.
    Flat colour alone reads as a painted square, not as an object. */
static void block(PA_Canvas *c, float x, float y, float size, PA_Color tint, float scale) {
    if (scale <= 0.02f) return;
    float inset = size * (1.0f - scale) * 0.5f;
    x += inset; y += inset; size *= scale;

    float r = size * 0.22f;
    /* Drop, base, face, then a bright cap. The cap has to be strong: a subtle
       one leaves the block reading as a painted square rather than an object,
       which is the whole difference between this format looking finished and
       looking placeholder. */
    pa_round_rect(c, x + 1.0f, y + size * 0.12f, size - 2.0f, size - 2.0f, r,
                  PA_RGBA(8, 10, 22, 105));
    pa_round_rect(c, x + 1.0f, y + 1.0f, size - 2.0f, size - 2.0f, r, pa_shade(tint, -0.42f));
    pa_round_rect(c, x + 1.0f, y + 1.0f, size - 2.0f, size * 0.80f, r, tint);
    pa_round_rect(c, x + size * 0.13f, y + size * 0.10f, size * 0.74f, size * 0.30f,
                  r * 0.65f, pa_shade(tint, 0.46f));
    /* A hairline rim keeps adjacent blocks of the same colour from fusing into
       one shape, which is what makes a filled row unreadable. */
    PA_Vec2 rim[4] = {
        { x + 1.5f, y + 1.5f }, { x + size - 1.5f, y + 1.5f },
        { x + size - 1.5f, y + size - 1.5f }, { x + 1.5f, y + size - 1.5f }
    };
    pa_stroke_poly(c, rim, 4, 1, 1.2f, PA_RGBA(10, 8, 24, 80));
}

static void draw_shape(PA_Canvas *c, int shape, int tint, float x, float y,
                       float cell, float alpha) {
    const Shape *s = &SHAPES[shape];
    PA_Color col = pa_alpha(pa_hex(TINTS[tint]), alpha);
    for (int i = 0; i < s->count; i++) {
        block(c, x + (float)s->cx[i] * cell, y + (float)s->cy[i] * cell, cell, col, 1.0f);
    }
}

static void storm_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) compute_layout(c->w, c->h);

    PA_Paint bg = pa_linear(0, 0, 0, (float)c->h);
    pa_stop(&bg, 0.0f, pa_hex(0x241D4E));
    pa_stop(&bg, 0.55f, pa_hex(0x18133A));
    pa_stop(&bg, 1.0f, pa_hex(0x0E0B24));
    pa_fill_rect_paint(c, 0, 0, (float)c->w, (float)c->h, &bg);

    /* Board plate, so the grid reads as an object sitting on the background
       rather than as holes cut in it. */
    pa_round_rect(c, L.ox - 10.0f, L.oy - 10.0f, L.cell * N + 20.0f, L.cell * N + 20.0f,
                  16.0f, PA_RGBA(0, 0, 0, 70));
    pa_round_rect(c, L.ox - 8.0f, L.oy - 8.0f, L.cell * N + 16.0f, L.cell * N + 16.0f,
                  14.0f, pa_hex(0x2A2358));

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

            if (v < 0) {
                pa_round_rect(c, px + 2.0f, py + 2.0f, L.cell - 4.0f, L.cell - 4.0f,
                              L.cell * 0.20f, PA_RGBA(12, 8, 30, 105));
            } else {
                block(c, px, py, L.cell, pa_hex(TINTS[v]), 1.0f);
            }

            if (B.pop[idx(x, y)] > 0.0f) {
                /* Clearing blocks flash out rather than vanishing, so a line
                   clear is something that happened rather than something that
                   simply is. */
                float t = B.pop[idx(x, y)];
                pa_round_rect(c, px + 2.0f, py + 2.0f, L.cell - 4.0f, L.cell - 4.0f,
                              L.cell * 0.20f, PA_RGBA(255, 255, 255, (int)(t * 200.0f)));
            }
        }
    }

    /* Placement ghost. Green where it fits, red where it does not - guessing
       is the worst part of this format and it costs nothing to answer. */
    if (B.dragging >= 0) {
        const Shape *s = &SHAPES[B.tray[B.dragging].shape];
        for (int i = 0; i < s->count; i++) {
            int cx = ghost_x + s->cx[i], cy = ghost_y + s->cy[i];
            if (cx < 0 || cy < 0 || cx >= N || cy >= N) continue;
            pa_round_rect(c, L.ox + (float)cx * L.cell + 2.0f,
                          L.oy + (float)cy * L.cell + 2.0f,
                          L.cell - 4.0f, L.cell - 4.0f, L.cell * 0.20f,
                          ghost_ok ? PA_RGBA(126, 240, 160, 90) : PA_RGBA(255, 107, 122, 80));
        }
    }

    /* Tray. */
    for (int i = 0; i < TRAY; i++) {
        float x, y, w;
        tray_rect(i, &x, &y, &w);
        pa_round_rect(c, x + 4.0f, y - 6.0f, w - 8.0f, L.tray_cell * 5.0f + 12.0f, 12.0f,
                      PA_RGBA(255, 255, 255, 14));
        if (B.tray[i].shape < 0 || B.dragging == i) continue;
        const Shape *s = &SHAPES[B.tray[i].shape];
        float sx = x + (w - (float)s->w * L.tray_cell) * 0.5f;
        float sy = y + (L.tray_cell * 5.0f - (float)s->h * L.tray_cell) * 0.5f;
        draw_shape(c, B.tray[i].shape, B.tray[i].tint, sx, sy, L.tray_cell, 1.0f);
    }

    /* The dragged piece rides above the finger at full board scale, so what is
       being placed matches what is previewed. */
    if (B.dragging >= 0) {
        const Shape *s = &SHAPES[B.tray[B.dragging].shape];
        draw_shape(c, B.tray[B.dragging].shape, B.tray[B.dragging].tint,
                   B.drag_x - (float)s->w * L.cell * 0.5f,
                   B.drag_y - (float)s->h * L.cell * 0.5f - L.cell * 0.9f,
                   L.cell, 0.92f);
    }

    pa_vignette(c, 0.45f);
    pa_hud_scrim(c, 108.0f);

    char buf[64];
    snprintf(buf, sizeof(buf), "%d", B.score);
    pa_text(c, buf, 104.0f, 34.0f, 22.0f, PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 2.0f);
    snprintf(buf, sizeof(buf), "BEST %d", g_best > B.score ? g_best : B.score);
    pa_text(c, buf, (float)c->w - 20.0f, 34.0f, 14.0f,
            PA_RGBA(255, 255, 255, 180), PA_ALIGN_RIGHT, 2.0f);

    if (B.placed < 3) {
        pa_text(c, "DRAG A PIECE ONTO THE BOARD", (float)c->w * 0.5f, 80.0f, 11.0f,
                PA_RGBA(255, 255, 255, 150), PA_ALIGN_CENTER, 3.0f);
    }

    if (B.banner_t > 0.0f) {
        pa_text(c, B.banner, (float)c->w * 0.5f, (float)c->h * 0.30f, 26.0f,
                PA_RGBA(255, 201, 60, (int)(pa_clamp01(B.banner_t) * 235.0f)),
                PA_ALIGN_CENTER, 5.0f);
    }

    if (B.over) {
        float a = pa_clamp01(B.over_t * 2.2f);
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(10, 8, 24, (int)(a * 180.0f)));
        pa_text(c, "NO ROOM LEFT", (float)c->w * 0.5f, (float)c->h * 0.42f, 30.0f,
                PA_RGB(255, 107, 122), PA_ALIGN_CENTER, 5.0f);
        snprintf(buf, sizeof(buf), "%d - BEST %d", B.score, g_best);
        pa_text(c, buf, (float)c->w * 0.5f, (float)c->h * 0.50f, 14.0f,
                PA_RGBA(255, 255, 255, 185), PA_ALIGN_CENTER, 3.0f);
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
