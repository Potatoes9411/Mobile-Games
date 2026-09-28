/* ===========================================================================
   HELIX DROP - native port
   A ball falls down a segmented tower. Drag to spin the tower and line a gap up
   under it. Land on a safe wedge and it bounces; land on a red one and the run
   ends; fall through three floors without touching anything and the ball turns
   into a wrecking ball that smashes straight through the next one.

   The tower is drawn as a stack of squashed annular rings. There is no real 3D
   here and there does not need to be: every ring sits at a known depth, so the
   whole scene is ellipse arcs sorted back to front.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define SEGMENTS   12
#define MAX_FLOORS 40

enum { SEG_SAFE = 0, SEG_DEADLY = 1, SEG_GAP = 2 };

typedef struct {
    uint32_t sky_top, sky_bot;     /* pale sky, never dark: the plates' mean v is 0.8 */
    uint32_t plate;                /* the one saturated theme colour for the level */
    uint32_t ball;
} Palette;

/* Measured off the Helix Jump plates: a pale, almost white field, a white
   pillar, and one saturated platform colour per level with red hazards. The
   ball contrasts with the platforms because its paint is left on them. */
static const Palette PALETTES[] = {
    { 0xA6DAFF, 0xEEF8FF, 0x2FCF4F, 0xFF5A36 },
    { 0xFFD2BE, 0xFFF4EC, 0x7A4BF0, 0x20C4E8 },
    { 0xC4F0E0, 0xF2FFF9, 0xFF8A1E, 0x3B6CF5 },
    { 0xE4D6FF, 0xF8F3FF, 0x1FA2F2, 0xFFC21C },
    { 0xFFEFB0, 0xFFFBEA, 0x14BFAE, 0xF0408E },
};
#define PALETTE_COUNT ((int)(sizeof(PALETTES) / sizeof(PALETTES[0])))

#define HAZARD      0xE3242B
#define MAX_SPLATS  4
#define MAX_FLOATS  6

typedef struct {
    unsigned char seg[SEGMENTS];
    float y;
    float broken;      /* 1 -> 0 while the floor shatters */
    float hit;         /* landing squash */
    int   smashed;     /* gone for good once the shatter has played out */
    int   splat_count;
    float splat_a[MAX_SPLATS];     /* tower-frame angle of each paint splat */
    unsigned char splat_seed[MAX_SPLATS];
} Floor;

typedef struct { char text[12]; float t; float x; } Floater;

typedef struct {
    Floor  floors[MAX_FLOORS];
    int    floor_count;
    Palette pal;

    float  ball_y, ball_v;
    float  spin, spin_vel;
    int    combo;              /* consecutive floors fallen through */
    int    smashing;
    float  cam_y, shake;
    int    level, score, passed;
    int    over, won;
    float  over_t, time;
    int    dragging;
    float  drag_x;
    float  squish;             /* ball squash on landing, 1 -> 0 */
    char   banner[24];
    float  banner_t;
    Floater floats[MAX_FLOATS];
} Helix;

static Helix H;
static int   g_best, g_best_loaded;

static struct {
    int w, h;
    float cx, radius, inner, squash, spacing, thickness, ball_screen_y, ball_r;
} L;

static void compute_layout(int w, int h) {
    L.w = w; L.h = h;
    L.cx = (float)w * 0.5f;
    /* The plates frame the tower close: platforms run nearly to the screen
       edges and the pillar is about a third of the width. */
    float r = (float)w * 0.46f;
    if (r > (float)h * 0.30f) r = (float)h * 0.30f;
    L.radius = r;
    L.inner = r * 0.34f;
    L.squash = 0.36f;
    L.spacing = (float)h * 0.20f > 90.0f ? (float)h * 0.20f : 90.0f;
    L.thickness = r * 0.10f > 8.0f ? r * 0.10f : 8.0f;
    L.ball_r = r * 0.13f;
    L.ball_screen_y = (float)h * 0.30f;
}

static float screen_y(float y) { return L.ball_screen_y + (y - H.cam_y) * L.spacing; }

/* ------------------------------------------------------------- generation */
static void build_tower(int level) {
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)(level * 48271 + 7));

    int count = 12 + (level * 2 < 26 ? level * 2 : 26);
    if (count > MAX_FLOORS) count = MAX_FLOORS;
    H.floor_count = count;

    for (int i = 0; i < count; i++) {
        Floor *f = &H.floors[i];
        memset(f, 0, sizeof(*f));
        f->y = (float)(i + 1);

        float difficulty = pa_clamp01((float)i / (float)count) * pa_clamp01((float)level / 12.0f);

        /* Every floor gets one guaranteed run of gaps, so there is always a way
           through. A generator that can produce a sealed floor produces an
           unwinnable level, and no amount of tuning elsewhere recovers that. */
        int gap_width = 4 - level / 6;
        if (gap_width < 2) gap_width = 2;
        int gap_start = pa_rng_int(&r, 0, SEGMENTS - 1);
        for (int g = 0; g < gap_width; g++) f->seg[(gap_start + g) % SEGMENTS] = SEG_GAP;

        int extra = pa_rng_int(&r, 0, 2);
        for (int e = 0; e < extra; e++) f->seg[pa_rng_int(&r, 0, SEGMENTS - 1)] = SEG_GAP;

        int deadly = (int)(pa_lerpf(1.0f, 5.0f, difficulty) + 0.5f) + (i == 0 ? -1 : 0);
        for (int d = 0; d < deadly; d++) {
            int slot = pa_rng_int(&r, 0, SEGMENTS - 1);
            if (f->seg[slot] == SEG_GAP) continue;
            f->seg[slot] = SEG_DEADLY;
        }
    }
}

static void banner(const char *t) {
    snprintf(H.banner, sizeof(H.banner), "%s", t);
    H.banner_t = 1.4f;
}

static void helix_start(void) {
    if (!g_best_loaded) { g_best = pa_save_get("helix.best", 0); g_best_loaded = 1; }
    if (L.w == 0) compute_layout(540, 960);

    int level = pa_save_get("helix.level", 1);
    if (level < 1) level = 1;

    int score = H.score;
    memset(&H, 0, sizeof(H));
    H.level = level;
    H.score = score;
    H.pal = PALETTES[(level - 1) % PALETTE_COUNT];
    if (L.w == 0) compute_layout(540, 960);
    build_tower(level);
    H.ball_y = 0.0f;
    H.dragging = -1;
}

static void helix_stop(void) { }

static void floater(const char *text) {
    for (int i = 0; i < MAX_FLOATS; i++) {
        if (H.floats[i].t > 0.0f) continue;
        snprintf(H.floats[i].text, sizeof(H.floats[i].text), "%s", text);
        H.floats[i].t = 1.0f;
        H.floats[i].x = (float)((i % 3) - 1) * 18.0f;
        return;
    }
}

/* --------------------------------------------------------------- physics */
/** Which segment sits under the ball right now, given the tower's rotation. */
static int segment_under_ball(void) {
    /* The ball is fixed at the front of the tower, facing the camera (screen
       angle a quarter turn, where the ellipse is lowest), so "under the ball"
       is always that world angle; spinning moves the segments past it. This
       used to test angle zero - the right-hand edge - so the wedge the player
       saw under the ball was not the one the physics landed on. */
    float a = pa_wrapf(PA_TAU * 0.25f - H.spin, PA_TAU);
    int seg = (int)(a / PA_TAU * (float)SEGMENTS);
    return seg % SEGMENTS;
}

static void die(void) {
    if (H.over) return;
    H.over = 1;
    H.over_t = 0.0f;
    H.shake = 1.0f;
    pa_sfx("lose");
    if (H.score > g_best) {
        g_best = H.score;
        pa_save_set("helix.best", g_best);
        pa_save_flush();
    }
}

static void helix_update(float dt, const PA_Input *in) {
    H.time += dt;
    if (H.banner_t > 0.0f) H.banner_t -= dt;
    if (H.shake > 0.0f) H.shake = H.shake > dt * 3.0f ? H.shake - dt * 3.0f : 0.0f;
    if (H.squish > 0.0f) H.squish = H.squish > dt * 6.0f ? H.squish - dt * 6.0f : 0.0f;
    for (int i = 0; i < MAX_FLOATS; i++)
        if (H.floats[i].t > 0.0f) H.floats[i].t -= dt * 1.1f;


    if (H.over) {
        H.over_t += dt;
        if (H.over_t > 2.4f) {
            if (H.won) pa_save_set("helix.level", H.level + 1);
            pa_save_flush();
            H.score = 0;
            helix_start();
        }
        return;
    }

    /* Spin. Direct drag with inertia and damping on release - an abrupt stop
       makes lining a gap up feel like fighting the control. */
    if (in->pressed) { H.dragging = 1; H.drag_x = in->x; }
    if (in->down && H.dragging > 0) {
        float delta = (in->x - H.drag_x) * 0.011f;
        H.spin += delta;
        H.spin_vel = delta / (dt > 0.0001f ? dt : 0.0001f);
        H.drag_x = in->x;
    }
    if (in->released) H.dragging = -1;
    if (H.dragging < 0) {
        H.spin += H.spin_vel * dt;
        H.spin_vel *= expf(-4.2f * dt);
        if (fabsf(H.spin_vel) < 0.02f) H.spin_vel = 0.0f;
    }
    float key = 0.0f;
    if (in->keys[PA_KEY_LEFT])  key -= 1.0f;
    if (in->keys[PA_KEY_RIGHT]) key += 1.0f;
    if (key != 0.0f) { H.spin += key * 2.4f * dt; H.spin_vel = key * 2.4f; }

    /* Fall. */
    float prev_y = H.ball_y;
    H.ball_v += 22.0f * dt;
    H.ball_y += H.ball_v * dt;

    /*
     * Continuous collision. At full fall speed the ball covers more than a
     * floor's thickness in one step, so testing only the current position lets
     * it tunnel straight through. Every floor crossed between the previous
     * position and this one is considered, nearest first.
     */
    if (H.ball_v > 0.0f) {
        for (int i = 0; i < H.floor_count; i++) {
            Floor *f = &H.floors[i];
            if (f->smashed || f->y <= prev_y || f->y > H.ball_y) continue;

            int seg = segment_under_ball();
            int kind = f->seg[seg];

            if (kind == SEG_GAP) {
                H.combo++;
                H.passed++;
                /* Chained floors score more each, as the reference's stacked
                   "+N" readouts do. */
                int gain = 10 * H.combo;
                H.score += gain;
                char t[12];
                snprintf(t, sizeof(t), "+%d", gain);
                floater(t);
                if (H.combo == 3) { H.smashing = 1; banner("SMASH"); pa_sfx("levelup"); }
                continue;
            }

            if (H.smashing) {
                /* A wrecking ball goes through a hazard floor as happily as a
                   safe one - that is the entire reward for the combo. */
                f->broken = 1.0f;
                f->smashed = 1;
                H.score += 40;
                H.passed++;
                H.shake = 0.8f;
                pa_sfx("boom");
                continue;
            }

            if (kind == SEG_DEADLY) {
                H.ball_y = f->y;
                die();
                return;
            }

            /* Safe landing: snap to the surface and bounce. */
            H.ball_y = f->y;
            H.ball_v = -8.4f;
            f->hit = 1.0f;
            H.squish = 1.0f;
            {
                /* Leave a splat of the ball's paint where it landed; the oldest
                   is overwritten once a floor has a few. */
                int k = f->splat_count < MAX_SPLATS ? f->splat_count++ : (int)(H.time * 7.0f) % MAX_SPLATS;
                f->splat_a[k] = pa_wrapf(PA_TAU * 0.25f - H.spin, PA_TAU);
                f->splat_seed[k] = (unsigned char)((int)(H.time * 97.0f) & 255);
            }
            H.combo = 0;
            H.smashing = 0;
            pa_sfx("pop");
            break;
        }
    }

    for (int i = 0; i < H.floor_count; i++) {
        Floor *f = &H.floors[i];
        if (f->broken > 0.0f) f->broken = f->broken > dt * 2.2f ? f->broken - dt * 2.2f : 0.0f;
        if (f->hit > 0.0f) f->hit = f->hit > dt * 5.0f ? f->hit - dt * 5.0f : 0.0f;
    }

    /* Cleared the tower. */
    if (H.ball_y > (float)H.floor_count + 1.0f && !H.over) {
        H.over = 1;
        H.won = 1;
        H.over_t = 0.0f;
        H.ball_y = (float)H.floor_count + 1.0f;   /* resting on the finish disc */
        H.squish = 1.0f;
        H.score += 250;
        banner("CLEARED");
        pa_sfx("win");
        if (H.score > g_best) {
            g_best = H.score;
            pa_save_set("helix.best", g_best);
        }
    }

    H.cam_y = pa_approach(H.cam_y, H.ball_y, 9.0f, dt);
}

/* ---------------------------------------------------------------- drawing */
/*
 * The tower is a stack of annular rings round a white pillar, drawn back to
 * front: lowest ring first, since the camera looks down on the tower and a
 * higher ring's near edge overhangs the far edge of the one below. Each ring is
 * split at the horizon into a far half (behind the pillar) and a near half (in
 * front of it), with the pillar section above that ring painted in between.
 * Within a ring, same-kind segments are merged into runs so a platform reads as
 * one smooth piece - the plates show no seams between segments.
 */
static int arc_steps(float a0, float a1) {
    int n = (int)((a1 - a0) / (PA_TAU / 72.0f)) + 2;
    return n > 40 ? 40 : n;
}

static void band(PA_Canvas *c, float cx, float cy, float ri, float ro,
                 float a0, float a1, const PA_Paint *p) {
    PA_Vec2 pts[84];
    int n = 0, st = arc_steps(a0, a1);
    for (int i = 0; i <= st; i++) {
        float a = a0 + (a1 - a0) * ((float)i / (float)st);
        pts[n].x = cx + cosf(a) * ro;
        pts[n].y = cy + sinf(a) * ro * L.squash;
        n++;
    }
    for (int i = st; i >= 0; i--) {
        float a = a0 + (a1 - a0) * ((float)i / (float)st);
        pts[n].x = cx + cosf(a) * ri;
        pts[n].y = cy + sinf(a) * ri * L.squash;
        n++;
    }
    pa_fill_poly_paint(c, pts, n, p);
}

/** Outer curtain of a near-side piece: the platform's visible thickness. */
static void skirt(PA_Canvas *c, float cx, float cy, float ro, float a0, float a1,
                  float t, PA_Color col) {
    PA_Vec2 pts[84];
    int n = 0, st = arc_steps(a0, a1);
    for (int i = 0; i <= st; i++) {
        float a = a0 + (a1 - a0) * ((float)i / (float)st);
        pts[n].x = cx + cosf(a) * ro;
        pts[n].y = cy + sinf(a) * ro * L.squash;
        n++;
    }
    for (int i = st; i >= 0; i--) {
        float a = a0 + (a1 - a0) * ((float)i / (float)st);
        pts[n].x = cx + cosf(a) * ro;
        pts[n].y = cy + sinf(a) * ro * L.squash + t;
        n++;
    }
    pa_fill_poly(c, pts, n, col);
}

/** Radial end face where a platform meets a gap. */
static void cap(PA_Canvas *c, float cx, float cy, float ri, float ro, float a,
                float t, PA_Color col) {
    float ca = cosf(a), sa = sinf(a) * L.squash;
    PA_Vec2 q[4] = {
        { cx + ca * ri, cy + sa * ri },     { cx + ca * ro, cy + sa * ro },
        { cx + ca * ro, cy + sa * ro + t }, { cx + ca * ri, cy + sa * ri + t },
    };
    pa_fill_poly(c, q, 4, col);
}

static void splat(PA_Canvas *c, float cx, float cy, float a, float rm, int seed,
                  PA_Color col) {
    float px = cx + cosf(a) * rm, py = cy + sinf(a) * rm * L.squash;
    float r = L.ball_r * 1.15f;
    pa_fill_ellipse(c, px, py, r, r * L.squash * 1.2f, col);
    PA_Rng rng;
    pa_rng_seed(&rng, (uint32_t)seed * 2654435761u + 17u);
    for (int i = 0; i < 8; i++) {
        float da = pa_rng_range(&rng, 0.0f, PA_TAU);
        float d = r * pa_rng_range(&rng, 0.95f, 1.75f);
        float s = r * pa_rng_range(&rng, 0.10f, 0.32f);
        pa_fill_ellipse(c, px + cosf(da) * d, py + sinf(da) * d * L.squash, s,
                        s * L.squash * 1.3f, col);
    }
}

typedef struct { float a0, a1; int kind; } Run;

/** Merge the ring's segments into runs of one kind, in tower-frame angles. */
static int ring_runs(const Floor *f, Run *out) {
    const float step = PA_TAU / (float)SEGMENTS;
    int start = -1;
    for (int s = 0; s < SEGMENTS; s++)
        if (f->seg[s] != f->seg[(s + SEGMENTS - 1) % SEGMENTS]) { start = s; break; }
    if (start < 0) {
        if (f->seg[0] == SEG_GAP) return 0;
        out[0].a0 = 0.0f; out[0].a1 = PA_TAU; out[0].kind = f->seg[0];
        return 1;
    }
    int n = 0;
    for (int k = 0; k < SEGMENTS;) {
        int kind = f->seg[(start + k) % SEGMENTS], len = 1;
        while (k + len < SEGMENTS && f->seg[(start + k + len) % SEGMENTS] == kind) len++;
        if (kind != SEG_GAP) {
            out[n].a0 = (float)(start + k) * step;
            out[n].a1 = (float)(start + k + len) * step;
            out[n].kind = kind;
            n++;
        }
        k += len;
    }
    return n;
}

/** One half of a floor. `finish` draws the solid goal disc with its checker. */
static void draw_half(PA_Canvas *c, const Floor *f, float cy, int near, int finish) {
    float grow = f->broken > 0.0f ? 1.0f + (1.0f - f->broken) * 0.45f : 1.0f;
    float alpha = f->broken > 0.0f ? f->broken : 1.0f;
    float ro = L.radius * grow, ri = L.inner * (f->broken > 0.0f ? grow : 1.0f);
    float t = L.thickness * (1.0f - f->hit * 0.25f);
    float rm = (L.inner + L.radius) * 0.5f;

    Run runs[SEGMENTS];
    int n = ring_runs(f, runs);
    PA_Color plate = pa_hex(H.pal.plate);

    /* Pieces: every run cut at the horizon, keeping those on this side. */
    struct { float a0, a1; int kind; } piece[SEGMENTS * 3];
    int pc = 0;
    for (int r = 0; r < n; r++) {
        float a = runs[r].a0 + H.spin, end = runs[r].a1 + H.spin;
        while (a < end - 1e-5f) {
            float next = (floorf(a / PA_PI) + 1.0f) * PA_PI;
            if (next > end) next = end;
            int is_near = sinf((a + next) * 0.5f) > 0.0f;
            if (is_near == near) {
                /* Near pieces reach a hair past the horizon where the run
                   carries on, so the two antialiased edges meeting there do not
                   leave a hairline of sky between them. */
                float lap = 0.035f;
                piece[pc].a0 = (near && a > runs[r].a0 + H.spin + 1e-4f) ? a - lap : a;
                piece[pc].a1 = (near && next < end - 1e-4f) ? next + lap : next;
                piece[pc].kind = runs[r].kind;
                pc++;
            }
            a = next;
        }
    }

    /* Sides first: the outer curtain on the near side, and the end faces that
       turn toward the camera wherever a run meets a gap. */
    for (int i = 0; near && i < pc; i++) {
        PA_Color base = piece[i].kind == SEG_DEADLY ? pa_hex(HAZARD) : plate;
        skirt(c, L.cx, cy, ro, piece[i].a0, piece[i].a1, t, pa_alpha(pa_shade(base, -0.34f), alpha));
    }
    for (int r = 0; r < n; r++) {
        if (runs[r].a1 - runs[r].a0 >= PA_TAU - 1e-4f) break;
        PA_Color side = pa_alpha(pa_shade(runs[r].kind == SEG_DEADLY ? pa_hex(HAZARD) : plate,
                                          -0.46f), alpha);
        float s = runs[r].a0 + H.spin, e = runs[r].a1 + H.spin;
        if ((sinf(s) > 0.0f) == near && cosf(s) < 0.0f) cap(c, L.cx, cy, ri, ro, s, t, side);
        if ((sinf(e) > 0.0f) == near && cosf(e) > 0.0f) cap(c, L.cx, cy, ri, ro, e, t, side);
    }

    /* Tops, lit a touch brighter at the back as the plates' are. */
    for (int i = 0; i < pc; i++) {
        PA_Color base = piece[i].kind == SEG_DEADLY ? pa_hex(HAZARD) : plate;
        if (finish) base = pa_shade(plate, 0.18f);
        PA_Paint p = pa_linear(0, cy - ro * L.squash, 0, cy + ro * L.squash);
        pa_stop(&p, 0.0f, pa_alpha(pa_shade(base, 0.14f), alpha));
        pa_stop(&p, 1.0f, pa_alpha(pa_shade(base, -0.04f), alpha));
        band(c, L.cx, cy, ri, ro, piece[i].a0, piece[i].a1, &p);
    }

    if (finish) {
        /* Checkered goal: alternate cells in white across two rows. */
        const int CELLS = 16;
        PA_Paint w = pa_flat(PA_RGBA(255, 255, 255, 235));
        for (int row = 0; row < 2; row++) {
            float r0 = row == 0 ? ri : rm, r1 = row == 0 ? rm : ro;
            for (int k = row; k < CELLS; k += 2) {
                float a0 = H.spin + (float)k / (float)CELLS * PA_TAU;
                float a1 = a0 + PA_TAU / (float)CELLS;
                if ((sinf((a0 + a1) * 0.5f) > 0.0f) != near) continue;
                band(c, L.cx, cy, r0, r1, a0, a1, &w);
            }
        }
    }

    for (int k = 0; k < f->splat_count; k++) {
        float a = f->splat_a[k] + H.spin;
        if ((sinf(a) > 0.0f) != near) continue;
        splat(c, L.cx, cy, a, rm, f->splat_seed[k],
              pa_alpha(pa_shade(pa_hex(H.pal.ball), -0.06f), alpha * 0.95f));
    }
}

static void pillar(PA_Canvas *c, float y0, float y1) {
    if (y0 < -10.0f) y0 = -10.0f;
    if (y1 > (float)c->h + 10.0f) y1 = (float)c->h + 10.0f;
    if (y1 <= y0) return;
    PA_Paint p = pa_linear(L.cx - L.inner, 0, L.cx + L.inner, 0);
    pa_stop(&p, 0.0f, pa_hex(0xC4CAD3));
    pa_stop(&p, 0.30f, pa_hex(0xFFFFFF));
    pa_stop(&p, 0.58f, pa_hex(0xF0F2F5));
    pa_stop(&p, 1.0f, pa_hex(0xB3BAC5));
    /* One pixel of overlap into the section below hides the seam between
       sections; both are the same paint, and the ring covers the join. */
    pa_fill_rect_paint(c, L.cx - L.inner, y0, L.inner * 2.0f, y1 - y0 + 1.0f, &p);
}

static void draw_ball(PA_Canvas *c) {
    float rm = (L.inner + L.radius) * 0.5f;
    float base = screen_y(H.ball_y) + rm * L.squash;
    float br = L.ball_r;
    PA_Color col = H.smashing ? PA_RGB(255, 150, 40) : pa_hex(H.pal.ball);

    /* Contact shadow on the next surface below, shrinking with height. */
    for (int i = 0; i < H.floor_count; i++) {
        Floor *f = &H.floors[i];
        if (f->smashed || f->y < H.ball_y - 0.001f) continue;
        float a = pa_wrapf(PA_TAU * 0.25f - H.spin, PA_TAU);
        if (f->seg[(int)(a / PA_TAU * (float)SEGMENTS) % SEGMENTS] == SEG_GAP) continue;
        float gap = f->y - H.ball_y;
        float k = pa_clamp01(1.0f - gap * 0.9f);
        if (k > 0.0f)
            pa_shadow(c, L.cx, screen_y(f->y) + rm * L.squash, br * (0.7f + 0.4f * k),
                      br * 0.36f, 0.30f * k);
        break;
    }

    /* Fall streak once the ball is really moving. */
    if (H.ball_v > 6.0f && !H.over) {
        float len = pa_clampf((H.ball_v - 6.0f) * 0.12f, 0.0f, 1.0f) * br * 5.0f;
        PA_Vec2 tail[3] = { { L.cx - br * 0.8f, base - br }, { L.cx + br * 0.8f, base - br },
                            { L.cx, base - br - len } };
        pa_fill_poly(c, tail, 3, pa_alpha(col, 0.35f));
    }

    float sq = H.squish;
    float rx = br * (1.0f + 0.22f * sq), ry = br * (1.0f - 0.22f * sq);
    float by = base - ry;
    if (H.smashing) {
        PA_Paint glow = pa_radial(L.cx, by, br * 0.8f, br * 2.4f);
        pa_stop(&glow, 0.0f, PA_RGBA(255, 170, 60, 150));
        pa_stop(&glow, 1.0f, PA_RGBA(255, 170, 60, 0));
        pa_fill_ellipse_paint(c, L.cx, by, br * 2.4f, br * 2.4f, &glow);
    }
    PA_Paint ball = pa_radial(L.cx - rx * 0.34f, by - ry * 0.40f, br * 0.10f, br * 1.3f);
    pa_stop(&ball, 0.0f, pa_shade(col, 0.62f));
    pa_stop(&ball, 0.45f, col);
    pa_stop(&ball, 1.0f, pa_shade(col, -0.36f));
    pa_fill_ellipse_paint(c, L.cx, by, rx, ry, &ball);
}

static void helix_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) compute_layout(c->w, c->h);

    PA_Paint bg = pa_linear(0, 0, 0, (float)c->h);
    pa_stop(&bg, 0.0f, pa_hex(H.pal.sky_top));
    pa_stop(&bg, 1.0f, pa_hex(H.pal.sky_bot));
    pa_fill_rect_paint(c, 0, 0, (float)c->w, (float)c->h, &bg);

    /* Soft cloud banks drifting behind the tower, as on the sky plates. */
    for (int i = 0; i < 5; i++) {
        float cy = pa_wrapf((float)i * 0.23f * (float)c->h - H.cam_y * L.spacing * 0.15f,
                            (float)c->h * 1.2f) - (float)c->h * 0.1f;
        float cx = (float)c->w * ((i & 1) ? 0.82f : 0.16f);
        pa_fill_ellipse(c, cx, cy, (float)c->w * 0.30f, (float)c->w * 0.09f,
                        PA_RGBA(255, 255, 255, 110));
        pa_fill_ellipse(c, cx + (float)c->w * 0.10f, cy - (float)c->w * 0.04f,
                        (float)c->w * 0.16f, (float)c->w * 0.08f, PA_RGBA(255, 255, 255, 110));
    }

    float shake_x = H.shake > 0.0f ? sinf(H.time * 60.0f) * H.shake * 7.0f : 0.0f;
    float saved_cx = L.cx;
    L.cx += shake_x;

    /* Lowest first. Index floor_count is the finish disc under the last floor. */
    Floor goal;
    memset(&goal, 0, sizeof(goal));
    goal.y = (float)H.floor_count + 1.0f;

    float reach = L.radius * L.squash + L.thickness + 4.0f;
    int ball_drawn = 0;
    pillar(c, screen_y(goal.y), (float)c->h + 10.0f);
    for (int i = H.floor_count; i >= 0; i--) {
        Floor *f = i == H.floor_count ? &goal : &H.floors[i];
        if (!ball_drawn && f->y < H.ball_y - 0.001f) { draw_ball(c); ball_drawn = 1; }

        float cy = screen_y(f->y);
        float above = i == 0 ? -1e6f : screen_y(H.floors[i - 1].y);
        int visible = !(f->smashed && f->broken <= 0.0f) &&
                      cy + reach > 0.0f && cy - reach < (float)c->h;
        if (visible) draw_half(c, f, cy, 0, i == H.floor_count);
        pillar(c, above, cy);
        if (visible) draw_half(c, f, cy, 1, i == H.floor_count);
    }
    if (!ball_drawn) draw_ball(c);

    L.cx = saved_cx;

    /* HUD in the reference's layout: level progress bar between two numbered
       discs, the score large and dark beneath it, combo "+N" stacked under. */
    char buf[64];
    int done = 0;
    for (int i = 0; i < H.floor_count; i++)
        if (H.floors[i].smashed || H.floors[i].y < H.ball_y - 0.001f) done++;
    float prog = H.floor_count > 0 ? (float)done / (float)H.floor_count : 0.0f;

    float bw = (float)c->w * 0.52f < 300.0f ? (float)c->w * 0.52f : 300.0f;
    float bx = (float)c->w * 0.5f - bw * 0.5f;
    if (bx < 124.0f) { bw -= (124.0f - bx) * 2.0f; bx = 124.0f; }
    float by = 38.0f;
    pa_round_rect(c, bx, by - 4.0f, bw, 8.0f, 4.0f, PA_RGBA(40, 44, 60, 50));
    pa_round_rect(c, bx, by - 4.0f, bw * prog, 8.0f, 4.0f, pa_hex(0xFFC21C));
    pa_fill_circle(c, bx, by, 17.0f, PA_RGB(255, 255, 255));
    pa_fill_circle(c, bx, by, 14.5f, pa_hex(0xFFC21C));
    pa_fill_circle(c, bx + bw, by, 17.0f, PA_RGB(255, 255, 255));
    pa_fill_circle(c, bx + bw, by, 14.5f, prog >= 1.0f ? pa_hex(0xFFC21C) : pa_hex(0x5A606C));
    snprintf(buf, sizeof(buf), "%d", H.level);
    pa_text(c, buf, bx, by - 6.0f, 12.0f, PA_RGB(60, 40, 0), PA_ALIGN_CENTER, 1.0f);
    snprintf(buf, sizeof(buf), "%d", H.level + 1);
    pa_text(c, buf, bx + bw, by - 6.0f, 12.0f, PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.0f);

    snprintf(buf, sizeof(buf), "%d", H.score);
    pa_text_bold(c, buf, (float)c->w * 0.5f, 74.0f, 44.0f, pa_hex(0x2E3038),
                 pa_hex(0x2E3038), PA_ALIGN_CENTER, 3.0f, 1.2f);

    for (int i = 0; i < MAX_FLOATS; i++) {
        const Floater *fl = &H.floats[i];
        if (fl->t <= 0.0f) continue;
        float a = pa_clamp01(fl->t * 1.6f);
        pa_text_bold(c, fl->text, (float)c->w * 0.5f + fl->x, 136.0f - (1.0f - fl->t) * 36.0f,
                     26.0f, PA_RGBA(255, 178, 28, (int)(a * 255.0f)),
                     PA_RGBA(255, 255, 255, (int)(a * 200.0f)), PA_ALIGN_CENTER, 2.0f, 1.6f);
    }

    if (H.passed < 3 && !H.over) {
        float tw = pa_text_width("DRAG TO SPIN THE TOWER", 12.0f, 3.0f) + 32.0f;
        pa_round_rect(c, (float)c->w * 0.5f - tw * 0.5f, (float)c->h - 60.0f, tw, 32.0f, 16.0f,
                      PA_RGBA(255, 255, 255, 215));
        pa_text(c, "DRAG TO SPIN THE TOWER", (float)c->w * 0.5f, (float)c->h - 51.0f,
                12.0f, PA_RGBA(40, 44, 60, 210), PA_ALIGN_CENTER, 3.0f);
    }

    if (H.banner_t > 0.0f) {
        int a = (int)(pa_clamp01(H.banner_t) * 255.0f);
        float pop = 1.0f + (1.0f - pa_clamp01((1.4f - H.banner_t) * 6.0f)) * 0.25f;
        pa_text_bold(c, H.banner, (float)c->w * 0.5f, (float)c->h * 0.18f, 30.0f * pop,
                     PA_RGBA(255, 150, 40, a), PA_RGBA(255, 255, 255, a), PA_ALIGN_CENTER,
                     4.0f, 2.0f);
    }

    if (H.over) {
        float a = pa_clamp01(H.over_t * 2.2f);
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(255, 255, 255, (int)(a * 120.0f)));
        int ai = (int)(a * 255.0f);
        if (H.won) {
            snprintf(buf, sizeof(buf), "LEVEL %d", H.level);
            pa_text_bold(c, buf, (float)c->w * 0.5f, (float)c->h * 0.40f, 28.0f,
                         PA_RGBA(255, 255, 255, ai), pa_alpha(pa_shade(pa_hex(H.pal.plate), -0.35f), a),
                         PA_ALIGN_CENTER, 3.0f, 2.0f);
            pa_text_bold(c, "COMPLETED!", (float)c->w * 0.5f, (float)c->h * 0.40f + 44.0f, 36.0f,
                         pa_alpha(pa_hex(H.pal.plate), a), PA_RGBA(255, 255, 255, ai),
                         PA_ALIGN_CENTER, 3.0f, 2.2f);
        } else {
            pa_text_bold(c, "SPIKED!", (float)c->w * 0.5f, (float)c->h * 0.42f, 40.0f,
                         pa_alpha(pa_hex(HAZARD), a), PA_RGBA(255, 255, 255, ai),
                         PA_ALIGN_CENTER, 4.0f, 2.2f);
        }
        snprintf(buf, sizeof(buf), "BEST %d", g_best > H.score ? g_best : H.score);
        pa_text(c, buf, (float)c->w * 0.5f, (float)c->h * 0.40f + 100.0f, 16.0f,
                PA_RGBA(40, 44, 60, (int)(a * 200.0f)), PA_ALIGN_CENTER, 3.0f);
    }
}

static void helix_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    PA_Paint bg = pa_linear(x, y, x, y + h);
    pa_stop(&bg, 0.0f, pa_hex(0xA6DAFF));
    pa_stop(&bg, 1.0f, pa_hex(0xEEF8FF));
    pa_fill_rect_paint(c, x, y, w, h, &bg);

    float cx = x + w * 0.5f;
    float rad = w * 0.40f, inner = rad * 0.34f, squash = 0.36f;
    PA_Paint p = pa_linear(cx - inner, 0, cx + inner, 0);
    pa_stop(&p, 0.0f, pa_hex(0xC4CAD3));
    pa_stop(&p, 0.30f, pa_hex(0xFFFFFF));
    pa_stop(&p, 1.0f, pa_hex(0xB3BAC5));
    pa_fill_rect_paint(c, cx - inner, y + h * 0.88f, inner * 2.0f, h * 0.12f, &p);

    for (int ring = 2; ring >= 0; ring--) {
        float cy = y + h * (0.34f + (float)ring * 0.27f);
        float spin = t * 0.9f + (float)ring * 0.9f;
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1) {
                float top = ring == 0 ? y : y + h * (0.34f + (float)(ring - 1) * 0.27f);
                pa_fill_rect_paint(c, cx - inner, top, inner * 2.0f, cy - top, &p);
            }
            for (int s = 0; s < 8; s++) {
                if (s == 2 || s == 3) continue;          /* the gap */
                float a0 = spin + (float)s / 8.0f * PA_TAU;
                float a1 = spin + (float)(s + 1) / 8.0f * PA_TAU + 0.03f;  /* hide seams */
                if ((sinf((a0 + a1) * 0.5f) > 0.0f) != pass) continue;
                PA_Vec2 pts[16];
                int n = 0;
                for (int i = 0; i <= 4; i++) {
                    float a = a0 + (a1 - a0) * ((float)i / 4.0f);
                    pts[n].x = cx + cosf(a) * rad;
                    pts[n].y = cy + sinf(a) * rad * squash;
                    n++;
                }
                for (int i = 4; i >= 0; i--) {
                    float a = a0 + (a1 - a0) * ((float)i / 4.0f);
                    pts[n].x = cx + cosf(a) * inner;
                    pts[n].y = cy + sinf(a) * inner * squash;
                    n++;
                }
                pa_fill_poly(c, pts, n, s == 5 ? pa_hex(HAZARD) : pa_hex(0x2FCF4F));
            }
        }
    }

    float by = y + h * (0.20f + fabsf(sinf(t * 2.4f)) * 0.08f);
    pa_fill_circle(c, cx, by, w * 0.06f, pa_hex(0xFF5A36));
}

const PA_Game PA_GAME_HELIX = {
    "helix", "Helix Drop", "Arcade",
    "Spin the tower, thread the gaps, and chain three floors to smash straight through.",
    PA_RGB(64, 200, 190),
    helix_start, helix_stop, helix_update, helix_render, helix_thumb
};
