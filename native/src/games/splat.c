/* ===========================================================================
   ROLLER SPLAT - native port
   Swipe a direction, the ball rolls until it hits a wall, painting everything
   it crosses. Cover every tile to clear the level.

   Drawn the way the reference plates show it: one solid, vivid backdrop per
   level with a few soft decorations, and the maze as a raised white slab tilted
   away from the camera, its corridors sunk into it. Walls are real boxes under
   a small perspective projection, so the corridor walls facing the camera show
   their sides, which is most of what makes the reference read as 3D.
   =========================================================================== */
#include "../pa.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_W 16
#define MAX_H 18

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
    uint32_t bg, deco, paint;
    int      deco_kind;            /* 0 swooshes, 1 logs, 2 bushes */
} Theme;

/* Backdrops and paints measured off the plates: periwinkle with cyan swooshes,
   mustard yellow with logs, saturated green with bushes. Paint always contrasts
   hard with the backdrop and with the white slab. */
static const Theme THEMES[] = {
    { 0x6B9CFF, 0x4AD6FF, 0x7B22F2, 0 },
    { 0xEBDF52, 0xF4A64A, 0xF2287E, 1 },
    { 0x22DD3A, 0x14A82A, 0xFF9A1A, 2 },
    { 0xFFB0C8, 0xFFD6E2, 0x2A6CF5, 0 },
    { 0x58D0F5, 0x2FA7E0, 0xF23A3A, 1 },
};
#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))

#define CONFETTI 90

typedef struct { float x, y, vx, vy, rot, vr; PA_Color col; } Bit;

typedef struct {
    int   level;
    int   w, h;
    char  grid[MAX_H][MAX_W + 1];
    unsigned char painted[MAX_H][MAX_W];   /* 0 bare, 1 painted, 2 painted once the ball arrives */
    int   total, done, moves;

    float bx, by;          /* drawn position, tiles */
    int   tx, ty;          /* target tile */
    int   dx, dy;
    int   rolling;
    float roll_t, roll_len;
    float from_x, from_y;

    int   cleared;
    float clear_t;
    float time;
    Bit   bits[CONFETTI];
} Splat;

static Splat S;
static int   g_score;

static int walkable(int x, int y) {
    if (x < 0 || y < 0 || x >= S.w || y >= S.h) return 0;
    return S.grid[y][x] != '#' && S.grid[y][x] != 0;
}

/* ------------------------------------------------------------ generation */
/*
 * Past the hand-built set, levels are carved at random and kept only if they
 * are fair: every floor tile must lie on some roll reachable from the start,
 * and every reachable resting spot must be able to get back to the start. The
 * second condition matters - without it a level can have a one-way pocket, and
 * a player who rolls into it early is stuck with tiles they can never reach.
 */
static int roll_end(int x, int y, int dx, int dy, int *ex, int *ey) {
    int n = 0;
    while (walkable(x + dx, y + dy)) { x += dx; y += dy; n++; }
    *ex = x; *ey = y;
    return n;
}

static int level_is_fair(int sx, int sy) {
    static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
    unsigned char seen[MAX_H][MAX_W], covered[MAX_H][MAX_W];
    int qx[MAX_W * MAX_H], qy[MAX_W * MAX_H], head = 0, tail = 0;
    memset(seen, 0, sizeof(seen));
    memset(covered, 0, sizeof(covered));
    seen[sy][sx] = 1; covered[sy][sx] = 1;
    qx[tail] = sx; qy[tail] = sy; tail++;
    while (head < tail) {
        int x = qx[head], y = qy[head]; head++;
        for (int d = 0; d < 4; d++) {
            int ex, ey;
            if (!roll_end(x, y, DX[d], DY[d], &ex, &ey)) continue;
            for (int cx = x, cy = y; cx != ex || cy != ey;) {
                cx += DX[d]; cy += DY[d]; covered[cy][cx] = 1;
            }
            if (!seen[ey][ex]) { seen[ey][ex] = 1; qx[tail] = ex; qy[tail] = ey; tail++; }
        }
    }
    for (int y = 0; y < S.h; y++)
        for (int x = 0; x < S.w; x++)
            if (walkable(x, y) && !covered[y][x]) return 0;

    /* Every resting spot must reach the start again. */
    for (int i = 0; i < tail; i++) {
        unsigned char back[MAX_H][MAX_W];
        int bx[MAX_W * MAX_H], by[MAX_W * MAX_H], h2 = 0, t2 = 0, ok = 0;
        memset(back, 0, sizeof(back));
        back[qy[i]][qx[i]] = 1; bx[t2] = qx[i]; by[t2] = qy[i]; t2++;
        while (h2 < t2 && !ok) {
            int x = bx[h2], y = by[h2]; h2++;
            if (x == sx && y == sy) { ok = 1; break; }
            for (int d = 0; d < 4; d++) {
                int ex, ey;
                if (!roll_end(x, y, DX[d], DY[d], &ex, &ey)) continue;
                if (!back[ey][ex]) { back[ey][ex] = 1; bx[t2] = ex; by[t2] = ey; t2++; }
            }
        }
        if (!ok) return 0;
    }
    return 1;
}

static int generate_level(int index, int *sx, int *sy) {
    int size = index / 5;
    int w = 9 + 2 * (size < 3 ? size : 3);
    int h = w + 4 > MAX_H ? MAX_H : w + 4;
    if (w > MAX_W) w = MAX_W;
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)index * 7919u + 13u);

    for (int attempt = 0; attempt < 400; attempt++) {
        S.w = w; S.h = h;
        for (int y = 0; y < h; y++) { memset(S.grid[y], '#', (size_t)w); S.grid[y][w] = 0; }
        /* A depth-first maze grown over the odd lattice keeps every corridor
           one tile wide with single-tile walls, the way the plates' mazes are
           built. It is stopped part-grown - a complete maze is almost never
           fair to roll - and given a few loops for variety. Free random
           carving was tried first: it opened rooms and nearly every result
           was rejected for being too small. */
        int nx = (w - 1) / 2, ny = (h - 1) / 2;
        int target = (int)((float)(nx * ny) * pa_rng_range(&r, 0.52f, 0.68f));
        int stx[MAX_W * MAX_H], sty[MAX_W * MAX_H], top = 0, grown = 1;
        int x = 1 + 2 * pa_rng_int(&r, 0, nx - 1);
        int y = 1 + 2 * pa_rng_int(&r, 0, ny - 1);
        *sx = x; *sy = y;
        S.grid[y][x] = '.';
        stx[top] = x; sty[top] = y; top++;
        while (top > 0 && grown < target) {
            x = stx[top - 1]; y = sty[top - 1];
            int opts[4], no = 0;
            static const int OX[4] = { 2, -2, 0, 0 }, OY[4] = { 0, 0, 2, -2 };
            for (int d = 0; d < 4; d++) {
                int ex = x + OX[d], ey = y + OY[d];
                if (ex >= 1 && ey >= 1 && ex <= w - 2 && ey <= h - 2 && S.grid[ey][ex] == '#')
                    opts[no++] = d;
            }
            if (!no) { top--; continue; }
            int d = opts[pa_rng_int(&r, 0, no - 1)];
            S.grid[y + OY[d] / 2][x + OX[d] / 2] = '.';
            S.grid[y + OY[d]][x + OX[d]] = '.';
            stx[top] = x + OX[d]; sty[top] = y + OY[d]; top++;
            grown++;
        }
        int loops = pa_rng_int(&r, 0, 4);
        for (int k = 0; k < loops * 8 && loops > 0; k++) {
            int lx = pa_rng_int(&r, 1, w - 2), ly = pa_rng_int(&r, 1, h - 2);
            if (S.grid[ly][lx] != '#') continue;
            int horiz = (lx % 2 == 0 && ly % 2 == 1 && S.grid[ly][lx - 1] == '.' && S.grid[ly][lx + 1] == '.');
            int vert = (lx % 2 == 1 && ly % 2 == 0 && S.grid[ly - 1][lx] == '.' && S.grid[ly + 1][lx] == '.');
            if (horiz || vert) { S.grid[ly][lx] = '.'; loops--; }
        }
        if (!level_is_fair(*sx, *sy)) continue;

        /* Crop to the carved area plus its wall border: the plates' slabs
           hug the maze, and a part-grown maze leaves dead white margins. */
        int x0 = w, y0 = h, x1 = 0, y1 = 0;
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++)
                if (S.grid[yy][xx] == '.') {
                    if (xx < x0) x0 = xx;
                    if (xx > x1) x1 = xx;
                    if (yy < y0) y0 = yy;
                    if (yy > y1) y1 = yy;
                }
        x0--; y0--; x1++; y1++;
        int cw = x1 - x0 + 1, chh = y1 - y0 + 1;
        for (int yy = 0; yy < chh; yy++) {
            memmove(S.grid[yy], S.grid[yy + y0] + x0, (size_t)cw);
            S.grid[yy][cw] = 0;
        }
        for (int yy = chh; yy < MAX_H; yy++) S.grid[yy][0] = 0;
        S.w = cw; S.h = chh;
        *sx -= x0; *sy -= y0;
        S.grid[*sy][*sx] = 'o';
        return 1;
    }
    return 0;
}

static void load_level(int index) {
    memset(&S.grid, 0, sizeof(S.grid));
    memset(&S.painted, 0, sizeof(S.painted));
    S.level = index;

    int sx = -1, sy = -1;
    if (index >= LEVEL_COUNT && generate_level(index, &sx, &sy)) {
        /* grid already filled in */
    } else {
        const char **rows = LEVELS[index % LEVEL_COUNT];
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
            if (c == '#' || c == 0) continue;
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

static void try_roll(int dx, int dy) {
    if (S.rolling || S.cleared) return;

    int x = S.tx, y = S.ty;
    int steps = 0;
    while (walkable(x + dx, y + dy)) {
        x += dx; y += dy;
        /* Counted now so the clear is decided by the move, but only shown as
           painted once the ball actually rolls over it. */
        if (!S.painted[y][x]) { S.painted[y][x] = 2; S.done++; }
        steps++;
    }
    if (steps == 0) return;

    S.from_x = (float)S.tx;
    S.from_y = (float)S.ty;
    S.tx = x;
    S.ty = y;
    S.dx = dx; S.dy = dy;
    S.rolling = 1;
    S.roll_t = 0.0f;
    S.roll_len = (float)steps;
    S.moves++;
    pa_sfx("hop");
}

static void splat_start(void) {
    /* Resume at the furthest level reached rather than restarting the whole
       campaign every launch. */
    load_level(pa_save_get("splat.level", 0));
    g_score = 0;

    /* Capture hook: pre-paint everything except one run out of the start
       (right, else left, else up - the directions headless --auto can swipe),
       so a single swipe clears the level and a capture can show the
       completion screen. */
    if (getenv("PA_DEMO_CLEAR")) {
        static const int DX[3] = { 1, -1, 0 }, DY[3] = { 0, 0, -1 };
        int d = 0, ex = S.tx, ey = S.ty;
        for (; d < 3; d++) if (roll_end(S.tx, S.ty, DX[d], DY[d], &ex, &ey)) break;
        for (int y = 0; y < S.h; y++)
            for (int x = 0; x < S.w; x++)
                if (walkable(x, y)) S.painted[y][x] = 1;
        S.done = S.total;
        for (int x = S.tx, y = S.ty; d < 3 && (x != ex || y != ey);) {
            x += DX[d]; y += DY[d];
            S.painted[y][x] = 0;
            S.done--;
        }
    }
}

static void splat_stop(void) { }

static void burst_confetti(void) {
    static const uint32_t COLS[] = { 0xF2287E, 0xFFC21C, 0x2A6CF5, 0x22DD3A, 0xFF7A1A, 0x9B3CF2 };
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)S.level * 31u + 5u);
    for (int i = 0; i < CONFETTI; i++) {
        Bit *b = &S.bits[i];
        b->x = pa_rng_range(&r, 0.1f, 0.9f);
        b->y = pa_rng_range(&r, -0.15f, 0.25f);
        b->vx = pa_rng_range(&r, -0.35f, 0.35f);
        b->vy = pa_rng_range(&r, -0.6f, 0.1f);
        b->rot = pa_rng_range(&r, 0.0f, PA_TAU);
        b->vr = pa_rng_range(&r, -9.0f, 9.0f);
        b->col = pa_hex(COLS[i % 6]);
    }
}

static void splat_update(float dt, const PA_Input *in) {
    S.time += dt;
    if (S.cleared) {
        S.clear_t += dt;
        for (int i = 0; i < CONFETTI; i++) {
            Bit *b = &S.bits[i];
            b->vy += 1.1f * dt;
            b->vx *= expf(-1.2f * dt);
            b->x += b->vx * dt;
            b->y += b->vy * dt;
            b->rot += b->vr * dt;
        }
        if (S.clear_t > 1.8f) {
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
        /* Constant speed per tile, as the reference rolls: a long run takes
           longer than a short one instead of every move lasting the same. */
        S.roll_t += dt * 26.0f / (S.roll_len + 1.5f);
        float t = S.roll_t >= 1.0f ? 1.0f : S.roll_t;
        float e = t * t * (3.0f - 2.0f * t) * 0.35f + t * 0.65f;
        S.bx = pa_lerpf(S.from_x, (float)S.tx, e);
        S.by = pa_lerpf(S.from_y, (float)S.ty, e);

        /* Paint every tile the ball has reached so far. */
        int reached = (int)floorf(e * S.roll_len + 0.5f);
        for (int k = 1; k <= reached; k++) {
            int x = (int)S.from_x + S.dx * k, y = (int)S.from_y + S.dy * k;
            if (S.painted[y][x] == 2) S.painted[y][x] = 1;
        }

        if (S.roll_t >= 1.0f) {
            S.rolling = 0;
            S.bx = (float)S.tx;
            S.by = (float)S.ty;
            pa_sfx("pop");
            if (S.done >= S.total) {
                S.cleared = 1;
                S.clear_t = 0.0f;
                burst_confetti();
                pa_sfx("win");
            }
        }
        return;
    }

    if (in->swipe == PA_SWIPE_LEFT  || in->key_pressed[PA_KEY_LEFT])  try_roll(-1, 0);
    else if (in->swipe == PA_SWIPE_RIGHT || in->key_pressed[PA_KEY_RIGHT]) try_roll(1, 0);
    else if (in->swipe == PA_SWIPE_UP    || in->key_pressed[PA_KEY_UP])    try_roll(0, -1);
    else if (in->swipe == PA_SWIPE_DOWN  || in->key_pressed[PA_KEY_DOWN])  try_roll(0, 1);
}

/* --------------------------------------------------------------- drawing */
/*
 * A small perspective camera. Slab coordinates are (u, v) in tiles from the
 * maze centre and a height hgt in tiles; the slab leans away at the top by
 * TILT, so far rows are narrower and shorter, as in the plates.
 */
#define TILT      0.40f            /* radians, ~23 degrees */
#define WALL_H    0.55f
#define SLAB_T    0.40f            /* slab thickness under the floor */

static struct { float cx, cy, k, D; } P;

static PA_Vec2 proj(float u, float v, float hgt) {
    float st = sinf(TILT), ct = cosf(TILT);
    float d = P.D - v * st - hgt * ct;
    PA_Vec2 o;
    o.x = P.cx + u * P.k / d;
    o.y = P.cy + (v * ct - hgt * st) * P.k / d;
    return o;
}

static void quad(PA_Canvas *c, PA_Vec2 a, PA_Vec2 b, PA_Vec2 d, PA_Vec2 e, PA_Color col) {
    PA_Vec2 q[4] = { a, b, d, e };
    pa_fill_poly(c, q, 4, col);
}

/** Fit the projected slab into the space under the HUD. */
static void fit_camera(PA_Canvas *c) {
    float hw = (float)S.w * 0.5f, hh = (float)S.h * 0.5f;
    P.D = (float)(S.w > S.h ? S.w : S.h) * 4.2f;
    P.cx = 0.0f; P.cy = 0.0f; P.k = 1.0f;
    PA_Vec2 tl = proj(-hw, -hh, WALL_H), br = proj(hw, hh, -SLAB_T);
    PA_Vec2 bl = proj(hw, hh, WALL_H);
    float bw = bl.x * 2.0f, bh = br.y - tl.y;
    float top = 150.0f, bottom = (float)c->h - 70.0f;
    float avail_w = (float)c->w * 0.86f, avail_h = bottom - top;
    float s = avail_w / bw;
    if (bh * s > avail_h) s = avail_h / bh;
    P.k = s;
    P.cx = (float)c->w * 0.5f;
    P.cy = top + (avail_h - bh * s) * 0.5f - tl.y * s;
}

static void decorations(PA_Canvas *c, const Theme *th) {
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)S.level * 977u + 3u);
    PA_Color col = pa_hex(th->deco);
    for (int i = 0; i < 5; i++) {
        /* Keep them to the margins, where the plates put them. */
        float x = pa_rng_range(&r, -0.05f, 1.05f) * (float)c->w;
        float y = (i & 1) ? pa_rng_range(&r, 0.02f, 0.20f) : pa_rng_range(&r, 0.80f, 1.0f);
        y *= (float)c->h;
        float s = (float)c->w * pa_rng_range(&r, 0.09f, 0.14f);
        float bob = sinf(S.time * 0.8f + (float)i) * 3.0f;
        if (th->deco_kind == 0) {
            /* Swoosh: a thick curved stroke. */
            /* Swoosh: a fat crescent, shaded underneath and lit on top so
               it reads as a soft 3D shape like the plates' ones. */
            PA_Vec2 pts[11], hi[11];
            float a0 = pa_rng_range(&r, 0.0f, PA_TAU);
            for (int k = 0; k < 11; k++) {
                float a = a0 + (float)k * 0.20f;
                pts[k].x = x + cosf(a) * s * 1.5f;
                pts[k].y = y + bob + sinf(a) * s * 0.8f;
                hi[k].x = pts[k].x - s * 0.08f;
                hi[k].y = pts[k].y - s * 0.16f;
            }
            pa_stroke_poly(c, pts, 11, 0, s * 0.95f, pa_shade(col, -0.18f));
            pa_stroke_poly(c, hi, 11, 0, s * 0.62f, col);
        } else if (th->deco_kind == 1) {
            /* Log: a rounded cylinder lying at an angle. */
            float a = pa_rng_range(&r, -0.4f, 0.1f);
            float lx = cosf(a) * s * 1.4f, ly = sinf(a) * s * 1.4f;
            pa_line(c, x - lx, y + bob - ly, x + lx, y + bob + ly, s * 0.5f, col);
            pa_fill_ellipse(c, x + lx, y + bob + ly, s * 0.18f, s * 0.25f, pa_shade(col, 0.25f));
        } else {
            /* Bush: three overlapping blobs with a shaded underside. */
            pa_fill_ellipse(c, x, y + bob + s * 0.25f, s * 1.1f, s * 0.35f, pa_shade(col, -0.25f));
            pa_fill_circle(c, x - s * 0.5f, y + bob, s * 0.6f, col);
            pa_fill_circle(c, x + s * 0.45f, y + bob + s * 0.05f, s * 0.55f, col);
            pa_fill_circle(c, x, y + bob - s * 0.35f, s * 0.7f, pa_shade(col, 0.12f));
        }
    }
}

static void wall_box(PA_Canvas *c, int x, int y, float hw, float hh) {
    float u0 = (float)x - hw, u1 = u0 + 1.0f, v0 = (float)y - hh, v1 = v0 + 1.0f;
    const PA_Color south = pa_hex(0xA9BED3), side = pa_hex(0xC4D4E4), top = pa_hex(0xFDFEFF);

    /* Faces toward the camera, only where they border open floor. The south
       face always faces it; an east face only left of centre, a west face only
       right of it. */
    if (y + 1 < S.h && S.grid[y + 1][x] != '#')
        quad(c, proj(u0, v1, WALL_H), proj(u1, v1, WALL_H), proj(u1, v1, 0), proj(u0, v1, 0), south);
    if (u1 < 0.0f && x + 1 < S.w && S.grid[y][x + 1] != '#')
        quad(c, proj(u1, v0, WALL_H), proj(u1, v1, WALL_H), proj(u1, v1, 0), proj(u1, v0, 0), side);
    if (u0 > 0.0f && x > 0 && S.grid[y][x - 1] != '#')
        quad(c, proj(u0, v0, WALL_H), proj(u0, v1, WALL_H), proj(u0, v1, 0), proj(u0, v0, 0), side);

    /* Tops overlap a hair so neighbouring walls merge into one white surface. */
    float e = 0.02f;
    quad(c, proj(u0 - e, v0 - e, WALL_H), proj(u1 + e, v0 - e, WALL_H),
         proj(u1 + e, v1 + e, WALL_H), proj(u0 - e, v1 + e, WALL_H), top);
}

static void draw_ball(PA_Canvas *c, float hw, float hh, PA_Color paint) {
    float u = S.bx + 0.5f - hw, v = S.by + 0.5f - hh;
    float r = 0.36f;
    PA_Vec2 foot = proj(u, v, 0.0f), mid = proj(u, v, r), edge = proj(u + r, v, r);
    float pr = edge.x - mid.x;
    pa_shadow(c, foot.x, foot.y, pr * 1.1f, pr * 0.45f, 0.35f);
    PA_Paint ball = pa_radial(mid.x - pr * 0.35f, mid.y - pr * 0.4f, pr * 0.1f, pr * 1.25f);
    pa_stop(&ball, 0.0f, pa_shade(paint, 0.60f));
    pa_stop(&ball, 0.5f, paint);
    pa_stop(&ball, 1.0f, pa_shade(paint, -0.38f));
    pa_fill_ellipse_paint(c, mid.x, mid.y, pr, pr, &ball);
}

static void splat_render(PA_Canvas *c) {
    const Theme *th = &THEMES[S.level % THEME_COUNT];
    PA_Color paint = pa_hex(th->paint);
    pa_clear(c, pa_hex(th->bg));
    decorations(c, th);
    fit_camera(c);

    float hw = (float)S.w * 0.5f, hh = (float)S.h * 0.5f;

    /* Drop shadow of the slab, then its front face under the maze. */
    {
        PA_Vec2 a = proj(-hw, -hh, -SLAB_T), b = proj(hw, -hh, -SLAB_T);
        PA_Vec2 d = proj(hw, hh, -SLAB_T), e = proj(-hw, hh, -SLAB_T);
        /* A short contact shadow, not a long cast one: the plates are lit
           almost straight on. */
        float off = P.k / P.D * 0.30f;     /* P.k / P.D is pixels per tile */
        a.y += off; b.y += off; d.y += off; e.y += off;
        quad(c, a, b, d, e, pa_alpha(pa_shade(pa_hex(th->bg), -0.45f), 0.30f));
    }
    {
        PA_Vec2 a = proj(-hw, hh, WALL_H), b = proj(hw, hh, WALL_H);
        PA_Vec2 d = proj(hw, hh, -SLAB_T), e = proj(-hw, hh, -SLAB_T);
        PA_Paint face = pa_linear(0, a.y, 0, d.y);
        pa_stop(&face, 0.0f, pa_hex(0xDCE6F0));
        pa_stop(&face, 1.0f, pa_hex(0xB7C8DA));
        PA_Vec2 q[4] = { a, b, d, e };
        pa_fill_poly_paint(c, q, 4, &face);
    }

    /* Corridor floor: bare tiles a pale blue-grey, painted ones the paint. The
       quads overlap slightly so a painted run reads as one continuous stripe. */
    for (int y = 0; y < S.h; y++) {
        for (int x = 0; x < S.w; x++) {
            char g = S.grid[y][x];
            if (g == '#' || g == 0) continue;
            float u0 = (float)x - hw, v0 = (float)y - hh, e = 0.03f;
            PA_Color col = S.painted[y][x] == 1 ? paint : pa_hex(0xC9DCEE);
            quad(c, proj(u0 - e, v0 - e, 0), proj(u0 + 1 + e, v0 - e, 0),
                 proj(u0 + 1 + e, v0 + 1 + e, 0), proj(u0 - e, v0 + 1 + e, 0), col);
        }
    }

    /* Walls far to near, outer columns before inner ones within a row, with
       the ball slotted in before the first row that can overlap it. */
    int ball_drawn = 0;
    for (int y = 0; y < S.h; y++) {
        if (!ball_drawn && (float)y > S.by + 0.5f) { draw_ball(c, hw, hh, paint); ball_drawn = 1; }
        for (int pass = 0; pass < S.w; pass++) {
            /* Alternate from the edges inward. */
            int x = (pass & 1) ? S.w - 1 - pass / 2 : pass / 2;
            if (S.grid[y][x] == '#') wall_box(c, x, y, hw, hh);
        }
    }
    if (!ball_drawn) draw_ball(c, hw, hh, paint);

    /* HUD: the plates show nothing but the level number. */
    char buf[64];
    snprintf(buf, sizeof(buf), "LEVEL %d", S.level + 1);
    pa_text_bold(c, buf, (float)c->w * 0.5f, 36.0f, 24.0f, PA_RGB(255, 255, 255),
                 pa_shade(pa_hex(th->bg), -0.35f), PA_ALIGN_CENTER, 3.0f, 1.6f);

    float prog = (float)S.done / (float)(S.total > 0 ? S.total : 1);
    float mw = (float)c->w * 0.42f;
    float mx = (float)c->w * 0.5f - mw * 0.5f;
    pa_round_rect(c, mx, 84.0f, mw, 10.0f, 5.0f, pa_alpha(pa_shade(pa_hex(th->bg), -0.35f), 0.45f));
    if (prog > 0.0f)
        pa_round_rect(c, mx, 84.0f, mw * prog, 10.0f, 5.0f, PA_RGB(255, 255, 255));

    if (S.moves == 0 && !S.cleared) {
        pa_text_bold(c, "SWIPE TO ROLL", (float)c->w * 0.5f, (float)c->h - 52.0f, 16.0f,
                     PA_RGB(255, 255, 255), pa_shade(pa_hex(th->bg), -0.35f),
                     PA_ALIGN_CENTER, 4.0f, 1.2f);
    }

    if (S.cleared) {
        for (int i = 0; i < CONFETTI; i++) {
            const Bit *b = &S.bits[i];
            float px = b->x * (float)c->w, py = b->y * (float)c->h;
            float s = 9.0f, cr = cosf(b->rot) * s, sr = sinf(b->rot) * s * 0.6f;
            PA_Vec2 q[4] = { { px - cr, py - sr }, { px + sr, py - cr },
                             { px + cr, py + sr }, { px - sr, py + cr } };
            pa_fill_poly(c, q, 4, b->col);
        }
        float pop = pa_clamp01(S.clear_t * 5.0f);
        float sc = 0.6f + 0.4f * pop + sinf(pop * PA_PI) * 0.12f;
        float y0 = (float)c->h * 0.30f;
        /* A splash ribbon in the paint colour behind the banner. */
        float bw = (float)c->w * 0.74f * sc, bh = 96.0f * sc;
        pa_round_rect(c, (float)c->w * 0.5f - bw * 0.5f, y0 - 14.0f * sc, bw, bh, 18.0f * sc, paint);
        snprintf(buf, sizeof(buf), "LEVEL %d", S.level + 1);
        pa_text_bold(c, buf, (float)c->w * 0.5f, y0, 26.0f * sc, PA_RGB(255, 255, 255),
                     pa_shade(paint, -0.45f), PA_ALIGN_CENTER, 3.0f, 1.6f);
        pa_text_bold(c, "COMPLETE", (float)c->w * 0.5f, y0 + 38.0f * sc, 30.0f * sc,
                     PA_RGB(255, 255, 255), pa_shade(paint, -0.45f), PA_ALIGN_CENTER, 3.0f, 1.8f);
    }
}

static void splat_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    pa_fill_rect(c, x, y, w, h, pa_hex(0x6B9CFF));
    int n = 6;
    float cell = (w < h ? w : h) * 0.8f / (float)n;
    float ox = x + (w - cell * n) * 0.5f;
    float oy = y + (h - cell * n) * 0.5f;
    float progress = pa_wrapf(t * 0.35f, 1.0f);

    pa_fill_rect(c, ox + 4.0f, oy + 6.0f, cell * n, cell * n, PA_RGBA(30, 50, 120, 70));
    pa_fill_rect(c, ox, oy + cell * n, cell * n, cell * 0.35f, pa_hex(0xB7C8DA));
    for (int gy = 0; gy < n; gy++) {
        for (int gx = 0; gx < n; gx++) {
            int wall = gx == 0 || gy == 0 || gx == n - 1 || gy == n - 1 ||
                       (gx == 2 && gy == 2) || (gx == 3 && gy == 3);
            float px = ox + (float)gx * cell;
            float py = oy + (float)gy * cell;
            float order = (float)(gy * n + gx) / (float)(n * n);
            PA_Color col = wall ? pa_hex(0xFDFEFF)
                                : (order < progress ? pa_hex(0x7B22F2) : pa_hex(0xC9DCEE));
            pa_fill_rect(c, px, py, cell + 0.5f, cell + 0.5f, col);
            if (wall && gy + 1 < n - 1 && gy > 0)
                pa_fill_rect(c, px, py + cell, cell, cell * 0.25f, pa_hex(0xA9BED3));
        }
    }
}

const PA_Game PA_GAME_SPLAT = {
    "splat", "Roller Splat", "Puzzle",
    "Swipe the ball, it rolls until it hits a wall. Paint every tile in the maze.",
    PA_RGB(123, 34, 242),
    splat_start, splat_stop, splat_update, splat_render, splat_thumb
};
