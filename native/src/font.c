/* ===========================================================================
   POCKET ARCADE - vector text
   Two embedded TrueType faces (SIL OFL, see third_party/), read with
   stb_truetype. Each glyph's quadratic outline is flattened once into polygons
   in cap-height units and cached; a draw thins, scales and places them, so
   text is crisp at any size and costs no atlas.

   A draw renders two coverage masks over the text's box: the glyphs, and the
   glyphs dilated by the outline width (the union of a quad per segment and a
   round join per vertex, which needs no offset-curve clipping and cannot
   self-intersect into holes). The masks are then composited once - shadow,
   then fill over outline as a group - with the same paint maths as the
   rasterizer, so flat colours, linear and radial gradients all work, and a
   fading banner fades as a whole.

   The masks use an exact-area accumulation rasterizer (non-zero, |winding|
   saturated) rather than pa_fill_poly. The scanline filler tests every edge
   on every sub-scanline and composites each call separately: a union of
   hundreds of outline pieces would show seams wherever they abut, and even a
   plain glyph fill measured three to four times the cost. Coverage is exact
   area, so edges match the rest of the art's antialiasing.
   =========================================================================== */
#include "pa.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* stb_truetype is third-party code compiled into this file; its warnings are
   not ours to fix, so they are silenced for the include alone. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "third_party/font_display.h"   /* g_font_display: Fredoka Bold    */
#include "third_party/font_ui.h"        /* g_font_ui: Nunito ExtraBold     */

/* --------------------------------------------------------------- glyphs -- */
/* Flattening tolerance in cap-height units. 0.004 keeps the chord error under
   half a pixel up to a 120 px cap, and costs a dozen points per bowl. */
#define FLAT_TOL 0.004f

/* The stroke font's tracking padded skeletal glyphs apart. A filled face
   carries its own sidebearings, so the legacy calls apply only part of it;
   that also keeps line widths close to what the old metrics laid out. */
#define LEGACY_TRACKING 0.5f

typedef struct {
    int      state;            /* 0 not built, 1 built */
    int      index;            /* glyph index in the font, 0 = missing */
    float    adv;              /* advance, cap units */
    float    x0, y0, x1, y1;   /* ink box, cap units, y down from the cap top */
    int      npts, ncont;
    PA_Vec2 *pts;              /* every contour, back to back */
    int     *ends;             /* exclusive end of each contour in pts */
} Glyph;

#define EXTRA_SLOTS 8

typedef struct {
    const unsigned char *data;
    stbtt_fontinfo info;
    int    state;              /* 0 untried, 1 ready, -1 unusable */
    float  unit;               /* 1 / cap height in font units */
    float  cap;                /* cap height in font units */
    Glyph  low[256];           /* code points below 256 */
    Glyph  extra[EXTRA_SLOTS];
    uint32_t extra_cp[EXTRA_SLOTS];
    int    extra_n;
} Face;

static Face g_faces[2] = {
    { g_font_ui, { 0 }, 0, 0, 0, { { 0 } }, { { 0 } }, { 0 }, 0 },
    { g_font_display, { 0 }, 0, 0, 0, { { 0 } }, { { 0 } }, { 0 }, 0 },
};

static Face *face_get(PA_Face which) {
    Face *f = &g_faces[which == PA_FACE_DISPLAY ? 1 : 0];
    if (f->state == 0) {
        f->state = -1;
        int off = stbtt_GetFontOffsetForIndex(f->data, 0);
        if (off >= 0 && stbtt_InitFont(&f->info, f->data, off)) {
            int x0, y0, x1, y1;
            if (stbtt_GetCodepointBox(&f->info, 'H', &x0, &y0, &x1, &y1) && y1 > 0) {
                f->cap = (float)y1;
                f->unit = 1.0f / f->cap;
                f->state = 1;
            }
        }
    }
    return f->state == 1 ? f : NULL;
}

/* Growable point list used while flattening. */
typedef struct { PA_Vec2 *p; int n, cap, start; } PtBuf;   /* start: current contour */

static void pt_push(PtBuf *b, float x, float y) {
    if (b->n == b->cap) {
        int nc = b->cap ? b->cap * 2 : 128;
        PA_Vec2 *np = (PA_Vec2 *)realloc(b->p, (size_t)nc * sizeof(PA_Vec2));
        if (!np) return;
        b->p = np;
        b->cap = nc;
    }
    if (b->n > b->start) {              /* drop exact repeats within a contour */
        PA_Vec2 l = b->p[b->n - 1];
        if (fabsf(l.x - x) < 1e-6f && fabsf(l.y - y) < 1e-6f) return;
    }
    b->p[b->n].x = x;
    b->p[b->n].y = y;
    b->n++;
}

static int seg_count(float dd) {
    /* A quadratic's chord error with n even steps is |p0 - 2c + p1| / (8 n^2). */
    int n = (int)ceilf(sqrtf(dd / (8.0f * FLAT_TOL)));
    return n < 1 ? 1 : (n > 24 ? 24 : n);
}

static void glyph_build(Face *f, Glyph *g, uint32_t cp) {
    g->state = 1;
    g->index = stbtt_FindGlyphIndex(&f->info, (int)cp);
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&f->info, g->index, &adv, &lsb);
    g->adv = (float)adv * f->unit;
    if (g->index == 0) {                /* not in the subset: a blank space */
        int sp = stbtt_FindGlyphIndex(&f->info, ' ');
        stbtt_GetGlyphHMetrics(&f->info, sp, &adv, &lsb);
        g->adv = (float)adv * f->unit;
        return;
    }

    stbtt_vertex *v = NULL;
    int nv = stbtt_GetGlyphShape(&f->info, g->index, &v);
    PtBuf b = { NULL, 0, 0, 0 };
    int *ends = NULL, ne = 0;
    const float u = f->unit, cap = f->cap;
#define TX(px) ((float)(px) * u)
#define TY(py) ((cap - (float)(py)) * u)
    float cx = 0.0f, cy = 0.0f;         /* current point, cap units */
    for (int i = 0; i < nv; i++) {
        const stbtt_vertex *s = &v[i];
        float x = TX(s->x), y = TY(s->y);
        if (s->type == STBTT_vmove) {
            if (b.n - b.start >= 3) {
                int *ne2 = (int *)realloc(ends, (size_t)(ne + 1) * sizeof(int));
                if (ne2) { ends = ne2; ends[ne++] = b.n; }
            } else {
                b.n = b.start;          /* a degenerate contour adds nothing */
            }
            b.start = b.n;
            pt_push(&b, x, y);
        } else if (s->type == STBTT_vline) {
            pt_push(&b, x, y);
        } else if (s->type == STBTT_vcurve) {
            float qx = TX(s->cx), qy = TY(s->cy);
            float ddx = cx - 2.0f * qx + x, ddy = cy - 2.0f * qy + y;
            int n = seg_count(sqrtf(ddx * ddx + ddy * ddy));
            for (int k = 1; k <= n; k++) {
                float t = (float)k / (float)n, mt = 1.0f - t;
                pt_push(&b, mt * mt * cx + 2.0f * mt * t * qx + t * t * x,
                            mt * mt * cy + 2.0f * mt * t * qy + t * t * y);
            }
        } else if (s->type == STBTT_vcubic) {
            float q1x = TX(s->cx), q1y = TY(s->cy), q2x = TX(s->cx1), q2y = TY(s->cy1);
            float d1x = cx - 2.0f * q1x + q2x, d1y = cy - 2.0f * q1y + q2y;
            float d2x = q1x - 2.0f * q2x + x, d2y = q1y - 2.0f * q2y + y;
            float dd = fmaxf(sqrtf(d1x * d1x + d1y * d1y), sqrtf(d2x * d2x + d2y * d2y)) * 1.5f;
            int n = seg_count(dd);
            for (int k = 1; k <= n; k++) {
                float t = (float)k / (float)n, mt = 1.0f - t;
                float a = mt * mt * mt, bb = 3.0f * mt * mt * t, c = 3.0f * mt * t * t, d = t * t * t;
                pt_push(&b, a * cx + bb * q1x + c * q2x + d * x, a * cy + bb * q1y + c * q2y + d * y);
            }
        }
        cx = x;
        cy = y;
    }
#undef TX
#undef TY
    if (b.n - b.start >= 3) {
        int *ne2 = (int *)realloc(ends, (size_t)(ne + 1) * sizeof(int));
        if (ne2) { ends = ne2; ends[ne++] = b.n; }
    } else {
        b.n = b.start;
    }
    stbtt_FreeShape(&f->info, v);

    /* Drop each contour's explicit closing point, and orient the glyph so its
       outer contours wind the same way as the stroke pieces below (negative
       shoelace area); holes then wind the other way and cancel. */
    int s0 = 0;
    for (int c = 0; c < ne; c++) {
        int e = ends[c];
        if (e - s0 > 3 && fabsf(b.p[e - 1].x - b.p[s0].x) < 1e-6f
                       && fabsf(b.p[e - 1].y - b.p[s0].y) < 1e-6f) {
            memmove(&b.p[e - 1], &b.p[e], (size_t)(b.n - e) * sizeof(PA_Vec2));
            b.n--;
            for (int k = c; k < ne; k++) ends[k]--;
        }
        s0 = ends[c];
    }
    float area = 0.0f;
    s0 = 0;
    for (int c = 0; c < ne; c++) {
        for (int i = s0; i < ends[c]; i++) {
            PA_Vec2 p = b.p[i], q = b.p[i + 1 < ends[c] ? i + 1 : s0];
            area += p.x * q.y - q.x * p.y;
        }
        s0 = ends[c];
    }
    if (area > 0.0f) {
        s0 = 0;
        for (int c = 0; c < ne; c++) {
            for (int i = s0, j = ends[c] - 1; i < j; i++, j--) {
                PA_Vec2 t = b.p[i]; b.p[i] = b.p[j]; b.p[j] = t;
            }
            s0 = ends[c];
        }
    }

    g->x0 = g->y0 = 1e30f;
    g->x1 = g->y1 = -1e30f;
    for (int i = 0; i < b.n; i++) {
        if (b.p[i].x < g->x0) g->x0 = b.p[i].x;
        if (b.p[i].x > g->x1) g->x1 = b.p[i].x;
        if (b.p[i].y < g->y0) g->y0 = b.p[i].y;
        if (b.p[i].y > g->y1) g->y1 = b.p[i].y;
    }
    g->pts = b.p;
    g->npts = b.n;
    g->ends = ends;
    g->ncont = ne;
    if (b.n == 0) { g->x0 = g->y0 = g->x1 = g->y1 = 0.0f; }
}

static Glyph *glyph_get(Face *f, uint32_t cp) {
    Glyph *g = NULL;
    if (cp < 256) {
        g = &f->low[cp];
    } else {
        for (int i = 0; i < f->extra_n; i++)
            if (f->extra_cp[i] == cp) { g = &f->extra[i]; break; }
        if (!g) {
            if (f->extra_n == EXTRA_SLOTS) return glyph_get(f, '?');
            f->extra_cp[f->extra_n] = cp;
            g = &f->extra[f->extra_n++];
        }
    }
    if (!g->state) {
        glyph_build(f, g, cp);
        /* A glyph the subset lacks shows as a question mark, not as nothing. */
        if (g->index == 0 && cp > ' ' && cp != '?') {
            Glyph *q = glyph_get(f, '?');
            if (q->index) {
                g->adv = q->adv;
                g->x0 = q->x0; g->y0 = q->y0; g->x1 = q->x1; g->y1 = q->y1;
                g->pts = q->pts; g->npts = q->npts; g->ends = q->ends; g->ncont = q->ncont;
                g->index = q->index;
            }
        }
    }
    return g;
}

/* ---------------------------------------------------------------- layout -- */
static uint32_t next_cp(const unsigned char **ps) {
    const unsigned char *s = *ps;
    uint32_t c = s[0];
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (c >= 0x80 && n > 0) {
        uint32_t v = c & (0x3Fu >> n);
        int ok = 1;
        for (int i = 1; i <= n; i++) {
            if ((s[i] & 0xC0) != 0x80) { ok = 0; break; }
            v = (v << 6) | (s[i] & 0x3Fu);
        }
        if (ok) { *ps = s + n + 1; return v; }
    }
    *ps = s + 1;                         /* ASCII, or a stray Latin-1 byte */
    return c;
}

#define MAX_RUN 256

typedef struct { Glyph *g; float x; } Placed;   /* x: pen offset, px */

/* Lays `text` out from pen 0. Returns the glyph count; *width the advance. */
static int layout(Face *f, const char *text, float size, float tracking, int caps,
                  Placed *out, float *width) {
    const unsigned char *p = (const unsigned char *)text;
    int n = 0;
    float pen = 0.0f;
    Glyph *prev = NULL;
    while (*p && n < MAX_RUN) {
        uint32_t cp = next_cp(&p);
        if (caps && cp >= 'a' && cp <= 'z') cp -= 32;
        Glyph *g = glyph_get(f, cp);
        if (prev) {
            pen += tracking;
            if (prev->index && g->index)
                pen += (float)stbtt_GetGlyphKernAdvance(&f->info, prev->index, g->index)
                       * f->unit * size;
        }
        out[n].g = g;
        out[n].x = pen;
        n++;
        pen += g->adv * size;
        prev = g;
    }
    *width = pen;
    return n;
}

static float align_offset(PA_Align a, float width) {
    if (a == PA_ALIGN_CENTER) return -width * 0.5f;
    if (a == PA_ALIGN_RIGHT) return -width;
    return 0.0f;
}

/* ----------------------------------------------------------------- masks -- */
/* Scratch for one draw, kept for the program and grown on demand. */
static float    *g_acc;          /* accumulation planes, (w + 2) x h each */
static uint8_t  *g_cov8;         /* fill and outline coverage, a byte each */
static size_t    g_plane_cap, g_acc_cap;
static uint8_t  *g_blur_mem;
static size_t    g_blur_cap;

static int scratch_reserve(size_t plane, size_t accn) {
    if (plane > g_plane_cap) {
        uint8_t *q = (uint8_t *)realloc(g_cov8, plane * 2);
        if (!q) return 0;
        g_cov8 = q;
        g_plane_cap = plane;
    }
    if (accn > g_acc_cap) {
        float *a = (float *)realloc(g_acc, accn * sizeof(float));
        if (!a) return 0;
        g_acc = a;
        g_acc_cap = accn;
    }
    return 1;
}

/* ------------------------------------------------------------- dilation -- */
/*
 * The outline is the glyph dilated by `r`: the glyph itself, plus per
 * flattened segment a quad of half-width r, plus at each vertex the wedge of a
 * round join on the side that turns away (the other side is already covered
 * by the overlapping quads). That is a few hundred small overlapping polygons
 * per glyph, and they must union with no seams where they abut.
 *
 * The scanline filler composites each polygon separately, so abutting pieces
 * would leave a faint line of partial coverage at every join, and its per-call
 * row setup would dominate the cost. The dilation therefore goes through a
 * signed-area accumulation buffer instead: every edge deposits its exact area
 * contribution, each row is integrated once, and coverage is
 * min(1, |winding|). All pieces wind the same way as the glyph's outer
 * contours, so overlaps saturate and abutting edges sum to exactly 1 - no
 * seams - and the cost follows edge length rather than edges x rows.
 */
typedef struct { float *a; int w, h, stride; } Acc;   /* stride = w + 2 */

static void acc_line(Acc *m, float x0, float y0, float x1, float y1) {
    if (fabsf(y1 - y0) < 1e-6f) return;
    float dir = 1.0f;
    if (y0 > y1) {
        float t;
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
        dir = -1.0f;
    }
    if (y1 <= 0.0f || y0 >= (float)m->h) return;
    float dxdy = (x1 - x0) / (y1 - y0);
    /* y1 > 0 and, after the clamps, x >= 0 here: integer casts are floor,
       which keeps libm's floorf out of the hottest loop in the file. */
    int ys = y0 <= 0.0f ? 0 : (int)y0;
    int ye = (int)y1;
    if ((float)ye < y1) ye++;
    if (ye > m->h) ye = m->h;
    const float xmax = (float)m->w;
    for (int yy = ys; yy < ye; yy++) {
        float top = (float)yy > y0 ? (float)yy : y0;
        float bot = (float)(yy + 1) < y1 ? (float)(yy + 1) : y1;
        float dy = bot - top;
        if (dy <= 0.0f) continue;
        float xs = x0 + (top - y0) * dxdy;
        float xe = xs + dxdy * dy;
        float d = dy * dir;
        float xa = xs < xe ? xs : xe, xb = xs < xe ? xe : xs;
        /* Anything left of the buffer lands in column 0 and anything right
           of it in the spare column: the row integral stays right. */
        if (xa < 0.0f) xa = 0.0f;
        if (xb < 0.0f) xb = 0.0f;
        if (xa > xmax) xa = xmax;
        if (xb > xmax) xb = xmax;
        float *row = m->a + (size_t)yy * (size_t)m->stride;
        int ia = (int)xa;
        float xaf = (float)ia;
        int ib = (int)xb;
        if ((float)ib < xb) ib++;
        if (ib <= ia + 1) {
            /* within one column: split at the mean x */
            float xm = 0.5f * (xa + xb) - xaf;
            row[ia] += d - d * xm;
            row[ia + 1] += d * xm;
        } else {
            float sinv = 1.0f / (xb - xa);
            float fa = xa - xaf;
            float a0 = 0.5f * sinv * (1.0f - fa) * (1.0f - fa);
            float fb = xb - (float)ib + 1.0f;
            float am = 0.5f * sinv * fb * fb;
            row[ia] += d * a0;
            if (ib == ia + 2) {
                row[ia + 1] += d * (1.0f - a0 - am);
            } else {
                float a1 = sinv * (1.5f - fa);
                row[ia + 1] += d * (a1 - a0);
                for (int xi = ia + 2; xi < ib - 1; xi++) row[xi] += d * sinv;
                float a2 = a1 + (float)(ib - ia - 3) * sinv;
                row[ib - 1] += d * (1.0f - a2 - am);
            }
            row[ib] += d * am;
        }
    }
}

static void acc_poly(Acc *m, const PA_Vec2 *p, int n) {
    for (int i = 0; i < n; i++) {
        const PA_Vec2 *a = &p[i], *b = &p[i + 1 < n ? i + 1 : 0];
        acc_line(m, a->x, a->y, b->x, b->y);
    }
}

/* The cache is flattened finely enough for a title-sized glyph; at label
   sizes most of those points are closer together than a fifth of a pixel.
   Each contour is thinned for the size it is drawn at (greedy chord test: a
   run of points collapses to one segment while none strays more than PLACE_TOL
   pixels from it) and placed in mask space. */
#define PLACE_TOL 0.2f

static PA_Vec2 *g_tmp;
static int      g_tmp_cap;

static int place_contour(const PA_Vec2 *P, int cnt, float ox, float oy, float s) {
    if (cnt > g_tmp_cap) {
        int nc = g_tmp_cap ? g_tmp_cap : 256;
        while (nc < cnt) nc *= 2;
        PA_Vec2 *np = (PA_Vec2 *)realloc(g_tmp, (size_t)nc * sizeof(PA_Vec2));
        if (!np) return 0;
        g_tmp = np;
        g_tmp_cap = nc;
    }
    const float tol = PLACE_TOL / s;          /* in cap units */
    int n = 0, k = 0;                           /* k: last kept index */
    g_tmp[n].x = ox + P[0].x * s;
    g_tmp[n].y = oy + P[0].y * s;
    n++;
    for (int j = 1; j < cnt; j++) {
        /* can the chord k -> j+1 (wrapping to the start) stand in for j? */
        const PA_Vec2 a = P[k], b = P[j + 1 < cnt ? j + 1 : 0];
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = sqrtf(dx * dx + dy * dy);
        int keep = len < 1e-9f;
        for (int i = k + 1; i <= j && !keep; i++) {
            float d = fabsf(dx * (P[i].y - a.y) - dy * (P[i].x - a.x));
            if (d > tol * len) keep = 1;
        }
        if (keep) {
            g_tmp[n].x = ox + P[j].x * s;
            g_tmp[n].y = oy + P[j].y * s;
            n++;
            k = j;
        }
    }
    return n;
}

/* Every contour of a glyph, scaled and placed. */
static void fill_glyph(Acc *m, const Glyph *g, float ox, float oy, float s) {
    int s0 = 0;
    for (int c = 0; c < g->ncont; c++) {
        int n = place_contour(g->pts + s0, g->ends[c] - s0, ox, oy, s);
        if (n >= 3) acc_poly(m, g_tmp, n);
        s0 = g->ends[c];
    }
}

static void dilate_glyph(Acc *m, const Glyph *g, float ox, float oy, float s, float r) {
    if (g->ncont == 0) return;
    fill_glyph(m, g, ox, oy, s);      /* the interior is part of the dilation */
    /* join arc step: keep the chord within a quarter pixel of the circle */
    float k = 1.0f - 0.25f / r;
    float step = k > -1.0f ? 2.0f * acosf(k) : 1.0f;
    if (step > 0.9f) step = 0.9f;
    if (step < 0.15f) step = 0.15f;

    int s0 = 0;
    for (int c = 0; c < g->ncont; c++) {
        int cnt = place_contour(g->pts + s0, g->ends[c] - s0, ox, oy, s);
        s0 = g->ends[c];
        if (cnt < 3) continue;
        const PA_Vec2 *P = g_tmp;
        for (int i = 0; i < cnt; i++) {
            PA_Vec2 a = P[i], b = P[(i + 1) % cnt], nx = P[(i + 2) % cnt];
            float dx = b.x - a.x, dy = b.y - a.y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 1e-4f) continue;
            dx /= len; dy /= len;
            float ex = nx.x - b.x, ey = nx.y - b.y;
            float l2 = sqrtf(ex * ex + ey * ey);
            if (l2 < 1e-4f) { ex = dx; ey = dy; } else { ex /= l2; ey /= l2; }
            float nX = -dy * r, nY = dx * r;            /* this segment's normal */
            float cross = dx * ey - dy * ex, dot = dx * ex + dy * ey;
            float th = atan2f(cross, dot);               /* turn into the next */
            int steps = fabsf(th) < 1e-3f ? 0 : (int)ceilf(fabsf(th) / step);
            if (steps > 64) steps = 64;

            /* quad A B C F, with the join wedge spliced in at the end */
            PA_Vec2 piece[72];
            int n = 0;
            if (steps > 0 && cross > 0.0f) {
                /* turning toward +n leaves the gap on the -n side: arc from
                   the next segment's -normal back round to this one's */
                piece[n].x = b.x; piece[n].y = b.y; n++;
                float am = atan2f(-ex, ey);                  /* -m = (ey, -ex) r */
                for (int t = 0; t <= steps; t++) {
                    float an = am - th * (float)t / (float)steps;
                    piece[n].x = b.x + cosf(an) * r;
                    piece[n].y = b.y + sinf(an) * r;
                    n++;
                }
            } else {
                piece[n].x = b.x - nX; piece[n].y = b.y - nY; n++;   /* F */
            }
            piece[n].x = a.x - nX; piece[n].y = a.y - nY; n++;       /* A */
            piece[n].x = a.x + nX; piece[n].y = a.y + nY; n++;       /* B */
            piece[n].x = b.x + nX; piece[n].y = b.y + nY; n++;       /* C */
            if (steps > 0 && cross < 0.0f) {
                /* turning toward -n: the arc goes on the +n side, n to m */
                float an0 = atan2f(nY, nX);
                for (int t = 1; t <= steps; t++) {
                    float an = an0 + th * (float)t / (float)steps;
                    piece[n].x = b.x + cosf(an) * r;
                    piece[n].y = b.y + sinf(an) * r;
                    n++;
                }
                piece[n].x = b.x; piece[n].y = b.y; n++;
            }
            acc_poly(m, piece, n);
        }
    }
}

/* Separable box blur run twice per axis: close enough to a gaussian for a
   drop shadow, and linear in the mask size whatever the radius. */
static void box_pass(uint8_t *buf, int stride_x, int stride_y, int len, int count, int r) {
    static uint8_t line[8192];
    if (len > 8192) return;
    for (int l = 0; l < count; l++) {
        uint8_t *base = buf + (size_t)l * (size_t)stride_y;
        for (int i = 0; i < len; i++) line[i] = base[(size_t)i * (size_t)stride_x];
        int sum = 0, win = 2 * r + 1;
        for (int i = -r; i <= r; i++) sum += (i >= 0 && i < len) ? line[i] : 0;
        for (int i = 0; i < len; i++) {
            base[(size_t)i * (size_t)stride_x] = (uint8_t)((sum + win / 2) / win);
            int add = i + r + 1, sub = i - r;
            sum += (add < len ? line[add] : 0) - (sub >= 0 ? line[sub] : 0);
        }
    }
}

/* ------------------------------------------------------------- compositing -- */
static PA_Color paint_eval(const PA_Paint *p, float px, float py) {
    if (p->kind == PA_PAINT_FLAT || p->stop_count == 0) return p->flat;
    float t;
    if (p->kind == PA_PAINT_LINEAR) {
        float dx = p->x1 - p->x0, dy = p->y1 - p->y0;
        float len2 = dx * dx + dy * dy;
        t = len2 <= 0.0f ? 0.0f : ((px - p->x0) * dx + (py - p->y0) * dy) / len2;
    } else {
        float dx = px - p->x0, dy = py - p->y0;
        t = (sqrtf(dx * dx + dy * dy) - p->r0) / (p->r1 - p->r0);
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

static inline void blend(uint32_t *dst, float r, float g, float b, float a) {
    if (a <= 0.002f) return;
    if (a >= 0.998f) {
        *dst = ((uint32_t)(r + 0.5f) << 16) | ((uint32_t)(g + 0.5f) << 8) | (uint32_t)(b + 0.5f);
        return;
    }
    uint32_t d = *dst;
    float inv = 1.0f - a;
    int rr = (int)(r * a + (float)((d >> 16) & 0xFF) * inv + 0.5f);
    int gg = (int)(g * a + (float)((d >> 8) & 0xFF) * inv + 0.5f);
    int bb = (int)(b * a + (float)(d & 0xFF) * inv + 0.5f);
    *dst = ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
}

/* ----------------------------------------------------------------- draw -- */
PA_TextStyle pa_text_style(PA_Face face, PA_Color fill) {
    PA_TextStyle s;
    memset(&s, 0, sizeof(s));
    s.face = face;
    s.fill = fill;
    s.align = PA_ALIGN_LEFT;
    return s;
}

PA_TextExtent pa_text_measure(const char *text, float size, const PA_TextStyle *st) {
    PA_TextExtent e = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    Face *f = face_get(st->face);
    if (!f || !text || size <= 0.0f) return e;
    Placed run[MAX_RUN];
    float width;
    int n = layout(f, text, size, st->tracking, st->caps, run, &width);
    e.width = width;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (int i = 0; i < n; i++) {
        const Glyph *g = run[i].g;
        if (g->ncont == 0) continue;
        if (run[i].x + g->x0 * size < x0) x0 = run[i].x + g->x0 * size;
        if (run[i].x + g->x1 * size > x1) x1 = run[i].x + g->x1 * size;
        if (g->y0 * size < y0) y0 = g->y0 * size;
        if (g->y1 * size > y1) y1 = g->y1 * size;
    }
    if (x1 < x0) return e;
    float o = st->outline > 0.0f ? st->outline : 0.0f;
    x0 -= o; y0 -= o; x1 += o; y1 += o;
    if (st->shadow_dx != 0.0f || st->shadow_dy != 0.0f) {
        float b = st->shadow_blur > 0.0f ? st->shadow_blur : 0.0f;
        x0 = fminf(x0, x0 + st->shadow_dx - b); x1 = fmaxf(x1, x1 + st->shadow_dx + b);
        y0 = fminf(y0, y0 + st->shadow_dy - b); y1 = fmaxf(y1, y1 + st->shadow_dy + b);
    }
    float a = align_offset(st->align, width);
    e.x0 = x0 + a; e.x1 = x1 + a; e.y0 = y0; e.y1 = y1;
    return e;
}

void pa_text_ex(PA_Canvas *c, const char *text, float x, float y, float size,
                const PA_TextStyle *st) {
    Face *f = face_get(st->face);
    if (!f || !text || !*text || size <= 0.5f) return;

    /* Small type sits its baseline on a pixel row, so flat-bottomed letters
       end on a crisp edge instead of a half-covered one. */
    if (size < 24.0f) y = floorf(y + size + 0.5f) - size;

    Placed run[MAX_RUN];
    float width;
    int n = layout(f, text, size, st->tracking, st->caps, run, &width);
    float px = x + align_offset(st->align, width);

    float r = st->outline > 0.0f ? st->outline : 0.0f;
    int has_shadow = (st->shadow_dx != 0.0f || st->shadow_dy != 0.0f) && PA_A(st->shadow_col) > 0;
    int blur = has_shadow && st->shadow_blur > 0.5f ? (int)(st->shadow_blur * 0.5f + 0.5f) : 0;
    int sdx = has_shadow ? (int)floorf(st->shadow_dx + 0.5f) : 0;
    int sdy = has_shadow ? (int)floorf(st->shadow_dy + 0.5f) : 0;

    /* inked box of the body, canvas pixels */
    float ix0 = 1e30f, iy0 = 1e30f, ix1 = -1e30f, iy1 = -1e30f;
    for (int i = 0; i < n; i++) {
        const Glyph *g = run[i].g;
        if (g->ncont == 0) continue;
        float gx = px + run[i].x;
        if (gx + g->x0 * size < ix0) ix0 = gx + g->x0 * size;
        if (gx + g->x1 * size > ix1) ix1 = gx + g->x1 * size;
        if (y + g->y0 * size < iy0) iy0 = y + g->y0 * size;
        if (y + g->y1 * size > iy1) iy1 = y + g->y1 * size;
    }
    if (ix1 < ix0) return;
    /* Mask covers the body, trimmed to what the canvas clip can show either
       directly or through the shadow offset. */
    int pad = (int)ceilf(r) + 2;
    int mx0 = (int)floorf(ix0) - pad, my0 = (int)floorf(iy0) - pad;
    int mx1 = (int)ceilf(ix1) + pad, my1 = (int)ceilf(iy1) + pad;
    int reach = 3 * blur + 1;
    int vx0 = c->clip_x0 - (sdx > 0 ? sdx : 0) - reach, vx1 = c->clip_x1 - (sdx < 0 ? sdx : 0) + reach;
    int vy0 = c->clip_y0 - (sdy > 0 ? sdy : 0) - reach, vy1 = c->clip_y1 - (sdy < 0 ? sdy : 0) + reach;
    if (mx0 < vx0) mx0 = vx0;
    if (my0 < vy0) my0 = vy0;
    if (mx1 > vx1) mx1 = vx1;
    if (my1 > vy1) my1 = vy1;
    int mw = mx1 - mx0, mh = my1 - my0;
    if (mw <= 0 || mh <= 0 || mw > 8192) return;

    int need_outline = r > 0.0f;
    size_t plane = (size_t)mw * (size_t)mh;
    size_t accplane = (size_t)(mw + 2) * (size_t)mh;
    size_t accn = accplane * (need_outline ? 2 : 1);
    if (!scratch_reserve(plane, accn)) return;
    memset(g_acc, 0, accn * sizeof(float));
    Acc accf = { g_acc, mw, mh, mw + 2 };
    Acc acco = { g_acc + accplane, mw, mh, mw + 2 };

    for (int i = 0; i < n; i++) {
        const Glyph *g = run[i].g;
        if (g->ncont == 0) continue;
        float ox = px + run[i].x - (float)mx0, oy = y - (float)my0;
        /* skip glyphs wholly outside the mask */
        if (ox + g->x1 * size + r < 0.0f || ox + g->x0 * size - r > (float)mw) continue;
        fill_glyph(&accf, g, ox, oy, size);
        if (need_outline) dilate_glyph(&acco, g, ox, oy, size, r);
    }

    /* Integrate each row into one byte of coverage a pixel. */
    uint8_t *f8 = g_cov8, *o8 = need_outline ? g_cov8 + plane : g_cov8;
    for (int pl = 0; pl < (need_outline ? 2 : 1); pl++) {
        const float *src = g_acc + accplane * (size_t)pl;
        uint8_t *dst = pl ? o8 : f8;
        for (int yy = 0; yy < mh; yy++) {
            const float *ar = src + (size_t)yy * (size_t)(mw + 2);
            uint8_t *orow = dst + (size_t)yy * (size_t)mw;
            float sum = 0.0f;
            for (int xx = 0; xx < mw; xx++) {
                sum += ar[xx];
                float v = fabsf(sum);
                orow[xx] = (uint8_t)(v >= 1.0f ? 255 : (int)(v * 255.0f + 0.5f));
            }
        }
    }

    /* Shadow source: the outline shape when there is one, else the fill. */
    const uint8_t *shadow_src = o8;
    uint8_t *sb = NULL;
    int sw = mw + 2 * reach, sh = mh + 2 * reach;
    if (has_shadow && blur > 0) {
        size_t sz = (size_t)sw * (size_t)sh;
        if (sz > g_blur_cap) {
            uint8_t *nb = (uint8_t *)realloc(g_blur_mem, sz);
            if (!nb) { has_shadow = 0; }
            else { g_blur_mem = nb; g_blur_cap = sz; }
        }
        if (has_shadow) {
            sb = g_blur_mem;
            memset(sb, 0, sz);
            for (int yy = 0; yy < mh; yy++)
                memcpy(sb + (size_t)(yy + reach) * (size_t)sw + (size_t)reach,
                       shadow_src + (size_t)yy * (size_t)mw, (size_t)mw);
            for (int pass = 0; pass < 2; pass++) {
                box_pass(sb, 1, sw, sw, sh, blur);
                box_pass(sb, sw, 1, sh, sw, blur);
            }
        }
    }

    /* Composite region: the mask, plus where the shadow lands. */
    int cx0 = mx0, cy0 = my0, cx1 = mx1, cy1 = my1;
    if (has_shadow) {
        int e = sb ? reach : 0;
        if (mx0 + sdx - e < cx0) cx0 = mx0 + sdx - e;
        if (my0 + sdy - e < cy0) cy0 = my0 + sdy - e;
        if (mx1 + sdx + e > cx1) cx1 = mx1 + sdx + e;
        if (my1 + sdy + e > cy1) cy1 = my1 + sdy + e;
    }
    if (cx0 < c->clip_x0) cx0 = c->clip_x0;
    if (cy0 < c->clip_y0) cy0 = c->clip_y0;
    if (cx1 > c->clip_x1) cx1 = c->clip_x1;
    if (cy1 > c->clip_y1) cy1 = c->clip_y1;
    if (cx1 <= cx0 || cy1 <= cy0) return;

    /* Fill paint: explicit paint, a vertical gradient over the caps, or flat. */
    PA_Paint grad;
    const PA_Paint *paint = st->paint;
    if (!paint && PA_A(st->fill_bottom) > 0) {
        grad = pa_linear(0.0f, y, 0.0f, y + size);
        pa_stop(&grad, 0.0f, st->fill);
        pa_stop(&grad, 1.0f, st->fill_bottom);
        paint = &grad;
    }
    int flat_fill = !paint || paint->kind == PA_PAINT_FLAT || paint->stop_count == 0;
    PA_Color fflat = !paint ? st->fill : paint->flat;
    PA_Color oc = st->outline_col;
    float oa_raw = (float)PA_A(oc) / 255.0f;
    float sr = (float)PA_R(st->shadow_col), sg = (float)PA_G(st->shadow_col), sbb = (float)PA_B(st->shadow_col);
    float sa = (float)PA_A(st->shadow_col) / 255.0f;
    const float inv255 = 1.0f / 255.0f;
    int opaque_fill = PA_A(fflat) == 255, opaque_outline = PA_A(oc) == 255;
    uint32_t fill_rgb = fflat & 0x00FFFFFFu, outline_rgb = oc & 0x00FFFFFFu;

    for (int yy = cy0; yy < cy1; yy++) {
        uint32_t *row = c->px + (size_t)yy * (size_t)c->w;
        int my = yy - my0;
        const uint8_t *frow = NULL, *orow = NULL;
        if (my >= 0 && my < mh) {
            frow = f8 + (size_t)my * (size_t)mw;
            orow = o8 + (size_t)my * (size_t)mw;
        }
        const uint8_t *srow = NULL;
        int s_off = 0, s_w = 0;
        if (has_shadow) {
            int sy = yy - sdy - my0 + (sb ? reach : 0);
            int shh = sb ? sh : mh;
            if (sy >= 0 && sy < shh) {
                s_w = sb ? sw : mw;
                srow = (sb ? sb : shadow_src) + (size_t)sy * (size_t)s_w;
                s_off = -sdx - mx0 + (sb ? reach : 0);
            }
        }
        for (int xx = cx0; xx < cx1; xx++) {
            int mxp = xx - mx0;
            int f = 0, o = 0;
            if (frow && mxp >= 0 && mxp < mw) {
                f = frow[mxp];
                o = orow[mxp];
            }
            if (srow && o < 255) {
                int sx = xx + s_off;
                if (sx >= 0 && sx < s_w && srow[sx])
                    blend(&row[xx], sr, sg, sbb, sa * (float)srow[sx] * inv255);
            }
            if (o == 0) continue;

            /* Opaque interiors, the bulk of the pixels, are a plain store. */
            if (flat_fill && opaque_fill && f == 255) { row[xx] = fill_rgb; continue; }
            if (need_outline && opaque_outline && o == 255 && f == 0) { row[xx] = outline_rgb; continue; }

            PA_Color fc = flat_fill ? fflat : paint_eval(paint, (float)xx + 0.5f, (float)yy + 0.5f);
            float fa = (float)PA_A(fc) * inv255;
            float ff = (float)f * inv255, fo = (float)o * inv255;
            if (!need_outline) {
                blend(&row[xx], (float)PA_R(fc), (float)PA_G(fc), (float)PA_B(fc), fa * ff);
                continue;
            }
            /* Fill over outline as one group: the group's opacity is the
               larger of the two, so a banner fading both together fades as a
               whole instead of showing its outline through the fill. */
            float gmax = fa > oa_raw ? fa : oa_raw;
            if (gmax <= 0.0f) continue;
            float la = fa / gmax * ff;              /* fill layer alpha */
            float lo = oa_raw / gmax * fo;          /* outline layer alpha */
            float a = la + lo * (1.0f - la);
            if (a <= 0.0f) continue;
            float w1 = la / a, w2 = lo * (1.0f - la) / a;
            blend(&row[xx],
                  (float)PA_R(fc) * w1 + (float)PA_R(oc) * w2,
                  (float)PA_G(fc) * w1 + (float)PA_G(oc) * w2,
                  (float)PA_B(fc) * w1 + (float)PA_B(oc) * w2,
                  a * gmax);
        }
    }
}

/* --------------------------------------------------------------- legacy -- */
float pa_text_width(const char *text, float size, float tracking) {
    Face *f = face_get(PA_FACE_UI);
    if (!f || !text) return 0.0f;
    Placed run[MAX_RUN];
    float width;
    layout(f, text, size, tracking * LEGACY_TRACKING, 1, run, &width);
    return width;
}

void pa_text(PA_Canvas *c, const char *text, float x, float y, float size,
             PA_Color col, PA_Align align, float tracking) {
    PA_TextStyle s = pa_text_style(PA_FACE_UI, col);
    s.align = align;
    s.tracking = tracking * LEGACY_TRACKING;
    s.caps = 1;
    pa_text_ex(c, text, x, y, size, &s);
}

PA_TextStyle pa_text_bold_style(float size, PA_Color fill, PA_Color outline,
                                PA_Align align, float tracking, float weight) {
    PA_TextStyle s = pa_text_style(PA_FACE_DISPLAY, fill);
    s.align = align;
    s.tracking = tracking * LEGACY_TRACKING;
    s.caps = 1;
    s.outline_col = outline;
    if (weight < 0.0f) weight = 0.0f;
    if ((fill & 0x00FFFFFFu) == (outline & 0x00FFFFFFu)) {
        /* Same colour for both: the stroke font's way of asking for heavier
           type, not an outline. Embolden slightly; no drop, which in the same
           colour would only smear the numerals. */
        s.outline = size * 0.012f * weight;
        return s;
    }
    /* Thick enough to read as the reference outline, thin enough that the
       counters of 0, 6, 8 and 9 stay open at score sizes. */
    s.outline = size * (0.035f + 0.02f * weight);
    if (s.outline < 1.2f) s.outline = 1.2f;
    /* A little air so neighbouring outlines touch without the letters
       inside them crowding, which matters most at label sizes. */
    s.tracking += s.outline * 0.35f;
    /* A hard drop in the outline colour: the score sits on the playfield
       rather than being printed onto it. */
    s.shadow_dy = size * 0.07f;
    if (s.shadow_dy < 1.0f) s.shadow_dy = 1.0f;
    s.shadow_col = outline;
    return s;
}

void pa_text_bold(PA_Canvas *c, const char *text, float x, float y, float size,
                  PA_Color fill, PA_Color outline, PA_Align align, float tracking,
                  float weight) {
    PA_TextStyle s = pa_text_bold_style(size, fill, outline, align, tracking, weight);
    pa_text_ex(c, text, x, y, size, &s);
}
