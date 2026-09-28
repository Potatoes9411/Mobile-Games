/* ===========================================================================
   CHROME RUSH - native port
   Endless highway. Hold a lane, weave the traffic, and bank near misses -
   scoring rewards the thing that scares you, so the safe line down an empty
   shoulder is also the poorest line.

   Built to the same rules the rest of the native suite follows: flat shaded,
   vivid, silhouette carrying the read, and a soft contact shadow under anything
   that touches the road, because there is no lighting in the scene to do it.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define LANES        5
#define LANE_W       2.4f
#define ROAD_W       (LANES * LANE_W)
#define VIEW_AHEAD   42.0f
#define VIEW_BEHIND  10.0f
#define MAX_TRAFFIC  40
#define MAX_COINS    60
#define MAX_PROPS    80

typedef struct {
    int   lane, big, oncoming, counted;
    float x, z, speed, len, wide, drift, shoved;
    float hue;
} Car;

typedef struct { float x, z; int spin; } Coin;
typedef struct { float x, z, w, h; PA_Color tint; int lit; uint32_t mask; } Prop;

typedef struct {
    const char *name;
    float top, accel, grip, len, wide;
    int   bulk;
    uint32_t body, roof;
    int   ability;              /* 0 boost, 1 siren, 2 ram, 3 phase */
    const char *ability_name;
    float cool;
} CarDef;

enum { AB_BOOST, AB_SIREN, AB_RAM, AB_PHASE };

static const CarDef CARS[] = {
    { "CITY HATCH",  30.f, 7.0f,  9.0f, 1.9f, 1.05f, 0, 0xE8283C, 0xFF9AA8, AB_BOOST, "NITRO",   9.f },
    { "WEDGE GT",    38.f, 8.6f, 11.0f, 2.0f, 1.02f, 0, 0x3D8BFF, 0x9FCBFF, AB_BOOST, "OVERDRIVE", 7.f },
    { "INTERCEPTOR", 35.f, 8.0f, 12.0f, 2.1f, 1.10f, 1, 0x1E2740, 0xEDEDF2, AB_SIREN, "SIREN",   11.f },
    { "HAUL RIG",    31.f, 5.4f,  6.6f, 3.4f, 1.24f, 2, 0xE9A03F, 0xFFD79A, AB_RAM,   "BULL BAR", 10.f },
    { "SKIFF",       41.f, 9.4f, 14.0f, 2.0f, 0.98f, 0, 0x2FD6A4, 0xC4FFEA, AB_PHASE, "PHASE",   12.f }
};
#define CAR_COUNT ((int)(sizeof(CARS) / sizeof(CARS[0])))

typedef struct {
    const char *name;
    uint32_t sky_top, sky_mid, sky_low;   /* vivid three-stop sky */
    uint32_t skyline, ground, road, line, rail;
    int      night;                       /* stars, moon, lit windows */
    int      sun;                         /* big low sun disc */
} Biome;

/* Measured off the Dashy Crashy plates: saturated sky gradients (bright day
   blue, red-to-orange sunset, violet night), pale lavender tarmac by day and a
   purple one by night, white lines and rails, green verges. */
static const Biome BIOMES[] = {
    { "DOWNTOWN",   0x1F7BEA, 0x5FB4FF, 0xCDEBFF, 0x3F72D8, 0x6CCB4E, 0xC7C4EC, 0xFFFFFF, 0xF4F4FA, 0, 0 },
    { "SUNSET RUN", 0xD81E4E, 0xFF5A3C, 0xFFB45C, 0x8A1E52, 0x72C04A, 0xB9B4D8, 0xFFFFFF, 0xF4F0FA, 0, 1 },
    { "NIGHT DRIVE",0x140F5A, 0x3B1D9E, 0x8A3CD8, 0x2A1666, 0x3A1E82, 0x7D3FD0, 0xFFC4F0, 0xE6D8FF, 1, 0 },
    { "PINE PASS",  0x1E9AD8, 0x78D0F0, 0xE6FAFF, 0x2E8A5A, 0x58B848, 0xCFCBE8, 0xFFFFFF, 0xF4F4FA, 0, 0 },
};
#define BIOME_COUNT ((int)(sizeof(BIOMES) / sizeof(BIOMES[0])))


typedef struct {
    PA_Rng rand;
    int    car_index;
    float  x, vx, z, speed;
    Car    traffic[MAX_TRAFFIC];
    int    traffic_count;
    Coin   coins[MAX_COINS];
    int    coin_count;
    Prop   props[MAX_PROPS];
    int    prop_count;
    float  next_spawn, next_prop;
    int    score, picked, near_miss;
    float  near_timer, cooldown, ability_t, lean;
    int    biome;
    float  biome_mix;
    int    crashed;
    float  crash_t, time;
    char   banner[24];
    float  banner_t;
    float  yaw, yaw_v;          /* crash spin */
    struct { float x, z, y, t, r; } smoke[24];
    int    smoke_next;
    float  smoke_timer;
    int    level_seen;
} Rush;

static Rush R;
static int  g_best, g_best_loaded;

/* Chase camera: CAM_BACK behind the car and CAM_HIGH above it, looking down the
   road at a horizon HORIZON of the way down the screen. L.scale is pixels per
   world unit at the car's own depth, which is what steering maps against. */
#define CAM_BACK  4.6f
#define CAM_HIGH  3.7f
#define HORIZON   0.36f

static struct { int w, h; float scale, cx, focal, horizon; } L;

static const CarDef *me(void) { return &CARS[R.car_index]; }

static void compute_layout(int w, int h) {
    L.w = w; L.h = h;
    L.cx = (float)w * 0.5f;
    L.horizon = (float)h * HORIZON;
    /* Framed tight on the car the way the plates are - about two lanes either
       side at its own depth, the rest opening out ahead - but never so wide
       that the car would sit below ~80% of the height on a landscape window. */
    float by_width = (float)w * 0.5f * CAM_BACK / 2.0f;
    float by_height = ((float)h * 0.80f - L.horizon) * CAM_BACK / CAM_HIGH;
    L.focal = by_width < by_height ? by_width : by_height;
    L.scale = L.focal / CAM_BACK;
}

static float cam_x(void) { return R.x * 0.80f; }

/** Perspective projection; returns 0 when the point is behind the near plane. */
static int projz(float x, float z, float y, PA_Vec2 *out) {
    float dz = z - (R.z - CAM_BACK);
    if (dz < 0.35f) return 0;
    out->x = L.cx + (x - cam_x()) * L.focal / dz;
    out->y = L.horizon + (CAM_HIGH - y) * L.focal / dz;
    return 1;
}

static PA_Vec2 project(float x, float z, float y) {
    PA_Vec2 p;
    if (!projz(x, z, y, &p)) { p.x = L.cx; p.y = (float)L.h * 4.0f; }
    return p;
}

static float lane_centre(int i) { return ((float)i - (float)(LANES - 1) * 0.5f) * LANE_W; }
static float difficulty(void)   { return pa_clamp01((float)R.score / 4200.0f); }

/* ---------------------------------------------------------------- spawning */
static void spawn_traffic(void) {
    float d = difficulty();

    /* Never seal the road: a lane is unavailable if it already has something
       near the spawn line, and at least one gap always remains. */
    int free_lanes[LANES], free_count = 0;
    for (int i = 0; i < LANES; i++) {
        int blocked = 0;
        for (int t = 0; t < R.traffic_count; t++) {
            if (R.traffic[t].lane == i &&
                fabsf(R.traffic[t].z - (R.z + VIEW_AHEAD)) < 9.0f) { blocked = 1; break; }
        }
        if (!blocked) free_lanes[free_count++] = i;
    }
    if (free_count <= 1 || R.traffic_count >= MAX_TRAFFIC) return;

    int lane = free_lanes[pa_rng_int(&R.rand, 0, free_count - 1)];
    Car *c = &R.traffic[R.traffic_count++];
    memset(c, 0, sizeof(*c));
    c->lane = lane;
    c->oncoming = lane >= LANES - 2 && pa_rng_chance(&R.rand, 0.50f + d * 0.18f);
    c->big = pa_rng_chance(&R.rand, 0.16f + d * 0.12f);
    c->x = lane_centre(lane) + pa_rng_range(&R.rand, -0.16f, 0.16f);
    c->z = R.z + VIEW_AHEAD + pa_rng_range(&R.rand, 0.0f, 8.0f);
    /* Same-direction traffic is slower than you; oncoming closes fast. */
    c->speed = c->oncoming ? -(pa_rng_range(&R.rand, 16.f, 24.f) + d * 8.0f)
                           : pa_rng_range(&R.rand, 7.f, 15.f) + d * 6.0f;
    c->len = c->big ? pa_rng_range(&R.rand, 3.1f, 3.9f) : pa_rng_range(&R.rand, 1.8f, 2.2f);
    c->wide = c->big ? 1.22f : pa_rng_range(&R.rand, 0.98f, 1.10f);
    c->hue = pa_rng_next(&R.rand);
    c->drift = pa_rng_range(&R.rand, -0.20f, 0.20f);

    /* Coins ride in the gaps, which nudges the player toward the risky line. */
    if (pa_rng_chance(&R.rand, 0.55f)) {
        int cl = free_lanes[pa_rng_int(&R.rand, 0, free_count - 1)];
        int n = pa_rng_int(&R.rand, 3, 6);
        for (int k = 0; k < n && R.coin_count < MAX_COINS; k++) {
            Coin *co = &R.coins[R.coin_count++];
            co->x = lane_centre(cl);
            co->z = R.z + VIEW_AHEAD + 4.0f + (float)k * 1.6f;
            co->spin = k;
        }
    }
}

static void spawn_prop(void) {
    if (R.prop_count >= MAX_PROPS) return;
    float side = pa_rng_chance(&R.rand, 0.5f) ? -1.0f : 1.0f;
    Prop *p = &R.props[R.prop_count++];
    /* Trees line the verge; blocks of buildings stand further back. Height
       above 2.2 is what the renderer reads as a building. */
    int tall = pa_rng_chance(&R.rand, R.biome == 3 ? 0.10f : 0.38f);
    p->x = side * (ROAD_W * 0.5f + (tall ? pa_rng_range(&R.rand, 3.0f, 6.0f) : pa_rng_range(&R.rand, 1.4f, 3.4f)));
    p->z = R.z + VIEW_AHEAD + pa_rng_range(&R.rand, 0.0f, 10.0f);
    p->w = tall ? pa_rng_range(&R.rand, 1.6f, 2.8f) : pa_rng_range(&R.rand, 0.8f, 1.9f);
    p->h = tall ? pa_rng_range(&R.rand, 3.0f, 9.0f) : pa_rng_range(&R.rand, 0.6f, 1.6f);
    p->tint = 0;
    p->lit = 0;
    p->mask = (uint32_t)pa_rng_int(&R.rand, 0, 0xFFFF);
}

/* ------------------------------------------------------------------ frame */
static void banner(const char *t) {
    snprintf(R.banner, sizeof(R.banner), "%s", t);
    R.banner_t = 1.6f;
}

static void rush_start(void) {
    if (!g_best_loaded) { g_best = pa_save_get("chromerush.best", 0); g_best_loaded = 1; }
    if (L.w == 0) compute_layout(540, 960);

    int keep = R.car_index;
    memset(&R, 0, sizeof(R));
    R.car_index = keep;
    pa_rng_seed(&R.rand, 0x1234u ^ ((uint32_t)(g_best * 2654435761u) + 0x9E3779B9u));
    R.speed = 12.0f;
    banner(me()->ability_name);
}

static void rush_stop(void) { }

static void fire_ability(void) {
    const CarDef *car = me();
    R.cooldown = car->cool;
    pa_sfx(car->ability == AB_SIREN ? "horn" : "boom");
    R.ability_t = car->ability == AB_BOOST ? 2.4f : (car->ability == AB_SIREN ? 4.0f : 3.0f);
    banner(car->ability_name);
}

static void crash(void) {
    R.crashed = 1;
    R.crash_t = 0.0f;
    R.yaw_v = (R.vx >= 0.0f ? 1.0f : -1.0f) * 9.0f;
    pa_sfx("boom");
    if (R.score > g_best) {
        g_best = R.score;
        pa_save_set("chromerush.best", g_best);
        pa_save_flush();
    }
}

static void rush_update(float dt, const PA_Input *in) {
    R.time += dt;
    if (R.banner_t > 0.0f) R.banner_t -= dt;

    if (R.crashed) {
        R.crash_t += dt;
        R.yaw += R.yaw_v * dt;
        R.yaw_v *= expf(-1.8f * dt);
        for (int i = 0; i < 24; i++) if (R.smoke[i].t > 0.0f) R.smoke[i].t -= dt;
        R.speed = pa_approach(R.speed, 0.0f, 3.4f, dt);
        R.z += R.speed * dt;
        if (R.crash_t > 2.6f) rush_start();
        return;
    }

    const CarDef *car = me();

    /* Speed climbs toward the car's top with a slow global creep past it, so a
       perfect run still eventually ends. */
    float creep = 1.0f + pa_clamp01((float)R.score / 6000.0f) * 0.30f;
    float target = car->top * creep *
                   ((R.ability_t > 0.0f && car->ability == AB_BOOST) ? 1.45f : 1.0f);
    R.speed = pa_approach(R.speed, target, car->accel * 0.18f, dt);
    R.z += R.speed * dt;
    R.score = (int)R.z;

    /* Steering has weight: grip sets how fast lateral velocity converges. */
    float want;
    if (in->down) {
        /* The screen width spans the road, whatever the zoom: the camera
           follows the car, so pixels at the car's depth are not road units. */
        float dx_pix = in->x - L.cx;
        want = pa_clampf(dx_pix / ((float)L.w * 0.42f), -1.0f, 1.0f) * (ROAD_W * 0.5f + 0.6f);
    } else {
        float k = 0.0f;
        if (in->keys[PA_KEY_LEFT])  k -= 1.0f;
        if (in->keys[PA_KEY_RIGHT]) k += 1.0f;
        want = R.x + k * 4.5f;
    }
    want = pa_clampf(want, -(ROAD_W * 0.5f + 0.45f), ROAD_W * 0.5f + 0.45f);

    float prev = R.x;
    R.x = pa_approach(R.x, want, car->grip, dt);
    R.vx = (R.x - prev) / (dt > 0.0001f ? dt : 0.0001f);
    R.lean = pa_approach(R.lean, pa_clampf(R.vx * 0.05f, -1.0f, 1.0f), 8.0f, dt);

    /* Tyre smoke puffs behind the car, thicker when it swerves. */
    R.smoke_timer -= dt * (1.0f + fabsf(R.vx) * 0.6f);
    if (R.smoke_timer <= 0.0f) {
        R.smoke_timer = 0.07f;
        int k = R.smoke_next++ % 24;
        R.smoke[k].x = R.x + pa_rng_range(&R.rand, -0.5f, 0.5f) * me()->wide;
        R.smoke[k].z = R.z - me()->len * 0.5f;
        R.smoke[k].y = 0.15f;
        R.smoke[k].t = 1.0f;
        R.smoke[k].r = pa_rng_range(&R.rand, 0.22f, 0.34f);
    }
    for (int i = 0; i < 24; i++) {
        if (R.smoke[i].t <= 0.0f) continue;
        R.smoke[i].t -= dt * 1.6f;
        R.smoke[i].y += dt * 0.5f;
        R.smoke[i].r += dt * 0.35f;
    }

    int lvl = R.score / 500 + 1;
    if (lvl != R.level_seen) {
        if (R.level_seen) { banner("LEVEL UP"); pa_sfx("levelup"); }
        R.level_seen = lvl;
    }

    /* Off the tarmac is survivable but slow - a soft wall, not a hard one. */
    if (fabsf(R.x) > ROAD_W * 0.5f - car->wide * 0.5f) R.speed *= expf(-2.2f * dt);

    if (R.cooldown > 0.0f) R.cooldown = R.cooldown > dt ? R.cooldown - dt : 0.0f;
    if (R.ability_t > 0.0f) R.ability_t = R.ability_t > dt ? R.ability_t - dt : 0.0f;
    if ((in->tapped || in->key_pressed[PA_KEY_SPACE]) && R.cooldown <= 0.0f) fire_ability();

    for (int i = R.traffic_count - 1; i >= 0; i--) {
        Car *c = &R.traffic[i];
        c->z += c->speed * dt;
        c->x += c->drift * dt * 0.4f;

        if (c->shoved != 0.0f) {
            c->x += c->shoved * 9.0f * dt;
            c->shoved *= expf(-1.6f * dt);
        }

        /* Siren: nearby traffic slides toward the nearest shoulder. */
        if (R.ability_t > 0.0f && car->ability == AB_SIREN && fabsf(c->z - R.z) < 26.0f) {
            float side = c->x >= 0.0f ? 1.0f : -1.0f;
            c->x = pa_approach(c->x, side * (ROAD_W * 0.5f + 0.4f), 3.2f, dt);
        }

        if (c->z < R.z - VIEW_BEHIND - 6.0f || c->z > R.z + VIEW_AHEAD + 30.0f) {
            R.traffic[i] = R.traffic[--R.traffic_count];
            continue;
        }

        float dz = fabsf(c->z - R.z);
        float dx = fabsf(c->x - R.x);
        float hit_z = (c->len + car->len) * 0.5f;
        float hit_x = (c->wide + car->wide) * 0.5f;

        if (dz < hit_z && dx < hit_x) {
            int phased = R.ability_t > 0.0f && car->ability == AB_PHASE;
            int rammed = R.ability_t > 0.0f && car->ability == AB_RAM;
            if (phased) {
                /* straight through */
            } else if ((rammed && !c->big) || (car->bulk >= 2 && !c->big)) {
                c->shoved = c->x >= R.x ? 1.0f : -1.0f;
                R.speed *= 0.92f;
                pa_sfx("thud");
            } else {
                crash();
                return;
            }
        } else if (!c->counted && dz < hit_z + 0.6f && dx < hit_x + 0.75f) {
            /* Near miss: close enough to scare, far enough to live. */
            c->counted = 1;
            R.near_miss++;
            R.near_timer = 1.8f;
            R.picked += 2;
            pa_sfx("pop");
            banner("NEAR MISS");
        }
    }

    for (int i = R.coin_count - 1; i >= 0; i--) {
        Coin *co = &R.coins[i];
        if (co->z < R.z - 6.0f) { R.coins[i] = R.coins[--R.coin_count]; continue; }
        if (fabsf(co->z - R.z) < 1.1f && fabsf(co->x - R.x) < 0.9f) {
            R.coins[i] = R.coins[--R.coin_count];
            R.picked++;
            pa_sfx("coin");
        }
    }

    for (int i = R.prop_count - 1; i >= 0; i--) {
        if (R.props[i].z < R.z - VIEW_BEHIND - 4.0f) R.props[i] = R.props[--R.prop_count];
    }

    if (R.near_timer > 0.0f) {
        R.near_timer -= dt;
        if (R.near_timer <= 0.0f) R.near_miss = 0;
    }

    /* Spawn pacing tightens with difficulty but has a hard floor - otherwise a
       late run is a solid wall rather than a hard one. */
    R.next_spawn -= dt;
    if (R.next_spawn <= 0.0f) {
        spawn_traffic();
        R.next_spawn = pa_lerpf(0.30f, 0.11f, difficulty()) * pa_rng_range(&R.rand, 0.75f, 1.3f);
    }
    R.next_prop -= dt;
    if (R.next_prop <= 0.0f) {
        spawn_prop();
        R.next_prop = pa_rng_range(&R.rand, 0.10f, 0.34f);
    }

    /* Biome every 900 units, cross-faded so the change is felt not jarring. */
    int want_biome = ((int)(R.z / 900.0f)) % BIOME_COUNT;
    if (want_biome != R.biome) {
        R.biome_mix += dt * 0.6f;
        if (R.biome_mix >= 1.0f) {
            R.biome = want_biome;
            R.biome_mix = 0.0f;
            banner(BIOMES[R.biome].name);
        }
    } else {
        R.biome_mix = R.biome_mix > dt * 0.6f ? R.biome_mix - dt * 0.6f : 0.0f;
    }
}

/* ---------------------------------------------------------------- drawing */
/*
 * A box under the chase camera, optionally turned about its vertical axis.
 * A box is convex, so the faces toward the camera never overlap one another
 * and need no sorting among themselves: a side is drawn when the camera is on
 * its outer side, the top when the box is below the camera.
 */
static void pbox(PA_Canvas *c, float x, float z, float y, float w, float len, float h,
                 float yaw, PA_Color col, PA_Color top_col) {
    float ca = cosf(yaw), sa = sinf(yaw);
    static const float LX[4] = { -1, 1, 1, -1 }, LZ[4] = { -1, -1, 1, 1 };
    static const float NX[4] = { 0, 1, 0, -1 }, NZ[4] = { -1, 0, 1, 0 };
    float wx[4], wz[4];
    PA_Vec2 b[4], t[4];
    for (int k = 0; k < 4; k++) {
        float lx = LX[k] * w * 0.5f, lz = LZ[k] * len * 0.5f;
        wx[k] = x + lx * ca + lz * sa;
        wz[k] = z - lx * sa + lz * ca;
        if (!projz(wx[k], wz[k], y, &b[k]) || !projz(wx[k], wz[k], y + h, &t[k])) return;
    }
    float camx = cam_x(), camz = R.z - CAM_BACK;
    for (int k = 0; k < 4; k++) {
        int n = (k + 1) & 3;
        float nx = NX[k] * ca + NZ[k] * sa, nz = -NX[k] * sa + NZ[k] * ca;
        float mx = (wx[k] + wx[n]) * 0.5f, mz = (wz[k] + wz[n]) * 0.5f;
        if (nx * (camx - mx) + nz * (camz - mz) <= 0.0f) continue;
        /* Faces toward the camera (rear) mid tone, sides darker. */
        float shade = fabsf(nz) > 0.7f ? -0.12f : -0.30f;
        PA_Vec2 q[4] = { b[k], b[n], t[n], t[k] };
        pa_fill_poly(c, q, 4, pa_shade(col, shade));
    }
    if (y + h < CAM_HIGH) {
        PA_Vec2 q[4] = { t[0], t[1], t[2], t[3] };
        pa_fill_poly(c, q, 4, top_col);
    }
}

/** Quad on the rear face of a car-aligned box, in face-local u (0..1 across)
    and v (height). */
static void rear_quad(PA_Canvas *c, float x, float z, float w, float u0, float u1,
                      float y0, float y1, PA_Color col) {
    PA_Vec2 a, b, d, e;
    if (!projz(x - w * 0.5f + w * u0, z, y0, &a) || !projz(x - w * 0.5f + w * u1, z, y0, &b) ||
        !projz(x - w * 0.5f + w * u1, z, y1, &d) || !projz(x - w * 0.5f + w * u0, z, y1, &e)) return;
    PA_Vec2 q[4] = { a, b, d, e };
    pa_fill_poly(c, q, 4, col);
}

static void draw_vehicle(PA_Canvas *c, float x, float z, float len, float wide, float yaw,
                         PA_Color body, int big, int oncoming, int learner, float alpha) {
    PA_Vec2 g;
    if (!projz(x, z, 0.0f, &g)) return;
    float px = L.focal / (z - (R.z - CAM_BACK));
    pa_shadow(c, g.x, g.y + px * 0.1f, wide * px * 0.72f, len * px * 0.30f, 0.30f * alpha);

    body = pa_alpha(body, alpha);
    /* Parts sit at offsets along the car's own axis, turned with it. */
    float sa = sinf(yaw), ca = cosf(yaw);
    #define AX(dz) (x + (dz) * sa)
    #define AZ(dz) (z + (dz) * ca)
    PA_Color dark = pa_alpha(pa_hex(0x22242E), alpha);
    PA_Color glass = pa_alpha(pa_hex(0x9FD2FF), alpha);

    /* Wheels stick out past the body, or the body's rear face hides them. */
    for (int q = -1; q <= 1; q += 2)
        pbox(c, AX((float)q * len * 0.30f), AZ((float)q * len * 0.30f), 0.0f, wide * 1.10f, len * 0.20f,
             0.34f, yaw, dark, dark);

    if (big) {
        /* Box truck: cab up front, tall white box behind. */
        pbox(c, AX(len * 0.36f), AZ(len * 0.36f), 0.18f, wide * 0.96f, len * 0.26f, 0.95f, yaw, body,
             pa_shade(body, 0.14f));
        pbox(c, AX(-len * 0.12f), AZ(-len * 0.12f), 0.18f, wide, len * 0.74f, 1.35f, yaw,
             pa_alpha(pa_hex(0xF1F2F7), alpha), pa_alpha(pa_hex(0xFFFFFF), alpha));
    } else {
        pbox(c, x, z, 0.16f, wide, len, 0.50f, yaw, body, pa_shade(body, 0.12f));
        /* Glasshouse: big windows, the plates' cars are mostly glass above
           the waist. */
        pbox(c, AX(-len * 0.04f), AZ(-len * 0.04f), 0.66f, wide * 0.84f, len * 0.56f, 0.40f, yaw, glass,
             pa_shade(body, 0.18f));
        if (yaw == 0.0f && !oncoming) {
            /* Two diagonal glare streaks across the rear window, the plates'
               cars' most recognisable detail. */
            float gz = z - len * 0.04f - len * 0.28f, gw = wide * 0.84f;
            for (int k = 0; k < 2; k++) {
                float u = 0.22f + 0.30f * (float)k, du = k ? 0.08f : 0.14f;
                PA_Vec2 q[4];
                if (projz(x - gw * 0.5f + gw * u, gz, 0.70f, &q[0]) &&
                    projz(x - gw * 0.5f + gw * (u + du), gz, 0.70f, &q[1]) &&
                    projz(x - gw * 0.5f + gw * (u + du + 0.16f), gz, 1.02f, &q[2]) &&
                    projz(x - gw * 0.5f + gw * (u + 0.16f), gz, 1.02f, &q[3]))
                    pa_fill_poly(c, q, 4, PA_RGBA(255, 255, 255, (int)(170.0f * alpha)));
            }
        }
    }

    if (yaw == 0.0f) {
        /* Rear details: tail lights and a bumper, or headlights on oncoming. */
        float rz = oncoming ? z + len * 0.5f : z - len * 0.5f;
        PA_Color lamp = oncoming ? pa_alpha(pa_hex(0xFFF6D0), alpha) : pa_alpha(pa_hex(0xFF3A48), alpha);
        rear_quad(c, x, rz, wide, 0.06f, 0.26f, 0.44f, 0.58f, lamp);
        rear_quad(c, x, rz, wide, 0.74f, 0.94f, 0.44f, 0.58f, lamp);
        rear_quad(c, x, rz, wide, 0.10f, 0.90f, 0.20f, 0.30f, pa_alpha(pa_hex(0x2A2C38), alpha * 0.8f));
    }

    if (learner) {
        /* The learner plate on the roof, the reference car's signature. */
        float ty = big ? 1.53f : 1.06f;
        pbox(c, x, z, ty, wide * 0.40f, len * 0.10f, 0.38f, yaw, pa_alpha(pa_hex(0xF4F4FA), alpha),
             pa_alpha(pa_hex(0xFFFFFF), alpha));
        if (yaw == 0.0f) {
            float fz = z - len * 0.05f;
            rear_quad(c, x, fz, wide * 0.40f, 0.30f, 0.44f, ty + 0.08f, ty + 0.32f, pa_alpha(pa_hex(0xE8283C), alpha));
            rear_quad(c, x, fz, wide * 0.40f, 0.30f, 0.70f, ty + 0.08f, ty + 0.14f, pa_alpha(pa_hex(0xE8283C), alpha));
        }
    }
    #undef AX
    #undef AZ
}

static void draw_sky(PA_Canvas *c, const Biome *b, const Biome *n, float mix) {
    PA_Color top = pa_mix(pa_hex(b->sky_top), pa_hex(n->sky_top), mix);
    PA_Color mid = pa_mix(pa_hex(b->sky_mid), pa_hex(n->sky_mid), mix);
    PA_Color low = pa_mix(pa_hex(b->sky_low), pa_hex(n->sky_low), mix);
    PA_Paint sky = pa_linear(0, 0, 0, L.horizon);
    pa_stop(&sky, 0.0f, top);
    pa_stop(&sky, 0.55f, mid);
    pa_stop(&sky, 1.0f, low);
    pa_fill_rect_paint(c, 0, 0, (float)c->w, L.horizon + 2.0f, &sky);

    const Biome *d = mix < 0.5f ? b : n;
    float drift = -cam_x() * 2.0f;
    if (d->night) {
        PA_Rng r;
        pa_rng_seed(&r, 99u);
        for (int i = 0; i < 70; i++) {
            float sx = pa_rng_range(&r, 0.0f, (float)c->w), sy = pa_rng_range(&r, 0.0f, L.horizon * 0.85f);
            float tw = 0.5f + 0.5f * sinf(R.time * 3.0f + (float)i);
            pa_fill_circle(c, sx, sy, 1.0f + tw * 0.8f, PA_RGBA(255, 255, 255, (int)(120 + tw * 120)));
        }
        float mx = (float)c->w * 0.52f + drift * 0.2f, my = L.horizon * 0.34f, mr = (float)c->w * 0.09f;
        PA_Paint glow = pa_radial(mx, my, mr, mr * 2.6f);
        pa_stop(&glow, 0.0f, PA_RGBA(255, 255, 255, 90));
        pa_stop(&glow, 1.0f, PA_RGBA(255, 255, 255, 0));
        pa_fill_ellipse_paint(c, mx, my, mr * 2.6f, mr * 2.6f, &glow);
        pa_fill_circle(c, mx, my, mr, pa_hex(0xF4F2FF));
        pa_fill_circle(c, mx - mr * 0.3f, my - mr * 0.1f, mr * 0.22f, pa_hex(0xDAD6EE));
        pa_fill_circle(c, mx + mr * 0.35f, my + mr * 0.3f, mr * 0.15f, pa_hex(0xDAD6EE));
    } else {
        if (d->sun) {
            float sx = (float)c->w * 0.5f + drift * 0.2f, sy = L.horizon * 0.80f, sr = (float)c->w * 0.16f;
            pa_fill_circle(c, sx, sy, sr * 1.35f, PA_RGBA(255, 220, 140, 80));
            pa_fill_circle(c, sx, sy, sr, pa_hex(0xFFE08A));
        }
        /* Cumulus banks: overlapping discs with a flat base. */
        for (int k = 0; k < 4; k++) {
            float cx = pa_wrapf((float)k * 0.31f * (float)c->w + drift * 0.3f + R.time * 6.0f,
                                (float)c->w * 1.4f) - (float)c->w * 0.2f;
            float cy = L.horizon * (0.22f + 0.16f * (float)(k & 1));
            float s = (float)c->w * (0.08f + 0.02f * (float)(k % 3));
            PA_Color cl = PA_RGBA(255, 255, 255, d->sun ? 170 : 235);
            pa_fill_circle(c, cx, cy, s, cl);
            pa_fill_circle(c, cx + s * 0.9f, cy + s * 0.2f, s * 0.8f, cl);
            pa_fill_circle(c, cx - s * 0.9f, cy + s * 0.25f, s * 0.7f, cl);
            pa_fill_rect(c, cx - s * 1.6f, cy + s * 0.25f, s * 3.2f, s * 0.6f, cl);
        }
    }

    /* Skyline strip on the horizon, a few shades from the sky. */
    PA_Color sk = pa_mix(pa_hex(b->skyline), pa_hex(n->skyline), mix);
    PA_Rng r;
    pa_rng_seed(&r, 7u);
    float x = -40.0f + pa_wrapf(drift * 0.5f, 40.0f);
    while (x < (float)c->w + 40.0f) {
        float bw = pa_rng_range(&r, 18.0f, 44.0f);
        float bh = L.horizon * pa_rng_range(&r, 0.08f, 0.30f);
        float tone = pa_rng_range(&r, -0.10f, 0.10f);
        pa_fill_rect(c, x, L.horizon - bh, bw - 2.0f, bh + 2.0f, pa_shade(sk, tone));
        if (d->night) {
            for (float wy = L.horizon - bh + 5.0f; wy < L.horizon - 4.0f; wy += 7.0f)
                for (float wx = x + 4.0f; wx < x + bw - 6.0f; wx += 6.0f)
                    if (pa_rng_chance(&r, 0.45f)) pa_fill_rect(c, wx, wy, 2.5f, 3.0f, pa_hex(0xFFD86A));
        } else {
            pa_fill_rect(c, x, L.horizon - bh, bw * 0.28f, bh + 2.0f, pa_shade(sk, tone + 0.12f));
        }
        x += bw;
    }
}

typedef struct { float z; int kind, idx; } Drawable;

static void rush_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) compute_layout(c->w, c->h);

    const Biome *a = &BIOMES[R.biome];
    const Biome *b = &BIOMES[(R.biome + 1) % BIOME_COUNT];
    float mix = R.biome_mix;
    PA_Color ground_c = pa_mix(pa_hex(a->ground), pa_hex(b->ground), mix);
    PA_Color road_c   = pa_mix(pa_hex(a->road), pa_hex(b->road), mix);
    PA_Color line_c   = pa_mix(pa_hex(a->line), pa_hex(b->line), mix);
    PA_Color rail_c   = pa_mix(pa_hex(a->rail), pa_hex(b->rail), mix);

    draw_sky(c, a, b, mix);
    PA_Paint grd = pa_linear(0, L.horizon, 0, (float)c->h);
    pa_stop(&grd, 0.0f, pa_shade(ground_c, -0.10f));
    pa_stop(&grd, 1.0f, pa_shade(ground_c, 0.08f));
    pa_fill_rect_paint(c, 0, L.horizon, (float)c->w, (float)c->h - L.horizon, &grd);

    /* Road: one trapezoid, straight to the vanishing point. */
    float z_near = R.z - CAM_BACK + 0.4f, z_far = R.z + 400.0f;
    float hw = ROAD_W * 0.5f;
    {
        PA_Vec2 q[4] = { project(-hw - 0.5f, z_near, 0), project(hw + 0.5f, z_near, 0),
                         project(hw + 0.5f, z_far, 0), project(-hw - 0.5f, z_far, 0) };
        pa_fill_poly(c, q, 4, pa_shade(road_c, -0.12f));
        PA_Vec2 r2[4] = { project(-hw, z_near, 0), project(hw, z_near, 0),
                          project(hw, z_far, 0), project(-hw, z_far, 0) };
        PA_Paint rp = pa_linear(0, L.horizon, 0, (float)c->h);
        pa_stop(&rp, 0.0f, pa_shade(road_c, 0.06f));
        pa_stop(&rp, 1.0f, road_c);
        pa_fill_poly_paint(c, r2, 4, &rp);
    }
    /* Solid edge lines and lane dashes, as world-space quads so they scroll. */
    for (int side = -1; side <= 1; side += 2) {
        float ex = (float)side * (hw - 0.25f);
        PA_Vec2 q[4] = { project(ex - 0.10f, z_near, 0), project(ex + 0.10f, z_near, 0),
                         project(ex + 0.10f, z_far, 0), project(ex - 0.10f, z_far, 0) };
        pa_fill_poly(c, q, 4, line_c);
    }
    float first = floorf((R.z - CAM_BACK) / 5.0f) * 5.0f;
    for (float d = first; d < R.z + VIEW_AHEAD + 30.0f; d += 5.0f) {
        if (d + 2.4f < z_near) continue;
        for (int l = 1; l < LANES; l++) {
            float lx = lane_centre(l) - LANE_W * 0.5f;
            PA_Vec2 q0, q1, q2, q3;
            float d0 = d < z_near ? z_near : d;
            if (!projz(lx - 0.09f, d0, 0, &q0) || !projz(lx + 0.09f, d0, 0, &q1) ||
                !projz(lx + 0.09f, d + 2.4f, 0, &q2) || !projz(lx - 0.09f, d + 2.4f, 0, &q3)) continue;
            PA_Vec2 q[4] = { q0, q1, q2, q3 };
            pa_fill_poly(c, q, 4, pa_alpha(line_c, 0.9f));
        }
    }

    /* Everything standing on the road, far to near. */
    static Drawable list[MAX_TRAFFIC + MAX_PROPS + MAX_COINS + 64];
    int n = 0;
    for (int i = 0; i < R.traffic_count; i++) { list[n].z = R.traffic[i].z; list[n].kind = 0; list[n].idx = i; n++; }
    for (int i = 0; i < R.prop_count; i++)    { list[n].z = R.props[i].z;   list[n].kind = 1; list[n].idx = i; n++; }
    for (int i = 0; i < R.coin_count; i++)    { list[n].z = R.coins[i].z;   list[n].kind = 2; list[n].idx = i; n++; }
    /* Guard rail posts every four units either side. */
    float pfirst = floorf((R.z - CAM_BACK) / 4.0f) * 4.0f;
    for (float d = pfirst + 8.0f; d < R.z + VIEW_AHEAD && n < (int)(sizeof(list) / sizeof(list[0])) - 2; d += 4.0f) {
        list[n].z = d; list[n].kind = 3; list[n].idx = -1; n++;
        list[n].z = d; list[n].kind = 3; list[n].idx = 1; n++;
    }
    list[n].z = R.z; list[n].kind = 4; list[n].idx = 0; n++;
    for (int i = 1; i < n; i++) {
        Drawable key = list[i];
        int j = i - 1;
        while (j >= 0 && list[j].z < key.z) { list[j + 1] = list[j]; j--; }
        list[j + 1] = key;
    }

    /* The rail itself: a continuous white band just off each road edge, drawn
       before the posts and cars so they stand in front of it. */
    for (int side = -1; side <= 1; side += 2) {
        float rx = (float)side * (hw + 0.45f);
        PA_Vec2 q[4] = { project(rx, z_near, 0.55f), project(rx, z_far, 0.55f),
                         project(rx, z_far, 0.85f), project(rx, z_near, 0.85f) };
        pa_fill_poly(c, q, 4, pa_shade(rail_c, -0.08f));
        PA_Vec2 q2[4] = { project(rx, z_near, 0.25f), project(rx, z_far, 0.25f),
                          project(rx, z_far, 0.40f), project(rx, z_near, 0.40f) };
        pa_fill_poly(c, q2, 4, pa_shade(rail_c, -0.18f));
    }

    const CarDef *car = me();
    int phasing = R.ability_t > 0.0f && car->ability == AB_PHASE;
    for (int i = 0; i < n; i++) {
        const Drawable *d = &list[i];
        if (d->kind == 0) {
            Car *cc = &R.traffic[d->idx];
            /* The plates' traffic palette: saturated primaries and white. */
            static const uint32_t PAL[] = { 0x3E7FF0, 0xF6C340, 0x2FC98E, 0xF1F2F7, 0x9B5BE8, 0xFF7A2E, 0xE8455F };
            PA_Color body = pa_hex(PAL[(int)(cc->hue * 7.0f) % 7]);
            draw_vehicle(c, cc->x, cc->z, cc->len, cc->wide, 0.0f, body, cc->big, cc->oncoming, 0, 1.0f);
        } else if (d->kind == 1) {
            Prop *p = &R.props[d->idx];
            const Biome *bi = &BIOMES[R.biome];
            if (p->h > 2.2f) {
                /* Roadside building: box plus window rows on its rear face. */
                PA_Color col = pa_shade(pa_hex(bi->skyline), 0.25f + (float)(p->mask & 7) * 0.03f);
                pbox(c, p->x + (p->x > 0 ? p->w * 0.8f : -p->w * 0.8f), p->z, 0.0f, p->w * 1.4f, p->w * 1.4f,
                     p->h, 0.0f, col, pa_shade(col, 0.15f));
                float bx = p->x + (p->x > 0 ? p->w * 0.8f : -p->w * 0.8f);
                for (float wy = 0.6f; wy < p->h - 0.4f; wy += 0.8f)
                    rear_quad(c, bx, p->z - p->w * 0.7f, p->w * 1.4f, 0.14f, 0.86f, wy, wy + 0.4f,
                              bi->night ? pa_hex(0xFFD86A) : pa_shade(pa_hex(0xBFE0FF), -0.05f));
            } else {
                /* Tree: trunk and a round crown. */
                PA_Vec2 base, crown;
                float tx = p->x + (p->x > 0 ? 0.6f : -0.6f);
                if (!projz(tx, p->z, 0.0f, &base) || !projz(tx, p->z, 1.4f + p->h, &crown)) continue;
                float px = L.focal / (p->z - (R.z - CAM_BACK));
                pa_shadow(c, base.x, base.y, px * 0.9f, px * 0.3f, 0.25f);
                pbox(c, tx, p->z, 0.0f, 0.24f, 0.24f, 1.4f, 0.0f, pa_hex(0x8A5A34), pa_hex(0x8A5A34));
                PA_Color leaf = bi->night ? pa_hex(0x2E7A5A) : pa_hex(0x3FAE4A);
                float rr = px * (0.7f + p->w * 0.25f);
                pa_fill_circle(c, crown.x, crown.y + rr * 0.1f, rr, pa_shade(leaf, -0.25f));
                pa_fill_circle(c, crown.x, crown.y, rr * 0.92f, leaf);
                pa_fill_circle(c, crown.x - rr * 0.3f, crown.y - rr * 0.3f, rr * 0.45f, pa_shade(leaf, 0.2f));
            }
        } else if (d->kind == 2) {
            Coin *co = &R.coins[d->idx];
            PA_Vec2 cp, gp;
            if (!projz(co->x, co->z, 0.55f, &cp) || !projz(co->x, co->z, 0.0f, &gp)) continue;
            float px = L.focal / (co->z - (R.z - CAM_BACK));
            float rr = px * 0.34f;
            float squash = fabsf(cosf(R.time * 3.0f + (float)co->spin));
            pa_shadow(c, gp.x, gp.y, rr * 0.8f, rr * 0.30f, 0.22f);
            pa_fill_ellipse(c, cp.x, cp.y + rr * 0.08f, (rr * squash > 1.5f ? rr * squash : 1.5f), rr,
                            pa_hex(0xC98A12));
            pa_fill_ellipse(c, cp.x, cp.y, (rr * squash > 1.5f ? rr * squash : 1.5f) * 0.9f, rr * 0.9f,
                            pa_hex(0xFFC93C));
        } else if (d->kind == 3) {
            float rx = (float)d->idx * (hw + 0.45f);
            pbox(c, rx, d->z, 0.0f, 0.14f, 0.14f, 0.9f, 0.0f, pa_shade(rail_c, -0.2f), rail_c);
        } else {
            /* Tyre smoke first, then the player's car on top of it. */
            for (int k = 0; k < 24; k++) {
                if (R.smoke[k].t <= 0.0f) continue;
                PA_Vec2 sp;
                if (!projz(R.smoke[k].x, R.smoke[k].z, R.smoke[k].y, &sp)) continue;
                float px = L.focal / (R.smoke[k].z - (R.z - CAM_BACK));
                pa_fill_circle(c, sp.x, sp.y, R.smoke[k].r * px,
                               PA_RGBA(255, 255, 255, (int)(pa_clamp01(R.smoke[k].t) * 200.0f)));
            }
            if (!R.crashed || R.crash_t < 2.0f) {
                PA_Color body = pa_hex(car->body);
                draw_vehicle(c, R.x, R.z, car->len, car->wide, R.yaw, body, car->bulk >= 2, 0, 1,
                             phasing ? 0.5f : 1.0f);
            }
        }
    }

    if (R.ability_t > 0.0f && car->ability == AB_BOOST) {
        PA_Vec2 fp = project(R.x, R.z - car->len * 0.55f, 0.35f);
        float fw = car->wide * L.scale * 0.30f;
        PA_Vec2 flame[3] = {
            { fp.x - fw, fp.y }, { fp.x + fw, fp.y },
            { fp.x, fp.y + L.scale * (1.2f + fabsf(sinf(R.time * 30.0f)) * 0.5f) }
        };
        pa_fill_poly(c, flame, 3, PA_RGBA(255, 190, 60, 220));
    }
    if (R.ability_t > 0.0f && car->ability == AB_SIREN) {
        PA_Vec2 sp = project(R.x, R.z, 1.2f);
        int on = ((int)(R.time * 9.0f) % 2) == 0;
        pa_fill_circle(c, sp.x, sp.y, L.scale * 0.35f, on ? pa_hex(0x4B8BFF) : pa_hex(0xFF4B5C));
    }

    /* Speed lines at the screen edges once the pace is up. */
    float pace = pa_clamp01((R.speed - 18.0f) / 26.0f);
    if (pace > 0.02f) {
        for (int k = 0; k < 12; k++) {
            float t = pa_wrapf(R.time * (1.6f + pace * 2.4f) + (float)k * 0.29f, 1.0f);
            float side = (k % 2) ? 1.0f : -1.0f;
            float ang = side * (0.35f + (float)(k % 5) * 0.08f);
            float r0 = (float)c->h * (0.25f + t * 0.8f);
            float sx = L.cx + sinf(ang) * r0, sy = L.horizon + cosf(ang) * r0 * 0.6f;
            pa_line(c, sx, sy, sx + sinf(ang) * 40.0f * (0.5f + pace), sy + cosf(ang) * 24.0f * (0.5f + pace),
                    2.0f, PA_RGBA(255, 255, 255, (int)(pace * 110.0f * t)));
        }
    }

    /* HUD: level progress bar with the car riding it, as the plates show. */
    char buf[64];
    int lvl = R.score / 500 + 1;
    float prog = (float)(R.score % 500) / 500.0f;
    float bw = (float)c->w * 0.50f < 300.0f ? (float)c->w * 0.50f : 300.0f;
    float bx = L.cx - bw * 0.5f, by = 38.0f;
    if (bx < 124.0f) { bw -= (124.0f - bx) * 2.0f; bx = 124.0f; }
    pa_round_rect(c, bx, by - 4.0f, bw, 8.0f, 4.0f, PA_RGBA(20, 22, 50, 110));
    pa_round_rect(c, bx, by - 4.0f, bw * prog, 8.0f, 4.0f, PA_RGB(255, 255, 255));
    snprintf(buf, sizeof(buf), "%d", lvl);
    pa_text_bold(c, buf, bx - 14.0f, by - 8.0f, 14.0f, PA_RGB(255, 255, 255), pa_hex(0x1C2040),
                 PA_ALIGN_RIGHT, 1.0f, 1.1f);
    snprintf(buf, sizeof(buf), "%d", lvl + 1);
    pa_text_bold(c, buf, bx + bw + 14.0f, by - 8.0f, 14.0f, PA_RGB(255, 255, 255), pa_hex(0x1C2040),
                 PA_ALIGN_LEFT, 1.0f, 1.1f);
    {
        float ix = bx + bw * prog;
        pa_round_rect(c, ix - 9.0f, by - 7.0f, 18.0f, 10.0f, 3.0f, pa_hex(0xE8283C));
        pa_round_rect(c, ix - 6.0f, by - 11.0f, 12.0f, 6.0f, 2.0f, pa_hex(0x9FD2FF));
    }

    snprintf(buf, sizeof(buf), "%d M", R.score);
    pa_text_bold(c, buf, L.cx, 58.0f, 26.0f, PA_RGB(255, 255, 255), pa_hex(0x1C2040),
                 PA_ALIGN_CENTER, 2.0f, 1.6f);

    /* Coins, right. */
    pa_fill_circle(c, (float)c->w - 70.0f, 32.0f, 10.0f, pa_hex(0xC98A12));
    pa_fill_circle(c, (float)c->w - 70.0f, 31.0f, 8.5f, pa_hex(0xFFC93C));
    snprintf(buf, sizeof(buf), "%d", R.picked);
    pa_text_bold(c, buf, (float)c->w - 54.0f, 24.0f, 16.0f, PA_RGB(255, 255, 255), pa_hex(0x1C2040),
                 PA_ALIGN_LEFT, 1.5f, 1.2f);

    /* Ability pill at the bottom. */
    {
        int ready = R.cooldown <= 0.0f;
        snprintf(buf, sizeof(buf), ready ? "TAP  %s" : "%s", car->ability_name);
        float tw = pa_text_width(buf, 12.0f, 2.0f) + 34.0f;
        float px = L.cx - tw * 0.5f, py = (float)c->h - 58.0f;
        pa_round_rect(c, px, py, tw, 30.0f, 15.0f, PA_RGBA(20, 22, 50, 150));
        if (!ready) {
            float k = 1.0f - R.cooldown / car->cool;
            pa_round_rect(c, px, py, tw * pa_clamp01(k), 30.0f, 15.0f, PA_RGBA(255, 255, 255, 60));
        }
        pa_text(c, buf, L.cx, py + 9.0f, 12.0f, ready ? pa_hex(0xFFD24A) : PA_RGBA(255, 255, 255, 170),
                PA_ALIGN_CENTER, 2.0f);
    }

    if (R.banner_t > 0.25f && !R.crashed) {
        float pop = 1.0f + (1.0f - pa_clamp01((1.6f - R.banner_t) * 6.0f)) * 0.3f;
        pa_text_bold(c, R.banner, L.cx, (float)c->h * 0.22f, 26.0f * pop, pa_hex(0xFFD24A),
                     pa_hex(0x1C2040), PA_ALIGN_CENTER, 3.0f, 2.0f);
    }

    if (R.crashed) {
        float a2 = pa_clamp01(R.crash_t * 2.2f);
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(28, 20, 60, (int)(a2 * 120.0f)));
        float pop = 0.6f + 0.4f * pa_clamp01(R.crash_t * 5.0f);
        int record = R.score >= g_best && R.score > 0;
        pa_text_bold(c, record ? "NEW RECORD!" : "KO!", L.cx, (float)c->h * 0.36f, (record ? 34.0f : 64.0f) * pop,
                     pa_hex(0xFFD24A), pa_hex(0x1C2040), PA_ALIGN_CENTER, 3.0f, 2.4f);
        snprintf(buf, sizeof(buf), "%d M  -  BEST %d M", R.score, g_best);
        pa_text_bold(c, buf, L.cx, (float)c->h * 0.36f + 84.0f, 15.0f, PA_RGB(255, 255, 255),
                     pa_hex(0x1C2040), PA_ALIGN_CENTER, 2.0f, 1.1f);
    }
}

static void rush_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    float hz = y + h * 0.36f;
    PA_Paint sky = pa_linear(0, y, 0, hz);
    pa_stop(&sky, 0.0f, pa_hex(0x1F7BEA));
    pa_stop(&sky, 1.0f, pa_hex(0xCDEBFF));
    pa_fill_rect_paint(c, x, y, w, hz - y, &sky);
    pa_fill_rect(c, x, hz, w, y + h - hz, pa_hex(0x6CCB4E));
    float cx = x + w * 0.5f;
    PA_Vec2 road[4] = { { cx - w * 0.04f, hz }, { cx + w * 0.04f, hz }, { x + w * 0.95f, y + h }, { x + w * 0.05f, y + h } };
    pa_fill_poly(c, road, 4, pa_hex(0xC7C4EC));
    for (int k = 0; k < 5; k++) {
        float tt = pa_wrapf(t * 0.8f + (float)k * 0.2f, 1.0f);
        float yy = hz + (y + h - hz) * tt * tt;
        pa_fill_rect(c, cx - 1.0f - tt * 1.5f, yy, 2.0f + tt * 3.0f, 3.0f + tt * 10.0f, PA_RGB(255, 255, 255));
    }
    float cw = w * 0.22f, ch = h * 0.18f, cy = y + h * 0.70f;
    pa_shadow(c, cx, cy + ch * 0.9f, cw * 0.6f, ch * 0.2f, 0.3f);
    pa_round_rect(c, cx - cw * 0.5f, cy, cw, ch * 0.7f, 3.0f, pa_hex(0xB81E30));
    pa_round_rect(c, cx - cw * 0.5f, cy - ch * 0.1f, cw, ch * 0.45f, 3.0f, pa_hex(0xE8283C));
    pa_round_rect(c, cx - cw * 0.38f, cy - ch * 0.42f, cw * 0.76f, ch * 0.36f, 3.0f, pa_hex(0x9FD2FF));
    pa_fill_rect(c, cx - cw * 0.12f, cy - ch * 0.62f, cw * 0.24f, ch * 0.2f, PA_RGB(255, 255, 255));
}

const PA_Game PA_GAME_CHROMERUSH = {
    "chromerush", "Chrome Rush", "Endless Driver",
    "Endless highway. Thread the traffic, bank near misses, and unlock five very different rides.",
    PA_RGB(232, 40, 60),
    rush_start, rush_stop, rush_update, rush_render, rush_thumb
};
