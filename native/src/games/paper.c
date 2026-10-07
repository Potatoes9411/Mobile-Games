/* ===========================================================================
   PAPER TERRITORY - native port
   Drag to steer a little cube around a round arena. Leaving your colour lays a
   soft trail; coming home claims everything the trail enclosed. Rivals with
   names and skins do the same, and the trails are the weak point: run through
   one and its owner is gone, get yours cut and the run is over.

   Drawn the way the Paper.io 2 plates show it: a near-white mint field inside a
   raised teal rim, water beyond it, and every territory a smooth pastel slab
   with a darker side wall and a soft drop shadow. The land is held on a fine
   grid for the rules, but it is never drawn as cells: each owner's cells are
   blurred into a field and the slab is rendered per pixel from the 0.5
   contour of that field, so edges come out round at any zoom.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ tuning -- */
#define G          112              /* grid cells per side */
#define NCELL      (G * G)
#define ACX        56.0f            /* arena centre, cells */
#define ACY        56.0f
#define AR         50.0f            /* arena radius, cells */
#define RIM_W      1.7f
#define MAXP       10               /* you + nine rivals */
#define START_RIVALS 8
#define RESPAWNS   24
#define TPTS       1600
#define SPEED      7.4f             /* cells per second, everyone */
#define SLAB_Z     0.82f            /* territory thickness, cells */
#define VIEW_K     0.84f            /* ground foreshortening (camera tilt) */
#define VIEW_Z     0.62f            /* height to screen */
#define AV_HALF    1.42f            /* avatar half width, cells */
#define AV_H       1.60f            /* avatar height, cells */
#define AV_K       0.62f            /* the mascots are seen a little more side-on */
#define AV_ZK      0.80f
#define TRAIL_W    2.00f            /* drawn trail width, cells */
#define HOME_R     5.2f
#define WHO_RIM    200
#define MAXPART    520
#define MAXFT      24
#define REVEAL_T   2.3f

enum { PH_TITLE, PH_PLAY, PH_DEAD, PH_WIN, PH_RESULTS };
enum { AI_PLAN, AI_RETURN, AI_ATTACK };
enum { CAUSE_NONE, CAUSE_CUT, CAUSE_SELF, CAUSE_SURROUND, CAUSE_HEAD, CAUSE_LAND };

/* ------------------------------------------------------------------- skins -- */
enum { D_DONUT, D_CHICK, D_CAT, D_SANDWICH, D_FROG, D_ROBOT, D_BERRY, D_GRAPE, D_SLIME, D_PANDA };

typedef struct {
    const char *name;
    uint32_t body, top, land;
    int pattern;           /* land texture: 0 flat, 1 sprinkles, 2 dots, 3 stripes */
    int unlock;            /* best % needed */
} Skin;

static const Skin SKINS[] = {
    { "DONUT",    0xEBA052, 0xFF6FAE, 0xFF84BB, 1,  0 },
    { "CHICK",    0xF5A90A, 0xFFCF1F, 0xFFCC1F, 2,  2 },
    { "CAT",      0x24307E, 0x33439F, 0x14BBFF, 0,  5 },
    { "SANDWICH", 0xE5802E, 0xF29A42, 0xFF9A3A, 0,  8 },
    { "FROG",     0x2FB043, 0x4FD24A, 0x5FD94C, 2, 12 },
    { "ROBOT",    0x6F84C6, 0x9AAFEA, 0x9E82FF, 0, 18 },
    { "BERRY",    0xDB2433, 0xF33A45, 0xFF6464, 0, 25 },
    { "GRAPE",    0x7634CC, 0x974FF0, 0xCB6BFF, 3, 35 },
    { "SLIME",    0x0FB396, 0x2CDDBC, 0x25DFC0, 3, 50 },
    { "PANDA",    0xDCE2EE, 0xFFFFFF, 0x739DFF, 0, 75 },
};
#define SKIN_COUNT ((int)(sizeof(SKINS) / sizeof(SKINS[0])))

static const char *NAMES[] = {
    "JASON", "MARIA", "NIELS", "LUNA", "KENJI", "ZOE", "OMAR", "IVY", "THEO", "ROSA",
    "MAX", "AIKO", "LEO", "NOVA", "FINN", "MIA", "RAJ", "ELLA", "OTTO", "SKY", "PIP", "JUNO"
};
#define NAME_COUNT ((int)(sizeof(NAMES) / sizeof(NAMES[0])))

/* ------------------------------------------------------------------- state -- */
typedef struct {
    int   used, alive, human;
    int   dying;  float fade;            /* land shrinking away after death */
    float respawn;
    int   skin;
    char  name[12];
    float x, y, head, want;
    int   cx, cy;
    int   out, tcount;
    PA_Vec2 tp[TPTS]; int tn; float tacc;
    PA_Vec2 gp[TPTS]; int gn; float gt; /* claimed trail, fading out */
    int   kills;
    float pop, squash, hop, z;
    /* AI */
    int   st; float think;
    float wx[3], wy[3]; int leg, nlegs;
    float aggr, greed, caution;
    int   tq; float tx, ty;              /* attack target */
    float side;
} Player;

typedef struct {
    float x, y, z, vx, vy, vz, life, max, size, rot, vr;
    PA_Color col; int kind;
} Part;

typedef struct { float x, y, t; char text[16]; PA_Color col; float size; int pill; } FText;

static struct {
    int   phase; float t, pt;
    Player pl[MAXP];
    uint8_t arena[NCELL];
    uint8_t own[NCELL], vis[NCELL], trl[NCELL];
    uint16_t tix[NCELL];
    float rev[NCELL]; uint8_t revo[NCELL]; float ct[NCELL];
    int   rv_x0, rv_y0, rv_x1, rv_y1, rv_on;
    int   dt_x0, dt_y0, dt_x1, dt_y1, dirty;
    uint8_t fld[MAXP][NCELL];
    uint8_t dom[NCELL], domv[NCELL];
    uint8_t deco[NCELL], sand[NCELL];
    int   arena_cells, cnt[MAXP + 1];
    PA_Rng rng;
    int   budget, name_next;
    float cam_x, cam_y, shake, flash; PA_Color flash_col;
    Part  parts[MAXPART];
    FText ft[MAXFT];
    char  banner[24]; float banner_t; PA_Color banner_col;
    int   streak; float streak_t;
    int   drag; float ax, ay;
    float nudge, lock_shake;
    float peak;
    /* results */
    float final_pct; int final_kills, rank, new_best, unlocked, cause;
    char  killer[12]; int killer_i;
    float cut_x, cut_y;
    int   res_n, res_me;
    struct { char name[12]; int skin; float pct; } res[MAXP];
    struct { float x, y, r; } isle[12]; int nisle;
    float res_btn_y;
    struct { float x, y; uint8_t type, var; } props[480]; int nprops;
    int   reward, confetti_done;
    float last_ct;          /* last time any land was revealed */
} S;

static int   g_best;            /* best %, tenths */
static int   g_skin;
static int   g_runs;
static int   g_total_kills;
static int   g_loaded;
static int   g_stars;            /* reward currency */

/* ------------------------------------------------------------------ helpers -- */
static inline int cell_at(float x, float y) {
    int cx = (int)floorf(x), cy = (int)floorf(y);
    if (cx < 0 || cy < 0 || cx >= G || cy >= G) return -1;
    return cy * G + cx;
}

static uint32_t hash2(int x, int y, int k) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)k * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float angle_diff(float a, float b) {
    float d = fmodf(b - a + PA_PI, PA_TAU);
    if (d < 0) d += PA_TAU;
    return d - PA_PI;
}

static float turn_toward(float a, float b, float max) {
    float d = angle_diff(a, b);
    if (d > max) d = max;
    if (d < -max) d = -max;
    return a + d;
}

static float pct_of(int i) { return S.arena_cells ? 100.0f * (float)S.cnt[i + 1] / (float)S.arena_cells : 0.0f; }

static void mark_dirty(int x, int y) {
    if (!S.dirty) { S.dt_x0 = S.dt_x1 = x; S.dt_y0 = S.dt_y1 = y; S.dirty = 1; return; }
    if (x < S.dt_x0) S.dt_x0 = x;
    if (x > S.dt_x1) S.dt_x1 = x;
    if (y < S.dt_y0) S.dt_y0 = y;
    if (y > S.dt_y1) S.dt_y1 = y;
}

/* Logic ownership changes now; the drawn ownership follows at `when`. */
static void set_owner(int c, int o, float when) {
    int old = S.own[c];
    if (old == o) return;
    S.cnt[old]--;
    S.cnt[o]++;
    S.own[c] = (uint8_t)o;
    int x = c % G, y = c / G;
    if (when <= S.t) {
        S.vis[c] = (uint8_t)o; S.ct[c] = S.t; S.rev[c] = -1.0f; S.last_ct = S.t;
        mark_dirty(x, y);
        return;
    }
    S.rev[c] = when; S.revo[c] = (uint8_t)o;
    if (!S.rv_on) { S.rv_x0 = S.rv_x1 = x; S.rv_y0 = S.rv_y1 = y; S.rv_on = 1; }
    if (x < S.rv_x0) S.rv_x0 = x;
    if (x > S.rv_x1) S.rv_x1 = x;
    if (y < S.rv_y0) S.rv_y0 = y;
    if (y > S.rv_y1) S.rv_y1 = y;
}

static void process_reveals(void) {
    if (!S.rv_on) return;
    int left = 0;
    for (int y = S.rv_y0; y <= S.rv_y1; y++)
        for (int x = S.rv_x0; x <= S.rv_x1; x++) {
            int c = y * G + x;
            if (S.rev[c] < 0.0f) continue;
            if (S.rev[c] <= S.t) {
                S.vis[c] = S.revo[c]; S.ct[c] = S.t; S.rev[c] = -1.0f; S.last_ct = S.t;
                mark_dirty(x, y);
            } else left++;
        }
    if (!left) S.rv_on = 0;
}

/* ------------------------------------------------------------ particles -- */
static void add_part(float x, float y, float z, float vx, float vy, float vz,
                     float life, float size, PA_Color col, int kind) {
    for (int i = 0; i < MAXPART; i++) {
        Part *p = &S.parts[i];
        if (p->life > 0.0f) continue;
        p->x = x; p->y = y; p->z = z; p->vx = vx; p->vy = vy; p->vz = vz;
        p->life = p->max = life; p->size = size; p->col = col; p->kind = kind;
        p->rot = pa_rng_range(&S.rng, 0, PA_TAU); p->vr = pa_rng_range(&S.rng, -9, 9);
        return;
    }
}

static FText *add_text(float x, float y, const char *txt, PA_Color col, float size) {
    int slot = 0; float oldest = -1.0f;
    for (int i = 0; i < MAXFT; i++) {
        if (S.ft[i].t <= 0.0f) { slot = i; break; }
        if (S.ft[i].t > oldest) { oldest = S.ft[i].t; slot = i; }
    }
    FText *f = &S.ft[slot];
    f->x = x; f->y = y; f->t = 1.3f; f->col = col; f->size = size; f->pill = 0;
    snprintf(f->text, sizeof(f->text), "%s", txt);
    return f;
}

static void burst(float x, float y, float z, int n, PA_Color a, PA_Color b, int kind, float speed) {
    for (int k = 0; k < n; k++) {
        float ang = pa_rng_range(&S.rng, 0, PA_TAU), sp = pa_rng_range(&S.rng, 0.3f, 1.0f) * speed;
        add_part(x, y, z, cosf(ang) * sp, sinf(ang) * sp, pa_rng_range(&S.rng, 3.0f, 9.0f),
                 pa_rng_range(&S.rng, 0.6f, 1.1f), pa_rng_range(&S.rng, 0.22f, 0.42f),
                 (k & 1) ? a : b, kind);
    }
}

static void banner(const char *txt, PA_Color col) {
    snprintf(S.banner, sizeof(S.banner), "%s", txt);
    S.banner_t = 1.6f;
    S.banner_col = col;
}

/* ----------------------------------------------------------------- world -- */
static void clear_trail(int i) {
    Player *p = &S.pl[i];
    uint8_t me = (uint8_t)(i + 1);
    for (int c = 0; c < NCELL; c++) if (S.trl[c] == me) S.trl[c] = 0;
    p->out = 0; p->tcount = 0; p->tn = 0;
}

static void stamp_home(int i, float x, float y, float r, int animate) {
    int x0 = (int)(x - r - 1), x1 = (int)(x + r + 1), y0 = (int)(y - r - 1), y1 = (int)(y + r + 1);
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++) {
            if (cx < 0 || cy < 0 || cx >= G || cy >= G) continue;
            int c = cy * G + cx;
            if (!S.arena[c]) continue;
            float dx = (float)cx + 0.5f - x, dy = (float)cy + 0.5f - y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > r) continue;
            set_owner(c, i + 1, animate ? S.t + d / r * 0.32f : 0.0f);
        }
}

static void kill_player(int q, int killer, int cause);
static void snapshot_standings(void);

static void pick_skin_name(int i) {
    Player *p = &S.pl[i];
    int used[SKIN_COUNT] = { 0 };
    for (int k = 0; k < MAXP; k++) if (k != i && S.pl[k].used) used[S.pl[k].skin] = 1;
    used[g_skin] = 1;
    int start = pa_rng_int(&S.rng, 0, SKIN_COUNT - 1);
    p->skin = start;
    for (int k = 0; k < SKIN_COUNT; k++) {
        int s = (start + k) % SKIN_COUNT;
        if (!used[s]) { p->skin = s; break; }
    }
    for (int tries = 0; tries < NAME_COUNT; tries++) {
        const char *nm = NAMES[S.name_next % NAME_COUNT];
        S.name_next += 1 + pa_rng_int(&S.rng, 0, 2);
        int clash = 0;
        for (int k = 0; k < MAXP; k++)
            if (k != i && S.pl[k].used && (S.pl[k].alive || S.pl[k].dying) && !strcmp(S.pl[k].name, nm)) clash = 1;
        snprintf(p->name, sizeof(p->name), "%s", nm);
        if (!clash) break;
    }
}

static int find_spawn(float *ox, float *oy) {
    float best = -1.0f, bx = ACX, by = ACY;
    for (int tries = 0; tries < 80; tries++) {
        float ang = pa_rng_range(&S.rng, 0, PA_TAU), rr = sqrtf(pa_rng_next(&S.rng)) * (AR - 9.0f);
        float x = ACX + cosf(ang) * rr, y = ACY + sinf(ang) * rr;
        int ok = 1;
        for (int dy = -8; dy <= 8 && ok; dy++)
            for (int dx = -8; dx <= 8 && ok; dx++) {
                int c = cell_at(x + (float)dx, y + (float)dy);
                if (c < 0) continue;
                if (dx * dx + dy * dy > 64) continue;
                if (S.own[c] || S.trl[c] || (S.vis[c] && S.rev[c] < 0.0f)) ok = 0;
            }
        if (!ok) continue;
        float md = 1e9f;
        for (int k = 0; k < MAXP; k++) {
            Player *q = &S.pl[k];
            if (!q->used || !q->alive) continue;
            float d = hypotf(q->x - x, q->y - y);
            if (d < md) md = d;
        }
        if (md > best) { best = md; bx = x; by = y; }
    }
    *ox = bx; *oy = by;
    return best > 10.0f;
}

static void spawn_player(int i, int human, float x, float y, int animate) {
    Player *p = &S.pl[i];
    int skin = p->skin;
    char name[12];
    memcpy(name, p->name, sizeof(name));
    memset(p, 0, sizeof(*p));
    p->skin = skin;
    memcpy(p->name, name, sizeof(name));
    p->used = 1; p->alive = 1; p->human = human;
    p->x = x; p->y = y;
    p->cx = (int)floorf(x); p->cy = (int)floorf(y);
    p->head = p->want = pa_rng_range(&S.rng, 0, PA_TAU);
    p->pop = animate ? 0.0f : 1.0f;
    p->aggr = pa_rng_range(&S.rng, 0.15f, 0.85f);
    p->greed = pa_rng_range(&S.rng, 0.2f, 1.0f);
    p->caution = pa_rng_range(&S.rng, 0.2f, 1.0f);
    p->think = pa_rng_range(&S.rng, 0.0f, 0.3f);
    p->side = pa_rng_chance(&S.rng, 0.5f) ? 1.0f : -1.0f;
    p->st = AI_PLAN; p->nlegs = 0; p->tq = -1;
    stamp_home(i, x, y, HOME_R, animate);
    if (animate) burst(x, y, 0.5f, 10, pa_hex(SKINS[p->skin].land), PA_RGB(255, 255, 255), 2, 4.0f);
}

static void make_deco(void) {
    static float acc[NCELL];
    memset(acc, 0, sizeof(acc));
    for (int b = 0; b < 26; b++) {
        float ang = pa_rng_range(&S.rng, 0, PA_TAU), rr = sqrtf(pa_rng_next(&S.rng)) * (AR - 4.0f);
        float cx = ACX + cosf(ang) * rr, cy = ACY + sinf(ang) * rr;
        int lobes = pa_rng_int(&S.rng, 3, 6);
        for (int l = 0; l <= lobes; l++) {
            float r = l == 0 ? pa_rng_range(&S.rng, 2.4f, 4.2f) : pa_rng_range(&S.rng, 0.9f, 1.8f);
            float la = pa_rng_range(&S.rng, 0, PA_TAU), ld = l == 0 ? 0.0f : pa_rng_range(&S.rng, 2.6f, 4.6f);
            float lx = cx + cosf(la) * ld, ly = cy + sinf(la) * ld;
            for (int y = (int)(ly - r - 1); y <= (int)(ly + r + 1); y++)
                for (int x = (int)(lx - r - 1); x <= (int)(lx + r + 1); x++) {
                    if (x < 0 || y < 0 || x >= G || y >= G) continue;
                    float dx = (float)x + 0.5f - lx, dy = (float)y + 0.5f - ly;
                    if (dx * dx + dy * dy < r * r) acc[y * G + x] = 1.0f;
                }
        }
    }
    /* Two box passes make the splats soft enough to threshold smoothly. */
    static float tmp[NCELL];
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < G; y++)
            for (int x = 0; x < G; x++) {
                float s = 0; int n = 0;
                for (int k = -1; k <= 1; k++) if (x + k >= 0 && x + k < G) { s += acc[y * G + x + k]; n++; }
                tmp[y * G + x] = s / (float)n;
            }
        for (int y = 0; y < G; y++)
            for (int x = 0; x < G; x++) {
                float s = 0; int n = 0;
                for (int k = -1; k <= 1; k++) if (y + k >= 0 && y + k < G) { s += tmp[(y + k) * G + x]; n++; }
                acc[y * G + x] = s / (float)n;
            }
    }
    for (int c = 0; c < NCELL; c++) S.deco[c] = (uint8_t)(acc[c] * 255.0f);

    /* Sand: a few broad beaches against the rim. */
    memset(acc, 0, sizeof(acc));
    for (int b = 0; b < 6; b++) {
        float ang = (float)b / 6.0f * PA_TAU + pa_rng_range(&S.rng, -0.3f, 0.3f);
        for (int l = 0; l < 5; l++) {
            float la = ang + pa_rng_range(&S.rng, -0.22f, 0.22f), ld = AR - pa_rng_range(&S.rng, 2.0f, 8.0f);
            float r = pa_rng_range(&S.rng, 3.5f, 6.5f);
            float lx = ACX + cosf(la) * ld, ly = ACY + sinf(la) * ld;
            for (int y = (int)(ly - r - 1); y <= (int)(ly + r + 1); y++)
                for (int x = (int)(lx - r - 1); x <= (int)(lx + r + 1); x++) {
                    if (x < 0 || y < 0 || x >= G || y >= G) continue;
                    float dx = (float)x + 0.5f - lx, dy = (float)y + 0.5f - ly;
                    if (dx * dx + dy * dy < r * r) acc[y * G + x] = 1.0f;
                }
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < G; y++)
            for (int x = 0; x < G; x++) {
                float sm = 0; int n = 0;
                for (int k = -1; k <= 1; k++) if (x + k >= 0 && x + k < G) { sm += acc[y * G + x + k]; n++; }
                tmp[y * G + x] = sm / (float)n;
            }
        for (int y = 0; y < G; y++)
            for (int x = 0; x < G; x++) {
                float sm = 0; int n = 0;
                for (int k = -1; k <= 1; k++) if (y + k >= 0 && y + k < G) { sm += tmp[(y + k) * G + x]; n++; }
                acc[y * G + x] = sm / (float)n;
            }
    }
    for (int c = 0; c < NCELL; c++) S.sand[c] = (uint8_t)(acc[c] * 255.0f);

    /* Small props scattered over the ground: what grows where depends on
       the terrain under it. */
    S.nprops = 0;
    for (int tries = 0; tries < 3000 && S.nprops < 480; tries++) {
        float ang = pa_rng_range(&S.rng, 0, PA_TAU), rr = sqrtf(pa_rng_next(&S.rng)) * (AR - 1.5f);
        float x = ACX + cosf(ang) * rr, y = ACY + sinf(ang) * rr;
        int c = cell_at(x, y);
        if (c < 0) continue;
        int t;
        float k = pa_rng_next(&S.rng);
        if (S.sand[c] > 140) t = k < 0.55f ? 4 : 2;
        else if (S.deco[c] > 140) t = k < 0.45f ? 3 : (k < 0.8f ? 0 : 5);
        else t = k < 0.5f ? 0 : (k < 0.75f ? 2 : 3);
        S.props[S.nprops].x = x; S.props[S.nprops].y = y;
        S.props[S.nprops].type = (uint8_t)t; S.props[S.nprops].var = (uint8_t)pa_rng_int(&S.rng, 0, 255);
        S.nprops++;
    }

    S.nisle = 0;
    for (int k = 0; k < 10; k++) {
        float ang = (float)k / 10.0f * PA_TAU + pa_rng_range(&S.rng, -0.2f, 0.2f);
        float d = AR + pa_rng_range(&S.rng, 9.0f, 15.0f);
        S.isle[S.nisle].x = ACX + cosf(ang) * d;
        S.isle[S.nisle].y = ACY + sinf(ang) * d;
        S.isle[S.nisle].r = pa_rng_range(&S.rng, 5.0f, 9.0f);
        S.nisle++;
    }
}

static void new_round(uint32_t seed) {
    memset(S.pl, 0, sizeof(S.pl));
    memset(S.own, 0, sizeof(S.own));
    memset(S.vis, 0, sizeof(S.vis));
    memset(S.trl, 0, sizeof(S.trl));
    memset(S.tix, 0, sizeof(S.tix));
    memset(S.cnt, 0, sizeof(S.cnt));
    memset(S.parts, 0, sizeof(S.parts));
    memset(S.ft, 0, sizeof(S.ft));
    for (int c = 0; c < NCELL; c++) { S.rev[c] = -1.0f; S.ct[c] = -10.0f; }
    S.last_ct = -10.0f;
    S.rv_on = 0;
    pa_rng_seed(&S.rng, seed);
    S.t = 0.0f; S.pt = 0.0f;
    S.arena_cells = 0;
    for (int y = 0; y < G; y++)
        for (int x = 0; x < G; x++) {
            float dx = (float)x + 0.5f - ACX, dy = (float)y + 0.5f - ACY;
            int in = dx * dx + dy * dy < (AR - 0.3f) * (AR - 0.3f);
            S.arena[y * G + x] = (uint8_t)in;
            S.arena_cells += in;
        }
    S.cnt[0] = S.arena_cells;
    make_deco();
    S.budget = RESPAWNS;
    S.name_next = pa_rng_int(&S.rng, 0, NAME_COUNT - 1);
    S.banner_t = 0; S.streak = 0; S.streak_t = 0; S.flash = 0; S.shake = 0;
    S.peak = 0; S.drag = 0;

    /* You start a little off centre, rivals spread around the rest. */
    float ang = pa_rng_range(&S.rng, 0, PA_TAU);
    float hx = ACX + cosf(ang) * 14.0f, hy = ACY + sinf(ang) * 14.0f;
    S.pl[0].skin = g_skin;
    snprintf(S.pl[0].name, sizeof(S.pl[0].name), "YOU");
    spawn_player(0, 1, hx, hy, 0);
    S.pl[0].head = S.pl[0].want = -PA_PI * 0.5f;
    S.pl[0].greed = 1.0f; S.pl[0].caution = 1.0f;     /* only used when a capture self-drives */
    for (int i = 1; i <= START_RIVALS; i++) {
        float x, y;
        pick_skin_name(i);
        find_spawn(&x, &y);
        spawn_player(i, 0, x, y, 0);
    }
    S.dirty = 1; S.dt_x0 = 0; S.dt_y0 = 0; S.dt_x1 = G - 1; S.dt_y1 = G - 1;
    S.cam_x = hx; S.cam_y = hy;
}

/* ------------------------------------------------------------------ claim -- */
static void claim(int i) {
    Player *p = &S.pl[i];
    uint8_t me = (uint8_t)(i + 1);
    static uint8_t mine[NCELL], seen[NCELL];
    static int stack[NCELL];
    int before = S.cnt[me];

    /* The trail plus one cell either side, so even a there-and-back excursion
       leaves a band as wide as the trail was drawn. */
    for (int c = 0; c < NCELL; c++) mine[c] = S.own[c] == me;
    for (int c = 0; c < NCELL; c++) {
        if (S.trl[c] != me) continue;
        int x = c % G, y = c / G;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= G || ny >= G) continue;
                int n = ny * G + nx;
                if (S.arena[n]) mine[n] = 1;
            }
    }
    /* Flood the outside in from beyond the arena; whatever it cannot reach
       was enclosed. */
    memset(seen, 0, sizeof(seen));
    int sp = 0;
    for (int c = 0; c < NCELL; c++) if (!S.arena[c]) { seen[c] = 1; stack[sp++] = c; }
    while (sp) {
        int c = stack[--sp];
        int x = c % G, y = c / G;
        int nb[4] = { x > 0 ? c - 1 : -1, x < G - 1 ? c + 1 : -1, y > 0 ? c - G : -1, y < G - 1 ? c + G : -1 };
        for (int k = 0; k < 4; k++) {
            int n = nb[k];
            if (n < 0 || seen[n] || mine[n]) continue;
            seen[n] = 1;
            stack[sp++] = n;
        }
    }
    float maxd = 1.0f;
    for (int c = 0; c < NCELL; c++) {
        if (!S.arena[c] || seen[c] || S.own[c] == me) continue;
        float d = hypotf((float)(c % G) + 0.5f - p->x, (float)(c / G) + 0.5f - p->y);
        if (d > maxd) maxd = d;
    }
    float speed = maxd / 0.42f;
    if (speed < 28.0f) speed = 28.0f;
    int gained = 0;
    float sx = 0, sy = 0;
    float bx[40], by[40]; int nb = 0;
    for (int c = 0; c < NCELL; c++) {
        if (!S.arena[c] || seen[c] || S.own[c] == me) continue;
        float fx = (float)(c % G) + 0.5f, fy = (float)(c / G) + 0.5f;
        float d = hypotf(fx - p->x, fy - p->y);
        set_owner(c, me, S.t + d / speed);
        gained++;
        sx += fx; sy += fy;
        /* Reservoir sample of claimed cells for the particle burst. */
        if (nb < 40) { bx[nb] = fx; by[nb] = fy; nb++; }
        else { int r = pa_rng_int(&S.rng, 0, gained - 1); if (r < 40) { bx[r] = fx; by[r] = fy; } }
        if (gained % 9 == 0 && i == 0)
            add_part(fx, fy, SLAB_Z, 0, 0, pa_rng_range(&S.rng, 2.0f, 5.0f), 0.7f + d / speed,
                     0.32f, PA_RGB(255, 255, 255), 2);
    }

    /* The claimed trail stays drawn while the fill sweeps over it. */
    memcpy(p->gp, p->tp, sizeof(PA_Vec2) * (size_t)p->tn);
    p->gn = p->tn; p->gt = 0.45f;
    clear_trail(i);
    p->squash = 1.0f;

    /* Anyone standing in what was just taken is surrounded; anyone left with
       no land at all is finished. */
    for (int k = 0; k < MAXP; k++) {
        Player *q = &S.pl[k];
        if (k == i || !q->used || !q->alive) continue;
        int c = cell_at(q->x, q->y);
        if (c >= 0 && S.own[c] == me && !seen[c] && !mine[c] && q->out) { kill_player(k, i, CAUSE_SURROUND); continue; }
        if (S.cnt[k + 1] == 0) kill_player(k, i, CAUSE_LAND);
    }

    {
        /* Confetti and cubes in the owner's colour pop off the new land. */
        PA_Color lc = pa_hex(SKINS[p->skin].land);
        int np = i == 0 ? 34 : 14;
        for (int k = 0; k < np && nb > 0; k++) {
            int j = k % nb;
            float ang = pa_rng_range(&S.rng, 0, PA_TAU), sp2 = pa_rng_range(&S.rng, 0.5f, 3.0f);
            add_part(bx[j], by[j], SLAB_Z, cosf(ang) * sp2, sinf(ang) * sp2, pa_rng_range(&S.rng, 6.0f, 12.0f),
                     pa_rng_range(&S.rng, 0.7f, 1.2f), pa_rng_range(&S.rng, 0.22f, 0.38f),
                     (k % 3 == 0) ? pa_shade(lc, 0.45f) : lc, (k & 1) ? 1 : 0);
        }
    }
    if (i == 0 && gained > 0) {
        float gp = 100.0f * (float)(S.cnt[me] - before) / (float)S.arena_cells;
        char buf[16];
        snprintf(buf, sizeof(buf), "+%.1f%%", (double)gp);
        add_text(p->x, p->y, buf, pa_hex(SKINS[p->skin].land), gp > 4.0f ? 30.0f : 25.0f)->pill = 1;
        float k = pa_clamp01(gp / 8.0f);
        pa_tone(520.0f + 200.0f * k, 1040.0f + 500.0f * k, 0.12f, 1, 0.12f);
        pa_tone(780.0f + 260.0f * k, 1560.0f + 400.0f * k, 0.18f, 0, 0.08f);
        S.shake = 0.10f + 0.25f * k;
        (void)sx; (void)sy;
    }
}

static void kill_player(int q, int killer, int cause) {
    Player *v = &S.pl[q];
    if (!v->used || !v->alive) return;
    if (v->human && (S.phase != PH_PLAY || pa_demo_mode() == 1 || pa_demo_mode() == 3)) {
        /* Captures that need you alive: the cut only costs the trail. */
        if (S.phase == PH_PLAY) clear_trail(q);
        return;
    }
    float final = pct_of(q);
    if (v->human) {
        S.final_pct = final;
        S.final_kills = v->kills;
        snapshot_standings();
    }
    v->alive = 0; v->dying = 1; v->fade = 0.0f;
    v->respawn = pa_rng_range(&S.rng, 2.4f, 4.0f);
    /* The cut trail shrinks away rather than vanishing. */
    memcpy(v->gp, v->tp, sizeof(PA_Vec2) * (size_t)v->tn);
    v->gn = v->tn; v->gt = v->tn > 1 ? 0.45f : 0.0f;
    clear_trail(q);
    if (cause == CAUSE_CUT) {
        burst(S.cut_x, S.cut_y, 0.3f, 14, PA_RGB(255, 255, 255), pa_hex(0xFF4F6A), 2, 6.0f);
        burst(S.cut_x, S.cut_y, 0.3f, 8, pa_hex(SKINS[v->skin].land), PA_RGB(255, 255, 255), 0, 5.0f);
    }
    uint8_t me = (uint8_t)(q + 1);
    for (int c = 0; c < NCELL; c++) {
        if (S.own[c] == me) { S.cnt[me]--; S.cnt[0]++; S.own[c] = 0; }
        if (S.rev[c] >= 0.0f && S.revo[c] == me) S.rev[c] = -1.0f;
    }
    const Skin *sk = &SKINS[v->skin];
    burst(v->x, v->y, 0.8f, 30, pa_hex(sk->top), pa_hex(sk->land), 1, 10.0f);
    burst(v->x, v->y, 0.8f, 14, PA_RGB(255, 255, 255), pa_hex(sk->body), 0, 8.0f);
    add_part(v->x, v->y, 0.0f, 0, 0, 0, 0.55f, 4.5f, PA_RGB(255, 255, 255), 3);
    add_part(v->x, v->y, 0.0f, 0, 0, 0, 0.40f, 3.0f, pa_hex(sk->land), 3);

    if (killer >= 0 && killer != q) {
        Player *k = &S.pl[killer];
        k->kills++;
        if (k->human) {
            S.streak = S.streak_t > 0.0f ? S.streak + 1 : 1;
            S.streak_t = 4.0f;
            static const char *TXT[] = { "KILL!", "DOUBLE KILL!", "TRIPLE KILL!", "MEGA KILL!", "UNSTOPPABLE!" };
            banner(TXT[S.streak > 5 ? 4 : S.streak - 1], pa_hex(0xFFE14A));
            pa_tone(660, 1320, 0.10f, 2, 0.10f);
            pa_tone(990, 1980, 0.20f, 1, 0.10f);
            pa_noise(0.12f, 0.10f);
            S.shake = 0.45f;
            S.flash = 0.25f; S.flash_col = PA_RGB(255, 255, 255);
            add_text(v->x, v->y, v->name, pa_hex(SKINS[v->skin].land), 22.0f);
        }
    }
    if (v->human) {
        S.cause = cause;
        S.killer_i = killer;
        snprintf(S.killer, sizeof(S.killer), "%s", killer >= 0 && killer != q ? S.pl[killer].name : "");
        S.phase = PH_DEAD; S.pt = 0.0f;
        S.shake = 0.8f;
        S.flash = 0.35f; S.flash_col = PA_RGB(255, 90, 110);
        pa_sfx("lose");
        pa_noise(0.35f, 0.18f);
    } else {
        pa_tone(300, 120, 0.16f, 3, 0.05f);
    }
}

static void end_round(int won);

/* ------------------------------------------------------------- movement -- */
static void enter_cell(int i, int cx, int cy) {
    Player *p = &S.pl[i];
    if (cx < 0 || cy < 0 || cx >= G || cy >= G) return;
    int c = cy * G + cx;
    if (!S.arena[c]) return;
    uint8_t me = (uint8_t)(i + 1);
    uint8_t t = S.trl[c];
    if (t && t != me) { S.cut_x = (float)cx + 0.5f; S.cut_y = (float)cy + 0.5f; kill_player(t - 1, i, CAUSE_CUT); }
    if (t == me && (int)S.tix[c] + 4 < p->tcount) { kill_player(i, i, CAUSE_SELF); return; }
    if (S.own[c] == me) {
        if (p->out) claim(i);
        return;
    }
    if (!p->out) {
        p->out = 1; p->tn = 0; p->tacc = 0;
        float bx = p->x - cosf(p->head) * 0.6f, by = p->y - sinf(p->head) * 0.6f;
        p->tp[p->tn].x = bx; p->tp[p->tn].y = by; p->tn++;
        if (p->human) pa_tone(330, 440, 0.05f, 0, 0.04f);
    }
    if (t != me) {
        S.trl[c] = me;
        S.tix[c] = (uint16_t)(p->tcount < 65000 ? p->tcount : 65000);
        p->tcount++;
    }
}

static void move_player(int i, float dt) {
    Player *p = &S.pl[i];
    float nx = p->x + cosf(p->head) * SPEED * dt, ny = p->y + sinf(p->head) * SPEED * dt;
    float dx = nx - ACX, dy = ny - ACY, d = sqrtf(dx * dx + dy * dy);
    float lim = AR - 1.0f;
    if (d > lim) {
        float ux = dx / d, uy = dy / d;
        nx = ACX + ux * lim; ny = ACY + uy * lim;
        float tx = -uy, ty = ux;
        if (cosf(p->head) * tx + sinf(p->head) * ty < 0) { tx = -tx; ty = -ty; }
        p->head = atan2f(ty, tx);
    }
    p->x = nx; p->y = ny;
    int cx = (int)floorf(nx), cy = (int)floorf(ny);
    int guard = 0;
    while ((cx != p->cx || cy != p->cy) && guard++ < 4) {
        if (cx != p->cx) p->cx += cx > p->cx ? 1 : -1;
        else p->cy += cy > p->cy ? 1 : -1;
        enter_cell(i, p->cx, p->cy);
        if (!p->alive) return;
    }
    p->cx = cx; p->cy = cy;

    /* Touching a rival trail with the body counts, not just the centre. */
    for (int oy = -1; oy <= 1; oy++)
        for (int ox = -1; ox <= 1; ox++) {
            int c = cell_at(nx + (float)ox, ny + (float)oy);
            if (c < 0) continue;
            int t = S.trl[c];
            if (!t || t == i + 1) continue;
            float ex = (float)(c % G) + 0.5f - nx, ey = (float)(c / G) + 0.5f - ny;
            if (ex * ex + ey * ey < 1.1f) {
                S.cut_x = (float)(c % G) + 0.5f; S.cut_y = (float)(c / G) + 0.5f;
                kill_player(t - 1, i, CAUSE_CUT);
            }
        }

    if (p->out) {
        p->tacc += SPEED * dt;
        if (p->tacc >= 0.45f && p->tn < TPTS - 1) {
            p->tacc = 0;
            p->tp[p->tn].x = nx; p->tp[p->tn].y = ny; p->tn++;
        }
    }
}

/* -------------------------------------------------------------------- AI -- */
static int nearest_own(int i, float x, float y, float *tx, float *ty) {
    uint8_t me = (uint8_t)(i + 1);
    int cx = (int)floorf(x), cy = (int)floorf(y);
    for (int r = 0; r < 60; r++) {
        float best = 1e9f; int bc = -1;
        for (int oy = -r; oy <= r; oy++)
            for (int ox = -r; ox <= r; ox++) {
                if (oy != -r && oy != r && ox != -r && ox != r) continue;
                int nx = cx + ox, ny = cy + oy;
                if (nx < 0 || ny < 0 || nx >= G || ny >= G) continue;
                int c = ny * G + nx;
                if (S.own[c] != me) continue;
                float d = (float)(ox * ox + oy * oy);
                if (d < best) { best = d; bc = c; }
            }
        if (bc >= 0) { *tx = (float)(bc % G) + 0.5f; *ty = (float)(bc / G) + 0.5f; return 1; }
    }
    return 0;
}

/* True when the straight run from (x,y) to (tx,ty) crosses none of i's own
   older trail. */
static int line_clear(int i, float x, float y, float tx, float ty) {
    Player *p = &S.pl[i];
    float dx = tx - x, dy = ty - y, L = sqrtf(dx * dx + dy * dy);
    int n = (int)(L * 2.0f) + 1;
    for (int k = 1; k <= n; k++) {
        float t = (float)k / (float)n;
        int c = cell_at(x + dx * t, y + dy * t);
        if (c < 0) return 0;
        if (S.trl[c] == i + 1 && (int)S.tix[c] + 4 < p->tcount) return 0;
    }
    return 1;
}

/* Nearest home cell you can reach without crossing your own trail. */
static int nearest_home_clear(int i, float x, float y, float *tx, float *ty) {
    uint8_t me = (uint8_t)(i + 1);
    int cx = (int)floorf(x), cy = (int)floorf(y);
    float best = 1e9f; int found_r = -1;
    for (int r = 0; r < 60; r++) {
        if (found_r >= 0 && r > found_r + 5) break;
        for (int oy = -r; oy <= r; oy++)
            for (int ox = -r; ox <= r; ox++) {
                if (oy != -r && oy != r && ox != -r && ox != r) continue;
                int nx = cx + ox, ny = cy + oy;
                if (nx < 0 || ny < 0 || nx >= G || ny >= G) continue;
                if (S.own[ny * G + nx] != me) continue;
                float d = (float)(ox * ox + oy * oy);
                if (d >= best) continue;
                float fx = (float)nx + 0.5f, fy = (float)ny + 0.5f;
                if (!line_clear(i, x, y, fx, fy)) continue;
                best = d; *tx = fx; *ty = fy;
                if (found_r < 0) found_r = r;
            }
    }
    return found_r >= 0;
}

static int probe_ok(int i, float x, float y, float a) {
    Player *p = &S.pl[i];
    static const float D[4] = { 1.0f, 2.0f, 3.1f, 4.3f };
    for (int k = 0; k < 4; k++) {
        float px = x + cosf(a) * D[k], py = y + sinf(a) * D[k];
        float dx = px - ACX, dy = py - ACY;
        if (dx * dx + dy * dy > (AR - 1.6f) * (AR - 1.6f)) return 0;
        int c = cell_at(px, py);
        if (c < 0) return 0;
        if (S.trl[c] == i + 1 && (int)S.tix[c] + 4 < p->tcount) return 0;
    }
    return 1;
}

static float steer_safe(int i, float want) {
    Player *p = &S.pl[i];
    static const float OFF[] = { 0.0f, 0.3f, 0.6f, 0.9f, 1.2f, 1.6f, 2.0f, 2.5f, 3.0f };
    for (int k = 0; k < (int)(sizeof(OFF) / sizeof(OFF[0])); k++) {
        float a1 = want + OFF[k] * p->side, a2 = want - OFF[k] * p->side;
        if (probe_ok(i, p->x, p->y, a1)) return a1;
        if (k && probe_ok(i, p->x, p->y, a2)) { p->side = -p->side; return a2; }
    }
    return want;
}

static void ai_plan(int i) {
    Player *p = &S.pl[i];
    uint8_t me = (uint8_t)(i + 1);
    float best = -1e9f, ba = p->head;
    for (int k = 0; k < 12; k++) {
        float a = (float)k / 12.0f * PA_TAU + pa_rng_range(&S.rng, -0.2f, 0.2f);
        float score = 0; int exit = -1;
        for (int st = 1; st < 22; st++) {
            float x = p->x + cosf(a) * (float)st, y = p->y + sinf(a) * (float)st;
            float dx = x - ACX, dy = y - ACY;
            if (dx * dx + dy * dy > (AR - 3.0f) * (AR - 3.0f)) { score -= (float)(22 - st) * 1.5f; break; }
            int c = cell_at(x, y);
            if (c < 0) break;
            if (S.own[c] != me) {
                /* Captures that show your land growing keep rivals off it. */
                if (S.own[c] == 1 && !p->human && pa_demo_mode() == 1) score -= 3.0f;
                else score += S.own[c] ? 1.6f : 1.0f;
                if (exit < 0) exit = st;
            }
        }
        for (int q = 0; q < MAXP; q++) {
            Player *o = &S.pl[q];
            if (q == i || !o->used || !o->alive) continue;
            float ex = o->x - (p->x + cosf(a) * 8.0f), ey = o->y - (p->y + sinf(a) * 8.0f);
            float d2 = ex * ex + ey * ey;
            if (d2 < 100.0f) score -= (10.0f - sqrtf(d2)) * 2.0f * p->caution;
        }
        score += pa_rng_range(&S.rng, 0, 6.0f);
        if (exit > 0) score -= (float)exit * 0.4f;
        if (score > best) { best = score; ba = a; }
    }
    /* Find where the heading leaves home, then plan a box loop out from it. */
    float ex = p->x, ey = p->y;
    for (int st = 0; st < 40; st++) {
        int c = cell_at(ex, ey);
        if (c < 0 || S.own[c] != me) break;
        ex += cosf(ba); ey += sinf(ba);
    }
    float d1 = pa_rng_range(&S.rng, 3.5f, 6.0f + p->greed * 9.0f);
    float d2 = pa_rng_range(&S.rng, 3.5f, 5.0f + p->greed * 10.0f);
    float turn = pa_rng_chance(&S.rng, 0.5f) ? 1.0f : -1.0f;
    float px = -sinf(ba) * turn, py = cosf(ba) * turn;
    p->wx[0] = ex + cosf(ba) * d1; p->wy[0] = ey + sinf(ba) * d1;
    p->wx[1] = p->wx[0] + px * d2; p->wy[1] = p->wy[0] + py * d2;
    p->wx[2] = p->wx[1] - cosf(ba) * d1 * 0.5f; p->wy[2] = p->wy[1] - sinf(ba) * d1 * 0.5f;
    for (int k = 0; k < 3; k++) {
        float dx = p->wx[k] - ACX, dy = p->wy[k] - ACY, d = sqrtf(dx * dx + dy * dy);
        if (d > AR - 4.0f) { p->wx[k] = ACX + dx / d * (AR - 4.0f); p->wy[k] = ACY + dy / d * (AR - 4.0f); }
    }
    p->leg = 0; p->nlegs = 3; p->st = AI_PLAN;
}

static void ai_update(int i, float dt) {
    Player *p = &S.pl[i];
    int demo = pa_demo_mode();
    p->think -= dt;
    if (p->think <= 0.0f) {
        p->think = 0.12f + pa_rng_next(&S.rng) * 0.08f;
        float hx = p->x, hy = p->y, hd = 0.0f;
        if (nearest_own(i, p->x, p->y, &hx, &hy)) hd = hypotf(hx - p->x, hy - p->y);

        if (p->out) {
            float threat = 1e9f;
            for (int q = 0; q < MAXP; q++) {
                Player *o = &S.pl[q];
                if (q == i || !o->used || !o->alive) continue;
                if (o->human && S.phase != PH_PLAY) continue;
                float d = hypotf(o->x - p->x, o->y - p->y);
                for (int k = 0; k < p->tn; k += 3) {
                    float e = hypotf(o->x - p->tp[k].x, o->y - p->tp[k].y);
                    if (e < d) d = e;
                }
                if (d < threat) threat = d;
            }
            int limit = (int)(26.0f + p->greed * 56.0f);
            if (p->human) limit = 140;
            /* Review capture of a death: you push your luck and stay out. */
            int bait = p->human && demo == 2 && S.pt > 12.0f;
            if (bait) { threat = 1e9f; limit = 400; p->greed = 2.2f; }
            if (threat < hd * 0.9f + 2.0f + p->caution * 3.5f || p->tcount > limit) {
                if (p->st != AI_ATTACK) p->st = AI_RETURN;
            }
        }

        /* Rival trails in reach are the best thing on the map. */
        float aggr = p->aggr;
        if (p->human) aggr = demo == 3 ? 1.0f : 0.0f;
        int bq = -1; float bd = 1e9f, bx = 0, by = 0;
        if (aggr > 0.0f && (!p->out || p->tcount < 30)) {
            for (int q = 0; q < MAXP; q++) {
                Player *o = &S.pl[q];
                if (q == i || !o->used || !o->alive || !o->out || o->tn < 3) continue;
                float ag = aggr;
                if (o->human) {
                    if (S.phase != PH_PLAY || demo == 1 || demo == 3) continue;
                    ag *= 0.5f + pct_of(0) / 25.0f;
                    if (demo == 2 && S.pt > 12.0f) ag = 1.6f;
                }
                float reach = 5.0f + 9.0f * ag;
                float home = hypotf(o->x - o->tp[0].x, o->y - o->tp[0].y);
                for (int k = 0; k < o->tn; k += 2) {
                    float d = hypotf(o->tp[k].x - p->x, o->tp[k].y - p->y);
                    if (d < reach && d < home + 5.0f && d < bd) { bd = d; bq = q; bx = o->tp[k].x; by = o->tp[k].y; }
                }
            }
        }
        if (bq >= 0) { p->st = AI_ATTACK; p->tq = bq; p->tx = bx; p->ty = by; }
        else if (p->st == AI_ATTACK) { p->st = p->out ? AI_RETURN : AI_PLAN; p->nlegs = 0; }

        if (p->st == AI_PLAN && !p->out && p->nlegs == 0) ai_plan(i);
        if (p->st == AI_RETURN && !p->out) { p->nlegs = 0; ai_plan(i); }
        if (p->st == AI_RETURN) {
            p->tx = hx; p->ty = hy;
            if (p->out) nearest_home_clear(i, p->x, p->y, &p->tx, &p->ty);
        }
    }

    if (p->st == AI_PLAN) {
        if (p->nlegs == 0) ai_plan(i);
        float dx = p->wx[p->leg] - p->x, dy = p->wy[p->leg] - p->y;
        if (dx * dx + dy * dy < 2.2f) {
            p->leg++;
            if (p->leg >= p->nlegs) { p->st = AI_RETURN; p->think = 0.0f; p->nlegs = 0; }
        }
        if (p->st == AI_PLAN) { p->tx = p->wx[p->leg]; p->ty = p->wy[p->leg]; }
    }
    float want = atan2f(p->ty - p->y, p->tx - p->x);
    p->want = steer_safe(i, want);
}

/* --------------------------------------------------------------- results -- */
/* Standings at the moment the run ended: everyone still alive plus you. */
static void snapshot_standings(void) {
    S.res_n = 0;
    for (int i = 0; i < MAXP; i++) {
        Player *p = &S.pl[i];
        if (!p->used || (!p->alive && i != 0)) continue;
        snprintf(S.res[S.res_n].name, sizeof(S.res[0].name), "%s", i == 0 ? "YOU" : p->name);
        S.res[S.res_n].skin = p->skin;
        S.res[S.res_n].pct = i == 0 ? S.final_pct : pct_of(i);
        S.res_n++;
    }
    for (int a = 0; a < S.res_n; a++)
        for (int b = a + 1; b < S.res_n; b++)
            if (S.res[b].pct > S.res[a].pct || (S.res[b].pct == S.res[a].pct && !strcmp(S.res[b].name, "YOU"))) {
                char tn[12]; int ts = S.res[a].skin; float tp = S.res[a].pct;
                memcpy(tn, S.res[a].name, 12);
                memcpy(S.res[a].name, S.res[b].name, 12); S.res[a].skin = S.res[b].skin; S.res[a].pct = S.res[b].pct;
                memcpy(S.res[b].name, tn, 12); S.res[b].skin = ts; S.res[b].pct = tp;
            }
    S.res_me = 0;
    for (int a = 0; a < S.res_n; a++) if (!strcmp(S.res[a].name, "YOU")) S.res_me = a;
    S.rank = S.res_me + 1;
}

static void end_round(int won) {
    (void)won;

    int tenths = (int)(S.final_pct * 10.0f + 0.5f);
    int old_best = g_best;
    S.new_best = tenths > g_best;
    if (S.new_best) g_best = tenths;
    S.unlocked = -1;
    for (int k = 0; k < SKIN_COUNT; k++)
        if (SKINS[k].unlock * 10 > old_best && SKINS[k].unlock * 10 <= g_best) S.unlocked = k;
    g_total_kills += S.final_kills;
    g_runs++;
    S.reward = 10 + (int)(S.final_pct * 3.0f) + S.final_kills * 15 + (S.rank == 1 ? 25 : 0);
    g_stars += S.reward;
    S.confetti_done = 0;
    if (!pa_demo_mode()) {
        pa_save_set("paper.stars", g_stars);
        pa_save_set("paper.best", g_best);
        pa_save_set("paper.kills", g_total_kills);
        pa_save_set("paper.runs", g_runs);
        pa_save_flush();
    }
    S.phase = PH_RESULTS; S.pt = 0.0f;
}

/* ---------------------------------------------------------------- update -- */
static void s_start(void) {
    if (!g_loaded && pa_demo_mode()) g_loaded = 1;     /* captures start from a clean profile */
    if (!g_loaded) {
        g_best = pa_save_get("paper.best", 0);
        g_skin = pa_save_get("paper.skin", 0);
        g_runs = pa_save_get("paper.runs", 0);
        g_total_kills = pa_save_get("paper.kills", 0);
        g_stars = pa_save_get("paper.stars", 0);
        g_loaded = 1;
    }
    if (g_skin < 0 || g_skin >= SKIN_COUNT || SKINS[g_skin].unlock * 10 > g_best) g_skin = 0;
    new_round(pa_demo_mode() ? 7771u : 0x9E3779B9u ^ (uint32_t)(g_runs * 7919 + 17));
    S.phase = PH_TITLE;
    S.pt = 0.0f;
}

static void s_stop(void) {}

static void start_play(void) {
    Player *me = &S.pl[0];
    if (!me->alive) return;
    stamp_home(0, me->x, me->y, HOME_R, 1);
    S.phase = PH_PLAY; S.pt = 0.0f;
    S.drag = 0;
    pa_sfx("good");
    me->squash = 1.0f;
    if (!pa_demo_mode()) pa_save_set("paper.skin", g_skin);
}

static void change_skin(int dir) {
    if (dir == 0) return;
    g_skin = (g_skin + dir + SKIN_COUNT * 2) % SKIN_COUNT;
    S.pl[0].skin = g_skin;
    for (int i = 1; i < MAXP; i++)
        if (S.pl[i].used && S.pl[i].skin == g_skin) {
            char keep[12];
            memcpy(keep, S.pl[i].name, sizeof(keep));
            pick_skin_name(i);
            memcpy(S.pl[i].name, keep, sizeof(keep));
        }
    S.nudge = (float)dir;
    S.pl[0].squash = 1.0f;
    pa_tone(560, 760, 0.06f, 1, 0.08f);
}

static int skin_locked(int s) { return SKINS[s].unlock * 10 > g_best; }

/* Title layout, shared by update (hit tests) and render. */
/* The skin cards on the title: two rows of five. */
static void skin_card(int w, int h, int k, float *x, float *y, float *sz) {
    float size = fminf(62.0f, (float)w * 0.115f);
    float gap = size * 0.18f;
    float row_w = size * 5.0f + gap * 4.0f;
    *sz = size;
    *x = ((float)w - row_w) * 0.5f + (float)(k % 5) * (size + gap);
    *y = (float)h * 0.255f + (float)(k / 5) * (size + gap + 4.0f);
}

static void title_arrows(int w, int h, float *lx, float *rx, float *ay, float *r) {
    *ay = (float)h * 0.56f - 22.0f;
    *lx = (float)w * 0.5f - 118.0f;
    *rx = (float)w * 0.5f + 118.0f;
    *r = 30.0f;
}

static int g_vw = 540, g_vh = 1170;

static void results_buttons(int w, int h, float *bx, float *by, float *bw, float *bh, float *my) {
    *bw = (float)w * 0.62f; *bh = 70.0f;
    *bx = ((float)w - *bw) * 0.5f;
    *by = fminf((float)h - 150.0f, S.res_btn_y > 0.0f ? S.res_btn_y : 1e9f);
    *my = *by + *bh + 42.0f;
}

static void step_world(float dt) {
    int demo = pa_demo_mode();
    for (int i = 0; i < MAXP; i++) {
        Player *p = &S.pl[i];
        if (!p->used) continue;
        p->pop = pa_clamp01(p->pop + dt * 3.2f);
        p->squash = pa_approach(p->squash, 0.0f, 9.0f, dt);
        if (p->gt > 0.0f) p->gt -= dt;
        if (p->dying) {
            p->fade += dt * 1.6f;
            if (p->fade >= 1.0f) {
                p->dying = 0;
                uint8_t me = (uint8_t)(i + 1);
                for (int c = 0; c < NCELL; c++)
                    if (S.vis[c] == me) { S.vis[c] = S.own[c]; mark_dirty(c % G, c / G); }
            }
        }
        if (!p->alive) {
            if (!p->human && !p->dying && S.budget > 0) {
                p->respawn -= dt;
                if (p->respawn <= 0.0f) {
                    float x, y;
                    if (find_spawn(&x, &y)) {
                        pick_skin_name(i);
                        spawn_player(i, 0, x, y, 1);
                        S.budget--;
                    } else p->respawn = 1.0f;
                }
            }
            continue;
        }
        {
            int c = cell_at(p->x, p->y);
            p->z = pa_approach(p->z, (c >= 0 && S.vis[c]) ? SLAB_Z : 0.0f, 14.0f, dt);
        }
        int moving = !(p->human && S.phase == PH_TITLE);
        if (!moving) continue;
        if (!p->human || demo) ai_update(i, dt);
        float rate = p->human ? 7.5f : 4.6f;
        p->head = turn_toward(p->head, p->want, rate * dt);
        move_player(i, dt);
        p->hop += dt;
    }

    /* Heads meeting: whoever is out on a trail loses. */
    for (int a = 0; a < MAXP; a++)
        for (int b = a + 1; b < MAXP; b++) {
            Player *A = &S.pl[a], *B = &S.pl[b];
            if (!A->used || !B->used || !A->alive || !B->alive) continue;
            if ((A->human || B->human) && S.phase != PH_PLAY) continue;
            float dx = A->x - B->x, dy = A->y - B->y;
            if (dx * dx + dy * dy > 1.25f * 1.25f) continue;
            if (A->out && !B->out) kill_player(a, b, CAUSE_HEAD);
            else if (B->out && !A->out) kill_player(b, a, CAUSE_HEAD);
            else if (A->out && B->out) {
                if (A->tcount >= B->tcount) kill_player(a, b, CAUSE_HEAD);
                else kill_player(b, a, CAUSE_HEAD);
            }
        }
}

static void s_update(float dt, const PA_Input *in) {
    int demo = pa_demo_mode();
    S.t += dt; S.pt += dt;
    process_reveals();

    Player *me = &S.pl[0];
    switch (S.phase) {
    case PH_TITLE: {
        float lx, rx, ay, r;
        title_arrows(g_vw, g_vh, &lx, &rx, &ay, &r);
        S.nudge = pa_approach(S.nudge, 0.0f, 10.0f, dt);
        S.lock_shake = pa_approach(S.lock_shake, 0.0f, 6.0f, dt);
        if (in->key_pressed[PA_KEY_LEFT]) change_skin(-1);
        if (in->key_pressed[PA_KEY_RIGHT]) change_skin(1);
        int go = in->key_pressed[PA_KEY_SPACE] || in->key_pressed[PA_KEY_ENTER];
        if (in->tapped) {
            float dl = hypotf(in->x - lx, in->y - ay), dr = hypotf(in->x - rx, in->y - ay);
            int card = -1;
            for (int k = 0; k < SKIN_COUNT; k++) {
                float cx, cy, cs;
                skin_card(g_vw, g_vh, k, &cx, &cy, &cs);
                if (in->x >= cx && in->x <= cx + cs && in->y >= cy && in->y <= cy + cs) card = k;
            }
            if (card >= 0) change_skin(card - g_skin);
            else if (dl < r + 18.0f) change_skin(-1);
            else if (dr < r + 18.0f) change_skin(1);
            else if (in->y > 110.0f) go = 1;
        }
        if (demo && S.pt > 1.2f) go = 1;
        if (go) {
            if (skin_locked(g_skin)) { S.lock_shake = 1.0f; pa_sfx("bad"); }
            else start_play();
        }
        step_world(dt);
        break;
    }
    case PH_PLAY: {
        if (!demo) {
            if (in->pressed) { S.drag = 1; S.ax = in->x; S.ay = in->y; }
            if (in->down && S.drag) {
                float dx = in->x - S.ax, dy = in->y - S.ay, l = sqrtf(dx * dx + dy * dy);
                if (l > 8.0f) me->want = atan2f(dy / VIEW_K, dx);
                if (l > 64.0f) { S.ax = in->x - dx / l * 64.0f; S.ay = in->y - dy / l * 64.0f; }
            }
            if (in->released) S.drag = 0;
            float kx = (float)(in->keys[PA_KEY_RIGHT] - in->keys[PA_KEY_LEFT]);
            float ky = (float)(in->keys[PA_KEY_DOWN] - in->keys[PA_KEY_UP]);
            if (kx != 0.0f || ky != 0.0f) me->want = atan2f(ky, kx);
            if (in->swipe == PA_SWIPE_LEFT) me->want = PA_PI;
            else if (in->swipe == PA_SWIPE_RIGHT) me->want = 0.0f;
            else if (in->swipe == PA_SWIPE_UP) me->want = -PA_PI * 0.5f;
            else if (in->swipe == PA_SWIPE_DOWN) me->want = PA_PI * 0.5f;
        }
        step_world(dt);
        if (S.phase == PH_PLAY) {
            float pc = pct_of(0);
            if (pc > S.peak) S.peak = pc;
            int rivals = 0;
            for (int i = 1; i < MAXP; i++) if (S.pl[i].used && (S.pl[i].alive || S.pl[i].dying)) rivals++;
            if (pc >= 99.5f || (rivals == 0 && S.budget == 0)) {
                S.phase = PH_WIN; S.pt = 0.0f;
                S.final_pct = pc; S.final_kills = me->kills;
                S.cause = CAUSE_NONE;
                snapshot_standings();
                pa_sfx("win");
                S.flash = 0.5f; S.flash_col = PA_RGB(255, 236, 140);
                for (int k = 0; k < 60; k++)
                    add_part(me->x + pa_rng_range(&S.rng, -12, 12), me->y + pa_rng_range(&S.rng, -18, 8),
                             pa_rng_range(&S.rng, 6, 14), 0, 0, 0, pa_rng_range(&S.rng, 1.5f, 2.5f), 0.35f,
                             pa_hsl(pa_rng_next(&S.rng), 0.85f, 0.62f), 0);
            }
            /* Review capture of the win: the arena empties out. */
            if (demo == 4 && S.pt > 2.0f && S.budget > 0) {
                S.budget = 0;
                for (int i = 1; i < MAXP; i++) kill_player(i, -1, CAUSE_CUT);
            }
            /* Review capture: the run ends on cue so the results card is seen. */
            if (demo == 2 && S.pt > 30.0f) {
                int k = 1; float bd = 1e9f;
                for (int i = 1; i < MAXP; i++)
                    if (S.pl[i].alive) { float d = hypotf(S.pl[i].x - me->x, S.pl[i].y - me->y); if (d < bd) { bd = d; k = i; } }
                S.cut_x = me->x; S.cut_y = me->y;
                kill_player(0, k, CAUSE_CUT);
            }
        }
        break;
    }
    case PH_DEAD:
        step_world(dt);
        if (S.pt > 1.7f) end_round(0);
        break;
    case PH_WIN:
        step_world(dt);
        if (S.pt > 2.6f) end_round(1);
        break;
    case PH_RESULTS: {
        float bx, by, bw, bh, my;
        results_buttons(g_vw, g_vh, &bx, &by, &bw, &bh, &my);
        if (S.pt < REVEAL_T) {
            if (in->tapped && S.pt > 0.3f) S.pt = REVEAL_T;
            if (S.confetti_done == 0 && S.pt > 0.85f) {
                /* The chest pops: confetti out of it. */
                S.confetti_done = 1;
                pa_tone(400, 1200, 0.25f, 1, 0.14f);
                pa_noise(0.15f, 0.12f);
                for (int k = 0; k < 70; k++) {
                    float an = pa_rng_range(&S.rng, PA_PI * 1.05f, PA_PI * 1.95f), sp2 = pa_rng_range(&S.rng, 0.25f, 0.8f);
                    add_part(0.5f, 0.5f, 0, cosf(an) * sp2 * 0.7f, sinf(an) * sp2, 0, pa_rng_range(&S.rng, 2.5f, 4.0f), 0.4f,
                             pa_hsl(pa_rng_next(&S.rng), 0.85f, 0.62f), 9);
                }
            }
        } else if (S.confetti_done < 2) {
            S.confetti_done = 2;
            pa_sfx(S.rank == 1 ? "win" : "levelup");
            for (int k = 0; k < 64; k++)
                add_part(pa_rng_range(&S.rng, 0, 1), pa_rng_range(&S.rng, -0.3f, -0.02f), 0, pa_rng_range(&S.rng, -0.05f, 0.05f),
                         pa_rng_range(&S.rng, 0.05f, 0.2f), 0, 5.0f, 0.4f, pa_hsl(pa_rng_next(&S.rng), 0.85f, 0.62f), 9);
        }
        if (S.pt > REVEAL_T + 0.5f) {
            int retry = in->key_pressed[PA_KEY_SPACE] || in->key_pressed[PA_KEY_ENTER];
            int menu = in->key_pressed[PA_KEY_ESC];
            if (in->tapped) {
                if (in->x > bx && in->x < bx + bw && in->y > by && in->y < by + bh) retry = 1;
                else if (fabsf(in->y - my) < 30.0f && fabsf(in->x - (float)g_vw * 0.5f) < 110.0f) menu = 1;
            }
            if (retry || menu) {
                pa_sfx("select");
                new_round(0x9E3779B9u ^ (uint32_t)(g_runs * 7919 + 17));
                if (retry) { S.phase = PH_TITLE; start_play(); }
                else { S.phase = PH_TITLE; S.pt = 0.0f; }
            }
        }
        /* Confetti keeps falling. */
        if (S.pt > REVEAL_T && pa_rng_chance(&S.rng, 0.3f))
            add_part(pa_rng_range(&S.rng, 0, 1), -0.05f, 0, pa_rng_range(&S.rng, -0.02f, 0.02f), 0.18f, 0,
                     4.0f, 0.4f, pa_hsl(pa_rng_next(&S.rng), 0.8f, 0.64f), 9);
        break;
    }
    }

    /* Particles: world kinds fall under gravity, kind 9 is screen confetti. */
    for (int i = 0; i < MAXPART; i++) {
        Part *p = &S.parts[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        p->rot += p->vr * dt;
        if (p->kind == 9) {
            p->x += p->vx * dt; p->y += p->vy * dt;
            p->vx = p->vx * (1.0f - 1.2f * dt) + sinf(p->rot) * 0.02f * dt;
            p->vy = p->vy * (1.0f - 1.2f * dt) + 0.32f * dt;
            continue;
        }
        p->x += p->vx * dt; p->y += p->vy * dt; p->z += p->vz * dt;
        p->vz -= 22.0f * dt;
        p->vx *= 1.0f - 1.5f * dt; p->vy *= 1.0f - 1.5f * dt;
        if (p->z < 0.0f) { p->z = 0.0f; p->vz = -p->vz * 0.35f; }
    }
    for (int i = 0; i < MAXFT; i++) if (S.ft[i].t > 0.0f) S.ft[i].t -= dt;
    if (S.banner_t > 0.0f) S.banner_t -= dt;
    if (S.streak_t > 0.0f) S.streak_t -= dt;
    S.shake = pa_approach(S.shake, 0.0f, 5.0f, dt);
    if (S.flash > 0.0f) S.flash -= dt;

    /* Camera: follows with a little lead in the travel direction. */
    float lead = (S.phase == PH_PLAY && me->alive) ? 2.6f : 0.0f;
    float tx = me->x + cosf(me->head) * lead, ty = me->y + sinf(me->head) * lead;
    if (S.phase == PH_DEAD && S.killer_i > 0 && S.pl[S.killer_i].alive) {
        /* Swing over to whoever did it. */
        tx = (me->x + S.pl[S.killer_i].x) * 0.5f;
        ty = (me->y + S.pl[S.killer_i].y) * 0.5f;
    }
    S.cam_x = pa_approach(S.cam_x, tx, 5.0f, dt);
    S.cam_y = pa_approach(S.cam_y, ty, 5.0f, dt);
}

/* ================================================================ drawing == */
static float g_s = 20.0f, g_ax, g_ay;
static float g_foff[MAXP + 1];
static uint32_t g_top[MAXP + 1], g_side[MAXP + 1], g_lip[MAXP + 1];
static float g_aa = 50.0f;

static inline PA_Vec2 proj(float x, float y, float z) {
    PA_Vec2 o;
    o.x = g_ax + (x - S.cam_x) * g_s;
    o.y = g_ay + (y - S.cam_y) * g_s * VIEW_K - z * g_s * VIEW_Z;
    return o;
}

static inline uint32_t mixc(uint32_t a, uint32_t b, int t) {
    if (t <= 0) return a;
    if (t >= 256) return b;
    int r = (int)((a >> 16) & 255), g = (int)((a >> 8) & 255), bl = (int)(a & 255);
    int r2 = (int)((b >> 16) & 255), g2 = (int)((b >> 8) & 255), b2 = (int)(b & 255);
    r += ((r2 - r) * t) >> 8; g += ((g2 - g) * t) >> 8; bl += ((b2 - bl) * t) >> 8;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static void reblur(void) {
    if (!S.dirty) return;
    S.dirty = 0;
    int x0 = S.dt_x0 - 2, x1 = S.dt_x1 + 2, y0 = S.dt_y0 - 2, y1 = S.dt_y1 + 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > G - 1) x1 = G - 1;
    if (y1 > G - 1) y1 = G - 1;
    int hy0 = y0 - 2 < 0 ? 0 : y0 - 2, hy1 = y1 + 2 > G - 1 ? G - 1 : y1 + 2;
    static uint8_t tmp[NCELL];
    static const int W5[5] = { 1, 4, 6, 4, 1 };
    for (int o = 1; o <= MAXP; o++) {
        uint8_t *f = S.fld[o - 1];
        for (int y = hy0; y <= hy1; y++)
            for (int x = x0; x <= x1; x++) {
                int s = 0;
                for (int k = -2; k <= 2; k++) {
                    int xx = x + k;
                    if (xx >= 0 && xx < G && S.vis[y * G + xx] == o) s += W5[k + 2];
                }
                tmp[y * G + x] = (uint8_t)s;
            }
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                int s = 0;
                for (int k = -2; k <= 2; k++) {
                    int yy = y + k;
                    if (yy >= 0 && yy < G) s += tmp[yy * G + x] * W5[k + 2];
                }
                f[y * G + x] = (uint8_t)((s * 255 + 128) / 256);
            }
    }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int c = y * G + x, bo = 0, bv = 0;
            for (int o = 1; o <= MAXP; o++) {
                int v = S.fld[o - 1][c];
                if (v > bv) { bv = v; bo = o; }
            }
            S.dom[c] = (uint8_t)bo; S.domv[c] = (uint8_t)bv;
        }
}

/* The field value of whichever land is strongest at a world point. */
static inline float land_sample(float wx, float wy, int *who) {
    float u = wx - 0.5f, w = wy - 0.5f;
    *who = 0;
    if (u < 0.0f || w < 0.0f) return 0.0f;
    int i0 = (int)u, j0 = (int)w;
    if (i0 >= G - 1 || j0 >= G - 1) return 0.0f;
    int c = j0 * G + i0;
    int m0 = S.domv[c], m1 = S.domv[c + 1], m2 = S.domv[c + G], m3 = S.domv[c + G + 1];
    if (!(m0 | m1 | m2 | m3)) return 0.0f;
    int d[4] = { S.dom[c], S.dom[c + 1], S.dom[c + G], S.dom[c + G + 1] };
    if (d[0] == d[1] && d[0] == d[2] && d[0] == d[3] && (m0 & m1 & m2 & m3) == 255) {
        *who = d[0];
        return 1.0f - g_foff[d[0]];
    }
    float fx = u - (float)i0, fy = w - (float)j0;
    float best = 0.0f;
    for (int k = 0; k < 4; k++) {
        int o = d[k];
        if (!o) continue;
        int dup = 0;
        for (int j = 0; j < k; j++) if (d[j] == o) dup = 1;
        if (dup) continue;
        const uint8_t *f = S.fld[o - 1];
        float a = (float)f[c] + ((float)f[c + 1] - (float)f[c]) * fx;
        float b = (float)f[c + G] + ((float)f[c + G + 1] - (float)f[c + G]) * fx;
        float v = (a + (b - a) * fy) * (1.0f / 255.0f) - g_foff[o];
        if (v > best) { best = v; *who = o; }
    }
    return best;
}

static inline float field_sample(const uint8_t *fd, float wx, float wy) {
    float u = wx - 0.5f, w = wy - 0.5f;
    if (u < 0.0f || w < 0.0f) return 0.0f;
    int i0 = (int)u, j0 = (int)w;
    if (i0 >= G - 1 || j0 >= G - 1) return 0.0f;
    int c = j0 * G + i0;
    int d0 = fd[c], d1 = fd[c + 1], d2 = fd[c + G], d3 = fd[c + G + 1];
    /* Away from a splat edge all four corners agree: skip the blend. */
    if (d0 < 100 && d1 < 100 && d2 < 100 && d3 < 100) return 0.0f;
    if (d0 > 170 && d1 > 170 && d2 > 170 && d3 > 170) return 1.0f;
    float fx = u - (float)i0, fy = w - (float)j0;
    float a = (float)d0 + ((float)d1 - (float)d0) * fx;
    float b = (float)d2 + ((float)d3 - (float)d2) * fx;
    return (a + (b - a) * fy) * (1.0f / 255.0f);
}

typedef struct { uint32_t ground; uint8_t who, cov, v, pad; } Base;
static Base *g_base;
static int   g_base_cap;
static uint8_t *g_tb;
static int   g_tb_cap;

#define C_MINT     0xCFF7E4
#define C_SPLAT    0xB8F0D2
#define C_WATER    0x10A8F5
#define C_WATER2   0x56D6FF
#define C_RIM_TOP  0x4FE8DC
#define C_RIM_SIDE 0x14AFC4
#define C_GRASS    0x9BDB26
#define C_GRASS_D  0x86C23A
#define C_NAVY     0x1D2A86
#define C_MEADOW   0xB2F0A0
#define C_MEADOW_D 0x9AE38C
#define C_SAND     0xFFEBA8
#define C_SAND_D   0xF2D27C

static uint32_t ground_colour(float wx, float wy, float d) {
    if (d < AR + 0.2f) {
        /* Three grounds: mint, meadow and sand, each edge a soft step. */
        uint32_t col = C_MINT;
        float v = field_sample(S.deco, wx, wy);
        if (v > 0.3f) {
            col = mixc(col, C_MEADOW_D, (int)((v - 0.38f) * 12.0f * 256.0f));
            col = mixc(col, C_MEADOW, (int)((v - 0.5f) * 12.0f * 256.0f));
        }
        float sd = field_sample(S.sand, wx, wy);
        if (sd > 0.3f) {
            col = mixc(col, C_SAND_D, (int)((sd - 0.38f) * 12.0f * 256.0f));
            col = mixc(col, C_SAND, (int)((sd - 0.5f) * 12.0f * 256.0f));
        }
        return col;
    }
    float out = d - (AR + RIM_W);
    uint32_t col = mixc(C_WATER2, C_WATER, (int)(pa_clamp01(out / 5.0f) * 256.0f));
    if (out < 0.7f) col = mixc(col, 0xE8FBFF, (int)((1.0f - pa_clamp01(out / 0.7f)) * 200.0f));
    else if (out < 16.0f) {
        /* Swell lines rolling in toward the rim. */
        float f = out * 0.42f + S.t * 0.55f + 0.15f * sinf(atan2f(wy - ACY, wx - ACX) * 7.0f);
        float fr = f - floorf(f);
        float k = 1.0f - fabsf(fr - 0.12f) / 0.12f;
        if (k > 0.0f) col = mixc(col, 0x7FE6FF, (int)(pa_clamp01(k * 2.0f) * (1.0f - out / 16.0f) * 230.0f));
    }
    for (int k = 0; k < S.nisle; k++) {
        float ex = wx - S.isle[k].x, ey = wy - S.isle[k].y;
        float e2 = ex * ex + ey * ey, r = S.isle[k].r;
        if (e2 > (r + 1.0f) * (r + 1.0f)) continue;
        float e = sqrtf(e2);
        /* A slightly wobbly edge so the islands are not perfect discs. */
        float wob = 0.5f * sinf(atan2f(ey, ex) * 5.0f + (float)k);
        float edge = r + wob - e;
        if (edge <= -0.06f) continue;
        uint32_t g = edge < 0.45f ? C_GRASS_D : C_GRASS;
        if (edge > 0.45f) {
            int gi = (int)floorf(wx * 0.8f), gj = (int)floorf(wy * 0.8f);
            uint32_t h = hash2(gi, gj, 77);
            if ((h & 7) == 0) {
                float cx = ((float)gi + 0.3f + 0.4f * (float)((h >> 8) & 255) / 255.0f) / 0.8f;
                float cy = ((float)gj + 0.3f + 0.4f * (float)((h >> 16) & 255) / 255.0f) / 0.8f;
                float dd = hypotf(wx - cx, wy - cy);
                if (dd < 0.13f) g = 0xFFD23A;
                else if (dd < 0.32f) g = 0xFFFFFF;
            }
        }
        float a = pa_clamp01((edge + 0.06f) * g_s * 0.9f);
        col = mixc(col, g, (int)(a * 256.0f));
    }
    return col;
}

static const uint32_t SPRINKLE[6] = { 0xFFE45C, 0x5ED8F5, 0xFFFFFF, 0x7EE07A, 0xB98AF5, 0xFF9F4C };

static uint32_t top_colour(int who, float v, float wx, float wy) {
    if (who == WHO_RIM) {
        float d = hypotf(wx - ACX, wy - ACY);
        float m = d - AR;
        uint32_t c = C_RIM_TOP;
        if (m < 0.35f || m > RIM_W - 0.35f) c = mixc(c, 0xFFFFFF, 90);
        return c;
    }
    uint32_t col = g_top[who];
    const Player *p = &S.pl[who - 1];
    int pat = SKINS[p->skin].pattern;
    if (pat == 2) {
        /* Soft polka dots on a staggered grid. */
        float gy = wy * 0.62f;
        int gj = (int)floorf(gy);
        float gx = wx * 0.62f + ((gj & 1) ? 0.5f : 0.0f);
        int gi = (int)floorf(gx);
        float dx = gx - (float)gi - 0.5f, dy = gy - (float)gj - 0.5f;
        float dd = sqrtf(dx * dx + dy * dy);
        float cov = pa_clamp01((0.17f - dd) * g_s / 0.62f + 0.5f);
        if (cov > 0.0f) col = mixc(col, 0xFFFFFF, (int)(cov * 70.0f));
    } else if (pat == 3) {
        float t = (wx + wy) * 0.42f;
        float f = t - floorf(t);
        float band = pa_clamp01((0.22f - fabsf(f - 0.5f)) * g_s / 0.42f * 0.7f + 0.5f);
        if (band > 0.0f) col = mixc(col, 0xFFFFFF, (int)(band * 40.0f));
    } else if (pat == 1) {
        float px = wx * 1.15f, py = wy * 1.15f;
        int gi = (int)floorf(px), gj = (int)floorf(py);
        uint32_t h = hash2(gi, gj, 5);
        if (h % 7 < 3) {
            float cx = (float)gi + 0.25f + 0.5f * (float)(h & 255) / 255.0f;
            float cy = (float)gj + 0.25f + 0.5f * (float)((h >> 8) & 255) / 255.0f;
            static const float DIRX[8] = { 0.2f, 0.185f, 0.141f, 0.077f, 0.0f, -0.077f, -0.141f, -0.185f };
            static const float DIRY[8] = { 0.0f, 0.077f, 0.141f, 0.185f, 0.2f, 0.185f, 0.141f, 0.077f };
            float ux = DIRX[(h >> 16) & 7], uy = DIRY[(h >> 16) & 7];
            float qx = px - cx, qy = py - cy;
            float t = pa_clampf((qx * ux + qy * uy) / (0.04f), -1.0f, 1.0f);
            float ex = qx - ux * t, ey = qy - uy * t;
            float dd = sqrtf(ex * ex + ey * ey);
            float cov = pa_clamp01((0.075f - dd) * g_s / 1.15f + 0.5f);
            if (cov > 0.0f) col = mixc(col, SPRINKLE[(h >> 24) % 6], (int)(cov * 256.0f));
        }
    }
    {
        /* Fresh land glows white for a moment; the claim time is sampled
           bilinearly so the glow front is a smooth curve, not cell steps. */
        float u = wx - 0.5f, w = wy - 0.5f;
        int i0 = (int)floorf(u), j0 = (int)floorf(w);
        if (S.t - S.last_ct < 0.55f && i0 >= 0 && j0 >= 0 && i0 < G - 1 && j0 < G - 1) {
            int c = j0 * G + i0;
            float fx = u - (float)i0, fy = w - (float)j0;
            /* 120 ms of hard white flash, then a quick fade. */
            #define GLOW(t0) (S.t - (t0) < 0.12f ? 1.0f : pa_clamp01(1.0f - (S.t - (t0) - 0.12f) / 0.4f))
            float g0 = GLOW(S.ct[c]), g1 = GLOW(S.ct[c + 1]), g2 = GLOW(S.ct[c + G]), g3 = GLOW(S.ct[c + G + 1]);
            #undef GLOW
            if (g0 + g1 + g2 + g3 > 0.0f) {
                float a = g0 + (g1 - g0) * fx, b = g2 + (g3 - g2) * fx;
                float gl = pa_clamp01(a + (b - a) * fy);
                col = mixc(col, 0xFFFFFF, (int)(pa_smooth(gl) * 200.0f));
            }
        }
    }
    if (p->dying) col = mixc(col, 0xFFFFFF, (int)(p->fade * 160.0f));
    /* Glossy lip along the top edge, as on the plates' slabs. */
    /* A crisp 2 px white line inside the edge, then a soft bevel glow. */
    float e = (v - 0.5f) * g_aa;
    if (e < 2.4f) col = mixc(col, 0xFFFFFF, 112);
    else if (e < 10.0f) col = mixc(col, 0xFFFFFF, (int)(40.0f * (1.0f - (e - 2.4f) / 7.6f)));
    return col;
}

static void draw_world(PA_Canvas *c) {
    int W = c->w, H = c->h;
    int HP = (int)(SLAB_Z * g_s * VIEW_Z + 0.5f);
    int SH = (int)(g_s * 0.32f);
    if (HP < 2) HP = 2;
    int lip_rows = HP * 45 / 100 < 2 ? 2 : HP * 45 / 100;
    int rows = H + HP + SH + 1;
    if (g_base_cap < rows * W) {
        free(g_base);
        g_base = (Base *)malloc(sizeof(Base) * (size_t)rows * (size_t)W);
        g_base_cap = g_base ? rows * W : 0;
        if (!g_base) return;
    }
    static float colx[4096];
    if (W > 4096) return;
    for (int x = 0; x < W; x++) colx[x] = S.cam_x + ((float)x + 0.5f - g_ax) / g_s;
    float aa = g_s * 2.6f;
    g_aa = aa;
    float inv_k = 1.0f / (g_s * VIEW_K);

    for (int r = 0; r < rows; r++) {
        int by = r - SH;
        float wy = S.cam_y + ((float)by + 0.5f - g_ay) * inv_k;
        float dy = wy - ACY, dy2 = dy * dy;
        Base *row = g_base + (size_t)r * (size_t)W;
        for (int x = 0; x < W; x++) {
            float wx = colx[x];
            float dx = wx - ACX, d2 = dx * dx + dy2;
            Base *b = &row[x];
            if (d2 < (AR - 0.6f) * (AR - 0.6f)) {
                int who;
                float v = land_sample(wx, wy, &who);
                float cov = pa_clamp01((v - 0.5f) * aa + 0.5f);
                b->who = (uint8_t)(cov > 0.0f ? who : 0);
                b->cov = (uint8_t)(cov * 255.0f);
                b->v = (uint8_t)(pa_clamp01(v) * 255.0f);
                b->ground = (b->cov == 255) ? 0 : ground_colour(wx, wy, 0.0f);
                continue;
            }
            float d = sqrtf(d2);
            float cov = pa_clamp01((d - AR) * g_s + 0.5f) * pa_clamp01((AR + RIM_W - d) * g_s + 0.5f);
            if (cov > 0.0f) {
                b->who = WHO_RIM; b->cov = (uint8_t)(cov * 255.0f); b->v = 255;
                b->ground = ground_colour(wx, wy, d);
                if (cov < 1.0f && d < AR + 1.0f) {
                    int who;
                    float v = land_sample(wx, wy, &who);
                    if (v > 0.5f) { b->who = (uint8_t)who; b->cov = 255; b->v = (uint8_t)(pa_clamp01(v) * 255.0f); }
                }
            } else {
                b->who = 0; b->cov = 0;
                b->v = (uint8_t)(d < AR + RIM_W + 1.2f ? 200 : 0);
                b->ground = ground_colour(wx, wy, d);
            }
        }
    }

    static int lfull[4096], lany[4096];
    for (int x = 0; x < W; x++) { lfull[x] = -100000; lany[x] = -100000; }
    /* Prime the trackers with the rows y = 0 .. HP-2. */
    for (int by = 0; by < HP - 1; by++) {
        Base *row = g_base + (size_t)(by + SH) * (size_t)W;
        for (int x = 0; x < W; x++) {
            if (row[x].cov) lany[x] = by;
            if (row[x].cov == 255) lfull[x] = by;
        }
    }
    for (int y = 0; y < H; y++) {
        int nb = y + HP - 1;
        Base *nrow = g_base + (size_t)(nb + SH) * (size_t)W;
        for (int x = 0; x < W; x++) {
            if (nrow[x].cov) lany[x] = nb;
            if (nrow[x].cov == 255) lfull[x] = nb;
        }
        Base *grow = g_base + (size_t)(y + SH) * (size_t)W;
        Base *srow = g_base + (size_t)(y) * (size_t)W;              /* SH px above */
        Base *trow = g_base + (size_t)(y + HP + SH) * (size_t)W;
        uint32_t *px = c->px + (size_t)y * (size_t)W;
        float wy_top = S.cam_y + ((float)(y + HP) + 0.5f - g_ay) * inv_k;
        for (int x = 0; x < W; x++) {
            Base *tb = &trow[x];
            uint32_t col;
            if (tb->cov == 255) {
                col = top_colour(tb->who, (float)tb->v / 255.0f, colx[x], wy_top);
                px[x] = col;
                continue;
            }
            col = grow[x].ground;
            /* Soft drop shadow under every slab. */
            int sv = srow[x].v;
            if (sv > 30 && grow[x].cov < 255) {
                int k = (sv - 30) * 96 / 225;
                col = mixc(col, srow[x].who == WHO_RIM ? 0x0E70B0 : 0x4FBF98, k);
            }
            int side = -1, scov = 0;
            if (lfull[x] >= y) { side = lfull[x]; scov = 256; }
            else if (lany[x] >= y) { side = lany[x]; scov = g_base[(size_t)(side + SH) * (size_t)W + x].cov; }
            if (side >= 0) {
                int who = g_base[(size_t)(side + SH) * (size_t)W + x].who;
                int t = side - y;
                /* Darker lower lip on every slab wall. */
                uint32_t sc = who == WHO_RIM ? C_RIM_SIDE : (t < lip_rows ? g_lip[who] : g_side[who]);
                if (who == WHO_RIM && t < lip_rows) sc = mixc(sc, 0x000000, 60);
                col = mixc(col, sc, scov);
            }
            if (tb->cov) col = mixc(col, top_colour(tb->who, (float)tb->v / 255.0f, colx[x], wy_top), tb->cov + 1);
            px[x] = col;
        }
    }
}

/* ---------------------------------------------------------------- trails -- */
static void trail_ensure(int W, int H) {
    if (g_tb_cap >= W * H) return;
    free(g_tb);
    g_tb = (uint8_t *)malloc((size_t)W * (size_t)H);
    g_tb_cap = g_tb ? W * H : 0;
    if (g_tb) memset(g_tb, 255, (size_t)W * (size_t)H);
}

static float point_z(float x, float y) {
    int c = cell_at(x, y);
    return (c >= 0 && S.vis[c]) ? SLAB_Z : 0.0f;
}

static void draw_trail(PA_Canvas *c, const PA_Vec2 *pts, int n, float hx, float hy, int has_head,
                       PA_Color land, float width_k) {
    int W = c->w, H = c->h;
    if (!g_tb || n + has_head < 2) return;
    float r = TRAIL_W * 0.5f * g_s * width_k;
    if (r < 1.0f) return;
    int bx0 = W, by0 = H, bx1 = -1, by1 = -1;
    int total = n + has_head;
    PA_Vec2 prev = { 0, 0 };
    for (int k = 0; k < total; k++) {
        float wx = k < n ? pts[k].x : hx, wy = k < n ? pts[k].y : hy;
        PA_Vec2 cur = proj(wx, wy, point_z(wx, wy));
        if (k == 0) { prev = cur; if (total > 1) continue; }
        /* Capsule from prev to cur into the distance buffer (min). */
        float x0 = fminf(prev.x, cur.x) - r - 1, x1 = fmaxf(prev.x, cur.x) + r + 1;
        float y0 = fminf(prev.y, cur.y) - r - 1, y1 = fmaxf(prev.y, cur.y) + r + 1;
        int ix0 = (int)x0, ix1 = (int)x1, iy0 = (int)y0, iy1 = (int)y1;
        if (ix0 < 0) ix0 = 0;
        if (iy0 < 0) iy0 = 0;
        if (ix1 > W - 1) ix1 = W - 1;
        if (iy1 > H - 1) iy1 = H - 1;
        if (ix0 <= ix1 && iy0 <= iy1) {
            float ux = cur.x - prev.x, uy = cur.y - prev.y, ll = ux * ux + uy * uy;
            float il = ll > 1e-6f ? 1.0f / ll : 0.0f;
            for (int y = iy0; y <= iy1; y++) {
                uint8_t *row = g_tb + (size_t)y * (size_t)W;
                float py = (float)y + 0.5f - prev.y;
                for (int x = ix0; x <= ix1; x++) {
                    float pxx = (float)x + 0.5f - prev.x;
                    float t = (pxx * ux + py * uy) * il;
                    if (t < 0) t = 0;
                    if (t > 1) t = 1;
                    float ex = pxx - ux * t, ey = py - uy * t;
                    float d2 = ex * ex + ey * ey;
                    if (d2 > (r + 1) * (r + 1)) continue;
                    int dv = (int)(sqrtf(d2) * 4.0f);
                    if (dv < row[x]) row[x] = (uint8_t)dv;
                }
            }
            if (ix0 < bx0) bx0 = ix0;
            if (iy0 < by0) by0 = iy0;
            if (ix1 > bx1) bx1 = ix1;
            if (iy1 > by1) by1 = iy1;
        }
        prev = cur;
    }
    if (bx1 < 0) return;
    uint32_t inner = mixc(land & 0xFFFFFF, 0xFFFFFF, 112);
    uint32_t edge = mixc(land & 0xFFFFFF, 0xFFFFFF, 30);
    for (int y = by0; y <= by1; y++) {
        uint8_t *row = g_tb + (size_t)y * (size_t)W;
        uint32_t *px = c->px + (size_t)y * (size_t)W;
        for (int x = bx0; x <= bx1; x++) {
            int dv = row[x];
            if (dv == 255) continue;
            row[x] = 255;
            float d = (float)dv * 0.25f;
            if (d > r + 0.5f) continue;
            float cov = pa_clamp01(r + 0.5f - d);
            float e = pa_clamp01((d - (r - 3.2f)) * 0.6f);
            uint32_t col = mixc(inner, edge, (int)(e * 256.0f));
            float alpha = (0.80f + 0.15f * e) * cov;
            px[x] = mixc(px[x], col, (int)(alpha * 256.0f));
        }
    }
}

/* ---------------------------------------------------------------- avatar -- */
/* The mascots are chunky boxes that face the camera, the way the plates
   show them: a big glossy pair of eyes on the front, the skin's ears, hat or
   prop on top, a dark ink outline round the whole silhouette and a soft
   contact shadow. They lean into the direction of travel and their pupils
   look where they are going. */
#define C_INK 0x2A1F5C

typedef struct { float gx, gy, s, z0, scale, hgt, lnx, lny, ow; } AvView;

static PA_Vec2 av_pt(const AvView *v, float lx, float ly, float z) {
    float k = v->hgt > 0.0f ? z / v->hgt : 0.0f;
    PA_Vec2 o;
    float wx = (lx + v->lnx * k) * v->scale, wy = (ly + v->lny * k) * v->scale;
    o.x = v->gx + wx * v->s;
    o.y = v->gy + wy * v->s * AV_K - v->z0 * v->s * VIEW_Z - z * v->scale * v->s * AV_ZK;
    return o;
}

static float av_px(const AvView *v, float cells) { return cells * v->s * v->scale; }

/* Filled shape with the ink outline under it. */
static void inked_poly(PA_Canvas *c, const AvView *v, const PA_Vec2 *p, int n, PA_Color col) {
    pa_stroke_poly(c, p, n, 1, v->ow * 2.0f, pa_hex(C_INK));
    pa_fill_poly(c, p, n, pa_hex(C_INK));
    pa_fill_poly(c, p, n, col);
}

static void inked_circle(PA_Canvas *c, const AvView *v, float x, float y, float r, PA_Color col) {
    pa_fill_circle(c, x, y, r + v->ow, pa_hex(C_INK));
    pa_fill_circle(c, x, y, r, col);
}

/* A big cartoon eye: ink ring, white, dark pupil looking along the heading,
   two highlights. */
static void glossy_eye(PA_Canvas *c, const AvView *v, float x, float y, float r, float lx, float ly,
                       PA_Color iris) {
    pa_fill_ellipse(c, x, y, r + v->ow, r * 1.08f + v->ow, pa_hex(C_INK));
    pa_fill_ellipse(c, x, y, r, r * 1.08f, PA_RGB(255, 255, 255));
    float px = x + lx * r * 0.28f, py = y + ly * r * 0.22f + r * 0.08f;
    if (PA_A(iris) > 0) pa_fill_circle(c, px, py, r * 0.62f, iris);
    pa_fill_circle(c, px, py, r * 0.46f, pa_hex(0x1A1A40));
    pa_fill_circle(c, px - r * 0.2f, py - r * 0.22f, r * 0.2f, PA_RGB(255, 255, 255));
    pa_fill_circle(c, px + r * 0.18f, py + r * 0.18f, r * 0.09f, PA_RGB(255, 255, 255));
}

static int hull2d(PA_Vec2 *p, int n, PA_Vec2 *out) {
    for (int i = 1; i < n; i++) {
        PA_Vec2 t = p[i]; int j = i - 1;
        while (j >= 0 && (p[j].x > t.x || (p[j].x == t.x && p[j].y > t.y))) { p[j + 1] = p[j]; j--; }
        p[j + 1] = t;
    }
    int k = 0;
    for (int i = 0; i < n; i++) {
        while (k >= 2 && (out[k - 1].x - out[k - 2].x) * (p[i].y - out[k - 2].y) -
                         (out[k - 1].y - out[k - 2].y) * (p[i].x - out[k - 2].x) <= 0) k--;
        out[k++] = p[i];
    }
    for (int i = n - 2, t = k + 1; i >= 0; i--) {
        while (k >= t && (out[k - 1].x - out[k - 2].x) * (p[i].y - out[k - 2].y) -
                         (out[k - 1].y - out[k - 2].y) * (p[i].x - out[k - 2].x) <= 0) k--;
        out[k++] = p[i];
    }
    return k - 1;
}

static void draw_avatar(PA_Canvas *c, int skin, float gx, float gy, float s, float head,
                        float z0, float scale, float squash, float shadow, float lean) {
    const Skin *sk = &SKINS[skin];
    float a = AV_HALF, rc = a * 0.36f;
    float hgt = AV_H * (1.0f - 0.24f * squash);
    float wide = 1.0f + 0.14f * squash;
    float hx = cosf(head), hy = sinf(head);
    AvView v = { gx, gy, s, z0, scale, hgt, hx * lean * 0.22f, hy * lean * 0.22f, 0.0f };
    v.ow = fmaxf(1.4f, s * scale * 0.11f);
    PA_Vec2 fp[24]; int n = 0;
    static const float CX[4] = { 1, -1, -1, 1 }, CY[4] = { 1, 1, -1, -1 };
    for (int k = 0; k < 4; k++) {
        float cx = CX[k] * (a - rc), cy = CY[k] * (a - rc);
        for (int j = 0; j <= 5; j++) {
            float ang = ((float)k + (float)j / 5.0f) * PA_PI * 0.5f;
            fp[n].x = (cx + cosf(ang) * rc) * wide; fp[n].y = (cy + sinf(ang) * rc) * wide; n++;
        }
    }
    if (shadow > 0.0f) {
        PA_Vec2 g = av_pt(&v, 0, 0, 0);
        pa_shadow(c, g.x + s * 0.08f, g.y + s * 0.22f + v.z0 * s * VIEW_Z * 0.0f,
                  a * s * scale * 1.35f, a * s * scale * 1.0f * VIEW_K, shadow);
    }

    /* Ears and anything else that sits behind the top face. */
    float z = hgt;
    PA_Color body = pa_hex(sk->body), topc = pa_hex(sk->top);
    if (skin == D_CAT || skin == D_PANDA) {
        for (int sd = -1; sd <= 1; sd += 2) {
            if (skin == D_CAT) {
                PA_Vec2 b0 = av_pt(&v, (float)sd * 0.88f * a, -0.35f * a, z), b1 = av_pt(&v, (float)sd * 0.22f * a, -0.35f * a, z);
                PA_Vec2 tp = av_pt(&v, (float)sd * 0.66f * a, -0.42f * a, z + 1.05f);
                PA_Vec2 e[3] = { b0, tp, b1 };
                inked_poly(c, &v, e, 3, topc);
                PA_Vec2 c0 = av_pt(&v, (float)sd * 0.76f * a, -0.35f * a, z + 0.05f), c1 = av_pt(&v, (float)sd * 0.38f * a, -0.35f * a, z + 0.05f);
                PA_Vec2 ct = av_pt(&v, (float)sd * 0.63f * a, -0.4f * a, z + 0.72f);
                PA_Vec2 e2[3] = { c0, ct, c1 };
                pa_fill_poly(c, e2, 3, pa_hex(0xFF8AC0));
            } else {
                PA_Vec2 e = av_pt(&v, (float)sd * 0.72f * a, -0.45f * a, z + 0.35f);
                inked_circle(c, &v, e.x, e.y, av_px(&v, 0.42f), pa_hex(0x2A2D3E));
            }
        }
    }

    /* Ink silhouette of the body. */
    {
        PA_Vec2 pts[48], hl[50];
        for (int i = 0; i < n; i++) { pts[i] = av_pt(&v, fp[i].x, fp[i].y, 0); pts[n + i] = av_pt(&v, fp[i].x, fp[i].y, hgt); }
        int hn = hull2d(pts, n * 2, hl);
        float cx = 0, cy = 0;
        for (int i = 0; i < hn; i++) { cx += hl[i].x; cy += hl[i].y; }
        cx /= (float)hn; cy /= (float)hn;
        for (int i = 0; i < hn; i++) {
            float dx = hl[i].x - cx, dy = hl[i].y - cy, L = sqrtf(dx * dx + dy * dy);
            if (L > 0.001f) { hl[i].x += dx / L * v.ow; hl[i].y += dy / L * v.ow; }
        }
        pa_fill_poly(c, hl, hn, pa_hex(C_INK));
    }

    /* Side bands, bottom to top. */
    float bz[5]; uint32_t bc[4]; int nb;
    if (skin == D_SANDWICH) {
        bz[0] = 0; bz[1] = 0.30f; bz[2] = 0.46f; bz[3] = 0.62f; bz[4] = 1.0f;
        bc[0] = 0xD97A2E; bc[1] = 0x45C93A; bc[2] = 0xFFD21A; bc[3] = 0xEE9440; nb = 4;
    } else if (skin == D_DONUT) {
        bz[0] = 0; bz[1] = 0.5f; bz[2] = 1.0f;
        bc[0] = sk->body; bc[1] = sk->top; nb = 2;
    } else if (skin == D_PANDA) {
        bz[0] = 0; bz[1] = 0.22f; bz[2] = 1.0f;
        bc[0] = 0x2A2D3E; bc[1] = sk->body; nb = 2;
    } else {
        bz[0] = 0; bz[1] = 0.16f; bz[2] = 1.0f;
        bc[0] = (uint32_t)pa_shade(body, -0.2f) & 0xFFFFFF; bc[1] = sk->body; nb = 2;
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float ex = fp[j].x - fp[i].x, ey = fp[j].y - fp[i].y;
        float nx = ey, ny = -ex, L = sqrtf(nx * nx + ny * ny);
        if (L < 1e-6f) continue;
        nx /= L; ny /= L;
        if (ny <= 0.0f) continue;
        float shade = -0.02f - 0.18f * nx - 0.08f * (1.0f - ny);
        for (int b = 0; b < nb; b++) {
            PA_Vec2 q[4] = {
                av_pt(&v, fp[i].x, fp[i].y, bz[b] * hgt), av_pt(&v, fp[j].x, fp[j].y, bz[b] * hgt),
                av_pt(&v, fp[j].x, fp[j].y, bz[b + 1] * hgt + 0.02f), av_pt(&v, fp[i].x, fp[i].y, bz[b + 1] * hgt + 0.02f)
            };
            pa_fill_poly(c, q, 4, pa_shade(pa_hex(bc[b]), shade));
        }
    }
    /* Top face: pale lip, face colour, then a white gloss on the far half. */
    PA_Vec2 top[24], in[24], gl[24];
    for (int i = 0; i < n; i++) {
        top[i] = av_pt(&v, fp[i].x, fp[i].y, hgt);
        in[i] = av_pt(&v, fp[i].x * 0.88f, fp[i].y * 0.88f + 0.04f, hgt);
        gl[i] = av_pt(&v, fp[i].x * 0.6f - 0.12f * a, fp[i].y * 0.34f - 0.42f * a, hgt);
    }
    pa_fill_poly(c, top, n, pa_shade(topc, 0.28f));
    pa_fill_poly(c, in, n, topc);
    pa_fill_poly(c, gl, n, PA_RGBA(255, 255, 255, 84));

    /* Front-face anchor: the plane at local y = +a. */
    float fy = a * wide;
    #define FRONT(lx, zf) av_pt(&v, (lx), fy, (zf) * hgt)

    /* Top decorations. */
    switch (skin) {
    case D_DONUT: {
        PA_Vec2 h = av_pt(&v, 0, -0.1f * a, z);
        pa_fill_ellipse(c, h.x, h.y, av_px(&v, 0.36f), av_px(&v, 0.36f) * VIEW_K, pa_hex(0xD04F8A));
        pa_fill_ellipse(c, h.x, h.y + 1, av_px(&v, 0.28f), av_px(&v, 0.28f) * VIEW_K, pa_hex(0xB5743A));
        static const float SP[9][3] = { { .62f, .2f, .3f }, { .5f, -.55f, 1.2f }, { -.1f, .6f, 2.0f },
            { -.62f, -.3f, .7f }, { .1f, -.75f, 2.6f }, { -.55f, .42f, 1.6f }, { .72f, -.15f, 2.2f },
            { -.25f, -.6f, .2f }, { .4f, .55f, 1.0f } };
        for (int k = 0; k < 9; k++) {
            float lx = SP[k][0] * a, ly = SP[k][1] * a, an = SP[k][2];
            PA_Vec2 p0 = av_pt(&v, lx - cosf(an) * 0.14f, ly - sinf(an) * 0.14f, z);
            PA_Vec2 p1 = av_pt(&v, lx + cosf(an) * 0.14f, ly + sinf(an) * 0.14f, z);
            pa_line(c, p0.x, p0.y, p1.x, p1.y, av_px(&v, 0.12f), pa_hex(SPRINKLE[k % 6]));
        }
        /* Icing drips over the front edge. */
        for (int k = 0; k < 4; k++) {
            float lx = (-0.72f + 0.48f * (float)k) * a;
            PA_Vec2 d = FRONT(lx, 0.86f - 0.08f * (float)(k & 1));
            pa_fill_circle(c, d.x, d.y, av_px(&v, 0.2f), topc);
        }
        PA_Vec2 l0 = FRONT(-a * 0.95f, 1.0f), l1 = FRONT(a * 0.95f, 1.0f), l2 = FRONT(a * 0.95f, 0.84f), l3 = FRONT(-a * 0.95f, 0.84f);
        PA_Vec2 band[4] = { l0, l1, l2, l3 };
        pa_fill_poly(c, band, 4, topc);
        break;
    }
    case D_CHICK: {
        for (int k = -1; k <= 1; k++) {
            PA_Vec2 b0 = av_pt(&v, (float)k * 0.2f * a - 0.08f, -0.2f * a, z), b1 = av_pt(&v, (float)k * 0.2f * a + 0.08f, -0.2f * a, z);
            PA_Vec2 tp = av_pt(&v, (float)k * 0.38f * a, -0.3f * a, z + 0.62f - 0.15f * (float)(k * k));
            PA_Vec2 f[3] = { b0, tp, b1 };
            inked_poly(c, &v, f, 3, pa_hex(0xFFB000));
        }
        break;
    }
    case D_SANDWICH: {
        PA_Vec2 b = av_pt(&v, 0.2f * a, -0.2f * a, z), t = av_pt(&v, 0.2f * a, -0.2f * a, z + 1.2f);
        pa_line(c, b.x, b.y, t.x, t.y, av_px(&v, 0.1f) + v.ow, pa_hex(C_INK));
        pa_line(c, b.x, b.y, t.x, t.y, av_px(&v, 0.1f), pa_hex(0xF4DDB0));
        PA_Vec2 f[3] = { t, { t.x + av_px(&v, 0.7f), t.y + av_px(&v, 0.18f) }, { t.x, t.y + av_px(&v, 0.4f) } };
        inked_poly(c, &v, f, 3, pa_hex(0xFF3B4E));
        static const float SE[6][2] = { { .5f, .3f }, { -.25f, .5f }, { -.6f, -.2f }, { .1f, -.55f }, { .62f, -.4f }, { -.5f, .55f } };
        for (int k = 0; k < 6; k++) {
            PA_Vec2 p = av_pt(&v, SE[k][0] * a, SE[k][1] * a, z);
            pa_fill_ellipse(c, p.x, p.y, av_px(&v, 0.09f), av_px(&v, 0.06f), pa_hex(0xFFF3D6));
        }
        break;
    }
    case D_FROG: {
        /* A little gold crown. */
        PA_Vec2 cb = av_pt(&v, 0, -0.25f * a, z);
        float cw = av_px(&v, 0.62f), ch = av_px(&v, 0.55f);
        PA_Vec2 cr[7] = { { cb.x - cw, cb.y }, { cb.x - cw, cb.y - ch }, { cb.x - cw * 0.5f, cb.y - ch * 0.55f },
                          { cb.x, cb.y - ch * 1.15f }, { cb.x + cw * 0.5f, cb.y - ch * 0.55f }, { cb.x + cw, cb.y - ch },
                          { cb.x + cw, cb.y } };
        inked_poly(c, &v, cr, 7, pa_hex(0xFFC21A));
        pa_fill_circle(c, cb.x, cb.y - ch * 0.4f, av_px(&v, 0.1f), pa_hex(0xFF3B6A));
        break;
    }
    case D_ROBOT: {
        PA_Vec2 b = av_pt(&v, 0, -0.2f * a, z), t = av_pt(&v, 0, -0.2f * a, z + 1.0f);
        pa_line(c, b.x, b.y, t.x, t.y, av_px(&v, 0.1f) + v.ow, pa_hex(C_INK));
        pa_line(c, b.x, b.y, t.x, t.y, av_px(&v, 0.1f), pa_hex(0x55607E));
        inked_circle(c, &v, t.x, t.y, av_px(&v, 0.2f), pa_hex(0xFF4F5E));
        for (int sd = -1; sd <= 1; sd += 2) {
            PA_Vec2 bo = av_pt(&v, (float)sd * 0.65f * a, -0.6f * a, z);
            pa_fill_circle(c, bo.x, bo.y, av_px(&v, 0.08f), pa_hex(0x55607E));
        }
        break;
    }
    case D_BERRY: {
        for (int k = 0; k < 5; k++) {
            float an = (float)k / 5.0f * PA_TAU + 0.3f;
            PA_Vec2 o = av_pt(&v, 0, -0.15f * a, z + 0.05f);
            PA_Vec2 l1 = av_pt(&v, cosf(an - 0.4f) * 0.3f, -0.15f * a + sinf(an - 0.4f) * 0.3f, z + 0.1f);
            PA_Vec2 l2 = av_pt(&v, cosf(an) * 0.75f, -0.15f * a + sinf(an) * 0.75f, z + 0.18f);
            PA_Vec2 l3 = av_pt(&v, cosf(an + 0.4f) * 0.3f, -0.15f * a + sinf(an + 0.4f) * 0.3f, z + 0.1f);
            PA_Vec2 lf[4] = { o, l1, l2, l3 };
            inked_poly(c, &v, lf, 4, pa_hex(0x34C24A));
        }
        PA_Vec2 st = av_pt(&v, 0, -0.15f * a, z + 0.55f);
        PA_Vec2 sb = av_pt(&v, 0, -0.15f * a, z + 0.1f);
        pa_line(c, sb.x, sb.y, st.x, st.y, av_px(&v, 0.12f), pa_hex(0x2E8F3A));
        break;
    }
    case D_GRAPE: {
        /* Party hat. */
        PA_Vec2 hb = av_pt(&v, 0, -0.2f * a, z);
        float hw = av_px(&v, 0.55f), hh = av_px(&v, 1.25f);
        PA_Vec2 cone[3] = { { hb.x - hw, hb.y }, { hb.x + hw * 0.15f, hb.y - hh }, { hb.x + hw, hb.y } };
        inked_poly(c, &v, cone, 3, pa_hex(0x3FD0FF));
        for (int k = 1; k <= 2; k++) {
            float t0 = (float)k * 0.3f, t1 = t0 + 0.13f;
            PA_Vec2 st[4] = {
                { hb.x - hw * (1 - t0) + hw * 0.15f * t0, hb.y - hh * t0 }, { hb.x + hw * (1 - t0) + hw * 0.15f * t0, hb.y - hh * t0 },
                { hb.x + hw * (1 - t1) + hw * 0.15f * t1, hb.y - hh * t1 }, { hb.x - hw * (1 - t1) + hw * 0.15f * t1, hb.y - hh * t1 } };
            pa_fill_poly(c, st, 4, pa_hex(0xFFE04A));
        }
        inked_circle(c, &v, hb.x + hw * 0.15f, hb.y - hh, av_px(&v, 0.17f), pa_hex(0xFF5FA8));
        break;
    }
    case D_SLIME: {
        PA_Vec2 bw = av_pt(&v, 0.35f * a, -0.35f * a, z + 0.25f);
        float r = av_px(&v, 0.32f);
        PA_Vec2 l[3] = { bw, { bw.x - r * 1.5f, bw.y - r }, { bw.x - r * 1.5f, bw.y + r } };
        PA_Vec2 rr[3] = { bw, { bw.x + r * 1.5f, bw.y - r }, { bw.x + r * 1.5f, bw.y + r } };
        inked_poly(c, &v, l, 3, pa_hex(0xFF5FA8));
        inked_poly(c, &v, rr, 3, pa_hex(0xFF5FA8));
        inked_circle(c, &v, bw.x, bw.y, r * 0.42f, pa_hex(0xFF8AC0));
        break;
    }
    default: break;
    }

    /* Face on the front. */
    float er = av_px(&v, (skin == D_FROG ? 0.34f : 0.31f) * a);
    float ez = skin == D_FROG ? 1.0f : 0.62f;
    PA_Color iris = PA_RGBA(0, 0, 0, 0);
    if (skin == D_CAT) iris = pa_hex(0x8BE84A);
    if (skin == D_CHICK || skin == D_SLIME) iris = pa_hex(0x2B6DE8);
    PA_Vec2 eL = FRONT(-0.42f * a, ez), eR = FRONT(0.42f * a, ez);
    if (skin == D_PANDA) {
        pa_fill_ellipse(c, eL.x, eL.y + er * 0.2f, er * 1.45f, er * 1.35f, pa_hex(0x2A2D3E));
        pa_fill_ellipse(c, eR.x, eR.y + er * 0.2f, er * 1.45f, er * 1.35f, pa_hex(0x2A2D3E));
    }
    if (skin == D_ROBOT) {
        PA_Vec2 v0 = FRONT(-0.85f * a, 0.98f), v1 = FRONT(0.85f * a, 0.98f), v2 = FRONT(0.85f * a, 0.5f), v3 = FRONT(-0.85f * a, 0.5f);
        PA_Vec2 vis[4] = { v0, v1, v2, v3 };
        pa_fill_poly(c, vis, 4, pa_hex(0x1E2444));
        float g = av_px(&v, 0.2f);
        pa_round_rect(c, eL.x - g, eL.y - g * 0.6f, g * 2, g * 1.4f, g * 0.5f, pa_hex(0x5CF2FF));
        pa_round_rect(c, eR.x - g, eR.y - g * 0.6f, g * 2, g * 1.4f, g * 0.5f, pa_hex(0x5CF2FF));
    } else {
        glossy_eye(c, &v, eL.x, eL.y, er, hx, hy, iris);
        glossy_eye(c, &v, eR.x, eR.y, er, hx, hy, iris);
    }
    PA_Vec2 m = FRONT(0, 0.42f);
    float mr = av_px(&v, 0.16f);
    if (skin == D_CHICK) {
        PA_Vec2 bk[3] = { { m.x - mr * 1.3f, m.y - mr * 0.6f }, { m.x + mr * 1.3f, m.y - mr * 0.6f }, { m.x, m.y + mr * 1.2f } };
        inked_poly(c, &v, bk, 3, pa_hex(0xFF7A12));
    } else if (skin == D_CAT) {
        PA_Vec2 ns[3] = { { m.x - mr * 0.6f, m.y - mr * 0.6f }, { m.x + mr * 0.6f, m.y - mr * 0.6f }, { m.x, m.y } };
        pa_fill_poly(c, ns, 3, pa_hex(0xFF7AB0));
        for (int sd = -1; sd <= 1; sd += 2) {
            pa_line(c, m.x + (float)sd * mr * 1.6f, m.y - mr * 0.2f, m.x + (float)sd * mr * 4.0f, m.y - mr * 0.6f, 1.6f, pa_hex(0xC8D0FF));
            pa_line(c, m.x + (float)sd * mr * 1.6f, m.y + mr * 0.3f, m.x + (float)sd * mr * 4.0f, m.y + mr * 0.5f, 1.6f, pa_hex(0xC8D0FF));
        }
    } else if (skin != D_ROBOT) {
        /* Smile. */
        PA_Vec2 sm[7];
        float w2 = skin == D_FROG ? mr * 3.2f : mr * 1.4f;
        for (int k = 0; k < 7; k++) {
            float t = (float)k / 6.0f;
            sm[k].x = m.x - w2 + 2.0f * w2 * t;
            sm[k].y = m.y + sinf(t * PA_PI) * mr * 0.9f;
        }
        pa_stroke_poly(c, sm, 7, 0, fmaxf(1.6f, v.ow * 0.8f), pa_hex(C_INK));
    }
    if (skin != D_ROBOT && skin != D_PANDA) {
        PA_Vec2 bl = FRONT(-0.78f * a, 0.5f), br = FRONT(0.78f * a, 0.5f);
        pa_fill_ellipse(c, bl.x, bl.y, er * 0.42f, er * 0.26f, PA_RGBA(255, 90, 140, 110));
        pa_fill_ellipse(c, br.x, br.y, er * 0.42f, er * 0.26f, PA_RGBA(255, 90, 140, 110));
    }
    if (skin == D_BERRY) {
        static const float SE[5][2] = { { -.7f, .25f }, { .7f, .2f }, { -.3f, .18f }, { .35f, .12f }, { 0, .2f } };
        for (int k = 0; k < 5; k++) {
            PA_Vec2 p = FRONT(SE[k][0] * a, SE[k][1] + 0.05f);
            pa_fill_ellipse(c, p.x, p.y, av_px(&v, 0.05f), av_px(&v, 0.08f), pa_hex(0xFFE27A));
        }
    }
    #undef FRONT
}

/* ------------------------------------------------------------- HUD bits -- */
static void star_poly(PA_Canvas *c, float cx, float cy, float r, float inner, PA_Color col) {
    PA_Vec2 p[10];
    for (int k = 0; k < 10; k++) {
        float an = -PA_PI * 0.5f + (float)k * PA_PI / 5.0f;
        float rr = (k & 1) ? r * inner : r;
        p[k].x = cx + cosf(an) * rr; p[k].y = cy + sinf(an) * rr;
    }
    pa_fill_poly(c, p, 10, col);
}

static void gem(PA_Canvas *c, float cx, float cy, float r, int lit) {
    PA_Vec2 h[6];
    for (int k = 0; k < 6; k++) {
        float an = PA_PI / 6.0f + (float)k * PA_PI / 3.0f;
        h[k].x = cx + cosf(an) * r; h[k].y = cy + sinf(an) * r;
    }
    pa_fill_circle(c, cx, cy, r + 3.0f, PA_RGB(255, 255, 255));
    pa_fill_poly(c, h, 6, lit ? pa_hex(0xD63AC8) : pa_hex(0x8C6BB8));
    PA_Vec2 f[4] = { { cx, cy - r * 0.7f }, { cx + r * 0.6f, cy - r * 0.35f }, { cx, cy }, { cx - r * 0.6f, cy - r * 0.35f } };
    pa_fill_poly(c, f, 4, lit ? pa_hex(0xFF8AF0) : pa_hex(0xB49AD6));
}

static void crown(PA_Canvas *c, float cx, float cy, float s, PA_Color col) {
    PA_Vec2 p[7] = { { cx - s, cy + s * 0.6f }, { cx - s, cy - s * 0.4f }, { cx - s * 0.5f, cy + s * 0.05f },
                     { cx, cy - s * 0.65f }, { cx + s * 0.5f, cy + s * 0.05f }, { cx + s, cy - s * 0.4f },
                     { cx + s, cy + s * 0.6f } };
    pa_fill_poly(c, p, 7, col);
}

static void big_star(PA_Canvas *c, float cx, float cy, float r) {
    star_poly(c, cx, cy + r * 0.06f, r * 1.12f, 0.56f, pa_hex(0x1D2A86));
    star_poly(c, cx, cy, r * 1.04f, 0.56f, PA_RGB(255, 255, 255));
    PA_Paint p = pa_linear(cx, cy - r, cx, cy + r);
    pa_stop(&p, 0.0f, pa_hex(0xFF5FC8));
    pa_stop(&p, 0.45f, pa_hex(0x8A6BFF));
    pa_stop(&p, 1.0f, pa_hex(0x3FD0FF));
    PA_Vec2 pts[10];
    for (int k = 0; k < 10; k++) {
        float an = -PA_PI * 0.5f + (float)k * PA_PI / 5.0f;
        float rr = (k & 1) ? r * 0.82f * 0.56f : r * 0.82f;
        pts[k].x = cx + cosf(an) * rr; pts[k].y = cy + sinf(an) * rr;
    }
    pa_fill_poly_paint(c, pts, 10, &p);
    crown(c, cx, cy + r * 0.05f, r * 0.34f, pa_hex(0xFFD23A));
}

static void progress_bar(PA_Canvas *c, float pct) {
    float x0 = 74.0f, x1 = (float)c->w - 22.0f, y = 26.0f, bh = 38.0f, bw = x1 - x0;
    pa_round_rect(c, x0 - 6, y - 6, bw + 12, bh + 12, (bh + 12) * 0.5f, pa_hex(0x8FE6FF));
    pa_round_rect(c, x0 - 3.5f, y - 3.5f, bw + 7, bh + 7, (bh + 7) * 0.5f, PA_RGB(255, 255, 255));
    PA_Paint bg = pa_linear(0, y, 0, y + bh);
    pa_stop(&bg, 0.0f, pa_hex(0x14208A));
    pa_stop(&bg, 1.0f, pa_hex(0x2D44D8));
    pa_round_rect_paint(c, x0, y, bw, bh, bh * 0.5f, &bg);
    float f = pa_clamp01(pct / 100.0f);
    PA_Color land = pa_hex(SKINS[S.pl[0].skin].land);
    float fw = (bw - 8.0f) * f;
    if (fw < bh - 8.0f) fw = bh - 8.0f;
    PA_Paint fg = pa_linear(x0, 0, x0 + fw, 0);
    pa_stop(&fg, 0.0f, pa_shade(land, 0.35f));
    pa_stop(&fg, 1.0f, land);
    pa_round_rect_paint(c, x0 + 4, y + 4, fw, bh - 8, (bh - 8) * 0.5f, &fg);
    pa_round_rect(c, x0 + 10, y + 7, fw - 12 > 4 ? fw - 12 : 4, (bh - 8) * 0.32f, (bh - 8) * 0.16f, pa_shade(land, 0.45f));
    char buf[16];
    if (pct < 10.0f) snprintf(buf, sizeof(buf), "%.1f%%", (double)pct);
    else snprintf(buf, sizeof(buf), "%d%%", (int)pct);
    float tw = pa_text_width(buf, 19.0f, 1.0f);
    float tx = x0 + 4 + fw + 10.0f;
    if (tx + tw > x1 - 40.0f) tx = x0 + 4 + fw - tw - 12.0f;
    pa_text_bold(c, buf, tx, y + 9.5f, 19.0f, PA_RGB(255, 255, 255), pa_hex(0x101A6A), PA_ALIGN_LEFT, 1.0f, 1.3f);
    /* The half-way gem and the full-map star. */
    float gx = x0 + bw * 0.5f;
    pa_line(c, gx, y - 4, gx, y + bh, 3.0f, PA_RGB(255, 255, 255));
    gem(c, gx, y - 6, 12.0f, pct >= 50.0f);
    star_poly(c, x1 - 2, y + bh * 0.5f + 1.5f, 22.0f, 0.56f, pa_hex(0x1D2A86));
    star_poly(c, x1 - 2, y + bh * 0.5f, 20.0f, 0.56f, PA_RGB(255, 255, 255));
    star_poly(c, x1 - 2, y + bh * 0.5f, 15.0f, 0.56f, pct >= 100.0f ? pa_hex(0xFFC21E) : pa_hex(0xFF7AD0));
    crown(c, x1 - 2, y + bh * 0.5f + 1, 5.5f, pa_hex(0xFFE04A));
}

static void skull(PA_Canvas *c, float cx, float cy, float r, PA_Color col, PA_Color bg) {
    pa_fill_circle(c, cx, cy - r * 0.1f, r, col);
    pa_round_rect(c, cx - r * 0.55f, cy + r * 0.4f, r * 1.1f, r * 0.6f, r * 0.2f, col);
    pa_fill_circle(c, cx - r * 0.38f, cy - r * 0.05f, r * 0.27f, bg);
    pa_fill_circle(c, cx + r * 0.38f, cy - r * 0.05f, r * 0.27f, bg);
}

static int standings(int *order) {
    int n = 0;
    for (int i = 0; i < MAXP; i++) if (S.pl[i].used && S.pl[i].alive) order[n++] = i;
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (S.cnt[order[b] + 1] > S.cnt[order[a] + 1]) { int t = order[a]; order[a] = order[b]; order[b] = t; }
    return n;
}

static void mini_face(PA_Canvas *c, float x, float y, float r, int skin) {
    const Skin *sk = &SKINS[skin];
    pa_round_rect(c, x - r, y - r + 2, r * 2, r * 2, r * 0.45f, pa_shade(pa_hex(sk->body), -0.15f));
    pa_round_rect(c, x - r, y - r, r * 2, r * 2 - 2, r * 0.45f, pa_hex(sk->top));
    if (skin == D_DONUT) { pa_fill_circle(c, x, y, r * 0.32f, pa_hex(0xB9814C)); return; }
    PA_Color iris = skin == D_PANDA ? pa_hex(0x2A2D3E) : PA_RGB(255, 255, 255);
    pa_fill_circle(c, x - r * 0.38f, y - r * 0.05f, r * 0.26f, iris);
    pa_fill_circle(c, x + r * 0.38f, y - r * 0.05f, r * 0.26f, iris);
    pa_fill_circle(c, x - r * 0.34f, y, r * 0.13f, skin == D_PANDA ? PA_RGB(255, 255, 255) : pa_hex(0x141836));
    pa_fill_circle(c, x + r * 0.42f, y, r * 0.13f, skin == D_PANDA ? PA_RGB(255, 255, 255) : pa_hex(0x141836));
}

static void leaderboard(PA_Canvas *c) {
    int order[MAXP];
    int n = standings(order);
    int show = n < 5 ? n : 5;
    int me_rank = -1;
    for (int k = 0; k < n; k++) if (order[k] == 0) me_rank = k;
    float x = 14.0f, y = 86.0f, rw = 196.0f, rh = 27.0f;
    char buf[24];
    int rows = show + (me_rank >= show ? 1 : 0);
    pa_round_rect(c, x - 4, y - 4, rw + 8, (float)rows * (rh + 3) + 6, 14.0f, PA_RGBA(20, 34, 120, 70));
    for (int r = 0; r < rows; r++) {
        int k = r < show ? r : me_rank;
        int i = order[k];
        Player *p = &S.pl[i];
        float ry = y + (float)r * (rh + 3);
        int mine = i == 0;
        pa_round_rect(c, x, ry, rw, rh, rh * 0.5f, mine ? pa_hex(SKINS[p->skin].land) : PA_RGBA(255, 255, 255, 215));
        snprintf(buf, sizeof(buf), "%d", k + 1);
        pa_text(c, buf, x + 15, ry + 7.5f, 12.0f, pa_hex(C_NAVY), PA_ALIGN_CENTER, 0.0f);
        mini_face(c, x + 39, ry + rh * 0.5f, 9.0f, p->skin);
        pa_text(c, mine ? "YOU" : p->name, x + 56, ry + 7.5f, 12.0f, pa_hex(C_NAVY), PA_ALIGN_LEFT, 0.8f);
        snprintf(buf, sizeof(buf), "%.1f%%", (double)pct_of(i));
        pa_text(c, buf, x + rw - 10, ry + 7.5f, 12.0f, pa_hex(C_NAVY), PA_ALIGN_RIGHT, 0.5f);
    }
    /* Kill counter, top right. */
    float kx = (float)c->w - 22.0f - 84.0f, ky = 84.0f;
    pa_round_rect(c, kx, ky, 84, 34, 17, PA_RGBA(20, 34, 120, 200));
    skull(c, kx + 22, ky + 15, 9.5f, PA_RGB(255, 255, 255), pa_hex(0x1C2C9E));
    snprintf(buf, sizeof(buf), "%d", S.pl[0].kills);
    pa_text_bold(c, buf, kx + 58, ky + 8, 18.0f, PA_RGB(255, 255, 255), pa_hex(0x101A6A), PA_ALIGN_CENTER, 1.0f, 1.2f);
}

/* ------------------------------------------------------------- world pass -- */
typedef struct { float y; int i; } Sort;

/* Flowers, pebbles, grass tufts, shells and mushrooms on open ground. */
static void draw_props(PA_Canvas *c) {
    float u = g_s;
    for (int i = 0; i < S.nprops; i++) {
        float x = S.props[i].x, y = S.props[i].y;
        PA_Vec2 q = proj(x, y, 0);
        if (q.x < -20 || q.x > (float)c->w + 20 || q.y < -20 || q.y > (float)c->h + 20) continue;
        int who;
        if (land_sample(x, y, &who) > 0.25f) continue;
        int t = S.props[i].type, var = S.props[i].var;
        float k = 0.8f + 0.4f * (float)(var & 15) / 15.0f;
        switch (t) {
        case 0: {   /* flower */
            static const uint32_t PET[4] = { 0xFFFFFF, 0xFFE14A, 0xFF8AC8, 0xB59BFF };
            uint32_t pc = PET[var & 3];
            float r = u * 0.17f * k;
            pa_fill_ellipse(c, q.x + 1, q.y + r * 1.4f, r * 2.2f, r * 0.9f, PA_RGBA(40, 120, 90, 50));
            for (int j = 0; j < 5; j++) {
                float an = (float)j / 5.0f * PA_TAU + (float)var;
                pa_fill_circle(c, q.x + cosf(an) * r * 1.15f, q.y + sinf(an) * r * 1.0f, r * 0.85f, pa_hex(pc));
            }
            pa_fill_circle(c, q.x, q.y, r * 0.75f, pa_hex((var & 3) == 1 ? 0xFF9A1A : 0xFFC81E));
            break;
        }
        case 2: {   /* pebble */
            float r = u * 0.22f * k;
            pa_fill_ellipse(c, q.x + 1, q.y + r * 0.5f, r * 1.3f, r * 0.7f, PA_RGBA(40, 90, 80, 60));
            pa_fill_ellipse(c, q.x, q.y, r * 1.2f, r * 0.75f, pa_hex(0xA9B8C4));
            pa_fill_ellipse(c, q.x - r * 0.3f, q.y - r * 0.25f, r * 0.55f, r * 0.3f, pa_hex(0xD8E4EC));
            break;
        }
        case 3: {   /* grass tuft */
            float h = u * 0.45f * k, w = u * 0.1f;
            for (int j = -1; j <= 1; j++) {
                PA_Vec2 b[3] = { { q.x + (float)j * w * 1.5f - w, q.y }, { q.x + (float)j * w * 1.5f + w, q.y },
                                 { q.x + (float)j * w * 3.2f, q.y - h * (j ? 0.8f : 1.0f) } };
                pa_fill_poly(c, b, 3, pa_hex(j ? 0x45B83C : 0x5ACC48));
            }
            break;
        }
        case 4: {   /* shell */
            float r = u * 0.26f * k;
            PA_Vec2 f[9];
            f[0].x = q.x; f[0].y = q.y + r * 0.4f;
            for (int j = 0; j < 8; j++) {
                float an = PA_PI + (float)j / 7.0f * PA_PI;
                f[j + 1].x = q.x + cosf(an) * r; f[j + 1].y = q.y + sinf(an) * r * 0.8f;
            }
            pa_fill_poly(c, f, 9, pa_hex((var & 1) ? 0xFFB59A : 0xFFD6E4));
            for (int j = 1; j < 4; j++) {
                float an = PA_PI + (float)j / 4.0f * PA_PI;
                pa_line(c, q.x, q.y + r * 0.3f, q.x + cosf(an) * r * 0.85f, q.y + sinf(an) * r * 0.7f, 1.2f, pa_hex(0xE58A70));
            }
            break;
        }
        case 5: {   /* mushroom */
            float r = u * 0.24f * k;
            pa_fill_rect(c, q.x - r * 0.35f, q.y - r * 0.9f, r * 0.7f, r * 0.9f, pa_hex(0xFFF4E0));
            pa_fill_ellipse(c, q.x, q.y - r * 0.9f, r * 1.1f, r * 0.7f, pa_hex(0xFF4A5A));
            pa_fill_circle(c, q.x - r * 0.4f, q.y - r * 1.05f, r * 0.18f, PA_RGB(255, 255, 255));
            pa_fill_circle(c, q.x + r * 0.35f, q.y - r * 0.85f, r * 0.14f, PA_RGB(255, 255, 255));
            break;
        }
        default: break;
        }
    }
}

/* About a 10% darkening toward the corners. */
static void vignette(PA_Canvas *c) {
    static int vx[4096], vy[4096];
    int W = c->w, H = c->h;
    if (W > 4096 || H > 4096) return;
    for (int x = 0; x < W; x++) { float n = ((float)x - (float)W * 0.5f) / ((float)W * 0.5f); vx[x] = (int)(n * n * 18.0f); }
    for (int y = 0; y < H; y++) { float n = ((float)y - (float)H * 0.5f) / ((float)H * 0.5f); vy[y] = (int)(n * n * 16.0f); }
    for (int y = 0; y < H; y++) {
        uint32_t *px = c->px + (size_t)y * (size_t)W;
        for (int x = 0; x < W; x++) {
            int k = vx[x] + vy[y] - 4;
            if (k > 0) px[x] = mixc(px[x], 0x0B2A4A, k);
        }
    }
}

static void draw_scene(PA_Canvas *c) {
    int W = c->w, H = c->h;
    float sx = 0, sy = 0;
    if (S.shake > 0.01f) {
        sx = sinf(S.t * 61.0f) * S.shake * 9.0f;
        sy = cosf(S.t * 47.0f) * S.shake * 7.0f;
    }
    /* The view opens out a little as your land grows. */
    float grow = pa_clamp01(pct_of(0) / 30.0f);
    g_s = fminf((float)W / 21.0f, (float)H / 26.0f) * (1.0f - 0.15f * grow);
    g_ax = (float)W * 0.5f + sx;
    g_ay = (float)H * 0.56f + sy;

    for (int o = 0; o <= MAXP; o++) { g_foff[o] = 0.0f; g_top[o] = 0; g_side[o] = 0; }
    for (int i = 0; i < MAXP; i++) {
        Player *p = &S.pl[i];
        const Skin *sk = &SKINS[p->skin];
        g_top[i + 1] = sk->land;
        g_side[i + 1] = (uint32_t)pa_shade(pa_hex(sk->land), -0.2f) & 0xFFFFFF;
        g_lip[i + 1] = (uint32_t)pa_shade(pa_hex(sk->land), -0.42f) & 0xFFFFFF;
        if (p->dying) g_foff[i + 1] = p->fade * 0.55f;
    }
    reblur();
    draw_world(c);
    draw_props(c);

    trail_ensure(W, H);
    for (int i = 0; i < MAXP; i++) {
        Player *p = &S.pl[i];
        if (!p->used) continue;
        PA_Color land = pa_hex(SKINS[p->skin].land);
        if (p->gt > 0.0f && p->gn > 1) draw_trail(c, p->gp, p->gn, 0, 0, 0, land, pa_clamp01(p->gt / 0.45f));
        if (p->alive && p->out) draw_trail(c, p->tp, p->tn, p->x, p->y, 1, land, 1.0f);
    }

    /* Ground-level effects: claim sparkles and spawn rings. */
    Sort order[MAXP]; int n = 0;
    for (int i = 0; i < MAXP; i++) {
        Player *p = &S.pl[i];
        if (!p->used || !p->alive) continue;
        PA_Vec2 g = proj(p->x, p->y, 0);
        if (g.x < -80 || g.x > (float)W + 80 || g.y < -80 || g.y > (float)H + 120) continue;
        order[n].y = g.y; order[n].i = i; n++;
    }
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (order[b].y < order[a].y) { Sort t = order[a]; order[a] = order[b]; order[b] = t; }
    for (int k = 0; k < n; k++) {
        Player *p = &S.pl[order[k].i];
        PA_Vec2 g = proj(p->x, p->y, 0);
        float pop = p->pop < 1.0f ? 1.0f + 0.25f * sinf(p->pop * PA_PI) * (1.0f - p->pop) * 2.0f : 1.0f;
        float sc = (p->pop < 1.0f ? pa_smooth(p->pop) : 1.0f) * pop;
        float hop = 0.0f;
        if (p->human && S.phase == PH_TITLE) hop = fabsf(sinf(S.t * 3.2f)) * 0.55f;
        else hop = fabsf(sinf(p->hop * 9.0f)) * 0.07f;
        float head = p->head;
        if (p->human && S.phase == PH_TITLE) { head = PA_PI * 0.5f + S.nudge * 0.9f; sc *= 1.45f; }
        if (p->human && S.phase != PH_TITLE) {
            /* The soft halo the plates put around your cube. */
            PA_Vec2 gz = proj(p->x, p->y, p->z);
            float rr = g_s * 3.4f;
            PA_Paint halo = pa_radial(gz.x, gz.y, rr * 0.2f, rr);
            pa_stop(&halo, 0.0f, PA_RGBA(255, 255, 255, 120));
            pa_stop(&halo, 0.6f, PA_RGBA(255, 255, 255, 45));
            pa_stop(&halo, 1.0f, PA_RGBA(255, 255, 255, 0));
            pa_fill_ellipse_paint(c, gz.x, gz.y, rr, rr * VIEW_K, &halo);
        }
        draw_avatar(c, p->skin, g.x, g.y, g_s, head, p->z + hop, sc, p->squash, 0.38f * sc,
                    (p->human && S.phase == PH_TITLE) ? 0.0f : 1.0f);
    }
    for (int k = 0; k < n; k++) {
        Player *p = &S.pl[order[k].i];
        if (p->human && S.phase == PH_TITLE) continue;
        PA_Vec2 t = proj(p->x, p->y, p->z + AV_H + 1.2f);
        pa_text_bold(c, p->human ? "YOU" : p->name, t.x, t.y - 18.0f, 15.0f, pa_hex(0x2A2F86),
                     PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.0f, 1.3f);
    }

    /* Particles. */
    for (int i = 0; i < MAXPART; i++) {
        Part *p = &S.parts[i];
        if (p->life <= 0.0f || p->kind == 9) continue;
        float k = p->life / p->max;
        PA_Vec2 q = proj(p->x, p->y, p->z);
        if (q.x < -20 || q.x > (float)W + 20 || q.y < -20 || q.y > (float)H + 20) continue;
        float r = p->size * g_s * (p->kind == 2 ? (0.4f + 0.6f * k) : 1.0f);
        if (p->kind == 3) {
            /* Shock ring on the ground. */
            float e = 1.0f - k;
            float rr = p->size * g_s * (0.25f + 0.75f * (1.0f - (1.0f - e) * (1.0f - e)));
            float th = 2.0f + 9.0f * k;
            PA_Vec2 ring[66];
            PA_Vec2 ctr = proj(p->x, p->y, p->z);
            for (int j = 0; j <= 32; j++) {
                float an = (float)j / 32.0f * PA_TAU;
                ring[j].x = ctr.x + cosf(an) * (rr + th); ring[j].y = ctr.y + sinf(an) * (rr + th) * VIEW_K;
                ring[65 - j].x = ctr.x + cosf(an) * rr; ring[65 - j].y = ctr.y + sinf(an) * rr * VIEW_K;
            }
            pa_fill_poly(c, ring, 66, pa_alpha(p->col, k));
            continue;
        }
        if (p->kind == 2) {
            float rr = r * (0.6f + 0.4f * sinf(p->rot));
            PA_Vec2 st[8];
            for (int j = 0; j < 8; j++) {
                float an = (float)j * PA_PI / 4.0f;
                float l = (j & 1) ? rr * 0.25f : rr;
                st[j].x = q.x + cosf(an) * l; st[j].y = q.y + sinf(an) * l;
            }
            pa_fill_poly(c, st, 8, pa_alpha(p->col, pa_clamp01(k * 1.5f)));
        } else {
            float ca = cosf(p->rot) * r * 0.5f, sa = sinf(p->rot) * r * 0.5f;
            float flat = p->kind == 0 ? fabsf(sinf(p->rot * 0.7f)) * 0.7f + 0.3f : 1.0f;
            PA_Vec2 sq[4] = { { q.x + ca, q.y + sa * flat }, { q.x - sa, q.y + ca * flat },
                              { q.x - ca, q.y - sa * flat }, { q.x + sa, q.y - ca * flat } };
            if (p->kind == 1) {
                PA_Vec2 sd[4] = { sq[0], sq[1], sq[2], sq[3] };
                for (int j = 0; j < 4; j++) sd[j].y += r * 0.35f;
                pa_fill_poly(c, sd, 4, pa_alpha(pa_shade(p->col, -0.25f), pa_clamp01(k * 2.0f)));
            }
            pa_fill_poly(c, sq, 4, pa_alpha(p->col, pa_clamp01(k * 2.0f)));
        }
    }

    vignette(c);

    /* Floating score text: pops, rises, then cuts. */
    for (int i = 0; i < MAXFT; i++) {
        FText *f = &S.ft[i];
        if (f->t <= 0.0f) continue;
        float age = 1.3f - f->t;
        PA_Vec2 q = proj(f->x, f->y, AV_H + 6.0f + age * 2.4f);
        float pop = age < 0.12f ? 0.6f + age / 0.12f * 0.6f : (age < 0.24f ? 1.2f - (age - 0.12f) / 0.12f * 0.2f : 1.0f);
        if (f->t < 0.12f) continue;
        if (f->pill) {
            /* "+x%" in a pill badge that pops in at 1.3x and settles. */
            float sc2 = age < 0.07f ? 0.5f + age / 0.07f * 0.8f : (age < 0.25f ? 1.3f - (age - 0.07f) / 0.18f * 0.3f : 1.0f);
            float sz = f->size * sc2;
            PA_TextStyle st = pa_text_style(PA_FACE_DISPLAY, PA_RGB(255, 255, 255));
            st.outline = 2.5f * sc2; st.outline_col = pa_shade(f->col, -0.55f);
            st.align = PA_ALIGN_CENTER;
            PA_TextExtent ex = pa_text_measure(f->text, sz, &st);
            float pw = ex.width + sz * 1.3f, ph = sz * 1.55f;
            float px = q.x - pw * 0.5f, py = q.y - ph * 0.5f;
            pa_round_rect(c, px - 3, py - 3, pw + 6, ph + 9, ph * 0.5f + 3, pa_hex(C_INK));
            PA_Paint pp = pa_linear(0, py, 0, py + ph);
            pa_stop(&pp, 0.0f, pa_shade(f->col, 0.25f));
            pa_stop(&pp, 1.0f, pa_shade(f->col, -0.15f));
            pa_round_rect_paint(c, px, py, pw, ph, ph * 0.5f, &pp);
            pa_round_rect(c, px + ph * 0.3f, py + 3, pw - ph * 0.6f, ph * 0.26f, ph * 0.13f, PA_RGBA(255, 255, 255, 110));
            pa_text_ex(c, f->text, q.x, py + (ph - sz) * 0.5f + 1.0f, sz, &st);
            continue;
        }
        pa_text_bold(c, f->text, q.x, q.y - f->size * pop * 0.5f, f->size * pop, PA_RGB(255, 255, 255),
                     pa_shade(f->col, -0.45f), PA_ALIGN_CENTER, 1.5f, 1.7f);
    }
}

/* Big outlined headline with a navy drop, shrunk to fit the width. */
static void headline(PA_Canvas *c, const char *txt, float x, float y, float size, PA_Color fill) {
    float tr = size * 0.2f;
    float tw = pa_text_width(txt, size, tr);
    float maxw = (float)c->w - 36.0f;
    if (tw > maxw) { size *= maxw / tw; tr = size * 0.2f; }
    pa_text_bold(c, txt, x + size * 0.05f, y + size * 0.09f, size, pa_hex(0x101A6A), pa_hex(0x101A6A), PA_ALIGN_CENTER, tr, 1.7f);
    pa_text_bold(c, txt, x, y, size, fill, pa_hex(0x1D2A86), PA_ALIGN_CENTER, tr, 1.7f);
}

static void draw_banner(PA_Canvas *c) {
    if (S.banner_t <= 0.0f) return;
    float age = 1.6f - S.banner_t;
    if (S.banner_t < 0.15f) return;
    float pop = age < 0.1f ? 0.5f + age * 8.0f : (age < 0.22f ? 1.3f - (age - 0.1f) * 2.5f : 1.0f);
    float size = 44.0f * pop;
    float y = (float)c->h * 0.31f, cx = (float)c->w * 0.5f;
    /* Star badge with a gold gradient, a skull, and eight sparkles round it. */
    float sy = y - size * 0.5f - 52.0f * pop, r = 44.0f * pop;
    float spin = age * 1.6f;
    for (int k = 0; k < 8; k++) {
        float an = (float)k / 8.0f * PA_TAU + spin;
        float d = r * (1.45f + 0.15f * sinf(age * 9.0f + (float)k));
        float sr = r * (k & 1 ? 0.16f : 0.24f) * (0.7f + 0.3f * sinf(age * 12.0f + (float)k * 2.0f));
        PA_Vec2 sp[8];
        for (int j = 0; j < 8; j++) {
            float a2 = (float)j * PA_PI / 4.0f;
            float l = (j & 1) ? sr * 0.28f : sr;
            sp[j].x = cx + cosf(an) * d + cosf(a2) * l; sp[j].y = sy + sinf(an) * d * 0.8f + sinf(a2) * l;
        }
        pa_fill_poly(c, sp, 8, (k & 1) ? pa_hex(0xFFF3A0) : PA_RGB(255, 255, 255));
    }
    star_poly(c, cx, sy + 4, r * 1.14f, 0.56f, pa_hex(C_INK));
    PA_Vec2 st[10];
    for (int k = 0; k < 10; k++) {
        float an = -PA_PI * 0.5f + (float)k * PA_PI / 5.0f;
        float rr = (k & 1) ? r * 0.56f : r;
        st[k].x = cx + cosf(an) * rr; st[k].y = sy + sinf(an) * rr;
    }
    PA_Paint gp = pa_linear(cx, sy - r, cx, sy + r);
    pa_stop(&gp, 0.0f, pa_hex(0xFFD23F));
    pa_stop(&gp, 1.0f, pa_hex(0xFF8A00));
    pa_fill_poly_paint(c, st, 10, &gp);
    star_poly(c, cx, sy - r * 0.05f, r * 0.6f, 0.56f, PA_RGBA(255, 255, 255, 70));
    skull(c, cx, sy + r * 0.02f, r * 0.27f, PA_RGB(255, 255, 255), pa_hex(0xE07000));
    PA_TextStyle ts = pa_text_style(PA_FACE_DISPLAY, pa_hex(0xFFD23F));
    ts.fill_bottom = pa_hex(0xFF8A00);
    ts.outline = 4.5f * pop; ts.outline_col = pa_hex(C_INK);
    ts.shadow_dy = 5.0f; ts.shadow_col = pa_hex(C_INK);
    ts.align = PA_ALIGN_CENTER; ts.tracking = 2.0f;
    PA_TextExtent ex = pa_text_measure(S.banner, size, &ts);
    if (ex.width > (float)c->w - 40.0f) size *= ((float)c->w - 40.0f) / ex.width;
    pa_text_ex(c, S.banner, cx, y - size * 0.5f, size, &ts);
}

static void button(PA_Canvas *c, float x, float y, float w, float h, const char *label, PA_Color top, PA_Color side, float size) {
    pa_round_rect(c, x - 3, y - 3, w + 6, h + 11, (h + 6) * 0.5f, pa_hex(0x101A6A));
    pa_round_rect(c, x, y + 6, w, h, h * 0.5f, side);
    pa_round_rect(c, x, y, w, h, h * 0.5f, top);
    pa_round_rect(c, x + h * 0.4f, y + 5, w - h * 0.8f, h * 0.28f, h * 0.14f, pa_shade(top, 0.4f));
    pa_text_bold(c, label, x + w * 0.5f, y + (h - size) * 0.5f, size, PA_RGB(255, 255, 255),
                 pa_hex(0x101A6A), PA_ALIGN_CENTER, 2.0f, 1.8f);
}

static void draw_title(PA_Canvas *c) {
    float w = (float)c->w, h = (float)c->h;
    char buf[48];
    /* Logo. */
    float ty = h * 0.085f;
    pa_text_bold(c, "PAPER", w * 0.5f + 3, ty + 5, 64.0f, pa_hex(0x101A6A), pa_hex(0x101A6A), PA_ALIGN_CENTER, 4.0f, 3.2f);
    pa_text_bold(c, "PAPER", w * 0.5f, ty, 64.0f, PA_RGB(255, 255, 255), pa_hex(0x1D2A86), PA_ALIGN_CENTER, 4.0f, 3.2f);
    pa_text_bold(c, "TERRITORY", w * 0.5f + 2, ty + 84, 36.0f, pa_hex(0x101A6A), pa_hex(0x101A6A), PA_ALIGN_CENTER, 3.0f, 2.6f);
    pa_text_bold(c, "TERRITORY", w * 0.5f, ty + 80, 36.0f, pa_hex(0xFFD83A), pa_hex(0x1D2A86), PA_ALIGN_CENTER, 3.0f, 2.6f);

    float bw = 190.0f, bx = (w - bw) * 0.5f, by = ty + 140.0f;
    pa_round_rect(c, bx, by, bw, 36, 18, PA_RGBA(20, 34, 120, 210));
    big_star(c, bx + 22, by + 18, 13.0f);
    snprintf(buf, sizeof(buf), "BEST %.1f%%", (double)g_best / 10.0);
    pa_text_bold(c, buf, bx + bw * 0.5f + 14, by + 10, 16.0f, PA_RGB(255, 255, 255), pa_hex(0x101A6A), PA_ALIGN_CENTER, 1.5f, 1.2f);

    /* Stars wallet, top right. */
    snprintf(buf, sizeof(buf), "%d", g_stars);
    pa_round_rect(c, w - 128, 22, 110, 38, 19, PA_RGBA(20, 34, 120, 220));
    big_star(c, w - 108, 41, 14.0f);
    pa_text_bold(c, buf, w - 54, 31, 18.0f, PA_RGB(255, 255, 255), pa_hex(0x101A6A), PA_ALIGN_CENTER, 1.0f, 1.2f);

    /* Skin picker around your cube. */
    float lx, rx, ay, r;
    title_arrows(c->w, c->h, &lx, &rx, &ay, &r);
    for (int sd = -1; sd <= 1; sd += 2) {
        float x = sd < 0 ? lx : rx;
        pa_fill_circle(c, x, ay + 4, r, pa_hex(0x101A6A));
        pa_fill_circle(c, x, ay, r, PA_RGB(255, 255, 255));
        pa_fill_circle(c, x, ay, r - 4, pa_hex(0x3D8BFF));
        PA_Vec2 t[3] = { { x + (float)sd * 9.0f, ay }, { x - (float)sd * 6.0f, ay - 11.0f }, { x - (float)sd * 6.0f, ay + 11.0f } };
        pa_fill_poly(c, t, 3, PA_RGB(255, 255, 255));
    }
    float shake = sinf(S.t * 40.0f) * S.lock_shake * 8.0f;
    float ny = h * 0.56f + 52.0f;
    int locked = skin_locked(g_skin);
    float nw = pa_text_width(SKINS[g_skin].name, 20.0f, 2.0f) + 48.0f;
    pa_round_rect(c, w * 0.5f - nw * 0.5f + shake, ny, nw, 36, 18, locked ? PA_RGBA(40, 44, 70, 220) : PA_RGBA(20, 34, 120, 220));
    pa_text_bold(c, SKINS[g_skin].name, w * 0.5f + shake, ny + 8, 20.0f, PA_RGB(255, 255, 255), pa_hex(0x101A6A), PA_ALIGN_CENTER, 2.0f, 1.3f);
    /* Skin cards. */
    int owned = 0;
    for (int k = 0; k < SKIN_COUNT; k++) {
        float cx, cy, cs;
        skin_card(c->w, c->h, k, &cx, &cy, &cs);
        int lk = skin_locked(k), sel = k == g_skin;
        owned += !lk;
        if (sel) pa_round_rect(c, cx - 4, cy - 4, cs + 8, cs + 12, 16, pa_hex(0xFFD23A));
        pa_round_rect(c, cx, cy + 4, cs, cs, 13, pa_hex(0x1A3AAE));
        PA_Paint cp = pa_linear(0, cy, 0, cy + cs);
        pa_stop(&cp, 0.0f, lk ? pa_hex(0xB9C3DC) : pa_hex(0x6FD0FF));
        pa_stop(&cp, 1.0f, lk ? pa_hex(0x8E9AB8) : pa_hex(0x3D7BF0));
        pa_round_rect_paint(c, cx, cy, cs, cs, 13, &cp);
        draw_avatar(c, k, cx + cs * 0.5f, cy + cs * 0.78f, cs * 0.26f, PA_PI * 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
        if (lk) {
            pa_round_rect(c, cx, cy, cs, cs, 13, PA_RGBA(60, 66, 100, 120));
            float lx2 = cx + cs * 0.5f, ly2 = cy + cs * 0.56f;
            pa_round_rect(c, lx2 - 9, ly2 - 4, 18, 14, 3, PA_RGB(255, 255, 255));
            pa_stroke_circle(c, lx2, ly2 - 5, 5.5f, 3.0f, PA_RGB(255, 255, 255));
        }
    }
    snprintf(buf, sizeof(buf), "SKINS %d / %d", owned, SKIN_COUNT);
    {
        float cx, cy, cs;
        skin_card(c->w, c->h, SKIN_COUNT - 1, &cx, &cy, &cs);
        pa_text_bold(c, buf, w * 0.5f, cy + cs + 14, 13.0f, pa_hex(0x2A2F86), PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.5f, 1.0f);
    }
    if (locked) {
        float lyy = h * 0.56f - 112.0f;
        pa_round_rect(c, w * 0.5f - 13, lyy - 6, 26, 22, 5, pa_hex(0x1D2A86));
        pa_stroke_circle(c, w * 0.5f, lyy - 8, 8, 4, pa_hex(0x1D2A86));
        snprintf(buf, sizeof(buf), "REACH %d%% TO UNLOCK", SKINS[g_skin].unlock);
        pa_text_bold(c, buf, w * 0.5f, ny + 66, 15.0f, pa_hex(0xFFD83A), pa_hex(0x1D2A86), PA_ALIGN_CENTER, 1.5f, 1.3f);
    }

    /* Play. */
    float pulse = 1.0f + 0.04f * sinf(S.t * 5.0f);
    float pw = 250.0f * pulse, ph = 74.0f * pulse;
    button(c, (w - pw) * 0.5f, h * 0.80f - ph * 0.5f, pw, ph, locked ? "LOCKED" : "PLAY",
           locked ? pa_hex(0x9AA3C0) : pa_hex(0xFFC81E), locked ? pa_hex(0x6E7898) : pa_hex(0xF08A00), 32.0f * pulse);
    pa_text_bold(c, "DRAG TO STEER  -  LOOP BACK TO CLAIM", w * 0.5f, h * 0.80f + 58.0f, 12.0f,
                 pa_hex(0x2A2F86), PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.2f, 1.0f);
}

static void podium(PA_Canvas *c, float cx, float base, float w, float hgt, PA_Color col, int rank) {
    float rx = w * 0.5f, ry = w * 0.16f;
    pa_shadow(c, cx, base + 4, rx * 1.1f, ry * 1.1f, 0.3f);
    pa_fill_ellipse(c, cx, base, rx, ry, pa_shade(col, -0.3f));
    PA_Paint p = pa_linear(cx - rx, 0, cx + rx, 0);
    pa_stop(&p, 0.0f, pa_shade(col, -0.12f));
    pa_stop(&p, 0.35f, pa_shade(col, 0.15f));
    pa_stop(&p, 1.0f, pa_shade(col, -0.28f));
    pa_fill_rect_paint(c, cx - rx, base - hgt, w, hgt, &p);
    pa_fill_ellipse(c, cx, base - hgt, rx, ry, pa_shade(col, 0.25f));
    pa_fill_ellipse(c, cx, base - hgt, rx * 0.8f, ry * 0.7f, pa_shade(col, 0.12f));
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", rank);
    float my = base - hgt * 0.42f;
    pa_fill_circle(c, cx, my, 20, pa_shade(col, 0.35f));
    pa_fill_circle(c, cx, my, 16, pa_shade(col, -0.05f));
    pa_text_bold(c, buf, cx, my - 9, 18.0f, PA_RGB(255, 255, 255), pa_shade(col, -0.5f), PA_ALIGN_CENTER, 0.0f, 1.2f);
}

/* Light rays fanning out from a point. */
static void rays(PA_Canvas *c, float cx, float cy, float rot, PA_Color col) {
    float R = (float)(c->w + c->h);
    for (int k = 0; k < 14; k++) {
        float a0 = rot + (float)k / 14.0f * PA_TAU, a1 = a0 + PA_TAU / 28.0f;
        PA_Vec2 t[3] = { { cx, cy }, { cx + cosf(a0) * R, cy + sinf(a0) * R }, { cx + cosf(a1) * R, cy + sinf(a1) * R } };
        pa_fill_poly(c, t, 3, col);
    }
}

/* A rounded card with the skin's cube standing in it. */
static void portrait(PA_Canvas *c, float x, float y, float r, int skin) {
    const Skin *sk = &SKINS[skin];
    pa_round_rect(c, x - r - 2.5f, y - r - 2.5f, r * 2 + 5, r * 2 + 7, r * 0.42f, pa_hex(C_INK));
    PA_Paint g = pa_linear(0, y - r, 0, y + r);
    pa_stop(&g, 0.0f, pa_shade(pa_hex(sk->land), 0.45f));
    pa_stop(&g, 1.0f, pa_hex(sk->land));
    pa_round_rect_paint(c, x - r, y - r, r * 2, r * 2, r * 0.38f, &g);
    draw_avatar(c, skin, x, y + r * 0.62f, r * 0.5f, PA_PI * 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
}

/* Before the standings: a chest shakes, pops and shows what the run earned. */
static void draw_reveal(PA_Canvas *c) {
    float w = (float)c->w, h = (float)c->h, T = S.pt;
    PA_Paint bg = pa_linear(0, 0, 0, h);
    pa_stop(&bg, 0.0f, pa_hex(0xB04BEA));
    pa_stop(&bg, 0.45f, pa_hex(0x6A5CF5));
    pa_stop(&bg, 1.0f, pa_hex(0x2FA8F5));
    pa_fill_rect_paint(c, 0, 0, w, h, &bg);
    float cx = w * 0.5f, cy = h * 0.5f;
    float open = pa_clamp01((T - 0.85f) * 6.0f);
    rays(c, cx, cy - 40.0f, S.t * 0.35f, PA_RGBA(255, 255, 255, (int)(28 + 40 * open)));
    if (open > 0.0f) {
        PA_Paint gl = pa_radial(cx, cy - 40, 10.0f, 260.0f);
        pa_stop(&gl, 0.0f, PA_RGBA(255, 255, 230, (int)(230 * open)));
        pa_stop(&gl, 1.0f, PA_RGBA(255, 220, 120, 0));
        pa_fill_rect_paint(c, cx - 260, cy - 300, 520, 520, &gl);
    }
    headline(c, open <= 0.0f ? "OPENING..." : S.unlocked >= 0 ? "NEW SKIN!" : "REWARD!", cx, h * 0.1f, 46.0f,
             pa_hex(0xFFE04A));

    /* Chest. */
    float shake = T < 0.85f ? sinf(T * 60.0f) * 6.0f * T : 0.0f;
    float bw = 190.0f, bh = 118.0f, bx = cx - bw * 0.5f + shake, by = cy + 40.0f;
    pa_shadow(c, cx, by + bh + 6, bw * 0.6f, 18.0f, 0.4f);
    pa_round_rect(c, bx - 4, by - 4, bw + 8, bh + 8, 20, pa_hex(C_INK));
    PA_Paint cg = pa_linear(0, by, 0, by + bh);
    pa_stop(&cg, 0.0f, pa_hex(0xFFC21E));
    pa_stop(&cg, 1.0f, pa_hex(0xF07A00));
    pa_round_rect_paint(c, bx, by, bw, bh, 16, &cg);
    pa_fill_rect(c, bx + bw * 0.18f, by, 22, bh, pa_hex(0x7A3BE0));
    pa_fill_rect(c, bx + bw * 0.82f - 22, by, 22, bh, pa_hex(0x7A3BE0));
    pa_round_rect(c, cx - 18 + shake, by + 18, 36, 40, 8, pa_hex(C_INK));
    pa_round_rect(c, cx - 14 + shake, by + 22, 28, 32, 6, pa_hex(0xFFE9A0));
    float lid_y = by - 64.0f - open * 160.0f, lid_a = 1.0f - open;
    if (lid_a > 0.02f) {
        pa_round_rect(c, bx - 10, lid_y - 4, bw + 20, 72, 30, pa_alpha(pa_hex(C_INK), lid_a));
        pa_round_rect(c, bx - 6, lid_y, bw + 12, 64, 28, pa_alpha(pa_hex(0xFFD23F), lid_a));
        pa_fill_rect(c, bx + bw * 0.18f, lid_y, 22, 64, pa_alpha(pa_hex(0x7A3BE0), lid_a));
        pa_fill_rect(c, bx + bw * 0.82f - 22, lid_y, 22, 64, pa_alpha(pa_hex(0x7A3BE0), lid_a));
    }
    /* The card rises out of it. */
    float rise = pa_smooth(pa_clamp01((T - 0.9f) / 0.5f));
    if (rise > 0.0f) {
        float sc = 0.3f + 0.7f * rise;
        float cw2 = 200.0f * sc, ch2 = 250.0f * sc;
        float kx = cx - cw2 * 0.5f, ky = by - 40.0f - rise * 210.0f - ch2 * 0.5f;
        pa_round_rect(c, kx - 5, ky - 5, cw2 + 10, ch2 + 14, 26 * sc, pa_hex(C_INK));
        PA_Paint kg = pa_linear(0, ky, 0, ky + ch2);
        pa_stop(&kg, 0.0f, pa_hex(0x7FE6FF));
        pa_stop(&kg, 1.0f, pa_hex(0x3D7BF0));
        pa_round_rect_paint(c, kx, ky, cw2, ch2, 22 * sc, &kg);
        pa_round_rect(c, kx + 10 * sc, ky + 8 * sc, cw2 - 20 * sc, 26 * sc, 12 * sc, PA_RGBA(255, 255, 255, 90));
        char buf[24];
        if (S.unlocked >= 0) {
            draw_avatar(c, S.unlocked, cx, ky + ch2 * 0.6f, 30.0f * sc, PA_PI * 0.5f,
                        0.15f * fabsf(sinf(S.t * 4.0f)), 1.0f, 0.0f, 0.3f, 0.0f);
            snprintf(buf, sizeof(buf), "%s", SKINS[S.unlocked].name);
        } else {
            big_star(c, cx, ky + ch2 * 0.42f, 62.0f * sc);
            snprintf(buf, sizeof(buf), "+%d", S.reward);
        }
        headline(c, buf, cx, ky + ch2 * 0.74f, 32.0f * sc, pa_hex(0xFFE04A));
        for (int k = 0; k < 8; k++) {
            float an = (float)k / 8.0f * PA_TAU + S.t * 1.2f;
            float d = cw2 * 0.75f + 10.0f * sinf(S.t * 6.0f + (float)k);
            star_poly(c, cx + cosf(an) * d, ky + ch2 * 0.5f + sinf(an) * d, 7.0f * sc + 3.0f * (float)(k & 1), 0.4f,
                      (k & 1) ? pa_hex(0xFFF3A0) : PA_RGB(255, 255, 255));
        }
    }
    if (T > 1.5f) pa_text_bold(c, "TAP TO CONTINUE", cx, h * 0.86f, 16.0f, PA_RGB(255, 255, 255), pa_hex(C_INK),
                               PA_ALIGN_CENTER, 2.0f, 1.2f);
    for (int i = 0; i < MAXPART; i++) {
        Part *q = &S.parts[i];
        if (q->life <= 0.0f || q->kind != 9) continue;
        float x = q->x * w, y = q->y * h, r = 7.0f;
        float ca = cosf(q->rot) * r, sa = sinf(q->rot) * r * 0.5f;
        PA_Vec2 qq[4] = { { x + ca, y + sa }, { x - sa, y + ca * 0.5f }, { x - ca, y - sa }, { x + sa, y - ca * 0.5f } };
        pa_fill_poly(c, qq, 4, q->col);
    }
}

static void draw_results(PA_Canvas *c) {
    float w = (float)c->w, h = (float)c->h;
    float T = S.pt - REVEAL_T;
    float a = pa_smooth(pa_clamp01(T * 3.0f));
    PA_Paint bg = pa_linear(0, 0, 0, h);
    pa_stop(&bg, 0.0f, pa_hex(0x9A4BEA));
    pa_stop(&bg, 0.30f, pa_hex(0x5B6FF2));
    pa_stop(&bg, 0.48f, pa_hex(0x55B6F6));
    pa_stop(&bg, 0.49f, pa_hex(0xA6E9F4));
    pa_stop(&bg, 1.0f, pa_hex(0xE4FBF3));
    pa_fill_rect_paint(c, 0, 0, w, h, &bg);
    /* Stadium lights. */
    for (int k = 0; k < 2; k++) {
        float lx = k ? w * 0.88f : w * 0.12f;
        PA_Paint g = pa_radial(lx, h * 0.07f, 10.0f, 130.0f);
        pa_stop(&g, 0.0f, PA_RGBA(255, 255, 255, 220));
        pa_stop(&g, 0.25f, PA_RGBA(255, 240, 255, 120));
        pa_stop(&g, 1.0f, PA_RGBA(160, 120, 255, 0));
        pa_fill_rect_paint(c, lx - 130, h * 0.07f - 130, 260, 260, &g);
    }
    rays(c, w * 0.5f, h * 0.30f, S.t * 0.15f, PA_RGBA(255, 255, 255, 34));
    pa_fill_ellipse(c, w * 0.5f, h * 0.53f, w * 0.75f, h * 0.06f, pa_hex(0xF2FFFB));

    /* Title. */
    const char *title = S.cause == CAUSE_NONE ? "VICTORY!" : S.rank == 1 ? "#1 FINISH!" : "GAME OVER";
    char sub[48];
    switch (S.cause) {
    case CAUSE_CUT:      snprintf(sub, sizeof(sub), "CUT BY %s", S.killer); break;
    case CAUSE_SELF:     snprintf(sub, sizeof(sub), "YOU CROSSED YOUR OWN TRAIL"); break;
    case CAUSE_SURROUND: snprintf(sub, sizeof(sub), "SURROUNDED BY %s", S.killer); break;
    case CAUSE_HEAD:     snprintf(sub, sizeof(sub), "CRASHED INTO %s", S.killer); break;
    case CAUSE_LAND:     snprintf(sub, sizeof(sub), "%s TOOK YOUR LAND", S.killer); break;
    default:             snprintf(sub, sizeof(sub), S.final_pct >= 99.5f ? "THE WHOLE ARENA IS YOURS" : "EVERY RIVAL IS GONE"); break;
    }
    float ty = h * 0.045f - (1.0f - a) * 60.0f;
    headline(c, title, w * 0.5f, ty, 44.0f, S.rank == 1 ? pa_hex(0xFFD83A) : PA_RGB(255, 255, 255));
    pa_text_bold(c, sub, w * 0.5f, ty + 60, 17.0f, PA_RGB(255, 255, 255), pa_hex(0x1D2A86), PA_ALIGN_CENTER, 1.5f, 1.3f);
    char buf[24];
    {
        /* Ribbons for a new best and a skin it unlocked. */
        float ry = ty + 92.0f;
        int nr = S.new_best + 1;
        float rx = w * 0.5f - (nr == 2 ? 110.0f : 0.0f);
        float pop = 1.0f + 0.06f * sinf(S.t * 7.0f);
        if (S.new_best) {
            pa_round_rect(c, rx - 90 * pop, ry, 180 * pop, 34, 17, pa_hex(0xFFD23A));
            pa_text_bold(c, "NEW BEST!", rx, ry + 9, 17.0f, PA_RGB(255, 255, 255), pa_hex(0xC2570C), PA_ALIGN_CENTER, 2.0f, 1.3f);
            rx += 220.0f;
        }
        if (S.unlocked < 0) {
            snprintf(buf, sizeof(buf), "+%d STARS", S.reward);
            float bw2 = pa_text_width(buf, 14.0f, 1.0f) + 44.0f;
            pa_round_rect(c, rx - bw2 * 0.5f, ry, bw2, 34, 17, pa_hex(0x6A3BE0));
            big_star(c, rx - bw2 * 0.5f + 18, ry + 17, 11.0f);
            pa_text_bold(c, buf, rx + 10, ry + 10, 14.0f, PA_RGB(255, 255, 255), pa_hex(0x2A1A70), PA_ALIGN_CENTER, 1.0f, 1.2f);
        }
        if (S.unlocked >= 0) {
            snprintf(buf, sizeof(buf), "NEW SKIN: %s", SKINS[S.unlocked].name);
            float bw2 = pa_text_width(buf, 14.0f, 1.0f) + 34.0f;
            pa_round_rect(c, rx - bw2 * 0.5f, ry, bw2, 34, 17, pa_hex(0xE04FD0));
            pa_text_bold(c, buf, rx, ry + 10, 14.0f, PA_RGB(255, 255, 255), pa_hex(0x7A1A70), PA_ALIGN_CENTER, 1.0f, 1.2f);
        }
    }

    /* Podium with the top three cubes on it. */
    float base = h * 0.46f;
    static const float PX[3] = { 0.0f, -1.0f, 1.0f };
    static const float PH[3] = { 150.0f, 112.0f, 86.0f };
    static const uint32_t PC[3] = { 0xFFC22E, 0x4FA3F7, 0xF5594E };
    int order[3] = { 1, 2, 0 };
    for (int oi = 0; oi < 3; oi++) {
        int k = order[oi];
        if (k >= S.res_n) continue;
        float cx = w * 0.5f + PX[k] * w * 0.28f;
        float rise = pa_smooth(pa_clamp01(T * 2.5f - (float)k * 0.25f));
        float ph = PH[k] * rise;
        podium(c, cx, base, w * 0.25f, ph, pa_hex(PC[k]), k + 1);
        if (rise > 0.3f) {
            float bob = fabsf(sinf(S.t * 3.0f + (float)k)) * (k == 0 ? 0.6f : 0.25f);
            draw_avatar(c, S.res[k].skin, cx, base - ph - 6.0f, 34.0f, PA_PI * 0.5f, bob, 1.0f, 0.0f, 0.0f, 0.0f);
            pa_text_bold(c, S.res[k].name, cx, base - ph - 96.0f - bob * 20.0f, 14.0f, PA_RGB(255, 255, 255),
                         pa_hex(0x1D2A86), PA_ALIGN_CENTER, 1.0f, 1.2f);
        }
    }

    /* Leaderboard card. */
    float cw = w * 0.88f, cx = (w - cw) * 0.5f, cy = h * 0.475f + (1.0f - a) * 80.0f;
    int rows = S.res_n < 3 ? S.res_n : 3;
    int extra = S.res_me >= 3 ? 1 : 0;
    float rh = h < 1050.0f ? 46.0f : 54.0f, chh = 64.0f + (float)(rows + extra) * (rh + 8.0f) + 8.0f;
    pa_round_rect(c, cx - 4, cy - 4, cw + 8, chh + 14, 26, pa_hex(0x101A6A));
    pa_round_rect(c, cx, cy + 6, cw, chh, 24, pa_hex(0x1A3AAE));
    pa_round_rect(c, cx, cy, cw, chh, 24, pa_hex(0x2E5BE6));
    pa_round_rect(c, cx + 8, cy + 8, cw - 16, 44, 18, pa_hex(0x2448C8));
    pa_text_bold(c, "LEADERBOARD", w * 0.5f, cy + 18, 24.0f, PA_RGB(255, 255, 255), pa_hex(0x101A6A), PA_ALIGN_CENTER, 2.0f, 1.6f);
    static const uint32_t MEDAL[3] = { 0xFFC22E, 0xC8D4E6, 0xF09A4E };
    for (int r = 0; r < rows + extra; r++) {
        int k = r < rows ? r : S.res_me;
        float ry = cy + 62.0f + (float)r * (rh + 8.0f);
        int mine = k == S.res_me;
        pa_round_rect(c, cx + 12, ry + 4, cw - 24, rh, 16, pa_hex(0x1A3AAE));
        pa_round_rect(c, cx + 12, ry, cw - 24, rh, 16, mine ? pa_hex(0xFFDE3A) : pa_hex(0xF4F8FF));
        float mx = cx + 42, my = ry + rh * 0.5f;
        pa_fill_circle(c, mx, my + 2, 19, pa_hex(0x101A6A));
        pa_fill_circle(c, mx, my, 19, k < 3 ? pa_hex(MEDAL[k]) : pa_hex(0x8FA6D8));
        pa_fill_circle(c, mx, my, 14, k < 3 ? pa_shade(pa_hex(MEDAL[k]), 0.25f) : pa_hex(0xA9BCE6));
        snprintf(buf, sizeof(buf), "%d", k + 1);
        pa_text_bold(c, buf, mx, my - 8, 16.0f, PA_RGB(255, 255, 255), pa_hex(0x6A4A10), PA_ALIGN_CENTER, 0.0f, 1.1f);
        portrait(c, cx + 92, my, 21.0f, S.res[k].skin);
        pa_text(c, S.res[k].name, cx + 120, my - 9, 18.0f, pa_hex(C_NAVY), PA_ALIGN_LEFT, 1.5f);
        snprintf(buf, sizeof(buf), "%.1f%%", (double)S.res[k].pct);
        float pw = 98.0f, px = cx + cw - 24 - pw - 8;
        pa_round_rect(c, px, my - 16, pw, 32, 16, mine ? pa_hex(0xFFB800) : pa_hex(0xBFE6FF));
        pa_text_bold(c, buf, px + pw * 0.5f, my - 8, 16.0f, PA_RGB(255, 255, 255), pa_hex(0x1D2A86), PA_ALIGN_CENTER, 1.0f, 1.2f);
    }

    /* Your numbers. */
    float sy = cy + chh + 22.0f;
    skull(c, w * 0.21f, sy + 9, 9.0f, pa_hex(0x2A2F86), pa_hex(0xE4FBF3));
    snprintf(buf, sizeof(buf), "KILLS %d", S.final_kills);
    pa_text_bold(c, buf, w * 0.21f + 16, sy, 17.0f, pa_hex(0x2A2F86), PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 1.5f, 1.2f);
    big_star(c, w * 0.60f, sy + 9, 11.0f);
    snprintf(buf, sizeof(buf), "BEST %.1f%%", (double)g_best / 10.0);
    pa_text_bold(c, buf, w * 0.60f + 18, sy, 17.0f, pa_hex(0x2A2F86), PA_RGB(255, 255, 255), PA_ALIGN_LEFT, 1.5f, 1.2f);

    float bx, by, bw, bh, my;
    S.res_btn_y = sy + 56.0f;
    results_buttons(c->w, c->h, &bx, &by, &bw, &bh, &my);
    float pulse = 1.0f + 0.03f * sinf(S.t * 5.0f);
    button(c, bx - bw * (pulse - 1.0f) * 0.5f, by, bw * pulse, bh, "PLAY AGAIN", pa_hex(0xFFC81E), pa_hex(0xF08A00), 26.0f);
    pa_text_bold(c, "MENU", w * 0.5f, my - 9, 18.0f, PA_RGB(255, 255, 255), pa_hex(0x1D2A86), PA_ALIGN_CENTER, 3.0f, 1.3f);

    /* Confetti in screen space. */
    for (int i = 0; i < MAXPART; i++) {
        Part *p = &S.parts[i];
        if (p->life <= 0.0f || p->kind != 9) continue;
        float x = p->x * w, y = p->y * h, r = 7.0f;
        float ca = cosf(p->rot) * r, sa = sinf(p->rot) * r * 0.5f;
        PA_Vec2 q[4] = { { x + ca, y + sa }, { x - sa, y + ca * 0.5f }, { x - ca, y - sa }, { x + sa, y - ca * 0.5f } };
        pa_fill_poly(c, q, 4, p->col);
    }
}

static void s_render(PA_Canvas *c) {
    g_vw = c->w; g_vh = c->h;
    if (S.phase == PH_RESULTS) {
        pa_hub_hide_pause();
        if (S.pt < REVEAL_T) draw_reveal(c);
        else draw_results(c);
        return;
    }
    draw_scene(c);
    float w = (float)c->w, h = (float)c->h;

    if (S.phase == PH_TITLE) {
        pa_hub_pause_anchor(36.0f, 40.0f, 22.0f);
        draw_title(c);
    } else {
        pa_hub_pause_anchor(36.0f, 45.0f, 22.0f);
        progress_bar(c, pct_of(0) > 0.0f ? pct_of(0) : S.final_pct);
        leaderboard(c);
        if (S.phase == PH_PLAY && S.pt < 2.5f) {
            float k = S.pt < 2.0f ? 1.0f : (2.5f - S.pt) * 2.0f;
            if (k > 0.5f)
                pa_text_bold(c, "LOOP BACK HOME TO CLAIM LAND", w * 0.5f, h * 0.84f, 15.0f, pa_hex(0x2A2F86),
                             PA_RGB(255, 255, 255), PA_ALIGN_CENTER, 1.5f, 1.2f);
        }
        draw_banner(c);
        if (S.phase == PH_DEAD) {
            char sub[40];
            const char *head = S.cause == CAUSE_SELF ? "OOPS!" : S.cause == CAUSE_SURROUND ? "SURROUNDED!"
                             : S.cause == CAUSE_HEAD ? "CRASH!" : S.cause == CAUSE_LAND ? "WIPED OUT!" : "CUT OFF!";
            headline(c, head, w * 0.5f, h * 0.3f, 52.0f, pa_hex(0xFF5A6E));
            if (S.killer[0] && S.cause != CAUSE_SELF) snprintf(sub, sizeof(sub), "BY %s", S.killer);
            else snprintf(sub, sizeof(sub), "YOU CROSSED YOUR TRAIL");
            pa_text_bold(c, sub, w * 0.5f, h * 0.3f + 70, 20.0f, PA_RGB(255, 255, 255), pa_hex(0x1D2A86), PA_ALIGN_CENTER, 2.0f, 1.4f);
        }
        if (S.phase == PH_WIN) {
            float a = pa_smooth(pa_clamp01(S.pt * 2.5f));
            PA_Paint p = pa_radial(w * 0.5f, h * 0.28f, 10.0f, w * 0.8f);
            pa_stop(&p, 0.0f, PA_RGBA(255, 244, 170, (int)(150 * a)));
            pa_stop(&p, 1.0f, PA_RGBA(255, 200, 40, (int)(40 * a)));
            pa_fill_rect_paint(c, 0, 0, w, h, &p);
            float sy = h * 0.19f;
            int full = S.final_pct >= 99.5f;
            char pc[16];
            if (full) snprintf(pc, sizeof(pc), "100%%");
            else snprintf(pc, sizeof(pc), "%.1f%%", (double)S.final_pct);
            big_star(c, w * 0.5f, sy, 76.0f * a);
            if (a > 0.05f) {
                headline(c, pc, w * 0.5f, sy + 92.0f, 40.0f * a, PA_RGB(255, 255, 255));
                headline(c, full ? "ARENA" : "LAST ONE", w * 0.5f, sy + 152.0f, 46.0f * a, pa_hex(0xFFE04A));
                headline(c, full ? "CONQUERED!" : "STANDING!", w * 0.5f, sy + 212.0f, 46.0f * a, pa_hex(0xFFE04A));
            }
        }
    }
    if (S.flash > 0.0f) pa_fill_rect(c, 0, 0, w, h, pa_alpha(S.flash_col, pa_clamp01(S.flash * 1.6f) * 0.5f));
}

/* ------------------------------------------------------------------ thumb -- */
static void blob(PA_Canvas *c, float cx, float cy, float rx, float ry, float th, PA_Color col) {
    pa_fill_ellipse(c, cx + 2, cy + th + 3, rx, ry, PA_RGBA(60, 120, 110, 50));
    pa_fill_ellipse(c, cx, cy + th, rx, ry, pa_shade(col, -0.2f));
    pa_fill_rect(c, cx - rx, cy, rx * 2, th, pa_shade(col, -0.2f));
    pa_fill_ellipse(c, cx, cy, rx, ry, col);
}

static void s_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    pa_fill_rect(c, x, y, w, h, pa_hex(C_MINT));
    pa_fill_ellipse(c, x + w * 0.8f, y + h * 0.2f, w * 0.18f, h * 0.12f, pa_hex(C_SPLAT));
    float th = h * 0.04f;
    blob(c, x + w * 0.30f, y + h * 0.62f, w * 0.26f, h * 0.2f, th, pa_hex(0xF7A3CB));
    blob(c, x + w * 0.78f, y + h * 0.78f, w * 0.2f, h * 0.14f, th, pa_hex(0x3CC3F2));
    float ang = t * 1.4f;
    float bx = x + w * 0.55f + cosf(ang) * w * 0.18f, by = y + h * 0.40f + sinf(ang) * h * 0.14f;
    PA_Vec2 pts[24];
    for (int k = 0; k < 24; k++) {
        float a2 = ang - (float)k * 0.1f;
        pts[k].x = x + w * 0.55f + cosf(a2) * w * 0.18f;
        pts[k].y = y + h * 0.40f + sinf(a2) * h * 0.14f;
    }
    pa_stroke_poly(c, pts, 24, 0, w * 0.09f, pa_hex(0xFBD0E3));
    draw_avatar(c, D_DONUT, bx, by + h * 0.04f, w * 0.06f, ang + PA_PI * 0.5f, 0.0f, 1.0f, 0.0f, 0.3f, 1.0f);
}

const PA_Game PA_GAME_PAPER = {
    "paper", "Paper Territory", "Territory io",
    "Drive out, loop back, claim what you enclosed.",
    PA_RGB(247, 140, 190),
    s_start, s_stop, s_update, s_render, s_thumb
};
