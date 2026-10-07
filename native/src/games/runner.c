/* ===========================================================================
   ROOFTOP RUN - native port
   Three tracks, a chase camera riding just behind and above the runner, and a
   city that never stops coming. Swipe across to hop a lane, up to jump, down
   to roll. Trains are walls unless a ramp gets you onto their roofs; barriers
   are jumped or rolled under; a stumble brings the inspector running.

   Everything in the scene is real 3D projected through one pitched camera:
   boxes and quads are clipped against the near plane and painted far to near,
   and the runner is a jointed rig of shaded capsules, so a run cycle, a tuck
   roll and a lane-change lean are poses rather than sprites.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------ world scale */
#define LANE_W        2.6f
#define TRACK_HALF    (1.5f * LANE_W + 0.55f)
#define FACADE_X      8.4f
#define TRAIN_W       2.36f
#define TRAIN_H       3.4f
#define CAR_LEN       9.0f
#define CAR_GAP       0.55f
#define RAMP_LEN      6.0f
#define LOW_H         1.2f
#define HIGH_Y0       1.55f
#define HIGH_Y1       2.75f
#define BODY_H        2.05f
#define ROLL_H        1.0f
#define GRAVITY       44.0f
#define JUMP_H        1.95f
#define SNEAK_H       4.7f
#define JET_Y         10.6f
#define GANTRY_EVERY  34.0f
#define GANTRY_H      9.6f
#define LAMP_EVERY    23.0f
#define AHEAD         175.0f
#define ACT_D         92.0f
#define CHASE_WINDOW  6.0f
#define ROLL_TIME     0.62f

#define MAX_OBS    110
#define MAX_COINS  420
#define MAX_PICK   12
#define MAX_PART   220
#define MAX_BLD    52

enum { OB_TRAIN, OB_LOW, OB_HIGH, OB_SIGNAL };
enum { PK_MAGNET, PK_JET, PK_SNEAK, PK_MULT, PK_KEY, PK_BOARD };
enum { PT_SPARK, PT_DUST, PT_FLAME, PT_STAR, PT_CONFETTI, PT_RING };
enum { ST_MENU, ST_RUN, ST_CRASH, ST_REVIVE, ST_RESULTS };
enum { CR_TRAIN, CR_TRIP, CR_CAUGHT };

typedef struct { float x, y, z; } V3;

typedef struct {
    int   kind, lane, cars, ramp, style, moving, active;
    float z, len, v, zprev;
} Obs;

typedef struct { float x, y, z; int pulled; } Coin;
typedef struct { int kind; float x, y, z; } Pick;
typedef struct { float x, y, z, vx, vy, vz, t, life, size, spin; PA_Color col; int kind; } Part;
typedef struct { float z0, z1, h, depth; int style, gable, shop, sign, world, variant; uint32_t seed; } Bld;

typedef struct {
    int    st;
    float  st_t, time, run_t;
    PA_Rng rng, fxrng;
    /* runner */
    float  z, zprev, speed, dist;
    int    lane, from_lane;
    float  x, xv, y, vy, ground;
    int    air;
    float  roll_t, phase, land_t, hop_t, stagger, stumble_t, invuln;
    /* power-ups */
    float  magnet_t, jet_t, sneak_t, mult_t, glide;
    /* chaser */
    float  chase_d, chase_x, chase_phase;
    /* scoring */
    float  score;
    int    coins, level, revives, new_best, keys_found;
    /* crash */
    int    crash_kind;
    float  crash_z, fall_dir;
    /* world */
    Obs    obs[MAX_OBS];   int nobs;
    Coin   coin[MAX_COINS]; int ncoin;
    Pick   pick[MAX_PICK]; int npick;
    Part   part[MAX_PART]; int npart;
    Bld    bld[2][MAX_BLD]; int nbld[2]; float bld_z[2];
    float  gen_z; int gen_n; float pu_next; int pu_order; float key_next;
    /* camera */
    float  cam_x, cam_gy, cam_blend, cam_air, cam_crash;
    float  shake, flash; PA_Color flash_col;
    char   banner[32]; float banner_t; PA_Color banner_col;
    float  coin_pop, streak_t; int streak;
    float  bot_t, jet_sfx;
    float  pack_p, pack_pv, pack_r, pack_rv;
    float  board_t, tap_t;
    V3     trail[24]; int ntrail; float trail_t;
    int    demo_script;
} Game;

static Game G;
static int  g_loaded, g_best, g_bank, g_keys, g_runs, g_boards;
static int  g_mute, g_force_bot;
static struct { int w, h; float u; } L = { 540, 1170, 1.0f };

/* -------------------------------------------------------------- helpers */
static float rr(float a, float b) { return pa_rng_range(&G.rng, a, b); }
static int   ri(int a, int b)     { return pa_rng_int(&G.rng, a, b); }
static int   rc(float p)          { return pa_rng_chance(&G.rng, p); }
/* Cosmetic randomness has its own stream so effects never shift the level. */
static float fr(float a, float b) { return pa_rng_range(&G.fxrng, a, b); }
static float lane_x(int l)        { return ((float)l - 1.0f) * LANE_W; }
/* Which theme a stretch of line belongs to: Old Town, then Canyon Town from
   120 m, alternating every 400 m after that. */
static int world_at(float z)      { if (z < 120.0f) return 0; return (((int)floorf((z - 120.0f) / 400.0f)) & 1) ? 0 : 1; }
static float canyon_amt(float z);
static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }
static float train_len(int cars)  { return (float)cars * CAR_LEN + (float)(cars - 1) * CAR_GAP; }

static void snd_tone(float a, float b, float s, int sh, float g) { if (!g_mute) pa_tone(a, b, s, sh, g); }
static void snd_noise(float s, float g) { if (!g_mute) pa_noise(s, g); }

static void banner(const char *t, PA_Color col) {
    snprintf(G.banner, sizeof(G.banner), "%s", t);
    G.banner_t = 1.5f;
    G.banner_col = col;
}

static void load_save(void) {
    if (g_loaded) return;
    g_loaded = 1;
    g_best = pa_save_get("runner.best", 0);
    g_bank = pa_save_get("runner.coins", 0);
    g_keys = pa_save_get("runner.keys", 3);
    g_runs = pa_save_get("runner.runs", 0);
    g_boards = pa_save_get("runner.boards", 3);
}

static void star_shape(PA_Canvas *c, float x, float y, float r, float inner, float rot, PA_Color col);

/* =========================================================== CAMERA ===== */
static struct {
    float x, y, z, yaw, pitch;
    float cyaw, syaw, cp, sp;
    float F, cx, cy;
    float ox, oy, w, h;
    PA_Color haze;
} K;

#define NEAR_Z 0.22f

static void cam_xf(float x, float y, float z, V3 *o) {
    float dx = x - K.x, dy = y - K.y, dz = z - K.z;
    float xr = dx * K.cyaw - dz * K.syaw;
    float zh = dx * K.syaw + dz * K.cyaw;
    o->x = xr;
    o->y = zh * K.sp + dy * K.cp;
    o->z = zh * K.cp - dy * K.sp;
}

static PA_Vec2 scr(const V3 *v) {
    PA_Vec2 s;
    float k = K.F / v->z;
    s.x = K.cx + v->x * k;
    s.y = K.cy - v->y * k;
    return s;
}

static int proj(float x, float y, float z, PA_Vec2 *o, float *d) {
    V3 v;
    cam_xf(x, y, z, &v);
    if (v.z < NEAR_Z) return 0;
    *o = scr(&v);
    if (d) *d = v.z;
    return 1;
}

static float depth_of(float x, float y, float z) { V3 v; cam_xf(x, y, z, &v); return v.z; }

static PA_Color fogc(PA_Color c, float d) {
    float t = pa_clamp01((d - 40.0f) / 115.0f);
    t = t * t * (3.0f - 2.0f * t) * 0.94f;
    if (t <= 0.0f) return c;
    PA_Color m = pa_mix(c | 0xFF000000u, K.haze, t);
    return (m & 0x00FFFFFFu) | (c & 0xFF000000u);
}

/* Screen-space fills with the scissor narrowed to the shape's own columns:
   the scanline filler walks the whole clip width on every row it touches, so
   hundreds of small far polygons would otherwise each cost a full screen row. */
static void narrow(PA_Canvas *c, float x0, float x1, int *k0, int *k1) {
    *k0 = c->clip_x0; *k1 = c->clip_x1;
    x0 = pa_clampf(x0, -100000.0f, 100000.0f);
    x1 = pa_clampf(x1, -100000.0f, 100000.0f);
    int a = (int)floorf(x0) - 1, b = (int)ceilf(x1) + 2;
    if (a > c->clip_x0) c->clip_x0 = a;
    if (b < c->clip_x1) c->clip_x1 = b;
}

static void fpoly(PA_Canvas *c, const PA_Vec2 *p, int n, PA_Color col) {
    if (n < 3 || PA_A(col) == 0) return;
    float x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y;
    for (int i = 1; i < n; i++) {
        x0 = minf(x0, p[i].x); x1 = maxf(x1, p[i].x);
        y0 = minf(y0, p[i].y); y1 = maxf(y1, p[i].y);
    }
    if (x1 < (float)c->clip_x0 || x0 > (float)c->clip_x1 || y1 < (float)c->clip_y0 || y0 > (float)c->clip_y1) return;
    int k0, k1;
    narrow(c, x0, x1, &k0, &k1);
    if (c->clip_x1 > c->clip_x0) pa_fill_poly(c, p, n, col);
    c->clip_x0 = k0; c->clip_x1 = k1;
}

static void fell(PA_Canvas *c, float x, float y, float rx, float ry, PA_Color col) {
    if (rx < 0.3f || ry < 0.3f || PA_A(col) == 0) return;
    if (x + rx < (float)c->clip_x0 || x - rx > (float)c->clip_x1 ||
        y + ry < (float)c->clip_y0 || y - ry > (float)c->clip_y1) return;
    int k0, k1;
    narrow(c, x - rx, x + rx, &k0, &k1);
    if (c->clip_x1 > c->clip_x0) pa_fill_ellipse(c, x, y, rx, ry, col);
    c->clip_x0 = k0; c->clip_x1 = k1;
}

static void fcirc(PA_Canvas *c, float x, float y, float r, PA_Color col) { fell(c, x, y, r, r, col); }

static void glow(PA_Canvas *c, float x, float y, float r, PA_Color col, float a) {
    if (r < 1.0f) return;
    PA_Paint p = pa_radial(x, y, 0.0f, r);
    pa_stop(&p, 0.0f, pa_alpha(col, a));
    pa_stop(&p, 0.45f, pa_alpha(col, a * 0.45f));
    pa_stop(&p, 1.0f, pa_alpha(col, 0.0f));
    int k0, k1;
    narrow(c, x - r, x + r, &k0, &k1);
    if (c->clip_x1 > c->clip_x0) pa_fill_ellipse_paint(c, x, y, r, r, &p);
    c->clip_x0 = k0; c->clip_x1 = k1;
}

/* One warm key light from the upper left (slightly toward the camera), and a
   cool violet tint in whatever faces away from it. Every world polygon goes
   through this, so trains, buildings, props and the rig's boxes all agree. */
static int g_unlit;
#define KEY_X (-0.50f)
#define KEY_Y ( 0.72f)
#define KEY_Z (-0.48f)

static PA_Color lightc(PA_Color c, float d) {
    PA_Color a = c & 0xFF000000u, o = c | 0xFF000000u;
    if (d >= 0.0f) {
        o = pa_mix(o, pa_hex(0xFFD9A0), 0.26f * d);
        o = pa_shade(o, 0.05f * d);
    } else {
        float k = pa_clamp01(-d / 0.55f);
        o = pa_shade(o, -0.14f * k);
        o = pa_mix(o, pa_hex(0x6A4C8C), 0.30f * k);
    }
    return (o & 0x00FFFFFFu) | a;
}

static void fpoly_grad(PA_Canvas *c, const PA_Vec2 *p, int n, PA_Vec2 a, PA_Vec2 b, PA_Color ca, PA_Color cb) {
    if (n < 3) return;
    float x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y;
    for (int i = 1; i < n; i++) {
        x0 = minf(x0, p[i].x); x1 = maxf(x1, p[i].x);
        y0 = minf(y0, p[i].y); y1 = maxf(y1, p[i].y);
    }
    if (x1 < (float)c->clip_x0 || x0 > (float)c->clip_x1 || y1 < (float)c->clip_y0 || y0 > (float)c->clip_y1) return;
    PA_Paint pt = pa_linear(a.x, a.y, b.x, b.y);
    pa_stop(&pt, 0.0f, ca);
    pa_stop(&pt, 1.0f, cb);
    int k0, k1;
    narrow(c, x0, x1, &k0, &k1);
    if (c->clip_x1 > c->clip_x0) pa_fill_poly_paint(c, p, n, &pt);
    c->clip_x0 = k0; c->clip_x1 = k1;
}

/** World polygon: lit by its normal, near-plane clipped, projected, fogged by
    its mean depth. Tall vertical faces get a top-to-bottom gradient that
    darkens into the ground, which is most of what reads as solid. */
static void wpoly(PA_Canvas *c, const V3 *p, int n, PA_Color col) {
    V3 cs[8], out[16];
    int m = 0;
    if (n > 8) n = 8;
    float nx = 0, ny = 0, nz = 0, ymin = p[0].y, ymax = p[0].y, xm = 0, zm = 0;
    if (!g_unlit && n >= 3) {
        float ax = p[1].x - p[0].x, ay = p[1].y - p[0].y, az = p[1].z - p[0].z;
        float bx = p[2].x - p[0].x, by = p[2].y - p[0].y, bz = p[2].z - p[0].z;
        if (n >= 4) { bx = p[3].x - p[1].x; by = p[3].y - p[1].y; bz = p[3].z - p[1].z;
                      ax = p[2].x - p[0].x; ay = p[2].y - p[0].y; az = p[2].z - p[0].z; }
        nx = ay * bz - az * by; ny = az * bx - ax * bz; nz = ax * by - ay * bx;
        float l = sqrtf(nx * nx + ny * ny + nz * nz);
        if (l > 1e-6f) {
            nx /= l; ny /= l; nz /= l;
            if (nx * (K.x - p[0].x) + ny * (K.y - p[0].y) + nz * (K.z - p[0].z) < 0.0f) { nx = -nx; ny = -ny; nz = -nz; }
            col = lightc(col, nx * KEY_X + ny * KEY_Y + nz * KEY_Z);
        }
        for (int i = 0; i < n; i++) { ymin = minf(ymin, p[i].y); ymax = maxf(ymax, p[i].y); xm += p[i].x; zm += p[i].z; }
        xm /= (float)n; zm /= (float)n;
    }
    for (int i = 0; i < n; i++) cam_xf(p[i].x, p[i].y, p[i].z, &cs[i]);
    for (int i = 0; i < n; i++) {
        const V3 *a = &cs[i], *b = &cs[(i + 1) % n];
        int ia = a->z >= NEAR_Z, ib = b->z >= NEAR_Z;
        if (ia) out[m++] = *a;
        if (ia != ib) {
            float t = (NEAR_Z - a->z) / (b->z - a->z);
            out[m].x = a->x + (b->x - a->x) * t;
            out[m].y = a->y + (b->y - a->y) * t;
            out[m].z = NEAR_Z;
            m++;
        }
    }
    if (m < 3) return;
    PA_Vec2 s[16];
    float zs = 0.0f;
    for (int k = 0; k < m; k++) { s[k] = scr(&out[k]); zs += out[k].z; }
    float d = zs / (float)m;
    if (!g_unlit && fabsf(ny) < 0.3f && ymax - ymin > 0.9f && PA_A(col) == 255 && d < 120.0f) {
        PA_Vec2 ta, tb; float da, db;
        if (proj(xm, ymax, zm, &ta, &da) && proj(xm, ymin, zm, &tb, &db)) {
            PA_Color top = pa_shade(col, 0.06f);
            PA_Color bot = ymin < 1.0f ? pa_mix(pa_shade(col, -0.10f), pa_hex(0x6A4C8C), 0.22f) : pa_shade(col, -0.07f);
            fpoly_grad(c, s, m, ta, tb, fogc(top, d), fogc(bot, d));
            return;
        }
    }
    fpoly(c, s, m, fogc(col, d));
}

static V3 v3(float x, float y, float z) { V3 r; r.x = x; r.y = y; r.z = z; return r; }

static void wquad(PA_Canvas *c, V3 a, V3 b, V3 d, V3 e, PA_Color col) {
    V3 q[4]; q[0] = a; q[1] = b; q[2] = d; q[3] = e;
    wpoly(c, q, 4, col);
}

/* Quads on the three axis planes, for decals on box faces. */
static void qz(PA_Canvas *c, float z, float x0, float x1, float y0, float y1, PA_Color col) {
    wquad(c, v3(x0, y0, z), v3(x1, y0, z), v3(x1, y1, z), v3(x0, y1, z), col);
}
static void qx(PA_Canvas *c, float x, float z0, float z1, float y0, float y1, PA_Color col) {
    wquad(c, v3(x, y0, z0), v3(x, y0, z1), v3(x, y1, z1), v3(x, y1, z0), col);
}
static void qy(PA_Canvas *c, float y, float x0, float x1, float z0, float z1, PA_Color col) {
    wquad(c, v3(x0, y, z0), v3(x1, y, z0), v3(x1, y, z1), v3(x0, y, z1), col);
}

/** Axis-aligned box: only the faces turned toward the camera, which for a
    convex box never overlap each other. Light comes from the upper left. */
static void wbox(PA_Canvas *c, float x0, float x1, float y0, float y1, float z0, float z1,
                 PA_Color col, PA_Color top) {
    if (K.z < z0) qz(c, z0, x0, x1, y0, y1, col);
    if (K.z > z1) qz(c, z1, x0, x1, y0, y1, col);
    if (K.x < x0) qx(c, x0, z0, z1, y0, y1, col);
    if (K.x > x1) qx(c, x1, z0, z1, y0, y1, col);
    if (K.y > y1) qy(c, y1, x0, x1, z0, z1, top);
    if (K.y < y0) qy(c, y0, x0, x1, z0, z1, pa_shade(col, -0.35f));
}

/** Line between two world points, clipped to the near plane. */
static void wline(PA_Canvas *c, V3 a, V3 b, float width_world, PA_Color col, float min_px) {
    V3 ca, cb;
    cam_xf(a.x, a.y, a.z, &ca);
    cam_xf(b.x, b.y, b.z, &cb);
    if (ca.z < NEAR_Z && cb.z < NEAR_Z) return;
    if (ca.z < NEAR_Z || cb.z < NEAR_Z) {
        float t = (NEAR_Z - ca.z) / (cb.z - ca.z);
        V3 m;
        m.x = ca.x + (cb.x - ca.x) * t; m.y = ca.y + (cb.y - ca.y) * t; m.z = NEAR_Z;
        if (ca.z < NEAR_Z) ca = m; else cb = m;
    }
    PA_Vec2 sa = scr(&ca), sb = scr(&cb);
    float zm = (ca.z + cb.z) * 0.5f;
    float w = maxf(min_px, width_world * K.F / zm);
    float x0 = minf(sa.x, sb.x) - w, x1 = maxf(sa.x, sb.x) + w;
    int k0, k1;
    narrow(c, x0, x1, &k0, &k1);
    if (c->clip_x1 > c->clip_x0) pa_line(c, sa.x, sa.y, sb.x, sb.y, w, fogc(col, zm));
    c->clip_x0 = k0; c->clip_x1 = k1;
}

/* Camera framing. Mode 1 is the run: high and close behind, pitched down so
   the track runs up the screen to a vanishing point near the top third. Mode 0
   is the start screen: in front of the runner at chest height. */
static void cam_frame(float *back, float *high, float *pitch, float *hz, float b) {
    *back  = pa_lerpf(5.4f, 4.3f, b);
    *high  = pa_lerpf(2.5f, 5.5f, b);
    *pitch = pa_lerpf(0.10f, 0.50f, b);
    *hz    = pa_lerpf(0.47f, 0.29f, b);
}

static void cam_setup(float ox, float oy, float w, float h, float shake) {
    float b = pa_smooth(pa_clamp01(G.cam_blend));
    float back, high, pitch, hz;
    cam_frame(&back, &high, &pitch, &hz, b);
    back += G.cam_crash * 1.6f;
    high += G.cam_crash * 0.6f;
    float yaw = PA_PI * (1.0f - b);
    float tx = pa_lerpf(G.x * 0.35f, G.cam_x, b);
    float ty = G.cam_gy + G.cam_air;
    float tz = G.z;
    K.yaw = yaw;
    K.pitch = pitch;
    K.cyaw = cosf(yaw); K.syaw = sinf(yaw);
    K.cp = cosf(pitch); K.sp = sinf(pitch);
    K.x = tx - K.syaw * back;
    K.z = tz - K.cyaw * back;
    K.y = ty + high;
    K.ox = ox; K.oy = oy; K.w = w; K.h = h;
    /* Focal length from the height of the frame, capped by its width so a
       wide window never zooms past three lanes. */
    float lane_cap = 1.38f * w;
    /* Feet land at ~80% of the height: the horizon-to-feet span is F times
       (camera height / cos pitch) over the feet's depth. */
    float zf = back * cosf(pitch) + high * sinf(pitch);
    float span = (high / cosf(pitch)) / zf;
    K.F = minf(lane_cap, (0.80f - hz) * h / span);
    if (b < 1.0f) K.F = pa_lerpf(minf(1.25f * w, 0.62f * h), K.F, b);
    K.cx = ox + w * 0.5f;
    K.cy = oy + h * hz + K.F * tanf(pitch);
    if (shake > 0.0f) {
        K.cx += sinf(G.time * 71.0f) * shake * 9.0f * L.u;
        K.cy += cosf(G.time * 53.0f) * shake * 7.0f * L.u;
    }
    K.haze = pa_mix(pa_hex(0xCFE6F6), pa_hex(0xF9D7B2), canyon_amt(G.z + 60.0f));
}

/* ============================================================ SPAWNING == */
static Obs *add_obs(int kind, int lane, float z) {
    if (G.nobs >= MAX_OBS) return NULL;
    Obs *o = &G.obs[G.nobs++];
    memset(o, 0, sizeof(*o));
    o->kind = kind; o->lane = lane; o->z = z; o->zprev = z;
    return o;
}

static Obs *add_train(int lane, float z, int cars, int ramp, int style) {
    Obs *o = add_obs(OB_TRAIN, lane, z);
    if (!o) return NULL;
    o->cars = cars; o->ramp = ramp; o->style = style;
    o->len = train_len(cars);
    return o;
}

static int pick_style(void) {
    static const int W[] = { 0, 0, 1, 1, 2, 2, 3 };
    return W[ri(0, 6)];
}

static void add_coin(float x, float y, float z) {
    if (G.ncoin >= MAX_COINS) return;
    Coin *c = &G.coin[G.ncoin++];
    c->x = x; c->y = y; c->z = z; c->pulled = 0;
}

static int add_pick(int kind, int lane, float y, float z) {
    if (G.npick >= MAX_PICK) return 0;
    Pick *p = &G.pick[G.npick++];
    p->kind = kind; p->x = lane_x(lane); p->y = y; p->z = z;
    return 1;
}

/* A line of coins in a lane at a height; a due power-up takes the first slot. */
static void coin_line(int lane, float z, int n, float y) {
    int start = 0;
    if (z > G.pu_next) {
        static const int ORDER[] = { PK_MAGNET, PK_SNEAK, PK_JET, PK_MULT, PK_MAGNET, PK_JET, PK_MULT, PK_SNEAK };
        int kind = G.demo_script ? ORDER[G.pu_order % 8] : ORDER[ri(0, 7)];
        if (add_pick(kind, lane, y + 0.25f, z)) {
            G.pu_order++;
            G.pu_next = z + (G.demo_script ? 210.0f : rr(240.0f, 420.0f));
            start = 2;
        }
    } else if (z > G.key_next) {
        if (add_pick(PK_KEY, lane, y + 0.25f, z)) {
            G.key_next = z + rr(900.0f, 1500.0f);
            start = 2;
        }
    }
    for (int k = start; k < n; k++) add_coin(lane_x(lane), y, z + (float)k * 1.7f);
}

/* Coins along the arc a jump actually flies, centred on a barrier. */
static void coin_arc(int lane, float zc, float base) {
    float half = 3.4f * pa_clampf(G.speed / 12.5f, 1.0f, 1.9f);
    for (int k = 0; k < 7; k++) {
        float t = (float)k / 6.0f * 2.0f - 1.0f;
        add_coin(lane_x(lane), base + 0.9f + JUMP_H * 0.92f * (1.0f - t * t), zc + t * half);
    }
}

/* Coins up a ramp and along the roof of the train behind it. */
static void coin_ramp(int lane, float body_z, int cars) {
    float zr = body_z - RAMP_LEN;
    for (int k = 0; k < 4; k++) {
        float t = ((float)k + 0.5f) / 4.0f;
        add_coin(lane_x(lane), 0.9f + TRAIN_H * t, zr + RAMP_LEN * t);
    }
    float len = train_len(cars);
    for (float z = body_z + 1.5f; z < body_z + len - 1.0f; z += 1.7f) add_coin(lane_x(lane), TRAIN_H + 0.9f, z);
}

static void perm3(int *a) {
    a[0] = 0; a[1] = 1; a[2] = 2;
    for (int i = 2; i > 0; i--) { int j = ri(0, i); int t = a[i]; a[i] = a[j]; a[j] = t; }
}

/* Each pattern lays a stretch of track starting at z and returns its length.
   All of them leave a way through that a player who reads the row can take:
   a free lane, a barrier that can be jumped or rolled, or a ramp. */
static float sp_scale(void) { return pa_clampf(G.speed / 12.5f, 1.0f, 1.9f); }

/* Coins strung along a roof: a line of 8+ the player can see from the ground. */
static void roof_coins(int lane, float z0, int cars) {
    float len = train_len(cars);
    int n = 0;
    for (float z = z0 + 2.0f; z < z0 + len - 1.0f && n < 14; z += 1.7f, n++) add_coin(lane_x(lane), TRAIN_H + 0.9f, z);
}

/* A parked train covering about [z0, z1] in a lane, roof coins half the time. */
static float side_train(int lane, float z0, float z1) {
    int cars = (int)floorf((z1 - z0 + CAR_GAP) / (CAR_LEN + CAR_GAP));
    if (cars < 1) cars = 1;
    if (cars > 4) cars = 4;
    add_train(lane, z0, cars, 0, pick_style());
    if (rc(0.55f)) roof_coins(lane, z0, cars);
    return z0 + train_len(cars);
}

static float pat_low(float z) {
    int p[3]; perm3(p);
    add_obs(OB_LOW, p[0], z + 6.0f);
    coin_arc(p[0], z + 6.0f, 0.0f);
    coin_line(p[1], z, 6, 0.9f);
    return side_train(p[2], z, z + 19.0f) - z;
}

/* Two lanes walled by long trains, the third a gauntlet of barriers. A ramp
   on one of the trains half the time gives the roof route. */
static float pat_corridor(float z) {
    int p[3]; perm3(p);
    float s = sp_scale();
    int ca = ri(2, 4), cb = ri(2, 4);
    int ramp = rc(0.45f);
    float za = z + (ramp ? RAMP_LEN : 0.0f);
    add_train(p[0], za, ca, ramp, pick_style());
    if (ramp) coin_ramp(p[0], za, ca); else roof_coins(p[0], za, ca);
    float zb = z + rr(0.0f, 8.0f);
    add_train(p[1], zb, cb, 0, pick_style());
    if (rc(0.5f)) roof_coins(p[1], zb, cb);
    float end = maxf(za + train_len(ca), zb + train_len(cb));
    coin_line(p[2], z, 5, 0.9f);
    for (float bz = z + 12.0f * s; bz < end - 6.0f; bz += 16.0f * s) {
        int kind = rc(0.55f) ? OB_LOW : OB_HIGH;
        add_obs(kind, p[2], bz);
        if (kind == OB_LOW) coin_arc(p[2], bz, 0.0f);
        else for (int k = 0; k < 4; k++) add_coin(lane_x(p[2]), 0.6f, bz - 2.5f + (float)k * 1.7f);
    }
    return end - z;
}

static float pat_lowhigh(float z) {
    int p[3]; perm3(p);
    add_obs(OB_LOW, p[0], z + 6.0f);
    add_obs(OB_HIGH, p[1], z + 6.0f);
    coin_line(p[2], z, 7, 0.9f);
    return side_train(p[2], z + 14.0f, z + 30.0f) - z;
}

static float pat_high(float z) {
    int p[3]; perm3(p);
    add_obs(OB_HIGH, p[0], z + 6.0f);
    coin_line(p[0], z + 2.0f, 6, 0.9f);
    add_obs(OB_LOW, p[1], z + 6.0f + 10.0f * sp_scale());
    coin_arc(p[1], z + 6.0f + 10.0f * sp_scale(), 0.0f);
    float e = side_train(p[2], z, z + 20.0f + 10.0f * sp_scale());
    return maxf(14.0f + 10.0f * sp_scale(), e - z);
}

static float pat_wall(float z) {
    int p[3]; perm3(p);
    add_obs(OB_LOW, p[0], z + 6.0f);
    add_obs(OB_HIGH, p[1], z + 6.0f);
    add_obs(rc(0.5f) ? OB_LOW : OB_HIGH, p[2], z + 6.0f);
    coin_arc(p[0], z + 6.0f, 0.0f);
    return 10.0f;
}

static float pat_trains2(float z) {
    int p[3]; perm3(p);
    float s = sp_scale();
    int ca = ri(2, 3);
    add_train(p[0], z, ca, 0, pick_style());
    if (rc(0.5f)) roof_coins(p[0], z, ca);
    add_train(p[1], z + rr(4.0f, 10.0f), ri(2, 3), 0, pick_style());
    coin_line(p[2], z, 8, 0.9f);
    float zb = z + 16.0f * s;
    add_obs(rc(0.5f) ? OB_LOW : OB_HIGH, p[2], zb);
    return maxf(16.0f * s + 4.0f, train_len(ca));
}

static float pat_ramp(float z) {
    int p[3]; perm3(p);
    float s = sp_scale();
    int cars = ri(2, 3);
    add_train(p[0], z + RAMP_LEN, cars, 1, pick_style());
    coin_ramp(p[0], z + RAMP_LEN, cars);
    add_train(p[1], z + 3.0f, ri(1, 2), 0, pick_style());
    add_obs(OB_LOW, p[2], z + 8.0f * s);
    coin_arc(p[2], z + 8.0f * s, 0.0f);
    add_obs(OB_HIGH, p[2], z + 22.0f * s);
    return RAMP_LEN + train_len(cars);
}

static float pat_oncoming(float z) {
    /* The parked train takes an edge lane, so the lane with the oncoming train
       and the escape lane are always side by side. */
    int p[3];
    p[1] = rc(0.5f) ? 0 : 2;
    p[0] = 1;
    p[2] = 2 - p[1];
    if (rc(0.5f)) { int t = p[0]; p[0] = p[2]; p[2] = t; }
    float s = sp_scale();
    add_train(p[1], z + 2.0f, 2, 0, pick_style());
    coin_line(p[0], z, 7, 0.9f);
    add_obs(OB_LOW, p[2], z + 12.0f * s);
    coin_arc(p[2], z + 12.0f * s, 0.0f);
    Obs *o = add_train(p[0], z + 52.0f, ri(1, 2), 0, rc(0.5f) ? 0 : 3);
    if (o) { o->moving = 1; o->v = rr(8.0f, 11.0f) + 4.0f * pa_clamp01(G.dist / 3000.0f); }
    return 52.0f + (o ? o->len : CAR_LEN);
}

static float pat_signal(float z) {
    int p[3]; perm3(p);
    float s = sp_scale();
    add_obs(OB_SIGNAL, p[0], z + 4.0f);
    add_obs(OB_LOW, p[1], z + 4.0f);
    coin_line(p[2], z, 6, 0.9f);
    add_obs(OB_SIGNAL, p[2], z + 4.0f + 13.0f * s);
    add_obs(OB_HIGH, p[1], z + 4.0f + 13.0f * s);
    coin_line(p[0], z + 4.0f + 8.0f * s, 5, 0.9f);
    return 8.0f + 13.0f * s;
}

static float pat_force_ramp(float z) {
    int p[3]; perm3(p);
    int cars = ri(3, 4);
    add_train(p[0], z + RAMP_LEN, cars, 1, pick_style());
    coin_ramp(p[0], z + RAMP_LEN, cars);
    float zs = z + RAMP_LEN + rr(2.0f, 5.0f);
    add_train(p[1], zs, ri(2, 3), 0, pick_style());
    add_train(p[2], zs + rr(-2.0f, 4.0f), ri(2, 3), 0, pick_style());
    for (int k = 1; k < 3; k++)
        for (float cz = zs + 3.0f; cz < zs + CAR_LEN * 2.0f; cz += 1.7f) add_coin(lane_x(p[k]), TRAIN_H + 0.9f, cz);
    return RAMP_LEN + train_len(cars) + 2.0f;
}

static float pat_roofhop(float z) {
    int a = rc(0.5f) ? 0 : 2, b = 1, cl = 2 - a;
    add_train(a, z + RAMP_LEN, 2, 1, pick_style());
    coin_ramp(a, z + RAMP_LEN, 2);
    float zb = z + RAMP_LEN + CAR_LEN * 1.2f;
    add_train(b, zb, 3, 0, pick_style());
    for (float cz = zb + CAR_LEN; cz < zb + train_len(3) - 1.0f; cz += 1.7f) add_coin(lane_x(b), TRAIN_H + 0.9f, cz);
    float s = sp_scale();
    add_obs(OB_LOW, cl, z + 8.0f * s);
    add_obs(OB_HIGH, cl, z + 24.0f * s);
    add_obs(OB_LOW, cl, z + 40.0f * s);
    return maxf(zb - z + train_len(3), 40.0f * s + 4.0f);
}

typedef float (*PatFn)(float);

static void generate(void) {
    static const PatFn EASY[] = { pat_low, pat_corridor, pat_trains2, pat_ramp, pat_high, pat_corridor };
    static const PatFn ALL[]  = { pat_low, pat_lowhigh, pat_trains2, pat_ramp, pat_high, pat_corridor,
                                  pat_wall, pat_oncoming, pat_signal, pat_force_ramp, pat_roofhop,
                                  pat_corridor, pat_corridor, pat_ramp, pat_oncoming };
    /* The capture run opens with a fixed sequence, so a reviewer always sees
       a jump, a roll, a ramp onto a roof and an oncoming train. */
    static const PatFn DEMO[] = { pat_low, pat_high, pat_ramp, pat_corridor, pat_oncoming, pat_force_ramp,
                                  pat_corridor, pat_roofhop, pat_lowhigh, pat_corridor, pat_signal };
    while (G.gen_z < G.z + AHEAD) {
        if (G.gen_n == 0) {
            /* A clear middle lane to find your feet, parked stock either side
               and coins down the middle. */
            for (int k = 0; k < 10; k++) add_coin(0.0f, 0.9f, 14.0f + (float)k * 1.7f);
            add_train(0, 6.0f, 3, 0, 1);
            roof_coins(0, 6.0f, 3);
            add_train(2, 12.0f, 2, 0, 2);
            G.gen_z = 46.0f;
            G.gen_n++;
            continue;
        }
        float d = pa_clamp01(G.gen_z / 2400.0f);
        float len;
        if (G.demo_script && G.gen_n - 1 < (int)(sizeof(DEMO) / sizeof(DEMO[0])))
            len = DEMO[G.gen_n - 1](G.gen_z);
        else if (G.gen_z < 320.0f)
            len = EASY[ri(0, (int)(sizeof(EASY) / sizeof(EASY[0])) - 1)](G.gen_z);
        else
            len = ALL[ri(0, (int)(sizeof(ALL) / sizeof(ALL[0])) - 1)](G.gen_z);
        G.gen_z += len + pa_lerpf(9.0f, 5.0f, d) * sp_scale();
        G.gen_n++;
    }
}

static void spawn_buildings(float upto) {
    static const int NSTY = 10;
    for (int s = 0; s < 2; s++) {
        while (G.bld_z[s] < upto && G.nbld[s] < MAX_BLD) {
            Bld *b = &G.bld[s][G.nbld[s]++];
            b->z0 = G.bld_z[s];
            b->z1 = b->z0 + rr(6.0f, 13.0f);
            b->h = rr(7.5f, 17.0f);
            b->depth = 8.0f;
            b->style = ri(0, NSTY - 1);
            b->gable = rc(0.38f);
            b->shop = rc(0.6f);
            b->sign = rc(0.3f) ? ri(1, 4) : 0;
            b->seed = (uint32_t)ri(0, 0x7FFFFFFF);
            b->world = world_at(b->z0);
            if (b->world == 0) {
                b->variant = ri(0, 3);
                if (b->seed % 9 == 0) { b->variant = 4; b->h = rr(10.0f, 12.0f); b->z1 = b->z0 + 10.0f; }
            } else {
                int r = ri(0, 9);
                b->variant = r < 3 ? 0 : (r < 5 ? 1 : (r < 7 ? 2 : 3));
                b->h = b->variant == 0 ? rr(4.8f, 7.0f) : b->variant == 1 ? rr(4.0f, 5.5f) : b->variant == 2 ? rr(7.0f, 8.6f) : rr(12.0f, 24.0f);
                b->shop = 0; b->sign = 0;
            }
            G.bld_z[s] = b->z1 + (rc(0.22f) ? rr(1.6f, 3.2f) : 0.0f);
        }
    }
}

/* ========================================================= GAME STATE == */
static void new_run(int from_menu) {
    load_save();
    memset(&G, 0, sizeof(G));
    G.demo_script = pa_demo_mode() != 0;
    uint32_t seed = G.demo_script ? 0x5EED1234u
                                  : 0xA511u ^ ((uint32_t)g_runs * 2654435761u) ^ ((uint32_t)g_best * 40503u);
    pa_rng_seed(&G.rng, seed);
    pa_rng_seed(&G.fxrng, seed ^ 0x9E3779B9u);
    G.st = from_menu ? ST_MENU : ST_RUN;
    G.lane = 1; G.from_lane = 1;
    G.stumble_t = 99.0f;
    G.chase_d = 0.9f; G.chase_x = 1.45f;
    G.cam_blend = from_menu ? 0.0f : 1.0f;
    G.pu_next = G.demo_script ? 140.0f : rr(150.0f, 260.0f);
    G.key_next = G.demo_script ? 99999.0f : rr(500.0f, 900.0f);
    G.bld_z[0] = -160.0f; G.bld_z[1] = -158.0f;
    spawn_buildings(AHEAD);
    /* Parked stock behind the start line, for the start screen to look down. */
    add_train(0, -44.0f, 2, 0, 1);
    add_train(2, -70.0f, 2, 0, 2);
    add_obs(OB_LOW, 1, -30.0f);
    generate();
    if (!from_menu) { G.speed = 6.0f; banner("GO!", pa_hex(0xFFD23A)); }
}

static void begin_running(void) {
    G.st = ST_RUN;
    G.st_t = 0.0f;
    g_runs++;
    pa_save_set("runner.runs", g_runs);
    snd_tone(392, 784, 0.18f, 1, 0.10f);
}

static void add_part(int kind, float x, float y, float z, float vx, float vy, float vz, float life,
                     float size, PA_Color col) {
    if (G.npart >= MAX_PART) return;
    Part *p = &G.part[G.npart++];
    p->kind = kind; p->x = x; p->y = y; p->z = z; p->vx = vx; p->vy = vy; p->vz = vz;
    p->t = 0.0f; p->life = life; p->size = size; p->col = col; p->spin = fr(-6.0f, 6.0f);
}

static void dust(float n, float spread) {
    for (int i = 0; i < (int)n; i++)
        add_part(PT_DUST, G.x + fr(-spread, spread), G.ground + 0.1f, G.z + fr(-0.4f, 0.2f),
                 fr(-2.2f, 2.2f), fr(0.6f, 2.0f), G.speed * fr(0.55f, 0.85f), fr(0.4f, 0.7f), fr(0.28f, 0.46f),
                 pa_mix(pa_hex(0xEFE4D2), pa_hex(0xF2C8A0), canyon_amt(G.z)));
}

static float surface(float x, float z, float y) {
    float s = 0.0f;
    for (int i = 0; i < G.nobs; i++) {
        const Obs *o = &G.obs[i];
        if (o->kind != OB_TRAIN) continue;
        if (fabsf(x - lane_x(o->lane)) > TRAIN_W * 0.5f + 0.1f) continue;
        if (z >= o->z - 0.25f && z <= o->z + o->len + 0.3f && y >= TRAIN_H - 0.8f) s = maxf(s, TRAIN_H);
        if (o->ramp && z >= o->z - RAMP_LEN && z < o->z) {
            float h = TRAIN_H * (z - (o->z - RAMP_LEN)) / RAMP_LEN;
            if (y >= h - 1.1f) s = maxf(s, h);
        }
    }
    return s;
}

static int score_mult(void) {
    int m = 1 + G.level;
    if (G.mult_t > 0.0f) m *= 2;
    return m;
}

static void crash(int kind, float zstop) {
    G.st = ST_CRASH;
    G.st_t = 0.0f;
    G.crash_kind = kind;
    G.crash_z = zstop;
    G.fall_dir = G.x > 0.1f ? -1.0f : (G.x < -0.1f ? 1.0f : -1.0f);
    G.shake = 1.0f;
    G.flash = 0.6f; G.flash_col = PA_RGB(255, 255, 255);
    G.roll_t = 0.0f;
    if (!g_mute) pa_sfx("boom");
    snd_tone(220, 70, 0.4f, 3, 0.10f);
    for (int i = 0; i < 16; i++)
        add_part(PT_STAR, G.x, G.y + 1.6f, zstop, fr(-4.0f, 4.0f), fr(2.0f, 7.0f), fr(-3.0f, 1.0f), fr(0.6f, 1.0f),
                 fr(0.14f, 0.22f), i & 1 ? pa_hex(0xFFE04A) : pa_hex(0xFFFFFF));
}

static void board_on(void) {
    if (g_boards <= 0 || G.board_t > 0.0f || G.jet_t > 0.0f) return;
    g_boards--;
    if (!g_mute) pa_save_set("runner.boards", g_boards);
    G.board_t = 10.0f;
    G.ntrail = 0;
    banner("HOVERBOARD!", pa_hex(0x4FE6FF));
    snd_tone(300, 1200, 0.35f, 3, 0.07f);
    snd_tone(600, 1500, 0.25f, 1, 0.06f);
}

/* A hit while riding costs the board instead of the run. */
static int board_save(void) {
    if (G.board_t <= 0.0f) return 0;
    G.board_t = 0.0f;
    G.invuln = 1.6f;
    G.shake = 0.6f;
    G.flash = 0.4f; G.flash_col = pa_hex(0x4FE6FF);
    for (int i = 0; i < 16; i++)
        add_part(PT_CONFETTI, G.x, G.y + 0.3f, G.z, fr(-5.0f, 5.0f), fr(2.0f, 7.0f), fr(-2.0f, 4.0f) + G.speed * 0.6f,
                 fr(0.5f, 0.8f), fr(0.12f, 0.2f), i & 1 ? pa_hex(0xE8343C) : pa_hex(0x4FE6FF));
    if (!g_mute) pa_sfx("boom");
    banner("BOARD SMASHED!", pa_hex(0xFF6A5A));
    return 1;
}

static void stumble(void) {
    if (board_save()) return;
    if (G.stumble_t < CHASE_WINDOW) {
        crash(CR_CAUGHT, G.z);
        return;
    }
    G.stumble_t = 0.0f;
    G.stagger = 0.55f;
    G.lane = G.from_lane;
    G.xv = -G.xv * 0.6f;
    G.shake = 0.55f;
    G.flash = 0.35f; G.flash_col = PA_RGB(255, 70, 70);
    G.speed *= 0.86f;
    if (!g_mute) pa_sfx("thud");
    snd_noise(0.12f, 0.12f);
    banner("WATCH OUT!", pa_hex(0xFF6A5A));
}

static void collect_coin(const Coin *c) {
    G.coins++;
    G.coin_pop = 1.0f;
    G.streak = G.streak_t > 0.0f ? G.streak + 1 : 0;
    G.streak_t = 0.5f;
    float pitch = 1320.0f * (1.0f + 0.03f * (float)(G.streak % 8));
    snd_tone(pitch, pitch * 1.5f, 0.07f, 1, 0.06f);
    /* Burst: a ring flash and ten sparks thrown outward, riding along with
       the runner so the burst stays on screen. */
    add_part(PT_RING, c->x, c->y, c->z, 0.0f, 0.0f, G.speed * 0.95f, 0.22f, 0.5f, pa_hex(0xFFF6C8));
    for (int i = 0; i < 10; i++) {
        float a = PA_TAU * (float)i / 10.0f + fr(-0.2f, 0.2f);
        add_part(PT_SPARK, c->x, c->y, c->z, cosf(a) * fr(3.0f, 5.0f), sinf(a) * fr(3.0f, 5.0f) + 1.5f, G.speed * 0.9f,
                 fr(0.28f, 0.42f), fr(0.09f, 0.14f), i & 1 ? pa_hex(0xFFFFFF) : pa_hex(0xFFE45C));
    }
}

static void activate(int kind, const Pick *p) {
    static const char *NAMES[] = { "COIN MAGNET!", "JETPACK!", "SUPER SNEAKERS!", "2X SCORE!", "KEY!" };
    static const uint32_t COLS[] = { 0xFF5A5A, 0xFFB43A, 0x6CE05A, 0xFFD23A, 0x5AC8FF };
    banner(NAMES[kind], pa_hex(COLS[kind]));
    snd_tone(600, 1800, 0.30f, 1, 0.10f);
    snd_tone(900, 1350, 0.20f, 0, 0.06f);
    for (int i = 0; i < 18; i++)
        add_part(PT_CONFETTI, p->x, p->y, p->z, fr(-5.0f, 5.0f), fr(2.0f, 8.0f), fr(-2.0f, 4.0f) + G.speed * 0.7f,
                 fr(0.5f, 0.9f), fr(0.10f, 0.16f), pa_hsl(fr(0.0f, 1.0f), 0.85f, 0.6f));
    switch (kind) {
        case PK_MAGNET: G.magnet_t = 10.0f; break;
        case PK_SNEAK:  G.sneak_t = 10.0f; break;
        case PK_MULT:   G.mult_t = 10.0f; break;
        case PK_KEY:    g_keys++; G.keys_found++; pa_save_set("runner.keys", g_keys); break;
        case PK_JET: {
            G.jet_t = 7.0f;
            G.roll_t = 0.0f;
            /* A coin trail at altitude for the length of the flight, wandering
               between lanes. */
            float z = G.z + 14.0f, end = G.z + 7.0f * G.speed * 1.12f;
            int l = G.lane;
            int k = 0;
            while (z < end) {
                add_coin(lane_x(l), JET_Y + 1.0f, z);
                z += 1.9f;
                if (++k % 9 == 0) {
                    int nl = l + (rc(0.5f) ? 1 : -1);
                    l = (nl < 0 || nl > 2) ? 1 : nl;
                }
            }
            break;
        }
        default: break;
    }
}

static void finish_run(void) {
    G.st = ST_RESULTS;
    G.st_t = 0.0f;
    int sc = (int)G.score;
    g_bank += G.coins;
    G.new_best = sc > g_best;
    if (G.new_best) g_best = sc;
    pa_save_set("runner.best", g_best);
    pa_save_set("runner.coins", g_bank);
    pa_save_set("runner.keys", g_keys);
    pa_save_flush();
    if (!g_mute) pa_sfx(G.new_best ? "win" : "lose");
}

static int revive_cost(void) { return 1 << (G.revives > 3 ? 3 : G.revives); }

static void revive(void) {
    g_keys -= revive_cost();
    pa_save_set("runner.keys", g_keys);
    pa_save_flush();
    G.revives++;
    /* Clear the stretch ahead so the run resumes on open track. */
    for (int i = G.nobs - 1; i >= 0; i--) {
        Obs *o = &G.obs[i];
        float z0 = o->z - (o->ramp ? RAMP_LEN : 0.0f), z1 = o->z + o->len + 0.5f;
        if ((z1 > G.z - 3.0f && z0 < G.z + 48.0f) || (o->moving && o->z < G.z + 160.0f))
            G.obs[i] = G.obs[--G.nobs];
    }
    G.st = ST_RUN;
    G.st_t = 0.0f;
    G.invuln = 2.6f;
    G.stumble_t = 99.0f;
    G.y = 0.0f; G.vy = 0.0f; G.air = 0; G.ground = 0.0f;
    G.speed = maxf(10.0f, G.speed * 0.8f);
    G.jet_t = 0.0f; G.glide = 0.0f;
    if (!g_mute) pa_sfx("good");
    banner("GO!", pa_hex(0xFFD23A));
}

/* ================================================================ BOT === */
/* Nearest thing in a lane that the runner, at its current height, has to
   deal with. Returns the effective distance (closing speed folded in). */
static Obs *hazard(int lane, float look, float *dist) {
    Obs *best = NULL;
    float bd = 1e9f;
    for (int i = 0; i < G.nobs; i++) {
        Obs *o = &G.obs[i];
        if (o->lane != lane) continue;
        float z0 = o->z, z1 = o->z + (o->kind == OB_TRAIN ? o->len : 0.0f);
        if (z1 < G.z - 0.4f) continue;
        if (o->kind == OB_TRAIN) {
            if (G.y >= TRAIN_H - 0.8f) continue;                  /* on or above roofs */
            if (o->ramp && !o->moving && G.z < z0 - 0.5f && G.z > z0 - RAMP_LEN - 40.0f) {
                if (G.z > z0 - RAMP_LEN || G.y < 0.5f) continue;  /* the ramp takes us up */
            }
        }
        float d = z0 - G.z;
        if (d < -0.4f && o->kind != OB_TRAIN) continue;
        if (o->moving && o->active) d *= G.speed / (G.speed + o->v);
        if (d > look) continue;
        if (d < bd) { bd = d; best = o; }
    }
    *dist = bd;
    return best;
}

static int lane_safe(int l) {
    if (l < 0 || l > 2) return 0;
    for (int i = 0; i < G.nobs; i++) {
        const Obs *o = &G.obs[i];
        if (o->lane != l) continue;
        float z0 = o->z, z1 = o->z + (o->kind == OB_TRAIN ? o->len : 0.0f);
        if (o->kind == OB_TRAIN) {
            if (G.y >= TRAIN_H - 0.8f) {
                continue;
            }
            float zr = o->ramp ? z0 - RAMP_LEN : z0;
            if (z1 > G.z - 0.8f && zr < G.z + 1.6f) return 0;      /* alongside: would clip it */
            float d = z0 - G.z;
            if (o->moving && o->active) d *= G.speed / (G.speed + o->v);
            if (!o->ramp && d > 0.0f && d < G.speed * 0.75f + 2.0f) return 0;
            if (o->ramp && d > 0.0f && G.z > zr) return 0;          /* mid-ramp from the side */
        } else {
            float d = z0 - G.z;
            if (d > -0.5f && d < (o->kind == OB_SIGNAL ? G.speed * 0.5f + 2.0f : 1.4f)) return 0;
        }
    }
    return 1;
}

static int lane_value(int l) {
    float v = 0.0f;
    for (int i = 0; i < G.nobs; i++) {
        const Obs *o = &G.obs[i];
        if (o->lane == l && o->ramp && o->z - RAMP_LEN > G.z + 2.0f && o->z - G.z < 60.0f) v += 14.0f;
    }
    for (int i = 0; i < G.npick; i++)
        if (fabsf(G.pick[i].x - lane_x(l)) < 0.5f && G.pick[i].z > G.z && G.pick[i].z < G.z + 30.0f &&
            fabsf(G.pick[i].y - (G.y + 1.1f)) < 2.0f) v += 8.0f;
    for (int i = 0; i < G.ncoin; i++)
        if (fabsf(G.coin[i].x - lane_x(l)) < 0.5f && G.coin[i].z > G.z && G.coin[i].z < G.z + 16.0f &&
            fabsf(G.coin[i].y - (G.y + 0.9f)) < 1.6f) v += 0.5f;
    return (int)v;
}

static int bot_think(float dt) {
    G.bot_t -= dt;
    if (G.bot_t > 0.0f) return PA_SWIPE_NONE;
    int reckless = pa_demo_mode() == 1 && G.run_t > 21.4f && G.jet_t <= 0.0f && G.board_t <= 0.0f;
    float look = G.speed * 1.1f + 6.0f;
    float d;
    if (reckless && !G.air && G.y < 0.05f) {
        /* The capture run ends the way most real runs do: head first into the
           front of a train. Steer for one. */
        for (int i = 0; i < G.nobs; i++) {
            const Obs *t = &G.obs[i];
            if (t->kind != OB_TRAIN || t->ramp || abs(t->lane - G.lane) != 1) continue;
            float dz = t->z - G.z;
            int clear = 1;
            for (int j = 0; j < G.nobs; j++) {
                const Obs *q = &G.obs[j];
                if (q->lane != t->lane || q == t) continue;
                float q0 = q->z - (q->ramp ? RAMP_LEN : 0.0f), q1 = q->z + q->len + 0.3f;
                if (q1 > G.z - 1.0f && q0 < t->z) clear = 0;
            }
            if (dz > 6.0f && dz < 45.0f && clear) {
                G.bot_t = 0.3f;
                return t->lane < G.lane ? PA_SWIPE_LEFT : PA_SWIPE_RIGHT;
            }
        }
    }
    Obs *o = hazard(G.lane, look, &d);
    if (o && !(reckless && o->kind == OB_TRAIN)) {
        if (o->kind == OB_LOW && !G.air && d < G.speed * 0.17f + 1.1f) { G.bot_t = 0.25f; return PA_SWIPE_UP; }
        if (o->kind == OB_HIGH && G.roll_t <= 0.0f && d < G.speed * 0.10f + 1.4f) { G.bot_t = 0.2f; return PA_SWIPE_DOWN; }
        if (o->kind == OB_TRAIN || o->kind == OB_SIGNAL) {
            int c1 = G.lane - 1, c2 = G.lane + 1;
            int s1 = lane_safe(c1), s2 = lane_safe(c2);
            int pick = 0;
            if (s1 && s2) pick = lane_value(c1) >= lane_value(c2) ? -1 : 1;
            else if (s1) pick = -1;
            else if (s2) pick = 1;
            if (pick && d < G.speed * 0.9f + 3.0f) { G.bot_t = 0.22f; G.from_lane = G.lane; return pick < 0 ? PA_SWIPE_LEFT : PA_SWIPE_RIGHT; }
            if (!pick && G.sneak_t > 0.0f && !G.air && d < G.speed * 0.30f + 1.0f) { G.bot_t = 0.2f; return PA_SWIPE_UP; }
        }
        if ((o->kind == OB_LOW || o->kind == OB_HIGH) && d > 4.0f) {
            /* Far enough out to simply step around it if the next lane is open. */
        } else {
            return PA_SWIPE_NONE;
        }
    }
    /* Nothing pressing: drift toward ramps, power-ups and coins - but never
       off a ramp or a roof the runner is already committed to. */
    if (G.air || G.y > 0.05f) { G.bot_t = 0.05f; return PA_SWIPE_NONE; }
    int best_l = G.lane, bestv = lane_value(G.lane) + 2;
    for (int l = 0; l < 3; l++) {
        int v = lane_value(l) - 2 * abs(l - G.lane);
        if (v > bestv) { bestv = v; best_l = l; }
    }
    int best = best_l > G.lane ? 1 : (best_l < G.lane ? -1 : 0);
    if (best) {
        int l = G.lane + best;
        float d2;
        Obs *h2 = hazard(l, G.speed * 0.8f + 4.0f, &d2);
        if (!lane_safe(l) || (h2 && (h2->kind == OB_TRAIN || h2->kind == OB_SIGNAL))) best = 0;
    }
    if (best) { G.bot_t = 0.35f; return best < 0 ? PA_SWIPE_LEFT : PA_SWIPE_RIGHT; }
    G.bot_t = 0.06f;
    return PA_SWIPE_NONE;
}

/* ============================================================= UPDATE === */
typedef struct { float x, y, w, h; } Rect;

static int in_rect(Rect r, float x, float y) { return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h; }

static Rect pause_rect(void) { Rect r = { 14.0f * L.u, 14.0f * L.u, 60.0f * L.u, 60.0f * L.u }; return r; }

static float revive_top(void) { return (float)L.h * 0.5f - 230.0f * L.u; }
static Rect revive_button(void) {
    float u = L.u, bw = 260.0f * u, bh = 70.0f * u;
    Rect r = { (float)L.w * 0.5f - bw * 0.5f, revive_top() + 290.0f * u, bw, bh };
    return r;
}
static Rect skip_button(void) {
    float u = L.u;
    Rect r = { (float)L.w * 0.5f - 100.0f * u, revive_top() + 370.0f * u, 200.0f * u, 46.0f * u };
    return r;
}
static Rect play_button(void) {
    float u = L.u, bw = 260.0f * u, bh = 76.0f * u;
    Rect r = { (float)L.w * 0.5f - bw * 0.5f, (float)L.h * 0.5f + 150.0f * u, bw, bh };
    return r;
}
static Rect home_button(void) {
    float u = L.u, bw = 170.0f * u, bh = 52.0f * u;
    Rect r = { (float)L.w * 0.5f - bw * 0.5f, (float)L.h * 0.5f + 244.0f * u, bw, bh };
    return r;
}

static int read_swipe(const PA_Input *in) {
    int sw = in->swipe;
    if (sw == PA_SWIPE_TAP) sw = PA_SWIPE_NONE;
    if (in->key_pressed[PA_KEY_LEFT])  sw = PA_SWIPE_LEFT;
    if (in->key_pressed[PA_KEY_RIGHT]) sw = PA_SWIPE_RIGHT;
    if (in->key_pressed[PA_KEY_UP] || in->key_pressed[PA_KEY_SPACE]) sw = PA_SWIPE_UP;
    if (in->key_pressed[PA_KEY_DOWN])  sw = PA_SWIPE_DOWN;
    return sw;
}

static void update_particles(float dt) {
    for (int i = G.npart - 1; i >= 0; i--) {
        Part *p = &G.part[i];
        p->t += dt;
        if (p->t >= p->life) { G.part[i] = G.part[--G.npart]; continue; }
        float g = p->kind == PT_DUST ? -1.0f : (p->kind == PT_FLAME ? -2.0f : 14.0f);
        if (p->kind == PT_CONFETTI) g = 7.0f;
        p->vy -= g * dt;
        p->x += p->vx * dt; p->y += p->vy * dt; p->z += p->vz * dt;
        if (p->kind == PT_DUST) { p->vx *= expf(-3.0f * dt); p->vz *= expf(-3.0f * dt); }
    }
}

static void world_upkeep(void) {
    for (int i = G.nobs - 1; i >= 0; i--)
        if (G.obs[i].z + G.obs[i].len < G.z - 14.0f) G.obs[i] = G.obs[--G.nobs];
    for (int i = G.ncoin - 1; i >= 0; i--)
        if (G.coin[i].z < G.z - 7.0f) G.coin[i] = G.coin[--G.ncoin];
    for (int i = G.npick - 1; i >= 0; i--)
        if (G.pick[i].z < G.z - 7.0f) G.pick[i] = G.pick[--G.npick];
    for (int s = 0; s < 2; s++) {
        int w = 0;
        for (int i = 0; i < G.nbld[s]; i++)
            if (G.bld[s][i].z1 > G.z - 30.0f) G.bld[s][w++] = G.bld[s][i];
        G.nbld[s] = w;
    }
    spawn_buildings(G.z + AHEAD + 10.0f);
    generate();
}

static void run_step(float dt, const PA_Input *in) {
    G.run_t += dt;
    G.cam_blend = minf(1.0f, G.cam_blend + dt / 1.1f);
    int sw = read_swipe(in);
    if ((pa_demo_mode() || g_force_bot) && sw == PA_SWIPE_NONE) sw = bot_think(dt);

    if (sw == PA_SWIPE_LEFT || sw == PA_SWIPE_RIGHT) {
        int nl = G.lane + (sw == PA_SWIPE_LEFT ? -1 : 1);
        if (nl >= 0 && nl <= 2) {
            G.from_lane = G.lane;
            G.lane = nl;
            G.hop_t = 0.0f;
            snd_noise(0.06f, 0.05f);
            snd_tone(500, 760, 0.07f, 1, 0.05f);
            if (!G.air) dust(3, 0.3f);
        } else {
            /* Running into the wall at the edge of the line is a stumble. */
            G.shake = 0.25f;
            if (!g_mute) pa_sfx("thud");
        }
    } else if (sw == PA_SWIPE_UP) {
        if (!G.air && G.jet_t <= 0.0f) {
            float h = G.sneak_t > 0.0f ? SNEAK_H : JUMP_H;
            G.vy = sqrtf(2.0f * GRAVITY * h);
            G.air = 1;
            G.roll_t = 0.0f;
            snd_tone(300, G.sneak_t > 0.0f ? 1100 : 760, 0.16f, 1, 0.10f);
        }
    } else if (sw == PA_SWIPE_DOWN) {
        if (G.jet_t <= 0.0f) {
            if (G.air) G.vy = minf(G.vy, -26.0f);
            G.roll_t = ROLL_TIME;
            snd_noise(0.16f, 0.07f);
            snd_tone(420, 160, 0.16f, 0, 0.06f);
        }
    }

    /* Speed climbs on a long curve toward a ceiling. */
    float target = 12.5f + 11.0f * (1.0f - expf(-G.dist / 2600.0f));
    if (G.jet_t > 0.0f) target *= 1.12f;
    G.speed = pa_approach(G.speed, target, G.speed < 10.0f ? 2.5f : 1.0f, dt);
    G.zprev = G.z;
    G.z += G.speed * dt;
    G.dist = G.z;

    /* Lane hop: a stiff critically damped spring, settles in about 0.2 s. */
    float tx = lane_x(G.lane);
    G.xv += (300.0f * (tx - G.x) - 34.6f * G.xv) * dt;
    G.x += G.xv * dt;
    G.hop_t += dt;

    /* Vertical. */
    float gnd = surface(G.x, G.z, G.y);
    if (G.jet_t > 0.0f) {
        G.y = pa_approach(G.y, JET_Y, 2.6f, dt);
        G.vy = 0.0f; G.air = 1;
        G.jet_t -= dt;
        G.jet_sfx -= dt;
        if (G.jet_sfx <= 0.0f) { G.jet_sfx = 0.22f; snd_noise(0.22f, 0.025f); }
        for (int k = 0; k < 2; k++)
            add_part(PT_FLAME, G.x + (k ? 0.16f : -0.16f), G.y + 1.05f, G.z - 0.36f, fr(-0.3f, 0.3f), fr(-6.0f, -3.0f),
                     G.speed * 0.6f, fr(0.18f, 0.3f), fr(0.14f, 0.22f), pa_hex(0xFFB43A));
        if (G.jet_t <= 0.0f) { G.glide = 1.0f; G.vy = 0.0f; }
    } else if (G.air) {
        float g = G.glide > 0.0f ? GRAVITY * 0.22f : GRAVITY;
        G.vy -= g * dt;
        if (G.glide > 0.0f && G.vy < -7.0f) G.vy = -7.0f;
        G.y += G.vy * dt;
        if (G.y <= gnd) {
            if (G.vy < -9.0f) { dust(10, 0.55f); snd_tone(170, 90, 0.07f, 0, 0.08f); G.land_t = 0.16f; }
            G.y = gnd; G.vy = 0.0f; G.air = 0;
            if (G.glide > 0.0f) { G.glide = 0.0f; G.invuln = maxf(G.invuln, 0.6f); }
        }
    } else {
        if (gnd < G.y - 0.05f) { G.air = 1; G.vy = 0.0f; }
        else G.y = gnd;
    }
    G.ground = surface(G.x, G.z, G.y + 0.01f);

    if (G.roll_t > 0.0f) G.roll_t -= dt;
    if (G.land_t > 0.0f) G.land_t -= dt;
    if (G.stagger > 0.0f) G.stagger -= dt;
    if (G.invuln > 0.0f) G.invuln -= dt;
    if (G.magnet_t > 0.0f) G.magnet_t -= dt;
    if (G.sneak_t > 0.0f) G.sneak_t -= dt;
    if (G.mult_t > 0.0f) G.mult_t -= dt;
    G.stumble_t += dt;
    G.phase += dt * PA_TAU * (1.45f + G.speed * 0.065f);

    /* Hoverboard: double tap to ride; the demo rider boards once mid-run. */
    if (G.tap_t > 0.0f) G.tap_t -= dt;
    if (in->tapped && !in_rect(pause_rect(), in->x, in->y)) {
        if (G.tap_t > 0.0f) { board_on(); G.tap_t = 0.0f; } else G.tap_t = 0.32f;
    }
    if (pa_demo_mode() == 1 && G.run_t >= 11.0f && G.run_t - dt < 11.0f) board_on();
    if (G.board_t > 0.0f) {
        G.board_t -= dt;
        G.trail_t += dt;
        while (G.trail_t >= 1.0f / 60.0f) {
            G.trail_t -= 1.0f / 60.0f;
            for (int i = 23; i > 0; i--) G.trail[i] = G.trail[i - 1];
            G.trail[0] = v3(G.x - G.xv * 0.012f, G.y + 0.28f, G.z - 0.85f);
            if (G.ntrail < 24) G.ntrail++;
        }
    } else if (G.ntrail > 0) {
        G.trail_t += dt;
        if (G.trail_t > 0.02f) { G.trail_t = 0.0f; G.ntrail--; }
    }

    /* Moving trains wake when the runner gets within range, with a horn. */
    for (int i = 0; i < G.nobs; i++) {
        Obs *o = &G.obs[i];
        o->zprev = o->z;
        if (!o->moving) continue;
        if (!o->active && o->z - G.z < ACT_D) {
            o->active = 1;
            snd_tone(330, 330, 0.45f, 2, 0.05f);
            snd_tone(415, 415, 0.45f, 2, 0.04f);
        }
        if (o->active) o->z -= o->v * dt;
    }

    /* Collisions. */
    if (G.invuln <= 0.0f && G.jet_t <= 0.0f && G.glide <= 0.0f) {
        float pz0 = G.z - 0.3f, pz1 = G.z + 0.3f;
        float px0 = G.x - 0.34f, px1 = G.x + 0.34f;
        float py0 = G.y, py1 = G.y + (G.roll_t > 0.0f ? ROLL_H : BODY_H);
        for (int i = 0; i < G.nobs; i++) {
            Obs *o = &G.obs[i];
            float lx = lane_x(o->lane);
            float ox0, ox1, oy0, oy1, oz0, oz1;
            switch (o->kind) {
                case OB_TRAIN:  ox0 = lx - TRAIN_W * 0.5f; ox1 = lx + TRAIN_W * 0.5f; oy0 = 0.0f; oy1 = TRAIN_H - 0.8f;
                                oz0 = o->z; oz1 = o->z + o->len; break;
                case OB_LOW:    ox0 = lx - 1.25f; ox1 = lx + 1.25f; oy0 = 0.0f; oy1 = LOW_H; oz0 = o->z - 0.12f; oz1 = o->z + 0.12f; break;
                case OB_HIGH:   ox0 = lx - 1.25f; ox1 = lx + 1.25f; oy0 = HIGH_Y0; oy1 = HIGH_Y1; oz0 = o->z - 0.12f; oz1 = o->z + 0.12f; break;
                default:        ox0 = lx - 0.36f; ox1 = lx + 0.36f; oy0 = 0.0f; oy1 = 3.4f; oz0 = o->z - 0.3f; oz1 = o->z + 0.3f; break;
            }
            if (px1 <= ox0 || px0 >= ox1 || py1 <= oy0 || py0 >= oy1 || pz1 <= oz0 || pz0 >= oz1) continue;
            float prev_front = G.zprev + 0.3f;
            float prev_near = (o->kind == OB_TRAIN) ? o->zprev : o->zprev - (o->kind == OB_SIGNAL ? 0.3f : 0.12f);
            if (o->moving) prev_near = o->zprev;
            if (board_save()) break;
            if (prev_front <= prev_near + 0.05f) {
                crash(o->kind == OB_TRAIN || o->kind == OB_SIGNAL ? CR_TRAIN : CR_TRIP,
                      o->kind == OB_TRAIN || o->kind == OB_SIGNAL ? oz0 - 0.32f : G.z);
                return;
            }
            stumble();
            if (G.st != ST_RUN) return;
            break;
        }
    }

    /* Coins: magnet pull, then pickup. */
    for (int i = G.ncoin - 1; i >= 0; i--) {
        Coin *c = &G.coin[i];
        if (G.magnet_t > 0.0f && !c->pulled && c->z > G.z - 0.5f && c->z < G.z + 18.0f &&
            fabsf(c->y - (G.y + 1.0f)) < 6.0f) c->pulled = 1;
        if (c->pulled) {
            float dx = G.x - c->x, dy = G.y + 1.1f - c->y, dz = G.z + 0.4f - c->z;
            float d = sqrtf(dx * dx + dy * dy + dz * dz);
            float step = (26.0f + G.speed) * dt;
            if (d <= step + 0.4f) { collect_coin(c); G.coin[i] = G.coin[--G.ncoin]; continue; }
            c->x += dx / d * step; c->y += dy / d * step; c->z += dz / d * step + G.speed * dt;
            continue;
        }
        if (fabsf(c->z - G.z) < 0.9f && fabsf(c->x - G.x) < 0.95f && fabsf(c->y - (G.y + 0.9f)) < 1.3f) {
            collect_coin(c);
            G.coin[i] = G.coin[--G.ncoin];
        }
    }
    for (int i = G.npick - 1; i >= 0; i--) {
        Pick *p = &G.pick[i];
        if (fabsf(p->z - G.z) < 1.0f && fabsf(p->x - G.x) < 1.0f && fabsf(p->y - (G.y + 1.0f)) < 1.5f) {
            Pick copy = *p;
            G.pick[i] = G.pick[--G.npick];
            activate(copy.kind, &copy);
        }
    }

    /* Score and the multiplier ladder. */
    G.score += G.speed * dt * 2.6f * (float)score_mult();
    /* The multiplier climbs a widening ladder of distances. */
    static const float LADDER[] = { 150.0f, 400.0f, 750.0f, 1200.0f, 1800.0f, 2500.0f, 3300.0f, 4200.0f };
    int lvl = 0;
    while (lvl < 8 && G.dist >= LADDER[lvl]) lvl++;
    if (lvl != G.level) {
        G.level = lvl;
        char b[24];
        snprintf(b, sizeof(b), "X%d MULTIPLIER", 1 + lvl);
        banner(b, pa_hex(0xFFD23A));
        if (!g_mute) pa_sfx("levelup");
    }

    /* The chaser: close behind at the start and after a stumble, else gone. */
    float want_d = (G.run_t < 3.2f || G.stumble_t < CHASE_WINDOW) ? 0.9f : 9.0f;
    G.chase_d = pa_approach(G.chase_d, want_d, G.chase_d < want_d ? 0.7f : 3.0f, dt);
    G.chase_x = pa_approach(G.chase_x, G.x + 1.45f, 3.0f, dt);
    G.chase_phase += dt * PA_TAU * 2.6f;
    world_upkeep();
}

static void camera_follow(float dt) {
    /* Backpack secondary motion: a damped spring chasing the body's own
       vertical and sideways movement plus the step bounce. */
    {
        int running = G.st == ST_RUN && !G.air && G.board_t <= 0.0f;
        float tp = pa_clampf(-G.vy * 0.03f, -0.6f, 0.6f) + (running ? 0.24f * sinf(G.phase * 2.0f) : 0.0f);
        float tr = pa_clampf(-G.xv * 0.035f, -0.5f, 0.5f) + (running ? 0.10f * sinf(G.phase) : 0.0f);
        G.pack_pv += (150.0f * (tp - G.pack_p) - 8.0f * G.pack_pv) * dt;
        G.pack_p += G.pack_pv * dt;
        G.pack_rv += (150.0f * (tr - G.pack_r) - 8.0f * G.pack_rv) * dt;
        G.pack_r += G.pack_rv * dt;
    }
    float gy = G.jet_t > 0.0f || G.glide > 0.0f ? G.y : G.ground;
    if (G.st == ST_RUN && !G.air) gy = G.y;
    G.cam_gy = pa_approach(G.cam_gy, gy, G.jet_t > 0.0f ? 3.0f : 5.0f, dt);
    float air = (G.jet_t > 0.0f || G.glide > 0.0f) ? 0.0f : maxf(0.0f, G.y - G.ground) * 0.35f;
    G.cam_air = pa_approach(G.cam_air, air, 8.0f, dt);
    int down0 = G.st == ST_CRASH || G.st == ST_REVIVE || G.st == ST_RESULTS;
    G.cam_x = pa_approach(G.cam_x, down0 ? G.x : G.x * 0.72f, down0 ? 2.0f : 7.0f, dt);
    int down = G.st == ST_CRASH || G.st == ST_REVIVE || G.st == ST_RESULTS;
    G.cam_crash = pa_approach(G.cam_crash, down && G.st_t > 0.3f ? 1.0f : (down ? G.cam_crash : 0.0f), 2.5f, dt);
}

static void rn_update(float dt, const PA_Input *in) {
    G.time += dt;
    G.st_t += dt;
    if (G.banner_t > 0.0f) G.banner_t -= dt;
    if (G.shake > 0.0f) G.shake = maxf(0.0f, G.shake - dt * 2.2f);
    if (G.flash > 0.0f) G.flash = maxf(0.0f, G.flash - dt * 2.0f);
    if (G.coin_pop > 0.0f) G.coin_pop = maxf(0.0f, G.coin_pop - dt * 5.0f);
    if (G.streak_t > 0.0f) G.streak_t -= dt;

    /* Our own pause button, in the reference's top-left blue square. */
    if (in->pressed && (G.st == ST_RUN) && in_rect(pause_rect(), in->x, in->y)) { pa_hub_pause(); return; }

    switch (G.st) {
    case ST_MENU:
        G.phase += dt * 2.0f;
        G.chase_phase += dt * 2.0f;
        if (in->tapped || in->swipe || in->key_pressed[PA_KEY_SPACE] || in->key_pressed[PA_KEY_ENTER] ||
            in->key_pressed[PA_KEY_UP]) begin_running();
        break;
    case ST_RUN:
        run_step(dt, in);
        break;
    case ST_CRASH:
        G.chase_d = pa_approach(G.chase_d, 0.45f, 2.0f, dt);
        G.chase_x = pa_approach(G.chase_x, G.x - G.fall_dir * 0.95f, 3.0f, dt);
        G.chase_phase += dt * PA_TAU * 2.0f;
        if (G.y > G.ground && G.crash_kind != CR_TRAIN) { G.vy -= GRAVITY * dt; G.y = maxf(G.ground, G.y + G.vy * dt); }
        if (G.crash_kind == CR_TRAIN && G.st_t > 0.35f) G.y = pa_approach(G.y, surface(G.x, G.z - 1.0f, G.y), 6.0f, dt);
        if (G.st_t > 1.5f) { G.st = ST_REVIVE; G.st_t = 0.0f; }
        break;
    case ST_REVIVE: {
        int can = g_keys >= revive_cost();
        if (in->tapped && can && in_rect(revive_button(), in->x, in->y)) revive();
        else if (in->tapped && in_rect(skip_button(), in->x, in->y)) finish_run();
        else if (in->key_pressed[PA_KEY_ENTER] && can) revive();
        else if (G.st_t > 4.0f) finish_run();
        else if ((int)(G.st_t * 1.0f) != (int)((G.st_t - dt) * 1.0f)) snd_tone(880, 880, 0.05f, 0, 0.05f);
        break;
    }
    case ST_RESULTS:
        if (G.st_t > 0.6f) {
            if ((in->tapped && in_rect(play_button(), in->x, in->y)) || in->key_pressed[PA_KEY_ENTER] ||
                in->key_pressed[PA_KEY_SPACE]) {
                if (!g_mute) pa_sfx("select");
                new_run(0);
                begin_running();
                return;
            }
            if (in->tapped && in_rect(home_button(), in->x, in->y)) { pa_hub_exit(); return; }
        }
        break;
    }
    update_particles(dt);
    camera_follow(dt);
}

static void rn_start(void) {
    new_run(1);
}

static void rn_stop(void) { pa_save_flush(); }

/* ============================================================ DRAWING === */
/* ---- character rig ---- */
typedef struct {
    float hipL, kneeL, hipR, kneeR, legOut;
    float shL, elL, shR, elR, outL, outR;
    float lean, bob, head, twist;
} Pose;

typedef struct {
    float x, y, z, yaw, roll, pitch, piv, s;
    float cy_, sy_, cr, sr, cpt, spt;
} Place;

typedef struct {
    uint32_t skin, hair, top, top2, pants, shoe, shoe2, cap, cap2, pack, pack2;
    float girth;
    int kind;              /* 0 runner, 1 inspector */
} Look;

static const Look RUNNER = { 0xE9A47C, 0x4A2A1C, 0xFFB52A, 0xF0841A, 0x2F5BC0, 0xF6F6F6, 0xE8343C,
                             0xE8323C, 0xA81E28, 0x6E4BD8, 0x9A7BF4, 1.0f, 0 };
static const Look INSPECTOR = { 0xE5A07A, 0x7A7470, 0x587A3C, 0x41602A, 0x46553A, 0x2C2C30, 0x1C1C20,
                                0x2F4426, 0x22331C, 0, 0, 1.28f, 1 };

static void place_init(Place *p) {
    p->cy_ = cosf(p->yaw); p->sy_ = sinf(p->yaw);
    p->cr = cosf(p->roll); p->sr = sinf(p->roll);
    p->cpt = cosf(p->pitch); p->spt = sinf(p->pitch);
}

static V3 xf(const Place *p, float lx, float ly, float lz) {
    float y1 = ly - p->piv;
    float y2 = y1 * p->cpt - lz * p->spt + p->piv;
    float z2 = y1 * p->spt + lz * p->cpt;
    float x3 = lx * p->cr + y2 * p->sr;
    float y3 = -lx * p->sr + y2 * p->cr;
    float wx = x3 * p->cy_ + z2 * p->sy_;
    float wz = -x3 * p->sy_ + z2 * p->cy_;
    return v3(p->x + wx * p->s, p->y + y3 * p->s, p->z + wz * p->s);
}

/* ---- cel shading ----
   Every body shape is a 2D hull of projected circles painted in four layers:
   a warm rim sliver on the right edge, the shadow tone, the mid tone pushed
   toward the key light, and a small light patch. Hard edges between the
   tones are what make it read as cel-shaded rather than airbrushed. */
static PA_Color tone_shadow(PA_Color c) { return pa_mix(pa_shade(c, -0.14f), pa_hex(0x6A4C8C), 0.30f); }
static PA_Color tone_light(PA_Color c)  { return pa_shade(pa_mix(c, pa_hex(0xFFD9A0), 0.32f), 0.10f); }

static int hull_cmp(const void *a, const void *b) {
    const PA_Vec2 *p = (const PA_Vec2 *)a, *q = (const PA_Vec2 *)b;
    if (p->x != q->x) return p->x < q->x ? -1 : 1;
    return p->y < q->y ? -1 : (p->y > q->y ? 1 : 0);
}

static float hcross(PA_Vec2 o, PA_Vec2 a, PA_Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

static void hull_fill(PA_Canvas *c, const PA_Vec2 *ce, const float *r, int n, float dx, float dy, float k,
                      PA_Color col) {
    PA_Vec2 pts[8 * 18], h[8 * 18 + 2];
    int m = 0;
    for (int i = 0; i < n && i < 8; i++) {
        float rad = r[i] * k;
        if (rad < 0.3f) rad = 0.3f;
        int seg = rad > 14.0f ? 18 : 12;
        for (int s = 0; s < seg; s++) {
            float a = PA_TAU * (float)s / (float)seg;
            pts[m].x = ce[i].x + dx * r[i] + cosf(a) * rad;
            pts[m].y = ce[i].y + dy * r[i] + sinf(a) * rad;
            m++;
        }
    }
    if (n == 1) { fpoly(c, pts, m, col); return; }
    qsort(pts, (size_t)m, sizeof(PA_Vec2), hull_cmp);
    int t = 0;
    for (int i = 0; i < m; i++) {
        while (t >= 2 && hcross(h[t - 2], h[t - 1], pts[i]) <= 0.0f) t--;
        h[t++] = pts[i];
    }
    for (int i = m - 2, lo = t + 1; i >= 0; i--) {
        while (t >= lo && hcross(h[t - 2], h[t - 1], pts[i]) <= 0.0f) t--;
        h[t++] = pts[i];
    }
    fpoly(c, h, t - 1, col);
}

static void cel(PA_Canvas *c, const PA_Vec2 *ce, const float *r, int n, PA_Color col, float alpha, int rim) {
    float rmax = 0.0f;
    for (int i = 0; i < n; i++) rmax = maxf(rmax, r[i]);
    if (rim) {
        float o = maxf(1.6f, rmax * 0.12f) / maxf(rmax, 0.5f);
        hull_fill(c, ce, r, n, o, -o * 0.55f, 1.0f, pa_alpha(pa_hex(0xFFF0D2), alpha));
    }
    hull_fill(c, ce, r, n, 0.0f, 0.0f, 1.0f, pa_alpha(tone_shadow(col), alpha));
    hull_fill(c, ce, r, n, -0.17f, -0.13f, 0.80f, pa_alpha(col, alpha));
    if (rmax > 2.5f) hull_fill(c, ce, r, n, -0.40f, -0.34f, 0.36f, pa_alpha(tone_light(col), alpha * 0.9f));
}

/* World-space circles projected and celled as one shape. */
static void wcel(PA_Canvas *c, const V3 *p, const float *rw, int n, PA_Color col, float alpha) {
    PA_Vec2 ce[8];
    float r[8];
    for (int i = 0; i < n && i < 8; i++) {
        float d;
        if (!proj(p[i].x, p[i].y, p[i].z, &ce[i], &d)) return;
        r[i] = rw[i] * K.F / d;
    }
    cel(c, ce, r, n, col, alpha, 1);
}

static void limb(PA_Canvas *c, V3 a, V3 b, float ra, float rb, PA_Color col, float alpha) {
    V3 p[2] = { a, b };
    float r[2] = { ra, rb };
    wcel(c, p, r, 2, col, alpha);
}

/* Oriented box in body space: centre and three half-axes. Lit like the world. */
static void obox(PA_Canvas *c, const Place *pl, V3 ce, V3 ax, V3 ay, V3 az, PA_Color col, float alpha) {
    V3 w[8];
    for (int i = 0; i < 8; i++) {
        float sx = (i & 1) ? 1.0f : -1.0f, sy = (i & 2) ? 1.0f : -1.0f, sz = (i & 4) ? 1.0f : -1.0f;
        w[i] = xf(pl, ce.x + ax.x * sx + ay.x * sy + az.x * sz, ce.y + ax.y * sx + ay.y * sy + az.y * sz,
                  ce.z + ax.z * sx + ay.z * sy + az.z * sz);
    }
    static const int F[6][4] = { { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 6, 7, 5 } };
    V3 cen = v3(0, 0, 0);
    for (int i = 0; i < 8; i++) { cen.x += w[i].x * 0.125f; cen.y += w[i].y * 0.125f; cen.z += w[i].z * 0.125f; }
    for (int f = 0; f < 6; f++) {
        V3 fc = v3(0, 0, 0);
        for (int k = 0; k < 4; k++) { fc.x += w[F[f][k]].x * 0.25f; fc.y += w[F[f][k]].y * 0.25f; fc.z += w[F[f][k]].z * 0.25f; }
        V3 n = v3(fc.x - cen.x, fc.y - cen.y, fc.z - cen.z);
        if (n.x * (K.x - fc.x) + n.y * (K.y - fc.y) + n.z * (K.z - fc.z) <= 0.0f) continue;
        V3 q[4] = { w[F[f][0]], w[F[f][1]], w[F[f][2]], w[F[f][3]] };
        wpoly(c, q, 4, pa_alpha(col, alpha));
    }
}

typedef struct { float d; int kind, side; V3 a, b; float ra, rb; PA_Color col; } BodyPart;

/* Rotate a body-local point about a vertical axis through `piv` (twist). */
static V3 local_rot_y(V3 o, V3 piv, float a) {
    float dx = o.x - piv.x, dz = o.z - piv.z;
    return v3(piv.x + dx * cosf(a) + dz * sinf(a), o.y, piv.z - dx * sinf(a) + dz * cosf(a));
}

static void head_draw(PA_Canvas *c, const Place *pl, V3 hc_l, const Look *lk, float alpha, float tilt) {
    V3 hc = xf(pl, hc_l.x, hc_l.y, hc_l.z);
    PA_Vec2 hs;
    float hd;
    if (!proj(hc.x, hc.y, hc.z, &hs, &hd)) return;
    float s = pl->s;
    float R = 0.30f * s * K.F / hd;
    float cl = cosf(tilt), sl = sinf(tilt);
    V3 up = xf(pl, hc_l.x, hc_l.y + cl, hc_l.z + sl);
    V3 fw = xf(pl, hc_l.x, hc_l.y - sl, hc_l.z + cl);
    V3 rt = xf(pl, hc_l.x + 1.0f, hc_l.y, hc_l.z);
    up.x -= hc.x; up.y -= hc.y; up.z -= hc.z;
    fw.x -= hc.x; fw.y -= hc.y; fw.z -= hc.z;
    rt.x -= hc.x; rt.y -= hc.y; rt.z -= hc.z;
#define HP(ax, ay, az) v3(hc.x + rt.x * (ax) + up.x * (ay) + fw.x * (az), hc.y + rt.y * (ax) + up.y * (ay) + fw.y * (az), \
                          hc.z + rt.z * (ax) + up.z * (ay) + fw.z * (az))
    float tx = K.x - hc.x, ty = K.y - hc.y, tz = K.z - hc.z;
    float tl = sqrtf(tx * tx + ty * ty + tz * tz);
    float facing = (fw.x * tx + fw.y * ty + fw.z * tz) / (tl * s + 1e-4f);
    PA_Vec2 us; float ud;
    V3 upp = HP(0, 0.3f, 0);
    if (!proj(upp.x, upp.y, upp.z, &us, &ud)) return;
    float ang = atan2f(us.y - hs.y, us.x - hs.x);
    PA_Color skin = pa_hex(lk->skin), hair = pa_hex(lk->hair), cap = pa_hex(lk->cap), cap2 = pa_hex(lk->cap2);
    int runner = lk->kind == 0;

    /* Brim: behind the skull when we see the back of the head. */
    V3 bq[4] = { HP(-0.23f, 0.10f, 0.20f), HP(0.23f, 0.10f, 0.20f), HP(0.19f, 0.05f, 0.50f), HP(-0.19f, 0.05f, 0.50f) };
    int brim_first = facing < 0.0f;
    if (brim_first) wpoly(c, bq, 4, pa_alpha(cap2, alpha));

    /* Ears, then the skull: hair round the back, skin where the face turns. */
    for (int sd = -1; sd <= 1; sd += 2) {
        V3 e = HP(0.29f * (float)sd, -0.04f, -0.02f);
        V3 p1[1] = { e }; float r1[1] = { 0.075f * s };
        if (depth_of(e.x, e.y, e.z) > hd - 0.05f) wcel(c, p1, r1, 1, skin, alpha);
    }
    {
        PA_Vec2 ce[1] = { hs }; float r[1] = { R };
        cel(c, ce, r, 1, runner ? hair : skin, alpha, 1);
    }
    if (facing > -0.35f) {
        V3 f = HP(0, -0.03f, 0.10f);
        V3 p1[1] = { f }; float r1[1] = { 0.255f * s };
        wcel(c, p1, r1, 1, skin, alpha);
    }
    if (!runner && facing < 0.4f) {
        V3 f = HP(0, -0.08f, -0.08f);
        V3 p1[1] = { f }; float r1[1] = { 0.24f * s };
        wcel(c, p1, r1, 1, hair, alpha);
    }
    if (facing > 0.35f) {
        for (int sd = -1; sd <= 1; sd += 2) {
            V3 e = HP(0.10f * (float)sd, 0.02f, 0.26f);
            PA_Vec2 es; float ed;
            if (!proj(e.x, e.y, e.z, &es, &ed)) continue;
            float er = 0.058f * s * K.F / ed;
            fell(c, es.x, es.y, er * 0.85f, er * 1.12f, pa_alpha(PA_RGB(255, 255, 255), alpha));
            fcirc(c, es.x, es.y + er * 0.15f, er * 0.58f, pa_alpha(pa_hex(0x2A1A12), alpha));
            fcirc(c, es.x - er * 0.18f, es.y - er * 0.12f, er * 0.22f, pa_alpha(PA_RGB(255, 255, 255), alpha));
            pa_line(c, es.x - er, es.y - er * 1.7f + (float)sd * er * 0.15f, es.x + er, es.y - er * 1.7f - (float)sd * er * 0.15f,
                    maxf(1.2f, er * 0.4f), pa_alpha(pa_shade(hair, -0.2f), alpha));
        }
        V3 m = HP(0, -0.12f, 0.27f);
        PA_Vec2 ms; float md;
        if (proj(m.x, m.y, m.z, &ms, &md)) {
            float mr = 0.075f * s * K.F / md;
            if (!runner) fell(c, ms.x, ms.y - mr * 0.5f, mr * 1.8f, mr * 0.62f, pa_alpha(pa_hex(0x6E6A66), alpha));
            else {
                PA_Vec2 q[5] = { { ms.x - mr, ms.y - mr * 0.25f }, { ms.x + mr, ms.y - mr * 0.25f }, { ms.x + mr * 0.6f, ms.y + mr * 0.5f },
                                 { ms.x, ms.y + mr * 0.72f }, { ms.x - mr * 0.6f, ms.y + mr * 0.5f } };
                fpoly(c, q, 5, pa_alpha(pa_hex(0x7A2A22), alpha));
                fell(c, ms.x, ms.y - mr * 0.08f, mr * 0.78f, mr * 0.2f, pa_alpha(PA_RGB(255, 255, 255), alpha));
            }
        }
    }

    /* Hair spilling out under the cap: spiky tufts round the back and sides. */
    if (runner) {
        for (int k = 0; k < 7; k++) {
            float a = PA_PI * (0.15f + 0.7f * (float)k / 6.0f);
            float ox = cosf(a) * 0.27f, oz = -sinf(a) * 0.25f;
            V3 b0 = HP(ox * 1.02f, 0.02f, oz * 1.02f), b1 = HP(ox * 1.14f, -0.11f - 0.03f * (float)(k & 1), oz * 1.12f);
            PA_Vec2 s0, s1; float d0, d1;
            if (!proj(b0.x, b0.y, b0.z, &s0, &d0) || !proj(b1.x, b1.y, b1.z, &s1, &d1)) continue;
            if (d1 > hd + 0.25f * s) continue;
            float w = 0.07f * s * K.F / d0;
            float dx = s1.x - s0.x, dy = s1.y - s0.y, l = sqrtf(dx * dx + dy * dy) + 1e-3f;
            PA_Vec2 tri[3] = { { s0.x - dy / l * w, s0.y + dx / l * w }, { s0.x + dy / l * w, s0.y - dx / l * w }, s1 };
            fpoly(c, tri, 3, pa_alpha(pa_shade(hair, -0.1f), alpha));
        }
    }

    /* Cap: a dome over the top of the skull in three cel tones. */
    {
        PA_Vec2 q[40];
        int n = 0;
        float cr = R * 1.06f;
        float ox = cosf(ang) * R * 0.12f, oy = sinf(ang) * R * 0.12f;
        for (int k = 0; k <= 20; k++) {
            float a = ang - 1.66f + 3.32f * (float)k / 20.0f;
            q[n].x = hs.x + ox + cosf(a) * cr; q[n].y = hs.y + oy + sinf(a) * cr; n++;
        }
        fpoly(c, q, n, pa_alpha(tone_shadow(cap), alpha));
        n = 0;
        for (int k = 0; k <= 16; k++) {
            float a = ang - 1.45f + 2.6f * (float)k / 16.0f;
            q[n].x = hs.x + ox * 1.6f - R * 0.10f + cosf(a) * cr * 0.86f; q[n].y = hs.y + oy * 1.6f - R * 0.07f + sinf(a) * cr * 0.86f; n++;
        }
        fpoly(c, q, n, pa_alpha(cap, alpha));
        n = 0;
        for (int k = 0; k <= 10; k++) {
            float a = ang - 0.9f + 1.1f * (float)k / 10.0f;
            q[n].x = hs.x + ox * 2.2f - R * 0.2f + cosf(a) * cr * 0.55f; q[n].y = hs.y + oy * 2.2f - R * 0.15f + sinf(a) * cr * 0.55f; n++;
        }
        fpoly(c, q, n, pa_alpha(tone_light(cap), alpha * 0.9f));
        fcirc(c, hs.x + cosf(ang) * cr * 1.02f, hs.y + sinf(ang) * cr * 1.02f, R * 0.11f, pa_alpha(cap2, alpha));
        if (runner && facing < 0.2f) {
            /* The strap gap at the back, hair showing through it. */
            V3 g = HP(0, 0.04f, -0.29f);
            PA_Vec2 gs; float gd;
            if (proj(g.x, g.y, g.z, &gs, &gd)) {
                float gr = 0.075f * s * K.F / gd;
                fell(c, gs.x, gs.y, gr * 1.2f, gr * 0.75f, pa_alpha(pa_shade(hair, -0.05f), alpha));
                pa_line(c, gs.x - gr * 1.3f, gs.y + gr * 0.7f, gs.x + gr * 1.3f, gs.y + gr * 0.7f, maxf(1.2f, gr * 0.35f), pa_alpha(cap2, alpha));
            }
        }
        if (!runner && facing > 0.2f) {
            V3 b = HP(0, 0.17f, 0.27f);
            PA_Vec2 bs; float bd;
            if (proj(b.x, b.y, b.z, &bs, &bd)) fcirc(c, bs.x, bs.y, R * 0.14f, pa_alpha(pa_hex(0xF4C430), alpha));
        }
    }
    if (!brim_first) wpoly(c, bq, 4, pa_alpha(cap2, alpha));

    /* Headphones: a band over the cap and bright cups on the ears. */
    if (runner) {
        V3 prev = HP(-0.30f, 0.0f, 0.0f);
        for (int k = 1; k <= 8; k++) {
            float a = PA_PI * (float)k / 8.0f;
            V3 cur = HP(-cosf(a) * 0.31f, sinf(a) * 0.33f, 0.03f);
            wline(c, prev, cur, 0.045f * s, pa_alpha(pa_hex(0x2A2C3A), alpha), 1.5f);
            prev = cur;
        }
        for (int sd = -1; sd <= 1; sd += 2) {
            V3 e = HP(0.30f * (float)sd, -0.02f, 0.0f);
            V3 p1[1] = { e }; float r1[1] = { 0.11f * s };
            wcel(c, p1, r1, 1, pa_hex(0x2A2C3A), alpha);
            V3 e2 = HP(0.33f * (float)sd, -0.02f, 0.0f);
            V3 p2[1] = { e2 }; float r2[1] = { 0.075f * s };
            wcel(c, p2, r2, 1, pa_hex(0x2EC4F0), alpha);
        }
    }
#undef HP
}

static void humanoid(PA_Canvas *c, const Place *pl, const Pose *po, const Look *lk, float alpha, int jet, int magnet,
                     int sneak) {
    float g = lk->girth;
    float py = 1.02f + po->bob;
    float cl = cosf(po->lean), sl = sinf(po->lean);
    V3 pel = v3(0, py, 0);
    V3 chest = v3(0, py + cl * 0.50f, sl * 0.50f);
    V3 neck = v3(0, chest.y + cl * 0.15f, chest.z + sl * 0.15f);
    float hl = po->lean * 0.45f + po->head;
    V3 head = v3(0, neck.y + cosf(hl) * 0.28f, neck.z + sinf(hl) * 0.28f);
    float tw = po->twist;

    BodyPart parts[32];
    int n = 0;
    PA_Color pants = pa_hex(lk->pants), top = pa_hex(lk->top), top2 = pa_hex(lk->top2), skin = pa_hex(lk->skin);
#define PART(K_, S_, A_, B_, RA_, RB_, C_) do { BodyPart *p_ = &parts[n++]; p_->kind = (K_); p_->side = (S_); \
        p_->a = (A_); p_->b = (B_); p_->ra = (RA_); p_->rb = (RB_); p_->col = (C_); } while (0)

    for (int sd = -1; sd <= 1; sd += 2) {
        float a = sd < 0 ? po->hipL : po->hipR, k = sd < 0 ? po->kneeL : po->kneeR;
        float out = po->legOut * (float)sd;
        V3 hip = local_rot_y(v3(0.13f * (float)sd * g, py - 0.05f, 0), pel, -tw * 0.4f);
        V3 knee = v3(hip.x + sinf(out) * 0.46f, hip.y - cosf(a) * cosf(out) * 0.46f, hip.z + sinf(a) * cosf(out) * 0.46f);
        float th = a - k;
        V3 ank = v3(knee.x + sinf(out) * 0.2f, knee.y - cosf(th) * 0.45f, knee.z + sinf(th) * 0.45f);
        PART(0, sd, hip, knee, 0.14f * g, 0.115f, pants);
        PART(1, sd, knee, ank, 0.11f, 0.095f, pants);
        float fdy = sinf(th), fdz = cosf(th);
        V3 heel = v3(ank.x, ank.y - fdy * 0.08f - 0.04f, ank.z - fdz * 0.08f);
        V3 toe = v3(ank.x, ank.y + fdy * 0.21f - 0.04f, ank.z + fdz * 0.21f);
        PART(2, sd, heel, toe, 0.105f, 0.095f, sneak ? pa_hex(0x5BE05A) : pa_hex(lk->shoe));
    }
    for (int sd = -1; sd <= 1; sd += 2) {
        float s_ = sd < 0 ? po->shL : po->shR, e = sd < 0 ? po->elL : po->elR, o = sd < 0 ? po->outL : po->outR;
        V3 sh = local_rot_y(v3(0.27f * (float)sd * g, chest.y + 0.03f * cl, chest.z + 0.03f * sl), chest, tw);
        V3 el = v3(sh.x + sinf(o) * 0.31f * (float)sd, sh.y - cosf(s_) * cosf(o) * 0.31f, sh.z + sinf(s_) * cosf(o) * 0.31f);
        V3 ha = v3(el.x + sinf(o) * 0.12f * (float)sd, el.y - cosf(s_ + e) * 0.29f, el.z + sinf(s_ + e) * 0.29f);
        PART(3, sd, sh, el, 0.115f * g, 0.10f, top);
        PART(4, sd, el, ha, 0.098f, 0.085f, lk->kind == 1 ? top : top2);
        PART(5, sd, ha, ha, 0.09f, 0.09f, skin);
        if (magnet && sd > 0) PART(9, sd, ha, ha, 0.2f, 0, 0);
    }
    PART(6, 0, pel, chest, 0, 0, top);
    if (lk->kind == 0)
        PART(7, 0, v3(0, py + cl * 0.30f + sl * 0.22f, sl * 0.30f - cl * 0.22f), pel, 0, 0,
             jet ? pa_hex(0xD8343C) : pa_hex(lk->pack));
    PART(8, 0, head, head, 0.30f, 0, 0);

    for (int i = 0; i < n; i++) {
        V3 m = xf(pl, (parts[i].a.x + parts[i].b.x) * 0.5f, (parts[i].a.y + parts[i].b.y) * 0.5f,
                  (parts[i].a.z + parts[i].b.z) * 0.5f);
        parts[i].d = depth_of(m.x, m.y, m.z);
        if (parts[i].kind == 6) parts[i].d += 0.03f;
    }
    for (int i = 1; i < n; i++) {
        BodyPart key = parts[i];
        int j = i - 1;
        while (j >= 0 && parts[j].d < key.d) { parts[j + 1] = parts[j]; j--; }
        parts[j + 1] = key;
    }
    float S = pl->s;
    for (int i = 0; i < n; i++) {
        BodyPart *b = &parts[i];
        V3 wa = xf(pl, b->a.x, b->a.y, b->a.z), wb = xf(pl, b->b.x, b->b.y, b->b.z);
        switch (b->kind) {
        case 0:
            limb(c, wa, wb, b->ra * S, b->rb * S, b->col, alpha);
            if (lk->kind == 0) {
                /* Back pocket on the seat of the jeans. */
                V3 pk = xf(pl, b->a.x, b->a.y - 0.06f, b->a.z - 0.11f);
                V3 p1[1] = { pk }; float r1[1] = { 0.055f * S };
                if (depth_of(pk.x, pk.y, pk.z) < depth_of(wa.x, wa.y, wa.z)) wcel(c, p1, r1, 1, pa_shade(b->col, -0.18f), alpha);
            }
            break;
        case 1: {
            limb(c, wa, wb, b->ra * S, b->rb * S, b->col, alpha);
            /* Rolled cuff. */
            V3 cu = xf(pl, b->b.x, b->b.y + 0.07f, b->b.z);
            V3 p2[2] = { cu, wb }; float r2[2] = { 0.105f * S, 0.10f * S };
            wcel(c, p2, r2, 2, pa_shade(b->col, 0.25f), alpha);
            break;
        }
        case 2: {
            /* Sneaker: dark sole, white upper, a coloured flash down the side. */
            V3 s0 = xf(pl, b->a.x, b->a.y - 0.05f, b->a.z), s1 = xf(pl, b->b.x, b->b.y - 0.05f, b->b.z + 0.02f);
            limb(c, s0, s1, b->ra * S * 0.92f, b->rb * S * 0.9f, pa_hex(lk->kind == 0 ? 0x3A2E3A : 0x111114), alpha);
            limb(c, wa, wb, b->ra * S, b->rb * S, b->col, alpha);
            if (lk->kind == 0) {
                V3 f0 = xf(pl, b->a.x + 0.02f * (float)b->side, b->a.y + 0.01f, b->a.z + 0.03f);
                V3 f1 = xf(pl, b->b.x + 0.02f * (float)b->side, b->b.y - 0.02f, b->b.z - 0.04f);
                limb(c, f0, f1, 0.035f * S, 0.03f * S, sneak ? pa_hex(0x2E9E3A) : pa_hex(lk->shoe2), alpha);
            }
            break;
        }
        case 3: limb(c, wa, wb, b->ra * S, b->rb * S, b->col, alpha); break;
        case 4: {
            limb(c, wa, wb, b->ra * S, b->rb * S, b->col, alpha);
            if (lk->kind == 0) {
                V3 cf = xf(pl, b->b.x * 0.85f + b->a.x * 0.15f, b->b.y * 0.85f + b->a.y * 0.15f, b->b.z * 0.85f + b->a.z * 0.15f);
                V3 p1[1] = { cf }; float r1[1] = { 0.092f * S };
                wcel(c, p1, r1, 1, pa_hex(0xF6F2EA), alpha);
            }
            break;
        }
        case 5: { V3 p1[1] = { wa }; float r1[1] = { b->ra * S }; wcel(c, p1, r1, 1, b->col, alpha); break; }
        case 6: {
            /* Jeans seat, then the hoodie as one hull from hips to shoulders. */
            V3 hp[2] = { xf(pl, -0.13f * g, py - 0.02f, 0), xf(pl, 0.13f * g, py - 0.02f, 0) };
            float hr[2] = { 0.16f * g * S, 0.16f * g * S };
            wcel(c, hp, hr, 2, pants, alpha);
            V3 shl = local_rot_y(v3(-0.21f * g, chest.y + 0.02f, chest.z), chest, tw);
            V3 shr = local_rot_y(v3(0.21f * g, chest.y + 0.02f, chest.z), chest, tw);
            V3 tp[4] = { xf(pl, shl.x, shl.y, shl.z), xf(pl, shr.x, shr.y, shr.z),
                         xf(pl, -0.15f * g, py + 0.14f, sl * 0.14f), xf(pl, 0.15f * g, py + 0.14f, sl * 0.14f) };
            float tr[4] = { 0.16f * g * S, 0.16f * g * S, 0.17f * g * S, 0.17f * g * S };
            wcel(c, tp, tr, 4, top, alpha);
            V3 hb[2] = { xf(pl, -0.16f * g, py + 0.06f, sl * 0.06f), xf(pl, 0.16f * g, py + 0.06f, sl * 0.06f) };
            float hbr[2] = { 0.08f * S, 0.08f * S };
            wcel(c, hb, hbr, 2, lk->kind == 1 ? pa_hex(0x3A2A1E) : top2, alpha);
            if (lk->kind == 1) {
                V3 bk = xf(pl, 0, py + 0.07f, sl * 0.07f - 0.17f * g);
                V3 p1[1] = { bk }; float r1[1] = { 0.04f * S };
                wcel(c, p1, r1, 1, pa_hex(0xE0B040), alpha);
            }
            /* Neck, and the hood bunched behind it. */
            V3 nk = xf(pl, neck.x, neck.y - 0.05f, neck.z), hd2 = xf(pl, head.x, head.y - 0.14f, head.z);
            limb(c, nk, hd2, 0.08f * S, 0.075f * S, skin, alpha);
            if (lk->kind == 0) {
                V3 h3[3] = { xf(pl, -0.12f, neck.y - 0.04f, neck.z - 0.12f), xf(pl, 0.12f, neck.y - 0.04f, neck.z - 0.12f),
                             xf(pl, 0, neck.y + 0.04f, neck.z - 0.16f) };
                float h3r[3] = { 0.09f * S, 0.09f * S, 0.1f * S };
                if (depth_of(h3[2].x, h3[2].y, h3[2].z) < depth_of(nk.x, nk.y, nk.z) + 0.05f) wcel(c, h3, h3r, 3, top2, alpha);
            }
            break;
        }
        case 7: {
            /* Backpack swinging on its straps: pitched and rolled about its
               top edge by the spring in G.pack_p / G.pack_r. */
            float pp = G.pack_p, pr = G.pack_r;
            V3 ce = b->a;
            ce.z -= sinf(pp) * 0.12f;
            ce.y += (cosf(pp) - 1.0f) * 0.1f;
            ce.x += sinf(pr) * 0.08f;
            V3 ax = v3(0.18f * cosf(pr), -sinf(pr) * 0.18f, 0);
            V3 ay = v3(sinf(pr) * 0.21f, cl * 0.21f * cosf(pr), sl * 0.21f);
            V3 az = v3(0, sl * 0.10f + sinf(pp) * 0.05f, -cl * 0.10f);
            if (jet) {
                for (int sd = -1; sd <= 1; sd += 2) {
                    V3 t0 = xf(pl, ce.x + 0.13f * (float)sd, ce.y - 0.26f, ce.z), t1 = xf(pl, ce.x + 0.13f * (float)sd, ce.y + 0.22f, ce.z);
                    limb(c, t0, t1, 0.12f * S, 0.12f * S, pa_hex(0xD8343C), alpha);
                    V3 n0 = xf(pl, ce.x + 0.13f * (float)sd, ce.y - 0.32f, ce.z), n1 = xf(pl, ce.x + 0.13f * (float)sd, ce.y - 0.40f, ce.z);
                    limb(c, n0, n1, 0.08f * S, 0.10f * S, pa_hex(0x9AA0AE), alpha);
                }
                break;
            }
            for (int sd = -1; sd <= 1; sd += 2) {
                V3 s0 = xf(pl, 0.12f * (float)sd, chest.y + 0.08f, chest.z - 0.12f), s1 = xf(pl, 0.12f * (float)sd, ce.y + 0.15f, ce.z + 0.05f);
                limb(c, s0, s1, 0.035f * S, 0.035f * S, pa_hex(0x3A2A5A), alpha);
            }
            obox(c, pl, ce, ax, ay, az, b->col, alpha);
            V3 flap = v3(ce.x - az.x * 0.25f + ay.x * 0.45f, ce.y - az.y * 0.25f + ay.y * 0.45f, ce.z - az.z * 0.25f + ay.z * 0.45f);
            obox(c, pl, flap, v3(ax.x * 1.04f, ax.y * 1.04f, 0), v3(ay.x * 0.5f, ay.y * 0.5f, ay.z * 0.5f),
                 v3(az.x * 0.9f, az.y * 0.9f, az.z * 0.9f), pa_hex(lk->pack2), alpha);
            V3 pock = v3(ce.x - az.x * 1.15f - ay.x * 0.42f, ce.y - az.y * 1.15f - ay.y * 0.42f, ce.z - az.z * 1.15f - ay.z * 0.42f);
            obox(c, pl, pock, v3(ax.x * 0.62f, ax.y * 0.62f, 0), v3(ay.x * 0.36f, ay.y * 0.36f, ay.z * 0.36f),
                 v3(az.x * 0.3f, az.y * 0.3f, az.z * 0.3f), pa_hex(0xFFD23A), alpha);
            break;
        }
        case 8: head_draw(c, pl, head, lk, alpha, hl); break;
        case 9: {
            V3 a = xf(pl, b->a.x + 0.05f, b->a.y + 0.12f, b->a.z + 0.05f);
            PA_Vec2 s; float d;
            if (proj(a.x, a.y, a.z, &s, &d)) {
                float r = 0.17f * S * K.F / d;
                PA_Vec2 q[26];
                int m = 0;
                for (int k = 0; k <= 12; k++) { float t = PA_PI * (float)k / 12.0f; q[m].x = s.x + cosf(t) * r; q[m].y = s.y - sinf(t) * r; m++; }
                for (int k = 12; k >= 0; k--) { float t = PA_PI * (float)k / 12.0f; q[m].x = s.x + cosf(t) * r * 0.45f; q[m].y = s.y - sinf(t) * r * 0.45f; m++; }
                fpoly(c, q, m, pa_hex(0xE8343C));
                pa_fill_rect(c, s.x - r, s.y, r * 0.55f, r * 0.35f, pa_hex(0xE6E8F0));
                pa_fill_rect(c, s.x + r * 0.45f, s.y, r * 0.55f, r * 0.35f, pa_hex(0xE6E8F0));
            }
            break;
        }
        default: break;
        }
    }
#undef PART
}

/* The hoverboard: a rounded deck with a flame graphic, an edge, glowing
   thrusters and an underglow. The trail is drawn by the caller. */
static void draw_board(PA_Canvas *c, const Place *pl, float alpha) {
    PA_Vec2 top[20], bot[20];
    int n = 0;
    for (int k = 0; k < 20; k++) {
        float a = PA_TAU * (float)k / 20.0f;
        float lx = cosf(a) * 0.27f, lz = sinf(a);
        lz = (lz > 0 ? 1.0f : -1.0f) * powf(fabsf(lz), 0.45f) * 0.72f;
        V3 t = xf(pl, lx, -0.02f, lz + 0.05f), b = xf(pl, lx, -0.12f, lz + 0.05f);
        float d;
        if (!proj(t.x, t.y, t.z, &top[n], &d) || !proj(b.x, b.y, b.z, &bot[n], &d)) return;
        n++;
    }
    V3 gc = xf(pl, 0, -0.3f, 0.05f);
    PA_Vec2 gs; float gd;
    if (proj(gc.x, gc.y, gc.z, &gs, &gd)) glow(c, gs.x, gs.y, 1.0f * K.F / gd, pa_hex(0x4FE6FF), 0.55f * alpha);
    fpoly(c, bot, n, pa_alpha(pa_hex(0x7A1424), alpha));
    fpoly(c, top, n, pa_alpha(pa_hex(0xE8343C), alpha));
    PA_Vec2 in[20];
    PA_Vec2 cc = { 0, 0 };
    for (int k = 0; k < n; k++) { cc.x += top[k].x / (float)n; cc.y += top[k].y / (float)n; }
    for (int k = 0; k < n; k++) { in[k].x = cc.x + (top[k].x - cc.x) * 0.78f; in[k].y = cc.y + (top[k].y - cc.y) * 0.78f; }
    fpoly(c, in, n, pa_alpha(pa_hex(0xFF5A4A), alpha));
    V3 f[5] = { xf(pl, -0.12f, -0.01f, -0.45f), xf(pl, 0.12f, -0.01f, -0.45f), xf(pl, 0.16f, -0.01f, 0.15f),
                xf(pl, 0.0f, -0.01f, 0.55f), xf(pl, -0.16f, -0.01f, 0.15f) };
    PA_Vec2 fs[5]; float d;
    int ok = 1;
    for (int k = 0; k < 5; k++) ok &= proj(f[k].x, f[k].y, f[k].z, &fs[k], &d);
    if (ok) fpoly(c, fs, 5, pa_alpha(pa_hex(0xFFD23A), alpha));
    for (int sd = -1; sd <= 1; sd += 2) {
        V3 t = xf(pl, 0.13f * (float)sd, -0.07f, -0.66f);
        PA_Vec2 ts; float td;
        if (!proj(t.x, t.y, t.z, &ts, &td)) continue;
        float r = 0.08f * K.F / td;
        glow(c, ts.x, ts.y, r * 4.0f, pa_hex(0x4FE6FF), 0.8f * alpha);
        fcirc(c, ts.x, ts.y, r, pa_alpha(pa_hex(0xE6FFFF), alpha));
    }
}

/* Glowing ribbon through the recorded thruster positions. */
static void draw_trail(PA_Canvas *c) {
    if (G.ntrail < 2) return;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i + 1 < G.ntrail; i++) {
            PA_Vec2 a, b; float da, db;
            if (!proj(G.trail[i].x, G.trail[i].y, G.trail[i].z, &a, &da) ||
                !proj(G.trail[i + 1].x, G.trail[i + 1].y, G.trail[i + 1].z, &b, &db)) continue;
            float k0 = 1.0f - (float)i / (float)G.ntrail, k1 = 1.0f - (float)(i + 1) / (float)G.ntrail;
            float w0 = (pass ? 0.08f : 0.26f) * k0 * K.F / da, w1 = (pass ? 0.08f : 0.26f) * k1 * K.F / db;
            float dx = b.x - a.x, dy = b.y - a.y, l = sqrtf(dx * dx + dy * dy) + 1e-3f;
            float nx = -dy / l, ny = dx / l;
            PA_Vec2 q[4] = { { a.x + nx * w0, a.y + ny * w0 }, { b.x + nx * w1, b.y + ny * w1 },
                             { b.x - nx * w1, b.y - ny * w1 }, { a.x - nx * w0, a.y - ny * w0 } };
            PA_Color col = pass ? PA_RGBA(255, 255, 255, (int)(220.0f * k0))
                                : pa_alpha(pa_mix(pa_hex(0xFF4FD8), pa_hex(0x4FE6FF), k0), 0.75f * k0);
            fpoly(c, q, 4, col);
        }
    }
}

static void pose_blend(Pose *a, const Pose *b, float t) {
    float *pa_ = (float *)a;
    const float *pb = (const float *)b;
    for (size_t i = 0; i < sizeof(Pose) / sizeof(float); i++) pa_[i] = pa_lerpf(pa_[i], pb[i], t);
}

static void pose_run(Pose *p, float ph, float amp) {
    float s = sinf(ph), c = cosf(ph);
    memset(p, 0, sizeof(*p));
    p->hipL = 0.82f * amp * s;
    p->hipR = -0.82f * amp * s;
    p->kneeL = 0.30f + 1.65f * amp * maxf(0.0f, c);
    p->kneeR = 0.30f + 1.65f * amp * maxf(0.0f, -c);
    p->shL = -1.0f * amp * s;
    p->shR = 1.0f * amp * s;
    p->elL = 1.45f + 0.35f * s;
    p->elR = 1.45f - 0.35f * s;
    p->outL = p->outR = 0.34f;
    p->lean = 0.24f * amp;
    p->bob = 0.07f * fabsf(cosf(ph)) - 0.05f;
    p->head = -0.12f;
    p->twist = -0.24f * amp * s;
}

static void pose_idle(Pose *p, float t, int wave) {
    memset(p, 0, sizeof(*p));
    p->kneeL = 0.08f; p->kneeR = 0.12f; p->hipL = 0.04f; p->hipR = -0.02f;
    p->legOut = 0.06f;
    p->shL = 0.08f; p->elL = 0.25f; p->outL = 0.18f;
    if (wave) { p->shR = 2.7f; p->elR = 0.5f + 0.35f * sinf(t * 8.0f); p->outR = 0.55f; }
    else { p->shR = 0.08f; p->elR = 0.3f; p->outR = 0.18f; }
    p->bob = 0.012f * sinf(t * 2.2f) - 0.01f;
    p->lean = 0.02f;
    p->head = 0.03f * sinf(t * 1.3f);
}

static void pose_jump(Pose *p, float rise) {
    memset(p, 0, sizeof(*p));
    p->hipL = 1.25f; p->kneeL = 1.9f;
    p->hipR = -0.25f + rise * 0.3f; p->kneeR = 1.45f;
    p->shL = 2.4f; p->elL = 0.6f; p->outL = 0.75f;
    p->shR = 1.9f; p->elR = 0.9f; p->outR = 0.85f;
    p->lean = 0.12f;
    p->bob = 0.04f;
    p->head = -0.05f;
}

static void pose_roll(Pose *p) {
    memset(p, 0, sizeof(*p));
    /* A tight ball: knees to the chest, arms hugging the shins. */
    p->hipL = p->hipR = 2.35f;
    p->kneeL = p->kneeR = 2.75f;
    p->legOut = 0.06f;
    p->shL = p->shR = 1.75f;
    p->elL = p->elR = 1.55f;
    p->outL = p->outR = 0.12f;
    p->lean = 1.15f;
    p->head = 0.55f;
    p->bob = -0.2f;
}

static void pose_board(Pose *p, float t) {
    memset(p, 0, sizeof(*p));
    p->hipL = 0.45f; p->kneeL = 0.85f; p->hipR = -0.25f; p->kneeR = 0.75f; p->legOut = 0.2f;
    p->shL = 0.5f + 0.1f * sinf(t * 3.0f); p->outL = 1.05f; p->elL = 0.6f;
    p->shR = 0.2f; p->outR = 0.95f + 0.08f * sinf(t * 2.3f); p->elR = 0.5f;
    p->lean = 0.3f; p->twist = 0.35f; p->bob = -0.12f; p->head = -0.08f;
}

static void pose_splat(Pose *p) {
    memset(p, 0, sizeof(*p));
    p->hipL = p->hipR = 0.1f; p->kneeL = 0.3f; p->kneeR = 0.5f;
    p->legOut = 0.38f;
    p->shL = p->shR = 1.6f; p->outL = p->outR = 1.35f; p->elL = p->elR = 0.3f;
    p->lean = -0.05f;
}

static void draw_runner(PA_Canvas *c) {
    Place pl;
    memset(&pl, 0, sizeof(pl));
    pl.x = G.x; pl.y = G.y; pl.z = G.z; pl.s = 1.12f; pl.piv = 1.0f;
    Pose po;
    int sneak = G.sneak_t > 0.0f, jet = G.jet_t > 0.0f || G.glide > 0.0f;
    if (G.st == ST_MENU) {
        pl.yaw = 0.0f;
        pose_idle(&po, G.time, 1);
    } else if (G.st == ST_CRASH || G.st == ST_REVIVE || G.st == ST_RESULTS) {
        float t = G.st == ST_CRASH ? G.st_t : 2.0f;
        pl.z = G.crash_z;
        if (G.crash_kind == CR_TRAIN) {
            /* Flattened against the cab, then slumps sideways onto the ballast. */
            pose_splat(&po);
            float fall = pa_smooth(pa_clamp01((t - 0.4f) / 0.45f));
            Pose lie;
            pose_splat(&lie);
            lie.kneeL = 0.9f; lie.hipL = 0.6f; lie.kneeR = 0.3f; lie.legOut = 0.25f;
            lie.shL = 2.8f; lie.outL = 0.4f; lie.shR = 0.6f; lie.outR = 0.9f; lie.elR = 0.8f;
            lie.head = 0.3f;
            pose_blend(&po, &lie, fall);
            pl.roll = G.fall_dir * 1.38f * fall;
            pl.z = G.crash_z - 0.05f - fall * 0.5f;
            pl.y = G.y + 0.12f * fall;
        } else {
            Pose a; pose_run(&a, 1.0f, 0.4f);
            po = a;
            po.shL = po.shR = 2.6f; po.outL = po.outR = 0.6f;
            float fall = pa_smooth(pa_clamp01(t / 0.45f));
            pl.pitch = 1.5f * fall;
            pl.piv = 0.05f;
            pl.z += fall * 0.9f;
        }
    } else {
        float amp = pa_clamp01(G.speed / 9.0f);
        pose_run(&po, G.phase, amp);
        if (G.cam_blend < 1.0f) {
            Pose idle;
            pose_idle(&idle, G.time, 0);
            pose_blend(&po, &idle, 1.0f - pa_smooth(pa_clamp01(G.cam_blend * 2.0f)));
        }
        if (G.board_t > 0.0f && !jet) {
            Pose b;
            pose_board(&b, G.time);
            if (G.air) { b.kneeL += 0.6f; b.kneeR += 0.6f; b.hipL += 0.4f; b.hipR += 0.3f; b.bob -= 0.1f; }
            if (G.roll_t > 0.0f) { b.kneeL += 0.9f; b.kneeR += 0.9f; b.hipL += 0.7f; b.hipR += 0.7f; b.bob -= 0.3f; b.lean += 0.4f; }
            po = b;
        } else if (jet) {
            Pose j; pose_jump(&j, 0.5f);
            j.hipL = 0.5f; j.kneeL = 0.9f; j.hipR = 0.1f; j.kneeR = 0.8f;
            j.shL = j.shR = 0.5f; j.outL = j.outR = 0.5f; j.lean = 0.45f;
            j.hipL += 0.2f * sinf(G.time * 9.0f); j.hipR -= 0.2f * sinf(G.time * 9.0f);
            po = j;
        } else if (G.roll_t > 0.0f && G.board_t <= 0.0f) {
            pose_roll(&po);
            float k = 1.0f - G.roll_t / ROLL_TIME;
            pl.pitch = k * PA_TAU;
            pl.piv = 0.62f;
            pl.y = G.y - 0.45f * sinf(pa_clamp01(k * 1.2f) * PA_PI) + 0.0f;
            pl.y -= 0.42f;
        } else if (G.air && G.board_t <= 0.0f) {
            Pose j;
            pose_jump(&j, pa_clamp01(G.vy / 10.0f));
            float t = pa_clamp01((G.y - G.ground) / 0.8f + 0.25f);
            pose_blend(&po, &j, t);
        }
        if (G.land_t > 0.0f && G.roll_t <= 0.0f && G.board_t <= 0.0f) { po.kneeL += 0.6f; po.kneeR += 0.6f; po.hipL += 0.3f; po.hipR += 0.3f; po.bob -= 0.12f; }
        if (G.stagger > 0.0f) {
            float k = G.stagger / 0.55f;
            po.lean -= 0.5f * k; po.shL = 2.4f * k + po.shL * (1 - k); po.shR = 2.0f * k + po.shR * (1 - k);
            po.outL = po.outR = 0.8f * k + 0.16f;
        }
        /* Lane hop: lean into it and turn the shoulders toward the new lane. */
        pl.roll = pa_clampf(G.xv * 0.035f, -0.42f, 0.42f);
        pl.yaw = pa_clampf(G.xv * 0.030f, -0.4f, 0.4f);
        if (G.roll_t <= 0.0f && !G.air) pl.y += 0.22f * sinf(pa_clamp01(G.hop_t / 0.2f) * PA_PI) * pa_clamp01(fabsf(G.xv) / 6.0f);
    }
    place_init(&pl);
    if (G.st == ST_CRASH || G.st == ST_REVIVE || G.st == ST_RESULTS) {
        /* Dizzy stars circling the head. */
        V3 hd = xf(&pl, 0.0f, 2.35f, 0.0f);
        float t = G.time * 5.0f;
        for (int k = 0; k < 4; k++) {
            float a = t + (float)k * PA_TAU / 4.0f;
            PA_Vec2 sp; float d;
            if (proj(hd.x + cosf(a) * 0.42f, hd.y + 0.08f * sinf(a * 2.0f), hd.z + sinf(a) * 0.42f, &sp, &d))
                star_shape(c, sp.x, sp.y, 0.13f * K.F / d, 0.45f, a, pa_hex(0xFFE04A));
        }
    }
    float alpha = 1.0f;
    if (G.invuln > 0.0f && G.st == ST_RUN && fmodf(G.time, 0.2f) < 0.08f) alpha = 0.35f;
    if (G.st == ST_RUN && G.roll_t > 0.0f && G.board_t <= 0.0f) {
        /* Speed swooshes curling round the tumbling ball. */
        PA_Vec2 bc; float bd;
        if (proj(G.x, G.y + 0.62f, G.z, &bc, &bd)) {
            float R = 0.95f * K.F / bd, a0 = G.roll_t * 16.0f;
            for (int k = 0; k < 2; k++) {
                PA_Vec2 q[14];
                int m = 0;
                for (int j = 0; j < 14; j++) {
                    float a = a0 + (float)k * PA_PI + (float)j * 0.13f;
                    float rr2 = R * (1.0f + 0.004f * (float)j);
                    q[m].x = bc.x + cosf(a) * rr2; q[m].y = bc.y + sinf(a) * rr2 * 0.8f; m++;
                }
                pa_stroke_poly(c, q, m, 0, maxf(2.0f, R * 0.09f), PA_RGBA(255, 255, 255, 170));
            }
        }
    }
    if (G.st == ST_RUN) draw_trail(c);
    if (G.board_t > 0.0f && G.st == ST_RUN) {
        Place bp = pl;
        bp.y += 0.30f + 0.05f * sinf(G.time * 6.0f);
        bp.pitch = 0.0f; bp.piv = 0.0f;
        place_init(&bp);
        draw_board(c, &bp, alpha);
        pl.y = bp.y;
        place_init(&pl);
    }
    humanoid(c, &pl, &po, &RUNNER, alpha, jet, G.magnet_t > 0.0f && G.st == ST_RUN, sneak);
}

static void draw_chaser(PA_Canvas *c, float x, float y, float z, float yaw, int running, int dog) {
    Place pl;
    memset(&pl, 0, sizeof(pl));
    pl.x = x; pl.y = y; pl.z = z; pl.s = 1.12f; pl.yaw = yaw; pl.piv = 1.0f;
    place_init(&pl);
    Pose po;
    if (running) {
        pose_run(&po, G.chase_phase, 0.9f);
        po.shR = 2.7f + 0.2f * sinf(G.time * 14.0f); po.elR = 0.6f; po.outR = 0.35f;
    } else {
        pose_idle(&po, G.time + 1.3f, 0);
        po.shR = 1.1f; po.elR = 1.6f; po.outR = 0.4f;
    }
    if (dog) {
        /* The dog: a capsule body, head, ears, four legs and a tail. */
        Place dp = pl;
        dp.x = G.x - 1.35f;
        dp.z = z + 0.4f;
        if (G.st == ST_CRASH || G.st == ST_REVIVE || G.st == ST_RESULTS) {
            dp.x = x - G.fall_dir * 0.25f;
            dp.z = z - 1.1f;
        }
        dp.s = 1.0f;
        place_init(&dp);
        float ph = G.chase_phase * 1.4f;
        float r = running ? 1.0f : 0.15f;
        PA_Color fur = pa_hex(0xF2EEE6), spot = pa_hex(0x8A5A3A);
        V3 legs[4][2];
        for (int k = 0; k < 4; k++) {
            float fx = (k & 1) ? 0.1f : -0.1f, fz = (k & 2) ? 0.22f : -0.22f;
            float sw = sinf(ph + (k & 2 ? 0.0f : PA_PI) + (k & 1 ? 0.6f : 0.0f)) * 0.5f * r;
            legs[k][0] = xf(&dp, fx, 0.42f, fz);
            legs[k][1] = xf(&dp, fx, 0.04f, fz + sinf(sw) * 0.25f);
        }
        for (int k = 0; k < 4; k++) if (k & 1) limb(c, legs[k][0], legs[k][1], 0.05f, 0.045f, fur, 1.0f);
        limb(c, xf(&dp, 0, 0.5f, -0.30f), xf(&dp, 0, 0.78f + 0.08f * sinf(G.time * 12.0f), -0.48f), 0.04f, 0.03f, spot, 1.0f);
        limb(c, xf(&dp, 0, 0.47f, -0.25f), xf(&dp, 0, 0.5f, 0.26f), 0.17f, 0.18f, fur, 1.0f);
        limb(c, xf(&dp, 0.02f, 0.55f, -0.1f), xf(&dp, 0.02f, 0.56f, 0.08f), 0.10f, 0.10f, spot, 1.0f);
        limb(c, xf(&dp, 0, 0.66f, 0.34f), xf(&dp, 0, 0.66f, 0.52f), 0.13f, 0.09f, fur, 1.0f);
        limb(c, xf(&dp, -0.08f, 0.8f, 0.34f), xf(&dp, -0.12f, 0.66f, 0.30f), 0.05f, 0.04f, spot, 1.0f);
        limb(c, xf(&dp, 0.08f, 0.8f, 0.34f), xf(&dp, 0.12f, 0.66f, 0.30f), 0.05f, 0.04f, spot, 1.0f);
        for (int k = 0; k < 4; k++) if (!(k & 1)) limb(c, legs[k][0], legs[k][1], 0.05f, 0.045f, fur, 1.0f);
    }
    humanoid(c, &pl, &po, &INSPECTOR, 1.0f, 0, 0, 0);
}

/* ---- world pieces ---- */
typedef struct { uint32_t body, stripe, roof, glass, face, accent; int kind; } TStyle;
static const TStyle TSTY[] = {
    { 0xEEF1F6, 0xE8344A, 0xB3BAC6, 0x5AA8E6, 0xE8344A, 0x2E3B66, 0 },   /* white metro, red band   */
    { 0xD8344C, 0xF6F2EA, 0xA7283C, 0x7CC0F0, 0xC22A42, 0xF6F2EA, 0 },   /* red commuter           */
    { 0x2BA4B2, 0xF08A24, 0x75CDD6, 0x000000, 0x2593A0, 0x1E2A30, 1 },   /* teal container wagon   */
    { 0xF39A2E, 0x6B6E7A, 0x8D909C, 0x6FB6EA, 0xF39A2E, 0x3A3D48, 0 },   /* orange commuter        */
};

static void stripes_z(PA_Canvas *c, float z, float x0, float x1, float y0, float y1, PA_Color a, PA_Color b, int n) {
    /* Diagonal hazard stripes across a face that looks down -z. */
    qz(c, z, x0, x1, y0, y1, a);
    float w = (x1 - x0) / (float)n;
    float h = y1 - y0;
    for (int k = 0; k < n; k += 2) {
        float s0 = x0 + w * (float)k, s1 = s0 + w;
        V3 q[4] = { v3(s0, y0, z), v3(minf(s1, x1), y0, z), v3(minf(s1 + h * 0.6f, x1), y1, z), v3(minf(s0 + h * 0.6f, x1), y1, z) };
        if (s0 + h * 0.6f >= x1) { q[3] = v3(x1, y0 + (x1 - s0) / 0.6f, z); q[2] = v3(x1, y0 + (x1 - s0) / 0.6f, z); }
        wpoly(c, q, 4, b);
    }
}

static void draw_car(PA_Canvas *c, const Obs *o, int ci) {
    const TStyle *st = &TSTY[o->style & 3];
    float x = lane_x(o->lane);
    float za = o->z + (float)ci * (CAR_LEN + CAR_GAP), zb = za + CAR_LEN;
    float x0 = x - TRAIN_W * 0.5f, x1 = x + TRAIN_W * 0.5f;
    float yb = 0.62f, ys = TRAIN_H - 0.40f, ch = 0.30f;
    PA_Color body = pa_hex(st->body), roof = pa_hex(st->roof), dark = pa_hex(0x30323C);
    int first = ci == 0, last = ci == o->cars - 1;
    float dmid = depth_of(x, 1.5f, za);
    int detail = dmid < 70.0f;

    /* Undercarriage and bogies. */
    wbox(c, x0 + 0.32f, x1 - 0.32f, 0.0f, yb, za + 0.5f, zb - 0.5f, dark, dark);
    if (detail) {
        for (int k = 0; k < 2; k++) {
            float bz = k ? zb - 2.6f : za + 0.9f;
            wbox(c, x0 + 0.22f, x1 - 0.22f, 0.05f, yb - 0.05f, bz, bz + 1.7f, pa_hex(0x24252C), pa_hex(0x24252C));
        }
    }

    /* Side faces, with the window band, livery stripe and doors on the face we see. */
    int see_left = K.x < x0, see_right = K.x > x1;
    for (int side = 0; side < 2; side++) {
        if ((side == 0 && !see_left) || (side == 1 && !see_right)) continue;
        float fx = side ? x1 : x0;
        PA_Color sc = body;
        qx(c, fx, za, zb, yb, ys, sc);
        if (!detail) continue;
        float off = side ? 0.012f : -0.012f;
        if (st->kind == 1) {
            for (float rz = za + 0.5f; rz < zb - 0.3f; rz += 0.75f) qx(c, fx + off, rz, rz + 0.18f, yb + 0.15f, ys - 0.1f, pa_shade(sc, -0.12f));
            qx(c, fx + off * 2.0f, za + 0.1f, zb - 0.1f, yb + 0.05f, yb + 0.42f, pa_hex(0x22262C));
            for (float rz = za + 0.1f; rz < zb - 0.3f; rz += 0.6f) qx(c, fx + off * 3.0f, rz, rz + 0.3f, yb + 0.05f, yb + 0.42f, pa_hex(st->stripe));
        } else {
            qx(c, fx + off, za + 0.05f, zb - 0.05f, 0.95f, 1.22f, pa_shade(pa_hex(st->stripe), side ? -0.04f : -0.16f));
            PA_Color gl = pa_hex(st->glass);
            for (int w = 0; w < 4; w++) {
                float wz = za + 0.7f + (float)w * 2.15f;
                if (w == 1 || w == 3) {
                    qx(c, fx + off, wz, wz + 1.25f, yb + 0.05f, 2.6f, pa_shade(sc, -0.14f));
                    qx(c, fx + off * 2.0f, wz + 0.15f, wz + 1.1f, 1.55f, 2.45f, gl);
                    qx(c, fx + off * 2.0f, wz + 0.61f, wz + 0.64f, yb + 0.1f, 2.5f, pa_shade(sc, -0.3f));
                } else {
                    qx(c, fx + off, wz, wz + 1.6f, 1.55f, 2.5f, pa_shade(sc, -0.25f));
                    qx(c, fx + off * 2.0f, wz + 0.08f, wz + 1.52f, 1.62f, 2.43f, gl);
                    qx(c, fx + off * 3.0f, wz + 0.2f, wz + 0.55f, 2.0f, 2.43f, pa_alpha(PA_RGB(255, 255, 255), 0.25f));
                }
            }
        }
    }
    /* Chamfered roof and the flat top the runner lands on. */
    {
        PA_Color rc_ = roof;
        if (K.y > ys) {
            V3 l[4] = { v3(x0, ys, za), v3(x0, ys, zb), v3(x0 + ch, TRAIN_H, zb), v3(x0 + ch, TRAIN_H, za) };
            V3 r[4] = { v3(x1, ys, za), v3(x1 - ch, TRAIN_H, za), v3(x1 - ch, TRAIN_H, zb), v3(x1, ys, zb) };
            if (K.x < x0 + ch * 3.0f || K.y > TRAIN_H) wpoly(c, l, 4, pa_shade(rc_, -0.10f));
            if (K.x > x1 - ch * 3.0f || K.y > TRAIN_H) wpoly(c, r, 4, pa_shade(rc_, -0.22f));
        }
        if (K.y > TRAIN_H) {
            qy(c, TRAIN_H, x0 + ch, x1 - ch, za, zb, rc_);
            if (detail) {
                for (int k = 0; k < 3; k++) {
                    float hz = za + 1.6f + (float)k * 2.9f;
                    qy(c, TRAIN_H + 0.01f, x - 0.55f, x + 0.55f, hz, hz + 0.32f, pa_shade(rc_, -0.24f));
                }
                wbox(c, x - 0.45f, x + 0.45f, TRAIN_H, TRAIN_H + 0.22f, za + 3.6f, za + 5.2f, pa_shade(rc_, 0.06f), pa_shade(rc_, 0.16f));
                qy(c, TRAIN_H + 0.01f, x0 + ch + 0.05f, x0 + ch + 0.17f, za + 0.2f, zb - 0.2f, pa_shade(rc_, 0.14f));
            }
        }
    }
    /* End faces. */
    for (int end = 0; end < 2; end++) {
        float fz = end ? zb : za;
        int vis = end ? K.z > zb : K.z < za;
        if (!vis) continue;
        int cab = end ? last : first;
        PA_Color fc = cab ? pa_hex(st->face) : pa_shade(body, -0.25f);
        if (end) fc = pa_shade(fc, -0.08f);
        V3 hex[6] = { v3(x0, yb, fz), v3(x1, yb, fz), v3(x1, ys, fz), v3(x1 - ch, TRAIN_H, fz), v3(x0 + ch, TRAIN_H, fz), v3(x0, ys, fz) };
        wpoly(c, hex, 6, fc);
        if (!cab || !detail) continue;
        float dz = end ? 0.012f : -0.012f;
        if (st->kind == 1) {
            for (float rx = x0 + 0.25f; rx < x1 - 0.2f; rx += 0.42f) qz(c, fz + dz, rx, rx + 0.12f, yb + 0.1f, ys - 0.05f, pa_shade(fc, -0.15f));
            qz(c, fz + dz * 2, x - 0.05f, x + 0.05f, yb + 0.1f, ys - 0.05f, pa_hex(0x1E2A30));
            stripes_z(c, fz + dz * 3, x0 + 0.05f, x1 - 0.05f, yb + 0.05f, yb + 0.45f, pa_hex(0x22262C), pa_hex(st->stripe), 10);
        } else {
            qz(c, fz + dz, x0 + 0.05f, x1 - 0.05f, 0.80f, 1.02f, pa_hex(st->stripe));
            /* Windscreen with a glare streak, destination board above it. */
            qz(c, fz + dz, x - 0.98f, x + 0.98f, 1.62f, 2.72f, pa_hex(0x1C2A48));
            qz(c, fz + dz * 2, x - 0.9f, x + 0.9f, 1.70f, 2.64f, pa_hex(0x3D6FB4));
            V3 gq[4] = { v3(x - 0.70f, 1.72f, fz + dz * 3), v3(x - 0.42f, 1.72f, fz + dz * 3),
                         v3(x - 0.05f, 2.62f, fz + dz * 3), v3(x - 0.33f, 2.62f, fz + dz * 3) };
            wpoly(c, gq, 4, PA_RGBA(255, 255, 255, 80));
            qz(c, fz + dz * 3, x - 0.02f, x + 0.02f, 1.7f, 2.64f, pa_hex(0x1C2A48));
            qz(c, fz + dz, x - 0.6f, x + 0.6f, 2.82f, 3.02f, pa_hex(0x1A1C24));
            qz(c, fz + dz * 2, x - 0.5f, x + 0.1f, 2.87f, 2.97f, pa_hex(0xFFA23A));
            int lit = o->moving && o->active && !end;
            PA_Color lamp = end ? pa_hex(0xFF3A3A) : (lit ? pa_hex(0xFFF6C8) : pa_hex(0xD9D6C0));
            for (int sd = -1; sd <= 1; sd += 2) {
                float lx = x + (float)sd * 0.72f;
                qz(c, fz + dz * 2, lx - 0.17f, lx + 0.17f, 1.12f, 1.36f, pa_hex(0x22242C));
                qz(c, fz + dz * 3, lx - 0.12f, lx + 0.12f, 1.16f, 1.32f, lamp);
                if (lit) {
                    PA_Vec2 s; float d;
                    if (proj(lx, 1.24f, fz - 0.05f, &s, &d)) glow(c, s.x, s.y, 0.9f * K.F / d, pa_hex(0xFFF2B0), 0.75f);
                }
            }
            qz(c, fz + dz, x - 0.85f, x + 0.85f, yb, yb + 0.16f, pa_hex(0x2A2C34));
        }
    }
}

static void draw_ramp(PA_Canvas *c, const Obs *o) {
    float x = lane_x(o->lane), zr = o->z - RAMP_LEN, zt = o->z;
    float x0 = x - TRAIN_W * 0.5f + 0.06f, x1 = x + TRAIN_W * 0.5f - 0.06f;
    PA_Color side = pa_hex(0x8C6A3E), deck = pa_hex(0xF2C230);
    for (int s = 0; s < 2; s++) {
        float fx = s ? x1 : x0;
        if ((s == 0 && K.x < x0) || (s == 1 && K.x > x1)) {
            V3 t[3] = { v3(fx, 0, zr), v3(fx, 0, zt), v3(fx, TRAIN_H, zt) };
            wpoly(c, t, 3, pa_shade(side, s ? -0.05f : -0.2f));
            for (int k = 1; k < 4; k++) {
                float zz = zr + RAMP_LEN * (float)k / 4.0f;
                wline(c, v3(fx, 0.05f, zz), v3(fx, TRAIN_H * (float)k / 4.0f - 0.05f, zz), 0.07f, pa_shade(side, -0.35f), 1.0f);
            }
            wline(c, v3(fx, 0.1f, zr + 0.4f), v3(fx, TRAIN_H - 0.2f, zt), 0.07f, pa_shade(side, -0.35f), 1.0f);
        }
    }
    /* The deck: yellow with dark slats running across it. */
    float n = 9.0f;
    for (int k = 0; k < (int)n; k++) {
        float t0 = (float)k / n, t1 = (float)(k + 1) / n;
        PA_Color col = (k & 1) ? pa_shade(deck, -0.08f) : deck;
        wquad(c, v3(x0, TRAIN_H * t0, zr + RAMP_LEN * t0), v3(x1, TRAIN_H * t0, zr + RAMP_LEN * t0),
              v3(x1, TRAIN_H * t1, zr + RAMP_LEN * t1), v3(x0, TRAIN_H * t1, zr + RAMP_LEN * t1), col);
        float tm = t0 + (t1 - t0) * 0.82f;
        wquad(c, v3(x0, TRAIN_H * tm, zr + RAMP_LEN * tm), v3(x1, TRAIN_H * tm, zr + RAMP_LEN * tm),
              v3(x1, TRAIN_H * t1, zr + RAMP_LEN * t1), v3(x0, TRAIN_H * t1, zr + RAMP_LEN * t1), pa_hex(0x2A2620));
    }
    wquad(c, v3(x0, 0.0f, zr), v3(x1, 0.0f, zr), v3(x1, 0.08f, zr + 0.05f), v3(x0, 0.08f, zr + 0.05f), pa_hex(0x3A3630));
}

static void draw_barrier(PA_Canvas *c, const Obs *o) {
    float x = lane_x(o->lane), z = o->z;
    int high = o->kind == OB_HIGH;
    float y0 = high ? HIGH_Y0 + 0.05f : 0.42f, y1 = high ? HIGH_Y1 : LOW_H;
    float ptop = high ? HIGH_Y1 + 0.1f : LOW_H + 0.05f;
    PA_Color post = pa_hex(0xE9E9EF);
    for (int s = -1; s <= 1; s += 2) {
        float px = x + (float)s * 1.12f;
        wbox(c, px - 0.08f, px + 0.08f, 0.0f, ptop, z - 0.08f, z + 0.08f, post, pa_shade(post, 0.1f));
        wbox(c, px - 0.2f, px + 0.2f, 0.0f, 0.1f, z - 0.3f, z + 0.3f, pa_hex(0x5A5C66), pa_hex(0x6A6C76));
    }
    /* Board with diagonal stripes on the face we see. */
    PA_Color a = high ? pa_hex(0xFFD23A) : pa_hex(0xF6F6F6), b = high ? pa_hex(0x22242C) : pa_hex(0xE8343C);
    wbox(c, x - 1.22f, x + 1.22f, y0, y1, z - 0.07f, z + 0.07f, a, pa_shade(a, 0.05f));
    if (K.z < z - 0.07f) {
        stripes_z(c, z - 0.08f, x - 1.18f, x + 1.18f, y0 + 0.05f, y1 - 0.05f, a, b, 12);
    }
    /* Blinking lamps on the posts. */
    for (int s = -1; s <= 1; s += 2) {
        PA_Vec2 sp; float d;
        if (!proj(x + (float)s * 1.12f, ptop + 0.12f, z, &sp, &d)) continue;
        float r = 0.13f * K.F / d;
        int on = ((int)(G.time * 3.0f) + (s > 0)) & 1;
        fcirc(c, sp.x, sp.y, r, fogc(pa_hex(0x3A3A40), d));
        fcirc(c, sp.x, sp.y - r * 0.1f, r * 0.75f, fogc(on ? pa_hex(0xFFB23A) : pa_hex(0xB06A2A), d));
        if (on && d < 60.0f) glow(c, sp.x, sp.y, r * 3.0f, pa_hex(0xFFB23A), 0.5f);
    }
}

static void draw_signal(PA_Canvas *c, const Obs *o) {
    float x = lane_x(o->lane), z = o->z;
    wbox(c, x - 0.36f, x + 0.36f, 0.0f, 0.45f, z - 0.3f, z + 0.3f, pa_hex(0x9C9AA2), pa_hex(0xB4B2BA));
    wbox(c, x - 0.08f, x + 0.08f, 0.45f, 3.3f, z - 0.08f, z + 0.08f, pa_hex(0x3E4048), pa_hex(0x4E5058));
    wbox(c, x - 0.28f, x + 0.28f, 2.3f, 3.5f, z - 0.15f, z + 0.15f, pa_hex(0x1E2026), pa_hex(0x2A2C34));
    if (K.z < z - 0.15f) {
        PA_Vec2 s; float d;
        for (int k = 0; k < 2; k++) {
            float ly = k ? 2.62f : 3.15f;
            if (!proj(x, ly, z - 0.16f, &s, &d)) continue;
            float r = 0.16f * K.F / d;
            PA_Color col = k ? pa_hex(0x2A5A34) : pa_hex(0xFF3B3B);
            fcirc(c, s.x, s.y, r, fogc(col, d));
            if (!k) glow(c, s.x, s.y, r * 3.2f, pa_hex(0xFF4A4A), 0.55f);
        }
    }
}

static void draw_coin(PA_Canvas *c, const Coin *co) {
    PA_Vec2 s; float d;
    if (!proj(co->x, co->y, co->z, &s, &d)) return;
    float r = 0.40f * K.F / d;
    if (r < 0.8f) return;
    float spin = cosf(G.time * 4.2f + co->z * 0.45f);
    float sx = maxf(0.18f, fabsf(spin));
    PA_Color rim = fogc(pa_hex(0xC9820E), d), face = fogc(pa_hex(0xFFC52A), d), hi = fogc(pa_hex(0xFFE27A), d);
    float edge = r * 0.16f * (1.0f - sx) * (spin > 0 ? 1.0f : -1.0f);
    fell(c, s.x + edge, s.y, r * sx + r * 0.08f, r, rim);
    fell(c, s.x - edge * 0.3f, s.y, r * sx * 0.92f, r * 0.92f, face);
    if (r > 4.0f) {
        fell(c, s.x - edge * 0.3f, s.y, r * sx * 0.68f, r * 0.68f, hi);
        /* Embossed star. */
        PA_Vec2 q[10];
        for (int k = 0; k < 10; k++) {
            float a = -PA_PI * 0.5f + (float)k * PA_PI / 5.0f;
            float rr2 = (k & 1) ? r * 0.24f : r * 0.52f;
            q[k].x = s.x - edge * 0.3f + cosf(a) * rr2 * sx;
            q[k].y = s.y + sinf(a) * rr2;
        }
        fpoly(c, q, 10, fogc(pa_hex(0xF2A21A), d));
        /* Specular: a hard white glint top-left and a soft sheen down the rim. */
        fell(c, s.x - r * 0.32f * sx, s.y - r * 0.46f, r * 0.24f * sx, r * 0.15f, PA_RGBA(255, 255, 255, 235));
        fell(c, s.x - r * 0.55f * sx, s.y - r * 0.05f, r * 0.08f * sx, r * 0.3f, PA_RGBA(255, 255, 255, 120));
        float tw = sinf(G.time * 7.0f + co->z * 1.3f);
        if (tw > 0.75f) star_shape(c, s.x - r * 0.35f * sx, s.y - r * 0.5f, r * 0.5f * (tw - 0.75f) * 4.0f, 0.2f, 0.0f,
                                   PA_RGBA(255, 255, 255, 230));
    }
}

/* ---- icons, shared by pickups and HUD ---- */
static void star_shape(PA_Canvas *c, float x, float y, float r, float inner, float rot, PA_Color col) {
    PA_Vec2 q[10];
    for (int k = 0; k < 10; k++) {
        float a = -PA_PI * 0.5f + rot + (float)k * PA_PI / 5.0f;
        float rr2 = (k & 1) ? r * inner : r;
        q[k].x = x + cosf(a) * rr2;
        q[k].y = y + sinf(a) * rr2;
    }
    fpoly(c, q, 10, col);
}

static void coin_icon(PA_Canvas *c, float x, float y, float r) {
    fcirc(c, x, y + r * 0.08f, r, pa_hex(0xB8740C));
    fcirc(c, x, y, r, pa_hex(0xE89A14));
    fcirc(c, x, y, r * 0.84f, pa_hex(0xFFC52A));
    star_shape(c, x, y + r * 0.04f, r * 0.55f, 0.45f, 0.0f, pa_hex(0xF09A10));
    fell(c, x - r * 0.35f, y - r * 0.42f, r * 0.2f, r * 0.13f, PA_RGBA(255, 255, 255, 170));
}

static void key_icon(PA_Canvas *c, float x, float y, float s) {
    PA_Color k = pa_hex(0x3FB0FF), kd = pa_hex(0x1C6FB8);
    fcirc(c, x - s * 0.42f, y, s * 0.42f, kd);
    fcirc(c, x - s * 0.42f, y - s * 0.03f, s * 0.36f, k);
    fcirc(c, x - s * 0.42f, y - s * 0.03f, s * 0.16f, kd);
    pa_round_rect(c, x - s * 0.1f, y - s * 0.12f, s * 0.9f, s * 0.22f, s * 0.08f, k);
    pa_fill_rect(c, x + s * 0.48f, y + s * 0.06f, s * 0.14f, s * 0.26f, k);
    pa_fill_rect(c, x + s * 0.68f, y + s * 0.06f, s * 0.12f, s * 0.2f, k);
}

static void pick_icon(PA_Canvas *c, int kind, float x, float y, float s) {
    switch (kind) {
    case PK_MAGNET: {
        PA_Vec2 q[40];
        int m = 0;
        for (int k = 0; k <= 14; k++) { float t = PA_PI * (float)k / 14.0f; q[m].x = x + cosf(t) * s * 0.62f; q[m].y = y - s * 0.05f - sinf(t) * s * 0.62f; m++; }
        q[m].x = x - s * 0.62f; q[m].y = y + s * 0.42f; m++;
        q[m].x = x - s * 0.26f; q[m].y = y + s * 0.42f; m++;
        for (int k = 14; k >= 0; k--) { float t = PA_PI * (float)k / 14.0f; q[m].x = x + cosf(t) * s * 0.26f; q[m].y = y - s * 0.05f - sinf(t) * s * 0.26f; m++; }
        q[m].x = x + s * 0.26f; q[m].y = y + s * 0.42f; m++;
        q[m].x = x + s * 0.62f; q[m].y = y + s * 0.42f; m++;
        /* Two halves: closing the outline the long way round keeps it one polygon. */
        fpoly(c, q, m, pa_hex(0xE8343C));
        pa_fill_rect(c, x - s * 0.62f, y + s * 0.20f, s * 0.36f, s * 0.24f, pa_hex(0xE8EAF2));
        pa_fill_rect(c, x + s * 0.26f, y + s * 0.20f, s * 0.36f, s * 0.24f, pa_hex(0xE8EAF2));
        break;
    }
    case PK_JET:
        pa_round_rect(c, x - s * 0.55f, y - s * 0.5f, s * 0.48f, s * 0.9f, s * 0.22f, pa_hex(0xE8343C));
        pa_round_rect(c, x + s * 0.07f, y - s * 0.5f, s * 0.48f, s * 0.9f, s * 0.22f, pa_hex(0xE8343C));
        pa_round_rect(c, x - s * 0.5f, y - s * 0.42f, s * 0.14f, s * 0.6f, s * 0.07f, pa_hex(0xFF8A8A));
        pa_round_rect(c, x + s * 0.12f, y - s * 0.42f, s * 0.14f, s * 0.6f, s * 0.07f, pa_hex(0xFF8A8A));
        for (int k = 0; k < 2; k++) {
            float fx = x + (k ? s * 0.31f : -s * 0.31f);
            PA_Vec2 f[3] = { { fx - s * 0.18f, y + s * 0.4f }, { fx + s * 0.18f, y + s * 0.4f }, { fx, y + s * 0.85f } };
            fpoly(c, f, 3, pa_hex(0xFFB43A));
        }
        break;
    case PK_SNEAK: {
        PA_Vec2 q[7] = { { x - s * 0.6f, y - s * 0.35f }, { x - s * 0.15f, y - s * 0.35f }, { x + s * 0.05f, y - s * 0.05f },
                         { x + s * 0.55f, y + s * 0.05f }, { x + s * 0.68f, y + s * 0.3f }, { x + s * 0.6f, y + s * 0.42f },
                         { x - s * 0.62f, y + s * 0.42f } };
        fpoly(c, q, 7, pa_hex(0x4CD24A));
        pa_round_rect(c, x - s * 0.64f, y + s * 0.30f, s * 1.34f, s * 0.18f, s * 0.08f, pa_hex(0xF6F6F6));
        pa_line(c, x - s * 0.2f, y - s * 0.2f, x + s * 0.05f, y + s * 0.05f, maxf(1.0f, s * 0.07f), pa_hex(0xF6F6F6));
        break;
    }
    case PK_MULT:
        star_shape(c, x, y, s * 0.72f, 0.48f, 0.0f, pa_hex(0xC98A12));
        star_shape(c, x, y - s * 0.04f, s * 0.66f, 0.48f, 0.0f, pa_hex(0xFFD23A));
        pa_text_bold(c, "2X", x, y - s * 0.24f, s * 0.42f, PA_RGB(255, 255, 255), pa_hex(0x8A3A12), PA_ALIGN_CENTER, 0.0f, 0.9f);
        break;
    case PK_BOARD: {
        glow(c, x, y + s * 0.4f, s * 0.9f, pa_hex(0x4FE6FF), 0.6f);
        pa_round_rect(c, x - s * 0.75f, y - s * 0.2f, s * 1.5f, s * 0.42f, s * 0.21f, pa_hex(0x7A1424));
        pa_round_rect(c, x - s * 0.75f, y - s * 0.28f, s * 1.5f, s * 0.38f, s * 0.19f, pa_hex(0xE8343C));
        PA_Vec2 f[3] = { { x - s * 0.45f, y - s * 0.1f }, { x + s * 0.45f, y - s * 0.18f }, { x - s * 0.1f, y - s * 0.02f } };
        fpoly(c, f, 3, pa_hex(0xFFD23A));
        break;
    }
    default:
        key_icon(c, x, y, s);
        break;
    }
}

static void draw_pick(PA_Canvas *c, const Pick *p) {
    float bob = sinf(G.time * 3.0f + p->z) * 0.15f;
    PA_Vec2 s; float d;
    if (!proj(p->x, p->y + bob, p->z, &s, &d)) return;
    float r = 0.62f * K.F / d;
    if (r < 1.5f) return;
    PA_Vec2 g; float gd;
    if (proj(p->x, G.ground > 0.0f && p->y > TRAIN_H ? TRAIN_H : 0.0f, p->z, &g, &gd))
        pa_shadow(c, g.x, g.y, r * 0.7f, r * 0.25f, 0.25f);
    glow(c, s.x, s.y, r * 1.7f, pa_hex(0xFFF6C0), 0.55f + 0.2f * sinf(G.time * 6.0f));
    fcirc(c, s.x, s.y, r, PA_RGBA(255, 255, 255, 70));
    pa_stroke_circle(c, s.x, s.y, r, maxf(1.0f, r * 0.08f), PA_RGBA(255, 255, 255, 200));
    pick_icon(c, p->kind, s.x, s.y, r * 0.95f);
}

/* ---- worlds ----
   Two themes alternate down the line: Old Town (pastel gabled terraces,
   steel gantries, a station bridge and a clock tower) and Canyon Town
   (false-front timber stores, adobe, saloons, red mesas, cacti and a SALOON
   gate). Ground, sky and props blend across a short stretch at each border. */
typedef struct {
    uint32_t sky_top, sky_mid, haze, pave, pave2, bed, gravel, tie, tie_top, wall, wall_top, steel, steel2;
} WorldPal;

static const WorldPal WP[2] = {
    { 0x2C82EE, 0x62B4F6, 0xCFE6F6, 0xC9BBAA, 0xB8A996, 0x8C7464, 0xA28A78, 0x6E4C3C, 0x86604C, 0xBDB4AE, 0xD8D0CA, 0x5D6676, 0x7A8494 },
    { 0x7F6AD0, 0xF2A08E, 0xF9D7B2, 0xE8B486, 0xD39A6C, 0x9A5E44, 0xB87A58, 0x5E3828, 0x7A4C36, 0xC8845C, 0xE4A87C, 0x7A4E34, 0x9A6A48 },
};

#define WMIX(f, a) pa_mix(pa_hex(WP[0].f), pa_hex(WP[1].f), (a))

static float canyon_amt(float z) {
    float s = 0.0f;
    for (int k = -2; k <= 2; k++) s += (float)world_at(z + (float)k * 10.0f);
    return s / 5.0f;
}

#define MARK_EVERY 40.0f
static int mark_kind(float z) {           /* landmark at a marker: 0 none, 1 overhead, 2 side */
    int k = (int)floorf(z / MARK_EVERY + 0.5f);
    if (k < 2) return 0;
    return (k & 1) ? 2 : 1;
}

static void draw_gantry(PA_Canvas *c, float z) {
    float a = canyon_amt(z);
    PA_Color st = WMIX(steel, a), st2 = WMIX(steel2, a);
    for (int s = -1; s <= 1; s += 2) {
        float px = (float)s * (TRACK_HALF + 0.05f);
        wbox(c, px - 0.17f, px + 0.17f, 0.0f, GANTRY_H, z - 0.17f, z + 0.17f, st, st2);
    }
    wbox(c, -TRACK_HALF - 0.3f, TRACK_HALF + 0.3f, GANTRY_H - 0.42f, GANTRY_H, z - 0.14f, z + 0.14f, st, st2);
    for (int l = 0; l < 3; l++) {
        float lx = lane_x(l);
        wbox(c, lx - 0.06f, lx + 0.06f, GANTRY_H - 1.0f, GANTRY_H - 0.42f, z - 0.06f, z + 0.06f, pa_hex(0x3A3E48), pa_hex(0x3A3E48));
    }
}

/* Station bridge (Old Town) or SALOON gate (Canyon) spanning the line. */
static void draw_overhead_mark(PA_Canvas *c, float z) {
    int w = world_at(z);
    if (w == 0) {
        PA_Color br = pa_hex(0xA4553E), stone = pa_hex(0xE6D6BE);
        float y0 = 11.6f, y1 = 15.2f, x0 = -FACADE_X - 2.0f, x1 = FACADE_X + 2.0f;
        wbox(c, x0, x1, y0, y1, z, z + 3.2f, br, pa_shade(br, 0.1f));
        if (K.z < z) {
            qz(c, z - 0.01f, x0, x1, y0, y0 + 0.45f, stone);
            qz(c, z - 0.01f, x0, x1, y1 - 0.35f, y1, stone);
            for (float wx = x0 + 1.0f; wx < x1 - 1.4f; wx += 2.1f) {
                qz(c, z - 0.02f, wx, wx + 1.2f, y0 + 0.9f, y1 - 0.8f, pa_hex(0xF4EEE2));
                qz(c, z - 0.03f, wx + 0.12f, wx + 1.08f, y0 + 1.0f, y1 - 0.9f, pa_hex(0x7FB8E6));
            }
            qz(c, z - 0.04f, -2.6f, 2.6f, y1 - 0.2f, y1 + 1.3f, pa_hex(0x1F3A6E));
            PA_Vec2 s; float d;
            if (proj(0.0f, y1 + 0.55f, z - 0.05f, &s, &d)) {
                float sz = 0.75f * K.F / d;
                if (sz > 5.0f) {
                    PA_TextStyle ts = pa_text_style(PA_FACE_DISPLAY, fogc(pa_hex(0xFFF4D0), d));
                    ts.align = PA_ALIGN_CENTER;
                    pa_text_ex(c, "CENTRAL", s.x, s.y - sz * 0.5f, sz, &ts);
                }
            }
        }
    } else {
        PA_Color wood = pa_hex(0x8A5634), wood2 = pa_hex(0xA8703E);
        for (int sd = -1; sd <= 1; sd += 2) {
            float px = (float)sd * (TRACK_HALF + 0.75f);
            wbox(c, px - 0.42f, px + 0.42f, 0.0f, 13.0f, z - 0.42f, z + 0.42f, wood, wood2);
            wbox(c, px - 0.6f, px + 0.6f, 12.8f, 13.4f, z - 0.6f, z + 0.6f, wood2, pa_shade(wood2, 0.15f));
        }
        wbox(c, -TRACK_HALF - 1.4f, TRACK_HALF + 1.4f, 10.0f, 10.7f, z - 0.3f, z + 0.3f, wood, wood2);
        wbox(c, -5.2f, 5.2f, 10.7f, 12.9f, z - 0.18f, z + 0.18f, pa_hex(0x6E3A22), pa_hex(0x8A4E2E));
        if (K.z < z - 0.18f) {
            qz(c, z - 0.19f, -4.95f, 4.95f, 10.92f, 12.68f, pa_hex(0xB0402E));
            PA_Vec2 s; float d;
            if (proj(0.0f, 11.8f, z - 0.2f, &s, &d)) {
                float sz = 1.15f * K.F / d;
                if (sz > 5.0f) {
                    PA_TextStyle ts = pa_text_style(PA_FACE_DISPLAY, fogc(pa_hex(0xFFE7A8), d));
                    ts.align = PA_ALIGN_CENTER;
                    ts.outline = maxf(1.0f, sz * 0.08f); ts.outline_col = fogc(pa_hex(0x4A1E12), d);
                    pa_text_ex(c, "SALOON", s.x, s.y - sz * 0.5f, sz, &ts);
                }
            }
        }
        /* Bull skull on top of the sign. */
        PA_Vec2 s; float d;
        if (proj(0.0f, 13.4f, z, &s, &d)) {
            float r = 0.55f * K.F / d;
            fell(c, s.x, s.y, r, r * 0.8f, fogc(pa_hex(0xF4EEE2), d));
            pa_line(c, s.x - r, s.y - r * 0.3f, s.x - r * 2.2f, s.y - r * 1.2f, maxf(1.0f, r * 0.3f), fogc(pa_hex(0xF4EEE2), d));
            pa_line(c, s.x + r, s.y - r * 0.3f, s.x + r * 2.2f, s.y - r * 1.2f, maxf(1.0f, r * 0.3f), fogc(pa_hex(0xF4EEE2), d));
            fcirc(c, s.x - r * 0.35f, s.y - r * 0.05f, r * 0.18f, fogc(pa_hex(0x3A2A22), d));
            fcirc(c, s.x + r * 0.35f, s.y - r * 0.05f, r * 0.18f, fogc(pa_hex(0x3A2A22), d));
        }
    }
}

/* Bunting strung across the street, pennants cycling through the palette. */
static void draw_bunting(PA_Canvas *c, float z) {
    static const uint32_t TOWN[] = { 0xE8343C, 0xFFD23A, 0x2E8BF0, 0x4CC74A, 0xFF7AB0 };
    static const uint32_t DESERT[] = { 0x8A4FD8, 0xFF8A1E, 0xFFD23A, 0x2EC4B0, 0xE8343C };
    int w = world_at(z);
    float y = 10.6f, sag = 1.1f, x0 = -FACADE_X + 0.2f, x1 = FACADE_X - 0.2f;
    int n = 18;
    V3 prev = v3(x0, y, z);
    for (int k = 1; k <= n; k++) {
        float t = (float)k / (float)n;
        V3 cur = v3(x0 + (x1 - x0) * t, y - sag * sinf(t * PA_PI), z);
        wline(c, prev, cur, 0.03f, pa_hex(0x3A2E2A), 1.0f);
        V3 mid = v3((prev.x + cur.x) * 0.5f, (prev.y + cur.y) * 0.5f - 0.6f, z);
        V3 tri[3] = { prev, cur, mid };
        uint32_t col = w ? DESERT[k % 5] : TOWN[k % 5];
        wpoly(c, tri, 3, pa_hex(col));
        prev = cur;
    }
}

static void draw_lamp(PA_Canvas *c, float x, float z) {
    float s = x > 0 ? -1.0f : 1.0f;
    PA_Color pc = pa_hex(0x2F3B48);
    wbox(c, x - 0.09f, x + 0.09f, 0.0f, 4.6f, z - 0.09f, z + 0.09f, pc, pc);
    wbox(c, x - 0.2f, x + 0.2f, 0.0f, 0.3f, z - 0.2f, z + 0.2f, pc, pa_shade(pc, 0.1f));
    float lx = x + s * 0.9f;
    wbox(c, minf(x, lx), maxf(x, lx), 4.45f, 4.58f, z - 0.05f, z + 0.05f, pc, pc);
    g_unlit = 1;
    wbox(c, lx - 0.22f, lx + 0.22f, 3.95f, 4.5f, z - 0.22f, z + 0.22f, pa_hex(0xFFF0B8), pa_hex(0x2F3B48));
    g_unlit = 0;
    /* A flower basket hanging from the arm. */
    V3 fb[1] = { v3(x + s * 0.45f, 3.95f, z) };
    float fr[1] = { 0.28f };
    wcel(c, fb, fr, 1, pa_hex(0xE8508A), 1.0f);
}

static void draw_cactus(PA_Canvas *c, float x, float z) {
    PA_Color g = pa_hex(0x4E9E4A);
    float s = x > 0 ? 1.0f : -1.0f;
    limb(c, v3(x, 0.0f, z), v3(x, 3.3f, z), 0.32f, 0.30f, g, 1.0f);
    limb(c, v3(x, 1.5f, z), v3(x - s * 0.85f, 1.6f, z), 0.2f, 0.2f, g, 1.0f);
    limb(c, v3(x - s * 0.85f, 1.55f, z), v3(x - s * 0.85f, 2.6f, z), 0.2f, 0.19f, g, 1.0f);
    limb(c, v3(x, 2.1f, z), v3(x + s * 0.7f, 2.2f, z), 0.18f, 0.18f, g, 1.0f);
    limb(c, v3(x + s * 0.7f, 2.15f, z), v3(x + s * 0.7f, 2.9f, z), 0.18f, 0.17f, g, 1.0f);
    V3 r[1] = { v3(x + 0.6f, 0.15f, z - 0.4f) };
    float rr_[1] = { 0.3f };
    wcel(c, r, rr_, 1, pa_hex(0xB0705A), 1.0f);
}

static void draw_water_tower(PA_Canvas *c, float x, float z) {
    PA_Color wood = pa_hex(0x8A5634), tank = pa_hex(0xA8683C);
    for (int k = 0; k < 4; k++) {
        float lx = x + ((k & 1) ? 1.0f : -1.0f), lz = z + ((k & 2) ? 1.0f : -1.0f);
        wbox(c, lx - 0.13f, lx + 0.13f, 0.0f, 6.0f, lz - 0.13f, lz + 0.13f, wood, wood);
    }
    wline(c, v3(x - 1.0f, 1.0f, z - 1.0f), v3(x + 1.0f, 5.0f, z - 1.0f), 0.05f, wood, 1.0f);
    wline(c, v3(x + 1.0f, 1.0f, z - 1.0f), v3(x - 1.0f, 5.0f, z - 1.0f), 0.05f, wood, 1.0f);
    /* Octagonal tank and conical roof. */
    V3 rim[8], top[8];
    for (int k = 0; k < 8; k++) {
        float a = PA_TAU * ((float)k + 0.5f) / 8.0f;
        rim[k] = v3(x + cosf(a) * 1.6f, 6.0f, z + sinf(a) * 1.6f);
        top[k] = v3(x + cosf(a) * 1.6f, 9.0f, z + sinf(a) * 1.6f);
    }
    for (int k = 0; k < 8; k++) {
        int n = (k + 1) & 7;
        float mx = (rim[k].x + rim[n].x) * 0.5f - x, mz = (rim[k].z + rim[n].z) * 0.5f - z;
        if (mx * (K.x - x) + mz * (K.z - z) <= 0.0f) continue;
        wquad(c, rim[k], rim[n], top[n], top[k], tank);
        V3 band[4] = { v3(rim[k].x, 7.0f, rim[k].z), v3(rim[n].x, 7.0f, rim[n].z), v3(rim[n].x, 7.25f, rim[n].z), v3(rim[k].x, 7.25f, rim[k].z) };
        wpoly(c, band, 4, pa_hex(0x3A2A22));
    }
    for (int k = 0; k < 8; k++) {
        int n = (k + 1) & 7;
        V3 t[3] = { v3(top[k].x + (top[k].x - x) * 0.1f, 9.0f, top[k].z + (top[k].z - z) * 0.1f),
                    v3(top[n].x + (top[n].x - x) * 0.1f, 9.0f, top[n].z + (top[n].z - z) * 0.1f), v3(x, 10.6f, z) };
        wpoly(c, t, 3, pa_hex(0x6E3A26));
    }
}

static const uint32_t FACADES[] = { 0xC9563F, 0xEDD9AE, 0x4FA4A8, 0xE8B53C, 0xEC92A6, 0x6F86C4, 0x86B95E, 0xE57F3C,
                                    0xB86C4C, 0xF3EEE4 };
static const uint32_t SIGNS[] = { 0xFF4FA0, 0x2EC4F0, 0xFFD23A, 0x7A5AF0 };
static const uint32_t WOODS[] = { 0xB0703E, 0xC98A4E, 0x9A6038, 0xD9A066, 0x8E5A44, 0xC4734A };
static const uint32_t ADOBE[] = { 0xE0A472, 0xD98C5F, 0xEAC08E, 0xCF7F5A };

/* A window on the facade plane at x=fx: frame, glass, glazing bar, sill. */
static void window(PA_Canvas *c, float fx, float off, float wz, float wy, float ww, float wh, PA_Color frame,
                   PA_Color glass, PA_Color sill, int detail, int arched) {
    qx(c, fx + off, wz - ww * 0.5f - 0.12f, wz + ww * 0.5f + 0.12f, wy - 0.08f, wy + wh + 0.12f, frame);
    qx(c, fx + off * 2.0f, wz - ww * 0.5f, wz + ww * 0.5f, wy + 0.04f, wy + wh, glass);
    if (arched) {
        V3 fan[10];
        for (int k = 0; k < 10; k++) {
            float a = PA_PI * (float)k / 9.0f;
            fan[k] = v3(fx + off * 2.0f, wy + wh + sinf(a) * ww * 0.5f, wz + cosf(a) * ww * 0.5f);
        }
        wpoly(c, fan, 8, glass);
    }
    if (!detail) return;
    qx(c, fx + off * 3.0f, wz - 0.03f, wz + 0.03f, wy + 0.04f, wy + wh, frame);
    qx(c, fx + off * 3.0f, wz - ww * 0.5f, wz - ww * 0.1f, wy + wh * 0.6f, wy + wh - 0.1f, PA_RGBA(255, 255, 255, 60));
    qx(c, fx + off * 3.0f, wz - ww * 0.5f - 0.2f, wz + ww * 0.5f + 0.2f, wy - 0.2f, wy - 0.05f, sill);
}

static void draw_building_town(PA_Canvas *c, int side, const Bld *b, float d) {
    float s = side ? 1.0f : -1.0f;
    float fx = s * FACADE_X, bx = s * (FACADE_X + b->depth);
    int v = b->variant;
    PA_Color col = v == 3 ? pa_hex(0xB5513C) : pa_hex(FACADES[b->style % 10]);
    float zc = (b->z0 + b->z1) * 0.5f, off = -s * 0.015f;
    PA_Color endc = pa_shade(col, -0.10f);
    if (K.z < b->z0) wquad(c, v3(fx, 0, b->z0), v3(bx, 0, b->z0), v3(bx, b->h, b->z0), v3(fx, b->h, b->z0), endc);
    if (K.z > b->z1) wquad(c, v3(fx, 0, b->z1), v3(bx, 0, b->z1), v3(bx, b->h, b->z1), v3(fx, b->h, b->z1), endc);
    qx(c, fx, b->z0, b->z1, 0.0f, b->h, col);
    float gh = 0.0f;
    if (v == 0) {
        gh = minf(3.4f, (b->z1 - b->z0) * 0.42f);
        V3 t0[3] = { v3(fx, b->h - 0.05f, b->z0 - 0.25f), v3(fx, b->h - 0.05f, b->z1 + 0.25f), v3(fx, b->h + gh + 0.3f, zc) };
        wpoly(c, t0, 3, pa_hex(0x6A3A34));
        V3 t1[3] = { v3(fx, b->h, b->z0 + 0.25f), v3(fx, b->h, b->z1 - 0.25f), v3(fx, b->h + gh - 0.15f, zc) };
        wpoly(c, t1, 3, col);
    } else if (v == 2) {
        /* Stepped Dutch gable. */
        float hw = (b->z1 - b->z0) * 0.5f;
        for (int k = 0; k < 3; k++) {
            float w = hw * (0.82f - 0.25f * (float)k);
            float y0 = b->h + (float)k * 1.0f;
            qx(c, fx, zc - w, zc + w, y0 - 0.02f, y0 + 1.0f, col);
            qx(c, fx + off, zc - w - 0.12f, zc + w + 0.12f, y0 + 0.88f, y0 + 1.04f, pa_hex(0xF4EEE2));
        }
        gh = 3.0f;
    } else if (v == 4) {
        /* Clock tower rising out of the block. */
        float tz0 = zc - 2.0f, tz1 = zc + 2.0f, th = b->h + 9.0f;
        float tx = s * (FACADE_X + 0.1f), tb = s * (FACADE_X + 4.0f);
        if (K.z < tz0) wquad(c, v3(tx, b->h, tz0), v3(tb, b->h, tz0), v3(tb, th, tz0), v3(tx, th, tz0), endc);
        qx(c, tx, tz0, tz1, b->h - 0.1f, th, pa_shade(col, 0.05f));
        float apx = s * (FACADE_X + 2.05f);
        V3 r1[3] = { v3(tx, th, tz0 - 0.3f), v3(tx, th, tz1 + 0.3f), v3(apx, th + 4.5f, zc) };
        wpoly(c, r1, 3, pa_hex(0x2F6B5A));
        if (K.z < tz0) { V3 r2[3] = { v3(tx, th, tz0 - 0.3f), v3(tb, th, tz0 - 0.3f), v3(apx, th + 4.5f, zc) }; wpoly(c, r2, 3, pa_hex(0x3E8A74)); }
        /* Clock face: a circle on the facade plane, with hands. */
        float cy = th - 2.6f;
        V3 face[16];
        for (int k = 0; k < 16; k++) {
            float a = PA_TAU * (float)k / 16.0f;
            face[k] = v3(tx + off * 2.0f, cy + sinf(a) * 1.45f, zc + cosf(a) * 1.45f);
        }
        wpoly(c, face, 8, pa_hex(0xE6C25A));
        wpoly(c, face + 8, 8, pa_hex(0xE6C25A));
        for (int k = 0; k < 16; k++) face[k] = v3(tx + off * 3.0f, cy + (face[k].y - cy) * 0.86f, zc + (face[k].z - zc) * 0.86f);
        wpoly(c, face, 8, pa_hex(0xFFFBEE));
        wpoly(c, face + 8, 8, pa_hex(0xFFFBEE));
        V3 cpt = v3(tx + off * 4.0f, cy, zc);
        wline(c, cpt, v3(tx + off * 4.0f, cy + 0.95f, zc + 0.15f), 0.08f, pa_hex(0x2A2A30), 1.0f);
        wline(c, cpt, v3(tx + off * 4.0f, cy - 0.2f, zc + 0.65f), 0.1f, pa_hex(0x2A2A30), 1.0f);
    }
    qx(c, fx + off, b->z0, b->z1, 0.0f, 0.5f, pa_shade(col, -0.18f));
    if (v == 1 || v == 3) qx(c, fx + off, b->z0, b->z1, b->h - 0.45f, b->h, pa_shade(col, 0.16f));
    if (d > 125.0f) return;
    int cols = (int)((b->z1 - b->z0 - 0.6f) / 2.25f);
    if (cols < 1) cols = 1;
    float span = (b->z1 - b->z0) / (float)cols;
    PA_Color frame = v == 3 ? pa_hex(0xE8DCC8) : pa_hex(0xF8F6F0);
    PA_Color glass = pa_hex(0x86C4EE), glass2 = pa_hex(0x5A9AD2), sill = pa_shade(col, -0.25f);
    uint32_t h = b->seed;
    int detail = d < 70.0f;
    if (v == 3 && detail)
        for (float my = 0.9f; my < b->h - 0.6f; my += 0.55f) qx(c, fx + off * 0.5f, b->z0, b->z1, my, my + 0.05f, pa_shade(col, -0.12f));
    for (float wy = 3.5f; wy + 1.7f < b->h - 0.5f + gh * 0.5f; wy += 2.75f) {
        for (int k = 0; k < cols; k++) {
            float wz = b->z0 + span * ((float)k + 0.5f);
            if (wy + 1.7f > b->h - 0.4f && (gh <= 0.0f || fabsf(wz - zc) > 0.6f)) continue;
            h = h * 1103515245u + 12345u;
            window(c, fx, off, wz, wy, 1.0f, v == 3 ? 1.3f : 1.56f, frame, (h >> 16) % 5 == 0 ? glass2 : glass, sill, detail, v == 3);
            if (v == 1 && detail && wy < b->h - 3.0f) {
                /* Balcony slab and railing. */
                float bz0 = wz - 0.85f, bz1 = wz + 0.85f, ox = fx - s * 0.6f;
                wbox(c, minf(fx, ox), maxf(fx, ox), wy - 0.32f, wy - 0.18f, bz0, bz1, pa_hex(0xEDE6DA), pa_hex(0xF8F2E8));
                g_unlit = 1;
                qx(c, ox, bz0, bz1, wy - 0.18f, wy + 0.45f, PA_RGBA(40, 52, 60, 170));
                g_unlit = 0;
                qx(c, ox - s * 0.01f, bz0, bz1, wy + 0.38f, wy + 0.47f, pa_hex(0x2A343C));
            }
        }
    }
    if (b->shop && d < 95.0f) {
        float z0 = b->z0 + 0.6f, z1 = b->z1 - 0.6f;
        qx(c, fx + off, z0, z1, 0.5f, 2.5f, pa_hex(0x2B3446));
        qx(c, fx + off * 2.0f, z0 + 0.15f, z1 - 0.15f, 0.65f, 2.35f, pa_hex(0x4F6E8E));
        qx(c, fx + off * 3.0f, z0 + 0.3f, z0 + 1.0f, 1.2f, 2.3f, PA_RGBA(255, 255, 255, 50));
        PA_Color aw = pa_hex(SIGNS[b->seed % 4]);
        int n = (int)((z1 - z0) / 0.7f);
        if (n < 2) n = 2;
        float wz = (z1 - z0) / (float)n;
        for (int k = 0; k < n; k++) {
            float a0 = z0 + wz * (float)k, a1 = a0 + wz;
            wquad(c, v3(fx + off, 3.2f, a0), v3(fx + off, 3.2f, a1), v3(fx - s * 1.2f, 2.55f, a1), v3(fx - s * 1.2f, 2.55f, a0),
                  (k & 1) ? pa_hex(0xF6F2EA) : aw);
        }
        qx(c, fx - s * 1.2f, z0, z1, 2.35f, 2.55f, pa_shade(aw, -0.2f));
    }
    if (b->sign && d < 110.0f) {
        PA_Color sc = pa_hex(SIGNS[(b->sign - 1) % 4]);
        float sz = b->z0 + 1.2f, y0 = 4.0f, y1 = minf(b->h - 1.0f, 7.4f);
        if (y1 > y0 + 1.0f) {
            float a = fx - s * 0.15f, e = fx - s * 1.6f;
            wquad(c, v3(a, y0, sz), v3(e, y0, sz), v3(e, y1, sz), v3(a, y1, sz), pa_hex(0xF8F6F0));
            wquad(c, v3(a - s * 0.08f, y0 + 0.12f, sz - 0.01f), v3(e + s * 0.08f, y0 + 0.12f, sz - 0.01f),
                  v3(e + s * 0.08f, y1 - 0.12f, sz - 0.01f), v3(a - s * 0.08f, y1 - 0.12f, sz - 0.01f), sc);
            for (float ly = y0 + 0.5f; ly < y1 - 0.4f; ly += 0.75f)
                wquad(c, v3(a - s * 0.4f, ly, sz - 0.02f), v3(e + s * 0.4f, ly, sz - 0.02f), v3(e + s * 0.4f, ly + 0.32f, sz - 0.02f),
                      v3(a - s * 0.4f, ly + 0.32f, sz - 0.02f), pa_shade(sc, 0.45f));
        }
    }
}

static void draw_building_canyon(PA_Canvas *c, int side, const Bld *b, float d) {
    float s = side ? 1.0f : -1.0f;
    float fx = s * FACADE_X, bx = s * (FACADE_X + b->depth);
    int v = b->variant;
    float zc = (b->z0 + b->z1) * 0.5f, off = -s * 0.015f;
    int detail = d < 75.0f;
    if (v == 3) {
        /* Mesa: a faceted frustum of red rock with strata. */
        float r0 = s * (FACADE_X + 1.5f), r1 = s * (FACADE_X + 14.0f);
        float ti = 1.2f + (float)(b->seed % 5) * 0.3f;
        PA_Color rock = pa_hex((b->seed & 1) ? 0xC8664E : 0xD47A58);
        float z0 = b->z0 - 1.0f, z1 = b->z1 + 1.0f;
        V3 fz[4] = { v3(r0, 0, z0), v3(r1, 0, z0), v3(r1 - s * ti, b->h, z0 + ti), v3(r0 + s * ti, b->h, z0 + ti) };
        V3 fi[4] = { v3(r0, 0, z0), v3(r0, 0, z1), v3(r0 + s * ti, b->h, z1 - ti), v3(r0 + s * ti, b->h, z0 + ti) };
        V3 ft[4] = { v3(r0 + s * ti, b->h, z0 + ti), v3(r1 - s * ti, b->h, z0 + ti), v3(r1 - s * ti, b->h, z1 - ti), v3(r0 + s * ti, b->h, z1 - ti) };
        if (K.z < z0) wpoly(c, fz, 4, pa_shade(rock, -0.04f));
        wpoly(c, fi, 4, rock);
        if (K.y > b->h) wpoly(c, ft, 4, pa_shade(rock, 0.12f));
        if (detail || d < 140.0f) {
            for (float sy = 1.6f; sy < b->h - 0.8f; sy += 2.3f) {
                float k0 = sy / b->h, k1 = (sy + 0.35f) / b->h;
                V3 st[4] = { v3(r0 + s * ti * k0 + off, sy, z0 + ti * k0), v3(r0 + s * ti * k0 + off, sy, z1 - ti * k0),
                             v3(r0 + s * ti * k1 + off, sy + 0.35f, z1 - ti * k1), v3(r0 + s * ti * k1 + off, sy + 0.35f, z0 + ti * k1) };
                wpoly(c, st, 4, pa_shade(rock, -0.16f));
            }
        }
        return;
    }
    PA_Color col = v == 1 ? pa_hex(ADOBE[b->style % 4]) : pa_hex(WOODS[b->style % 6]);
    PA_Color endc = pa_shade(col, -0.10f);
    if (K.z < b->z0) wquad(c, v3(fx, 0, b->z0), v3(bx, 0, b->z0), v3(bx, b->h, b->z0), v3(fx, b->h, b->z0), endc);
    if (K.z > b->z1) wquad(c, v3(fx, 0, b->z1), v3(bx, 0, b->z1), v3(bx, b->h, b->z1), v3(fx, b->h, b->z1), endc);
    qx(c, fx, b->z0, b->z1, 0.0f, b->h, col);
    if (v == 0 || v == 2) {
        /* False front: a stepped parapet above the roofline, planks, sign board. */
        float ff = v == 2 ? 1.0f : 1.8f;
        V3 pf[6] = { v3(fx, b->h - 0.05f, b->z0), v3(fx, b->h - 0.05f, b->z1), v3(fx, b->h + ff * 0.55f, b->z1),
                     v3(fx, b->h + ff * 0.55f, b->z1 - 1.2f), v3(fx, b->h + ff, zc), v3(fx, b->h + ff * 0.55f, b->z0 + 1.2f) };
        V3 pf2[3] = { pf[0], pf[2], pf[5] };
        wpoly(c, pf, 6, col);
        (void)pf2;
        if (detail)
            for (float py = 0.55f; py < b->h + ff * 0.5f; py += 0.48f) qx(c, fx + off, b->z0, b->z1, py, py + 0.05f, pa_shade(col, -0.18f));
        float sy0 = b->h - 1.6f, sy1 = b->h - 0.3f;
        qx(c, fx + off * 2.0f, b->z0 + 0.8f, b->z1 - 0.8f, sy0, sy1, pa_hex(0xF2E2B8));
        qx(c, fx + off * 3.0f, b->z0 + 1.0f, b->z1 - 1.0f, sy0 + 0.15f, sy1 - 0.15f, pa_hex(0xF2E2B8));
        if (detail)
            for (float tz = b->z0 + 1.3f; tz < b->z1 - 1.5f; tz += 0.7f)
                qx(c, fx + off * 4.0f, tz, tz + 0.45f, sy0 + 0.4f, sy1 - 0.4f, pa_hex(0x8A2A1E));
    } else {
        /* Adobe: soft parapet bumps and viga beam ends. */
        for (float tz = b->z0 + 0.6f; tz < b->z1 - 0.6f; tz += 2.2f) {
            V3 bump[6];
            for (int k = 0; k < 6; k++) {
                float a = PA_PI * (float)k / 5.0f;
                bump[k] = v3(fx, b->h - 0.02f + sinf(a) * 0.45f, tz + 0.6f - cosf(a) * 0.6f);
            }
            wpoly(c, bump, 6, col);
        }
        if (detail)
            for (float tz = b->z0 + 0.7f; tz < b->z1 - 0.4f; tz += 0.95f)
                wbox(c, minf(fx, fx - s * 0.45f), maxf(fx, fx - s * 0.45f), b->h - 0.95f, b->h - 0.7f, tz, tz + 0.25f,
                     pa_hex(0x5A3A26), pa_hex(0x6E4A30));
    }
    if (d > 120.0f) return;
    /* Ground floor: a door, windows, and a porch roof on posts. */
    float dz = b->z0 + (b->z1 - b->z0) * 0.5f;
    PA_Color door = v == 1 ? pa_hex(0x2E8C9A) : pa_hex(0x5A3020);
    qx(c, fx + off, dz - 0.65f, dz + 0.65f, 0.3f, 2.4f, door);
    if (v == 2) {
        qx(c, fx + off * 2.0f, dz - 0.6f, dz - 0.05f, 0.9f, 1.9f, pa_hex(0xE8B23A));
        qx(c, fx + off * 2.0f, dz + 0.05f, dz + 0.6f, 0.9f, 1.9f, pa_hex(0xE8B23A));
    }
    for (int k = -1; k <= 1; k += 2) {
        float wz = dz + (float)k * ((b->z1 - b->z0) * 0.30f);
        window(c, fx, off, wz, 0.9f, 0.9f, 1.1f, pa_shade(col, -0.25f), pa_hex(0x3E5A78), pa_shade(col, -0.3f), detail, 0);
        if (v == 2 && b->h > 6.0f) {
            window(c, fx, off, wz, 4.1f, 0.85f, 1.3f, pa_shade(col, -0.25f), pa_hex(0x3E5A78), pa_shade(col, -0.3f), detail, 0);
            qx(c, fx + off * 3.0f, wz - 0.95f, wz - 0.6f, 4.0f, 5.5f, pa_hex(0x3E7A4A));
            qx(c, fx + off * 3.0f, wz + 0.6f, wz + 0.95f, 4.0f, 5.5f, pa_hex(0x3E7A4A));
        }
    }
    if (v != 1) {
        float px = fx - s * 1.6f, py = v == 2 ? 3.3f : 2.9f;
        wquad(c, v3(fx + off, py + 0.35f, b->z0 + 0.2f), v3(fx + off, py + 0.35f, b->z1 - 0.2f),
              v3(px, py, b->z1 - 0.2f), v3(px, py, b->z0 + 0.2f), pa_hex(0x7A4A2E));
        for (float pz = b->z0 + 0.4f; pz < b->z1; pz += maxf(2.0f, (b->z1 - b->z0 - 0.8f) / 2.0f))
            wbox(c, minf(px, px + s * 0.12f), maxf(px, px + s * 0.12f), 0.0f, py, pz, pz + 0.14f, pa_hex(0x6A3E26), pa_hex(0x7A4A2E));
        if (v == 2 && detail) {
            g_unlit = 1;
            qx(c, px, b->z0 + 0.2f, b->z1 - 0.2f, py + 0.35f, py + 1.05f, PA_RGBA(90, 50, 30, 190));
            g_unlit = 0;
        }
    }
}

static void draw_building(PA_Canvas *c, int side, const Bld *b) {
    float s = side ? 1.0f : -1.0f;
    float d = depth_of(s * FACADE_X, b->h * 0.4f, (b->z0 + b->z1) * 0.5f);
    if (d < -12.0f) return;
    if (b->world) draw_building_canyon(c, side, b, d);
    else draw_building_town(c, side, b, d);
}

static void draw_sky(PA_Canvas *c) {
    float a = canyon_amt(G.z + 60.0f);
    float hy = K.cy - K.F * tanf(K.pitch);
    PA_Paint sky = pa_linear(0, K.oy, 0, hy);
    pa_stop(&sky, 0.0f, WMIX(sky_top, a));
    pa_stop(&sky, 0.55f, WMIX(sky_mid, a));
    pa_stop(&sky, 1.0f, K.haze);
    pa_fill_rect_paint(c, K.ox, K.oy, K.w, maxf(1.0f, hy - K.oy + 2.0f), &sky);
    if (hy < K.oy + K.h) pa_fill_rect(c, K.ox, hy, K.w, K.oy + K.h - hy, K.haze);
    /* The key light's source: a warm sun glow, upper left. */
    glow(c, K.ox + K.w * 0.10f, K.oy + (hy - K.oy) * 0.15f, K.w * 0.55f, pa_hex(0xFFE2A8), 0.55f);
    fcirc(c, K.ox + K.w * 0.10f, K.oy + (hy - K.oy) * 0.15f, K.w * 0.07f, PA_RGBA(255, 246, 220, 230));
    float drift = K.yaw * K.w * 0.8f - K.x * 6.0f;
    /* Far horizon: city towers or red mesas, in the haze. */
    {
        PA_Rng r;
        pa_rng_seed(&r, 31u);
        float x = K.ox - 60.0f + pa_wrapf(drift * 0.3f, 60.0f);
        while (x < K.ox + K.w + 60.0f) {
            float bw = pa_rng_range(&r, 26.0f, 70.0f) * L.u;
            float bh = (hy - K.oy) * pa_rng_range(&r, 0.10f, 0.32f);
            PA_Color tc = pa_mix(pa_hex(0x9EB8D8), K.haze, 0.35f);
            PA_Color mc = pa_mix(pa_hex(0xD98A72), K.haze, 0.25f);
            if (a < 0.99f) pa_fill_rect(c, x, hy - bh, bw - 3.0f, bh + 2.0f, pa_alpha(tc, 1.0f - a));
            if (a > 0.01f) {
                PA_Vec2 m[4] = { { x - bw * 0.3f, hy + 2.0f }, { x + bw * 0.15f, hy - bh * 1.1f }, { x + bw * 0.9f, hy - bh * 1.1f },
                                 { x + bw * 1.3f, hy + 2.0f } };
                fpoly(c, m, 4, pa_alpha(mc, a));
            }
            x += bw;
        }
    }
    for (int k = 0; k < 5; k++) {
        float cx = K.ox + pa_wrapf((float)k * 0.29f * K.w + drift + G.time * 4.0f, K.w * 1.5f) - K.w * 0.25f;
        float cy = K.oy + (hy - K.oy) * (0.18f + 0.13f * (float)(k % 3));
        float s = K.w * (0.06f + 0.018f * (float)(k % 3));
        PA_Color cl = PA_RGBA(255, 255, 255, (int)(215.0f * (1.0f - a * 0.5f)));
        fcirc(c, cx, cy + s * 0.12f, s, pa_alpha(pa_hex(0xC8D8F0), 0.6f * (1.0f - a * 0.5f)));
        fcirc(c, cx, cy, s, cl);
        fcirc(c, cx + s * 0.95f, cy + s * 0.25f, s * 0.78f, cl);
        fcirc(c, cx - s * 0.95f, cy + s * 0.3f, s * 0.66f, cl);
        pa_fill_rect(c, cx - s * 1.6f, cy + s * 0.3f, s * 3.3f, s * 0.6f, cl);
    }
}

static void draw_ground(PA_Canvas *c) {
    float za, zb;
    if (K.cyaw > 0.3f)       { za = K.z - 2.0f; zb = K.z + AHEAD + 10.0f; }
    else if (K.cyaw < -0.3f) { za = K.z - AHEAD - 10.0f; zb = K.z + 2.0f; }
    else                     { za = K.z - 60.0f; zb = K.z + 60.0f; }
    float step = 12.0f;
    float z0 = floorf(za / step) * step;
    for (float z = z0; z < zb; z += step) {
        float z1 = z + step, a = canyon_amt(z + 6.0f);
        PA_Color pave = WMIX(pave, a), pave2 = WMIX(pave2, a), bed = WMIX(bed, a), gravel = WMIX(gravel, a);
        qy(c, 0.0f, -60.0f, -TRACK_HALF, z, z1, pave);
        qy(c, 0.0f, TRACK_HALF, 60.0f, z, z1, pave);
        qy(c, 0.0f, -TRACK_HALF, TRACK_HALF, z, z1, bed);
        for (int l = 0; l < 3; l++) {
            float lx = lane_x(l);
            qy(c, 0.005f, lx - 1.2f, lx + 1.2f, z, z1, gravel);
        }
        qy(c, 0.004f, -TRACK_HALF - 1.9f, -TRACK_HALF - 1.75f, z, z1, pave2);
        qy(c, 0.004f, TRACK_HALF + 1.75f, TRACK_HALF + 1.9f, z, z1, pave2);
    }
    float pj0 = floorf(za / 3.0f) * 3.0f;
    for (float z = pj0; z < zb && z < K.z + 70.0f; z += 3.0f) {
        if (z < K.z - 70.0f) continue;
        float a = canyon_amt(z);
        if (a > 0.5f) continue;
        PA_Color pave2 = WMIX(pave2, a);
        qy(c, 0.004f, -FACADE_X, -TRACK_HALF - 0.4f, z, z + 0.08f, pave2);
        qy(c, 0.004f, TRACK_HALF + 0.4f, FACADE_X, z, z + 0.08f, pave2);
    }
    float sl = 1.05f;
    float sa = floorf(maxf(za, K.z - 75.0f) / sl) * sl, sb = minf(zb, K.z + 75.0f);
    for (float z = sa; z < sb; z += sl) {
        float a = canyon_amt(z);
        PA_Color tie = WMIX(tie, a), tie_top = WMIX(tie_top, a);
        for (int l = 0; l < 3; l++) {
            float lx = lane_x(l);
            if (K.z < z) qz(c, z, lx - 1.08f, lx + 1.08f, 0.0f, 0.1f, tie);
            qy(c, 0.1f, lx - 1.08f, lx + 1.08f, z, z + 0.36f, tie_top);
        }
    }
    PA_Color head = pa_hex(0xEEF0F6), web = pa_hex(0x6A6E7C);
    for (float z = z0; z < zb; z += step) {
        float z1 = z + step;
        for (int l = 0; l < 3; l++) {
            float lx = lane_x(l);
            for (int r = -1; r <= 1; r += 2) {
                float rx = lx + (float)r * 0.74f;
                if (K.x > rx) qx(c, rx + 0.06f, z, z1, 0.08f, 0.24f, web);
                else qx(c, rx - 0.06f, z, z1, 0.08f, 0.24f, web);
                g_unlit = 1;
                qy(c, 0.24f, rx - 0.06f, rx + 0.06f, z, z1, head);
                g_unlit = 0;
            }
        }
    }
}

static void draw_walls(PA_Canvas *c) {
    float za, zb;
    if (K.cyaw > 0.3f)       { za = K.z - 2.0f; zb = K.z + AHEAD + 10.0f; }
    else if (K.cyaw < -0.3f) { za = K.z - AHEAD - 10.0f; zb = K.z + 2.0f; }
    else                     { za = K.z - 60.0f; zb = K.z + 60.0f; }
    float step = 12.0f;
    for (float z = floorf(za / step) * step; z < zb; z += step) {
        float a = canyon_amt(z + 6.0f);
        PA_Color wall = WMIX(wall, a), top = WMIX(wall_top, a);
        for (int s = -1; s <= 1; s += 2) {
            float x0 = (float)s * TRACK_HALF, x1 = (float)s * (TRACK_HALF + 0.35f);
            wbox(c, minf(x0, x1), maxf(x0, x1), 0.0f, 1.0f, z, z + step, wall, top);
        }
    }
}

/* Painter's list. */
enum { IT_CAR, IT_RAMP, IT_BAR, IT_SIGNAL, IT_COIN, IT_PICK, IT_RUNNER, IT_CHASER, IT_GANTRY, IT_PART, IT_SHADOW };
typedef struct { float d; int kind, idx, sub; } Item;
static Item g_items[1400];

static int item_cmp(const void *a, const void *b) {
    float da = ((const Item *)a)->d, db = ((const Item *)b)->d;
    return da < db ? 1 : (da > db ? -1 : 0);
}

static void draw_part(PA_Canvas *c, const Part *p) {
    PA_Vec2 s; float d;
    if (!proj(p->x, p->y, p->z, &s, &d)) return;
    float k = 1.0f - p->t / p->life;
    float r = p->size * K.F / d;
    switch (p->kind) {
    case PT_DUST: fcirc(c, s.x, s.y, r * (1.6f - k * 0.6f), pa_alpha(p->col, 0.55f * k)); break;
    case PT_FLAME:
        fcirc(c, s.x, s.y, r * (0.5f + k * 0.6f), pa_alpha(pa_mix(pa_hex(0xFF5A2A), pa_hex(0xFFE27A), k), 0.85f * k));
        break;
    case PT_STAR: star_shape(c, s.x, s.y, r * 1.4f, 0.45f, p->spin * p->t, pa_alpha(p->col, k)); break;
    case PT_CONFETTI: {
        float a = p->spin * p->t;
        PA_Vec2 q[4] = { { s.x + cosf(a) * r, s.y + sinf(a) * r * 0.5f }, { s.x - sinf(a) * r * 0.6f, s.y + cosf(a) * r },
                         { s.x - cosf(a) * r, s.y - sinf(a) * r * 0.5f }, { s.x + sinf(a) * r * 0.6f, s.y - cosf(a) * r } };
        fpoly(c, q, 4, pa_alpha(p->col, minf(1.0f, k * 2.0f)));
        break;
    }
    case PT_RING: {
        float rr2 = r * (0.6f + (1.0f - k) * 1.6f);
        pa_stroke_circle(c, s.x, s.y, rr2, maxf(1.5f, r * 0.25f * k), pa_alpha(p->col, k));
        break;
    }
    default: star_shape(c, s.x, s.y, r * 1.3f, 0.32f, 0.0f, pa_alpha(p->col, minf(1.0f, k * 1.6f))); break;
    }
}

static void draw_shadow_at(PA_Canvas *c, float x, float y, float z, float rx, float rz, float str) {
    PA_Vec2 a, b; float d, d2;
    if (!proj(x, y, z, &a, &d) || !proj(x + rx, y, z + rz, &b, &d2)) return;
    float sx = fabsf(b.x - a.x), sy = fabsf(b.y - a.y);
    pa_shadow(c, a.x, a.y, maxf(1.0f, sx), maxf(0.8f, sy), str);
}

static void draw_world(PA_Canvas *c) {
    draw_sky(c);
    draw_ground(c);
    /* Contact shadows on the ground under trains and barriers. */
    for (int i = 0; i < G.nobs; i++) {
        const Obs *o = &G.obs[i];
        float x = lane_x(o->lane);
        if (o->kind == OB_TRAIN) {
            if (depth_of(x, 0, o->z) > 160.0f) continue;
            /* Cast shadow thrown right and ahead by the key light, then a
               dark contact strip right under the body. */
            g_unlit = 1;
            qy(c, 0.11f, x - TRAIN_W * 0.5f, x + TRAIN_W * 0.5f + 0.75f, o->z + 0.2f, o->z + o->len + 1.1f,
               PA_RGBA(52, 34, 88, 105));
            qy(c, 0.115f, x - TRAIN_W * 0.5f - 0.12f, x + TRAIN_W * 0.5f + 0.12f, o->z - 0.25f, o->z + o->len + 0.25f,
               PA_RGBA(30, 18, 50, 120));
            g_unlit = 0;
        } else if (o->kind != OB_SIGNAL) {
            g_unlit = 1;
            qy(c, 0.11f, x - 1.25f, x + 1.6f, o->z - 0.2f, o->z + 0.6f, PA_RGBA(52, 34, 88, 80));
            g_unlit = 0;
        }
    }

    /* Buildings, far first, then street furniture, then the track walls. */
    {
        int n = 0;
        for (int s = 0; s < 2; s++)
            for (int i = 0; i < G.nbld[s]; i++) {
                const Bld *b = &G.bld[s][i];
                float d0 = depth_of(s ? FACADE_X : -FACADE_X, 3.0f, b->z0), d1 = depth_of(s ? FACADE_X : -FACADE_X, 3.0f, b->z1);
                if (d0 < -10.0f && d1 < -10.0f) continue;
                if (minf(d0, d1) > 180.0f) continue;
                g_items[n].d = maxf(d0, d1); g_items[n].kind = s; g_items[n].idx = i; n++;
            }
        qsort(g_items, (size_t)n, sizeof(Item), item_cmp);
        for (int i = 0; i < n; i++) draw_building(c, g_items[i].kind, &G.bld[g_items[i].kind][g_items[i].idx]);

        /* Street furniture: lamps in town, cacti in the canyon, and a water
           tower at every other landmark marker out west. */
        n = 0;
        float a = floorf((K.z - 100.0f) / LAMP_EVERY) * LAMP_EVERY;
        for (float z = a; z < K.z + 130.0f; z += LAMP_EVERY) {
            for (int s = -1; s <= 1; s += 2) {
                float x = (float)s * (TRACK_HALF + 1.5f);
                float d = depth_of(x, 2.0f, z);
                if (d < 0.3f || d > 130.0f) continue;
                g_items[n].d = d; g_items[n].kind = s; g_items[n].idx = 0; g_items[n].sub = (int)(z / LAMP_EVERY);
                n++;
            }
        }
        float ma = floorf((K.z - 100.0f) / MARK_EVERY) * MARK_EVERY;
        for (float z = ma; z < K.z + 150.0f; z += MARK_EVERY) {
            int k = (int)floorf(z / MARK_EVERY + 0.5f);
            if (mark_kind(z) != 2 || world_at(z) != 1) continue;
            float x = ((k >> 1) & 1 ? 1.0f : -1.0f) * 6.6f;
            float d = depth_of(x, 5.0f, z);
            if (d < 0.3f || d > 160.0f) continue;
            g_items[n].d = d; g_items[n].kind = x > 0 ? 3 : 2; g_items[n].idx = 1; g_items[n].sub = k;
            n++;
        }
        qsort(g_items, (size_t)n, sizeof(Item), item_cmp);
        for (int i = 0; i < n; i++) {
            const Item *it = &g_items[i];
            if (it->idx == 1) { draw_water_tower(c, it->kind == 3 ? 6.6f : -6.6f, (float)it->sub * MARK_EVERY); continue; }
            float z = (float)it->sub * LAMP_EVERY, x = (float)it->kind * (TRACK_HALF + 1.5f);
            if (world_at(z) == 1) {
                if ((it->sub + (it->kind > 0)) % 2 == 0) draw_cactus(c, x + (float)it->kind * 0.6f, z + 3.0f);
            } else draw_lamp(c, x, z);
        }
    }
    draw_walls(c);

    /* Gantries stand outside the track and their beams ride above the camera,
       so neither can cover anything on the track - unless a jetpack has
       lifted the camera over them, when they join the sorted pass instead. */
    float ga = floorf((K.z - 60.0f) / GANTRY_EVERY) * GANTRY_EVERY;
    int high_cam = K.y > GANTRY_H - 1.2f;
    if (!high_cam) {
        /* Overhead dressing, far to near: gantries, bunting, and the
           landmark bridge or gate at every other marker. */
        int n2 = 0;
        for (float z = ga; z < K.z + 175.0f; z += GANTRY_EVERY) {
            float d = depth_of(0.0f, GANTRY_H * 0.5f, z);
            if (d < 0.3f || d > 175.0f) continue;
            g_items[n2].d = d; g_items[n2].kind = 0; g_items[n2].sub = (int)(z / GANTRY_EVERY); n2++;
        }
        for (float z = floorf((K.z - 60.0f) / 26.0f) * 26.0f + 13.0f; z < K.z + 120.0f; z += 26.0f) {
            float d = depth_of(0.0f, 10.0f, z);
            if (d < 0.3f || d > 120.0f) continue;
            g_items[n2].d = d; g_items[n2].kind = 1; g_items[n2].sub = (int)floorf(z / 26.0f); n2++;
        }
        for (float z = floorf((K.z - 60.0f) / MARK_EVERY) * MARK_EVERY; z < K.z + 175.0f; z += MARK_EVERY) {
            if (mark_kind(z) != 1) continue;
            float d = depth_of(0.0f, 11.0f, z);
            if (d < 9.0f || d > 175.0f) continue;
            g_items[n2].d = d + 0.2f; g_items[n2].kind = 2; g_items[n2].sub = (int)floorf(z / MARK_EVERY + 0.5f); n2++;
        }
        qsort(g_items, (size_t)n2, sizeof(Item), item_cmp);
        for (int i = 0; i < n2; i++) {
            const Item *it = &g_items[i];
            if (it->kind == 0) draw_gantry(c, (float)it->sub * GANTRY_EVERY);
            else if (it->kind == 1) draw_bunting(c, (float)it->sub * 26.0f + 13.0f);
            else draw_overhead_mark(c, (float)it->sub * MARK_EVERY);
        }
    }

    /* Track objects, characters and effects, sorted by far extent. */
    int n = 0;
    const int cap = (int)(sizeof(g_items) / sizeof(g_items[0])) - 8;
    for (int i = 0; i < G.nobs && n < cap; i++) {
        const Obs *o = &G.obs[i];
        float x = lane_x(o->lane);
        if (o->kind == OB_TRAIN) {
            for (int k = 0; k < o->cars && n < cap; k++) {
                float za = o->z + (float)k * (CAR_LEN + CAR_GAP), zb = za + CAR_LEN;
                float d0 = depth_of(x, 1.5f, za), d1 = depth_of(x, 1.5f, zb);
                if (d0 < 0.0f && d1 < 0.0f) continue;
                if (minf(d0, d1) > 175.0f) continue;
                g_items[n].d = maxf(d0, d1); g_items[n].kind = IT_CAR; g_items[n].idx = i; g_items[n].sub = k; n++;
            }
            if (o->ramp) {
                float d0 = depth_of(x, 1.0f, o->z - RAMP_LEN), d1 = depth_of(x, 1.0f, o->z);
                if (d0 > 0.0f || d1 > 0.0f) {
                    g_items[n].d = maxf(d0, d1) - 0.01f; g_items[n].kind = IT_RAMP; g_items[n].idx = i; n++;
                }
            }
        } else {
            float d = depth_of(x, 1.0f, o->z);
            if (d < 0.3f || d > 170.0f) continue;
            g_items[n].d = d; g_items[n].kind = o->kind == OB_SIGNAL ? IT_SIGNAL : IT_BAR; g_items[n].idx = i; n++;
        }
    }
    for (int i = 0; i < G.ncoin && n < cap; i++) {
        float d = depth_of(G.coin[i].x, G.coin[i].y, G.coin[i].z);
        if (d < 0.3f || d > 140.0f) continue;
        g_items[n].d = d; g_items[n].kind = IT_COIN; g_items[n].idx = i; n++;
    }
    for (int i = 0; i < G.npick && n < cap; i++) {
        float d = depth_of(G.pick[i].x, G.pick[i].y, G.pick[i].z);
        if (d < 0.3f) continue;
        g_items[n].d = d; g_items[n].kind = IT_PICK; g_items[n].idx = i; n++;
    }
    for (int i = 0; i < G.npart && n < cap; i++) {
        float d = depth_of(G.part[i].x, G.part[i].y, G.part[i].z);
        if (d < 0.3f) continue;
        g_items[n].d = d - 0.05f; g_items[n].kind = IT_PART; g_items[n].idx = i; n++;
    }
    for (float z = ga; high_cam && z < K.z + 175.0f && n < cap; z += GANTRY_EVERY) {
        float d = depth_of(0.0f, GANTRY_H * 0.5f, z);
        if (d < 0.3f || d > 175.0f) continue;
        g_items[n].d = d + 0.3f; g_items[n].kind = IT_GANTRY; g_items[n].idx = 0; g_items[n].sub = (int)(z / GANTRY_EVERY); n++;
    }
    {
        float rz = G.st == ST_RUN ? G.z : G.crash_z;
        if (G.st == ST_MENU) rz = G.z;
        float d = depth_of(G.x, G.y + 1.0f, rz);
        g_items[n].d = d; g_items[n].kind = IT_RUNNER; n++;
        g_items[n].d = d + 0.01f; g_items[n].kind = IT_SHADOW; g_items[n].idx = 0; n++;
        int chase_vis = G.st == ST_MENU || G.st == ST_CRASH || G.st == ST_REVIVE || G.st == ST_RESULTS || G.chase_d < 6.0f;
        if (chase_vis) {
            float cz = G.st == ST_MENU ? G.z - 3.2f : (G.st == ST_RUN ? G.z : G.crash_z) - G.chase_d;
            float cx = G.st == ST_MENU ? 1.3f : G.chase_x;
            float cd = depth_of(cx, 1.0f, cz);
            if (cd > 0.3f) { g_items[n].d = cd; g_items[n].kind = IT_CHASER; n++; }
        }
    }
    qsort(g_items, (size_t)n, sizeof(Item), item_cmp);

    for (int i = 0; i < n; i++) {
        const Item *it = &g_items[i];
        switch (it->kind) {
        case IT_CAR:    draw_car(c, &G.obs[it->idx], it->sub); break;
        case IT_RAMP:   draw_ramp(c, &G.obs[it->idx]); break;
        case IT_BAR:    draw_barrier(c, &G.obs[it->idx]); break;
        case IT_SIGNAL: draw_signal(c, &G.obs[it->idx]); break;
        case IT_COIN:   draw_coin(c, &G.coin[it->idx]); break;
        case IT_PICK:   draw_pick(c, &G.pick[it->idx]); break;
        case IT_PART:   draw_part(c, &G.part[it->idx]); break;
        case IT_GANTRY: draw_gantry(c, (float)it->sub * GANTRY_EVERY); break;
        case IT_SHADOW: {
            float rz = G.st == ST_RUN || G.st == ST_MENU ? G.z : G.crash_z;
            float h = maxf(0.0f, G.y - G.ground);
            draw_shadow_at(c, G.x, G.ground + 0.02f, rz, 0.55f, 0.35f, 0.38f * (1.0f - pa_clamp01(h / 6.0f)));
            break;
        }
        case IT_RUNNER: draw_runner(c); break;
        case IT_CHASER: {
            int menu = G.st == ST_MENU;
            float cz = menu ? G.z - 3.2f : (G.st == ST_RUN ? G.z : G.crash_z) - G.chase_d;
            float cx = menu ? 1.3f : G.chase_x;
            draw_shadow_at(c, cx, 0.02f, cz, 0.6f, 0.4f, 0.3f);
            draw_shadow_at(c, cx - 1.0f, 0.02f, cz, 0.45f, 0.3f, 0.25f);
            draw_chaser(c, cx, 0.0f, cz, 0.0f, !menu && G.st != ST_RESULTS && G.st != ST_REVIVE, 1);
            break;
        }
        default: break;
        }
    }

    /* Overhead wires sag between the gantries. */
    for (float z = ga; z < K.z + 150.0f; z += GANTRY_EVERY) {
        for (int l = 0; l < 3; l++) {
            float lx = lane_x(l);
            V3 prev = v3(lx, GANTRY_H - 1.0f, z);
            for (int k = 1; k <= 4; k++) {
                float t = (float)k / 4.0f;
                V3 cur = v3(lx, GANTRY_H - 1.0f - 0.45f * sinf(t * PA_PI), z + GANTRY_EVERY * t);
                if (depth_of(prev.x, prev.y, prev.z) > 7.0f && depth_of(cur.x, cur.y, cur.z) > 7.0f)
                    wline(c, prev, cur, 0.035f, pa_hex(0x2A2E38), 1.0f);
                prev = cur;
            }
        }
        for (int s = -1; s <= 1; s += 2) {
            float wx = (float)s * (TRACK_HALF + 0.05f);
            if (depth_of(wx, GANTRY_H, z) < 7.0f || depth_of(wx, GANTRY_H, z + GANTRY_EVERY) < 7.0f) continue;
            wline(c, v3(wx, GANTRY_H - 0.2f, z), v3(wx, GANTRY_H - 0.6f, z + GANTRY_EVERY * 0.5f), 0.03f, pa_hex(0x2A2E38), 1.0f);
            wline(c, v3(wx, GANTRY_H - 0.6f, z + GANTRY_EVERY * 0.5f), v3(wx, GANTRY_H - 0.2f, z + GANTRY_EVERY), 0.03f, pa_hex(0x2A2E38), 1.0f);
        }
    }

    /* Speed streaks: always on the jetpack and board, and once the pace is up. */
    float streak = G.jet_t > 0.0f ? 1.0f : (G.board_t > 0.0f ? 0.75f : pa_clamp01((G.speed - 16.5f) / 5.0f) * 0.6f);
    if (streak > 0.02f && G.st == ST_RUN) {
        for (int k = 0; k < 16; k++) {
            float t = pa_wrapf(G.time * 2.6f + (float)k * 0.137f, 1.0f);
            float side = (k & 1) ? 1.0f : -1.0f;
            float ang = side * (0.3f + (float)(k % 5) * 0.12f);
            float r0 = K.h * (0.15f + t * 0.7f);
            float hx = K.cx, hy = K.cy - K.F * tanf(K.pitch);
            float sx = hx + sinf(ang) * r0, sy = hy + cosf(ang) * r0 * 0.9f;
            pa_line(c, sx, sy, sx + sinf(ang) * 70.0f * L.u, sy + cosf(ang) * 60.0f * L.u, 2.5f * L.u,
                    PA_RGBA(255, 255, 255, (int)(170.0f * t * streak)));
        }
    }
}

/* ================================================================= HUD == */
static void pill(PA_Canvas *c, float x, float y, float w, float h) {
    pa_round_rect(c, x, y + 3.0f * L.u, w, h, h * 0.32f, PA_RGBA(0, 0, 20, 60));
    pa_round_rect(c, x, y, w, h, h * 0.32f, PA_RGBA(22, 26, 56, 215));
    pa_round_rect(c, x + 2.0f, y + 2.0f, w - 4.0f, h * 0.4f, h * 0.25f, PA_RGBA(255, 255, 255, 18));
}

static void blue_button(PA_Canvas *c, Rect r) {
    float u = L.u;
    pa_round_rect(c, r.x, r.y + 4.0f * u, r.w, r.h, 12.0f * u, pa_hex(0x1A4E8E));
    PA_Paint p = pa_linear(0, r.y, 0, r.y + r.h);
    pa_stop(&p, 0.0f, pa_hex(0x76C2FF));
    pa_stop(&p, 1.0f, pa_hex(0x2E86E0));
    pa_round_rect(c, r.x, r.y, r.w, r.h, 12.0f * u, pa_hex(0xDDF0FF));
    pa_round_rect_paint(c, r.x + 3.0f * u, r.y + 3.0f * u, r.w - 6.0f * u, r.h - 6.0f * u, 10.0f * u, &p);
}

static void draw_pause_button(PA_Canvas *c) {
    Rect r = pause_rect();
    blue_button(c, r);
    float u = L.u, bw = 9.0f * u, bh = 26.0f * u;
    for (int k = -1; k <= 1; k += 2) {
        float x = r.x + r.w * 0.5f + (float)k * 8.5f * u - bw * 0.5f;
        pa_round_rect(c, x, r.y + r.h * 0.5f - bh * 0.5f + 2.0f * u, bw, bh, 3.0f * u, PA_RGBA(10, 40, 90, 120));
        pa_round_rect(c, x, r.y + r.h * 0.5f - bh * 0.5f, bw, bh, 3.0f * u, PA_RGB(255, 255, 255));
    }
}

static void draw_score_hud(PA_Canvas *c) {
    float u = L.u, w = (float)L.w;
    char buf[32];
    int sc = (int)G.score;
    snprintf(buf, sizeof(buf), "%05d", sc);
    float size = 30.0f * u;
    float tw = pa_text_width(buf, size, 1.0f * u);
    char mb[8];
    int m = score_mult();
    snprintf(mb, sizeof(mb), "X%d", m);
    float mw = pa_text_width(mb, 18.0f * u, 0.5f * u);
    float pw = tw + mw + 70.0f * u, ph = 52.0f * u;
    float px = w - 14.0f * u - pw, py = 16.0f * u;
    pill(c, px, py, pw, ph);
    PA_Color mc = G.mult_t > 0.0f ? pa_hex(0x6CF0FF) : pa_hex(0xFFD23A);
    pa_text_bold(c, mb, px + 14.0f * u, py + 17.0f * u, 18.0f * u, mc, pa_hex(0x10142E), PA_ALIGN_LEFT, 0.5f * u, 1.1f);
    float sx = px + 14.0f * u + mw + 18.0f * u;
    star_shape(c, sx, py + ph * 0.5f + 1.5f * u, 15.0f * u, 0.5f, 0.0f, pa_hex(0xC98A12));
    star_shape(c, sx, py + ph * 0.5f, 14.0f * u, 0.5f, 0.0f, pa_hex(0xFFD23A));
    pa_text_bold(c, buf, w - 26.0f * u, py + 11.0f * u, size, PA_RGB(255, 255, 255), pa_hex(0x10142E), PA_ALIGN_RIGHT, 1.0f * u, 1.3f);

    /* Coins under it. */
    snprintf(buf, sizeof(buf), "%d", G.coins);
    float cs = 24.0f * u;
    float cw = pa_text_width(buf, cs, 1.0f * u) + 62.0f * u, chh = 44.0f * u;
    float cx = w - 14.0f * u - cw, cy = py + ph + 10.0f * u;
    pill(c, cx, cy, cw, chh);
    float pop = 1.0f + G.coin_pop * 0.25f;
    pa_text_bold(c, buf, w - 52.0f * u, cy + 10.0f * u, cs, PA_RGB(255, 255, 255), pa_hex(0x10142E), PA_ALIGN_RIGHT, 1.0f * u, 1.2f);
    coin_icon(c, w - 34.0f * u, cy + chh * 0.5f, 14.0f * u * pop);

    /* Active power-up timers, stacked under the coins. */
    float ty = cy + chh + 14.0f * u;
    struct { int kind; float t, max; } act[5] = {
        { PK_MAGNET, G.magnet_t, 10.0f }, { PK_JET, G.jet_t, 7.0f }, { PK_SNEAK, G.sneak_t, 10.0f }, { PK_MULT, G.mult_t, 10.0f },
        { PK_BOARD, G.board_t, 10.0f } };
    for (int i = 0; i < 5; i++) {
        if (act[i].t <= 0.0f) continue;
        float bw = 96.0f * u, bh = 12.0f * u;
        float ix = w - 14.0f * u - bw - 44.0f * u;
        pill(c, ix, ty, bw + 44.0f * u, 38.0f * u);
        fcirc(c, ix + 20.0f * u, ty + 19.0f * u, 15.0f * u, PA_RGBA(255, 255, 255, 40));
        pick_icon(c, act[i].kind, ix + 20.0f * u, ty + 19.0f * u, 14.0f * u);
        float k = pa_clamp01(act[i].t / act[i].max);
        pa_round_rect(c, ix + 40.0f * u, ty + 13.0f * u, bw - 6.0f * u, bh, bh * 0.5f, PA_RGBA(0, 0, 0, 120));
        int blink = act[i].t < 2.0f && fmodf(G.time, 0.3f) < 0.15f;
        pa_round_rect(c, ix + 40.0f * u, ty + 13.0f * u, (bw - 6.0f * u) * k, bh, bh * 0.5f,
                      blink ? pa_hex(0xFFFFFF) : pa_hex(0x6CE05A));
        ty += 46.0f * u;
    }
}

static void draw_banner(PA_Canvas *c) {
    if (G.banner_t <= 0.0f) return;
    float t = 1.5f - G.banner_t;
    float pop = 1.0f + (1.0f - pa_clamp01(t * 7.0f)) * 0.5f;
    float a = pa_clamp01(G.banner_t * 4.0f);
    float size = 34.0f * L.u * pop;
    float y = (float)L.h * 0.22f - size * 0.5f;
    pa_text_bold(c, G.banner, (float)L.w * 0.5f, y, size, pa_alpha(G.banner_col, a), pa_alpha(pa_hex(0x1C1240), a),
                 PA_ALIGN_CENTER, 2.0f * L.u, 2.0f);
}

static void panel(PA_Canvas *c, float x, float y, float w, float h) {
    float u = L.u;
    pa_round_rect(c, x, y + 8.0f * u, w, h, 26.0f * u, PA_RGBA(0, 0, 30, 90));
    pa_round_rect(c, x, y, w, h, 26.0f * u, pa_hex(0xE9F2FF));
    PA_Paint p = pa_linear(0, y, 0, y + h);
    pa_stop(&p, 0.0f, pa_hex(0x2F6BD8));
    pa_stop(&p, 1.0f, pa_hex(0x1D3E9E));
    pa_round_rect_paint(c, x + 5.0f * u, y + 5.0f * u, w - 10.0f * u, h - 10.0f * u, 22.0f * u, &p);
}

static void big_button(PA_Canvas *c, Rect r, const char *label, uint32_t col, int enabled) {
    float u = L.u;
    PA_Color base = enabled ? pa_hex(col) : pa_hex(0x8A8E9A);
    pa_round_rect(c, r.x, r.y + 6.0f * u, r.w, r.h, 18.0f * u, pa_shade(base, -0.4f));
    pa_round_rect(c, r.x, r.y, r.w, r.h, 18.0f * u, base);
    pa_round_rect(c, r.x + 6.0f * u, r.y + 5.0f * u, r.w - 12.0f * u, r.h * 0.42f, 13.0f * u, PA_RGBA(255, 255, 255, 50));
    float size = r.h * 0.42f;
    pa_text_bold(c, label, r.x + r.w * 0.5f, r.y + r.h * 0.5f - size * 0.5f, size, PA_RGB(255, 255, 255), pa_hex(0x14203C),
                 PA_ALIGN_CENTER, 2.0f * u, 1.6f);
}

static void draw_menu_ui(PA_Canvas *c) {
    float u = L.u, w = (float)L.w, h = (float)L.h;
    /* Logo: a tilted sticker with the name sprayed across it. */
    float lx = w * 0.5f, ly = h * 0.13f;
    float sw = minf(w * 0.86f, 470.0f * u), sh = 190.0f * u;
    PA_Vec2 st[4] = { { lx - sw * 0.5f, ly - sh * 0.36f }, { lx + sw * 0.5f, ly - sh * 0.50f },
                      { lx + sw * 0.47f, ly + sh * 0.50f }, { lx - sw * 0.47f, ly + sh * 0.56f } };
    PA_Vec2 sd[4];
    for (int k = 0; k < 4; k++) { sd[k].x = st[k].x + 6.0f * u; sd[k].y = st[k].y + 10.0f * u; }
    fpoly(c, sd, 4, PA_RGBA(10, 20, 60, 90));
    fpoly(c, st, 4, pa_hex(0xFFFFFF));
    PA_Vec2 in[4];
    for (int k = 0; k < 4; k++) { in[k].x = lx + (st[k].x - lx) * 0.94f; in[k].y = ly + (st[k].y - ly) * 0.9f; }
    fpoly(c, in, 4, pa_hex(0xFF4F8E));
    /* Paint splats. */
    fcirc(c, lx - sw * 0.40f, ly + sh * 0.36f, 18.0f * u, pa_hex(0x2EC4F0));
    fcirc(c, lx - sw * 0.36f, ly + sh * 0.46f, 8.0f * u, pa_hex(0x2EC4F0));
    fcirc(c, lx + sw * 0.42f, ly - sh * 0.32f, 14.0f * u, pa_hex(0xFFD23A));
    float t1 = 60.0f * u, t2 = 78.0f * u;
    pa_text_bold(c, "ROOFTOP", lx + 4.0f * u, ly - sh * 0.36f + 5.0f * u, t1, pa_hex(0xFF8A1E), pa_hex(0x2A1450),
                 PA_ALIGN_CENTER, 2.0f * u, 2.6f);
    pa_text_bold(c, "ROOFTOP", lx, ly - sh * 0.36f, t1, pa_hex(0xFFD23A), pa_hex(0x2A1450), PA_ALIGN_CENTER, 2.0f * u, 2.2f);
    pa_text_bold(c, "RUN", lx + 4.0f * u, ly + sh * 0.02f + 5.0f * u, t2, pa_hex(0xFF8A1E), pa_hex(0x2A1450),
                 PA_ALIGN_CENTER, 6.0f * u, 2.6f);
    pa_text_bold(c, "RUN", lx, ly + sh * 0.02f, t2, pa_hex(0xFFD23A), pa_hex(0x2A1450), PA_ALIGN_CENTER, 6.0f * u, 2.2f);

    /* Wallet: coins and keys, top right; best under the logo. */
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", g_bank);
    float cw = pa_text_width(buf, 22.0f * u, 1.0f * u) + 60.0f * u;
    float py = ly + sh * 0.62f + 14.0f * u;
    pill(c, w * 0.5f - cw - 8.0f * u, py, cw, 42.0f * u);
    coin_icon(c, w * 0.5f - cw + 14.0f * u, py + 21.0f * u, 13.0f * u);
    pa_text_bold(c, buf, w * 0.5f - 22.0f * u, py + 10.0f * u, 22.0f * u, PA_RGB(255, 255, 255), pa_hex(0x10142E), PA_ALIGN_RIGHT, 1.0f * u, 1.2f);
    snprintf(buf, sizeof(buf), "%d", g_keys);
    float kw = pa_text_width(buf, 22.0f * u, 1.0f * u) + 70.0f * u;
    pill(c, w * 0.5f + 8.0f * u, py, kw, 42.0f * u);
    key_icon(c, w * 0.5f + 34.0f * u, py + 21.0f * u, 20.0f * u);
    pa_text_bold(c, buf, w * 0.5f + kw - 6.0f * u, py + 10.0f * u, 22.0f * u, PA_RGB(255, 255, 255), pa_hex(0x10142E), PA_ALIGN_RIGHT, 1.0f * u, 1.2f);

    snprintf(buf, sizeof(buf), "BEST %05d", g_best);
    pa_text_bold(c, buf, w * 0.5f, py + 56.0f * u, 18.0f * u, PA_RGB(255, 255, 255), pa_hex(0x10142E), PA_ALIGN_CENTER, 2.0f * u, 1.2f);

    /* Call to action. */
    float pulse = 1.0f + 0.06f * sinf(G.time * 5.0f);
    float ts = 34.0f * u * pulse;
    float by = h * 0.84f;
    pa_text_bold(c, "TAP TO PLAY", w * 0.5f, by - ts * 0.5f, ts, PA_RGB(255, 255, 255), pa_hex(0x14203C), PA_ALIGN_CENTER, 3.0f * u, 2.0f);
    pa_text_bold(c, "SWIPE < > TO SWITCH LANES", w * 0.5f, by + 36.0f * u, 14.0f * u, pa_hex(0xFFD23A), pa_hex(0x14203C),
                 PA_ALIGN_CENTER, 1.5f * u, 1.1f);
    pa_text_bold(c, "UP TO JUMP   DOWN TO ROLL", w * 0.5f, by + 60.0f * u, 14.0f * u, pa_hex(0xFFD23A), pa_hex(0x14203C),
                 PA_ALIGN_CENTER, 1.5f * u, 1.1f);
}

static void draw_revive(PA_Canvas *c) {
    float u = L.u, w = (float)L.w, h = (float)L.h;
    float k = pa_smooth(pa_clamp01(G.st_t * 4.0f));
    pa_fill_rect(c, 0, 0, w, h, PA_RGBA(10, 14, 40, (int)(150.0f * k)));
    float pw = minf(w * 0.86f, 400.0f * u), ph = 430.0f * u;
    float px = w * 0.5f - pw * 0.5f, py = revive_top() + (1.0f - k) * 80.0f * u;
    (void)h;
    panel(c, px, py, pw, ph);
    pa_text_bold(c, "SAVE ME!", w * 0.5f, py + 28.0f * u, 40.0f * u, pa_hex(0xFFD23A), pa_hex(0x14203C), PA_ALIGN_CENTER, 3.0f * u, 2.2f);
    /* Countdown ring around the key. */
    float cx = w * 0.5f, cy = py + 160.0f * u, r = 68.0f * u;
    float left = pa_clamp01(1.0f - G.st_t / 4.0f);
    pa_stroke_circle(c, cx, cy, r, 12.0f * u, PA_RGBA(0, 0, 0, 70));
    {
        PA_Vec2 q[80];
        int n = 0;
        int segs = (int)(64.0f * left) + 1;
        for (int i = 0; i <= segs && n < 80; i++) {
            float a = -PA_PI * 0.5f + PA_TAU * left * (float)i / (float)segs;
            q[n].x = cx + cosf(a) * r; q[n].y = cy + sinf(a) * r; n++;
        }
        if (n > 1) pa_stroke_poly(c, q, n, 0, 12.0f * u, pa_hex(0x6CE05A));
    }
    fcirc(c, cx, cy, r - 12.0f * u, PA_RGBA(255, 255, 255, 30));
    key_icon(c, cx + 4.0f * u, cy, 58.0f * u);
    char buf[32];
    int cost = revive_cost();
    snprintf(buf, sizeof(buf), "%d", (int)ceilf(4.0f - G.st_t));
    pa_text_bold(c, buf, cx + r * 0.75f, cy - r * 0.95f, 26.0f * u, PA_RGB(255, 255, 255), pa_hex(0x14203C), PA_ALIGN_CENTER, 0.0f, 1.4f);
    Rect b = revive_button();
    int can = g_keys >= cost;
    if (can) {
        Rect lb = b;
        lb.w -= 70.0f * u;
        big_button(c, b, "", 0x4CC74A, 1);
        float kx = b.x + b.w - 66.0f * u;
        float sz = b.h * 0.40f;
        PA_TextStyle ts = pa_text_bold_style(sz, PA_RGB(255, 255, 255), pa_hex(0x14203C), PA_ALIGN_CENTER, 1.0f * u, 1.6f);
        float tw = pa_text_measure("REVIVE", sz, &ts).width;
        float room = kx - 24.0f * u - (b.x + 14.0f * u);
        if (tw > room) { sz *= room / tw; ts = pa_text_bold_style(sz, PA_RGB(255, 255, 255), pa_hex(0x14203C), PA_ALIGN_CENTER, 1.0f * u, 1.6f); }
        pa_text_ex(c, "REVIVE", b.x + 14.0f * u + room * 0.5f, b.y + b.h * 0.5f - sz * 0.5f, sz, &ts);
        (void)lb;
        pa_round_rect(c, kx - 16.0f * u, b.y + 12.0f * u, 64.0f * u, b.h - 24.0f * u, 12.0f * u, PA_RGBA(0, 40, 0, 70));
        key_icon(c, kx + 6.0f * u, b.y + b.h * 0.5f, 16.0f * u);
        snprintf(buf, sizeof(buf), "%d", cost);
        pa_text_bold(c, buf, kx + 36.0f * u, b.y + b.h * 0.5f - 11.0f * u, 22.0f * u, PA_RGB(255, 255, 255), pa_hex(0x14203C),
                     PA_ALIGN_CENTER, 0.0f, 1.2f);
    } else {
        snprintf(buf, sizeof(buf), "NEED %d KEYS", cost);
        big_button(c, b, buf, 0x4CC74A, 0);
    }
    snprintf(buf, sizeof(buf), "YOU HAVE %d", g_keys);
    pa_text(c, buf, w * 0.5f, py + 248.0f * u, 14.0f * u, PA_RGBA(255, 255, 255, 210), PA_ALIGN_CENTER, 2.0f * u);
    Rect sk = skip_button();
    pa_text_bold(c, "NO THANKS", sk.x + sk.w * 0.5f, sk.y + 14.0f * u, 18.0f * u, pa_hex(0xCFE0FF), pa_hex(0x14203C), PA_ALIGN_CENTER, 2.0f * u, 1.0f);
}

static void draw_results(PA_Canvas *c) {
    float u = L.u, w = (float)L.w, h = (float)L.h;
    float k = pa_smooth(pa_clamp01(G.st_t * 3.0f));
    pa_fill_rect(c, 0, 0, w, h, PA_RGBA(10, 14, 40, (int)(165.0f * k)));
    float pw = minf(w * 0.88f, 420.0f * u), ph = 400.0f * u;
    float px = w * 0.5f - pw * 0.5f, py = h * 0.5f - 300.0f * u + (1.0f - k) * 100.0f * u;
    panel(c, px, py, pw, ph);
    /* Ribbon header. */
    float rw = pw + 30.0f * u, rh = 62.0f * u, rx = w * 0.5f - rw * 0.5f, ry = py - 26.0f * u;
    PA_Color rib = G.new_best ? pa_hex(0xFF4F8E) : pa_hex(0xFF8A1E);
    PA_Vec2 tl[3] = { { rx, ry + 10.0f * u }, { rx + 30.0f * u, ry + 10.0f * u }, { rx + 30.0f * u, ry + rh + 10.0f * u } };
    PA_Vec2 tr[3] = { { rx + rw, ry + 10.0f * u }, { rx + rw - 30.0f * u, ry + 10.0f * u }, { rx + rw - 30.0f * u, ry + rh + 10.0f * u } };
    fpoly(c, tl, 3, pa_shade(rib, -0.35f));
    fpoly(c, tr, 3, pa_shade(rib, -0.35f));
    pa_round_rect(c, rx + 14.0f * u, ry, rw - 28.0f * u, rh, 10.0f * u, rib);
    pa_round_rect(c, rx + 18.0f * u, ry + 4.0f * u, rw - 36.0f * u, rh * 0.4f, 8.0f * u, PA_RGBA(255, 255, 255, 50));
    const char *title = G.new_best ? "NEW HIGHSCORE!" : "RUN OVER";
    pa_text_bold(c, title, w * 0.5f, ry + 14.0f * u, 30.0f * u, PA_RGB(255, 255, 255), pa_hex(0x14203C), PA_ALIGN_CENTER, 2.0f * u, 1.8f);

    char buf[32];
    pa_text_bold(c, "SCORE", w * 0.5f, py + 62.0f * u, 18.0f * u, pa_hex(0xBFD8FF), pa_hex(0x14203C), PA_ALIGN_CENTER, 3.0f * u, 1.0f);
    float shown = (float)(int)G.score * pa_smooth(pa_clamp01((G.st_t - 0.2f) / 0.9f));
    snprintf(buf, sizeof(buf), "%d", (int)shown);
    pa_text_bold(c, buf, w * 0.5f, py + 92.0f * u, 62.0f * u, PA_RGB(255, 255, 255), pa_hex(0x14203C), PA_ALIGN_CENTER, 3.0f * u, 2.4f);
    /* Coins and keys rows. */
    float ry2 = py + 196.0f * u;
    pa_round_rect(c, px + 26.0f * u, ry2, pw - 52.0f * u, 56.0f * u, 14.0f * u, PA_RGBA(0, 0, 30, 70));
    coin_icon(c, px + 60.0f * u, ry2 + 28.0f * u, 17.0f * u);
    snprintf(buf, sizeof(buf), "+%d", G.coins);
    pa_text_bold(c, buf, px + 88.0f * u, ry2 + 15.0f * u, 26.0f * u, PA_RGB(255, 255, 255), pa_hex(0x14203C), PA_ALIGN_LEFT, 1.0f * u, 1.3f);
    snprintf(buf, sizeof(buf), "%d", g_bank);
    pa_text_bold(c, buf, px + pw - 48.0f * u, ry2 + 19.0f * u, 18.0f * u, pa_hex(0xFFD23A), pa_hex(0x14203C), PA_ALIGN_RIGHT, 1.0f * u, 1.1f);
    pa_text(c, "TOTAL", px + pw - 48.0f * u, ry2 + 2.0f * u, 10.0f * u, PA_RGBA(255, 255, 255, 170), PA_ALIGN_RIGHT, 2.0f * u);
    float ry3 = ry2 + 68.0f * u;
    snprintf(buf, sizeof(buf), "BEST  %d", g_best);
    pa_text_bold(c, buf, w * 0.5f, ry3 + 4.0f * u, 22.0f * u, pa_hex(0xFFD23A), pa_hex(0x14203C), PA_ALIGN_CENTER, 2.0f * u, 1.3f);
    snprintf(buf, sizeof(buf), "DISTANCE %dM", (int)G.dist);
    pa_text(c, buf, w * 0.5f, ry3 + 40.0f * u, 14.0f * u, PA_RGBA(255, 255, 255, 200), PA_ALIGN_CENTER, 2.0f * u);
    if (G.new_best) {
        for (int i = 0; i < 18; i++) {
            float t = pa_wrapf(G.st_t * 0.35f + (float)i * 0.173f, 1.0f);
            float x = w * pa_wrapf((float)i * 0.37f + 0.1f, 1.0f) + sinf(G.st_t * 2.0f + (float)i) * 20.0f * u;
            float y = -20.0f * u + t * h * 1.1f;
            float a = G.st_t * 4.0f + (float)i;
            PA_Color col = pa_hsl((float)i / 18.0f, 0.85f, 0.6f);
            PA_Vec2 q[4] = { { x + cosf(a) * 7 * u, y + sinf(a) * 4 * u }, { x - sinf(a) * 4 * u, y + cosf(a) * 7 * u },
                             { x - cosf(a) * 7 * u, y - sinf(a) * 4 * u }, { x + sinf(a) * 4 * u, y - cosf(a) * 7 * u } };
            fpoly(c, q, 4, col);
        }
    }
    big_button(c, play_button(), "PLAY", 0x4CC74A, 1);
    big_button(c, home_button(), "HOME", 0x3A8EE8, 1);
}

static void rn_render(PA_Canvas *c) {
    L.w = c->w; L.h = c->h;
    L.u = pa_clampf(minf((float)c->w / 540.0f, (float)c->h / 760.0f), 0.65f, 1.6f);
    cam_setup(0.0f, 0.0f, (float)c->w, (float)c->h, G.shake);
    draw_world(c);

    if (G.flash > 0.0f) pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, pa_alpha(G.flash_col, pa_clamp01(G.flash) * 0.6f));

    pa_hub_hide_pause();
    if (G.st == ST_MENU) {
        draw_menu_ui(c);
        pa_hub_pause_anchor((float)c->w - 36.0f * L.u, 38.0f * L.u, 22.0f * L.u);
        return;
    }
    if (G.st == ST_RUN || G.st == ST_CRASH) {
        /* Gentle scrim so white numerals hold over a bright sky. */
        PA_Paint s = pa_linear(0, 0, 0, 150.0f * L.u);
        pa_stop(&s, 0.0f, PA_RGBA(10, 20, 60, 70));
        pa_stop(&s, 1.0f, PA_RGBA(10, 20, 60, 0));
        pa_fill_rect_paint(c, 0, 0, (float)c->w, 150.0f * L.u, &s);
        draw_pause_button(c);
        draw_score_hud(c);
        draw_banner(c);
    }
    if (G.st == ST_REVIVE) draw_revive(c);
    if (G.st == ST_RESULTS) draw_results(c);
}

/* ============================================================== THUMB == */
static Game g_thumb;
static int  g_thumb_ready;

static void rn_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    static Game keep;
    keep = G;
    if (!g_thumb_ready) {
        g_mute = 1;
        uint32_t seed_runs = (uint32_t)g_runs;
        int b = g_best, k = g_keys, bk = g_bank, ld = g_loaded;
        g_loaded = 1;
        memset(&G, 0, sizeof(G));
        new_run(0);
        G.st = ST_RUN;
        g_force_bot = 1;
        PA_Input in;
        memset(&in, 0, sizeof(in));
        for (int i = 0; i < 120 * 9; i++) {
            run_step(1.0f / 120.0f, &in);
            G.time += 1.0f / 120.0f;
            update_particles(1.0f / 120.0f);
            camera_follow(1.0f / 120.0f);
            if (G.st != ST_RUN) break;
        }
        G.npart = 0;
        G.banner_t = 0.0f;
        g_thumb = G;
        g_runs = (int)seed_runs; g_best = b; g_keys = k; g_bank = bk; g_loaded = ld;
        g_mute = 0;
        g_force_bot = 0;
        g_thumb_ready = 1;
    }
    G = g_thumb;
    G.time = t;
    G.phase = t * 15.0f;
    int k0 = c->clip_x0, k1 = c->clip_x1, k2 = c->clip_y0, k3 = c->clip_y1;
    int nx0 = (int)x, ny0 = (int)y, nx1 = (int)(x + w), ny1 = (int)(y + h);
    if (nx0 > c->clip_x0) c->clip_x0 = nx0;
    if (ny0 > c->clip_y0) c->clip_y0 = ny0;
    if (nx1 < c->clip_x1) c->clip_x1 = nx1;
    if (ny1 < c->clip_y1) c->clip_y1 = ny1;
    float lw = (float)L.w, lh = (float)L.h, lu = L.u;
    L.u = 0.5f;
    cam_setup(x, y, w, h, 0.0f);
    K.F = minf(1.1f * w, 0.62f * h);
    K.cy = y + h * 0.30f + K.F * tanf(K.pitch);
    if (c->clip_x1 > c->clip_x0 && c->clip_y1 > c->clip_y0) draw_world(c);
    L.w = (int)lw; L.h = (int)lh; L.u = lu;
    c->clip_x0 = k0; c->clip_x1 = k1; c->clip_y0 = k2; c->clip_y1 = k3;
    G = keep;
}

const PA_Game PA_GAME_RUNNER = {
    "runner", "Rooftop Run", "Endless Runner",
    "Three lanes, vault and slide, dodge the trains.",
    PA_RGB(255, 70, 90),
    rn_start, rn_stop, rn_update, rn_render, rn_thumb
};
