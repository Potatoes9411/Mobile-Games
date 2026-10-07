/* ===========================================================================
   POCKET ARCADE - native hub
   The shell around the games: the lobby (account header, game of the day,
   missions and streak, the tile grid), the launch and return transitions,
   the reward popups and the pause sheet. Games are registered in one table;
   adding one is a line here and a file in games/. The cross-game economy
   lives in meta.c; this file only presents it.

   Every animation advances in pa_app_update on the fixed step and render
   only reads state, so a 60 Hz and a 240 Hz panel show identical frames.
   =========================================================================== */
#include "pa.h"
#include "meta.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

extern const PA_Game PA_GAME_SPLAT;
extern const PA_Game PA_GAME_ROADHOPPER;
extern const PA_Game PA_GAME_VOIDMUNCHER;
extern const PA_Game PA_GAME_CHROMERUSH;
extern const PA_Game PA_GAME_BLOCKSTORM;
extern const PA_Game PA_GAME_HELIX;
extern const PA_Game PA_GAME_MOBCLASH;
extern const PA_Game PA_GAME_PAPER;
extern const PA_Game PA_GAME_PINS;
extern const PA_Game PA_GAME_RUNNER;
extern const PA_Game PA_GAME_HORDE;
extern const PA_Game PA_GAME_AVIAN;

static const PA_Game *const GAMES[] = {
    &PA_GAME_ROADHOPPER,
    &PA_GAME_VOIDMUNCHER,
    &PA_GAME_CHROMERUSH,
    &PA_GAME_BLOCKSTORM,
    &PA_GAME_HELIX,
    &PA_GAME_SPLAT,
    &PA_GAME_MOBCLASH,
    &PA_GAME_PAPER,
    &PA_GAME_PINS,
    &PA_GAME_RUNNER,
    &PA_GAME_HORDE,
    &PA_GAME_AVIAN
};
#define GAME_COUNT ((int)(sizeof(GAMES) / sizeof(GAMES[0])))

/* ---------------------------------------------------------------- palette -- */
#define INK        pa_hex(0x15285A)   /* outline ink for all chunky UI */
#define SKY_TOP    pa_hex(0x56CCFF)
#define SKY_BOTTOM pa_hex(0x2F7BF2)
#define BAR_TOP    pa_hex(0x2C6BE0)
#define BAR_BOTTOM pa_hex(0x1D52BF)
#define BAR_LIP    pa_hex(0x153C8E)
#define GREEN      pa_hex(0x4CD137)
#define BLUE_BTN   pa_hex(0x3D8BFF)
#define GOLD       pa_hex(0xFFC21A)
#define RED_BADGE  pa_hex(0xFF3B4E)
#define CREAM      pa_hex(0xFFFFFF)

typedef enum { SCREEN_HOME, SCREEN_LAUNCH, SCREEN_GAME, SCREEN_PAUSE, SCREEN_EXIT } Screen;
typedef enum { MODAL_NONE, MODAL_MISSIONS, MODAL_STREAK, MODAL_LEVELUP } Modal;

typedef struct { float x, y, w, h; } Rect;
typedef struct { float x, v; } Spring;

static Screen         g_screen;
static const PA_Game *g_active;
static int            g_active_idx = -1;
static float          g_time;
static int            g_quit;
static int            g_view_w, g_view_h;

/* ------------------------------------------------------------ small math -- */
static float ease_out_back(float t) {
    t = pa_clamp01(t);
    float c1 = 1.70158f, c3 = c1 + 1.0f, u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
static float ease_in_out(float t) {
    t = pa_clamp01(t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
static void spring_step(Spring *s, float target, float k, float d, float dt) {
    float a = k * (target - s->x) - d * s->v;
    s->v += a * dt;
    s->x += s->v * dt;
}
static int in_rect(Rect r, float x, float y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}
static Rect rect_scale(Rect r, float s) {
    Rect o = { r.x + r.w * (1.0f - s) * 0.5f, r.y + r.h * (1.0f - s) * 0.5f, r.w * s, r.h * s };
    return o;
}

/* ------------------------------------------------------------- clip stack -- */
static int g_clip[8][4];
static int g_clip_n;

/** Intersects with the clip in force, never replaces it: a scrolled card's
    art must not escape the content viewport and paint over the header. */
static void clip_push(PA_Canvas *c, float x, float y, float w, float h) {
    if (g_clip_n < 8) {
        g_clip[g_clip_n][0] = c->clip_x0; g_clip[g_clip_n][1] = c->clip_y0;
        g_clip[g_clip_n][2] = c->clip_x1; g_clip[g_clip_n][3] = c->clip_y1;
        g_clip_n++;
    }
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
    if (x0 < c->clip_x0) x0 = c->clip_x0;
    if (y0 < c->clip_y0) y0 = c->clip_y0;
    if (x1 > c->clip_x1) x1 = c->clip_x1;
    if (y1 > c->clip_y1) y1 = c->clip_y1;
    pa_clip_rect(c, x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0);
}
static void clip_pop(PA_Canvas *c) {
    if (g_clip_n <= 0) return;
    g_clip_n--;
    c->clip_x0 = g_clip[g_clip_n][0]; c->clip_y0 = g_clip[g_clip_n][1];
    c->clip_x1 = g_clip[g_clip_n][2]; c->clip_y1 = g_clip[g_clip_n][3];
}

/* ----------------------------------------------------------------- shapes -- */
static int corner_segs(float r) { int n = (int)(r * 0.45f) + 3; return n > 18 ? 18 : n; }

/* Rounded rectangle outline, clockwise (or reversed for a hole). */
static int rrect_pts(PA_Vec2 *out, int at, Rect b, float r, int reverse) {
    float m = (b.w < b.h ? b.w : b.h) * 0.5f;
    if (r > m) r = m;
    if (r < 0.0f) r = 0.0f;
    PA_Vec2 tmp[4 * 20];
    int n = 0, segs = corner_segs(r);
    float cx[4] = { b.x + b.w - r, b.x + r, b.x + r, b.x + b.w - r };
    float cy[4] = { b.y + b.h - r, b.y + b.h - r, b.y + r, b.y + r };
    for (int k = 0; k < 4; k++) {
        float a0 = PA_PI * 0.5f * (float)k;
        for (int i = 0; i <= segs; i++) {
            float a = a0 + PA_PI * 0.5f * (float)i / (float)segs;
            tmp[n].x = cx[k] + cosf(a) * r;
            tmp[n].y = cy[k] + sinf(a) * r;
            n++;
        }
    }
    for (int i = 0; i < n; i++) out[at + i] = reverse ? tmp[n - 1 - i] : tmp[i];
    return at + n;
}

/** Outer rounded rect minus an inner one: frames, rings and windows. The two
    contours wind in opposite directions, so non-zero leaves the hole. */
static void fill_frame(PA_Canvas *c, Rect outer, float ro, Rect inner, float ri, const PA_Paint *p) {
    PA_Vec2 pts[200];
    int n = rrect_pts(pts, 0, outer, ro, 0);
    pts[n++] = pts[0];
    int start = n;
    n = rrect_pts(pts, n, inner, ri, 1);
    pts[n++] = pts[start];
    pa_fill_poly_paint(c, pts, n, p);
}

static int circle_pts(PA_Vec2 *out, int at, float cx, float cy, float r, int reverse) {
    int segs = (int)(r * 0.5f) + 16;
    if (segs > 120) segs = 120;
    for (int i = 0; i <= segs; i++) {
        float a = PA_TAU * (float)i / (float)segs * (reverse ? -1.0f : 1.0f);
        out[at].x = cx + cosf(a) * r;
        out[at].y = cy + sinf(a) * r;
        at++;
    }
    return at;
}

static void fill_ring(PA_Canvas *c, float cx, float cy, float r_out, float r_in, PA_Color col) {
    PA_Vec2 pts[260];
    int n = circle_pts(pts, 0, cx, cy, r_out, 0);
    n = circle_pts(pts, n, cx, cy, r_in > 0.0f ? r_in : 0.01f, 1);
    pa_fill_poly(c, pts, n, col);
}

/** Full-screen colour with a round hole: the launch and return iris. */
static void draw_iris(PA_Canvas *c, float cx, float cy, float r, PA_Color col) {
    float W = (float)c->w, H = (float)c->h;
    if (r <= 0.5f) { pa_fill_rect(c, 0, 0, W, H, col); return; }
    PA_Vec2 pts[140];
    int n = 0;
    pts[n++] = (PA_Vec2){ -2, -2 };
    pts[n++] = (PA_Vec2){ W + 2, -2 };
    pts[n++] = (PA_Vec2){ W + 2, H + 2 };
    pts[n++] = (PA_Vec2){ -2, H + 2 };
    pts[n++] = (PA_Vec2){ -2, -2 };
    n = circle_pts(pts, n, cx, cy, r, 1);
    PA_Paint p = pa_radial(cx, cy, r, r + (W > H ? W : H));
    pa_stop(&p, 0.0f, pa_shade(col, 0.18f));
    pa_stop(&p, 1.0f, pa_shade(col, -0.22f));
    pa_fill_poly_paint(c, pts, n, &p);
    fill_ring(c, cx, cy, r + 7.0f, r, PA_RGBA(255, 255, 255, 235));
}

static int star_pts(PA_Vec2 *out, float cx, float cy, float ro, float ri, int points, float rot) {
    int n = 0;
    for (int i = 0; i < points * 2; i++) {
        float a = rot - PA_PI * 0.5f + PA_PI * (float)i / (float)points;
        float r = (i & 1) ? ri : ro;
        out[n].x = cx + cosf(a) * r;
        out[n].y = cy + sinf(a) * r;
        n++;
    }
    return n;
}

static PA_Paint vgrad(float y0, float y1, PA_Color top, PA_Color bottom) {
    PA_Paint p = pa_linear(0, y0, 0, y1);
    pa_stop(&p, 0.0f, top);
    pa_stop(&p, 1.0f, bottom);
    return p;
}

/* Sunburst rays, for celebrations. */
static void draw_rays(PA_Canvas *c, float cx, float cy, float r, int n, float rot, PA_Color col) {
    for (int i = 0; i < n; i++) {
        float a = rot + PA_TAU * (float)i / (float)n;
        float hw = PA_PI / (float)n * 0.55f;
        PA_Vec2 tri[3] = {
            { cx, cy },
            { cx + cosf(a - hw) * r, cy + sinf(a - hw) * r },
            { cx + cosf(a + hw) * r, cy + sinf(a + hw) * r }
        };
        PA_Paint p = pa_radial(cx, cy, 0.0f, r);
        pa_stop(&p, 0.0f, col);
        pa_stop(&p, 1.0f, pa_alpha(col, 0.0f));
        pa_fill_poly_paint(c, tri, 3, &p);
    }
}

/* ------------------------------------------------------------------- text -- */
static PA_TextStyle dstyle(PA_Color fill, PA_Color bottom, float outline, PA_Color ink, PA_Align al) {
    PA_TextStyle s = pa_text_style(PA_FACE_DISPLAY, fill);
    s.fill_bottom = bottom;
    s.outline = outline;
    s.outline_col = ink;
    if (outline > 0.0f) { s.shadow_dy = outline * 0.9f + 1.0f; s.shadow_col = ink; }
    s.align = al;
    return s;
}

/** Display text, shrunk to fit `maxw` (0 = no limit), centred on its cap
    height so a shrunk name stays on the same line as its neighbours. */
static void dtext(PA_Canvas *c, const char *t, float x, float y, float size, float maxw,
                  PA_Color fill, PA_Color bottom, float outline, PA_Color ink, PA_Align al) {
    PA_TextStyle s = dstyle(fill, bottom, outline, ink, al);
    if (maxw > 0.0f) {
        float w = pa_text_measure(t, size, &s).width;
        if (w > maxw && w > 0.0f) {
            float ns = size * maxw / w;
            y += (size - ns) * 0.5f;
            s.outline *= ns / size;
            s.shadow_dy *= ns / size;
            size = ns;
        }
    }
    pa_text_ex(c, t, x, y, size, &s);
}

static void utext(PA_Canvas *c, const char *t, float x, float y, float size, PA_Color col,
                  PA_Align al, int caps) {
    PA_TextStyle s = pa_text_style(PA_FACE_UI, col);
    s.align = al;
    s.caps = caps;
    s.tracking = caps ? size * 0.06f : 0.0f;
    pa_text_ex(c, t, x, y, size, &s);
}

/* ------------------------------------------------------------------ icons -- */
static void icon_gem(PA_Canvas *c, float cx, float cy, float s) {
    PA_Vec2 o[6] = { { cx - s * 0.55f, cy - s * 0.78f }, { cx + s * 0.55f, cy - s * 0.78f },
                     { cx + s * 1.0f, cy - s * 0.22f }, { cx, cy + s * 0.95f },
                     { cx - s * 1.0f, cy - s * 0.22f }, { cx - s * 0.55f, cy - s * 0.78f } };
    PA_Vec2 ol[5];
    for (int i = 0; i < 5; i++) {
        float dx = o[i].x - cx, dy = o[i].y - (cy + s * 0.05f);
        float l = sqrtf(dx * dx + dy * dy);
        ol[i].x = o[i].x + dx / l * s * 0.2f;
        ol[i].y = o[i].y + dy / l * s * 0.2f + s * 0.08f;
    }
    pa_fill_poly(c, ol, 5, pa_hex(0x0A5B2A));
    PA_Vec2 base[5] = { o[0], o[1], o[2], o[3], o[4] };
    pa_fill_poly(c, base, 5, pa_hex(0x25B85A));
    PA_Vec2 top[4] = { o[0], o[1], { cx + s * 0.38f, cy - s * 0.22f }, { cx - s * 0.38f, cy - s * 0.22f } };
    pa_fill_poly(c, top, 4, pa_hex(0x9BFFC0));
    PA_Vec2 lf[4] = { o[0], { cx - s * 0.38f, cy - s * 0.22f }, o[4], o[0] };
    pa_fill_poly(c, lf, 3, pa_hex(0x5FEB8E));
    PA_Vec2 mid[3] = { { cx - s * 0.38f, cy - s * 0.22f }, { cx + s * 0.38f, cy - s * 0.22f }, o[3] };
    pa_fill_poly(c, mid, 3, pa_hex(0x3DD474));
    PA_Vec2 rt[3] = { { cx + s * 0.38f, cy - s * 0.22f }, o[2], o[3] };
    pa_fill_poly(c, rt, 3, pa_hex(0x169A45));
    PA_Vec2 sp[8];
    int n = star_pts(sp, cx - s * 0.32f, cy - s * 0.5f, s * 0.26f, s * 0.07f, 4, 0.0f);
    pa_fill_poly(c, sp, n, PA_RGBA(255, 255, 255, 230));
}

static void icon_coin(PA_Canvas *c, float cx, float cy, float r) {
    pa_fill_circle(c, cx, cy + r * 0.12f, r * 1.08f, pa_hex(0x7A3E00));
    pa_fill_circle(c, cx, cy, r * 1.08f, pa_hex(0x8A4A00));
    pa_fill_circle(c, cx, cy, r, pa_hex(0xE89A00));
    PA_Paint p = vgrad(cy - r, cy + r, pa_hex(0xFFE873), pa_hex(0xFFB300));
    pa_fill_ellipse_paint(c, cx, cy - r * 0.06f, r * 0.86f, r * 0.84f, &p);
    PA_Vec2 st[10];
    int n = star_pts(st, cx, cy, r * 0.48f, r * 0.21f, 5, 0.0f);
    pa_fill_poly(c, st, n, pa_hex(0xE08A00));
    n = star_pts(st, cx, cy - r * 0.05f, r * 0.44f, r * 0.19f, 5, 0.0f);
    pa_fill_poly(c, st, n, pa_hex(0xFFF2A8));
    pa_fill_ellipse(c, cx - r * 0.42f, cy - r * 0.45f, r * 0.18f, r * 0.11f, PA_RGBA(255, 255, 255, 200));
}

static void icon_star(PA_Canvas *c, float cx, float cy, float r, int filled) {
    PA_Vec2 st[10];
    int n = star_pts(st, cx, cy + r * 0.08f, r * 1.22f, r * 0.62f, 5, 0.0f);
    pa_fill_poly(c, st, n, filled ? pa_hex(0x6B3A00) : PA_RGBA(10, 20, 60, 120));
    n = star_pts(st, cx, cy, r, r * 0.47f, 5, 0.0f);
    if (filled) {
        PA_Paint p = vgrad(cy - r, cy + r, pa_hex(0xFFF07A), pa_hex(0xFFA800));
        pa_fill_poly_paint(c, st, n, &p);
        n = star_pts(st, cx - r * 0.12f, cy - r * 0.18f, r * 0.34f, r * 0.15f, 5, 0.0f);
        pa_fill_poly(c, st, n, PA_RGBA(255, 255, 255, 150));
    } else {
        pa_fill_poly(c, st, n, PA_RGBA(255, 255, 255, 70));
    }
}

static void flame_shape(PA_Vec2 *pts, int *n, float cx, float cy, float s, float wob) {
    int k = 0;
    for (int i = 0; i <= 24; i++) {
        float t = (float)i / 24.0f;          /* 0 tip, around the body, back to tip */
        float a = t * PA_TAU;
        float bx = sinf(a) * 0.62f;
        float by = 0.25f - cosf(a) * 0.62f;
        /* pinch the top into a tip */
        float up = pa_clamp01(-by + 0.1f);
        bx *= 1.0f - up * 0.55f;
        by -= up * up * 0.55f;
        pts[k].x = cx + (bx + sinf(by * 4.0f + wob) * 0.06f) * s;
        pts[k].y = cy + by * s;
        k++;
    }
    *n = k;
}

static void icon_flame(PA_Canvas *c, float cx, float cy, float s, float t) {
    PA_Vec2 p[32];
    int n;
    flame_shape(p, &n, cx, cy + s * 0.05f, s * 1.18f, t * 6.0f);
    pa_fill_poly(c, p, n, pa_hex(0x7A1A00));
    flame_shape(p, &n, cx, cy, s, t * 6.0f);
    PA_Paint g = vgrad(cy - s, cy + s * 0.9f, pa_hex(0xFFB020), pa_hex(0xFF3D0A));
    pa_fill_poly_paint(c, p, n, &g);
    flame_shape(p, &n, cx, cy + s * 0.28f, s * 0.58f, t * 7.0f + 1.0f);
    PA_Paint g2 = vgrad(cy - s * 0.3f, cy + s * 0.9f, pa_hex(0xFFF59A), pa_hex(0xFFC21A));
    pa_fill_poly_paint(c, p, n, &g2);
}

static void icon_check(PA_Canvas *c, float cx, float cy, float s, PA_Color col) {
    PA_Vec2 pts[3] = { { cx - s * 0.55f, cy + s * 0.02f }, { cx - s * 0.15f, cy + s * 0.42f },
                       { cx + s * 0.6f, cy - s * 0.45f } };
    pa_stroke_poly(c, pts, 3, 0, s * 0.42f, INK);
    pa_stroke_poly(c, pts, 3, 0, s * 0.26f, col);
}

static void icon_clipboard(PA_Canvas *c, float cx, float cy, float s) {
    Rect b = { cx - s * 0.78f, cy - s * 0.92f, s * 1.56f, s * 1.9f };
    pa_round_rect(c, b.x - 3, b.y - 3, b.w + 6, b.h + 6, s * 0.28f, INK);
    pa_round_rect(c, b.x, b.y, b.w, b.h, s * 0.24f, pa_hex(0xC9782F));
    pa_round_rect(c, b.x + s * 0.16f, b.y + s * 0.2f, b.w - s * 0.32f, b.h - s * 0.34f, s * 0.12f, CREAM);
    pa_round_rect(c, cx - s * 0.36f, b.y - s * 0.16f, s * 0.72f, s * 0.34f, s * 0.12f, pa_hex(0x8A97B3));
    for (int i = 0; i < 3; i++) {
        float ly = b.y + s * 0.62f + (float)i * s * 0.42f;
        icon_check(c, b.x + s * 0.48f, ly, s * 0.3f, i < 2 ? GREEN : pa_hex(0xD9E2F2));
        pa_round_rect(c, b.x + s * 0.78f, ly - s * 0.07f, s * 0.6f, s * 0.14f, s * 0.07f, pa_hex(0xB8C6E0));
    }
}

static void icon_chest(PA_Canvas *c, float cx, float cy, float s, int open) {
    pa_round_rect(c, cx - s * 1.05f, cy - s * 0.55f, s * 2.1f, s * 1.4f, s * 0.22f, INK);
    PA_Paint body = vgrad(cy, cy + s * 0.8f, pa_hex(0xC2672A), pa_hex(0x8C3F14));
    pa_round_rect_paint(c, cx - s * 0.95f, cy - s * 0.05f, s * 1.9f, s * 0.82f, s * 0.14f, &body);
    float ly = open ? -s * 0.95f : -s * 0.48f;
    PA_Paint lid = vgrad(cy + ly, cy + ly + s * 0.5f, pa_hex(0xE08A3A), pa_hex(0xB35A22));
    pa_round_rect_paint(c, cx - s * 0.95f, cy + ly, s * 1.9f, s * 0.5f, s * 0.2f, &lid);
    pa_fill_rect(c, cx - s * 0.95f, cy - s * 0.05f, s * 1.9f, s * 0.12f, pa_hex(0xFFC93C));
    pa_fill_rect(c, cx - s * 0.7f, cy + ly, s * 0.16f, s * 1.3f - (open ? s * 0.4f : 0), pa_hex(0xFFC93C));
    pa_fill_rect(c, cx + s * 0.54f, cy + ly, s * 0.16f, s * 1.3f - (open ? s * 0.4f : 0), pa_hex(0xFFC93C));
    pa_round_rect(c, cx - s * 0.18f, cy - s * 0.14f, s * 0.36f, s * 0.42f, s * 0.08f, pa_hex(0xFFE27A));
    if (open) {
        PA_Paint glow = pa_radial(cx, cy - s * 0.2f, 0.0f, s * 1.3f);
        pa_stop(&glow, 0.0f, PA_RGBA(255, 250, 200, 220));
        pa_stop(&glow, 1.0f, PA_RGBA(255, 250, 200, 0));
        pa_fill_ellipse_paint(c, cx, cy - s * 0.2f, s * 1.3f, s * 0.9f, &glow);
    }
}

static void icon_clock(PA_Canvas *c, float cx, float cy, float r, PA_Color col) {
    pa_fill_circle(c, cx, cy, r, col);
    pa_fill_circle(c, cx, cy, r * 0.72f, CREAM);
    pa_line(c, cx, cy, cx, cy - r * 0.5f, r * 0.2f, col);
    pa_line(c, cx, cy, cx + r * 0.38f, cy, r * 0.2f, col);
}

static void icon_x(PA_Canvas *c, float cx, float cy, float s) {
    pa_line(c, cx - s, cy - s, cx + s, cy + s, s * 0.62f, INK);
    pa_line(c, cx - s, cy + s, cx + s, cy - s, s * 0.62f, INK);
    pa_line(c, cx - s, cy - s, cx + s, cy + s, s * 0.38f, CREAM);
    pa_line(c, cx - s, cy + s, cx + s, cy - s, s * 0.38f, CREAM);
}

static void icon_speaker(PA_Canvas *c, float cx, float cy, float s, int on) {
    PA_Vec2 sp[6] = { { cx - s * 0.75f, cy - s * 0.28f }, { cx - s * 0.35f, cy - s * 0.28f },
                      { cx + s * 0.1f, cy - s * 0.7f }, { cx + s * 0.1f, cy + s * 0.7f },
                      { cx - s * 0.35f, cy + s * 0.28f }, { cx - s * 0.75f, cy + s * 0.28f } };
    pa_fill_poly(c, sp, 6, CREAM);
    if (on) {
        for (int k = 0; k < 2; k++) {
            PA_Vec2 arc[9];
            float rr = s * (0.42f + 0.32f * (float)k);
            for (int i = 0; i < 9; i++) {
                float a = -0.85f + 1.7f * (float)i / 8.0f;
                arc[i].x = cx + s * 0.12f + cosf(a) * rr;
                arc[i].y = cy + sinf(a) * rr;
            }
            pa_stroke_poly(c, arc, 9, 0, s * 0.16f, CREAM);
        }
    } else {
        pa_line(c, cx + s * 0.35f, cy - s * 0.3f, cx + s * 0.85f, cy + s * 0.3f, s * 0.18f, CREAM);
        pa_line(c, cx + s * 0.35f, cy + s * 0.3f, cx + s * 0.85f, cy - s * 0.3f, s * 0.18f, CREAM);
    }
}

static void icon_play(PA_Canvas *c, float cx, float cy, float s, PA_Color col) {
    PA_Vec2 t[3] = { { cx - s * 0.45f, cy - s * 0.6f }, { cx + s * 0.62f, cy }, { cx - s * 0.45f, cy + s * 0.6f } };
    PA_Vec2 o[3] = { { cx - s * 0.62f, cy - s * 0.85f }, { cx + s * 0.92f, cy }, { cx - s * 0.62f, cy + s * 0.85f } };
    pa_fill_poly(c, o, 3, INK);
    pa_fill_poly(c, t, 3, col);
}

static void icon_home(PA_Canvas *c, float cx, float cy, float s) {
    PA_Vec2 roof[3] = { { cx - s * 0.95f, cy - s * 0.05f }, { cx, cy - s * 0.85f }, { cx + s * 0.95f, cy - s * 0.05f } };
    pa_stroke_poly(c, roof, 3, 0, s * 0.42f, INK);
    pa_round_rect(c, cx - s * 0.68f, cy - s * 0.25f, s * 1.36f, s * 1.0f, s * 0.12f, INK);
    pa_stroke_poly(c, roof, 3, 0, s * 0.24f, CREAM);
    pa_round_rect(c, cx - s * 0.56f, cy - s * 0.18f, s * 1.12f, s * 0.86f, s * 0.08f, CREAM);
    pa_round_rect(c, cx - s * 0.18f, cy + s * 0.2f, s * 0.36f, s * 0.48f, s * 0.06f, pa_hex(0x2F6BD8));
}

/* --------------------------------------------------------- chunky widgets -- */
/** The tactile button: dark ink rim, a darker lip underneath that the face
    sinks into when pressed, a gradient face and a gloss band. */
static void chunky(PA_Canvas *c, Rect r, float rad, PA_Color face, float press) {
    float lip = 7.0f * (1.0f - press * 0.7f);
    float sink = 7.0f - lip;
    pa_round_rect(c, r.x + 1, r.y + 7.0f + 4.0f, r.w - 2, r.h, rad, PA_RGBA(8, 30, 90, 60));
    pa_round_rect(c, r.x - 3, r.y + sink - 3, r.w + 6, r.h + lip + 6, rad + 3, INK);
    pa_round_rect(c, r.x, r.y + sink, r.w, r.h + lip, rad, pa_shade(face, -0.38f));
    PA_Paint p = vgrad(r.y + sink, r.y + sink + r.h, pa_shade(face, 0.22f), pa_shade(face, -0.06f));
    pa_round_rect_paint(c, r.x, r.y + sink, r.w, r.h, rad, &p);
    pa_round_rect(c, r.x + rad * 0.45f, r.y + sink + 4.0f, r.w - rad * 0.9f, r.h * 0.34f, r.h * 0.17f,
                  PA_RGBA(255, 255, 255, 70));
}

static void button_label(PA_Canvas *c, Rect r, float press, const char *t, float size, PA_Color face) {
    float sink = 7.0f * press * 0.7f;
    dtext(c, t, r.x + r.w * 0.5f, r.y + sink + r.h * 0.5f - size * 0.5f - 1.0f, size, r.w - 24.0f,
          CREAM, 0, size * 0.11f, pa_shade(face, -0.6f), PA_ALIGN_CENTER);
}

static void red_badge(PA_Canvas *c, float cx, float cy, float r, const char *t) {
    pa_fill_circle(c, cx, cy + 2.0f, r + 3.0f, INK);
    pa_fill_circle(c, cx, cy, r + 3.0f, INK);
    PA_Paint p = vgrad(cy - r, cy + r, pa_hex(0xFF6B78), pa_hex(0xE8172E));
    pa_fill_ellipse_paint(c, cx, cy, r, r, &p);
    pa_fill_ellipse(c, cx, cy - r * 0.45f, r * 0.55f, r * 0.25f, PA_RGBA(255, 255, 255, 80));
    dtext(c, t, cx, cy - r * 0.55f, r * 1.1f, 0, CREAM, 0, 0, INK, PA_ALIGN_CENTER);
}

/** Banner ribbon with folded tails, for panel titles. */
static void ribbon(PA_Canvas *c, float cx, float cy, float w, float h, PA_Color col, const char *t) {
    float tail = h * 0.75f;
    for (int s = -1; s <= 1; s += 2) {
        float ex = cx + (float)s * (w * 0.5f - h * 0.1f);
        float ox = ex + (float)s * tail;
        PA_Vec2 tl[5] = { { ex, cy - h * 0.25f }, { ox, cy - h * 0.25f }, { ox - (float)s * h * 0.32f, cy + h * 0.2f },
                          { ox, cy + h * 0.65f }, { ex, cy + h * 0.65f } };
        PA_Vec2 to[5];
        for (int i = 0; i < 5; i++) { to[i] = tl[i]; to[i].x += (float)s * 2.5f; to[i].y += (i == 0 || i == 1) ? -2.5f : 2.5f; }
        pa_fill_poly(c, to, 5, INK);
        pa_fill_poly(c, tl, 5, pa_shade(col, -0.32f));
    }
    pa_round_rect(c, cx - w * 0.5f - 3, cy - h * 0.5f - 3, w + 6, h + 9, h * 0.22f, INK);
    PA_Paint p = vgrad(cy - h * 0.5f, cy + h * 0.5f, pa_shade(col, 0.2f), pa_shade(col, -0.1f));
    pa_round_rect_paint(c, cx - w * 0.5f, cy - h * 0.5f, w, h, h * 0.18f, &p);
    pa_round_rect(c, cx - w * 0.5f + 8, cy - h * 0.5f + 4, w - 16, h * 0.28f, h * 0.14f, PA_RGBA(255, 255, 255, 60));
    float ts = h * 0.5f;
    dtext(c, t, cx, cy - ts * 0.5f - 1.0f, ts, w - 30.0f, CREAM, 0, ts * 0.12f, INK, PA_ALIGN_CENTER);
}

static void close_button(PA_Canvas *c, float cx, float cy, float r, float press) {
    float sink = press * 4.0f;
    pa_fill_circle(c, cx, cy + 5.0f, r + 3.0f, INK);
    pa_fill_circle(c, cx, cy + sink, r + 3.0f, INK);
    pa_fill_circle(c, cx, cy + 4.0f, r, pa_hex(0xB3122A));
    PA_Paint p = vgrad(cy - r + sink, cy + r + sink, pa_hex(0xFF7A80), pa_hex(0xE82A3A));
    pa_fill_ellipse_paint(c, cx, cy + sink, r, r, &p);
    icon_x(c, cx, cy + sink, r * 0.38f);
}

/** The light panel every popup sits on. */
static void panel(PA_Canvas *c, Rect r) {
    pa_round_rect(c, r.x + 4, r.y + 14, r.w - 8, r.h, 30.0f, PA_RGBA(5, 20, 70, 90));
    pa_round_rect(c, r.x - 4, r.y - 4, r.w + 8, r.h + 16, 32.0f, INK);
    pa_round_rect(c, r.x, r.y, r.w, r.h + 9, 28.0f, pa_hex(0x8FA9D6));
    PA_Paint p = vgrad(r.y, r.y + r.h, pa_hex(0xF4F9FF), pa_hex(0xD9E8FB));
    pa_round_rect_paint(c, r.x, r.y, r.w, r.h, 28.0f, &p);
}

/* ===================================================================== META */
/* Display state: what the header shows lags the bank while a reward is in
   flight, so the counter ticks up exactly as the gems land. */
static float g_disp_gems, g_disp_coins, g_disp_xp;   /* xp: lifetime total */
static int   g_hold_gems, g_hold_coins, g_hold_xp;
static int   g_shown_level;
static int   g_levelup_queue;         /* level-ups still to celebrate */
static int   g_modal_level;           /* level the open popup celebrates */
static float g_xpfly_t = -1.0f;       /* "+N XP" flying to the bar */
static int   g_xpfly_amount;
static float g_since_home;            /* seconds since the lobby appeared */
static Spring g_pill_kick[3];         /* gem, coin, xp bar */
static Spring g_badge_kick;

typedef struct { int kind; float x0, y0, cx, cy, t, delay, dur; int value; float spin; } Flyer;
#define MAX_FLY 48
static Flyer g_fly[MAX_FLY];
static int   g_fly_n;

typedef struct { float x, y, vx, vy, rot, vr, life, w, h; PA_Color col; } Confetti;
#define MAX_CONF 140
static Confetti g_conf[MAX_CONF];
static int      g_conf_n;
static PA_Rng   g_fx;
static float    g_sfx_gap;

/* ================================================================== LAYOUT */
typedef struct {
    float h;
    float badge_x, badge_y, badge_r;
    Rect  xp, gem, coin;
} HeaderL;

static int landscape(void) { return g_view_w > g_view_h; }

static HeaderL header_layout(void) {
    HeaderL L;
    float W = (float)g_view_w;
    int land = landscape();
    L.h = land ? 78.0f : 100.0f;
    L.badge_r = land ? 27.0f : 31.0f;
    L.badge_x = 16.0f + L.badge_r;
    L.badge_y = L.h * 0.5f - 2.0f;
    float pill_h = land ? 38.0f : 42.0f;
    float coin_w = land ? 168.0f : 128.0f, gem_w = land ? 148.0f : 116.0f;
    L.coin = (Rect){ W - 16.0f - coin_w, L.badge_y - pill_h * 0.5f, coin_w, pill_h };
    L.gem = (Rect){ L.coin.x - 30.0f - gem_w, L.coin.y, gem_w, pill_h };
    float xp_x = L.badge_x + L.badge_r * 0.4f;
    float xp_w = L.gem.x - 34.0f - xp_x;
    if (land && xp_w > 260.0f) xp_w = 260.0f;
    L.xp = (Rect){ xp_x, L.badge_y - 3.0f, xp_w, 24.0f };
    return L;
}

typedef struct {
    Rect  featured, missions, streak;
    float label_y;
    Rect  tile[GAME_COUNT];
    float content_h;
    int   cols;
} Lay;

static Lay   g_lay;
static float g_scroll, g_scroll_max;

static void layout(void) {
    float W = (float)g_view_w;
    float pad = W >= 760.0f ? 24.0f : 16.0f;
    float inner = W - pad * 2.0f;
    float y = 18.0f;
    if (landscape()) {
        float fw = inner * 0.6f, fh = 236.0f;
        g_lay.featured = (Rect){ pad, y, fw, fh };
        float rx = pad + fw + 18.0f, rw = inner - fw - 18.0f, rh = (fh - 18.0f) * 0.5f;
        g_lay.missions = (Rect){ rx, y, rw, rh };
        g_lay.streak = (Rect){ rx, y + rh + 18.0f, rw, rh };
        y += fh + 30.0f;
    } else {
        float fh = inner * 0.6f;
        g_lay.featured = (Rect){ pad, y, inner, fh };
        y += fh + 26.0f;
        float mw = inner * 0.63f;
        g_lay.missions = (Rect){ pad, y, mw, 108.0f };
        g_lay.streak = (Rect){ pad + mw + 14.0f, y, inner - mw - 14.0f, 108.0f };
        y += 108.0f + 30.0f;
    }
    g_lay.label_y = y;
    y += 46.0f;
    int cols = inner >= 840.0f ? 4 : inner >= 620.0f ? 3 : 2;
    float gap = 16.0f;
    float tw = (inner - gap * (float)(cols - 1)) / (float)cols;
    float th = 8.0f + (tw - 16.0f) * 0.70f + 94.0f;
    for (int i = 0; i < GAME_COUNT; i++) {
        int col = i % cols, row = i / cols;
        g_lay.tile[i] = (Rect){ pad + (float)col * (tw + gap), y + (float)row * (th + gap + 8.0f), tw, th };
    }
    int rows = (GAME_COUNT + cols - 1) / cols;
    g_lay.content_h = y + (float)rows * (th + gap + 8.0f) + 20.0f;
    g_lay.cols = cols;
    HeaderL hl = header_layout();
    g_scroll_max = g_lay.content_h - ((float)g_view_h - hl.h);
    if (g_scroll_max < 0.0f) g_scroll_max = 0.0f;
}

/** Content space to screen. */
static Rect scr(Rect r) {
    HeaderL hl = header_layout();
    r.y += hl.h - g_scroll;
    return r;
}

/* ================================================================= HOME UI */
enum { BTN_FEATURED = 0, BTN_MISSIONS = 1, BTN_STREAK = 2, BTN_TILE0 = 3, BTN_COUNT = 3 + GAME_COUNT };
static Spring g_press[BTN_COUNT];
static int    g_hit = -1;            /* button under the finger */
static float  g_drag_from_y, g_scroll_from, g_drag_travel;
static int    g_dragging;
static float  g_samples[16][2];      /* (time, finger y) for fling velocity */
static int    g_sample_n;
static float  g_scroll_vel;

/* Launch and return transitions. */
static int   g_launch_idx = -1;
static float g_launch_t;
static Rect  g_launch_from;
static float g_reveal_t = 9.0f;      /* iris opening over the new screen */
static PA_Color g_reveal_col;
static int   g_reveal_name;          /* show the game's name during the reveal */
static float g_exit_t;
#define LAUNCH_DUR 0.62f
#define REVEAL_DUR 0.46f
#define EXIT_DUR   0.34f

static Modal  g_modal;
static float  g_modal_t, g_close_t;
static int    g_closing;
static Spring g_modal_k;
static Spring g_mpress[6];
static int    g_mhit = -1;

static Rect button_rect(int id) {
    if (id == BTN_FEATURED) return scr(g_lay.featured);
    if (id == BTN_MISSIONS) return scr(g_lay.missions);
    if (id == BTN_STREAK) return scr(g_lay.streak);
    return scr(g_lay.tile[id - BTN_TILE0]);
}

static int hit_home(float x, float y) {
    HeaderL hl = header_layout();
    if (y < hl.h) return -1;
    for (int i = 0; i < BTN_COUNT; i++) if (in_rect(button_rect(i), x, y)) return i;
    return -1;
}

/* ------------------------------------------------------------- background -- */
static void draw_background(PA_Canvas *c) {
    float W = (float)c->w, H = (float)c->h;
    PA_Paint bg = vgrad(0, H, SKY_TOP, SKY_BOTTOM);
    pa_fill_rect_paint(c, 0, 0, W, H, &bg);

    /* Two slow light pools so a still lobby is never a still image. */
    for (int i = 0; i < 2; i++) {
        float t = g_time * 0.11f + (float)i * 2.4f;
        float gx = W * (0.25f + 0.5f * (0.5f + 0.5f * sinf(t)));
        float gy = H * (0.22f + 0.45f * (float)i + 0.08f * cosf(t * 1.3f));
        float gr = W * 0.62f;
        PA_Paint glow = pa_radial(gx, gy, 0.0f, gr);
        pa_stop(&glow, 0.0f, PA_RGBA(255, 255, 255, 46));
        pa_stop(&glow, 1.0f, PA_RGBA(255, 255, 255, 0));
        pa_fill_ellipse_paint(c, gx, gy, gr, gr, &glow);
    }

    /* Drifting sparkle lattice with a little parallax against the scroll. */
    float cell = 84.0f;
    float ox = pa_wrapf(g_time * 9.0f, cell * 2.0f);
    float oy = pa_wrapf(g_time * 6.0f - g_scroll * 0.3f, cell);
    for (int row = -1; row < (int)(H / cell) + 2; row++) {
        for (int col = -2; col < (int)(W / cell) + 2; col++) {
            float x = (float)col * cell + ox + ((row & 1) ? cell * 0.5f : 0.0f);
            float y = (float)row * cell + oy;
            int big = ((row * 7 + col * 3) & 3) == 0;
            float s = big ? 9.0f : 5.0f;
            float tw = 0.6f + 0.4f * sinf(g_time * 1.6f + (float)(row * 13 + col * 7));
            PA_Vec2 sp[8];
            int n = star_pts(sp, x, y, s * (0.8f + 0.2f * tw), s * 0.3f, 4, 0.0f);
            pa_fill_poly(c, sp, n, PA_RGBA(255, 255, 255, (int)((big ? 60.0f : 38.0f) * tw)));
        }
    }
}

/* ----------------------------------------------------------------- header -- */
static void pill(PA_Canvas *c, Rect r) {
    pa_round_rect(c, r.x - 3, r.y - 3, r.w + 6, r.h + 6, (r.h + 6) * 0.5f, INK);
    PA_Paint p = vgrad(r.y, r.y + r.h, pa_hex(0x0F2C6E), pa_hex(0x173C8C));
    pa_round_rect_paint(c, r.x, r.y, r.w, r.h, r.h * 0.5f, &p);
    pa_round_rect(c, r.x + r.h * 0.4f, r.y + r.h - 7.0f, r.w - r.h * 0.8f, 3.0f, 1.5f, PA_RGBA(255, 255, 255, 30));
}

static void level_badge(PA_Canvas *c, float cx, float cy, float r, int level, float spin) {
    PA_Vec2 st[24];
    int n = star_pts(st, cx, cy + 3.0f, r * 1.12f, r * 0.92f, 12, spin);
    pa_fill_poly(c, st, n, INK);
    n = star_pts(st, cx, cy, r * 1.12f, r * 0.92f, 12, spin);
    pa_fill_poly(c, st, n, INK);
    n = star_pts(st, cx, cy, r, r * 0.82f, 12, spin);
    PA_Paint p = vgrad(cy - r, cy + r, pa_hex(0xFFE45C), pa_hex(0xFF9F0A));
    pa_fill_poly_paint(c, st, n, &p);
    PA_Paint q = vgrad(cy - r, cy + r, pa_hex(0x4FA3FF), pa_hex(0x1E5BD6));
    pa_fill_ellipse(c, cx, cy, r * 0.74f, r * 0.74f, pa_hex(0xC97400));
    pa_fill_ellipse_paint(c, cx, cy, r * 0.68f, r * 0.68f, &q);
    pa_fill_ellipse(c, cx, cy - r * 0.36f, r * 0.42f, r * 0.2f, PA_RGBA(255, 255, 255, 60));
    char t[12];
    snprintf(t, sizeof(t), "%d", level);
    float ts = r * (level >= 100 ? 0.62f : 0.78f);
    dtext(c, t, cx, cy - ts * 0.5f - 1.0f, ts, r * 1.25f, CREAM, 0, ts * 0.12f, INK, PA_ALIGN_CENTER);
}

static void draw_header(PA_Canvas *c) {
    HeaderL L = header_layout();
    float W = (float)c->w;
    PA_Paint bar = vgrad(0, L.h, BAR_TOP, BAR_BOTTOM);
    pa_fill_rect(c, 0, L.h, W, 10.0f, PA_RGBA(10, 40, 120, 50));
    pa_fill_rect(c, 0, L.h - 2.0f, W, 7.0f, BAR_LIP);
    pa_fill_rect_paint(c, 0, 0, W, L.h - 2.0f, &bar);
    pa_fill_rect(c, 0, L.h - 6.0f, W, 2.0f, PA_RGBA(255, 255, 255, 40));
    /* Faint chevrons in the bar so it reads as a designed strip, not a fill. */
    for (float x = -40.0f + pa_wrapf(g_time * 14.0f, 56.0f); x < W + 40.0f; x += 56.0f) {
        PA_Vec2 chev[4] = { { x, 0 }, { x + 22.0f, 0 }, { x + 22.0f - L.h * 0.5f, L.h - 4.0f },
                            { x - L.h * 0.5f, L.h - 4.0f } };
        pa_fill_poly(c, chev, 4, PA_RGBA(255, 255, 255, 10));
    }

    /* Wordmark in the gap between the level bar and the wallet, when the
       window is wide enough to have one. */
    {
        float gap0 = L.xp.x + L.xp.w + 24.0f, gap1 = L.gem.x - 40.0f;
        if (gap1 - gap0 > 190.0f) {
            float cx = (gap0 + gap1) * 0.5f;
            float tilt = sinf(g_time * 1.3f) * 2.0f;
            dtext(c, "POCKET ARCADE", cx, L.badge_y - 15.0f + tilt, 30.0f, gap1 - gap0,
                  pa_hex(0xFFF27A), pa_hex(0xFFA312), 4.0f, INK, PA_ALIGN_CENTER);
        }
    }

    /* Level and XP. */
    int lvl, into, need;
    meta_level_from_total((int)(g_disp_xp + 0.5f), &lvl, &into, &need);
    float kick = g_pill_kick[2].x;
    Rect xb = L.xp;
    xb.y -= kick * 3.0f;
    pa_round_rect(c, xb.x - 3, xb.y - 3, xb.w + 6, xb.h + 6, (xb.h + 6) * 0.5f, INK);
    pa_round_rect(c, xb.x, xb.y, xb.w, xb.h, xb.h * 0.5f, pa_hex(0x0E2A66));
    float frac = need > 0 ? pa_clamp01(((float)into + (g_disp_xp - floorf(g_disp_xp))) / (float)need) : 0.0f;
    float fx = xb.x + L.badge_r * 0.6f;
    float fw = (xb.x + xb.w - 3.0f - fx) * frac;
    if (fw > 1.0f) {
        PA_Paint fp = vgrad(xb.y, xb.y + xb.h, pa_hex(0xB6FF5C), pa_hex(0x36B526));
        pa_round_rect_paint(c, fx, xb.y + 3.0f, fw + xb.h * 0.3f > 0 ? fw : 0, xb.h - 6.0f, (xb.h - 6.0f) * 0.5f, &fp);
        pa_round_rect(c, fx + 4.0f, xb.y + 5.0f, fw - 8.0f > 0 ? fw - 8.0f : 0, 4.0f, 2.0f, PA_RGBA(255, 255, 255, 110));
    }
    char t[48], a[16], b[16];
    pa_fmt_int(into, a, sizeof(a));
    pa_fmt_int(need, b, sizeof(b));
    snprintf(t, sizeof(t), "%s / %s", a, b);
    dtext(c, t, fx + (xb.x + xb.w - fx) * 0.5f, xb.y + xb.h * 0.5f - 6.5f, 13.0f, xb.w - 40.0f,
          CREAM, 0, 2.2f, INK, PA_ALIGN_CENTER);
    dtext(c, "LEVEL", fx + 4.0f, xb.y - 21.0f, 13.0f, 0, pa_hex(0xFFE45C), 0, 2.2f, INK, PA_ALIGN_LEFT);
    level_badge(c, L.badge_x, L.badge_y, L.badge_r * (1.0f + kick * 0.12f), lvl, 0.0f);

    /* Currency pills: icon overlapping the left end, rolling counter. */
    for (int k = 0; k < 2; k++) {
        Rect r = k == 0 ? L.gem : L.coin;
        float s = 1.0f + g_pill_kick[k].x * 0.10f;
        Rect rr = rect_scale(r, s);
        pill(c, rr);
        char num[24];
        pa_fmt_int((int)((k == 0 ? g_disp_gems : g_disp_coins) + 0.5f), num, sizeof(num));
        float ts = 19.0f * s;
        dtext(c, num, rr.x + rr.w - 14.0f, rr.y + rr.h * 0.5f - ts * 0.5f, ts, rr.w - 42.0f,
              CREAM, 0, 2.6f, INK, PA_ALIGN_RIGHT);
        float ix = rr.x + 2.0f, iy = rr.y + rr.h * 0.5f;
        if (k == 0) icon_gem(c, ix, iy - 1.0f, 17.0f * s);
        else icon_coin(c, ix, iy, 18.0f * s);
    }
}

/* ------------------------------------------------------------------ tiles -- */
static void game_badge(PA_Canvas *c, float x, float y, const char *t, PA_Color col, float s) {
    PA_TextStyle st = dstyle(CREAM, 0, 2.0f * s, INK, PA_ALIGN_CENTER);
    float ts = 14.0f * s;
    float w = pa_text_measure(t, ts, &st).width + 22.0f * s, h = 26.0f * s;
    pa_round_rect(c, x - 2.5f, y - 2.5f, w + 5, h + 7, h * 0.4f, INK);
    pa_round_rect(c, x, y + 3.0f, w, h, h * 0.36f, pa_shade(col, -0.35f));
    PA_Paint p = vgrad(y, y + h, pa_shade(col, 0.2f), col);
    pa_round_rect_paint(c, x, y, w, h, h * 0.36f, &p);
    dtext(c, t, x + w * 0.5f, y + h * 0.5f - ts * 0.5f, ts, 0, CREAM, 0, 2.0f * s, INK, PA_ALIGN_CENTER);
}

static void sticker(PA_Canvas *c, float cx, float cy, float r, float rot, const char *top, const char *bottom) {
    PA_Vec2 st[32];
    int n = star_pts(st, cx, cy + 3.0f, r * 1.08f, r * 0.9f, 14, rot);
    pa_fill_poly(c, st, n, INK);
    n = star_pts(st, cx, cy, r * 1.08f, r * 0.9f, 14, rot);
    pa_fill_poly(c, st, n, INK);
    n = star_pts(st, cx, cy, r, r * 0.84f, 14, rot);
    PA_Paint p = vgrad(cy - r, cy + r, pa_hex(0xFF5A6E), pa_hex(0xE0102C));
    pa_fill_poly_paint(c, st, n, &p);
    dtext(c, top, cx, cy - r * 0.5f, r * 0.62f, r * 1.5f, pa_hex(0xFFF27A), pa_hex(0xFFC21A), r * 0.08f, INK, PA_ALIGN_CENTER);
    dtext(c, bottom, cx, cy + r * 0.2f, r * 0.34f, r * 1.4f, CREAM, 0, r * 0.06f, INK, PA_ALIGN_CENTER);
}

static void draw_thumb(PA_Canvas *c, const PA_Game *g, Rect win, float t) {
    clip_push(c, win.x, win.y, win.w, win.h);
    pa_fill_rect(c, win.x, win.y, win.w, win.h, g->accent);
    if (g->thumb) g->thumb(c, win.x, win.y, win.w, win.h, t);
    clip_pop(c);
}

/** One game tile at `r` (already on screen). `s` scales it about its centre
    for the pop-in, `press` sinks it into its lip. */
static void draw_tile(PA_Canvas *c, int gi, Rect r0, float s, float press) {
    if (s <= 0.02f) return;
    const PA_Game *g = GAMES[gi];
    const MetaState *M = meta_state();
    float sq = 1.0f - 0.05f * press;
    Rect r = rect_scale(r0, s * sq);
    r.y += press * 5.0f;
    float k = r.w / r0.w;                 /* uniform scale for inner sizes */
    float R = 24.0f * k, b = 8.0f * k;
    float lip = 9.0f * k * (1.0f - press * 0.6f);
    PA_Color acc = g->accent;

    pa_round_rect(c, r.x + 3, r.y + lip + 8.0f * k, r.w - 6, r.h, R, PA_RGBA(10, 40, 120, 60));
    pa_round_rect(c, r.x - 3, r.y - 3, r.w + 6, r.h + lip + 6, R + 3, INK);
    pa_round_rect(c, r.x, r.y + lip, r.w, r.h, R, pa_shade(acc, -0.45f));

    float th = (r.w - 2.0f * b) * 0.70f;
    Rect win = { r.x + b, r.y + b, r.w - 2.0f * b, th };
    draw_thumb(c, g, win, g_time + (float)gi * 1.7f);
    /* Gloss over the art so every thumb, whoever drew it, reads as glass. */
    PA_Paint sheen = vgrad(win.y, win.y + win.h, PA_RGBA(255, 255, 255, 34), PA_RGBA(0, 20, 60, 50));
    pa_fill_rect_paint(c, win.x, win.y, win.w, win.h, &sheen);

    if (press > 0.02f) pa_fill_rect(c, win.x, win.y, win.w, win.h, PA_RGBA(10, 25, 70, (int)(70.0f * pa_clamp01(press))));
    PA_Paint body = vgrad(r.y, r.y + r.h, pa_shade(acc, 0.28f), pa_shade(acc, -0.12f));
    fill_frame(c, r, R, win, 15.0f * k, &body);
    Rect wo = { win.x - 2.5f * k, win.y - 2.5f * k, win.w + 5.0f * k, win.h + 5.0f * k };
    PA_Paint rim = pa_flat(pa_alpha(pa_shade(acc, -0.55f), 0.75f));
    fill_frame(c, wo, 17.0f * k, win, 15.0f * k, &rim);
    pa_round_rect(c, r.x + R, r.y + 2.0f * k, r.w - 2.0f * R, 3.0f * k, 1.5f * k, PA_RGBA(255, 255, 255, 110));

    PA_Color ink = pa_shade(acc, -0.62f);
    float iy = win.y + win.h + 12.0f * k;
    dtext(c, g->name, r.x + 14.0f * k, iy, 21.0f * k, r.w - 28.0f * k, CREAM, 0, 2.8f * k, ink, PA_ALIGN_LEFT);
    PA_TextStyle gs = pa_text_style(PA_FACE_UI, PA_RGBA(255, 255, 255, 225));
    gs.caps = 1; gs.tracking = 1.0f * k;
    gs.shadow_dy = 1.5f * k; gs.shadow_col = pa_alpha(ink, 0.6f);
    pa_text_ex(c, g->genre, r.x + 15.0f * k, iy + 31.0f * k, 11.0f * k, &gs);

    float by = r.y + r.h - 22.0f * k;
    int stars = meta_game_stars(gi);
    for (int i = 0; i < 3; i++)
        icon_star(c, r.x + 25.0f * k + (float)i * 25.0f * k, by, 10.0f * k, i < stars);

    char prog[32];
    meta_game_progress(gi, prog, sizeof(prog));
    if (prog[0]) {
        PA_TextStyle ps = dstyle(CREAM, 0, 0, INK, PA_ALIGN_RIGHT);
        float ts = 13.0f * k;
        float pw = pa_text_measure(prog, ts, &ps).width + 18.0f * k, ph = 24.0f * k;
        float px = r.x + r.w - 10.0f * k - pw, py = by - ph * 0.5f;
        pa_round_rect(c, px, py, pw, ph, ph * 0.5f, PA_RGBA(10, 25, 70, 120));
        dtext(c, prog, px + pw * 0.5f, py + ph * 0.5f - ts * 0.5f, ts, 0, CREAM, 0, 0, INK, PA_ALIGN_CENTER);
    }

    if (gi == M->featured) game_badge(c, win.x + 8.0f * k, win.y + 8.0f * k, "2X XP", pa_hex(0xFF3B4E), k);
    else if (M->game[gi].runs == 0 && !prog[0]) game_badge(c, win.x + 8.0f * k, win.y + 8.0f * k, "NEW", GREEN, k);
}

/* ------------------------------------------------------- lobby top cards -- */
static void card_base(PA_Canvas *c, Rect r, float R, float lip, PA_Color lipcol, const PA_Paint *face) {
    pa_round_rect(c, r.x + 3, r.y + lip + 7.0f, r.w - 6, r.h, R, PA_RGBA(10, 40, 120, 60));
    pa_round_rect(c, r.x - 3, r.y - 3, r.w + 6, r.h + lip + 6, R + 3, INK);
    pa_round_rect(c, r.x, r.y + lip, r.w, r.h, R, lipcol);
    pa_round_rect_paint(c, r.x, r.y, r.w, r.h, R, face);
    pa_round_rect(c, r.x + R, r.y + 3.0f, r.w - 2.0f * R, 3.0f, 1.5f, PA_RGBA(255, 255, 255, 120));
}

static Rect pressed_rect(Rect r0, float s, float press, float *k) {
    Rect r = rect_scale(r0, s * (1.0f - 0.035f * press));
    r.y += press * 5.0f;
    *k = r.w / r0.w;
    return r;
}

static void draw_featured(PA_Canvas *c, Rect r0, float s, float press) {
    if (s <= 0.02f) return;
    const MetaState *M = meta_state();
    const PA_Game *g = GAMES[M->featured];
    float k;
    Rect r = pressed_rect(r0, s, press, &k);
    float R = 28.0f * k, b = 8.0f * k, lip = 10.0f * k * (1.0f - press * 0.6f);
    pa_round_rect(c, r.x + 3, r.y + lip + 8.0f, r.w - 6, r.h, R, PA_RGBA(10, 40, 120, 60));
    pa_round_rect(c, r.x - 3, r.y - 3, r.w + 6, r.h + lip + 6, R + 3, INK);
    pa_round_rect(c, r.x, r.y + lip, r.w, r.h, R, pa_hex(0xC26A00));

    Rect win = { r.x + b, r.y + b, r.w - 2.0f * b, r.h - 2.0f * b };
    draw_thumb(c, g, win, g_time * 0.9f + 2.0f);
    PA_Paint fade = vgrad(win.y + win.h * 0.38f, win.y + win.h, PA_RGBA(8, 20, 60, 0), PA_RGBA(8, 20, 60, 200));
    pa_fill_rect_paint(c, win.x, win.y + win.h * 0.38f, win.w, win.h * 0.62f, &fade);
    PA_Paint top = vgrad(win.y, win.y + win.h * 0.3f, PA_RGBA(255, 255, 255, 40), PA_RGBA(255, 255, 255, 0));
    pa_fill_rect_paint(c, win.x, win.y, win.w, win.h * 0.3f, &top);

    PA_Paint gold = vgrad(r.y, r.y + r.h, pa_hex(0xFFEA70), pa_hex(0xFFA312));
    fill_frame(c, r, R, win, 20.0f * k, &gold);
    Rect wo = { win.x - 2.5f * k, win.y - 2.5f * k, win.w + 5.0f * k, win.h + 5.0f * k };
    PA_Paint rim = pa_flat(PA_RGBA(150, 70, 0, 200));
    fill_frame(c, wo, 22.0f * k, win, 20.0f * k, &rim);
    pa_round_rect(c, r.x + R, r.y + 2.0f * k, r.w - 2.0f * R, 3.0f * k, 1.5f * k, PA_RGBA(255, 255, 255, 150));

    game_badge(c, win.x + 12.0f * k, win.y + 12.0f * k, "GAME OF THE DAY", pa_hex(0x8B4DFF), k * 1.05f);
    float sr = 40.0f * k * (1.0f + 0.05f * sinf(g_time * 4.0f));
    sticker(c, win.x + win.w - 50.0f * k, win.y + 50.0f * k, sr, sinf(g_time * 0.8f) * 0.12f, "2X", "XP");

    float bw = 148.0f * k, bh = 58.0f * k;
    float pulse = 1.0f + 0.035f * (0.5f + 0.5f * sinf(g_time * 5.0f));
    Rect pb = rect_scale((Rect){ win.x + win.w - bw - 16.0f * k, win.y + win.h - bh - 20.0f * k, bw, bh }, pulse);
    chunky(c, pb, 18.0f * k, GREEN, press);
    button_label(c, pb, press, "PLAY", 28.0f * k, GREEN);

    float tx = win.x + 18.0f * k, maxw = pb.x - tx - 14.0f * k;
    float ny = win.y + win.h - 92.0f * k;
    dtext(c, g->name, tx, ny, 38.0f * k, maxw, CREAM, pa_hex(0xFFF1B0), 4.0f * k, INK, PA_ALIGN_LEFT);
    PA_TextStyle gs = pa_text_style(PA_FACE_UI, pa_hex(0xFFE45C));
    gs.caps = 1; gs.tracking = 1.4f * k; gs.shadow_dy = 2.0f; gs.shadow_col = PA_RGBA(0, 0, 0, 120);
    pa_text_ex(c, g->genre, tx + 2.0f, ny + 50.0f * k, 13.0f * k, &gs);
    int stars = meta_game_stars(M->featured);
    for (int i = 0; i < 3; i++) icon_star(c, tx + 12.0f * k + (float)i * 26.0f * k, ny + 82.0f * k, 10.5f * k, i < stars);
}

static void draw_missions_card(PA_Canvas *c, Rect r0, float s, float press) {
    if (s <= 0.02f) return;
    const MetaState *M = meta_state();
    float k;
    Rect r = pressed_rect(r0, s, press, &k);
    PA_Paint face = vgrad(r.y, r.y + r.h, pa_hex(0xFFFFFF), pa_hex(0xD6E7FF));
    card_base(c, r, 22.0f * k, 9.0f * k * (1.0f - press * 0.6f), pa_hex(0x86A2D4), &face);
    icon_clipboard(c, r.x + 40.0f * k, r.y + r.h * 0.5f + 2.0f, 24.0f * k);

    float tx = r.x + 76.0f * k;
    dtext(c, "DAILY MISSIONS", tx, r.y + 17.0f * k, 18.0f * k, r.w - 88.0f * k,
          pa_hex(0x1D4FB8), 0, 0, INK, PA_ALIGN_LEFT);
    int done = 0;
    for (int i = 0; i < META_MISSIONS; i++) if (M->missions[i].state != MIS_ACTIVE) done++;
    Rect bar = { tx, r.y + 47.0f * k, r.x + r.w - 46.0f * k - tx, 22.0f * k };
    pa_round_rect(c, bar.x - 2, bar.y - 2, bar.w + 4, bar.h + 4, bar.h * 0.5f + 2, INK);
    pa_round_rect(c, bar.x, bar.y, bar.w, bar.h, bar.h * 0.5f, pa_hex(0x2A3F75));
    float f = (float)done / (float)META_MISSIONS;
    if (f > 0.0f) {
        float fw = (bar.w - 4) * f;
        PA_Paint fp = vgrad(bar.y, bar.y + bar.h, pa_hex(0xB6FF5C), pa_hex(0x36B526));
        pa_round_rect_paint(c, bar.x + 2, bar.y + 2, fw, bar.h - 4, (bar.h - 4) * 0.5f, &fp);
        pa_round_rect(c, bar.x + 6, bar.y + 4, fw - 10 > 0 ? fw - 10 : 0, 3.0f * k, 1.5f, PA_RGBA(255, 255, 255, 110));
    }
    char t[32];
    snprintf(t, sizeof(t), "%d/%d", done, META_MISSIONS);
    dtext(c, t, bar.x + bar.w * 0.5f, bar.y + bar.h * 0.5f - 6.5f * k, 13.0f * k, 0, CREAM, 0, 2.2f * k, INK, PA_ALIGN_CENTER);
    icon_chest(c, bar.x + bar.w + 18.0f * k, bar.y + bar.h * 0.5f, 15.0f * k, M->bonus_state == MIS_DONE);

    int hrs = M->secs_to_reset / 3600, mins = (M->secs_to_reset / 60) % 60;
    snprintf(t, sizeof(t), "%dh %02dm", hrs, mins);
    icon_clock(c, tx + 8.0f * k, r.y + r.h - 22.0f * k, 8.0f * k, pa_hex(0x4A6BB0));
    utext(c, t, tx + 22.0f * k, r.y + r.h - 28.0f * k, 12.0f * k, pa_hex(0x4A6BB0), PA_ALIGN_LEFT, 0);

    int n = meta_claimable();
    if (n > 0) {
        snprintf(t, sizeof(t), "%d", n);
        red_badge(c, r.x + r.w - 8.0f * k, r.y + 6.0f * k, 14.0f * k * (1.0f + g_badge_kick.x * 0.35f), t);
    }
}

static void draw_streak_card(PA_Canvas *c, Rect r0, float s, float press) {
    if (s <= 0.02f) return;
    const MetaState *M = meta_state();
    float k;
    Rect r = pressed_rect(r0, s, press, &k);
    PA_Paint face = vgrad(r.y, r.y + r.h, pa_hex(0xFFC042), pa_hex(0xFF7417));
    card_base(c, r, 22.0f * k, 9.0f * k * (1.0f - press * 0.6f), pa_hex(0xB8430A), &face);
    clip_push(c, r.x + 4, r.y + 4, r.w - 8, r.h - 8);
    draw_rays(c, r.x + 40.0f * k, r.y + r.h * 0.5f, 90.0f * k, 10, g_time * 0.4f, PA_RGBA(255, 255, 200, 90));
    clip_pop(c);
    icon_flame(c, r.x + 40.0f * k, r.y + r.h * 0.5f, 27.0f * k, g_time);
    char t[24];
    snprintf(t, sizeof(t), "DAY %d", M->streak);
    float tx = r.x + 72.0f * k;
    dtext(c, t, tx, r.y + 26.0f * k, 24.0f * k, r.w - 80.0f * k, CREAM, 0, 3.0f * k, pa_hex(0x8A2A00), PA_ALIGN_LEFT);
    PA_TextStyle st = pa_text_style(PA_FACE_UI, PA_RGBA(255, 255, 255, 235));
    st.caps = 1; st.tracking = 1.2f;
    st.shadow_dy = 1.5f; st.shadow_col = PA_RGBA(140, 40, 0, 160);
    pa_text_ex(c, M->streak_claimed ? "streak" : "claim!", tx + 1.0f, r.y + 64.0f * k, 12.0f * k, &st);
    if (!M->streak_claimed)
        red_badge(c, r.x + r.w - 8.0f * k, r.y + 6.0f * k, 14.0f * k * (1.0f + 0.12f * sinf(g_time * 7.0f)), "!");
}

static float item_pop(int order) {
    float delay = 0.04f + 0.04f * (float)order;
    float t = (g_since_home - delay) / 0.42f;
    if (t <= 0.0f) return 0.0f;
    return ease_out_back(t);
}

/* -------------------------------------------------------------- particles -- */
static void fly_target(int kind, float *x, float *y) {
    HeaderL L = header_layout();
    Rect r = kind == 0 ? L.gem : L.coin;
    *x = r.x + 2.0f;
    *y = r.y + r.h * 0.5f;
}

static void reward_burst(int kind, float x, float y, int amount) {
    if (amount <= 0) return;
    int n = amount / 3;
    if (n < 5) n = 5;
    if (n > 12) n = 12;
    int left = amount;
    for (int i = 0; i < n && g_fly_n < MAX_FLY; i++) {
        Flyer *f = &g_fly[g_fly_n++];
        f->kind = kind;
        f->x0 = x + pa_rng_range(&g_fx, -26.0f, 26.0f);
        f->y0 = y + pa_rng_range(&g_fx, -18.0f, 18.0f);
        float tx, ty;
        fly_target(kind, &tx, &ty);
        f->cx = (f->x0 + tx) * 0.5f + pa_rng_range(&g_fx, -150.0f, 150.0f);
        f->cy = (f->y0 < ty ? f->y0 : ty) + pa_rng_range(&g_fx, 40.0f, 220.0f);
        f->t = 0.0f;
        f->delay = 0.05f * (float)i;
        f->dur = pa_rng_range(&g_fx, 0.55f, 0.75f);
        f->value = i == n - 1 ? left : amount / n;
        left -= f->value;
        f->spin = pa_rng_range(&g_fx, -3.0f, 3.0f);
    }
}

static void spawn_confetti(float x, float y, int n) {
    static const uint32_t cols[] = { 0xFF3B4E, 0xFFC21A, 0x4CD137, 0x3D8BFF, 0xB15CFF, 0xFFFFFF, 0xFF8A1F };
    for (int i = 0; i < n && g_conf_n < MAX_CONF; i++) {
        Confetti *p = &g_conf[g_conf_n++];
        float a = pa_rng_range(&g_fx, -PA_PI * 0.95f, -PA_PI * 0.05f);
        float sp = pa_rng_range(&g_fx, 380.0f, 980.0f);
        p->x = x; p->y = y;
        p->vx = cosf(a) * sp; p->vy = sinf(a) * sp;
        p->rot = pa_rng_range(&g_fx, 0.0f, PA_TAU);
        p->vr = pa_rng_range(&g_fx, -12.0f, 12.0f);
        p->life = pa_rng_range(&g_fx, 1.6f, 2.6f);
        p->w = pa_rng_range(&g_fx, 7.0f, 13.0f);
        p->h = p->w * pa_rng_range(&g_fx, 0.4f, 0.7f);
        p->col = pa_hex(cols[pa_rng_int(&g_fx, 0, 6)]);
    }
}

static void fx_sfx(const char *name) {
    if (g_sfx_gap > 0.0f) return;
    pa_sfx(name);
    g_sfx_gap = 0.06f;
}

static void update_fx(float dt) {
    if (g_sfx_gap > 0.0f) g_sfx_gap -= dt;
    for (int i = 0; i < g_fly_n;) {
        Flyer *f = &g_fly[i];
        f->t += dt;
        if (f->t >= f->delay + f->dur) {
            if (f->kind == 0) { g_hold_gems -= f->value; if (g_hold_gems < 0) g_hold_gems = 0; }
            else { g_hold_coins -= f->value; if (g_hold_coins < 0) g_hold_coins = 0; }
            g_pill_kick[f->kind].v += 9.0f;
            fx_sfx(f->kind == 0 ? "pop" : "coin");
            g_fly[i] = g_fly[--g_fly_n];
            continue;
        }
        i++;
    }
    for (int i = 0; i < g_conf_n;) {
        Confetti *p = &g_conf[i];
        p->life -= dt;
        if (p->life <= 0.0f) { g_conf[i] = g_conf[--g_conf_n]; continue; }
        p->vy += 1100.0f * dt;
        p->vx *= expf(-1.6f * dt);
        p->vy *= expf(-1.2f * dt);
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->rot += p->vr * dt;
        i++;
    }
    for (int k = 0; k < 3; k++) spring_step(&g_pill_kick[k], 0.0f, 260.0f, 12.0f, dt);
    spring_step(&g_badge_kick, 0.0f, 260.0f, 10.0f, dt);
}

static void draw_fx(PA_Canvas *c) {
    for (int i = 0; i < g_conf_n; i++) {
        Confetti *p = &g_conf[i];
        float cs = cosf(p->rot), sn = sinf(p->rot);
        float sx = fabsf(cosf(p->rot * 1.7f));   /* tumbling flip */
        float hw = p->w * 0.5f * (0.25f + 0.75f * sx), hh = p->h * 0.5f;
        PA_Vec2 q[4];
        float px[4] = { -hw, hw, hw, -hw }, py[4] = { -hh, -hh, hh, hh };
        for (int k = 0; k < 4; k++) { q[k].x = p->x + px[k] * cs - py[k] * sn; q[k].y = p->y + px[k] * sn + py[k] * cs; }
        PA_Color col = p->life < 0.4f ? pa_alpha(p->col, p->life / 0.4f) : p->col;
        pa_fill_poly(c, q, 4, col);
    }
    for (int i = 0; i < g_fly_n; i++) {
        Flyer *f = &g_fly[i];
        float u = (f->t - f->delay) / f->dur;
        if (u < 0.0f) continue;
        float e = u * u * (3.0f - 2.0f * u);
        float tx, ty;
        fly_target(f->kind, &tx, &ty);
        float x = (1 - e) * (1 - e) * f->x0 + 2 * e * (1 - e) * f->cx + e * e * tx;
        float y = (1 - e) * (1 - e) * f->y0 + 2 * e * (1 - e) * f->cy + e * e * ty;
        float s = u < 0.2f ? 0.5f + 3.5f * u : 1.2f - 0.4f * u;
        if (f->kind == 0) icon_gem(c, x, y, 15.0f * s);
        else icon_coin(c, x, y, 14.0f * s);
    }
}

/* ============================================================ META FLOWS */
static int   g_coin_burst;     /* coins to throw at the header after a run */
static float g_clock_acc;
static int   g_streak_day_prompted = -1;
static int   g_xpfly_on;
static float g_streak_autoclose;

static void sync_display(void) {
    const MetaState *M = meta_state();
    g_disp_gems = (float)(M->gems - g_hold_gems);
    g_disp_coins = (float)(M->coins - g_hold_coins);
    g_disp_xp = (float)(meta_total_xp() - g_hold_xp);
    int into, need;
    meta_level_from_total((int)g_disp_xp, &g_shown_level, &into, &need);
}

/** Called whenever the lobby comes back: turn what the games reported into
    the celebration sequence. */
static void collect_rewards(void) {
    MetaPending p;
    meta_take_pending(&p);
    if (p.xp > 0) { g_hold_xp += p.xp; g_xpfly_amount = p.xp; g_xpfly_t = -0.55f; g_xpfly_on = 1; }
    if (p.coins > 0) { g_hold_coins += p.coins; g_coin_burst += p.coins; }
    if (p.level_gems > 0) g_hold_gems += p.level_gems;
    if (p.level_to > p.level_from) g_levelup_queue += p.level_to - p.level_from;
    if (p.missions_done > 0) g_badge_kick.v += 14.0f;
}

static void open_modal(Modal m) {
    g_modal = m;
    g_modal_t = 0.0f;
    g_closing = 0;
    g_close_t = 0.0f;
    g_modal_k.x = 0.0f;
    g_modal_k.v = 0.0f;
    g_mhit = -1;
    g_streak_autoclose = 0.0f;
    for (int i = 0; i < 6; i++) g_mpress[i].x = g_mpress[i].v = 0.0f;
    pa_sfx(m == MODAL_LEVELUP ? "levelup" : "select");
}

static void close_modal(void) {
    if (g_modal == MODAL_NONE || g_closing) return;
    g_closing = 1;
    g_close_t = 0.0f;
    pa_sfx("select");
}

/* ------------------------------------------------------- modal geometry -- */
static Rect modal_panel(void) {
    float W = (float)g_view_w, H = (float)g_view_h;
    float w = W - 32.0f;
    if (w > 480.0f) w = 480.0f;
    float h = g_modal == MODAL_STREAK ? 600.0f : 610.0f;
    if (h > H - 56.0f) h = H - 56.0f;
    return (Rect){ (W - w) * 0.5f, (H - h) * 0.5f + 12.0f, w, h };
}

static Rect mission_row(int i) {
    Rect p = modal_panel();
    float top = p.y + 92.0f, bottom = p.y + p.h - 24.0f, gap = 12.0f;
    float rh = (bottom - top - gap * 3.0f) / 4.0f;
    return (Rect){ p.x + 18.0f, top + (float)i * (rh + gap), p.w - 36.0f, rh };
}

static Rect claim_rect(int i) {
    Rect r = mission_row(i);
    float h = r.h * 0.56f;
    if (h > 50.0f) h = 50.0f;
    return (Rect){ r.x + r.w - 116.0f, r.y + (r.h - h) * 0.5f - 3.0f, 102.0f, h };
}

static int streak_compact(void) { return modal_panel().h < 560.0f; }

static Rect streak_box(int day) {   /* day 1..7 */
    Rect p = modal_panel();
    int cmp = streak_compact();
    float top = p.y + (cmp ? 150.0f : 214.0f);
    float bh = cmp ? 88.0f : 112.0f, gap = 10.0f;
    float bw = (p.w - 40.0f - gap * 3.0f) / 4.0f;
    int i = day - 1;
    if (i < 4) return (Rect){ p.x + 20.0f + (float)i * (bw + gap), top, bw, bh };
    i -= 4;
    float y = top + bh + gap + 4.0f;
    if (i < 2) return (Rect){ p.x + 20.0f + (float)i * (bw + gap), y, bw, bh };
    return (Rect){ p.x + 20.0f + 2.0f * (bw + gap), y, bw * 2.0f + gap, bh };
}

static Rect streak_claim(void) {
    Rect p = modal_panel();
    return (Rect){ p.x + p.w * 0.5f - 130.0f, p.y + p.h - 92.0f, 260.0f, 64.0f };
}

static float levelup_unit(void) { return g_view_h < 760 ? 0.72f : 1.0f; }

static Rect levelup_claim(void) {
    float u = levelup_unit();
    float cx = (float)g_view_w * 0.5f, cy = (float)g_view_h * (u < 1.0f ? 0.42f : 0.44f);
    return (Rect){ cx - 130.0f, cy + 196.0f * u, 260.0f, 68.0f };
}

static Rect close_rect(void) {
    Rect p = modal_panel();
    return (Rect){ p.x + p.w - 36.0f, p.y - 16.0f, 52.0f, 52.0f };
}

/* Modal buttons: 0-2 mission claims, 3 chest, 4 close, 5 streak/level claim. */
static int modal_hit(float x, float y) {
    if (g_modal == MODAL_LEVELUP) return in_rect(levelup_claim(), x, y) ? 5 : -1;
    Rect cr = close_rect();
    if (in_rect(cr, x, y)) return 4;
    if (g_modal == MODAL_MISSIONS) {
        const MetaState *M = meta_state();
        for (int i = 0; i < META_MISSIONS; i++)
            if (M->missions[i].state == MIS_DONE && in_rect(claim_rect(i), x, y)) return i;
        if (M->bonus_state == MIS_DONE && in_rect(claim_rect(3), x, y)) return 3;
    }
    if (g_modal == MODAL_STREAK && !meta_state()->streak_claimed && in_rect(streak_claim(), x, y)) return 5;
    return -1;
}

static void modal_act(int id) {
    if (id == 4) { close_modal(); return; }
    if (g_modal == MODAL_MISSIONS && id >= 0 && id <= 3) {
        Rect cr = claim_rect(id);
        int gems = id < 3 ? meta_claim_mission(id) : meta_claim_bonus();
        if (gems > 0) {
            g_hold_gems += gems;
            reward_burst(0, cr.x + cr.w * 0.5f, cr.y + cr.h * 0.5f, gems);
            spawn_confetti(cr.x + cr.w * 0.5f, cr.y, id == 3 ? 60 : 24);
            pa_sfx(id == 3 ? "win" : "good");
        }
        return;
    }
    if (g_modal == MODAL_STREAK && id == 5) {
        Rect cr = streak_claim();
        int gems = meta_claim_streak();
        if (gems > 0) {
            g_hold_gems += gems;
            Rect b = streak_box(meta_streak_cycle_day());
            reward_burst(0, b.x + b.w * 0.5f, b.y + b.h * 0.5f, gems);
            spawn_confetti(cr.x + cr.w * 0.5f, cr.y, 60);
            pa_sfx("win");
            g_streak_autoclose = 1.3f;
        }
        return;
    }
    if (g_modal == MODAL_LEVELUP && id == 5) {
        int gems = meta_levelup_gems(g_modal_level);
        if (gems > g_hold_gems) gems = g_hold_gems;
        Rect cr = levelup_claim();
        reward_burst(0, cr.x + cr.w * 0.5f, cr.y - 60.0f, gems);
        close_modal();
    }
}

static void update_modal(float dt, const PA_Input *in) {
    g_modal_t += dt;
    if (g_closing) {
        g_close_t += dt;
        if (g_close_t >= 0.18f) { g_modal = MODAL_NONE; g_closing = 0; }
        return;
    }
    spring_step(&g_modal_k, 1.0f, 340.0f, 19.0f, dt);
    if (g_streak_autoclose > 0.0f) {
        g_streak_autoclose -= dt;
        if (g_streak_autoclose <= 0.0f) close_modal();
    }
    if (in->pressed) g_mhit = modal_hit(in->x, in->y);
    for (int i = 0; i < 6; i++) {
        int held = g_mhit == i && in->down && modal_hit(in->x, in->y) == i;
        spring_step(&g_mpress[i], held ? 1.0f : 0.0f, 700.0f, 24.0f, dt);
    }
    if (in->released) {
        if (g_mhit >= 0 && in->tapped && modal_hit(in->x, in->y) == g_mhit) {
            g_mpress[g_mhit].x = 1.0f;
            modal_act(g_mhit);
        } else if (g_mhit < 0 && in->tapped && g_modal != MODAL_LEVELUP && g_modal_t > 0.3f &&
                   !in_rect(modal_panel(), in->x, in->y)) {
            close_modal();
        }
        g_mhit = -1;
    }
    if (in->key_pressed[PA_KEY_ENTER] || in->key_pressed[PA_KEY_SPACE]) {
        if (g_modal == MODAL_LEVELUP) modal_act(5);
        else if (g_modal == MODAL_STREAK && !meta_state()->streak_claimed) modal_act(5);
    }
}

/* -------------------------------------------------------- modal drawing -- */
static float modal_dy(void) {
    float H = (float)g_view_h;
    if (g_closing) return pa_smooth(pa_clamp01(g_close_t / 0.18f)) * H * 0.25f;
    return (1.0f - g_modal_k.x) * H * 0.45f;
}

static float modal_alpha(void) {
    float a = pa_clamp01(g_modal_t * 6.0f);
    if (g_closing) a *= 1.0f - pa_clamp01(g_close_t / 0.18f);
    return a;
}

static Rect shift(Rect r, float dy) { r.y += dy; return r; }

static void progress_bar(PA_Canvas *c, Rect b, float f, const char *label) {
    pa_round_rect(c, b.x - 2, b.y - 2, b.w + 4, b.h + 4, b.h * 0.5f + 2, INK);
    pa_round_rect(c, b.x, b.y, b.w, b.h, b.h * 0.5f, pa_hex(0x2A3F75));
    if (f > 0.0f) {
        float fw = (b.w - 4) * pa_clamp01(f);
        if (fw < b.h - 4) fw = b.h - 4;
        PA_Paint fp = vgrad(b.y, b.y + b.h, pa_hex(0xFFE45C), pa_hex(0xFF9F0A));
        if (f >= 1.0f) fp = vgrad(b.y, b.y + b.h, pa_hex(0xB6FF5C), pa_hex(0x36B526));
        pa_round_rect_paint(c, b.x + 2, b.y + 2, fw, b.h - 4, (b.h - 4) * 0.5f, &fp);
        pa_round_rect(c, b.x + 6, b.y + 4, fw - 8 > 0 ? fw - 8 : 0, 3.0f, 1.5f, PA_RGBA(255, 255, 255, 120));
    }
    dtext(c, label, b.x + b.w * 0.5f, b.y + b.h * 0.5f - 6.0f, 12.0f, 0, CREAM, 0, 2.2f, INK, PA_ALIGN_CENTER);
}

static void mission_icon(PA_Canvas *c, int type, float cx, float cy, float r) {
    static const uint32_t cols[MIS_TYPE_COUNT] = { 0x3D8BFF, 0xFFB300, 0x8B4DFF, 0xFF3B4E, 0x4CD137, 0xFF7A1A };
    PA_Color col = pa_hex(cols[type < 0 || type >= MIS_TYPE_COUNT ? 0 : type]);
    pa_fill_circle(c, cx, cy + 3.0f, r + 3.0f, INK);
    pa_fill_circle(c, cx, cy, r + 3.0f, INK);
    PA_Paint p = vgrad(cy - r, cy + r, pa_shade(col, 0.25f), pa_shade(col, -0.1f));
    pa_fill_ellipse_paint(c, cx, cy, r, r, &p);
    pa_fill_ellipse(c, cx, cy - r * 0.45f, r * 0.6f, r * 0.28f, PA_RGBA(255, 255, 255, 70));
    switch (type) {
    case MIS_COINS: icon_coin(c, cx, cy, r * 0.55f); break;
    case MIS_RUNS:  icon_play(c, cx + r * 0.08f, cy, r * 0.62f, CREAM); break;
    case MIS_WINS:  icon_star(c, cx, cy, r * 0.5f, 1); break;
    case MIS_BEST: {
        PA_Vec2 a[3] = { { cx - r * 0.5f, cy + r * 0.05f }, { cx, cy - r * 0.55f }, { cx + r * 0.5f, cy + r * 0.05f } };
        pa_stroke_poly(c, a, 3, 0, r * 0.34f, INK);
        pa_line(c, cx, cy - r * 0.4f, cx, cy + r * 0.55f, r * 0.34f, INK);
        pa_stroke_poly(c, a, 3, 0, r * 0.2f, CREAM);
        pa_line(c, cx, cy - r * 0.4f, cx, cy + r * 0.55f, r * 0.2f, CREAM);
        break;
    }
    case MIS_GAMES: {
        for (int i = 0; i < 4; i++) {
            float x = cx + ((i & 1) ? r * 0.26f : -r * 0.26f), y = cy + ((i & 2) ? r * 0.26f : -r * 0.26f);
            pa_round_rect(c, x - r * 0.22f, y - r * 0.22f, r * 0.44f, r * 0.44f, r * 0.1f, CREAM);
        }
        break;
    }
    default: {
        float s = r * 0.5f;
        pa_round_rect(c, cx - s * 1.3f, cy - s * 0.65f, s * 2.6f, s * 1.3f, s * 0.6f, CREAM);
        pa_fill_rect(c, cx - s * 0.85f, cy - s * 0.1f, s * 0.6f, s * 0.2f, col);
        pa_fill_rect(c, cx - s * 0.65f, cy - s * 0.3f, s * 0.2f, s * 0.6f, col);
        pa_fill_circle(c, cx + s * 0.6f, cy - s * 0.1f, s * 0.16f, col);
        pa_fill_circle(c, cx + s * 0.85f, cy + s * 0.15f, s * 0.16f, col);
        break;
    }
    }
}

static void draw_missions_modal(PA_Canvas *c, float dy) {
    const MetaState *M = meta_state();
    Rect p = shift(modal_panel(), dy);
    panel(c, p);
    ribbon(c, p.x + p.w * 0.5f, p.y + 2.0f, p.w * 0.7f, 56.0f, pa_hex(0x3D8BFF), "DAILY MISSIONS");
    char t[64];
    snprintf(t, sizeof(t), "New missions in %dh %02dm", M->secs_to_reset / 3600, (M->secs_to_reset / 60) % 60);
    PA_TextStyle ts = pa_text_style(PA_FACE_UI, pa_hex(0x4A6BB0));
    ts.align = PA_ALIGN_CENTER;
    float tw = pa_text_measure(t, 14.0f, &ts).width;
    icon_clock(c, p.x + p.w * 0.5f - tw * 0.5f - 8.0f, p.y + 57.0f, 9.0f, pa_hex(0x4A6BB0));
    pa_text_ex(c, t, p.x + p.w * 0.5f + 10.0f, p.y + 50.0f, 14.0f, &ts);

    for (int i = 0; i < 4; i++) {
        Rect r = shift(mission_row(i), dy);
        int bonus = i == 3;
        const MetaMission *m = bonus ? NULL : &M->missions[i];
        int state = bonus ? M->bonus_state : m->state;
        PA_Color rowcol = state == MIS_CLAIMED ? pa_hex(0xDDE6F2) : bonus ? pa_hex(0xFFF1C9) : CREAM;
        pa_round_rect(c, r.x - 2, r.y - 2, r.w + 4, r.h + 9, 18.0f, pa_hex(0x9FB4D8));
        pa_round_rect(c, r.x, r.y + 4, r.w, r.h, 16.0f, pa_hex(0xB9CBE6));
        pa_round_rect(c, r.x, r.y, r.w, r.h, 16.0f, rowcol);
        float ir = r.h * 0.3f;
        if (ir > 24.0f) ir = 24.0f;
        float icx = r.x + 14.0f + ir, icy = r.y + r.h * 0.5f;
        if (bonus) icon_chest(c, icx, icy + 2.0f, ir * 0.8f, state == MIS_DONE);
        else mission_icon(c, m->type, icx, icy, ir);

        float tx = icx + ir + 14.0f;
        Rect cr = shift(claim_rect(i), dy);
        float maxw = cr.x - tx - 10.0f;
        const char *label = bonus ? "Complete all missions" : m->label;
        dtext(c, label, tx, r.y + r.h * 0.5f - 22.0f, 15.0f, maxw,
              state == MIS_CLAIMED ? pa_hex(0x8A9BBB) : pa_hex(0x1D3F8F), 0, 0, INK, PA_ALIGN_LEFT);
        int prog = bonus ? 0 : m->progress, target = bonus ? META_MISSIONS : m->target;
        if (bonus) for (int k = 0; k < META_MISSIONS; k++) if (M->missions[k].state == MIS_CLAIMED) prog++;
        char a[16], b[16], lab[40];
        pa_fmt_int(prog, a, sizeof(a));
        pa_fmt_int(target, b, sizeof(b));
        snprintf(lab, sizeof(lab), "%s/%s", a, b);
        progress_bar(c, (Rect){ tx, r.y + r.h * 0.5f + 4.0f, maxw, 18.0f }, (float)prog / (float)target, lab);

        int reward = bonus ? M->bonus_reward : m->reward;
        if (state == MIS_DONE) {
            float pr = g_mpress[i].x;
            float pulse = 1.0f + 0.05f * sinf(g_time * 7.0f + (float)i);
            Rect pb = rect_scale(cr, pulse);
            chunky(c, pb, 14.0f, GREEN, pr);
            button_label(c, pb, pr, "CLAIM", 20.0f, GREEN);
        } else if (state == MIS_CLAIMED) {
            pa_fill_circle(c, cr.x + cr.w * 0.5f, cr.y + cr.h * 0.5f + 3.0f, 19.0f, INK);
            pa_fill_circle(c, cr.x + cr.w * 0.5f, cr.y + cr.h * 0.5f, 19.0f, GREEN);
            icon_check(c, cr.x + cr.w * 0.5f, cr.y + cr.h * 0.5f, 15.0f, CREAM);
        } else {
            Rect rb = { cr.x + 8.0f, cr.y + 4.0f, cr.w - 8.0f, cr.h - 8.0f };
            pa_round_rect(c, rb.x, rb.y, rb.w, rb.h, rb.h * 0.5f, pa_hex(0x2A4C8F));
            snprintf(t, sizeof(t), "%d", reward);
            dtext(c, t, rb.x + rb.w - 14.0f, rb.y + rb.h * 0.5f - 9.0f, 18.0f, 0, CREAM, 0, 2.4f, INK, PA_ALIGN_RIGHT);
            icon_gem(c, rb.x + 18.0f, rb.y + rb.h * 0.5f, 12.0f);
        }
    }
    Rect xr = shift(close_rect(), dy);
    close_button(c, xr.x + xr.w * 0.5f, xr.y + xr.h * 0.5f, 22.0f, g_mpress[4].x);
}

static void draw_streak_modal(PA_Canvas *c, float dy) {
    const MetaState *M = meta_state();
    Rect p = shift(modal_panel(), dy);
    int cmp = streak_compact();
    panel(c, p);
    float cx = p.x + p.w * 0.5f;
    float fy = p.y + (cmp ? 62.0f : 92.0f);
    clip_push(c, p.x + 4, p.y + 30, p.w - 8, p.h - 40);
    draw_rays(c, cx, fy, cmp ? 120.0f : 170.0f, 14, g_time * 0.35f, PA_RGBA(255, 190, 60, 120));
    clip_pop(c);
    ribbon(c, cx, p.y + 2.0f, p.w * 0.66f, 56.0f, pa_hex(0xFF8A1F), "DAILY REWARD");
    icon_flame(c, cx, fy + 8.0f, cmp ? 26.0f : 36.0f, g_time);
    char t[48];
    snprintf(t, sizeof(t), "%d DAY STREAK", M->streak);
    dtext(c, t, cx, fy + (cmp ? 38.0f : 54.0f), cmp ? 24.0f : 30.0f, p.w - 40.0f,
          pa_hex(0xFFE45C), pa_hex(0xFF9F0A), 3.5f, INK, PA_ALIGN_CENTER);
    if (!cmp) {
        PA_TextStyle ts = pa_text_style(PA_FACE_UI, pa_hex(0x4A6BB0));
        ts.align = PA_ALIGN_CENTER;
        pa_text_ex(c, "Play every day for bigger rewards", cx, fy + 96.0f, 14.0f, &ts);
    }

    int today = meta_streak_cycle_day();
    for (int d = 1; d <= META_STREAK_CYCLE; d++) {
        Rect b = shift(streak_box(d), dy);
        int past = d < today || (d == today && M->streak_claimed);
        int now = d == today && !M->streak_claimed;
        float bob = now ? sinf(g_time * 5.0f) * 2.5f : 0.0f;
        b.y += bob;
        PA_Color face = past ? pa_hex(0xC9D6EA) : now ? pa_hex(0xFFD54A) : d == 7 ? pa_hex(0xB98CFF) : pa_hex(0x6FB2FF);
        if (now) {
            PA_Paint glow = pa_radial(b.x + b.w * 0.5f, b.y + b.h * 0.5f, 0.0f, b.w);
            pa_stop(&glow, 0.0f, PA_RGBA(255, 230, 120, 170));
            pa_stop(&glow, 1.0f, PA_RGBA(255, 230, 120, 0));
            pa_fill_ellipse_paint(c, b.x + b.w * 0.5f, b.y + b.h * 0.5f, b.w * 0.9f, b.h * 0.85f, &glow);
        }
        pa_round_rect(c, b.x - 3, b.y - 3, b.w + 6, b.h + 10, 16.0f, INK);
        pa_round_rect(c, b.x, b.y + 5, b.w, b.h, 14.0f, pa_shade(face, -0.35f));
        PA_Paint fp = vgrad(b.y, b.y + b.h, pa_shade(face, 0.25f), face);
        pa_round_rect_paint(c, b.x, b.y, b.w, b.h, 14.0f, &fp);
        pa_round_rect(c, b.x + 8, b.y + 3, b.w - 16, 4.0f, 2.0f, PA_RGBA(255, 255, 255, 110));
        snprintf(t, sizeof(t), "DAY %d", d);
        dtext(c, t, b.x + b.w * 0.5f, b.y + 9.0f, 13.0f, b.w - 10.0f, CREAM, 0, 2.2f, INK, PA_ALIGN_CENTER);
        float iy = b.y + b.h * 0.5f + 4.0f;
        if (d == 7) icon_chest(c, b.x + b.w * 0.32f, iy + 2.0f, cmp ? 17.0f : 21.0f, 0);
        else icon_gem(c, b.x + b.w * 0.5f, iy - 2.0f, cmp ? 13.0f : 16.0f);
        snprintf(t, sizeof(t), "%d", meta_streak_reward(d));
        if (d == 7) dtext(c, t, b.x + b.w * 0.68f, iy - 12.0f, 24.0f, 0, CREAM, 0, 3.0f, INK, PA_ALIGN_CENTER);
        else dtext(c, t, b.x + b.w * 0.5f, b.y + b.h - 24.0f, 16.0f, 0, CREAM, 0, 2.6f, INK, PA_ALIGN_CENTER);
        if (past) {
            pa_round_rect(c, b.x, b.y, b.w, b.h, 14.0f, PA_RGBA(40, 60, 100, 70));
            pa_fill_circle(c, b.x + b.w * 0.5f, iy + 2.0f, 18.0f, INK);
            pa_fill_circle(c, b.x + b.w * 0.5f, iy, 17.0f, GREEN);
            icon_check(c, b.x + b.w * 0.5f, iy, 13.0f, CREAM);
        }
    }

    Rect cr = shift(streak_claim(), dy);
    if (!M->streak_claimed) {
        float pulse = 1.0f + 0.04f * sinf(g_time * 6.0f);
        Rect pb = rect_scale(cr, pulse);
        chunky(c, pb, 20.0f, GREEN, g_mpress[5].x);
        button_label(c, pb, g_mpress[5].x, "CLAIM", 30.0f, GREEN);
    } else {
        snprintf(t, sizeof(t), "Next reward in %dh %02dm", M->secs_to_reset / 3600, (M->secs_to_reset / 60) % 60);
        dtext(c, t, cr.x + cr.w * 0.5f, cr.y + 22.0f, 18.0f, p.w - 40.0f, pa_hex(0x4A6BB0), 0, 0, INK, PA_ALIGN_CENTER);
    }
    Rect xr = shift(close_rect(), dy);
    close_button(c, xr.x + xr.w * 0.5f, xr.y + xr.h * 0.5f, 22.0f, g_mpress[4].x);
}

static void draw_levelup_modal(PA_Canvas *c, float dy) {
    float W = (float)c->w, H = (float)c->h;
    float u = levelup_unit();
    float cx = W * 0.5f, cy = H * (u < 1.0f ? 0.42f : 0.44f) + dy;
    float pop = ease_out_back(g_modal_t / 0.45f);
    draw_rays(c, cx, cy, (W > H ? W : H) * 0.75f, 16, g_time * 0.3f, PA_RGBA(255, 220, 90, 150));
    PA_Paint glow = pa_radial(cx, cy, 0.0f, 220.0f * u);
    pa_stop(&glow, 0.0f, PA_RGBA(255, 250, 210, 200));
    pa_stop(&glow, 1.0f, PA_RGBA(255, 250, 210, 0));
    pa_fill_ellipse_paint(c, cx, cy, 220.0f * u, 220.0f * u, &glow);

    float tpop = ease_out_back((g_modal_t - 0.12f) / 0.4f);
    if (tpop > 0.01f) {
        float ts = 58.0f * u * tpop * (1.0f + 0.03f * sinf(g_time * 5.0f));
        dtext(c, "LEVEL UP!", cx, cy - 178.0f * u - ts * 0.5f, ts, 0, pa_hex(0xFFF27A), pa_hex(0xFFA312),
              6.0f * u * tpop, INK, PA_ALIGN_CENTER);
    }
    level_badge(c, cx, cy, 92.0f * u * pop, g_modal_level, sinf(g_time * 1.4f) * 0.05f);

    float rp = ease_out_back((g_modal_t - 0.3f) / 0.4f);
    if (rp > 0.01f) {
        char t[24];
        snprintf(t, sizeof(t), "+%d", meta_levelup_gems(g_modal_level));
        Rect pr = rect_scale((Rect){ cx - 82.0f, cy + 118.0f * u, 164.0f, 50.0f }, rp);
        pill(c, pr);
        icon_gem(c, pr.x + 8.0f, pr.y + pr.h * 0.5f, 21.0f * rp);
        dtext(c, t, pr.x + pr.w - 18.0f, pr.y + pr.h * 0.5f - 12.0f * rp, 24.0f * rp, 0, CREAM, 0, 3.0f, INK, PA_ALIGN_RIGHT);
    }
    float bp = ease_out_back((g_modal_t - 0.45f) / 0.4f);
    if (bp > 0.01f) {
        Rect cr = levelup_claim();
        cr.y += dy;
        Rect pb = rect_scale(cr, bp * (1.0f + 0.04f * sinf(g_time * 6.0f)));
        chunky(c, pb, 20.0f, GREEN, g_mpress[5].x);
        button_label(c, pb, g_mpress[5].x, "CLAIM", 30.0f * bp, GREEN);
    }
}

static void draw_modal(PA_Canvas *c) {
    if (g_modal == MODAL_NONE) return;
    float a = modal_alpha();
    int lv = g_modal == MODAL_LEVELUP;
    pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(10, 22, 70, (int)((lv ? 200.0f : 150.0f) * a)));
    float dy = modal_dy();
    if (g_modal == MODAL_MISSIONS) draw_missions_modal(c, dy);
    else if (g_modal == MODAL_STREAK) draw_streak_modal(c, dy);
    else draw_levelup_modal(c, dy);
}

/* -------------------------------------------------------------- XP flyer -- */
static void draw_xpfly(PA_Canvas *c) {
    if (!g_xpfly_on || g_xpfly_t < 0.0f) return;
    HeaderL L = header_layout();
    float W = (float)c->w, H = (float)c->h;
    char t[24];
    snprintf(t, sizeof(t), "+%d XP", g_xpfly_amount);
    float x = W * 0.5f, y = H * 0.42f, s;
    if (g_xpfly_t < 0.55f) {
        s = ease_out_back(g_xpfly_t / 0.35f);
    } else {
        float e = ease_in_out((g_xpfly_t - 0.55f) / 0.5f);
        float tx = L.xp.x + L.xp.w * 0.5f, ty = L.xp.y + L.xp.h * 0.5f;
        x = pa_lerpf(x, tx, e);
        y = pa_lerpf(y, ty, e) - sinf(e * PA_PI) * 60.0f;
        s = 1.0f - 0.6f * e;
    }
    float ts = 46.0f * s;
    PA_Paint dim = pa_radial(x, y, 0.0f, 190.0f * s + 1.0f);
    pa_stop(&dim, 0.0f, PA_RGBA(10, 22, 70, 150));
    pa_stop(&dim, 1.0f, PA_RGBA(10, 22, 70, 0));
    pa_fill_ellipse_paint(c, x, y, 220.0f * s + 1.0f, 120.0f * s + 1.0f, &dim);
    PA_Paint glow = pa_radial(x, y, 0.0f, 120.0f * s + 1.0f);
    pa_stop(&glow, 0.0f, PA_RGBA(190, 255, 120, 150));
    pa_stop(&glow, 1.0f, PA_RGBA(190, 255, 120, 0));
    pa_fill_ellipse_paint(c, x, y, 140.0f * s + 1.0f, 80.0f * s + 1.0f, &glow);
    dtext(c, t, x, y - ts * 0.5f, ts, 0, pa_hex(0xE8FF7A), pa_hex(0x52D62A), 5.0f * s, INK, PA_ALIGN_CENTER);
}

/* ------------------------------------------------------------------- home -- */
static void draw_home(PA_Canvas *c) {
    layout();
    draw_background(c);
    HeaderL hl = header_layout();
    float W = (float)c->w, H = (float)c->h;
    clip_push(c, 0, hl.h, W, H - hl.h);

    draw_featured(c, scr(g_lay.featured), item_pop(0), g_press[BTN_FEATURED].x);
    draw_missions_card(c, scr(g_lay.missions), item_pop(1), g_press[BTN_MISSIONS].x);
    draw_streak_card(c, scr(g_lay.streak), item_pop(2), g_press[BTN_STREAK].x);

    float ly = hl.h + g_lay.label_y - g_scroll;
    float pad = g_lay.featured.x;
    float lp = item_pop(3);
    if (lp > 0.01f && ly < H && ly > -60.0f) {
        dtext(c, "ALL GAMES", pad + 4.0f, ly, 24.0f * lp, 0, CREAM, 0, 3.2f, INK, PA_ALIGN_LEFT);
        PA_TextStyle st = dstyle(CREAM, 0, 3.2f, INK, PA_ALIGN_LEFT);
        float w = pa_text_measure("ALL GAMES", 24.0f, &st).width;
        char t[8];
        snprintf(t, sizeof(t), "%d", GAME_COUNT);
        Rect pr = { pad + w + 16.0f, ly - 2.0f, 40.0f, 28.0f };
        pa_round_rect(c, pr.x - 2, pr.y - 2, pr.w + 4, pr.h + 4, 16.0f, INK);
        pa_round_rect(c, pr.x, pr.y, pr.w, pr.h, 14.0f, pa_hex(0x1D52BF));
        dtext(c, t, pr.x + pr.w * 0.5f, pr.y + 7.0f, 15.0f, 0, CREAM, 0, 0, INK, PA_ALIGN_CENTER);
    }

    for (int i = 0; i < GAME_COUNT; i++) {
        Rect r = scr(g_lay.tile[i]);
        if (r.y > H + 20.0f || r.y + r.h + 20.0f < hl.h) continue;
        draw_tile(c, i, r, item_pop(4 + i), g_press[BTN_TILE0 + i].x);
    }
    clip_pop(c);

    draw_header(c);
    draw_fx(c);
    draw_xpfly(c);
    draw_modal(c);
}

/* ============================================================ PAUSE + GAME */
static int   g_pending_quit_home;
static float g_pause_x, g_pause_y, g_pause_r = 22.0f;
static int   g_pause_hidden;
static float g_pause_anim;
static Spring g_ppress[3];
static int   g_phit = -1;
static int   g_volume = 80;

void pa_hub_pause_anchor(float cx, float cy, float radius) {
    g_pause_x = cx; g_pause_y = cy; g_pause_r = radius;
}
void pa_hub_hide_pause(void) { g_pause_hidden = 1; }

static void begin_exit(void) {
    if (g_screen != SCREEN_GAME && g_screen != SCREEN_PAUSE) return;
    g_screen = SCREEN_EXIT;
    g_exit_t = 0.0f;
    pa_tone(820, 260, 0.26f, 1, 0.07f);
}
void pa_hub_exit(void) { begin_exit(); }
void pa_hub_pause(void) {
    if (g_screen == SCREEN_GAME) {
        g_screen = SCREEN_PAUSE;
        g_pause_anim = 0.0f;
        g_phit = -1;
        for (int i = 0; i < 3; i++) g_ppress[i].x = g_ppress[i].v = 0.0f;
        pa_sfx("select");
    }
}

static Rect pause_card(void) {
    float W = (float)g_view_w, H = (float)g_view_h;
    float w = W * 0.86f;
    if (w > 420.0f) w = 420.0f;
    float h = 540.0f;
    if (h > H - 70.0f) h = H - 70.0f;
    return (Rect){ (W - w) * 0.5f, (H - h) * 0.5f + 14.0f, w, h };
}

static Rect pause_button(int which) {
    Rect p = pause_card();
    float bw = p.w - 64.0f, bh = 70.0f;
    if (which == 2) return (Rect){ p.x + p.w - 62.0f, p.y + 18.0f, 46.0f, 46.0f };
    float y = which == 0 ? p.y + p.h - bh * 2.0f - 50.0f : p.y + p.h - bh - 30.0f;
    return (Rect){ p.x + 32.0f, y, bw, bh };
}

static void draw_pause_glyph(PA_Canvas *c) {
    float x = g_pause_x, y = g_pause_y, r = g_pause_r;
    pa_fill_circle(c, x, y + r * 0.24f, r + 2.5f, PA_RGBA(0, 0, 0, 60));
    pa_fill_circle(c, x, y + r * 0.12f, r + 2.5f, INK);
    pa_fill_circle(c, x, y, r + 2.5f, INK);
    pa_fill_circle(c, x, y + r * 0.12f, r, pa_hex(0x9DB3DA));
    PA_Paint p = vgrad(y - r, y + r, pa_hex(0xFFFFFF), pa_hex(0xDCE8FA));
    pa_fill_ellipse_paint(c, x, y, r, r * 0.94f, &p);
    float bw = r * 0.24f, bh = r * 0.82f;
    pa_round_rect(c, x - r * 0.30f - bw * 0.5f, y - bh * 0.5f, bw, bh, bw * 0.45f, INK);
    pa_round_rect(c, x + r * 0.30f - bw * 0.5f, y - bh * 0.5f, bw, bh, bw * 0.45f, INK);
}

static void draw_pause_sheet(PA_Canvas *c) {
    float k = pa_clamp01(g_pause_anim);
    float e = ease_out_back(k);
    pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(10, 22, 70, (int)(165.0f * pa_clamp01(k * 2.5f))));
    float dy = (1.0f - e) * (float)c->h * 0.3f;
    Rect p = shift(pause_card(), dy);
    panel(c, p);
    ribbon(c, p.x + p.w * 0.5f, p.y + 2.0f, p.w * 0.62f, 58.0f, pa_hex(0xFFA41B), "PAUSED");

    Rect r0 = shift(pause_button(0), dy);
    float top = p.y + 72.0f, bottom = r0.y - 26.0f;
    if (g_active && bottom - top > 110.0f) {
        Rect win = { p.x + 26.0f, top, p.w - 52.0f, bottom - top };
        pa_round_rect(c, win.x - 6, win.y - 6, win.w + 12, win.h + 14, 22.0f, INK);
        pa_round_rect(c, win.x - 3, win.y - 3, win.w + 6, win.h + 9, 20.0f, pa_shade(g_active->accent, -0.4f));
        draw_thumb(c, g_active, win, g_time);
        PA_Paint fade = vgrad(win.y + win.h * 0.5f, win.y + win.h, PA_RGBA(8, 20, 60, 0), PA_RGBA(8, 20, 60, 190));
        pa_fill_rect_paint(c, win.x, win.y + win.h * 0.5f, win.w, win.h * 0.5f, &fade);
        PA_Paint fr = pa_flat(pa_shade(g_active->accent, -0.4f));
        fill_frame(c, (Rect){ win.x - 3, win.y - 3, win.w + 6, win.h + 6 }, 20.0f, win, 16.0f, &fr);
        dtext(c, g_active->name, win.x + 16.0f, win.y + win.h - 44.0f, 28.0f, win.w - 32.0f,
              CREAM, pa_hex(0xFFF1B0), 3.5f, INK, PA_ALIGN_LEFT);
    }
    const char *labels[2] = { "RESUME", "HOME" };
    PA_Color cols[2] = { GREEN, BLUE_BTN };
    for (int i = 0; i < 2; i++) {
        Rect r = shift(pause_button(i), dy);
        float pr = g_ppress[i].x;
        chunky(c, r, 22.0f, cols[i], pr);
        float sink = 7.0f * pr * 0.7f;
        PA_TextStyle st = dstyle(CREAM, 0, 3.0f, pa_shade(cols[i], -0.6f), PA_ALIGN_LEFT);
        float tw = pa_text_measure(labels[i], 28.0f, &st).width;
        float total = tw + 46.0f;
        float x0 = r.x + (r.w - total) * 0.5f, cy = r.y + sink + r.h * 0.5f;
        if (i == 0) icon_play(c, x0 + 14.0f, cy, 17.0f, CREAM);
        else icon_home(c, x0 + 14.0f, cy + 2.0f, 17.0f);
        dtext(c, labels[i], x0 + 46.0f, cy - 15.0f, 28.0f, 0, CREAM, 0, 3.0f, pa_shade(cols[i], -0.6f), PA_ALIGN_LEFT);
    }
    Rect sr = shift(pause_button(2), dy);
    float pr = g_ppress[2].x;
    chunky(c, (Rect){ sr.x, sr.y, sr.w, sr.h - 6.0f }, 14.0f, g_volume > 0 ? pa_hex(0xFFA41B) : pa_hex(0x8A97B3), pr);
    icon_speaker(c, sr.x + sr.w * 0.5f, sr.y + (sr.h - 6.0f) * 0.5f + pr * 5.0f, 15.0f, g_volume > 0);
}

/* ------------------------------------------------------------ transitions -- */
static void start_game(int index) {
    g_active = GAMES[index];
    g_active_idx = index;
    if (g_active->start) g_active->start();
    g_screen = SCREEN_GAME;
    g_reveal_t = 0.0f;
    g_reveal_col = g_active->accent;
    g_reveal_name = 1;
}

static void begin_launch(int index, Rect from) {
    if (index < 0 || index >= GAME_COUNT) return;
    g_screen = SCREEN_LAUNCH;
    g_launch_idx = index;
    g_launch_t = 0.0f;
    g_launch_from = from;
    pa_sfx("select");
    pa_tone(260, 980, 0.32f, 1, 0.07f);
}

static void go_home(void) {
    if (g_active && g_active->stop) g_active->stop();
    pa_set_landscape(0);
    g_reveal_col = g_active ? g_active->accent : SKY_BOTTOM;
    g_active = NULL;
    g_active_idx = -1;
    g_screen = SCREEN_HOME;
    g_reveal_t = 0.0f;
    g_reveal_name = 0;
    g_since_home = 0.0f;
    g_hit = -1;
    g_dragging = 0;
    for (int i = 0; i < BTN_COUNT; i++) g_press[i].x = g_press[i].v = 0.0f;
    collect_rewards();
    pa_sfx("pop");
}

static void draw_launch(PA_Canvas *c) {
    float W = (float)c->w, H = (float)c->h;
    float t = g_launch_t;
    const PA_Game *g = GAMES[g_launch_idx];
    float e = ease_in_out((t - 0.05f) / 0.40f);
    pa_fill_rect(c, 0, 0, W, H, PA_RGBA(10, 22, 70, (int)(140.0f * e)));
    Rect a = g_launch_from;
    Rect r = { pa_lerpf(a.x, -4.0f, e), pa_lerpf(a.y, -4.0f, e), pa_lerpf(a.w, W + 8.0f, e), pa_lerpf(a.h, H + 8.0f, e) };
    float pop = t < 0.08f ? 1.0f + 0.05f * sinf(t / 0.08f * PA_PI) : 1.0f;
    r = rect_scale(r, pop);
    float R = 26.0f * (1.0f - e), b = 8.0f * (1.0f - e) + 0.01f;
    pa_round_rect(c, r.x - 3, r.y - 3, r.w + 6, r.h + 6, R + 3, INK);
    Rect win = { r.x + b, r.y + b, r.w - 2 * b, r.h - 2 * b };
    draw_thumb(c, g, win, g_time);
    float tint = pa_smooth(pa_clamp01((t - 0.18f) / 0.3f));
    PA_Paint tp = pa_radial(W * 0.5f, H * 0.5f, 0.0f, H * 0.7f);
    pa_stop(&tp, 0.0f, pa_alpha(pa_shade(g->accent, 0.18f), tint));
    pa_stop(&tp, 1.0f, pa_alpha(pa_shade(g->accent, -0.22f), tint));
    pa_fill_rect_paint(c, win.x, win.y, win.w, win.h, &tp);
    PA_Paint fr = vgrad(r.y, r.y + r.h, pa_shade(g->accent, 0.28f), pa_shade(g->accent, -0.12f));
    fill_frame(c, r, R, win, R * 0.6f, &fr);

    float np = ease_out_back((t - 0.30f) / 0.28f);
    if (np > 0.01f) {
        float ts = 54.0f * np;
        dtext(c, g->name, W * 0.5f, H * 0.5f - ts * 0.8f, ts, W - 40.0f, CREAM, pa_hex(0xFFF1B0), 5.0f * np, INK, PA_ALIGN_CENTER);
        PA_TextStyle gs = pa_text_style(PA_FACE_UI, CREAM);
        gs.caps = 1; gs.tracking = 3.0f; gs.align = PA_ALIGN_CENTER;
        gs.shadow_dy = 2.0f; gs.shadow_col = PA_RGBA(0, 0, 0, 90);
        pa_text_ex(c, g->genre, W * 0.5f, H * 0.5f + ts * 0.5f, 16.0f * np, &gs);
    }
}

static void draw_reveal(PA_Canvas *c) {
    if (g_reveal_t >= REVEAL_DUR) return;
    float W = (float)c->w, H = (float)c->h;
    float diag = sqrtf(W * W + H * H) * 0.5f + 10.0f;
    float e = pa_clamp01(g_reveal_t / REVEAL_DUR);
    float r = e * e * diag * 1.02f;
    draw_iris(c, W * 0.5f, H * 0.5f, r, g_reveal_col);
    if (g_reveal_name && g_active) {
        float a = 1.0f - pa_clamp01(g_reveal_t / 0.14f);
        if (a > 0.01f) {
            float ts = 54.0f * (1.0f + 0.25f * (1.0f - a));
            dtext(c, g_active->name, W * 0.5f, H * 0.5f - ts * 0.8f, ts, W - 40.0f, pa_alpha(CREAM, a),
                  0, 5.0f, pa_alpha(INK, a), PA_ALIGN_CENTER);
        }
    }
}

static void draw_exit(PA_Canvas *c) {
    float W = (float)c->w, H = (float)c->h;
    float diag = sqrtf(W * W + H * H) * 0.5f + 10.0f;
    float e = ease_in_out(g_exit_t / EXIT_DUR);
    draw_iris(c, W * 0.5f, H * 0.5f, (1.0f - e) * diag, g_active ? g_active->accent : SKY_BOTTOM);
}

/* ============================================================= PUBLIC API */
int pa_app_debug_find(const char *id) {
    for (int i = 0; i < GAME_COUNT; i++) if (!strcmp(GAMES[i]->id, id)) return i;
    return -1;
}

/* Headless capture entry: launches a game straight from the command line so the
   build can be verified from CI, where nothing can click a card. */
void pa_app_debug_launch(int index) {
    if (index >= 0 && index < GAME_COUNT) {
        start_game(index);
        g_reveal_t = 9.0f;     /* captures start on the game itself */
    }
}

void pa_app_init(int w, int h) {
    g_view_w = w;
    g_view_h = h;
    g_screen = SCREEN_HOME;
    g_active = NULL;
    g_time = 0.0f;
    g_quit = 0;
    g_scroll = 0.0f;
    g_since_home = 0.0f;
    g_reveal_t = 9.0f;
    pa_rng_seed(&g_fx, 0xA11CEu);
    pa_save_load();
    g_volume = pa_save_get("volume", 80);
    pa_audio_set_volume((float)g_volume / 100.0f);
    meta_init(GAMES, GAME_COUNT);
    sync_display();
    if (pa_demo_mode() == 3) {
        /* Review capture of the return-home celebration: a Helix run that
           lands a level-up, through the same call a game makes. */
        PA_RunReport r = { 12840, 160, 1, 23, 3 };
        pa_meta_report("helix", &r);
        collect_rewards();
    }
    layout();
}

void pa_app_shutdown(void) {
    if (g_active && g_active->stop) g_active->stop();
    g_active = NULL;
    pa_save_flush();
}

int pa_app_should_quit(void) { return g_quit; }
int pa_app_in_game(void) { return g_screen == SCREEN_GAME || g_screen == SCREEN_PAUSE || g_screen == SCREEN_EXIT; }

/* ----------------------------------------------------------- home update -- */
static void activate(int id) {
    g_press[id].x = 1.0f;
    g_press[id].v = 0.0f;
    if (id == BTN_FEATURED) begin_launch(meta_state()->featured, button_rect(id));
    else if (id == BTN_MISSIONS) open_modal(MODAL_MISSIONS);
    else if (id == BTN_STREAK) open_modal(MODAL_STREAK);
    else begin_launch(id - BTN_TILE0, button_rect(id));
}

static float rubber(float x) { return x * 0.5f / (1.0f + x / 420.0f); }

static void update_scroll(float dt, const PA_Input *in) {
    if (in->wheel != 0.0f && g_scroll_max > 0.0f) {
        g_scroll = pa_clampf(g_scroll - in->wheel * 96.0f, 0.0f, g_scroll_max);
        g_scroll_vel = 0.0f;
    }
    float key = 0.0f;
    if (in->keys[PA_KEY_DOWN]) key += 1.0f;
    if (in->keys[PA_KEY_UP])   key -= 1.0f;
    if (key != 0.0f) { g_scroll = pa_clampf(g_scroll + key * 900.0f * dt, 0.0f, g_scroll_max); g_scroll_vel = 0.0f; }
    if (in->key_pressed[PA_KEY_PAGEDOWN]) g_scroll_vel = 2400.0f;
    if (in->key_pressed[PA_KEY_PAGEUP])   g_scroll_vel = -2400.0f;
    if (in->key_pressed[PA_KEY_HOME])     { g_scroll = 0.0f; g_scroll_vel = 0.0f; }
    if (in->key_pressed[PA_KEY_END])      { g_scroll = g_scroll_max; g_scroll_vel = 0.0f; }

    if (in->pressed) {
        g_hit = hit_home(in->x, in->y);
        g_drag_from_y = in->y;
        g_scroll_from = g_scroll;
        g_drag_travel = 0.0f;
        g_dragging = 0;
        g_scroll_vel = 0.0f;
        g_sample_n = 0;
    }
    if (in->down) {
        float moved = g_drag_from_y - in->y;
        if (fabsf(moved) > g_drag_travel) g_drag_travel = fabsf(moved);
        /* Only once the finger has clearly travelled does this become a
           scroll; below that it is still a press on a card. */
        if (g_drag_travel > 10.0f && !g_dragging) {
            g_dragging = 1;
            g_hit = -1;
        }
        if (g_dragging) {
            float raw = g_scroll_from + moved;
            if (raw < 0.0f) g_scroll = -rubber(-raw);
            else if (raw > g_scroll_max) g_scroll = g_scroll_max + rubber(raw - g_scroll_max);
            else g_scroll = raw;
        }
        if (g_sample_n == 16) { memmove(g_samples, g_samples + 1, sizeof(g_samples[0]) * 15); g_sample_n = 15; }
        g_samples[g_sample_n][0] = g_time;
        g_samples[g_sample_n][1] = in->y;
        g_sample_n++;
    }
    if (in->released) {
        if (g_dragging && g_sample_n > 1) {
            /* Fling velocity over the last 0.1 s of the drag, not the last
               frame: touch samples arrive unevenly. */
            int j = g_sample_n - 1, i = j;
            while (i > 0 && g_time - g_samples[i - 1][0] <= 0.1f) i--;
            float span = g_samples[j][0] - g_samples[i][0];
            if (span > 0.008f) g_scroll_vel = -(g_samples[j][1] - g_samples[i][1]) / span;
            g_scroll_vel = pa_clampf(g_scroll_vel, -5200.0f, 5200.0f);
        } else if (in->tapped && g_hit >= 0 && hit_home(in->x, in->y) == g_hit) {
            activate(g_hit);
        }
        g_hit = -1;
        g_dragging = 0;
    }
    if (!in->down) {
        g_scroll += g_scroll_vel * dt;
        g_scroll_vel *= expf(-2.4f * dt);
        if (fabsf(g_scroll_vel) < 6.0f) g_scroll_vel = 0.0f;
        float target = pa_clampf(g_scroll, 0.0f, g_scroll_max);
        if (target != g_scroll) {
            /* Ran past an end: bleed the fling fast and spring back. */
            g_scroll_vel *= expf(-22.0f * dt);
            g_scroll += (target - g_scroll) * (1.0f - expf(-13.0f * dt));
            if (fabsf(target - g_scroll) < 0.25f) g_scroll = target;
        }
    }
}

static void update_home(float dt, const PA_Input *in) {
    const MetaState *M = meta_state();
    g_since_home += dt;
    g_clock_acc += dt;
    if (g_clock_acc >= 1.0f) { g_clock_acc = 0.0f; meta_refresh_clock(); }
    layout();

    if (g_modal != MODAL_NONE) update_modal(dt, in);
    else if (g_screen == SCREEN_HOME && g_reveal_t > REVEAL_DUR * 0.5f) {
        update_scroll(dt, in);
        if (in->key_pressed[PA_KEY_ENTER]) activate(BTN_FEATURED);
    }
    if (in->key_pressed[PA_KEY_ESC] && g_screen == SCREEN_HOME) {
        if (g_modal != MODAL_NONE) close_modal(); else g_quit = 1;
    }

    for (int i = 0; i < BTN_COUNT; i++) {
        int held = g_hit == i && in->down && !g_dragging && g_modal == MODAL_NONE;
        spring_step(&g_press[i], held ? 1.0f : 0.0f, 620.0f, 20.0f, dt);
    }

    /* Header counters roll toward the bank minus whatever is still flying. */
    float tg = (float)(M->gems - g_hold_gems), tc = (float)(M->coins - g_hold_coins);
    g_disp_gems += (tg - g_disp_gems) * (1.0f - expf(-10.0f * dt));
    g_disp_coins += (tc - g_disp_coins) * (1.0f - expf(-10.0f * dt));
    if (fabsf(tg - g_disp_gems) < 0.5f) g_disp_gems = tg;
    if (fabsf(tc - g_disp_coins) < 0.5f) g_disp_coins = tc;

    if (g_modal != MODAL_LEVELUP) {
        float tx = (float)(meta_total_xp() - g_hold_xp);
        float diff = tx - g_disp_xp;
        float step = (fabsf(diff) * 2.4f > 70.0f ? fabsf(diff) * 2.4f : 70.0f) * dt;
        if (fabsf(diff) <= step) g_disp_xp = tx; else g_disp_xp += diff > 0 ? step : -step;
        int lvl, into, need;
        meta_level_from_total((int)g_disp_xp, &lvl, &into, &need);
        if (lvl > g_shown_level) {
            g_shown_level = lvl;
            g_pill_kick[2].v += 12.0f;
            if (g_levelup_queue > 0 && g_modal == MODAL_NONE) {
                g_levelup_queue--;
                g_modal_level = lvl;
                open_modal(MODAL_LEVELUP);
                spawn_confetti((float)g_view_w * 0.5f, (float)g_view_h * 0.42f, 110);
            }
        }
    }

    if (g_xpfly_on) {
        float before = g_xpfly_t;
        g_xpfly_t += dt;
        if (before < 0.0f && g_xpfly_t >= 0.0f) pa_sfx("good");
        if (g_xpfly_t >= 1.05f) {
            g_hold_xp = 0;
            g_xpfly_on = 0;
            g_pill_kick[2].v += 10.0f;
            pa_sfx("coin");
        }
    }
    if (g_coin_burst > 0 && g_since_home > 0.75f) {
        reward_burst(1, (float)g_view_w * 0.5f, (float)g_view_h * 0.55f, g_coin_burst);
        g_coin_burst = 0;
    }

    /* The login reward greets the player once a day, after any run rewards. */
    if (g_screen == SCREEN_HOME && g_modal == MODAL_NONE && !M->streak_claimed &&
        g_streak_day_prompted != M->day && g_since_home > 0.8f && !g_xpfly_on &&
        g_levelup_queue == 0 && g_hold_xp == 0) {
        g_streak_day_prompted = M->day;
        open_modal(MODAL_STREAK);
    }
}

/* ----------------------------------------------------------------- frame -- */
void pa_app_update(float dt, const PA_Input *in) {
    g_time += dt;
    if (g_reveal_t < REVEAL_DUR) g_reveal_t += dt;
    update_fx(dt);

    if (g_screen == SCREEN_HOME) { update_home(dt, in); return; }
    if (g_screen == SCREEN_LAUNCH) {
        PA_Input none;
        memset(&none, 0, sizeof(none));
        update_home(dt, &none);
        g_launch_t += dt;
        if (g_launch_t >= LAUNCH_DUR) start_game(g_launch_idx);
        return;
    }
    if (g_screen == SCREEN_EXIT) {
        g_exit_t += dt;
        if (g_exit_t >= EXIT_DUR) g_pending_quit_home = 1;
        return;
    }

    if (in->key_pressed[PA_KEY_ESC]) {
        if (g_screen == SCREEN_PAUSE) g_screen = SCREEN_GAME; else pa_hub_pause();
        pa_sfx("select");
        return;
    }
    if (g_screen == SCREEN_PAUSE) {
        g_pause_anim = pa_clamp01(g_pause_anim + dt * 3.4f);
        if (in->pressed) {
            g_phit = -1;
            for (int i = 0; i < 3; i++) if (in_rect(pause_button(i), in->x, in->y)) g_phit = i;
        }
        for (int i = 0; i < 3; i++) {
            int held = g_phit == i && in->down && in_rect(pause_button(i), in->x, in->y);
            spring_step(&g_ppress[i], held ? 1.0f : 0.0f, 700.0f, 24.0f, dt);
        }
        if (in->tapped && g_phit >= 0 && in_rect(pause_button(g_phit), in->x, in->y)) {
            if (g_phit == 0) { g_screen = SCREEN_GAME; pa_sfx("select"); }
            else if (g_phit == 1) begin_exit();
            else {
                g_volume = g_volume > 0 ? 0 : 80;
                pa_audio_set_volume((float)g_volume / 100.0f);
                if (!pa_demo_mode()) { pa_save_set("volume", g_volume); pa_save_flush(); }
                g_ppress[2].x = 1.0f;
                pa_sfx("select");
            }
        }
        if (in->released) g_phit = -1;
        return;
    }
    if (!g_pause_hidden && in->pressed) {
        float dx = in->x - g_pause_x, dy = in->y - g_pause_y, rr = g_pause_r + 12.0f;
        if (dx * dx + dy * dy <= rr * rr) { pa_hub_pause(); return; }
    }

    /* A paused game keeps painting but stops updating, so the overlay sits over
       a live scene rather than a frozen buffer. */
    if (g_active && g_screen == SCREEN_GAME && g_active->update) g_active->update(dt, in);
}

static void render_game(PA_Canvas *c) {
    g_pause_x = (float)c->w - 36.0f; g_pause_y = 38.0f; g_pause_r = 22.0f;
    g_pause_hidden = 0;
    if (g_active && g_active->render) g_active->render(c);
    pa_clip_reset(c);
}

void pa_app_render(PA_Canvas *c) {
    if (g_view_w != c->w || g_view_h != c->h) {
        g_view_w = c->w;
        g_view_h = c->h;
        layout();
    }
    if (g_pending_quit_home) { g_pending_quit_home = 0; go_home(); }
    g_clip_n = 0;

    switch (g_screen) {
    case SCREEN_HOME:
        draw_home(c);
        break;
    case SCREEN_LAUNCH:
        draw_home(c);
        draw_launch(c);
        break;
    case SCREEN_EXIT:
        render_game(c);
        draw_exit(c);
        break;
    default:
        render_game(c);
        if (g_screen == SCREEN_PAUSE) draw_pause_sheet(c);
        else if (!g_pause_hidden && g_reveal_t >= REVEAL_DUR) draw_pause_glyph(c);
        break;
    }
    draw_reveal(c);
}
