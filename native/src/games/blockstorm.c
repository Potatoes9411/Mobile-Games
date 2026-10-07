/* ===========================================================================
   BLOCK STORM - native port (reference: Block Blast!, classic mode)
   Eight by eight board, three pieces in the tray, drag one onto the grid.
   Fill a row or a column and it clears. No gravity, no timer: the only thing
   that ends a run is having nowhere left to put any of the three.

   Everything the reference does to make a placement feel good is here: the
   piece lifts off the tray and grows to board scale, a ghost shows where it
   lands and the lines it would finish light up in its colour, a clear bursts
   into a beam and shards with a floating score, praise and combo callouts
   pop over the board, the score counts up, and running out of room greys the
   board, says so, and hands over to a results card with the best score.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define N          8
#define TRAY       3
#define MAX_CELLS  9
#define MAX_PARTS  900
#define MAX_BEAMS  16
#define MAX_FLOATS 6

/* Gap between the finger and the bottom of a lifted piece, in board cells.
   On a phone the thumb covers the drop site otherwise. */
#define LIFT_GAP   1.15f

/* ------------------------------------------------------------------ shapes */
typedef struct {
    int w, h, count;
    signed char cx[MAX_CELLS], cy[MAX_CELLS];
    int weight;
} Shape;

typedef struct { const char *mask; int weight; } ShapeSrc;

/* The reference's piece set: lines, squares, rectangles, corners, L, T, S/Z
   and the two diagonals. Rows are separated by '|'. */
static const ShapeSrc SHAPE_SRC[] = {
    { "#", 2 },
    { "##", 3 }, { "#|#", 3 },
    { "###", 5 }, { "#|#|#", 5 },
    { "####", 4 }, { "#|#|#|#", 4 },
    { "#####", 2 }, { "#|#|#|#|#", 2 },
    { "##|##", 6 },
    { "###|###|###", 2 },
    { "###|###", 3 }, { "##|##|##", 3 },
    { "##|#.", 3 }, { "##|.#", 3 }, { "#.|##", 3 }, { ".#|##", 3 },
    { "#..|#..|###", 2 }, { "..#|..#|###", 2 }, { "###|#..|#..", 2 }, { "###|..#|..#", 2 },
    { "###|.#.", 3 }, { ".#.|###", 3 }, { "#.|##|#.", 3 }, { ".#|##|.#", 3 },
    { "##.|.##", 2 }, { ".##|##.", 2 }, { "#.|##|.#", 2 }, { ".#|##|#.", 2 },
    { "#.|#.|##", 2 }, { ".#|.#|##", 2 }, { "##|#.|#.", 2 }, { "##|.#|.#", 2 },
    { "###|#..", 2 }, { "###|..#", 2 }, { "#..|###", 2 }, { "..#|###", 2 },
    { "#.|.#", 1 }, { ".#|#.", 1 }
};
#define SHAPE_COUNT ((int)(sizeof(SHAPE_SRC) / sizeof(SHAPE_SRC[0])))
static Shape SHAPES[SHAPE_COUNT];
static int   g_shapes_ready;

static void build_shapes(void) {
    for (int i = 0; i < SHAPE_COUNT; i++) {
        Shape *s = &SHAPES[i];
        memset(s, 0, sizeof(*s));
        int x = 0, y = 0;
        for (const char *p = SHAPE_SRC[i].mask; *p; p++) {
            if (*p == '|') { y++; x = 0; continue; }
            if (*p == '#' && s->count < MAX_CELLS) {
                s->cx[s->count] = (signed char)x;
                s->cy[s->count] = (signed char)y;
                s->count++;
            }
            x++;
            if (x > s->w) s->w = x;
        }
        s->h = y + 1;
        s->weight = SHAPE_SRC[i].weight;
    }
    g_shapes_ready = 1;
}

static int find_shape(const char *mask) {
    for (int i = 0; i < SHAPE_COUNT; i++) if (!strcmp(SHAPE_SRC[i].mask, mask)) return i;
    return 0;
}

/* ----------------------------------------------------------------- palette */
/* The reference's seven block hues, fully saturated: the board is read as
   pattern, and two hues a few points apart would read as one. */
static const uint32_t TINTS[] = {
    0xE83A3A, 0xFF7A1A, 0xFFC21A, 0x3CC84A, 0x2EC8E8, 0x3A6CF0, 0xA04AE0
};
enum { T_RED, T_ORANGE, T_YELLOW, T_GREEN, T_CYAN, T_BLUE, T_PURPLE };
#define TINT_COUNT ((int)(sizeof(TINTS) / sizeof(TINTS[0])))

#define BG_TOP     0x4257B2
#define BG_BOT     0x5774CE
#define BOARD_NAVY 0x232A55
#define GRID_LINE  0x323C6E
#define GOLD       0xFFC83A
#define INK        0x1C2350

/* ------------------------------------------------------------------- state */
enum { P_SHARD, P_SPARK, P_CONFETTI, P_DOT };

typedef struct {
    float x, y, vx, vy, life, max, size, rot, vr;
    PA_Color col;
    int kind;
} Part;

typedef struct { int row, index, tint; float t; } Beam;
typedef struct { float x, y, t; int tint; char txt[16]; } FloatText;

typedef struct { int shape, tint; float pop; } Slot;

typedef struct {
    int   phase, slot, gx, gy, down;
    float t, think, x, y, sx, sy, ex, ey;
} Bot;

typedef struct {
    PA_Rng rand;
    int    cell[N * N];        /* -1 empty, else tint index */
    float  land[N * N];        /* placement settle, 1 -> 0 */
    float  clr[N * N];         /* clear burst clock; < -5 idle, < 0 waiting */
    int    clr_tint[N * N];
    float  grey[N * N];        /* game-over grey-out 0..1 */
    Slot   tray[TRAY];
    int    score, best_start, passed_best, combo, since_clear, placed;
    float  shown, punch;
    int    drag;               /* tray slot being dragged, -1 when idle */
    float  fx, fy, lift, grab_x, grab_y;
    int    ret_slot;           /* piece flying back to the tray */
    float  ret_t, ret_x, ret_y, ret_cell;
    float  shake;
    char   praise[24];
    float  praise_t;
    int    combo_show;
    float  combo_t, combo_y;
    float  toast_t;            /* counts up from negative while queued */
    int    toast_on;
    int    phase;              /* 0 play, 1 no space, 2 results */
    float  phase_t, res_shown;
    int    res_new_best, res_tick;
    float  time;
    Part   parts[MAX_PARTS];
    int    nparts;
    Beam   beams[MAX_BEAMS];
    FloatText floats[MAX_FLOATS];
    Bot    bot;
} Storm;

static Storm B;
static int   g_best, g_best_loaded;

static struct {
    int   w, h, land;
    float side, cell, ox, oy, tcell;
    float slot_cx[TRAY], slot_cy[TRAY], slot_w, slot_h;
    float hud_y, crown_x, crown_s, best_size, score_y, score_size;
    float pause_x, pause_r;
    float btn_x, btn_y, btn_w, btn_h;
} L;

static int idx(int x, int y) { return y * N + x; }

static float ease_out(float t) { t = pa_clamp01(t); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
static float ease_back(float t) {
    t = pa_clamp01(t);
    float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}
/* Alpha that multiplies rather than replaces. */
static PA_Color fade(PA_Color c, float a) { return pa_alpha(c, (float)PA_A(c) / 255.0f * pa_clamp01(a)); }

/* ------------------------------------------------------------------ layout */
static void compute_layout(int w, int h) {
    L.w = w; L.h = h;
    L.land = w > h;
    if (!L.land) {
        /* Measured off the phone plates: the board spans 0.88 of the width,
           the crown row sits 0.40 board-sides above it, the score 0.16 above
           it, and the tray rides close underneath (critique: no dead third). */
        const float stack_k = 1.84f;
        float side = (float)w * 0.88f;
        if (side * stack_k > (float)h * 0.93f) side = (float)h * 0.93f / stack_k;
        float top = ((float)h - side * stack_k) * 0.40f;
        if (top < (float)h * 0.025f) top = (float)h * 0.025f;
        L.side = side;
        L.cell = side / (float)N;
        L.ox = ((float)w - side) * 0.5f;
        L.oy = top + side * 0.40f;
        L.hud_y = top + side * 0.055f;
        L.score_y = L.oy - side * 0.165f;
        L.score_size = side * 0.112f;
        L.tcell = L.cell * 0.60f;
        L.slot_w = side / 3.0f;
        L.slot_h = L.tcell * 5.6f;
        for (int i = 0; i < TRAY; i++) {
            L.slot_cx[i] = L.ox + side * (0.18f + 0.32f * (float)i);
            L.slot_cy[i] = L.oy + side + side * 0.255f;
        }
        L.crown_x = L.ox + side * 0.005f;
        L.crown_s = side * 0.105f;
        L.best_size = side * 0.054f;
        L.pause_x = L.ox + side - side * 0.04f;
        L.pause_r = side * 0.046f;
        L.btn_w = side * 0.62f; L.btn_h = side * 0.15f;
        L.btn_x = (float)w * 0.5f; L.btn_y = L.slot_cy[0];
    } else {
        float side = (float)h * 0.80f;
        if (side > (float)w * 0.56f) side = (float)w * 0.56f;
        L.side = side;
        L.cell = side / (float)N;
        L.oy = (float)h - side - (float)h * 0.04f;
        L.ox = (float)w * 0.42f - side * 0.5f;
        L.hud_y = L.oy * 0.5f;
        L.score_y = L.oy * 0.5f;
        L.score_size = L.oy * 0.62f;
        if (L.score_size > side * 0.11f) L.score_size = side * 0.11f;
        float col_x = L.ox + side + ((float)w - L.ox - side) * 0.5f;
        L.tcell = L.cell * 0.60f;
        if (L.tcell > side / 3.0f / 5.6f) L.tcell = side / 3.0f / 5.6f;
        L.slot_w = ((float)w - L.ox - side) * 0.8f;
        L.slot_h = side / 3.0f;
        for (int i = 0; i < TRAY; i++) {
            L.slot_cx[i] = col_x;
            L.slot_cy[i] = L.oy + side * (1.0f / 6.0f + (float)i / 3.0f);
        }
        L.crown_x = (float)w * 0.04f;
        L.crown_s = L.oy * 0.5f;
        L.best_size = L.oy * 0.28f;
        L.pause_x = (float)w - L.oy * 0.5f;
        L.pause_r = L.oy * 0.24f;
        L.btn_w = side * 0.62f; L.btn_h = side * 0.14f;
        L.btn_x = L.ox + side * 0.5f; L.btn_y = L.oy + side * 0.84f;
    }
}

/* ------------------------------------------------------------- particles */
static Part *spawn(int kind, float x, float y, float vx, float vy, float life, float size, PA_Color col) {
    if (B.nparts >= MAX_PARTS) return NULL;
    Part *p = &B.parts[B.nparts++];
    memset(p, 0, sizeof(*p));
    p->kind = kind; p->x = x; p->y = y; p->vx = vx; p->vy = vy;
    p->life = p->max = life; p->size = size; p->col = col;
    p->rot = pa_rng_range(&B.rand, 0.0f, PA_TAU);
    p->vr = pa_rng_range(&B.rand, -9.0f, 9.0f);
    return p;
}

static void confetti_burst(float x, float y, int count, float spread) {
    static const uint32_t CONF[] = { 0xFFC83A, 0xFF5A7A, 0x3CC8F0, 0x7CE85A, 0xB66CFF, 0xFFFFFF, 0xFF8A2A };
    for (int i = 0; i < count; i++) {
        float a = pa_rng_range(&B.rand, -PA_PI * 0.95f, -PA_PI * 0.05f);
        float v = pa_rng_range(&B.rand, 0.35f, 1.0f) * spread;
        spawn(P_CONFETTI, x + pa_rng_range(&B.rand, -20.0f, 20.0f), y,
              cosf(a) * v, sinf(a) * v, pa_rng_range(&B.rand, 1.6f, 2.6f),
              L.side * pa_rng_range(&B.rand, 0.016f, 0.026f),
              pa_hex(CONF[pa_rng_int(&B.rand, 0, 6)]));
    }
}

static void sparkles(float x, float y, float rx, float ry, int count) {
    for (int i = 0; i < count; i++) {
        spawn(P_SPARK, x + pa_rng_range(&B.rand, -rx, rx), y + pa_rng_range(&B.rand, -ry, ry),
              pa_rng_range(&B.rand, -20.0f, 20.0f), pa_rng_range(&B.rand, -40.0f, -5.0f),
              pa_rng_range(&B.rand, 0.5f, 1.0f), L.cell * pa_rng_range(&B.rand, 0.10f, 0.22f),
              pa_rng_chance(&B.rand, 0.6f) ? pa_hex(0xFFF2A0) : PA_RGB(255, 255, 255));
    }
}

/* ------------------------------------------------------------- generation */
static int fits_on(const int *board, int shape, int gx, int gy) {
    const Shape *s = &SHAPES[shape];
    if (gx < 0 || gy < 0 || gx + s->w > N || gy + s->h > N) return 0;
    for (int i = 0; i < s->count; i++) if (board[idx(gx + s->cx[i], gy + s->cy[i])] >= 0) return 0;
    return 1;
}
static int fits(int shape, int gx, int gy) { return fits_on(B.cell, shape, gx, gy); }

static int any_fits_on(const int *board, int shape) {
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (fits_on(board, shape, x, y)) return 1;
    return 0;
}
static int any_fits(int shape) { return any_fits_on(B.cell, shape); }

static int tray_playable(void) {
    for (int i = 0; i < TRAY; i++) if (B.tray[i].shape >= 0 && any_fits(B.tray[i].shape)) return 1;
    return 0;
}

static int roll_shape(void) {
    int total = 0;
    for (int i = 0; i < SHAPE_COUNT; i++) total += SHAPES[i].weight;
    int roll = pa_rng_int(&B.rand, 0, total - 1);
    for (int i = 0; i < SHAPE_COUNT; i++) {
        roll -= SHAPES[i].weight;
        if (roll < 0) return i;
    }
    return 0;
}

static void refill_tray(void) {
    /* Like the reference, the dealer is generous: it re-deals a hand in which
       nothing fits a few times before letting it stand. */
    for (int attempt = 0; attempt < 5; attempt++) {
        int last = -1;
        for (int i = 0; i < TRAY; i++) {
            B.tray[i].shape = roll_shape();
            int t;
            do { t = pa_rng_int(&B.rand, 0, TINT_COUNT - 1); } while (t == last);
            last = t;
            B.tray[i].tint = t;
            B.tray[i].pop = -0.07f * (float)i;
        }
        if (tray_playable()) break;
    }
    pa_tone(620, 760, 0.05f, 0, 0.05f);
}

/* ------------------------------------------------------------- placement */
/** Which rows and columns a placement would finish. Used for the preview. */
static int completing_lines(int shape, int gx, int gy, int rows[N], int cols[N]) {
    int tmp[N * N];
    memcpy(tmp, B.cell, sizeof(tmp));
    const Shape *s = &SHAPES[shape];
    for (int i = 0; i < s->count; i++) tmp[idx(gx + s->cx[i], gy + s->cy[i])] = 0;
    int lines = 0;
    for (int k = 0; k < N; k++) {
        int fr = 1, fc = 1;
        for (int j = 0; j < N; j++) {
            if (tmp[idx(j, k)] < 0) fr = 0;
            if (tmp[idx(k, j)] < 0) fc = 0;
        }
        rows[k] = fr; cols[k] = fc;
        lines += fr + fc;
    }
    return lines;
}

static const char *praise_word(int lines) {
    switch (lines) {
        case 2: return "Good!";
        case 3: return "Great!";
        case 4: return "Excellent!";
        case 5: return "Amazing!";
        default: return lines >= 6 ? "Unbelievable!" : "";
    }
}

static void clear_sound(int lines, int combo) {
    float step = powf(2.0f, (float)(combo > 12 ? 12 : combo) / 12.0f);
    float base = 440.0f * step;
    pa_tone(base, base * 1.5f, 0.16f, 1, 0.12f);
    pa_tone(base * 1.26f, base * 2.0f, 0.24f, 0, 0.08f);
    if (lines >= 2) pa_tone(base * 1.5f, base * 3.0f, 0.34f, 1, 0.07f);
    pa_noise(0.12f, 0.05f);
}

static void resolve_clears(int tint, float pcx, float pcy) {
    int rows[N], cols[N], lines = 0;
    for (int k = 0; k < N; k++) {
        int fr = 1, fc = 1;
        for (int j = 0; j < N; j++) {
            if (B.cell[idx(j, k)] < 0) fr = 0;
            if (B.cell[idx(k, j)] < 0) fc = 0;
        }
        rows[k] = fr; cols[k] = fc;
        lines += fr + fc;
    }
    if (lines == 0) {
        /* The streak survives two dry placements, as the reference's does. */
        if (++B.since_clear >= 3) B.combo = 0;
        return;
    }
    B.since_clear = 0;
    B.combo++;

    /* Rows and columns are all marked before any is emptied, so a cross of
       two full lines scores both. The burst ripples out from the piece. */
    int cleared = 0;
    float sx = 0.0f, sy = 0.0f;
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            if (!rows[y] && !cols[x]) continue;
            int i = idx(x, y);
            float d = sqrtf(((float)x + 0.5f - pcx) * ((float)x + 0.5f - pcx) +
                            ((float)y + 0.5f - pcy) * ((float)y + 0.5f - pcy));
            B.clr[i] = -d * 0.035f;
            B.clr_tint[i] = tint;
            B.cell[i] = -1;
            B.land[i] = 0.0f;
            cleared++;
            sx += (float)x + 0.5f; sy += (float)y + 0.5f;
        }
    }
    for (int k = 0; k < N; k++) {
        for (int r = 0; r < 2; r++) {
            if (!(r == 0 ? rows[k] : cols[k])) continue;
            for (int b = 0; b < MAX_BEAMS; b++) {
                if (B.beams[b].t <= 0.0f || B.beams[b].t > 0.7f) {
                    B.beams[b].row = r == 0; B.beams[b].index = k; B.beams[b].tint = tint;
                    B.beams[b].t = 0.0001f;
                    break;
                }
            }
        }
    }

    int gain = cleared * 10;
    if (lines > 1) gain = gain * (2 + lines) / 3;
    gain += 20 * (B.combo - 1) * lines;
    B.score += gain;

    for (int f = 0; f < MAX_FLOATS; f++) {
        if (B.floats[f].t <= 0.0f) {
            FloatText *ft = &B.floats[f];
            ft->t = 0.0001f;
            ft->x = L.ox + sx / (float)cleared * L.cell;
            ft->y = L.oy + sy / (float)cleared * L.cell;
            ft->tint = tint;
            snprintf(ft->txt, sizeof(ft->txt), "+%d", gain);
            break;
        }
    }

    if (lines >= 2) {
        snprintf(B.praise, sizeof(B.praise), "%s", praise_word(lines));
        B.praise_t = 0.0001f;
        sparkles(L.ox + L.side * 0.5f, L.oy + L.side * 0.45f, L.side * 0.30f, L.cell * 0.9f, 14);
    }
    if (B.combo >= 2) {
        B.combo_show = B.combo;
        B.combo_t = 0.0001f;
        int first_row = -1;
        for (int k = 0; k < N && first_row < 0; k++) if (rows[k]) first_row = k;
        float y = first_row >= 0 ? L.oy + ((float)first_row + 0.5f) * L.cell : L.oy + L.side * 0.5f;
        if (lines >= 2) y = L.oy + L.side * 0.60f;
        B.combo_y = pa_clampf(y, L.oy + L.cell * 1.2f, L.oy + L.side - L.cell * 1.2f);
    }
    B.shake = lines >= 2 ? 0.22f + 0.05f * (float)lines : 0.10f;
    clear_sound(lines, B.combo);

    if (!B.passed_best && B.best_start > 0 && B.score > B.best_start) {
        B.passed_best = 1;
        B.toast_on = 1;
        /* Waits for any praise or combo callout to finish first. */
        B.toast_t = (lines >= 2 || B.combo >= 2) ? -1.3f : 0.0f;
    }
}

static void save_best(void) {
    if (pa_demo_mode()) return;     /* captures must not touch the player's save */
    if (B.score > g_best) {
        g_best = B.score;
        pa_save_set("blockstorm.best", g_best);
        pa_save_flush();
    }
}

static void place(int slot, int gx, int gy) {
    const Shape *s = &SHAPES[B.tray[slot].shape];
    int tint = B.tray[slot].tint;
    for (int i = 0; i < s->count; i++) {
        int k = idx(gx + s->cx[i], gy + s->cy[i]);
        B.cell[k] = tint;
        B.land[k] = 1.0f;
    }
    B.score += s->count;
    B.placed++;
    B.tray[slot].shape = -1;
    pa_tone(240, 130, 0.08f, 1, 0.12f);
    pa_noise(0.04f, 0.05f);

    resolve_clears(tint, (float)gx + (float)s->w * 0.5f, (float)gy + (float)s->h * 0.5f);

    int empty = 1;
    for (int i = 0; i < TRAY; i++) if (B.tray[i].shape >= 0) empty = 0;
    if (empty) refill_tray();

    if (!tray_playable()) {
        B.phase = 1;
        B.phase_t = 0.0f;
        B.res_new_best = B.score > B.best_start;
        B.toast_on = 0;            /* the results card says it instead */
        save_best();
    }
}

/* ----------------------------------------------------------------- demos */
static void load_board(const char *rows[N]) {
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            int t = -1;
            switch (rows[y][x]) {
                case 'r': t = T_RED; break;    case 'o': t = T_ORANGE; break;
                case 'y': t = T_YELLOW; break; case 'g': t = T_GREEN; break;
                case 'c': t = T_CYAN; break;   case 'b': t = T_BLUE; break;
                case 'p': t = T_PURPLE; break; default: break;
            }
            B.cell[idx(x, y)] = t;
        }
    }
}

static void set_tray(int i, const char *mask, int tint) {
    B.tray[i].shape = find_shape(mask);
    B.tray[i].tint = tint;
    B.tray[i].pop = -0.07f * (float)i;
}

/* Review captures only (pa_demo_mode): 1 plays from an empty board, 2 starts
   a streak one move from a four-line clear, 3 is one move from the end of a
   record run. Shipping builds always see 0 and never reach this. */
static void demo_setup(int mode) {
    if (mode == 2) {
        static const char *rows[N] = {
            "bb.cc.gg",
            "b..cc..g",
            "..orr...",
            ".oobby..",
            "...bbyy.",
            "ggp..rrc",
            "gbp..rcc",
            "bb.oo.yy",
        };
        load_board(rows);
        set_tray(0, "##|##", T_PURPLE);
        set_tray(1, "###", T_GREEN);
        set_tray(2, ".#|##", T_YELLOW);
        B.combo = 2; B.since_clear = 0;
        B.score = 1240; B.best_start = 2650;
    } else if (mode == 3) {
        static const char *rows[N] = {
            "rr.bbpp.",
            "r.ggb.pp",
            "oog.cc.y",
            "o..bcyyy",
            ".gbb.p.r",
            "c.cyypyr",
            "cc.ygg.r",
            "bbb...oo",
        };
        load_board(rows);
        set_tray(0, "###", T_ORANGE);
        set_tray(1, "###|###|###", T_BLUE);
        set_tray(2, "#|#|#|#|#", T_CYAN);
        B.score = 2590; B.best_start = 2650;
    } else {
        B.best_start = 2650;
    }
    B.shown = (float)B.score;
}

/* ------------------------------------------------------------------- bot */
static float bot_eval(int slot, int gx, int gy) {
    int bd[N * N];
    memcpy(bd, B.cell, sizeof(bd));
    const Shape *s = &SHAPES[B.tray[slot].shape];
    float score = 0.0f;
    for (int i = 0; i < s->count; i++) {
        int x = gx + s->cx[i], y = gy + s->cy[i];
        bd[idx(x, y)] = 0;
    }
    /* Contact: snug placements against walls and blocks leave fewer holes. */
    for (int i = 0; i < s->count; i++) {
        int x = gx + s->cx[i], y = gy + s->cy[i];
        static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N || bd[idx(nx, ny)] >= 0) score += 1.5f;
        }
    }
    int rows[N], cols[N], lines = 0;
    for (int k = 0; k < N; k++) {
        int fr = 1, fc = 1;
        for (int j = 0; j < N; j++) {
            if (bd[idx(j, k)] < 0) fr = 0;
            if (bd[idx(k, j)] < 0) fc = 0;
        }
        rows[k] = fr; cols[k] = fc; lines += fr + fc;
    }
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (rows[y] || cols[x]) bd[idx(x, y)] = -1;
    score += (float)(lines * lines) * 40.0f + (float)lines * 20.0f;

    int filled = 0;
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            if (bd[idx(x, y)] >= 0) { filled++; continue; }
            int blocked = 0;
            if (x == 0 || bd[idx(x - 1, y)] >= 0) blocked++;
            if (x == N - 1 || bd[idx(x + 1, y)] >= 0) blocked++;
            if (y == 0 || bd[idx(x, y - 1)] >= 0) blocked++;
            if (y == N - 1 || bd[idx(x, y + 1)] >= 0) blocked++;
            if (blocked == 4) score -= 14.0f;
            else if (blocked == 3) score -= 3.0f;
        }
    }
    score -= (float)filled * 0.8f;
    for (int i = 0; i < TRAY; i++) {
        if (i == slot || B.tray[i].shape < 0) continue;
        if (!any_fits_on(bd, B.tray[i].shape)) score -= 300.0f;
    }
    /* Keep room for the awkward pieces. */
    int big = 0;
    for (int y = 0; y + 3 <= N; y++) for (int x = 0; x + 3 <= N; x++) {
        int ok = 1;
        for (int k = 0; k < 9 && ok; k++) if (bd[idx(x + k % 3, y + k / 3)] >= 0) ok = 0;
        big += ok;
    }
    score += (float)(big > 6 ? 6 : big) * 2.0f;
    return score;
}

static int bot_choose(int *slot, int *gx, int *gy) {
    float best = -1e9f;
    int found = 0;
    for (int i = 0; i < TRAY; i++) {
        if (B.tray[i].shape < 0) continue;
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
            if (!fits(B.tray[i].shape, x, y)) continue;
            float v = bot_eval(i, x, y);
            if (v > best) { best = v; *slot = i; *gx = x; *gy = y; found = 1; }
        }
    }
    return found;
}

/** The bot drives the same pointer path a thumb would: press on the tray
    piece, carry it over, hover so the ghost shows, release. */
static void bot_step(float dt, PA_Input *o) {
    Bot *b = &B.bot;
    memset(o, 0, sizeof(*o));
    b->t += dt;
    if (B.phase != 0) {
        if (b->down) { b->down = 0; o->released = 1; }
        o->x = b->x; o->y = b->y;
        return;
    }
    switch (b->phase) {
    case 0:
        if (b->t >= b->think) {
            int slot = 0, gx = 0, gy = 0;
            if (bot_choose(&slot, &gx, &gy)) {
                const Shape *s = &SHAPES[B.tray[slot].shape];
                b->slot = slot; b->gx = gx; b->gy = gy;
                b->sx = L.slot_cx[slot] + L.tcell * 0.3f;
                b->sy = L.slot_cy[slot] + L.tcell * 0.4f;
                /* A real thumb never lands dead on the grid: hover a little
                   off so the snapped ghost and the held piece both read. */
                b->ex = L.ox + ((float)gx + (float)s->w * 0.5f + 0.32f) * L.cell;
                b->ey = L.oy + ((float)gy + (float)s->h * 0.5f - 0.36f) * L.cell
                      + (float)s->h * L.cell * 0.5f + L.cell * LIFT_GAP;
                b->x = b->sx; b->y = b->sy;
                b->down = 1;
                o->pressed = 1;
                b->phase = 1; b->t = 0.0f;
            } else b->t = 0.0f;
        }
        break;
    case 1: {
        float k = pa_smooth(pa_clamp01(b->t / 0.62f));
        b->x = pa_lerpf(b->sx, b->ex, k) + sinf(k * PA_PI) * L.cell * 0.4f;
        b->y = pa_lerpf(b->sy, b->ey, k) - sinf(k * PA_PI) * L.cell * 0.3f;
        if (b->t >= 0.62f) { b->x = b->ex; b->y = b->ey; b->phase = 2; b->t = 0.0f; }
        break;
    }
    case 2:
        if (b->t >= 0.32f) {
            b->down = 0;
            o->released = 1;
            b->phase = 0; b->t = 0.0f;
            b->think = 0.38f;
        }
        break;
    }
    o->x = b->x; o->y = b->y; o->down = b->down;
}

/* ------------------------------------------------------------------ frame */
static void new_run(void) {
    if (!g_shapes_ready) build_shapes();
    if (L.w == 0) compute_layout(540, 1170);
    int demo = pa_demo_mode();
    memset(&B, 0, sizeof(B));
    for (int i = 0; i < N * N; i++) { B.cell[i] = -1; B.clr[i] = -9.0f; }
    B.drag = -1;
    B.ret_slot = -1;
    B.best_start = g_best;
    pa_rng_seed(&B.rand, demo ? 0xB10C5u + (uint32_t)demo * 977u
                              : 0xB10Cu ^ ((uint32_t)g_best * 2654435761u + (uint32_t)g_best_loaded * 0x9E3779B9u));
    refill_tray();
    if (demo) demo_setup(demo);
    B.bot.think = 0.75f;
    g_best_loaded++;
}

static void storm_start(void) {
    if (!g_best_loaded) g_best = pa_demo_mode() ? 0 : pa_save_get("blockstorm.best", 0);
    new_run();
}

static void storm_stop(void) { if (B.phase == 0) save_best(); }

/** Geometry of the piece under the finger, easing from tray scale to board
    scale as it lifts. */
static void drag_geom(float *px, float *py, float *cs) {
    const Shape *s = &SHAPES[B.tray[B.drag].shape];
    float e = ease_out(B.lift);
    float c = pa_lerpf(L.tcell, L.cell, e);
    float tx = B.fx, ty = B.fy - ((float)s->h * L.cell * 0.5f + L.cell * LIFT_GAP);
    float cx = pa_lerpf(B.grab_x, tx, e), cy = pa_lerpf(B.grab_y, ty, e);
    *px = cx - (float)s->w * c * 0.5f;
    *py = cy - (float)s->h * c * 0.5f;
    *cs = c;
}

static void drag_target(int *gx, int *gy) {
    const Shape *s = &SHAPES[B.tray[B.drag].shape];
    float px = B.fx - (float)s->w * L.cell * 0.5f;
    float py = B.fy - ((float)s->h * L.cell * 0.5f + L.cell * LIFT_GAP) - (float)s->h * L.cell * 0.5f;
    *gx = (int)floorf((px - L.ox) / L.cell + 0.5f);
    *gy = (int)floorf((py - L.oy) / L.cell + 0.5f);
}

static void update_parts(float dt) {
    int n = 0;
    for (int i = 0; i < B.nparts; i++) {
        Part *p = &B.parts[i];
        p->life -= dt;
        if (p->life <= 0.0f) continue;
        p->x += p->vx * dt; p->y += p->vy * dt;
        p->rot += p->vr * dt;
        switch (p->kind) {
            case P_SHARD:    p->vy += L.side * 2.2f * dt; p->vx *= 1.0f - dt * 1.5f; break;
            case P_CONFETTI:
                p->vy += L.side * 0.9f * dt;
                p->vx *= 1.0f - dt * 2.0f;
                if (p->vy > L.side * 0.35f) p->vy = L.side * 0.35f;
                break;
            case P_SPARK:    p->vy *= 1.0f - dt * 2.0f; break;
            default:         p->vy -= 10.0f * dt; break;
        }
        B.parts[n++] = *p;
    }
    B.nparts = n;
}

static void restart_hit(const PA_Input *in) {
    if (B.phase != 2 || B.phase_t < 1.2f || !in->tapped) return;
    if (fabsf(in->x - L.btn_x) < L.btn_w * 0.5f && fabsf(in->y - L.btn_y) < L.btn_h * 0.5f + 8.0f) {
        g_best = g_best > B.score ? g_best : B.score;
        pa_sfx("select");
        new_run();
    }
}

static void storm_update(float dt, const PA_Input *real_in) {
    PA_Input bot_in;
    const PA_Input *in = real_in;
    if (pa_demo_mode()) { bot_step(dt, &bot_in); in = &bot_in; }

    B.time += dt;
    if (B.shake > 0.0f) B.shake = B.shake > dt ? B.shake - dt : 0.0f;
    if (B.punch > 0.0f) B.punch = B.punch > dt * 4.0f ? B.punch - dt * 4.0f : 0.0f;
    if (B.praise_t > 0.0f) { B.praise_t += dt; if (B.praise_t > 1.6f) B.praise_t = 0.0f; }
    if (B.combo_t > 0.0f) { B.combo_t += dt; if (B.combo_t > 1.5f) B.combo_t = 0.0f; }
    if (B.toast_on) {
        float before = B.toast_t;
        B.toast_t += dt;
        if (before <= 0.0f && B.toast_t > 0.0f) {
            confetti_burst(L.ox + L.side * 0.2f, L.oy + L.side * 0.3f, 45, L.side * 1.1f);
            confetti_burst(L.ox + L.side * 0.8f, L.oy + L.side * 0.3f, 45, L.side * 1.1f);
            sparkles(L.ox + L.side * 0.5f, L.oy + L.side * 0.3f, L.side * 0.3f, L.cell, 12);
            pa_sfx("win");
        }
        if (B.toast_t > 2.0f) { B.toast_on = 0; B.toast_t = 0.0f; }
    }
    for (int i = 0; i < TRAY; i++) if (B.tray[i].pop < 1.0f) B.tray[i].pop += dt * 4.0f;
    for (int b = 0; b < MAX_BEAMS; b++) if (B.beams[b].t > 0.0f) {
        B.beams[b].t += dt;
        if (B.beams[b].t > 0.75f) B.beams[b].t = 0.0f;
    }
    for (int f = 0; f < MAX_FLOATS; f++) if (B.floats[f].t > 0.0f) {
        B.floats[f].t += dt;
        if (B.floats[f].t > 1.1f) B.floats[f].t = 0.0f;
    }

    for (int i = 0; i < N * N; i++) {
        if (B.land[i] > 0.0f) B.land[i] = B.land[i] > dt * 5.0f ? B.land[i] - dt * 5.0f : 0.0f;
        if (B.clr[i] > -5.0f) {
            float before = B.clr[i];
            B.clr[i] += dt;
            if (before < 0.0f && B.clr[i] >= 0.0f) {
                /* Burst: shards in the line's colour. */
                float cx = L.ox + ((float)(i % N) + 0.5f) * L.cell;
                float cy = L.oy + ((float)(i / N) + 0.5f) * L.cell;
                PA_Color col = pa_hex(TINTS[B.clr_tint[i]]);
                for (int k = 0; k < 3; k++) {
                    float a = pa_rng_range(&B.rand, 0.0f, PA_TAU);
                    float v = L.side * pa_rng_range(&B.rand, 0.25f, 0.75f);
                    spawn(P_SHARD, cx + pa_rng_range(&B.rand, -L.cell * 0.3f, L.cell * 0.3f),
                          cy + pa_rng_range(&B.rand, -L.cell * 0.3f, L.cell * 0.3f),
                          cosf(a) * v, sinf(a) * v - L.side * 0.35f,
                          pa_rng_range(&B.rand, 0.45f, 0.85f),
                          L.cell * pa_rng_range(&B.rand, 0.12f, 0.26f),
                          k == 0 ? pa_shade(col, 0.45f) : col);
                }
                spawn(P_DOT, cx, cy, 0, -L.side * 0.05f, 0.5f, L.cell * 0.1f, PA_RGB(255, 255, 255));
            }
            if (B.clr[i] > 0.4f) B.clr[i] = -9.0f;
        }
    }
    update_parts(dt);

    /* Score counts up rather than jumping. */
    if (B.shown < (float)B.score) {
        float step = ((float)B.score - B.shown) * dt * 6.0f;
        if (step < dt * 60.0f) step = dt * 60.0f;
        B.shown += step;
        if (B.shown > (float)B.score) B.shown = (float)B.score;
        B.punch = 1.0f;
    }

    if (B.ret_slot >= 0) {
        B.ret_t += dt * 6.0f;
        if (B.ret_t >= 1.0f) B.ret_slot = -1;
    }

    if (B.phase == 1) {
        float before = B.phase_t;
        B.phase_t += dt;
        for (int y = 0; y < N; y++) {
            float start = 0.55f + (float)(N - 1 - y) * 0.07f;
            for (int x = 0; x < N; x++) B.grey[idx(x, y)] = pa_clamp01((B.phase_t - start) / 0.14f);
        }
        if (before < 0.55f && B.phase_t >= 0.55f) pa_sfx("lose");
        if (before < 1.25f && B.phase_t >= 1.25f) pa_tone(330, 220, 0.4f, 1, 0.09f);
        if (B.phase_t >= 3.0f) {
            B.phase = 2; B.phase_t = 0.0f; B.res_shown = 0.0f; B.res_tick = 0;
            if (B.res_new_best) { pa_sfx("win"); confetti_burst((float)L.w * 0.5f, L.oy + L.side * 0.3f, 90, L.side * 1.4f); }
        }
        return;
    }
    if (B.phase == 2) {
        B.phase_t += dt;
        float k = pa_clamp01((B.phase_t - 0.35f) / 1.1f);
        B.res_shown = (float)B.score * ease_out(k);
        int tick = (int)(k * 12.0f);
        if (tick > B.res_tick && k < 1.0f) { B.res_tick = tick; pa_tone(900 + 40.0f * (float)tick, 1000 + 40.0f * (float)tick, 0.03f, 0, 0.04f); }
        if (B.res_new_best && B.phase_t < 3.0f && pa_rng_chance(&B.rand, 0.7f)) {
            static const uint32_t CONF[] = { 0xFFC83A, 0xFF5A7A, 0x3CC8F0, 0x7CE85A, 0xB66CFF, 0xFFFFFF };
            spawn(P_CONFETTI, pa_rng_range(&B.rand, 0.0f, (float)L.w), -10.0f,
                  pa_rng_range(&B.rand, -30.0f, 30.0f), pa_rng_range(&B.rand, 60.0f, 160.0f),
                  4.0f, L.side * pa_rng_range(&B.rand, 0.016f, 0.026f),
                  pa_hex(CONF[pa_rng_int(&B.rand, 0, 5)]));
        }
        restart_hit(real_in);
        return;
    }

    /* Pick up. */
    if (in->pressed && B.drag < 0) {
        for (int i = 0; i < TRAY; i++) {
            if (B.tray[i].shape < 0) continue;
            if (fabsf(in->x - L.slot_cx[i]) <= L.slot_w * 0.5f &&
                fabsf(in->y - L.slot_cy[i]) <= L.slot_h * 0.5f) {
                B.drag = i;
                B.lift = 0.0f;
                B.grab_x = L.slot_cx[i]; B.grab_y = L.slot_cy[i];
                if (B.ret_slot == i) B.ret_slot = -1;
                pa_tone(520, 700, 0.05f, 0, 0.07f);
                break;
            }
        }
    }

    if (B.drag >= 0) {
        B.fx = in->x;
        B.fy = in->y;
        B.lift = pa_clamp01(B.lift + dt * 9.0f);
        if (in->released || !in->down) {
            int gx, gy;
            drag_target(&gx, &gy);
            if (fits(B.tray[B.drag].shape, gx, gy)) {
                place(B.drag, gx, gy);
            } else {
                /* Flies back to its slot from where it was let go. */
                float px, py, cs;
                drag_geom(&px, &py, &cs);
                const Shape *s = &SHAPES[B.tray[B.drag].shape];
                B.ret_slot = B.drag; B.ret_t = 0.0f;
                B.ret_x = px + (float)s->w * cs * 0.5f;
                B.ret_y = py + (float)s->h * cs * 0.5f;
                B.ret_cell = cs;
                pa_tone(300, 200, 0.10f, 1, 0.07f);
            }
            B.drag = -1;
        }
    }
}

/* ---------------------------------------------------------------- drawing */
/**
 * One block, built the way the reference builds it: a one-pixel dark rim,
 * four bevel trapezoids (lightest along the top, light left, darker right,
 * darkest bottom), a face with a faint top-to-bottom lift, and a gloss band
 * across the top-left of the face.
 */
static void block(PA_Canvas *c, float x, float y, float size, PA_Color tint, float alpha) {
    if (size < 1.5f || alpha <= 0.01f) return;
    float a = alpha;
    float x1 = x + 1.0f, y1 = y + 1.0f;
    float x2 = x + size - 1.0f, y2 = y + size - 1.0f;
    float b = size * 0.135f;

    pa_fill_rect(c, x, y, size, size, fade(pa_shade(tint, -0.62f), a));

    PA_Vec2 top[4]    = { { x1, y1 }, { x2, y1 }, { x2 - b, y1 + b }, { x1 + b, y1 + b } };
    PA_Vec2 left[4]   = { { x1, y1 }, { x1 + b, y1 + b }, { x1 + b, y2 - b }, { x1, y2 } };
    PA_Vec2 right[4]  = { { x2, y1 }, { x2, y2 }, { x2 - b, y2 - b }, { x2 - b, y1 + b } };
    PA_Vec2 bottom[4] = { { x1, y2 }, { x1 + b, y2 - b }, { x2 - b, y2 - b }, { x2, y2 } };
    pa_fill_poly(c, top, 4, fade(pa_shade(tint, 0.42f), a));
    pa_fill_poly(c, left, 4, fade(pa_shade(tint, 0.14f), a));
    pa_fill_poly(c, right, 4, fade(pa_shade(tint, -0.20f), a));
    pa_fill_poly(c, bottom, 4, fade(pa_shade(tint, -0.40f), a));

    float fx = x1 + b, fy = y1 + b, fw = (x2 - x1) - b * 2.0f, fh = (y2 - y1) - b * 2.0f;
    PA_Paint face = pa_linear(0, fy, 0, fy + fh);
    pa_stop(&face, 0.0f, fade(pa_shade(tint, 0.14f), a));
    pa_stop(&face, 1.0f, fade(pa_shade(tint, -0.06f), a));
    pa_fill_rect_paint(c, fx, fy, fw, fh, &face);

    /* Gloss: a soft white band over the top fifth of the face, heavier to the
       left, plus a crisp specular line along the top bevel's outer edge. */
    PA_Paint gloss = pa_linear(0, fy, 0, fy + fh * 0.32f);
    pa_stop(&gloss, 0.0f, PA_RGBA(255, 255, 255, (int)(70.0f * a)));
    pa_stop(&gloss, 1.0f, PA_RGBA(255, 255, 255, 0));
    PA_Vec2 gl[4] = { { fx, fy }, { fx + fw, fy }, { fx + fw * 0.72f, fy + fh * 0.32f }, { fx, fy + fh * 0.32f } };
    pa_fill_poly_paint(c, gl, 4, &gloss);
    pa_fill_rect(c, x1 + b * 0.5f, y1 + 0.5f, (x2 - x1) * 0.55f, 1.2f, PA_RGBA(255, 255, 255, (int)(110.0f * a)));
}

static PA_Color grey_of(PA_Color col, float k) {
    if (k <= 0.0f) return col;
    float lum = (float)PA_R(col) * 0.3f + (float)PA_G(col) * 0.55f + (float)PA_B(col) * 0.15f;
    int g = (int)(lum * 0.42f + 34.0f);
    return pa_mix(col, PA_RGB(g, g + 4, g + 18), k * 0.9f);
}

static void draw_shape(PA_Canvas *c, int shape, PA_Color tint, float x, float y, float cell, float alpha) {
    const Shape *s = &SHAPES[shape];
    for (int i = 0; i < s->count; i++)
        block(c, x + (float)s->cx[i] * cell, y + (float)s->cy[i] * cell, cell, tint, alpha);
}

static void shape_shadow(PA_Canvas *c, int shape, float x, float y, float cell, float off, int alpha) {
    const Shape *s = &SHAPES[shape];
    for (int i = 0; i < s->count; i++)
        pa_fill_rect(c, x + (float)s->cx[i] * cell + off * 0.6f, y + (float)s->cy[i] * cell + off,
                     cell, cell, PA_RGBA(10, 14, 40, alpha));
}

static void draw_crown(PA_Canvas *c, float cx, float cy, float w) {
    float h = w * 0.74f, x0 = cx - w * 0.5f, y0 = cy - h * 0.5f;
    PA_Vec2 body[7] = {
        { x0 + w * 0.10f, y0 + h * 0.86f }, { x0 + w * 0.02f, y0 + h * 0.30f },
        { x0 + w * 0.30f, y0 + h * 0.56f }, { cx, y0 + h * 0.14f },
        { x0 + w * 0.70f, y0 + h * 0.56f }, { x0 + w * 0.98f, y0 + h * 0.30f },
        { x0 + w * 0.90f, y0 + h * 0.86f }
    };
    PA_Color dark = pa_hex(0xB86A00);
    pa_stroke_poly(c, body, 7, 1, w * 0.07f, dark);
    float br = w * 0.085f;
    pa_fill_circle(c, x0 + w * 0.02f, y0 + h * 0.26f, br + w * 0.035f, dark);
    pa_fill_circle(c, cx, y0 + h * 0.10f, br + w * 0.035f, dark);
    pa_fill_circle(c, x0 + w * 0.98f, y0 + h * 0.26f, br + w * 0.035f, dark);
    pa_round_rect(c, x0 + w * 0.06f, y0 + h * 0.76f, w * 0.88f, h * 0.26f, h * 0.08f, dark);

    PA_Paint g = pa_linear(0, y0, 0, y0 + h);
    pa_stop(&g, 0.0f, pa_hex(0xFFEA70));
    pa_stop(&g, 0.6f, pa_hex(0xFFC21A));
    pa_stop(&g, 1.0f, pa_hex(0xF09A00));
    pa_fill_poly_paint(c, body, 7, &g);
    pa_fill_circle(c, x0 + w * 0.02f, y0 + h * 0.26f, br, pa_hex(0xFFD640));
    pa_fill_circle(c, cx, y0 + h * 0.10f, br, pa_hex(0xFFD640));
    pa_fill_circle(c, x0 + w * 0.98f, y0 + h * 0.26f, br, pa_hex(0xFFD640));
    pa_fill_circle(c, cx - br * 0.3f, y0 + h * 0.10f - br * 0.3f, br * 0.35f, PA_RGBA(255, 255, 255, 200));
    PA_Paint band = pa_linear(0, y0 + h * 0.78f, 0, y0 + h);
    pa_stop(&band, 0.0f, pa_hex(0xFFB000));
    pa_stop(&band, 1.0f, pa_hex(0xE08A00));
    pa_round_rect_paint(c, x0 + w * 0.09f, y0 + h * 0.78f, w * 0.82f, h * 0.20f, h * 0.06f, &band);
    pa_fill_ellipse(c, x0 + w * 0.30f, y0 + h * 0.62f, w * 0.06f, h * 0.10f, PA_RGBA(255, 255, 255, 110));
}

/* Text helpers: y is the vertical centre. */
static void text_mid(PA_Canvas *c, const char *t, float x, float cy, float size,
                     PA_Color fill, PA_Color outline, float weight) {
    pa_text_bold(c, t, x, cy - size * 0.5f, size, fill, outline, PA_ALIGN_CENTER, size * 0.06f, weight);
}

static void soft_glow(PA_Canvas *c, float cx, float cy, float half_w, float r, PA_Color col, float a) {
    int n = (int)(half_w / (r * 0.6f)) + 1;
    for (int i = -n; i <= n; i++) {
        float x = cx + half_w * (float)i / (float)(n > 0 ? n : 1);
        PA_Paint p = pa_radial(x, cy, 0.0f, r);
        pa_stop(&p, 0.0f, fade(col, a * 0.45f));
        pa_stop(&p, 1.0f, fade(col, 0.0f));
        pa_fill_ellipse_paint(c, x, cy, r, r, &p);
    }
}

static void star4(PA_Canvas *c, float x, float y, float r, PA_Color col) {
    float k = r * 0.28f;
    PA_Vec2 s[8] = {
        { x, y - r }, { x + k, y - k }, { x + r, y }, { x + k, y + k },
        { x, y + r }, { x - k, y + k }, { x - r, y }, { x - k, y - k }
    };
    pa_fill_poly(c, s, 8, col);
}

static void draw_parts(PA_Canvas *c, int confetti_pass) {
    for (int i = 0; i < B.nparts; i++) {
        const Part *p = &B.parts[i];
        if ((p->kind == P_CONFETTI) != confetti_pass) continue;
        float k = p->life / p->max;
        switch (p->kind) {
        case P_SHARD: {
            float s = p->size * (0.4f + 0.6f * k), cs = cosf(p->rot) * s * 0.5f, sn = sinf(p->rot) * s * 0.5f;
            PA_Vec2 q[4] = { { p->x - cs + sn, p->y - sn - cs }, { p->x + cs + sn, p->y + sn - cs },
                             { p->x + cs - sn, p->y + sn + cs }, { p->x - cs - sn, p->y - sn + cs } };
            pa_fill_poly(c, q, 4, fade(p->col, pa_clamp01(k * 1.6f)));
            break;
        }
        case P_SPARK: {
            float tw = 0.6f + 0.4f * sinf((p->max - p->life) * 18.0f + p->rot);
            star4(c, p->x, p->y, p->size * tw * (k < 0.3f ? k / 0.3f : 1.0f), fade(p->col, k * 1.5f));
            break;
        }
        case P_CONFETTI: {
            float w = p->size, h = p->size * 0.55f * fabsf(cosf(p->rot * 0.7f)) + 1.0f;
            float cs = cosf(p->rot * 0.3f), sn = sinf(p->rot * 0.3f);
            PA_Vec2 q[4] = {
                { p->x - cs * w * 0.5f + sn * h * 0.5f, p->y - sn * w * 0.5f - cs * h * 0.5f },
                { p->x + cs * w * 0.5f + sn * h * 0.5f, p->y + sn * w * 0.5f - cs * h * 0.5f },
                { p->x + cs * w * 0.5f - sn * h * 0.5f, p->y + sn * w * 0.5f + cs * h * 0.5f },
                { p->x - cs * w * 0.5f - sn * h * 0.5f, p->y - sn * w * 0.5f + cs * h * 0.5f } };
            pa_fill_poly(c, q, 4, fade(p->col, pa_clamp01(k * 3.0f)));
            break;
        }
        default:
            pa_fill_circle(c, p->x, p->y, p->size * (1.0f + (1.0f - k) * 3.0f),
                           PA_RGBA(255, 255, 255, (int)(k * 45.0f)));
            break;
        }
    }
}

static void draw_beam(PA_Canvas *c, const Beam *bm, float ox, float oy) {
    float t = bm->t, k = t / 0.75f;
    PA_Color hue = pa_shade(pa_hex(TINTS[bm->tint]), 0.15f);
    float a = (1.0f - k) * (1.0f - k);
    float bleed = L.cell * 0.35f;
    float thick = L.cell * (1.0f + 0.9f * ease_out(k));
    if (bm->row) {
        float cy = oy + L.oy + ((float)bm->index + 0.5f) * L.cell;
        PA_Paint p = pa_linear(0, cy - thick * 0.5f, 0, cy + thick * 0.5f);
        pa_stop(&p, 0.0f, fade(hue, 0.0f));
        pa_stop(&p, 0.30f, fade(hue, a * 0.62f));
        pa_stop(&p, 0.50f, fade(pa_shade(hue, 0.6f), a * 0.85f));
        pa_stop(&p, 0.70f, fade(hue, a * 0.62f));
        pa_stop(&p, 1.0f, fade(hue, 0.0f));
        pa_fill_rect_paint(c, ox + L.ox - bleed, cy - thick * 0.5f, L.side + bleed * 2.0f, thick, &p);
        if (t < 0.15f)
            pa_fill_rect(c, ox + L.ox, cy - L.cell * 0.5f, L.side, L.cell,
                         PA_RGBA(255, 255, 255, (int)(204.0f * (1.0f - t / 0.15f))));
    } else {
        float cx = ox + L.ox + ((float)bm->index + 0.5f) * L.cell;
        PA_Paint p = pa_linear(cx - thick * 0.5f, 0, cx + thick * 0.5f, 0);
        pa_stop(&p, 0.0f, fade(hue, 0.0f));
        pa_stop(&p, 0.30f, fade(hue, a * 0.62f));
        pa_stop(&p, 0.50f, fade(pa_shade(hue, 0.6f), a * 0.85f));
        pa_stop(&p, 0.70f, fade(hue, a * 0.62f));
        pa_stop(&p, 1.0f, fade(hue, 0.0f));
        pa_fill_rect_paint(c, cx - thick * 0.5f, oy + L.oy - bleed, thick, L.side + bleed * 2.0f, &p);
        if (t < 0.15f)
            pa_fill_rect(c, cx - L.cell * 0.5f, oy + L.oy, L.cell, L.side,
                         PA_RGBA(255, 255, 255, (int)(204.0f * (1.0f - t / 0.15f))));
    }
    /* Square motes drifting inside the beam, as the reference's do. */
    for (int m = 0; m < 10; m++) {
        uint32_t hsh = (uint32_t)(bm->index * 131 + m * 977 + bm->row * 7);
        float along = (float)((hsh * 2654435761u) >> 8 & 0xFFFF) / 65535.0f;
        float across = ((float)((hsh * 40503u) >> 4 & 0xFF) / 255.0f - 0.5f) * thick * 0.8f;
        float s = L.cell * (0.10f + 0.10f * (float)(m % 3) / 2.0f) * (1.0f - k * 0.5f);
        float drift = (k * L.cell * 0.8f) * ((m & 1) ? 1.0f : -1.0f);
        float px, py;
        if (bm->row) { px = ox + L.ox + along * L.side; py = oy + L.oy + ((float)bm->index + 0.5f) * L.cell + across + drift; }
        else         { py = oy + L.oy + along * L.side; px = ox + L.ox + ((float)bm->index + 0.5f) * L.cell + across + drift; }
        pa_fill_rect(c, px - s * 0.5f, py - s * 0.5f, s, s, PA_RGBA(255, 255, 255, (int)(a * 170.0f)));
    }
}

static void draw_ribbon(PA_Canvas *c, float cx, float cy, float w, float h, const char *label) {
    PA_Color dark = pa_hex(0x6A2A9A), mid = pa_hex(0x9440D0), light = pa_hex(0xB866F0);
    /* Folded tails behind the band. */
    for (int s = -1; s <= 1; s += 2) {
        float ex = cx + (float)s * w * 0.5f;
        PA_Vec2 tail[5] = {
            { ex - (float)s * h * 0.3f, cy - h * 0.25f }, { ex + (float)s * h * 0.95f, cy - h * 0.2f },
            { ex + (float)s * h * 0.6f, cy + h * 0.32f }, { ex + (float)s * h * 0.95f, cy + h * 0.82f },
            { ex - (float)s * h * 0.3f, cy + h * 0.78f } };
        pa_fill_poly(c, tail, 5, dark);
    }
    int n = 16;
    PA_Vec2 band[34];
    for (int i = 0; i <= n; i++) {
        float u = (float)i / (float)n, x = cx - w * 0.5f + w * u;
        float sag = sinf(u * PA_PI) * h * 0.18f;
        band[i].x = x; band[i].y = cy - h * 0.5f - sag;
        band[2 * n + 1 - i].x = x; band[2 * n + 1 - i].y = cy + h * 0.5f - sag;
    }
    PA_Paint g = pa_linear(0, cy - h * 0.7f, 0, cy + h * 0.5f);
    pa_stop(&g, 0.0f, light);
    pa_stop(&g, 0.55f, mid);
    pa_stop(&g, 1.0f, dark);
    pa_stroke_poly(c, band, 2 * n + 2, 1, 3.0f, pa_hex(0x4A1670));
    pa_fill_poly_paint(c, band, 2 * n + 2, &g);
    text_mid(c, label, cx, cy - h * 0.16f, h * 0.48f, PA_RGB(255, 246, 236), pa_hex(0x5A1A86), 1.7f);
}

static void draw_rays(PA_Canvas *c, float cx, float cy, float r, float rot, float a) {
    for (int i = 0; i < 14; i++) {
        float a0 = rot + (float)i * PA_TAU / 14.0f, a1 = a0 + PA_TAU / 28.0f;
        PA_Vec2 tri[3] = { { cx, cy }, { cx + cosf(a0) * r, cy + sinf(a0) * r }, { cx + cosf(a1) * r, cy + sinf(a1) * r } };
        PA_Paint p = pa_radial(cx, cy, r * 0.1f, r);
        pa_stop(&p, 0.0f, PA_RGBA(255, 240, 160, (int)(a * 120.0f)));
        pa_stop(&p, 1.0f, PA_RGBA(255, 240, 160, 0));
        pa_fill_poly_paint(c, tri, 3, &p);
    }
    PA_Paint g = pa_radial(cx, cy, 0.0f, r * 0.7f);
    pa_stop(&g, 0.0f, PA_RGBA(255, 236, 150, (int)(a * 170.0f)));
    pa_stop(&g, 1.0f, PA_RGBA(255, 236, 150, 0));
    pa_fill_ellipse_paint(c, cx, cy, r * 0.7f, r * 0.7f, &g);
}

static void restart_icon(PA_Canvas *c, float cx, float cy, float r, PA_Color col) {
    PA_Vec2 arc[20];
    int n = 0;
    for (int i = 0; i < 18; i++) {
        float a = -PA_PI * 0.35f + (float)i / 17.0f * PA_TAU * 0.80f;
        arc[n].x = cx + cosf(a) * r; arc[n].y = cy + sinf(a) * r; n++;
    }
    pa_stroke_poly(c, arc, n, 0, r * 0.34f, col);
    float a = -PA_PI * 0.35f;
    float hx = cx + cosf(a) * r, hy = cy + sinf(a) * r;
    PA_Vec2 head[3] = { { hx - r * 0.55f, hy - r * 0.35f }, { hx + r * 0.45f, hy - r * 0.55f }, { hx + r * 0.05f, hy + r * 0.45f } };
    pa_fill_poly(c, head, 3, col);
}

static void draw_board(PA_Canvas *c, float ox, float oy, int gx, int gy, int ghost_ok,
                       const int rows[N], const int cols[N], int prev_tint) {
    float bx = ox + L.ox, by = oy + L.oy, s = L.side;
    /* Soft contact shadow, a thin pale rim, then navy. */
    pa_fill_rect(c, bx - 2.0f, by + 4.0f, s + 4.0f, s + 4.0f, PA_RGBA(16, 22, 70, 60));
    pa_fill_rect(c, bx - 3.0f, by - 3.0f, s + 6.0f, s + 6.0f, pa_hex(0x6A7CC6));
    pa_fill_rect(c, bx - 1.5f, by - 1.5f, s + 3.0f, s + 3.0f, pa_hex(0x1D2348));
    pa_fill_rect(c, bx, by, s, s, pa_hex(BOARD_NAVY));
    for (int k = 1; k < N; k++) {
        pa_fill_rect(c, bx + (float)k * L.cell - 0.75f, by, 1.5f, s, pa_hex(GRID_LINE));
        pa_fill_rect(c, bx, by + (float)k * L.cell - 0.75f, s, 1.5f, pa_hex(GRID_LINE));
    }

    int previewing = prev_tint >= 0;
    PA_Color ptint = previewing ? pa_hex(TINTS[prev_tint]) : 0;
    if (previewing) {
        /* Lines the drop would finish glow in the piece's colour. */
        float pulse = 0.5f + 0.5f * sinf(B.time * 9.0f);
        for (int k = 0; k < N; k++) {
            if (rows[k]) pa_fill_rect(c, bx, by + (float)k * L.cell, s, L.cell, fade(ptint, 0.22f + 0.10f * pulse));
            if (cols[k]) pa_fill_rect(c, bx + (float)k * L.cell, by, L.cell, s, fade(ptint, 0.22f + 0.10f * pulse));
        }
    }

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            int i = idx(x, y);
            float px = bx + (float)x * L.cell, py = by + (float)y * L.cell;
            int v = B.cell[i];
            if (v >= 0) {
                PA_Color col = pa_hex(TINTS[v]);
                if (previewing && (rows[y] || cols[x])) col = ptint;
                col = grey_of(col, B.grey[i]);
                float ld = B.land[i];
                float sc = 1.0f + sinf(ld * PA_PI) * 0.07f;
                float sz = L.cell * sc, d = (sz - L.cell) * 0.5f;
                block(c, px - d, py - d, sz, col, 1.0f);
                if (ld > 0.6f) pa_fill_rect(c, px + 1, py + 1, L.cell - 2, L.cell - 2,
                                            PA_RGBA(255, 255, 255, (int)((ld - 0.6f) / 0.4f * 120.0f)));
            }
            float ct = B.clr[i];
            if (ct > -5.0f) {
                PA_Color col = pa_hex(TINTS[B.clr_tint[i]]);
                if (ct < 0.0f) block(c, px, py, L.cell, pa_mix(col, PA_RGB(255, 255, 255), 0.25f), 1.0f);
                else {
                    float k = pa_clamp01(ct / 0.32f);
                    float sc = 1.0f + 0.15f * sinf(pa_clamp01(k * 3.0f) * PA_PI) - k;
                    if (sc > 0.02f) {
                        float sz = L.cell * sc, d = (L.cell - sz) * 0.5f;
                        block(c, px + d, py + d, sz, pa_mix(col, PA_RGB(255, 255, 255), pa_clamp01(1.0f - k * 3.0f) * 0.8f), 1.0f - k * 0.3f);
                    }
                }
            }
        }
    }

    if (ghost_ok) {
        const Shape *sh = &SHAPES[B.tray[B.drag].shape];
        for (int i = 0; i < sh->count; i++) {
            float px = bx + (float)(gx + sh->cx[i]) * L.cell, py = by + (float)(gy + sh->cy[i]) * L.cell;
            pa_fill_rect(c, px + 1.0f, py + 1.0f, L.cell - 2.0f, L.cell - 2.0f, PA_RGBA(255, 255, 255, 76));
            pa_stroke_rect(c, px + 1.5f, py + 1.5f, L.cell - 3.0f, L.cell - 3.0f, 2.0f, PA_RGBA(255, 255, 255, 190));
        }
    }

    if (previewing) {
        /* Motes twinkling over the lines about to go. */
        for (int k = 0; k < N; k++) {
            for (int r = 0; r < 2; r++) {
                if (!(r ? cols[k] : rows[k])) continue;
                for (int m = 0; m < 12; m++) {
                    uint32_t h = (uint32_t)(k * 7919 + m * 104729 + r * 31) * 2654435761u;
                    float along = (float)(h >> 16 & 0xFFFF) / 65535.0f;
                    float across = (float)(h >> 4 & 0xFFF) / 4095.0f;
                    float ph = B.time * (1.2f + (float)(m % 4) * 0.4f) + (float)m;
                    float tw = 0.5f + 0.5f * sinf(ph * 3.0f);
                    float drift = pa_wrapf(ph * 0.25f, 1.0f);
                    float px, py;
                    if (r == 0) { px = bx + along * s; py = by + ((float)k + across) * L.cell - drift * L.cell * 0.6f; }
                    else        { py = by + along * s; px = bx + ((float)k + across) * L.cell - drift * L.cell * 0.3f; }
                    float sz = L.cell * (0.05f + 0.05f * tw);
                    pa_fill_rect(c, px, py, sz, sz, fade(pa_shade(ptint, 0.5f), 0.4f + 0.6f * tw));
                }
            }
        }
    }

    for (int b = 0; b < MAX_BEAMS; b++) if (B.beams[b].t > 0.0f) draw_beam(c, &B.beams[b], ox, oy);
}

static void draw_hud(PA_Canvas *c) {
    char buf[32];
    int best = B.best_start;
    if ((int)B.shown > best && B.passed_best) best = (int)B.shown;
    float cw = L.crown_s;
    draw_crown(c, L.crown_x + cw * 0.5f, L.hud_y, cw);
    snprintf(buf, sizeof(buf), "%d", best);
    pa_text_bold(c, buf, L.crown_x + cw * 1.22f, L.hud_y - L.best_size * 0.5f, L.best_size,
                 pa_hex(GOLD), pa_hex(0x8A4A00), PA_ALIGN_LEFT, L.best_size * 0.06f, 1.5f);

    snprintf(buf, sizeof(buf), "%d", (int)(B.shown + 0.5f));
    float sz = L.score_size * (1.0f + 0.06f * B.punch);
    float sx = L.land ? L.ox + L.side * 0.5f : (float)L.w * 0.5f;
    text_mid(c, buf, sx, L.score_y, sz, PA_RGB(255, 255, 255), pa_hex(0x26357E), 2.2f);
}

static void draw_callouts(PA_Canvas *c, float ox, float oy) {
    float bcx = ox + L.ox + L.side * 0.5f;
    for (int f = 0; f < MAX_FLOATS; f++) {
        const FloatText *ft = &B.floats[f];
        if (ft->t <= 0.0f) continue;
        float t = ft->t;
        float a = t < 0.75f ? 1.0f : 1.0f - (t - 0.75f) / 0.35f;
        float sc = 0.6f + 0.4f * ease_back(t / 0.22f);
        float size = L.side * 0.075f * sc;
        float x = pa_clampf(ox + ft->x, ox + L.ox + size * 1.5f, ox + L.ox + L.side - size * 1.5f);
        float fy0 = ft->y;
        /* Keep the score clear of the praise and combo callouts. */
        if (B.combo_t > 0.0f && fabsf(fy0 - B.combo_y) < L.cell * 1.3f) fy0 = B.combo_y + L.cell * 1.75f;
        if (B.praise_t > 0.0f && fabsf(fy0 - (L.oy + L.side * 0.36f)) < L.cell * 1.2f) fy0 = L.oy + L.side * 0.36f + L.cell * 1.3f;
        if (B.combo_t > 0.0f && fabsf(fy0 - B.combo_y) < L.cell * 1.3f) fy0 = B.combo_y + L.cell * 1.75f;
        float y = oy + fy0 - ease_out(t / 1.1f) * L.cell * 0.5f;
        text_mid(c, ft->txt, x, y, size, fade(PA_RGB(255, 255, 255), a),
                 fade(pa_shade(pa_hex(TINTS[ft->tint]), -0.55f), a), 1.9f);
    }

    int both = B.praise_t > 0.0f && B.combo_t > 0.0f;
    if (B.praise_t > 0.0f) {
        float t = B.praise_t;
        float a = t < 1.2f ? 1.0f : 1.0f - (t - 1.2f) / 0.4f;
        float sc = t < 0.14f ? 0.3f + 0.95f * (t / 0.14f) : (t < 0.28f ? 1.25f - 0.25f * ((t - 0.14f) / 0.14f) : 1.0f);
        float size = L.side * 0.115f;
        float fitw = pa_text_width(B.praise, size, size * 0.06f);
        if (fitw > L.side * 0.86f) size *= L.side * 0.86f / fitw;
        size *= sc;
        float y = oy + L.oy + L.side * (both ? 0.36f : 0.45f) - t * L.cell * 0.25f;
        soft_glow(c, bcx, y, pa_text_width(B.praise, size, 0) * 0.42f, size * 1.15f, pa_hex(0xFFB020), a);
        text_mid(c, B.praise, bcx, y, size, fade(pa_hex(0xFFD84A), a), fade(pa_hex(0x8A3600), a), 2.4f);
    }

    if (B.combo_t > 0.0f) {
        float t = B.combo_t;
        float a = t < 1.1f ? 1.0f : 1.0f - (t - 1.1f) / 0.4f;
        float y = oy + B.combo_y;
        float bh = L.cell * 1.5f * pa_clamp01(t / 0.12f);
        /* A dark glowing band across the board behind the callout. */
        PA_Paint band = pa_linear(0, y - bh * 0.5f, 0, y + bh * 0.5f);
        pa_stop(&band, 0.0f, PA_RGBA(80, 200, 255, 0));
        pa_stop(&band, 0.12f, PA_RGBA(80, 200, 255, (int)(a * 140.0f)));
        pa_stop(&band, 0.22f, PA_RGBA(14, 30, 90, (int)(a * 200.0f)));
        pa_stop(&band, 0.78f, PA_RGBA(14, 30, 90, (int)(a * 200.0f)));
        pa_stop(&band, 0.88f, PA_RGBA(80, 200, 255, (int)(a * 140.0f)));
        pa_stop(&band, 1.0f, PA_RGBA(80, 200, 255, 0));
        pa_fill_rect_paint(c, ox + L.ox - L.cell * 0.3f, y - bh * 0.5f, L.side + L.cell * 0.6f, bh, &band);

        char num[12];
        snprintf(num, sizeof(num), "%d", B.combo_show);
        float sc = t < 0.12f ? 0.4f + 0.8f * (t / 0.12f) : (t < 0.24f ? 1.2f - 0.2f * ((t - 0.12f) / 0.12f) : 1.0f);
        float ws = L.side * 0.085f * sc, ns = L.side * 0.12f * sc;
        float w1 = pa_text_width("Combo", ws, ws * 0.06f), w2 = pa_text_width(num, ns, ns * 0.06f);
        float gap = ws * 0.45f, x0 = bcx - (w1 + gap + w2) * 0.5f;
        soft_glow(c, x0 + w1 + gap + w2 * 0.5f, y, w2 * 0.4f, ns * 1.1f, pa_hex(0xFFD040), a);
        pa_text_bold(c, "Combo", x0, y - ws * 0.5f, ws, fade(PA_RGB(240, 248, 255), a), fade(pa_hex(0x1A3A9A), a),
                     PA_ALIGN_LEFT, ws * 0.06f, 2.0f);
        pa_text_bold(c, num, x0 + w1 + gap, y - ns * 0.55f, ns, fade(pa_hex(0xFFD23A), a), fade(pa_hex(0x7A3000), a),
                     PA_ALIGN_LEFT, ns * 0.06f, 2.3f);
    }

    if (B.toast_on && B.toast_t > 0.0f) {
        float t = B.toast_t;
        float a = t < 1.6f ? 1.0f : 1.0f - (t - 1.6f) / 0.4f;
        float sc = t < 0.16f ? 0.3f + 1.0f * (t / 0.16f) : (t < 0.3f ? 1.3f - 0.3f * ((t - 0.16f) / 0.14f) : 1.0f);
        float y = oy + L.oy + L.side * 0.40f;
        float s1 = L.side * 0.085f * sc, s2 = L.side * 0.13f * sc;
        soft_glow(c, bcx, y + s2 * 0.3f, L.side * 0.22f, s2 * 1.2f, pa_hex(0xFFB020), a);
        text_mid(c, "NEW", bcx, y - s1 * 0.55f, s1, fade(pa_hex(0xFFE27A), a), fade(pa_hex(0x8A3600), a), 2.2f);
        text_mid(c, "BEST!", bcx, y + s2 * 0.55f, s2, fade(pa_hex(0xFFC83A), a), fade(pa_hex(0x8A3600), a), 2.4f);
        for (int i = 0; i < 4; i++) {
            float ang = (float)i * 1.7f + t * 2.0f;
            star4(c, bcx + cosf(ang) * L.side * 0.24f, y + sinf(ang) * s2 * 0.9f,
                  L.cell * 0.18f * (0.6f + 0.4f * sinf(t * 10.0f + (float)i)), fade(PA_RGB(255, 250, 220), a));
        }
    }
}

static void draw_tray(PA_Canvas *c) {
    if (B.phase == 2) return;
    for (int i = 0; i < TRAY; i++) {
        if (B.tray[i].shape < 0 || B.drag == i || B.ret_slot == i) continue;
        const Shape *sh = &SHAPES[B.tray[i].shape];
        float pop = ease_back(pa_clamp01(B.tray[i].pop));
        if (pop <= 0.02f) continue;
        float tc = L.tcell * pop;
        float sx = L.slot_cx[i] - (float)sh->w * tc * 0.5f;
        float sy = L.slot_cy[i] - (float)sh->h * tc * 0.5f;
        PA_Color tint = pa_hex(TINTS[B.tray[i].tint]);
        float alpha = 1.0f;
        if (B.phase >= 1) tint = grey_of(tint, pa_clamp01((B.phase_t - 0.5f) / 0.2f));
        else if (!any_fits(B.tray[i].shape)) { tint = grey_of(tint, 0.85f); alpha = 0.75f; }
        shape_shadow(c, B.tray[i].shape, sx, sy, tc, tc * 0.14f, 50);
        draw_shape(c, B.tray[i].shape, tint, sx, sy, tc, alpha);
    }
}

static void draw_results(PA_Canvas *c) {
    float t = B.phase_t;
    float k = ease_out(t / 0.35f);
    pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(14, 18, 52, (int)(k * 90.0f)));
    pa_fill_rect(c, L.ox, L.oy, L.side, L.side, PA_RGBA(10, 12, 36, (int)(k * 120.0f)));
    float cx = L.land ? L.ox + L.side * 0.5f : (float)c->w * 0.5f;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", (int)(B.res_shown + 0.5f));

    if (B.res_new_best) {
        float ck = ease_back(pa_clamp01((t - 0.1f) / 0.45f));
        float crown_y = L.oy + L.side * 0.20f;
        draw_rays(c, cx, crown_y, L.side * 0.48f * ck, t * 0.35f, ck);
        if (ck > 0.02f) draw_crown(c, cx, crown_y, L.side * 0.34f * ck);
        float rk = ease_back(pa_clamp01((t - 0.3f) / 0.4f));
        if (rk > 0.02f) draw_ribbon(c, cx, L.oy + L.side * 0.46f, L.side * 0.66f * rk, L.side * 0.15f * rk, "NEW BEST");
        float sk = ease_back(pa_clamp01((t - 0.35f) / 0.35f));
        float ss = L.side * 0.17f * sk;
        if (sk > 0.02f) {
            soft_glow(c, cx, L.oy + L.side * 0.68f, L.side * 0.2f, ss, pa_hex(0xFFB020), 0.8f);
            text_mid(c, buf, cx, L.oy + L.side * 0.68f, ss, pa_hex(0xFFD23A), pa_hex(0x7A3000), 2.5f);
        }
        for (int i = 0; i < 5; i++) {
            float ang = (float)i * 1.256f + t * 0.8f;
            star4(c, cx + cosf(ang) * L.side * 0.3f, crown_y + sinf(ang) * L.side * 0.16f,
                  L.cell * 0.16f * (0.5f + 0.5f * sinf(t * 7.0f + (float)i * 2.0f)), PA_RGBA(255, 250, 220, (int)(ck * 255.0f)));
        }
    } else {
        float a = pa_clamp01((t - 0.1f) / 0.3f);
        /* A card over the greyed board keeps the numbers legible. */
        float pk = ease_back(pa_clamp01(t / 0.3f));
        if (pk > 0.02f) {
            float pw = L.side * 0.84f * pk, ph = L.side * 0.78f * pk;
            float px = cx - pw * 0.5f, py = L.oy + L.side * 0.47f - ph * 0.5f;
            pa_round_rect(c, px - 3.0f, py - 3.0f, pw + 6.0f, ph + 6.0f, L.side * 0.06f, pa_hex(0x6A7CC6));
            PA_Paint card = pa_linear(0, py, 0, py + ph);
            pa_stop(&card, 0.0f, pa_hex(0x2E3A86));
            pa_stop(&card, 1.0f, pa_hex(0x1E2558));
            pa_round_rect_paint(c, px, py, pw, ph, L.side * 0.055f, &card);
        }
        text_mid(c, "Score", cx, L.oy + L.side * 0.18f, L.side * 0.06f, fade(pa_hex(0xC8D4FF), a), fade(pa_hex(0x141C50), a), 1.5f);
        float sk = ease_back(pa_clamp01((t - 0.2f) / 0.35f));
        if (sk > 0.02f) text_mid(c, buf, cx, L.oy + L.side * 0.35f, L.side * 0.15f * sk, PA_RGB(255, 255, 255), pa_hex(0x141C50), 2.4f);
        char best[32];
        snprintf(best, sizeof(best), "%d", g_best > B.best_start ? g_best : B.best_start);
        float bs = L.side * 0.07f;
        float tw = pa_text_width(best, bs, bs * 0.06f), cw = bs * 1.6f;
        float x0 = cx - (cw + bs * 0.4f + tw) * 0.5f, y = L.oy + L.side * 0.68f;
        text_mid(c, "Best Score", cx, L.oy + L.side * 0.55f, L.side * 0.045f, fade(pa_hex(0xC8D4FF), a), fade(pa_hex(0x141C50), a), 1.4f);
        if (a > 0.02f) {
            draw_crown(c, x0 + cw * 0.5f, y, cw);
            pa_text_bold(c, best, x0 + cw + bs * 0.4f, y - bs * 0.5f, bs, fade(pa_hex(GOLD), a), fade(pa_hex(0x8A4A00), a),
                         PA_ALIGN_LEFT, bs * 0.06f, 1.6f);
        }
    }

    /* Restart button: green, a lip underneath, a replay arrow and a label. */
    float bk = ease_back(pa_clamp01((t - 0.9f) / 0.35f));
    if (bk > 0.02f) {
        float pulse = 1.0f + 0.03f * sinf(t * 5.0f);
        float w = L.btn_w * bk * pulse, h = L.btn_h * bk * pulse;
        float x = L.btn_x - w * 0.5f, y = L.btn_y - h * 0.5f;
        pa_round_rect(c, x, y + h * 0.10f, w, h, h * 0.32f, pa_hex(0x1E7A2E));
        PA_Paint g = pa_linear(0, y, 0, y + h);
        pa_stop(&g, 0.0f, pa_hex(0x7CE86A));
        pa_stop(&g, 0.5f, pa_hex(0x3EC64A));
        pa_stop(&g, 1.0f, pa_hex(0x2CA83A));
        pa_round_rect_paint(c, x, y, w, h, h * 0.32f, &g);
        pa_round_rect(c, x + h * 0.2f, y + h * 0.08f, w - h * 0.4f, h * 0.18f, h * 0.09f, PA_RGBA(255, 255, 255, 70));
        float ts = h * 0.40f;
        float lw = pa_text_width("Play Again", ts, ts * 0.06f);
        float ir = h * 0.2f, total = ir * 2.0f + h * 0.18f + lw;
        float ix = L.btn_x - total * 0.5f + ir;
        restart_icon(c, ix, L.btn_y, ir, PA_RGB(255, 255, 255));
        pa_text_bold(c, "Play Again", ix + ir + h * 0.18f, L.btn_y - ts * 0.5f, ts, PA_RGB(255, 255, 255),
                     pa_hex(0x1A6A28), PA_ALIGN_LEFT, ts * 0.06f, 1.6f);
    }
}

static void storm_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) compute_layout(c->w, c->h);

    PA_Paint bg = pa_linear(0, 0, 0, (float)c->h);
    pa_stop(&bg, 0.0f, pa_hex(BG_TOP));
    pa_stop(&bg, 1.0f, pa_hex(BG_BOT));
    pa_fill_rect_paint(c, 0, 0, (float)c->w, (float)c->h, &bg);
    /* A faint light pool behind the board keeps the flat field from going dead. */
    PA_Paint pool = pa_radial(L.ox + L.side * 0.5f, L.oy + L.side * 0.45f, L.side * 0.2f, L.side * 0.95f);
    pa_stop(&pool, 0.0f, PA_RGBA(120, 150, 255, 34));
    pa_stop(&pool, 1.0f, PA_RGBA(120, 150, 255, 0));
    pa_fill_rect_paint(c, 0, L.oy - L.side * 0.5f, (float)c->w, L.side * 2.0f, &pool);

    float sx = 0.0f, sy = 0.0f;
    if (B.shake > 0.0f) {
        float m = L.cell * 0.10f * pa_clamp01(B.shake / 0.3f);
        sx = sinf(B.time * 83.0f) * m;
        sy = cosf(B.time * 67.0f) * m;
    }

    int gx = -99, gy = -99, ghost_ok = 0, prev_tint = -1;
    int rows[N] = { 0 }, cols[N] = { 0 };
    if (B.drag >= 0 && B.lift > 0.5f) {
        drag_target(&gx, &gy);
        ghost_ok = fits(B.tray[B.drag].shape, gx, gy);
        if (ghost_ok && completing_lines(B.tray[B.drag].shape, gx, gy, rows, cols) > 0)
            prev_tint = B.tray[B.drag].tint;
    }
    draw_board(c, sx, sy, gx, gy, ghost_ok, rows, cols, prev_tint);
    draw_parts(c, 0);
    draw_tray(c);

    if (B.ret_slot >= 0) {
        const Shape *sh = &SHAPES[B.tray[B.ret_slot].shape];
        float k = ease_out(B.ret_t);
        float cs = pa_lerpf(B.ret_cell, L.tcell, k);
        float cx = pa_lerpf(B.ret_x, L.slot_cx[B.ret_slot], k), cy = pa_lerpf(B.ret_y, L.slot_cy[B.ret_slot], k);
        draw_shape(c, B.tray[B.ret_slot].shape, pa_hex(TINTS[B.tray[B.ret_slot].tint]),
                   cx - (float)sh->w * cs * 0.5f, cy - (float)sh->h * cs * 0.5f, cs, 1.0f);
    }
    if (B.drag >= 0) {
        float px, py, cs;
        drag_geom(&px, &py, &cs);
        shape_shadow(c, B.tray[B.drag].shape, px, py, cs, cs * 0.22f * ease_out(B.lift), 70);
        draw_shape(c, B.tray[B.drag].shape, pa_hex(TINTS[B.tray[B.drag].tint]), px, py, cs, 1.0f);
    }

    draw_callouts(c, sx, sy);
    draw_hud(c);

    if (B.phase == 1 && B.phase_t > 1.1f) {
        float t = B.phase_t - 1.1f;
        float a = t < 1.6f ? 1.0f : pa_clamp01(1.0f - (t - 1.6f) / 0.3f);
        float sc = ease_back(pa_clamp01(t / 0.3f));
        float y = L.oy + L.side * 0.5f;
        float bh = L.cell * 1.7f * sc;
        PA_Paint band = pa_linear(0, y - bh * 0.5f, 0, y + bh * 0.5f);
        pa_stop(&band, 0.0f, PA_RGBA(10, 14, 44, 0));
        pa_stop(&band, 0.2f, PA_RGBA(10, 14, 44, (int)(a * 210.0f)));
        pa_stop(&band, 0.8f, PA_RGBA(10, 14, 44, (int)(a * 210.0f)));
        pa_stop(&band, 1.0f, PA_RGBA(10, 14, 44, 0));
        pa_fill_rect_paint(c, 0, y - bh * 0.5f, (float)c->w, bh, &band);
        if (sc > 0.05f)
            text_mid(c, "No more space", L.ox + L.side * 0.5f, y, L.side * 0.085f * sc,
                     fade(PA_RGB(255, 255, 255), a), fade(pa_hex(0x1A2468), a), 2.2f);
    }
    if (B.phase == 2) draw_results(c);
    draw_parts(c, 1);

    if (B.phase == 2) pa_hub_hide_pause();
    else pa_hub_pause_anchor(L.pause_x, L.hud_y, L.pause_r);
}

static void storm_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    PA_Paint bg = pa_linear(0, y, 0, y + h);
    pa_stop(&bg, 0.0f, pa_hex(BG_TOP));
    pa_stop(&bg, 1.0f, pa_hex(BG_BOT));
    pa_fill_rect_paint(c, x, y, w, h, &bg);
    int n = 6;
    float cell = (w < h ? w : h) * 0.84f / (float)n;
    float ox = x + (w - cell * (float)n) * 0.5f;
    float oy = y + (h - cell * (float)n) * 0.5f;
    pa_fill_rect(c, ox - 2.0f, oy - 2.0f, cell * n + 4.0f, cell * n + 4.0f, pa_hex(0x6A7CC6));
    pa_fill_rect(c, ox, oy, cell * n, cell * n, pa_hex(BOARD_NAVY));
    for (int k = 1; k < n; k++) {
        pa_fill_rect(c, ox + (float)k * cell - 0.5f, oy, 1.0f, cell * n, pa_hex(GRID_LINE));
        pa_fill_rect(c, ox, oy + (float)k * cell - 0.5f, cell * n, 1.0f, pa_hex(GRID_LINE));
    }

    /* A row filling and bursting out, so the tile shows the payoff. */
    float phase = pa_wrapf(t * 0.42f, 1.0f);
    int fill_row = 3;
    for (int gy = 0; gy < n; gy++) {
        for (int gx = 0; gx < n; gx++) {
            float px = ox + (float)gx * cell, py = oy + (float)gy * cell;
            int pattern = ((gx * 3 + gy * 5) % 7) < 3;
            if (gy == fill_row) {
                float lead = (float)gx / (float)n;
                if (phase > lead && phase < 0.8f) {
                    block(c, px, py, cell, pa_hex(TINTS[T_PURPLE]), 1.0f);
                } else if (phase >= 0.8f) {
                    float k = (phase - 0.8f) / 0.2f;
                    float s = cell * (1.0f - k);
                    block(c, px + (cell - s) * 0.5f, py + (cell - s) * 0.5f, s, pa_mix(pa_hex(TINTS[T_PURPLE]), PA_RGB(255, 255, 255), 1.0f - k), 1.0f);
                }
            } else if (pattern) {
                block(c, px, py, cell, pa_hex(TINTS[(gx + gy) % TINT_COUNT]), 1.0f);
            }
        }
    }
    if (phase >= 0.8f) {
        float k = (phase - 0.8f) / 0.2f;
        float cy = oy + ((float)fill_row + 0.5f) * cell, th = cell * (1.0f + k);
        PA_Paint p = pa_linear(0, cy - th * 0.5f, 0, cy + th * 0.5f);
        pa_stop(&p, 0.0f, PA_RGBA(190, 120, 255, 0));
        pa_stop(&p, 0.5f, PA_RGBA(255, 255, 255, (int)((1.0f - k) * 220.0f)));
        pa_stop(&p, 1.0f, PA_RGBA(190, 120, 255, 0));
        pa_fill_rect_paint(c, ox - cell * 0.3f, cy - th * 0.5f, cell * n + cell * 0.6f, th, &p);
    }
}

const PA_Game PA_GAME_BLOCKSTORM = {
    "blockstorm", "Block Storm", "Puzzle",
    "Eight by eight. Drop pieces, clear rows and columns, and never run out of room.",
    PA_RGB(248, 196, 28),
    storm_start, storm_stop, storm_update, storm_render, storm_thumb
};
