/* ===========================================================================
   ROLLER SPLAT - native
   Swipe a direction, the ball rolls until it hits a wall, painting everything
   it crosses. Cover every tile to clear the level.

   Built against the Roller Splat! plates: one flat vivid backdrop per world
   with floating 3D props, and the maze as a big slab seen through a tilted
   perspective camera, its corridors sunk into it as real wells with lit side
   faces and contact shadow. Paint goes down wet: a pale speckled spatter at
   the leading edge that settles into solid colour, droplets thrown off the
   ball, a splash on the wall it stops against. Clearing a board pops a paint
   splat badge and a confetti storm, then the next board slides in.

   Levels are carved by rolling: the generator moves a virtual ball and digs
   out each roll it makes, locking the tile past every stop as a wall. The
   carving sequence is therefore always a solution, and a reachability check
   then rejects any board where a player could roll into a pocket and get
   stuck.
   =========================================================================== */
#include "../pa.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

#define MAX_W 16
#define MAX_H 24
#define CELLS (MAX_W * MAX_H)
#define MAX_DROPS 900
#define CONFETTI 440
#define MAX_SPARKS 120
#define MAX_PROPS 8

/* Camera and board geometry, in tiles. */
#define TILT    0.61f            /* ~35 deg off top-down: a 55 deg pitch */
#define WALL_H  0.62f
#define SLAB_T  0.32f
#define BALL_R  0.40f

/* Right, left, down, up: the opposite of d is d ^ 1. */
static const int DXS[4] = { 1, -1, 0, 0 }, DYS[4] = { 0, 0, 1, -1 };

/* ----------------------------------------------------------------- themes */
enum { PROP_TUBE, PROP_SHARD, PROP_LOG, PROP_BUSH, PROP_ORB, PROP_RING };

typedef struct {
    uint32_t bg;
    uint32_t rim;             /* contrasting lip around the slab */
    uint32_t slab, slab_front;
    uint32_t well, well_face; /* unpainted corridor floor and its walls */
    uint32_t ink;             /* HUD outline */
    uint32_t prop_a, prop_b;
    int      prop_kind;
    uint32_t paints[4];
} Theme;

/* Five worlds, five levels each, measured off the plates. Every one is a
   raised slab with a contrasting rim: periwinkle with cyan tubes and a white
   slab; mustard with orange logs and a white slab; an ice field of low-poly
   shards under a blue slab; a lawn with round bushes under a blue slab; and
   a lavender dusk with glossy orbs and rings. */
static const Theme THEMES[] = {
    { 0x6A9CF4, 0x3949D8, 0xFFFFFF, 0xD3DDEB, 0xA9D3DC, 0x7FA2B0, 0x1A237E,
      0x4FDCFF, 0x26AEEA, PROP_TUBE, { 0x7A1CF2, 0xFF9F00, 0x00C96B, 0xFF2D87 } },
    { 0xE8DE55, 0xFF8A1F, 0xFFFFFF, 0xE6E0C8, 0xACD8E0, 0x6F909C, 0x3B3B3B,
      0xFFB347, 0xF5781E, PROP_LOG,  { 0xFF2A78, 0xE81FE8, 0x1F5BFF, 0x8A2BE2 } },
    { 0xEDF2FA, 0x0A4FA8, 0x0AA2FF, 0x0770B8, 0x9DBACA, 0x6F8B9C, 0x4A4E63,
      0xF8FAFE, 0xBDC4E3, PROP_SHARD, { 0xD8246E, 0xFF9800, 0x7C2BF0, 0x00B860 } },
    { 0x14DE33, 0xFFFFFF, 0x0AA0F5, 0x0770B8, 0xA4C6D8, 0x6A8EA3, 0x174D1F,
      0x27B84A, 0x168A30, PROP_BUSH, { 0xFF2A78, 0xFFA000, 0xE81FE8, 0x8B1FFF } },
    { 0x9B87FF, 0xFF6FB5, 0xFFFFFF, 0xDAD3F7, 0xC8C2EF, 0x9890CC, 0x2D1F6B,
      0xFF7BC0, 0xFFD54F, PROP_ORB,  { 0x00BFA5, 0xFF6D00, 0xE91E63, 0x2962FF } },
};
#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))
#define LEVELS_PER_THEME 5

static const uint32_t CONFETTI_COLS[6] = { 0xFF1744, 0x00E676, 0xFFEA00, 0xD500F9, 0x2979FF, 0xFF9100 };

/* ------------------------------------------------------------------ state */
typedef struct { float u, v, h, vu, vv, vh, age, life, size; int stuck; PA_Color col; } Drop;
typedef struct { float x, y, vx, vy, rot, vr, flip, vflip, size; PA_Color col; } Bit;
typedef struct { int kind; float x, y, s, ang, sweep, phase; } Prop;
typedef struct { float u, v, age, life, size; } Spark;

typedef struct {
    int   level, theme, shape;
    int   w, h;
    char  grid[MAX_H][MAX_W + 1];
    unsigned char painted[MAX_H][MAX_W];
    float age[MAX_H][MAX_W];           /* seconds since the ball last crossed it */
    int   total, done, moves, par;

    int   tx, ty;                      /* resting tile, or the roll's end */
    int   fx, fy;                      /* roll start */
    int   dx, dy, rolling, roll_len, painted_upto;
    float roll_s, roll_v;
    float bx, by;                      /* ball position, tiles */
    int   last_dx, last_dy;

    float squash_t, squash_amp;
    int   squash_axis;                 /* 0 squashed along x, 1 along y */
    float shake;

    int   cleared;
    float clear_t;
    int   praise;
    float intro_t, idle_t, time;
    int   note_i;

    Drop  drops[MAX_DROPS];
    int   drop_next;
    Bit   bits[CONFETTI];
    Spark sparks[MAX_SPARKS];
    int   spark_next;
    Prop  props[MAX_PROPS];
    int   nprops;
    PA_Rng rng;

    float view_w, view_h;              /* last canvas size, for confetti spawn */
} Splat;

static Splat S;

static int walkable(int x, int y) {
    if (x < 0 || y < 0 || x >= S.w || y >= S.h) return 0;
    return S.grid[y][x] != '#' && S.grid[y][x] != ' ' && S.grid[y][x] != 0;
}

/** A raised wall tile (not a void tile cut out of the silhouette). */
static int is_block(int x, int y) {
    return x >= 0 && y >= 0 && x < S.w && y < S.h && S.grid[y][x] == '#';
}

static int is_solid(int x, int y) {
    return x >= 0 && y >= 0 && x < S.w && y < S.h && S.grid[y][x] != ' ' && S.grid[y][x] != 0;
}

static int is_wall(int x, int y) { return !walkable(x, y); }

static int roll_end(int x, int y, int dx, int dy, int *ex, int *ey) {
    int n = 0;
    while (walkable(x + dx, y + dy)) { x += dx; y += dy; n++; }
    *ex = x; *ey = y;
    return n;
}

static PA_Color paint_col(void) {
    const Theme *th = &THEMES[S.theme];
    return pa_hex(th->paints[S.level % 4]);
}

/* -------------------------------------------------------------- fairness */
/* Every floor tile must lie on some roll reachable from the start, and every
   reachable resting spot must be able to roll back to the start. Without the
   second condition a board can hold a one-way pocket: roll into it early and
   the tiles outside can never be reached. */
static int level_is_fair(int sx, int sy) {
    unsigned char seen[MAX_H][MAX_W], covered[MAX_H][MAX_W];
    static int qx[CELLS], qy[CELLS];
    int head = 0, tail = 0;
    memset(seen, 0, sizeof(seen));
    memset(covered, 0, sizeof(covered));
    seen[sy][sx] = 1; covered[sy][sx] = 1;
    qx[tail] = sx; qy[tail] = sy; tail++;
    while (head < tail) {
        int x = qx[head], y = qy[head]; head++;
        for (int d = 0; d < 4; d++) {
            int ex, ey;
            if (!roll_end(x, y, DXS[d], DYS[d], &ex, &ey)) continue;
            for (int cx = x, cy = y; cx != ex || cy != ey;) {
                cx += DXS[d]; cy += DYS[d]; covered[cy][cx] = 1;
            }
            if (!seen[ey][ex]) { seen[ey][ex] = 1; qx[tail] = ex; qy[tail] = ey; tail++; }
        }
    }
    for (int y = 0; y < S.h; y++)
        for (int x = 0; x < S.w; x++)
            if (walkable(x, y) && !covered[y][x]) return 0;

    for (int i = 0; i < tail; i++) {
        unsigned char back[MAX_H][MAX_W];
        static int bx[CELLS], by[CELLS];
        int h2 = 0, t2 = 0, ok = 0;
        memset(back, 0, sizeof(back));
        back[qy[i]][qx[i]] = 1; bx[t2] = qx[i]; by[t2] = qy[i]; t2++;
        while (h2 < t2) {
            int x = bx[h2], y = by[h2]; h2++;
            if (x == sx && y == sy) { ok = 1; break; }
            for (int d = 0; d < 4; d++) {
                int ex, ey;
                if (!roll_end(x, y, DXS[d], DYS[d], &ex, &ey)) continue;
                if (!back[ey][ex]) { back[ey][ex] = 1; bx[t2] = ex; by[t2] = ey; t2++; }
            }
        }
        if (!ok) return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- solver */
/* Greedy: breadth-first over resting spots to the nearest one that has a roll
   painting something new, preferring the roll that paints most. It always
   terminates on a fair board, it is what the demo bot plays, and its move
   count is the par for "PERFECT!". */
static int solver_dir(int x, int y, unsigned char pt[MAX_H][MAX_W]) {
    unsigned char seen[MAX_H][MAX_W];
    signed char first[MAX_H][MAX_W];
    static int qx[CELLS], qy[CELLS];
    int head = 0, tail = 0;
    memset(seen, 0, sizeof(seen));
    seen[y][x] = 1; first[y][x] = -1;
    qx[tail] = x; qy[tail] = y; tail++;
    while (head < tail) {
        int cx = qx[head], cy = qy[head]; head++;
        int best = -1, bestn = 0;
        for (int d = 0; d < 4; d++) {
            int ex, ey, n = roll_end(cx, cy, DXS[d], DYS[d], &ex, &ey), fresh = 0;
            for (int k = 1; k <= n; k++)
                if (!pt[cy + DYS[d] * k][cx + DXS[d] * k]) fresh++;
            if (fresh > bestn) { bestn = fresh; best = d; }
        }
        if (best >= 0) return first[cy][cx] < 0 ? best : first[cy][cx];
        for (int d = 0; d < 4; d++) {
            int ex, ey;
            if (!roll_end(cx, cy, DXS[d], DYS[d], &ex, &ey)) continue;
            if (seen[ey][ex]) continue;
            seen[ey][ex] = 1;
            first[ey][ex] = (signed char)(first[cy][cx] < 0 ? d : first[cy][cx]);
            qx[tail] = ex; qy[tail] = ey; tail++;
        }
    }
    return -1;
}

static int compute_par(void) {
    unsigned char pt[MAX_H][MAX_W];
    memcpy(pt, S.painted, sizeof(pt));
    int x = S.tx, y = S.ty, left = S.total - S.done, moves = 0;
    while (left > 0 && moves < 400) {
        int d = solver_dir(x, y, pt);
        if (d < 0) break;
        int ex, ey, n = roll_end(x, y, DXS[d], DYS[d], &ex, &ey);
        for (int k = 1; k <= n; k++) {
            unsigned char *p = &pt[y + DYS[d] * k][x + DXS[d] * k];
            if (!*p) { *p = 1; left--; }
        }
        x = ex; y = ey; moves++;
    }
    return moves;
}

/* ------------------------------------------------------------ generation */
/*
 * Boards are built on a lattice: corridors run only along odd rows and odd
 * columns, so every corridor is exactly one tile wide and every wall is a
 * straight run or a clean rectangular block between corridors. Tiles at
 * (odd, odd) are junction nodes, tiles between two nodes are the links, and
 * tiles at (even, even) are always wall.
 *
 * The carver rolls a virtual ball node to node and opens each roll, locking
 * the link past every stop as a wall, so its own sequence of rolls always
 * solves the board. Links are then opened one at a time (with their mirror
 * image on most boards), each kept only if the board stays fair. Openings
 * that would leave a lone one-tile wall island are refused past two.
 *
 * From level 10 the silhouette varies. A node mask restricts carving to a
 * diagonal staircase band or a diamond, and the slab is cut to that
 * shape; or the board is a floating path network, where only the walls that
 * touch a path exist and the backdrop shows between the runs. Void tiles are
 * ' ' in the grid.
 */
enum { SHAPE_RECT, SHAPE_FLOAT, SHAPE_STAIR, SHAPE_DIAMOND };

static int level_shape(int index) {
    if (index < 9) return SHAPE_RECT;
    static const int CYCLE[6] = { SHAPE_FLOAT, SHAPE_STAIR, SHAPE_RECT, SHAPE_DIAMOND, SHAPE_FLOAT, SHAPE_STAIR };
    return CYCLE[(index - 9) % 6];
}

static void level_dims(int index, int *W, int *H, float *density) {
    /* Whole-grid sizes including the outer wall: 9 to 13 columns, and tall,
       so the tilted board still fills well over half the screen height. */
    static const int TW[] = { 9, 9, 9, 11, 11, 11, 11, 13, 13, 13 };
    static const int TH[] = { 17, 17, 19, 19, 21, 21, 21, 23, 23, 23 };
    if (index < 10) {
        *W = TW[index]; *H = TH[index];
    } else {
        PA_Rng r;
        pa_rng_seed(&r, (uint32_t)index * 131u + 7u);
        *W = 11 + 2 * pa_rng_int(&r, 0, 1);
        *H = *W + 10;
        if (*H > MAX_H - 1) *H = MAX_H - 1;
    }
    float t = (float)(index < 14 ? index : 14) / 14.0f;
    *density = 0.72f + 0.14f * t;
}

static unsigned char g_mask[MAX_H][MAX_W];   /* node mask, indexed by node */

static void build_mask(int shape, int nx, int ny) {
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            float u = nx > 1 ? (float)i / (float)(nx - 1) : 0.5f;
            float v = ny > 1 ? (float)j / (float)(ny - 1) : 0.5f;
            int in = 1;
            if (shape == SHAPE_STAIR) in = fabsf(u - v) <= 0.42f;
            else if (shape == SHAPE_DIAMOND) in = fabsf(u - 0.5f) * 0.9f + fabsf(v - 0.5f) <= 0.78f;
            g_mask[j][i] = (unsigned char)in;
        }
}

static int node_ok(int x, int y) {
    if (x < 1 || y < 1 || x > S.w - 2 || y > S.h - 2) return 0;
    return g_mask[(y - 1) / 2][(x - 1) / 2];
}

static int is_island(int x, int y) {
    if (x < 1 || y < 1 || x >= S.w - 1 || y >= S.h - 1 || walkable(x, y)) return 0;
    return walkable(x + 1, y) && walkable(x - 1, y) && walkable(x, y + 1) && walkable(x, y - 1);
}

static int count_islands(void) {
    int n = 0;
    for (int y = 2; y < S.h - 1; y += 2)
        for (int x = 2; x < S.w - 1; x += 2) n += is_island(x, y);
    return n;
}

static int carve_lattice(PA_Rng *r, int W, int H, int *sx, int *sy) {
    unsigned char lock[MAX_H][MAX_W];
    memset(lock, 0, sizeof(lock));
    memset(S.grid, 0, sizeof(S.grid));
    S.w = W; S.h = H;
    for (int y = 0; y < H; y++) { memset(S.grid[y], '#', (size_t)W); S.grid[y][W] = 0; }
    int nx = (W - 1) / 2, ny = (H - 1) / 2, x = 1, y = 1, total = 0;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) total += g_mask[j][i];
    for (int k = 0; k < 200; k++) {
        x = 1 + 2 * pa_rng_int(r, 0, nx - 1); y = 1 + 2 * pa_rng_int(r, 0, ny - 1);
        if (node_ok(x, y)) break;
    }
    if (!node_ok(x, y)) return 0;
    *sx = x; *sy = y;
    S.grid[y][x] = '.';
    int nodes = 1, target = (total * 3) / 4, last = -1, fails = 0;
    while (nodes < target && fails < 900) {
        int d = pa_rng_int(r, 0, 3);
        if (last >= 0 && ((d ^ 1) == last || d == last)) { fails++; continue; }
        int span = DXS[d] ? nx - 1 : ny - 1;
        int k = pa_rng_chance(r, 0.6f) ? pa_rng_int(r, (span + 1) / 2, span) : pa_rng_int(r, 1, span);
        int L = 2 * k, ok = 1, fresh = 0;
        for (int i = 1; i <= L; i++) {
            int cx = x + DXS[d] * i, cy = y + DYS[d] * i;
            if (cx < 1 || cy < 1 || cx > W - 2 || cy > H - 2 || lock[cy][cx]) { ok = 0; break; }
            if ((i & 1) == 0) {
                if (!node_ok(cx, cy)) { ok = 0; break; }
                if (S.grid[cy][cx] == '#') fresh++;
            }
        }
        if (!ok) { fails++; continue; }
        int ex = x + DXS[d] * (L + 1), ey = y + DYS[d] * (L + 1);
        int border = !node_ok(ex + DXS[d], ey + DYS[d]);
        if (!border && S.grid[ey][ex] != '#') { fails++; continue; }
        if (fresh == 0 && !pa_rng_chance(r, 0.3f)) { fails++; continue; }
        for (int i = 1; i <= L; i++) S.grid[y + DYS[d] * i][x + DXS[d] * i] = '.';
        if (!border) lock[ey][ex] = 1;
        x += DXS[d] * L; y += DYS[d] * L;
        last = d;
        nodes += fresh;
    }
    return nodes >= target / 2;
}

/* The two nodes a link joins. */
static void link_nodes(int lx, int ly, int *ax, int *ay, int *bx, int *by) {
    int vert = (lx & 1);                /* odd x, even y: joins the nodes above and below */
    *ax = vert ? lx : lx - 1; *ay = vert ? ly - 1 : ly;
    *bx = vert ? lx : lx + 1; *by = vert ? ly + 1 : ly;
}

/* Open link (lx, ly) and any wall node at its ends; returns how many tiles. */
static int open_link(int lx, int ly, int undo[][2]) {
    int n = 0, ax, ay, bx, by;
    link_nodes(lx, ly, &ax, &ay, &bx, &by);
    if (!node_ok(ax, ay) || !node_ok(bx, by)) return 0;
    int cells[3][2] = { { lx, ly }, { ax, ay }, { bx, by } };
    for (int i = 0; i < 3; i++) {
        int cx = cells[i][0], cy = cells[i][1];
        if (S.grid[cy][cx] == '#') { S.grid[cy][cx] = '.'; undo[n][0] = cx; undo[n][1] = cy; n++; }
    }
    return n;
}

/** Cut the slab to its silhouette: a tile is solid when any node it touches
    is in `keep`; everything else becomes void. */
static void cut_silhouette(int shape) {
    int nx = (S.w - 1) / 2, ny = (S.h - 1) / 2;
    unsigned char keep[MAX_H][MAX_W];
    memset(keep, 0, sizeof(keep));
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++)
            keep[j][i] = (unsigned char)walkable(1 + 2 * i, 1 + 2 * j);
    /* A shaped slab also keeps the unused nodes its paths wrap around, so it
       reads as one cut slab; a floating network keeps only the paths. */
    if (shape != SHAPE_FLOAT) {
        unsigned char fill[MAX_H][MAX_W];
        memcpy(fill, keep, sizeof(fill));
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                if (keep[j][i] || !g_mask[j][i]) continue;
                int n = (i > 0 && keep[j][i - 1]) + (i + 1 < nx && keep[j][i + 1]) +
                        (j > 0 && keep[j - 1][i]) + (j + 1 < ny && keep[j + 1][i]);
                fill[j][i] = n >= 2;
            }
        memcpy(keep, fill, sizeof(fill));
    }
    for (int y = 0; y < S.h; y++)
        for (int x = 0; x < S.w; x++) {
            int solid = 0;
            for (int dy = -1; dy <= 1 && !solid; dy++)
                for (int dx = -1; dx <= 1 && !solid; dx++) {
                    int qx = x + dx, qy = y + dy;
                    if (qx < 1 || qy < 1 || qx > S.w - 2 || qy > S.h - 2 || !(qx & 1) || !(qy & 1)) continue;
                    solid = keep[(qy - 1) / 2][(qx - 1) / 2];
                }
            if (!solid) S.grid[y][x] = ' ';
        }
}

static int generate_level(int index, int *sx, int *sy) {
    int W, H;
    float density;
    level_dims(index, &W, &H, &density);
    int shape = level_shape(index);
    if (shape == SHAPE_FLOAT) density = 0.62f;   /* leave gaps for the backdrop */
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)index * 7919u + 13u);
    int nx = (W - 1) / 2, ny = (H - 1) / 2;
    build_mask(shape, nx, ny);
    int lattice = 0;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            if (!g_mask[j][i]) continue;
            lattice++;
            if (i + 1 < nx && g_mask[j][i + 1]) lattice++;
            if (j + 1 < ny && g_mask[j + 1][i]) lattice++;
        }
    /* Keep the densest fair board seen; stop early once one is dense enough. */
    static char best[MAX_H][MAX_W + 1];
    int best_open = -1, bsx = 0, bsy = 0;
    for (int attempt = 0; attempt < 400; attempt++) {
        if (!carve_lattice(&r, W, H, sx, sy)) continue;
        if (!level_is_fair(*sx, *sy)) continue;

        static int cand[CELLS];
        int nc = 0, open = 0;
        for (int yy = 1; yy < H - 1; yy++)
            for (int xx = 1; xx < W - 1; xx++) {
                if (S.grid[yy][xx] == '.') open++;
                else if (((xx ^ yy) & 1) == 1) cand[nc++] = yy * MAX_W + xx;   /* a link */
            }
        for (int i = nc - 1; i > 0; i--) {
            int j = pa_rng_int(&r, 0, i), t = cand[i];
            cand[i] = cand[j]; cand[j] = t;
        }
        int target = (int)((float)lattice * density);
        int mirror = (index % 3) != 2 && shape != SHAPE_STAIR;
        for (int i = 0; i < nc && open < target; i++) {
            int lx = cand[i] % MAX_W, ly = cand[i] / MAX_W;
            if (S.grid[ly][lx] != '#') continue;
            int undo[6][2], n = open_link(lx, ly, undo);
            if (!n) continue;
            int mx = W - 1 - lx;
            if (mirror && mx != lx && S.grid[ly][mx] == '#') n += open_link(mx, ly, undo + n);
            if (count_islands() > 2 || !level_is_fair(*sx, *sy)) {
                for (int k = 0; k < n; k++) S.grid[undo[k][1]][undo[k][0]] = '#';
                continue;
            }
            open += n;
        }
        /* A rectangular board must use its whole frame: an all-wall outer
           row or column of nodes reads as a mistake. */
        if (shape == SHAPE_RECT || shape == SHAPE_FLOAT) {
            int ux0 = 0, ux1 = 0, uy0 = 0, uy1 = 0;
            for (int k = 1; k < H - 1; k++) { ux0 |= walkable(1, k); ux1 |= walkable(W - 2, k); }
            for (int k = 1; k < W - 1; k++) { uy0 |= walkable(k, 1); uy1 |= walkable(k, H - 2); }
            if (!(ux0 && ux1 && uy0 && uy1)) continue;
        }
        int done = open >= (int)((float)lattice * (density - 0.06f));
        if (!done && open <= best_open) continue;
        best_open = open; bsx = *sx; bsy = *sy;
        memcpy(best, S.grid, sizeof(best));
        if (done) break;
    }
    if (best_open < 0) return 0;
    memcpy(S.grid, best, sizeof(best));
    S.w = W; S.h = H;
    *sx = bsx; *sy = bsy;
    if (shape != SHAPE_RECT) cut_silhouette(shape);
    S.grid[*sy][*sx] = 'o';
    S.shape = shape;
    return 1;
}

static const char *FALLBACK[] = {
    "#########",
    "#o......#",
    "#.#####.#",
    "#.#...#.#",
    "#.#.#.#.#",
    "#...#...#",
    "#.#####.#",
    "#.......#",
    "#########", NULL
};

/* ----------------------------------------------------------------- props */
static void add_prop(int kind, float x, float y, float s, float ang, float sweep, float phase) {
    if (S.nprops >= MAX_PROPS) return;
    Prop *p = &S.props[S.nprops++];
    p->kind = kind; p->x = x; p->y = y; p->s = s; p->ang = ang; p->sweep = sweep; p->phase = phase;
}

static void build_props(void) {
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)(S.level / LEVELS_PER_THEME) * 4099u + 11u);
    S.nprops = 0;
    float j = pa_rng_range(&r, -0.03f, 0.03f);
    switch (THEMES[S.theme].prop_kind) {
    case PROP_TUBE:
        add_prop(PROP_TUBE, 0.05f + j, 0.135f, 0.22f, 3.3f, 1.9f, 0.0f);
        add_prop(PROP_TUBE, 0.98f, 0.17f - j, 0.13f, 4.4f, 1.7f, 1.3f);
        add_prop(PROP_TUBE, 0.08f, 0.975f, 0.27f, 3.6f, 2.0f, 2.1f);
        add_prop(PROP_TUBE, 0.86f + j, 0.985f, 0.18f, 3.5f, 1.8f, 4.0f);
        break;
    case PROP_LOG:
        add_prop(PROP_LOG, 0.16f, 0.145f + j, 0.30f, -0.22f, 0.0f, 0.0f);
        add_prop(PROP_LOG, 0.97f, 0.16f, 0.16f, -0.10f, 0.0f, 1.7f);
        add_prop(PROP_LOG, 0.16f, 0.975f - j, 0.26f, 0.04f, 0.0f, 2.6f);
        add_prop(PROP_LOG, 0.88f, 0.99f, 0.20f, -0.30f, 0.0f, 3.3f);
        break;
    case PROP_SHARD:
        add_prop(PROP_SHARD, 0.10f, 0.23f, 0.70f, 0.38f, 0.22f, 0.0f);
        add_prop(PROP_SHARD, 0.40f, 0.09f, 0.42f, 1.95f, 0.16f, 1.0f);
        add_prop(PROP_SHARD, 0.93f, 0.40f, 0.62f, 3.45f, 0.20f, 2.0f);
        add_prop(PROP_SHARD, 0.24f, 0.99f, 0.55f, -1.25f, 0.18f, 3.0f);
        add_prop(PROP_SHARD, 0.84f, 0.97f, 0.48f, -2.05f, 0.16f, 4.0f);
        break;
    case PROP_BUSH:
        add_prop(PROP_BUSH, 0.07f, 0.15f, 0.15f, 0.0f, 0.0f, 0.0f);
        add_prop(PROP_BUSH, 0.95f, 0.165f + j, 0.09f, 0.0f, 1.0f, 1.2f);
        add_prop(PROP_BUSH, 0.12f, 0.975f, 0.12f, 0.0f, 1.0f, 2.2f);
        add_prop(PROP_BUSH, 0.88f, 0.985f, 0.17f, 0.0f, 0.0f, 3.2f);
        add_prop(PROP_ORB, 0.50f, 0.99f, 0.03f, 0.0f, 0.0f, 3.0f);
        break;
    default:
        add_prop(PROP_RING, 0.07f, 0.15f, 0.12f, 0.4f, 0.0f, 0.0f);
        add_prop(PROP_ORB, 0.94f, 0.18f + j, 0.06f, 0.0f, 0.0f, 1.0f);
        add_prop(PROP_ORB, 0.12f, 0.98f, 0.09f, 0.0f, 1.0f, 2.0f);
        add_prop(PROP_RING, 0.86f, 0.98f, 0.14f, -0.5f, 1.0f, 3.0f);
        break;
    }
}

/* ------------------------------------------------------------- level flow */
static void load_level(int index) {
    memset(S.painted, 0, sizeof(S.painted));
    for (int y = 0; y < MAX_H; y++)
        for (int x = 0; x < MAX_W; x++) S.age[y][x] = 99.0f;
    S.level = index;
    S.theme = (index / LEVELS_PER_THEME) % THEME_COUNT;

    int sx = -1, sy = -1;
    if (!generate_level(index, &sx, &sy)) {
        memset(S.grid, 0, sizeof(S.grid));
        S.h = 0; S.w = 0; S.shape = SHAPE_RECT;
        for (int y = 0; FALLBACK[y]; y++) {
            int len = (int)strlen(FALLBACK[y]);
            memcpy(S.grid[y], FALLBACK[y], (size_t)len);
            if (len > S.w) S.w = len;
            S.h++;
        }
    }

    S.total = 0;
    for (int y = 0; y < S.h; y++)
        for (int x = 0; x < S.w; x++) {
            char c = S.grid[y][x];
            if (c == '#' || c == ' ' || c == 0) continue;
            S.total++;
            if (c == 'o') { S.tx = x; S.ty = y; }
        }
    S.painted[S.ty][S.tx] = 1;
    S.done = 1;
    S.moves = 0;
    S.bx = (float)S.tx; S.by = (float)S.ty;
    S.rolling = 0; S.roll_s = 0.0f; S.roll_v = 0.0f;
    S.last_dx = 0; S.last_dy = 1;
    S.squash_t = 9.0f; S.squash_amp = 0.0f; S.shake = 0.0f;
    S.cleared = 0; S.clear_t = 0.0f;
    S.intro_t = 0.0f; S.idle_t = 0.0f;
    S.note_i = 0;
    for (int i = 0; i < MAX_DROPS; i++) S.drops[i].life = 0.0f;
    for (int i = 0; i < MAX_SPARKS; i++) S.sparks[i].life = 0.0f;
    S.par = compute_par();
    build_props();
}

static int start_level(void) {
    int d = pa_demo_mode();
    if (d > 0) return d - 1;              /* --demo N plays level N */
    return pa_save_get("splat.level", 0);
}

static void splat_start(void) {
    memset(&S, 0, sizeof(S));
    pa_rng_seed(&S.rng, 0xC0FFEEu);
    S.view_w = 540.0f; S.view_h = 1170.0f;
    load_level(start_level());
}

static void splat_stop(void) { }

/* --------------------------------------------------------------- effects */
static Drop *new_drop(void) {
    Drop *d = &S.drops[S.drop_next];
    S.drop_next = (S.drop_next + 1) % MAX_DROPS;
    memset(d, 0, sizeof(*d));
    return d;
}

static PA_Color fleck_col(PA_Color paint, PA_Rng *r) {
    float k = pa_rng_next(r);
    if (k < 0.22f) return pa_shade(paint, 0.55f);
    return pa_shade(paint, pa_rng_range(r, -0.12f, 0.12f));
}

/** Wet paint thrown off the ball as it crosses a tile. */
static void spatter(float u, float v, int n) {
    PA_Color paint = paint_col();
    for (int i = 0; i < n; i++) {
        Drop *d = new_drop();
        float px = (float)-S.dy, py = (float)S.dx;
        float side = pa_rng_range(&S.rng, -1.0f, 1.0f);
        d->u = u + px * side * 0.30f + (float)S.dx * pa_rng_range(&S.rng, -0.3f, 0.3f);
        d->v = v + py * side * 0.30f + (float)S.dy * pa_rng_range(&S.rng, -0.3f, 0.3f);
        d->h = pa_rng_range(&S.rng, 0.02f, 0.25f);
        float fwd = pa_rng_range(&S.rng, -2.5f, 4.0f), lat = side * pa_rng_range(&S.rng, 0.5f, 3.2f);
        d->vu = (float)S.dx * fwd + px * lat;
        d->vv = (float)S.dy * fwd + py * lat;
        d->vh = pa_rng_range(&S.rng, 0.5f, 3.5f);
        d->life = pa_rng_range(&S.rng, 0.35f, 0.8f);
        d->size = pa_rng_range(&S.rng, 0.05f, 0.11f);
        d->col = fleck_col(paint, &S.rng);
    }
}

/** Glints scattered over the last three tiles behind the ball. */
static void sparkle(int n) {
    for (int i = 0; i < n; i++) {
        Spark *k = &S.sparks[S.spark_next];
        S.spark_next = (S.spark_next + 1) % MAX_SPARKS;
        float back = pa_rng_range(&S.rng, 0.1f, 3.0f), side = pa_rng_range(&S.rng, -0.38f, 0.38f);
        k->u = S.bx + 0.5f - (float)S.dx * back + (float)-S.dy * side;
        k->v = S.by + 0.5f - (float)S.dy * back + (float)S.dx * side;
        k->age = 0.0f;
        k->life = pa_rng_range(&S.rng, 0.25f, 0.4f);
        k->size = pa_rng_range(&S.rng, 0.10f, 0.22f);
    }
}

/** A burst against the wall the ball stopped on, or all around it. */
static void splash(float u, float v, int n, float speed, int radial) {
    PA_Color paint = paint_col();
    for (int i = 0; i < n; i++) {
        Drop *d = new_drop();
        float a = pa_rng_range(&S.rng, 0.0f, PA_TAU), sp = pa_rng_range(&S.rng, 0.3f, 1.0f) * speed;
        d->u = u; d->v = v;
        d->h = pa_rng_range(&S.rng, 0.15f, 0.45f);
        if (radial) {
            d->vu = cosf(a) * sp; d->vv = sinf(a) * sp;
        } else {
            float px = (float)-S.dy, py = (float)S.dx, lat = pa_rng_range(&S.rng, -1.0f, 1.0f) * sp;
            float fwd = pa_rng_range(&S.rng, -0.4f, 1.0f) * sp;
            d->vu = (float)S.dx * fwd + px * lat;
            d->vv = (float)S.dy * fwd + py * lat;
        }
        d->vh = pa_rng_range(&S.rng, 1.0f, 5.0f);
        d->life = pa_rng_range(&S.rng, 0.5f, 1.1f);
        d->size = pa_rng_range(&S.rng, 0.05f, 0.14f);
        d->col = fleck_col(paint, &S.rng);
    }
}

static void update_drops(float dt) {
    for (int i = 0; i < MAX_DROPS; i++) {
        Drop *d = &S.drops[i];
        if (d->life <= 0.0f) continue;
        d->age += dt;
        if (d->age >= d->life) { d->life = 0.0f; continue; }
        if (d->stuck) continue;
        float pu = d->u, pv = d->v;
        d->vh -= 18.0f * dt;
        d->u += d->vu * dt; d->v += d->vv * dt; d->h += d->vh * dt;
        int cx = (int)floorf(d->u), cy = (int)floorf(d->v);
        float ground = is_wall(cx, cy) ? WALL_H : 0.0f;
        if (ground > 0.0f && d->h < ground - 0.04f) {
            /* Struck a wall face: stays where it hit. */
            d->u = pu; d->v = pv; d->stuck = 1;
        } else if (d->h <= ground) {
            d->h = ground; d->stuck = 1;
        }
    }
}

static void burst_confetti(void) {
    float aspect = S.view_h / (S.view_w > 1.0f ? S.view_w : 1.0f);
    for (int i = 0; i < CONFETTI; i++) {
        Bit *b = &S.bits[i];
        int g5 = i % 5, group = g5 <= 3 ? 0 : 2;
        if (group == 0) {
            /* Blown out from behind the badge. */
            float a = pa_rng_range(&S.rng, 0.0f, PA_TAU), sp = pa_rng_range(&S.rng, 0.35f, 1.6f);
            b->x = 0.5f + pa_rng_range(&S.rng, -0.3f, 0.3f);
            b->y = aspect * 0.2f + pa_rng_range(&S.rng, -0.06f, 0.06f);
            b->vx = cosf(a) * sp;
            b->vy = sinf(a) * sp - 0.35f;
        } else if (group == 1) {
            /* Blown up and out from the finished board. */
            float a = pa_rng_range(&S.rng, PA_PI * 1.05f, PA_PI * 1.95f), sp = pa_rng_range(&S.rng, 0.5f, 1.6f);
            b->x = 0.5f + pa_rng_range(&S.rng, -0.25f, 0.25f);
            b->y = aspect * pa_rng_range(&S.rng, 0.45f, 0.75f);
            b->vx = cosf(a) * sp * 0.7f;
            b->vy = sinf(a) * sp;
        } else {
            /* A curtain already falling over the whole screen height. */
            b->x = pa_rng_range(&S.rng, -0.05f, 1.05f);
            b->y = aspect * pa_rng_range(&S.rng, -0.35f, 0.95f);
            b->vx = pa_rng_range(&S.rng, -0.1f, 0.1f);
            b->vy = pa_rng_range(&S.rng, 0.05f, 0.3f);
        }
        b->rot = pa_rng_range(&S.rng, 0.0f, PA_TAU);
        b->vr = pa_rng_range(&S.rng, -7.0f, 7.0f);
        b->flip = pa_rng_range(&S.rng, 0.0f, PA_TAU);
        b->vflip = pa_rng_range(&S.rng, 4.0f, 11.0f);
        b->size = pa_rng_range(&S.rng, 0.015f, 0.030f);
        b->col = pa_hex(CONFETTI_COLS[i % 6]);
    }
}

/* ------------------------------------------------------------------ input */
static void try_roll(int d) {
    if (S.rolling || S.cleared || S.intro_t < 0.3f) return;
    int ex, ey, n = roll_end(S.tx, S.ty, DXS[d], DYS[d], &ex, &ey);
    if (!n) {
        /* Already against that wall: a small nudge so the swipe registers. */
        S.squash_t = 0.0f; S.squash_amp = 0.35f; S.squash_axis = DXS[d] ? 0 : 1;
        pa_tone(150, 120, 0.05f, 0, 0.05f);
        return;
    }
    S.fx = S.tx; S.fy = S.ty;
    S.tx = ex; S.ty = ey;
    S.dx = DXS[d]; S.dy = DYS[d];
    S.last_dx = S.dx; S.last_dy = S.dy;
    S.rolling = 1;
    S.roll_s = 0.0f; S.roll_v = 0.0f;
    S.roll_len = n;
    S.painted_upto = 0;
    S.moves++;
    pa_noise(0.07f, 0.035f);
    pa_tone(260, 420, 0.07f, 1, 0.05f);
}

#define ROLL_VMAX 24.0f        /* tiles per second at full speed */
#define ROLL_ACC  320.0f       /* reaches it in under a tenth of a second */
#define FRESH     0.38f        /* seconds wet paint takes to settle */

static void finish_roll(void) {
    S.rolling = 0;
    S.bx = (float)S.tx; S.by = (float)S.ty;
    S.squash_t = 0.0f; S.squash_amp = 1.0f; S.squash_axis = S.dx ? 0 : 1;
    S.shake = 1.0f;
    S.idle_t = 0.0f;
    splash((float)S.tx + 0.5f + (float)S.dx * 0.42f, (float)S.ty + 0.5f + (float)S.dy * 0.42f, 26, 2.2f, 0);
    float prog = (float)S.done / (float)(S.total > 0 ? S.total : 1);
    pa_tone(170.0f + prog * 90.0f, 105.0f, 0.09f, 0, 0.11f);
    pa_noise(0.04f, 0.05f);
    if (S.done >= S.total) {
        S.cleared = 1;
        S.clear_t = 0.0f;
        S.praise = S.moves <= S.par ? 0 : S.moves <= S.par + S.par / 3 + 1 ? 1 : 2;
        S.note_i = 0;
        burst_confetti();
        splash((float)S.tx + 0.5f, (float)S.ty + 0.5f, 90, 4.5f, 1);
        pa_noise(0.25f, 0.10f);
    }
}

static void next_level(void) {
    int next = S.level + 1;
    if (!pa_demo_mode() && next > pa_save_get("splat.level", 0)) {
        pa_save_set("splat.level", next);
        pa_save_flush();
    }
    load_level(next);
}

static void splat_update(float dt, const PA_Input *in) {
    S.time += dt;
    S.intro_t += dt;
    S.idle_t += dt;
    S.squash_t += dt;
    S.shake = S.shake > 0.0f ? S.shake - dt * 7.0f : 0.0f;
    for (int y = 0; y < S.h; y++)
        for (int x = 0; x < S.w; x++)
            if (S.age[y][x] < 50.0f) S.age[y][x] += dt;
    update_drops(dt);
    for (int i = 0; i < MAX_SPARKS; i++)
        if (S.sparks[i].life > 0.0f && (S.sparks[i].age += dt) >= S.sparks[i].life) S.sparks[i].life = 0.0f;

    if (S.cleared) {
        S.clear_t += dt;
        static const float NOTES[5] = { 523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f };
        while (S.note_i < 5 && S.clear_t >= 0.08f + (float)S.note_i * 0.085f) {
            pa_tone(NOTES[S.note_i], NOTES[S.note_i] * 1.01f, S.note_i == 4 ? 0.35f : 0.12f, 1, 0.10f);
            S.note_i++;
        }
        for (int i = 0; i < CONFETTI; i++) {
            Bit *b = &S.bits[i];
            b->vy += 1.5f * dt;
            float drag = expf(-1.6f * dt);
            b->vx *= drag;
            if (b->vy > 0.32f) b->vy = pa_approach(b->vy, 0.32f, 4.0f, dt);
            b->x += b->vx * dt + sinf(b->flip) * 0.02f * dt;
            b->y += b->vy * dt;
            b->rot += b->vr * dt;
            b->flip += b->vflip * dt;
        }
        if (S.clear_t > 3.0f || (S.clear_t > 1.0f && (in->tapped || in->key_pressed[PA_KEY_SPACE])))
            next_level();
        return;
    }

    if (S.rolling) {
        S.roll_v += ROLL_ACC * dt;
        if (S.roll_v > ROLL_VMAX) S.roll_v = ROLL_VMAX;
        S.roll_s += S.roll_v * dt;
        if (S.roll_s > (float)S.roll_len) S.roll_s = (float)S.roll_len;
        S.bx = (float)S.fx + (float)S.dx * S.roll_s;
        S.by = (float)S.fy + (float)S.dy * S.roll_s;
        if (S.painted_upto == 0) S.age[S.fy][S.fx] = 0.0f;
        while (S.painted_upto < S.roll_len && S.roll_s >= (float)S.painted_upto + 0.8f) {
            int k = ++S.painted_upto;
            int x = S.fx + S.dx * k, y = S.fy + S.dy * k;
            if (!S.painted[y][x]) {
                S.painted[y][x] = 1;
                S.done++;
                float prog = (float)S.done / (float)S.total;
                pa_tone(620.0f + prog * 700.0f, 700.0f + prog * 700.0f, 0.03f, 0, 0.025f);
            }
            S.age[y][x] = 0.0f;
            spatter((float)x + 0.5f, (float)y + 0.5f, 12);
            sparkle(8);
        }
        if (S.roll_s >= (float)S.roll_len) finish_roll();
        return;
    }

    if (pa_demo_mode()) {
        /* Review captures: the greedy solver plays, at a human-ish cadence. */
        if (S.time > 1.2f && S.intro_t > 0.6f && S.idle_t > 0.14f) {
            int d = solver_dir(S.tx, S.ty, S.painted);
            if (d >= 0) try_roll(d);
        }
        return;
    }

    if (in->swipe == PA_SWIPE_LEFT  || in->key_pressed[PA_KEY_LEFT])       try_roll(1);
    else if (in->swipe == PA_SWIPE_RIGHT || in->key_pressed[PA_KEY_RIGHT]) try_roll(0);
    else if (in->swipe == PA_SWIPE_UP    || in->key_pressed[PA_KEY_UP])    try_roll(3);
    else if (in->swipe == PA_SWIPE_DOWN  || in->key_pressed[PA_KEY_DOWN])  try_roll(2);
}

/* =============================================================== drawing */
/*
 * Camera. Grid coordinates (gx, gy) in tiles and a height in tiles. The board
 * leans away from the viewer by TILT with a mild keystone; heights also
 * magnify outward from the screen centre, as a close camera does, so the
 * wells show their inner walls on the far side and on the side away from the
 * centre line - which is what makes the plates read as cut into a slab.
 */

static struct {
    float cx, cy, k, D, st, ct, hw, hh, hpx, hpy, tile, rim, shadow;
} P;

static PA_Vec2 pj(float gx, float gy, float h) {
    float u = gx - P.hw, v = gy - P.hh;
    float s = P.k / (P.D - v * P.st);
    PA_Vec2 o;
    o.x = P.cx + u * (1.0f + h * P.hpx) * s;
    o.y = P.cy + (v * P.ct * (1.0f + h * P.hpy) - h * P.st) * s;
    return o;
}

static void quad(PA_Canvas *c, PA_Vec2 a, PA_Vec2 b, PA_Vec2 d, PA_Vec2 e, PA_Color col) {
    PA_Vec2 q[4] = { a, b, d, e };
    pa_fill_poly(c, q, 4, col);
}

static void quad_paint(PA_Canvas *c, PA_Vec2 a, PA_Vec2 b, PA_Vec2 d, PA_Vec2 e, const PA_Paint *p) {
    PA_Vec2 q[4] = { a, b, d, e };
    pa_fill_poly_paint(c, q, 4, p);
}

/** A wall face a-b along the top, d-e along the floor: lit at the lip, falling
    into shade at the foot, as the reference's extruded walls are. */
static void face_quad(PA_Canvas *c, PA_Vec2 a, PA_Vec2 b, PA_Vec2 d, PA_Vec2 e, PA_Color col) {
    PA_Paint g = pa_linear((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (d.x + e.x) * 0.5f, (d.y + e.y) * 0.5f);
    pa_stop(&g, 0.0f, pa_shade(col, 0.16f));
    pa_stop(&g, 0.55f, col);
    pa_stop(&g, 1.0f, pa_shade(col, -0.22f));
    quad_paint(c, a, b, d, e, &g);
}

static void tile_quad(PA_Canvas *c, float x0, float y0, float x1, float y1, float h, PA_Color col) {
    quad(c, pj(x0, y0, h), pj(x1, y0, h), pj(x1, y1, h), pj(x0, y1, h), col);
}

static float ease_out_cubic(float t) { t = 1.0f - pa_clamp01(t); return 1.0f - t * t * t; }

static void fit_camera(PA_Canvas *c, float top, float bottom, float slide) {
    P.hw = (float)S.w * 0.5f; P.hh = (float)S.h * 0.5f;
    float md = (float)(S.w > S.h ? S.w : S.h);
    P.st = sinf(TILT); P.ct = cosf(TILT);
    /* Far edge about 85% of the near edge's width. */
    P.D = 12.3f * P.hh * P.st;
    (void)md;
    P.hpx = 0.42f / P.hw;
    P.hpy = 0.10f / P.hh;
    P.cx = 0.0f; P.cy = 0.0f; P.k = 1.0f;
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    /* The rim is about 0.035 of the screen width at the usual board size. */
    P.rim = S.shape == SHAPE_RECT ? 0.045f * (float)S.w : 0.0f;
    for (int i = 0; i < 8; i++) {
        PA_Vec2 p = pj((i & 1) ? (float)S.w + P.rim : -P.rim, (i & 2) ? (float)S.h + P.rim : -P.rim,
                       (i & 4) ? WALL_H : -SLAB_T);
        if (p.x < minx) minx = p.x;
        if (p.x > maxx) maxx = p.x;
        if (p.y < miny) miny = p.y;
        if (p.y > maxy) maxy = p.y;
    }
    float avail_w = (float)c->w * 0.92f, avail_h = bottom - top;
    float s = avail_w / (maxx - minx);
    if ((maxy - miny) * s > avail_h) s = avail_h / (maxy - miny);
    P.k = s;
    P.cx = (float)c->w * 0.5f - (minx + maxx) * 0.5f * s;
    P.cy = top + (avail_h - (maxy - miny) * s) * 0.5f - miny * s + slide;
    PA_Vec2 a = pj(P.hw, P.hh, 0.0f), b = pj(P.hw + 1.0f, P.hh, 0.0f);
    P.tile = b.x - a.x;
    P.shadow = (float)c->w * 0.02f;
}

/* ------------------------------------------------------------ prop art */
static void fill_rot_ellipse(PA_Canvas *c, float cx, float cy, float rx, float ry, float ang,
                             const PA_Paint *p) {
    PA_Vec2 pts[32];
    float ca = cosf(ang), sa = sinf(ang);
    for (int i = 0; i < 32; i++) {
        float t = (float)i * PA_TAU / 32.0f, ex = cosf(t) * rx, ey = sinf(t) * ry;
        pts[i].x = cx + ex * ca - ey * sa;
        pts[i].y = cy + ex * sa + ey * ca;
    }
    pa_fill_poly_paint(c, pts, 32, p);
}

static void glossy_sphere(PA_Canvas *c, float x, float y, float r, PA_Color col) {
    PA_Paint g = pa_radial(x - r * 0.35f, y - r * 0.42f, r * 0.05f, r * 1.35f);
    pa_stop(&g, 0.0f, pa_shade(col, 0.55f));
    pa_stop(&g, 0.35f, pa_shade(col, 0.12f));
    pa_stop(&g, 0.75f, col);
    pa_stop(&g, 1.0f, pa_shade(col, -0.35f));
    pa_fill_ellipse_paint(c, x, y, r, r, &g);
}

/** A soft 3D tube bent along an arc: cast shadow, dark underside, lit body,
    highlight and a specular streak, each swept as overlapping discs. */
static void tube_arc(PA_Canvas *c, float cx, float cy, float R, float T, float a0, float sweep,
                     PA_Color col, PA_Color shadow) {
    enum { N = 40 };
    static const float OX[5] = { 0.30f, 0.0f, -0.07f, -0.13f, -0.17f };
    static const float OY[5] = { 0.55f, 0.0f, -0.10f, -0.20f, -0.26f };
    static const float RR[5] = { 0.50f, 0.50f, 0.42f, 0.24f, 0.08f };
    PA_Color cols[5] = { shadow, pa_shade(col, -0.30f), col, pa_shade(col, 0.30f),
                         pa_alpha(pa_shade(col, 0.75f), 0.8f) };
    for (int pass = 0; pass < 5; pass++) {
        for (int i = 0; i <= N; i++) {
            float t = (float)i / (float)N;
            if (pass == 4 && (t < 0.2f || t > 0.75f)) continue;
            float a = a0 + sweep * t;
            /* Taper the ends so the tube reads as rounded, not cut off. */
            float taper = 0.82f + 0.18f * sinf(t * PA_PI);
            float x = cx + cosf(a) * R + OX[pass] * T, y = cy + sinf(a) * R * 0.78f + OY[pass] * T;
            pa_fill_circle(c, x, y, RR[pass] * T * taper, cols[pass]);
        }
    }
}

static void log_prop(PA_Canvas *c, float x, float y, float L, float R, float ang, const Theme *th) {
    float ca = cosf(ang), sa = sinf(ang);
    float ax = x - ca * L * 0.5f, ay = y - sa * L * 0.5f, bx = x + ca * L * 0.5f, by = y + sa * L * 0.5f;
    float nx = sa, ny = -ca;                   /* the side facing up the screen */
    PA_Color base = pa_hex(th->prop_a), dark = pa_hex(th->prop_b);

    /* Flat cast shadow on the ground, hanging off its lower edge. */
    PA_Color sh = pa_mix(pa_hex(th->bg), dark, 0.38f);
    float ox = -R * 0.5f, oy = R * 1.5f;
    PA_Vec2 s[4] = { { ax - nx * R, ay - ny * R }, { bx - nx * R, by - ny * R },
                     { bx - nx * R + ox, by - ny * R + oy }, { ax - nx * R + ox, ay - ny * R + oy } };
    pa_fill_poly(c, s, 4, sh);
    pa_fill_ellipse(c, ax - nx * R * 0.4f + ox * 0.6f, ay - ny * R * 0.4f + oy * 0.55f, R * 0.5f, R * 0.85f, sh);

    PA_Paint g = pa_linear(x + nx * R, y + ny * R, x - nx * R, y - ny * R);
    pa_stop(&g, 0.0f, pa_shade(base, 0.45f));
    pa_stop(&g, 0.30f, base);
    pa_stop(&g, 0.75f, pa_mix(base, dark, 0.6f));
    pa_stop(&g, 1.0f, pa_shade(dark, -0.1f));
    fill_rot_ellipse(c, ax, ay, R * 0.45f, R, ang, &g);
    PA_Vec2 body[4] = { { ax + nx * R, ay + ny * R }, { bx + nx * R, by + ny * R },
                        { bx - nx * R, by - ny * R }, { ax - nx * R, ay - ny * R } };
    pa_fill_poly_paint(c, body, 4, &g);
    PA_Paint cap = pa_radial(bx - nx * R * 0.2f, by - ny * R * 0.2f, R * 0.1f, R * 1.1f);
    pa_stop(&cap, 0.0f, pa_shade(dark, 0.25f));
    pa_stop(&cap, 1.0f, dark);
    fill_rot_ellipse(c, bx, by, R * 0.45f, R, ang, &cap);
}

static void bush_prop(PA_Canvas *c, float x, float y, float s, int variant, const Theme *th) {
    PA_Color col = pa_hex(th->prop_a);
    PA_Color sh = pa_alpha(pa_shade(pa_hex(th->bg), -0.28f), 0.55f);
    pa_fill_ellipse(c, x - s * 0.55f, y + s * 0.62f, s * 1.25f, s * 0.42f, sh);
    float tall = variant ? 1.05f : 0.85f;
    PA_Paint g = pa_radial(x - s * 0.55f, y - s * 0.75f, s * 0.05f, s * 1.3f);
    pa_stop(&g, 0.0f, pa_shade(col, 0.35f));
    pa_stop(&g, 0.5f, col);
    pa_stop(&g, 1.0f, pa_shade(col, -0.30f));
    pa_fill_ellipse_paint(c, x - s * 0.3f, y - s * 0.25f, s * 0.55f, s * tall, &g);
    PA_Paint g2 = pa_radial(x + s * 0.2f, y - s * 0.15f, s * 0.05f, s * 0.95f);
    pa_stop(&g2, 0.0f, pa_shade(col, 0.30f));
    pa_stop(&g2, 0.5f, col);
    pa_stop(&g2, 1.0f, pa_shade(col, -0.32f));
    pa_fill_ellipse_paint(c, x + s * 0.42f, y + s * 0.12f, s * 0.62f, s * 0.58f, &g2);
}

/** A low-poly ice shard: a long blade with a lit face and a shaded face. */
static void shard_prop(PA_Canvas *c, float x, float y, float len, float ang, float width, const Theme *th) {
    float ca = cosf(ang), sa = sinf(ang), nx = -sa, ny = ca;
    PA_Vec2 tip = { x, y };
    PA_Vec2 b1 = { x - ca * len + nx * len * width, y - sa * len + ny * len * width };
    PA_Vec2 b2 = { x - ca * len - nx * len * width, y - sa * len - ny * len * width };
    PA_Vec2 m = { x - ca * len * 1.05f + nx * len * width * 0.25f, y - sa * len * 1.05f + ny * len * width * 0.25f };
    PA_Color lit = pa_hex(th->prop_a), shade = pa_hex(th->prop_b);
    /* A broad translucent plane behind gives the field its layered planes. */
    PA_Vec2 plane[4] = { { x - ca * len * 0.2f + nx * len * 0.55f, y - sa * len * 0.2f + ny * len * 0.55f },
                         { x - ca * len * 1.6f + nx * len * 0.65f, y - sa * len * 1.6f + ny * len * 0.65f },
                         { x - ca * len * 1.6f - nx * len * 0.05f, y - sa * len * 1.6f - ny * len * 0.05f },
                         { x - ca * len * 0.3f + nx * len * 0.15f, y - sa * len * 0.3f + ny * len * 0.15f } };
    pa_fill_poly(c, plane, 4, pa_alpha(pa_hex(0xC4D8F4), 0.55f));
    PA_Vec2 f1[3] = { tip, b1, m }, f2[3] = { tip, m, b2 };
    pa_fill_poly(c, f1, 3, lit);
    pa_fill_poly(c, f2, 3, shade);
    pa_line(c, tip.x, tip.y, b2.x, b2.y, 3.0f, pa_mix(shade, pa_hex(0x8E7A90), 0.6f));
}

static void ring_prop(PA_Canvas *c, float x, float y, float s, float tilt, const Theme *th) {
    PA_Color col = pa_hex(th->prop_a);
    PA_Color sh = pa_alpha(pa_shade(pa_hex(th->bg), -0.25f), 0.45f);
    (void)tilt;
    tube_arc(c, x, y, s, s * 0.55f, 0.0f, PA_TAU, col, sh);
}

static int prop_radius(const Prop *p, float U, float *r) {
    switch (p->kind) {
    case PROP_TUBE: *r = p->s * U * 1.35f; return 1;
    case PROP_LOG:  *r = p->s * U * 0.62f; return 1;
    case PROP_BUSH: *r = p->s * U * 1.25f; return 1;
    case PROP_ORB:  *r = p->s * U * 1.1f;  return 1;
    case PROP_RING: *r = p->s * U * 1.4f;  return 1;
    default:        *r = p->s * U * 0.25f; return 1;
    }
}

static void draw_props(PA_Canvas *c, const Theme *th, float U, float pause_x, float pause_y,
                       float pause_r) {
    float W = (float)c->w, H = (float)c->h;
    for (int i = 0; i < S.nprops; i++) {
        const Prop *p = &S.props[i];
        float x = p->x * W, y = p->y * H + sinf(S.time * 0.9f + p->phase) * H * 0.005f;
        float drift = sinf(S.time * 0.45f + p->phase) * 0.05f;
        float r;
        prop_radius(p, U, &r);
        /* Keep clear of the hub's pause button: nudge down until it is. */
        for (int k = 0; k < 40; k++) {
            float ddx = x - pause_x, ddy = y - pause_y;
            if (ddx * ddx + ddy * ddy >= (r + pause_r + 14.0f) * (r + pause_r + 14.0f)) break;
            y += 6.0f;
        }
        switch (p->kind) {
        case PROP_TUBE:
            tube_arc(c, x, y, p->s * U, p->s * U * 0.55f, p->ang + drift, p->sweep,
                     pa_hex(th->prop_a), pa_alpha(pa_shade(pa_hex(th->bg), -0.22f), 0.55f));
            break;
        case PROP_LOG:
            log_prop(c, x, y, p->s * U * 2.0f, p->s * U * 0.2f, p->ang + drift * 0.5f, th);
            break;
        case PROP_SHARD:
            shard_prop(c, x, y, p->s * U, p->ang + drift * 0.3f, p->sweep, th);
            break;
        case PROP_BUSH:
            bush_prop(c, x, y, p->s * U, p->sweep > 0.5f, th);
            break;
        case PROP_ORB: {
            float rr = p->s * U;
            pa_shadow(c, x + rr * 0.3f, y + rr * 1.3f, rr * 0.9f, rr * 0.3f, 0.18f);
            glossy_sphere(c, x, y, rr, p->sweep > 0.5f ? pa_hex(th->prop_b) : pa_hex(th->prop_a));
            break;
        }
        case PROP_RING:
            ring_prop(c, x, y, p->s * U, p->ang, th);
            break;
        }
    }
}

/* --------------------------------------------------------------- board */
static PA_Color face_col(const Theme *th, int painted, int side, PA_Color paint) {
    /* side 0: a south face toward the camera; 1: an east face, turned from
       the upper-left light; 2: a west face, turned into it. */
    static const float PAINTED[3] = { -0.25f, -0.40f, -0.15f };
    static const float BARE[3] = { 0.0f, -0.30f, 0.10f };
    if (painted) return pa_shade(paint, PAINTED[side]);
    return pa_shade(pa_hex(th->well_face), BARE[side]);
}

static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
static float hashf(uint32_t h) { return (float)(h & 0xFFFFFFu) / 16777216.0f; }

static PA_Color fresh_base(PA_Color paint, float age) {
    float k = pa_smooth(pa_clamp01(age / FRESH));
    return pa_mix(pa_mix(paint, PA_RGB(255, 255, 255), 0.5f), paint, k);
}

static void draw_floor(PA_Canvas *c, const Theme *th, PA_Color paint, float flash) {
    PA_Color bare = pa_hex(th->well);
    PA_Color solid = flash > 0.0f ? pa_mix(paint, PA_RGB(255, 255, 255), flash * 0.35f) : paint;
    const float e = 0.02f;
    for (int y = 0; y < S.h; y++)
        for (int x = 0; x < S.w; x++) {
            if (!walkable(x, y)) continue;
            PA_Color col = bare;
            if (S.painted[y][x]) col = S.age[y][x] < FRESH ? fresh_base(paint, S.age[y][x]) : solid;
            tile_quad(c, (float)x - e, (float)y - e, (float)x + 1 + e, (float)y + 1 + e, 0.0f, col);
        }

    /* The wet stripe from where the roll began up to the ball itself, so the
       paint is never ahead of or behind it. */
    if (S.rolling) {
        float x0 = (float)S.fx + 0.5f, y0 = (float)S.fy + 0.5f, x1 = S.bx + 0.5f, y1 = S.by + 0.5f;
        float ax0 = fminf(x0, x1), ax1 = fmaxf(x0, x1), ay0 = fminf(y0, y1), ay1 = fmaxf(y0, y1);
        if (S.dx) { ay0 -= 0.5f; ay1 += 0.5f; } else { ax0 -= 0.5f; ax1 += 0.5f; }
        tile_quad(c, ax0, ay0, ax1, ay1, 0.0f, fresh_base(paint, 0.0f));
    }

    /* Speckle: wet flecks of the paint, +/-12% lightness, a few pale ones,
       spilling a little past the tile. They fade as the paint settles. */
    float fs = P.tile / 26.0f;
    if (fs < 0.8f) fs = 0.8f;
    for (int y = 0; y < S.h; y++)
        for (int x = 0; x < S.w; x++) {
            if (!walkable(x, y) || !S.painted[y][x] || S.age[y][x] >= FRESH) continue;
            float a = 1.0f - pa_clamp01(S.age[y][x] / FRESH);
            for (int i = 0; i < 34; i++) {
                uint32_t h = hash3((uint32_t)x, (uint32_t)y, (uint32_t)i);
                float fu = -0.1f + 1.2f * hashf(h), fv = -0.1f + 1.2f * hashf(hash3(h, 7u, 1u));
                float sz = (2.0f + 2.0f * hashf(hash3(h, 3u, 5u))) * fs;
                float kind = hashf(hash3(h, 11u, 2u));
                PA_Color col = kind < 0.3f ? pa_shade(paint, 0.7f)
                             : pa_shade(paint, -0.12f + 0.24f * hashf(hash3(h, 13u, 9u)));
                PA_Vec2 p = pj((float)x + fu, (float)y + fv, 0.0f);
                pa_fill_rect(c, p.x - sz * 0.5f, p.y - sz * 0.5f, sz, sz, pa_alpha(col, a));
            }
        }

    /* A darker inner edge where paint meets a wall or bare floor, about two
       pixels wide, so the painted channel reads as a glossy filled groove. */
    {
        float bw = 2.2f / (P.tile > 1.0f ? P.tile : 1.0f);
        PA_Color edge = pa_shade(paint, -0.15f);
        for (int y = 0; y < S.h; y++)
            for (int x = 0; x < S.w; x++) {
                if (!walkable(x, y) || !S.painted[y][x] || S.age[y][x] < FRESH) continue;
                float x0 = (float)x, y0 = (float)y;
                if (!walkable(x, y - 1) || !S.painted[y - 1][x]) tile_quad(c, x0, y0, x0 + 1, y0 + bw, 0, edge);
                if (!walkable(x, y + 1) || !S.painted[y + 1][x]) tile_quad(c, x0, y0 + 1 - bw, x0 + 1, y0 + 1, 0, edge);
                if (!walkable(x - 1, y) || !S.painted[y][x - 1]) tile_quad(c, x0, y0, x0 + bw, y0 + 1, 0, edge);
                if (!walkable(x + 1, y) || !S.painted[y][x + 1]) tile_quad(c, x0 + 1 - bw, y0, x0 + 1, y0 + 1, 0, edge);
            }
    }

    /* Contact shadow at the foot of the walls, strongest under the far wall
       whose face the camera sees. */
    for (int y = 0; y < S.h; y++)
        for (int x = 0; x < S.w; x++) {
            if (!walkable(x, y)) continue;
            float fx0 = (float)x, fy0 = (float)y;
            if (is_wall(x, y - 1)) {
                PA_Vec2 a = pj(fx0, fy0, 0), b = pj(fx0 + 1, fy0, 0), d = pj(fx0 + 1, fy0 + 0.22f, 0), f = pj(fx0, fy0 + 0.22f, 0);
                PA_Paint g = pa_linear(0, a.y, 0, d.y);
                pa_stop(&g, 0.0f, PA_RGBA(10, 20, 40, 60));
                pa_stop(&g, 1.0f, PA_RGBA(10, 20, 40, 0));
                quad_paint(c, a, b, d, f, &g);
            }
            if (is_wall(x - 1, y)) {
                PA_Vec2 a = pj(fx0, fy0, 0), b = pj(fx0 + 0.16f, fy0, 0), d = pj(fx0 + 0.16f, fy0 + 1, 0), f = pj(fx0, fy0 + 1, 0);
                PA_Paint g = pa_linear(a.x, 0, b.x, 0);
                pa_stop(&g, 0.0f, PA_RGBA(10, 20, 40, 40));
                pa_stop(&g, 1.0f, PA_RGBA(10, 20, 40, 0));
                quad_paint(c, a, b, d, f, &g);
            }
            if (is_wall(x + 1, y)) {
                PA_Vec2 a = pj(fx0 + 1, fy0, 0), b = pj(fx0 + 0.84f, fy0, 0), d = pj(fx0 + 0.84f, fy0 + 1, 0), f = pj(fx0 + 1, fy0 + 1, 0);
                PA_Paint g = pa_linear(a.x, 0, b.x, 0);
                pa_stop(&g, 0.0f, PA_RGBA(10, 20, 40, 40));
                pa_stop(&g, 1.0f, PA_RGBA(10, 20, 40, 0));
                quad_paint(c, a, b, d, f, &g);
            }
        }
}

/* South faces in horizontal runs and side faces in vertical runs, so a long
   wall is one polygon and no seams show between its tiles. */
static void draw_faces(PA_Canvas *c, const Theme *th, PA_Color paint) {
    for (int y = 0; y < S.h - 1; y++) {
        int x = 0;
        while (x < S.w) {
            if (!(is_wall(x, y) && walkable(x, y + 1))) { x++; continue; }
            int p = S.painted[y + 1][x], x1 = x + 1;
            while (x1 < S.w && is_wall(x1, y) && walkable(x1, y + 1) && S.painted[y + 1][x1] == p) x1++;
            float a = (float)x - 0.01f, b = (float)x1 + 0.01f, yy = (float)(y + 1);
            face_quad(c, pj(a, yy, WALL_H), pj(b, yy, WALL_H), pj(b, yy, 0), pj(a, yy, 0),
                      face_col(th, p, 0, paint));
            x = x1;
        }
    }
    for (int x = 0; x < S.w; x++)
        for (int side = 0; side < 2; side++) {
            /* An east face is seen left of the centre line, a west one right of it. */
            int nx = side ? x - 1 : x + 1;
            float fxp = side ? (float)x : (float)(x + 1);
            if ((side == 0 && fxp > P.hw) || (side == 1 && fxp < P.hw)) continue;
            int y = 0;
            while (y < S.h) {
                if (!(is_wall(x, y) && walkable(nx, y))) { y++; continue; }
                int p = S.painted[y][nx], y1 = y + 1;
                while (y1 < S.h && is_wall(x, y1) && walkable(nx, y1) && S.painted[y1][nx] == p) y1++;
                float a = (float)y - 0.01f, b = (float)y1 + 0.01f;
                face_quad(c, pj(fxp, a, WALL_H), pj(fxp, b, WALL_H), pj(fxp, b, 0), pj(fxp, a, 0),
                          face_col(th, p, side ? 2 : 1, paint));
                y = y1;
            }
        }
}

static void draw_tops(PA_Canvas *c, PA_Color top) {
    const float e = 0.02f;
    for (int y = 0; y < S.h; y++) {
        int x = 0;
        while (x < S.w) {
            if (!is_block(x, y)) { x++; continue; }
            int x1 = x + 1;
            while (x1 < S.w && is_block(x1, y)) x1++;
            tile_quad(c, (float)x - e, (float)y - e, (float)x1 + e, (float)y + 1 + e, WALL_H, top);
            x = x1;
        }
    }
}

static void draw_drops(PA_Canvas *c, int high) {
    float fs = P.tile / 30.0f;
    for (int i = 0; i < MAX_DROPS; i++) {
        const Drop *d = &S.drops[i];
        if (d->life <= 0.0f) continue;
        int is_high = d->h > WALL_H * 0.85f;
        if (is_high != high) continue;
        float a = 1.0f - pa_clamp01((d->age - d->life * 0.6f) / (d->life * 0.4f));
        PA_Vec2 p = pj(d->u, d->v, d->h);
        float sz = d->size * P.tile * 0.5f + 1.5f * fs;
        pa_fill_rect(c, p.x - sz * 0.5f, p.y - sz * 0.5f, sz, sz, pa_alpha(d->col, a));
    }
}

static void draw_ball(PA_Canvas *c, PA_Color paint) {
    float gx = S.bx + 0.5f, gy = S.by + 0.5f;
    /* On impact: flattened to 0.8 x 1.2 for 80 ms, then springs back. */
    float sq = 0.0f;
    if (S.squash_t < 0.08f) sq = S.squash_amp;
    else if (S.squash_t < 1.0f) {
        float t = S.squash_t - 0.08f;
        sq = S.squash_amp * expf(-t * 14.0f) * cosf(t * 30.0f);
    }
    float along = 1.0f - 0.2f * sq, across = 1.0f + 0.2f * sq;
    if (S.rolling) { along = 1.06f; across = 0.97f; }
    PA_Vec2 foot = pj(gx, gy, 0.0f), mid = pj(gx, gy, BALL_R), edge = pj(gx + BALL_R, gy, BALL_R);
    float pr = edge.x - mid.x;
    float rx = pr, ry = pr;
    int axis = S.rolling ? (S.dx ? 0 : 1) : S.squash_axis;
    if (axis == 0) { rx *= along; ry *= across; } else { ry *= along; rx *= across; }
    /* Pressed flat against the wall it hit. */
    if (!S.rolling && sq > 0.0f) {
        mid.x += (float)S.last_dx * pr * (1.0f - along);
        mid.y += (float)S.last_dy * pr * (1.0f - along);
    }
    mid.y += pr - ry;

    pa_shadow(c, foot.x + pr * 0.12f, foot.y + pr * 0.08f, rx * 1.0f, ry * 0.5f, 0.32f);

    PA_Paint g = pa_radial(mid.x - rx * 0.32f, mid.y - ry * 0.38f, pr * 0.05f, pr * 1.3f);
    pa_stop(&g, 0.0f, pa_shade(paint, 0.55f));
    pa_stop(&g, 0.30f, pa_shade(paint, 0.12f));
    pa_stop(&g, 0.72f, paint);
    pa_stop(&g, 1.0f, pa_shade(paint, -0.45f));
    pa_fill_ellipse_paint(c, mid.x, mid.y, rx, ry, &g);
    /* Rim light along the lower right, then the specular highlight. */
    PA_Paint rim = pa_radial(mid.x + rx * 0.35f, mid.y + ry * 0.45f, pr * 0.0f, pr * 0.6f);
    pa_stop(&rim, 0.0f, pa_alpha(pa_shade(paint, 0.35f), 0.55f));
    pa_stop(&rim, 1.0f, pa_alpha(pa_shade(paint, 0.35f), 0.0f));
    pa_fill_ellipse_paint(c, mid.x + rx * 0.3f, mid.y + ry * 0.4f, rx * 0.5f, ry * 0.4f, &rim);
    pa_fill_ellipse(c, mid.x - rx * 0.36f, mid.y - ry * 0.40f, rx * 0.30f, ry * 0.20f, PA_RGBA(255, 255, 255, 90));
    pa_fill_ellipse(c, mid.x - rx * 0.36f, mid.y - ry * 0.40f, rx * 0.25f, ry * 0.21f, PA_RGBA(255, 255, 255, 245));
}

/** A slab cut to a silhouette, or a floating path network: shadow on the
    backdrop, then the outer faces of the solid tiles, three-tone lit. Its
    tops are drawn later with the walls. */
static void draw_cut_slab(PA_Canvas *c, const Theme *th) {
    PA_Color sh = pa_mix(pa_hex(th->bg), PA_RGB(0, 0, 0), 0.20f);
    float o = P.shadow;
    for (int y = 0; y < S.h; y++) {
        int x = 0;
        while (x < S.w) {
            if (!is_solid(x, y)) { x++; continue; }
            int x1 = x + 1;
            while (x1 < S.w && is_solid(x1, y)) x1++;
            PA_Vec2 q[4] = { pj((float)x, (float)y, -SLAB_T), pj((float)x1, (float)y, -SLAB_T),
                             pj((float)x1, (float)y + 1.02f, -SLAB_T), pj((float)x, (float)y + 1.02f, -SLAB_T) };
            for (int k = 0; k < 4; k++) { q[k].x += o; q[k].y += o; }
            pa_fill_poly(c, q, 4, sh);
            x = x1;
        }
    }
    PA_Color base = pa_hex(th->slab_front);
    for (int y = 0; y < S.h; y++)
        for (int x = 0; x < S.w; x++) {
            if (!is_solid(x, y)) continue;
            float x0 = (float)x, y0 = (float)y, x1 = x0 + 1.0f, y1 = y0 + 1.0f;
            if (!is_solid(x - 1, y) && x0 >= P.hw)
                face_quad(c, pj(x0, y0, WALL_H), pj(x0, y1, WALL_H), pj(x0, y1, -SLAB_T), pj(x0, y0, -SLAB_T),
                          pa_shade(base, 0.10f));
            if (!is_solid(x + 1, y) && x1 <= P.hw)
                face_quad(c, pj(x1, y0, WALL_H), pj(x1, y1, WALL_H), pj(x1, y1, -SLAB_T), pj(x1, y0, -SLAB_T),
                          pa_shade(base, -0.30f));
        }
    /* Front faces in horizontal runs, so no seams show between tiles. */
    for (int y = 0; y < S.h; y++) {
        int x = 0;
        while (x < S.w) {
            if (!(is_solid(x, y) && !is_solid(x, y + 1))) { x++; continue; }
            int x1 = x + 1;
            while (x1 < S.w && is_solid(x1, y) && !is_solid(x1, y + 1)) x1++;
            float a = (float)x - 0.01f, b = (float)x1 + 0.01f, yy = (float)(y + 1);
            face_quad(c, pj(a, yy, WALL_H), pj(b, yy, WALL_H), pj(b, yy, -SLAB_T), pj(a, yy, -SLAB_T), base);
            x = x1;
        }
    }
}

static void draw_board(PA_Canvas *c, const Theme *th, PA_Color paint, float flash) {
    PA_Color top = pa_hex(th->slab);
    float W = (float)S.w, H = (float)S.h, rw = P.rim;
    if (S.shape != SHAPE_RECT) {
        draw_cut_slab(c, th);
    } else {
        {
            /* Soft drop shadow, 30% black, offset about 0.02 of the screen width. */
            PA_Vec2 q[4] = { pj(-rw, -rw, -SLAB_T), pj(W + rw, -rw, -SLAB_T),
                             pj(W + rw, H + rw, -SLAB_T), pj(-rw, H + rw, -SLAB_T) };
            for (int k = 0; k < 3; k++) {
                float o = P.shadow, g = P.shadow * 0.35f * (float)(2 - k);
                PA_Vec2 sq[4] = { { q[0].x + o - g, q[0].y + o - g }, { q[1].x + o + g, q[1].y + o - g },
                                  { q[2].x + o + g, q[2].y + o + g }, { q[3].x + o - g, q[3].y + o + g } };
                pa_fill_poly(c, sq, 4, PA_RGBA(0, 0, 0, 26));
            }
        }
        {
            /* Front edge of the slab under the rim, then the rim, then the top. */
            PA_Color rim = pa_hex(th->rim);
            PA_Vec2 a = pj(-rw, H + rw, WALL_H), b = pj(W + rw, H + rw, WALL_H);
            PA_Vec2 d = pj(W + rw, H + rw, -SLAB_T), e = pj(-rw, H + rw, -SLAB_T);
            PA_Paint fr = pa_linear(0, a.y, 0, d.y);
            pa_stop(&fr, 0.0f, pa_shade(rim, -0.22f));
            pa_stop(&fr, 1.0f, pa_shade(rim, -0.38f));
            quad_paint(c, a, b, d, e, &fr);
            tile_quad(c, -rw, -rw, W + rw, H + rw, WALL_H, rim);
            /* A fine lit edge where the rim meets the top. */
            tile_quad(c, -rw * 0.25f, -rw * 0.25f, W + rw * 0.25f, H + rw * 0.25f, WALL_H, pa_shade(rim, 0.25f));
            tile_quad(c, 0, 0, W, H, WALL_H, top);
        }
    }
    draw_floor(c, th, paint, flash);

    /* Walls cast their shadow into the corridors, light from the upper left:
       each row of wall is shifted right and down on the floor. Rows are drawn
       as runs so neighbouring tiles never double the darkness. */
    for (int y = 0; y < S.h; y++) {
        int x = 0;
        while (x < S.w) {
            if (!is_block(x, y)) { x++; continue; }
            int x1 = x + 1;
            while (x1 < S.w && is_block(x1, y)) x1++;
            float ox = 0.34f, oy = 0.30f;
            PA_Vec2 a = pj((float)x + ox, (float)y + oy, 0), b = pj((float)x1 + ox, (float)y + oy, 0);
            PA_Vec2 d = pj((float)x1 + ox, (float)y + 1 + oy, 0), e = pj((float)x + ox, (float)y + 1 + oy, 0);
            quad(c, a, b, d, e, PA_RGBA(12, 22, 48, 52));
            x = x1;
        }
    }

    /* Ball contact shadow sits on the floor, under the walls' faces. */
    draw_faces(c, th, paint);
    draw_drops(c, 0);
    draw_tops(c, top);
    draw_ball(c, paint);
    draw_drops(c, 1);
    for (int i = 0; i < MAX_SPARKS; i++) {
        const Spark *k = &S.sparks[i];
        if (k->life <= 0.0f) continue;
        float t = k->age / k->life, a = 1.0f - t;
        PA_Vec2 p = pj(k->u, k->v, 0.05f);
        float r = k->size * P.tile * sinf(PA_PI * pa_clamp01(t * 1.6f + 0.15f)) * 0.75f, w = r * 0.22f;
        PA_Vec2 v[4] = { { p.x, p.y - r }, { p.x + w, p.y }, { p.x, p.y + r }, { p.x - w, p.y } };
        PA_Vec2 h[4] = { { p.x - r, p.y }, { p.x, p.y - w }, { p.x + r, p.y }, { p.x, p.y + w } };
        PA_Color col = PA_RGBA(255, 255, 255, (int)(235.0f * a));
        pa_fill_poly(c, v, 4, col);
        pa_fill_poly(c, h, 4, col);
        pa_fill_circle(c, p.x, p.y, w * 1.3f, PA_RGBA(255, 255, 255, (int)(255.0f * a)));
    }
}

/* ------------------------------------------------------------------ HUD */
static void level_badge(PA_Canvas *c, float x, float y, float r, int n, PA_Color fill, PA_Color ink,
                        PA_Color text) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", n);
    pa_fill_circle(c, x, y + r * 0.12f, r, pa_alpha(pa_shade(ink, -0.2f), 0.35f));
    pa_fill_circle(c, x, y, r, PA_RGB(255, 255, 255));
    pa_fill_circle(c, x, y, r * 0.84f, fill);
    float ts = r * (n >= 100 ? 0.62f : 0.8f);
    pa_text_bold(c, buf, x, y - ts * 0.5f, ts, text, ink, PA_ALIGN_CENTER, 1.0f, 1.2f);
}

static void draw_hud(PA_Canvas *c, const Theme *th, PA_Color paint, float U, float label_y,
                     float label_size) {
    float W = (float)c->w;
    PA_Color ink = pa_hex(th->ink);
    char buf[32];
    snprintf(buf, sizeof(buf), "LEVEL %d", S.level + 1);
    pa_text_bold(c, buf, W * 0.5f, label_y - label_size * 0.5f, label_size, PA_RGB(255, 255, 255),
                 ink, PA_ALIGN_CENTER, label_size * 0.10f, 2.1f);

    /* Progress: current level, a bar of the board's coverage, next level. */
    float by = label_y + label_size * 0.95f + U * 0.03f;
    float bw = U * 0.44f, bh = U * 0.026f, r = U * 0.034f;
    float bx = W * 0.5f - bw * 0.5f;
    float prog = (float)S.done / (float)(S.total > 0 ? S.total : 1);
    pa_round_rect(c, bx, by - bh * 0.5f + bh * 0.2f, bw, bh, bh * 0.5f, pa_alpha(pa_shade(ink, -0.2f), 0.25f));
    pa_round_rect(c, bx, by - bh * 0.5f, bw, bh, bh * 0.5f, pa_alpha(PA_RGB(255, 255, 255), 0.85f));
    if (prog > 0.0f) {
        float fw = (bw - 4.0f) * prog;
        if (fw < bh - 4.0f) fw = bh - 4.0f;
        pa_round_rect(c, bx + 2.0f, by - bh * 0.5f + 2.0f, fw, bh - 4.0f, (bh - 4.0f) * 0.5f, paint);
    }
    level_badge(c, bx - r * 0.6f, by, r, S.level + 1, paint, ink, PA_RGB(255, 255, 255));
    level_badge(c, bx + bw + r * 0.6f, by, r, S.level + 2,
                prog >= 1.0f ? paint : pa_hex(0xEEF1F6), ink,
                prog >= 1.0f ? PA_RGB(255, 255, 255) : pa_shade(ink, 0.25f));
}

static void draw_hint(PA_Canvas *c, const Theme *th, float U) {
    float W = (float)c->w, H = (float)c->h;
    float y = H - U * 0.035f;
    PA_Color ink = pa_hex(th->ink);
    float t = pa_wrapf(S.time * 0.8f, 1.0f);
    float k = pa_smooth(pa_clamp01(t / 0.7f));
    float fx = W * 0.5f + (k - 0.5f) * U * 0.36f, fy = y - U * 0.075f;
    float a = t < 0.75f ? 1.0f : 1.0f - (t - 0.75f) / 0.25f;
    /* A fingertip swiping, with the trail it leaves. */
    for (int i = 0; i < 8; i++) {
        float tk = pa_clamp01(k - (float)i * 0.04f);
        float tx = W * 0.5f + (tk - 0.5f) * U * 0.36f;
        pa_fill_circle(c, tx, fy, U * (0.022f - (float)i * 0.002f), pa_alpha(PA_RGB(255, 255, 255), a * 0.25f));
    }
    pa_fill_circle(c, fx, fy + 3.0f, U * 0.03f, pa_alpha(pa_shade(ink, -0.2f), a * 0.3f));
    pa_fill_circle(c, fx, fy, U * 0.03f, pa_alpha(PA_RGB(255, 255, 255), a));
    pa_text_bold(c, "SWIPE TO ROLL", W * 0.5f, y - U * 0.02f, U * 0.042f, PA_RGB(255, 255, 255),
                 ink, PA_ALIGN_CENTER, U * 0.006f, 1.6f);
}

/* ------------------------------------------------------- level complete */
static void splat_blob(PA_Canvas *c, float x, float y, float R, uint32_t seed, PA_Color col) {
    PA_Vec2 pts[72];
    float bump[9];
    float bang[9];
    for (int i = 0; i < 9; i++) {
        bump[i] = 0.25f + 0.55f * hashf(hash3(seed, (uint32_t)i, 1u));
        bang[i] = PA_TAU * hashf(hash3(seed, (uint32_t)i, 2u));
    }
    for (int i = 0; i < 72; i++) {
        float a = (float)i * PA_TAU / 72.0f;
        float r = 1.0f + 0.07f * sinf(a * 3.0f + (float)seed) + 0.05f * sinf(a * 7.0f);
        for (int k = 0; k < 9; k++) {
            float d = a - bang[k];
            d = d - PA_TAU * floorf((d + PA_PI) / PA_TAU);
            r += bump[k] * expf(-d * d / 0.006f);
        }
        pts[i].x = x + cosf(a) * R * r;
        pts[i].y = y + sinf(a) * R * r;
    }
    pa_fill_poly(c, pts, 72, col);
    for (int k = 0; k < 9; k++) {
        float rr = R * (1.25f + bump[k] * 1.1f);
        pa_fill_circle(c, x + cosf(bang[k]) * rr, y + sinf(bang[k]) * rr, R * (0.06f + 0.06f * bump[k]), col);
    }
}

/** A brush-stroke banner: a rotated strip with ragged long edges. */
static void brush_banner(PA_Canvas *c, float cx, float cy, float w, float h, float ang, uint32_t seed,
                         PA_Color col) {
    enum { N = 14 };
    PA_Vec2 pts[N * 2 + 2];
    float ca = cosf(ang), sa = sinf(ang);
    int n = 0;
    for (int i = 0; i <= N; i++) {
        float t = (float)i / (float)N, lx = (t - 0.5f) * w;
        float ly = -h * 0.5f - h * 0.08f * hashf(hash3(seed, (uint32_t)i, 3u));
        pts[n].x = cx + lx * ca - ly * sa; pts[n].y = cy + lx * sa + ly * ca; n++;
    }
    for (int i = N; i >= 0; i--) {
        float t = (float)i / (float)N, lx = (t - 0.5f) * w;
        float ly = h * 0.5f + h * 0.10f * hashf(hash3(seed, (uint32_t)i, 4u));
        pts[n].x = cx + lx * ca - ly * sa; pts[n].y = cy + lx * sa + ly * ca; n++;
    }
    pa_fill_poly(c, pts, n, col);
    /* Bristle flecks off the ends. */
    for (int i = 0; i < 8; i++) {
        uint32_t h2 = hash3(seed, (uint32_t)i, 9u);
        float side = (i & 1) ? 1.0f : -1.0f;
        float lx = side * (w * 0.5f + h * 0.1f * hashf(h2)), ly = (hashf(hash3(h2, 1u, 1u)) - 0.5f) * h;
        pa_fill_circle(c, cx + lx * ca - ly * sa, cy + lx * sa + ly * ca, h * (0.04f + 0.05f * hashf(h2)), col);
    }
}

static void draw_confetti(PA_Canvas *c, int from, int to) {
    float W = (float)c->w;
    for (int i = from; i < to; i++) {
        const Bit *b = &S.bits[i];
        float px = b->x * W, py = b->y * W, s = b->size * W;
        if (py < -s * 2.0f || py > (float)c->h + s * 2.0f) continue;
        float fl = cosf(b->flip);
        float ca = cosf(b->rot), sa = sinf(b->rot);
        float hx = s * 0.5f * (0.25f + 0.75f * fabsf(fl)), hy = s * 0.5f;
        PA_Vec2 q[4] = { { px - hx * ca + hy * sa, py - hx * sa - hy * ca },
                         { px + hx * ca + hy * sa, py + hx * sa - hy * ca },
                         { px + hx * ca - hy * sa, py + hx * sa + hy * ca },
                         { px - hx * ca - hy * sa, py - hx * sa + hy * ca } };
        pa_fill_poly(c, q, 4, fl < 0.0f ? pa_shade(b->col, -0.18f) : b->col);
    }
}

static void draw_celebration(PA_Canvas *c, PA_Color paint, float U) {
    float W = (float)c->w, H = (float)c->h;
    float t = S.clear_t - 0.12f;
    draw_confetti(c, CONFETTI / 2, CONFETTI);
    if (t <= 0.0f) return;
    /* 0 -> 1.15 -> 1.0 over 0.35 s. */
    float sc = t < 0.22f ? 1.15f * ease_out_cubic(t / 0.22f)
             : 1.15f - 0.15f * pa_smooth(pa_clamp01((t - 0.22f) / 0.13f));
    float out = S.clear_t > 2.6f ? pa_clamp01((S.clear_t - 2.6f) / 0.3f) : 0.0f;
    sc *= 1.0f - out;
    if (sc <= 0.01f) return;
    PA_Color dark = pa_shade(paint, -0.30f), light = pa_shade(paint, 0.06f);
    PA_Color ink = pa_shade(paint, -0.55f);
    float cx = W * 0.5f, cy = H * 0.2f;
    float ang = -0.14f;                        /* -8 degrees: rising to the right */
    float ca = cosf(ang), sa = sinf(ang);
    float line = U * 0.12f * sc;
    static const char *PRAISE[3] = { "PERFECT!", "GREAT!", "NICE!" };

    /* Two-tone splat: a dark burst behind, the lighter paint over it. */
    for (int tone = 0; tone < 2; tone++) {
        float k = tone ? 0.82f : 1.0f;
        PA_Color col = tone ? light : dark;
        float ox = tone ? -U * 0.01f : 0.0f, oy = tone ? -U * 0.012f : 0.0f;
        splat_blob(c, cx - W * 0.25f * sc + ox, cy + W * 0.035f * sc + oy, W * 0.15f * sc * k, 3u + (uint32_t)S.level, col);
        splat_blob(c, cx + W * 0.24f * sc + ox, cy - W * 0.03f * sc + oy, W * 0.14f * sc * k, 17u + (uint32_t)S.level, col);
        splat_blob(c, cx + ox, cy + oy, W * 0.17f * sc * k, 29u + (uint32_t)S.level, col);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "LEVEL %d", S.level + 1);
    const char *lines[3] = { buf, "COMPLETE", PRAISE[S.praise] };
    float sizes[3] = { U * 0.072f, U * 0.084f, U * 0.062f };
    float widths[3] = { 0.66f, 0.80f, 0.56f };
    for (int i = 0; i < 3; i++) {
        float off = ((float)i - 1.0f) * line;
        float lx = cx - sa * off, ly = cy + ca * off;
        float ts = sizes[i] * sc;
        brush_banner(c, lx + U * 0.006f, ly + U * 0.01f, W * widths[i] * sc, ts * 1.8f, ang, 40u + (uint32_t)i, dark);
        brush_banner(c, lx, ly, W * widths[i] * sc, ts * 1.7f, ang, 50u + (uint32_t)i, light);
        /* A lit band along the top edge so the strip reads as a thick stroke. */
        brush_banner(c, lx + sa * ts * 0.52f, ly - ca * ts * 0.52f, W * widths[i] * sc * 0.96f, ts * 0.32f, ang,
                     60u + (uint32_t)i, pa_shade(light, 0.20f));
    }
    for (int i = 0; i < 3; i++) {
        float off = ((float)i - 1.0f) * line;
        float lx = cx - sa * off, ly = cy + ca * off;
        float ts = sizes[i] * sc;
        pa_text_bold(c, lines[i], lx, ly - ts * 0.5f, ts, PA_RGB(255, 255, 255), ink,
                     PA_ALIGN_CENTER, ts * 0.08f, 1.4f);
    }
    draw_confetti(c, 0, CONFETTI / 2);
}

/* --------------------------------------------------------------- render */
static void splat_render(PA_Canvas *c) {
    const Theme *th = &THEMES[S.theme];
    PA_Color paint = paint_col();
    float W = (float)c->w, H = (float)c->h;
    float U = W < H * 0.5f ? W : H * 0.5f;
    S.view_w = W; S.view_h = H;

    float shake = S.shake > 0.0f ? S.shake * U * 0.004f : 0.0f;
    float slide = (1.0f - ease_out_cubic(S.intro_t / 0.5f)) * H * 0.9f;
    if (S.cleared && S.clear_t > 2.55f) {
        float k = pa_clamp01((S.clear_t - 2.55f) / 0.45f);
        slide = -k * k * H * 0.9f;
    }

    pa_clear(c, pa_hex(th->bg));

    float label_size = U * 0.085f;
    float label_y = H * 0.058f;
    if (label_y < label_size * 0.9f) label_y = label_size * 0.9f;
    float pause_r = U * 0.048f;
    float pause_x = W - pause_r - U * 0.045f, pause_y = label_y;

    /* The board takes the screen from about 22% down to the bottom margin. */
    float top = fmaxf(H * 0.16f, label_y + label_size * 1.6f + U * 0.06f);
    float bottom = H * 0.935f;
    fit_camera(c, top, bottom, slide);
    P.cy += sinf(S.time * 90.0f) * shake;
    if (S.cleared) {
        /* The finished board pulses to 1.05 and back. */
        float pulse = 1.0f + 0.05f * sinf(PA_PI * pa_clamp01(S.clear_t / 0.4f));
        PA_Vec2 mid = pj((float)S.w * 0.5f, (float)S.h * 0.5f, 0.0f);
        P.k *= pulse;
        P.cx = mid.x + (P.cx - mid.x) * pulse;
        P.cy = mid.y + (P.cy - mid.y) * pulse;
        P.tile *= pulse;
    }

    draw_props(c, th, U, pause_x, pause_y, pause_r);

    float flash = S.cleared ? expf(-S.clear_t * 3.0f) : 0.0f;
    draw_board(c, th, paint, flash);

    if (S.cleared) {
        pa_hub_hide_pause();
        draw_celebration(c, paint, U);
        return;
    }
    pa_hub_pause_anchor(pause_x, pause_y, pause_r);
    draw_hud(c, th, paint, U, label_y, label_size);
    if (S.moves == 0 && S.level < 3 && !S.rolling) draw_hint(c, th, U);
}

/* ---------------------------------------------------------------- thumb */
static void splat_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    pa_fill_rect(c, x, y, w, h, pa_hex(0x6A9CF4));
    float m = w < h ? w : h;
    tube_arc(c, x + w * 0.12f, y + h * 0.12f, m * 0.16f, m * 0.08f, 0.2f, 1.9f, pa_hex(0x4FDCFF),
             PA_RGBA(40, 70, 160, 90));
    tube_arc(c, x + w * 0.9f, y + h * 0.9f, m * 0.16f, m * 0.08f, 3.4f, 1.9f, pa_hex(0x4FDCFF),
             PA_RGBA(40, 70, 160, 90));
    static const char *G[7] = { "#######", "#.....#", "#.###.#", "#.#...#", "#.#.###", "#.....#", "#######" };
    /* The ball's path around the board, in tiles. */
    static const int PATH[][2] = { {1,5},{1,1},{5,1},{5,3},{3,3},{3,5},{5,5} };
    int n = 7;
    float cell = m * 0.7f / (float)n;
    float ox = x + (w - cell * (float)n) * 0.5f, oy = y + (h - cell * (float)n) * 0.5f;
    pa_fill_rect(c, ox + cell * 0.1f, oy + cell * 0.25f, cell * (float)n, cell * (float)n, PA_RGBA(20, 40, 120, 60));
    pa_fill_rect(c, ox, oy + cell * (float)n - 1.0f, cell * (float)n, cell * 0.3f, pa_hex(0xD3DDEB));
    pa_fill_rect(c, ox, oy, cell * (float)n, cell * (float)n, PA_RGB(255, 255, 255));
    float prog = pa_wrapf(t * 0.25f, 1.0f) * 6.0f;
    int seg = (int)prog;
    float f = prog - (float)seg;
    float bxp = (float)PATH[seg][0] + ((float)PATH[seg + 1][0] - (float)PATH[seg][0]) * f;
    float byp = (float)PATH[seg][1] + ((float)PATH[seg + 1][1] - (float)PATH[seg][1]) * f;
    for (int gy = 0; gy < n; gy++)
        for (int gx = 0; gx < n; gx++) {
            if (G[gy][gx] == '#') continue;
            int painted = 0;
            for (int s = 0; s <= seg && !painted; s++) {
                float ax = (float)PATH[s][0], ay = (float)PATH[s][1];
                float ex = s < seg ? (float)PATH[s + 1][0] : bxp, ey = s < seg ? (float)PATH[s + 1][1] : byp;
                if (fminf(ax, ex) - 0.01f <= (float)gx && (float)gx <= fmaxf(ax, ex) + 0.01f &&
                    fminf(ay, ey) - 0.01f <= (float)gy && (float)gy <= fmaxf(ay, ey) + 0.01f) painted = 1;
            }
            float px = ox + (float)gx * cell, py = oy + (float)gy * cell;
            pa_fill_rect(c, px, py, cell + 0.5f, cell + 0.5f, painted ? pa_hex(0x7A1CF2) : pa_hex(0xA9D3DC));
            if (G[gy - 1][gx] == '#')
                pa_fill_rect(c, px, py, cell + 0.5f, cell * 0.22f, painted ? pa_hex(0x4E0FA8) : pa_hex(0x7FA2B0));
        }
    float bcx = ox + (bxp + 0.5f) * cell, bcy = oy + (byp + 0.5f) * cell;
    glossy_sphere(c, bcx, bcy, cell * 0.36f, pa_hex(0x7A1CF2));
}

const PA_Game PA_GAME_SPLAT = {
    "splat", "Roller Splat", "Puzzle",
    "Swipe the ball, it rolls until it hits a wall. Paint every tile in the maze.",
    PA_RGB(122, 28, 242),
    splat_start, splat_stop, splat_update, splat_render, splat_thumb
};
