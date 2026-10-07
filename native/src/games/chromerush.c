/* ===========================================================================
   CHROME RUSH - native
   A chase-camera traffic dodger modelled on the format Dashy Crashy charts
   with: a chunky toy car on a wide highway, swipe a lane at a time, shave past
   traffic for near misses (and the drivers let you know about it), fill the
   nitro and plough through, and when it ends it ends loudly - slow motion,
   cars cartwheeling, a cracked screen and a big KO!

   The look depends on the cars reading as little low-poly toys, so this file
   carries its own tiny 3D path: convex meshes (bevelled bodies, tapered glass
   cabins, octagonal wheels) under a perspective camera with flat per-face
   lighting, painter's order between parts, and real rotation matrices so a
   crashed car can tumble end over end. Everything is still drawn with the
   engine's 2D polygon filler.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------- tunables -- */
#define LANES        4
#define LANE_W       3.3f
#define ROAD_HW      (LANES * LANE_W * 0.5f)
#define RAIL_X       (ROAD_HW + 1.25f)
#define CAM_H        4.3f
#define CAM_BACK     6.4f
#define VIEW_AHEAD   200.0f
#define MAX_CARS     72
#define MAX_PROPS    150
#define MAX_COINS    60
#define MAX_PARTS    260
#define MAX_POPS     12
#define MAX_RINGS    10

/* ------------------------------------------------------- draw helpers -- */
/* The engine's span filler walks the whole clip width on every scanline it
   touches, so each fill here first narrows the clip to its own bounding box.
   A car is forty small faces; without this each one would cost a full-width
   pass over its rows. */
typedef struct { int x0, y0, x1, y1; } ClipSave;

static int fl_i(float v) {
    if (!(v > -60000.0f)) v = -60000.0f;
    if (v > 60000.0f) v = 60000.0f;
    return (int)floorf(v);
}

static int clip_push(PA_Canvas *c, float x0, float y0, float x1, float y1, ClipSave *s) {
    s->x0 = c->clip_x0; s->y0 = c->clip_y0; s->x1 = c->clip_x1; s->y1 = c->clip_y1;
    int a = fl_i(x0) - 1, b = fl_i(y0) - 1, cc = fl_i(x1) + 2, d = fl_i(y1) + 2;
    if (a > c->clip_x0) c->clip_x0 = a;
    if (b > c->clip_y0) c->clip_y0 = b;
    if (cc < c->clip_x1) c->clip_x1 = cc;
    if (d < c->clip_y1) c->clip_y1 = d;
    if (c->clip_x1 <= c->clip_x0 || c->clip_y1 <= c->clip_y0) {
        c->clip_x0 = s->x0; c->clip_y0 = s->y0; c->clip_x1 = s->x1; c->clip_y1 = s->y1;
        return 0;
    }
    return 1;
}
static void clip_pop(PA_Canvas *c, const ClipSave *s) {
    c->clip_x0 = s->x0; c->clip_y0 = s->y0; c->clip_x1 = s->x1; c->clip_y1 = s->y1;
}

static PA_Color amul(PA_Color c, float a) {
    int al = (int)((float)PA_A(c) * pa_clamp01(a) + 0.5f);
    return (c & 0x00FFFFFFu) | ((uint32_t)al << 24);
}

static void fpoly(PA_Canvas *c, const PA_Vec2 *p, int n, PA_Color col) {
    if (n < 3 || PA_A(col) == 0) return;
    float x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y;
    for (int i = 1; i < n; i++) {
        if (p[i].x < x0) x0 = p[i].x;
        if (p[i].x > x1) x1 = p[i].x;
        if (p[i].y < y0) y0 = p[i].y;
        if (p[i].y > y1) y1 = p[i].y;
    }
    ClipSave s;
    if (!clip_push(c, x0, y0, x1, y1, &s)) return;
    pa_fill_poly(c, p, n, col);
    clip_pop(c, &s);
}

static void fpoly_paint(PA_Canvas *c, const PA_Vec2 *p, int n, const PA_Paint *pt) {
    if (n < 3) return;
    float x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y;
    for (int i = 1; i < n; i++) {
        if (p[i].x < x0) x0 = p[i].x;
        if (p[i].x > x1) x1 = p[i].x;
        if (p[i].y < y0) y0 = p[i].y;
        if (p[i].y > y1) y1 = p[i].y;
    }
    ClipSave s;
    if (!clip_push(c, x0, y0, x1, y1, &s)) return;
    pa_fill_poly_paint(c, p, n, pt);
    clip_pop(c, &s);
}

static void fell(PA_Canvas *c, float x, float y, float rx, float ry, PA_Color col) {
    if (rx < 0.3f || ry < 0.3f || PA_A(col) == 0) return;
    ClipSave s;
    if (!clip_push(c, x - rx, y - ry, x + rx, y + ry, &s)) return;
    pa_fill_ellipse(c, x, y, rx, ry, col);
    clip_pop(c, &s);
}
static void fcirc(PA_Canvas *c, float x, float y, float r, PA_Color col) { fell(c, x, y, r, r, col); }

static void fell_paint(PA_Canvas *c, float x, float y, float rx, float ry, const PA_Paint *p) {
    if (rx < 0.3f || ry < 0.3f) return;
    ClipSave s;
    if (!clip_push(c, x - rx, y - ry, x + rx, y + ry, &s)) return;
    pa_fill_ellipse_paint(c, x, y, rx, ry, p);
    clip_pop(c, &s);
}

/** Soft additive-looking bloom: a radial falloff from `col` to clear. */
static void glow(PA_Canvas *c, float x, float y, float r, PA_Color col) {
    if (r < 1.0f) return;
    PA_Paint p = pa_radial(x, y, 0.0f, r);
    pa_stop(&p, 0.0f, col);
    pa_stop(&p, 0.45f, amul(col, 0.45f));
    pa_stop(&p, 1.0f, amul(col, 0.0f));
    fell_paint(c, x, y, r, r, &p);
}

static void rrect(PA_Canvas *c, float x, float y, float w, float h, float r, PA_Color col) {
    ClipSave s;
    if (!clip_push(c, x, y, x + w, y + h, &s)) return;
    pa_round_rect(c, x, y, w, h, r, col);
    clip_pop(c, &s);
}

static void sline(PA_Canvas *c, float x0, float y0, float x1, float y1, float w, PA_Color col) {
    ClipSave s;
    float a = x0 < x1 ? x0 : x1, b = x0 < x1 ? x1 : x0, d = y0 < y1 ? y0 : y1, e = y0 < y1 ? y1 : y0;
    if (!clip_push(c, a - w, d - w, b + w, e + w, &s)) return;
    pa_line(c, x0, y0, x1, y1, w, col);
    clip_pop(c, &s);
}

static void spoly(PA_Canvas *c, const PA_Vec2 *p, int n, int closed, float w, PA_Color col) {
    if (n < 2) return;
    float x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y;
    for (int i = 1; i < n; i++) {
        if (p[i].x < x0) x0 = p[i].x;
        if (p[i].x > x1) x1 = p[i].x;
        if (p[i].y < y0) y0 = p[i].y;
        if (p[i].y > y1) y1 = p[i].y;
    }
    ClipSave s;
    if (!clip_push(c, x0 - w, y0 - w, x1 + w, y1 + w, &s)) return;
    pa_stroke_poly(c, p, n, closed, w, col);
    clip_pop(c, &s);
}

static void arc(PA_Canvas *c, float cx, float cy, float r, float a0, float a1, float w, PA_Color col) {
    PA_Vec2 p[64];
    int n = 6 + (int)(fabsf(a1 - a0) * r * 0.12f);
    if (n > 63) n = 63;
    for (int i = 0; i <= n; i++) {
        float a = a0 + (a1 - a0) * (float)i / (float)n;
        p[i].x = cx + cosf(a) * r;
        p[i].y = cy + sinf(a) * r;
    }
    spoly(c, p, n + 1, 0, w, col);
}

static void txt(PA_Canvas *c, const char *s, float x, float y, float size, PA_Color col, PA_Align al) {
    float w = pa_text_width(s, size, size * 0.06f);
    float x0 = al == PA_ALIGN_CENTER ? x - w * 0.5f : (al == PA_ALIGN_RIGHT ? x - w : x);
    ClipSave cs;
    if (!clip_push(c, x0 - size, y - size * 0.5f, x0 + w + size, y + size * 1.6f, &cs)) return;
    pa_text(c, s, x, y, size, col, al, size * 0.06f);
    clip_pop(c, &cs);
}
static void txtb(PA_Canvas *c, const char *s, float x, float y, float size, PA_Color fill,
                 PA_Color out, PA_Align al, float weight) {
    float w = pa_text_width(s, size, size * 0.06f);
    float x0 = al == PA_ALIGN_CENTER ? x - w * 0.5f : (al == PA_ALIGN_RIGHT ? x - w : x);
    ClipSave cs;
    if (!clip_push(c, x0 - size, y - size * 0.6f, x0 + w + size, y + size * 1.8f, &cs)) return;
    pa_text_bold(c, s, x, y, size, fill, out, al, size * 0.06f, weight);
    clip_pop(c, &cs);
}

/* ------------------------------------------------------------------- 3D -- */
typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { V3 r; r.x = x; r.y = y; r.z = z; return r; }
static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vmul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vnorm(V3 a) {
    float l = sqrtf(vdot(a, a));
    return l > 1e-6f ? vmul(a, 1.0f / l) : v3(0, 0, 0);
}

typedef struct { float m[9]; } M3;
/** R = Ry(yaw) * Rx(pitch) * Rz(roll). Yaw turns the nose toward +x, positive
    pitch dips it, positive roll lifts the right flank. */
static M3 m3_euler(float yaw, float pitch, float roll) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
    M3 r;
    r.m[0] = cy * cr + sy * sp * sr;  r.m[1] = -cy * sr + sy * sp * cr; r.m[2] = sy * cp;
    r.m[3] = cp * sr;                 r.m[4] = cp * cr;                 r.m[5] = -sp;
    r.m[6] = -sy * cr + cy * sp * sr; r.m[7] = sy * sr + cy * sp * cr;  r.m[8] = cy * cp;
    return r;
}
static V3 m3v(const M3 *m, V3 v) {
    return v3(m->m[0] * v.x + m->m[1] * v.y + m->m[2] * v.z,
              m->m[3] * v.x + m->m[4] * v.y + m->m[5] * v.z,
              m->m[6] * v.x + m->m[7] * v.y + m->m[8] * v.z);
}

typedef struct {
    V3    pos;
    float cyw, syw, cp, sp, f, ox, oy, nearz;
    /* screen-space post transform: roll about a pivot, zoom toward a focus, shake */
    float rc, rs, pvx, pvy;
    float zoom, zfx, zfy, ztx, zty, shx, shy;
} Cam;

static void cam_init(Cam *k, V3 pos, float yaw, float pitch, float f, float ox, float oy) {
    memset(k, 0, sizeof(*k));
    k->pos = pos;
    k->cyw = cosf(yaw); k->syw = sinf(yaw);
    k->cp = cosf(pitch); k->sp = sinf(pitch);
    k->f = f; k->ox = ox; k->oy = oy;
    k->nearz = 0.3f;
    k->rc = 1.0f; k->zoom = 1.0f;
}
static float cam_space(const Cam *k, V3 p, float *xo, float *yo) {
    float dx = p.x - k->pos.x, dy = p.y - k->pos.y, dz = p.z - k->pos.z;
    float x1 = dx * k->cyw - dz * k->syw, z1 = dx * k->syw + dz * k->cyw;
    *xo = x1;
    *yo = dy * k->cp + z1 * k->sp;
    return z1 * k->cp - dy * k->sp;
}
static PA_Vec2 cam_post(const Cam *k, float sx, float sy) {
    float dx = sx - k->pvx, dy = sy - k->pvy;
    float rx = k->pvx + dx * k->rc - dy * k->rs, ry = k->pvy + dx * k->rs + dy * k->rc;
    PA_Vec2 o;
    o.x = k->ztx + (rx - k->zfx) * k->zoom + k->shx;
    o.y = k->zty + (ry - k->zfy) * k->zoom + k->shy;
    return o;
}
static int cam_proj(const Cam *k, V3 p, PA_Vec2 *o) {
    float x, y, z = cam_space(k, p, &x, &y);
    if (z < k->nearz) return 0;
    *o = cam_post(k, k->ox + k->f * x / z, k->oy - k->f * y / z);
    return 1;
}
/** Pixels per metre at a point (0 when behind the camera). */
static float cam_ppm(const Cam *k, V3 p) {
    float x, y, z = cam_space(k, p, &x, &y);
    return z < k->nearz ? 0.0f : k->f * k->zoom / z;
}

/* ---------------------------------------------------------------- meshes -- */
enum { MAT_SOLID, MAT_GLASS, MAT_EMIT, MAT_HIDE };
enum { ROLE_FIX, ROLE_BODY, ROLE_ACC };
enum { F_BOT, F_TOP, F_REAR, F_RIGHT, F_FRONT, F_LEFT };

#define MV 20
#define MF 12
typedef struct {
    unsigned char nv, nf, role, layer;
    uint32_t      color;
    V3            v[MV];
    unsigned char fn[MF], fm[MF], fi[MF][10];
    V3            n[MF];
    V3            ctr;
} Mesh;

#define MM 26
typedef struct { int n; float len, wid, hgt; Mesh m[MM]; } Model;

static Mesh g_dummy_mesh;

static Mesh *mk(Model *md, int role, uint32_t color, int layer) {
    Mesh *m = md->n < MM ? &md->m[md->n++] : &g_dummy_mesh;
    memset(m, 0, sizeof(*m));
    m->role = (unsigned char)role;
    m->color = color;
    m->layer = (unsigned char)layer;
    return m;
}

static void mfinish(Mesh *m, const V3 *hint) {
    V3 c = v3(0, 0, 0);
    for (int i = 0; i < m->nv; i++) c = vadd(c, m->v[i]);
    if (m->nv) c = vmul(c, 1.0f / (float)m->nv);
    m->ctr = c;
    for (int f = 0; f < m->nf; f++) {
        V3 n = v3(0, 0, 0), fc = v3(0, 0, 0);
        int k = m->fn[f];
        for (int i = 0; i < k; i++) {
            V3 a = m->v[m->fi[f][i]], b = m->v[m->fi[f][(i + 1) % k]];
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
            fc = vadd(fc, a);
        }
        n = vnorm(n);
        fc = vmul(fc, 1.0f / (float)k);
        V3 out = hint ? *hint : vsub(fc, c);
        if (vdot(n, out) < 0.0f) n = vmul(n, -1.0f);
        m->n[f] = n;
    }
}

static void mface4(Mesh *m, int mat, int a, int b, int c, int d) {
    int f = m->nf++;
    m->fn[f] = 4; m->fm[f] = (unsigned char)mat;
    m->fi[f][0] = (unsigned char)a; m->fi[f][1] = (unsigned char)b;
    m->fi[f][2] = (unsigned char)c; m->fi[f][3] = (unsigned char)d;
}

static Mesh *hexa(Model *md, int role, uint32_t col, int layer, int mat,
                  float bx0, float bx1, float bz0, float bz1, float y0,
                  float tx0, float tx1, float tz0, float tz1, float y1) {
    Mesh *m = mk(md, role, col, layer);
    m->nv = 8;
    m->v[0] = v3(bx0, y0, bz0); m->v[1] = v3(bx1, y0, bz0); m->v[2] = v3(bx1, y0, bz1); m->v[3] = v3(bx0, y0, bz1);
    m->v[4] = v3(tx0, y1, tz0); m->v[5] = v3(tx1, y1, tz0); m->v[6] = v3(tx1, y1, tz1); m->v[7] = v3(tx0, y1, tz1);
    mface4(m, mat, 0, 1, 2, 3);   /* bottom */
    mface4(m, mat, 4, 5, 6, 7);   /* top */
    mface4(m, mat, 0, 1, 5, 4);   /* rear  (-z) */
    mface4(m, mat, 1, 2, 6, 5);   /* right (+x) */
    mface4(m, mat, 2, 3, 7, 6);   /* front (+z) */
    mface4(m, mat, 3, 0, 4, 7);   /* left  (-x) */
    mfinish(m, NULL);
    return m;
}

static Mesh *box(Model *md, int role, uint32_t col, int layer, int mat,
                 float cx, float y0, float cz, float w, float h, float l) {
    return hexa(md, role, col, layer, mat, cx - w * 0.5f, cx + w * 0.5f, cz - l * 0.5f, cz + l * 0.5f, y0,
                cx - w * 0.5f, cx + w * 0.5f, cz - l * 0.5f, cz + l * 0.5f, y0 + h);
}

/** Convex polygon in the x-y plane extruded along z. */
static Mesh *prism_z(Model *md, int role, uint32_t col, int layer, int mat,
                     const float *xy, int k, float z0, float z1) {
    Mesh *m = mk(md, role, col, layer);
    m->nv = (unsigned char)(k * 2);
    for (int i = 0; i < k; i++) {
        m->v[i] = v3(xy[i * 2], xy[i * 2 + 1], z0);
        m->v[k + i] = v3(xy[i * 2], xy[i * 2 + 1], z1);
    }
    m->fn[0] = (unsigned char)k; m->fn[1] = (unsigned char)k;
    m->fm[0] = m->fm[1] = (unsigned char)mat;
    for (int i = 0; i < k; i++) { m->fi[0][i] = (unsigned char)i; m->fi[1][i] = (unsigned char)(k + i); }
    m->nf = 2;
    for (int i = 0; i < k; i++) mface4(m, mat, i, (i + 1) % k, k + (i + 1) % k, k + i);
    mfinish(m, NULL);
    return m;
}

/** Convex polygon in the z-y plane extruded along x (wheels, side profiles). */
static Mesh *prism_x(Model *md, int role, uint32_t col, int layer, int mat,
                     const float *zy, int k, float x0, float x1) {
    Mesh *m = mk(md, role, col, layer);
    m->nv = (unsigned char)(k * 2);
    for (int i = 0; i < k; i++) {
        m->v[i] = v3(x0, zy[i * 2 + 1], zy[i * 2]);
        m->v[k + i] = v3(x1, zy[i * 2 + 1], zy[i * 2]);
    }
    m->fn[0] = (unsigned char)k; m->fn[1] = (unsigned char)k;
    m->fm[0] = m->fm[1] = (unsigned char)mat;
    for (int i = 0; i < k; i++) { m->fi[0][i] = (unsigned char)i; m->fi[1][i] = (unsigned char)(k + i); }
    m->nf = 2;
    for (int i = 0; i < k; i++) mface4(m, mat, i, (i + 1) % k, k + (i + 1) % k, k + i);
    mfinish(m, NULL);
    return m;
}

/** A single one-sided quad (decals: stripes, door panels, letters). */
static Mesh *quad(Model *md, int role, uint32_t col, int layer, int mat, V3 a, V3 b, V3 c, V3 d, V3 out) {
    Mesh *m = mk(md, role, col, layer);
    m->nv = 4;
    m->v[0] = a; m->v[1] = b; m->v[2] = c; m->v[3] = d;
    mface4(m, mat, 0, 1, 2, 3);
    mfinish(m, &out);
    return m;
}

/* Axis-aligned decal on a side of the car: x = const plane, z/y extents. */
static void decal_x(Model *md, int role, uint32_t col, int mat, float x, float z0, float z1, float y0, float y1) {
    float s = x >= 0.0f ? 1.0f : -1.0f;
    quad(md, role, col, 3, mat, v3(x, y0, z0), v3(x, y0, z1), v3(x, y1, z1), v3(x, y1, z0), v3(s, 0, 0));
}
static void decal_z(Model *md, int role, uint32_t col, int mat, float z, float x0, float x1, float y0, float y1) {
    float s = z >= 0.0f ? 1.0f : -1.0f;
    quad(md, role, col, 3, mat, v3(x0, y0, z), v3(x1, y0, z), v3(x1, y1, z), v3(x0, y1, z), v3(0, 0, s));
}
static void decal_y(Model *md, int role, uint32_t col, int mat, float y, float x0, float x1, float z0, float z1) {
    quad(md, role, col, 3, mat, v3(x0, y, z0), v3(x1, y, z0), v3(x1, y, z1), v3(x0, y, z1), v3(0, 1, 0));
}

static void wheel(Model *md, float xc, float z, float r, float w) {
    float zy[16], hub[16];
    for (int i = 0; i < 8; i++) {
        float a = ((float)i + 0.5f) * PA_TAU / 8.0f;
        zy[i * 2] = z + cosf(a) * r;          zy[i * 2 + 1] = r + sinf(a) * r;
        hub[i * 2] = z + cosf(a) * r * 0.48f; hub[i * 2 + 1] = r + sinf(a) * r * 0.48f;
    }
    prism_x(md, ROLE_FIX, 0x2A2633, 0, MAT_SOLID, zy, 8, xc - w * 0.5f, xc + w * 0.5f);
    Mesh *h;
    if (xc >= 0.0f) h = prism_x(md, ROLE_FIX, 0xE4E4EE, 0, MAT_SOLID, hub, 8, xc + w * 0.5f, xc + w * 0.5f + 0.02f);
    else            h = prism_x(md, ROLE_FIX, 0xE4E4EE, 0, MAT_SOLID, hub, 8, xc - w * 0.5f - 0.02f, xc - w * 0.5f);
    /* only the outer cap: the inner one would show through from the far side */
    if (xc >= 0.0f) h->fm[0] = MAT_HIDE; else h->fm[1] = MAT_HIDE;
}

static void wheels4(Model *md, float hw, float zf, float zr, float r, float w) {
    wheel(md, -hw, zr, r, w); wheel(md, hw, zr, r, w);
    wheel(md, -hw, zf, r, w); wheel(md, hw, zf, r, w);
}

/** Body with chamfered shoulders: an octagonal section run along z. */
static Mesh *body_oct(Model *md, int role, uint32_t col, float hw, float y0, float y1, float z0, float z1, float bev) {
    float b2 = bev * 0.45f;
    float xy[16] = { -hw + b2, y0,  hw - b2, y0,  hw, y0 + b2,  hw, y1 - bev,
                     hw - bev, y1,  -hw + bev, y1,  -hw, y1 - bev,  -hw, y0 + b2 };
    return prism_z(md, role, col, 1, MAT_SOLID, xy, 8, z0, z1);
}

static Mesh *cabin(Model *md, int role, uint32_t col, float bw, float tw, float y0, float y1,
                   float bz0, float bz1, float tz0, float tz1) {
    Mesh *m = hexa(md, role, col, 2, MAT_GLASS, -bw, bw, bz0, bz1, y0, -tw, tw, tz0, tz1, y1);
    m->fm[F_TOP] = MAT_SOLID;
    m->fm[F_BOT] = MAT_HIDE;
    return m;
}

static void lamps_rear(Model *md, float hw, float y0, float y1, float z, float w) {
    box(md, ROLE_FIX, 0xFF3048, 2, MAT_EMIT, -hw + w * 0.5f + 0.08f, y0, z - 0.02f, w, y1 - y0, 0.06f);
    box(md, ROLE_FIX, 0xFF3048, 2, MAT_EMIT, hw - w * 0.5f - 0.08f, y0, z - 0.02f, w, y1 - y0, 0.06f);
}
static void lamps_front(Model *md, float hw, float y0, float y1, float z, float w) {
    box(md, ROLE_FIX, 0xFFF4C8, 2, MAT_EMIT, -hw + w * 0.5f + 0.08f, y0, z + 0.02f, w, y1 - y0, 0.06f);
    box(md, ROLE_FIX, 0xFFF4C8, 2, MAT_EMIT, hw - w * 0.5f - 0.08f, y0, z + 0.02f, w, y1 - y0, 0.06f);
}
static void bumper(Model *md, float hw, float y0, float y1, float z, uint32_t col) {
    box(md, ROLE_FIX, col, 2, MAT_SOLID, 0.0f, y0, z, hw * 2.0f + 0.04f, y1 - y0, 0.16f);
}

/* ----------------------------------------------------------- the fleet -- */
enum {
    VM_LEARNER, VM_POLICE, VM_MUSCLE, VM_ICECREAM, VM_TAXI, VM_HOVER, VM_TRACTOR, VM_F1,
    VM_HIPPIE, VM_KART, VM_SPORTS, VM_MINT, VM_MONSTER, VM_TOW, VM_BUS, VM_FIRE,
    VM_SEDAN, VM_HATCH, VM_VAN, VM_BOXTRUCK, VM_PICKUP,
    VM_CONE, VM_TREE, VM_LAMP, VM_BOARD, VM_GATE,
    VM_COUNT
};

static Model g_model[VM_COUNT];
static int   g_models_ready;

/** The common hatch/sedan recipe: wheels, bevelled body, tapered glasshouse,
    lamps and bumpers. Everything else is extras on top of it. */
static void car_base(Model *md, float len, float wid, float wr, float by0, float by1, float bev,
                     float cy1, float cz0, float cz1, float tz0, float tz1, float cw, float tw) {
    float hw = wid * 0.5f, hl = len * 0.5f;
    wheels4(md, hw - 0.14f, hl * 0.62f, -hl * 0.62f, wr, 0.40f);
    body_oct(md, ROLE_BODY, 0, hw, by0, by1, -hl, hl, bev);
    cabin(md, ROLE_BODY, 0, hw * cw, hw * tw, by1, cy1, cz0, cz1, tz0, tz1);
    lamps_rear(md, hw, by1 - 0.30f, by1 - 0.10f, -hl, 0.36f);
    lamps_front(md, hw, by1 - 0.30f, by1 - 0.12f, hl, 0.32f);
    bumper(md, hw, by0, by0 + 0.16f, -hl - 0.04f, 0xE9E7F0);
    bumper(md, hw, by0, by0 + 0.16f, hl + 0.04f, 0xE9E7F0);
}

static void model_bounds(Model *md) {
    float x0 = 1e9f, x1 = -1e9f, y1 = 0.0f, z0 = 1e9f, z1 = -1e9f;
    for (int i = 0; i < md->n; i++)
        for (int k = 0; k < md->m[i].nv; k++) {
            V3 v = md->m[i].v[k];
            if (v.x < x0) x0 = v.x;
            if (v.x > x1) x1 = v.x;
            if (v.y > y1) y1 = v.y;
            if (v.z < z0) z0 = v.z;
            if (v.z > z1) z1 = v.z;
        }
    md->wid = x1 - x0; md->len = z1 - z0; md->hgt = y1;
}

static void build_learner(Model *md, int plate) {
    car_base(md, 3.7f, 1.96f, 0.36f, 0.26f, 1.00f, 0.16f, 1.66f, -1.62f, 0.48f, -1.42f, 0.10f, 0.92f, 0.80f);
    if (plate) {
        /* The L-plate tent on the roof, red L on both slopes. */
        float zy[6] = { -0.30f, 1.66f, 0.30f, 1.66f, 0.0f, 2.08f };
        prism_x(md, ROLE_FIX, 0xFAFAFD, 2, MAT_SOLID, zy, 3, -0.34f, 0.34f);
        for (int side = -1; side <= 1; side += 2) {
            float s = (float)side;
            /* slope from (z = 0.30 s, y 1.66) to (0, 2.08); outward normal */
            V3 out = vnorm(v3(0.0f, 0.30f, 0.42f * s));
            #define SP(u, t) vadd(v3((u), 1.66f + 0.42f * (t), s * 0.30f * (1.0f - (t))), vmul(out, 0.012f))
            quad(md, ROLE_FIX, 0xE8283C, 3, MAT_EMIT, SP(-0.14f, 0.16f), SP(-0.05f, 0.16f), SP(-0.05f, 0.80f), SP(-0.14f, 0.80f), out);
            quad(md, ROLE_FIX, 0xE8283C, 3, MAT_EMIT, SP(-0.14f, 0.16f), SP(0.14f, 0.16f), SP(0.14f, 0.32f), SP(-0.14f, 0.32f), out);
            #undef SP
        }
    }
}

static void build_sedan(Model *md) {
    car_base(md, 4.2f, 2.0f, 0.37f, 0.26f, 0.98f, 0.14f, 1.60f, -1.00f, 0.86f, -0.78f, 0.42f, 0.92f, 0.78f);
}

static void build_police(Model *md) {
    build_sedan(md);
    decal_x(md, ROLE_ACC, 0, MAT_SOLID, 1.003f, -0.95f, 0.85f, 0.44f, 0.84f);
    decal_x(md, ROLE_ACC, 0, MAT_SOLID, -1.003f, -0.95f, 0.85f, 0.44f, 0.84f);
    decal_y(md, ROLE_ACC, 0, MAT_SOLID, 1.605f, -0.62f, 0.62f, -0.70f, 0.36f);
    box(md, ROLE_FIX, 0xFF2E48, 4, MAT_EMIT, -0.30f, 1.60f, -0.15f, 0.56f, 0.18f, 0.30f);
    box(md, ROLE_FIX, 0x2E7BFF, 4, MAT_EMIT, 0.30f, 1.60f, -0.15f, 0.56f, 0.18f, 0.30f);
}

static void build_muscle(Model *md) {
    car_base(md, 4.3f, 2.08f, 0.40f, 0.24f, 0.92f, 0.12f, 1.42f, -1.05f, 0.55f, -0.82f, 0.18f, 0.90f, 0.76f);
    for (int s = -1; s <= 1; s += 2) {
        float x0 = (float)s * 0.12f, x1 = (float)s * 0.34f;
        float a = x0 < x1 ? x0 : x1, b = x0 < x1 ? x1 : x0;
        decal_y(md, ROLE_ACC, 0, MAT_SOLID, 0.925f, a, b, 0.55f, 2.15f);
        decal_y(md, ROLE_ACC, 0, MAT_SOLID, 0.925f, a, b, -2.15f, -1.05f);
        decal_y(md, ROLE_ACC, 0, MAT_SOLID, 1.425f, a * 0.8f, b * 0.8f, -0.82f, 0.18f);
    }
    box(md, ROLE_ACC, 0, 4, MAT_SOLID, 0.0f, 1.12f, -1.98f, 1.9f, 0.08f, 0.32f);
    box(md, ROLE_ACC, 0, 3, MAT_SOLID, -0.7f, 0.92f, -1.98f, 0.10f, 0.22f, 0.18f);
    box(md, ROLE_ACC, 0, 3, MAT_SOLID, 0.7f, 0.92f, -1.98f, 0.10f, 0.22f, 0.18f);
}

static void build_taxi(Model *md) {
    build_sedan(md);
    for (int s = -1; s <= 1; s += 2)
        for (int k = 0; k < 8; k++)
            decal_x(md, ROLE_ACC, 0, MAT_SOLID, (float)s * 1.003f, -1.4f + (float)k * 0.36f, -1.4f + (float)k * 0.36f + 0.18f,
                    0.58f + (float)(k & 1) * 0.12f, 0.70f + (float)(k & 1) * 0.12f);
    box(md, ROLE_FIX, 0xFFF6C8, 4, MAT_EMIT, 0.0f, 1.60f, -0.20f, 0.70f, 0.24f, 0.30f);
}

static void build_hatch(Model *md) { build_learner(md, 0); }

static void build_van(Model *md) {
    float hw = 1.06f;
    wheels4(md, hw - 0.12f, 1.55f, -1.55f, 0.40f, 0.40f);
    body_oct(md, ROLE_BODY, 0, hw, 0.30f, 1.05f, -2.4f, 2.4f, 0.10f);
    box(md, ROLE_BODY, 0, 2, MAT_SOLID, 0.0f, 1.05f, -0.75f, hw * 2.0f, 1.25f, 3.3f);
    cabin(md, ROLE_BODY, 0, hw * 0.94f, hw * 0.86f, 1.05f, 2.05f, 0.90f, 1.90f, 0.90f, 1.35f);
    decal_z(md, ROLE_FIX, 0x3B3F58, MAT_SOLID, -2.41f, -0.03f, 0.03f, 0.40f, 2.20f);
    lamps_rear(md, hw, 0.62f, 0.95f, -2.4f, 0.24f);
    lamps_front(md, hw, 0.70f, 0.90f, 2.4f, 0.30f);
    bumper(md, hw, 0.30f, 0.46f, -2.44f, 0x3B3F58);
}

static void build_icecream(Model *md) {
    float hw = 1.06f;
    wheels4(md, hw - 0.12f, 1.5f, -1.5f, 0.40f, 0.40f);
    body_oct(md, ROLE_BODY, 0, hw, 0.30f, 1.05f, -2.3f, 2.3f, 0.10f);
    box(md, ROLE_FIX, 0xF6F3FA, 2, MAT_SOLID, 0.0f, 1.05f, -0.70f, hw * 2.0f, 1.30f, 3.2f);
    for (int s = -1; s <= 1; s += 2) {
        decal_x(md, ROLE_ACC, 0, MAT_SOLID, (float)s * (hw + 0.004f), -2.3f, 0.9f, 1.10f, 1.30f);
        decal_x(md, ROLE_FIX, 0x2B3352, MAT_SOLID, (float)s * (hw + 0.004f), -1.6f, 0.2f, 1.45f, 2.05f);
    }
    decal_z(md, ROLE_ACC, 0, MAT_SOLID, -2.305f, -hw, hw, 1.10f, 1.30f);
    cabin(md, ROLE_BODY, 0, hw * 0.94f, hw * 0.86f, 1.05f, 2.0f, 0.90f, 1.80f, 0.90f, 1.30f);
    /* the cone on the roof */
    hexa(md, ROLE_FIX, 0xE7B064, 4, MAT_SOLID, -0.08f, 0.08f, -0.78f, -0.62f, 2.35f, -0.30f, 0.30f, -1.0f, -0.40f, 2.95f);
    box(md, ROLE_ACC, 0, 4, MAT_SOLID, 0.0f, 2.95f, -0.70f, 0.66f, 0.42f, 0.66f);
    box(md, ROLE_ACC, 0, 4, MAT_SOLID, 0.0f, 3.37f, -0.70f, 0.40f, 0.22f, 0.40f);
    box(md, ROLE_FIX, 0xE8283C, 4, MAT_SOLID, 0.0f, 3.59f, -0.70f, 0.14f, 0.14f, 0.14f);
    lamps_rear(md, hw, 0.62f, 0.95f, -2.3f, 0.24f);
    lamps_front(md, hw, 0.70f, 0.90f, 2.3f, 0.30f);
    bumper(md, hw, 0.30f, 0.46f, -2.34f, 0xE9E7F0);
}

static void build_hover(Model *md) {
    /* inflated skirt, deck, bubble cab and the big fan at the back */
    float sk[20];
    for (int i = 0; i < 10; i++) {
        float a = (float)i * PA_TAU / 10.0f + PA_PI * 0.5f;
        sk[i * 2] = cosf(a) * 1.18f;
        sk[i * 2 + 1] = 0.30f + sinf(a) * 0.30f;
    }
    prism_z(md, ROLE_FIX, 0x2A2732, 1, MAT_SOLID, sk, 10, -2.0f, 2.0f);
    box(md, ROLE_BODY, 0, 1, MAT_SOLID, 0.0f, 0.58f, 0.0f, 2.0f, 0.24f, 3.7f);
    cabin(md, ROLE_BODY, 0, 0.82f, 0.60f, 0.82f, 1.70f, -0.8f, 1.2f, -0.5f, 0.7f);
    float ring[16], hub[16];
    for (int i = 0; i < 8; i++) {
        float a = ((float)i + 0.5f) * PA_TAU / 8.0f;
        ring[i * 2] = cosf(a) * 1.0f; ring[i * 2 + 1] = 1.90f + sinf(a) * 1.0f;
        hub[i * 2] = cosf(a) * 0.30f; hub[i * 2 + 1] = 1.90f + sinf(a) * 0.30f;
    }
    prism_z(md, ROLE_BODY, 0, 2, MAT_SOLID, ring, 8, -2.05f, -1.80f);
    float in[16];
    for (int i = 0; i < 16; i += 2) { in[i] = ring[i] * 0.80f; in[i + 1] = 1.90f + (ring[i + 1] - 1.90f) * 0.80f; }
    prism_z(md, ROLE_FIX, 0x3A3746, 3, MAT_SOLID, in, 8, -2.08f, -2.06f);
    prism_z(md, ROLE_ACC, 0, 4, MAT_SOLID, hub, 8, -2.12f, -2.09f);
    box(md, ROLE_FIX, 0xD8D8E2, 4, MAT_SOLID, 0.0f, 1.10f, -2.10f, 0.08f, 1.60f, 0.04f);
    box(md, ROLE_FIX, 0xD8D8E2, 4, MAT_SOLID, 0.0f, 1.86f, -2.10f, 1.6f, 0.08f, 0.04f);
    box(md, ROLE_BODY, 0, 2, MAT_SOLID, 0.0f, 0.82f, -1.85f, 0.18f, 0.9f, 0.18f);
}

static void build_tractor(Model *md) {
    wheel(md, -1.05f, -0.80f, 0.80f, 0.56f); wheel(md, 1.05f, -0.80f, 0.80f, 0.56f);
    wheel(md, -0.66f, 1.30f, 0.44f, 0.34f);  wheel(md, 0.66f, 1.30f, 0.44f, 0.34f);
    box(md, ROLE_FIX, 0x34323E, 1, MAT_SOLID, 0.0f, 0.40f, 0.10f, 0.9f, 0.40f, 3.2f);
    box(md, ROLE_BODY, 0, 1, MAT_SOLID, 0.0f, 0.70f, 0.95f, 1.0f, 0.62f, 1.9f);
    decal_z(md, ROLE_FIX, 0x2B2D3C, MAT_SOLID, 1.905f, -0.36f, 0.36f, 0.80f, 1.22f);
    box(md, ROLE_BODY, 0, 1, MAT_SOLID, 0.0f, 0.70f, -0.75f, 1.5f, 0.50f, 1.4f);
    cabin(md, ROLE_BODY, 0, 0.72f, 0.72f, 1.20f, 2.45f, -1.40f, -0.05f, -1.40f, -0.05f);
    box(md, ROLE_ACC, 0, 4, MAT_SOLID, 0.0f, 2.45f, -0.72f, 1.70f, 0.14f, 1.62f);
    box(md, ROLE_FIX, 0x2A2633, 4, MAT_SOLID, 0.30f, 1.30f, 1.55f, 0.14f, 1.10f, 0.14f);
    lamps_front(md, 0.5f, 0.98f, 1.18f, 1.9f, 0.22f);
}

static void build_f1(Model *md) {
    wheels4(md, 0.98f, 1.35f, -1.25f, 0.44f, 0.46f);
    hexa(md, ROLE_BODY, 0, 1, MAT_SOLID, -0.55f, 0.55f, -1.9f, 2.3f, 0.16f, -0.42f, 0.42f, -1.7f, 2.0f, 0.62f);
    hexa(md, ROLE_ACC, 0, 2, MAT_SOLID, -0.70f, 0.70f, -1.4f, 0.6f, 0.16f, -0.62f, 0.62f, -1.3f, 0.4f, 0.55f);
    box(md, ROLE_FIX, 0x26232E, 3, MAT_SOLID, 0.0f, 0.62f, -0.25f, 0.56f, 0.06f, 0.9f);
    box(md, ROLE_FIX, 0xFFD21E, 4, MAT_SOLID, 0.0f, 0.62f, -0.40f, 0.40f, 0.40f, 0.42f);
    box(md, ROLE_FIX, 0x2A3A66, 4, MAT_EMIT, 0.0f, 0.78f, -0.18f, 0.34f, 0.14f, 0.04f);
    box(md, ROLE_ACC, 0, 3, MAT_SOLID, 0.0f, 1.00f, -1.95f, 2.0f, 0.10f, 0.50f);
    box(md, ROLE_BODY, 0, 3, MAT_SOLID, -1.0f, 0.55f, -1.95f, 0.06f, 0.60f, 0.6f);
    box(md, ROLE_BODY, 0, 3, MAT_SOLID, 1.0f, 0.55f, -1.95f, 0.06f, 0.60f, 0.6f);
    box(md, ROLE_ACC, 0, 3, MAT_SOLID, 0.0f, 0.10f, 2.25f, 2.0f, 0.08f, 0.40f);
    lamps_rear(md, 0.2f, 0.28f, 0.42f, -1.9f, 0.14f);
}

static void build_hippie(Model *md) {
    float hw = 1.02f;
    wheels4(md, hw - 0.12f, 1.4f, -1.4f, 0.38f, 0.40f);
    body_oct(md, ROLE_BODY, 0, hw, 0.28f, 1.15f, -2.15f, 2.15f, 0.12f);
    cabin(md, ROLE_FIX, 0xF6F3FA, hw * 0.98f, hw * 0.90f, 1.15f, 2.12f, -2.05f, 2.0f, -1.9f, 1.6f);
    box(md, ROLE_FIX, 0x6B4A2E, 4, MAT_SOLID, 0.0f, 2.12f, -0.15f, 1.5f, 0.10f, 2.6f);
    for (int s = -1; s <= 1; s += 2) {
        decal_x(md, ROLE_ACC, 0, MAT_SOLID, (float)s * (hw + 0.004f), -2.1f, 2.1f, 0.62f, 0.78f);
        decal_x(md, ROLE_FIX, 0xFFC21A, MAT_SOLID, (float)s * (hw + 0.004f), -2.1f, 2.1f, 0.80f, 0.92f);
    }
    decal_z(md, ROLE_ACC, 0, MAT_SOLID, 2.155f, -0.6f, 0.6f, 0.62f, 0.78f);
    lamps_rear(md, hw, 0.70f, 1.00f, -2.15f, 0.26f);
    lamps_front(md, hw, 0.70f, 0.95f, 2.15f, 0.30f);
    bumper(md, hw, 0.28f, 0.44f, -2.19f, 0xE9E7F0);
    bumper(md, hw, 0.28f, 0.44f, 2.19f, 0xE9E7F0);
}

static void build_kart(Model *md) {
    wheels4(md, 0.78f, 0.80f, -0.75f, 0.30f, 0.30f);
    box(md, ROLE_FIX, 0x34323E, 1, MAT_SOLID, 0.0f, 0.14f, 0.0f, 1.20f, 0.14f, 2.1f);
    hexa(md, ROLE_BODY, 0, 2, MAT_SOLID, -0.55f, 0.55f, 0.55f, 1.25f, 0.18f, -0.40f, 0.40f, 0.62f, 1.05f, 0.52f);
    box(md, ROLE_BODY, 0, 2, MAT_SOLID, 0.0f, 0.28f, -0.50f, 0.9f, 0.40f, 0.5f);
    box(md, ROLE_ACC, 0, 3, MAT_SOLID, 0.0f, 0.40f, -0.10f, 0.62f, 0.55f, 0.46f);
    box(md, ROLE_FIX, 0xFFC93C, 4, MAT_SOLID, 0.0f, 0.95f, -0.10f, 0.72f, 0.66f, 0.66f);
    decal_z(md, ROLE_FIX, 0x2A2633, MAT_SOLID, 0.231f, -0.22f, -0.10f, 1.30f, 1.44f);
    decal_z(md, ROLE_FIX, 0x2A2633, MAT_SOLID, 0.231f, 0.10f, 0.22f, 1.30f, 1.44f);
    decal_z(md, ROLE_FIX, 0xB8402A, MAT_SOLID, 0.231f, -0.20f, 0.20f, 1.06f, 1.16f);
    box(md, ROLE_FIX, 0x2A2633, 3, MAT_SOLID, 0.0f, 0.18f, -1.08f, 1.40f, 0.18f, 0.10f);
    lamps_rear(md, 0.45f, 0.36f, 0.50f, -0.75f, 0.16f);
}

static void build_sports(Model *md) {
    float hw = 1.05f;
    wheels4(md, hw - 0.12f, 1.35f, -1.35f, 0.38f, 0.44f);
    hexa(md, ROLE_BODY, 0, 1, MAT_SOLID, -hw, hw, -2.15f, 2.15f, 0.22f, -hw * 0.96f, hw * 0.96f, -2.05f, 1.75f, 0.84f);
    cabin(md, ROLE_BODY, 0, hw * 0.84f, hw * 0.66f, 0.84f, 1.28f, -1.30f, 0.40f, -1.05f, -0.15f);
    for (int s = -1; s <= 1; s += 2)
        decal_x(md, ROLE_ACC, 0, MAT_SOLID, (float)s * (hw + 0.004f), -1.1f, -0.25f, 0.42f, 0.66f);
    box(md, ROLE_FIX, 0xFF3048, 2, MAT_EMIT, 0.0f, 0.58f, -2.17f, hw * 1.8f, 0.12f, 0.04f);
    lamps_front(md, hw * 0.9f, 0.56f, 0.70f, 2.15f, 0.34f);
    bumper(md, hw, 0.22f, 0.36f, -2.19f, 0x2A2633);
}

static void build_mint(Model *md) {
    car_base(md, 3.5f, 1.92f, 0.38f, 0.28f, 1.02f, 0.26f, 1.70f, -1.40f, 0.40f, -1.15f, 0.05f, 0.92f, 0.80f);
    decal_y(md, ROLE_ACC, 0, MAT_SOLID, 1.705f, -0.66f, 0.66f, -1.08f, -0.02f);
}

static void build_monster(Model *md) {
    wheels4(md, 1.18f, 1.35f, -1.35f, 0.92f, 0.72f);
    box(md, ROLE_FIX, 0x34323E, 1, MAT_SOLID, 0.0f, 0.70f, 0.0f, 1.6f, 0.40f, 3.4f);
    body_oct(md, ROLE_BODY, 0, 1.08f, 1.05f, 1.75f, -2.1f, 2.1f, 0.14f);
    cabin(md, ROLE_BODY, 0, 0.98f, 0.86f, 1.75f, 2.45f, -0.70f, 0.70f, -0.55f, 0.35f);
    for (int s = -1; s <= 1; s += 2)
        decal_x(md, ROLE_ACC, 0, MAT_SOLID, (float)s * 1.084f, -2.0f, 2.0f, 1.36f, 1.50f);
    lamps_rear(md, 1.08f, 1.40f, 1.62f, -2.1f, 0.30f);
    lamps_front(md, 1.08f, 1.40f, 1.60f, 2.1f, 0.30f);
    box(md, ROLE_FIX, 0xFFF4C8, 4, MAT_EMIT, 0.0f, 2.45f, 0.25f, 1.20f, 0.14f, 0.16f);
}

static void build_pickup(Model *md, int crane) {
    float hw = 1.02f;
    wheels4(md, hw - 0.12f, 1.45f, -1.45f, 0.42f, 0.42f);
    body_oct(md, ROLE_BODY, 0, hw, 0.32f, 1.05f, -2.3f, 2.3f, 0.12f);
    cabin(md, ROLE_BODY, 0, hw * 0.94f, hw * 0.84f, 1.05f, 1.85f, -0.20f, 1.00f, -0.10f, 0.70f);
    box(md, ROLE_FIX, 0x3A3746, 2, MAT_SOLID, 0.0f, 1.04f, -1.25f, hw * 1.8f, 0.02f, 1.9f);
    box(md, ROLE_BODY, 0, 2, MAT_SOLID, -hw + 0.06f, 1.05f, -1.25f, 0.12f, 0.32f, 2.0f);
    box(md, ROLE_BODY, 0, 2, MAT_SOLID, hw - 0.06f, 1.05f, -1.25f, 0.12f, 0.32f, 2.0f);
    box(md, ROLE_BODY, 0, 2, MAT_SOLID, 0.0f, 1.05f, -2.24f, hw * 2.0f, 0.32f, 0.12f);
    if (crane) {
        box(md, ROLE_ACC, 0, 4, MAT_SOLID, 0.0f, 1.05f, -1.0f, 0.36f, 0.70f, 0.36f);
        hexa(md, ROLE_ACC, 0, 4, MAT_SOLID, -0.14f, 0.14f, -1.2f, -0.8f, 1.55f, -0.14f, 0.14f, -2.6f, -2.3f, 2.25f);
        box(md, ROLE_FIX, 0x2A2633, 4, MAT_SOLID, 0.0f, 1.45f, -2.5f, 0.05f, 0.80f, 0.05f);
        box(md, ROLE_FIX, 0xFFB01E, 4, MAT_EMIT, 0.0f, 1.85f, 0.30f, 0.9f, 0.14f, 0.20f);
    }
    lamps_rear(md, hw, 0.70f, 0.98f, -2.3f, 0.24f);
    lamps_front(md, hw, 0.72f, 0.92f, 2.3f, 0.30f);
    bumper(md, hw, 0.32f, 0.48f, -2.34f, 0xE9E7F0);
}

static void build_bus(Model *md) {
    float hw = 1.25f;
    wheel(md, -hw + 0.14f, -2.9f, 0.50f, 0.46f); wheel(md, hw - 0.14f, -2.9f, 0.50f, 0.46f);
    wheel(md, -hw + 0.14f, 3.0f, 0.50f, 0.46f);  wheel(md, hw - 0.14f, 3.0f, 0.50f, 0.46f);
    body_oct(md, ROLE_BODY, 0, hw, 0.36f, 3.05f, -4.6f, 4.6f, 0.16f);
    for (int s = -1; s <= 1; s += 2) {
        decal_x(md, ROLE_ACC, 0, MAT_SOLID, (float)s * (hw + 0.004f), -4.6f, 4.6f, 0.44f, 0.95f);
        decal_x(md, ROLE_FIX, 0x2B3352, MAT_SOLID, (float)s * (hw + 0.004f), -4.3f, 4.3f, 1.35f, 2.55f);
        for (int k = 0; k < 6; k++)
            decal_x(md, ROLE_FIX, 0xA8D8FF, MAT_EMIT, (float)s * (hw + 0.006f), -4.0f + (float)k * 1.4f,
                    -4.0f + (float)k * 1.4f + 0.10f, 1.40f, 2.50f);
    }
    decal_z(md, ROLE_FIX, 0x2B3352, MAT_SOLID, -4.605f, -hw + 0.2f, hw - 0.2f, 1.45f, 2.55f);
    decal_z(md, ROLE_FIX, 0xFFFFFF, MAT_EMIT, -4.61f, -0.95f, -0.75f, 1.55f, 2.45f);
    decal_z(md, ROLE_FIX, 0xFFFFFF, MAT_EMIT, -4.61f, -0.55f, -0.45f, 1.55f, 2.45f);
    decal_z(md, ROLE_FIX, 0x2B3352, MAT_SOLID, 4.605f, -hw + 0.15f, hw - 0.15f, 1.20f, 2.70f);
    decal_z(md, ROLE_FIX, 0x26232E, MAT_SOLID, -4.605f, -0.8f, 0.8f, 2.68f, 2.94f);
    decal_z(md, ROLE_FIX, 0x7CFF6A, MAT_EMIT, -4.61f, -0.7f, 0.1f, 2.74f, 2.88f);
    lamps_rear(md, hw, 0.70f, 1.05f, -4.6f, 0.26f);
    lamps_front(md, hw, 0.70f, 0.95f, 4.6f, 0.34f);
    box(md, ROLE_FIX, 0xD8DCE6, 4, MAT_SOLID, 0.0f, 3.05f, 0.5f, 1.6f, 0.26f, 2.4f);
}

static void build_boxtruck(Model *md) {
    float hw = 1.18f;
    wheels4(md, hw - 0.14f, 2.3f, -2.1f, 0.48f, 0.46f);
    box(md, ROLE_FIX, 0x34323E, 1, MAT_SOLID, 0.0f, 0.45f, 0.0f, 2.0f, 0.30f, 6.6f);
    body_oct(md, ROLE_BODY, 0, hw, 0.55f, 1.55f, 1.70f, 3.4f, 0.10f);
    cabin(md, ROLE_BODY, 0, hw * 0.96f, hw * 0.90f, 1.55f, 2.40f, 1.70f, 3.10f, 1.70f, 2.70f);
    box(md, ROLE_FIX, 0xF4F3F8, 2, MAT_SOLID, 0.0f, 0.75f, -1.10f, hw * 2.08f, 2.55f, 5.2f);
    for (int s = -1; s <= 1; s += 2)
        decal_x(md, ROLE_BODY, 0, MAT_SOLID, (float)s * (hw * 1.04f + 0.004f), -3.4f, 1.2f, 1.0f, 1.25f);
    decal_z(md, ROLE_FIX, 0xD9D6E2, MAT_SOLID, -3.705f, -0.03f, 0.03f, 0.85f, 3.2f);
    lamps_rear(md, hw * 1.04f, 0.85f, 1.15f, -3.7f, 0.24f);
    lamps_front(md, hw, 0.85f, 1.05f, 3.4f, 0.30f);
    bumper(md, hw, 0.45f, 0.62f, -3.74f, 0x3B3F58);
}

static void build_fire(Model *md) {
    float hw = 1.18f;
    wheels4(md, hw - 0.14f, 2.0f, -1.9f, 0.48f, 0.46f);
    body_oct(md, ROLE_BODY, 0, hw, 0.45f, 1.55f, -3.1f, 3.1f, 0.12f);
    cabin(md, ROLE_BODY, 0, hw * 0.96f, hw * 0.92f, 1.55f, 2.45f, 1.30f, 2.90f, 1.30f, 2.50f);
    box(md, ROLE_BODY, 0, 2, MAT_SOLID, 0.0f, 1.55f, -1.0f, hw * 2.0f, 0.70f, 4.2f);
    for (int s = -1; s <= 1; s += 2) {
        decal_x(md, ROLE_ACC, 0, MAT_SOLID, (float)s * (hw + 0.004f), -3.1f, 3.1f, 0.95f, 1.12f);
        box(md, ROLE_ACC, 0, 4, MAT_SOLID, (float)s * 0.55f, 2.25f, -1.0f, 0.10f, 0.10f, 4.4f);
    }
    for (int k = 0; k < 8; k++) box(md, ROLE_ACC, 0, 4, MAT_SOLID, 0.0f, 2.27f, -3.0f + (float)k * 0.58f, 1.1f, 0.06f, 0.08f);
    box(md, ROLE_FIX, 0xFF2E48, 4, MAT_EMIT, -0.35f, 2.45f, 2.1f, 0.6f, 0.16f, 0.26f);
    box(md, ROLE_FIX, 0x2E7BFF, 4, MAT_EMIT, 0.35f, 2.45f, 2.1f, 0.6f, 0.16f, 0.26f);
    lamps_rear(md, hw, 1.00f, 1.35f, -3.1f, 0.26f);
    lamps_front(md, hw, 0.90f, 1.10f, 3.1f, 0.30f);
    bumper(md, hw, 0.45f, 0.62f, -3.14f, 0xE9E7F0);
}

static void build_cone(Model *md) {
    box(md, ROLE_FIX, 0xF0A21A, 1, MAT_SOLID, 0.0f, 0.0f, 0.0f, 0.82f, 0.10f, 0.82f);
    hexa(md, ROLE_FIX, 0xFFC21A, 2, MAT_SOLID, -0.30f, 0.30f, -0.30f, 0.30f, 0.10f, -0.21f, 0.21f, -0.21f, 0.21f, 0.48f);
    hexa(md, ROLE_FIX, 0xFFFFFF, 3, MAT_SOLID, -0.21f, 0.21f, -0.21f, 0.21f, 0.48f, -0.155f, 0.155f, -0.155f, 0.155f, 0.68f);
    hexa(md, ROLE_FIX, 0xFFC21A, 4, MAT_SOLID, -0.155f, 0.155f, -0.155f, 0.155f, 0.68f, -0.06f, 0.06f, -0.06f, 0.06f, 1.0f);
}

static void build_tree(Model *md) {
    box(md, ROLE_FIX, 0x8A5A34, 1, MAT_SOLID, 0.0f, 0.0f, 0.0f, 0.40f, 1.3f, 0.40f);
    box(md, ROLE_BODY, 0, 2, MAT_SOLID, 0.0f, 1.0f, 0.0f, 2.0f, 1.55f, 2.0f);
    box(md, ROLE_BODY, 0, 3, MAT_SOLID, 0.0f, 2.55f, 0.0f, 1.25f, 0.80f, 1.25f);
}

/** Built for the right-hand verge, arm reaching left over the road; the left
    verge uses it turned half a circle. */
static void build_lamp(Model *md) {
    box(md, ROLE_FIX, 0xB9BACB, 1, MAT_SOLID, 0.0f, 0.0f, 0.0f, 0.22f, 7.6f, 0.22f);
    box(md, ROLE_FIX, 0xB9BACB, 2, MAT_SOLID, -1.45f, 7.45f, 0.0f, 2.9f, 0.16f, 0.16f);
    box(md, ROLE_ACC, 0, 3, MAT_EMIT, -2.75f, 7.18f, 0.0f, 1.25f, 0.24f, 0.42f);
}

static void build_board(Model *md) {
    box(md, ROLE_FIX, 0x8E90A6, 1, MAT_SOLID, -1.6f, 0.0f, 0.0f, 0.24f, 3.0f, 0.24f);
    box(md, ROLE_FIX, 0x8E90A6, 1, MAT_SOLID, 1.6f, 0.0f, 0.0f, 0.24f, 3.0f, 0.24f);
    box(md, ROLE_FIX, 0xF4F3F8, 2, MAT_SOLID, 0.0f, 2.6f, 0.0f, 5.6f, 3.2f, 0.30f);
}

/** The finish gate at the end of each level: two big yellow towers and a
    chequered beam across the road. */
static void build_gate(Model *md) {
    float px = ROAD_HW + 2.9f;
    for (int s = -1; s <= 1; s += 2) {
        box(md, ROLE_FIX, 0xFFC21A, 1, MAT_SOLID, (float)s * px, 0.0f, 0.0f, 3.2f, 17.0f, 3.2f);
        for (int r = 0; r < 4; r++)
            decal_z(md, ROLE_FIX, 0xB0700A, MAT_SOLID, -1.605f, (float)s * px - 0.6f, (float)s * px + 0.6f,
                    3.0f + (float)r * 3.2f, 4.4f + (float)r * 3.2f);
    }
    box(md, ROLE_FIX, 0xFFC21A, 2, MAT_SOLID, 0.0f, 12.0f, 0.0f, px * 2.0f, 3.2f, 2.4f);
    for (int k = 0; k < 12; k++)
        for (int r = 0; r < 2; r++) {
            float x0 = -px + 1.6f + (float)k * ((px * 2.0f - 3.2f) / 12.0f);
            float x1 = x0 + (px * 2.0f - 3.2f) / 12.0f;
            decal_z(md, ROLE_FIX, ((k + r) & 1) ? 0x26232E : 0xFFFFFF, MAT_SOLID, -1.205f, x0, x1,
                    12.4f + (float)r * 1.2f, 13.6f + (float)r * 1.2f);
        }
}

static void models_init(void) {
    if (g_models_ready) return;
    g_models_ready = 1;
    build_learner(&g_model[VM_LEARNER], 1);
    build_police(&g_model[VM_POLICE]);
    build_muscle(&g_model[VM_MUSCLE]);
    build_icecream(&g_model[VM_ICECREAM]);
    build_taxi(&g_model[VM_TAXI]);
    build_hover(&g_model[VM_HOVER]);
    build_tractor(&g_model[VM_TRACTOR]);
    build_f1(&g_model[VM_F1]);
    build_hippie(&g_model[VM_HIPPIE]);
    build_kart(&g_model[VM_KART]);
    build_sports(&g_model[VM_SPORTS]);
    build_mint(&g_model[VM_MINT]);
    build_monster(&g_model[VM_MONSTER]);
    build_pickup(&g_model[VM_TOW], 1);
    build_bus(&g_model[VM_BUS]);
    build_fire(&g_model[VM_FIRE]);
    build_sedan(&g_model[VM_SEDAN]);
    build_hatch(&g_model[VM_HATCH]);
    build_van(&g_model[VM_VAN]);
    build_boxtruck(&g_model[VM_BOXTRUCK]);
    build_pickup(&g_model[VM_PICKUP], 0);
    build_cone(&g_model[VM_CONE]);
    build_tree(&g_model[VM_TREE]);
    build_lamp(&g_model[VM_LAMP]);
    build_board(&g_model[VM_BOARD]);
    build_gate(&g_model[VM_GATE]);
    for (int i = 0; i < VM_COUNT; i++) model_bounds(&g_model[i]);
}

/* -------------------------------------------------------------- renderer -- */
typedef struct {
    PA_Color body, acc, glass, fogc;
    float    fog, alpha;
    int      sil;            /* garage silhouette: one flat colour */
    PA_Color silc;
} Look;

static V3 g_light = { 0.5f, 0.62f, 0.6f };

static PA_Color lit(PA_Color base, V3 n) {
    float d = vdot(n, g_light);
    float s = -0.07f + 0.27f * (d > 0.0f ? d : 0.0f) - 0.15f * (d < 0.0f ? -d : 0.0f) + 0.06f * (n.y > 0.0f ? n.y : 0.0f);
    return pa_shade(base, s);
}

static PA_Vec2 bil(const PA_Vec2 *q, float u, float v) {
    PA_Vec2 a, b, r;
    a.x = q[0].x + (q[1].x - q[0].x) * u; a.y = q[0].y + (q[1].y - q[0].y) * u;
    b.x = q[3].x + (q[2].x - q[3].x) * u; b.y = q[3].y + (q[2].y - q[3].y) * u;
    r.x = a.x + (b.x - a.x) * v; r.y = a.y + (b.y - a.y) * v;
    return r;
}

/* A window: the face is painted in the body colour as its frame, the glass is
   inset inside it, and two diagonal glare bars cross it - the toy cars'
   single most recognisable detail. */
static void glass_face(PA_Canvas *c, const PA_Vec2 *poly, int n, V3 nw, V3 to, const Look *lk) {
    PA_Color g = lit(lk->glass, nw);
    if (nw.y > 0.3f) g = pa_shade(g, 0.10f);
    if (lk->fog > 0.0f) g = pa_mix(g, lk->fogc, lk->fog);
    g = amul(g, lk->alpha);
    if (n == 4) {
        PA_Vec2 in[4] = { bil(poly, 0.09f, 0.10f), bil(poly, 0.91f, 0.10f), bil(poly, 0.91f, 0.88f), bil(poly, 0.09f, 0.88f) };
        fpoly(c, in, 4, g);
        float facing = vdot(nw, vnorm(to));
        if (facing > 0.22f && lk->fog < 0.6f) {
            PA_Color w = PA_RGBA(255, 255, 255, (int)(150.0f * lk->alpha * (1.0f - lk->fog)));
            PA_Vec2 s1[4] = { bil(in, 0.10f, 0.0f), bil(in, 0.30f, 0.0f), bil(in, 0.62f, 1.0f), bil(in, 0.42f, 1.0f) };
            PA_Vec2 s2[4] = { bil(in, 0.40f, 0.0f), bil(in, 0.48f, 0.0f), bil(in, 0.80f, 1.0f), bil(in, 0.72f, 1.0f) };
            fpoly(c, s1, 4, w);
            fpoly(c, s2, 4, w);
        }
    } else {
        PA_Vec2 ctr = { 0, 0 }, in[10];
        for (int i = 0; i < n; i++) { ctr.x += poly[i].x; ctr.y += poly[i].y; }
        ctr.x /= (float)n; ctr.y /= (float)n;
        for (int i = 0; i < n; i++) { in[i].x = ctr.x + (poly[i].x - ctr.x) * 0.80f; in[i].y = ctr.y + (poly[i].y - ctr.y) * 0.80f; }
        fpoly(c, in, n, g);
    }
}

static void draw_mesh(PA_Canvas *c, const Cam *k, const Mesh *m, V3 pos, const M3 *R, float sc, const Look *lk) {
    PA_Vec2 sp[MV];
    V3 wv[MV];
    for (int i = 0; i < m->nv; i++) {
        wv[i] = vadd(pos, m3v(R, vmul(m->v[i], sc)));
        if (!cam_proj(k, wv[i], &sp[i])) return;
    }
    PA_Color base = m->role == ROLE_BODY ? lk->body : m->role == ROLE_ACC ? lk->acc : pa_hex(m->color);
    for (int f = 0; f < m->nf; f++) {
        int mat = m->fm[f];
        if (mat == MAT_HIDE) continue;
        V3 nw = m3v(R, m->n[f]);
        V3 to = vsub(k->pos, wv[m->fi[f][0]]);
        if (vdot(nw, to) <= 0.0f) continue;
        PA_Vec2 poly[10];
        int fn = m->fn[f];
        for (int i = 0; i < fn; i++) poly[i] = sp[m->fi[f][i]];
        if (lk->sil) { fpoly(c, poly, fn, lk->silc); continue; }
        PA_Color col = mat == MAT_EMIT ? base : lit(base, nw);
        if (lk->fog > 0.0f) col = pa_mix(col, lk->fogc, lk->fog);
        fpoly(c, poly, fn, amul(col, lk->alpha));
        if (mat == MAT_GLASS) glass_face(c, poly, fn, nw, to, lk);
    }
}

static void draw_model(PA_Canvas *c, const Cam *k, const Model *md, V3 pos, const M3 *R, float sc, const Look *lk) {
    /* Wheels, then the body, then everything else in layer order, each later
       layer sorted far to near. Fixed layers keep the body from ever being
       painted over its own wheels' far halves; sorting the rest keeps a
       spoiler behind the cabin when the car is seen from the front. */
    int order[MM], n = 0;
    for (int L0 = 0; L0 <= 4; L0++) {
        int start = n;
        for (int i = 0; i < md->n; i++) if (md->m[i].layer == L0) order[n++] = i;
        if (L0 >= 2 && n - start > 1) {
            float d[MM];
            for (int i = start; i < n; i++) {
                float x, y;
                d[i] = cam_space(k, vadd(pos, m3v(R, vmul(md->m[order[i]].ctr, sc))), &x, &y);
            }
            for (int i = start + 1; i < n; i++) {
                int key = order[i];
                float kd = d[i];
                int j = i - 1;
                while (j >= start && d[j] < kd) { order[j + 1] = order[j]; d[j + 1] = d[j]; j--; }
                order[j + 1] = key; d[j + 1] = kd;
            }
        }
    }
    for (int i = 0; i < n; i++) draw_mesh(c, k, &md->m[order[i]], pos, R, sc, lk);
}

/* Monotone-chain hull, used for projected shadows. */
static int hull2(float (*p)[2], int n, float (*out)[2]) {
    for (int i = 1; i < n; i++) {
        float a = p[i][0], b = p[i][1];
        int j = i - 1;
        while (j >= 0 && (p[j][0] > a || (p[j][0] == a && p[j][1] > b))) { p[j + 1][0] = p[j][0]; p[j + 1][1] = p[j][1]; j--; }
        p[j + 1][0] = a; p[j + 1][1] = b;
    }
    int k = 0;
    for (int i = 0; i < n; i++) {
        while (k >= 2 && (out[k - 1][0] - out[k - 2][0]) * (p[i][1] - out[k - 2][1]) -
                         (out[k - 1][1] - out[k - 2][1]) * (p[i][0] - out[k - 2][0]) <= 0.0f) k--;
        out[k][0] = p[i][0]; out[k][1] = p[i][1]; k++;
    }
    for (int i = n - 2, t = k + 1; i >= 0; i--) {
        while (k >= t && (out[k - 1][0] - out[k - 2][0]) * (p[i][1] - out[k - 2][1]) -
                         (out[k - 1][1] - out[k - 2][1]) * (p[i][0] - out[k - 2][0]) <= 0.0f) k--;
        out[k][0] = p[i][0]; out[k][1] = p[i][1]; k++;
    }
    return k - 1;
}

/** Hard cast shadow (the bounding box swept along the light onto the road)
    plus a darker contact patch under the footprint. */
static void draw_shadow(PA_Canvas *c, const Cam *k, const Model *md, V3 pos, const M3 *R, float sc,
                        PA_Color col, float lift) {
    float hw = md->wid * 0.5f * sc, hl = md->len * 0.5f * sc, h = md->hgt * sc * 0.85f;
    float pts[8][2], hull[18][2];
    int n = 0;
    float gx = g_light.y > 0.15f ? g_light.x / g_light.y : 0.0f, gz = g_light.y > 0.15f ? g_light.z / g_light.y : 0.0f;
    for (int i = 0; i < 8; i++) {
        V3 l = v3((i & 1) ? hw : -hw, (i & 4) ? h : 0.0f, (i & 2) ? hl : -hl);
        V3 w = vadd(pos, m3v(R, l));
        float y = w.y > 0.0f ? w.y : 0.0f;
        pts[n][0] = w.x - gx * y;
        pts[n][1] = w.z - gz * y;
        n++;
    }
    int hn = hull2(pts, n, hull);
    PA_Vec2 sp[18];
    int ok = 1;
    for (int i = 0; i < hn && ok; i++) ok = cam_proj(k, v3(hull[i][0], 0.01f, hull[i][1]), &sp[i]);
    float fade = 1.0f / (1.0f + lift * 0.6f);
    if (ok && hn >= 3) fpoly(c, sp, hn, amul(col, fade));
    if (lift < 0.3f) {
        PA_Vec2 q[4];
        float ex = hw + 0.10f, ez = hl + 0.08f;
        V3 cs[4] = { v3(-ex, 0, -ez), v3(ex, 0, -ez), v3(ex, 0, ez), v3(-ex, 0, ez) };
        for (int i = 0; i < 4 && ok; i++) {
            V3 w = vadd(pos, m3v(R, cs[i]));
            w.y = 0.015f;
            ok = cam_proj(k, w, &q[i]);
        }
        if (ok) fpoly(c, q, 4, amul(col, 0.9f));
    }
}

/* ---------------------------------------------------------------- emoji -- */
enum { EMO_NONE, EMO_ANGRY, EMO_SKULL, EMO_CRY, EMO_SHOCK, EMO_SWEAR, EMO_LAUGH, EMO_COOL, EMO_SWEAT, EMO_COUNT };

static void face_disc(PA_Canvas *c, float x, float y, float r, uint32_t top, uint32_t bot) {
    PA_Paint p = pa_linear(x, y - r, x, y + r);
    pa_stop(&p, 0.0f, pa_hex(top));
    pa_stop(&p, 1.0f, pa_hex(bot));
    fell_paint(c, x, y, r, r, &p);
    fell(c, x - r * 0.30f, y - r * 0.45f, r * 0.34f, r * 0.18f, PA_RGBA(255, 255, 255, 70));
}

/* Our own little cast of drivers. Drawn, not typed: the engine font has no
   emoji, and these need to read at thumbnail size over a moving road. */
static void draw_emoji(PA_Canvas *c, float x, float y, float r, int kind) {
    PA_Color ink = pa_hex(0x3A1A10);
    float lw = r * 0.10f > 1.2f ? r * 0.10f : 1.2f;
    switch (kind) {
    case EMO_SKULL: {
        PA_Vec2 jaw[4] = { { x - r * 0.42f, y + r * 0.20f }, { x + r * 0.42f, y + r * 0.20f },
                           { x + r * 0.32f, y + r * 0.78f }, { x - r * 0.32f, y + r * 0.78f } };
        fpoly(c, jaw, 4, pa_hex(0xF2F2F6));
        fell(c, x, y - r * 0.12f, r * 0.80f, r * 0.70f, pa_hex(0xFFFFFF));
        fell(c, x, y + r * 0.10f, r * 0.70f, r * 0.40f, pa_hex(0xF2F2F6));
        fell(c, x - r * 0.33f, y - r * 0.05f, r * 0.24f, r * 0.27f, pa_hex(0x26232E));
        fell(c, x + r * 0.33f, y - r * 0.05f, r * 0.24f, r * 0.27f, pa_hex(0x26232E));
        PA_Vec2 nose[3] = { { x, y + r * 0.18f }, { x - r * 0.10f, y + r * 0.36f }, { x + r * 0.10f, y + r * 0.36f } };
        fpoly(c, nose, 3, pa_hex(0x26232E));
        for (int i = -1; i <= 1; i++) sline(c, x + (float)i * r * 0.16f, y + r * 0.50f, x + (float)i * r * 0.16f, y + r * 0.74f, lw * 0.7f, pa_hex(0x9A98A8));
        break;
    }
    case EMO_ANGRY:
    case EMO_SWEAR: {
        face_disc(c, x, y, r, 0xFF5A3A, 0xFFB43A);
        sline(c, x - r * 0.55f, y - r * 0.38f, x - r * 0.12f, y - r * 0.18f, lw * 1.3f, ink);
        sline(c, x + r * 0.55f, y - r * 0.38f, x + r * 0.12f, y - r * 0.18f, lw * 1.3f, ink);
        fell(c, x - r * 0.30f, y - r * 0.02f, r * 0.10f, r * 0.13f, ink);
        fell(c, x + r * 0.30f, y - r * 0.02f, r * 0.10f, r * 0.13f, ink);
        if (kind == EMO_ANGRY) {
            arc(c, x, y + r * 0.62f, r * 0.34f, PA_PI * 1.15f, PA_PI * 1.85f, lw * 1.2f, ink);
        } else {
            rrect(c, x - r * 0.62f, y + r * 0.22f, r * 1.24f, r * 0.42f, r * 0.08f, pa_hex(0x1A1420));
            txt(c, "!%?!", x, y + r * 0.27f, r * 0.30f, PA_RGB(255, 255, 255), PA_ALIGN_CENTER);
        }
        break;
    }
    case EMO_CRY: {
        face_disc(c, x, y, r, 0xFFD64A, 0xFFA830);
        arc(c, x - r * 0.30f, y - r * 0.02f, r * 0.16f, PA_PI * 1.1f, PA_PI * 1.9f, lw, ink);
        arc(c, x + r * 0.30f, y - r * 0.02f, r * 0.16f, PA_PI * 1.1f, PA_PI * 1.9f, lw, ink);
        fell(c, x, y + r * 0.42f, r * 0.26f, r * 0.20f, pa_hex(0x6A2A1A));
        fell(c, x, y + r * 0.50f, r * 0.15f, r * 0.08f, pa_hex(0xFF7A8A));
        rrect(c, x - r * 0.40f, y + r * 0.02f, r * 0.18f, r * 0.80f, r * 0.08f, PA_RGBA(80, 170, 255, 230));
        rrect(c, x + r * 0.22f, y + r * 0.02f, r * 0.18f, r * 0.80f, r * 0.08f, PA_RGBA(80, 170, 255, 230));
        break;
    }
    case EMO_SHOCK: {
        face_disc(c, x, y, r, 0xFFD64A, 0xFFA830);
        PA_Paint p = pa_linear(x, y - r, x, y);
        pa_stop(&p, 0.0f, PA_RGBA(90, 120, 255, 200));
        pa_stop(&p, 1.0f, PA_RGBA(90, 120, 255, 0));
        fell_paint(c, x, y - r * 0.35f, r * 0.80f, r * 0.62f, &p);
        fcirc(c, x - r * 0.30f, y - r * 0.08f, r * 0.20f, PA_RGB(255, 255, 255));
        fcirc(c, x + r * 0.30f, y - r * 0.08f, r * 0.20f, PA_RGB(255, 255, 255));
        fcirc(c, x - r * 0.30f, y - r * 0.08f, r * 0.08f, ink);
        fcirc(c, x + r * 0.30f, y - r * 0.08f, r * 0.08f, ink);
        fell(c, x, y + r * 0.45f, r * 0.16f, r * 0.22f, pa_hex(0x6A2A1A));
        break;
    }
    case EMO_LAUGH: {
        face_disc(c, x, y, r, 0xFFD64A, 0xFFA830);
        arc(c, x - r * 0.30f, y - r * 0.10f, r * 0.15f, PA_PI * 1.1f, PA_PI * 1.9f, lw, ink);
        arc(c, x + r * 0.30f, y - r * 0.10f, r * 0.15f, PA_PI * 1.1f, PA_PI * 1.9f, lw, ink);
        PA_Vec2 m[16];
        int k = 0;
        m[k].x = x - r * 0.48f; m[k].y = y + r * 0.18f; k++;
        m[k].x = x + r * 0.48f; m[k].y = y + r * 0.18f; k++;
        for (int i = 1; i < 12; i++) {
            float a = (float)i / 12.0f * PA_PI;
            m[k].x = x + cosf(a) * r * 0.48f; m[k].y = y + r * 0.18f + sinf(a) * r * 0.44f; k++;
        }
        fpoly(c, m, k, pa_hex(0x6A2A1A));
        fell(c, x, y + r * 0.46f, r * 0.24f, r * 0.12f, pa_hex(0xFF7A8A));
        rrect(c, x - r * 0.40f, y + r * 0.22f, r * 0.80f, r * 0.10f, r * 0.04f, PA_RGB(255, 255, 255));
        break;
    }
    case EMO_COOL: {
        face_disc(c, x, y, r, 0xFFD64A, 0xFFA830);
        rrect(c, x - r * 0.66f, y - r * 0.24f, r * 0.58f, r * 0.34f, r * 0.14f, pa_hex(0x1A1420));
        rrect(c, x + r * 0.08f, y - r * 0.24f, r * 0.58f, r * 0.34f, r * 0.14f, pa_hex(0x1A1420));
        sline(c, x - r * 0.1f, y - r * 0.16f, x + r * 0.1f, y - r * 0.16f, lw, pa_hex(0x1A1420));
        arc(c, x, y + r * 0.22f, r * 0.34f, PA_PI * 0.2f, PA_PI * 0.8f, lw * 1.2f, ink);
        break;
    }
    case EMO_SWEAT:
    default: {
        face_disc(c, x, y, r, 0xFFD64A, 0xFFA830);
        fell(c, x - r * 0.30f, y - r * 0.05f, r * 0.09f, r * 0.14f, ink);
        fell(c, x + r * 0.30f, y - r * 0.05f, r * 0.09f, r * 0.14f, ink);
        sline(c, x - r * 0.30f, y + r * 0.42f, x + r * 0.30f, y + r * 0.36f, lw * 1.2f, ink);
        PA_Vec2 d[3] = { { x + r * 0.62f, y - r * 0.62f }, { x + r * 0.50f, y - r * 0.30f }, { x + r * 0.74f, y - r * 0.30f } };
        fpoly(c, d, 3, pa_hex(0x6AC0FF));
        fcirc(c, x + r * 0.62f, y - r * 0.30f, r * 0.12f, pa_hex(0x6AC0FF));
        break;
    }
    }
}

/** Speech bubble on a driver: white ring, coloured disc, the face, a tail. */
static void draw_bubble(PA_Canvas *c, float x, float y, float r, int kind, float a) {
    if (r < 3.0f || a <= 0.01f) return;
    static const uint32_t BG[EMO_COUNT] = { 0, 0xFF8A3C, 0x3BA0F0, 0x7A6CFF, 0x5AD0FF, 0xFF4F7A, 0x47D78C, 0xFFB43A, 0x8AD8FF };
    PA_Vec2 tail[3] = { { x - r * 0.30f, y + r * 0.70f }, { x + r * 0.30f, y + r * 0.70f }, { x, y + r * 1.32f } };
    fcirc(c, x + r * 0.06f, y + r * 0.10f, r * 1.02f, PA_RGBA(20, 10, 40, (int)(70 * a)));
    fpoly(c, tail, 3, amul(PA_RGB(255, 255, 255), a));
    fcirc(c, x, y, r, amul(PA_RGB(255, 255, 255), a));
    fcirc(c, x, y, r * 0.84f, amul(pa_hex(BG[kind]), a));
    if (a > 0.6f) draw_emoji(c, x, y + r * 0.04f, r * 0.66f, kind);
}

/* --------------------------------------------------------------- biomes -- */
enum { B_SUNSET, B_DAY, B_NIGHT, BIOME_COUNT };

typedef struct {
    const char *name;
    uint32_t sky_top, sky_mid, sky_low, glow;
    uint32_t ground, verge, road, road_far, dash, edge, rail, glass, skyline, fog, leaf, lamp;
    float    lx, ly, lz;
    float    shadow_a;
} Biome;

/* Measured off the plates: violet night with a big moon and a hot horizon,
   a red-to-peach sunset over green fields, a saturated blue day of glass
   towers. Night road is purple, day road lavender, sunset road grey. */
static const Biome BIOMES[BIOME_COUNT] = {
    { "SUNSET",  0xC0135A, 0xF2243E, 0xFF9A6A, 0xFFE0A0,
      0x7CC84A, 0x5FB23E, 0xA9A8BA, 0xC4BFCC, 0xFFFFFF, 0xFFD23A, 0xF2F0F6, 0xFF86A8, 0x3F9A3A, 0xFFB49A, 0x46B04A, 0xFFE9B0,
      -0.35f, 0.55f, 0.76f, 0.30f },
    { "DAYTIME", 0x1B58E0, 0x3C8CF5, 0xC4E6FF, 0xFFFFFF,
      0x5CC84A, 0x4AB43E, 0xC3C6EC, 0xD8DBF4, 0xFFFFFF, 0xFFD23A, 0xFFFFFF, 0x2E8FEF, 0x2E6BEA, 0xD6ECFF, 0x3FB04A, 0xFFFFFF,
      0.30f, 0.86f, 0.40f, 0.24f },
    { "NIGHT",   0x170A58, 0x3A1290, 0xB03BD8, 0xFF8A3C,
      0x1F6E6A, 0x1A5A5A, 0x8C46DC, 0xA060E6, 0xF0DCFF, 0xFFB02E, 0xD6B8FF, 0x6A3AD0, 0x3A1670, 0x9A48D8, 0x2E8A6A, 0xCFE6FF,
      0.55f, 0.60f, 0.58f, 0.36f },
};

static int biome_of_level(int level) { return (level + 1) % BIOME_COUNT; }

/* ---------------------------------------------------------------- rides -- */
typedef struct { int model; const char *name; int price; uint32_t body, acc; } Ride;

static const Ride RIDES[] = {
    { VM_LEARNER,  "LEARNER",    0,    0xE8283C, 0xFFFFFF },
    { VM_POLICE,   "PATROL",     150,  0x1E1F2C, 0xF6F6FA },
    { VM_MUSCLE,   "BUMBLE GT",  300,  0xFFD21E, 0x1E1F2C },
    { VM_ICECREAM, "SOFT SERVE", 450,  0x57B6F5, 0xFF8FC8 },
    { VM_TAXI,     "CABBIE",     600,  0xFFC21A, 0x1E1F2C },
    { VM_HOVER,    "HOVERBOAT",  800,  0xFFC21A, 0x2A2732 },
    { VM_TRACTOR,  "FARMHAND",   1000, 0x3CC24E, 0xF6F6FA },
    { VM_F1,       "PIT ROCKET", 1250, 0x2D6BFF, 0xF6F6FA },
    { VM_HIPPIE,   "FLOWER BUS", 1500, 0x7ED957, 0xFF5A3C },
    { VM_KART,     "GO KART",    1800, 0xE8283C, 0x2D6BFF },
    { VM_SPORTS,   "ROSSO",      2100, 0xE3242B, 0x1E1F2C },
    { VM_MINT,     "MINT DROP",  2500, 0x7FE3D2, 0xFFFFFF },
    { VM_MONSTER,  "BIGFOOT",    3000, 0x8B3DF0, 0xFFD21E },
    { VM_TOW,      "HOOK",       3500, 0xFF9A1E, 0x2A2732 },
    { VM_BUS,      "CITY BUS",   4000, 0xF2F4F8, 0x39C26B },
    { VM_FIRE,     "BLAZE",      5000, 0xE5262E, 0xF6F6FA },
};
#define RIDE_COUNT ((int)(sizeof(RIDES) / sizeof(RIDES[0])))

static const uint32_t TRAFFIC_PAL[] = { 0x3E7FF0, 0xF6C340, 0xE8455F, 0xF2F2F6, 0x9B5BE8, 0xFF8A2E, 0x3FC8C0, 0xC8CAD6, 0x57D06A };
#define TPAL_N ((int)(sizeof(TRAFFIC_PAL) / sizeof(TRAFFIC_PAL[0])))

/* ----------------------------------------------------------------- state -- */
enum { ST_MENU, ST_PLAY, ST_CRASH, ST_RESULTS, ST_GARAGE };
enum { CK_CAR, CK_CONE };
enum { P_TREE, P_LAMP, P_TOWER, P_BLOCK, P_HOUSE, P_BOARD, P_BUSH };
enum { PT_SMOKE, PT_FIRE, PT_SPARK, PT_CONFETTI, PT_BOOM };

typedef struct {
    int      kind, model, lane, near_done, angry_done, flying, emo;
    PA_Color body, acc;
    float    x, y, z, speed, cruise;
    V3       v;
    float    yaw, pitch, roll, wy, wp, wr;
    float    emo_t;
} Car;

typedef struct { int kind, biome; float x, z, w, h, d; uint32_t seed; } Prop;
typedef struct { float x, z; int spin; } Coin;
typedef struct { V3 p, v; float t, life, r, grow; PA_Color col; int kind; } Part;
typedef struct { float x, y, tx, ty, t, life; int kind; char txt[16]; } Pop;
typedef struct { float t, life, r0, r1; PA_Color col; int attach; float x, y; } Ring;

static struct {
    int    state;
    float  st_t, time;
    PA_Rng rng;
    /* level + world */
    int    level, biome, nbiome, sky_from;
    float  lvl_z0, lvl_len, gate_z, sky_mix;
    /* player */
    float  z, x, vx, speed, run_z0, top;
    int    lane, prev_lane;
    float  lane_t, yaw, roll, cam_roll, camx, camz;
    float  nitro, nitro_t, nitro_k;
    float  drag_x;
    int    drag_live;
    /* crash */
    V3     pp, pv;
    float  pyaw, ppitch, proll, wyaw, wpitch, wroll;
    float  crash_t, ts, shake, flash;
    V3     impact;
    uint32_t crack_seed;
    int    ko_sfx;
    /* things */
    Car    car[MAX_CARS];   int ncar;
    Prop   prop[MAX_PROPS]; int nprop;
    float  prop_z[2], lamp_z;
    int    lamp_side;
    Coin   coin[MAX_COINS]; int ncoin;
    Part   part[MAX_PARTS]; int npart;
    Pop    pop[MAX_POPS];   int npop;
    Ring   ring[MAX_RINGS]; int nring;
    float  row_z, traffic_v;
    int    open_lane;
    /* run tallies */
    int    run_coins, run_near, run_smash, combo;
    float  combo_t;
    char   banner[40];
    float  banner_t;
    PA_Color banner_col;
    float  smoke_acc, flame_acc, engine_acc;
    float  touch_x, touch_y, touch_t;
    int    new_best, earned, pre_best;
    float  coin_pulse;
    /* garage */
    float  gscroll, gflash, gdeny_t;
    int    gflash_i, gdeny_i, gdrag;
    float  gdrag_y, gdrag_travel;
    /* demo bot */
    float  bot_t, play_t;
    int    demo_nitro_done;
} R;

static struct { int best, level, coins, car, own, time_s, runs, loaded; } S;

static struct { int w, h; float f, cx, hy, ui; } L;

static const Model *my_model(void) { return &g_model[RIDES[S.car].model]; }
static float my_len(void) { return my_model()->len; }
static float my_wid(void) { return my_model()->wid; }

static float lane_x(int l) { return -ROAD_HW + LANE_W * ((float)l + 0.5f); }
static float lvl_length(int level) { float l = 560.0f + 6.0f * (float)level; return l > 900.0f ? 900.0f : l; }

static void compute_layout(int w, int h) {
    L.w = w; L.h = h;
    L.cx = (float)w * 0.5f;
    int land = w > h;
    L.hy = (float)h * (land ? 0.40f : 0.435f);
    float car_y = (float)h * (land ? 0.90f : 0.875f);
    float dz = CAM_BACK - 1.95f;
    L.f = (car_y - L.hy) * dz / CAM_H;
    if (L.f > (float)w * 1.25f) L.f = (float)w * 1.25f;
    float a = (float)w / 540.0f, b = (float)h / 960.0f;
    L.ui = a < b ? a : b;
    if (land) L.ui = (float)h / 760.0f;
}

/* ------------------------------------------------------------------ save -- */
static int demo(void) { return pa_demo_mode(); }

static void save_load(void) {
    if (S.loaded) return;
    S.loaded = 1;
    if (demo()) {
        /* A fixed, mid-progress profile so every review capture is identical
           whatever save file happens to sit beside the binary. */
        S.best = 2140; S.level = demo() == 3 ? 36 : 34; S.coins = 1320; S.car = 0;
        S.own = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 10) | (1 << 11);
        S.time_s = 4 * 3600 + 52 * 60; S.runs = 61;
        return;
    }
    S.best = pa_save_get("chromerush.best", 0);
    S.level = pa_save_get("chromerush.level", 1);
    S.coins = pa_save_get("chromerush.coins", 0);
    S.car = pa_save_get("chromerush.car", 0);
    S.own = pa_save_get("chromerush.own", 1) | 1;
    S.time_s = pa_save_get("chromerush.time", 0);
    S.runs = pa_save_get("chromerush.runs", 0);
    if (S.level < 1) S.level = 1;
    if (S.car < 0 || S.car >= RIDE_COUNT || !(S.own & (1 << S.car))) S.car = 0;
}

static void save_store(void) {
    if (demo()) return;
    pa_save_set("chromerush.best", S.best);
    pa_save_set("chromerush.level", S.level);
    pa_save_set("chromerush.coins", S.coins);
    pa_save_set("chromerush.car", S.car);
    pa_save_set("chromerush.own", S.own);
    pa_save_set("chromerush.time", S.time_s);
    pa_save_set("chromerush.runs", S.runs);
    pa_save_flush();
}

/* ----------------------------------------------------------------- sound -- */
static void snd_lane(void)  { pa_noise(0.07f, 0.05f); pa_tone(560.0f, 360.0f, 0.08f, 0, 0.05f); }
static void snd_near(int combo) {
    float k = 1.0f + 0.08f * (float)(combo > 6 ? 6 : combo);
    pa_tone(760.0f * k, 1500.0f * k, 0.12f, 1, 0.10f);
}
static void snd_coin(void)  { pa_tone(1320.0f, 1980.0f, 0.07f, 1, 0.07f); }
static void snd_nitro(void) { pa_tone(110.0f, 560.0f, 0.9f, 3, 0.10f); pa_noise(0.7f, 0.10f); }
static void snd_smash(void) { pa_noise(0.25f, 0.22f); pa_tone(220.0f, 70.0f, 0.25f, 3, 0.14f); }
static void snd_cone(void)  { pa_tone(320.0f, 170.0f, 0.07f, 2, 0.06f); }
static void snd_crash(void) { pa_noise(0.55f, 0.32f); pa_tone(160.0f, 38.0f, 0.55f, 3, 0.20f); }
static void snd_crack(void) { pa_tone(3400.0f, 1700.0f, 0.10f, 2, 0.05f); pa_noise(0.18f, 0.14f); }
static void snd_ko(void)    { pa_tone(523.0f, 523.0f, 0.10f, 2, 0.09f); pa_tone(392.0f, 262.0f, 0.36f, 2, 0.09f); }
static void snd_gate(void)  { pa_sfx("levelup"); pa_tone(660.0f, 1320.0f, 0.25f, 1, 0.08f); }

/* ------------------------------------------------------------- particles -- */
static Part *part_new(int kind, V3 p, V3 v, float life, float r, PA_Color col) {
    if (R.npart >= MAX_PARTS) return NULL;
    Part *q = &R.part[R.npart++];
    q->kind = kind; q->p = p; q->v = v; q->t = 0.0f; q->life = life; q->r = r; q->grow = 0.0f; q->col = col;
    return q;
}

static void pop_text(float x, float y, float tx, float ty, int kind, const char *s) {
    if (R.npop >= MAX_POPS) { memmove(&R.pop[0], &R.pop[1], sizeof(Pop) * (MAX_POPS - 1)); R.npop--; }
    Pop *p = &R.pop[R.npop++];
    p->x = x; p->y = y; p->tx = tx; p->ty = ty; p->t = 0.0f; p->life = 0.85f; p->kind = kind;
    snprintf(p->txt, sizeof(p->txt), "%s", s);
}

static void ring_add(int attach, float x, float y, PA_Color col, float r0, float r1, float life) {
    if (R.nring >= MAX_RINGS) { memmove(&R.ring[0], &R.ring[1], sizeof(Ring) * (MAX_RINGS - 1)); R.nring--; }
    Ring *g = &R.ring[R.nring++];
    g->attach = attach; g->x = x; g->y = y; g->col = col; g->r0 = r0; g->r1 = r1; g->t = 0.0f; g->life = life;
}

static void banner(const char *s, PA_Color col) {
    snprintf(R.banner, sizeof(R.banner), "%s", s);
    R.banner_t = 1.3f;
    R.banner_col = col;
}

static void boom_at(V3 p, int big) {
    int n = big ? 22 : 12;
    for (int i = 0; i < n; i++) {
        float a = pa_rng_range(&R.rng, 0.0f, PA_TAU), s = pa_rng_range(&R.rng, 1.5f, big ? 7.0f : 4.5f);
        V3 v = v3(cosf(a) * s, pa_rng_range(&R.rng, 1.5f, 5.5f), sinf(a) * s * 0.6f + 2.0f);
        static const uint32_t FC[] = { 0xFFE24A, 0xFFB42E, 0xFF7A1E, 0xFFF2A0 };
        Part *q = part_new(i < n * 2 / 3 ? PT_BOOM : PT_SMOKE, vadd(p, v3(0, 0.6f, 0)), v,
                           pa_rng_range(&R.rng, 0.7f, 1.4f), pa_rng_range(&R.rng, 0.45f, big ? 1.1f : 0.7f),
                           i < n * 2 / 3 ? pa_hex(FC[i & 3]) : pa_hex(0xB8B4C8));
        if (q) q->grow = pa_rng_range(&R.rng, 0.6f, 1.6f);
    }
    for (int i = 0; i < (big ? 14 : 8); i++) {
        float a = pa_rng_range(&R.rng, 0.0f, PA_TAU), s = pa_rng_range(&R.rng, 4.0f, 10.0f);
        part_new(PT_SPARK, vadd(p, v3(0, 0.8f, 0)), v3(cosf(a) * s, pa_rng_range(&R.rng, 3.0f, 9.0f), sinf(a) * s),
                 pa_rng_range(&R.rng, 0.4f, 0.9f), 0.10f, pa_hex(0xFFF2A0));
    }
}

/* ----------------------------------------------------------- spawning -- */
static int pick_traffic_model(void) {
    static const int M[] = { VM_SEDAN, VM_SEDAN, VM_SEDAN, VM_HATCH, VM_HATCH, VM_HATCH, VM_TAXI, VM_POLICE,
                             VM_BUS, VM_BUS, VM_BOXTRUCK, VM_BOXTRUCK, VM_PICKUP, VM_VAN, VM_VAN, VM_SPORTS,
                             VM_MINT, VM_MINT, VM_ICECREAM };
    return M[pa_rng_int(&R.rng, 0, (int)(sizeof(M) / sizeof(M[0])) - 1)];
}

static void car_colors(Car *c) {
    switch (c->model) {
    case VM_TAXI:     c->body = pa_hex(0xFFC21A); c->acc = pa_hex(0x1E1F2C); break;
    case VM_POLICE:   c->body = pa_hex(0x1E1F2C); c->acc = pa_hex(0xF6F6FA); break;
    case VM_BUS:      c->body = pa_hex(0xF4F5F9); c->acc = pa_hex(pa_rng_chance(&R.rng, 0.5f) ? 0x39C26B : 0x3E7FF0); break;
    case VM_ICECREAM: c->body = pa_hex(0x57B6F5); c->acc = pa_hex(0xFF8FC8); break;
    case VM_VAN:      c->body = pa_hex(pa_rng_chance(&R.rng, 0.6f) ? 0xF2F2F6 : 0xF6C340); c->acc = pa_hex(0x3B3F58); break;
    case VM_MINT:     c->body = pa_hex(pa_rng_chance(&R.rng, 0.5f) ? 0x7FE3D2 : 0xFFB0C8); c->acc = pa_hex(0xFFFFFF); break;
    default:
        c->body = pa_hex(TRAFFIC_PAL[pa_rng_int(&R.rng, 0, TPAL_N - 1)]);
        c->acc = pa_hex(0x1E1F2C);
        break;
    }
}

static Car *car_new(void) {
    if (R.ncar >= MAX_CARS) return NULL;
    Car *c = &R.car[R.ncar++];
    memset(c, 0, sizeof(*c));
    return c;
}

static float difficulty(void) {
    float p = pa_clamp01((R.z - R.lvl_z0) / R.lvl_len);
    return pa_clamp01((float)(R.level - 1) / 40.0f + p * 0.12f);
}

/* Traffic comes in rows. Each row leaves at least one lane open, and the open
   lane only ever drifts by one between rows, so there is always a line
   through - tight, but never sealed. */
static void spawn_row(float z) {
    float d = difficulty();
    int prev = R.open_lane;
    int open = prev + pa_rng_int(&R.rng, -1, 1);
    if (open < 0) open = 0;
    if (open >= LANES) open = LANES - 1;
    R.open_lane = open;
    /* when the gap moves, the row it moves on keeps the old gap open too */
    float v = R.traffic_v + pa_rng_range(&R.rng, -0.6f, 0.6f);

    if (pa_rng_chance(&R.rng, 0.12f)) {
        /* a cone line shutting one lane */
        int l = open;
        while (l == open) l = pa_rng_int(&R.rng, 0, LANES - 1);
        for (int k = 0; k < 5; k++) {
            Car *c = car_new();
            if (!c) break;
            c->kind = CK_CONE; c->model = VM_CONE; c->lane = l;
            c->x = lane_x(l) + ((float)k - 2.0f) * 0.25f * (l < open ? 1.0f : -1.0f);
            c->z = z + (float)k * 3.2f;
        }
    } else {
        float p = 0.40f + d * 0.40f;
        int placed = 0;
        for (int l = 0; l < LANES; l++) {
            if (l == open || l == prev) continue;
            if (!pa_rng_chance(&R.rng, p) && !(placed == 0 && l == LANES - 1)) continue;
            Car *c = car_new();
            if (!c) break;
            c->kind = CK_CAR; c->lane = l;
            c->model = pick_traffic_model();
            car_colors(c);
            c->x = lane_x(l) + pa_rng_range(&R.rng, -0.15f, 0.15f);
            c->z = z + pa_rng_range(&R.rng, -1.0f, 1.0f) + g_model[c->model].len * 0.5f;
            c->cruise = c->speed = v;
            placed++;
        }
    }
    if (pa_rng_chance(&R.rng, 0.42f) && R.ncoin < MAX_COINS - 6) {
        for (int k = 0; k < 6; k++) {
            Coin *co = &R.coin[R.ncoin++];
            co->x = lane_x(open);
            co->z = z - 4.0f + (float)k * 2.4f;
            co->spin = k;
        }
    }
}

static int biome_at(float z) { return z < R.gate_z ? R.biome : R.nbiome; }

static void spawn_prop(int side, float z) {
    if (R.nprop >= MAX_PROPS) return;
    int b = biome_at(z);
    float s = side ? 1.0f : -1.0f;
    Prop *p = &R.prop[R.nprop++];
    memset(p, 0, sizeof(*p));
    p->biome = b;
    p->z = z;
    p->seed = (uint32_t)pa_rng_int(&R.rng, 0, 0x7FFFFFF);
    float r = pa_rng_next(&R.rng);
    if (b == B_DAY) {
        if (r < 0.46f) {
            p->kind = P_TOWER;
            p->w = pa_rng_range(&R.rng, 7.0f, 12.0f);
            p->d = pa_rng_range(&R.rng, 7.0f, 12.0f);
            p->h = pa_rng_range(&R.rng, 18.0f, 52.0f);
            p->x = s * (RAIL_X + 5.0f + p->w * 0.5f + pa_rng_range(&R.rng, 0.0f, 9.0f));
        } else if (r < 0.90f) {
            p->kind = P_TREE; p->w = pa_rng_range(&R.rng, 0.8f, 1.2f);
            p->x = s * (RAIL_X + pa_rng_range(&R.rng, 1.6f, 4.0f));
        } else {
            p->kind = P_BOARD; p->x = s * (RAIL_X + 4.5f);
        }
    } else if (b == B_NIGHT) {
        if (r < 0.34f) {
            p->kind = P_BLOCK;
            p->w = pa_rng_range(&R.rng, 6.0f, 11.0f);
            p->d = pa_rng_range(&R.rng, 6.0f, 10.0f);
            p->h = pa_rng_range(&R.rng, 6.0f, 16.0f);
            p->x = s * (RAIL_X + 6.0f + p->w * 0.5f + pa_rng_range(&R.rng, 0.0f, 8.0f));
        } else if (r < 0.90f) {
            p->kind = P_TREE; p->w = pa_rng_range(&R.rng, 0.8f, 1.25f);
            p->x = s * (RAIL_X + pa_rng_range(&R.rng, 1.8f, 6.0f));
        } else {
            p->kind = P_BOARD; p->x = s * (RAIL_X + 4.5f);
        }
    } else {
        if (r < 0.62f) {
            p->kind = P_TREE; p->w = pa_rng_range(&R.rng, 0.8f, 1.3f);
            p->x = s * (RAIL_X + pa_rng_range(&R.rng, 1.8f, 16.0f));
        } else if (r < 0.80f) {
            p->kind = P_HOUSE;
            p->w = pa_rng_range(&R.rng, 4.0f, 6.0f); p->d = pa_rng_range(&R.rng, 4.0f, 6.0f); p->h = pa_rng_range(&R.rng, 2.8f, 4.0f);
            p->x = s * (RAIL_X + 7.0f + pa_rng_range(&R.rng, 0.0f, 12.0f));
        } else if (r < 0.88f) {
            p->kind = P_BOARD; p->x = s * (RAIL_X + 4.5f);
        } else {
            p->kind = P_BUSH; p->w = pa_rng_range(&R.rng, 0.6f, 1.0f);
            p->x = s * (RAIL_X + pa_rng_range(&R.rng, 1.2f, 3.0f));
        }
    }
}

static void spawn_world(void) {
    float horizon = R.z + VIEW_AHEAD;
    while (R.row_z < horizon) {
        spawn_row(R.row_z);
        float d = difficulty();
        R.row_z += pa_lerpf(32.0f, 19.0f, d) * pa_rng_range(&R.rng, 0.9f, 1.25f);
    }
    for (int s = 0; s < 2; s++)
        while (R.prop_z[s] < horizon) {
            spawn_prop(s, R.prop_z[s]);
            int b = biome_at(R.prop_z[s]);
            R.prop_z[s] += b == B_DAY ? pa_rng_range(&R.rng, 6.0f, 11.0f) : pa_rng_range(&R.rng, 5.0f, 12.0f);
        }
    while (R.lamp_z < horizon && R.nprop < MAX_PROPS) {
        Prop *p = &R.prop[R.nprop++];
        memset(p, 0, sizeof(*p));
        p->kind = P_LAMP;
        p->biome = biome_at(R.lamp_z);
        p->z = R.lamp_z;
        p->x = (R.lamp_side ? 1.0f : -1.0f) * (RAIL_X + 0.6f);
        R.lamp_side ^= 1;
        R.lamp_z += 26.0f;
    }
}

/* -------------------------------------------------------------- run setup -- */
static void world_reset(float start_progress) {
    int keep_state = R.state;
    float keep_time = R.time;
    memset(&R, 0, sizeof(R));
    R.state = keep_state;
    R.time = keep_time;
    pa_rng_seed(&R.rng, 0xC4A5E5u + (uint32_t)S.level * 7919u + (uint32_t)S.runs * 104729u);
    R.level = S.level;
    R.lvl_len = lvl_length(R.level);
    R.lvl_z0 = 0.0f;
    R.gate_z = R.lvl_len;
    R.z = R.lvl_len * start_progress;
    R.run_z0 = R.z;
    R.biome = biome_of_level(R.level);
    R.nbiome = biome_of_level(R.level + 1);
    R.sky_from = R.biome;
    R.sky_mix = 1.0f;
    R.lane = R.prev_lane = 1;
    R.x = lane_x(R.lane);
    R.camx = R.x * 0.72f;
    R.camz = R.z - CAM_BACK;
    R.traffic_v = 11.0f + (float)(R.level > 40 ? 40 : R.level) * 0.05f;
    R.open_lane = 1;
    R.row_z = R.z + 34.0f;
    R.prop_z[0] = R.prop_z[1] = R.z - 14.0f;
    R.lamp_z = R.z - 10.0f;
    R.ts = 1.0f;
    R.pre_best = S.best;
    spawn_world();
    /* Rows spawned at the start are given a little head start so the opening
       stretch reads as a road already in use. */
}

static void enter(int st) {
    R.state = st;
    R.st_t = 0.0f;
}

static void start_play(void) {
    enter(ST_PLAY);
    R.speed = 6.0f;
    R.play_t = 0.0f;
    pa_sfx("select");
    pa_tone(90.0f, 320.0f, 0.45f, 3, 0.08f);
}

static void rush_start(void) {
    models_init();
    save_load();
    if (L.w == 0) compute_layout(540, 1170);
    R.state = ST_MENU;
    float p0 = 0.0f;
    if (demo() == 1) p0 = 0.66f;
    if (demo() == 3) p0 = 0.30f;
    world_reset(p0);
    enter(demo() == 2 ? ST_GARAGE : ST_MENU);
}

static void rush_stop(void) { save_store(); }

/* --------------------------------------------------------------- gameplay -- */
static void change_lane(int dir) {
    int t = R.lane + dir;
    if (t < 0 || t >= LANES) {
        /* bump the barrier: a little shove back and a puff */
        R.vx -= (float)dir * 2.0f;
        return;
    }
    R.prev_lane = R.lane;
    R.lane = t;
    R.lane_t = 0.0f;
    snd_lane();
}

static void fire_nitro(void) {
    if (R.nitro < 0.999f || R.nitro_t > 0.0f) return;
    R.nitro = 0.0f;
    R.nitro_t = 3.2f;
    snd_nitro();
    banner("NITRO!", pa_hex(0xFF7A00));
    R.shake = 0.25f;
}

static void add_nitro(float a) {
    if (R.nitro_t > 0.0f) return;
    R.nitro = pa_clamp01(R.nitro + a);
}

static int nitro_hit(float px, float py) {
    float r = 62.0f * L.ui;
    float cx = (float)L.w - 84.0f * L.ui, cy = (float)L.h - 96.0f * L.ui;
    float dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= (r + 16.0f) * (r + 16.0f);
}

static void launch_car(Car *c, float dir, float power) {
    c->flying = 1;
    c->v = v3(dir * pa_rng_range(&R.rng, 3.0f, 6.0f) * power, pa_rng_range(&R.rng, 6.0f, 9.0f) * power,
              R.speed * 0.75f + pa_rng_range(&R.rng, 2.0f, 6.0f));
    c->wy = pa_rng_range(&R.rng, -4.0f, 4.0f);
    c->wp = pa_rng_range(&R.rng, -7.0f, -3.0f);
    c->wr = dir * pa_rng_range(&R.rng, 5.0f, 10.0f);
}

static void crash(Car *hit) {
    enter(ST_CRASH);
    R.crash_t = 0.0f;
    R.ts = 0.25f;
    R.flash = 1.0f;
    R.shake = 0.8f;
    float dir = hit ? (hit->x >= R.x ? -1.0f : 1.0f) : 1.0f;
    if (hit && fabsf(hit->x - R.x) < 0.4f) dir = R.vx >= 0.0f ? 1.0f : -1.0f;
    R.pp = v3(R.x, 0.0f, R.z);
    R.pv = v3(dir * 3.0f + R.vx * 0.3f, 8.5f, R.speed * 0.26f);
    R.pyaw = R.yaw; R.ppitch = 0.0f; R.proll = R.roll;
    R.wyaw = dir * 2.5f; R.wpitch = -4.2f; R.wroll = dir * 6.5f;
    if (hit) {
        hit->flying = 1;
        hit->v = v3(-dir * 3.0f, 7.0f, hit->speed * 0.5f + R.speed * 0.12f);
        hit->wy = -dir * 2.0f; hit->wp = 3.5f; hit->wr = -dir * 5.5f;
        R.impact = v3((R.x + hit->x) * 0.5f, 0.8f, hit->z - g_model[hit->model].len * 0.5f);
    } else {
        R.impact = v3(R.x, 0.8f, R.z + my_len() * 0.5f);
    }
    R.crack_seed = 0x51EDu + (uint32_t)(R.z * 13.0f);
    R.ncoin = 0;
    boom_at(R.impact, 1);
    snd_crash();
    /* every driver nearby has an opinion */
    static const int REACT[] = { EMO_SHOCK, EMO_CRY, EMO_SWEAR, EMO_SHOCK, EMO_LAUGH, EMO_ANGRY };
    int k = 0;
    for (int i = 0; i < R.ncar; i++) {
        Car *c = &R.car[i];
        if (c == hit || c->kind != CK_CAR || c->flying) continue;
        float dz = c->z - R.z;
        if (dz > 4.0f && dz < 70.0f) { c->emo = REACT[k % 6]; c->emo_t = -0.12f * (float)k; k++; }
    }
    /* tally */
    int dist = (int)(R.z - R.run_z0);
    R.earned = R.run_coins;
    S.coins += R.earned;
    S.runs++;
    S.time_s += (int)R.play_t;
    R.new_best = dist > S.best;
    if (dist > S.best) S.best = dist;
    save_store();
}

static int lane_clear_beside(int l, float margin) {
    float lx = lane_x(l);
    for (int i = 0; i < R.ncar; i++) {
        const Car *c = &R.car[i];
        if (c->flying) continue;
        if (fabsf(c->x - lx) > 1.9f) continue;
        float need = (g_model[c->model].len + my_len()) * 0.5f + margin;
        if (fabsf(c->z - R.z) < need) return 0;
    }
    return 1;
}

static float lane_gap(int l) {
    float lx = lane_x(l), best = 400.0f;
    for (int i = 0; i < R.ncar; i++) {
        const Car *c = &R.car[i];
        if (c->flying) continue;
        if (fabsf(c->x - lx) > 1.9f) continue;
        float gap = (c->z - g_model[c->model].len * 0.5f) - (R.z + my_len() * 0.5f);
        if (gap > -(g_model[c->model].len + my_len()) && gap < best) best = gap;
    }
    return best;
}

static int bot_safe(int la, int lb, float t0, float t1) {
    float mw = my_wid() * 0.5f, ml = my_len() * 0.5f;
    float tsplit = la == lb ? t0 : t0 + (t1 - t0) * 0.6f;
    for (int i = 0; i < R.ncar; i++) {
        const Car *c = &R.car[i];
        if (c->flying) continue;
        const Model *cm = &g_model[c->model];
        float hx = mw + cm->wid * 0.5f * 0.88f + 0.10f, hz = ml + cm->len * 0.5f * 0.92f + 0.5f;
        int in_a = fabsf(c->x - lane_x(la)) < hx, in_b = fabsf(c->x - lane_x(lb)) < hx;
        if (!in_a && !in_b) continue;
        for (float t = t0; t <= t1 + 1e-4f; t += 0.05f) {
            if (t < R.nitro_t - 0.15f) continue;   /* under nitro a hit is a smash */
            if (!in_b && t > tsplit) continue;
            float k = R.speed > R.top ? 3.5f : 0.9f;
            float pz = R.z + R.top * t + (R.speed - R.top) * (1.0f - expf(-k * t)) / k;
            float dz = (c->z + c->speed * t) - pz;
            if (fabsf(dz) < hz) return 0;
        }
    }
    return 1;
}

/* Review-capture driver: weaves for the clearest lane, fires nitro at a set
   moment, then picks a car to hit so the crash sequence gets filmed. */
static void bot_drive(void) {
    R.bot_t -= 1.0f / 120.0f;
    if (R.bot_t > 0.0f || fabsf(R.x - lane_x(R.lane)) > 1.9f) return;
    R.bot_t = 0.05f;
    int kamikaze = demo() == 1 && R.play_t > 9.6f && R.nitro_t <= 0.0f;
    int smash = R.nitro_t > 1.6f;
    if (kamikaze || smash) {
        int best = R.lane;
        float bz = 1e9f;
        for (int i = 0; i < R.ncar; i++) {
            const Car *c = &R.car[i];
            if (c->flying || c->kind != CK_CAR) continue;
            float gap = c->z - R.z;
            if (gap < 14.0f || gap > 60.0f) continue;
            if (gap < bz) { bz = gap; best = c->lane; }
        }
        if (best != R.lane) {
            int dir = best > R.lane ? 1 : -1;
            if (smash || lane_clear_beside(R.lane + dir, 1.0f)) change_lane(dir);
        }
        return;
    }
    /* Look-ahead over the next couple of seconds: traffic holds its speed,
       so every car's future is known. Search lane choices in 0.3 s steps and
       take the first move of the path that survives longest, preferring to
       stay put. */
    enum { NS = 8 };
    const float step = 0.30f;
    int alive[NS + 1][LANES], from[NS + 1][LANES];
    for (int l = 0; l < LANES; l++) { alive[0][l] = l == R.lane; from[0][l] = l; }
    int last = 0;
    for (int i = 0; i < NS; i++) {
        int any = 0;
        for (int l2 = 0; l2 < LANES; l2++) {
            alive[i + 1][l2] = 0;
            for (int dl = 0; dl <= 2 && !alive[i + 1][l2]; dl++) {
                /* stay first, then the two neighbours */
                int l = dl == 0 ? l2 : (dl == 1 ? l2 - 1 : l2 + 1);
                if (l < 0 || l >= LANES || !alive[i][l]) continue;
                if (!bot_safe(l, l2, (float)i * step, (float)(i + 1) * step)) continue;
                alive[i + 1][l2] = 1;
                from[i + 1][l2] = i == 0 ? l2 : from[i][l];
                any = 1;
            }
        }
        if (!any) break;
        last = i + 1;
    }
    int pick = R.lane;
    if (last > 0) {
        if (!alive[last][R.lane] || from[last][R.lane] != R.lane) {
            int bestl = -1, bestd = 99;
            for (int l = 0; l < LANES; l++)
                if (alive[last][l] && abs(from[last][l] - R.lane) < bestd) { bestd = abs(from[last][l] - R.lane); bestl = l; }
            if (bestl >= 0) pick = from[last][bestl];
        }
    }
    if (last == 0) {
        float bg = lane_gap(R.lane);
        for (int d = -1; d <= 1; d += 2) {
            int l = R.lane + d;
            if (l < 0 || l >= LANES || !lane_clear_beside(l, 0.2f)) continue;
            if (lane_gap(l) > bg) { bg = lane_gap(l); pick = l; }
        }
    }
    if (pick != R.lane) change_lane(pick > R.lane ? 1 : -1);
}

static void player_input(const PA_Input *in) {
    if (in->pressed) {
        R.touch_x = in->x; R.touch_y = in->y; R.touch_t = 0.45f;
        if (nitro_hit(in->x, in->y)) { fire_nitro(); R.drag_live = 0; }
        else { R.drag_x = in->x; R.drag_live = 1; }
    }
    if (in->down && R.drag_live) {
        float d = in->x - R.drag_x, th = 34.0f * L.ui;
        if (d > th) { change_lane(1); R.drag_x = in->x; }
        else if (d < -th) { change_lane(-1); R.drag_x = in->x; }
    }
    if (in->released) R.drag_live = 0;
    if (in->swipe == PA_SWIPE_LEFT && !in->down && !in->released) change_lane(-1);
    if (in->swipe == PA_SWIPE_RIGHT && !in->down && !in->released) change_lane(1);
    if (in->swipe == PA_SWIPE_UP && !in->released) fire_nitro();
    if (in->key_pressed[PA_KEY_LEFT]) change_lane(-1);
    if (in->key_pressed[PA_KEY_RIGHT]) change_lane(1);
    if (in->key_pressed[PA_KEY_SPACE] || in->key_pressed[PA_KEY_UP]) fire_nitro();
}

static void update_parts(float dt) {
    for (int i = R.npart - 1; i >= 0; i--) {
        Part *q = &R.part[i];
        q->t += dt;
        if (q->t >= q->life) { R.part[i] = R.part[--R.npart]; continue; }
        q->p = vadd(q->p, vmul(q->v, dt));
        q->r += q->grow * dt;
        if (q->kind == PT_SMOKE || q->kind == PT_BOOM) { q->v = vmul(q->v, expf(-2.4f * dt)); q->v.y += 0.6f * dt; }
        else if (q->kind == PT_FIRE) { q->v = vmul(q->v, expf(-3.0f * dt)); }
        else { q->v.y -= 22.0f * dt; if (q->p.y < 0.0f) { q->p.y = 0.0f; q->v.y *= -0.4f; q->v.x *= 0.7f; q->v.z *= 0.7f; } }
    }
    for (int i = R.npop - 1; i >= 0; i--) {
        R.pop[i].t += dt;
        if (R.pop[i].t >= R.pop[i].life) { memmove(&R.pop[i], &R.pop[i + 1], sizeof(Pop) * (size_t)(R.npop - i - 1)); R.npop--; }
    }
    for (int i = R.nring - 1; i >= 0; i--) {
        R.ring[i].t += dt;
        if (R.ring[i].t >= R.ring[i].life) { memmove(&R.ring[i], &R.ring[i + 1], sizeof(Ring) * (size_t)(R.nring - i - 1)); R.nring--; }
    }
}

static void fly(Car *c, float dt) {
    c->v.y -= 24.0f * dt;
    c->x += c->v.x * dt; c->y += c->v.y * dt; c->z += c->v.z * dt;
    c->yaw += c->wy * dt; c->pitch += c->wp * dt; c->roll += c->wr * dt;
    if (c->y < 0.0f) {
        c->y = 0.0f;
        if (c->v.y < -3.0f) {
            c->v.y = -c->v.y * 0.38f;
            c->wy *= 0.6f; c->wp *= 0.6f; c->wr *= 0.6f;
            part_new(PT_SMOKE, v3(c->x, 0.3f, c->z), v3(0, 1.0f, 0), 0.8f, 0.6f, pa_hex(0xD8D6E4));
        } else {
            c->v.y = 0.0f;
            c->v = vmul(c->v, expf(-3.5f * dt));
            c->wy *= expf(-4.0f * dt); c->wp *= expf(-4.0f * dt); c->wr *= expf(-4.0f * dt);
            /* settle onto the nearest flat face */
            float q = PA_PI * 0.5f;
            c->pitch = pa_approach(c->pitch, roundf(c->pitch / q) * q, 6.0f, dt);
            c->roll = pa_approach(c->roll, roundf(c->roll / q) * q, 6.0f, dt);
        }
    }
    c->v.z = pa_approach(c->v.z, 0.0f, 0.8f, dt);
}

static void update_traffic(float dt, int braking) {
    for (int i = 0; i < R.ncar; i++) {
        Car *c = &R.car[i];
        if (c->kind != CK_CAR || c->flying) continue;
        float target = braking ? 0.0f : c->cruise;
        /* car following: never drive into the one ahead in your lane */
        for (int j = 0; j < R.ncar; j++) {
            if (j == i) continue;
            const Car *o = &R.car[j];
            if (o->flying || fabsf(o->x - c->x) > 1.6f) continue;
            float gap = o->z - c->z;
            float need = (g_model[o->model].len + g_model[c->model].len) * 0.5f + 3.0f;
            if (gap > 0.0f && gap < need + 6.0f) {
                float s = o->kind == CK_CONE ? 0.0f : o->speed;
                if (gap < need) s *= 0.5f;
                if (s < target) target = s;
            }
        }
        c->speed = pa_approach(c->speed, target, braking ? 2.2f : 3.0f, dt);
    }
    for (int i = R.ncar - 1; i >= 0; i--) {
        Car *c = &R.car[i];
        if (c->flying) fly(c, dt);
        else c->z += c->speed * dt;
        if (c->emo) c->emo_t += dt;
        if (c->emo && c->emo_t > 2.2f) c->emo = 0;
        if (c->z < R.camz - 6.0f || c->z > R.z + VIEW_AHEAD + 60.0f) R.car[i] = R.car[--R.ncar];
    }
}

static void project_player_screen(float *sx, float *sy);

static void update_play(float dt, const PA_Input *in) {
    R.play_t += dt;
    if (demo()) bot_drive();
    player_input(in);
    if (demo() && !R.demo_nitro_done) {
        float at = demo() == 1 ? 4.2f : 7.0f;
        if (R.play_t > at - 0.7f && R.nitro < 1.0f) R.nitro = 1.0f;
        if (R.play_t > at) { fire_nitro(); R.demo_nitro_done = 1; }
    }

    /* speed */
    float prog = pa_clamp01((R.z - R.lvl_z0) / R.lvl_len);
    float top = 27.0f + (float)(R.level > 50 ? 50 : R.level) * 0.14f + prog * 2.5f;
    R.top = top;
    if (R.nitro_t > 0.0f) {
        R.nitro_t -= dt;
        top *= 1.45f;
        if (R.nitro_t <= 0.0f) R.nitro_t = 0.0f;
    }
    R.nitro_k = pa_approach(R.nitro_k, R.nitro_t > 0.0f ? 1.0f : 0.0f, R.nitro_t > 0.0f ? 5.0f : 2.0f, dt);
    R.speed = pa_approach(R.speed, top, R.nitro_t > 0.0f ? 3.0f : (R.speed > top ? 3.5f : 0.9f), dt);
    R.z += R.speed * dt;

    /* lane spring */
    float tx = lane_x(R.lane);
    float ax = 210.0f * (tx - R.x) - 26.0f * R.vx;
    R.vx += ax * dt;
    R.x += R.vx * dt;
    R.lane_t += dt;
    R.yaw = pa_approach(R.yaw, atanf(R.vx / (R.speed + 4.0f)) * 1.5f, 18.0f, dt);
    R.roll = pa_approach(R.roll, pa_clampf(R.vx * 0.030f, -0.16f, 0.16f), 10.0f, dt);
    R.cam_roll = pa_approach(R.cam_roll, pa_clampf(-R.vx * 0.006f, -0.055f, 0.055f), 6.0f, dt);
    R.camx = pa_approach(R.camx, R.x * 0.72f, 5.0f, dt);
    R.camz = R.z - CAM_BACK - R.nitro_k * 1.1f;

    /* tyre smoke when swerving, flames under nitro */
    R.smoke_acc += dt * (fabsf(R.vx) > 1.6f ? 45.0f : 5.0f);
    while (R.smoke_acc > 1.0f) {
        R.smoke_acc -= 1.0f;
        float side = R.vx > 0.0f ? -1.0f : 1.0f;
        if (fabsf(R.vx) < 1.6f) side = pa_rng_chance(&R.rng, 0.5f) ? -1.0f : 1.0f;
        Part *q = part_new(PT_SMOKE, v3(R.x + side * my_wid() * 0.42f, 0.25f, R.z - my_len() * 0.42f),
                           v3(pa_rng_range(&R.rng, -0.8f, 0.8f), pa_rng_range(&R.rng, 0.4f, 1.2f), R.speed * 0.80f),
                           pa_rng_range(&R.rng, 0.30f, 0.50f), fabsf(R.vx) > 1.6f ? 0.24f : 0.12f, pa_hex(0xFFFFFF));
        if (q) q->grow = 0.6f;
    }
    if (R.nitro_t > 0.0f) {
        R.flame_acc += dt * 40.0f;
        while (R.flame_acc > 1.0f) {
            R.flame_acc -= 1.0f;
            for (int s = -1; s <= 1; s += 2) {
                Part *q = part_new(PT_FIRE, v3(R.x + (float)s * 0.45f, 0.40f, R.z - my_len() * 0.5f - 0.2f),
                                   v3(pa_rng_range(&R.rng, -0.5f, 0.5f), pa_rng_range(&R.rng, 0.0f, 0.6f), R.speed * 0.70f),
                                   0.12f, 0.16f, pa_hex(pa_rng_chance(&R.rng, 0.5f) ? 0xFFD23A : 0x6AC8FF));
                if (q) q->grow = -0.8f;
            }
        }
        /* speed sparks off the sides */
    }

    /* engine purr */
    R.engine_acc -= dt;
    if (R.engine_acc <= 0.0f) {
        R.engine_acc = 0.24f;
        float hz = 70.0f + R.speed * 2.2f;
        pa_tone(hz, hz * 1.04f, 0.26f, 3, 0.012f + R.nitro_k * 0.01f);
    }

    update_traffic(dt, 0);

    /* collisions, near misses, angry drivers */
    float mw = my_wid() * 0.5f, ml = my_len() * 0.5f;
    for (int i = 0; i < R.ncar; i++) {
        Car *c = &R.car[i];
        if (c->flying) continue;
        const Model *cm = &g_model[c->model];
        float dz = c->z - R.z, dx = c->x - R.x;
        float hx = mw + cm->wid * 0.5f * 0.88f, hz = ml + cm->len * 0.5f * 0.92f;
        if (fabsf(dz) < hz && fabsf(dx) < hx) {
            if (c->kind == CK_CONE) {
                launch_car(c, dx >= 0.0f ? 1.0f : -1.0f, 0.7f);
                c->v.z = R.speed * 1.05f;
                R.speed *= 0.97f;
                snd_cone();
                continue;
            }
            if (R.nitro_t > 0.0f) {
                launch_car(c, dx >= 0.0f ? 1.0f : -1.0f, 1.2f);
                c->emo = EMO_SKULL; c->emo_t = 0.0f;
                boom_at(v3(c->x, 0.6f, c->z - cm->len * 0.5f), 0);
                R.run_smash++;
                R.run_coins += 5;
                R.shake = 0.35f;
                snd_smash();
                float sx, sy;
                project_player_screen(&sx, &sy);
                pop_text(sx, sy - 70.0f * L.ui, 70.0f * L.ui, 44.0f * L.ui, 1, "+5");
                banner("SMASH!", pa_hex(0xFF4FA8));
                continue;
            }
            crash(c);
            return;
        }
        if (c->kind != CK_CAR) continue;
        /* tailgating: the driver ahead gets cross */
        if (!c->angry_done && fabsf(dx) < 1.2f && dz > hz && dz < hz + 12.0f) {
            c->angry_done = 1;
            if (!c->emo) { c->emo = EMO_ANGRY; c->emo_t = 0.0f; }
        }
        if (!c->near_done && dz > 0.0f && dz < hz + 11.0f && fabsf(dx) >= hx && fabsf(dx) < hx + 1.9f &&
            fabsf(R.x - lane_x(R.lane)) < 1.2f) {
            c->near_done = 1;
            int close = R.lane_t < 0.45f && dz < hz + 7.0f && fabsf(lane_x(R.prev_lane) - c->x) < 1.4f;
            R.run_near++;
            R.combo = R.combo_t > 0.0f ? R.combo + 1 : 1;
            R.combo_t = 2.2f;
            int gain = close ? 3 : 2;
            R.run_coins += gain;
            add_nitro(close ? 0.26f : 0.17f);
            static const int NEAR[] = { EMO_SKULL, EMO_SWEAR, EMO_SHOCK, EMO_SWEAT, EMO_CRY, EMO_ANGRY };
            c->emo = NEAR[(R.run_near + (int)(c->z)) % 6];
            c->emo_t = 0.0f;
            float sx, sy;
            project_player_screen(&sx, &sy);
            ring_add(1, sx, sy, pa_hex(0xFF4FA8), 40.0f * L.ui, 150.0f * L.ui, 0.45f);
            char b[32];
            snprintf(b, sizeof(b), "+%d", gain);
            pop_text(sx + (dx > 0 ? 70.0f : -70.0f) * L.ui, sy - 60.0f * L.ui, 70.0f * L.ui, 44.0f * L.ui, 0, b);
            if (close) banner("CLOSE CALL!", pa_hex(0xFF4FA8));
            else if (R.combo >= 2) { snprintf(b, sizeof(b), "NEAR MISS X%d", R.combo); banner(b, pa_hex(0xFFC21A)); }
            else banner("NEAR MISS!", pa_hex(0xFFC21A));
            snd_near(R.combo);
        }
    }
    if (R.combo_t > 0.0f) { R.combo_t -= dt; if (R.combo_t <= 0.0f) R.combo = 0; }

    /* coins */
    for (int i = R.ncoin - 1; i >= 0; i--) {
        Coin *co = &R.coin[i];
        if (co->z < R.camz) { R.coin[i] = R.coin[--R.ncoin]; continue; }
        if (fabsf(co->z - R.z) < 1.6f && fabsf(co->x - R.x) < 1.5f) {
            R.run_coins++;
            add_nitro(0.03f);
            R.coin_pulse = 1.0f;
            float sx, sy;
            project_player_screen(&sx, &sy);
            ring_add(0, sx, sy - 60.0f * L.ui, pa_hex(0xFFD23A), 10.0f * L.ui, 46.0f * L.ui, 0.3f);
            R.coin[i] = R.coin[--R.ncoin];
            snd_coin();
        }
    }

    /* the level gate */
    if (R.z >= R.gate_z) {
        R.level++;
        S.level = R.level;
        R.sky_from = R.biome;
        R.biome = R.nbiome;
        R.nbiome = biome_of_level(R.level + 1);
        R.sky_mix = 0.0f;
        R.lvl_z0 = R.gate_z;
        R.lvl_len = lvl_length(R.level);
        R.gate_z += R.lvl_len;
        char b[24];
        snprintf(b, sizeof(b), "LEVEL %d", R.level);
        banner(b, pa_hex(0xFFFFFF));
        R.run_coins += 10;
        snd_gate();
        save_store();
        for (int i = 0; i < 40; i++) {
            static const uint32_t CC[] = { 0xFF4FA8, 0xFFC21A, 0x3BA0F0, 0x47D78C, 0xFFFFFF };
            float x = pa_rng_range(&R.rng, -ROAD_HW, ROAD_HW);
            part_new(PT_CONFETTI, v3(x, pa_rng_range(&R.rng, 4.0f, 10.0f), R.z + pa_rng_range(&R.rng, 4.0f, 18.0f)),
                     v3(pa_rng_range(&R.rng, -2.0f, 2.0f), pa_rng_range(&R.rng, -1.0f, 2.0f), R.speed * 0.6f),
                     2.0f, 0.12f, pa_hex(CC[i % 5]));
        }
    }
    if (R.sky_mix < 1.0f) R.sky_mix = pa_clamp01(R.sky_mix + dt * 0.8f);

    /* tidy */
    for (int i = R.nprop - 1; i >= 0; i--) if (R.prop[i].z < R.camz - 30.0f) R.prop[i] = R.prop[--R.nprop];
    spawn_world();
}

static void update_crash(float dt_real) {
    R.crash_t += dt_real;
    R.ts = R.crash_t < 0.42f ? 0.25f : pa_approach(R.ts, 1.0f, 3.0f, dt_real);
    float dt = dt_real * R.ts;
    if (!R.ko_sfx && R.crash_t > 0.10f) { snd_crack(); R.ko_sfx = 1; }
    if (R.ko_sfx == 1 && R.crash_t > 0.30f) { snd_ko(); R.ko_sfx = 2; }

    /* the player car cartwheels */
    R.pv.y -= 24.0f * dt;
    R.pp = vadd(R.pp, vmul(R.pv, dt));
    R.pyaw += R.wyaw * dt; R.ppitch += R.wpitch * dt; R.proll += R.wroll * dt;
    if (R.pp.y < 0.0f) {
        R.pp.y = 0.0f;
        if (R.pv.y < -3.0f) {
            R.pv.y = -R.pv.y * 0.40f;
            R.wyaw *= 0.7f; R.wpitch *= 0.7f; R.wroll *= 0.7f;
            R.shake = 0.35f;
            part_new(PT_SMOKE, v3(R.pp.x, 0.3f, R.pp.z), v3(0, 1.2f, 1.0f), 1.0f, 0.8f, pa_hex(0xD8D6E4));
        } else {
            R.pv.y = 0.0f;
            R.pv = vmul(R.pv, expf(-3.0f * dt));
            float q = PA_PI * 0.5f;
            R.ppitch = pa_approach(R.ppitch, roundf(R.ppitch / q) * q, 5.0f, dt);
            R.proll = pa_approach(R.proll, roundf(R.proll / q) * q, 5.0f, dt);
            R.wyaw *= expf(-4.0f * dt); R.wpitch *= expf(-4.0f * dt); R.wroll *= expf(-4.0f * dt);
        }
    }
    R.pv.z = pa_approach(R.pv.z, 0.0f, 4.0f, dt);
    R.pv.x = pa_approach(R.pv.x, 0.0f, 2.0f, dt);
    R.z = R.pp.z;
    R.speed = R.pv.z;
    R.camz = pa_approach(R.camz, R.pp.z - CAM_BACK - 4.5f, 3.0f, dt);
    R.camx = pa_approach(R.camx, R.pp.x * 0.6f, 2.0f, dt);
    R.cam_roll = pa_approach(R.cam_roll, 0.0f, 3.0f, dt);
    R.nitro_k = pa_approach(R.nitro_k, 0.0f, 3.0f, dt);
    if (R.crash_t < 0.9f && R.crash_t > 0.05f) {
        R.smoke_acc += dt * 22.0f;
        while (R.smoke_acc > 1.0f) {
            R.smoke_acc -= 1.0f;
            part_new(PT_SMOKE, vadd(R.pp, v3(pa_rng_range(&R.rng, -0.8f, 0.8f), 0.6f, -1.0f)),
                     v3(pa_rng_range(&R.rng, -1.0f, 1.0f), 1.0f, 0.0f), 0.9f, 0.40f, pa_hex(0xE8E6F0));
        }
    }
    update_traffic(dt, 1);
    update_parts(dt);
    if (R.crash_t > 2.7f) enter(ST_RESULTS);
}

/* ------------------------------------------------------- results + menu -- */
typedef struct { float x, y, w, h; } Box;
static int in_box(Box b, float x, float y) { return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h; }

static Box btn_drive(void) {
    float w = 300.0f * L.ui, h = 84.0f * L.ui;
    Box b = { L.cx - w * 0.5f, (float)L.h * 0.835f - h * 0.5f, w, h };
    if (L.w > L.h) { b.x = (float)L.w * 0.78f - w * 0.5f; b.y = (float)L.h * 0.50f - h * 0.5f; }
    return b;
}
static Box btn_garage_results(void) {
    float w = 220.0f * L.ui, h = 60.0f * L.ui;
    Box b = { L.cx - w * 0.5f, (float)L.h * 0.835f + 58.0f * L.ui, w, h };
    if (L.w > L.h) { b.x = (float)L.w * 0.78f - w * 0.5f; b.y = (float)L.h * 0.50f + 58.0f * L.ui; }
    return b;
}
static Box btn_garage_menu(void) {
    float s = 92.0f * L.ui;
    Box b = { 20.0f * L.ui, (float)L.h - s - 26.0f * L.ui, s, s };
    return b;
}

/* garage grid geometry, shared by input and drawing */
static float g_top(void) { return 86.0f * L.ui + 214.0f * L.ui + 22.0f * L.ui; }
static void garage_tile(int i, float *x, float *y, float *s) {
    float m = 22.0f * L.ui, gap = 12.0f * L.ui;
    float cw = (float)L.w - m * 2.0f;
    if (cw > 560.0f * L.ui) cw = 560.0f * L.ui;
    float x0 = ((float)L.w - cw) * 0.5f;
    float t = (cw - gap * 3.0f) / 4.0f;
    *s = t;
    *x = x0 + (float)(i % 4) * (t + gap);
    *y = g_top() + (float)(i / 4) * (t + gap) - R.gscroll;
}
static Box btn_garage_back(void) { Box b = { 14.0f * L.ui, 18.0f * L.ui, 56.0f * L.ui, 56.0f * L.ui }; return b; }
static Box btn_garage_drive(void) {
    float w = 260.0f * L.ui, h = 70.0f * L.ui;
    Box b = { L.cx - w * 0.5f, (float)L.h - h - 22.0f * L.ui, w, h };
    return b;
}

static void update_garage(float dt, const PA_Input *in) {
    if (R.gflash > 0.0f) R.gflash -= dt;
    if (R.gdeny_t > 0.0f) R.gdeny_t -= dt;
    float x, y, s;
    garage_tile(RIDE_COUNT - 1, &x, &y, &s);
    float content_bottom = y + s + R.gscroll + 120.0f * L.ui;
    float max_scroll = content_bottom - (float)L.h;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (in->pressed) { R.gdrag = 1; R.gdrag_y = in->y; R.gdrag_travel = 0.0f; }
    if (in->down && R.gdrag) {
        float d = in->y - R.gdrag_y;
        R.gscroll -= d;
        R.gdrag_travel += fabsf(d);
        R.gdrag_y = in->y;
    }
    if (in->wheel != 0.0f) R.gscroll -= in->wheel * 60.0f * L.ui;
    R.gscroll = pa_clampf(R.gscroll, 0.0f, max_scroll);
    if (in->released) R.gdrag = 0;
    if (in->key_pressed[PA_KEY_ENTER]) { start_play(); return; }
    if (!in->tapped) return;
    if (in_box(btn_garage_back(), in->x, in->y)) { pa_sfx("select"); enter(ST_MENU); return; }
    if (in_box(btn_garage_drive(), in->x, in->y)) { start_play(); return; }
    for (int i = 0; i < RIDE_COUNT; i++) {
        garage_tile(i, &x, &y, &s);
        if (in->x < x || in->x > x + s || in->y < y || in->y > y + s) continue;
        if (S.own & (1 << i)) {
            S.car = i;
            R.gflash = 0.35f; R.gflash_i = i;
            pa_sfx("select");
            save_store();
        } else if (S.coins >= RIDES[i].price) {
            S.coins -= RIDES[i].price;
            S.own |= 1 << i;
            S.car = i;
            R.gflash = 0.6f; R.gflash_i = i;
            pa_sfx("win");
            save_store();
        } else {
            R.gdeny_t = 0.4f; R.gdeny_i = i;
            pa_sfx("bad");
        }
        break;
    }
}

static void rush_update(float dt, const PA_Input *in) {
    R.time += dt;
    R.st_t += dt;
    if (R.banner_t > 0.0f) R.banner_t -= dt;
    if (R.touch_t > 0.0f) R.touch_t -= dt;
    if (R.coin_pulse > 0.0f) R.coin_pulse -= dt * 4.0f;
    if (R.shake > 0.0f) R.shake -= dt * 1.6f;
    if (R.flash > 0.0f) R.flash -= dt * 3.0f;
    if (R.sky_mix < 1.0f && R.state != ST_PLAY) R.sky_mix = pa_clamp01(R.sky_mix + dt * 0.8f);

    switch (R.state) {
    case ST_MENU:
        update_traffic(dt, 0);
        update_parts(dt);
        R.camx = pa_approach(R.camx, R.x * 0.72f, 5.0f, dt);
        if (demo() == 1 || demo() == 3) {
            if (R.st_t > (demo() == 3 ? 0.05f : 1.0f)) start_play();
            break;
        }
        if (in->tapped && in_box(btn_garage_menu(), in->x, in->y)) { pa_sfx("select"); enter(ST_GARAGE); break; }
        if (in->tapped || in->swipe == PA_SWIPE_LEFT || in->swipe == PA_SWIPE_RIGHT || in->swipe == PA_SWIPE_UP ||
            in->key_pressed[PA_KEY_SPACE] || in->key_pressed[PA_KEY_ENTER] || in->key_pressed[PA_KEY_UP] ||
            in->key_pressed[PA_KEY_LEFT] || in->key_pressed[PA_KEY_RIGHT])
            start_play();
        break;
    case ST_PLAY:
        update_play(dt, in);
        if (R.state == ST_PLAY) update_parts(dt);
        break;
    case ST_CRASH:
        update_crash(dt);
        break;
    case ST_RESULTS:
        R.crash_t += dt;
        update_traffic(dt, 1);
        update_parts(dt);
        if (demo()) break;
        if (R.st_t < 0.5f) break;
        if ((in->tapped && in_box(btn_drive(), in->x, in->y)) || in->key_pressed[PA_KEY_SPACE] || in->key_pressed[PA_KEY_ENTER]) {
            world_reset(0.0f);
            start_play();
        } else if (in->tapped && in_box(btn_garage_results(), in->x, in->y)) {
            world_reset(0.0f);
            enter(ST_GARAGE);
        }
        break;
    case ST_GARAGE:
        update_garage(dt, in);
        if (R.state == ST_PLAY) { world_reset(0.0f); R.state = ST_PLAY; R.speed = 6.0f; }
        break;
    }
}

/* ================================================================ render == */
static Cam g_cam;

static void setup_world_cam(Cam *k) {
    float nk = R.nitro_k;
    float lift = 0.0f;
    if (R.state == ST_CRASH || R.state == ST_RESULTS) lift = pa_smooth(pa_clamp01(R.crash_t / 0.6f)) * 1.8f;
    cam_init(k, v3(R.camx, CAM_H + nk * 0.15f + lift, R.camz), 0.0f, 0.0f, L.f * (1.0f - 0.16f * nk), L.cx, L.hy);
    /* roll about the car */
    k->pvx = L.cx; k->pvy = (float)L.h * 0.78f;
    k->rc = cosf(R.cam_roll); k->rs = sinf(R.cam_roll);
    if (R.state == ST_CRASH || R.state == ST_RESULTS) {
        float zt = R.state == ST_RESULTS ? 1.0f : pa_smooth(pa_clamp01(R.crash_t / 0.35f));
        float back = R.state == ST_RESULTS ? 1.0f : pa_smooth(pa_clamp01((R.crash_t - 1.6f) / 1.0f));
        float zoom = 1.0f + 0.45f * zt - 0.2f * back;
        PA_Vec2 f;
        V3 focus = R.state == ST_RESULTS ? R.pp : vadd(vmul(R.impact, 1.0f - zt * 0.7f), vmul(R.pp, zt * 0.7f));
        focus.y = 0.9f;
        if (cam_proj(k, focus, &f)) {
            k->zfx = f.x; k->zfy = f.y;
            k->ztx = pa_lerpf(f.x, L.cx, 0.55f * zt);
            k->zty = pa_lerpf(f.y, (float)L.h * 0.56f, 0.55f * zt);
            k->zoom = zoom;
        }
    }
    if (R.shake > 0.0f) {
        float a = R.shake * R.shake * 14.0f * L.ui;
        k->shx = sinf(R.time * 71.0f) * a;
        k->shy = cosf(R.time * 53.0f) * a;
    }
}

static void project_player_screen(float *sx, float *sy) {
    PA_Vec2 p;
    if (cam_proj(&g_cam, v3(R.x, 0.9f, R.z), &p)) { *sx = p.x; *sy = p.y; }
    else { *sx = L.cx; *sy = (float)L.h * 0.75f; }
}

/* ---- sky ---- */
static void draw_clouds(PA_Canvas *c, float drift, PA_Color col, PA_Color shade, float scale, float y0, float y1, uint32_t seed) {
    PA_Rng r;
    pa_rng_seed(&r, seed);
    for (int k = 0; k < 5; k++) {
        float span = (float)c->w * 1.6f;
        float cx = pa_wrapf(pa_rng_range(&r, 0.0f, span) + drift * (0.3f + 0.1f * (float)k) + R.time * 3.0f, span) - (float)c->w * 0.3f;
        float cy = pa_lerpf(y0, y1, pa_rng_next(&r));
        float s = (float)c->w * scale * pa_rng_range(&r, 0.8f, 1.3f);
        fcirc(c, cx - s * 0.9f, cy + s * 0.35f, s * 0.62f, shade);
        fcirc(c, cx + s * 0.95f, cy + s * 0.30f, s * 0.70f, shade);
        fcirc(c, cx, cy + s * 0.15f, s, shade);
        fcirc(c, cx - s * 0.9f, cy + s * 0.25f, s * 0.60f, col);
        fcirc(c, cx + s * 0.95f, cy + s * 0.22f, s * 0.66f, col);
        fcirc(c, cx, cy, s * 0.96f, col);
        fcirc(c, cx + s * 0.35f, cy - s * 0.35f, s * 0.62f, col);
    }
}

static void draw_sky_one(PA_Canvas *c, const Cam *k, const Biome *b, int bi, float a) {
    if (a <= 0.01f) return;
    float hy = k->oy;
    float top = -40.0f;
    PA_Paint sky = pa_linear(0, top, 0, hy);
    pa_stop(&sky, 0.0f, amul(pa_hex(b->sky_top), a));
    pa_stop(&sky, 0.55f, amul(pa_hex(b->sky_mid), a));
    pa_stop(&sky, 0.92f, amul(pa_hex(b->sky_low), a));
    pa_stop(&sky, 1.0f, amul(pa_hex(b->glow), a));
    pa_fill_rect_paint(c, -20.0f, top, (float)c->w + 40.0f, hy + 60.0f - top, &sky);
    float drift = -R.camx * 3.0f;
    float W = (float)c->w;
    if (bi == B_NIGHT) {
        PA_Rng r;
        pa_rng_seed(&r, 99u);
        for (int i = 0; i < 120; i++) {
            float sx = pa_rng_range(&r, 0.0f, W), sy = pa_rng_range(&r, 0.0f, hy * 0.92f);
            float tw = 0.5f + 0.5f * sinf(R.time * 2.6f + (float)i * 1.7f);
            float sz = pa_rng_range(&r, 0.8f, 2.4f) * L.ui;
            PA_Vec2 d[4] = { { sx, sy - sz * 1.4f }, { sx + sz, sy }, { sx, sy + sz * 1.4f }, { sx - sz, sy } };
            fpoly(c, d, 4, amul(PA_RGBA(255, 255, 255, (int)(140 + tw * 115)), a));
        }
        /* shooting stars */
        for (int i = 0; i < 2; i++) {
            float t = pa_wrapf(R.time * 0.35f + (float)i * 0.5f, 1.0f);
            float sx = W * (0.15f + 0.6f * (float)i) + t * W * 0.25f, sy = hy * (0.55f + 0.1f * (float)i) - t * hy * 0.20f;
            sline(c, sx, sy, sx - W * 0.16f, sy + W * 0.08f, 2.4f * L.ui, amul(PA_RGBA(255, 255, 255, (int)(180 * (1.0f - t))), a));
        }
        float mx = W * 0.50f + drift * 0.15f, my = hy * 0.60f, mr = W * 0.125f;
        glow(c, mx, my, mr * 2.8f, amul(PA_RGBA(200, 210, 255, 120), a));
        fcirc(c, mx, my, mr * 1.08f, amul(PA_RGBA(230, 236, 255, 120), a));
        PA_Paint mp = pa_linear(mx - mr, my - mr, mx + mr, my + mr);
        pa_stop(&mp, 0.0f, amul(pa_hex(0xFFFFFF), a));
        pa_stop(&mp, 1.0f, amul(pa_hex(0xDCE4FF), a));
        fell_paint(c, mx, my, mr, mr, &mp);
        static const float CR[][3] = { { -0.35f, -0.30f, 0.20f }, { 0.30f, 0.35f, 0.26f }, { 0.42f, -0.22f, 0.12f },
                                       { -0.10f, 0.42f, 0.10f }, { -0.48f, 0.18f, 0.12f }, { 0.05f, -0.05f, 0.09f } };
        for (int i = 0; i < 6; i++)
            fcirc(c, mx + CR[i][0] * mr, my + CR[i][1] * mr, CR[i][2] * mr, amul(PA_RGBA(176, 200, 245, 200), a));
        /* hot glow behind the skyline */
        glow(c, W * 0.85f + drift * 0.1f, hy * 0.86f, W * 0.30f, amul(PA_RGBA(255, 140, 200, 120), a));
    } else if (bi == B_DAY) {
        float sx = W * 0.74f + drift * 0.1f, sy = hy * 0.50f;
        glow(c, sx, sy, W * 0.62f, amul(PA_RGBA(255, 255, 240, 170), a));
        /* god rays */
        for (int i = 0; i < 4; i++) {
            float ang = 0.9f + (float)i * 0.42f + sinf(R.time * 0.3f + (float)i) * 0.03f;
            float len = W * (0.9f + 0.15f * (float)i);
            PA_Vec2 ray[3] = { { sx, sy }, { sx + cosf(ang) * len, sy + sinf(ang) * len * 0.8f },
                               { sx + cosf(ang + 0.07f) * len, sy + sinf(ang + 0.07f) * len * 0.8f } };
            fpoly(c, ray, 3, amul(PA_RGBA(255, 255, 255, 34), a));
        }
        draw_clouds(c, drift, amul(PA_RGBA(255, 255, 255, 250), a), amul(PA_RGBA(206, 222, 255, 255), a), 0.12f, hy * 0.18f, hy * 0.72f, 11u);
        fcirc(c, sx, sy, W * 0.10f, amul(PA_RGBA(255, 255, 245, 235), a));
    } else {
        float sx = W * 0.5f + drift * 0.1f, sy = hy * 0.93f;
        glow(c, sx, sy, W * 0.70f, amul(PA_RGBA(255, 220, 160, 170), a));
        fcirc(c, sx, sy, W * 0.13f, amul(PA_RGBA(255, 236, 190, 245), a));
        draw_clouds(c, drift, amul(PA_RGBA(255, 214, 220, 235), a), amul(PA_RGBA(240, 120, 150, 220), a), 0.13f, hy * 0.20f, hy * 0.62f, 23u);
    }

    /* far skyline, rolled with the camera */
    PA_Rng r;
    pa_rng_seed(&r, 7u + (uint32_t)bi);
    float x = -80.0f + pa_wrapf(drift * 0.5f, 60.0f);
    PA_Color sk = pa_hex(b->skyline);
    while (x < W + 80.0f) {
        float bw = pa_rng_range(&r, 26.0f, 64.0f) * L.ui;
        float bh = hy * (bi == B_SUNSET ? pa_rng_range(&r, 0.03f, 0.08f) : pa_rng_range(&r, 0.06f, 0.24f));
        float tone = pa_rng_range(&r, -0.12f, 0.08f);
        PA_Color col = amul(pa_shade(sk, tone), a);
        if (bi == B_SUNSET) {
            /* rolling green hills with toy trees */
            PA_Vec2 hill[12];
            for (int i = 0; i < 12; i++) {
                float t = (float)i / 11.0f;
                PA_Vec2 p = { x + t * bw * 2.0f, hy + 2.0f - sinf(t * PA_PI) * bh };
                hill[i] = cam_post(k, p.x, p.y);
            }
            fpoly(c, hill, 12, col);
            x += bw * 1.2f;
            continue;
        }
        PA_Vec2 q[4] = { cam_post(k, x, hy - bh), cam_post(k, x + bw - 2.0f, hy - bh),
                         cam_post(k, x + bw - 2.0f, hy + 4.0f), cam_post(k, x, hy + 4.0f) };
        fpoly(c, q, 4, col);
        if (bi == B_NIGHT) {
            for (float wy = hy - bh + 6.0f * L.ui; wy < hy - 6.0f * L.ui; wy += 8.0f * L.ui)
                for (float wx = x + 5.0f * L.ui; wx < x + bw - 8.0f * L.ui; wx += 7.0f * L.ui)
                    if (pa_rng_chance(&r, 0.30f)) {
                        PA_Vec2 p = cam_post(k, wx, wy);
                        pa_fill_rect(c, p.x, p.y, 2.5f * L.ui, 3.2f * L.ui, amul(pa_hex(0xFFC86A), a));
                    }
        } else {
            PA_Vec2 h[4] = { cam_post(k, x, hy - bh), cam_post(k, x + bw * 0.3f, hy - bh),
                             cam_post(k, x + bw * 0.3f, hy + 4.0f), cam_post(k, x, hy + 4.0f) };
            fpoly(c, h, 4, amul(pa_shade(sk, tone + 0.14f), a));
        }
        x += bw;
    }
}

/* ---- ground & road ---- */
static void ground_quad(PA_Canvas *c, const Cam *k, float x0, float x1, float z0, float z1, float y, PA_Color col) {
    PA_Vec2 q[4];
    if (!cam_proj(k, v3(x0, y, z0), &q[0]) || !cam_proj(k, v3(x1, y, z0), &q[1]) ||
        !cam_proj(k, v3(x1, y, z1), &q[2]) || !cam_proj(k, v3(x0, y, z1), &q[3])) return;
    fpoly(c, q, 4, col);
}

static void draw_ground_span(PA_Canvas *c, const Cam *k, const Biome *b, float z0, float z1) {
    if (z1 <= z0) return;
    ground_quad(c, k, -900.0f, 900.0f, z0, z1, 0.0f, pa_hex(b->ground));
    /* verge strips and the shoulder */
    ground_quad(c, k, -RAIL_X - 4.0f, RAIL_X + 4.0f, z0, z1, 0.0f, pa_hex(b->verge));
    ground_quad(c, k, -RAIL_X - 0.2f, RAIL_X + 0.2f, z0, z1, 0.0f, pa_shade(pa_hex(b->road), 0.18f));
    PA_Vec2 q[4];
    if (cam_proj(k, v3(-ROAD_HW - 0.3f, 0, z0), &q[0]) && cam_proj(k, v3(ROAD_HW + 0.3f, 0, z0), &q[1]) &&
        cam_proj(k, v3(ROAD_HW + 0.3f, 0, z1), &q[2]) && cam_proj(k, v3(-ROAD_HW - 0.3f, 0, z1), &q[3])) {
        float yn = q[0].y > q[2].y ? q[0].y : q[2].y, yf = q[0].y > q[2].y ? q[2].y : q[0].y;
        PA_Paint p = pa_linear(0, yf, 0, yn);
        float far = pa_clamp01((z0 - k->pos.z) / 160.0f);
        pa_stop(&p, 0.0f, pa_mix(pa_hex(b->road), pa_hex(b->road_far), far > 0.5f ? 1.0f : 0.6f));
        pa_stop(&p, 1.0f, pa_hex(b->road));
        fpoly_paint(c, q, 4, &p);
    }
    /* solid edge lines: yellow on the left, white on the right */
    ground_quad(c, k, -ROAD_HW + 0.25f, -ROAD_HW + 0.42f, z0, z1, 0.0f, pa_hex(b->edge));
    ground_quad(c, k, ROAD_HW - 0.42f, ROAD_HW - 0.25f, z0, z1, 0.0f, pa_hex(b->dash));
}

static void draw_road(PA_Canvas *c, const Cam *k) {
    float zn = k->pos.z + 1.2f, zf = k->pos.z + 2600.0f;
    const Biome *a = &BIOMES[R.biome], *n = &BIOMES[R.nbiome];
    float split = R.gate_z;
    if (split < zn) split = zn;
    if (split > zf) split = zf;
    draw_ground_span(c, k, a, zn, split);
    draw_ground_span(c, k, n, split, zf);

    /* lane dashes, stretched with speed */
    float period = 9.0f, dash = 3.2f + R.speed * 0.05f + R.nitro_k * 2.5f;
    float first = floorf(zn / period) * period;
    for (float d = first; d < k->pos.z + 180.0f; d += period) {
        float d0 = d < zn ? zn : d, d1 = d + dash;
        if (d1 <= zn) continue;
        const Biome *bb = d < R.gate_z ? a : n;
        for (int l = 1; l < LANES; l++) {
            float lx = -ROAD_HW + LANE_W * (float)l;
            ground_quad(c, k, lx - 0.11f, lx + 0.11f, d0, d1, 0.0f, amul(pa_hex(bb->dash), 0.92f));
        }
    }
    /* chequered finish strip at the gate */
    if (R.gate_z > zn && R.gate_z < k->pos.z + 260.0f) {
        for (int i = 0; i < 16; i++)
            for (int j = 0; j < 2; j++)
                ground_quad(c, k, -ROAD_HW + (float)i * ROAD_HW / 8.0f, -ROAD_HW + (float)(i + 1) * ROAD_HW / 8.0f,
                            R.gate_z + (float)j * 0.9f, R.gate_z + (float)(j + 1) * 0.9f, 0.01f,
                            ((i + j) & 1) ? pa_hex(0x26232E) : pa_hex(0xFFFFFF));
    }
    /* pools of light under the night lamps */
    for (int i = 0; i < R.nprop; i++) {
        const Prop *p = &R.prop[i];
        if (p->kind != P_LAMP || p->biome != B_NIGHT) continue;
        float lx = p->x - (p->x > 0 ? 2.75f : -2.75f);
        PA_Vec2 ctr;
        if (!cam_proj(k, v3(lx, 0, p->z), &ctr)) continue;
        float ppm = cam_ppm(k, v3(lx, 0, p->z));
        if (ppm < 1.0f) continue;
        PA_Paint g = pa_radial(ctr.x, ctr.y, 0.0f, ppm * 3.6f);
        pa_stop(&g, 0.0f, PA_RGBA(230, 200, 255, 90));
        pa_stop(&g, 1.0f, PA_RGBA(230, 200, 255, 0));
        fell_paint(c, ctr.x, ctr.y, ppm * 3.6f, ppm * 3.6f * (CAM_H / ((p->z - k->pos.z) > 1.0f ? (p->z - k->pos.z) : 1.0f)) * 3.0f + 1.0f, &g);
    }
}

/* ---- props ---- */
static float fog_at(const Cam *k, float z) { return pa_clamp01((z - k->pos.z - 120.0f) / 90.0f); }

static void draw_building(PA_Canvas *c, const Cam *k, const Prop *p, float fog) {
    static Model tmp;
    const Biome *b = &BIOMES[p->biome];
    tmp.n = 0;
    PA_Rng r;
    pa_rng_seed(&r, p->seed);
    float hw = p->w * 0.5f, hd = p->d * 0.5f, s = p->x > 0 ? -1.0f : 1.0f;
    PA_Color body;
    if (p->kind == P_TOWER) {
        static const uint32_t TW[] = { 0x2E6BEA, 0x3F7CF2, 0x2A5FD8, 0x4A8CFF };
        body = pa_hex(TW[p->seed & 3]);
        box(&tmp, ROLE_BODY, 0, 1, MAT_SOLID, 0, 0, 0, p->w, p->h, p->d);
        /* window bands on the road side and the near face */
        for (float y = 2.0f; y < p->h - 1.5f; y += 2.6f)
            decal_x(&tmp, ROLE_FIX, 0x9CD0FF, MAT_SOLID, s * (hw + 0.01f), -hd + 0.6f, hd - 0.6f, y, y + 1.1f);
        for (float y = 2.0f; y < p->h - 1.5f; y += 2.6f)
            decal_z(&tmp, ROLE_FIX, 0x6FA8FF, MAT_SOLID, -hd - 0.01f, -hw + 0.6f, hw - 0.6f, y, y + 1.1f);
        box(&tmp, ROLE_FIX, 0x7FB6FF, 2, MAT_SOLID, 0, p->h, 0, p->w * 0.6f, 1.2f, p->d * 0.6f);
    } else if (p->kind == P_BLOCK) {
        static const uint32_t BW[] = { 0x4A2290, 0x3C1C7A, 0x5A2AA6 };
        body = pa_hex(BW[p->seed % 3]);
        box(&tmp, ROLE_BODY, 0, 1, MAT_SOLID, 0, 0, 0, p->w, p->h, p->d);
        for (float y = 1.4f; y < p->h - 1.0f; y += 1.8f)
            for (float zz = -hd + 0.8f; zz < hd - 1.2f; zz += 1.6f)
                if (pa_rng_chance(&r, 0.55f))
                    decal_x(&tmp, ROLE_FIX, pa_rng_chance(&r, 0.7f) ? 0xFFC86A : 0xFF8AD8, MAT_EMIT, s * (hw + 0.01f), zz, zz + 0.8f, y, y + 0.9f);
        for (float y = 1.4f; y < p->h - 1.0f; y += 1.8f)
            for (float xx = -hw + 0.8f; xx < hw - 1.2f; xx += 1.6f)
                if (pa_rng_chance(&r, 0.5f))
                    decal_z(&tmp, ROLE_FIX, 0xFFC86A, MAT_EMIT, -hd - 0.01f, xx, xx + 0.8f, y, y + 0.9f);
    } else {
        static const uint32_t HW[] = { 0xFFF2E0, 0xFFE1C8, 0xF6F0FF };
        body = pa_hex(HW[p->seed % 3]);
        box(&tmp, ROLE_BODY, 0, 1, MAT_SOLID, 0, 0, 0, p->w, p->h, p->d);
        float zy[6] = { -hd - 0.2f, p->h, hd + 0.2f, p->h, 0.0f, p->h + 2.0f };
        prism_x(&tmp, ROLE_FIX, 0xE0483A, 2, MAT_SOLID, zy, 3, -hw - 0.2f, hw + 0.2f);
        decal_x(&tmp, ROLE_FIX, 0x2B3352, MAT_SOLID, s * (hw + 0.01f), -0.6f, 0.6f, 1.0f, 2.0f);
        decal_z(&tmp, ROLE_FIX, 0x8A5A34, MAT_SOLID, -hd - 0.01f, -0.5f, 0.5f, 0.0f, 2.0f);
    }
    (void)b;
    M3 I = m3_euler(0, 0, 0);
    Look lk = { body, body, body, pa_hex(BIOMES[p->biome].fog), fog, 1.0f, 0, 0 };
    draw_model(c, k, &tmp, v3(p->x, 0, p->z), &I, 1.0f, &lk);
}

static void draw_board_face(PA_Canvas *c, const Cam *k, const Prop *p, float fog) {
    PA_Vec2 q[4];
    float z = p->z - 0.16f;
    if (!cam_proj(k, v3(p->x - 2.6f, 2.8f, z), &q[0]) || !cam_proj(k, v3(p->x + 2.6f, 2.8f, z), &q[1]) ||
        !cam_proj(k, v3(p->x + 2.6f, 5.6f, z), &q[2]) || !cam_proj(k, v3(p->x - 2.6f, 5.6f, z), &q[3])) return;
    static const uint32_t BG[] = { 0xFFC21A, 0x3BA0F0, 0xFF4FA8, 0x47D78C };
    PA_Color bg = pa_mix(pa_hex(BG[p->seed & 3]), pa_hex(BIOMES[p->biome].fog), fog);
    fpoly(c, q, 4, bg);
    float cx = (q[0].x + q[1].x + q[2].x + q[3].x) * 0.25f, cy = (q[0].y + q[1].y + q[2].y + q[3].y) * 0.25f;
    float hgt = fabsf(q[0].y - q[3].y);
    if (hgt > 10.0f && fog < 0.5f) {
        static const int FACES[] = { EMO_COOL, EMO_LAUGH, EMO_SHOCK, EMO_SWEAT };
        draw_emoji(c, cx - (q[1].x - q[0].x) * 0.22f, cy, hgt * 0.36f, FACES[(p->seed >> 2) & 3]);
        float w = (q[1].x - q[0].x);
        rrect(c, cx + w * 0.02f, cy - hgt * 0.20f, w * 0.36f, hgt * 0.14f, hgt * 0.06f, PA_RGBA(255, 255, 255, 220));
        rrect(c, cx + w * 0.02f, cy + hgt * 0.04f, w * 0.26f, hgt * 0.14f, hgt * 0.06f, PA_RGBA(255, 255, 255, 160));
    }
}

static void draw_prop(PA_Canvas *c, const Cam *k, const Prop *p) {
    float fog = fog_at(k, p->z);
    const Biome *b = &BIOMES[p->biome];
    M3 I = m3_euler(0, 0, 0);
    if (p->kind == P_TREE || p->kind == P_BUSH) {
        PA_Color leaf = pa_hex(b->leaf);
        if (p->seed & 1) leaf = pa_shade(leaf, 0.10f);
        Look lk = { leaf, leaf, leaf, pa_hex(b->fog), fog, 1.0f, 0, 0 };
        float sc = p->kind == P_BUSH ? p->w * 0.55f : p->w * (p->biome == B_SUNSET ? 1.25f : 1.0f);
        draw_model(c, k, &g_model[VM_TREE], v3(p->x, 0, p->z), &I, sc, &lk);
    } else if (p->kind == P_LAMP) {
        int night = p->biome == B_NIGHT;
        PA_Color head = night ? pa_hex(b->lamp) : pa_hex(0xE8E8F2);
        Look lk = { head, head, head, pa_hex(b->fog), fog, 1.0f, 0, 0 };
        M3 rot = m3_euler(p->x < 0 ? PA_PI : 0.0f, 0, 0);
        draw_model(c, k, &g_model[VM_LAMP], v3(p->x, 0, p->z), &rot, 1.0f, &lk);
        if (night) {
            float hx = p->x + (p->x > 0 ? -2.75f : 2.75f);
            PA_Vec2 hp;
            if (cam_proj(k, v3(hx, 7.1f, p->z), &hp)) {
                float ppm = cam_ppm(k, v3(hx, 7.1f, p->z));
                glow(c, hp.x, hp.y, ppm * 2.6f + 4.0f, PA_RGBA(200, 225, 255, (int)(170 * (1.0f - fog))));
            }
        }
    } else if (p->kind == P_BOARD) {
        Look lk = { 0, 0, 0, pa_hex(b->fog), fog, 1.0f, 0, 0 };
        draw_model(c, k, &g_model[VM_BOARD], v3(p->x, 0, p->z), &I, 1.0f, &lk);
        draw_board_face(c, k, p, fog);
    } else {
        draw_building(c, k, p, fog);
    }
}

static void draw_rail_seg(PA_Canvas *c, const Cam *k, int side, float z0, float z1, PA_Color rail) {
    float s = side ? 1.0f : -1.0f, x = s * RAIL_X;
    PA_Vec2 q[4];
    if (cam_proj(k, v3(x, 0.52f, z0), &q[0]) && cam_proj(k, v3(x, 0.52f, z1), &q[1]) &&
        cam_proj(k, v3(x, 0.92f, z1), &q[2]) && cam_proj(k, v3(x, 0.92f, z0), &q[3]))
        fpoly(c, q, 4, pa_shade(rail, -0.06f));
    if (cam_proj(k, v3(x, 0.92f, z0), &q[0]) && cam_proj(k, v3(x, 0.92f, z1), &q[1]) &&
        cam_proj(k, v3(x + s * 0.18f, 0.92f, z1), &q[2]) && cam_proj(k, v3(x + s * 0.18f, 0.92f, z0), &q[3]))
        fpoly(c, q, 4, pa_shade(rail, 0.12f));
    if (cam_proj(k, v3(x, 0.52f, z0), &q[0]) && cam_proj(k, v3(x, 0.52f, z1), &q[1]) &&
        cam_proj(k, v3(x, 0.58f, z1), &q[2]) && cam_proj(k, v3(x, 0.58f, z0), &q[3]))
        fpoly(c, q, 4, pa_shade(rail, -0.30f));
    /* post */
    if (cam_proj(k, v3(x + s * 0.1f, 0.0f, z0), &q[0]) && cam_proj(k, v3(x + s * 0.1f, 0.0f, z0 + 0.22f), &q[1]) &&
        cam_proj(k, v3(x + s * 0.1f, 0.52f, z0 + 0.22f), &q[2]) && cam_proj(k, v3(x + s * 0.1f, 0.52f, z0), &q[3]))
        fpoly(c, q, 4, pa_shade(rail, -0.35f));
}

static void draw_coin(PA_Canvas *c, const Cam *k, const Coin *co) {
    PA_Vec2 p;
    V3 w = v3(co->x, 0.9f + sinf(R.time * 4.0f + (float)co->spin) * 0.12f, co->z);
    if (!cam_proj(k, w, &p)) return;
    float ppm = cam_ppm(k, w);
    float r = ppm * 0.48f;
    if (r < 1.0f) return;
    float sq = fabsf(cosf(R.time * 3.4f + (float)co->spin * 0.7f));
    float rx = r * (0.18f + 0.82f * sq);
    PA_Vec2 g;
    if (cam_proj(k, v3(co->x, 0.01f, co->z), &g)) fell(c, g.x, g.y, r * 0.8f, r * 0.22f, PA_RGBA(40, 20, 60, 50));
    fell(c, p.x + r * 0.08f, p.y, rx + r * 0.10f, r, pa_hex(0xD98A12));
    fell(c, p.x, p.y, rx, r * 0.96f, pa_hex(0xFFC93C));
    fell(c, p.x, p.y, rx * 0.66f, r * 0.62f, pa_hex(0xFFDE6A));
    if (sq > 0.4f) fell(c, p.x - rx * 0.30f, p.y - r * 0.35f, rx * 0.18f, r * 0.20f, PA_RGBA(255, 255, 255, 200));
}

static void draw_part(PA_Canvas *c, const Cam *k, const Part *q) {
    PA_Vec2 p;
    if (!cam_proj(k, q->p, &p)) return;
    float ppm = cam_ppm(k, q->p);
    float life = q->t / q->life;
    float r = q->r * ppm;
    if (r < 0.6f) return;
    if (r > 46.0f * L.ui) r = 46.0f * L.ui;
    switch (q->kind) {
    case PT_SMOKE: {
        float a = 1.0f - life;
        fcirc(c, p.x + r * 0.12f, p.y + r * 0.12f, r, amul(pa_shade(q->col, -0.18f), a * 0.85f));
        fcirc(c, p.x, p.y, r * 0.92f, amul(q->col, a));
        fcirc(c, p.x - r * 0.25f, p.y - r * 0.25f, r * 0.45f, amul(PA_RGB(255, 255, 255), a * 0.7f));
        break;
    }
    case PT_BOOM: {
        float a = life < 0.7f ? 1.0f : (1.0f - life) / 0.3f;
        PA_Color col = pa_mix(q->col, pa_hex(0xFF7A2E), pa_clamp01((life - 0.30f) * 1.6f));
        fcirc(c, p.x + r * 0.15f, p.y + r * 0.15f, r, amul(pa_shade(col, -0.20f), a));
        fcirc(c, p.x, p.y, r * 0.9f, amul(col, a));
        fcirc(c, p.x - r * 0.28f, p.y - r * 0.28f, r * 0.42f, amul(pa_shade(col, 0.4f), a));
        break;
    }
    case PT_FIRE:
        fcirc(c, p.x, p.y, r * 1.5f, amul(q->col, 0.35f * (1.0f - life)));
        fcirc(c, p.x, p.y, r, amul(PA_RGB(255, 250, 220), 1.0f - life));
        break;
    case PT_SPARK:
        fcirc(c, p.x, p.y, r < 1.5f ? 1.5f : r, amul(q->col, 1.0f - life));
        break;
    case PT_CONFETTI: {
        float s = r < 2.0f ? 2.0f : r;
        float a = q->t * 9.0f;
        PA_Vec2 d[4] = { { p.x + cosf(a) * s, p.y + sinf(a) * s * 0.5f }, { p.x - sinf(a) * s * 0.6f, p.y + cosf(a) * s },
                         { p.x - cosf(a) * s, p.y - sinf(a) * s * 0.5f }, { p.x + sinf(a) * s * 0.6f, p.y - cosf(a) * s } };
        fpoly(c, d, 4, q->col);
        break;
    }
    }
}

typedef struct { float key; int kind, idx; } Drawable;
enum { DK_CAR, DK_PROP, DK_COIN, DK_PART, DK_RAIL, DK_PLAYER, DK_GATE };

static int dcmp(const void *a, const void *b) {
    float x = ((const Drawable *)a)->key, y = ((const Drawable *)b)->key;
    return x < y ? 1 : (x > y ? -1 : 0);
}

static Look car_look(PA_Color body, PA_Color acc, float fog) {
    const Biome *b = &BIOMES[R.biome];
    Look lk;
    lk.body = body; lk.acc = acc; lk.glass = pa_hex(b->glass);
    lk.fogc = pa_hex(b->fog); lk.fog = fog; lk.alpha = 1.0f; lk.sil = 0; lk.silc = 0;
    return lk;
}

static void player_pose(V3 *pos, M3 *rot) {
    if (R.state == ST_CRASH || R.state == ST_RESULTS) {
        *pos = R.pp;
        *rot = m3_euler(R.pyaw, R.ppitch, R.proll);
    } else {
        *pos = v3(R.x, 0.0f, R.z);
        float bob = R.state == ST_PLAY ? sinf(R.time * 23.0f) * 0.008f : 0.0f;
        *rot = m3_euler(R.yaw, -0.02f * R.nitro_k + bob, R.roll);
    }
}

static void draw_player(PA_Canvas *c, const Cam *k) {
    V3 pos;
    M3 rot;
    player_pose(&pos, &rot);
    const Ride *rd = &RIDES[S.car];
    Look lk = car_look(pa_hex(rd->body), pa_hex(rd->acc), 0.0f);
    draw_model(c, k, my_model(), pos, &rot, 1.0f, &lk);
    if (R.nitro_k > 0.05f && R.state == ST_PLAY) {
        for (int s = -1; s <= 1; s += 2) {
            V3 ex = vadd(pos, m3v(&rot, v3((float)s * 0.45f, 0.42f, -my_len() * 0.5f - 0.1f)));
            float fl = (1.6f + 0.5f * sinf(R.time * 47.0f + (float)s)) * R.nitro_k;
            V3 tip = vadd(pos, m3v(&rot, v3((float)s * 0.45f, 0.42f, -my_len() * 0.5f - 0.1f - fl)));
            PA_Vec2 a, b;
            if (!cam_proj(k, ex, &a) || !cam_proj(k, tip, &b)) continue;
            float ppm = cam_ppm(k, ex);
            float r = ppm * 0.26f;
            glow(c, a.x, a.y, r * 3.5f, PA_RGBA(255, 150, 40, 170));
            PA_Vec2 f[4] = { { a.x - r, a.y }, { a.x, a.y - r * 0.8f }, { a.x + r, a.y }, { b.x, b.y } };
            fpoly(c, f, 4, PA_RGBA(255, 140, 30, 230));
            PA_Vec2 g[4] = { { a.x - r * 0.55f, a.y }, { a.x, a.y - r * 0.45f }, { a.x + r * 0.55f, a.y },
                             { a.x + (b.x - a.x) * 0.6f, a.y + (b.y - a.y) * 0.6f } };
            fpoly(c, g, 4, PA_RGBA(120, 210, 255, 240));
            fcirc(c, a.x, a.y, r * 0.45f, PA_RGB(255, 255, 255));
        }
    }
    /* tail-light bloom: the pink rings the night plates are full of */
    if (R.state != ST_CRASH && R.state != ST_RESULTS) {
        int night = R.biome == B_NIGHT;
        float hw = my_wid() * 0.5f - 0.3f;
        for (int s = -1; s <= 1; s += 2) {
            V3 lp = vadd(pos, m3v(&rot, v3((float)s * hw, 0.75f, -my_len() * 0.5f - 0.1f)));
            PA_Vec2 sp;
            if (!cam_proj(k, lp, &sp)) continue;
            float ppm = cam_ppm(k, lp);
            glow(c, sp.x, sp.y, ppm * (night ? 1.1f : 0.6f), PA_RGBA(255, 79, 168, night ? 150 : 80));
        }
    }
}

static void cr_draw_ko(PA_Canvas *c);

static void draw_world(PA_Canvas *c, const Cam *k) {
    /* sky: the old biome under, the new one fading in */
    const Biome *a = &BIOMES[R.biome];
    if (R.sky_mix < 1.0f) {
        draw_sky_one(c, k, &BIOMES[R.sky_from], R.sky_from, 1.0f);
        draw_sky_one(c, k, a, R.biome, pa_smooth(R.sky_mix));
    } else {
        draw_sky_one(c, k, a, R.biome, 1.0f);
    }
    draw_road(c, k);

    /* cast shadows go down before anything stands on them */
    g_light = vnorm(v3(a->lx, a->ly, a->lz));
    PA_Color shc = PA_RGBA(30, 14, 70, (int)(a->shadow_a * 255.0f));
    for (int i = 0; i < R.ncar; i++) {
        const Car *cc = &R.car[i];
        if (cc->z - k->pos.z > 140.0f) continue;
        M3 rot = m3_euler(cc->yaw, cc->pitch, cc->roll);
        draw_shadow(c, k, &g_model[cc->model], v3(cc->x, cc->y, cc->z), &rot, 1.0f, shc, cc->y);
    }
    {
        V3 pos;
        M3 rot;
        player_pose(&pos, &rot);
        draw_shadow(c, k, my_model(), pos, &rot, 1.0f, shc, pos.y);
    }

    static Drawable list[MAX_CARS + MAX_PROPS + MAX_COINS + MAX_PARTS + 128];
    int n = 0;
    float cz = k->pos.z, cx = k->pos.x;
    #define PUSH(Z, X, K, I) do { list[n].key = ((Z) - cz) + fabsf((X) - cx) * 0.25f; list[n].kind = (K); list[n].idx = (I); n++; } while (0)
    for (int i = 0; i < R.ncar; i++) if (R.car[i].z - g_model[R.car[i].model].len * 0.5f > cz + 3.2f || (R.car[i].flying && R.car[i].z > cz + 1.0f)) PUSH(R.car[i].z - g_model[R.car[i].model].len * 0.5f + 1.0f, R.car[i].x, DK_CAR, i);
    for (int i = 0; i < R.nprop; i++) {
        const Prop *p = &R.prop[i];
        float zz = p->z - (p->kind == P_TOWER || p->kind == P_BLOCK || p->kind == P_HOUSE ? p->d * 0.5f : 0.0f);
        if (zz > cz + 0.5f || (p->kind == P_TOWER && p->z + p->d * 0.5f > cz + 1.0f)) PUSH(zz, p->x, DK_PROP, i);
    }
    for (int i = 0; i < R.ncoin; i++) if (R.coin[i].z > cz + 0.5f) PUSH(R.coin[i].z, R.coin[i].x, DK_COIN, i);
    for (int i = 0; i < R.npart; i++) if (R.part[i].p.z > cz + 0.4f) PUSH(R.part[i].p.z - 0.3f, R.part[i].p.x, DK_PART, i);
    float seg = 6.0f, rf = floorf(cz / seg) * seg;
    for (float z = rf; z < cz + 170.0f; z += seg) {
        if (z + seg < cz + 1.0f) continue;
        PUSH(z + seg, RAIL_X, DK_RAIL, ((int)(z / seg)) * 2);
        PUSH(z + seg, -RAIL_X, DK_RAIL, ((int)(z / seg)) * 2 + 1);
    }
    {
        V3 pos;
        M3 rot;
        player_pose(&pos, &rot);
        PUSH(pos.z - my_len() * 0.5f + 0.6f, pos.x, DK_PLAYER, 0);
    }
    if (R.gate_z > cz + 2.0f && R.gate_z < cz + 600.0f) PUSH(R.gate_z - 1.6f, 0.0f, DK_GATE, 0);
    #undef PUSH
    qsort(list, (size_t)n, sizeof(Drawable), dcmp);

    int ko_drawn = !(R.state == ST_CRASH || R.state == ST_RESULTS);
    for (int i = 0; i < n; i++) {
        const Drawable *d = &list[i];
        if (!ko_drawn && d->key < (R.impact.z - cz) + 3.0f) {
            /* the KO! billboard stands behind the wreck, as in the plates */
            ko_drawn = 1;
            cr_draw_ko(c);
        }
        switch (d->kind) {
        case DK_CAR: {
            const Car *cc = &R.car[d->idx];
            float fog = fog_at(k, cc->z);
            Look lk = car_look(cc->body, cc->acc, fog);
            M3 rot = m3_euler(cc->yaw, cc->pitch, cc->roll);
            draw_model(c, k, &g_model[cc->model], v3(cc->x, cc->y, cc->z), &rot, 1.0f, &lk);
            break;
        }
        case DK_PROP: draw_prop(c, k, &R.prop[d->idx]); break;
        case DK_COIN: draw_coin(c, k, &R.coin[d->idx]); break;
        case DK_PART: draw_part(c, k, &R.part[d->idx]); break;
        case DK_RAIL: {
            int side = d->idx & 1 ? 0 : 1;
            float z0 = (float)(d->idx >> 1) * seg;
            const Biome *bb = z0 < R.gate_z ? a : &BIOMES[R.nbiome];
            draw_rail_seg(c, k, side, z0 < cz + 1.0f ? cz + 1.0f : z0, z0 + seg, pa_mix(pa_hex(bb->rail), pa_hex(bb->fog), fog_at(k, z0)));
            break;
        }
        case DK_PLAYER: draw_player(c, k); break;
        case DK_GATE: {
            M3 I = m3_euler(0, 0, 0);
            Look lk = car_look(0, 0, fog_at(k, R.gate_z));
            lk.fogc = pa_hex(BIOMES[R.nbiome].fog);
            draw_model(c, k, &g_model[VM_GATE], v3(0, 0, R.gate_z), &I, 1.0f, &lk);
            break;
        }
        }
    }
    if (!ko_drawn) cr_draw_ko(c);
}

/* ---- bubbles, speed lines, effects ---- */
static void draw_bubbles(PA_Canvas *c, const Cam *k) {
    for (int i = 0; i < R.ncar; i++) {
        const Car *cc = &R.car[i];
        if (!cc->emo || cc->emo_t < 0.0f) continue;
        const Model *m = &g_model[cc->model];
        V3 top = v3(cc->x, cc->y + m->hgt + 1.1f, cc->z - m->len * 0.25f);
        PA_Vec2 p;
        if (!cam_proj(k, top, &p)) continue;
        float ppm = cam_ppm(k, top);
        float r = pa_clampf(ppm * 0.95f, 20.0f * L.ui, 64.0f * L.ui);
        float t = cc->emo_t;
        float pop = t < 0.12f ? t / 0.12f * 1.25f : (t < 0.24f ? 1.25f - (t - 0.12f) / 0.12f * 0.25f : 1.0f);
        float a = t > 1.9f ? (2.2f - t) / 0.3f : 1.0f;
        draw_bubble(c, p.x, p.y - r * 0.4f, r * pop, cc->emo, a);
    }
}

static void draw_speedlines(PA_Canvas *c) {
    float pace = pa_clamp01((R.speed - 24.0f) / 20.0f) * 0.6f + R.nitro_k;
    if (R.state != ST_PLAY) pace *= 0.0f;
    if (pace < 0.05f) return;
    float W = (float)c->w, H = (float)c->h;
    float vx = L.cx, vy = L.hy;
    int n = 10 + (int)(R.nitro_k * 18.0f);
    for (int i = 0; i < n; i++) {
        float t = pa_wrapf(R.time * (1.5f + pace * 1.8f) + (float)i * 0.618f, 1.0f);
        float ang = pa_wrapf((float)i * 2.39996f, PA_TAU);
        float dx = cosf(ang), dy = sinf(ang);
        if (fabsf(dy) < 0.10f) dy = dy < 0 ? -0.10f : 0.10f;
        float rmax = sqrtf(W * W + H * H) * 0.6f;
        float r0 = rmax * (0.35f + t * 0.75f), r1 = r0 + rmax * (0.06f + 0.18f * pace);
        float a = pace * (t < 0.2f ? t / 0.2f : 1.0f) * 0.55f;
        sline(c, vx + dx * r0, vy + dy * r0 * 0.9f, vx + dx * r1, vy + dy * r1 * 0.9f, (1.6f + 2.0f * t) * L.ui,
              PA_RGBA(255, 255, 255, (int)(a * 255.0f)));
    }
    if (R.nitro_k > 0.05f) {
        /* warm edge glow under nitro */
        PA_Paint p = pa_radial(L.cx, (float)c->h * 0.55f, (float)c->w * 0.45f, (float)c->h * 0.75f);
        pa_stop(&p, 0.0f, PA_RGBA(255, 120, 0, 0));
        pa_stop(&p, 1.0f, PA_RGBA(255, 120, 0, (int)(70 * R.nitro_k)));
        pa_fill_rect_paint(c, 0, 0, W, H, &p);
    }
}

static void draw_rings_pops(PA_Canvas *c) {
    float px, py;
    project_player_screen(&px, &py);
    for (int i = 0; i < R.nring; i++) {
        const Ring *g = &R.ring[i];
        float t = g->t / g->life;
        float x = g->attach ? px : g->x, y = g->attach ? py : g->y;
        float e = 1.0f - (1.0f - t) * (1.0f - t);
        float r = pa_lerpf(g->r0, g->r1, e);
        float w = (1.0f - t) * 12.0f * L.ui + 2.0f;
        PA_Vec2 pts[48];
        int ok = 1;
        if (g->attach) {
            /* a shockwave on the road around the car */
            V3 ctr = v3(R.x, 0.05f, R.z);
            float rw = pa_lerpf(1.6f, 5.5f, e);
            for (int k = 0; k < 48 && ok; k++) {
                float a = (float)k / 48.0f * PA_TAU;
                ok = cam_proj(&g_cam, v3(ctr.x + cosf(a) * rw, 0.05f, ctr.z + sinf(a) * rw * 1.2f), &pts[k]);
            }
            if (ok) fpoly(c, pts, 48, amul(g->col, 0.16f * (1.0f - t)));
        } else {
            for (int k = 0; k < 48; k++) {
                float a = (float)k / 48.0f * PA_TAU;
                pts[k].x = x + cosf(a) * r;
                pts[k].y = y + sinf(a) * r;
            }
        }
        if (ok) spoly(c, pts, 48, 1, w, amul(g->col, 1.0f - t));
    }
    for (int i = 0; i < R.npop; i++) {
        const Pop *p = &R.pop[i];
        float t = p->t / p->life;
        float e = t < 0.35f ? 0.0f : pa_smooth((t - 0.35f) / 0.65f);
        float x = pa_lerpf(p->x, p->tx, e), y = pa_lerpf(p->y - t * 30.0f * L.ui, p->ty, e);
        float s = (t < 0.15f ? 0.6f + t / 0.15f * 0.6f : 1.2f - e * 0.6f) * 26.0f * L.ui;
        fcirc(c, x - s * 0.8f, y + s * 0.45f, s * 0.42f, pa_hex(0xD98A12));
        fcirc(c, x - s * 0.8f, y + s * 0.42f, s * 0.36f, pa_hex(0xFFC93C));
        txtb(c, p->txt, x - s * 0.25f, y, s, p->kind ? pa_hex(0xFF4FA8) : pa_hex(0xFFE24A), pa_hex(0x3A1A00), PA_ALIGN_LEFT, 1.3f);
    }
}

/* ---- crash overlay ---- */
static void cr_draw_ko(PA_Canvas *c) {
    if (!(R.state == ST_CRASH || R.state == ST_RESULTS)) return;
    float t = R.crash_t - 0.22f;
    if (t <= 0.0f) return;
    float s = t < 0.12f ? 0.3f + t / 0.12f * 1.0f : (t < 0.30f ? 1.3f - (t - 0.12f) / 0.18f * 0.3f : 1.0f);
    float size = 540.0f * L.ui * 0.27f * s;
    float y = (float)c->h * 0.20f - size * 0.5f;
    if (R.state == ST_RESULTS) {
        float k = pa_smooth(pa_clamp01(R.st_t / 0.4f));
        size *= 1.0f - (c->w > c->h ? 0.52f : 0.38f) * k;
        y = pa_lerpf(y, (float)c->h * (c->w > c->h ? 0.03f : 0.075f), k);
    }
    /* white frame, dark outline, gold fill */
    pa_text_bold(c, "KO!", L.cx + size * 0.04f, y + size * 0.06f, size, PA_RGBA(255, 255, 255, 255), PA_RGBA(255, 255, 255, 255),
                 PA_ALIGN_CENTER, size * 0.04f, 2.9f);
    pa_text_bold(c, "KO!", L.cx, y, size, pa_hex(0xFFC21A), pa_hex(0x3A1A00), PA_ALIGN_CENTER, size * 0.04f, 1.7f);
    pa_text(c, "KO!", L.cx - size * 0.03f, y - size * 0.03f, size, PA_RGBA(255, 236, 150, 140), PA_ALIGN_CENTER, size * 0.04f);
}

static void draw_crack(PA_Canvas *c, float amount) {
    if (amount <= 0.0f) return;
    PA_Rng r;
    pa_rng_seed(&r, R.crack_seed);
    PA_Vec2 o;
    if (!cam_proj(&g_cam, R.impact, &o)) { o.x = L.cx; o.y = (float)c->h * 0.6f; }
    o.x = pa_clampf(o.x, (float)c->w * 0.25f, (float)c->w * 0.75f);
    o.y = pa_clampf(o.y, (float)c->h * 0.35f, (float)c->h * 0.75f);
    float diag = sqrtf((float)(c->w * c->w + c->h * c->h));
    enum { NR = 11, NP = 6 };
    float ang[NR];
    for (int i = 0; i < NR; i++) ang[i] = ((float)i + pa_rng_range(&r, -0.3f, 0.3f)) / (float)NR * PA_TAU;
    PA_Vec2 ray[NR][NP];
    for (int i = 0; i < NR; i++) {
        float a = ang[i];
        for (int j = 0; j < NP; j++) {
            float d = diag * (0.05f + 0.20f * (float)j) * (j ? pa_rng_range(&r, 0.85f, 1.15f) : 0.5f);
            float aa = a + (j ? pa_rng_range(&r, -0.10f, 0.10f) : 0.0f);
            ray[i][j].x = o.x + cosf(aa) * d;
            ray[i][j].y = o.y + sinf(aa) * d;
        }
    }
    float grow = pa_clamp01(amount);
    int jmax = 1 + (int)(grow * (float)(NP - 1));
    /* shards: alternate panes catch the light differently */
    for (int i = 0; i < NR; i++) {
        int i2 = (i + 1) % NR;
        float tone = pa_rng_next(&r);
        for (int j = 1; j < jmax; j++) {
            PA_Vec2 q[4] = { ray[i][j - 1], ray[i][j], ray[i2][j], ray[i2][j - 1] };
            if (((i + j) & 1) == 0)
                fpoly(c, q, 4, tone > 0.5f ? PA_RGBA(255, 255, 255, 34) : PA_RGBA(40, 20, 60, 30));
            else if (tone > 0.75f)
                fpoly(c, q, 4, PA_RGBA(255, 255, 255, 18));
        }
    }
    float lw = 3.4f * L.ui;
    for (int i = 0; i < NR; i++) {
        PA_Vec2 pts[NP];
        int m = 0;
        for (int j = 0; j < jmax; j++) pts[m++] = ray[i][j];
        for (int j = 0; j < m; j++) { pts[j].x += 1.5f; pts[j].y += 1.5f; }
        spoly(c, pts, m, 0, lw + 1.5f, PA_RGBA(30, 10, 40, 70));
        for (int j = 0; j < m; j++) { pts[j].x -= 1.5f; pts[j].y -= 1.5f; }
        spoly(c, pts, m, 0, lw, PA_RGBA(255, 255, 255, 235));
    }
    /* cross cracks between some rays */
    for (int i = 0; i < NR; i++) {
        int i2 = (i + 1) % NR;
        for (int j = 1; j < jmax; j++)
            if (pa_rng_chance(&r, 0.45f))
                sline(c, ray[i][j].x, ray[i][j].y, ray[i2][j].x, ray[i2][j].y, lw * 0.7f, PA_RGBA(255, 255, 255, 200));
    }
    fcirc(c, o.x, o.y, diag * 0.02f, PA_RGBA(255, 255, 255, 120));
}

/* ---- HUD ---- */
static void draw_coin_icon(PA_Canvas *c, float x, float y, float r) {
    fcirc(c, x + r * 0.08f, y + r * 0.10f, r, pa_hex(0xC07408));
    fcirc(c, x, y, r, pa_hex(0xFFC93C));
    fcirc(c, x, y, r * 0.66f, pa_hex(0xFFDE6A));
    rrect(c, x - r * 0.16f, y - r * 0.42f, r * 0.32f, r * 0.84f, r * 0.12f, pa_hex(0xE8A21E));
}

static void model_icon(PA_Canvas *c, int model, PA_Color body, PA_Color acc, float cx, float cy, float size,
                       float yaw, float pitch, int sil) {
    const Model *m = &g_model[model];
    float rad = 0.5f * sqrtf(m->len * m->len + m->wid * m->wid + m->hgt * m->hgt);
    float dist = rad * 7.0f;
    V3 tgt = v3(0, m->hgt * 0.45f, 0);
    V3 fwd = v3(sinf(yaw) * cosf(pitch), -sinf(pitch), cosf(yaw) * cosf(pitch));
    Cam k;
    cam_init(&k, vsub(tgt, vmul(fwd, dist)), yaw, pitch, size * 0.5f * dist / rad, cx, cy);
    V3 keep = g_light;
    g_light = vnorm(v3(0.55f, 0.75f, 0.45f));
    Look lk;
    lk.body = body; lk.acc = acc; lk.glass = pa_hex(0x2B3350); lk.fogc = 0; lk.fog = 0.0f; lk.alpha = 1.0f;
    lk.sil = sil; lk.silc = pa_hex(0x1C0A18);
    M3 I = m3_euler(0, 0, 0);
    draw_model(c, &k, m, v3(0, 0, 0), &I, 1.0f, &lk);
    g_light = keep;
}

static void draw_progress_bar(PA_Canvas *c, float cx, float y, float bw, float prog, int lvl, int dark_text) {
    float bh = 13.0f * L.ui;
    float x0 = cx - bw * 0.5f;
    char b[16];
    PA_Color tc = dark_text ? pa_hex(0x2A1458) : PA_RGB(255, 255, 255);
    snprintf(b, sizeof(b), "%d", lvl);
    if (dark_text) txt(c, b, x0 - 24.0f * L.ui, y - 11.0f * L.ui, 22.0f * L.ui, tc, PA_ALIGN_RIGHT);
    else txtb(c, b, x0 - 24.0f * L.ui, y - 11.0f * L.ui, 22.0f * L.ui, tc, PA_RGBA(20, 10, 50, 160), PA_ALIGN_RIGHT, 1.0f);
    snprintf(b, sizeof(b), "%d", lvl + 1);
    if (dark_text) txt(c, b, x0 + bw + 24.0f * L.ui, y - 11.0f * L.ui, 22.0f * L.ui, tc, PA_ALIGN_LEFT);
    else txtb(c, b, x0 + bw + 24.0f * L.ui, y - 11.0f * L.ui, 22.0f * L.ui, tc, PA_RGBA(20, 10, 50, 160), PA_ALIGN_LEFT, 1.0f);
    rrect(c, x0, y - bh * 0.5f, bw, bh, 3.0f * L.ui, dark_text ? PA_RGBA(42, 20, 88, 50) : PA_RGBA(255, 255, 255, 90));
    rrect(c, x0, y - bh * 0.5f, bw * pa_clamp01(prog), bh, 3.0f * L.ui, dark_text ? pa_hex(0xFF7A00) : PA_RGBA(255, 255, 255, 120));
    for (float d = x0 + 10.0f * L.ui; d < x0 + bw - 12.0f * L.ui; d += 22.0f * L.ui)
        pa_fill_rect(c, d, y - 1.5f * L.ui, 10.0f * L.ui, 3.0f * L.ui, dark_text ? PA_RGBA(42, 20, 88, 90) : PA_RGBA(40, 20, 90, 120));
    const Ride *rd = &RIDES[S.car];
    model_icon(c, rd->model, pa_hex(rd->body), pa_hex(rd->acc), x0 + bw * pa_clamp01(prog), y - 8.0f * L.ui, 46.0f * L.ui,
               PA_PI * 0.5f, 0.12f, 0);
}

static void draw_nitro_button(PA_Canvas *c) {
    float r = 54.0f * L.ui;
    float cx = (float)c->w - 84.0f * L.ui, cy = (float)c->h - 96.0f * L.ui;
    int ready = R.nitro >= 0.999f && R.nitro_t <= 0.0f;
    float pulse = ready ? 1.0f + 0.06f * sinf(R.time * 10.0f) : 1.0f;
    if (ready) glow(c, cx, cy, r * 2.0f, PA_RGBA(255, 140, 0, 150));
    fcirc(c, cx, cy + 5.0f * L.ui, r * pulse, PA_RGBA(80, 20, 0, 110));
    fcirc(c, cx, cy, r * pulse, R.nitro_t > 0.0f ? pa_hex(0xFFB01E) : (ready ? pa_hex(0xFF7A00) : pa_hex(0x8A5A3A)));
    fcirc(c, cx, cy - r * 0.08f, r * 0.82f * pulse, R.nitro_t > 0.0f ? pa_hex(0xFFC93C) : (ready ? pa_hex(0xFF9A2E) : pa_hex(0x9E6A48)));
    /* charge ring */
    float k = R.nitro_t > 0.0f ? R.nitro_t / 3.2f : R.nitro;
    arc(c, cx, cy, r * 1.14f, 0.0f, PA_TAU, 7.0f * L.ui, PA_RGBA(30, 10, 40, 120));
    if (k > 0.01f) arc(c, cx, cy, r * 1.14f, -PA_PI * 0.5f, -PA_PI * 0.5f + PA_TAU * k, 7.0f * L.ui, pa_hex(0xFFE24A));
    /* flame glyph */
    float s = r * 0.42f;
    PA_Vec2 f1[7] = { { cx, cy - s * 1.3f }, { cx + s * 0.75f, cy - s * 0.1f }, { cx + s * 0.6f, cy + s * 0.6f }, { cx, cy + s * 0.95f },
                      { cx - s * 0.6f, cy + s * 0.6f }, { cx - s * 0.75f, cy - s * 0.1f }, { cx - s * 0.25f, cy - s * 0.45f } };
    fpoly(c, f1, 7, PA_RGB(255, 255, 255));
    PA_Vec2 f2[5] = { { cx, cy - s * 0.2f }, { cx + s * 0.38f, cy + s * 0.35f }, { cx, cy + s * 0.75f }, { cx - s * 0.38f, cy + s * 0.35f }, { cx, cy - s * 0.2f } };
    fpoly(c, f2, 5, ready || R.nitro_t > 0.0f ? pa_hex(0xFF7A00) : pa_hex(0x9E6A48));
    txtb(c, "NITRO", cx, cy + r * 1.30f, 15.0f * L.ui, PA_RGB(255, 255, 255), PA_RGBA(40, 10, 0, 200), PA_ALIGN_CENTER, 1.0f);
}

static void draw_hud(PA_Canvas *c, float alpha) {
    if (alpha <= 0.01f) return;
    float u = L.ui;
    /* coins, top-left */
    char b[32];
    snprintf(b, sizeof(b), "%d", S.coins + (R.state == ST_PLAY ? R.run_coins : 0));
    float pw = pa_text_width(b, 24.0f * u, 1.4f * u) + 66.0f * u;
    rrect(c, 16.0f * u, 24.0f * u, pw, 44.0f * u, 22.0f * u, PA_RGBA(20, 10, 50, 110));
    float cp = 1.0f + 0.25f * pa_clamp01(R.coin_pulse);
    draw_coin_icon(c, 38.0f * u, 46.0f * u, 15.0f * u * cp);
    txtb(c, b, 62.0f * u, 34.0f * u, 24.0f * u, PA_RGB(255, 255, 255), PA_RGBA(20, 10, 50, 200), PA_ALIGN_LEFT, 1.0f);

    /* level progress, the plates' signature strip */
    float prog = pa_clamp01((R.z - R.lvl_z0) / R.lvl_len);
    float bw = (float)c->w * 0.56f;
    if (bw > 420.0f * u) bw = 420.0f * u;
    draw_progress_bar(c, L.cx, 104.0f * u, bw, prog, R.level, 0);

    /* distance: heavy display numerals */
    int dist = (int)(R.z - R.run_z0);
    snprintf(b, sizeof(b), "%d", dist);
    float ds = 64.0f * u;
    txtb(c, b, L.cx, 128.0f * u, ds, PA_RGB(255, 255, 255), PA_RGBA(26, 10, 60, 230), PA_ALIGN_CENTER, 2.2f);
    float dw = pa_text_width(b, ds, ds * 0.06f);
    txtb(c, "M", L.cx + dw * 0.5f + 10.0f * u, 128.0f * u + ds * 0.42f, ds * 0.5f, PA_RGB(255, 255, 255), PA_RGBA(26, 10, 60, 230), PA_ALIGN_LEFT, 1.6f);
    if (S.best > 0 && R.state == ST_PLAY) {
        snprintf(b, sizeof(b), dist > S.best ? "NEW BEST!" : "BEST %d", dist > S.best ? 0 : S.best);
        txtb(c, b, L.cx, 128.0f * u + ds + 18.0f * u, 17.0f * u, dist > S.best ? pa_hex(0xFFE24A) : PA_RGBA(255, 255, 255, 220),
             PA_RGBA(26, 10, 60, 180), PA_ALIGN_CENTER, 1.0f);
    }
    if (R.state == ST_PLAY) draw_nitro_button(c);

    /* banners */
    if (R.banner_t > 0.0f && R.state == ST_PLAY) {
        float t = 1.3f - R.banner_t;
        float pop = t < 0.10f ? 0.5f + t / 0.10f * 0.75f : (t < 0.22f ? 1.25f - (t - 0.10f) / 0.12f * 0.25f : 1.0f);
        float a = R.banner_t < 0.25f ? R.banner_t / 0.25f : 1.0f;
        float s = 32.0f * u * pop;
        txtb(c, R.banner, L.cx, (float)c->h * 0.285f, s, amul(R.banner_col, a), PA_RGBA(58, 26, 0, (int)(230 * a)), PA_ALIGN_CENTER, 2.0f);
    }
}

static void draw_touch(PA_Canvas *c) {
    if (R.touch_t <= 0.0f) return;
    float t = 1.0f - R.touch_t / 0.45f;
    fcirc(c, R.touch_x, R.touch_y, (60.0f + 40.0f * t) * L.ui, PA_RGBA(255, 236, 80, (int)(70 * (1.0f - t))));
    fcirc(c, R.touch_x, R.touch_y, 30.0f * L.ui, PA_RGBA(255, 236, 80, (int)(140 * (1.0f - t))));
}

/* ---- menu ---- */
static void draw_menu(PA_Canvas *c) {
    float u = L.ui;
    float bob = sinf(R.time * 2.2f) * 4.0f * u;
    PA_Paint p = pa_linear(0, 0, 0, (float)c->h * 0.36f);
    pa_stop(&p, 0.0f, PA_RGBA(10, 4, 40, 120));
    pa_stop(&p, 1.0f, PA_RGBA(10, 4, 40, 0));
    pa_fill_rect_paint(c, 0, 0, (float)c->w, (float)c->h * 0.36f, &p);
    txtb(c, "CHROME", L.cx, (float)c->h * 0.11f + bob, 62.0f * u, PA_RGB(255, 255, 255), pa_hex(0x2A1458), PA_ALIGN_CENTER, 2.4f);
    txtb(c, "RUSH", L.cx, (float)c->h * 0.11f + 74.0f * u + bob, 84.0f * u, pa_hex(0xFFC21A), pa_hex(0x3A1A00), PA_ALIGN_CENTER, 2.6f);
    char b[32];
    snprintf(b, sizeof(b), "LEVEL %d", S.level);
    float lw = pa_text_width(b, 22.0f * u, 1.3f * u) + 44.0f * u;
    float ly = (float)c->h * 0.11f + 182.0f * u;
    rrect(c, L.cx - lw * 0.5f, ly, lw, 42.0f * u, 21.0f * u, pa_hex(0xFF4FA8));
    txt(c, b, L.cx, ly + 10.0f * u, 22.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_CENTER);
    if (S.best > 0) {
        snprintf(b, sizeof(b), "BEST %d M", S.best);
        txtb(c, b, L.cx, ly + 56.0f * u, 18.0f * u, PA_RGB(255, 255, 255), PA_RGBA(20, 10, 50, 200), PA_ALIGN_CENTER, 1.0f);
    }
    /* swipe hint with a sliding finger */
    float hy = (float)c->h * 0.555f;
    float k = sinf(R.time * 3.0f);
    txtb(c, "SWIPE TO DRIVE", L.cx, hy, 28.0f * u, PA_RGB(255, 255, 255), PA_RGBA(20, 10, 50, 220), PA_ALIGN_CENTER, 1.5f);
    float fx = L.cx + k * 70.0f * u, fy = hy + 70.0f * u;
    txtb(c, "<", L.cx - 120.0f * u, fy - 16.0f * u, 30.0f * u, PA_RGBA(255, 255, 255, 200), PA_RGBA(20, 10, 50, 160), PA_ALIGN_CENTER, 1.2f);
    txtb(c, ">", L.cx + 120.0f * u, fy - 16.0f * u, 30.0f * u, PA_RGBA(255, 255, 255, 200), PA_RGBA(20, 10, 50, 160), PA_ALIGN_CENTER, 1.2f);
    fcirc(c, fx, fy, 34.0f * u, PA_RGBA(255, 236, 80, 90));
    fcirc(c, fx, fy, 18.0f * u, PA_RGBA(255, 255, 255, 230));
    /* garage button */
    Box gb = btn_garage_menu();
    rrect(c, gb.x, gb.y + 6.0f * u, gb.w, gb.h, 20.0f * u, PA_RGBA(120, 50, 0, 160));
    rrect(c, gb.x, gb.y, gb.w, gb.h, 20.0f * u, pa_hex(0xFE9416));
    rrect(c, gb.x + 6.0f * u, gb.y + 6.0f * u, gb.w - 12.0f * u, gb.h - 30.0f * u, 14.0f * u, pa_hex(0xF6F4F6));
    const Ride *rd = &RIDES[S.car];
    model_icon(c, rd->model, pa_hex(rd->body), pa_hex(rd->acc), gb.x + gb.w * 0.5f, gb.y + gb.h * 0.40f, gb.w * 0.62f, PA_PI * 1.22f, 0.45f, 0);
    txt(c, "GARAGE", gb.x + gb.w * 0.5f, gb.y + gb.h - 21.0f * u, 14.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_CENTER);
}

/* ---- results ---- */
static void draw_results(PA_Canvas *c) {
    float u = L.ui;
    float k = pa_smooth(pa_clamp01(R.st_t / 0.45f));
    pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(30, 10, 60, (int)(130 * k)));
    float cw = (float)c->w - 56.0f * u;
    if (cw > 480.0f * u) cw = 480.0f * u;
    float ccx = c->w > c->h ? (float)c->w * 0.38f : L.cx;
    float x0 = ccx - cw * 0.5f;
    float ch = 420.0f * u;
    float y0 = (float)c->h * 0.205f + (1.0f - k) * (float)c->h * 0.6f;
    rrect(c, x0, y0 + 8.0f * u, cw, ch, 26.0f * u, PA_RGBA(20, 6, 40, 120));
    rrect(c, x0, y0, cw, ch, 26.0f * u, pa_hex(0xFBFAFD));
    /* level + progress */
    float prog = pa_clamp01((R.z - R.lvl_z0) / R.lvl_len);
    char b[40];
    snprintf(b, sizeof(b), "LEVEL %d  -  %d%%", R.level, (int)(prog * 100.0f));
    txt(c, b, ccx, y0 + 22.0f * u, 18.0f * u, pa_hex(0xFE7A16), PA_ALIGN_CENTER);
    draw_progress_bar(c, ccx, y0 + 78.0f * u, cw - 120.0f * u, prog, R.level, 1);
    /* distance */
    int dist = (int)(R.z - R.run_z0);
    snprintf(b, sizeof(b), "%d", dist);
    float ds = 70.0f * u;
    txtb(c, b, ccx - 12.0f * u, y0 + 112.0f * u, ds, pa_hex(0x2A1458), pa_hex(0x2A1458), PA_ALIGN_CENTER, 0.9f);
    float dw = pa_text_width(b, ds, ds * 0.06f);
    txtb(c, "M", ccx - 12.0f * u + dw * 0.5f + 10.0f * u, y0 + 112.0f * u + ds * 0.45f, ds * 0.45f, pa_hex(0x2A1458), pa_hex(0x2A1458), PA_ALIGN_LEFT, 0.9f);
    if (R.new_best) {
        float bw = 200.0f * u;
        float pop = 1.0f + 0.05f * sinf(R.time * 8.0f);
        rrect(c, ccx - bw * 0.5f * pop, y0 + 200.0f * u, bw * pop, 40.0f * u, 20.0f * u, pa_hex(0xFFC21A));
        txt(c, "NEW BEST!", ccx, y0 + 210.0f * u, 20.0f * u, pa_hex(0x3A1A00), PA_ALIGN_CENTER);
    } else {
        snprintf(b, sizeof(b), "BEST  %d M", S.best);
        txt(c, b, ccx, y0 + 210.0f * u, 20.0f * u, pa_hex(0x8A80A8), PA_ALIGN_CENTER);
    }
    /* stat chips */
    float cy = y0 + 262.0f * u, chw = (cw - 64.0f * u) / 3.0f;
    for (int i = 0; i < 3; i++) {
        float cx0 = x0 + 20.0f * u + (float)i * (chw + 12.0f * u);
        rrect(c, cx0, cy, chw, 58.0f * u, 14.0f * u, pa_hex(0xF0ECF6));
        if (i == 0) {
            draw_coin_icon(c, cx0 + 24.0f * u, cy + 29.0f * u, 13.0f * u);
            snprintf(b, sizeof(b), "+%d", R.earned);
            txt(c, b, cx0 + 44.0f * u, cy + 19.0f * u, 21.0f * u, pa_hex(0x2A1458), PA_ALIGN_LEFT);
        } else {
            txt(c, i == 1 ? "NEAR" : "SMASH", cx0 + chw * 0.5f, cy + 9.0f * u, 12.0f * u, pa_hex(0x8A80A8), PA_ALIGN_CENTER);
            snprintf(b, sizeof(b), "%d", i == 1 ? R.run_near : R.run_smash);
            txt(c, b, cx0 + chw * 0.5f, cy + 28.0f * u, 20.0f * u, i == 1 ? pa_hex(0xFF4FA8) : pa_hex(0xFF7A00), PA_ALIGN_CENTER);
        }
    }
    /* next unlock teaser */
    int next = -1;
    for (int i = 0; i < RIDE_COUNT; i++) if (!(S.own & (1 << i))) { next = i; break; }
    float ty = y0 + 338.0f * u;
    if (next >= 0) {
        rrect(c, x0 + 20.0f * u, ty, 68.0f * u, 64.0f * u, 14.0f * u, pa_hex(0xF0ECF6));
        model_icon(c, RIDES[next].model, 0, 0, x0 + 54.0f * u, ty + 32.0f * u, 56.0f * u, PA_PI * 1.22f, 0.45f, 1);
        txt(c, "NEXT RIDE", x0 + 104.0f * u, ty + 6.0f * u, 14.0f * u, pa_hex(0x8A80A8), PA_ALIGN_LEFT);
        float bw = cw - 130.0f * u;
        float f = pa_clamp01((float)S.coins / (float)RIDES[next].price);
        rrect(c, x0 + 104.0f * u, ty + 30.0f * u, bw, 18.0f * u, 9.0f * u, pa_hex(0xE6E0EE));
        rrect(c, x0 + 104.0f * u, ty + 30.0f * u, bw * f > 18.0f * u ? bw * f : 18.0f * u, 18.0f * u, 9.0f * u, pa_hex(0xFFC21A));
        if (S.coins >= RIDES[next].price) snprintf(b, sizeof(b), "READY TO UNLOCK!");
        else snprintf(b, sizeof(b), "%d / %d", S.coins, RIDES[next].price);
        txt(c, b, x0 + 104.0f * u + bw, ty + 6.0f * u, 14.0f * u, S.coins >= RIDES[next].price ? pa_hex(0x2FB86A) : pa_hex(0x2A1458), PA_ALIGN_RIGHT);
    } else {
        txt(c, "GARAGE COMPLETE!", ccx, ty + 20.0f * u, 18.0f * u, pa_hex(0x2A1458), PA_ALIGN_CENTER);
    }
    /* buttons */
    Box d = btn_drive();
    d.y += (1.0f - k) * (float)c->h * 0.4f;
    float pulse = 1.0f + 0.03f * sinf(R.time * 6.0f);
    rrect(c, d.x, d.y + 7.0f * u, d.w, d.h, d.h * 0.5f, pa_hex(0xB04A00));
    rrect(c, d.x - (pulse - 1.0f) * d.w * 0.5f, d.y, d.w * pulse, d.h, d.h * 0.5f, pa_hex(0xFF7A00));
    txtb(c, "DRIVE AGAIN", d.x + d.w * 0.5f, d.y + d.h * 0.5f - 15.0f * u, 30.0f * u, PA_RGB(255, 255, 255), pa_hex(0xB04A00), PA_ALIGN_CENTER, 1.3f);
    Box g = btn_garage_results();
    g.y += (1.0f - k) * (float)c->h * 0.4f;
    rrect(c, g.x, g.y + 5.0f * u, g.w, g.h, g.h * 0.5f, PA_RGBA(20, 6, 40, 120));
    rrect(c, g.x, g.y, g.w, g.h, g.h * 0.5f, pa_hex(0xFBFAFD));
    txt(c, "GARAGE", g.x + g.w * 0.5f, g.y + g.h * 0.5f - 10.0f * u, 21.0f * u, pa_hex(0xFE7A16), PA_ALIGN_CENTER);
}

/* ---- garage ---- */
static void draw_driver_avatar(PA_Canvas *c, float x, float y, float r) {
    fcirc(c, x, y, r, pa_hex(0xFE9416));
    face_disc(c, x, y + r * 0.08f, r * 0.74f, 0xFFD64A, 0xFFA830);
    /* racing helmet with visor up */
    PA_Vec2 h[24];
    int n = 0;
    for (int i = 0; i <= 20; i++) {
        float a = PA_PI + (float)i / 20.0f * PA_PI;
        h[n].x = x + cosf(a) * r * 0.82f; h[n].y = y - r * 0.05f + sinf(a) * r * 0.80f; n++;
    }
    fpoly(c, h, n, pa_hex(0xE8283C));
    rrect(c, x - r * 0.84f, y - r * 0.12f, r * 1.68f, r * 0.20f, r * 0.08f, pa_hex(0xB81E30));
    rrect(c, x - r * 0.10f, y - r * 0.84f, r * 0.20f, r * 0.72f, r * 0.08f, PA_RGB(255, 255, 255));
    fell(c, x - r * 0.26f, y + r * 0.22f, r * 0.08f, r * 0.12f, pa_hex(0x3A1A10));
    fell(c, x + r * 0.26f, y + r * 0.22f, r * 0.08f, r * 0.12f, pa_hex(0x3A1A10));
    arc(c, x, y + r * 0.30f, r * 0.30f, PA_PI * 0.2f, PA_PI * 0.8f, r * 0.07f, pa_hex(0x3A1A10));
}

static void draw_garage(PA_Canvas *c) {
    float u = L.ui;
    pa_clear(c, pa_hex(0xFE9416));
    PA_Paint bg = pa_linear(0, 0, 0, (float)c->h);
    pa_stop(&bg, 0.0f, pa_hex(0xFFA22A));
    pa_stop(&bg, 1.0f, pa_hex(0xFB8A10));
    pa_fill_rect_paint(c, 0, 0, (float)c->w, (float)c->h, &bg);
    float m = 22.0f * u;
    float cw = (float)c->w - m * 2.0f;
    if (cw > 560.0f * u) cw = 560.0f * u;
    float x0 = ((float)c->w - cw) * 0.5f;
    float top = 86.0f * u - R.gscroll;

    /* header card */
    float hh = 214.0f * u;
    rrect(c, x0, top + 7.0f * u, cw, hh, 18.0f * u, pa_hex(0xE07400));
    rrect(c, x0, top, cw, hh, 18.0f * u, pa_hex(0xF6F4F6));
    draw_driver_avatar(c, x0 + 104.0f * u, top + hh * 0.5f, 84.0f * u);
    float tx = x0 + 214.0f * u;
    PA_Color oc = pa_hex(0xFE8A10);
    txt(c, "DRIVER", tx, top + 42.0f * u, 20.0f * u, oc, PA_ALIGN_LEFT);
    char b[40];
    snprintf(b, sizeof(b), "LV %d", S.level);
    txtb(c, b, tx, top + 72.0f * u, 54.0f * u, oc, oc, PA_ALIGN_LEFT, 0.9f);
    snprintf(b, sizeof(b), "DRIVE TIME %d:%02d", S.time_s / 3600, (S.time_s / 60) % 60);
    txt(c, b, tx, top + 146.0f * u, 16.0f * u, oc, PA_ALIGN_LEFT);
    draw_coin_icon(c, tx + 12.0f * u, top + 186.0f * u, 12.0f * u);
    snprintf(b, sizeof(b), "%d", S.coins);
    txt(c, b, tx + 32.0f * u, top + 177.0f * u, 18.0f * u, pa_hex(0x2A1458), PA_ALIGN_LEFT);

    /* grid */
    for (int i = 0; i < RIDE_COUNT; i++) {
        float x, y, s;
        garage_tile(i, &x, &y, &s);
        if (y > (float)c->h || y + s < 0.0f) continue;
        float shake = (R.gdeny_t > 0.0f && R.gdeny_i == i) ? sinf(R.gdeny_t * 60.0f) * 6.0f * u : 0.0f;
        x += shake;
        int own = (S.own >> i) & 1, sel = S.car == i;
        float pop = (R.gflash > 0.0f && R.gflash_i == i) ? 1.0f + 0.08f * sinf(R.gflash * 18.0f) : 1.0f;
        float px = x + s * 0.5f * (1.0f - pop), py = y + s * 0.5f * (1.0f - pop), ps = s * pop;
        rrect(c, px, py + 6.0f * u, ps, ps, 12.0f * u, pa_hex(0xDC6E00));
        rrect(c, px, py, ps, ps, 12.0f * u, pa_hex(0xF6F4F6));
        if (sel) {
            PA_Vec2 ring[4] = { { px, py }, { px + ps, py }, { px + ps, py + ps }, { px, py + ps } };
            spoly(c, ring, 4, 1, 4.0f * u, pa_hex(0xFF4FA8));
        }
        /* ground shadow under the toy */
        fell(c, x + s * 0.5f, y + s * 0.70f, s * 0.36f, s * 0.08f, PA_RGBA(40, 30, 50, 40));
        const Ride *rd = &RIDES[i];
        model_icon(c, rd->model, pa_hex(rd->body), pa_hex(rd->acc), x + s * 0.5f, y + s * (own ? 0.50f : 0.44f), s * 1.02f,
                   PA_PI * 1.22f, 0.45f, !own);
        if (!own) {
            snprintf(b, sizeof(b), "%d", rd->price);
            float w = pa_text_width(b, 13.0f * u, 0.8f * u) + 30.0f * u;
            int afford = S.coins >= rd->price;
            rrect(c, x + s * 0.5f - w * 0.5f, y + s - 27.0f * u, w, 22.0f * u, 11.0f * u, afford ? pa_hex(0x47D78C) : pa_hex(0x2A1458));
            draw_coin_icon(c, x + s * 0.5f - w * 0.5f + 12.0f * u, y + s - 16.0f * u, 7.0f * u);
            txt(c, b, x + s * 0.5f - w * 0.5f + 22.0f * u, y + s - 23.0f * u, 13.0f * u, PA_RGB(255, 255, 255), PA_ALIGN_LEFT);
        } else if (sel) {
            fcirc(c, x + s - 12.0f * u, y + 12.0f * u, 13.0f * u, pa_hex(0xFF4FA8));
            sline(c, x + s - 18.0f * u, y + 12.0f * u, x + s - 13.0f * u, y + 17.0f * u, 3.0f * u, PA_RGB(255, 255, 255));
            sline(c, x + s - 13.0f * u, y + 17.0f * u, x + s - 5.0f * u, y + 7.0f * u, 3.0f * u, PA_RGB(255, 255, 255));
        }
    }

    /* top bar over the scroll */
    PA_Paint tb = pa_linear(0, 0, 0, 84.0f * u);
    pa_stop(&tb, 0.0f, pa_hex(0xFFA22A));
    pa_stop(&tb, 0.8f, pa_hex(0xFFA22A));
    pa_stop(&tb, 1.0f, PA_RGBA(255, 162, 42, 0));
    pa_fill_rect_paint(c, 0, 0, (float)c->w, 84.0f * u, &tb);
    Box bk = btn_garage_back();
    fcirc(c, bk.x + bk.w * 0.5f, bk.y + bk.h * 0.5f + 4.0f * u, bk.w * 0.5f, pa_hex(0xDC6E00));
    fcirc(c, bk.x + bk.w * 0.5f, bk.y + bk.h * 0.5f, bk.w * 0.5f, pa_hex(0xF6F4F6));
    txtb(c, "<", bk.x + bk.w * 0.5f, bk.y + bk.h * 0.5f - 13.0f * u, 26.0f * u, oc, oc, PA_ALIGN_CENTER, 0.8f);
    txtb(c, "GARAGE", L.cx, 30.0f * u, 30.0f * u, PA_RGB(255, 255, 255), pa_hex(0xC05A00), PA_ALIGN_CENTER, 1.6f);

    /* selected ride + drive */
    PA_Paint fb = pa_linear(0, (float)c->h - 150.0f * u, 0, (float)c->h);
    pa_stop(&fb, 0.0f, PA_RGBA(251, 138, 16, 0));
    pa_stop(&fb, 0.4f, pa_hex(0xFB8A10));
    pa_stop(&fb, 1.0f, pa_hex(0xFB8A10));
    pa_fill_rect_paint(c, 0, (float)c->h - 150.0f * u, (float)c->w, 150.0f * u, &fb);
    Box d = btn_garage_drive();
    txtb(c, RIDES[S.car].name, L.cx, d.y - 40.0f * u, 22.0f * u, PA_RGB(255, 255, 255), pa_hex(0xC05A00), PA_ALIGN_CENTER, 1.2f);
    rrect(c, d.x, d.y + 6.0f * u, d.w, d.h, d.h * 0.5f, pa_hex(0x8A1A50));
    rrect(c, d.x, d.y, d.w, d.h, d.h * 0.5f, pa_hex(0xFF4FA8));
    txtb(c, "DRIVE", L.cx, d.y + d.h * 0.5f - 14.0f * u, 28.0f * u, PA_RGB(255, 255, 255), pa_hex(0x8A1A50), PA_ALIGN_CENTER, 1.3f);
}

static void rush_render(PA_Canvas *c) {
    models_init();
    if (L.w != c->w || L.h != c->h) compute_layout(c->w, c->h);
    float u = L.ui;
    if (R.state == ST_GARAGE) {
        draw_garage(c);
        pa_hub_pause_anchor((float)c->w - 40.0f * u, 46.0f * u, 24.0f * u);
        return;
    }
    setup_world_cam(&g_cam);
    draw_world(c, &g_cam);
    draw_bubbles(c, &g_cam);
    draw_speedlines(c);

    if (R.state == ST_PLAY) {
        draw_rings_pops(c);
        draw_hud(c, 1.0f);
        draw_touch(c);
    } else if (R.state == ST_MENU) {
        draw_hud(c, 0.0f);
        draw_menu(c);
        /* coins on the title too */
        char b[24];
        snprintf(b, sizeof(b), "%d", S.coins);
        float pw = pa_text_width(b, 24.0f * u, 1.4f * u) + 66.0f * u;
        rrect(c, 16.0f * u, 24.0f * u, pw, 44.0f * u, 22.0f * u, PA_RGBA(20, 10, 50, 110));
        draw_coin_icon(c, 38.0f * u, 46.0f * u, 15.0f * u);
        txtb(c, b, 62.0f * u, 34.0f * u, 24.0f * u, PA_RGB(255, 255, 255), PA_RGBA(20, 10, 50, 200), PA_ALIGN_LEFT, 1.0f);
    } else {
        if (R.state == ST_CRASH) {
            float a = 1.0f - pa_clamp01(R.crash_t * 8.0f);
            if (a > 0.0f) draw_hud(c, a);
        }
        draw_crack(c, (R.crash_t - 0.10f) * 6.0f);
        if (R.flash > 0.0f) pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(255, 255, 255, (int)(200 * pa_clamp01(R.flash))));
        if (R.state == ST_RESULTS) {
            draw_results(c);
            cr_draw_ko(c);
            pa_hub_hide_pause();
            return;
        }
    }
    pa_hub_pause_anchor((float)c->w - 40.0f * u, 46.0f * u, 24.0f * u);
}

/* ---- hub tile ---- */
static void rush_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    models_init();
    ClipSave cs;
    if (!clip_push(c, x, y, x + w, y + h, &cs)) return;
    const Biome *b = &BIOMES[B_NIGHT];
    float hz = y + h * 0.46f;
    PA_Paint sky = pa_linear(0, y, 0, hz);
    pa_stop(&sky, 0.0f, pa_hex(b->sky_top));
    pa_stop(&sky, 0.6f, pa_hex(b->sky_mid));
    pa_stop(&sky, 1.0f, pa_hex(b->sky_low));
    pa_fill_rect_paint(c, x, y, w, hz - y + 1.0f, &sky);
    glow(c, x + w * 0.5f, y + h * 0.20f, w * 0.30f, PA_RGBA(200, 210, 255, 110));
    fcirc(c, x + w * 0.5f, y + h * 0.20f, w * 0.12f, pa_hex(0xF4F6FF));
    pa_fill_rect(c, x, hz, w, y + h - hz, pa_hex(b->ground));
    float cx = x + w * 0.5f;
    PA_Vec2 road[4] = { { cx - w * 0.06f, hz }, { cx + w * 0.06f, hz }, { x + w * 1.1f, y + h }, { x - w * 0.1f, y + h } };
    fpoly(c, road, 4, pa_hex(b->road));
    for (int k = 0; k < 6; k++) {
        float tt = pa_wrapf(t * 0.9f + (float)k / 6.0f, 1.0f);
        float yy = hz + (y + h - hz) * tt * tt;
        for (int s = -1; s <= 1; s += 2) {
            float xx = cx + (float)s * (w * 0.02f + tt * tt * w * 0.30f);
            pa_fill_rect(c, xx - 1.0f - tt * 1.5f, yy, 2.0f + tt * 3.0f, 2.0f + tt * 10.0f, pa_hex(b->dash));
        }
    }
    V3 keep = g_light;
    model_icon(c, VM_LEARNER, pa_hex(0xE8283C), pa_hex(0xFFFFFF), cx + sinf(t * 1.3f) * w * 0.08f, y + h * 0.74f, w * 0.52f,
               0.0f, 0.30f, 0);
    g_light = keep;
    clip_pop(c, &cs);
}

const PA_Game PA_GAME_CHROMERUSH = {
    "chromerush", "Chrome Rush", "Endless Driver",
    "Swipe through traffic, shave past bumpers for near misses, fire the nitro and smash through. Unlock sixteen rides.",
    PA_RGB(232, 40, 60),
    rush_start, rush_stop, rush_update, rush_render, rush_thumb
};
