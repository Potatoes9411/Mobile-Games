/* ===========================================================================
   HELIX DROP - native port, rebuilt against the Helix Jump plates
   A ball bounces down a segmented tower. Drag to spin the tower and line a gap
   up under it. Every floor the ball drops through shatters into shards; drop
   through three in a row without touching anything and the ball catches fire
   and smashes the next floor it meets, red wedges included. Land on red
   otherwise and the run ends.

   The tower is a stack of squashed annular rings seen from above with a soft
   perspective: deeper rings shrink and crowd together, which is what gives the
   reference its look of staring down a well. Every ring sits at a known depth,
   so the scene is ellipse arcs sorted back to front.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SEGMENTS    12
#define SEG_STEP    (PA_TAU / (float)SEGMENTS)
#define MAX_FLOORS  44
#define MAX_SPLATS  6
#define MAX_PARTS   900
#define MAX_POPS    6
#define MAX_WORDS   3

#define GRAVITY     15.0f      /* floors / s^2 */
#define BOUNCE_V    4.1f       /* apex a little over half a floor */
#define MAX_FALL    10.0f
#define CONTINUE_COST 30
#define CONTINUE_TIME 5.0f

enum { SEG_SAFE = 0, SEG_DEADLY = 1, SEG_GAP = 2 };
enum { MAT_GLOSS, MAT_SPONGE, MAT_STONE, MAT_CANDY };
enum { COL_PLAIN, COL_GROOVES, COL_BRICK, COL_BANDS };
enum { BACK_CLOUDS, BACK_CITY };
enum { ST_READY, ST_PLAY, ST_DEAD, ST_FAIL, ST_WIN };
enum { P_SHARD, P_DROP, P_EMBER, P_CONFETTI };

/* ------------------------------------------------------------------ themes */
typedef struct {
    uint32_t sky_top, sky_bot, cloud;
    uint32_t pillar, pillar_mark;
    int      pillar_style;
    uint32_t plate, plate_mark;
    int      material;
    uint32_t ball;
    int      backdrop;
    uint32_t skyline_far, skyline_near;
    int      boss;
} Theme;

/* The plates agree on a pale sky, a near-white column and one saturated
   platform colour per level; what changes level to level is that colour, the
   platform material (glossy, sponge, stone) and the backdrop. The boss level
   goes dark and grey so its spiked red rings are the only colour on screen. */
static const Theme THEMES[] = {
    /* meadow: glossy green on a blue sky */
    { 0x86CCFF, 0xEAF6FF, 0xFFFFFF, 0xFAFBFD, 0xD8DEE8, COL_PLAIN,
      0x33CC55, 0x22A043, MAT_GLOSS,  0xFF5A36, BACK_CLOUDS, 0, 0, 0 },
    /* sponge: yellow sponge on a warm sky */
    { 0xFFCF9E, 0xFFF6EA, 0xFFFFFF, 0xFFFDF7, 0xEDE3D0, COL_GROOVES,
      0xF6D24B, 0xC99E22, MAT_SPONGE, 0x2BA8F0, BACK_CLOUDS, 0, 0, 0 },
    /* metro: blue stone in front of a grey skyline */
    { 0xC4D0E2, 0xF4F6FA, 0xFFFFFF, 0xF4F5F7, 0xC9CED8, COL_BRICK,
      0x3F7CF6, 0x2A5BCB, MAT_STONE,  0xFFC21C, BACK_CITY, 0xAEB8C8, 0x8C97AA, 0 },
    /* candy: striped violet on a pink sky */
    { 0xFFB8DC, 0xFFF0F8, 0xFFFFFF, 0xFFFFFF, 0xFF8FC6, COL_BANDS,
      0x9B5CF6, 0xC49BFF, MAT_CANDY,  0x16C9A0, BACK_CLOUDS, 0, 0, 0 },
};
#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))

static const Theme BOSS_THEME = {
    0x3D424C, 0x7C828D, 0x9AA0AA, 0x666B75, 0x4E535C, COL_GROOVES,
    0x8E949E, 0x6F757F, MAT_STONE, 0xFFFFFF, BACK_CITY, 0x50555F, 0x454A53, 1
};

#define HAZARD 0xE53935

/* ------------------------------------------------------------------- state */
typedef struct {
    unsigned char seg[SEGMENTS];
    float y;
    float broken;          /* 1 -> 0 while the shatter plays */
    int   gone;            /* passed or smashed */
    float hit;             /* landing dip */
    int   splat_count;
    float splat_a[MAX_SPLATS];
    float splat_t[MAX_SPLATS];        /* age, for the pop-in */
    unsigned char splat_seed[MAX_SPLATS];
    int   slider;          /* a red block riding back and forth on the ring */
    float slide_c, slide_amp, slide_speed, slide_w, slide_phase;
} Floor;

typedef struct {
    float x, z, wy, vx, vz, vy;
    float life, max, size, rot, vr;
    PA_Color col;
    int kind;
} Part;

typedef struct { char text[16]; float t; } Pop;
typedef struct { char text[16]; float t; PA_Color col; } Word;

typedef struct {
    Floor  floors[MAX_FLOORS];
    int    floor_count;
    Theme  th;
    int    level, score, level_start_score;
    int    state;
    float  state_t, time;

    float  ball_y, ball_v, land_t;
    float  spin, spin_vel;
    int    combo, smashing;
    float  fire_t;              /* fireball glow ramp */
    float  cam_y, shake;
    int    passed, continued;
    int    dragging;
    float  drag_x;
    int    death_floor;
    int    coins_shown, win_coins;

    Part   parts[MAX_PARTS];
    int    part_next;
    Pop    pops[MAX_POPS];
    Word   words[MAX_WORDS];

    /* demo bot */
    int    bot_bounces[MAX_FLOORS];
    int    bot_floor;
    float  bot_vel;
    int    demo_runs;
} Helix;

static Helix H;
static int   g_best, g_coins, g_loaded;

static struct {
    int   w, h;
    float cx, R, inner, squash, spacing, thick, ball_r, B, pq, u;
} L;

static void compute_layout(int w, int h) {
    L.w = w; L.h = h;
    L.cx = (float)w * 0.5f;
    float r = (float)w * 0.52f;
    if (r > (float)h * 0.30f) r = (float)h * 0.30f;
    L.R = r;
    L.inner = r * 0.34f;
    L.squash = 0.40f;
    L.spacing = r * 0.80f;
    L.thick = r * 0.11f;
    L.ball_r = r * 0.105f;
    L.B = (float)h * 0.34f;
    L.pq = 0.34f / (float)h;
    float s = (float)w < (float)h * 0.62f ? (float)w : (float)h * 0.62f;
    L.u = s / 540.0f;
}

/* -------------------------------------------------------------- projection */
/* Depth d is the distance below the camera in pixels at the ball's depth; the
   camera looks down the tower, so deeper things shrink and crowd together. */
static float persp_k(float d) {
    float zc = 1.0f + d * L.pq;
    if (zc < 0.55f) zc = 0.55f;
    return 1.0f / zc;
}
static float depth_of(float y) { return (y - H.cam_y) * L.spacing; }
static float proj_y(float d) { return L.B + d * persp_k(d); }

static int demo(void) { return pa_demo_mode(); }

/* -------------------------------------------------------------- generation */
static int is_boss(int level) { return level % 5 == 0; }

static void pick_theme(int level) {
    if (is_boss(level)) { H.th = BOSS_THEME; return; }
    int n = level - 1 - (level - 1) / 5;
    H.th = THEMES[n % THEME_COUNT];
}

static void build_tower(int level) {
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)(level * 48271 + 7));
    int boss = is_boss(level);

    int count = 12 + (level * 2 < 24 ? level * 2 : 24) - 2;
    if (boss) count += 4;
    if (count > MAX_FLOORS) count = MAX_FLOORS;
    H.floor_count = count;

    int prev_gap = 3;
    for (int i = 0; i < count; i++) {
        Floor *f = &H.floors[i];
        memset(f, 0, sizeof(*f));
        f->y = (float)(i + 1);

        float difficulty = pa_clamp01((float)i / (float)count) * pa_clamp01((float)level / 10.0f);

        /* One guaranteed run of gaps per floor, never straight under the last
           one, so every floor asks for a turn and none can seal the tower. */
        int gap_width = level < 3 ? 3 : 2;
        int gap_start;
        do gap_start = pa_rng_int(&r, 0, SEGMENTS - 1);
        while (i > 0 && abs(((gap_start - prev_gap + SEGMENTS + SEGMENTS / 2) % SEGMENTS) - SEGMENTS / 2) < 2);
        if (i == 0) gap_start = 7;           /* away from the ball's start wedge */
        prev_gap = gap_start;
        for (int g = 0; g < gap_width; g++) f->seg[(gap_start + g) % SEGMENTS] = SEG_GAP;
        if (pa_rng_chance(&r, 0.35f)) {
            int s = pa_rng_int(&r, 0, SEGMENTS - 1);
            f->seg[s] = SEG_GAP;
        }

        int deadly = (int)(pa_lerpf(1.0f, 4.0f, difficulty) + 0.5f) + (boss ? 2 : 0);
        if (i == 0) deadly = 0;
        for (int d = 0; d < deadly; d++) {
            int slot = pa_rng_int(&r, 0, SEGMENTS - 1);
            if (f->seg[slot] == SEG_GAP) continue;
            f->seg[slot] = SEG_DEADLY;
            /* Hazards come in pairs past the opening floors. */
            if (i > 3 && pa_rng_chance(&r, 0.5f)) {
                int nb = (slot + 1) % SEGMENTS;
                if (f->seg[nb] == SEG_SAFE) f->seg[nb] = SEG_DEADLY;
            }
        }
        if (i == 0) { f->seg[2] = f->seg[3] = f->seg[4] = SEG_SAFE; }

        /* Moving hazard from level 3 on: a red block sliding across a safe run. */
        if (level >= 3 && i >= 3 && pa_rng_chance(&r, boss ? 0.35f : 0.28f)) {
            int best_s = -1, best_len = 0;
            for (int s = 0; s < SEGMENTS; s++) {
                if (f->seg[s] != SEG_SAFE) continue;
                if (f->seg[(s + SEGMENTS - 1) % SEGMENTS] == SEG_SAFE) continue;
                int len = 0;
                while (len < SEGMENTS && f->seg[(s + len) % SEGMENTS] == SEG_SAFE) len++;
                if (len > best_len) { best_len = len; best_s = s; }
            }
            if (best_len >= 4) {
                f->slider = 1;
                f->slide_w = SEG_STEP * 0.9f;
                f->slide_c = ((float)best_s + (float)best_len * 0.5f) * SEG_STEP;
                f->slide_amp = ((float)best_len * SEG_STEP - f->slide_w) * 0.5f - 0.05f;
                f->slide_speed = pa_rng_range(&r, 1.4f, 2.2f);
                f->slide_phase = pa_rng_range(&r, 0.0f, PA_TAU);
            }
        }
    }
}

/* Kind of floor at a tower-frame angle, sliders included. */
static float slider_angle(const Floor *f) {
    return f->slide_c + f->slide_amp * sinf(H.time * f->slide_speed + f->slide_phase);
}
static float ang_diff(float a, float b) {
    return pa_wrapf(a - b + PA_PI, PA_TAU) - PA_PI;
}
static int kind_at_t(const Floor *f, float a, float dt_ahead) {
    a = pa_wrapf(a, PA_TAU);
    if (f->slider) {
        float sc = f->slide_c + f->slide_amp * sinf((H.time + dt_ahead) * f->slide_speed + f->slide_phase);
        if (fabsf(ang_diff(a, sc)) < f->slide_w * 0.5f + (dt_ahead > 0.0f ? 0.12f : 0.0f)) return SEG_DEADLY;
    }
    int s = (int)(a / SEG_STEP) % SEGMENTS;
    return f->seg[s];
}
static int kind_at(const Floor *f, float a) { return kind_at_t(f, a, 0.0f); }
static float under_angle(void) { return pa_wrapf(PA_TAU * 0.25f - H.spin, PA_TAU); }

/* ------------------------------------------------------------------- setup */
static void load_save(void) {
    if (g_loaded) return;
    g_loaded = 1;
    if (demo()) { g_best = 2460; g_coins = 1240; return; }
    g_best = pa_save_get("helix.best", 0);
    g_coins = pa_save_get("helix.coins", 0);
}

static void save_progress(void) {
    if (demo()) return;
    pa_save_set("helix.best", g_best);
    pa_save_set("helix.coins", g_coins);
    pa_save_set("helix.level", H.level);
    pa_save_flush();
}

static void begin_level(int level, int score) {
    float t = H.time;
    int runs = H.demo_runs;
    memset(&H, 0, sizeof(H));
    H.time = t;
    H.demo_runs = runs;
    H.level = level;
    H.score = score;
    H.level_start_score = score;
    pick_theme(level);
    build_tower(level);
    H.state = ST_READY;
    H.ball_y = 0.55f;
    H.cam_y = 1.0f;
    H.dragging = -1;
    H.coins_shown = g_coins;
    H.death_floor = -1;
}

static void helix_start(void) {
    load_save();
    if (L.w == 0) compute_layout(540, 1170);
    int level;
    if (demo()) {
        static const int DEMO_LEVEL[] = { 1, 1, 5, 3, 4 };
        int d = demo();
        level = DEMO_LEVEL[d >= 0 && d < 5 ? d : 1];
    } else {
        level = pa_save_get("helix.level", 1);
        if (level < 1) level = 1;
    }
    H.time = 0.0f;
    H.demo_runs = 0;
    begin_level(level, 0);
}

static void helix_stop(void) { save_progress(); }

/* --------------------------------------------------------------- feedback */
static Part *new_part(void) {
    Part *p = &H.parts[H.part_next];
    H.part_next = (H.part_next + 1) % MAX_PARTS;
    memset(p, 0, sizeof(*p));
    return p;
}

static void pop_score(int gain) {
    for (int i = MAX_POPS - 1; i > 0; i--) H.pops[i] = H.pops[i - 1];
    snprintf(H.pops[0].text, sizeof(H.pops[0].text), "+%d", gain);
    H.pops[0].t = 1.0f;
}

static void word(const char *t, PA_Color col) {
    for (int i = MAX_WORDS - 1; i > 0; i--) H.words[i] = H.words[i - 1];
    snprintf(H.words[0].text, sizeof(H.words[0].text), "%s", t);
    H.words[0].t = 1.0f;
    H.words[0].col = col;
}

/* Burst a ring into cube shards thrown outward from where its pieces were. */
static void shatter(Floor *f, int count, float power) {
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)(f->y * 977.0f) + (uint32_t)(H.time * 1000.0f));
    float ro = L.R, ri = L.inner;
    for (int n = 0, tries = 0; n < count && tries < count * 4; tries++) {
        float a = pa_rng_range(&r, 0.0f, PA_TAU);
        int kind = kind_at(f, a);
        if (kind == SEG_GAP) continue;
        float sa = a + H.spin;
        float rad = pa_rng_range(&r, ri * 1.1f, ro);
        Part *p = new_part();
        p->kind = P_SHARD;
        p->x = cosf(sa) * rad;
        p->z = sinf(sa) * rad;
        p->wy = f->y * L.spacing - pa_rng_range(&r, 0.0f, L.thick);
        float out = pa_rng_range(&r, 160.0f, 420.0f) * power * L.u;
        p->vx = cosf(sa) * out + pa_rng_range(&r, -40.0f, 40.0f);
        p->vz = sinf(sa) * out;
        p->vy = -pa_rng_range(&r, 80.0f, 340.0f) * power * L.u;
        p->max = p->life = pa_rng_range(&r, 0.38f, 0.62f);
        p->size = pa_rng_range(&r, 0.010f, 0.020f) * 540.0f * L.u;
        p->rot = pa_rng_range(&r, 0.0f, PA_TAU);
        p->vr = pa_rng_range(&r, -14.0f, 14.0f);
        PA_Color base = kind == SEG_DEADLY ? pa_hex(HAZARD) : pa_hex(H.th.plate);
        p->col = pa_shade(base, pa_rng_range(&r, -0.15f, 0.25f));
        n++;
    }
    f->broken = 1.0f;
    f->gone = 1;
}

static void splash(float wy, PA_Color col, int count, float power) {
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)(H.time * 7919.0f) + 3u);
    float rm = (L.inner + L.R) * 0.5f;
    for (int i = 0; i < count; i++) {
        Part *p = new_part();
        p->kind = P_DROP;
        float a = pa_rng_range(&r, 0.0f, PA_TAU);
        float s = pa_rng_range(&r, 60.0f, 220.0f) * power * L.u;
        p->x = 0.0f; p->z = rm; p->wy = wy;
        p->vx = cosf(a) * s; p->vz = sinf(a) * s;
        p->vy = -pa_rng_range(&r, 120.0f, 330.0f) * power * L.u;
        p->max = p->life = pa_rng_range(&r, 0.25f, 0.42f);
        p->size = pa_rng_range(&r, 3.0f, 6.5f) * L.u;
        p->col = col;
    }
}

static void confetti(void) {
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)H.level * 31u + 5u);
    static const uint32_t C[] = { 0xFFC21C, 0xFF4F7B, 0x35D07F, 0x3FA9FF, 0xB46BFF, 0xFFFFFF };
    for (int i = 0; i < 120; i++) {
        Part *p = new_part();
        p->kind = P_CONFETTI;
        p->x = (float)L.w * (pa_rng_next(&r) < 0.5f ? 0.08f : 0.92f);
        p->wy = (float)L.h * 0.62f;
        float a = (p->x < L.cx ? -1.0f : 1.0f) * pa_rng_range(&r, 0.15f, 0.75f);
        float s = pa_rng_range(&r, 500.0f, 1050.0f) * L.u;
        p->vx = sinf(a) * s;
        p->vy = -cosf(a) * s;
        p->max = p->life = pa_rng_range(&r, 1.6f, 2.6f);
        p->size = pa_rng_range(&r, 5.0f, 9.0f) * L.u;
        p->rot = pa_rng_range(&r, 0.0f, PA_TAU);
        p->vr = pa_rng_range(&r, -10.0f, 10.0f);
        p->col = pa_hex(C[i % 6]);
    }
}

static int done_floors(void) {
    int n = 0;
    for (int i = 0; i < H.floor_count; i++) if (H.floors[i].gone) n++;
    return n;
}

static int percent_done(void) {
    return H.floor_count ? (done_floors() * 100) / H.floor_count : 0;
}

/* ------------------------------------------------------------------ events */
static void die(int floor_index) {
    H.state = ST_DEAD;
    H.state_t = 0.0f;
    H.shake = 1.0f;
    H.death_floor = floor_index;
    splash(H.floors[floor_index].y * L.spacing, pa_hex(H.th.ball), 14, 1.0f);
    pa_tone(300, 70, 0.45f, 3, 0.13f);
    pa_noise(0.22f, 0.16f);
    if (H.score > g_best) g_best = H.score;
    save_progress();
}

static void win(void) {
    H.state = ST_WIN;
    H.state_t = 0.0f;
    H.ball_v = -BOUNCE_V * 0.8f;
    H.land_t = 0.0f;
    H.score += 50 + H.level * 10;
    pop_score(50 + H.level * 10);
    H.win_coins = 20 + H.level * 5;
    H.coins_shown = g_coins;
    g_coins += H.win_coins;
    if (H.score > g_best) g_best = H.score;
    confetti();
    pa_sfx("win");
    H.level++;
    save_progress();
    H.level--;
}

static void land_bounce(Floor *f) {
    H.ball_y = f->y;
    H.ball_v = -BOUNCE_V;
    f->hit = 1.0f;
    H.land_t = 0.0f;
    int k = f->splat_count < MAX_SPLATS ? f->splat_count++ : (int)(H.time * 7.0f) % MAX_SPLATS;
    f->splat_a[k] = under_angle() + ((float)((int)(H.time * 53.0f) % 7) - 3.0f) * 0.012f;
    f->splat_t[k] = 0.0f;
    f->splat_seed[k] = (unsigned char)((int)(H.time * 97.0f) & 255);
    splash(f->y * L.spacing, pa_hex(H.th.ball), 7, 0.7f);
    H.combo = 0;
    pa_tone(520.0f + (float)(H.level % 4) * 40.0f, 760.0f, 0.06f, 0, 0.08f);
}

/* ------------------------------------------------------------------- the bot
   Review captures only. It plays like a person would: bounces a few times,
   swings the tower to the nearest gap while the ball is in the air, chains a
   run of gaps to light the fireball and aims the fire at a red wedge. */
static int bot_plan_bounces(int idx) {
    static const int PLAN[] = { 2, 1, 1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 0 };
    if (H.level != 1) {
        static const int PLAN2[] = { 1, 0, 1, 0, 0, 0, 1, 1, 0, 2, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1 };
        return PLAN2[idx % 20];
    }
    return PLAN[idx % 20];
}
static int bot_wants_death(int idx) {
    /* Second level of the demo run ends on red so the fail card is shown. */
    return demo() == 1 && H.demo_runs == 1 && idx >= 7 && H.bot_bounces[idx] >= 1;
}

static void bot_update(float dt) {
    int idx = -1;
    for (int i = 0; i < H.floor_count; i++)
        if (!H.floors[i].gone && H.floors[i].y >= H.ball_y - 1e-4f) { idx = i; break; }
    if (idx < 0) { H.bot_vel = 0.0f; return; }
    Floor *f = &H.floors[idx];

    int want;  /* kind to put under the ball */
    if (H.smashing) want = SEG_DEADLY;
    else if (H.bot_bounces[idx] >= bot_plan_bounces(idx)) want = bot_wants_death(idx) ? SEG_DEADLY : SEG_GAP;
    else want = SEG_SAFE;
    if (want == SEG_DEADLY) {
        int any = 0;
        for (int s = 0; s < SEGMENTS; s++) if (f->seg[s] == SEG_DEADLY) any = 1;
        if (!any && !H.smashing) want = SEG_GAP;
    }

    float cur = under_angle();
    float dist0 = f->y - H.ball_y;
    float v0 = H.ball_v;
    float t_hit = dist0 <= 0.0f ? 0.0f : (-v0 + sqrtf(v0 * v0 + 2.0f * GRAVITY * dist0)) / GRAVITY;
    float reach = 10.0f * (t_hit - 0.05f);
    if (reach < 0.0f) reach = 0.0f;
    float best = 1e9f, target = cur;
    /* Wanted kind within reach before touch-down, else the nearest safe spot. */
    for (int pass = 0; pass < 3 && best > 1e8f; pass++) {
        int w = pass == 0 ? want : SEG_SAFE;
        float lim = pass == 2 ? 1e9f : reach + 0.25f;
        if (pass == 1 && want == SEG_SAFE) continue;
        for (int s = 0; s < SEGMENTS * 3; s++) {
            float a = ((float)s + 0.5f) / 3.0f * SEG_STEP;
            int k = kind_at_t(f, a, t_hit);
            int ok = k == w;
            /* Aim at the middle of a gap, never its lip. */
            if (ok && w == SEG_GAP &&
                (kind_at_t(f, a - SEG_STEP * 0.45f, t_hit) != SEG_GAP || kind_at_t(f, a + SEG_STEP * 0.45f, t_hit) != SEG_GAP)) ok = 0;
            if (ok && w == SEG_SAFE &&
                (kind_at_t(f, a - SEG_STEP * 0.3f, t_hit) == SEG_DEADLY || kind_at_t(f, a + SEG_STEP * 0.3f, t_hit) == SEG_DEADLY)) ok = 0;
            if (!ok) continue;
            float d = fabsf(ang_diff(a, cur));
            if (d > lim) continue;
            float cost = d;
            /* About to light the fireball: line the gap up over red below. */
            if (w == SEG_GAP && H.combo == 2 && idx + 1 < H.floor_count) {
                float near_red = PA_PI;
                for (int q = 0; q < SEGMENTS * 2; q++) {
                    float b2 = ((float)q + 0.5f) * SEG_STEP * 0.5f;
                    if (kind_at(&H.floors[idx + 1], b2) == SEG_DEADLY) {
                        float dd = fabsf(ang_diff(b2, a));
                        if (dd < near_red) near_red = dd;
                    }
                }
                cost = d * 0.3f + near_red;
            }
            if (cost < best) { best = cost; target = a; }
        }
    }
    if (best > 1e8f) { H.bot_vel = 0.0f; return; }
    /* Turning the tower moves the segment under the ball the other way. */
    float delta = -ang_diff(target, cur);
    float dist = f->y - H.ball_y;
    float vmax = 13.0f;
    float desired = pa_clampf(delta * 20.0f, -vmax, vmax);
    if (want == SEG_SAFE && kind_at(f, cur) == SEG_SAFE && best < SEG_STEP * 0.4f) desired = 0.0f;
    /* About to touch down: never sweep red under the ball by accident. */
    if (H.ball_v > 0.0f && dist < 0.18f && want != SEG_DEADLY) {
        float next = pa_wrapf(cur - desired * dt, PA_TAU);
        if (kind_at(f, next) == SEG_DEADLY && kind_at(f, cur) != SEG_DEADLY) desired = 0.0f;
    }
    H.bot_vel = pa_approach(H.bot_vel, desired, 40.0f, dt);
    H.spin += H.bot_vel * dt;
}

/* ------------------------------------------------------------------ update */
static void update_parts(float dt) {
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *p = &H.parts[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        if (p->kind == P_CONFETTI) {
            p->vy += 900.0f * L.u * dt;
            p->vx *= expf(-1.8f * dt);
            p->vy *= expf(-1.6f * dt);
        } else if (p->kind == P_EMBER) {
            p->vy -= 120.0f * L.u * dt;
        } else {
            p->vy += 1500.0f * L.u * dt;
        }
        p->x += p->vx * dt;
        p->z += p->vz * dt;
        p->wy += p->vy * dt;
        p->rot += p->vr * dt;
    }
}

typedef struct { float x, y, w, h; } Rect;
static Rect card_button(int which) {
    /* 0: continue / next level, 1: retry text line */
    Rect r;
    r.w = 300.0f * L.u; r.h = 82.0f * L.u;
    r.x = L.cx - r.w * 0.5f;
    r.y = (float)L.h * 0.66f;
    if (which == 1) { r.y = (float)L.h * 0.66f + 120.0f * L.u; r.h = 60.0f * L.u; }
    return r;
}
static int in_rect(Rect r, float x, float y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

static void retry(void) {
    begin_level(H.level, 0);
}

static void do_continue(void) {
    g_coins -= CONTINUE_COST;
    H.continued = 1;
    Floor *f = &H.floors[H.death_floor];
    shatter(f, 46, 1.0f);
    H.passed++;
    H.state = ST_PLAY;
    H.state_t = 0.0f;
    H.ball_v = 1.0f;
    H.combo = 0;
    pa_sfx("coin");
    save_progress();
}

static void helix_update(float dt, const PA_Input *in) {
    if (L.w == 0) compute_layout(540, 1170);
    H.time += dt;
    H.state_t += dt;
    if (H.shake > 0.0f) H.shake = H.shake > dt * 3.2f ? H.shake - dt * 3.2f : 0.0f;
    H.land_t += dt;
    for (int i = 0; i < MAX_POPS; i++) if (H.pops[i].t > 0.0f) H.pops[i].t -= dt * 1.25f;
    for (int i = 0; i < MAX_WORDS; i++) if (H.words[i].t > 0.0f) H.words[i].t -= dt * 0.8f;
    {
        float ft = H.smashing ? 1.0f : (H.combo >= 2 && H.ball_v > 0.0f ? 0.4f : 0.0f);
        H.fire_t = pa_approach(H.fire_t, ft, ft > H.fire_t ? 30.0f : 2.5f, dt);
    }
    update_parts(dt);
    for (int i = 0; i < H.floor_count; i++) {
        Floor *f = &H.floors[i];
        if (f->broken > 0.0f) f->broken = f->broken > dt * 2.0f ? f->broken - dt * 2.0f : 0.0f;
        if (f->hit > 0.0f) f->hit = f->hit > dt * 6.0f ? f->hit - dt * 6.0f : 0.0f;
        for (int k = 0; k < f->splat_count; k++) f->splat_t[k] += dt;
    }
    if (H.coins_shown < g_coins && (H.state != ST_WIN || H.state_t > 1.0f)) {
        int step = (g_coins - H.coins_shown) / 12 + 1;
        H.coins_shown += step;
        if ((int)(H.time * 30.0f) % 3 == 0) pa_tone(1320, 1760, 0.03f, 1, 0.04f);
    }
    if (H.coins_shown > g_coins) H.coins_shown = g_coins;

    int bot = demo() != 0;

    /* Results cards. */
    if (H.state == ST_DEAD) {
        if (H.state_t > 0.8f) { H.state = ST_FAIL; H.state_t = 0.0f; pa_sfx("lose"); }
        return;
    }
    if (H.state == ST_FAIL) {
        int can_continue = !H.continued && g_coins >= CONTINUE_COST && H.state_t < CONTINUE_TIME;
        int tap = in->tapped, tx = (int)in->x, ty = (int)in->y;
        if (bot) { tap = 0; }
        if (tap && H.state_t > 0.4f) {
            if (can_continue && in_rect(card_button(0), (float)tx, (float)ty)) do_continue();
            else if (!can_continue || in_rect(card_button(1), (float)tx, (float)ty)) retry();
        }
        return;
    }
    if (H.state == ST_WIN) {
        /* Ball keeps bouncing on the goal disc behind the card. */
        H.ball_v += GRAVITY * dt;
        H.ball_y += H.ball_v * dt;
        float gy = (float)H.floor_count + 1.0f;
        if (H.ball_y >= gy) { H.ball_y = gy; H.ball_v = -BOUNCE_V * 0.8f; H.land_t = 0.0f; }
        int go = in->tapped && H.state_t > 0.6f;
        if (bot) go = H.state_t > 2.6f;
        if (go) {
            H.demo_runs++;
            begin_level(H.level + 1, H.score);
            pa_sfx("select");
        }
        return;
    }

    /* Spin: direct drag with inertia after release. */
    if (!bot) {
        if (in->pressed) { H.dragging = 1; H.drag_x = in->x; if (H.state == ST_READY) { H.state = ST_PLAY; H.state_t = 0.0f; } }
        if (in->down && H.dragging > 0) {
            float delta = (in->x - H.drag_x) * 0.0105f / (L.u > 0.3f ? L.u : 0.3f);
            H.spin += delta;
            H.spin_vel = delta / (dt > 0.0001f ? dt : 0.0001f);
            H.drag_x = in->x;
        }
        if (in->released) H.dragging = -1;
        if (H.dragging < 0) {
            H.spin += H.spin_vel * dt;
            H.spin_vel *= expf(-5.0f * dt);
            if (fabsf(H.spin_vel) < 0.02f) H.spin_vel = 0.0f;
        }
        float key = 0.0f;
        if (in->keys[PA_KEY_LEFT])  key -= 1.0f;
        if (in->keys[PA_KEY_RIGHT]) key += 1.0f;
        if (key != 0.0f) {
            H.spin += key * 2.6f * dt; H.spin_vel = key * 2.6f;
            if (H.state == ST_READY) { H.state = ST_PLAY; H.state_t = 0.0f; }
        }
    } else {
        if (H.state == ST_READY && H.state_t > (H.demo_runs ? 0.5f : 1.3f)) { H.state = ST_PLAY; H.state_t = 0.0f; }
        if (H.state == ST_PLAY) bot_update(dt);
    }

    /* Fall. */
    float prev_y = H.ball_y;
    H.ball_v += GRAVITY * dt;
    if (H.ball_v > MAX_FALL) H.ball_v = MAX_FALL;
    H.ball_y += H.ball_v * dt;

    if (H.smashing && H.ball_v > 2.0f) {
        /* Embers shed off the fireball. */
        if ((int)(H.time * 120.0f) % 2 == 0) {
            Part *p = new_part();
            float rm = (L.inner + L.R) * 0.5f;
            float j = sinf(H.time * 91.0f) * L.ball_r * 0.7f;
            p->kind = P_EMBER;
            p->x = j; p->z = rm;
            p->wy = H.ball_y * L.spacing - L.ball_r * 1.2f;
            p->vx = j * 2.0f; p->vy = -60.0f * L.u;
            p->max = p->life = 0.35f;
            p->size = L.ball_r * (0.25f + 0.15f * fabsf(sinf(H.time * 37.0f)));
            p->col = PA_RGB(255, 190, 40);
        }
    }

    /* Continuous collision against every floor crossed this step. */
    if (H.ball_v > 0.0f) {
        for (int i = 0; i < H.floor_count; i++) {
            Floor *f = &H.floors[i];
            if (f->gone || f->y <= prev_y || f->y > H.ball_y) continue;
            int kind = kind_at(f, under_angle());

            if (kind == SEG_GAP) {
                H.combo++;
                H.passed++;
                int gain = (H.level + 2) * H.combo;
                H.score += gain;
                pop_score(gain);
                g_coins += 1;
                shatter(f, 34, 0.8f);
                H.shake = H.shake > 0.25f ? H.shake : 0.25f;
                pa_tone(380.0f + 90.0f * (float)H.combo, 620.0f + 120.0f * (float)H.combo, 0.10f, 1, 0.08f);
                if (H.combo == 3) {
                    H.smashing = 1;
                    word("GREAT!", pa_hex(0x7CFF3A));
                    pa_tone(160, 520, 0.35f, 3, 0.07f);
                    pa_noise(0.25f, 0.06f);
                } else if (H.combo == 4) word("WOW!", pa_hex(0x7CFF3A));
                else if (H.combo == 5) word("AMAZING!", pa_hex(0x7CFF3A));
                else if (H.combo >= 6) word("GODLIKE!", pa_hex(0x7CFF3A));
                if (bot) H.bot_bounces[i] = 0;
                continue;
            }

            if (H.smashing) {
                /* The fireball goes through red as happily as through safe. */
                int gain = (H.level + 2) * (H.combo + 2);
                H.score += gain;
                pop_score(gain);
                g_coins += 3;
                shatter(f, 52, 1.25f);
                H.shake = 1.0f;
                H.passed++;
                H.smashing = 0;
                H.combo = 0;
                H.ball_v *= 0.35f;
                word(kind == SEG_DEADLY ? "SMASH!" : "CRUSH!", pa_hex(0xFFB300));
                pa_sfx("boom");
                continue;
            }

            if (kind == SEG_DEADLY) {
                H.ball_y = f->y;
                H.ball_v = 0.0f;
                H.land_t = 0.0f;
                die(i);
                return;
            }

            land_bounce(f);
            if (bot) {
                if (H.bot_floor != i) { H.bot_floor = i; }
                H.bot_bounces[i]++;
            }
            break;
        }
    }

    /* Cleared the tower: the goal disc. */
    if (H.ball_y >= (float)H.floor_count + 1.0f && H.state == ST_PLAY) {
        H.ball_y = (float)H.floor_count + 1.0f;
        H.smashing = 0;
        win();
    }

    /* The camera only ever follows the ball down. */
    float target = H.ball_y > H.cam_y ? H.ball_y : H.cam_y;
    H.cam_y = pa_approach(H.cam_y, target, 10.0f, dt);
}

/* ---------------------------------------------------------------- drawing */
static int arc_steps(float a0, float a1) {
    int n = (int)((a1 - a0) / (PA_TAU / 90.0f)) + 2;
    return n > 40 ? 40 : n;
}

static void band(PA_Canvas *c, float cx, float cy, float ri, float ro,
                 float a0, float a1, const PA_Paint *p) {
    PA_Vec2 pts[84];
    int n = 0, st = arc_steps(a0, a1);
    for (int i = 0; i <= st; i++) {
        float a = a0 + (a1 - a0) * ((float)i / (float)st);
        pts[n].x = cx + cosf(a) * ro; pts[n].y = cy + sinf(a) * ro * L.squash; n++;
    }
    for (int i = st; i >= 0; i--) {
        float a = a0 + (a1 - a0) * ((float)i / (float)st);
        pts[n].x = cx + cosf(a) * ri; pts[n].y = cy + sinf(a) * ri * L.squash; n++;
    }
    pa_fill_poly_paint(c, pts, n, p);
}
static void band_col(PA_Canvas *c, float cx, float cy, float ri, float ro,
                     float a0, float a1, PA_Color col) {
    PA_Paint p = pa_flat(col);
    band(c, cx, cy, ri, ro, a0, a1, &p);
}

/** Outer curtain of a near-side piece: the platform's visible thickness. */
static void skirt(PA_Canvas *c, float cx, float cy, float ro, float a0, float a1,
                  float t, const PA_Paint *p) {
    PA_Vec2 pts[84];
    int n = 0, st = arc_steps(a0, a1);
    for (int i = 0; i <= st; i++) {
        float a = a0 + (a1 - a0) * ((float)i / (float)st);
        pts[n].x = cx + cosf(a) * ro; pts[n].y = cy + sinf(a) * ro * L.squash; n++;
    }
    for (int i = st; i >= 0; i--) {
        float a = a0 + (a1 - a0) * ((float)i / (float)st);
        pts[n].x = cx + cosf(a) * ro; pts[n].y = cy + sinf(a) * ro * L.squash + t; n++;
    }
    pa_fill_poly_paint(c, pts, n, p);
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

static void splat_decal(PA_Canvas *c, float cx, float cy, float a, float rm, float k,
                        int seed, float age, PA_Color col) {
    float px = cx + cosf(a) * rm, py = cy + sinf(a) * rm * L.squash;
    /* Pops in: overshoots to 1.2 then settles, the reference's paint slap. */
    float pop = age < 0.06f ? 0.4f + age / 0.06f * 0.8f
              : age < 0.20f ? 1.2f - (age - 0.06f) / 0.14f * 0.2f : 1.0f;
    float r = L.ball_r * 1.1f * k * pop;
    pa_fill_ellipse(c, px, py, r, r * L.squash * 1.25f, col);
    PA_Rng rng;
    pa_rng_seed(&rng, (uint32_t)seed * 2654435761u + 17u);
    for (int i = 0; i < 9; i++) {
        float da = pa_rng_range(&rng, 0.0f, PA_TAU);
        float d = r * pa_rng_range(&rng, 0.9f, 1.8f);
        float s = r * pa_rng_range(&rng, 0.10f, 0.34f);
        float ex = px + cosf(da) * d, ey = py + sinf(da) * d * L.squash;
        pa_fill_ellipse(c, ex, ey, s, s * L.squash * 1.3f, col);
        if (i < 4) pa_line(c, px + cosf(da) * r * 0.7f, py + sinf(da) * r * 0.7f * L.squash,
                           ex, ey, s * 0.9f, col);
    }
    /* Wet sheen. */
    pa_fill_ellipse(c, px - r * 0.25f, py - r * 0.12f, r * 0.35f, r * 0.12f,
                    pa_alpha(pa_shade(col, 0.5f), (float)PA_A(col) / 255.0f * 0.5f));
}

typedef struct { float a0, a1; int kind; } Run;

static int ring_runs(const Floor *f, Run *out, int goal) {
    if (goal) { out[0].a0 = 0.0f; out[0].a1 = PA_TAU; out[0].kind = SEG_SAFE; return 1; }
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
            out[n].a0 = (float)(start + k) * SEG_STEP;
            out[n].a1 = (float)(start + k + len) * SEG_STEP;
            out[n].kind = kind;
            n++;
        }
        k += len;
    }
    return n;
}

typedef struct { float a0, a1; int kind; } Piece;

/** Cut runs (screen-frame angles) at the horizon, keeping this side's pieces. */
static int cut_pieces(const Run *runs, int n, float spin, int near, Piece *piece) {
    int pc = 0;
    for (int r = 0; r < n; r++) {
        float a = runs[r].a0 + spin, end = runs[r].a1 + spin;
        while (a < end - 1e-5f) {
            float next = (floorf(a / PA_PI) + 1.0f) * PA_PI;
            if (next > end) next = end;
            int is_near = sinf((a + next) * 0.5f) > 0.0f;
            if (is_near == near) {
                float lap = 0.03f;
                piece[pc].a0 = (near && a > runs[r].a0 + spin + 1e-4f) ? a - lap : a;
                piece[pc].a1 = (near && next < end - 1e-4f) ? next + lap : next;
                piece[pc].kind = runs[r].kind;
                pc++;
            }
            a = next;
        }
    }
    return pc;
}

static PA_Color hazard_col(void) {
    float pulse = 0.5f + 0.5f * sinf(H.time * PA_TAU);
    return pa_shade(pa_hex(HAZARD), 0.10f * pulse);
}

/* Material detail on a top face, in tower-frame positions so it turns with
   the tower: sponge pores, stone speckle and cracks, candy stripes, glossy rim. */
static void floor_texture(PA_Canvas *c, const Floor *f, int idx, float cx, float cy,
                          float ri, float ro, float k, int near, float alpha, const Piece *pc, int npc) {
    PA_Color mark = pa_hex(H.th.plate_mark);
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)idx * 7919u + 13u);
    switch (H.th.material) {
    case MAT_SPONGE:
        for (int i = 0; i < 70; i++) {
            float a = pa_rng_range(&r, 0.0f, PA_TAU);
            float rad = pa_lerpf(ri * 1.18f, ro * 0.95f, pa_rng_next(&r));
            float s = pa_rng_range(&r, 0.014f, 0.036f) * L.R * k;
            if (kind_at(f, a) != SEG_SAFE || (f->slider && fabsf(ang_diff(a, slider_angle(f))) < f->slide_w)) continue;
            float sa = a + H.spin;
            if ((sinf(sa) > 0.0f) != near) continue;
            float px = cx + cosf(sa) * rad, py = cy + sinf(sa) * rad * L.squash;
            pa_fill_ellipse(c, px, py, s, s * L.squash * 1.3f, pa_alpha(mark, alpha * 0.85f));
            pa_fill_ellipse(c, px, py + s * 0.35f, s * 0.8f, s * L.squash * 0.7f,
                            pa_alpha(pa_shade(mark, -0.25f), alpha * 0.7f));
        }
        break;
    case MAT_STONE:
        for (int i = 0; i < 90; i++) {
            float a = pa_rng_range(&r, 0.0f, PA_TAU);
            float rad = pa_lerpf(ri * 1.1f, ro * 0.97f, pa_rng_next(&r));
            float s = pa_rng_range(&r, 1.0f, 2.6f) * L.u * k;
            int light = pa_rng_chance(&r, 0.45f);
            if (kind_at(f, a) == SEG_GAP) continue;
            float sa = a + H.spin;
            if ((sinf(sa) > 0.0f) != near) continue;
            pa_fill_circle(c, cx + cosf(sa) * rad, cy + sinf(sa) * rad * L.squash, s,
                           light ? PA_RGBA(255, 255, 255, (int)(70 * alpha)) : PA_RGBA(0, 0, 0, (int)(55 * alpha)));
        }
        for (int i = 0; i < 4; i++) {
            float a = pa_rng_range(&r, 0.0f, PA_TAU);
            if (kind_at(f, a) != SEG_SAFE) continue;
            PA_Vec2 pts[5];
            float rad = ri * 1.2f;
            for (int j = 0; j < 5; j++) {
                float aa = a + H.spin + pa_rng_range(&r, -0.05f, 0.05f);
                pts[j].x = cx + cosf(aa) * rad;
                pts[j].y = cy + sinf(aa) * rad * L.squash;
                rad += (ro - ri) * 0.17f;
            }
            if ((sinf(a + H.spin) > 0.0f) != near) continue;
            pa_stroke_poly(c, pts, 5, 0, 1.6f * k, PA_RGBA(0, 0, 0, (int)(60 * alpha)));
        }
        break;
    case MAT_CANDY:
        for (int s = 0; s < SEGMENTS * 2; s++) {
            float a0 = (float)s * SEG_STEP * 0.5f, a1 = a0 + SEG_STEP * 0.2f;
            if (kind_at(f, (a0 + a1) * 0.5f) != SEG_SAFE) continue;
            float sa = (a0 + a1) * 0.5f + H.spin;
            if ((sinf(sa) > 0.0f) != near) continue;
            band_col(c, cx, cy, ri * 1.05f, ro * 0.985f, a0 + H.spin, a1 + H.spin,
                     pa_alpha(mark, alpha * 0.55f));
        }
        break;
    default: break;
    }
    /* Glossy rim catching the light, every material. */
    for (int i = 0; i < npc; i++) {
        float gl = H.th.material == MAT_GLOSS ? 0.55f : 0.25f;
        band_col(c, cx, cy, ro * 0.94f, ro * 0.985f, pc[i].a0, pc[i].a1,
                 PA_RGBA(255, 255, 255, (int)(gl * 255.0f * alpha * (near ? 0.6f : 1.0f))));
    }
}

static void spike(PA_Canvas *c, float x, float y, float bw, float h, float alpha) {
    PA_Vec2 l[3] = { { x - bw, y }, { x, y - h }, { x, y + bw * 0.25f } };
    PA_Vec2 r[3] = { { x, y + bw * 0.25f }, { x, y - h }, { x + bw, y } };
    pa_fill_poly(c, l, 3, pa_alpha(pa_shade(pa_hex(HAZARD), 0.12f), alpha));
    pa_fill_poly(c, r, 3, pa_alpha(pa_shade(pa_hex(HAZARD), -0.30f), alpha));
}

/** One half of a floor at screen centre (cx, cy) and perspective scale k. */
static void draw_half(PA_Canvas *c, const Floor *f, int idx, float cx, float cy, float k,
                      int near, int goal) {
    float b = f->broken;
    int shatter_anim = f->gone;
    float grow = shatter_anim ? 1.0f + (1.0f - b) * 0.55f : 1.0f;
    float alpha = shatter_anim ? pa_clamp01(b * 1.4f) : 1.0f;
    float flash = shatter_anim ? pa_clamp01((b - 0.4f) / 0.6f) * 0.85f : 0.0f;
    if (shatter_anim) cy += (1.0f - b) * (1.0f - b) * L.spacing * 0.35f * k;
    float ro = L.R * k * grow, ri = L.inner * k * (shatter_anim ? grow : 1.0f);
    float t = L.thick * k * (1.0f - f->hit * 0.3f);
    if (f->hit > 0.0f) cy += f->hit * L.thick * 0.25f * k;

    Run runs[SEGMENTS];
    int n = ring_runs(f, runs, goal);
    Piece piece[SEGMENTS * 3];
    int pc = cut_pieces(runs, n, H.spin, near, piece);
    PA_Color plate = pa_hex(H.th.plate);
    PA_Color haz = hazard_col();

    /* Sides: the outer curtain on the near side, end faces where runs meet gaps. */
    for (int i = 0; near && i < pc; i++) {
        PA_Color base = piece[i].kind == SEG_DEADLY ? haz : plate;
        base = pa_mix(base, PA_RGB(255, 255, 255), flash);
        PA_Paint p = pa_linear(0, cy, 0, cy + ro * L.squash + t);
        pa_stop(&p, 0.0f, pa_alpha(pa_shade(base, -0.22f), alpha));
        pa_stop(&p, 1.0f, pa_alpha(pa_shade(base, -0.42f), alpha));
        skirt(c, cx, cy, ro, piece[i].a0, piece[i].a1, t, &p);
    }
    for (int r = 0; r < n; r++) {
        if (runs[r].a1 - runs[r].a0 >= PA_TAU - 1e-4f) break;
        PA_Color base = runs[r].kind == SEG_DEADLY ? haz : plate;
        PA_Color side = pa_alpha(pa_mix(pa_shade(base, -0.48f), PA_RGB(255, 255, 255), flash), alpha);
        float s = runs[r].a0 + H.spin, e = runs[r].a1 + H.spin;
        if ((sinf(s) > 0.0f) == near && cosf(s) < 0.0f) cap(c, cx, cy, ri, ro, s, t, side);
        if ((sinf(e) > 0.0f) == near && cosf(e) > 0.0f) cap(c, cx, cy, ri, ro, e, t, side);
    }

    /* Tops: +15% at the back, falling off toward the camera. */
    for (int i = 0; i < pc; i++) {
        PA_Color base = piece[i].kind == SEG_DEADLY ? haz : plate;
        if (goal) base = pa_shade(plate, 0.05f);
        base = pa_mix(base, PA_RGB(255, 255, 255), flash);
        PA_Paint p = pa_linear(0, cy - ro * L.squash, 0, cy + ro * L.squash);
        pa_stop(&p, 0.0f, pa_alpha(pa_shade(base, 0.16f), alpha));
        pa_stop(&p, 0.6f, pa_alpha(base, alpha));
        pa_stop(&p, 1.0f, pa_alpha(pa_shade(base, -0.06f), alpha));
        band(c, cx, cy, ri, ro, piece[i].a0, piece[i].a1, &p);
    }

    if (goal) {
        const int CELLS = 18;
        for (int row = 0; row < 3; row++) {
            float r0 = pa_lerpf(ri, ro, (float)row / 3.0f), r1 = pa_lerpf(ri, ro, (float)(row + 1) / 3.0f);
            for (int q = row & 1; q < CELLS; q += 2) {
                float a0 = H.spin + (float)q / (float)CELLS * PA_TAU;
                float a1 = a0 + PA_TAU / (float)CELLS;
                if ((sinf((a0 + a1) * 0.5f) > 0.0f) != near) continue;
                band_col(c, cx, cy, r0, r1, a0, a1, PA_RGBA(255, 255, 255, 220));
            }
        }
    } else if (!shatter_anim || b > 0.5f) {
        floor_texture(c, f, idx, cx, cy, ri, ro, k, near, alpha, piece, pc);
    }

    /* Ambient occlusion where the platform meets the column. */
    for (int i = 0; i < pc; i++) {
        float w = ro - ri;
        band_col(c, cx, cy, ri, ri + w * 0.07f, piece[i].a0, piece[i].a1, PA_RGBA(0, 0, 0, (int)(46 * alpha)));
        band_col(c, cx, cy, ri + w * 0.07f, ri + w * 0.16f, piece[i].a0, piece[i].a1, PA_RGBA(0, 0, 0, (int)(24 * alpha)));
        band_col(c, cx, cy, ri + w * 0.16f, ri + w * 0.27f, piece[i].a0, piece[i].a1, PA_RGBA(0, 0, 0, (int)(10 * alpha)));
    }

    /* Hazard glow: the red rim breathes at 1 Hz. */
    float pulse = 0.5f + 0.5f * sinf(H.time * PA_TAU);
    for (int i = 0; i < pc; i++) {
        if (piece[i].kind != SEG_DEADLY) continue;
        band_col(c, cx, cy, ro * 0.90f, ro, piece[i].a0, piece[i].a1,
                 PA_RGBA(255, 200, 190, (int)((40 + 60 * pulse) * alpha)));
    }

    /* Spikes on boss hazards. */
    if (H.th.boss && !goal) {
        for (int s = 0; s < SEGMENTS; s++) {
            if (f->seg[s] != SEG_DEADLY) continue;
            float sa = ((float)s + 0.5f) * SEG_STEP + H.spin;
            if ((sinf(sa) > 0.0f) != near) continue;
            float rad = ro * 0.80f;
            spike(c, cx + cosf(sa) * rad, cy + sinf(sa) * rad * L.squash,
                  L.R * k * 0.07f, L.R * k * 0.22f, alpha);
        }
    }

    /* The sliding red block, raised off the ring. */
    if (f->slider && !goal) {
        float lift = L.thick * k * 0.55f;
        float sc = slider_angle(f) + H.spin;
        Run sr = { sc - f->slide_w * 0.5f - H.spin, sc + f->slide_w * 0.5f - H.spin, SEG_DEADLY };
        Piece sp[3];
        int spc = cut_pieces(&sr, 1, H.spin, near, sp);
        float ty = cy - lift;
        for (int i = 0; near && i < spc; i++) {
            PA_Paint p = pa_flat(pa_alpha(pa_shade(haz, -0.35f), alpha));
            skirt(c, cx, ty, ro * 0.97f, sp[i].a0, sp[i].a1, lift + t * 0.4f, &p);
        }
        float s0 = sc - f->slide_w * 0.5f, s1 = sc + f->slide_w * 0.5f;
        PA_Color side = pa_alpha(pa_shade(haz, -0.48f), alpha);
        if ((sinf(s0) > 0.0f) == near && cosf(s0) < 0.0f) cap(c, cx, ty, ri, ro * 0.97f, s0, lift, side);
        if ((sinf(s1) > 0.0f) == near && cosf(s1) > 0.0f) cap(c, cx, ty, ri, ro * 0.97f, s1, lift, side);
        for (int i = 0; i < spc; i++) {
            PA_Paint p = pa_linear(0, ty - ro * L.squash, 0, ty + ro * L.squash);
            pa_stop(&p, 0.0f, pa_alpha(pa_shade(haz, 0.18f), alpha));
            pa_stop(&p, 1.0f, pa_alpha(haz, alpha));
            band(c, cx, ty, ri, ro * 0.97f, sp[i].a0, sp[i].a1, &p);
            band_col(c, cx, ty, ro * 0.88f, ro * 0.97f, sp[i].a0, sp[i].a1,
                     PA_RGBA(255, 210, 200, (int)((50 + 70 * pulse) * alpha)));
        }
        if (H.th.boss && (sinf(sc) > 0.0f) == near) {
            float rad = ro * 0.78f;
            spike(c, cx + cosf(sc) * rad, ty + sinf(sc) * rad * L.squash, L.R * k * 0.07f, L.R * k * 0.22f, alpha);
        }
    }

    /* Paint splats, popping in when fresh. */
    float rm = (ri + ro) * 0.5f;
    for (int q = 0; q < f->splat_count; q++) {
        float a = f->splat_a[q] + H.spin;
        if ((sinf(a) > 0.0f) != near) continue;
        if (kind_at(f, f->splat_a[q]) == SEG_GAP) continue;
        splat_decal(c, cx, cy, a, rm, k, f->splat_seed[q], f->splat_t[q],
                    pa_alpha(pa_shade(pa_hex(H.th.ball), -0.04f), alpha * 0.95f));
    }
}

/* Pillar between screen depths d0 (top) and d1 (bottom), as one polygon whose
   sides follow the perspective, then the column's own texture. */
static void pillar(PA_Canvas *c, float cx, float d0, float d1, int shadow_top) {
    if (d1 <= d0) return;
    float y0 = proj_y(d0), y1 = proj_y(d1);
    if (y1 < -4.0f || y0 > (float)c->h + 4.0f) return;
    const int ST = 6;
    PA_Vec2 pts[2 * (ST + 1)];
    for (int i = 0; i <= ST; i++) {
        float d = pa_lerpf(d0, d1, (float)i / (float)ST);
        float k = persp_k(d), y = proj_y(d);
        pts[i].x = cx + L.inner * k; pts[i].y = y;
        pts[2 * ST + 1 - i].x = cx - L.inner * k; pts[2 * ST + 1 - i].y = y;
    }
    pts[0].y -= 1.0f; pts[2 * ST + 1].y -= 1.0f;
    pts[ST].y += 1.0f; pts[ST + 1].y += 1.0f;
    float km = persp_k((d0 + d1) * 0.5f);
    float hw = L.inner * km;
    PA_Color base = pa_hex(H.th.pillar);
    PA_Paint p = pa_linear(cx - hw, 0, cx + hw, 0);
    pa_stop(&p, 0.0f, pa_shade(base, -0.24f));
    pa_stop(&p, 0.22f, pa_shade(base, -0.02f));
    pa_stop(&p, 0.40f, pa_shade(base, 0.10f));
    pa_stop(&p, 0.70f, pa_shade(base, -0.05f));
    pa_stop(&p, 1.0f, pa_shade(base, -0.30f));
    pa_fill_poly_paint(c, pts, 2 * (ST + 1), &p);

    PA_Color mark = pa_hex(H.th.pillar_mark);
    float ys = y0 < -2.0f ? -2.0f : y0, ye = y1 > (float)c->h + 2.0f ? (float)c->h + 2.0f : y1;
    switch (H.th.pillar_style) {
    case COL_GROOVES:
    case COL_BRICK: {
        /* Vertical joints turning with the tower: they are the spin feedback. */
        int nj = H.th.pillar_style == COL_BRICK ? 10 : 8;
        for (int j = 0; j < nj; j++) {
            float a = (float)j / (float)nj * PA_TAU + H.spin;
            float s = sinf(a);
            if (s < 0.08f) continue;
            float xr = cosf(a);
            PA_Vec2 q[4] = {
                { cx + xr * L.inner * persp_k(d0) - 1.2f * s, ys }, { cx + xr * L.inner * persp_k(d0) + 1.2f * s, ys },
                { cx + xr * L.inner * persp_k(d1) + 1.2f * s, ye }, { cx + xr * L.inner * persp_k(d1) - 1.2f * s, ye },
            };
            pa_fill_poly(c, q, 4, pa_alpha(mark, 0.55f * s));
        }
        if (H.th.pillar_style == COL_BRICK) {
            float stepd = L.spacing * 0.25f;
            float wd0 = d0 + H.cam_y * L.spacing;
            float first = ceilf(wd0 / stepd) * stepd - H.cam_y * L.spacing;
            for (float d = first; d < d1; d += stepd) {
                float k = persp_k(d), y = proj_y(d);
                PA_Vec2 arc[13];
                for (int i = 0; i <= 12; i++) {
                    float a = PA_PI * (float)i / 12.0f;
                    arc[i].x = cx + cosf(a) * L.inner * k;
                    arc[i].y = y + sinf(a) * L.inner * k * L.squash * 0.35f;
                }
                pa_stroke_poly(c, arc, 13, 0, 1.6f, pa_alpha(mark, 0.6f));
            }
        }
    } break;
    case COL_BANDS: {
        float stepd = L.spacing * 0.5f;
        float wd0 = d0 + H.cam_y * L.spacing + H.spin * 8.0f;
        float first = ceilf(wd0 / stepd) * stepd - H.cam_y * L.spacing - H.spin * 8.0f;
        for (float d = first - stepd; d < d1; d += stepd) {
            float da = d < d0 ? d0 : d, db = d + stepd * 0.38f > d1 ? d1 : d + stepd * 0.38f;
            if (db <= da) continue;
            PA_Vec2 q[26];
            for (int i = 0; i <= 12; i++) {
                float a = PA_PI * (float)i / 12.0f;
                float k = persp_k(da);
                q[i].x = cx + cosf(a) * L.inner * k;
                q[i].y = proj_y(da) + sinf(a) * L.inner * k * L.squash * 0.35f;
            }
            for (int i = 12; i >= 0; i--) {
                float a = PA_PI * (float)i / 12.0f;
                float k = persp_k(db);
                q[25 - i].x = cx + cosf(a) * L.inner * k;
                q[25 - i].y = proj_y(db) + sinf(a) * L.inner * k * L.squash * 0.35f;
            }
            pa_fill_poly(c, q, 26, pa_alpha(mark, 0.75f));
        }
    } break;
    default: break;
    }

    /* Contact shadow the ring above casts down the column. */
    if (shadow_top) {
        float sh = L.spacing * 0.32f * km;
        PA_Paint s = pa_linear(0, y0, 0, y0 + sh);
        pa_stop(&s, 0.0f, PA_RGBA(0, 0, 0, 60));
        pa_stop(&s, 1.0f, PA_RGBA(0, 0, 0, 0));
        float yy = y0 + sh > y1 ? y1 - y0 : sh;
        if (yy > 1.0f) pa_fill_rect_paint(c, cx - hw * 0.98f, y0, hw * 1.96f, yy, &s);
    }
}

static void draw_ball(PA_Canvas *c, float cx) {
    float d = depth_of(H.ball_y);
    float k = persp_k(d);
    float rm = (L.inner + L.R) * 0.5f * k;
    float base = proj_y(d) + rm * L.squash;
    float br = L.ball_r * k;
    int boss = H.th.boss;
    PA_Color col = pa_mix(pa_hex(H.th.ball), pa_hex(0xFF6A00), H.fire_t);

    /* Contact shadow on the next surface below. */
    for (int i = 0; i < H.floor_count; i++) {
        Floor *f = &H.floors[i];
        if (f->gone || f->y < H.ball_y - 0.001f) continue;
        if (kind_at(f, under_angle()) == SEG_GAP) continue;
        float gap = f->y - H.ball_y;
        float kk = pa_clamp01(1.0f - gap * 1.2f);
        float df = depth_of(f->y), kf = persp_k(df);
        if (kk > 0.0f)
            pa_shadow(c, cx, proj_y(df) + rm / k * kf * L.squash, br * (0.7f + 0.4f * kk),
                      br * 0.34f, 0.32f * kk);
        break;
    }

    /* Squash on contact with an elastic recovery; stretch while falling fast. */
    float sq = expf(-H.land_t * 9.0f) * cosf(H.land_t * 34.0f);
    if (H.state == ST_DEAD || H.state == ST_FAIL) sq = 0.9f;
    float stretch = pa_clampf((H.ball_v - 3.0f) * 0.03f, 0.0f, 0.22f);
    float rx = br * (1.0f + 0.32f * sq - stretch * 0.5f);
    float ry = br * (1.0f - 0.30f * sq + stretch);
    float by = base - ry;

    /* Fall streak, or a flame trail when on fire. */
    if (H.fire_t > 0.02f) {
        float len = 0.25f * 540.0f * L.u * k * H.fire_t;
        float flick = 1.0f + 0.12f * sinf(H.time * 47.0f);
        PA_Vec2 tail[7];
        tail[0].x = cx - rx * 1.05f; tail[0].y = by;
        tail[1].x = cx - rx * 0.8f;  tail[1].y = by - len * 0.45f;
        tail[2].x = cx - rx * 0.30f; tail[2].y = by - len * 0.85f * flick;
        tail[3].x = cx + sinf(H.time * 31.0f) * rx * 0.2f; tail[3].y = by - len * flick;
        tail[4].x = cx + rx * 0.30f; tail[4].y = by - len * 0.8f;
        tail[5].x = cx + rx * 0.8f;  tail[5].y = by - len * 0.5f * flick;
        tail[6].x = cx + rx * 1.05f; tail[6].y = by;
        PA_Paint fp = pa_linear(0, by, 0, by - len);
        pa_stop(&fp, 0.0f, PA_RGBA(255, 210, 0, (int)(240 * H.fire_t)));
        pa_stop(&fp, 0.5f, PA_RGBA(255, 120, 0, (int)(200 * H.fire_t)));
        pa_stop(&fp, 1.0f, PA_RGBA(255, 74, 0, 0));
        pa_fill_poly_paint(c, tail, 7, &fp);
        PA_Vec2 core[3] = { { cx - rx * 0.55f, by }, { cx + rx * 0.55f, by }, { cx, by - len * 0.55f } };
        PA_Paint cp = pa_linear(0, by, 0, by - len * 0.55f);
        pa_stop(&cp, 0.0f, PA_RGBA(255, 250, 200, (int)(230 * H.fire_t)));
        pa_stop(&cp, 1.0f, PA_RGBA(255, 220, 60, 0));
        pa_fill_poly_paint(c, core, 3, &cp);
        PA_Paint glow = pa_radial(cx, by, br * 0.8f, br * 2.6f);
        pa_stop(&glow, 0.0f, PA_RGBA(255, 150, 30, (int)(150 * H.fire_t)));
        pa_stop(&glow, 1.0f, PA_RGBA(255, 120, 30, 0));
        pa_fill_ellipse_paint(c, cx, by, br * 2.6f, br * 2.6f, &glow);
    } else if (H.ball_v > 4.0f && H.state == ST_PLAY) {
        float len = pa_clampf((H.ball_v - 4.0f) * 0.15f, 0.0f, 1.0f) * br * 4.0f;
        PA_Vec2 tail[3] = { { cx - br * 0.8f, by }, { cx + br * 0.8f, by }, { cx, by - br - len } };
        PA_Paint tp = pa_linear(0, by, 0, by - br - len);
        pa_stop(&tp, 0.0f, pa_alpha(col, 0.5f));
        pa_stop(&tp, 1.0f, pa_alpha(col, 0.0f));
        pa_fill_poly_paint(c, tail, 3, &tp);
    }

    if (boss && H.fire_t < 0.5f) {
        /* The boss-level skin: an eyeball rolling its eye at the player. */
        PA_Paint bp = pa_radial(cx - rx * 0.3f, by - ry * 0.4f, br * 0.1f, br * 1.3f);
        pa_stop(&bp, 0.0f, PA_RGB(255, 255, 255));
        pa_stop(&bp, 0.6f, PA_RGB(236, 238, 244));
        pa_stop(&bp, 1.0f, PA_RGB(170, 176, 190));
        pa_fill_ellipse_paint(c, cx, by, rx, ry, &bp);
        float ex = cx + rx * 0.18f, ey = by - ry * 0.05f;
        pa_fill_ellipse(c, ex, ey, rx * 0.46f, ry * 0.46f, pa_hex(0x2C8FE0));
        pa_fill_ellipse(c, ex, ey, rx * 0.30f, ry * 0.30f, pa_hex(0x0D4A86));
        pa_fill_ellipse(c, ex, ey, rx * 0.20f, ry * 0.20f, PA_RGB(10, 12, 20));
        pa_fill_circle(c, ex - rx * 0.12f, ey - ry * 0.14f, br * 0.09f, PA_RGB(255, 255, 255));
        return;
    }
    PA_Paint ball = pa_radial(cx - rx * 0.34f, by - ry * 0.40f, br * 0.08f, br * 1.35f);
    pa_stop(&ball, 0.0f, pa_shade(col, 0.70f));
    pa_stop(&ball, 0.40f, col);
    pa_stop(&ball, 1.0f, pa_shade(col, -0.40f));
    pa_fill_ellipse_paint(c, cx, by, rx, ry, &ball);
    pa_fill_ellipse(c, cx - rx * 0.36f, by - ry * 0.42f, rx * 0.22f, ry * 0.15f, PA_RGBA(255, 255, 255, 200));
}

static void draw_parts(PA_Canvas *c, float cx, int screen_space) {
    for (int i = 0; i < MAX_PARTS; i++) {
        const Part *p = &H.parts[i];
        if (p->life <= 0.0f) continue;
        if ((p->kind == P_CONFETTI) != screen_space) continue;
        float a = pa_clamp01(p->life / (p->max * 0.45f));
        float sx, sy, k = 1.0f;
        if (screen_space) { sx = p->x; sy = p->wy; }
        else {
            float d = p->wy - H.cam_y * L.spacing;
            k = persp_k(d);
            sx = cx + p->x * k;
            sy = proj_y(d) + p->z * L.squash * k;
        }
        float s = p->size * k;
        if (p->kind == P_SHARD || p->kind == P_CONFETTI) {
            float ca = cosf(p->rot) * s, sa = sinf(p->rot) * s;
            float sq = p->kind == P_CONFETTI ? 0.55f + 0.45f * cosf(p->rot * 1.7f) : 1.0f;
            PA_Vec2 q[4] = {
                { sx - ca + sa * sq, sy - sa - ca * sq }, { sx + ca + sa * sq, sy + sa - ca * sq },
                { sx + ca - sa * sq, sy + sa + ca * sq }, { sx - ca - sa * sq, sy - sa + ca * sq },
            };
            pa_fill_poly(c, q, 4, pa_alpha(p->col, a));
            if (p->kind == P_SHARD) {
                PA_Vec2 h2[3] = { q[0], q[1], { sx, sy } };
                pa_fill_poly(c, h2, 3, pa_alpha(pa_shade(p->col, 0.3f), a));
            }
        } else if (p->kind == P_DROP) {
            pa_fill_circle(c, sx, sy, s, pa_alpha(p->col, a));
        } else {
            float lf = pa_clamp01(p->life / p->max);
            pa_fill_circle(c, sx, sy, s * (0.5f + 0.5f * lf),
                           pa_alpha(pa_mix(PA_RGB(255, 80, 0), p->col, lf), 0.85f * lf));
        }
    }
}

/* ------------------------------------------------------------------ scene */
static void backdrop(PA_Canvas *c) {
    PA_Paint bg = pa_linear(0, 0, 0, (float)c->h);
    pa_stop(&bg, 0.0f, pa_hex(H.th.sky_top));
    pa_stop(&bg, 1.0f, pa_hex(H.th.sky_bot));
    pa_fill_rect_paint(c, 0, 0, (float)c->w, (float)c->h, &bg);
    float w = (float)c->w, h = (float)c->h;
    float par = H.cam_y * L.spacing;

    if (H.th.backdrop == BACK_CITY) {
        /* Two skyline layers with parallax, pale far and darker near. */
        for (int layer = 0; layer < 2; layer++) {
            PA_Color col = pa_hex(layer ? H.th.skyline_near : H.th.skyline_far);
            float base = h * (layer ? 0.78f : 0.64f) - pa_wrapf(par * (layer ? 0.10f : 0.05f), h * 0.6f) * 0.0f;
            PA_Rng r;
            pa_rng_seed(&r, 91u + (uint32_t)layer * 17u);
            float x = -20.0f;
            while (x < w + 20.0f) {
                float bw = pa_rng_range(&r, 26.0f, 70.0f) * L.u;
                float bh = pa_rng_range(&r, 0.08f, layer ? 0.30f : 0.40f) * h;
                pa_fill_rect(c, x, base - bh, bw - 2.0f, h - base + bh, col);
                if (pa_rng_chance(&r, 0.3f))
                    pa_fill_rect(c, x + bw * 0.4f, base - bh - 18.0f * L.u, 4.0f * L.u, 18.0f * L.u, col);
                x += bw;
            }
            pa_fill_rect(c, 0, base, w, h - base, col);
        }
        PA_Paint fog = pa_linear(0, h * 0.4f, 0, h);
        pa_stop(&fog, 0.0f, pa_alpha(pa_hex(H.th.sky_bot), 0.0f));
        pa_stop(&fog, 1.0f, pa_alpha(pa_hex(H.th.sky_bot), 0.55f));
        pa_fill_rect_paint(c, 0, h * 0.4f, w, h * 0.6f, &fog);
    }
    PA_Color cloud = pa_alpha(pa_hex(H.th.cloud), H.th.backdrop == BACK_CITY ? 0.35f : 0.55f);
    float cw = 540.0f * L.u;
    for (int i = 0; i < 6; i++) {
        float cy = pa_wrapf((float)i * 0.21f * h - par * 0.12f, h * 1.3f) - h * 0.15f;
        float cx = w * ((i & 1) ? 0.84f : 0.14f) + sinf((float)i * 2.1f) * w * 0.06f;
        pa_fill_ellipse(c, cx, cy, cw * 0.26f, cw * 0.07f, cloud);
        pa_fill_ellipse(c, cx + cw * 0.08f, cy - cw * 0.04f, cw * 0.14f, cw * 0.07f, cloud);
        pa_fill_ellipse(c, cx - cw * 0.09f, cy - cw * 0.02f, cw * 0.10f, cw * 0.05f, cloud);
    }
}

static void draw_tower(PA_Canvas *c, float cx) {
    Floor goal;
    memset(&goal, 0, sizeof(goal));
    goal.y = (float)H.floor_count + 1.0f;

    int ball_drawn = 0;
    float bottom_d = depth_of(goal.y);
    /* Column below the goal disc, to the bottom of the screen. */
    {
        float d = bottom_d, dd = bottom_d;
        while (proj_y(dd) < (float)c->h + 10.0f && dd < bottom_d + 6000.0f) dd += L.spacing;
        pillar(c, cx, d, dd, 0);
    }
    for (int i = H.floor_count; i >= 0; i--) {
        Floor *f = i == H.floor_count ? &goal : &H.floors[i];
        if (!ball_drawn && f->y < H.ball_y - 0.001f) { draw_ball(c, cx); ball_drawn = 1; }

        float d = depth_of(f->y);
        float k = persp_k(d);
        float cy = proj_y(d);
        /* Depth of the next ring up that is still standing, or the screen top. */
        float above_d = -1e9f;
        for (int j = i - 1; j >= 0; j--)
            if (!(H.floors[j].gone && H.floors[j].broken <= 0.0f)) { above_d = depth_of(H.floors[j].y); break; }
        if (above_d < -1e8f) {
            above_d = d;
            while (proj_y(above_d) > -10.0f && above_d > d - 6000.0f) above_d -= L.spacing * 0.5f;
        }
        float reach = (L.R * L.squash + L.thick) * k * 1.6f + 6.0f;
        int visible = !(f->gone && f->broken <= 0.0f) && cy + reach > 0.0f && cy - reach < (float)c->h;
        if (visible) draw_half(c, f, i, cx, cy, k, 0, i == H.floor_count);
        pillar(c, cx, above_d, d, above_d > -1e8f && i > 0);
        if (visible) draw_half(c, f, i, cx, cy, k, 1, i == H.floor_count);
    }
    if (!ball_drawn) draw_ball(c, cx);
}

/* ---------------------------------------------------------------------- HUD */
static void coin_icon(PA_Canvas *c, float x, float y, float r) {
    pa_fill_circle(c, x, y + r * 0.12f, r, pa_hex(0xC98A00));
    pa_fill_circle(c, x, y, r, pa_hex(0xFFC21C));
    pa_fill_circle(c, x, y, r * 0.68f, pa_hex(0xFFD95A));
    pa_round_rect(c, x - r * 0.12f, y - r * 0.42f, r * 0.24f, r * 0.84f, r * 0.1f, pa_hex(0xE0A000));
    pa_fill_ellipse(c, x - r * 0.35f, y - r * 0.45f, r * 0.22f, r * 0.12f, PA_RGBA(255, 255, 255, 180));
}

static void coin_pill(PA_Canvas *c, float right, float cy, int value) {
    float u = L.u;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    float ts = 22.0f * u;
    float tw = pa_text_width(buf, ts, 1.0f * u);
    float h = 40.0f * u, w = tw + 62.0f * u;
    float x = right - w;
    pa_round_rect(c, x, cy - h * 0.5f, w, h, h * 0.5f, PA_RGBA(20, 30, 45, 120));
    coin_icon(c, x + h * 0.5f + 2.0f * u, cy, 14.0f * u);
    pa_text_bold(c, buf, x + h + 6.0f * u, cy - ts * 0.5f, ts, PA_RGB(255, 255, 255),
                 PA_RGBA(20, 30, 45, 160), PA_ALIGN_LEFT, 1.0f * u, 0.8f);
}

static void level_bar(PA_Canvas *c, float cy, float prog) {
    float u = L.u;
    float bw = 200.0f * u, r = 17.0f * u;
    float bx = L.cx - bw * 0.5f;
    pa_round_rect(c, bx, cy - 4.0f * u, bw, 8.0f * u, 4.0f * u, PA_RGBA(40, 44, 60, 55));
    if (prog > 0.0f)
        pa_round_rect(c, bx, cy - 4.0f * u, bw * pa_clamp01(prog), 8.0f * u, 4.0f * u, pa_hex(0xFFC21C));
    char buf[16];
    for (int s = 0; s < 2; s++) {
        float x = s ? bx + bw : bx;
        int lit = s == 0 || prog >= 1.0f;
        pa_fill_circle(c, x, cy + 2.0f * u, r, PA_RGBA(0, 0, 0, 40));
        pa_fill_circle(c, x, cy, r, lit ? pa_hex(0xFFC21C) : pa_hex(0x56585E));
        pa_fill_circle(c, x, cy - r * 0.25f, r * 0.72f, lit ? pa_hex(0xFFD552) : pa_hex(0x64666D));
        snprintf(buf, sizeof(buf), "%d", H.level + s);
        float ts = (H.level + s >= 10 ? 13.0f : 15.0f) * u;
        pa_text_bold(c, buf, x, cy - ts * 0.5f, ts, lit ? pa_hex(0x3A2A00) : PA_RGB(255, 255, 255),
                     lit ? pa_hex(0xFFD552) : pa_hex(0x56585E), PA_ALIGN_CENTER, 0.5f * u, 0.7f);
    }
}

static float elastic(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return 1.0f + powf(2.0f, -10.0f * t) * sinf((t - 0.075f) * PA_TAU / 0.3f);
}

static void hand_hint(PA_Canvas *c, float x, float y) {
    float u = L.u;
    float s = 1.0f * u;
    PA_Color skin = PA_RGB(255, 255, 255), line = PA_RGB(60, 64, 80);
    /* Curved double arrow under the hand. */
    PA_Vec2 arc[17];
    for (int i = 0; i <= 16; i++) {
        float a = PA_PI * (0.15f + 0.7f * (float)i / 16.0f);
        arc[i].x = L.cx - cosf(a) * 110.0f * u;
        arc[i].y = y + 70.0f * u + sinf(a) * 26.0f * u;
    }
    pa_stroke_poly(c, arc, 17, 0, 6.0f * u, PA_RGBA(255, 255, 255, 220));
    for (int e = 0; e < 2; e++) {
        PA_Vec2 a = arc[e ? 16 : 0];
        float dir = e ? 1.0f : -1.0f;
        PA_Vec2 tri[3] = { { a.x + dir * 14.0f * u, a.y - 4.0f * u }, { a.x - dir * 4.0f * u, a.y - 14.0f * u },
                           { a.x - dir * 2.0f * u, a.y + 10.0f * u } };
        pa_fill_poly(c, tri, 3, PA_RGBA(255, 255, 255, 220));
    }
    /* Hand: palm, index finger, thumb, outlined. */
    for (int pass = 0; pass < 2; pass++) {
        float o = pass == 0 ? 3.0f * u : 0.0f;
        PA_Color col = pass == 0 ? line : skin;
        pa_round_rect(c, x - 9.0f * s - o, y - 44.0f * s - o, 18.0f * s + 2 * o, 52.0f * s + 2 * o, 9.0f * s + o, col);
        pa_round_rect(c, x - 14.0f * s - o, y - 2.0f * s - o, 46.0f * s + 2 * o, 46.0f * s + 2 * o, 16.0f * s + o, col);
        pa_round_rect(c, x + 9.0f * s - o, y - 14.0f * s - o, 14.0f * s + 2 * o, 30.0f * s + 2 * o, 7.0f * s + o, col);
        pa_round_rect(c, x + 20.0f * s - o, y - 8.0f * s - o, 13.0f * s + 2 * o, 28.0f * s + 2 * o, 6.5f * s + o, col);
        pa_fill_ellipse(c, x - 16.0f * s, y + 14.0f * s, 10.0f * s + o, 16.0f * s + o, col);
    }
}

static void button(PA_Canvas *c, Rect r, PA_Color col, const char *label, float ts, int coin_cost) {
    pa_round_rect(c, r.x, r.y + 7.0f * L.u, r.w, r.h, r.h * 0.5f, pa_shade(col, -0.35f));
    PA_Paint p = pa_linear(0, r.y, 0, r.y + r.h);
    pa_stop(&p, 0.0f, pa_shade(col, 0.15f));
    pa_stop(&p, 1.0f, col);
    pa_round_rect_paint(c, r.x, r.y, r.w, r.h, r.h * 0.5f, &p);
    pa_round_rect(c, r.x + r.h * 0.3f, r.y + 6.0f * L.u, r.w - r.h * 0.6f, r.h * 0.22f, r.h * 0.11f,
                  PA_RGBA(255, 255, 255, 60));
    float ty = r.y + r.h * 0.5f - ts * 0.5f - (coin_cost ? 8.0f * L.u : 0.0f);
    pa_text_bold(c, label, r.x + r.w * 0.5f, ty, ts, PA_RGB(255, 255, 255), pa_shade(col, -0.45f),
                 PA_ALIGN_CENTER, 2.0f * L.u, 1.0f);
    if (coin_cost) {
        char b[12];
        snprintf(b, sizeof(b), "%d", coin_cost);
        float cs = 16.0f * L.u;
        float tw = pa_text_width(b, cs, 1.0f);
        float x0 = r.x + r.w * 0.5f - (tw + 24.0f * L.u) * 0.5f;
        coin_icon(c, x0 + 8.0f * L.u, ty + ts + 18.0f * L.u, 9.0f * L.u);
        pa_text_bold(c, b, x0 + 22.0f * L.u, ty + ts + 10.0f * L.u, cs, PA_RGB(255, 255, 255),
                     pa_shade(col, -0.45f), PA_ALIGN_LEFT, 1.0f, 0.8f);
    }
}

static void ribbon(PA_Canvas *c, float cy, float w, float h, PA_Color col, const char *text, float ts) {
    float x = L.cx - w * 0.5f;
    PA_Color dark = pa_shade(col, -0.35f);
    PA_Vec2 lt[5] = { { x - h * 0.5f, cy - h * 0.3f }, { x + h * 0.3f, cy - h * 0.3f }, { x + h * 0.3f, cy + h * 0.7f },
                      { x - h * 0.5f, cy + h * 0.7f }, { x - h * 0.2f, cy + h * 0.2f } };
    PA_Vec2 rt[5] = { { x + w + h * 0.5f, cy - h * 0.3f }, { x + w - h * 0.3f, cy - h * 0.3f }, { x + w - h * 0.3f, cy + h * 0.7f },
                      { x + w + h * 0.5f, cy + h * 0.7f }, { x + w + h * 0.2f, cy + h * 0.2f } };
    pa_fill_poly(c, lt, 5, dark);
    pa_fill_poly(c, rt, 5, dark);
    PA_Paint p = pa_linear(0, cy - h * 0.5f, 0, cy + h * 0.5f);
    pa_stop(&p, 0.0f, pa_shade(col, 0.12f));
    pa_stop(&p, 1.0f, pa_shade(col, -0.08f));
    pa_round_rect_paint(c, x, cy - h * 0.5f, w, h, 4.0f * L.u, &p);
    pa_fill_rect(c, x, cy - h * 0.5f, w, h * 0.12f, PA_RGBA(255, 255, 255, 60));
    pa_text_bold(c, text, L.cx, cy - ts * 0.5f, ts, PA_RGB(255, 255, 255), dark, PA_ALIGN_CENTER, 2.0f * L.u, 0.9f);
}

static void draw_hud(PA_Canvas *c) {
    float u = L.u;
    char buf[48];
    float top = 46.0f * u;

    pa_hub_pause_anchor(36.0f * u, top, 21.0f * u);
    coin_pill(c, (float)c->w - 14.0f * u, top, H.coins_shown);
    level_bar(c, top, (float)done_floors() / (float)(H.floor_count > 0 ? H.floor_count : 1));

    float sy = 84.0f * u;
    if (H.th.boss) {
        ribbon(c, 98.0f * u, 210.0f * u, 38.0f * u, pa_hex(0xE8232B), "BOSS LEVEL", 20.0f * u);
        sy = 130.0f * u;
    }
    float ss = 58.0f * u;
    snprintf(buf, sizeof(buf), "%d", H.score);
    pa_text_bold(c, buf, L.cx, sy, ss, pa_hex(0x2B2B2B), PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 3.0f * u, 0.85f);

    if (H.state == ST_READY) {
        snprintf(buf, sizeof(buf), "BEST : %d", g_best);
        pa_text_bold(c, buf, L.cx, sy + ss + 16.0f * u, 22.0f * u, pa_hex(0x2B2B2B), PA_RGBA(255, 255, 255, 220),
                     PA_ALIGN_CENTER, 1.5f * u, 0.9f);
    }

    /* "+N" popups, newest on top, the older ones rising and fading behind. */
    for (int i = MAX_POPS - 1; i >= 0; i--) {
        const Pop *p = &H.pops[i];
        if (p->t <= 0.0f) continue;
        float age = 1.0f - p->t;
        float a = pa_clamp01(p->t * 2.0f);
        float sc = 0.7f + 0.3f * elastic(pa_clamp01(age * 5.0f));
        float py = sy + ss + 0.06f * 540.0f * u * 0.4f + 6.0f * u - age * 14.0f * u;
        float ts = 40.0f * u * sc;
        pa_text_bold(c, p->text, L.cx - 4.0f * u, py, ts, pa_alpha(pa_hex(0xFFB300), a),
                     PA_RGBA(255, 255, 255, (int)(a * 230)), PA_ALIGN_CENTER, 2.0f * u, 0.9f);
    }

    /* Combo words, elastic scale-in. */
    for (int i = 1; i >= 0; i--) {
        const Word *w = &H.words[i];
        if (w->t <= 0.0f) continue;
        float age = 1.0f - w->t;
        float a = pa_clamp01(w->t * 3.0f);
        float sc = elastic(pa_clamp01(age * 2.6f));
        float ts = 0.08f * 540.0f * u * sc * (i == 0 ? 1.0f : 0.75f);
        if (ts < 2.0f) continue;
        float wy = sy + ss + 98.0f * u + (float)i * 58.0f * u - ts * 0.5f;
        pa_text_bold(c, w->text, L.cx, wy, ts, pa_alpha(w->col, a), PA_RGBA(24, 40, 20, (int)(a * 255)),
                     PA_ALIGN_CENTER, 2.0f * u, 1.1f);
    }

    if (H.state == ST_READY) {
        float hx = L.cx + sinf(H.time * 3.2f) * 80.0f * u;
        float hy = (float)c->h * 0.80f;
        hand_hint(c, hx, hy);
        pa_text_bold(c, "HOLD AND DRAG TO PLAY", L.cx, hy + 116.0f * u, 22.0f * u, pa_hex(0x2B2B2B),
                     PA_RGBA(255, 255, 255, 230), PA_ALIGN_CENTER, 1.5f * u, 0.9f);
    }
}

static void fail_card(PA_Canvas *c) {
    float u = L.u, w = (float)c->w, h = (float)c->h;
    float a = pa_clamp01(H.state_t * 3.0f);
    pa_fill_rect(c, 0, 0, w, h, PA_RGBA(255, 255, 255, (int)(a * 150.0f)));
    pa_hub_hide_pause();
    char buf[48];
    float pop = elastic(pa_clamp01(H.state_t * 2.2f));
    float y = h * 0.20f;
    pa_text_bold(c, "SPIKED!", L.cx, y, 62.0f * u * pop, pa_alpha(pa_hex(HAZARD), a), PA_RGBA(255, 255, 255, (int)(a * 255)),
                 PA_ALIGN_CENTER, 4.0f * u, 1.0f);
    int pct = percent_done();
    snprintf(buf, sizeof(buf), "%d%% COMPLETED", pct);
    pa_text_bold(c, buf, L.cx, y + 96.0f * u, 32.0f * u, pa_alpha(pa_hex(0x2B2B2B), a),
                 PA_RGBA(255, 255, 255, (int)(a * 255)), PA_ALIGN_CENTER, 2.0f * u, 0.9f);
    /* The level bar with how far the run got. */
    float bw = 300.0f * u, bx = L.cx - bw * 0.5f, by = y + 160.0f * u;
    float fill = pa_clamp01((float)pct / 100.0f * pa_smooth(pa_clamp01(H.state_t * 1.6f)));
    pa_round_rect(c, bx, by - 8.0f * u, bw, 16.0f * u, 8.0f * u, PA_RGBA(40, 44, 60, (int)(60 * a)));
    if (fill > 0.0f) pa_round_rect(c, bx, by - 8.0f * u, bw * fill, 16.0f * u, 8.0f * u, pa_alpha(pa_hex(0xFFC21C), a));
    for (int s = 0; s < 2; s++) {
        float x = s ? bx + bw : bx;
        pa_fill_circle(c, x, by, 22.0f * u, pa_alpha(s ? pa_hex(0x56585E) : pa_hex(0xFFC21C), a));
        snprintf(buf, sizeof(buf), "%d", H.level + s);
        pa_text_bold(c, buf, x, by - 9.0f * u, 18.0f * u, s ? PA_RGB(255, 255, 255) : pa_hex(0x3A2A00),
                     s ? pa_hex(0x56585E) : pa_hex(0xFFC21C), PA_ALIGN_CENTER, 0.5f * u, 0.7f);
    }
    snprintf(buf, sizeof(buf), "%d", H.score);
    pa_text_bold(c, buf, L.cx, by + 50.0f * u, 64.0f * u, pa_alpha(pa_hex(0x2B2B2B), a),
                 PA_RGBA(255, 255, 255, (int)(a * 255)), PA_ALIGN_CENTER, 3.0f * u, 0.9f);
    snprintf(buf, sizeof(buf), "BEST : %d", g_best);
    pa_text_bold(c, buf, L.cx, by + 130.0f * u, 22.0f * u, pa_alpha(pa_hex(0x2B2B2B), a),
                 PA_RGBA(255, 255, 255, (int)(a * 255)), PA_ALIGN_CENTER, 1.5f * u, 0.9f);

    int can_continue = !H.continued && g_coins >= CONTINUE_COST && H.state_t < CONTINUE_TIME;
    if (can_continue) {
        Rect r = card_button(0);
        float rs = pa_smooth(pa_clamp01(H.state_t * 3.0f));
        r.y += (1.0f - rs) * 60.0f * u;
        button(c, r, pa_hex(0x2FC85A), "CONTINUE", 30.0f * u, CONTINUE_COST);
        /* Countdown ring at the button's end. */
        float rx = r.x + r.w - 2.0f * u, ry = r.y + r.h * 0.5f, rr = 34.0f * u;
        pa_fill_circle(c, rx, ry, rr, PA_RGB(255, 255, 255));
        float left = 1.0f - H.state_t / CONTINUE_TIME;
        PA_Vec2 arc[41];
        int n = 0;
        for (int i = 0; i <= 40; i++) {
            float aa = -PA_PI * 0.5f + PA_TAU * left * (float)i / 40.0f;
            arc[n].x = rx + cosf(aa) * rr * 0.78f; arc[n].y = ry + sinf(aa) * rr * 0.78f; n++;
        }
        pa_stroke_poly(c, arc, n, 0, 7.0f * u, pa_hex(0xFFB300));
        snprintf(buf, sizeof(buf), "%d", (int)ceilf(CONTINUE_TIME - H.state_t));
        pa_text_bold(c, buf, rx, ry - 12.0f * u, 24.0f * u, pa_hex(0x2B2B2B), PA_RGB(255, 255, 255),
                     PA_ALIGN_CENTER, 0.0f, 0.9f);
        Rect r2 = card_button(1);
        pa_text_bold(c, "NO THANKS", L.cx, r2.y + 16.0f * u, 22.0f * u, PA_RGBA(60, 64, 80, (int)(a * 220)),
                     PA_RGBA(255, 255, 255, (int)(a * 200)), PA_ALIGN_CENTER, 2.0f * u, 0.8f);
    } else {
        Rect r = card_button(0);
        button(c, r, pa_hex(0x3F8CF6), "RETRY", 32.0f * u, 0);
        float blink = 0.6f + 0.4f * sinf(H.time * 5.0f);
        Rect r2 = card_button(1);
        pa_text_bold(c, "TAP TO RESTART", L.cx, r2.y + 16.0f * u, 22.0f * u, PA_RGBA(60, 64, 80, (int)(a * 220 * blink)),
                     PA_RGBA(255, 255, 255, (int)(a * 200 * blink)), PA_ALIGN_CENTER, 2.0f * u, 0.8f);
    }
}

static void win_card(PA_Canvas *c) {
    float u = L.u, w = (float)c->w, h = (float)c->h;
    float t = H.state_t - 0.35f;
    if (t <= 0.0f) return;
    float a = pa_clamp01(t * 3.0f);
    pa_hub_hide_pause();
    pa_fill_rect(c, 0, 0, w, h, PA_RGBA(255, 255, 255, (int)(a * 90.0f)));

    /* Sunburst behind the headline. */
    float cy = h * 0.24f;
    PA_Color plate = pa_hex(H.th.plate);
    for (int i = 0; i < 14; i++) {
        float a0 = (float)i / 14.0f * PA_TAU + H.time * 0.4f, a1 = a0 + PA_TAU / 28.0f;
        PA_Vec2 tri[3] = { { L.cx, cy }, { L.cx + cosf(a0) * w, cy + sinf(a0) * w },
                           { L.cx + cosf(a1) * w, cy + sinf(a1) * w } };
        pa_fill_poly(c, tri, 3, PA_RGBA(255, 255, 255, (int)(a * 70.0f)));
    }
    PA_Paint glow = pa_radial(L.cx, cy, 10.0f, w * 0.5f);
    pa_stop(&glow, 0.0f, pa_alpha(pa_shade(plate, 0.6f), a * 0.6f));
    pa_stop(&glow, 1.0f, pa_alpha(pa_shade(plate, 0.6f), 0.0f));
    pa_fill_ellipse_paint(c, L.cx, cy, w * 0.5f, w * 0.5f, &glow);

    char buf[48];
    float pop = elastic(pa_clamp01(t * 1.8f));
    snprintf(buf, sizeof(buf), "LEVEL %d", H.level);
    ribbon(c, cy - 52.0f * u, 250.0f * u * pop + 1.0f, 56.0f * u, pa_shade(plate, -0.05f), buf, 30.0f * u * pop + 0.5f);
    pa_text_bold(c, "COMPLETED!", L.cx, cy + 10.0f * u, 54.0f * u * pop + 0.5f, PA_RGB(255, 255, 255),
                 pa_shade(plate, -0.40f), PA_ALIGN_CENTER, 3.0f * u, 1.0f);
    snprintf(buf, sizeof(buf), "%d", H.score);
    pa_text_bold(c, buf, L.cx, h * 0.47f, 46.0f * u, pa_alpha(pa_hex(0x2B2B2B), a),
                 PA_RGBA(255, 255, 255, (int)(a * 255)), PA_ALIGN_CENTER, 3.0f * u, 0.9f);

    /* Coin reward pill. */
    float pw = 190.0f * u, ph = 62.0f * u, px = L.cx - pw * 0.5f, py = h * 0.47f + 66.0f * u;
    float rs = pa_smooth(pa_clamp01((t - 0.3f) * 3.0f));
    pa_round_rect(c, px, py + (1.0f - rs) * 30.0f * u, pw, ph, ph * 0.5f, PA_RGBA(20, 30, 45, (int)(150 * rs)));
    coin_icon(c, px + ph * 0.5f + 4.0f * u, py + ph * 0.5f + (1.0f - rs) * 30.0f * u, 20.0f * u);
    snprintf(buf, sizeof(buf), "+%d", H.win_coins);
    pa_text_bold(c, buf, px + ph + 14.0f * u, py + ph * 0.5f - 15.0f * u + (1.0f - rs) * 30.0f * u, 30.0f * u,
                 PA_RGBA(255, 214, 80, (int)(255 * rs)), PA_RGBA(20, 30, 45, (int)(200 * rs)), PA_ALIGN_LEFT, 2.0f * u, 0.9f);

    Rect r = card_button(0);
    float bs = pa_smooth(pa_clamp01((t - 0.5f) * 3.0f));
    r.y += (1.0f - bs) * 80.0f * u;
    if (bs > 0.01f) button(c, r, pa_hex(0x2FC85A), "NEXT LEVEL", 30.0f * u, 0);
}

static void helix_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) compute_layout(c->w, c->h);

    backdrop(c);
    float shake_x = H.shake > 0.0f ? sinf(H.time * 63.0f) * H.shake * 6.0f * L.u : 0.0f;
    float shake_y = H.shake > 0.0f ? cosf(H.time * 51.0f) * H.shake * 3.0f * L.u : 0.0f;
    float saved_b = L.B;
    L.B += shake_y;
    float cx = L.cx + shake_x;

    draw_tower(c, cx);
    draw_parts(c, cx, 0);
    L.B = saved_b;

    if (H.state == ST_DEAD || H.state == ST_FAIL) {
        /* The tower drains toward white as the run ends. */
        float a = pa_clamp01(H.state == ST_DEAD ? H.state_t * 0.6f : 0.5f);
        pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(255, 255, 255, (int)(a * 120.0f)));
    }

    if (H.state != ST_FAIL && !(H.state == ST_WIN && H.state_t > 0.35f)) draw_hud(c);
    if (H.state == ST_FAIL) fail_card(c);
    if (H.state == ST_WIN) win_card(c);
    draw_parts(c, cx, 1);
}

/* ------------------------------------------------------------------ thumb */
static void helix_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    PA_Paint bg = pa_linear(x, y, x, y + h);
    pa_stop(&bg, 0.0f, pa_hex(0x86CCFF));
    pa_stop(&bg, 1.0f, pa_hex(0xEAF6FF));
    pa_fill_rect_paint(c, x, y, w, h, &bg);

    float cx = x + w * 0.5f;
    float rad = w * 0.40f, inner = rad * 0.36f, squash = 0.38f;
    PA_Paint p = pa_linear(cx - inner, 0, cx + inner, 0);
    pa_stop(&p, 0.0f, pa_hex(0xC4CAD3));
    pa_stop(&p, 0.35f, pa_hex(0xFFFFFF));
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
                if (s == 2 || s == 3) continue;
                float a0 = spin + (float)s / 8.0f * PA_TAU;
                float a1 = spin + (float)(s + 1) / 8.0f * PA_TAU + 0.03f;
                if ((sinf((a0 + a1) * 0.5f) > 0.0f) != pass) continue;
                PA_Vec2 pts[16];
                int n = 0;
                for (int i = 0; i <= 4; i++) {
                    float a = a0 + (a1 - a0) * ((float)i / 4.0f);
                    pts[n].x = cx + cosf(a) * rad; pts[n].y = cy + sinf(a) * rad * squash; n++;
                }
                for (int i = 4; i >= 0; i--) {
                    float a = a0 + (a1 - a0) * ((float)i / 4.0f);
                    pts[n].x = cx + cosf(a) * inner; pts[n].y = cy + sinf(a) * inner * squash; n++;
                }
                pa_fill_poly(c, pts, n, s == 5 ? pa_hex(HAZARD) : pa_hex(0x33CC55));
            }
        }
    }
    float by = y + h * (0.20f + fabsf(sinf(t * 2.4f)) * 0.08f);
    pa_fill_circle(c, cx, by, w * 0.06f, pa_hex(0xFF5A36));
    pa_fill_circle(c, cx - w * 0.02f, by - w * 0.02f, w * 0.018f, PA_RGBA(255, 255, 255, 200));
}

const PA_Game PA_GAME_HELIX = {
    "helix", "Helix Drop", "Arcade",
    "Spin the tower, thread the gaps, and chain three floors to smash straight through.",
    PA_RGB(64, 200, 190),
    helix_start, helix_stop, helix_update, helix_render, helix_thumb
};
