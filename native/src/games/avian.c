/* ===========================================================================
   AVIAN ARTILLERY - slingshot physics demolition (reference: Angry Birds 2)

   Landscape. A slingshot on a ledge at the left, structures of wood, ice and
   stone on the right with green pigs in and on them. Drag back to aim along a
   dotted arc, release to launch, tap again in flight for the bird's ability.
   Bird cards at the bottom left choose the next bird; the destructometer under
   the score pays out an extra card when it fills.

   The heart of it is a small rigid-body solver written for this game: oriented
   boxes and circles, SAT + reference-face clipping for box contacts (two
   points per face, each with a feature id), sequential impulses with
   accumulated clamping, warm starting and a 2-point block solver, split-impulse
   position correction with a slop,
   and island sleeping so a settled tower stands perfectly still until hit. It
   runs at the fixed 1/120 s step, so results are identical at any refresh.
   Damage is impact energy: 0.5 * effective mass * approach speed^2 at the
   moment a contact closes, scaled by bird/material affinity.
   =========================================================================== */
#include "../pa.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------ vec maths -- */
typedef PA_Vec2 V2;
static V2 v2(float x, float y) { V2 r; r.x = x; r.y = y; return r; }
static V2 vadd(V2 a, V2 b) { return v2(a.x + b.x, a.y + b.y); }
static V2 vsub(V2 a, V2 b) { return v2(a.x - b.x, a.y - b.y); }
static V2 vmul(V2 a, float s) { return v2(a.x * s, a.y * s); }
static V2 vneg(V2 a) { return v2(-a.x, -a.y); }
static float vdot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
static float vcross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
static V2 vcross_sv(float s, V2 v) { return v2(-s * v.y, s * v.x); }
static float vlen(V2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static V2 vrot(V2 v, float c, float s) { return v2(c * v.x - s * v.y, s * v.x + c * v.y); }
static V2 vrotT(V2 v, float c, float s) { return v2(c * v.x + s * v.y, -s * v.x + c * v.y); }
static float fminf2(float a, float b) { return a < b ? a : b; }
static float fmaxf2(float a, float b) { return a > b ? a : b; }

/* ================================================================ PHYSICS == */
#define MAXB    200
#define MAXARB  2400
#define GRAVITY 9.0f
#define SOLVER_ITERS 24
#define BIAS_FACTOR 0.2f
#define SLOP 0.005f
#define MARGIN 0.012f   /* speculative contact distance */

enum { SH_BOX, SH_CIRC };
enum { K_STATIC, K_BLOCK, K_PIG, K_BIRD };
enum { M_WOOD, M_ICE, M_STONE, M_ROCK };
enum { BT_RED, BT_YELLOW, BT_BLUE, BT_BOMB, BT_SILVER, BT_COUNT };

typedef struct {
    int   used, shape, kind, mat, var;
    V2    p, v, bv;
    float a, w, c, s, bw;
    float hw, hh, r;
    float m, im, ii;
    float fric, rest, ldamp, adamp, roll;
    int   awake;
    float sleep_t;
    float hp, hpmax, flash, dmg;
    int   dmg_bird;            /* bird type behind this step's biggest hit, -1 none */
    float impact;              /* largest contact energy this step, for sound */
    uint32_t seed;
    float minx, miny, maxx, maxy;
    float squash, blink, look;
    int   bird_t;              /* birds: type */
    float age, quiet, first_hit;
    int   hits;
    int   ability_spent;
} Body;

typedef struct {
    V2 pos, n;
    float sep, pn, pt, mn, mt, bias, pbias, pnb;
    uint32_t feat;
} Contact;

typedef struct {
    int a, b, n, live;
    Contact c[2];
    float fric, rest, roll, pr;
} Arb;

static Body  g_b[MAXB];
static Arb   g_arb[MAXARB];
static short g_pair[MAXB][MAXB];   /* arbiter index + 1 for a < b, 0 none */
static int   g_free_arb[MAXARB], g_nfree_arb;
static int   g_armed;              /* damage on; off while a fresh room settles */

/* Game hook, defined further down: a contact closed with this much energy. */
static void on_impact(Body *a, Body *b, float energy, V2 at);

static void phys_reset(void) {
    memset(g_b, 0, sizeof(g_b));
    memset(g_pair, 0, sizeof(g_pair));
    memset(g_arb, 0, sizeof(g_arb));
    g_nfree_arb = 0;
    for (int i = MAXARB - 1; i >= 0; i--) g_free_arb[g_nfree_arb++] = i;
}

static void body_sync(Body *b) {
    b->c = cosf(b->a);
    b->s = sinf(b->a);
    if (b->shape == SH_CIRC) {
        b->minx = b->p.x - b->r; b->maxx = b->p.x + b->r;
        b->miny = b->p.y - b->r; b->maxy = b->p.y + b->r;
    } else {
        float ex = fabsf(b->c) * b->hw + fabsf(b->s) * b->hh;
        float ey = fabsf(b->s) * b->hw + fabsf(b->c) * b->hh;
        b->minx = b->p.x - ex; b->maxx = b->p.x + ex;
        b->miny = b->p.y - ey; b->maxy = b->p.y + ey;
    }
}

static int phys_alloc(void) {
    for (int i = 0; i < MAXB; i++) if (!g_b[i].used) {
        memset(&g_b[i], 0, sizeof(Body));
        g_b[i].used = 1;
        g_b[i].dmg_bird = -1;
        return i;
    }
    return -1;
}

static int phys_box(int kind, float x, float y, float w, float h, float ang, float density) {
    int i = phys_alloc();
    if (i < 0) return -1;
    Body *b = &g_b[i];
    b->shape = SH_BOX; b->kind = kind;
    b->p = v2(x, y); b->a = ang;
    b->hw = w * 0.5f; b->hh = h * 0.5f;
    if (density > 0.0f) {
        b->m = density * w * h;
        b->im = 1.0f / b->m;
        b->ii = 1.0f / (b->m * (w * w + h * h) / 12.0f);
        b->awake = 1;
    }
    b->fric = 0.6f; b->rest = 0.05f;
    body_sync(b);
    return i;
}

static int phys_circle(int kind, float x, float y, float r, float density) {
    int i = phys_alloc();
    if (i < 0) return -1;
    Body *b = &g_b[i];
    b->shape = SH_CIRC; b->kind = kind;
    b->p = v2(x, y); b->r = r;
    b->hw = b->hh = r;
    if (density > 0.0f) {
        b->m = density * PA_PI * r * r;
        b->im = 1.0f / b->m;
        b->ii = 1.0f / (0.5f * b->m * r * r);
        b->awake = 1;
    }
    b->fric = 0.6f; b->rest = 0.1f;
    body_sync(b);
    return i;
}

static void arb_free(int a, int b) {
    if (a > b) { int t = a; a = b; b = t; }
    int k = g_pair[a][b];
    if (!k) return;
    g_arb[k - 1].live = 0;
    g_free_arb[g_nfree_arb++] = k - 1;
    g_pair[a][b] = 0;
}

static void body_wake(Body *b) {
    if (b->im > 0.0f && !b->awake) { b->awake = 1; b->sleep_t = 0.0f; }
}

/* Remove a body and wake whatever was leaning on it, plus anything whose
   bounds touch it, so nothing is left hovering over a gap. */
static void phys_remove(int i) {
    Body *d = &g_b[i];
    for (int j = 0; j < MAXB; j++) {
        if (j == i || !g_b[j].used) continue;
        Body *o = &g_b[j];
        int a = i < j ? i : j, b = i < j ? j : i;
        if (g_pair[a][b]) { body_wake(o); arb_free(a, b); }
        else if (o->maxx > d->minx - 0.3f && o->minx < d->maxx + 0.3f &&
                 o->maxy > d->miny - 0.3f && o->miny < d->maxy + 0.6f) body_wake(o);
    }
    d->used = 0;
}

/* ----------------------------------------------------------- narrowphase */
enum { NO_EDGE = 0, EDGE1, EDGE2, EDGE3, EDGE4 };
typedef struct { V2 v; unsigned char e[4]; } ClipV;   /* in1 out1 in2 out2 */

static uint32_t fkey(const unsigned char *e) {
    return (uint32_t)e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24);
}

static int clip_seg(ClipV out[2], const ClipV in[2], V2 n, float off, unsigned char edge) {
    int k = 0;
    float d0 = vdot(n, in[0].v) - off, d1 = vdot(n, in[1].v) - off;
    if (d0 <= 0.0f) out[k++] = in[0];
    if (d1 <= 0.0f) out[k++] = in[1];
    if (d0 * d1 < 0.0f && k < 2) {
        float t = d0 / (d0 - d1);
        out[k].v = vadd(in[0].v, vmul(vsub(in[1].v, in[0].v), t));
        if (d0 > 0.0f) {
            memcpy(out[k].e, in[0].e, 4);
            out[k].e[0] = edge; out[k].e[2] = NO_EDGE;
        } else {
            memcpy(out[k].e, in[1].e, 4);
            out[k].e[1] = edge; out[k].e[3] = NO_EDGE;
        }
        k++;
    }
    return k;
}

static void incident_edge(ClipV c[2], V2 h, V2 pos, float cs, float sn, V2 normal) {
    V2 n = vneg(vrotT(normal, cs, sn));
    memset(c, 0, sizeof(ClipV) * 2);
    if (fabsf(n.x) > fabsf(n.y)) {
        if (n.x >= 0.0f) {
            c[0].v = v2(h.x, -h.y); c[0].e[2] = EDGE3; c[0].e[3] = EDGE4;
            c[1].v = v2(h.x,  h.y); c[1].e[2] = EDGE4; c[1].e[3] = EDGE1;
        } else {
            c[0].v = v2(-h.x,  h.y); c[0].e[2] = EDGE1; c[0].e[3] = EDGE2;
            c[1].v = v2(-h.x, -h.y); c[1].e[2] = EDGE2; c[1].e[3] = EDGE3;
        }
    } else {
        if (n.y >= 0.0f) {
            c[0].v = v2( h.x, h.y); c[0].e[2] = EDGE4; c[0].e[3] = EDGE1;
            c[1].v = v2(-h.x, h.y); c[1].e[2] = EDGE1; c[1].e[3] = EDGE2;
        } else {
            c[0].v = v2(-h.x, -h.y); c[0].e[2] = EDGE2; c[0].e[3] = EDGE3;
            c[1].v = v2( h.x, -h.y); c[1].e[2] = EDGE3; c[1].e[3] = EDGE4;
        }
    }
    c[0].v = vadd(pos, vrot(c[0].v, cs, sn));
    c[1].v = vadd(pos, vrot(c[1].v, cs, sn));
}

/* Box against box, normal from A to B. */
static int collide_bb(const Body *A, const Body *B, Contact *out) {
    V2 hA = v2(A->hw, A->hh), hB = v2(B->hw, B->hh);
    V2 dp = vsub(B->p, A->p);
    V2 dA = vrotT(dp, A->c, A->s), dB = vrotT(dp, B->c, B->s);
    float c00 = A->c * B->c + A->s * B->s, c01 = -A->c * B->s + A->s * B->c;
    float c10 = -A->s * B->c + A->c * B->s, c11 = A->s * B->s + A->c * B->c;
    float a00 = fabsf(c00), a01 = fabsf(c01), a10 = fabsf(c10), a11 = fabsf(c11);

    V2 faceA = v2(fabsf(dA.x) - hA.x - (a00 * hB.x + a01 * hB.y),
                  fabsf(dA.y) - hA.y - (a10 * hB.x + a11 * hB.y));
    if (faceA.x > MARGIN || faceA.y > MARGIN) return 0;
    V2 faceB = v2(fabsf(dB.x) - (a00 * hA.x + a10 * hA.y) - hB.x,
                  fabsf(dB.y) - (a01 * hA.x + a11 * hA.y) - hB.y);
    if (faceB.x > MARGIN || faceB.y > MARGIN) return 0;

    V2 A1 = v2(A->c, A->s), A2 = v2(-A->s, A->c);
    V2 B1 = v2(B->c, B->s), B2 = v2(-B->s, B->c);
    const float rt = 0.95f, at = 0.01f;
    int axis = 0;
    float sep = faceA.x;
    V2 normal = dA.x > 0.0f ? A1 : vneg(A1);
    if (faceA.y > rt * sep + at * hA.y) { axis = 1; sep = faceA.y; normal = dA.y > 0.0f ? A2 : vneg(A2); }
    if (faceB.x > rt * sep + at * hB.x) { axis = 2; sep = faceB.x; normal = dB.x > 0.0f ? B1 : vneg(B1); }
    if (faceB.y > rt * sep + at * hB.y) { axis = 3; sep = faceB.y; normal = dB.y > 0.0f ? B2 : vneg(B2); }

    V2 front_n, side_n;
    float front, neg_side, pos_side, side;
    unsigned char neg_edge, pos_edge;
    ClipV inc[2];
    switch (axis) {
    case 0:
        front_n = normal; front = vdot(A->p, front_n) + hA.x;
        side_n = A2; side = vdot(A->p, side_n);
        neg_side = -side + hA.y; pos_side = side + hA.y;
        neg_edge = EDGE3; pos_edge = EDGE1;
        incident_edge(inc, hB, B->p, B->c, B->s, front_n);
        break;
    case 1:
        front_n = normal; front = vdot(A->p, front_n) + hA.y;
        side_n = A1; side = vdot(A->p, side_n);
        neg_side = -side + hA.x; pos_side = side + hA.x;
        neg_edge = EDGE2; pos_edge = EDGE4;
        incident_edge(inc, hB, B->p, B->c, B->s, front_n);
        break;
    case 2:
        front_n = vneg(normal); front = vdot(B->p, front_n) + hB.x;
        side_n = B2; side = vdot(B->p, side_n);
        neg_side = -side + hB.y; pos_side = side + hB.y;
        neg_edge = EDGE3; pos_edge = EDGE1;
        incident_edge(inc, hA, A->p, A->c, A->s, front_n);
        break;
    default:
        front_n = vneg(normal); front = vdot(B->p, front_n) + hB.y;
        side_n = B1; side = vdot(B->p, side_n);
        neg_side = -side + hB.x; pos_side = side + hB.x;
        neg_edge = EDGE2; pos_edge = EDGE4;
        incident_edge(inc, hA, A->p, A->c, A->s, front_n);
        break;
    }
    ClipV cp1[2], cp2[2];
    if (clip_seg(cp1, inc, vneg(side_n), neg_side, neg_edge) < 2) return 0;
    if (clip_seg(cp2, cp1, side_n, pos_side, pos_edge) < 2) return 0;

    int n = 0;
    for (int i = 0; i < 2; i++) {
        float s = vdot(front_n, cp2[i].v) - front;
        if (s <= MARGIN) {
            out[n].sep = s;
            out[n].n = normal;
            out[n].pos = vsub(cp2[i].v, vmul(front_n, s));
            unsigned char e[4];
            memcpy(e, cp2[i].e, 4);
            if (axis >= 2) {
                unsigned char t = e[0]; e[0] = e[2]; e[2] = t;
                t = e[1]; e[1] = e[3]; e[3] = t;
            }
            out[n].feat = fkey(e);
            n++;
        }
    }
    return n;
}

/* Box A against circle B, normal from A to B. */
static int collide_bc(const Body *A, const Body *B, Contact *out) {
    V2 c = vrotT(vsub(B->p, A->p), A->c, A->s);
    V2 q = v2(pa_clampf(c.x, -A->hw, A->hw), pa_clampf(c.y, -A->hh, A->hh));
    V2 nl, pt;
    float sep;
    if (q.x == c.x && q.y == c.y) {
        float dx = A->hw - fabsf(c.x), dy = A->hh - fabsf(c.y);
        if (dx < dy) { nl = v2(c.x >= 0 ? 1.0f : -1.0f, 0); sep = -dx - B->r; pt = v2(nl.x * A->hw, c.y); }
        else         { nl = v2(0, c.y >= 0 ? 1.0f : -1.0f); sep = -dy - B->r; pt = v2(c.x, nl.y * A->hh); }
    } else {
        V2 d = vsub(c, q);
        float dist = vlen(d);
        if (dist > B->r + MARGIN) return 0;
        nl = dist > 1e-6f ? vmul(d, 1.0f / dist) : v2(0, 1);
        sep = dist - B->r;
        pt = q;
    }
    out[0].n = vrot(nl, A->c, A->s);
    out[0].sep = sep;
    out[0].pos = vadd(A->p, vrot(vadd(pt, vmul(nl, sep * 0.5f)), A->c, A->s));
    out[0].feat = 0;
    return 1;
}

static int collide_cc(const Body *A, const Body *B, Contact *out) {
    V2 d = vsub(B->p, A->p);
    float dist = vlen(d), rs = A->r + B->r;
    if (dist > rs + MARGIN) return 0;
    V2 n = dist > 1e-6f ? vmul(d, 1.0f / dist) : v2(0, 1);
    out[0].n = n;
    out[0].sep = dist - rs;
    out[0].pos = vadd(A->p, vmul(n, A->r + out[0].sep * 0.5f));
    out[0].feat = 0;
    return 1;
}

static int collide(const Body *A, const Body *B, Contact *out) {
    if (A->shape == SH_BOX && B->shape == SH_BOX) return collide_bb(A, B, out);
    if (A->shape == SH_BOX && B->shape == SH_CIRC) return collide_bc(A, B, out);
    if (A->shape == SH_CIRC && B->shape == SH_CIRC) return collide_cc(A, B, out);
    int n = collide_bc(B, A, out);
    if (n) out[0].n = vneg(out[0].n);
    return n;
}

/* ------------------------------------------------------------------ step */
static int g_uf[MAXB];
static int uf_find(int i) { while (g_uf[i] != i) { g_uf[i] = g_uf[g_uf[i]]; i = g_uf[i]; } return i; }

/* Two-point block solver: both normal impulses of a face contact solved
   as one 2x2 LCP (the Box2D approach). Solving them one after the other is
   what lets a tall stack rock and creep; solving them together is what keeps
   a tower of planks standing still. Returns 0 if the pair is ill-conditioned. */
static int solve_block(Arb *ar, Body *A, Body *B) {
    Contact *c1 = &ar->c[0], *c2 = &ar->c[1];
    V2 n = c1->n;
    V2 a1 = vsub(c1->pos, A->p), b1 = vsub(c1->pos, B->p);
    V2 a2 = vsub(c2->pos, A->p), b2 = vsub(c2->pos, B->p);
    float rn1a = vcross(a1, n), rn1b = vcross(b1, n), rn2a = vcross(a2, n), rn2b = vcross(b2, n);
    float m = A->im + B->im;
    float k11 = m + A->ii * rn1a * rn1a + B->ii * rn1b * rn1b;
    float k22 = m + A->ii * rn2a * rn2a + B->ii * rn2b * rn2b;
    float k12 = m + A->ii * rn1a * rn2a + B->ii * rn1b * rn2b;
    float det = k11 * k22 - k12 * k12;
    if (k11 * k11 >= 1000.0f * det || det <= 0.0f) return 0;
    float id = 1.0f / det;
    V2 dv1 = vsub(vadd(B->v, vcross_sv(B->w, b1)), vadd(A->v, vcross_sv(A->w, a1)));
    V2 dv2 = vsub(vadd(B->v, vcross_sv(B->w, b2)), vadd(A->v, vcross_sv(A->w, a2)));
    float ax = c1->pn, ay = c2->pn;
    float bx = vdot(dv1, n) - c1->bias - (k11 * ax + k12 * ay);
    float by = vdot(dv2, n) - c2->bias - (k12 * ax + k22 * ay);
    float x1, x2;
    for (;;) {
        x1 = -(k22 * bx - k12 * by) * id;
        x2 = -(-k12 * bx + k11 * by) * id;
        if (x1 >= 0.0f && x2 >= 0.0f) break;
        x1 = -bx / k11; x2 = 0.0f;
        if (x1 >= 0.0f && k12 * x1 + by >= 0.0f) break;
        x1 = 0.0f; x2 = -by / k22;
        if (x2 >= 0.0f && k12 * x2 + bx >= 0.0f) break;
        x1 = 0.0f; x2 = 0.0f;
        if (bx >= 0.0f && by >= 0.0f) break;
        return 1;    /* no solution this pass; leave impulses as they are */
    }
    float d1 = x1 - ax, d2 = x2 - ay;
    V2 P1 = vmul(n, d1), P2 = vmul(n, d2);
    A->v = vsub(A->v, vmul(vadd(P1, P2), A->im));
    A->w -= A->ii * (vcross(a1, P1) + vcross(a2, P2));
    B->v = vadd(B->v, vmul(vadd(P1, P2), B->im));
    B->w += B->ii * (vcross(b1, P1) + vcross(b2, P2));
    c1->pn = x1; c2->pn = x2;
    return 1;
}

static void phys_step(float dt) {
    float idt = 1.0f / dt;

    for (int i = 0; i < MAXB; i++) if (g_b[i].used) { g_b[i].dmg = 0.0f; g_b[i].impact = 0.0f; g_b[i].dmg_bird = -1; }

    /* Broad + narrow phase, keeping accumulated impulses for warm starting. */
    for (int i = 0; i < MAXB; i++) {
        Body *A = &g_b[i];
        if (!A->used) continue;
        for (int j = i + 1; j < MAXB; j++) {
            Body *B = &g_b[j];
            if (!B->used) continue;
            if (A->im == 0.0f && B->im == 0.0f) continue;
            int act_a = A->im > 0.0f && A->awake, act_b = B->im > 0.0f && B->awake;
            if (!act_a && !act_b) continue;
            if (A->maxx < B->minx - 0.02f || B->maxx < A->minx - 0.02f ||
                A->maxy < B->miny - 0.02f || B->maxy < A->miny - 0.02f) {
                if (g_pair[i][j]) arb_free(i, j);
                continue;
            }
            Contact nc[2];
            int n = collide(A, B, nc);
            if (!n) { if (g_pair[i][j]) arb_free(i, j); continue; }
            if (!act_a) body_wake(A);
            if (!act_b) body_wake(B);
            Arb *ar;
            if (g_pair[i][j]) {
                ar = &g_arb[g_pair[i][j] - 1];
                for (int k = 0; k < n; k++) {
                    nc[k].pn = nc[k].pt = 0.0f;
                    int found = 0;
                    for (int o = 0; o < ar->n; o++) if (ar->c[o].feat == nc[k].feat) {
                        nc[k].pn = ar->c[o].pn; nc[k].pt = ar->c[o].pt; found = 1; break;
                    }
                    /* Feature ids flicker when equal faces line up exactly;
                       fall back to the nearest old point so warm starting holds. */
                    if (!found) {
                        float bd = 0.05f * 0.05f;
                        for (int o = 0; o < ar->n; o++) {
                            V2 d = vsub(ar->c[o].pos, nc[k].pos);
                            if (vdot(d, d) < bd) { bd = vdot(d, d); nc[k].pn = ar->c[o].pn; nc[k].pt = ar->c[o].pt; }
                        }
                    }
                }
            } else {
                if (!g_nfree_arb) continue;
                int k = g_free_arb[--g_nfree_arb];
                g_pair[i][j] = (short)(k + 1);
                ar = &g_arb[k];
                ar->a = i; ar->b = j; ar->live = 1;
                for (int q = 0; q < n; q++) nc[q].pn = nc[q].pt = 0.0f;
                ar->fric = sqrtf(A->fric * B->fric);
                ar->rest = fmaxf2(A->rest, B->rest);
                ar->roll = fmaxf2(A->roll, B->roll);
            }
            ar->n = n;
            ar->c[0] = nc[0];
            if (n > 1) ar->c[1] = nc[1];
        }
    }

    /* Forces. */
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (!b->used || b->im == 0.0f || !b->awake) continue;
        b->v.y -= GRAVITY * dt;
        b->v = vmul(b->v, 1.0f / (1.0f + dt * b->ldamp));
        b->w *= 1.0f / (1.0f + dt * b->adamp);
    }

    /* Pre-step: effective masses, bias, restitution, impact energy, warm start. */
    for (int k = 0; k < MAXARB; k++) {
        Arb *ar = &g_arb[k];
        if (!ar->live) continue;
        Body *A = &g_b[ar->a], *B = &g_b[ar->b];
        if (!((A->im > 0.0f && A->awake) || (B->im > 0.0f && B->awake))) continue;
        float best_e = 0.0f; V2 best_at = v2(0, 0);
        for (int q = 0; q < ar->n; q++) {
            Contact *c = &ar->c[q];
            V2 r1 = vsub(c->pos, A->p), r2 = vsub(c->pos, B->p);
            float rn1 = vdot(r1, c->n), rn2 = vdot(r2, c->n);
            float kn = A->im + B->im + A->ii * (vdot(r1, r1) - rn1 * rn1) + B->ii * (vdot(r2, r2) - rn2 * rn2);
            c->mn = 1.0f / kn;
            V2 t = v2(c->n.y, -c->n.x);
            float rt1 = vdot(r1, t), rt2 = vdot(r2, t);
            float kt = A->im + B->im + A->ii * (vdot(r1, r1) - rt1 * rt1) + B->ii * (vdot(r2, r2) - rt2 * rt2);
            c->mt = 1.0f / kt;
            /* Speculative: a contact still apart may close by its gap this step. */
            c->bias = c->sep > 0.0f ? -c->sep * idt : 0.0f;
            c->pbias = -BIAS_FACTOR * idt * fminf2(0.0f, c->sep + SLOP);
            if (c->pbias > 2.0f) c->pbias = 2.0f;
            c->pnb = 0.0f;
            V2 dv = vsub(vadd(B->v, vcross_sv(B->w, r2)), vadd(A->v, vcross_sv(A->w, r1)));
            float vn = vdot(dv, c->n);
            float closing = vn - c->bias;      /* what the solver will have to stop */
            if (closing < -1.0f) {
                if (c->sep <= 0.0f) {
                    float rb = -ar->rest * vn;
                    if (rb > c->bias) c->bias = rb;
                }
                float e = 0.5f * c->mn * closing * closing;
                if (e > best_e) { best_e = e; best_at = c->pos; }
            }
            V2 P = vadd(vmul(c->n, c->pn), vmul(t, c->pt));
            A->v = vsub(A->v, vmul(P, A->im)); A->w -= A->ii * vcross(r1, P);
            B->v = vadd(B->v, vmul(P, B->im)); B->w += B->ii * vcross(r2, P);
        }
        ar->pr = 0.0f;
        if (best_e > 0.0f) on_impact(A, B, best_e, best_at);
    }

    /* Sequential impulses. */
    for (int it = 0; it < SOLVER_ITERS; it++) {
        for (int k = 0; k < MAXARB; k++) {
            Arb *ar = &g_arb[k];
            if (!ar->live) continue;
            Body *A = &g_b[ar->a], *B = &g_b[ar->b];
            if (!((A->im > 0.0f && A->awake) || (B->im > 0.0f && B->awake))) continue;
            /* Friction first, against the current normal impulse. */
            for (int q = 0; q < ar->n; q++) {
                Contact *c = &ar->c[q];
                V2 r1 = vsub(c->pos, A->p), r2 = vsub(c->pos, B->p);
                V2 dv = vsub(vadd(B->v, vcross_sv(B->w, r2)), vadd(A->v, vcross_sv(A->w, r1)));
                V2 t = v2(c->n.y, -c->n.x);
                float vt = vdot(dv, t);
                float dpt = c->mt * (-vt);
                float maxpt = ar->fric * c->pn;
                float pt0 = c->pt;
                c->pt = pa_clampf(pt0 + dpt, -maxpt, maxpt);
                dpt = c->pt - pt0;
                V2 P = vmul(t, dpt);
                A->v = vsub(A->v, vmul(P, A->im)); A->w -= A->ii * vcross(r1, P);
                B->v = vadd(B->v, vmul(P, B->im)); B->w += B->ii * vcross(r2, P);
            }
            if (ar->n == 2 && solve_block(ar, A, B)) {
                /* both normal impulses solved together */
            } else {
                for (int q = 0; q < ar->n; q++) {
                    Contact *c = &ar->c[q];
                    V2 r1 = vsub(c->pos, A->p), r2 = vsub(c->pos, B->p);
                    V2 dv = vsub(vadd(B->v, vcross_sv(B->w, r2)), vadd(A->v, vcross_sv(A->w, r1)));
                    float vn = vdot(dv, c->n);
                    float dpn = c->mn * (-vn + c->bias);
                    float pn0 = c->pn;
                    c->pn = fmaxf2(pn0 + dpn, 0.0f);
                    dpn = c->pn - pn0;
                    V2 P = vmul(c->n, dpn);
                    A->v = vsub(A->v, vmul(P, A->im)); A->w -= A->ii * vcross(r1, P);
                    B->v = vadd(B->v, vmul(P, B->im)); B->w += B->ii * vcross(r2, P);
                }
            }
        }
    }

    /* Rolling resistance for round bodies, applied once after the velocity
       solve: a pig on a plank that is a hair off level stays put instead of
       creeping off the end. Doing it inside the iterations kept tall towers
       buzzing. */
    for (int k = 0; k < MAXARB; k++) {
        Arb *ar = &g_arb[k];
        if (!ar->live || ar->roll <= 0.0f) continue;
        Body *A = &g_b[ar->a], *B = &g_b[ar->b];
        Body *R = B->shape == SH_CIRC && B->ii > 0.0f ? B : (A->shape == SH_CIRC && A->ii > 0.0f ? A : NULL);
        if (!R || !R->awake) continue;
        float pn = 0.0f;
        for (int q = 0; q < ar->n; q++) pn += ar->c[q].pn;
        float maxdw = ar->roll * pn * R->ii;
        if (R->w > maxdw) R->w -= maxdw;
        else if (R->w < -maxdw) R->w += maxdw;
        else R->w = 0.0f;
    }

    /* Split-impulse position correction: overlap is pushed out through a
       pseudo velocity that moves bodies this step and is then thrown away,
       so correcting penetration never adds real momentum (the source of
       rocking and "walking" in tall stacks under plain Baumgarte). */
    for (int i = 0; i < MAXB; i++) { g_b[i].bv = v2(0, 0); g_b[i].bw = 0.0f; }
    for (int it = 0; it < SOLVER_ITERS / 2; it++) {
        for (int k = 0; k < MAXARB; k++) {
            Arb *ar = &g_arb[k];
            if (!ar->live) continue;
            Body *A = &g_b[ar->a], *B = &g_b[ar->b];
            if (!((A->im > 0.0f && A->awake) || (B->im > 0.0f && B->awake))) continue;
            for (int q = 0; q < ar->n; q++) {
                Contact *c = &ar->c[q];
                if (c->pbias <= 0.0f && c->pnb <= 0.0f) continue;
                V2 r1 = vsub(c->pos, A->p), r2 = vsub(c->pos, B->p);
                V2 dv = vsub(vadd(B->bv, vcross_sv(B->bw, r2)), vadd(A->bv, vcross_sv(A->bw, r1)));
                float dp = c->mn * (c->pbias - vdot(dv, c->n));
                float p0 = c->pnb;
                c->pnb = fmaxf2(p0 + dp, 0.0f);
                dp = c->pnb - p0;
                V2 P = vmul(c->n, dp);
                A->bv = vsub(A->bv, vmul(P, A->im)); A->bw -= A->ii * vcross(r1, P);
                B->bv = vadd(B->bv, vmul(P, B->im)); B->bw += B->ii * vcross(r2, P);
            }
        }
    }

    /* Integrate. */
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (!b->used || b->im == 0.0f || !b->awake) continue;
        float sp = vlen(b->v);
        if (sp > 60.0f) b->v = vmul(b->v, 60.0f / sp);
        b->w = pa_clampf(b->w, -40.0f, 40.0f);
        b->p = vadd(b->p, vmul(vadd(b->v, b->bv), dt));
        b->a += (b->w + b->bw) * dt;
        body_sync(b);
    }

    /* Island sleeping: a connected group sleeps only once every member has
       been still for half a second, and wakes as a whole. */
    for (int i = 0; i < MAXB; i++) g_uf[i] = i;
    for (int k = 0; k < MAXARB; k++) {
        Arb *ar = &g_arb[k];
        if (!ar->live || !ar->n) continue;
        if (g_b[ar->a].im == 0.0f || g_b[ar->b].im == 0.0f) continue;
        int ra = uf_find(ar->a), rb = uf_find(ar->b);
        if (ra != rb) g_uf[ra] = rb;
    }
    static float island_min[MAXB];
    for (int i = 0; i < MAXB; i++) island_min[i] = 1e9f;
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (!b->used || b->im == 0.0f) continue;
        if (b->awake) {
            int still = vdot(b->v, b->v) < 0.05f * 0.05f && b->w * b->w < 0.06f * 0.06f;
            if (b->kind == K_BIRD) still = 0;      /* a live bird never parks */
            b->sleep_t = still ? b->sleep_t + dt : 0.0f;
        }
        int r = uf_find(i);
        if (b->sleep_t < island_min[r]) island_min[r] = b->sleep_t;
    }
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (!b->used || b->im == 0.0f) continue;
        float m = island_min[uf_find(i)];
        if (m >= 0.5f) { b->awake = 0; b->v = v2(0, 0); b->w = 0.0f; }
        else if (!b->awake) { b->awake = 1; b->sleep_t = 0.0f; }
    }
}

/* Apply an instantaneous velocity change and wake the body. */
static void body_kick(Body *b, V2 imp, float spin) {
    if (b->im == 0.0f) return;
    body_wake(b);
    b->v = vadd(b->v, vmul(imp, b->im));
    b->w += spin * b->ii;
}

/* ============================================================ GAME DATA == */
typedef struct { float dens, fric, rest, hp, thresh; int score; } MatDef;
static const MatDef MATS[3] = {
    /* wood  */ { 5.0f,  0.70f, 0.04f, 30.0f,  3.0f,  500 },
    /* ice   */ { 4.0f,  0.42f, 0.04f, 13.0f,  2.0f,  500 },
    /* stone */ { 11.0f, 0.80f, 0.02f, 120.0f, 6.0f, 1000 },
};

typedef struct {
    float r, dens, rest;
    float aff[3];                 /* damage multiplier against wood, ice, stone */
    const char *name, *ability;
} BirdDef;
static const BirdDef BIRDS[BT_COUNT] = {
    { 0.30f, 14.0f, 0.22f, { 1.0f, 1.0f, 0.8f }, "RED",    "BATTLE CRY" },
    { 0.29f, 13.0f, 0.20f, { 2.2f, 0.7f, 0.5f }, "CHUCK",  "SPEED DASH" },
    { 0.19f, 14.0f, 0.25f, { 0.5f, 3.6f, 0.3f }, "BLUES",  "SPLIT" },
    { 0.40f, 16.0f, 0.10f, { 1.0f, 1.0f, 1.7f }, "BOMB",   "EXPLODE" },
    { 0.30f, 16.0f, 0.15f, { 1.2f, 1.2f, 1.6f }, "SILVER", "LOOP SLAM" },
};

enum { PIG_S, PIG_M, PIG_L, PIG_KING };
static const float PIG_R[4]  = { 0.26f, 0.35f, 0.47f, 0.62f };
static const float PIG_HP[4] = { 5.0f, 9.0f, 15.0f, 40.0f };

enum { TH_JUNGLE, TH_CANYON, TH_FROST, TH_VOLCANO };
static const char *THEME_NAME[4] = { "JUNGLE ISLAND", "SUNSET CANYON", "FROST PEAKS", "MAGMA MOUNTAIN" };

typedef struct { const char *birds; int rooms; } LevelDef;
#define NLEVELS 20
static const LevelDef LEVELS[NLEVELS] = {
    { "RRR", 1 },   { "RRY", 1 },   { "YRY", 1 },   { "BBR", 1 },   { "RKY", 1 },
    { "YYR", 1 },   { "RBYKR", 2 }, { "KRS", 1 },   { "SYBR", 1 },  { "RYBKS", 2 },
    { "BBY", 1 },   { "RKBYR", 2 }, { "YSKB", 1 },  { "KBRYS", 2 }, { "SKYBRKY", 3 },
    { "KKR", 1 },   { "SYKBR", 2 }, { "KSRB", 1 },  { "YKSBR", 2 }, { "KSKYBRBY", 3 },
};
static int level_theme(int l) { return l / 5; }

#define SLING_X   0.0f
#define LEDGE_TOP 1.7f
#define ANCHOR_Y  (LEDGE_TOP + 1.30f)
#define MAXPULL   1.25f
#define VMAX      15.5f
#define T_        0.22f         /* plank thickness */

/* ------------------------------------------------------------ builder -- */
static int   g_bld_pigs, g_bld_blockpts;
static float g_bld_maxx, g_bld_maxy;
static uint32_t g_bld_seed;

static void bld_note(float x1, float y1) {
    if (x1 > g_bld_maxx) g_bld_maxx = x1;
    if (y1 > g_bld_maxy) g_bld_maxy = y1;
}

static float size_factor(float w, float h) { return pa_clampf(w * h / 0.242f, 0.6f, 3.0f); }

static int block_points(int mat, float w, float h) {
    int p = MATS[mat].score;
    return size_factor(w, h) >= 1.8f ? p * 2 : p;
}

static int mk_block(int mat, float cx, float cy, float w, float h, float ang, int var) {
    int i = phys_box(K_BLOCK, cx, cy, w, h, ang, MATS[mat].dens);
    if (i < 0) return -1;
    Body *b = &g_b[i];
    b->mat = mat; b->var = var;
    b->fric = MATS[mat].fric; b->rest = MATS[mat].rest;
    b->hpmax = b->hp = MATS[mat].hp * size_factor(w, h) * (var == 1 ? 0.8f : 1.0f);
    g_bld_seed = g_bld_seed * 1664525u + 1013904223u;
    b->seed = g_bld_seed;
    g_bld_blockpts += block_points(mat, w, h);
    bld_note(cx + w * 0.5f, cy + h * 0.5f);
    return i;
}

/* Box resting on `bot`; returns its top. */
static float blk(int mat, float cx, float bot, float w, float h) {
    mk_block(mat, cx, bot + h * 0.5f, w, h, 0.0f, 0);
    return bot + h;
}
/* Hollow square frame (solid to physics, drawn hollow). */
static float hol(int mat, float cx, float bot, float s) {
    mk_block(mat, cx, bot + s * 0.5f, s, s, 0.0f, 1);
    return bot + s;
}
static void ball(int mat, float cx, float bot, float r) {
    int i = phys_circle(K_BLOCK, cx, bot + r, r, MATS[mat].dens);
    if (i < 0) return;
    Body *b = &g_b[i];
    b->mat = mat; b->var = 2;
    b->fric = MATS[mat].fric; b->rest = 0.05f; b->adamp = 0.3f; b->roll = 0.012f;
    b->hpmax = b->hp = MATS[mat].hp * 1.4f;
    g_bld_seed = g_bld_seed * 1664525u + 1013904223u;
    b->seed = g_bld_seed;
    g_bld_blockpts += MATS[mat].score;
    bld_note(cx + r, bot + 2 * r);
}
static float pig(float cx, float bot, int type, int helmet) {
    float r = PIG_R[type];
    int i = phys_circle(K_PIG, cx, bot + r, r, 3.0f);
    if (i < 0) return bot;
    Body *b = &g_b[i];
    b->var = type | (helmet ? 8 : 0);
    b->fric = 0.8f; b->rest = 0.1f; b->adamp = 3.0f; b->ldamp = 0.1f; b->roll = 0.05f;
    b->hpmax = b->hp = PIG_HP[type] * (helmet ? 2.4f : 1.0f);
    g_bld_seed = g_bld_seed * 1664525u + 1013904223u;
    b->seed = g_bld_seed;
    b->blink = (float)(b->seed % 1000u) / 300.0f;
    g_bld_pigs++;
    bld_note(cx + r, bot + 2 * r);
    return bot + 2 * r;
}
/* Static rock column from the ground up to `top`. var 0 = cliff, 1 = floating island slab. */
static void plat(float x0, float x1, float top) {
    int i = phys_box(K_STATIC, (x0 + x1) * 0.5f, top * 0.5f - 0.5f, x1 - x0, top + 1.0f, 0.0f, 0.0f);
    if (i < 0) return;
    g_b[i].mat = M_ROCK; g_b[i].fric = 0.9f; g_b[i].rest = 0.05f; g_b[i].var = 0;
    g_bld_seed = g_bld_seed * 1664525u + 1013904223u;
    g_b[i].seed = g_bld_seed;
    bld_note(x1, top);
}
static void island(float x0, float x1, float top) {
    int i = phys_box(K_STATIC, (x0 + x1) * 0.5f, top - 0.35f, x1 - x0, 0.7f, 0.0f, 0.0f);
    if (i < 0) return;
    g_b[i].mat = M_ROCK; g_b[i].fric = 0.9f; g_b[i].rest = 0.05f; g_b[i].var = 1;
    g_bld_seed = g_bld_seed * 1664525u + 1013904223u;
    g_b[i].seed = g_bld_seed;
    bld_note(x1, top);
}

/* Two posts under a lintel. Returns the lintel top. */
static float frame(int pm, int lm, float cx, float bot, float span, float ph) {
    blk(pm, cx - span * 0.5f + T_ * 0.5f, bot, T_, ph);
    blk(pm, cx + span * 0.5f - T_ * 0.5f, bot, T_, ph);
    return blk(lm, cx, bot + ph, span + 0.2f, T_);
}
/* A stack of frames. Bit f of pigs puts a pig in floor f; bit f+8 makes it big. */
static float tower(int pm, int lm, float cx, float bot, int floors, float span, float ph, int pigs) {
    for (int f = 0; f < floors; f++) {
        float top = frame(pm, lm, cx, bot, span, ph);
        if (pigs & (1 << f)) pig(cx, bot, (pigs & (256 << f)) ? PIG_M : PIG_S, 0);
        bot = top;
    }
    return bot;
}
/* Pyramid of hollow squares, `base` wide. Returns the top. */
static float pyramid(int mat, float cx, float bot, int base, float s) {
    for (int row = 0; row < base; row++) {
        int n = base - row;
        for (int k = 0; k < n; k++) hol(mat, cx + ((float)k - (float)(n - 1) * 0.5f) * s, bot, s);
        bot += s;
    }
    return bot;
}

enum { W_ = M_WOOD, I_ = M_ICE, S_ = M_STONE };

static void build_room(int L, int room) {
    float t, t2;
    switch (L) {
    case 0:
        t = frame(W_, W_, 10.0f, 0, 1.6f, 1.1f);
        pig(10.0f, 0, PIG_M, 0);
        pig(10.0f, t, PIG_S, 0);
        t = hol(W_, 12.6f, 0, 0.55f); t = hol(W_, 12.6f, t, 0.55f);
        pig(12.6f, t, PIG_S, 0);
        break;
    case 1:
        t = tower(W_, W_, 10.0f, 0, 2, 1.6f, 1.1f, 0x103);
        pig(10.0f, t, PIG_S, 0);
        t = frame(W_, W_, 13.2f, 0, 1.6f, 1.1f);
        pig(13.2f, 0, PIG_M, 0);
        hol(W_, 12.75f, t, 0.55f); hol(W_, 13.65f, t, 0.55f);
        break;
    case 2:
        t = frame(W_, W_, 9.6f, 0, 1.4f, 1.1f);
        pig(9.6f, 0, PIG_S, 0);
        hol(W_, 9.6f, t, 0.55f);
        plat(12.2f, 16.8f, 1.2f);
        t = tower(W_, W_, 14.5f, 1.2f, 3, 2.0f, 1.1f, 0x105);
        pig(14.5f, t, PIG_M, 0);
        break;
    case 3:
        t = tower(I_, I_, 10.4f, 0, 2, 1.6f, 1.1f, 0x003);
        pig(10.4f, t, PIG_S, 0);
        t = tower(I_, W_, 13.6f, 0, 3, 1.6f, 1.1f, 0x206);
        t = hol(I_, 13.6f, t, 0.55f);
        pig(13.6f, t, PIG_S, 0);
        break;
    case 4:
        t = frame(S_, S_, 11.0f, 0, 2.4f, 1.1f);
        pig(11.0f, 0, PIG_L, 0);
        t2 = frame(W_, W_, 11.0f, t, 1.6f, 1.1f);
        pig(11.0f, t, PIG_M, 0);
        pig(11.0f, t2, PIG_S, 0);
        plat(14.0f, 16.6f, 2.0f);
        t = tower(W_, I_, 15.3f, 2.0f, 2, 1.4f, 1.1f, 0x001);
        pig(15.3f, t, PIG_S, 0);
        break;
    case 5:
        plat(9.2f, 11.8f, 1.0f);
        t = tower(W_, W_, 10.5f, 1.0f, 2, 1.5f, 1.1f, 0x002);
        pig(10.5f, t, PIG_S, 0);
        plat(14.0f, 16.8f, 2.6f);
        t = tower(W_, W_, 15.4f, 2.6f, 2, 1.6f, 1.1f, 0x101);
        pig(15.4f, t, PIG_M, 0);
        t = hol(W_, 12.9f, 0, 0.55f);
        pig(12.9f, t, PIG_S, 0);
        break;
    case 6:
        if (room == 0) {
            t = tower(W_, I_, 10.0f, 0, 3, 1.6f, 1.1f, 0x002);
            pig(10.0f, t, PIG_S, 0);
            t = frame(I_, I_, 12.8f, 0, 1.4f, 1.1f);
            pig(12.8f, 0, PIG_S, 0);
            hol(I_, 12.8f, t, 0.55f);
        } else {
            t = frame(S_, W_, 12.5f, 0, 3.0f, 1.1f);
            pig(11.9f, 0, PIG_M, 0); pig(13.1f, 0, PIG_M, 0);
            t = tower(W_, W_, 12.5f, t, 2, 1.6f, 1.1f, 0x001);
            pig(12.5f, t, PIG_M, 1);
            t = hol(W_, 9.8f, 0, 0.55f); t = hol(W_, 9.8f, t, 0.55f);
        }
        break;
    case 7:
        t = frame(S_, S_, 12.4f, 0, 3.0f, 1.2f);
        pig(11.8f, 0, PIG_L, 0); pig(13.0f, 0, PIG_M, 1);
        t = frame(S_, W_, 12.4f, t, 2.0f, 1.1f);
        t2 = t;
        pig(12.4f, t - 1.1f - T_, PIG_M, 1);
        t = hol(W_, 12.4f, t2, 0.55f);
        pig(12.4f, t, PIG_S, 0);
        t = hol(W_, 9.6f, 0, 0.55f); t = hol(W_, 9.6f, t, 0.55f); t = hol(W_, 9.6f, t, 0.55f);
        pig(9.6f, t, PIG_S, 0);
        break;
    case 8:
        t = tower(W_, W_, 12.0f, 0, 5, 1.4f, 1.1f, 0x015);
        pig(12.0f, t, PIG_M, 0);
        t = tower(I_, I_, 14.8f, 0, 2, 1.3f, 1.1f, 0x001);
        pig(14.8f, t, PIG_S, 0);
        t = hol(I_, 9.8f, 0, 0.55f);
        hol(I_, 9.8f, t, 0.55f);
        break;
    case 9:
        if (room == 0) {
            plat(9.0f, 10.6f, 1.6f);
            plat(14.4f, 16.0f, 1.6f);
            blk(W_, 10.3f, 1.6f, T_, 1.1f);
            blk(W_, 14.7f, 1.6f, T_, 1.1f);
            t = blk(W_, 12.5f, 1.6f + 1.1f, 5.0f, T_);
            pig(11.0f, t, PIG_M, 0);
            pig(12.5f, t, PIG_S, 0);
            pig(14.0f, t, PIG_M, 0);
        } else {
            t = frame(S_, S_, 13.0f, 0, 2.6f, 1.3f);
            pig(13.0f, 0, PIG_KING, 0);
            t = frame(W_, W_, 13.0f, t, 1.6f, 1.1f);
            hol(S_, 13.0f, t, 0.55f);
            t = tower(W_, I_, 10.4f, 0, 2, 1.3f, 1.1f, 0x003);
            t = tower(I_, W_, 15.6f, 0, 2, 1.3f, 1.1f, 0x002);
            pig(15.6f, t, PIG_S, 1);
        }
        break;
    case 10:
        t = tower(I_, I_, 10.6f, 0, 3, 1.6f, 1.1f, 0x107);
        pig(10.6f, t, PIG_S, 0);
        t = tower(I_, I_, 13.6f, 0, 3, 1.6f, 1.1f, 0x105);
        pig(13.6f, t, PIG_S, 0);
        break;
    case 11:
        if (room == 0) {
            island(9.0f, 12.0f, 2.2f);
            t = tower(W_, I_, 10.5f, 2.2f, 2, 1.8f, 1.1f, 0x103);
            pig(10.5f, t, PIG_S, 0);
            t = frame(I_, I_, 13.6f, 0, 1.5f, 1.1f);
            pig(13.6f, 0, PIG_M, 0);
            pig(13.6f, t, PIG_S, 0);
        } else {
            t = pyramid(S_, 13.0f, 0, 4, 0.55f);
            pig(13.0f, t, PIG_M, 1);
            pig(11.4f, 0, PIG_M, 0);
            pig(14.6f, 0, PIG_M, 0);
            t = frame(W_, W_, 10.0f, 0, 1.4f, 1.1f);
            hol(I_, 10.0f, t, 0.55f);
        }
        break;
    case 12:
        plat(11.6f, 17.4f, 2.2f);
        t = frame(S_, S_, 13.2f, 2.2f, 2.2f, 1.1f);
        pig(13.2f, 2.2f, PIG_L, 1);
        t = tower(W_, I_, 13.2f, t, 2, 1.6f, 1.1f, 0x102);
        pig(13.2f, t, PIG_S, 0);
        t = tower(I_, I_, 16.0f, 2.2f, 3, 1.3f, 1.1f, 0x005);
        pig(16.0f, t, PIG_M, 0);
        t = hol(W_, 9.6f, 0, 0.55f); hol(W_, 9.6f, t, 0.55f);
        pig(10.4f, 0, PIG_S, 0);
        break;
    case 13:
        if (room == 0) {
            t = tower(S_, W_, 11.0f, 0, 2, 2.0f, 1.1f, 0x103);
            ball(S_, 10.5f, t, 0.3f);
            pig(11.4f, t, PIG_S, 0);
            t = tower(I_, I_, 14.0f, 0, 3, 1.4f, 1.1f, 0x002);
            pig(14.0f, t, PIG_M, 0);
        } else {
            island(10.0f, 13.6f, 3.0f);
            t = tower(W_, W_, 11.8f, 3.0f, 2, 2.4f, 1.1f, 0x103);
            pig(11.8f, t, PIG_M, 1);
            t = frame(S_, I_, 15.4f, 0, 2.2f, 1.2f);
            pig(15.4f, 0, PIG_L, 0);
            t = frame(I_, I_, 15.4f, t, 1.4f, 1.1f);
            pig(15.4f, t, PIG_S, 0);
        }
        break;
    case 14:
        if (room == 0) {
            t = pyramid(I_, 11.5f, 0, 3, 0.55f);
            pig(11.5f, t, PIG_S, 0);
            t = tower(W_, I_, 14.2f, 0, 3, 1.4f, 1.1f, 0x005);
            pig(14.2f, t, PIG_S, 1);
        } else if (room == 1) {
            plat(10.8f, 12.6f, 1.4f);
            t = tower(W_, W_, 11.7f, 1.4f, 3, 1.4f, 1.1f, 0x002);
            pig(11.7f, t, PIG_M, 0);
            plat(14.6f, 16.6f, 2.4f);
            t = tower(S_, W_, 15.6f, 2.4f, 2, 1.6f, 1.1f, 0x101);
            pig(15.6f, t, PIG_S, 1);
        } else {
            t = frame(S_, S_, 13.6f, 0, 3.0f, 1.3f);
            pig(13.6f, 0, PIG_KING, 0);
            t = tower(I_, I_, 13.6f, t, 2, 2.0f, 1.1f, 0x003);
            hol(S_, 13.6f, t, 0.55f);
            t = tower(W_, W_, 10.6f, 0, 2, 1.3f, 1.1f, 0x003);
        }
        break;
    case 15:
        t = frame(S_, S_, 11.0f, 0, 2.4f, 1.2f);
        pig(11.0f, 0, PIG_L, 1);
        t = frame(S_, W_, 11.0f, t, 2.0f, 1.1f);
        pig(11.0f, t - 1.1f - T_, PIG_M, 0);
        pig(11.0f, t, PIG_S, 1);
        t = frame(S_, S_, 14.4f, 0, 2.4f, 1.2f);
        pig(14.4f, 0, PIG_L, 0);
        t = tower(W_, S_, 14.4f, t, 2, 1.6f, 1.1f, 0x003);
        ball(S_, 14.4f, t, 0.32f);
        break;
    case 16:
        if (room == 0) {
            island(9.6f, 12.2f, 2.6f);
            t = tower(S_, W_, 10.9f, 2.6f, 2, 1.8f, 1.1f, 0x103);
            pig(10.9f, t, PIG_S, 1);
            t = tower(W_, W_, 14.4f, 0, 4, 1.4f, 1.1f, 0x00A);
            pig(14.4f, t, PIG_M, 0);
        } else {
            t = pyramid(S_, 12.6f, 0, 5, 0.55f);
            pig(12.6f, t, PIG_L, 1);
            t = tower(I_, I_, 9.8f, 0, 2, 1.3f, 1.1f, 0x003);
            t = tower(W_, I_, 15.6f, 0, 3, 1.4f, 1.1f, 0x005);
            pig(15.6f, t, PIG_S, 0);
        }
        break;
    case 17:
        plat(12.0f, 17.6f, 1.6f);
        t = frame(S_, S_, 13.6f, 1.6f, 2.6f, 1.2f);
        pig(13.0f, 1.6f, PIG_M, 1); pig(14.2f, 1.6f, PIG_M, 1);
        t = frame(S_, S_, 13.6f, t, 2.0f, 1.1f);
        pig(13.6f, t - 1.1f - T_, PIG_L, 0);
        t = tower(W_, I_, 13.6f, t, 2, 1.4f, 1.1f, 0x002);
        pig(13.6f, t, PIG_S, 1);
        t = tower(S_, W_, 16.4f, 1.6f, 3, 1.3f, 1.1f, 0x005);
        pig(16.4f, t, PIG_S, 0);
        t = frame(W_, W_, 9.8f, 0, 1.5f, 1.1f);
        pig(9.8f, 0, PIG_S, 0);
        break;
    case 18:
        if (room == 0) {
            t = tower(S_, S_, 10.4f, 0, 3, 1.8f, 1.1f, 0x107);
            pig(10.4f, t, PIG_S, 1);
            t = tower(I_, W_, 13.4f, 0, 3, 1.6f, 1.1f, 0x105);
            ball(S_, 13.4f, t, 0.3f);
        } else {
            plat(9.4f, 11.4f, 2.0f);
            t = tower(W_, I_, 10.4f, 2.0f, 2, 1.6f, 1.1f, 0x003);
            plat(13.2f, 17.2f, 1.0f);
            t = frame(S_, S_, 15.2f, 1.0f, 3.2f, 1.3f);
            pig(15.2f, 1.0f, PIG_KING, 0);
            t = tower(S_, W_, 15.2f, t, 2, 2.0f, 1.1f, 0x101);
            pig(15.2f, t, PIG_M, 1);
        }
        break;
    default: /* 19 */
        if (room == 0) {
            t = tower(W_, W_, 10.0f, 0, 4, 1.4f, 1.1f, 0x00F);
            pig(10.0f, t, PIG_S, 0);
            t = tower(S_, I_, 13.0f, 0, 2, 2.0f, 1.2f, 0x103);
            pig(13.0f, t, PIG_M, 1);
        } else if (room == 1) {
            island(9.6f, 12.6f, 2.8f);
            t = tower(S_, S_, 11.1f, 2.8f, 2, 2.2f, 1.1f, 0x303);
            pig(11.1f, t, PIG_S, 1);
            t = pyramid(I_, 15.0f, 0, 4, 0.55f);
            pig(15.0f, t, PIG_M, 1);
            pig(13.3f, 0, PIG_M, 0);
        } else {
            plat(12.4f, 18.0f, 1.2f);
            t = frame(S_, S_, 14.0f, 1.2f, 3.0f, 1.3f);
            pig(14.0f, 1.2f, PIG_KING, 0);
            t = frame(S_, S_, 14.0f, t, 2.4f, 1.2f);
            pig(14.0f, t - 1.2f - T_, PIG_L, 1);
            t = tower(W_, I_, 14.0f, t, 2, 1.6f, 1.1f, 0x003);
            t = tower(S_, W_, 16.9f, 1.2f, 3, 1.2f, 1.1f, 0x005);
            pig(16.9f, t, PIG_S, 1);
            t = frame(W_, W_, 10.0f, 0, 1.5f, 1.1f);
            pig(10.0f, 0, PIG_M, 1);
        }
        break;
    }
}

/* ================================================================ STATE == */
enum { SC_TITLE, SC_SELECT, SC_PLAY };
enum { PH_INTRO, PH_AIM, PH_FLY, PH_SETTLE, PH_CLEAR, PH_WIPE, PH_BONUS, PH_WIN, PH_FAIL };
enum { P_SHARD, P_DUST, P_FEATHER, P_SMOKE, P_SPARK, P_RING, P_FIRE, P_SNOW };

typedef struct { int kind; V2 p, v; float a, va, life, max, size; PA_Color col; } Part;
typedef struct { V2 p; float t; int value; PA_Color col; float size; int screen; } Floater;
#define MAXP 900
#define MAXF 48
#define MAXDECK 10
#define TRAILN 220

static struct {
    int   screen; float time, scr_t;
    int   W, H;
    int   level, room, nrooms, theme;
    int   phase; float pt;
    int   deck[MAXDECK], used[MAXDECK], ndeck, sel, ninit;
    int   bird, flock[3], nflock;
    float load_t;
    int   dragging; V2 press, pull; float pull_step;
    int   score; float shown;
    float destr, destr_cap, destr_flash; int destr_paid; float card_pop;
    int   pigs_total, pigs_alive, blockpts_total, t2, t3;
    float cx, z, shake, fit_z, fit_cx, lx0, lx1, ly1;
    V2    trail[TRAILN]; int ntrail; float trail_acc;
    Part  parts[MAXP]; int np;
    Floater fl[MAXF];
    int   stars, best, newbest; float res_t; int bonus_left; float bonus_t;
    float clear_t;
    float wipe; int wipe_loaded;
    float banner_t; const char *banner;
    float bot_t; int bot_stage; V2 bot_pull; float bot_tx, bot_ty; int bot_target;
    float cd_wood, cd_ice, cd_stone, cd_pig, cd_bird;
    float hint_t;
    int   progress_stars[NLEVELS];
    int   progress_best[NLEVELS];
    int   unlocked;
    float sel_scroll;
    int   last_result;          /* 1 win, 2 fail */
} S;


static int demo(void) { return pa_demo_mode(); }
static int bot_on(void) { return demo() != 0; }
static int bot_weak(void) { return demo() >= 200 && demo() < 300; }

/* ------------------------------------------------------------ progress -- */
static void progress_load(void) {
    char key[32];
    S.unlocked = 0;
    for (int i = 0; i < NLEVELS; i++) {
        snprintf(key, sizeof key, "avian.star%d", i + 1);
        S.progress_stars[i] = pa_save_get(key, 0);
        snprintf(key, sizeof key, "avian.best%d", i + 1);
        S.progress_best[i] = pa_save_get(key, 0);
    }
    for (int i = 0; i < NLEVELS; i++) { if (S.progress_stars[i] > 0) S.unlocked = i + 1; }
    if (S.unlocked > NLEVELS - 1) S.unlocked = NLEVELS - 1;
    if (demo() == 300) {
        /* Review capture of the map: a campaign part way through. */
        static const int st[9] = { 3, 3, 2, 3, 1, 2, 3, 2, 1 };
        for (int i = 0; i < 9; i++) { S.progress_stars[i] = st[i]; S.progress_best[i] = 30000 + i * 7000; }
        S.unlocked = 9;
    }
}

static void progress_save(int L, int stars, int score) {
    if (demo()) {            /* captures never touch the player's save */
        if (stars > S.progress_stars[L]) S.progress_stars[L] = stars;
        if (score > S.progress_best[L]) S.progress_best[L] = score;
        if (L + 1 > S.unlocked && L + 1 < NLEVELS) S.unlocked = L + 1;
        return;
    }
    char key[32];
    if (stars > S.progress_stars[L]) {
        S.progress_stars[L] = stars;
        snprintf(key, sizeof key, "avian.star%d", L + 1);
        pa_save_set(key, stars);
    }
    if (score > S.progress_best[L]) {
        S.progress_best[L] = score;
        snprintf(key, sizeof key, "avian.best%d", L + 1);
        pa_save_set(key, score);
    }
    if (L + 1 > S.unlocked && L + 1 < NLEVELS) S.unlocked = L + 1;
    pa_save_flush();
}

/* ------------------------------------------------------------- effects -- */
static Part *part_new(int kind, V2 p, V2 v, float life, float size, PA_Color col) {
    Part *q;
    if (S.np < MAXP) q = &S.parts[S.np++];
    else {
        /* Full: recycle the oldest-looking slot deterministically. */
        int best = 0; float bl = 1e9f;
        for (int i = 0; i < MAXP; i++) if (S.parts[i].life < bl) { bl = S.parts[i].life; best = i; }
        q = &S.parts[best];
    }
    memset(q, 0, sizeof *q);
    q->kind = kind; q->p = p; q->v = v; q->life = q->max = life; q->size = size; q->col = col;
    return q;
}

static PA_Rng g_fx_rng;
static float frand(float a, float b) { return pa_rng_range(&g_fx_rng, a, b); }

static void floater(V2 p, int value, PA_Color col, float size) {
    for (int i = 0; i < MAXF; i++) if (S.fl[i].t <= 0.0f) {
        S.fl[i].p = p; S.fl[i].t = 1.4f; S.fl[i].value = value; S.fl[i].col = col; S.fl[i].size = size; S.fl[i].screen = 0;
        return;
    }
}

static PA_Color mat_col(int mat, int k) {
    static const uint32_t C[3][3] = {
        { 0xE9A43C, 0xB86A1F, 0xFFD27A },     /* wood: face, dark, light */
        { 0x9EDDF4, 0x4E9CC8, 0xE4FAFF },     /* ice */
        { 0x9AA5B5, 0x5D6878, 0xC9D1DC },     /* stone */
    };
    return pa_hex(C[mat][k]);
}

static void burst_shards(V2 p, int mat, int n, float speed, float size) {
    for (int i = 0; i < n; i++) {
        float a = frand(0, PA_TAU), s = frand(0.3f, 1.0f) * speed;
        Part *q = part_new(P_SHARD, p, v2(cosf(a) * s, sinf(a) * s + speed * 0.35f), frand(0.7f, 1.3f),
                           size * frand(0.5f, 1.2f), mat_col(mat, i % 3 == 0 ? 1 : (i % 3 == 1 ? 0 : 2)));
        q->a = frand(0, PA_TAU); q->va = frand(-12, 12);
    }
}

static void burst_dust(V2 p, int n, float spread, PA_Color col) {
    for (int i = 0; i < n; i++) {
        V2 o = v2(frand(-spread, spread), frand(-spread * 0.6f, spread * 0.6f));
        part_new(P_DUST, vadd(p, o), v2(o.x * 0.8f, frand(0.2f, 0.9f)), frand(0.6f, 1.1f), frand(0.25f, 0.5f), col);
    }
}

static void burst_feathers(V2 p, PA_Color col, int n) {
    for (int i = 0; i < n; i++) {
        Part *q = part_new(P_FEATHER, p, v2(frand(-3, 3), frand(0, 4)), frand(1.0f, 1.8f), frand(0.12f, 0.2f), col);
        q->a = frand(0, PA_TAU); q->va = frand(-6, 6);
    }
}

static void add_shake(float s) { if (s > S.shake) S.shake = s; }

/* --------------------------------------------------------------- sound -- */
static void snd_hit(int mat, float e) {
    float g = pa_clampf(e / 60.0f, 0.03f, 0.16f);
    if (mat == M_WOOD && S.cd_wood <= 0) { pa_tone(240, 150, 0.08f, 1, g); pa_noise(0.05f, g * 0.6f); S.cd_wood = 0.06f; }
    else if (mat == M_ICE && S.cd_ice <= 0) { pa_tone(1900, 1500, 0.07f, 0, g * 0.8f); S.cd_ice = 0.06f; }
    else if (mat == M_STONE && S.cd_stone <= 0) { pa_tone(130, 70, 0.10f, 0, g * 1.2f); pa_noise(0.06f, g * 0.5f); S.cd_stone = 0.07f; }
}
static void snd_break(int mat) {
    if (mat == M_WOOD) { pa_noise(0.16f, 0.13f); pa_tone(320, 90, 0.18f, 3, 0.08f); }
    else if (mat == M_ICE) { pa_noise(0.12f, 0.10f); pa_tone(2600, 900, 0.20f, 0, 0.07f); pa_tone(3400, 1800, 0.12f, 0, 0.04f); }
    else { pa_noise(0.24f, 0.15f); pa_tone(110, 40, 0.26f, 3, 0.10f); }
}
static void snd_pig_hit(void) { if (S.cd_pig <= 0) { pa_tone(330, 250, 0.10f, 2, 0.05f); S.cd_pig = 0.15f; } }
static void snd_pig_pop(void) { pa_tone(520, 1250, 0.10f, 0, 0.11f); pa_noise(0.10f, 0.08f); }
static void snd_squawk(int bt) {
    float base = bt == BT_BLUE ? 1300.0f : bt == BT_BOMB ? 380.0f : bt == BT_YELLOW ? 900.0f : 700.0f;
    pa_tone(base, base * 1.45f, 0.09f, 2, 0.06f);
    pa_tone(base * 1.4f, base * 0.9f, 0.12f, 2, 0.05f);
}

/* -------------------------------------------------------- level flow ---- */
static void destroy_body(int i, int scored);

static void apply_hit(Body *x, Body *other, float e) {
    if (x->kind == K_BLOCK) {
        float th = MATS[x->mat].thresh;
        if (e <= th) return;
        float mult = other->kind == K_BIRD ? BIRDS[other->bird_t].aff[x->mat]
                   : other->kind == K_STATIC ? 0.55f : 0.7f;
        x->dmg += (e - th) * mult;
        if (other->kind == K_BIRD) x->dmg_bird = other->bird_t;
    } else if (x->kind == K_PIG) {
        if (e <= 0.9f) return;
        float mult = other->kind == K_BIRD ? 1.6f : 0.85f;
        x->dmg += (e - 0.9f) * mult;
    } else if (x->kind == K_BIRD) {
        if (e > 2.0f) {
            if (!x->hits) x->first_hit = 0.0001f;
            x->hits++;
        }
    }
    if (e > x->impact) x->impact = e;
}

static void on_impact(Body *a, Body *b, float e, V2 at) {
    if (!g_armed) return;
    apply_hit(a, b, e);
    apply_hit(b, a, e);
    Body *bird = a->kind == K_BIRD ? a : (b->kind == K_BIRD ? b : NULL);
    Body *blkb = a->kind == K_BLOCK ? a : (b->kind == K_BLOCK ? b : NULL);
    if (blkb && e > 4.0f) snd_hit(blkb->mat, e);
    if (blkb && e > 10.0f) burst_shards(at, blkb->mat, e > 60.0f ? 4 : 2, 3.0f, 0.10f);
    if (bird && e > 6.0f) {
        if (bird->hits <= 1 && S.cd_bird <= 0) { snd_squawk(bird->bird_t); S.cd_bird = 0.4f; }
        static const uint32_t FC[BT_COUNT] = { 0xD8262B, 0xF4CF1E, 0x5DB7EA, 0x2A2D33, 0xD5DAE2 };
        burst_feathers(bird->p, pa_hex(FC[bird->bird_t]), e > 40.0f ? 5 : 2);
        if (e > 40.0f) add_shake(0.35f);
    }
    if (e > 25.0f && !bird) burst_dust(at, 2, 0.2f, PA_RGBA(235, 225, 205, 200));
}

static int count_pigs(void) {
    int n = 0;
    for (int i = 0; i < MAXB; i++) if (g_b[i].used && g_b[i].kind == K_PIG) n++;
    return n;
}

/* Height of the ground strip below the ground line. A portrait canvas (a
   phone that refused to rotate) keeps the scene mid-screen instead of
   leaving it at the bottom under a tall empty sky. */
static float ground_pad(float W, float H) { return W < H ? H * 0.30f : H * 0.13f; }

static void compute_fit(void) {
    float W = (float)S.W, H = (float)S.H;
    float gp = ground_pad(W, H);
    float spanx = S.lx1 - S.lx0;
    float zx = W / spanx;
    float zy = (H - gp - 74.0f) / (S.ly1 + 1.0f);
    float z = zx < zy ? zx : zy;
    if (z > 76.0f) z = 76.0f;
    if (W < H && z < 20.0f) z = 20.0f;
    S.fit_z = z;
    S.fit_cx = (S.lx0 + S.lx1) * 0.5f;
    /* Keep the slingshot side anchored when the level is narrower than the view. */
    float half = W * 0.5f / z;
    if (S.fit_cx - half > S.lx0) S.fit_cx = S.lx0 + half;
}

static void load_room(int r) {
    S.room = r;
    phys_reset();
    g_bld_pigs = g_bld_blockpts = 0; g_bld_maxx = 0; g_bld_maxy = 0;
    g_bld_seed = (uint32_t)(S.level * 977 + r * 131 + 7);
    int gi = phys_box(K_STATIC, 20.0f, -5.0f, 240.0f, 10.0f, 0.0f, 0.0f);
    g_b[gi].mat = M_ROCK; g_b[gi].var = 9; g_b[gi].fric = 0.9f;
    plat(-2.8f, 1.4f, LEDGE_TOP);
    g_b[gi + 1].var = 3;
    build_room(S.level, r);
    S.lx0 = -3.0f;
    S.lx1 = g_bld_maxx + 2.2f;
    if (S.lx1 < 15.5f) S.lx1 = 15.5f;
    S.ly1 = g_bld_maxy > 4.0f ? g_bld_maxy : 4.0f;
    /* Let the room settle before damage is armed: stacking pushes things a
       hair, and that must not count as an impact. */
    g_armed = 0;
    for (int k = 0; k < 360; k++) phys_step(1.0f / 120.0f);
    /* Give a slow settler more time, then park everything: a level must
       stand perfectly still until the first bird arrives. */
    for (int k = 0; k < 600; k++) {
        int awake = 0;
        for (int i = 0; i < MAXB; i++) if (g_b[i].used && g_b[i].im > 0.0f && g_b[i].awake) awake++;
        if (!awake) break;
        phys_step(1.0f / 120.0f);
    }
    for (int i = 0; i < MAXB; i++) if (g_b[i].used && g_b[i].im > 0.0f) {
        g_b[i].awake = 0; g_b[i].v = v2(0, 0); g_b[i].w = 0.0f; g_b[i].sleep_t = 1.0f;
    }
    g_armed = 1;
    for (int i = 0; i < MAXB; i++) if (g_b[i].used) { g_b[i].hp = g_b[i].hpmax; g_b[i].flash = 0; }
    S.pigs_alive = count_pigs();
    S.bird = -1; S.nflock = 0;
    S.ntrail = 0;
    S.np = 0;
    S.clear_t = 0;
    compute_fit();
    S.z = S.fit_z * 1.12f;
    S.cx = S.lx1 - (float)S.W * 0.5f / S.z + 0.5f;
    S.phase = PH_INTRO; S.pt = 0;
    S.dragging = 0; S.pull = v2(0, 0);
    S.load_t = 0;
    S.bot_stage = 0; S.bot_t = 0;
}

static void deck_pick_next(void) {
    if (S.sel >= 0 && S.sel < S.ndeck && !S.used[S.sel]) return;
    S.sel = -1;
    for (int i = 0; i < S.ndeck; i++) if (!S.used[i]) { S.sel = i; break; }
}

static void start_level(int L) {
    if (L < 0) L = 0;
    if (L >= NLEVELS) L = NLEVELS - 1;
    S.screen = SC_PLAY; S.scr_t = 0;
    S.level = L; S.theme = level_theme(L); S.nrooms = LEVELS[L].rooms;
    pa_rng_seed(&g_fx_rng, (uint32_t)(L * 7919 + 17));
    S.pigs_total = 0; S.blockpts_total = 0;
    for (int r = 0; r < S.nrooms; r++) {
        phys_reset();
        g_bld_pigs = g_bld_blockpts = 0;
        build_room(L, r);
        S.pigs_total += g_bld_pigs;
        S.blockpts_total += g_bld_blockpts;
    }
    const char *bs = LEVELS[L].birds;
    S.ndeck = 0;
    for (const char *p = bs; *p && S.ndeck < MAXDECK; p++) {
        int t = *p == 'Y' ? BT_YELLOW : *p == 'B' ? BT_BLUE : *p == 'K' ? BT_BOMB : *p == 'S' ? BT_SILVER : BT_RED;
        S.deck[S.ndeck] = t; S.used[S.ndeck] = 0; S.ndeck++;
    }
    S.ninit = S.ndeck;
    S.sel = 0;
    S.score = 0; S.shown = 0;
    S.destr = 0; S.destr_cap = (float)S.blockpts_total * 0.45f; S.destr_paid = 0; S.card_pop = 0;
    float pig_pts = (float)S.pigs_total * 5000.0f;
    S.t2 = (int)((pig_pts + S.blockpts_total * 0.35f + 10000.0f * (S.ninit >= 3 ? 1 : 0)) / 1000.0f) * 1000;
    S.t3 = (int)((pig_pts + S.blockpts_total * 0.55f + 10000.0f * (float)(S.ninit - 1) * 0.6f) / 1000.0f) * 1000;
    if (S.t3 <= S.t2) S.t3 = S.t2 + 5000;
    S.newbest = 0; S.stars = 0; S.res_t = 0;
    for (int i = 0; i < MAXF; i++) S.fl[i].t = 0;
    S.banner = NULL;
    load_room(0);
    S.banner_t = 2.0f;
    S.hint_t = L == 0 ? 6.0f : 0.0f;
    S.last_result = 0;
}

/* ------------------------------------------------------------- damage --- */
static void destroy_body(int i, int scored) {
    Body *b = &g_b[i];
    if (!b->used) return;
    if (b->kind == K_BLOCK) {
        float sz = b->shape == SH_CIRC ? b->r * 2.0f : (b->hw + b->hh);
        int n = 6 + (int)(sz * 6.0f);
        burst_shards(b->p, b->mat, n, 3.5f, b->mat == M_ICE ? 0.13f : 0.12f);
        burst_dust(b->p, 3, (b->hw > b->hh ? b->hw : b->hh) * 0.6f,
                   b->mat == M_ICE ? PA_RGBA(225, 245, 255, 190) : PA_RGBA(230, 220, 200, 200));
        snd_break(b->mat);
        if (scored) {
            int pts = b->shape == SH_CIRC ? MATS[b->mat].score : block_points(b->mat, b->hw * 2, b->hh * 2);
            S.score += pts;
            S.destr += (float)pts;
            floater(b->p, pts, b->mat == M_WOOD ? PA_RGB(255, 205, 90) : b->mat == M_ICE ? PA_RGB(170, 235, 255) : PA_RGB(225, 230, 240), 0.5f);
        }
    } else if (b->kind == K_PIG) {
        for (int k = 0; k < 9; k++) {
            float a = (float)k / 9.0f * PA_TAU;
            V2 v = v2(cosf(a) * 1.6f, sinf(a) * 1.6f + 0.6f);
            part_new(P_SMOKE, vadd(b->p, vmul(v, b->r * 0.25f)), v, frand(0.7f, 1.1f), b->r * frand(0.55f, 0.85f),
                     k % 3 == 0 ? PA_RGBA(160, 220, 110, 230) : PA_RGBA(245, 250, 240, 235));
        }
        snd_pig_pop();
        add_shake(0.25f);
        int pts = (b->var & 7) == PIG_KING ? 10000 : 5000;
        S.score += pts;
        floater(b->p, pts, PA_RGB(150, 230, 90), 0.75f);
        S.pigs_alive--;
    }
    phys_remove(i);
}

static void hurt(int i, float amount) {
    Body *b = &g_b[i];
    if (!b->used || amount <= 0.0f) return;
    if (b->kind != K_BLOCK && b->kind != K_PIG) return;
    b->hp -= amount;
    b->flash = 1.0f;
    if (b->kind == K_PIG) { b->squash = pa_clampf(amount / 6.0f, 0.2f, 1.0f); if (b->hp > 0) snd_pig_hit(); }
    if (b->hp <= 0.0f) destroy_body(i, 1);
}

static void post_step(void) {
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (!b->used || b->im == 0.0f) continue;
        if (b->dmg > 0.0f) hurt(i, b->dmg);
        if (!b->used) continue;
        if (b->p.y < -4.0f || b->p.x < S.lx0 - 14.0f || b->p.x > S.lx1 + 14.0f) {
            if (b->kind == K_BIRD) phys_remove(i);
            else destroy_body(i, b->kind == K_PIG);
        }
    }
    /* Destructometer pays out one extra card. */
    if (!S.destr_paid && S.destr >= S.destr_cap && S.destr_cap > 0 && S.ndeck < MAXDECK) {
        S.destr_paid = 1;
        S.deck[S.ndeck] = BT_RED; S.used[S.ndeck] = 0; S.ndeck++;
        S.card_pop = 1.6f;
        pa_tone(660, 990, 0.12f, 1, 0.10f); pa_tone(990, 1320, 0.16f, 1, 0.08f);
    }
}

/* --------------------------------------------------------- birds -------- */
static V2 anchor(void) { return v2(SLING_X, ANCHOR_Y); }

static int spawn_bird(int bt, V2 p, V2 v) {
    const BirdDef *d = &BIRDS[bt];
    int i = phys_circle(K_BIRD, p.x, p.y, d->r, d->dens);
    if (i < 0) return -1;
    Body *b = &g_b[i];
    b->bird_t = bt; b->rest = d->rest; b->fric = 0.6f; b->adamp = 0.8f; b->roll = 0.03f;
    b->v = v;
    b->w = -v.x * 0.4f;
    return i;
}

static void launch(void) {
    if (S.sel < 0) return;
    V2 p = vadd(anchor(), S.pull);
    V2 v = vmul(S.pull, -VMAX / MAXPULL);
    int bt = S.deck[S.sel];
    S.bird = spawn_bird(bt, p, v);
    S.used[S.sel] = 1;
    S.nflock = 0;
    S.phase = PH_FLY; S.pt = 0;
    S.dragging = 0;
    S.ntrail = 0; S.trail_acc = 0;
    S.pull = v2(0, 0);
    pa_noise(0.12f, 0.10f);
    pa_tone(300, 900, 0.16f, 1, 0.08f);
    snd_squawk(bt);
}

static void ability(void) {
    if (S.bird < 0) return;
    Body *b = &g_b[S.bird];
    if (!b->used || b->ability_spent) return;
    b->ability_spent = 1;
    V2 p = b->p;
    switch (b->bird_t) {
    case BT_RED: {
        /* Battle cry: a shockwave that shoves everything in front. */
        for (int i = 0; i < MAXB; i++) {
            Body *o = &g_b[i];
            if (!o->used || o->im == 0.0f || i == S.bird) continue;
            V2 d = vsub(o->p, p);
            float dist = vlen(d);
            if (dist > 3.4f || dist < 1e-4f) continue;
            float k = 1.0f - dist / 3.4f;
            body_kick(o, vmul(d, (7.5f * k / dist) * o->m), 0.0f);
            if (o->kind == K_BLOCK) o->dmg_bird = BT_RED;
        }
        for (int k = 0; k < 3; k++) {
            Part *q = part_new(P_RING, p, v2(0, 0), 0.35f + k * 0.1f, 0.3f + k * 0.3f, PA_RGBA(255, 255, 255, 220));
            q->va = 5.0f + k * 1.0f;
        }
        pa_tone(220, 420, 0.25f, 2, 0.10f); pa_tone(330, 520, 0.22f, 3, 0.06f);
        add_shake(0.3f);
        break;
    }
    case BT_YELLOW: {
        float sp = vlen(b->v);
        V2 dir = sp > 0.01f ? vmul(b->v, 1.0f / sp) : v2(1, 0);
        float ns = sp * 2.0f; if (ns < 25.0f) ns = 25.0f;
        b->v = vmul(dir, ns);
        for (int k = 0; k < 8; k++)
            part_new(P_SPARK, p, vadd(vmul(dir, -frand(2, 6)), v2(frand(-1.5f, 1.5f), frand(-1.5f, 1.5f))), 0.35f, 0.1f, PA_RGB(255, 236, 120));
        pa_tone(500, 1600, 0.18f, 3, 0.07f);
        break;
    }
    case BT_BLUE: {
        S.nflock = 0;
        for (int s = -1; s <= 1; s += 2) {
            float ang = 0.16f * (float)s;
            V2 nv = vrot(b->v, cosf(ang), sinf(ang));
            V2 np = vadd(p, vmul(v2(-b->v.y, b->v.x), 0.02f * (float)s));
            int k = spawn_bird(BT_BLUE, np, nv);
            if (k >= 0) { g_b[k].ability_spent = 1; S.flock[S.nflock++] = k; }
        }
        for (int k = 0; k < 6; k++) part_new(P_SPARK, p, v2(frand(-2, 2), frand(-2, 2)), 0.3f, 0.09f, PA_RGB(200, 240, 255));
        pa_tone(1200, 1700, 0.06f, 2, 0.05f); pa_tone(1500, 2000, 0.06f, 2, 0.04f);
        break;
    }
    case BT_BOMB: {
        const float R = 3.0f;
        for (int i = 0; i < MAXB; i++) {
            Body *o = &g_b[i];
            if (!o->used || o->im == 0.0f || i == S.bird) continue;
            V2 d = vsub(o->p, p);
            float dist = vlen(d);
            if (dist > R) continue;
            float k = 1.0f - dist / R;
            V2 dir = dist > 1e-4f ? vmul(d, 1.0f / dist) : v2(0, 1);
            body_kick(o, vmul(dir, 13.0f * k * o->m), frand(-1, 1) * o->m * 0.5f);
            if (o->kind == K_BLOCK) hurt(i, 110.0f * k * BIRDS[BT_BOMB].aff[o->mat]);
            else if (o->kind == K_PIG) hurt(i, 26.0f * k);
        }
        part_new(P_RING, p, v2(0, 0), 0.4f, 0.5f, PA_RGBA(255, 240, 200, 255))->va = 9.0f;
        for (int k = 0; k < 16; k++) {
            float a = frand(0, PA_TAU), s = frand(1.5f, 5.0f);
            part_new(P_FIRE, p, v2(cosf(a) * s, sinf(a) * s), frand(0.35f, 0.6f), frand(0.35f, 0.6f), PA_RGB(255, 170, 40));
        }
        for (int k = 0; k < 12; k++) {
            float a = frand(0, PA_TAU), s = frand(0.6f, 2.0f);
            part_new(P_SMOKE, p, v2(cosf(a) * s, sinf(a) * s + 0.8f), frand(1.0f, 1.6f), frand(0.5f, 0.9f), PA_RGBA(70, 66, 70, 220));
        }
        burst_feathers(p, PA_RGB(40, 40, 46), 6);
        pa_sfx("boom");
        pa_tone(90, 35, 0.45f, 3, 0.14f);
        add_shake(1.0f);
        phys_remove(S.bird);
        S.bird = -1;
        break;
    }
    case BT_SILVER: {
        b->v = v2(b->v.x * 0.12f, -26.0f);
        b->w = 18.0f;
        for (int k = 0; k < 6; k++) part_new(P_SPARK, p, v2(frand(-2, 2), frand(1, 3)), 0.35f, 0.1f, PA_RGB(230, 236, 245));
        pa_tone(1400, 300, 0.25f, 3, 0.07f);
        break;
    }
    }
}

static void bird_poof(int i) {
    Body *b = &g_b[i];
    for (int k = 0; k < 6; k++) {
        float a = (float)k / 6.0f * PA_TAU;
        part_new(P_SMOKE, b->p, v2(cosf(a) * 1.2f, sinf(a) * 1.2f + 0.5f), frand(0.6f, 0.9f), b->r * 1.1f, PA_RGBA(250, 250, 250, 230));
    }
    static const uint32_t FC[BT_COUNT] = { 0xD8262B, 0xF4CF1E, 0x5DB7EA, 0x2A2D33, 0xD5DAE2 };
    burst_feathers(b->p, pa_hex(FC[b->bird_t]), 4);
    phys_remove(i);
}

/* Returns 1 while any bird of this shot is still live. */
static int birds_update(float dt) {
    int live = 0;
    int ids[4], n = 0;
    if (S.bird >= 0) ids[n++] = S.bird;
    for (int k = 0; k < S.nflock; k++) ids[n++] = S.flock[k];
    for (int k = 0; k < n; k++) {
        int i = ids[k];
        Body *b = &g_b[i];
        if (!b->used || b->kind != K_BIRD) { if (i == S.bird) S.bird = -1; continue; }
        b->age += dt;
        if (b->hits) b->first_hit += dt;
        float sp = vlen(b->v);
        if (b->hits && sp < 0.6f) b->quiet += dt; else b->quiet = 0;
        if (b->bird_t == BT_BOMB && b->hits && !b->ability_spent && b->first_hit > 1.4f && i == S.bird) {
            ability();
            continue;
        }
        if ((b->hits && b->quiet > 0.7f) || (b->hits && b->first_hit > 4.5f) || b->age > 9.0f) {
            bird_poof(i);
            if (i == S.bird) S.bird = -1;
            continue;
        }
        live = 1;
    }
    /* Flight trail: puffs behind the lead bird until it first hits. */
    if (S.bird >= 0 && g_b[S.bird].used && !g_b[S.bird].hits) {
        S.trail_acc += dt;
        if (S.trail_acc >= 0.028f && S.ntrail < TRAILN) { S.trail_acc = 0; S.trail[S.ntrail++] = g_b[S.bird].p; }
    }
    return live;
}

/* ---------------------------------------------------------- prediction -- */
static int static_blocks(V2 p, float r) {
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (!b->used || b->kind != K_STATIC || b->var == 9) continue;
        if (p.x > b->minx - r && p.x < b->maxx + r && p.y > b->miny - r && p.y < b->maxy + r) return 1;
    }
    return p.y < r;
}

/* Same integrator as the solver (gravity into v, then v into p), so the dots
   are exactly where an unobstructed bird will fly. */
static int predict(V2 p, V2 v, float r, V2 *out, int maxn, int stride, int steps) {
    int n = 0;
    const float dt = 1.0f / 120.0f;
    for (int i = 1; i <= steps; i++) {
        v.y -= GRAVITY * dt;
        p = vadd(p, vmul(v, dt));
        if (i % stride == 0 && n < maxn) out[n++] = p;
        if (static_blocks(p, r * 0.5f)) break;
    }
    return n;
}

/* ------------------------------------------------------------- demo bot -- */
static int bot_pick_target(int bt) {
    int best = -1; float score = 1e9f;
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (!b->used || b->kind != K_PIG) continue;
        float s = b->p.x;
        if (bt == BT_BOMB || bt == BT_SILVER) {
            int nb = 0;
            for (int j = 0; j < MAXB; j++) if (g_b[j].used && g_b[j].kind == K_BLOCK && vlen(vsub(g_b[j].p, b->p)) < 1.8f) nb++;
            s = -(float)nb * 2.0f - b->r * 4.0f;
        }
        if (s < score) { score = s; best = i; }
    }
    return best;
}

static int bot_solve(V2 target, int bt, V2 *pull_out) {
    float r = BIRDS[bt].r;
    for (int pass = 0; pass < 2; pass++) {
        for (float pw = 1.0f; pw >= 0.55f; pw -= 0.05f) {
            for (float deg = -8.0f; deg <= 72.0f; deg += 0.25f) {
                float a = deg * PA_PI / 180.0f;
                V2 dir = v2(cosf(a), sinf(a));
                V2 pull = vmul(dir, -MAXPULL * pw);
                V2 p = vadd(anchor(), pull);
                if (p.y < LEDGE_TOP + 0.35f) continue;
                V2 v = vmul(dir, VMAX * pw);
                const float dt = 1.0f / 120.0f;
                float bestd = 1e9f;
                for (int s = 0; s < 600; s++) {
                    v.y -= GRAVITY * dt;
                    p = vadd(p, vmul(v, dt));
                    if (p.x > 1.6f && static_blocks(p, r * 0.7f)) break;
                    float d = vlen(vsub(p, target));
                    if (d < bestd) bestd = d;
                    if (p.x > target.x + 0.6f) break;
                }
                if (bestd < (pass ? 0.35f : 0.14f)) { *pull_out = pull; return 1; }
            }
            if (pass == 0) break;     /* first pass: full power only */
        }
    }
    *pull_out = v2(-MAXPULL * 0.8f, -MAXPULL * 0.45f);
    return 0;
}

static void bot_update(float dt) {
    if (S.phase == PH_AIM && S.load_t >= 1.0f && S.sel >= 0) {
        S.bot_t += dt;
        if (S.bot_stage == 0 && S.bot_t > 0.45f) {
            int bt = S.deck[S.sel];
            S.bot_target = bot_pick_target(bt);
            V2 tg = S.bot_target >= 0 ? g_b[S.bot_target].p : v2(12, 1);
            if (bt == BT_SILVER && S.bot_target >= 0) tg.y += 2.6f;   /* fly over, then slam */
            if (bt == BT_BLUE && S.bot_target >= 0) tg.y += 0.15f;
            if (bot_weak()) tg = v2(30.0f, 9.0f);     /* sail over everything */
            S.bot_tx = tg.x; S.bot_ty = tg.y;
            bot_solve(tg, bt, &S.bot_pull);
            S.bot_stage = 1; S.bot_t = 0;
            S.dragging = 1;
        }
        if (S.bot_stage == 1) {
            float k = pa_smooth(pa_clamp01(S.bot_t / 0.75f));
            S.pull = vmul(S.bot_pull, k);
            if (S.bot_t > 1.25f) { S.bot_stage = 0; S.bot_t = 0; launch(); }
        }
    }
    if (S.phase == PH_FLY && S.bird >= 0 && g_b[S.bird].used && !g_b[S.bird].ability_spent && !bot_weak()) {
        Body *b = &g_b[S.bird];
        float tx = S.bot_tx;
        int go = 0;
        switch (b->bird_t) {
        case BT_RED:    go = b->hits && b->first_hit > 0.06f; break;
        case BT_YELLOW: go = b->p.x > tx - 4.0f || b->hits; break;
        case BT_BLUE:   go = b->p.x > tx - 3.2f; break;
        case BT_BOMB:   go = (b->hits && b->first_hit > 0.05f) || vlen(vsub(b->p, v2(tx, S.bot_ty))) < 0.8f; break;
        case BT_SILVER: go = b->p.x > tx - 0.35f; break;
        }
        if (go) ability();
    }
}

/* ============================================================== UPDATE == */
static float ui_u(void) {
    float u = (float)S.W / 960.0f;
    float v = (float)S.H / 540.0f;
    u = u < v ? u : v;
    return pa_clampf(u, 0.5f, 2.5f);
}

typedef struct { float x, y, w, h; } Rc;
static int in_rc(Rc r, float x, float y) { return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h; }
static int in_circle(float cx, float cy, float r, float x, float y) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; }

static void pause_btn(float *x, float *y, float *r) { float u = ui_u(); *x = 42 * u; *y = 40 * u; *r = 27 * u; }

/* Cards: remaining birds in deck order. Returns count; fills rects. */
static int card_layout(Rc *out, int *idx) {
    float u = ui_u();
    float cw = 60 * u, ch = 76 * u, gap = 8 * u;
    float x = 16 * u, y = (float)S.H - ch - 12 * u;
    int n = 0;
    for (int i = 0; i < S.ndeck; i++) {
        if (S.used[i]) continue;
        Rc r = { x, y, cw, ch };
        if (i == S.sel && (S.phase == PH_AIM || S.phase == PH_INTRO)) r.y -= 12 * u;
        out[n] = r; idx[n] = i; n++;
        x += cw + gap;
    }
    return n;
}

/* Results buttons. */
static void result_buttons(Rc *menu, Rc *retry, Rc *next) {
    float u = ui_u(), W = (float)S.W, H = (float)S.H;
    float y = H - 118 * u;
    *menu  = (Rc){ W * 0.5f - 190 * u, y, 76 * u, 76 * u };
    *retry = (Rc){ W * 0.5f - 96 * u, y, 76 * u, 76 * u };
    *next  = (Rc){ W * 0.5f + 6 * u, y - 4 * u, 190 * u, 84 * u };
    if (S.phase == PH_FAIL) {
        *menu  = (Rc){ W * 0.5f - 150 * u, y, 76 * u, 76 * u };
        *retry = (Rc){ W * 0.5f - 52 * u, y - 4 * u, 200 * u, 84 * u };
        *next  = (Rc){ -1000, -1000, 0, 0 };
    }
}

static Rc play_button(void) {
    float u = ui_u();
    return (Rc){ (float)S.W * 0.5f - 140 * u, (float)S.H * 0.70f - 44 * u, 280 * u, 88 * u };
}

static void select_layout(int L, Rc *r) {
    float u = ui_u();
    float tile = 84 * u, gapx = 18 * u, label = 250 * u;
    float total = label + 5 * tile + 4 * gapx;
    float x0 = ((float)S.W - total) * 0.5f + label;
    float y0 = 96 * u;
    int row = L / 5, col = L % 5;
    r->x = x0 + (float)col * (tile + gapx);
    r->y = y0 + (float)row * 108 * u;
    r->w = tile; r->h = tile;
}

static void enter_title(void) {
    S.screen = SC_TITLE; S.scr_t = 0;
    S.level = 0; S.theme = TH_JUNGLE; S.nrooms = 1;
    S.ndeck = 0;
    load_room(0);
    S.phase = PH_AIM;
    S.cx = S.fit_cx; S.z = S.fit_z;
}

static void enter_select(void) {
    S.screen = SC_SELECT; S.scr_t = 0;
}

static void cam_update(float dt) {
    float W = (float)S.W, H = (float)S.H;
    float tz = S.fit_z, tcx = S.fit_cx, rate = 2.8f;
    if (S.phase == PH_INTRO) {
        if (S.pt < 0.8f) { tz = S.fit_z * 1.12f; tcx = S.lx1 - W * 0.5f / tz + 0.5f; rate = 8.0f; }
        else rate = 3.2f;
    } else if (S.phase == PH_FLY && S.bird >= 0 && g_b[S.bird].used) {
        Body *b = &g_b[S.bird];
        tz = S.fit_z * 1.22f;
        float room = H - ground_pad(W, H) - 64.0f;
        if ((b->p.y + 1.4f) * tz > room) tz = room / (b->p.y + 1.4f);
        if (tz < S.fit_z * 0.75f) tz = S.fit_z * 0.75f;
        tcx = b->p.x + 2.4f;
        rate = 3.6f;
    }
    float half = W * 0.5f / tz;
    float lo = S.lx0 + half - 0.4f, hi = S.lx1 + 1.0f - half;
    if (lo > hi) tcx = S.fit_cx; else tcx = pa_clampf(tcx, lo, hi);
    S.z = pa_approach(S.z, tz, rate, dt);
    S.cx = pa_approach(S.cx, tcx, rate, dt);
}

static void fx_update(float dt) {
    for (int i = 0; i < S.np; i++) {
        Part *q = &S.parts[i];
        q->life -= dt;
        if (q->life <= 0) { S.parts[i] = S.parts[--S.np]; i--; continue; }
        switch (q->kind) {
        case P_SHARD:
            q->v.y -= GRAVITY * dt;
            q->p = vadd(q->p, vmul(q->v, dt));
            q->a += q->va * dt;
            if (q->p.y < 0.03f) { q->p.y = 0.03f; q->v.y *= -0.3f; q->v.x *= 0.6f; q->va *= 0.5f; }
            break;
        case P_FEATHER:
            q->v.y -= 1.2f * dt;
            q->v = vmul(q->v, 1.0f / (1.0f + 2.5f * dt));
            q->p = vadd(q->p, vmul(q->v, dt));
            q->p.x += sinf(q->life * 6.0f + q->a) * 0.6f * dt;
            q->a += q->va * dt;
            break;
        case P_SMOKE: case P_DUST: case P_FIRE:
            q->v = vmul(q->v, 1.0f / (1.0f + 3.0f * dt));
            q->v.y += (q->kind == P_FIRE ? 0.5f : 0.35f) * dt;
            q->p = vadd(q->p, vmul(q->v, dt));
            break;
        case P_SPARK:
            q->v.y -= 3.0f * dt;
            q->p = vadd(q->p, vmul(q->v, dt));
            break;
        default: break;
        }
    }
    for (int i = 0; i < MAXF; i++) if (S.fl[i].t > 0) {
        S.fl[i].t -= dt;
        if (!S.fl[i].screen) S.fl[i].p.y += 0.55f * dt;
        else S.fl[i].p.y -= 40.0f * dt;
    }
    S.shake = S.shake > 0 ? S.shake - dt * 2.2f : 0;
    if (S.cd_wood > 0) S.cd_wood -= dt;
    if (S.cd_ice > 0) S.cd_ice -= dt;
    if (S.cd_stone > 0) S.cd_stone -= dt;
    if (S.cd_pig > 0) S.cd_pig -= dt;
    if (S.cd_bird > 0) S.cd_bird -= dt;
    if (S.destr_flash > 0) S.destr_flash -= dt;
    if (S.card_pop > 0) S.card_pop -= dt;
    if (S.banner_t > 0) S.banner_t -= dt;
    if (S.hint_t > 0 && S.phase != PH_AIM) S.hint_t -= dt * 0.25f;
    /* Pig faces: blink and look at the bird. */
    float tx = S.bird >= 0 && g_b[S.bird].used ? g_b[S.bird].p.x : SLING_X;
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (!b->used) continue;
        if (b->flash > 0) b->flash -= dt * 4.0f;
        if (b->kind == K_PIG) {
            b->blink -= dt;
            if (b->blink < -0.14f) b->blink = 2.0f + (float)((b->seed >> 3) % 300u) / 100.0f + frand(0, 1);
            b->look = pa_approach(b->look, pa_clampf((tx - b->p.x) * 0.3f, -1, 1), 6.0f, dt);
            if (b->squash > 0) b->squash -= dt * 3.0f;
        }
        if (b->kind == K_BIRD && b->impact > 8.0f) b->squash = 1.0f;
        if (b->kind == K_BIRD && b->squash > 0) b->squash -= dt * 5.0f;
    }
}

static int world_settled(void) {
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (!b->used || b->im == 0.0f || !b->awake) continue;
        if (vdot(b->v, b->v) > 0.25f * 0.25f || fabsf(b->w) > 0.4f) return 0;
    }
    return 1;
}

static void next_bird_or_end(void) {
    deck_pick_next();
    if (S.sel >= 0) { S.phase = PH_AIM; S.pt = 0; S.load_t = 0; S.bot_stage = 0; S.bot_t = 0; pa_tone(500, 760, 0.08f, 1, 0.05f); }
    else { S.phase = PH_FAIL; S.pt = 0; S.res_t = 0; S.last_result = 2; pa_sfx("lose"); pa_tone(300, 180, 0.5f, 2, 0.06f); }
}

static void finish_win(void) {
    S.phase = PH_WIN; S.pt = 0; S.res_t = 0; S.last_result = 1;
    S.stars = S.score >= S.t3 ? 3 : (S.score >= S.t2 ? 2 : 1);
    int prev = S.progress_best[S.level];
    S.newbest = S.score > prev;
    progress_save(S.level, S.stars, S.score);
    S.best = S.progress_best[S.level];
    S.shown = 0;
    pa_sfx("win");
}

static void play_input(const PA_Input *in) {
    float u = ui_u();
    float px, py, pr;
    pause_btn(&px, &py, &pr);
    if (S.phase != PH_WIN && S.phase != PH_FAIL && in->pressed && in_circle(px, py, pr + 10 * u, in->x, in->y)) {
        pa_hub_pause();
        return;
    }
    if (S.phase == PH_WIN || S.phase == PH_FAIL) {
        if (S.res_t < 0.6f || !in->tapped) return;
        Rc m, r, n;
        result_buttons(&m, &r, &n);
        if (in_rc(m, in->x, in->y)) { pa_sfx("select"); enter_select(); }
        else if (in_rc(r, in->x, in->y)) { pa_sfx("select"); start_level(S.level); }
        else if (S.phase == PH_WIN && in_rc(n, in->x, in->y)) {
            pa_sfx("select");
            if (S.level + 1 < NLEVELS) start_level(S.level + 1); else enter_select();
        }
        return;
    }
    if (S.phase == PH_INTRO && in->pressed) { S.pt = 10.0f; return; }
    if (S.phase == PH_AIM) {
        Rc cr[MAXDECK]; int ci[MAXDECK];
        int nc = card_layout(cr, ci);
        if (in->pressed && !S.dragging) {
            for (int k = 0; k < nc; k++) if (in_rc(cr[k], in->x, in->y)) {
                if (S.sel != ci[k]) { S.sel = ci[k]; S.load_t = 0; pa_tone(700, 900, 0.06f, 1, 0.06f); }
                return;
            }
            if (in->y > 70 * u || in->x < (float)S.W * 0.6f) {
                S.dragging = 1; S.press = v2(in->x, in->y); S.pull_step = 0;
            }
        }
        if (S.dragging && in->down) {
            float full = 150.0f * u;
            V2 d = v2(in->x - S.press.x, -(in->y - S.press.y));
            V2 pull = vmul(d, MAXPULL / full);
            float l = vlen(pull);
            if (l > MAXPULL) pull = vmul(pull, MAXPULL / l);
            float minx = LEDGE_TOP + 0.35f - ANCHOR_Y;
            if (pull.y < minx) pull.y = minx;
            S.pull = pull;
            float step = floorf(vlen(pull) / 0.25f);
            if (step > S.pull_step) { pa_tone(180 + step * 40, 220 + step * 40, 0.05f, 1, 0.04f); }
            S.pull_step = step;
        }
        if (S.dragging && in->released) {
            S.dragging = 0;
            if (vlen(S.pull) > 0.28f && S.load_t >= 1.0f) { launch(); S.hint_t = 0; }
            else { S.pull = v2(0, 0); }
        }
        return;
    }
    if (S.phase == PH_FLY && in->pressed) {
        if (S.bird >= 0 && g_b[S.bird].used && !g_b[S.bird].ability_spent && g_b[S.bird].age > 0.08f) ability();
    }
}

static void play_update(float dt, const PA_Input *in) {
    S.pt += dt;
    fx_update(dt);
    if (!bot_on()) play_input(in);
    else {
        /* Captures still honour taps on the result buttons (scripted). */
        if ((S.phase == PH_WIN || S.phase == PH_FAIL)) play_input(in);
        bot_update(dt);
    }
    if (S.screen != SC_PLAY) return;

    switch (S.phase) {
    case PH_INTRO:
        if (S.pt > 2.0f) next_bird_or_end();
        break;
    case PH_AIM:
        S.load_t = pa_clamp01(S.load_t + dt / 0.35f);
        if (S.load_t >= 1.0f && S.load_t - dt / 0.35f < 1.0f) pa_tone(420, 640, 0.06f, 1, 0.05f);
        break;
    case PH_FLY: {
        int live = birds_update(dt);
        if (!live && S.pt > 0.2f) { S.phase = PH_SETTLE; S.pt = 0; }
        break;
    }
    case PH_SETTLE:
        if ((S.pt > 0.9f && world_settled()) || S.pt > 4.5f) {
            if (S.pigs_alive > 0) next_bird_or_end();
        }
        break;
    case PH_CLEAR:
        if (S.pt > 1.5f) { S.phase = PH_WIPE; S.pt = 0; S.wipe = 0; S.wipe_loaded = 0; pa_noise(0.5f, 0.05f); }
        break;
    case PH_WIPE:
        S.wipe = S.pt / 1.0f;
        if (S.wipe >= 0.5f && !S.wipe_loaded) {
            S.wipe_loaded = 1;
            load_room(S.room + 1);
            S.phase = PH_WIPE; S.pt = 0.5f;
            S.cx = S.lx1 - (float)S.W * 0.5f / S.z + 0.5f;
        }
        if (S.wipe >= 1.0f) { S.phase = PH_INTRO; S.pt = 0.6f; deck_pick_next(); }
        break;
    case PH_BONUS:
        S.bonus_t += dt;
        if (S.bonus_t > 0.45f) {
            S.bonus_t = 0;
            int found = -1;
            for (int i = 0; i < S.ndeck; i++) if (!S.used[i]) { found = i; break; }
            if (found < 0) { finish_win(); break; }
            Rc cr[MAXDECK]; int ci[MAXDECK];
            int nc = card_layout(cr, ci);
            for (int k = 0; k < nc; k++) if (ci[k] == found) {
                for (int f = 0; f < MAXF; f++) if (S.fl[f].t <= 0) {
                    S.fl[f].p = v2(cr[k].x + cr[k].w * 0.5f, cr[k].y - 10);
                    S.fl[f].t = 1.4f; S.fl[f].value = 10000; S.fl[f].col = PA_RGB(255, 220, 80);
                    S.fl[f].size = 1.0f; S.fl[f].screen = 1;
                    break;
                }
            }
            S.used[found] = 1;
            S.score += 10000;
            pa_tone(880, 1320, 0.12f, 1, 0.09f);
        }
        break;
    case PH_WIN: case PH_FAIL:
        S.res_t += dt;
        break;
    default: break;
    }

    /* Physics keeps running behind every phase except the room wipe. */
    if (S.phase != PH_WIPE) {
        phys_step(dt);
        post_step();
    }

    /* All pigs gone: let the rubble fall a moment, then clear. */
    if (S.pigs_alive <= 0 && (S.phase == PH_FLY || S.phase == PH_SETTLE || S.phase == PH_AIM)) {
        S.clear_t += dt;
        if (S.clear_t > 1.6f) {
            for (int k = 0; k < 4; k++) {
                int id = k == 0 ? S.bird : (k - 1 < S.nflock ? S.flock[k - 1] : -1);
                if (id >= 0 && g_b[id].used && g_b[id].kind == K_BIRD) bird_poof(id);
            }
            S.bird = -1; S.nflock = 0;
            if (S.room + 1 < S.nrooms) {
                S.phase = PH_CLEAR; S.pt = 0; S.banner = "ROOM CLEARED!"; S.banner_t = 1.6f;
                pa_tone(660, 990, 0.15f, 1, 0.09f); pa_tone(990, 1480, 0.2f, 1, 0.07f);
            } else {
                S.phase = PH_BONUS; S.pt = 0; S.bonus_t = 0.2f;
                S.banner = "LEVEL CLEARED!"; S.banner_t = 1.4f;
            }
        }
    }
    cam_update(dt);
}

static void s_update(float dt, const PA_Input *in) {
    S.time += dt;
    S.scr_t += dt;
    if (S.screen == SC_TITLE) {
        fx_update(dt);
        phys_step(dt);
        Rc pb = play_button();
        if ((in->tapped && in_rc(pb, in->x, in->y)) || in->key_pressed[PA_KEY_ENTER] || in->key_pressed[PA_KEY_SPACE]) {
            pa_sfx("select"); pa_tone(520, 1040, 0.14f, 1, 0.08f);
            enter_select();
        }
        return;
    }
    if (S.screen == SC_SELECT) {
        float u = ui_u();
        if (in->tapped) {
            if (in_circle(42 * u, 40 * u, 34 * u, in->x, in->y)) { pa_sfx("select"); enter_title(); return; }
            for (int L = 0; L < NLEVELS; L++) {
                Rc r; select_layout(L, &r);
                if (in_rc(r, in->x, in->y)) {
                    if (L <= S.unlocked) { pa_sfx("select"); start_level(L); }
                    else pa_tone(200, 150, 0.12f, 2, 0.05f);
                    return;
                }
            }
        }
        if (in->key_pressed[PA_KEY_ENTER]) start_level(S.unlocked);
        return;
    }
    play_update(dt, in);
}

static void s_start(void) {
    pa_set_landscape(1);
    memset(&S, 0, sizeof S);
    S.W = 1170; S.H = 540;
    pa_rng_seed(&g_fx_rng, 99u);
    progress_load();
    int d = demo();
    if (d >= 100 && d < 300) start_level((d % 100) - 1);
    else if (d == 300) { enter_title(); enter_select(); }
    else enter_title();
}

static void s_stop(void) { phys_reset(); }

/* ============================================================== RENDER == */
static float g_gy, g_shx, g_shy;       /* ground line on screen, shake offset */
static float SX(float wx) { return (float)S.W * 0.5f + (wx - S.cx) * S.z + g_shx; }
static float SY(float wy) { return g_gy - wy * S.z + g_shy; }

typedef struct { float cx, cy, c, s, k; } Xf;
static Xf xf_make(float cx, float cy, float ang, float k) { Xf t; t.cx = cx; t.cy = cy; t.c = cosf(ang); t.s = sinf(ang); t.k = k; return t; }
static V2 xp(const Xf *t, float lx, float ly) {
    return v2(t->cx + (t->c * lx - t->s * ly) * t->k, t->cy - (t->s * lx + t->c * ly) * t->k);
}
static void xpoly(PA_Canvas *c, const Xf *t, const float *xy, int n, PA_Color col) {
    V2 pts[48];
    if (n > 48) n = 48;
    for (int i = 0; i < n; i++) pts[i] = xp(t, xy[i * 2], xy[i * 2 + 1]);
    pa_fill_poly(c, pts, n, col);
}
static void xell(PA_Canvas *c, const Xf *t, float lx, float ly, float rx, float ry, float rot, PA_Color col) {
    V2 pts[40];
    int n = (int)((rx > ry ? rx : ry) * t->k * 0.6f) + 10;
    if (n > 40) n = 40;
    float cr = cosf(rot), sr = sinf(rot);
    for (int i = 0; i < n; i++) {
        float a = PA_TAU * (float)i / (float)n;
        float ex = cosf(a) * rx, ey = sinf(a) * ry;
        pts[i] = xp(t, lx + cr * ex - sr * ey, ly + sr * ex + cr * ey);
    }
    pa_fill_poly(c, pts, n, col);
}
static void xcirc(PA_Canvas *c, const Xf *t, float lx, float ly, float r, PA_Color col) {
    V2 p = xp(t, lx, ly);
    pa_fill_circle(c, p.x, p.y, r * t->k, col);
}
static void xline(PA_Canvas *c, const Xf *t, float x0, float y0, float x1, float y1, float w, PA_Color col) {
    V2 a = xp(t, x0, y0), b = xp(t, x1, y1);
    pa_line(c, a.x, a.y, b.x, b.y, w, col);
}

static int g_clip_stack[8][4], g_clip_n;
static void clip_push(PA_Canvas *c, float x0, float y0, float x1, float y1) {
    if (g_clip_n < 8) {
        g_clip_stack[g_clip_n][0] = c->clip_x0; g_clip_stack[g_clip_n][1] = c->clip_y0;
        g_clip_stack[g_clip_n][2] = c->clip_x1; g_clip_stack[g_clip_n][3] = c->clip_y1;
        g_clip_n++;
    }
    int ix0 = (int)floorf(x0), iy0 = (int)floorf(y0), ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
    if (ix0 < c->clip_x0) ix0 = c->clip_x0;
    if (iy0 < c->clip_y0) iy0 = c->clip_y0;
    if (ix1 > c->clip_x1) ix1 = c->clip_x1;
    if (iy1 > c->clip_y1) iy1 = c->clip_y1;
    if (ix1 < ix0) ix1 = ix0;
    if (iy1 < iy0) iy1 = iy0;
    c->clip_x0 = ix0; c->clip_y0 = iy0; c->clip_x1 = ix1; c->clip_y1 = iy1;
}
static void clip_pop(PA_Canvas *c) {
    if (!g_clip_n) return;
    g_clip_n--;
    c->clip_x0 = g_clip_stack[g_clip_n][0]; c->clip_y0 = g_clip_stack[g_clip_n][1];
    c->clip_x1 = g_clip_stack[g_clip_n][2]; c->clip_y1 = g_clip_stack[g_clip_n][3];
}

static void fmt_score(char *buf, size_t n, int v) {
    char tmp[24];
    snprintf(tmp, sizeof tmp, "%d", v < 0 ? 0 : v);
    int len = (int)strlen(tmp), o = 0;
    for (int i = 0; i < len && o < (int)n - 1; i++) {
        buf[o++] = tmp[i];
        int rem = len - 1 - i;
        if (rem > 0 && rem % 3 == 0 && o < (int)n - 1) buf[o++] = ',';
    }
    buf[o] = 0;
}

static float hash1(float x) { float s = sinf(x * 12.9898f) * 43758.5453f; return s - floorf(s); }

/* --------------------------------------------------------------- themes -- */
typedef struct {
    uint32_t sky0, sky1, sky2, sun;
    uint32_t far0, far1, mid0, mid1, near0, near1;
    uint32_t grass0, grass1, dirt0, dirt1;
    uint32_t rock0, rock1, rockline;
} Theme;
static const Theme THEMES[4] = {
    { 0x2B93E0, 0x71C9F2, 0xD4F3FA, 0xFFF7D0,  0x8DC6D8, 0xC0E6EE, 0x4FA58C, 0x2E8466, 0x2F7E3C, 0x1F5C2B,
      0x8FD64A, 0x55A82B, 0x8B5B34, 0x5A3820,  0x9A8A7A, 0x6A5C50, 0x463A32 },
    { 0x4E3F96, 0xEE7F5A, 0xFFD69A, 0xFFF0B8,  0xD98468, 0xF1AE88, 0xB4583C, 0x8E4030, 0x7E3A2A, 0x5A2820,
      0xE8BC52, 0xBF8A2C, 0xB36838, 0x763A20,  0xD08858, 0x9E5A36, 0x5E3018 },
    { 0x2E3478, 0x8780C4, 0xEFC6DE, 0xFFF6EA,  0x8F8DC8, 0xB8B4E2, 0x6A70B2, 0x4E5394, 0xDDE6FF, 0xA9B7E8,
      0xF6F9FF, 0xC6D2F0, 0x75739F, 0x46446E,  0x8E92BA, 0x5E628A, 0x3A3D62 },
    { 0x2E0A10, 0x8A1A20, 0xE65A2A, 0xFFC050,  0x5E1A20, 0x84282A, 0x461416, 0x30100F, 0x2A0E10, 0x1A0809,
      0x5E4C48, 0x3A2E2E, 0x2E2222, 0x160F0F,  0x6E5652, 0x4A3634, 0x241818 },
};

static void draw_cloud(PA_Canvas *c, float x, float y, float s, PA_Color col) {
    pa_fill_ellipse(c, x, y, 60 * s, 18 * s, col);
    pa_fill_circle(c, x - 30 * s, y - 8 * s, 22 * s, col);
    pa_fill_circle(c, x + 2 * s, y - 20 * s, 30 * s, col);
    pa_fill_circle(c, x + 34 * s, y - 6 * s, 20 * s, col);
}

static void draw_background(PA_Canvas *c, int theme, float camx, float zoom, float time) {
    const Theme *T = &THEMES[theme];
    float W = (float)c->w, H = (float)c->h, hy = g_gy;
    float u = H / 540.0f;
    if (W < H) u = W / 760.0f;
    PA_Paint sky = pa_linear(0, 0, 0, hy);
    pa_stop(&sky, 0.0f, pa_hex(T->sky0));
    pa_stop(&sky, 0.55f, pa_hex(T->sky1));
    pa_stop(&sky, 1.0f, pa_hex(T->sky2));
    pa_fill_rect_paint(c, 0, 0, W, hy + 2, &sky);

    float base = camx * zoom;       /* world scroll in px */
    /* Sun / moon / lava glow. */
    float sunx = W * (theme == TH_CANYON ? 0.58f : 0.80f) - base * 0.02f;
    float suny = theme == TH_CANYON ? hy - 150 * u : (theme == TH_VOLCANO ? hy - 40 * u : 92 * u);
    float sr = (theme == TH_FROST ? 92 : theme == TH_CANYON ? 120 : 70) * u;
    PA_Paint glow = pa_radial(sunx, suny, sr * 0.4f, sr * 3.6f);
    pa_stop(&glow, 0.0f, pa_alpha(pa_hex(T->sun), 0.85f));
    pa_stop(&glow, 0.35f, pa_alpha(pa_hex(T->sun), 0.28f));
    pa_stop(&glow, 1.0f, pa_alpha(pa_hex(T->sun), 0.0f));
    pa_fill_rect_paint(c, sunx - sr * 3.6f, suny - sr * 3.6f, sr * 7.2f, sr * 7.2f, &glow);
    if (theme == TH_JUNGLE) {
        for (int k = 0; k < 7; k++) {
            float a = 1.75f + (float)k * 0.23f + sinf(time * 0.2f + (float)k) * 0.03f;
            float len = 900 * u, wdt = 0.05f + 0.02f * (float)(k % 3);
            V2 tri[3] = { { sunx, suny }, { sunx + cosf(a - wdt) * len, suny + sinf(a - wdt) * len },
                          { sunx + cosf(a + wdt) * len, suny + sinf(a + wdt) * len } };
            pa_fill_poly(c, tri, 3, PA_RGBA(255, 255, 235, 22));
        }
        pa_fill_circle(c, sunx, suny, sr * 0.55f, PA_RGBA(255, 255, 240, 235));
    } else if (theme == TH_CANYON) {
        pa_fill_circle(c, sunx, suny, sr * 0.75f, PA_RGBA(255, 236, 170, 240));
    } else if (theme == TH_FROST) {
        pa_fill_circle(c, sunx, suny, sr, PA_RGBA(250, 238, 240, 225));
        pa_fill_circle(c, sunx - sr * 0.3f, suny - sr * 0.2f, sr * 0.22f, PA_RGBA(225, 210, 225, 120));
        pa_fill_circle(c, sunx + sr * 0.35f, suny + sr * 0.25f, sr * 0.15f, PA_RGBA(225, 210, 225, 110));
    }

    /* Clouds. */
    if (theme != TH_VOLCANO) {
        PA_Color cc = theme == TH_CANYON ? PA_RGBA(255, 214, 190, 150) : theme == TH_FROST ? PA_RGBA(200, 190, 230, 120) : PA_RGBA(255, 255, 255, 170);
        for (int k = 0; k < 6; k++) {
            float span = W + 400 * u;
            float x = pa_wrapf((float)k * 260 * u - base * 0.06f + time * 6.0f * u, span) - 200 * u;
            float y = (60 + (float)((k * 37) % 5) * 26) * u;
            draw_cloud(c, x, y, (0.8f + 0.3f * (float)(k % 3)) * u, cc);
        }
    }

    /* Far layer. */
    {
        float off = base * 0.12f;
        float step = 210 * u;
        int i0 = (int)floorf(off / step) - 2;
        for (int i = i0; i < i0 + (int)(W / step) + 5; i++) {
            float fx = (float)i * step - off + hash1((float)i) * 80 * u;
            float h = (140 + hash1((float)i + 3.1f) * 170) * u;
            float w = (55 + hash1((float)i + 7.7f) * 70) * u;
            PA_Color c0 = pa_hex(T->far0), c1 = pa_hex(T->far1);
            if (theme == TH_JUNGLE) {
                /* Tall rock spires with a green cap, hazy with distance. */
                V2 sp[6] = { { fx - w * 0.5f, hy }, { fx - w * 0.42f, hy - h * 0.7f }, { fx - w * 0.3f, hy - h },
                             { fx + w * 0.3f, hy - h * 1.02f }, { fx + w * 0.45f, hy - h * 0.6f }, { fx + w * 0.55f, hy } };
                PA_Paint p = pa_linear(0, hy - h, 0, hy);
                pa_stop(&p, 0, c0); pa_stop(&p, 1, c1);
                pa_fill_poly_paint(c, sp, 6, &p);
                pa_fill_ellipse(c, fx, hy - h, w * 0.42f, 14 * u, pa_mix(c0, pa_hex(0x6FB79A), 0.6f));
            } else if (theme == TH_CANYON) {
                V2 sp[6] = { { fx - w * 0.9f, hy }, { fx - w * 0.6f, hy - h * 0.55f }, { fx - w * 0.55f, hy - h * 0.62f },
                             { fx + w * 0.6f, hy - h * 0.62f }, { fx + w * 0.7f, hy - h * 0.5f }, { fx + w, hy } };
                PA_Paint p = pa_linear(0, hy - h * 0.62f, 0, hy);
                pa_stop(&p, 0, c1); pa_stop(&p, 1, c0);
                pa_fill_poly_paint(c, sp, 6, &p);
                pa_line(c, fx - w * 0.6f, hy - h * 0.45f, fx + w * 0.68f, hy - h * 0.45f, 3 * u, pa_alpha(c0, 0.5f));
            } else if (theme == TH_FROST) {
                V2 sp[3] = { { fx - w * 1.1f, hy }, { fx, hy - h * 0.9f }, { fx + w * 1.1f, hy } };
                pa_fill_poly(c, sp, 3, c0);
                V2 cap[5] = { { fx - w * 0.3f, hy - h * 0.66f }, { fx, hy - h * 0.9f }, { fx + w * 0.3f, hy - h * 0.66f },
                              { fx + w * 0.1f, hy - h * 0.7f }, { fx - w * 0.1f, hy - h * 0.64f } };
                pa_fill_poly(c, cap, 5, c1);
            } else {
                V2 sp[5] = { { fx - w, hy }, { fx - w * 0.2f, hy - h * 0.8f }, { fx, hy - h * 0.9f }, { fx + w * 0.3f, hy - h * 0.7f }, { fx + w, hy } };
                pa_fill_poly(c, sp, 5, c0);
                pa_line(c, fx, hy - h * 0.88f, fx + w * 0.3f, hy - h * 0.3f, 4 * u, PA_RGBA(255, 120, 40, 160));
            }
        }
        if (theme == TH_VOLCANO) {
            float vx = W * 0.62f - base * 0.08f;
            V2 vol[6] = { { vx - 380 * u, hy }, { vx - 70 * u, hy - 280 * u }, { vx - 40 * u, hy - 270 * u },
                          { vx + 40 * u, hy - 272 * u }, { vx + 75 * u, hy - 282 * u }, { vx + 400 * u, hy } };
            pa_fill_poly(c, vol, 6, pa_hex(0x3E1216));
            PA_Paint lg = pa_radial(vx, hy - 276 * u, 10 * u, 140 * u);
            pa_stop(&lg, 0, PA_RGBA(255, 200, 80, 230)); pa_stop(&lg, 0.4f, PA_RGBA(255, 90, 30, 90)); pa_stop(&lg, 1, PA_RGBA(255, 60, 20, 0));
            pa_fill_rect_paint(c, vx - 140 * u, hy - 420 * u, 280 * u, 280 * u, &lg);
            for (int k = 0; k < 3; k++) {
                float sx = vx - 20 * u + (float)k * 30 * u;
                V2 st[4] = { { sx, hy - 274 * u }, { sx + 10 * u, hy - 274 * u }, { sx + 30 * u + (float)k * 20 * u, hy - 120 * u }, { sx + 16 * u + (float)k * 20 * u, hy - 120 * u } };
                pa_fill_poly(c, st, 4, PA_RGBA(255, 140, 40, 200));
            }
            for (int k = 0; k < 7; k++) {
                float t = pa_wrapf(time * 0.08f + (float)k / 7.0f, 1.0f);
                pa_fill_circle(c, vx + sinf((float)k * 2.1f) * 30 * u + t * 80 * u, hy - 300 * u - t * 220 * u, (30 + t * 60) * u, PA_RGBA(40, 20, 24, (int)(150 * (1 - t))));
            }
        }
    }

    /* Haze band over the far layer. */
    PA_Paint hz = pa_linear(0, hy - 200 * u, 0, hy);
    pa_stop(&hz, 0, pa_alpha(pa_hex(T->sky2), 0.0f));
    pa_stop(&hz, 1, pa_alpha(pa_hex(T->sky2), 0.55f));
    pa_fill_rect_paint(c, 0, hy - 200 * u, W, 200 * u, &hz);

    /* Mid hills. */
    {
        float off = base * 0.33f;
        V2 pts[70];
        int n = 0;
        float seg = W / 60.0f;
        for (int i = 0; i <= 60; i++) {
            float x = (float)i * seg;
            float wx = (x + off) / u;
            float y = hy - (70 + 26 * sinf(wx * 0.006f) + 18 * sinf(wx * 0.017f + 1.3f)) * u;
            pts[n++] = v2(x, y);
        }
        pts[n++] = v2(W, hy + 2); pts[n++] = v2(0, hy + 2);
        PA_Paint p = pa_linear(0, hy - 120 * u, 0, hy);
        pa_stop(&p, 0, pa_hex(T->mid0)); pa_stop(&p, 1, pa_hex(T->mid1));
        pa_fill_poly_paint(c, pts, n, &p);
        /* Trees on the hills. */
        float step = 90 * u;
        int i0 = (int)floorf(off / step) - 1;
        for (int i = i0; i < i0 + (int)(W / step) + 3; i++) {
            if (hash1((float)i * 1.7f) < 0.35f) continue;
            float x = (float)i * step - off + hash1((float)i + 0.5f) * 40 * u;
            float wx = (x + off) / u;
            float y = hy - (70 + 26 * sinf(wx * 0.006f) + 18 * sinf(wx * 0.017f + 1.3f)) * u + 6 * u;
            float s = (0.7f + hash1((float)i + 9.0f) * 0.6f) * u;
            PA_Color tc = pa_hex(T->mid1), tl = pa_shade(pa_hex(T->mid0), 0.08f);
            if (theme == TH_JUNGLE) {
                pa_line(c, x, y, x + 4 * s, y - 60 * s, 5 * s, pa_shade(tc, -0.2f));
                pa_fill_circle(c, x + 4 * s, y - 70 * s, 24 * s, tc);
                pa_fill_circle(c, x - 14 * s, y - 60 * s, 18 * s, tc);
                pa_fill_circle(c, x + 22 * s, y - 58 * s, 18 * s, tc);
                pa_fill_circle(c, x, y - 78 * s, 14 * s, tl);
            } else if (theme == TH_FROST) {
                for (int k = 0; k < 3; k++) {
                    float ty = y - (float)k * 22 * s;
                    V2 tr[3] = { { x - (26 - k * 6) * s, ty }, { x, ty - 34 * s }, { x + (26 - k * 6) * s, ty } };
                    pa_fill_poly(c, tr, 3, pa_hex(0x3C4C7C));
                    V2 sn[3] = { { x - (12 - k * 3) * s, ty - 18 * s }, { x, ty - 34 * s }, { x + (12 - k * 3) * s, ty - 18 * s } };
                    pa_fill_poly(c, sn, 3, PA_RGBA(240, 245, 255, 230));
                }
            } else if (theme == TH_CANYON) {
                pa_round_rect(c, x - 6 * s, y - 52 * s, 12 * s, 52 * s, 6 * s, pa_hex(0x6E7A3A));
                pa_round_rect(c, x - 22 * s, y - 38 * s, 10 * s, 22 * s, 5 * s, pa_hex(0x6E7A3A));
                pa_line(c, x - 17 * s, y - 20 * s, x - 4 * s, y - 20 * s, 8 * s, pa_hex(0x6E7A3A));
            } else {
                pa_line(c, x, y, x + 6 * s, y - 50 * s, 4 * s, pa_hex(0x1A0A0A));
                pa_line(c, x + 3 * s, y - 28 * s, x + 18 * s, y - 40 * s, 3 * s, pa_hex(0x1A0A0A));
                pa_line(c, x + 5 * s, y - 40 * s, x - 10 * s, y - 52 * s, 3 * s, pa_hex(0x1A0A0A));
            }
        }
    }
    /* Near bushes along the horizon. */
    {
        float off = base * 0.62f;
        float step = 46 * u;
        int i0 = (int)floorf(off / step) - 2;
        PA_Color b0 = pa_hex(T->near0), b1 = pa_hex(T->near1);
        for (int i = i0; i < i0 + (int)(W / step) + 4; i++) {
            float x = (float)i * step - off;
            float r = (22 + hash1((float)i * 3.3f) * 22) * u;
            pa_fill_circle(c, x, hy - r * 0.45f, r, b1);
            pa_fill_circle(c, x - r * 0.2f, hy - r * 0.6f, r * 0.75f, b0);
            if (theme == TH_JUNGLE && hash1((float)i + 0.2f) > 0.6f) {
                pa_fill_circle(c, x + r * 0.2f, hy - r * 1.05f, 4 * u, PA_RGB(255, 120, 150));
                pa_fill_circle(c, x - r * 0.4f, hy - r * 0.9f, 3.5f * u, PA_RGB(255, 236, 120));
            }
        }
    }
}

/* Ground strip, world locked. */
static void draw_ground(PA_Canvas *c, int theme) {
    const Theme *T = &THEMES[theme];
    float W = (float)c->w, H = (float)c->h;
    float y0 = SY(0);
    float band = 0.30f * S.z;
    PA_Paint d = pa_linear(0, y0, 0, H);
    pa_stop(&d, 0, pa_hex(T->dirt0)); pa_stop(&d, 1, pa_hex(T->dirt1));
    pa_fill_rect_paint(c, 0, y0 + band * 0.6f, W, H - y0, &d);
    /* Pebbles locked to world x. */
    float wx0 = S.cx - W * 0.5f / S.z - 1, wx1 = S.cx + W * 0.5f / S.z + 1;
    for (float wx = floorf(wx0 / 0.7f) * 0.7f; wx < wx1; wx += 0.7f) {
        float h = hash1(wx * 3.7f);
        float yy = y0 + band + (0.25f + h * 1.4f) * S.z * 0.6f;
        if (yy > H) continue;
        pa_fill_ellipse(c, SX(wx + h * 0.4f), yy, (0.10f + h * 0.12f) * S.z, (0.05f + h * 0.05f) * S.z, pa_alpha(pa_shade(pa_hex(T->dirt1), -0.25f), 0.6f));
        if (theme == TH_VOLCANO && h > 0.6f)
            pa_line(c, SX(wx), yy - 6, SX(wx + 0.6f), yy + 4, 2.5f, PA_RGBA(255, 120, 30, 190));
    }
    PA_Paint g = pa_linear(0, y0 - band * 0.2f, 0, y0 + band);
    pa_stop(&g, 0, pa_hex(T->grass0)); pa_stop(&g, 1, pa_hex(T->grass1));
    pa_fill_rect_paint(c, 0, y0, W, band, &g);
    for (float wx = floorf(wx0 / 0.32f) * 0.32f; wx < wx1; wx += 0.32f) {
        float h = hash1(wx * 1.3f + 4.0f);
        pa_fill_circle(c, SX(wx), y0 + band * 0.95f, (0.10f + h * 0.06f) * S.z, pa_hex(T->grass1));
        pa_fill_circle(c, SX(wx + 0.12f), y0 + 0.02f * S.z, (0.08f + h * 0.05f) * S.z, pa_hex(T->grass0));
    }
    pa_fill_rect(c, 0, y0 - 1, W, 2, pa_alpha(pa_shade(pa_hex(T->grass0), 0.3f), 0.7f));
}

/* Rock platform / cliff / floating island. */
static void draw_rock(PA_Canvas *c, const Body *b, int theme) {
    const Theme *T = &THEMES[theme];
    float x0 = SX(b->p.x - b->hw), x1 = SX(b->p.x + b->hw);
    float yt = SY(b->p.y + b->hh), yb = SY(b->p.y - b->hh);
    float z = S.z;
    if (x1 < -50 || x0 > (float)c->w + 50) return;
    uint32_t sd = b->seed;
    PA_Color r0 = pa_hex(T->rock0), r1 = pa_hex(T->rock1), rl = pa_hex(T->rockline);
    if (b->var == 1) {
        /* Island: slab plus a jagged underside that hangs below it. */
        V2 pts[12]; int n = 0;
        pts[n++] = v2(x0, yt);
        pts[n++] = v2(x1, yt);
        pts[n++] = v2(x1 + 0.1f * z, yt + 0.4f * z);
        pts[n++] = v2(x1 - 0.2f * z, yb + 0.2f * z);
        float mid = (x0 + x1) * 0.5f;
        pts[n++] = v2(mid + (x1 - x0) * 0.2f, yb + 0.9f * z);
        pts[n++] = v2(mid + (x1 - x0) * 0.05f, yb + 1.6f * z);
        pts[n++] = v2(mid - (x1 - x0) * 0.1f, yb + 1.1f * z);
        pts[n++] = v2(mid - (x1 - x0) * 0.3f, yb + 0.6f * z);
        pts[n++] = v2(x0 + 0.15f * z, yb + 0.15f * z);
        pts[n++] = v2(x0 - 0.1f * z, yt + 0.4f * z);
        V2 o[12];
        for (int i = 0; i < n; i++) o[i] = pts[i];
        PA_Paint p = pa_linear(0, yt, 0, yb + 1.6f * z);
        pa_stop(&p, 0, r0); pa_stop(&p, 1, pa_shade(r1, -0.25f));
        for (int i = 0; i < n; i++) { o[i].x += (o[i].x < mid ? -2 : 2); o[i].y += 2; }
        pa_fill_poly(c, o, n, rl);
        pa_fill_poly_paint(c, pts, n, &p);
        if (theme == TH_JUNGLE) {
            for (int k = 0; k < 4; k++) {
                float vx = x0 + (x1 - x0) * (0.15f + 0.22f * (float)k);
                float len = (0.5f + hash1((float)(sd % 97) + (float)k) * 0.9f) * z;
                pa_line(c, vx, yb, vx + 3, yb + len, 2.5f, pa_hex(0x3E7A2E));
                pa_fill_circle(c, vx + 3, yb + len, 3.5f, pa_hex(0x5DA83A));
            }
        }
    } else {
        PA_Paint p = pa_linear(0, yt, 0, yb);
        pa_stop(&p, 0, r0); pa_stop(&p, 1, r1);
        pa_round_rect(c, x0 - 2, yt - 2, x1 - x0 + 4, yb - yt + 4, 0.18f * z, rl);
        pa_round_rect_paint(c, x0, yt, x1 - x0, yb - yt, 0.16f * z, &p);
        /* Strata and boulders. */
        clip_push(c, x0, yt, x1, yb);
        for (int k = 1; k < 6; k++) {
            float yy = yt + (float)k * 0.55f * z + 0.2f * z;
            if (yy > yb) break;
            float jit = hash1((float)(sd % 211) + (float)k) * 0.3f * z;
            pa_line(c, x0 + jit, yy, x1 - jit, yy + 0.06f * z, 2.0f, pa_alpha(rl, 0.35f));
        }
        for (int k = 0; k < 5; k++) {
            float hx = hash1((float)(sd % 331) + (float)k * 2.3f), hy2 = hash1((float)(sd % 173) + (float)k * 5.1f);
            float bx = x0 + (x1 - x0) * hx, by = yt + 0.35f * z + (yb - yt) * hy2;
            pa_fill_ellipse(c, bx, by, 0.28f * z, 0.18f * z, pa_alpha(pa_shade(r0, 0.12f), 0.55f));
            pa_fill_ellipse(c, bx + 0.03f * z, by + 0.05f * z, 0.22f * z, 0.10f * z, pa_alpha(r1, 0.5f));
        }
        clip_pop(c);
    }
    /* Cap: grass / sand / snow / ash with drips. */
    PA_Color g0 = pa_hex(T->grass0), g1 = pa_hex(T->grass1);
    float capH = 0.2f * z;
    pa_round_rect(c, x0 - 0.06f * z, yt - 0.06f * z, x1 - x0 + 0.12f * z, capH, capH * 0.5f, g1);
    pa_round_rect(c, x0 - 0.06f * z, yt - 0.08f * z, x1 - x0 + 0.12f * z, capH * 0.75f, capH * 0.4f, g0);
    int nd = (int)((x1 - x0) / (0.35f * z));
    for (int k = 0; k < nd; k++) {
        float dx = x0 + (x1 - x0) * ((float)k + 0.5f) / (float)nd;
        float dl = (0.06f + hash1((float)(sd % 59) + (float)k) * 0.14f) * z;
        pa_fill_circle(c, dx, yt + capH * 0.7f + dl * 0.4f, dl * 0.55f, g1);
    }
    if (theme == TH_JUNGLE || theme == TH_CANYON) {
        for (int k = 0; k < nd; k += 2) {
            float dx = x0 + (x1 - x0) * ((float)k + 0.3f) / (float)nd;
            V2 tuft[3] = { { dx - 3, yt - 0.04f * z }, { dx + 1, yt - 0.2f * z }, { dx + 4, yt - 0.04f * z } };
            pa_fill_poly(c, tuft, 3, g0);
        }
    }
}

/* ------------------------------------------------------------- blocks --- */
static void crack_path(PA_Canvas *c, const Xf *t, float hw, float hh, uint32_t seed, int which, PA_Color col, float w) {
    PA_Rng r; pa_rng_seed(&r, seed * 2654435761u + (uint32_t)which * 977u);
    float side = pa_rng_next(&r);
    V2 p;
    if (side < 0.25f) p = v2(-hw, pa_rng_range(&r, -hh, hh));
    else if (side < 0.5f) p = v2(hw, pa_rng_range(&r, -hh, hh));
    else if (side < 0.75f) p = v2(pa_rng_range(&r, -hw, hw), hh);
    else p = v2(pa_rng_range(&r, -hw, hw), -hh);
    V2 dir = vmul(p, -1.0f / (vlen(p) + 1e-4f));
    V2 pts[6]; pts[0] = xp(t, p.x, p.y);
    float stepl = (hw < hh ? hw : hh) * 0.55f + (hw > hh ? hw : hh) * 0.12f;
    for (int k = 1; k < 6; k++) {
        float a = pa_rng_range(&r, -0.8f, 0.8f);
        V2 d = vrot(dir, cosf(a), sinf(a));
        p = vadd(p, vmul(d, stepl));
        p.x = pa_clampf(p.x, -hw, hw); p.y = pa_clampf(p.y, -hh, hh);
        pts[k] = xp(t, p.x, p.y);
    }
    pa_stroke_poly(c, pts, 6, 0, w, col);
}

/* Draws one block. (cx, cy) on screen, hw/hh in px, world-style angle. */
static void draw_block_raw(PA_Canvas *c, int mat, int var, float cx, float cy, float hw, float hh, float ang,
                           float hpf, uint32_t seed, float flash) {
    Xf t = xf_make(cx, cy, ang, 1.0f);
    float ext = sqrtf(hw * hw + hh * hh) + 3;
    clip_push(c, cx - ext, cy - ext, cx + ext, cy + ext);
    PA_Color face = mat_col(mat, 0), dark = mat_col(mat, 1), light = mat_col(mat, 2);
    PA_Color outl = mat == M_WOOD ? pa_hex(0x6A3510) : mat == M_ICE ? pa_hex(0x3A7EAA) : pa_hex(0x3E4656);
    float o = 1.6f;
    float mn = hw < hh ? hw : hh;

    if (var == 2) {                         /* ball */
        V2 p = xp(&t, 0, 0);
        pa_fill_circle(c, p.x, p.y, hw + o, outl);
        PA_Paint rp = pa_radial(p.x - hw * 0.35f, p.y - hw * 0.4f, hw * 0.1f, hw * 1.2f);
        pa_stop(&rp, 0, light); pa_stop(&rp, 0.5f, face); pa_stop(&rp, 1, dark);
        pa_fill_ellipse_paint(c, p.x, p.y, hw, hw, &rp);
        if (mat == M_STONE) {
            for (int k = 0; k < 4; k++) {
                float a = (float)k * 1.7f + (float)(seed % 7);
                xell(c, &t, cosf(a) * hw * 0.5f, sinf(a) * hw * 0.5f, hw * 0.16f, hw * 0.11f, a, pa_alpha(dark, 0.45f));
            }
        } else {
            pa_stroke_circle(c, p.x, p.y, hw * 0.62f, 1.5f, pa_alpha(dark, 0.6f));
            pa_stroke_circle(c, p.x, p.y, hw * 0.3f, 1.5f, pa_alpha(dark, 0.6f));
        }
        if (hpf < 0.6f) crack_path(c, &t, hw * 0.7f, hw * 0.7f, seed, 0, pa_alpha(outl, 0.85f), 1.8f);
        if (flash > 0) pa_fill_circle(c, p.x, p.y, hw, PA_RGBA(255, 255, 255, (int)(flash * 120)));
        clip_pop(c);
        return;
    }

    float ohw = hw + o, ohh = hh + o;
    float outer[8] = { -ohw, -ohh, ohw, -ohh, ohw, ohh, -ohw, ohh };
    xpoly(c, &t, outer, 4, outl);

    V2 top = xp(&t, 0, hh), bot = xp(&t, 0, -hh);
    PA_Paint fp = pa_linear(top.x, top.y, bot.x, bot.y);
    if (mat == M_ICE) {
        pa_stop(&fp, 0, pa_alpha(light, 0.92f)); pa_stop(&fp, 0.5f, pa_alpha(face, 0.82f)); pa_stop(&fp, 1, pa_alpha(pa_shade(face, -0.12f), 0.88f));
    } else {
        pa_stop(&fp, 0, pa_shade(face, 0.12f)); pa_stop(&fp, 0.6f, face); pa_stop(&fp, 1, pa_shade(face, -0.12f));
    }

    if (var == 1) {
        /* Hollow frame: outer path plus reversed inner path leaves a hole. */
        float ih = hw * 0.56f, iv = hh * 0.56f;
        float path[20] = { -hw, -hh, hw, -hh, hw, hh, -hw, hh, -hw, -hh,
                           -ih, -iv, -ih, iv, ih, iv, ih, -iv, -ih, -iv };
        V2 pts[10];
        for (int i = 0; i < 10; i++) pts[i] = xp(&t, path[i * 2], path[i * 2 + 1]);
        pa_fill_poly_paint(c, pts, 10, &fp);
        /* Inner rim: shadowed top-left, lit bottom-right. */
        float b2 = 1.4f;
        xline(c, &t, -ih, iv, ih, iv, 2.5f, pa_alpha(outl, 0.9f));
        xline(c, &t, -ih, -iv, -ih, iv, 2.5f, pa_alpha(outl, 0.9f));
        xline(c, &t, ih + b2, -iv - b2, ih + b2, iv, 1.6f, pa_alpha(light, 0.7f));
        xline(c, &t, -ih, -iv - b2, ih + b2, -iv - b2, 1.6f, pa_alpha(light, 0.7f));
        if (mat == M_WOOD) {
            float m = (hw + ih) * 0.5f;
            xline(c, &t, -m, hh * 0.98f, -m, -hh * 0.98f, 1.2f, pa_alpha(dark, 0.45f));
            xline(c, &t, m, hh * 0.98f, m, -hh * 0.98f, 1.2f, pa_alpha(dark, 0.45f));
            xcirc(c, &t, -m, (hh + iv) * 0.5f, 1.6f, pa_alpha(outl, 0.8f));
            xcirc(c, &t, m, -(hh + iv) * 0.5f, 1.6f, pa_alpha(outl, 0.8f));
        } else if (mat == M_STONE) {
            xell(c, &t, -hw * 0.75f, hh * 0.6f, hw * 0.12f, hh * 0.08f, 0.3f, pa_alpha(dark, 0.5f));
            xell(c, &t, hw * 0.7f, -hh * 0.72f, hw * 0.14f, hh * 0.07f, -0.2f, pa_alpha(dark, 0.5f));
        } else {
            xline(c, &t, -hw * 0.85f, -hh * 0.2f, -hw * 0.65f, hh * 0.75f, 2.0f, PA_RGBA(255, 255, 255, 140));
        }
    } else {
        float fo[8] = { -hw, -hh, hw, -hh, hw, hh, -hw, hh };
        V2 pts[4];
        for (int i = 0; i < 4; i++) pts[i] = xp(&t, fo[i * 2], fo[i * 2 + 1]);
        pa_fill_poly_paint(c, pts, 4, &fp);
        /* Bevel: lit top edge, dark bottom edge, inner panel. */
        float bv = pa_clampf(mn * 0.28f, 1.5f, 5.0f);
        xline(c, &t, -hw + bv, hh - bv * 0.5f, hw - bv, hh - bv * 0.5f, bv * 0.8f, pa_alpha(light, mat == M_ICE ? 0.9f : 0.75f));
        xline(c, &t, -hw + bv, -hh + bv * 0.5f, hw - bv, -hh + bv * 0.5f, bv * 0.8f, pa_alpha(dark, 0.55f));
        if (mat == M_WOOD) {
            int along_x = hw >= hh;
            float L = along_x ? hw : hh, Sx = along_x ? hh : hw;
            for (int k = -1; k <= 1; k += 2) {
                float off = (float)k * Sx * 0.32f + Sx * 0.08f * hash1((float)(seed % 101));
                float wv = Sx * 0.08f;
                V2 g[4];
                for (int q = 0; q < 4; q++) {
                    float a = -L * 0.85f + (float)q * L * 0.57f;
                    float b = off + ((q & 1) ? wv : -wv);
                    g[q] = along_x ? xp(&t, a, b) : xp(&t, b, a);
                }
                pa_stroke_poly(c, g, 4, 0, 1.3f, pa_alpha(dark, 0.55f));
            }
            if (mn > 4.0f) {
                float nx = hw - bv * 1.6f, ny = hh - bv * 1.6f;
                if (hw > hh * 2.5f) { xcirc(c, &t, -nx, 0, 1.5f, pa_alpha(outl, 0.8f)); xcirc(c, &t, nx, 0, 1.5f, pa_alpha(outl, 0.8f)); }
                else if (hh > hw * 2.5f) { xcirc(c, &t, 0, -ny, 1.5f, pa_alpha(outl, 0.8f)); xcirc(c, &t, 0, ny, 1.5f, pa_alpha(outl, 0.8f)); }
            }
        } else if (mat == M_STONE) {
            PA_Rng r; pa_rng_seed(&r, seed);
            for (int k = 0; k < 3; k++) {
                float ex = pa_rng_range(&r, -hw * 0.7f, hw * 0.7f), ey = pa_rng_range(&r, -hh * 0.6f, hh * 0.6f);
                float er = mn * pa_rng_range(&r, 0.25f, 0.45f);
                xell(c, &t, ex, ey, er * 1.3f, er * 0.8f, pa_rng_range(&r, 0, 3), pa_alpha(dark, 0.32f));
                xell(c, &t, ex - er * 0.2f, ey + er * 0.25f, er * 0.6f, er * 0.3f, 0.2f, pa_alpha(light, 0.35f));
            }
        } else {
            /* Ice glints. */
            float L = hw > hh ? hw : hh;
            if (hw >= hh) {
                xline(c, &t, -hw * 0.7f, -hh * 0.5f, -hw * 0.7f + hh * 0.9f, hh * 0.5f, 2.2f, PA_RGBA(255, 255, 255, 150));
                xline(c, &t, -hw * 0.5f, -hh * 0.5f, -hw * 0.5f + hh * 0.6f, hh * 0.2f, 1.4f, PA_RGBA(255, 255, 255, 110));
            } else {
                xline(c, &t, -hw * 0.5f, L * 0.2f, hw * 0.5f, L * 0.55f, 2.2f, PA_RGBA(255, 255, 255, 150));
                xline(c, &t, -hw * 0.5f, L * 0.0f, hw * 0.3f, L * 0.25f, 1.4f, PA_RGBA(255, 255, 255, 110));
            }
        }
    }
    /* Cracks as health drops. */
    PA_Color cc = mat == M_ICE ? PA_RGBA(255, 255, 255, 230) : mat == M_WOOD ? PA_RGBA(80, 36, 8, 220) : PA_RGBA(40, 44, 54, 220);
    float cw = pa_clampf(mn * 0.25f, 1.2f, 2.4f);
    if (hpf < 0.75f) crack_path(c, &t, hw, hh, seed, 0, cc, cw);
    if (hpf < 0.45f) { crack_path(c, &t, hw, hh, seed, 1, cc, cw); crack_path(c, &t, hw, hh, seed, 2, cc, cw * 0.8f); }
    if (hpf < 0.25f) crack_path(c, &t, hw, hh, seed, 3, cc, cw);
    if (flash > 0) {
        float fo2[8] = { -hw, -hh, hw, -hh, hw, hh, -hw, hh };
        xpoly(c, &t, fo2, 4, PA_RGBA(255, 255, 255, (int)(pa_clamp01(flash) * 110)));
    }
    clip_pop(c);
}

static void draw_block(PA_Canvas *c, const Body *b) {
    float sx = SX(b->p.x), sy = SY(b->p.y);
    float hw = (b->shape == SH_CIRC ? b->r : b->hw) * S.z, hh = (b->shape == SH_CIRC ? b->r : b->hh) * S.z;
    float e = (hw > hh ? hw : hh) * 1.5f;
    if (sx + e < 0 || sx - e > (float)c->w || sy + e < 0 || sy - e > (float)c->h) return;
    draw_block_raw(c, b->mat, b->shape == SH_CIRC ? 2 : b->var, sx, sy, hw, hh, b->a,
                   b->hpmax > 0 ? b->hp / b->hpmax : 1.0f, b->seed, b->flash);
}

/* --------------------------------------------------------------- pigs ---- */
/* r in px; angle world-style; look -1..1; hpf health; mood 0 normal 1 laughing */
static void draw_pig(PA_Canvas *c, float x, float y, float r, float ang, int var, float look, float hpf,
                     int blink, float squash, int mood) {
    int type = var & 7, helmet = var & 8;
    float sq = pa_clamp01(squash) * 0.18f;
    Xf t = xf_make(x, y, ang, r);
    PA_Color out = pa_hex(0x2E6A1C);
    /* Ears behind the head. */
    for (int s = -1; s <= 1; s += 2) {
        xcirc(c, &t, 0.62f * (float)s, 0.78f, 0.27f, out);
        xcirc(c, &t, 0.62f * (float)s, 0.78f, 0.21f, pa_hex(0x7CC444));
        xcirc(c, &t, 0.62f * (float)s, 0.78f, 0.11f, pa_hex(0x4E9A2C));
    }
    V2 cpt = xp(&t, 0, 0);
    pa_fill_ellipse(c, cpt.x, cpt.y, r * (1.0f + sq) + 2.0f, r * (1.0f - sq) + 2.0f, out);
    PA_Paint bp = pa_radial(cpt.x - r * 0.35f, cpt.y - r * 0.45f, r * 0.1f, r * 1.25f);
    pa_stop(&bp, 0, pa_hex(0xB6EA6E)); pa_stop(&bp, 0.5f, pa_hex(0x7EC646)); pa_stop(&bp, 1, pa_hex(0x4C9A2A));
    pa_fill_ellipse_paint(c, cpt.x, cpt.y, r * (1.0f + sq), r * (1.0f - sq), &bp);
    /* Bruises. */
    if (hpf < 0.66f) xell(c, &t, -0.45f, 0.05f, 0.24f, 0.2f, 0.3f, pa_hex(0x5A7E3A));
    if (hpf < 0.35f) { xell(c, &t, 0.5f, -0.45f, 0.18f, 0.13f, -0.4f, pa_hex(0x5A7E3A)); xell(c, &t, 0.35f, 0.55f, 0.12f, 0.08f, 0.2f, pa_hex(0x5A7E3A)); }
    /* Eyes. */
    float ey = 0.2f, ex = 0.4f, er = type == PIG_KING ? 0.17f : 0.22f;
    for (int s = -1; s <= 1; s += 2) {
        float exx = ex * (float)s;
        int shut = blink || (s < 0 && hpf < 0.35f);
        if (shut) {
            xline(c, &t, exx - er, ey, exx + er, ey, r * 0.08f + 1, out);
        } else {
            xcirc(c, &t, exx, ey, er + 0.05f, out);
            xcirc(c, &t, exx, ey, er, PA_RGB(255, 255, 255));
            xcirc(c, &t, exx + look * er * 0.45f, ey - 0.02f, er * 0.45f, PA_RGB(20, 24, 20));
            xcirc(c, &t, exx + look * er * 0.45f - er * 0.12f, ey + er * 0.15f, er * 0.13f, PA_RGB(255, 255, 255));
        }
        if (hpf < 0.66f && s < 0) xline(c, &t, exx - er, ey + er * 1.4f, exx + er, ey + er * 1.0f, r * 0.07f + 1, out);
    }
    /* Snout. */
    xell(c, &t, 0, -0.18f, 0.38f, 0.29f, 0, out);
    xell(c, &t, 0, -0.17f, 0.33f, 0.24f, 0, pa_hex(0xA2DE66));
    xell(c, &t, -0.12f, -0.17f, 0.07f, 0.1f, 0, pa_hex(0x2F6020));
    xell(c, &t, 0.12f, -0.17f, 0.07f, 0.1f, 0, pa_hex(0x2F6020));
    /* Mouth. */
    if (mood) {
        float m[8] = { -0.32f, -0.5f, 0.32f, -0.5f, 0.2f, -0.78f, -0.2f, -0.78f };
        xpoly(c, &t, m, 4, pa_hex(0x3A1A10));
        float tg[6] = { -0.12f, -0.72f, 0.12f, -0.72f, 0.0f, -0.62f };
        xpoly(c, &t, tg, 3, pa_hex(0xE04A5A));
    } else {
        xline(c, &t, -0.2f, -0.55f, 0.0f, -0.6f, r * 0.06f + 1, out);
        xline(c, &t, 0.0f, -0.6f, 0.2f, -0.54f, r * 0.06f + 1, out);
    }
    if (helmet) {
        V2 h[20]; int n = 0;
        for (int i = 0; i <= 16; i++) {
            float a = 0.15f + (PA_PI - 0.3f) * (float)i / 16.0f;
            h[n++] = xp(&t, cosf(a) * 1.08f, sinf(a) * 1.08f + 0.04f);
        }
        h[n++] = xp(&t, -1.05f, 0.42f);
        h[n++] = xp(&t, 1.05f, 0.42f);
        V2 hp2[20];
        for (int i = 0; i < n; i++) hp2[i] = h[n - 1 - i];
        pa_fill_poly(c, hp2, n, pa_hex(0x55606E));
        for (int i = 0; i < n; i++) { V2 d = vsub(hp2[i], cpt); hp2[i] = vadd(cpt, vmul(d, 0.93f)); }
        PA_Paint hpnt = pa_linear(cpt.x - r, cpt.y - r, cpt.x + r * 0.5f, cpt.y);
        pa_stop(&hpnt, 0, pa_hex(0xD2DAE4)); pa_stop(&hpnt, 1, pa_hex(0x8C96A4));
        pa_fill_poly_paint(c, hp2, n, &hpnt);
        xline(c, &t, -1.05f, 0.46f, 1.05f, 0.46f, r * 0.14f, pa_hex(0x6A7482));
        xcirc(c, &t, -0.6f, 0.46f, 0.05f, pa_hex(0xE8EEF4));
        xcirc(c, &t, 0.6f, 0.46f, 0.05f, pa_hex(0xE8EEF4));
        if (hpf < 0.5f) xline(c, &t, 0.1f, 1.0f, 0.3f, 0.6f, 2, pa_hex(0x3A424E));
    }
    if (type == PIG_KING) {
        float cr[14] = { -0.55f, 0.8f, 0.55f, 0.8f, 0.62f, 1.45f, 0.32f, 1.12f, 0.0f, 1.55f, -0.32f, 1.12f, -0.62f, 1.45f };
        float co[14];
        for (int i = 0; i < 7; i++) { co[i * 2] = cr[i * 2] * 1.1f; co[i * 2 + 1] = (cr[i * 2 + 1] - 1.1f) * 1.1f + 1.1f; }
        xpoly(c, &t, co, 7, pa_hex(0x8A5A06));
        xpoly(c, &t, cr, 7, pa_hex(0xFFD23A));
        xline(c, &t, -0.5f, 0.88f, 0.5f, 0.88f, r * 0.08f, pa_hex(0xE8A818));
        xcirc(c, &t, 0, 1.0f, 0.08f, pa_hex(0xE8304A));
        xcirc(c, &t, -0.38f, 0.98f, 0.06f, pa_hex(0x3AA8E8));
        xcirc(c, &t, 0.38f, 0.98f, 0.06f, pa_hex(0x3AA8E8));
    }
}

/* --------------------------------------------------------------- birds --- */
static void draw_bird(PA_Canvas *c, int bt, float x, float y, float r, float ang, int blink, float squash) {
    Xf t = xf_make(x, y, ang, r);
    float sq = pa_clamp01(squash) * 0.2f;
    t.k = r;
    PA_Color ink = PA_RGB(22, 18, 20);
    switch (bt) {
    case BT_YELLOW: {
        float tuft[6] = { -0.2f, 0.9f, -0.5f, 1.45f, 0.05f, 1.0f };
        xpoly(c, &t, tuft, 3, ink);
        float tuft2[6] = { 0.0f, 0.95f, 0.05f, 1.5f, 0.25f, 0.9f };
        xpoly(c, &t, tuft2, 3, ink);
        float tail[6] = { -0.95f, -0.45f, -1.45f, -0.3f, -1.4f, -0.7f };
        xpoly(c, &t, tail, 3, ink);
        float body[12] = { -1.02f, -0.82f, 1.12f, -0.82f, 1.1f, -0.7f, 0.08f, 1.12f, -0.08f, 1.12f, -1.05f, -0.7f };
        float bo[12];
        for (int i = 0; i < 6; i++) { bo[i * 2] = body[i * 2] * 1.1f; bo[i * 2 + 1] = body[i * 2 + 1] * 1.1f - 0.02f; }
        xpoly(c, &t, bo, 6, pa_hex(0x8E6406));
        xpoly(c, &t, body, 6, pa_hex(0xF6D21E));
        float hl[8] = { -0.6f, -0.3f, -0.1f, 0.75f, 0.05f, 0.6f, -0.4f, -0.35f };
        xpoly(c, &t, hl, 4, pa_hex(0xFFE96A));
        xell(c, &t, 0.05f, -0.62f, 0.62f, 0.22f, 0, pa_hex(0xFFF2C0));
        float ey = 0.05f;
        if (blink) { xline(c, &t, 0.0f, ey, 0.5f, ey, r * 0.1f + 1, ink); }
        else {
            xcirc(c, &t, 0.08f, ey, 0.19f, PA_RGB(255, 255, 255)); xcirc(c, &t, 0.44f, ey, 0.19f, PA_RGB(255, 255, 255));
            xcirc(c, &t, 0.15f, ey, 0.08f, ink); xcirc(c, &t, 0.5f, ey, 0.08f, ink);
        }
        float brow[12] = { -0.12f, 0.35f, 0.26f, 0.15f, 0.62f, 0.32f, 0.62f, 0.2f, 0.26f, 0.04f, -0.12f, 0.22f };
        xpoly(c, &t, brow, 6, pa_hex(0x6A2E10));
        float beak[6] = { 0.42f, -0.12f, 1.35f, -0.28f, 0.42f, -0.44f };
        xpoly(c, &t, beak, 3, pa_hex(0xF08A14));
        float lb[6] = { 0.42f, -0.36f, 1.05f, -0.34f, 0.45f, -0.5f };
        xpoly(c, &t, lb, 3, pa_hex(0xC86A0A));
        break;
    }
    default: {
        PA_Color body0, body1, body2, belly, edge;
        float rad = 1.0f;
        if (bt == BT_BLUE) { body0 = pa_hex(0x9AD8F8); body1 = pa_hex(0x5BB4EA); body2 = pa_hex(0x3584C4); belly = pa_hex(0xD8F0FC); edge = pa_hex(0x1E5A8A); }
        else if (bt == BT_BOMB) { body0 = pa_hex(0x5A5E68); body1 = pa_hex(0x2A2D33); body2 = pa_hex(0x141518); belly = pa_hex(0x6E6A66); edge = pa_hex(0x08090A); }
        else if (bt == BT_SILVER) { body0 = pa_hex(0xF4F6FA); body1 = pa_hex(0xD2D7E0); body2 = pa_hex(0x9AA2B0); belly = pa_hex(0xFFFFFF); edge = pa_hex(0x5A6270); }
        else { body0 = pa_hex(0xF2604E); body1 = pa_hex(0xD8262B); body2 = pa_hex(0x9E1418); belly = pa_hex(0xF4D9B6); edge = pa_hex(0x6A0A10); }
        /* Tail and crest. */
        float tail[8] = { -0.85f, 0.1f, -1.45f, 0.32f, -1.38f, -0.05f, -0.85f, -0.15f };
        xpoly(c, &t, tail, 4, bt == BT_SILVER ? pa_hex(0x6A7280) : ink);
        float tail2[6] = { -0.9f, -0.05f, -1.4f, -0.32f, -0.88f, -0.28f };
        xpoly(c, &t, tail2, 3, bt == BT_SILVER ? pa_hex(0x6A7280) : ink);
        if (bt == BT_RED) {
            xell(c, &t, -0.05f, 1.05f, 0.28f, 0.12f, 1.2f, body2);
            xell(c, &t, 0.18f, 1.0f, 0.24f, 0.1f, 0.6f, body1);
        } else if (bt == BT_BLUE) {
            float cr[6] = { -0.1f, 0.85f, -0.35f, 1.35f, 0.15f, 0.95f };
            xpoly(c, &t, cr, 3, ink);
        } else if (bt == BT_BOMB) {
            V2 a = xp(&t, 0.0f, 0.95f), b = xp(&t, -0.2f, 1.35f), d = xp(&t, 0.05f, 1.55f);
            pa_line(c, a.x, a.y, b.x, b.y, r * 0.16f, ink);
            pa_line(c, b.x, b.y, d.x, d.y, r * 0.13f, ink);
            pa_fill_circle(c, d.x, d.y, r * 0.13f, pa_hex(0xFFB020));
            pa_fill_circle(c, d.x, d.y, r * 0.07f, pa_hex(0xFFF2A0));
        } else if (bt == BT_SILVER) {
            float cr[8] = { -0.5f, 0.75f, -1.2f, 1.05f, -1.05f, 0.7f, -0.6f, 0.55f };
            xpoly(c, &t, cr, 4, pa_hex(0x8A92A0));
        }
        V2 cp = xp(&t, 0, 0);
        float rx = r * rad * (1.0f + sq), ry = r * rad * (1.0f - sq);
        pa_fill_ellipse(c, cp.x, cp.y, rx + 1.8f, ry + 1.8f, edge);
        PA_Paint bp = pa_radial(cp.x - r * 0.35f, cp.y - r * 0.45f, r * 0.08f, r * 1.2f);
        pa_stop(&bp, 0, body0); pa_stop(&bp, 0.5f, body1); pa_stop(&bp, 1, body2);
        pa_fill_ellipse_paint(c, cp.x, cp.y, rx, ry, &bp);
        if (bt == BT_BOMB) xell(c, &t, -0.38f, 0.45f, 0.26f, 0.14f, 0.7f, PA_RGBA(255, 255, 255, 70));
        xell(c, &t, 0.1f, -0.5f, 0.62f, 0.36f, 0.0f, belly);
        if (bt == BT_SILVER) xell(c, &t, -0.2f, 0.45f, 0.5f, 0.32f, 0.3f, pa_hex(0xB8C0CC));
        /* Eyes. */
        float ey = 0.16f, e1 = 0.28f, e2 = 0.64f, er = bt == BT_BLUE ? 0.22f : 0.2f;
        if (bt == BT_BOMB) {
            float band[12] = { 0.0f, 0.42f, 0.42f, 0.24f, 0.9f, 0.4f, 0.9f, 0.24f, 0.42f, 0.08f, 0.0f, 0.26f };
            xpoly(c, &t, band, 6, pa_hex(0xE2532A));
        }
        if (blink) {
            xline(c, &t, e1 - er, ey, e1 + er, ey, r * 0.09f + 1, ink);
            xline(c, &t, e2 - er, ey, e2 + er, ey, r * 0.09f + 1, ink);
        } else {
            xcirc(c, &t, e1, ey, er, PA_RGB(255, 255, 255));
            xcirc(c, &t, e2, ey, er, PA_RGB(255, 255, 255));
            xcirc(c, &t, e1 + 0.07f, ey - 0.01f, er * 0.42f, ink);
            xcirc(c, &t, e2 + 0.07f, ey - 0.01f, er * 0.42f, ink);
        }
        if (bt == BT_RED || bt == BT_SILVER) {
            float brow[12] = { 0.02f, 0.46f, 0.46f, 0.27f, 0.9f, 0.44f, 0.9f, 0.31f, 0.46f, 0.15f, 0.02f, 0.33f };
            xpoly(c, &t, brow, 6, bt == BT_SILVER ? pa_hex(0x4A5260) : ink);
        } else if (bt == BT_BLUE) {
            xline(c, &t, e1 - 0.15f, ey + 0.3f, e1 + 0.12f, ey + 0.25f, r * 0.08f + 1, ink);
        }
        /* Beak. */
        float bx = bt == BT_BLUE ? 0.58f : 0.6f, bl = bt == BT_BLUE ? 1.0f : 1.18f;
        float beak[6] = { bx, 0.0f, bl, -0.14f, bx, -0.26f };
        xpoly(c, &t, beak, 3, pa_hex(0xF5B21E));
        float lb[6] = { bx, -0.2f, bl - 0.16f, -0.24f, bx + 0.02f, -0.4f };
        xpoly(c, &t, lb, 3, pa_hex(0xD4860E));
        break;
    }
    }
}

/* ------------------------------------------------------------ slingshot -- */
static void sling_tips(V2 *back, V2 *front) {
    *back = v2(SLING_X - 0.20f, ANCHOR_Y + 0.02f);
    *front = v2(SLING_X + 0.22f, ANCHOR_Y - 0.01f);
}

static void draw_sling_part(PA_Canvas *c, int front) {
    float z = S.z;
    PA_Color wood = pa_hex(0x9A5424), wd = pa_hex(0x4E2410), wl = pa_hex(0xC8783A);
    V2 base = v2(SLING_X, LEDGE_TOP), fork = v2(SLING_X, LEDGE_TOP + 0.72f);
    V2 bt, ft; sling_tips(&bt, &ft);
    if (!front) {
        pa_line(c, SX(base.x), SY(base.y), SX(fork.x), SY(fork.y), 0.24f * z + 3, wd);
        pa_line(c, SX(base.x), SY(base.y), SX(fork.x), SY(fork.y), 0.24f * z, wood);
        pa_line(c, SX(base.x - 0.05f), SY(base.y + 0.05f), SX(fork.x - 0.05f), SY(fork.y), 0.06f * z, wl);
        pa_line(c, SX(fork.x), SY(fork.y), SX(bt.x), SY(bt.y + 0.1f), 0.17f * z + 3, wd);
        pa_line(c, SX(fork.x), SY(fork.y), SX(bt.x), SY(bt.y + 0.1f), 0.17f * z, pa_shade(wood, -0.15f));
        pa_line(c, SX(bt.x), SY(bt.y - 0.02f), SX(bt.x), SY(bt.y + 0.14f), 0.2f * z, wd);
    } else {
        pa_line(c, SX(fork.x), SY(fork.y), SX(ft.x), SY(ft.y + 0.1f), 0.17f * z + 3, wd);
        pa_line(c, SX(fork.x), SY(fork.y), SX(ft.x), SY(ft.y + 0.1f), 0.17f * z, wood);
        pa_line(c, SX(fork.x + 0.03f), SY(fork.y + 0.05f), SX(ft.x - 0.02f), SY(ft.y + 0.1f), 0.05f * z, wl);
        pa_line(c, SX(ft.x), SY(ft.y - 0.02f), SX(ft.x), SY(ft.y + 0.14f), 0.2f * z, wd);
    }
}

static void draw_band(PA_Canvas *c, V2 tip, V2 pouch, float width) {
    pa_line(c, SX(tip.x), SY(tip.y), SX(pouch.x), SY(pouch.y), width * S.z + 2, pa_hex(0x2A140A));
    pa_line(c, SX(tip.x), SY(tip.y), SX(pouch.x), SY(pouch.y), width * S.z, pa_hex(0x5A2E16));
}

/* ------------------------------------------------------------ particles -- */
static void draw_parts(PA_Canvas *c) {
    float z = S.z;
    for (int i = 0; i < S.np; i++) {
        Part *q = &S.parts[i];
        float k = q->life / q->max;
        float x = SX(q->p.x), y = SY(q->p.y);
        switch (q->kind) {
        case P_SHARD: {
            Xf t = xf_make(x, y, q->a, q->size * z);
            float sh[8] = { -0.6f, -0.35f, 0.5f, -0.45f, 0.65f, 0.3f, -0.4f, 0.4f };
            xpoly(c, &t, sh, 4, pa_alpha(q->col, pa_clamp01(k * 3.0f)));
            break;
        }
        case P_DUST:
            pa_fill_circle(c, x, y, q->size * z * (1.0f + (1.0f - k) * 1.4f), pa_alpha(q->col, k * k * 0.55f));
            break;
        case P_SMOKE:
            pa_fill_circle(c, x, y, q->size * z * (0.6f + (1.0f - k) * 1.0f), pa_alpha(q->col, pa_clamp01(k * 1.6f)));
            break;
        case P_FIRE: {
            PA_Color col = pa_mix(PA_RGB(120, 30, 20), pa_mix(PA_RGB(255, 140, 30), PA_RGB(255, 245, 170), pa_clamp01(k * 2 - 1)), pa_clamp01(k * 2));
            pa_fill_circle(c, x, y, q->size * z * (0.5f + (1.0f - k)), pa_alpha(col, pa_clamp01(k * 2.0f)));
            break;
        }
        case P_FEATHER: {
            Xf t = xf_make(x, y, q->a, q->size * z);
            xell(c, &t, 0, 0, 1.0f, 0.35f, 0, pa_alpha(q->col, pa_clamp01(k * 2)));
            xline(c, &t, -1.0f, 0, 1.0f, 0, 1.0f, pa_alpha(pa_shade(q->col, -0.4f), pa_clamp01(k * 2)));
            break;
        }
        case P_SPARK:
            pa_line(c, x, y, x - q->v.x * 0.03f * z, y + q->v.y * 0.03f * z, q->size * z, pa_alpha(q->col, k));
            break;
        case P_RING: {
            float rr = (q->size + (q->max - q->life) * q->va) * z;
            pa_stroke_circle(c, x, y, rr, 0.08f * z * k + 1, pa_alpha(q->col, k));
            break;
        }
        default: break;
        }
    }
}

static void draw_floaters(PA_Canvas *c, int screen) {
    char buf[24];
    for (int i = 0; i < MAXF; i++) {
        Floater *f = &S.fl[i];
        if (f->t <= 0 || f->screen != screen) continue;
        float age = 1.4f - f->t;
        float pop = age < 0.15f ? age / 0.15f * 1.2f : (age < 0.3f ? 1.2f - (age - 0.15f) / 0.15f * 0.2f : 1.0f);
        float a = pa_clamp01(f->t / 0.35f);
        float size = screen ? 26.0f * ui_u() : 0.42f * S.z * f->size * 1.4f;
        if (!screen && size < 16) size = 16;
        size *= pop;
        snprintf(buf, sizeof buf, "%d", f->value);
        float x = screen ? f->p.x : SX(f->p.x), y = screen ? f->p.y : SY(f->p.y);
        pa_text_bold(c, buf, x, y - size * 0.5f, size, pa_alpha(f->col, a), PA_RGBA(30, 24, 30, (int)(220 * a)), PA_ALIGN_CENTER, size * 0.06f, 1.6f);
    }
}

/* ----------------------------------------------------------------- world -- */
static void draw_world(PA_Canvas *c) {
    /* Static rock first. */
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (b->used && b->kind == K_STATIC && b->var != 9) draw_rock(c, b, S.theme);
    }
    /* Previous/current flight trail. */
    for (int i = 0; i < S.ntrail; i++) {
        float r = (i % 3 == 0 ? 0.075f : 0.045f) * S.z;
        pa_fill_circle(c, SX(S.trail[i].x), SY(S.trail[i].y), r + 1, PA_RGBA(40, 60, 80, 50));
        pa_fill_circle(c, SX(S.trail[i].x), SY(S.trail[i].y), r, PA_RGBA(255, 255, 255, 220));
    }
    /* Sling, back half. */
    if (S.screen != SC_SELECT) draw_sling_part(c, 0);
    V2 bt, ft; sling_tips(&bt, &ft);
    int have_bird = (S.phase == PH_AIM || S.phase == PH_INTRO) && S.sel >= 0 && S.sel < S.ndeck && !S.used[S.sel];
    V2 pouch = vadd(anchor(), S.pull);
    if (!have_bird) pouch = v2(SLING_X + 0.01f, ANCHOR_Y - 0.12f);
    float bandw = 0.11f - vlen(S.pull) * 0.03f;
    float br = have_bird ? BIRDS[S.deck[S.sel]].r : 0.25f;
    V2 back_attach = vadd(pouch, v2(-br * 0.8f, 0));
    draw_band(c, bt, have_bird ? back_attach : pouch, bandw);

    /* Blocks, pigs. */
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (!b->used) continue;
        if (b->kind == K_BLOCK) draw_block(c, b);
    }
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (!b->used || b->kind != K_PIG) continue;
        draw_pig(c, SX(b->p.x), SY(b->p.y), b->r * S.z, b->a, b->var, b->look, b->hp / b->hpmax, b->blink < 0, b->squash, 0);
    }

    /* Bird in the pouch, hopping in from the cards. */
    if (have_bird) {
        int bt2 = S.deck[S.sel];
        float r = BIRDS[bt2].r;
        V2 p = pouch;
        if (S.load_t < 1.0f) {
            float k = pa_smooth(S.load_t);
            V2 from = v2(S.cx - (float)S.W * 0.5f / S.z + 1.0f, -0.2f);
            p = v2(pa_lerpf(from.x, pouch.x, k), pa_lerpf(from.y, pouch.y, k) + sinf(k * PA_PI) * 1.6f);
        }
        float ang = 0.0f;
        if (vlen(S.pull) > 0.1f) ang = atan2f(-S.pull.y, -S.pull.x);
        int blink = fmodf(S.time + 0.3f, 3.1f) < 0.12f;
        /* Leather pouch behind the bird. */
        pa_fill_ellipse(c, SX(pouch.x - r * 0.7f), SY(pouch.y), 0.12f * S.z, 0.22f * S.z, pa_hex(0x3A1C0C));
        draw_bird(c, bt2, SX(p.x), SY(p.y), r * S.z, ang, blink, 0);
    }
    /* Live birds. */
    for (int i = 0; i < MAXB; i++) {
        Body *b = &g_b[i];
        if (!b->used || b->kind != K_BIRD) continue;
        float ang = b->hits ? b->a : atan2f(b->v.y, b->v.x);
        draw_bird(c, b->bird_t, SX(b->p.x), SY(b->p.y), b->r * S.z, ang, b->hits > 0 && b->quiet > 0.2f, b->squash);
    }
    /* Sling front half + front band over the bird. */
    draw_band(c, ft, have_bird ? vadd(pouch, v2(-br * 0.6f, -br * 0.15f)) : pouch, bandw);
    if (S.screen != SC_SELECT) draw_sling_part(c, 1);

    /* Aiming arc. */
    if (have_bird && S.dragging && vlen(S.pull) > 0.12f) {
        V2 pts[40];
        V2 v = vmul(S.pull, -VMAX / MAXPULL);
        int n = predict(pouch, v, BIRDS[S.deck[S.sel]].r, pts, 26, 6, 160);
        for (int i = 0; i < n; i++) {
            float k = 1.0f - (float)i / 28.0f;
            float r = (0.035f + 0.045f * k) * S.z;
            pa_fill_circle(c, SX(pts[i].x), SY(pts[i].y), r + 1.2f, PA_RGBA(30, 50, 70, (int)(90 * k)));
            pa_fill_circle(c, SX(pts[i].x), SY(pts[i].y), r, PA_RGBA(255, 255, 255, (int)(255 * pa_clamp01(k + 0.2f))));
        }
    }
    draw_parts(c);
    draw_floaters(c, 0);
}

/* Foreground framing: jungle leaves, snowfall, embers. */
static void draw_foreground(PA_Canvas *c, int theme, float time) {
    float W = (float)c->w, H = (float)c->h, u = H / 540.0f;
    if (W < H) u = W / 760.0f;
    if (theme == TH_JUNGLE) {
        for (int s = 0; s < 2; s++) {
            float bx = s ? W + 30 * u : -30 * u, by = -20 * u;
            float dir = s ? -1.0f : 1.0f;
            for (int k = 0; k < 4; k++) {
                float a = (0.35f + (float)k * 0.32f) + sinf(time * 0.9f + (float)k) * 0.03f;
                float len = (150 + (float)((k * 53) % 70)) * u;
                float ca = cosf(a) * dir, sa = sinf(a);
                float px = -sa, py = ca * dir;
                float wdt = (26 + (float)(k % 2) * 10) * u;
                V2 leaf[7];
                leaf[0] = v2(bx, by);
                leaf[1] = v2(bx + ca * len * 0.3f + px * wdt * 0.8f * dir, by + sa * len * 0.3f + py * wdt * 0.8f);
                leaf[2] = v2(bx + ca * len * 0.65f + px * wdt * dir, by + sa * len * 0.65f + py * wdt);
                leaf[3] = v2(bx + ca * len, by + sa * len);
                leaf[4] = v2(bx + ca * len * 0.65f - px * wdt * dir, by + sa * len * 0.65f - py * wdt);
                leaf[5] = v2(bx + ca * len * 0.3f - px * wdt * 0.8f * dir, by + sa * len * 0.3f - py * wdt * 0.8f);
                leaf[6] = v2(bx, by);
                pa_fill_poly(c, leaf, 7, k % 2 ? pa_hex(0x1E5A2A) : pa_hex(0x2A6E32));
                pa_line(c, bx, by, bx + ca * len * 0.95f, by + sa * len * 0.95f, 2.0f * u, pa_hex(0x174A22));
            }
        }
    } else if (theme == TH_FROST) {
        for (int k = 0; k < 70; k++) {
            float sx = pa_wrapf((float)k * 97.3f + time * (14 + (float)(k % 5) * 6) + sinf(time + (float)k) * 10, W + 20) - 10;
            float sy = pa_wrapf((float)k * 53.7f + time * (30 + (float)(k % 7) * 8), H + 20) - 10;
            pa_fill_circle(c, sx, sy, (1.4f + (float)(k % 3)) * u, PA_RGBA(255, 255, 255, 190));
        }
    } else if (theme == TH_VOLCANO) {
        for (int k = 0; k < 40; k++) {
            float sx = pa_wrapf((float)k * 131.7f + sinf(time * 0.7f + (float)k) * 30, W);
            float sy = H - pa_wrapf((float)k * 71.3f + time * (40 + (float)(k % 5) * 12), H + 40);
            pa_fill_circle(c, sx, sy, (1.5f + (float)(k % 3)) * u, PA_RGBA(255, 160 + (k % 4) * 20, 60, 200));
        }
    }
}

/* ------------------------------------------------------------------ HUD -- */
static void star_poly(V2 *pts, float x, float y, float r, float rot) {
    for (int i = 0; i < 10; i++) {
        float a = -PA_PI * 0.5f + rot + (float)i * PA_PI / 5.0f;
        float rr = (i & 1) ? r * 0.48f : r;
        pts[i] = v2(x + cosf(a) * rr, y + sinf(a) * rr);
    }
}
static void draw_star(PA_Canvas *c, float x, float y, float r, float rot, int filled, float u) {
    V2 p[10];
    star_poly(p, x, y + 3 * u, r * 1.08f, rot);
    pa_fill_poly(c, p, 10, PA_RGBA(0, 0, 0, 80));
    star_poly(p, x, y, r * 1.12f, rot);
    pa_fill_poly(c, p, 10, filled ? pa_hex(0x7A3A04) : PA_RGBA(20, 24, 40, 200));
    star_poly(p, x, y, r, rot);
    if (filled) {
        PA_Paint g = pa_linear(x, y - r, x, y + r);
        pa_stop(&g, 0, pa_hex(0xFFF27A)); pa_stop(&g, 0.5f, pa_hex(0xFFC81E)); pa_stop(&g, 1, pa_hex(0xF08A0A));
        pa_fill_poly_paint(c, p, 10, &g);
        star_poly(p, x - r * 0.08f, y - r * 0.1f, r * 0.45f, rot);
        pa_fill_poly(c, p, 10, PA_RGBA(255, 255, 255, 90));
    } else {
        pa_fill_poly(c, p, 10, PA_RGBA(70, 80, 110, 200));
    }
}

static void draw_pause(PA_Canvas *c) {
    float x, y, r; pause_btn(&x, &y, &r);
    float u = ui_u();
    pa_fill_circle(c, x, y + 3 * u, r + 3 * u, PA_RGBA(0, 0, 0, 80));
    pa_fill_circle(c, x, y, r + 3 * u, pa_hex(0x6A3206));
    PA_Paint g = pa_linear(x, y - r, x, y + r);
    pa_stop(&g, 0, pa_hex(0xFFC64A)); pa_stop(&g, 1, pa_hex(0xEE7E0E));
    pa_fill_ellipse_paint(c, x, y, r, r, &g);
    pa_fill_ellipse(c, x, y - r * 0.45f, r * 0.7f, r * 0.35f, PA_RGBA(255, 255, 255, 60));
    float bw = r * 0.26f, bh = r * 0.95f;
    pa_round_rect(c, x - r * 0.3f - bw * 0.5f, y - bh * 0.5f + 2 * u, bw, bh, bw * 0.3f, PA_RGBA(90, 40, 0, 140));
    pa_round_rect(c, x + r * 0.3f - bw * 0.5f, y - bh * 0.5f + 2 * u, bw, bh, bw * 0.3f, PA_RGBA(90, 40, 0, 140));
    pa_round_rect(c, x - r * 0.3f - bw * 0.5f, y - bh * 0.5f, bw, bh, bw * 0.3f, PA_RGB(255, 255, 255));
    pa_round_rect(c, x + r * 0.3f - bw * 0.5f, y - bh * 0.5f, bw, bh, bw * 0.3f, PA_RGB(255, 255, 255));
}

static PA_Color bird_card_col(int bt, int k) {
    static const uint32_t C[BT_COUNT][2] = {
        { 0xFF6A4A, 0xC21E26 }, { 0xFFE05A, 0xE89A12 }, { 0x7ED2F8, 0x2E80C6 }, { 0x6E7280, 0x24262C }, { 0xE8ECF2, 0x8E96A6 },
    };
    return pa_hex(C[bt][k]);
}

static void draw_card(PA_Canvas *c, Rc r, int bt, int selected, float u, float alpha) {
    float rad = 10 * u;
    pa_round_rect(c, r.x + 2 * u, r.y + 5 * u, r.w, r.h, rad, PA_RGBA(0, 0, 0, (int)(90 * alpha)));
    if (selected) pa_round_rect(c, r.x - 4 * u, r.y - 4 * u, r.w + 8 * u, r.h + 8 * u, rad + 4 * u, PA_RGBA(255, 220, 60, (int)(230 * alpha)));
    pa_round_rect(c, r.x, r.y, r.w, r.h, rad, PA_RGBA(255, 255, 255, (int)(255 * alpha)));
    PA_Paint g = pa_linear(r.x, r.y, r.x, r.y + r.h);
    pa_stop(&g, 0, pa_alpha(bird_card_col(bt, 0), alpha)); pa_stop(&g, 1, pa_alpha(bird_card_col(bt, 1), alpha));
    pa_round_rect_paint(c, r.x + 4 * u, r.y + 4 * u, r.w - 8 * u, r.h - 8 * u, rad - 3 * u, &g);
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.52f;
    PA_Paint rg = pa_radial(cx, cy, 2 * u, r.w * 0.5f);
    pa_stop(&rg, 0, PA_RGBA(255, 255, 230, (int)(130 * alpha))); pa_stop(&rg, 1, PA_RGBA(255, 255, 230, 0));
    pa_fill_rect_paint(c, r.x + 4 * u, r.y + 4 * u, r.w - 8 * u, r.h - 8 * u, &rg);
    if (alpha < 0.99f) return;
    if (bt == BT_BLUE) {
        draw_bird(c, BT_BLUE, cx - 9 * u, cy + 9 * u, 10 * u, 0, 0, 0);
        draw_bird(c, BT_BLUE, cx + 10 * u, cy + 10 * u, 10 * u, 0, 0, 0);
        draw_bird(c, BT_BLUE, cx, cy - 7 * u, 11 * u, 0, 0, 0);
    } else {
        float br = bt == BT_BOMB ? 20 * u : bt == BT_YELLOW ? 17 * u : 18 * u;
        draw_bird(c, bt, cx, cy + 2 * u, br, 0, 0, 0);
    }
}

static void draw_cards(PA_Canvas *c) {
    float u = ui_u();
    Rc cr[MAXDECK]; int ci[MAXDECK];
    int n = card_layout(cr, ci);
    for (int k = 0; k < n; k++) {
        int i = ci[k];
        int sel = i == S.sel && (S.phase == PH_AIM || S.phase == PH_INTRO);
        Rc r = cr[k];
        if (i == S.ndeck - 1 && S.card_pop > 0 && S.destr_paid) {
            float s = 1.0f + sinf(pa_clamp01((1.6f - S.card_pop) / 0.5f) * PA_PI) * 0.35f;
            float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
            r.w *= s; r.h *= s; r.x = cx - r.w * 0.5f; r.y = cy - r.h * 0.5f;
        }
        draw_card(c, r, S.deck[i], sel, u, 1.0f);
        if (sel && S.phase == PH_AIM && S.deck[i] != BT_RED + 99) {
            const char *ab = BIRDS[S.deck[i]].ability;
            float tw = pa_text_width(ab, 12 * u, 1 * u);
            float lx = r.x + r.w * 0.5f;
            if (lx - tw * 0.5f < 8 * u) lx = 8 * u + tw * 0.5f;
            pa_round_rect(c, lx - tw * 0.5f - 8 * u, r.y - 26 * u, tw + 16 * u, 20 * u, 10 * u, PA_RGBA(10, 20, 40, 170));
            pa_text(c, ab, lx, r.y - 22 * u, 12 * u, PA_RGB(255, 236, 160), PA_ALIGN_CENTER, 1 * u);
        }
    }
}

static void draw_hud(PA_Canvas *c) {
    float u = ui_u(), W = (float)c->w;
    char buf[32];
    draw_pause(c);
    /* Score, top right. */
    float x1 = W - 22 * u;
    fmt_score(buf, sizeof buf, S.score);
    pa_text_bold(c, buf, x1, 10 * u, 30 * u, PA_RGB(255, 255, 255), PA_RGB(16, 26, 40), PA_ALIGN_RIGHT, 1.5f * u, 2.0f);
    /* Destructometer. */
    float x0 = x1 - 236 * u, y = 54 * u, h = 18 * u, sk = 7 * u;
    float k = S.destr_cap > 0 ? pa_clamp01(S.destr / S.destr_cap) : 0;
    V2 bg[4] = { { x0 + sk, y }, { x1 - 30 * u, y }, { x1 - 30 * u - sk, y + h }, { x0, y + h } };
    V2 bo[4] = { { x0 + sk - 3 * u, y - 3 * u }, { x1 - 27 * u, y - 3 * u }, { x1 - 30 * u - sk + 1 * u, y + h + 3 * u }, { x0 - 4 * u, y + h + 3 * u } };
    pa_fill_poly(c, bo, 4, PA_RGBA(10, 24, 34, 230));
    pa_fill_poly(c, bg, 4, PA_RGBA(40, 70, 88, 230));
    if (k > 0) {
        float xf = x0 + (x1 - 30 * u - x0) * k;
        clip_push(c, 0, 0, xf + sk, (float)c->h);
        PA_Paint g = pa_linear(0, y, 0, y + h);
        pa_stop(&g, 0, pa_hex(0xFFE27A)); pa_stop(&g, 1, pa_hex(0xF0820E));
        pa_fill_poly_paint(c, bg, 4, &g);
        clip_pop(c);
    }
    /* Feather nib at the left end of the meter. */
    {
        Xf t = xf_make(x0 - 6 * u, y + h * 0.5f, 0.35f, u);
        float f[8] = { -26, 0, -6, 7, 14, 0, -6, -7 };
        xpoly(c, &t, f, 4, PA_RGB(240, 244, 250));
        xline(c, &t, -26, 0, 14, 0, 1.5f * u, PA_RGB(150, 160, 175));
    }
    /* Reward card at the right end. */
    {
        float pulse = S.destr_paid && S.card_pop > 0 ? 1.0f + 0.25f * sinf(S.card_pop * 12) : 1.0f;
        float cw = 22 * u * pulse, ch = 30 * u * pulse;
        float cx = x1 - 12 * u, cy = y + h * 0.5f;
        pa_round_rect(c, cx - cw * 0.5f - 2 * u, cy - ch * 0.5f - 2 * u, cw + 4 * u, ch + 4 * u, 5 * u, PA_RGB(255, 255, 255));
        pa_round_rect(c, cx - cw * 0.5f, cy - ch * 0.5f, cw, ch, 4 * u, S.destr_paid ? PA_RGB(150, 150, 160) : PA_RGB(226, 50, 46));
        if (!S.destr_paid) draw_bird(c, BT_RED, cx, cy + 1 * u, 7 * u * pulse, 0, 0, 0);
    }
    if (S.card_pop > 0 && S.destr_paid) {
        float a = pa_clamp01(S.card_pop);
        pa_text_bold(c, "EXTRA BIRD!", x1 - 120 * u, y + h + 10 * u, 20 * u, pa_alpha(PA_RGB(255, 230, 90), a), PA_RGBA(40, 20, 0, (int)(220 * a)), PA_ALIGN_CENTER, 1.5f * u, 1.6f);
    }

    /* Room progress, top centre. */
    {
        float cx = W * 0.5f, yy = 32 * u, sp = 34 * u;
        int n = S.nrooms;
        float sx = cx - (float)(n - 1) * sp * 0.5f + 16 * u;
        pa_round_rect(c, sx - 52 * u, yy - 17 * u, (float)(n - 1) * sp + 70 * u, 34 * u, 17 * u, PA_RGBA(10, 20, 40, 110));
        draw_bird(c, BT_RED, sx - 32 * u, yy, 12 * u, 0, 0, 0);
        if (n > 1) pa_line(c, sx, yy, sx + (float)(n - 1) * sp, yy, 4 * u, PA_RGBA(255, 255, 255, 170));
        for (int i = 0; i < n; i++) {
            float px = sx + (float)i * sp;
            if (i < S.room) {
                pa_fill_circle(c, px, yy, 11 * u, PA_RGB(255, 255, 255));
                pa_line(c, px - 5 * u, yy, px - 1 * u, yy + 4 * u, 3 * u, PA_RGB(70, 170, 60));
                pa_line(c, px - 1 * u, yy + 4 * u, px + 6 * u, yy - 5 * u, 3 * u, PA_RGB(70, 170, 60));
            } else if (i == S.room) {
                pa_fill_circle(c, px, yy, 12 * u, PA_RGB(255, 255, 255));
                pa_fill_circle(c, px, yy, 8 * u, PA_RGB(255, 196, 40));
            } else {
                pa_fill_circle(c, px, yy, 11 * u, PA_RGB(255, 255, 255));
                pa_fill_circle(c, px, yy, 7.5f * u, PA_RGBA(60, 80, 110, 255));
            }
        }
        snprintf(buf, sizeof buf, "LEVEL %d", S.level + 1);
        pa_text_bold(c, buf, cx, yy + 22 * u, 13 * u, PA_RGB(255, 255, 255), PA_RGBA(10, 20, 40, 200), PA_ALIGN_CENTER, 1.2f * u, 1.2f);
    }
    if (S.phase != PH_WIN && S.phase != PH_FAIL) draw_cards(c);
    draw_floaters(c, 1);

    /* Banners. */
    float H = (float)c->h;
    if (S.phase == PH_INTRO && S.room == 0 && S.pt < 1.9f) {
        float a = pa_clamp01(S.pt * 4) * pa_clamp01((1.9f - S.pt) * 3);
        float s = 1.0f + 0.15f * (1.0f - pa_clamp01(S.pt * 5));
        snprintf(buf, sizeof buf, "LEVEL %d", S.level + 1);
        pa_text_bold(c, buf, W * 0.5f, H * 0.26f, 54 * u * s, pa_alpha(PA_RGB(255, 255, 255), a), PA_RGBA(30, 16, 10, (int)(230 * a)), PA_ALIGN_CENTER, 3 * u, 2.2f);
        pa_text_bold(c, THEME_NAME[S.theme], W * 0.5f, H * 0.26f + 66 * u, 20 * u, pa_alpha(PA_RGB(255, 214, 90), a), PA_RGBA(30, 16, 10, (int)(220 * a)), PA_ALIGN_CENTER, 2 * u, 1.5f);
    }
    if (S.banner && S.banner_t > 0) {
        float age = 1.6f - S.banner_t;
        float a = pa_clamp01(S.banner_t * 3);
        float s = age < 0.2f ? 0.6f + age / 0.2f * 0.55f : (age < 0.35f ? 1.15f - (age - 0.2f) / 0.15f * 0.15f : 1.0f);
        pa_text_bold(c, S.banner, W * 0.5f, H * 0.3f, 50 * u * s, pa_alpha(PA_RGB(255, 236, 110), a), PA_RGBA(60, 20, 0, (int)(235 * a)), PA_ALIGN_CENTER, 3 * u, 2.2f);
    }
    if (S.hint_t > 0 && S.phase == PH_AIM && !S.dragging) {
        float a = 0.6f + 0.4f * sinf(S.time * 5);
        pa_text_bold(c, "DRAG BACK TO AIM, LET GO TO LAUNCH", W * 0.5f, H - 50 * u, 18 * u, pa_alpha(PA_RGB(255, 255, 255), a), PA_RGBA(10, 20, 40, (int)(200 * a)), PA_ALIGN_CENTER, 1.5f * u, 1.5f);
        /* Ghost hand drag beside the sling. */
        float k = pa_wrapf(S.time * 0.7f, 1.0f);
        float hx = SX(SLING_X) - k * 70 * u, hy = SY(ANCHOR_Y) + k * 30 * u;
        pa_fill_circle(c, hx, hy, 16 * u, PA_RGBA(255, 255, 255, (int)(170 * (1 - k * 0.5f))));
        pa_stroke_circle(c, hx, hy, 22 * u, 3 * u, PA_RGBA(255, 255, 255, (int)(120 * (1 - k))));
    }
}

/* --------------------------------------------------------------- buttons -- */
static void icon_retry(PA_Canvas *c, float x, float y, float r, PA_Color col) {
    V2 pts[24]; int n = 0;
    for (int i = 0; i <= 20; i++) {
        float a = -PA_PI * 0.35f + (float)i / 20.0f * PA_PI * 1.55f;
        pts[n++] = v2(x + cosf(a) * r, y + sinf(a) * r);
    }
    pa_stroke_poly(c, pts, n, 0, r * 0.32f, col);
    float a = -PA_PI * 0.35f;
    V2 tip = v2(x + cosf(a) * r, y + sinf(a) * r);
    V2 tri[3] = { { tip.x - r * 0.5f, tip.y - r * 0.15f }, { tip.x + r * 0.42f, tip.y - r * 0.25f }, { tip.x + r * 0.05f, tip.y + r * 0.6f } };
    pa_fill_poly(c, tri, 3, col);
}
static void icon_menu(PA_Canvas *c, float x, float y, float r, PA_Color col) {
    float s = r * 0.62f, g = r * 0.16f;
    pa_round_rect(c, x - s - g * 0.5f, y - s - g * 0.5f, s, s, s * 0.25f, col);
    pa_round_rect(c, x + g * 0.5f, y - s - g * 0.5f, s, s, s * 0.25f, col);
    pa_round_rect(c, x - s - g * 0.5f, y + g * 0.5f, s, s, s * 0.25f, col);
    pa_round_rect(c, x + g * 0.5f, y + g * 0.5f, s, s, s * 0.25f, col);
}
static void icon_play(PA_Canvas *c, float x, float y, float r, PA_Color col) {
    V2 tri[3] = { { x - r * 0.45f, y - r * 0.6f }, { x + r * 0.65f, y }, { x - r * 0.45f, y + r * 0.6f } };
    pa_fill_poly(c, tri, 3, col);
}
static void icon_back(PA_Canvas *c, float x, float y, float r, PA_Color col) {
    V2 tri[3] = { { x - r * 0.65f, y }, { x + r * 0.05f, y - r * 0.6f }, { x + r * 0.05f, y + r * 0.6f } };
    pa_fill_poly(c, tri, 3, col);
    pa_round_rect(c, x - r * 0.05f, y - r * 0.2f, r * 0.7f, r * 0.4f, r * 0.12f, col);
}

static void btn_round(PA_Canvas *c, Rc r, int icon, PA_Color base, float u) {
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f, rad = r.w * 0.5f;
    pa_fill_circle(c, cx, cy + 5 * u, rad, PA_RGBA(0, 0, 0, 90));
    pa_fill_circle(c, cx, cy, rad, pa_shade(base, -0.55f));
    PA_Paint g = pa_linear(cx, cy - rad, cx, cy + rad);
    pa_stop(&g, 0, pa_shade(base, 0.3f)); pa_stop(&g, 1, pa_shade(base, -0.12f));
    pa_fill_ellipse_paint(c, cx, cy, rad - 4 * u, rad - 4 * u, &g);
    pa_fill_ellipse(c, cx, cy - rad * 0.42f, rad * 0.62f, rad * 0.3f, PA_RGBA(255, 255, 255, 60));
    PA_Color ic = PA_RGB(255, 255, 255), sh = pa_alpha(pa_shade(base, -0.6f), 0.6f);
    float ir = rad * 0.5f;
    for (int pass = 0; pass < 2; pass++) {
        float oy = pass ? 0 : 3 * u;
        PA_Color col = pass ? ic : sh;
        if (icon == 0) icon_menu(c, cx, cy + oy, ir, col);
        else if (icon == 1) icon_retry(c, cx, cy + oy, ir * 0.9f, col);
        else if (icon == 2) icon_play(c, cx + ir * 0.1f, cy + oy, ir, col);
        else icon_back(c, cx, cy + oy, ir, col);
    }
}

static void btn_wide(PA_Canvas *c, Rc r, const char *label, PA_Color base, int icon, float u, float scale) {
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    float w = r.w * scale, h = r.h * scale;
    float x = cx - w * 0.5f, y = cy - h * 0.5f, rad = h * 0.32f;
    pa_round_rect(c, x, y + 6 * u, w, h, rad, PA_RGBA(0, 0, 0, 90));
    pa_round_rect(c, x, y, w, h, rad, pa_shade(base, -0.55f));
    PA_Paint g = pa_linear(0, y, 0, y + h);
    pa_stop(&g, 0, pa_shade(base, 0.3f)); pa_stop(&g, 1, pa_shade(base, -0.15f));
    pa_round_rect_paint(c, x + 4 * u, y + 4 * u, w - 8 * u, h - 8 * u, rad - 3 * u, &g);
    pa_round_rect(c, x + 12 * u, y + 8 * u, w - 24 * u, h * 0.3f, h * 0.15f, PA_RGBA(255, 255, 255, 55));
    float ts = h * 0.42f;
    float tw = pa_text_width(label, ts, 2 * u);
    float tx = cx - (icon >= 0 ? h * 0.22f : 0);
    pa_text_bold(c, label, tx, cy - ts * 0.5f, ts, PA_RGB(255, 255, 255), pa_shade(base, -0.65f), PA_ALIGN_CENTER, 2 * u, 1.6f);
    if (icon == 2) icon_play(c, tx + tw * 0.5f + h * 0.32f, cy, h * 0.3f, PA_RGB(255, 255, 255));
    if (icon == 1) icon_retry(c, tx + tw * 0.5f + h * 0.36f, cy, h * 0.2f, PA_RGB(255, 255, 255));
}

/* --------------------------------------------------------------- results - */
static void draw_results(PA_Canvas *c) {
    float u = ui_u(), W = (float)c->w, H = (float)c->h, t = S.res_t;
    char buf[48];
    pa_hub_hide_pause();
    PA_Paint ov = pa_radial(W * 0.5f, H * 0.45f, H * 0.1f, W * 0.7f);
    float oa = pa_clamp01(t * 3);
    pa_stop(&ov, 0, PA_RGBA(20, 30, 60, (int)(150 * oa))); pa_stop(&ov, 1, PA_RGBA(6, 8, 20, (int)(215 * oa)));
    pa_fill_rect_paint(c, 0, 0, W, H, &ov);
    Rc m, r, n;
    result_buttons(&m, &r, &n);
    float bt = pa_clamp01((t - 0.5f) * 3);
    if (S.phase == PH_WIN) {
        float s = t < 0.25f ? 0.5f + t / 0.25f * 0.6f : (t < 0.4f ? 1.1f - (t - 0.25f) / 0.15f * 0.1f : 1.0f);
        pa_text_bold(c, "LEVEL CLEARED!", W * 0.5f, 28 * u, 46 * u * s, PA_RGB(255, 255, 255), PA_RGB(40, 18, 8), PA_ALIGN_CENTER, 3 * u, 2.3f);
        snprintf(buf, sizeof buf, "%s  -  LEVEL %d", THEME_NAME[S.theme], S.level + 1);
        pa_text_bold(c, buf, W * 0.5f, 86 * u, 15 * u, PA_RGB(255, 214, 110), PA_RGB(30, 14, 6), PA_ALIGN_CENTER, 1.5f * u, 1.2f);
        /* Score count-up; each star lands as the count passes it. */
        float k = pa_clamp01((t - 0.35f) / 1.6f);
        k = 1.0f - (1.0f - k) * (1.0f - k);
        float shown = (float)S.score * k;
        int thr[3] = { 1, S.t2, S.t3 };
        float sx[3] = { W * 0.5f - 118 * u, W * 0.5f, W * 0.5f + 118 * u };
        float sy[3] = { 178 * u, 160 * u, 178 * u };
        float sr[3] = { 42 * u, 56 * u, 42 * u };
        float rot[3] = { -0.18f, 0.0f, 0.18f };
        for (int i = 0; i < 3; i++) {
            int got = i < S.stars && shown >= (float)thr[i] && t > 0.45f;
            draw_star(c, sx[i], sy[i], sr[i], rot[i], 0, u);
            if (got) {
                float st = (float)thr[i] <= 1 ? 0.45f + 0.0f : 0.35f + 1.6f * (1.0f - sqrtf(1.0f - pa_clamp01((float)thr[i] / (float)(S.score > 0 ? S.score : 1))));
                float age = t - st;
                if (age < 0) age = 0;
                float pop = age < 0.18f ? 1.6f - age / 0.18f * 0.7f : (age < 0.3f ? 0.9f + (age - 0.18f) / 0.12f * 0.1f : 1.0f);
                draw_star(c, sx[i], sy[i], sr[i] * pop, rot[i], 1, u);
                if (age < 0.6f) {
                    for (int q = 0; q < 8; q++) {
                        float a = (float)q / 8.0f * PA_TAU + (float)i;
                        float d = sr[i] * (1.0f + age * 2.4f);
                        pa_fill_circle(c, sx[i] + cosf(a) * d, sy[i] + sinf(a) * d, 4 * u * (1 - age / 0.6f), PA_RGBA(255, 240, 150, 230));
                    }
                }
            }
        }
        pa_text_bold(c, "SCORE", W * 0.5f, 240 * u, 18 * u, PA_RGB(200, 220, 255), PA_RGB(10, 14, 30), PA_ALIGN_CENTER, 3 * u, 1.2f);
        fmt_score(buf, sizeof buf, (int)shown);
        pa_text_bold(c, buf, W * 0.5f, 266 * u, 46 * u, PA_RGB(255, 255, 255), PA_RGB(10, 14, 30), PA_ALIGN_CENTER, 2.5f * u, 2.2f);
        if (S.newbest && k >= 1.0f) {
            float pulse = 1.0f + 0.05f * sinf(t * 8);
            float rw = 250 * u * pulse, rh = 32 * u * pulse;
            pa_round_rect(c, W * 0.5f - rw * 0.5f, 330 * u, rw, rh, 8 * u, pa_hex(0xE23A3A));
            pa_text_bold(c, "NEW HIGHSCORE!", W * 0.5f, 330 * u + rh * 0.5f - 9 * u * pulse, 18 * u * pulse, PA_RGB(255, 255, 255), PA_RGB(90, 10, 10), PA_ALIGN_CENTER, 1.5f * u, 1.3f);
        } else {
            char b2[32];
            fmt_score(b2, sizeof b2, S.best);
            snprintf(buf, sizeof buf, "HIGHSCORE %s", b2);
            pa_text_bold(c, buf, W * 0.5f, 336 * u, 16 * u, PA_RGB(255, 214, 110), PA_RGB(10, 14, 30), PA_ALIGN_CENTER, 1.5f * u, 1.2f);
        }
        float s2 = 0.6f + 0.4f * bt;
        if (bt > 0) {
            Rc mm = m, rr = r;
            btn_round(c, mm, 0, pa_hex(0xF5A623), u * s2);
            btn_round(c, rr, 1, pa_hex(0xF5A623), u * s2);
            float pulse = 1.0f + 0.04f * sinf(t * 6);
            btn_wide(c, n, S.level + 1 < NLEVELS ? "NEXT" : "MAP", pa_hex(0x5CC234), 2, u, pulse * s2);
        }
    } else {
        pa_text_bold(c, "LEVEL FAILED", W * 0.5f, 28 * u, 46 * u, PA_RGB(255, 255, 255), PA_RGB(40, 10, 10), PA_ALIGN_CENTER, 3 * u, 2.3f);
        float bob = sinf(t * 7) * 6 * u;
        float pr = 78 * u * pa_clamp01(t * 3);
        if (pr > 1) draw_pig(c, W * 0.5f, H * 0.43f + bob, pr, sinf(t * 7) * 0.08f, PIG_M, 0, 1, 0, 0, 1);
        snprintf(buf, sizeof buf, "%d PIG%s STILL STANDING", S.pigs_alive, S.pigs_alive == 1 ? "" : "S");
        pa_text_bold(c, buf, W * 0.5f, H * 0.43f + 96 * u, 18 * u, PA_RGB(255, 214, 110), PA_RGB(10, 14, 30), PA_ALIGN_CENTER, 1.5f * u, 1.2f);
        if (bt > 0) {
            btn_round(c, m, 0, pa_hex(0xF5A623), u);
            btn_wide(c, r, "RETRY", pa_hex(0x5CC234), 1, u, (1.0f + 0.04f * sinf(t * 6)) * (0.6f + 0.4f * bt));
        }
    }
}

/* Room wipe: a bank of cloud rolls across. */
static void draw_wipe(PA_Canvas *c) {
    float W = (float)c->w, H = (float)c->h, u = ui_u();
    float w = S.wipe;
    float L = W * (2.0f - 4.0f * w), R = L + 1.9f * W;
    PA_Color cc = PA_RGB(246, 250, 255);
    if (R > 0 && L < W) {
        pa_fill_rect(c, L, 0, R - L, H, cc);
        for (int k = 0; k < 9; k++) {
            float yy = H * ((float)k + 0.5f) / 9.0f;
            float rr = (54 + (float)((k * 29) % 30)) * u;
            pa_fill_circle(c, L, yy, rr, cc);
            pa_fill_circle(c, R, yy + 20 * u, rr, cc);
            pa_fill_circle(c, L - rr * 0.6f, yy + 26 * u, rr * 0.6f, PA_RGB(225, 236, 248));
        }
    }
}

/* ---------------------------------------------------------------- title --- */
static void draw_title(PA_Canvas *c) {
    float u = ui_u(), W = (float)c->w, H = (float)c->h, t = S.scr_t;
    draw_background(c, TH_JUNGLE, S.cx, S.z, S.time);
    draw_ground(c, TH_JUNGLE);
    draw_world(c);
    draw_foreground(c, TH_JUNGLE, S.time);
    PA_Paint top = pa_linear(0, 0, 0, H * 0.45f);
    pa_stop(&top, 0, PA_RGBA(10, 30, 70, 120)); pa_stop(&top, 1, PA_RGBA(10, 30, 70, 0));
    pa_fill_rect_paint(c, 0, 0, W, H * 0.45f, &top);
    /* Key art: the flock charging in from the left, a pig smirking right. */
    float bob = sinf(t * 2.2f) * 5 * u;
    draw_bird(c, BT_BOMB, W * 0.075f, H * 0.56f - bob, 66 * u, 0.15f, 0, 0);
    draw_bird(c, BT_BLUE, W * 0.245f, H * 0.36f + bob, 22 * u, 0.3f, 0, 0);
    draw_bird(c, BT_BLUE, W * 0.285f, H * 0.43f - bob, 20 * u, 0.25f, 0, 0);
    draw_bird(c, BT_RED, W * 0.17f, H * 0.72f + bob, 94 * u, 0.12f, fmodf(t, 3.3f) < 0.12f, 0);
    draw_bird(c, BT_YELLOW, W * 0.31f, H * 0.86f - bob * 0.6f, 50 * u, 0.2f, 0, 0);
    draw_pig(c, W * 0.87f, H * 0.66f + bob * 0.5f, 84 * u, -0.1f, PIG_M | 8, -1.0f, 1.0f, fmodf(t, 2.7f) < 0.12f, 0, 0);
    draw_pig(c, W * 0.75f, H * 0.80f - bob * 0.5f, 46 * u, 0.15f, PIG_S, -1.0f, 1.0f, 0, 0, 0);
    /* Logo. */
    float s = t < 0.35f ? 0.6f + t / 0.35f * 0.5f : (t < 0.5f ? 1.1f - (t - 0.35f) / 0.15f * 0.1f : 1.0f);
    float ls = 92 * u * s;
    pa_text_bold(c, "AVIAN", W * 0.5f + 4 * u, 34 * u + 6 * u, ls, PA_RGB(214, 40, 36), PA_RGB(214, 40, 36), PA_ALIGN_CENTER, 6 * u, 2.6f);
    pa_text_bold(c, "AVIAN", W * 0.5f, 34 * u, ls, PA_RGB(255, 255, 255), PA_RGB(42, 14, 8), PA_ALIGN_CENTER, 6 * u, 2.6f);
    pa_text_bold(c, "ARTILLERY", W * 0.5f, 34 * u + ls * 1.12f, 40 * u * s, PA_RGB(255, 210, 50), PA_RGB(42, 14, 8), PA_ALIGN_CENTER, 5 * u, 2.2f);
    Rc pb = play_button();
    float pulse = 1.0f + 0.05f * sinf(t * 5);
    btn_wide(c, pb, "PLAY", pa_hex(0x5CC234), 2, u, pulse * pa_clamp01(t * 3));
    int total = 0;
    for (int i = 0; i < NLEVELS; i++) total += S.progress_stars[i];
    char buf[32];
    snprintf(buf, sizeof buf, "%d/%d", total, NLEVELS * 3);
    draw_star(c, W * 0.5f - 46 * u, H * 0.70f + 70 * u, 14 * u, 0, 1, u);
    pa_text_bold(c, buf, W * 0.5f - 26 * u, H * 0.70f + 60 * u, 20 * u, PA_RGB(255, 255, 255), PA_RGB(16, 26, 40), PA_ALIGN_LEFT, 1.5f * u, 1.4f);
}

/* ------------------------------------------------------------- level map -- */
static void draw_lock(PA_Canvas *c, float x, float y, float s) {
    pa_stroke_circle(c, x, y - s * 0.3f, s * 0.36f, s * 0.14f, PA_RGB(220, 226, 236));
    pa_round_rect(c, x - s * 0.5f, y - s * 0.25f, s, s * 0.8f, s * 0.15f, PA_RGB(230, 236, 244));
    pa_fill_circle(c, x, y + s * 0.1f, s * 0.12f, PA_RGB(90, 98, 112));
}

static void draw_select(PA_Canvas *c) {
    float u = ui_u(), W = (float)c->w, H = (float)c->h, t = S.scr_t;
    g_gy = H - ground_pad(W, H);
    draw_background(c, TH_JUNGLE, t * 0.6f, 50.0f, S.time);
    pa_fill_rect(c, 0, g_gy, W, H - g_gy, pa_hex(0x55A82B));
    pa_fill_rect(c, 0, 0, W, H, PA_RGBA(8, 16, 40, 120));
    static const uint32_t TC[4] = { 0x4CB848, 0xF08A3A, 0x7A86E0, 0xD8382E };
    char buf[32];
    pa_text_bold(c, "SELECT LEVEL", W * 0.5f, 18 * u, 32 * u, PA_RGB(255, 255, 255), PA_RGB(20, 16, 30), PA_ALIGN_CENTER, 3 * u, 2.0f);
    Rc back = { 42 * u - 30 * u, 40 * u - 30 * u, 60 * u, 60 * u };
    btn_round(c, back, 3, pa_hex(0xF5A623), u);
    int total = 0;
    for (int i = 0; i < NLEVELS; i++) total += S.progress_stars[i];
    snprintf(buf, sizeof buf, "%d/%d", total, NLEVELS * 3);
    draw_star(c, W - 210 * u, 38 * u, 17 * u, 0, 1, u);
    pa_text_bold(c, buf, W - 186 * u, 26 * u, 22 * u, PA_RGB(255, 255, 255), PA_RGB(16, 26, 40), PA_ALIGN_LEFT, 1.5f * u, 1.4f);
    for (int row = 0; row < 4; row++) {
        Rc r0; select_layout(row * 5, &r0);
        float lx = r0.x - 238 * u, ly = r0.y + 6 * u;
        PA_Color tc = pa_hex(TC[row]);
        pa_round_rect(c, lx, ly + 4 * u, 216 * u, 72 * u, 14 * u, PA_RGBA(0, 0, 0, 80));
        PA_Paint g = pa_linear(0, ly, 0, ly + 72 * u);
        pa_stop(&g, 0, pa_shade(tc, 0.15f)); pa_stop(&g, 1, pa_shade(tc, -0.25f));
        pa_round_rect_paint(c, lx, ly, 216 * u, 72 * u, 14 * u, &g);
        pa_text_bold(c, THEME_NAME[row], lx + 108 * u, ly + 16 * u, 17 * u, PA_RGB(255, 255, 255), pa_shade(tc, -0.6f), PA_ALIGN_CENTER, 1.2f * u, 1.3f);
        snprintf(buf, sizeof buf, "LEVELS %d-%d", row * 5 + 1, row * 5 + 5);
        pa_text(c, buf, lx + 108 * u, ly + 44 * u, 12 * u, PA_RGBA(255, 255, 255, 220), PA_ALIGN_CENTER, 1.5f * u);
        for (int k = 0; k < 5; k++) {
            int L = row * 5 + k;
            Rc r; select_layout(L, &r);
            int locked = L > S.unlocked;
            int cur = L == S.unlocked;
            float pop = pa_clamp01(t * 4 - (float)L * 0.06f);
            float s = pop < 1 ? pa_smooth(pop) : 1.0f;
            if (cur) s *= 1.0f + 0.05f * sinf(S.time * 5);
            float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f, w = r.w * s, h = r.h * s;
            if (s < 0.05f) continue;
            pa_round_rect(c, cx - w * 0.5f, cy - h * 0.5f + 5 * u, w, h, 16 * u * s, PA_RGBA(0, 0, 0, 90));
            pa_round_rect(c, cx - w * 0.5f, cy - h * 0.5f, w, h, 16 * u * s, locked ? PA_RGB(90, 96, 110) : PA_RGB(255, 255, 255));
            PA_Paint tg = pa_linear(0, cy - h * 0.5f, 0, cy + h * 0.5f);
            if (locked) { pa_stop(&tg, 0, PA_RGB(128, 136, 152)); pa_stop(&tg, 1, PA_RGB(92, 98, 114)); }
            else { pa_stop(&tg, 0, pa_shade(tc, 0.22f)); pa_stop(&tg, 1, pa_shade(tc, -0.2f)); }
            pa_round_rect_paint(c, cx - w * 0.5f + 4 * u * s, cy - h * 0.5f + 4 * u * s, w - 8 * u * s, h - 8 * u * s, 12 * u * s, &tg);
            if (locked) { draw_lock(c, cx, cy - 2 * u, 26 * u * s); continue; }
            snprintf(buf, sizeof buf, "%d", L + 1);
            pa_text_bold(c, buf, cx, cy - 26 * u * s, 30 * u * s, PA_RGB(255, 255, 255), pa_shade(tc, -0.65f), PA_ALIGN_CENTER, 1.5f * u, 1.8f);
            for (int q = 0; q < 3; q++)
                draw_star(c, cx + (float)(q - 1) * 22 * u * s, cy + 24 * u * s - (q == 1 ? 3 * u : 0), 9 * u * s, 0, q < S.progress_stars[L], u * 0.5f);
        }
    }
}

/* ---------------------------------------------------------------- render -- */
static void s_render(PA_Canvas *c) {
    if (c->w != S.W || c->h != S.H) {
        S.W = c->w; S.H = c->h;
        if (S.screen != SC_SELECT) { compute_fit(); S.z = S.fit_z; S.cx = S.fit_cx; }
    }
    g_gy = (float)c->h - ground_pad((float)c->w, (float)c->h);
    float u = ui_u();
    g_shx = sinf(S.time * 53.0f) * S.shake * 9 * u;
    g_shy = cosf(S.time * 41.0f) * S.shake * 7 * u;
    if (S.screen == SC_TITLE) { g_shx = g_shy = 0; draw_title(c); return; }
    if (S.screen == SC_SELECT) { draw_select(c); return; }

    pa_hub_hide_pause();
    draw_background(c, S.theme, S.cx, S.z, S.time);
    draw_ground(c, S.theme);
    draw_world(c);
    draw_foreground(c, S.theme, S.time);
    if (S.phase == PH_WIPE) draw_wipe(c);
    draw_hud(c);
    if (S.phase == PH_WIN || S.phase == PH_FAIL) draw_results(c);
}

/* ----------------------------------------------------------------- thumb -- */
static void s_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    PA_Paint sky = pa_linear(0, y, 0, y + h);
    pa_stop(&sky, 0, pa_hex(0x2B93E0)); pa_stop(&sky, 0.7f, pa_hex(0x8AD4F4)); pa_stop(&sky, 1, pa_hex(0xD4F3FA));
    pa_fill_rect_paint(c, x, y, w, h, &sky);
    pa_fill_circle(c, x + w * 0.8f, y + h * 0.2f, h * 0.09f, PA_RGBA(255, 255, 230, 230));
    float gy = y + h * 0.8f;
    pa_fill_ellipse(c, x + w * 0.25f, gy, w * 0.45f, h * 0.2f, pa_hex(0x4FA58C));
    pa_fill_ellipse(c, x + w * 0.8f, gy, w * 0.4f, h * 0.16f, pa_hex(0x3E9478));
    pa_fill_rect(c, x, gy, w, h * 0.2f, pa_hex(0x6B4428));
    pa_fill_rect(c, x, gy, w, h * 0.05f, pa_hex(0x8FD64A));
    float k = h / 200.0f;
    float bx = x + w * 0.68f;
    draw_block_raw(c, M_WOOD, 0, bx - 24 * k, gy - 22 * k, 4 * k, 22 * k, 0, 1, 7, 0);
    draw_block_raw(c, M_WOOD, 0, bx + 24 * k, gy - 22 * k, 4 * k, 22 * k, 0, 1, 9, 0);
    draw_block_raw(c, M_WOOD, 0, bx, gy - 48 * k, 32 * k, 4 * k, 0, 1, 11, 0);
    draw_block_raw(c, M_ICE, 1, bx, gy - 63 * k, 11 * k, 11 * k, 0, 1, 13, 0);
    draw_pig(c, bx, gy - 13 * k, 13 * k, 0, PIG_M, -1, 1, fmodf(t, 2.5f) < 0.1f, 0, 0);
    draw_pig(c, bx, gy - 84 * k, 10 * k, 0, PIG_S, -1, 1, 0, 0, 0);
    /* Bird on a looping arc. */
    float p = pa_wrapf(t * 0.45f, 1.0f);
    float sx0 = x + w * 0.16f, sy0 = gy - 40 * k;
    for (int i = 0; i < 12; i++) {
        float q = p * (float)i / 12.0f;
        pa_fill_circle(c, sx0 + (bx - 30 * k - sx0) * q, sy0 - sinf(q * PA_PI) * h * 0.4f, (i % 2 ? 2.0f : 3.0f) * k, PA_RGBA(255, 255, 255, 220));
    }
    pa_line(c, sx0, gy, sx0, sy0 + 6 * k, 5 * k, pa_hex(0x7A4018));
    pa_line(c, sx0, sy0 + 6 * k, sx0 - 6 * k, sy0 - 8 * k, 4 * k, pa_hex(0x7A4018));
    pa_line(c, sx0, sy0 + 6 * k, sx0 + 6 * k, sy0 - 8 * k, 4 * k, pa_hex(0x7A4018));
    float fx = sx0 + (bx - 30 * k - sx0) * p, fy = sy0 - sinf(p * PA_PI) * h * 0.4f;
    draw_bird(c, BT_RED, fx, fy, 13 * k, -cosf(p * PA_PI) * 0.6f, 0, 0);
}

const PA_Game PA_GAME_AVIAN = {
    "avian", "Avian Artillery", "Physics",
    "Sling the birds, topple the towers, pop every pig.",
    PA_RGB(230, 50, 50),
    s_start, s_stop, s_update, s_render, s_thumb
};
