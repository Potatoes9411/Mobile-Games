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
    { "CITY HATCH",  30.f, 7.0f,  9.0f, 1.9f, 1.05f, 0, 0x3D8BFF, 0x9FCBFF, AB_BOOST, "NITRO",   9.f },
    { "WEDGE GT",    38.f, 8.6f, 11.0f, 2.0f, 1.02f, 0, 0xE2455F, 0xFF9AA8, AB_BOOST, "OVERDRIVE", 7.f },
    { "INTERCEPTOR", 35.f, 8.0f, 12.0f, 2.1f, 1.10f, 1, 0x1E2740, 0xEDEDF2, AB_SIREN, "SIREN",   11.f },
    { "HAUL RIG",    31.f, 5.4f,  6.6f, 3.4f, 1.24f, 2, 0xE9A03F, 0xFFD79A, AB_RAM,   "BULL BAR", 10.f },
    { "SKIFF",       41.f, 9.4f, 14.0f, 2.0f, 0.98f, 0, 0x2FD6A4, 0xC4FFEA, AB_PHASE, "PHASE",   12.f }
};
#define CAR_COUNT ((int)(sizeof(CARS) / sizeof(CARS[0])))

typedef struct {
    const char *name;
    uint32_t sky, ground, road, line, prop;
    int prop_tall, lit;
} Biome;

/* Each biome has to separate three planes at a glance: verge, tarmac and the
   stuff standing beside it. */
static const Biome BIOMES[] = {
    { "DOWNTOWN",     0x1B2545, 0x223054, 0x474E60, 0xF7ECC6, 0x33406B, 1, 1 },
    { "SUNSET FLATS", 0xE8823F, 0xC4763F, 0x6A5B4E, 0xFFF0C8, 0x8F5A36, 0, 0 },
    { "PINE PASS",    0x3D7E8C, 0x2C6A4C, 0x4A525C, 0xEEF6E0, 0x1D4630, 1, 0 },
    { "SALT NIGHT",   0x120F26, 0x1E1C3E, 0x353552, 0x8FF0FF, 0x34305F, 0, 1 }
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
} Rush;

static Rush R;
static int  g_best, g_best_loaded;

static struct { int w, h; float scale, cx, base_y, depth, rise; } L;

static const CarDef *me(void) { return &CARS[R.car_index]; }

static void compute_layout(int w, int h) {
    L.w = w; L.h = h;
    float by_width = (float)w / (ROAD_W + 5.0f);
    float by_height = (float)h / (VIEW_AHEAD + VIEW_BEHIND) * 2.35f;
    L.scale = by_width < by_height ? by_width : by_height;
    L.cx = (float)w * 0.5f;
    /* The player sits low so most of the screen is road not yet reached.
       Reaction time is the whole game. */
    L.base_y = (float)h * 0.80f;
    L.depth = L.scale * 0.86f;
    L.rise = L.scale * 0.52f;
}

static PA_Vec2 project(float x, float z, float y) {
    PA_Vec2 p;
    p.x = L.cx + x * L.scale - R.lean * L.scale * 0.2f;
    p.y = L.base_y - (z - R.z) * L.depth - y * L.rise;
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
    const Biome *b = &BIOMES[R.biome];
    float side = pa_rng_chance(&R.rand, 0.5f) ? -1.0f : 1.0f;
    Prop *p = &R.props[R.prop_count++];
    p->x = side * (ROAD_W * 0.5f + pa_rng_range(&R.rand, 1.0f, 2.6f));
    p->z = R.z + VIEW_AHEAD + pa_rng_range(&R.rand, 0.0f, 10.0f);
    p->w = pa_rng_range(&R.rand, 0.8f, 1.9f);
    p->h = b->prop_tall ? pa_rng_range(&R.rand, 2.6f, 7.5f) : pa_rng_range(&R.rand, 0.6f, 1.6f);
    p->tint = pa_shade(pa_hex(b->prop), pa_rng_range(&R.rand, -0.14f, 0.14f));
    p->lit = b->lit && pa_rng_chance(&R.rand, 0.75f);
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
        float dx_pix = in->x - L.cx;
        want = pa_clampf(dx_pix / (L.scale * (ROAD_W * 0.5f)), -1.0f, 1.0f) * (ROAD_W * 0.5f + 0.6f);
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
static void box(PA_Canvas *c, float x, float z, float y,
                float w, float len, float h, PA_Color colour) {
    PA_Vec2 top = project(x, z, y + h);
    float tw = w * L.scale;
    float tl = len * L.depth;
    float th = h * L.rise;
    if (top.y + tl > (float)L.h + 90.0f || top.y - th < -140.0f) return;

    pa_fill_rect(c, top.x - tw * 0.5f, top.y + tl * 0.5f, tw, th, pa_shade(colour, -0.30f));
    pa_fill_rect(c, top.x + tw * 0.5f - 1.0f, top.y - tl * 0.5f,
                 tw * 0.10f > 1.5f ? tw * 0.10f : 1.5f, th + tl, pa_shade(colour, -0.46f));
    pa_fill_rect(c, top.x - tw * 0.5f, top.y - tl * 0.5f, tw, tl, colour);
}

static void draw_vehicle(PA_Canvas *c, float x, float z, float len, float wide,
                         PA_Color body, PA_Color roof, int oncoming, int dim) {
    PA_Vec2 s = project(x, z, 0.0f);
    float tw = wide * L.scale, tl = len * L.depth;

    pa_shadow(c, s.x, s.y + tl * 0.10f, tw * 0.62f, tl * 0.46f, dim ? 0.18f : 0.34f);

    /*
     * Wheels are wider than the body, not narrower. A box's front face hangs
     * down from its top, so a body drawn over wheels of equal width buries them
     * completely - the only way they read is by protruding at the sides.
     */
    for (int q = -1; q <= 1; q += 2) {
        box(c, x, z + (float)q * len * 0.30f, 0.0f, wide * 1.16f, len * 0.22f, 0.16f,
            pa_hex(0x1F2128));
    }

    box(c, x, z, 0.08f, wide, len, 0.26f, body);
    /* Darker skirt below the waistline: two tones without a light source. */
    box(c, x, z, 0.08f, wide * 1.004f, len * 0.995f, 0.08f, pa_shade(body, -0.24f));
    box(c, x, z + len * 0.04f, 0.34f, wide * 0.78f, len * 0.50f, 0.18f, roof);

    /* Wrapped glass rather than one dark patch. */
    PA_Vec2 wp = project(x, z + len * 0.18f, 0.54f);
    pa_fill_rect(c, wp.x - tw * 0.30f, wp.y - tl * 0.09f, tw * 0.60f, tl * 0.18f,
                 PA_RGBA(30, 42, 68, 205));

    PA_Vec2 front = project(x, z + len * 0.5f, 0.20f);
    PA_Vec2 back  = project(x, z - len * 0.5f, 0.20f);
    float lw = tw * 0.17f, lh = L.depth * 0.11f > 2.0f ? L.depth * 0.11f : 2.0f;
    for (int side = -1; side <= 1; side += 2) {
        float ox = (float)side * tw * 0.30f;
        pa_round_rect(c, front.x + ox - lw * 0.5f, front.y - lh * 0.5f, lw, lh, lh * 0.4f,
                      oncoming ? PA_RGBA(255, 250, 226, 250) : PA_RGBA(255, 244, 200, 225));
        pa_round_rect(c, back.x + ox - lw * 0.5f, back.y - lh * 0.5f, lw, lh, lh * 0.4f,
                      PA_RGBA(232, 62, 62, 225));
    }
}

static void rush_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) compute_layout(c->w, c->h);

    const Biome *a = &BIOMES[R.biome];
    const Biome *b = &BIOMES[(R.biome + 1) % BIOME_COUNT];
    float mix = R.biome_mix;
    PA_Color sky_c    = pa_mix(pa_hex(a->sky), pa_hex(b->sky), mix);
    PA_Color ground_c = pa_mix(pa_hex(a->ground), pa_hex(b->ground), mix);
    PA_Color road_c   = pa_mix(pa_hex(a->road), pa_hex(b->road), mix);
    PA_Color line_c   = pa_mix(pa_hex(a->line), pa_hex(b->line), mix);

    PA_Paint sky = pa_linear(0, 0, 0, (float)c->h * 0.5f);
    pa_stop(&sky, 0.0f, pa_shade(sky_c, 0.22f));
    pa_stop(&sky, 1.0f, sky_c);
    pa_fill_rect_paint(c, 0, 0, (float)c->w, (float)c->h, &sky);

    float horizon = project(0.0f, R.z + VIEW_AHEAD + 12.0f, 0.0f).y;
    pa_fill_rect(c, 0, horizon, (float)c->w, (float)c->h - horizon, ground_c);

    float lx = project(-ROAD_W * 0.5f, R.z, 0.0f).x;
    float rx = project(ROAD_W * 0.5f, R.z, 0.0f).x;
    pa_fill_rect(c, lx, horizon, rx - lx, (float)c->h - horizon, road_c);
    /* Rumble strip either side, then a bright kerb line: the edge of the road
       has to be unmistakable at speed. */
    pa_fill_rect(c, lx - L.scale * 0.34f, horizon, L.scale * 0.34f,
                 (float)c->h - horizon, pa_shade(road_c, 0.20f));
    pa_fill_rect(c, rx, horizon, L.scale * 0.34f, (float)c->h - horizon,
                 pa_shade(road_c, 0.20f));
    pa_fill_rect(c, lx - 2.0f, horizon, 3.0f, (float)c->h - horizon, pa_alpha(line_c, 0.55f));
    pa_fill_rect(c, rx - 1.0f, horizon, 3.0f, (float)c->h - horizon, pa_alpha(line_c, 0.55f));

    /* Lane dashes as world-space rungs, so they scroll with distance. */
    float first = floorf(R.z / 4.0f) * 4.0f;
    for (float d = first - VIEW_BEHIND; d < R.z + VIEW_AHEAD + 8.0f; d += 4.0f) {
        for (int l = 1; l < LANES; l++) {
            PA_Vec2 px = project(lane_centre(l) - LANE_W * 0.5f, d, 0.0f);
            if (px.y < horizon - 20.0f || px.y > (float)c->h + 20.0f) continue;
            pa_fill_rect(c, px.x - L.scale * 0.05f, px.y - L.depth * 0.9f,
                         L.scale * 0.10f, L.depth * 1.8f, pa_alpha(line_c, 0.72f));
        }
    }

    for (int i = 0; i < R.prop_count; i++) {
        Prop *p = &R.props[i];
        box(c, p->x, p->z, 0.0f, p->w, p->w, p->h, p->tint);
        if (!p->lit || p->h < 1.6f) continue;
        PA_Vec2 top = project(p->x, p->z, p->h);
        float pw = p->w * L.scale, ph = p->h * L.rise;
        if (top.y > (float)c->h + 40.0f || top.y + ph < -40.0f) continue;
        int cols = (int)(p->w * 2.2f + 0.5f); if (cols < 2) cols = 2;
        int rows = (int)(p->h * 1.1f + 0.5f); if (rows < 2) rows = 2;
        float cw = pw / (float)cols * 0.42f, ch = ph / (float)rows * 0.40f;
        for (int wx = 0; wx < cols; wx++) {
            for (int wy = 0; wy < rows; wy++) {
                if ((p->mask >> ((wx * 3 + wy) % 16)) & 1) continue;
                int warm = ((wx * 5 + wy * 3) % 3) != 0;
                pa_fill_rect(c,
                    top.x - pw * 0.5f + ((float)wx + 0.5f) * (pw / (float)cols) - cw * 0.5f,
                    top.y + p->w * L.depth * 0.5f + ((float)wy + 0.35f) * (ph / (float)rows) - ch * 0.5f,
                    cw, ch, warm ? PA_RGBA(255, 238, 178, 185) : PA_RGBA(150, 205, 255, 130));
            }
        }
    }

    for (int i = 0; i < R.coin_count; i++) {
        Coin *co = &R.coins[i];
        PA_Vec2 cp = project(co->x, co->z, 0.42f);
        PA_Vec2 gp = project(co->x, co->z, 0.0f);
        if (cp.y < horizon - 20.0f || cp.y > (float)c->h + 30.0f) continue;
        float rr = L.scale * 0.24f;
        float squash = fabsf(cosf(R.time * 3.0f + (float)co->spin));
        pa_shadow(c, cp.x, gp.y, rr * 0.8f, rr * 0.30f, 0.22f);
        PA_Paint gold = pa_linear(cp.x, cp.y - rr, cp.x, cp.y + rr);
        pa_stop(&gold, 0.0f, pa_hex(0xFFE9A0));
        pa_stop(&gold, 0.55f, pa_hex(0xFFC93C));
        pa_stop(&gold, 1.0f, pa_hex(0xC98A12));
        pa_fill_ellipse_paint(c, cp.x, cp.y, rr * squash > 1.5f ? rr * squash : 1.5f, rr, &gold);
    }

    /*
     * Far to near, so nearer cars overlap correctly. An index array sorted by
     * depth, not a scan that marks the source: the first attempt stashed a
     * sentinel in the car's own drift field and restored it straight after
     * drawing, so every pass picked the same farthest car and the rest of the
     * traffic was never drawn at all.
     */
    int order[MAX_TRAFFIC];
    for (int i = 0; i < R.traffic_count; i++) order[i] = i;
    for (int i = 1; i < R.traffic_count; i++) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 && R.traffic[order[j]].z < R.traffic[key].z) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    for (int i = 0; i < R.traffic_count; i++) {
        Car *cc = &R.traffic[order[i]];
        PA_Color body = pa_hsl(cc->hue, 0.78f, 0.58f);
        draw_vehicle(c, cc->x, cc->z, cc->len, cc->wide, body, pa_shade(body, 0.30f),
                     cc->oncoming, 0);
    }

    const CarDef *car = me();
    int phasing = R.ability_t > 0.0f && car->ability == AB_PHASE;
    if (!R.crashed || R.crash_t < 0.35f) {
        draw_vehicle(c, R.x, R.z, car->len, car->wide, pa_hex(car->body), pa_hex(car->roof),
                     0, phasing);
    }

    if (R.ability_t > 0.0f && car->ability == AB_BOOST) {
        PA_Vec2 fp = project(R.x, R.z - car->len * 0.55f, 0.20f);
        float fw = car->wide * L.scale * 0.30f;
        PA_Vec2 flame[3] = {
            { fp.x - fw, fp.y },
            { fp.x + fw, fp.y },
            { fp.x, fp.y + L.depth * (2.0f + fabsf(sinf(R.time * 30.0f)) * 0.8f) }
        };
        pa_fill_poly(c, flame, 3, PA_RGBA(255, 214, 120, 190));
    }

    if (R.ability_t > 0.0f && car->ability == AB_SIREN) {
        PA_Vec2 sp = project(R.x, R.z, 0.60f);
        int on = ((int)(R.time * 9.0f) % 2) == 0;
        pa_fill_rect(c, sp.x - car->wide * L.scale * 0.34f, sp.y - 3.0f,
                     car->wide * L.scale * 0.68f, 6.0f,
                     on ? pa_hex(0x4B8BFF) : pa_hex(0xFF4B5C));
    }

    /* Speed streaks: the cheapest possible sense of pace. */
    float pace = pa_clamp01((R.speed - 18.0f) / 26.0f);
    if (pace > 0.02f) {
        for (int k = 0; k < 10; k++) {
            float t = pa_wrapf(R.time * (2.2f + pace * 3.0f) + (float)k * 0.31f, 1.0f);
            float side = (k % 2) ? 1.0f : -1.0f;
            float sx = (float)c->w * 0.5f + side * (float)c->w * (0.36f + (float)(k % 4) * 0.04f);
            pa_fill_rect(c, sx, t * (float)c->h, 2.0f,
                         (float)c->h * 0.10f * (0.4f + pace), PA_RGBA(255, 255, 255, (int)(pace * 58.0f)));
        }
    }

    pa_vignette(c, 0.5f);
    pa_hud_scrim(c, 96.0f);

    char buf[64];
    snprintf(buf, sizeof(buf), "%d M", R.score);
    pa_text(c, buf, 104.0f, 34.0f, 20.0f, PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 2.0f);
    snprintf(buf, sizeof(buf), "%d KM/H", (int)(R.speed * 3.6f));
    pa_text(c, buf, (float)c->w * 0.5f + 40.0f, 34.0f, 13.0f,
            PA_RGBA(255, 255, 255, 180), PA_ALIGN_CENTER, 2.0f);
    snprintf(buf, sizeof(buf), "%d", R.picked);
    pa_text(c, buf, (float)c->w - 20.0f, 34.0f, 18.0f,
            PA_RGB(255, 201, 60), PA_ALIGN_RIGHT, 2.0f);

    snprintf(buf, sizeof(buf), "%s %s", car->ability_name,
             R.cooldown <= 0.0f ? "READY" : "");
    pa_text(c, buf, (float)c->w * 0.5f, 72.0f, 11.0f,
            R.cooldown <= 0.0f ? PA_RGB(93, 224, 255) : PA_RGBA(255, 255, 255, 110),
            PA_ALIGN_CENTER, 3.0f);

    if (R.banner_t > 0.0f) {
        pa_text(c, R.banner, (float)c->w * 0.5f, (float)c->h * 0.30f, 22.0f,
                PA_RGBA(255, 214, 84, (int)(pa_clamp01(R.banner_t) * 230.0f)),
                PA_ALIGN_CENTER, 5.0f);
    }

    if (R.crashed) {
        float a2 = pa_clamp01(R.crash_t * 2.2f);
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(10, 8, 24, (int)(a2 * 175.0f)));
        pa_text(c, R.score >= g_best ? "NEW RECORD" : "WRECKED",
                (float)c->w * 0.5f, (float)c->h * 0.42f, 32.0f,
                R.score >= g_best ? PA_RGB(126, 240, 160) : PA_RGB(255, 107, 122),
                PA_ALIGN_CENTER, 5.0f);
        snprintf(buf, sizeof(buf), "%d M - BEST %d M", R.score, g_best);
        pa_text(c, buf, (float)c->w * 0.5f, (float)c->h * 0.50f, 13.0f,
                PA_RGBA(255, 255, 255, 185), PA_ALIGN_CENTER, 3.0f);
    }
}

static void rush_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    pa_fill_rect(c, x, y, w, h, pa_hex(0x2A3352));
    pa_fill_rect(c, x + w * 0.16f, y, w * 0.68f, h, pa_hex(0x474E60));
    pa_fill_rect(c, x, y, w * 0.16f, h, pa_hex(0x223054));
    pa_fill_rect(c, x + w * 0.84f, y, w * 0.16f, h, pa_hex(0x223054));

    for (int l = 1; l < 3; l++) {
        float lx = x + w * (0.16f + 0.68f * (float)l / 3.0f);
        for (int d = 0; d < 6; d++) {
            float yy = pa_wrapf(t * 0.9f + (float)d / 6.0f, 1.0f) * h;
            pa_fill_rect(c, lx - 1.5f, y + yy, 3.0f, h * 0.09f, PA_RGBA(247, 236, 198, 180));
        }
    }

    struct { float cx, phase; uint32_t body, roof; } cars[3] = {
        { 0.34f, 0.10f, 0xD9483C, 0xFF8E84 },
        { 0.66f, 0.60f, 0xE9C33F, 0xFFEBA0 },
        { 0.50f, -1.0f, 0x3D8BFF, 0x9FCBFF }
    };
    for (int i = 0; i < 3; i++) {
        float cy = cars[i].phase < 0.0f
                 ? y + h * 0.76f
                 : y + pa_wrapf(t * 0.5f + cars[i].phase, 1.0f) * (h + 40.0f) - 20.0f;
        float cw = w * 0.16f, chh = h * 0.21f;
        float cx = x + w * cars[i].cx;
        pa_shadow(c, cx, cy + chh * 0.42f, cw * 0.6f, chh * 0.24f, 0.30f);
        pa_round_rect(c, cx - cw * 0.5f, cy - chh * 0.5f, cw, chh, 3.0f, pa_hex(cars[i].body));
        pa_round_rect(c, cx - cw * 0.34f, cy - chh * 0.26f, cw * 0.68f, chh * 0.44f, 2.0f,
                      pa_hex(cars[i].roof));
    }
}

const PA_Game PA_GAME_CHROMERUSH = {
    "chromerush", "Chrome Rush", "Endless Driver",
    "Endless highway. Thread the traffic, bank near misses, and unlock five very different rides.",
    PA_RGB(255, 122, 75),
    rush_start, rush_stop, rush_update, rush_render, rush_thumb
};
