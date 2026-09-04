/* ===========================================================================
   POCKET ARCADE - software rasterizer
   Everything the engine draws lands here. The core is one scanline polygon
   filler with analytic coverage antialiasing; circles, rounded rectangles,
   strokes and text are all flattened to polygons and pushed through it, so
   there is exactly one piece of code that has to be correct about edges.
   =========================================================================== */
#include "pa.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ math -- */
float pa_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float pa_clamp01(float v) { return pa_clampf(v, 0.0f, 1.0f); }
float pa_lerpf(float a, float b, float t) { return a + (b - a) * t; }
float pa_smooth(float t) { return t * t * (3.0f - 2.0f * t); }

float pa_approach(float current, float target, float rate, float dt) {
    return current + (target - current) * (1.0f - expf(-rate * dt));
}

float pa_wrapf(float v, float m) {
    float r = fmodf(v, m);
    return r < 0.0f ? r + m : r;
}

void pa_rng_seed(PA_Rng *r, uint32_t seed) { r->state = seed ? seed : 1u; }

float pa_rng_next(PA_Rng *r) {
    r->state += 0x6D2B79F5u;
    uint32_t t = r->state;
    t = (t ^ (t >> 15)) * (1u | t);
    t ^= t + (t ^ (t >> 7)) * (61u | t);
    return (float)((t ^ (t >> 14)) >> 8) / 16777216.0f;
}

float pa_rng_range(PA_Rng *r, float lo, float hi) { return lo + pa_rng_next(r) * (hi - lo); }
int   pa_rng_int(PA_Rng *r, int lo, int hi) {
    int span = hi - lo + 1;
    if (span <= 0) return lo;
    int v = lo + (int)(pa_rng_next(r) * (float)span);
    return v > hi ? hi : v;
}
int pa_rng_chance(PA_Rng *r, float p) { return pa_rng_next(r) < p; }

/* ---------------------------------------------------------------- colour -- */
PA_Color pa_hex(uint32_t rgb) { return 0xFF000000u | (rgb & 0x00FFFFFFu); }

PA_Color pa_mix(PA_Color a, PA_Color b, float t) {
    t = pa_clamp01(t);
    /*
     * Both channels are pulled out as int before subtracting. The extraction
     * macros yield unsigned, so writing the difference as `PA_R(b) - PA_R(a)`
     * promotes the whole expression to unsigned: any darkening step wrapped to
     * about four billion, and the float-to-int conversion of the result is
     * undefined. It happened to land somewhere harmless under one compiler and
     * produced rainbow garbage under another - every gradient in the engine was
     * a coin flip.
     */
    int ar = (int)PA_R(a), ag = (int)PA_G(a), ab = (int)PA_B(a), aa = (int)PA_A(a);
    int br = (int)PA_R(b), bg = (int)PA_G(b), bb = (int)PA_B(b), ba = (int)PA_A(b);

    int r  = (int)((float)ar + (float)(br - ar) * t + 0.5f);
    int g  = (int)((float)ag + (float)(bg - ag) * t + 0.5f);
    int bl = (int)((float)ab + (float)(bb - ab) * t + 0.5f);
    int al = (int)((float)aa + (float)(ba - aa) * t + 0.5f);

    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (bl < 0) bl = 0; if (bl > 255) bl = 255;
    if (al < 0) al = 0; if (al > 255) al = 255;

    return PA_RGBA(r, g, bl, al);
}

PA_Color pa_shade(PA_Color c, float amount) {
    PA_Color target = amount < 0.0f ? PA_RGBA(0, 0, 0, PA_A(c)) : PA_RGBA(255, 255, 255, PA_A(c));
    return pa_mix(c, target, fabsf(amount));
}

PA_Color pa_alpha(PA_Color c, float a) {
    int al = (int)(pa_clamp01(a) * 255.0f + 0.5f);
    return (c & 0x00FFFFFFu) | ((uint32_t)al << 24);
}

static float hue_channel(float p, float q, float t) {
    t = pa_wrapf(t, 1.0f);
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f)        return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

PA_Color pa_hsl(float h, float s, float l) {
    h = pa_wrapf(h, 1.0f);
    float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    int r = (int)(hue_channel(p, q, h + 1.0f / 3.0f) * 255.0f + 0.5f);
    int g = (int)(hue_channel(p, q, h) * 255.0f + 0.5f);
    int b = (int)(hue_channel(p, q, h - 1.0f / 3.0f) * 255.0f + 0.5f);
    return PA_RGB(r, g, b);
}

/* ---------------------------------------------------------------- canvas -- */
int pa_canvas_init(PA_Canvas *c, int w, int h) {
    c->px = NULL;
    c->w = c->h = 0;
    return pa_canvas_resize(c, w, h);
}

void pa_canvas_free(PA_Canvas *c) {
    free(c->px);
    c->px = NULL;
    c->w = c->h = 0;
}

int pa_canvas_resize(PA_Canvas *c, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (c->px && c->w == w && c->h == h) return 1;
    uint32_t *next = (uint32_t *)realloc(c->px, (size_t)w * (size_t)h * sizeof(uint32_t));
    if (!next) return 0;
    c->px = next;
    c->w = w;
    c->h = h;
    pa_clip_reset(c);
    return 1;
}

void pa_clip_reset(PA_Canvas *c) {
    c->clip_x0 = 0; c->clip_y0 = 0;
    c->clip_x1 = c->w; c->clip_y1 = c->h;
}

void pa_clip_rect(PA_Canvas *c, int x, int y, int w, int h) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > c->w) x1 = c->w;
    int y1 = y + h; if (y1 > c->h) y1 = c->h;
    c->clip_x0 = x0; c->clip_y0 = y0;
    c->clip_x1 = x1 > x0 ? x1 : x0;
    c->clip_y1 = y1 > y0 ? y1 : y0;
}

void pa_clear(PA_Canvas *c, PA_Color colour) {
    uint32_t v = colour & 0x00FFFFFFu;
    size_t n = (size_t)c->w * (size_t)c->h;
    for (size_t i = 0; i < n; i++) c->px[i] = v;
}

/* ------------------------------------------------------------------ blend -- */
/* Source-over with an 8-bit coverage term folded into the source alpha. */
static inline void blend_px(uint32_t *dst, PA_Color src, int coverage) {
    int a = (int)PA_A(src) * coverage / 255;
    if (a <= 0) return;
    if (a >= 255) { *dst = src & 0x00FFFFFFu; return; }
    uint32_t d = *dst;
    int inv = 255 - a;
    int r = ((int)PA_R(src) * a + (int)((d >> 16) & 0xFF) * inv) / 255;
    int g = ((int)PA_G(src) * a + (int)((d >> 8) & 0xFF) * inv) / 255;
    int b = ((int)PA_B(src) * a + (int)(d & 0xFF) * inv) / 255;
    *dst = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* ---------------------------------------------------------------- paints -- */
PA_Paint pa_flat(PA_Color c) {
    PA_Paint p;
    memset(&p, 0, sizeof(p));
    p.kind = PA_PAINT_FLAT;
    p.flat = c;
    return p;
}

PA_Paint pa_linear(float x0, float y0, float x1, float y1) {
    PA_Paint p;
    memset(&p, 0, sizeof(p));
    p.kind = PA_PAINT_LINEAR;
    p.x0 = x0; p.y0 = y0; p.x1 = x1; p.y1 = y1;
    return p;
}

PA_Paint pa_radial(float cx, float cy, float r0, float r1) {
    PA_Paint p;
    memset(&p, 0, sizeof(p));
    p.kind = PA_PAINT_RADIAL;
    p.x0 = cx; p.y0 = cy;
    p.r0 = r0; p.r1 = r1 > r0 ? r1 : r0 + 0.001f;
    return p;
}

void pa_stop(PA_Paint *p, float pos, PA_Color c) {
    if (p->stop_count >= PA_MAX_STOPS) return;
    p->stop_pos[p->stop_count] = pa_clamp01(pos);
    p->stop_col[p->stop_count] = c;
    p->stop_count++;
}

static PA_Color paint_at(const PA_Paint *p, float px, float py) {
    if (p->kind == PA_PAINT_FLAT || p->stop_count == 0) return p->flat;

    float t;
    if (p->kind == PA_PAINT_LINEAR) {
        float dx = p->x1 - p->x0, dy = p->y1 - p->y0;
        float len2 = dx * dx + dy * dy;
        t = len2 <= 0.0f ? 0.0f : ((px - p->x0) * dx + (py - p->y0) * dy) / len2;
    } else {
        float dx = px - p->x0, dy = py - p->y0;
        float d = sqrtf(dx * dx + dy * dy);
        t = (d - p->r0) / (p->r1 - p->r0);
    }
    t = pa_clamp01(t);

    if (t <= p->stop_pos[0]) return p->stop_col[0];
    for (int i = 1; i < p->stop_count; i++) {
        if (t <= p->stop_pos[i]) {
            float span = p->stop_pos[i] - p->stop_pos[i - 1];
            float local = span <= 0.0f ? 0.0f : (t - p->stop_pos[i - 1]) / span;
            return pa_mix(p->stop_col[i - 1], p->stop_col[i], local);
        }
    }
    return p->stop_col[p->stop_count - 1];
}

/* ------------------------------------------------------- polygon filling -- */
/*
 * Scanline fill with the non-zero winding rule. Each pixel row is sampled at
 * PA_SUBS evenly spaced sub-scanlines; every sub-scanline contributes exact
 * horizontal coverage to a per-row accumulation buffer. That gives clean edges
 * in both axes for the cost of a few crossings per row - far cheaper than
 * supersampling the whole framebuffer, and it means the extruded-box art in the
 * games does not crawl when the camera moves a fraction of a pixel.
 */
#define PA_SUBS 5
#define PA_MAX_EDGES 4096
#define PA_MAX_XS 128

typedef struct { float x0, y0, x1, y1; int dir; } Edge;
typedef struct { float x; int dir; } Crossing;

/*
 * Both scratch buffers are file static rather than automatic. The edge list
 * alone is 80KB, and a frame that large on the stack relies on Windows' guard
 * page probing to grow the stack correctly on every call - which it does not
 * reliably do from a deep call chain, and the failure mode is silent garbage in
 * whole scanlines rather than a crash. Rendering is single threaded (the audio
 * mixer runs on its own thread and never draws), so sharing them is safe.
 */
static float g_cov[8192];   /* coverage accumulator, one row of the canvas */
static Edge  g_edges[PA_MAX_EDGES];
static Crossing g_xs[PA_MAX_XS];

static int build_edges(const PA_Vec2 *pts, int n, Edge *edges, float *ymin, float *ymax) {
    int count = 0;
    *ymin = 1e30f;
    *ymax = -1e30f;
    for (int i = 0; i < n && count < PA_MAX_EDGES; i++) {
        PA_Vec2 a = pts[i];
        PA_Vec2 b = pts[(i + 1) % n];
        if (a.y == b.y) continue;                 /* horizontal edges add nothing */
        Edge *e = &edges[count++];
        if (a.y < b.y) { e->x0 = a.x; e->y0 = a.y; e->x1 = b.x; e->y1 = b.y; e->dir = 1; }
        else           { e->x0 = b.x; e->y0 = b.y; e->x1 = a.x; e->y1 = a.y; e->dir = -1; }
        if (e->y0 < *ymin) *ymin = e->y0;
        if (e->y1 > *ymax) *ymax = e->y1;
    }
    return count;
}

static int crossing_cmp(const void *a, const void *b) {
    float d = ((const Crossing *)a)->x - ((const Crossing *)b)->x;
    return d < 0.0f ? -1 : (d > 0.0f ? 1 : 0);
}

/** Adds `amount` of coverage to the half-open span [x0, x1) on the row buffer. */
static void add_span(float *cov, int lo, int hi, float x0, float x1, float amount) {
    if (x1 <= x0) return;
    if (x0 < (float)lo) x0 = (float)lo;
    if (x1 > (float)hi) x1 = (float)hi;
    if (x1 <= x0) return;

    int ix0 = (int)floorf(x0);
    int ix1 = (int)floorf(x1);

    if (ix0 == ix1) {
        cov[ix0] += (x1 - x0) * amount;
        return;
    }
    cov[ix0] += ((float)(ix0 + 1) - x0) * amount;
    for (int x = ix0 + 1; x < ix1; x++) cov[x] += amount;
    if (ix1 < hi) cov[ix1] += (x1 - (float)ix1) * amount;
}

static void fill_path(PA_Canvas *c, const PA_Vec2 *pts, int n, const PA_Paint *paint) {
    if (n < 3) return;

    float ymin, ymax;
    int ecount = build_edges(pts, n, g_edges, &ymin, &ymax);
    if (ecount == 0) return;

    int y0 = (int)floorf(ymin); if (y0 < c->clip_y0) y0 = c->clip_y0;
    int y1 = (int)ceilf(ymax);  if (y1 > c->clip_y1) y1 = c->clip_y1;
    if (y1 <= y0) return;

    int lo = c->clip_x0, hi = c->clip_x1;
    if (hi <= lo || hi > (int)(sizeof(g_cov) / sizeof(g_cov[0]))) return;

    const float sub_weight = 1.0f / (float)PA_SUBS;
    Crossing *xs = g_xs;

    for (int y = y0; y < y1; y++) {
        memset(g_cov + lo, 0, (size_t)(hi - lo) * sizeof(float));
        int touched = 0;

        for (int s = 0; s < PA_SUBS; s++) {
            float sy = (float)y + ((float)s + 0.5f) * sub_weight;
            int xn = 0;

            for (int i = 0; i < ecount && xn < PA_MAX_XS; i++) {
                const Edge *e = &g_edges[i];
                if (sy < e->y0 || sy >= e->y1) continue;
                float t = (sy - e->y0) / (e->y1 - e->y0);
                xs[xn].x = e->x0 + (e->x1 - e->x0) * t;
                xs[xn].dir = e->dir;
                xn++;
            }
            if (xn < 2) continue;
            qsort(xs, (size_t)xn, sizeof(Crossing), crossing_cmp);

            /* Non-zero winding: a span is inside wherever the running total of
               edge directions is not zero. */
            int winding = 0;
            for (int i = 0; i < xn - 1; i++) {
                winding += xs[i].dir;
                if (winding != 0) {
                    add_span(g_cov, lo, hi, xs[i].x, xs[i + 1].x, sub_weight);
                    touched = 1;
                }
            }
        }

        if (!touched) continue;
        uint32_t *row = c->px + (size_t)y * (size_t)c->w;
        float py = (float)y + 0.5f;
        for (int x = lo; x < hi; x++) {
            float a = g_cov[x];
            if (a <= 0.002f) continue;
            if (a > 1.0f) a = 1.0f;
            PA_Color src = (paint->kind == PA_PAINT_FLAT)
                ? paint->flat
                : paint_at(paint, (float)x + 0.5f, py);
            blend_px(&row[x], src, (int)(a * 255.0f + 0.5f));
        }
    }
}

void pa_fill_poly(PA_Canvas *c, const PA_Vec2 *pts, int n, PA_Color col) {
    PA_Paint p = pa_flat(col);
    fill_path(c, pts, n, &p);
}

void pa_fill_poly_paint(PA_Canvas *c, const PA_Vec2 *pts, int n, const PA_Paint *p) {
    fill_path(c, pts, n, p);
}

/* ------------------------------------------------------------ rectangles -- */
void pa_fill_rect_paint(PA_Canvas *c, float x, float y, float w, float h, const PA_Paint *p) {
    if (w <= 0.0f || h <= 0.0f) return;
    PA_Vec2 q[4] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } };
    fill_path(c, q, 4, p);
}

void pa_fill_rect(PA_Canvas *c, float x, float y, float w, float h, PA_Color col) {
    /* Axis-aligned opaque rectangles are the single most common call in the
       whole engine, so they skip the scanline machinery entirely. */
    if (w <= 0.0f || h <= 0.0f) return;
    if (PA_A(col) == 255 && x == floorf(x) && y == floorf(y) &&
        w == floorf(w) && h == floorf(h)) {
        int x0 = (int)x, y0 = (int)y, x1 = (int)(x + w), y1 = (int)(y + h);
        if (x0 < c->clip_x0) x0 = c->clip_x0;
        if (y0 < c->clip_y0) y0 = c->clip_y0;
        if (x1 > c->clip_x1) x1 = c->clip_x1;
        if (y1 > c->clip_y1) y1 = c->clip_y1;
        uint32_t v = col & 0x00FFFFFFu;
        for (int yy = y0; yy < y1; yy++) {
            uint32_t *row = c->px + (size_t)yy * (size_t)c->w;
            for (int xx = x0; xx < x1; xx++) row[xx] = v;
        }
        return;
    }
    PA_Paint p = pa_flat(col);
    pa_fill_rect_paint(c, x, y, w, h, &p);
}

/* --------------------------------------------------------------- flatten -- */
/* Curve segment counts scale with radius: a 4px corner does not need 24 points,
   and a 400px circle looks faceted with 24. */
static int arc_segments(float r) {
    int n = (int)(r * 0.7f) + 6;
    return n > 96 ? 96 : n;
}

static int emit_arc(PA_Vec2 *out, int at, float cx, float cy, float rx, float ry,
                    float a0, float a1) {
    int segs = arc_segments((rx > ry ? rx : ry) * fabsf(a1 - a0) / PA_PI);
    for (int i = 0; i <= segs; i++) {
        float t = a0 + (a1 - a0) * ((float)i / (float)segs);
        out[at].x = cx + cosf(t) * rx;
        out[at].y = cy + sinf(t) * ry;
        at++;
    }
    return at;
}

void pa_fill_ellipse_paint(PA_Canvas *c, float cx, float cy, float rx, float ry,
                           const PA_Paint *p) {
    if (rx <= 0.0f || ry <= 0.0f) return;
    PA_Vec2 pts[128];
    int segs = arc_segments(rx > ry ? rx : ry);
    if (segs > 127) segs = 127;
    for (int i = 0; i < segs; i++) {
        float t = PA_TAU * (float)i / (float)segs;
        pts[i].x = cx + cosf(t) * rx;
        pts[i].y = cy + sinf(t) * ry;
    }
    fill_path(c, pts, segs, p);
}

void pa_fill_ellipse(PA_Canvas *c, float cx, float cy, float rx, float ry, PA_Color col) {
    PA_Paint p = pa_flat(col);
    pa_fill_ellipse_paint(c, cx, cy, rx, ry, &p);
}

void pa_fill_circle(PA_Canvas *c, float cx, float cy, float r, PA_Color col) {
    pa_fill_ellipse(c, cx, cy, r, r, col);
}

void pa_round_rect_paint(PA_Canvas *c, float x, float y, float w, float h, float r,
                         const PA_Paint *p) {
    if (w <= 0.0f || h <= 0.0f) return;
    float m = (w < h ? w : h) * 0.5f;
    if (r > m) r = m;
    if (r <= 0.5f) { pa_fill_rect_paint(c, x, y, w, h, p); return; }

    PA_Vec2 pts[256];
    int n = 0;
    n = emit_arc(pts, n, x + w - r, y + h - r, r, r, 0.0f, PA_PI * 0.5f);
    n = emit_arc(pts, n, x + r,     y + h - r, r, r, PA_PI * 0.5f, PA_PI);
    n = emit_arc(pts, n, x + r,     y + r,     r, r, PA_PI, PA_PI * 1.5f);
    n = emit_arc(pts, n, x + w - r, y + r,     r, r, PA_PI * 1.5f, PA_TAU);
    fill_path(c, pts, n, p);
}

void pa_round_rect(PA_Canvas *c, float x, float y, float w, float h, float r, PA_Color col) {
    PA_Paint p = pa_flat(col);
    pa_round_rect_paint(c, x, y, w, h, r, &p);
}

/* --------------------------------------------------------------- strokes -- */
/*
 * A stroke is drawn as one quad per segment plus a round join disc at each
 * interior vertex. Building a single offset outline would be tidier but needs
 * miter-limit handling at every reflex corner; overlapping quads composite
 * identically for an opaque stroke and never produce a spike.
 */
void pa_stroke_poly(PA_Canvas *c, const PA_Vec2 *pts, int n, int closed,
                    float width, PA_Color col) {
    if (n < 2 || width <= 0.0f) return;
    float hw = width * 0.5f;
    int segments = closed ? n : n - 1;

    for (int i = 0; i < segments; i++) {
        PA_Vec2 a = pts[i];
        PA_Vec2 b = pts[(i + 1) % n];
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.0001f) continue;
        float nx = -dy / len * hw, ny = dx / len * hw;
        PA_Vec2 quad[4] = {
            { a.x + nx, a.y + ny }, { b.x + nx, b.y + ny },
            { b.x - nx, b.y - ny }, { a.x - nx, a.y - ny }
        };
        pa_fill_poly(c, quad, 4, col);
    }

    if (width > 2.0f) {
        int first = closed ? 0 : 1;
        int last = closed ? n : n - 1;
        for (int i = first; i < last; i++) pa_fill_circle(c, pts[i].x, pts[i].y, hw, col);
    }
}

void pa_line(PA_Canvas *c, float x0, float y0, float x1, float y1, float width, PA_Color col) {
    PA_Vec2 pts[2] = { { x0, y0 }, { x1, y1 } };
    pa_stroke_poly(c, pts, 2, 0, width, col);
}

void pa_stroke_rect(PA_Canvas *c, float x, float y, float w, float h, float width, PA_Color col) {
    PA_Vec2 pts[4] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } };
    pa_stroke_poly(c, pts, 4, 1, width, col);
}

void pa_stroke_circle(PA_Canvas *c, float cx, float cy, float r, float width, PA_Color col) {
    PA_Vec2 pts[128];
    int segs = arc_segments(r);
    if (segs > 127) segs = 127;
    for (int i = 0; i < segs; i++) {
        float t = PA_TAU * (float)i / (float)segs;
        pts[i].x = cx + cosf(t) * r;
        pts[i].y = cy + sinf(t) * r;
    }
    pa_stroke_poly(c, pts, segs, 1, width, col);
}
