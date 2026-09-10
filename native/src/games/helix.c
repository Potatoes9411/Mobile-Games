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
    unsigned char seg[SEGMENTS];
    float y;
    float broken;      /* 1 -> 0 while the floor shatters */
    float hit;         /* landing squash */
} Floor;

typedef struct {
    uint32_t top, bot;
    PA_Color ring, ring2, ball;
} Palette;

static const Palette PALETTES[] = {
    { 0x0E3346, 0x071A26, PA_RGB(64, 200, 190), PA_RGB(40, 150, 150), PA_RGB(255, 120, 90) },
    { 0x3A1430, 0x1C0918, PA_RGB(255, 122, 160), PA_RGB(200, 80, 120), PA_RGB(120, 230, 255) },
    { 0x14331F, 0x081A10, PA_RGB(104, 214, 120), PA_RGB(66, 160, 88), PA_RGB(255, 214, 92) },
    { 0x2A1A44, 0x120A22, PA_RGB(168, 132, 255), PA_RGB(120, 92, 200), PA_RGB(120, 255, 190) }
};
#define PALETTE_COUNT ((int)(sizeof(PALETTES) / sizeof(PALETTES[0])))

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
    char   banner[24];
    float  banner_t;
} Helix;

static Helix H;
static int   g_best, g_best_loaded;

static struct {
    int w, h;
    float cx, radius, inner, squash, spacing, thickness, ball_screen_y;
} L;

static void compute_layout(int w, int h) {
    L.w = w; L.h = h;
    L.cx = (float)w * 0.5f;
    L.radius = (float)w * 0.40f < (float)h * 0.22f ? (float)w * 0.40f : (float)h * 0.22f;
    L.inner = L.radius * 0.30f;
    L.squash = 0.34f;
    L.spacing = (float)h * 0.115f > 52.0f ? (float)h * 0.115f : 52.0f;
    L.thickness = L.spacing * 0.20f > 9.0f ? L.spacing * 0.20f : 9.0f;
    /* The ball sits high so most of the screen is the tower still to come. */
    L.ball_screen_y = (float)h * 0.34f;
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
    build_tower(level);
    H.ball_y = 0.0f;
    H.dragging = -1;
}

static void helix_stop(void) { }

/* --------------------------------------------------------------- physics */
/** Which segment sits under the ball right now, given the tower's rotation. */
static int segment_under_ball(void) {
    /* The ball is fixed in front of the camera, so "under the ball" is always
       the same world angle; spinning the tower moves the segments past it. */
    float a = pa_wrapf(-H.spin, PA_TAU);
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
            if (f->broken > 0.0f || f->y <= prev_y || f->y > H.ball_y) continue;

            int seg = segment_under_ball();
            int kind = f->seg[seg];

            if (kind == SEG_GAP) {
                H.combo++;
                H.passed++;
                H.score += 10;
                if (H.combo == 3) { H.smashing = 1; banner("SMASH"); pa_sfx("levelup"); }
                continue;
            }

            if (H.smashing) {
                /* A wrecking ball goes through a hazard floor as happily as a
                   safe one - that is the entire reward for the combo. */
                f->broken = 1.0f;
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
/** One wedge of a ring, as a filled band between the inner and outer radius. */
static void wedge(PA_Canvas *c, float cy, float a0, float a1, float thickness,
                  PA_Color top, PA_Color side) {
    const int STEPS = 7;
    PA_Vec2 pts[STEPS * 2 + 2];
    int n = 0;

    for (int i = 0; i <= STEPS; i++) {
        float a = a0 + (a1 - a0) * ((float)i / (float)STEPS);
        pts[n].x = L.cx + cosf(a) * L.radius;
        pts[n].y = cy + sinf(a) * L.radius * L.squash;
        n++;
    }
    for (int i = STEPS; i >= 0; i--) {
        float a = a0 + (a1 - a0) * ((float)i / (float)STEPS);
        pts[n].x = L.cx + cosf(a) * L.inner;
        pts[n].y = cy + sinf(a) * L.inner * L.squash;
        n++;
    }

    /*
     * The extruded side only exists on the near half. Drawing it on far-half
     * wedges put a skirt below their outer edge - which, on the far side, is
     * above the ring's own centre line, so every ring grew teal tabs pointing
     * upward out of its back. The cutoff is comfortably positive rather than
     * near zero so wedges at the horizon do not flicker between the two cases.
     */
    if (sinf((a0 + a1) * 0.5f) > 0.12f) {
        PA_Vec2 skirt[STEPS * 2 + 2];
        int m = 0;
        for (int i = 0; i <= STEPS; i++) {
            float a = a0 + (a1 - a0) * ((float)i / (float)STEPS);
            skirt[m].x = L.cx + cosf(a) * L.radius;
            skirt[m].y = cy + sinf(a) * L.radius * L.squash;
            m++;
        }
        for (int i = STEPS; i >= 0; i--) {
            float a = a0 + (a1 - a0) * ((float)i / (float)STEPS);
            skirt[m].x = L.cx + cosf(a) * L.radius;
            skirt[m].y = cy + sinf(a) * L.radius * L.squash + thickness;
            m++;
        }
        pa_fill_poly(c, skirt, m, side);
    }

    pa_fill_poly(c, pts, n, top);
}

static void helix_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) compute_layout(c->w, c->h);

    PA_Paint bg = pa_linear(0, 0, 0, (float)c->h);
    pa_stop(&bg, 0.0f, pa_hex(H.pal.top));
    pa_stop(&bg, 1.0f, pa_hex(H.pal.bot));
    pa_fill_rect_paint(c, 0, 0, (float)c->w, (float)c->h, &bg);

    float shake_x = H.shake > 0.0f ? sinf(H.time * 60.0f) * H.shake * 7.0f : 0.0f;
    float saved_cx = L.cx;
    L.cx += shake_x;

    /* Central spindle, drawn behind every ring. */
    float top_y = screen_y(H.cam_y - 4.0f);
    float bot_y = screen_y(H.cam_y + 8.0f);
    PA_Paint pillar = pa_linear(L.cx - L.inner, 0, L.cx + L.inner, 0);
    pa_stop(&pillar, 0.0f, pa_shade(H.pal.ring2, -0.55f));
    pa_stop(&pillar, 0.45f, pa_shade(H.pal.ring2, -0.15f));
    pa_stop(&pillar, 1.0f, pa_shade(H.pal.ring2, -0.62f));
    pa_fill_rect_paint(c, L.cx - L.inner, top_y, L.inner * 2.0f, bot_y - top_y, &pillar);

    /* Rings, far to near. Floors above the camera are behind the ball. */
    for (int i = 0; i < H.floor_count; i++) {
        Floor *f = &H.floors[i];
        float cy = screen_y(f->y);
        if (cy < -80.0f || cy > (float)c->h + 80.0f) continue;

        float alpha = f->broken > 0.0f ? f->broken : 1.0f;
        float squash_hit = 1.0f - f->hit * 0.20f;
        float thick = L.thickness * squash_hit * (f->broken > 0.0f ? f->broken : 1.0f);
        if (thick < 1.0f) continue;

        /*
         * Back to front within the ring as well as between rings. Segment order
         * is arbitrary relative to the camera once the tower has spun, so a far
         * wedge drawn after a near one overlaps it wrongly. Two passes over the
         * same list - far half, then near half - costs nothing and needs no
         * sort.
         */
        for (int pass = 0; pass < 2; pass++) {
            for (int s = 0; s < SEGMENTS; s++) {
                if (f->seg[s] == SEG_GAP) continue;
                float a0 = H.spin + (float)s / (float)SEGMENTS * PA_TAU;
                float a1 = H.spin + (float)(s + 1) / (float)SEGMENTS * PA_TAU;
                int near = sinf((a0 + a1) * 0.5f) > 0.0f;
                if (near != pass) continue;

                PA_Color top = f->seg[s] == SEG_DEADLY ? PA_RGB(255, 86, 92) : H.pal.ring;
                PA_Color side = f->seg[s] == SEG_DEADLY ? PA_RGB(168, 44, 52)
                                                         : pa_shade(H.pal.ring, -0.42f);
                /* Alternate the safe wedges very slightly so the ring reads as
                   segments rather than as one solid disc. */
                if (f->seg[s] == SEG_SAFE && (s & 1)) top = pa_shade(top, -0.10f);

                wedge(c, cy, a0, a1, thick,
                      pa_alpha(top, alpha), pa_alpha(side, alpha));
            }
        }
    }

    /* Ball. Drawn after the rings above it and before those below, but since it
       is always at the camera's focus a single pass over the rings plus this is
       enough - a ring at the ball's own depth is the one it just landed on. */
    float by = screen_y(H.ball_y);
    float br = L.radius * 0.20f;
    PA_Color ball_col = H.smashing ? PA_RGB(255, 214, 92) : H.pal.ball;

    if (H.smashing) {
        PA_Paint glow = pa_radial(L.cx, by, br * 0.8f, br * 2.4f);
        pa_stop(&glow, 0.0f, pa_alpha(ball_col, 0.55f));
        pa_stop(&glow, 1.0f, pa_alpha(ball_col, 0.0f));
        pa_fill_ellipse_paint(c, L.cx, by, br * 2.4f, br * 2.4f, &glow);
    }

    PA_Paint ball = pa_radial(L.cx - br * 0.32f, by - br * 0.38f, br * 0.12f, br * 1.25f);
    pa_stop(&ball, 0.0f, pa_shade(ball_col, 0.58f));
    pa_stop(&ball, 0.55f, ball_col);
    pa_stop(&ball, 1.0f, pa_shade(ball_col, -0.42f));
    pa_fill_ellipse_paint(c, L.cx, by, br, br, &ball);

    L.cx = saved_cx;

    pa_vignette(c, 0.5f);
    pa_hud_scrim(c, 96.0f);

    char buf[64];
    snprintf(buf, sizeof(buf), "%d", H.score);
    pa_text(c, buf, 104.0f, 34.0f, 22.0f, PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 2.0f);
    snprintf(buf, sizeof(buf), "LEVEL %d", H.level);
    pa_text(c, buf, (float)c->w * 0.5f + 40.0f, 34.0f, 13.0f,
            PA_RGBA(255, 255, 255, 180), PA_ALIGN_CENTER, 2.0f);
    snprintf(buf, sizeof(buf), "BEST %d", g_best > H.score ? g_best : H.score);
    pa_text(c, buf, (float)c->w - 20.0f, 34.0f, 13.0f,
            PA_RGBA(255, 255, 255, 180), PA_ALIGN_RIGHT, 2.0f);

    if (H.passed < 3) {
        pa_text(c, "DRAG TO SPIN THE TOWER", (float)c->w * 0.5f, (float)c->h - 46.0f,
                12.0f, PA_RGBA(255, 255, 255, 150), PA_ALIGN_CENTER, 3.0f);
    }

    if (H.banner_t > 0.0f) {
        pa_text(c, H.banner, (float)c->w * 0.5f, (float)c->h * 0.20f, 26.0f,
                PA_RGBA(255, 214, 84, (int)(pa_clamp01(H.banner_t) * 235.0f)),
                PA_ALIGN_CENTER, 5.0f);
    }

    if (H.over) {
        float a = pa_clamp01(H.over_t * 2.2f);
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(8, 6, 18, (int)(a * 180.0f)));
        pa_text(c, H.won ? "TOWER CLEARED" : "SPIKED",
                (float)c->w * 0.5f, (float)c->h * 0.44f, 30.0f,
                H.won ? PA_RGB(126, 240, 160) : PA_RGB(255, 107, 122),
                PA_ALIGN_CENTER, 5.0f);
    }
}

static void helix_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    PA_Paint bg = pa_linear(x, y, x, y + h);
    pa_stop(&bg, 0.0f, pa_hex(0x0E3346));
    pa_stop(&bg, 1.0f, pa_hex(0x071A26));
    pa_fill_rect_paint(c, x, y, w, h, &bg);

    float cx = x + w * 0.5f;
    float rad = w * 0.34f, inner = rad * 0.32f, squash = 0.34f;
    pa_fill_rect(c, cx - inner, y, inner * 2.0f, h, pa_hex(0x12414F));

    for (int ring = 0; ring < 3; ring++) {
        float cy = y + h * (0.30f + (float)ring * 0.26f);
        float spin = t * 0.9f + (float)ring * 0.7f;
        for (int s = 0; s < 8; s++) {
            if (s == 2 || s == 3) continue;          /* the gap */
            float a0 = spin + (float)s / 8.0f * PA_TAU;
            float a1 = spin + (float)(s + 1) / 8.0f * PA_TAU;
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
            pa_fill_poly(c, pts, n, s == 5 ? PA_RGB(255, 86, 92) : PA_RGB(64, 200, 190));
        }
    }

    float by = y + h * (0.16f + fabsf(sinf(t * 2.4f)) * 0.10f);
    pa_fill_circle(c, cx, by, w * 0.075f, PA_RGB(255, 120, 90));
}

const PA_Game PA_GAME_HELIX = {
    "helix", "Helix Drop", "Arcade",
    "Spin the tower, thread the gaps, and chain three floors to smash straight through.",
    PA_RGB(64, 200, 190),
    helix_start, helix_stop, helix_update, helix_render, helix_thumb
};
