/* ===========================================================================
   ROLLER SPLAT - native port
   Swipe a direction, the ball rolls until it hits a wall, painting everything
   it crosses. Cover every tile to clear the level.

   First game through the native engine, and chosen for that reason: it exercises
   rounded rectangles, gradients, strokes and text without needing a physics
   solver, so a rendering fault shows up as a rendering fault.
   =========================================================================== */
#include "../pa.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

#define MAX_W 16
#define MAX_H 16

/* '#' wall, '.' floor, 'o' ball start. Hand built so each has one tidy path. */
static const char *LEVELS[][MAX_H] = {
    { "#######",
      "#o....#",
      "#.###.#",
      "#.....#",
      "#.###.#",
      "#.....#",
      "#######", NULL },

    { "#########",
      "#o......#",
      "#.#####.#",
      "#.#...#.#",
      "#.#.#.#.#",
      "#...#...#",
      "#########", NULL },

    { "#########",
      "#.......#",
      "#.#####.#",
      "#o#...#.#",
      "#.#.#.#.#",
      "#...#...#",
      "#.#####.#",
      "#.......#",
      "#########", NULL },

    { "##########",
      "#o.......#",
      "#.##..##.#",
      "#.#....#.#",
      "#....##..#",
      "#.##....##",
      "#....##..#",
      "#.######.#",
      "#........#",
      "##########", NULL },

    { "###########",
      "#o........#",
      "#.###.###.#",
      "#...#...#.#",
      "###.###.#.#",
      "#.......#.#",
      "#.#####.#.#",
      "#.#...#...#",
      "#.#.#.#####",
      "#...#.....#",
      "###########", NULL },

    { "############",
      "#o.........#",
      "#.####.###.#",
      "#.#......#.#",
      "#.#.####.#.#",
      "#.#.#..#.#.#",
      "#.#.#..#.#.#",
      "#.#.####.#.#",
      "#.#......#.#",
      "#.########.#",
      "#..........#",
      "############", NULL }
};
#define LEVEL_COUNT ((int)(sizeof(LEVELS) / sizeof(LEVELS[0])))

typedef struct {
    int   level;
    int   w, h;
    char  grid[MAX_H][MAX_W + 1];
    int   painted[MAX_H][MAX_W];
    int   total, done, moves;

    float bx, by;          /* drawn position, tiles */
    int   tx, ty;          /* target tile */
    int   rolling;
    float roll_t;
    float from_x, from_y;

    int   cleared;
    float clear_t;
} Splat;

static Splat S;
static int   g_score;

static void load_level(int index) {
    const char **rows = LEVELS[index % LEVEL_COUNT];
    memset(&S.grid, 0, sizeof(S.grid));
    memset(&S.painted, 0, sizeof(S.painted));

    S.level = index;
    S.h = 0;
    S.w = 0;
    for (int y = 0; y < MAX_H && rows[y]; y++) {
        int len = (int)strlen(rows[y]);
        if (len > MAX_W) len = MAX_W;
        memcpy(S.grid[y], rows[y], (size_t)len);
        S.grid[y][len] = 0;
        if (len > S.w) S.w = len;
        S.h++;
    }

    S.total = 0;
    S.done = 0;
    S.moves = 0;
    S.rolling = 0;
    S.roll_t = 0.0f;
    S.cleared = 0;
    S.clear_t = 0.0f;

    for (int y = 0; y < S.h; y++) {
        for (int x = 0; x < S.w; x++) {
            char c = S.grid[y][x];
            if (c == '#') continue;
            S.total++;
            if (c == 'o') {
                S.tx = x; S.ty = y;
                S.bx = (float)x; S.by = (float)y;
            }
        }
    }
    /* The starting tile counts as painted, or the target can never be reached. */
    S.painted[S.ty][S.tx] = 1;
    S.done = 1;
}

static int walkable(int x, int y) {
    if (x < 0 || y < 0 || x >= S.w || y >= S.h) return 0;
    return S.grid[y][x] != '#' && S.grid[y][x] != 0;
}

static void try_roll(int dx, int dy) {
    if (S.rolling || S.cleared) return;

    int x = S.tx, y = S.ty;
    int steps = 0;
    while (walkable(x + dx, y + dy)) {
        x += dx; y += dy;
        if (!S.painted[y][x]) { S.painted[y][x] = 1; S.done++; }
        steps++;
    }
    if (steps == 0) return;

    S.from_x = (float)S.tx;
    S.from_y = (float)S.ty;
    S.tx = x;
    S.ty = y;
    S.rolling = 1;
    /* Roll time scales with distance so long runs read as fast, not teleporting. */
    S.roll_t = 0.0f;
    S.moves++;
    pa_sfx("hop");
}

static void splat_start(void) {
    /* Resume at the furthest level reached rather than restarting the whole
       campaign every launch. */
    load_level(pa_save_get("splat.level", 0));
    g_score = 0;
}

static void splat_stop(void) { }

static void splat_update(float dt, const PA_Input *in) {
    if (S.cleared) {
        S.clear_t += dt;
        if (S.clear_t > 1.1f) {
            g_score += 500 + S.total * 10;
            int next = S.level + 1;
            if (next > pa_save_get("splat.level", 0)) {
                pa_save_set("splat.level", next);
                pa_save_flush();
            }
            load_level(next);
        }
        return;
    }

    if (S.rolling) {
        float dist = fabsf((float)S.tx - S.from_x) + fabsf((float)S.ty - S.from_y);
        float speed = 11.0f / (dist < 1.0f ? 1.0f : dist);
        S.roll_t += dt * speed * (dist < 1.0f ? 1.0f : dist) / (dist < 1.0f ? 1.0f : dist);
        S.roll_t += dt * 4.2f;
        if (S.roll_t >= 1.0f) {
            S.roll_t = 1.0f;
            S.rolling = 0;
            S.bx = (float)S.tx;
            S.by = (float)S.ty;
            if (S.done >= S.total) {
                S.cleared = 1;
                S.clear_t = 0.0f;
                pa_sfx("win");
            }
        } else {
            float t = pa_smooth(S.roll_t);
            S.bx = pa_lerpf(S.from_x, (float)S.tx, t);
            S.by = pa_lerpf(S.from_y, (float)S.ty, t);
        }
        return;
    }

    if (in->swipe == PA_SWIPE_LEFT  || in->key_pressed[PA_KEY_LEFT])  try_roll(-1, 0);
    else if (in->swipe == PA_SWIPE_RIGHT || in->key_pressed[PA_KEY_RIGHT]) try_roll(1, 0);
    else if (in->swipe == PA_SWIPE_UP    || in->key_pressed[PA_KEY_UP])    try_roll(0, -1);
    else if (in->swipe == PA_SWIPE_DOWN  || in->key_pressed[PA_KEY_DOWN])  try_roll(0, 1);
}

static const PA_Color PAINT = PA_RGB(93, 224, 255);

static void splat_render(PA_Canvas *c) {
    pa_clear(c, pa_hex(0x15102C));

    float pad = 20.0f;
    float cell = (float)(c->w - pad * 2) / (float)S.w;
    float ch = (float)(c->h - 190.0f) / (float)S.h;
    if (ch < cell) cell = ch;
    float ox = ((float)c->w - cell * (float)S.w) * 0.5f;
    float oy = 128.0f + ((float)c->h - 190.0f - cell * (float)S.h) * 0.5f;

    for (int y = 0; y < S.h; y++) {
        for (int x = 0; x < S.w; x++) {
            char g = S.grid[y][x];
            if (g == 0) continue;
            float px = ox + (float)x * cell;
            float py = oy + (float)y * cell;
            if (g == '#') {
                /* Walls need to read as solid at a glance. The first pass used a
                   dark violet only a few points off the unpainted floor, and the
                   maze simply disappeared. */
                pa_round_rect(c, px + 1, py + 1, cell - 2, cell - 2, cell * 0.18f,
                              pa_hex(0x4A3F86));
                pa_round_rect(c, px + 3, py + 3, cell - 6, cell * 0.34f, cell * 0.14f,
                              PA_RGBA(255, 255, 255, 26));
            } else if (S.painted[y][x]) {
                pa_round_rect(c, px + 1, py + 1, cell - 2, cell - 2, cell * 0.22f, PAINT);
            } else {
                pa_round_rect(c, px + 1, py + 1, cell - 2, cell - 2, cell * 0.22f,
                              pa_hex(0x241C4A));
            }
        }
    }

    float bx = ox + (S.bx + 0.5f) * cell;
    float by = oy + (S.by + 0.5f) * cell;
    float r = cell * 0.34f;

    pa_fill_ellipse(c, bx, by + r * 0.55f, r * 0.9f, r * 0.35f, PA_RGBA(0, 0, 0, 90));

    PA_Paint ball = pa_radial(bx - r * 0.35f, by - r * 0.4f, r * 0.1f, r * 1.2f);
    pa_stop(&ball, 0.0f, pa_shade(PAINT, 0.55f));
    pa_stop(&ball, 0.6f, PAINT);
    pa_stop(&ball, 1.0f, pa_shade(PAINT, -0.4f));
    pa_fill_ellipse_paint(c, bx, by, r, r, &ball);
    pa_stroke_circle(c, bx, by, r, 2.5f, PA_RGBA(15, 11, 28, 165));

    /* HUD */
    char buf[64];
    snprintf(buf, sizeof(buf), "LEVEL %d", S.level + 1);
    pa_text(c, buf, 104.0f, 34.0f, 18.0f, PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 2.0f);

    snprintf(buf, sizeof(buf), "%d / %d PAINTED", S.done, S.total);
    pa_text(c, buf, (float)c->w * 0.5f, 84.0f, 14.0f,
            PA_RGBA(255, 255, 255, 170), PA_ALIGN_CENTER, 3.0f);

    snprintf(buf, sizeof(buf), "%d MOVES", S.moves);
    pa_text(c, buf, (float)c->w - 20.0f, 34.0f, 18.0f,
            PA_RGB(255, 201, 60), PA_ALIGN_RIGHT, 2.0f);

    float meter_w = (float)c->w - 48.0f;
    pa_round_rect(c, 24.0f, 100.0f, meter_w, 8.0f, 4.0f, PA_RGBA(255, 255, 255, 26));
    float fill = meter_w * ((float)S.done / (float)(S.total > 0 ? S.total : 1));
    if (fill > 2.0f) pa_round_rect(c, 24.0f, 100.0f, fill, 8.0f, 4.0f, PAINT);

    if (S.moves == 0) {
        pa_text(c, "SWIPE TO ROLL", (float)c->w * 0.5f, (float)c->h - 60.0f, 15.0f,
                PA_RGBA(255, 255, 255, 140), PA_ALIGN_CENTER, 5.0f);
    }

    if (S.cleared) {
        float a = pa_clamp01(S.clear_t * 3.0f);
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h,
                     PA_RGBA(10, 8, 24, (int)(a * 170.0f)));
        pa_text(c, "CLEARED", (float)c->w * 0.5f, (float)c->h * 0.44f, 44.0f,
                PA_RGB(126, 240, 160), PA_ALIGN_CENTER, 6.0f);
    }
}

static void splat_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    pa_fill_rect(c, x, y, w, h, pa_hex(0x15102C));
    int n = 6;
    float cell = (w < h ? w : h) / (float)n;
    float ox = x + (w - cell * n) * 0.5f;
    float oy = y + (h - cell * n) * 0.5f;
    float progress = pa_wrapf(t * 0.35f, 1.0f);

    for (int gy = 0; gy < n; gy++) {
        for (int gx = 0; gx < n; gx++) {
            int wall = (gx == 2 && gy > 0 && gy < 4) || (gx == 4 && gy < 3);
            float px = ox + (float)gx * cell + 1.0f;
            float py = oy + (float)gy * cell + 1.0f;
            float order = (float)(gy * n + gx) / (float)(n * n);
            PA_Color col = wall ? pa_hex(0x2C2450)
                                : (order < progress ? PAINT : PA_RGBA(255, 255, 255, 16));
            pa_round_rect(c, px, py, cell - 2.0f, cell - 2.0f, 3.0f, col);
        }
    }
    int bi = (int)(progress * (float)(n * n));
    pa_fill_circle(c, ox + ((float)(bi % n) + 0.5f) * cell,
                      oy + ((float)(bi / n) + 0.5f) * cell, cell * 0.34f, pa_hex(0xBFF3FF));
}

const PA_Game PA_GAME_SPLAT = {
    "splat", "Roller Splat", "Puzzle",
    "Swipe the ball, it rolls until it hits a wall. Paint every tile in the maze.",
    PA_RGB(93, 224, 255),
    splat_start, splat_stop, splat_update, splat_render, splat_thumb
};
