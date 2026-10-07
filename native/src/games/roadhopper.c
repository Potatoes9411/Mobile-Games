/* ===========================================================================
   ROAD HOPPER - native
   The endless lane-hopper. Tap to hop forward, swipe to sidestep or back up,
   thread a voxel critter through traffic, log rivers, lily pads and express
   trains. The camera creeps forward the whole time and an eagle takes anyone
   who dawdles.

   Every character belongs to a world - the chicken to the classic meadow, the
   emu to the outback, the penguin to the arctic, the pumpkin to the spooky
   world - and picking one rebuilds the whole board in that world's palette,
   props, vehicles and rivers, the way the reference ties worlds to its roster.

   Rows are generated on demand from a hash of the row index and recycled once
   they fall behind, so an endless track costs a fixed amount of memory.
   =========================================================================== */
#include "../pa.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define HALF        4                 /* playfield spans -HALF..HALF columns */
#define COLS        (HALF * 2 + 1)
#define EDGE        (HALF + 0.5f)
#define SAFE_ROWS   3
#define HOP_TIME    0.14f
#define IDLE_LIMIT  7.0f
#define ROW_CAP     72
#define MAX_MOVERS  10
#define MAX_PARTS   24
#define MAX_FX      220
#define PRIZE_COST  100
#define RESULTS_AT  1.25f
#define CHAR_SCALE  1.14f

/* Ground heights. Grass sits a step above the road so its near edge shows a
   lip; rivers are sunk below both so their banks show. */
#define Z_GRASS     0.08f
#define Z_ROAD      0.00f
#define Z_WATER    -0.12f

enum { ROW_GRASS, ROW_ROAD, ROW_WATER, ROW_RAIL };
enum { P_NONE, P_TREE, P_BUSH, P_ROCK, P_STUMP };
enum { D_NONE, D_TUFT, D_FLOWER, D_PEBBLE };
enum { V_SEDAN, V_TAXI, V_VAN, V_TRUCK, V_BUS };
enum { DEATH_CAR, DEATH_TRAIN, DEATH_WATER, DEATH_EAGLE };

/* ------------------------------------------------------------------ worlds -- */
enum { W_CLASSIC, W_OUTBACK, W_ARCTIC, W_SPOOKY, W_COUNT };

typedef struct {
    const char *name;
    uint32_t grass_a, grass_b, road, dash, water, water_hi;
    uint32_t bed, sleeper, rail;
    uint32_t log, log_dark, pad;
    uint32_t tuft, flower_a, flower_b, pebble;
    uint32_t train_a, train_b;
    uint32_t car[6];
} World;

/*
 * Ground colours sampled from the plates of each world. Classic grass is a
 * fresh mid green rather than acid lime, the outback is warm sand, the arctic
 * is blue-white snow over deep blue water and the spooky world is pumpkin
 * orange over a dark plum road.
 */
static const World WORLDS[W_COUNT] = {
    { "MEADOW",
      0x88D65A, 0x7DCB50, 0x545468, 0x8C8EA2, 0x5CC6F7, 0xA8E6FF,
      0x4E4C5E, 0x7A4535, 0x9C96BA,
      0x8E4B3A, 0x6B3328, 0x4FA640,
      0x69B544, 0xF25A5A, 0xFFF4C0, 0xA9A9B8,
      0xE24B4B, 0x3E7BE0,
      { 0x8BD14E, 0x2FA4E7, 0x8B6CE6, 0xEF5B3A, 0xE8414E, 0xF27FB2 } },
    { "OUTBACK",
      0xE6AA66, 0xDDA05C, 0x4E4C5E, 0x7E7C8E, 0x5CC6F7, 0xA8E6FF,
      0x4E4A58, 0x7A4535, 0x9C96BA,
      0x8E4B3A, 0x6B3328, 0x6E9A4A,
      0xC98A4C, 0xE85A3C, 0xF5D36A, 0xC2B9E0,
      0xE8A33A, 0x8E5BD6,
      { 0x8BD14E, 0x2FA4E7, 0x8B6CE6, 0xEF5B3A, 0xF2C230, 0xE8414E } },
    { "ARCTIC",
      0xF1F6FB, 0xE4EDF6, 0x5E6A80, 0x9AA6BC, 0x2E9AF0, 0x8CCBFF,
      0x56607A, 0x6B4E46, 0xB8C2D8,
      0xF4F9FF, 0xBCD6EE, 0xDDEEFF,
      0xCFE0F0, 0x8FD4FF, 0xFFFFFF, 0xA8D8F4,
      0xD84040, 0x3E7BE0,
      { 0xE8414E, 0x2FA4E7, 0x8B6CE6, 0xF2C230, 0x4CB86A, 0xEF5B3A } },
    { "SPOOKY",
      0xDE852E, 0xD27A28, 0x3C3346, 0x6A5F78, 0x9A2E2A, 0xFFB04A,
      0x3A3040, 0x5A3A30, 0x7A6E90,
      0x3E3446, 0x2A2232, 0x6B6A3A,
      0xA85A20, 0xF7C84A, 0x6A4A8A, 0x8A84A0,
      0x5A4A78, 0x7A9A3A,
      { 0x8B6CE6, 0x8AA04A, 0xD9B25A, 0x5A6A9A, 0xB04A6A, 0x7A6A8A } }
};

/* -------------------------------------------------------------- characters -- */
/* Parts are authored facing +lf; lx is to the critter's right. */
typedef struct { float lx, lf, z, w, d, h; uint32_t col; } PartDef;
typedef struct { const char *name; int world; const PartDef *parts; int n; float hover; } CharDef;

#define CW 0xF7F7F2
#define CK 0x1A1424
static const PartDef P_CHICKEN[] = {
    { -0.10f,  0.00f, 0.00f, 0.07f, 0.07f, 0.16f, 0xF29A2E },
    {  0.10f,  0.00f, 0.00f, 0.07f, 0.07f, 0.16f, 0xF29A2E },
    {  0.00f, -0.04f, 0.14f, 0.50f, 0.48f, 0.40f, CW },
    {  0.00f, -0.31f, 0.40f, 0.30f, 0.10f, 0.18f, 0xE4E4DC },
    { -0.27f, -0.06f, 0.24f, 0.05f, 0.30f, 0.20f, 0xE4E4DC },
    {  0.27f, -0.06f, 0.24f, 0.05f, 0.30f, 0.20f, 0xE4E4DC },
    {  0.00f,  0.08f, 0.54f, 0.38f, 0.32f, 0.30f, CW },
    {  0.00f,  0.29f, 0.62f, 0.14f, 0.10f, 0.09f, 0xF29A2E },
    {  0.00f,  0.27f, 0.52f, 0.08f, 0.06f, 0.12f, 0xE8403C },
    {  0.00f,  0.06f, 0.84f, 0.09f, 0.22f, 0.12f, 0xE8403C },
    { -0.195f, 0.14f, 0.70f, 0.02f, 0.07f, 0.07f, CK },
    {  0.195f, 0.14f, 0.70f, 0.02f, 0.07f, 0.07f, CK }
};
static const PartDef P_BUNNY[] = {
    { -0.12f,  0.06f, 0.00f, 0.12f, 0.20f, 0.08f, 0xD8D4D0 },
    {  0.12f,  0.06f, 0.00f, 0.12f, 0.20f, 0.08f, 0xD8D4D0 },
    {  0.00f, -0.04f, 0.06f, 0.46f, 0.46f, 0.38f, 0xF4F2F0 },
    {  0.00f, -0.30f, 0.14f, 0.16f, 0.10f, 0.16f, 0xFFFFFF },
    {  0.00f,  0.06f, 0.42f, 0.40f, 0.36f, 0.32f, 0xF4F2F0 },
    { -0.10f,  0.02f, 0.74f, 0.09f, 0.08f, 0.36f, 0xF4F2F0 },
    {  0.10f,  0.02f, 0.74f, 0.09f, 0.08f, 0.36f, 0xF4F2F0 },
    { -0.10f,  0.065f,0.78f, 0.05f, 0.01f, 0.26f, 0xF4A6B6 },
    {  0.10f,  0.065f,0.78f, 0.05f, 0.01f, 0.26f, 0xF4A6B6 },
    {  0.00f,  0.245f,0.56f, 0.08f, 0.02f, 0.06f, 0xF4A6B6 },
    { -0.205f, 0.12f, 0.60f, 0.02f, 0.07f, 0.07f, CK },
    {  0.205f, 0.12f, 0.60f, 0.02f, 0.07f, 0.07f, CK }
};
static const PartDef P_PIG[] = {
    { -0.14f, -0.14f, 0.00f, 0.09f, 0.09f, 0.12f, 0xE07A92 },
    {  0.14f, -0.14f, 0.00f, 0.09f, 0.09f, 0.12f, 0xE07A92 },
    { -0.14f,  0.14f, 0.00f, 0.09f, 0.09f, 0.12f, 0xE07A92 },
    {  0.14f,  0.14f, 0.00f, 0.09f, 0.09f, 0.12f, 0xE07A92 },
    {  0.00f, -0.02f, 0.12f, 0.50f, 0.58f, 0.36f, 0xF5A3B5 },
    {  0.00f, -0.34f, 0.38f, 0.06f, 0.06f, 0.10f, 0xE07A92 },
    {  0.00f,  0.30f, 0.20f, 0.40f, 0.26f, 0.36f, 0xF5A3B5 },
    {  0.00f,  0.46f, 0.27f, 0.20f, 0.06f, 0.14f, 0xE07A92 },
    { -0.13f,  0.30f, 0.56f, 0.10f, 0.06f, 0.08f, 0xE07A92 },
    {  0.13f,  0.30f, 0.56f, 0.10f, 0.06f, 0.08f, 0xE07A92 },
    { -0.205f, 0.36f, 0.42f, 0.02f, 0.06f, 0.06f, CK },
    {  0.205f, 0.36f, 0.42f, 0.02f, 0.06f, 0.06f, CK }
};
static const PartDef P_EMU[] = {
    { -0.08f,  0.00f, 0.00f, 0.06f, 0.06f, 0.30f, 0x9A8C80 },
    {  0.08f,  0.00f, 0.00f, 0.06f, 0.06f, 0.30f, 0x9A8C80 },
    {  0.00f, -0.06f, 0.28f, 0.44f, 0.54f, 0.34f, 0x7A665C },
    {  0.00f, -0.38f, 0.42f, 0.30f, 0.12f, 0.18f, 0x5E4C44 },
    {  0.00f,  0.16f, 0.56f, 0.14f, 0.14f, 0.34f, 0x8C7A70 },
    {  0.00f,  0.20f, 0.86f, 0.22f, 0.24f, 0.18f, 0x7FA8DA },
    {  0.00f,  0.36f, 0.89f, 0.10f, 0.10f, 0.06f, 0x3A3236 },
    { -0.115f, 0.24f, 0.96f, 0.02f, 0.05f, 0.05f, CK },
    {  0.115f, 0.24f, 0.96f, 0.02f, 0.05f, 0.05f, CK }
};
static const PartDef P_ROO[] = {
    { -0.10f,  0.10f, 0.00f, 0.10f, 0.28f, 0.08f, 0x9A6438 },
    {  0.10f,  0.10f, 0.00f, 0.10f, 0.28f, 0.08f, 0x9A6438 },
    {  0.00f, -0.06f, 0.06f, 0.40f, 0.40f, 0.26f, 0xC98B55 },
    {  0.00f, -0.40f, 0.06f, 0.12f, 0.36f, 0.10f, 0xC98B55 },
    {  0.00f,  0.00f, 0.30f, 0.34f, 0.30f, 0.34f, 0xC98B55 },
    {  0.00f,  0.155f,0.32f, 0.20f, 0.01f, 0.26f, 0xE8C49A },
    { -0.12f,  0.18f, 0.36f, 0.06f, 0.10f, 0.12f, 0x9A6438 },
    {  0.12f,  0.18f, 0.36f, 0.06f, 0.10f, 0.12f, 0x9A6438 },
    {  0.00f,  0.08f, 0.64f, 0.26f, 0.30f, 0.22f, 0xC98B55 },
    {  0.00f,  0.27f, 0.66f, 0.14f, 0.10f, 0.12f, 0xE8C49A },
    { -0.08f,  0.02f, 0.86f, 0.07f, 0.05f, 0.18f, 0xC98B55 },
    {  0.08f,  0.02f, 0.86f, 0.07f, 0.05f, 0.18f, 0xC98B55 },
    { -0.135f, 0.14f, 0.76f, 0.02f, 0.05f, 0.05f, CK },
    {  0.135f, 0.14f, 0.76f, 0.02f, 0.05f, 0.05f, CK }
};
static const PartDef P_PENGUIN[] = {
    { -0.10f,  0.08f, 0.00f, 0.12f, 0.18f, 0.05f, 0xF7A32B },
    {  0.10f,  0.08f, 0.00f, 0.12f, 0.18f, 0.05f, 0xF7A32B },
    {  0.00f,  0.00f, 0.05f, 0.44f, 0.40f, 0.56f, 0x2A2E3C },
    {  0.00f,  0.205f,0.08f, 0.30f, 0.01f, 0.46f, 0xF4F6FA },
    { -0.235f, 0.00f, 0.18f, 0.03f, 0.18f, 0.30f, 0x2A2E3C },
    {  0.235f, 0.00f, 0.18f, 0.03f, 0.18f, 0.30f, 0x2A2E3C },
    {  0.00f,  0.02f, 0.61f, 0.38f, 0.34f, 0.26f, 0x2A2E3C },
    {  0.00f,  0.195f,0.64f, 0.26f, 0.01f, 0.16f, 0xF4F6FA },
    {  0.00f,  0.25f, 0.69f, 0.10f, 0.10f, 0.06f, 0xF7A32B },
    { -0.08f,  0.20f, 0.74f, 0.05f, 0.01f, 0.05f, CK },
    {  0.08f,  0.20f, 0.74f, 0.05f, 0.01f, 0.05f, CK }
};
static const PartDef P_POLAR[] = {
    { -0.15f, -0.16f, 0.00f, 0.12f, 0.12f, 0.14f, 0xDCE2E8 },
    {  0.15f, -0.16f, 0.00f, 0.12f, 0.12f, 0.14f, 0xDCE2E8 },
    { -0.15f,  0.16f, 0.00f, 0.12f, 0.12f, 0.14f, 0xDCE2E8 },
    {  0.15f,  0.16f, 0.00f, 0.12f, 0.12f, 0.14f, 0xDCE2E8 },
    {  0.00f, -0.02f, 0.12f, 0.52f, 0.62f, 0.38f, 0xF2F4F6 },
    {  0.00f,  0.34f, 0.26f, 0.36f, 0.28f, 0.30f, 0xF2F4F6 },
    {  0.00f,  0.52f, 0.28f, 0.18f, 0.10f, 0.12f, 0xDCE2E8 },
    {  0.00f,  0.575f,0.36f, 0.07f, 0.02f, 0.05f, CK },
    { -0.13f,  0.34f, 0.56f, 0.08f, 0.06f, 0.07f, 0xF2F4F6 },
    {  0.13f,  0.34f, 0.56f, 0.08f, 0.06f, 0.07f, 0xF2F4F6 },
    { -0.185f, 0.40f, 0.46f, 0.02f, 0.05f, 0.05f, CK },
    {  0.185f, 0.40f, 0.46f, 0.02f, 0.05f, 0.05f, CK }
};
static const PartDef P_JACK[] = {
    { -0.09f,  0.00f, 0.00f, 0.09f, 0.09f, 0.16f, 0x2A2238 },
    {  0.09f,  0.00f, 0.00f, 0.09f, 0.09f, 0.16f, 0x2A2238 },
    {  0.00f,  0.00f, 0.14f, 0.40f, 0.34f, 0.34f, 0x4A3A62 },
    { -0.23f,  0.00f, 0.22f, 0.06f, 0.10f, 0.22f, 0x4A3A62 },
    {  0.23f,  0.00f, 0.22f, 0.06f, 0.10f, 0.22f, 0x4A3A62 },
    {  0.00f,  0.00f, 0.48f, 0.48f, 0.42f, 0.36f, 0xF07A1E },
    {  0.00f,  0.00f, 0.84f, 0.08f, 0.08f, 0.10f, 0x4C8A30 },
    { -0.10f,  0.215f,0.66f, 0.09f, 0.01f, 0.08f, 0xFFD84A },
    {  0.10f,  0.215f,0.66f, 0.09f, 0.01f, 0.08f, 0xFFD84A },
    {  0.00f,  0.215f,0.54f, 0.26f, 0.01f, 0.05f, 0xFFD84A }
};
static const PartDef P_GHOST[] = {
    { -0.17f,  0.00f, 0.04f, 0.12f, 0.40f, 0.10f, 0xEDEBF7 },
    {  0.17f,  0.00f, 0.04f, 0.12f, 0.40f, 0.10f, 0xEDEBF7 },
    {  0.00f,  0.00f, 0.04f, 0.12f, 0.40f, 0.10f, 0xEDEBF7 },
    {  0.00f,  0.00f, 0.14f, 0.48f, 0.44f, 0.58f, 0xEDEBF7 },
    { -0.27f,  0.04f, 0.34f, 0.06f, 0.12f, 0.14f, 0xCFCBE6 },
    {  0.27f,  0.04f, 0.34f, 0.06f, 0.12f, 0.14f, 0xCFCBE6 },
    { -0.10f,  0.225f,0.50f, 0.08f, 0.01f, 0.12f, 0x2A2440 },
    {  0.10f,  0.225f,0.50f, 0.08f, 0.01f, 0.12f, 0x2A2440 },
    {  0.00f,  0.225f,0.34f, 0.10f, 0.01f, 0.08f, 0x2A2440 }
};

#define NPARTS(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const CharDef CHARS[] = {
    { "CHICKEN", W_CLASSIC, P_CHICKEN, NPARTS(P_CHICKEN), 0.0f },
    { "BUNNY",   W_CLASSIC, P_BUNNY,   NPARTS(P_BUNNY),   0.0f },
    { "PIGLET",  W_CLASSIC, P_PIG,     NPARTS(P_PIG),     0.0f },
    { "EMU",     W_OUTBACK, P_EMU,     NPARTS(P_EMU),     0.0f },
    { "ROO",     W_OUTBACK, P_ROO,     NPARTS(P_ROO),     0.0f },
    { "PENGUIN", W_ARCTIC,  P_PENGUIN, NPARTS(P_PENGUIN), 0.0f },
    { "POLAR",   W_ARCTIC,  P_POLAR,   NPARTS(P_POLAR),   0.0f },
    { "JACK",    W_SPOOKY,  P_JACK,    NPARTS(P_JACK),    0.0f },
    { "BOO",     W_SPOOKY,  P_GHOST,   NPARTS(P_GHOST),   0.10f }
};
#define CHAR_COUNT ((int)(sizeof(CHARS) / sizeof(CHARS[0])))

/* ------------------------------------------------------------------- rows -- */
typedef struct {
    float x, len;
    int   kind;
    uint32_t col;
} Mover;

typedef struct {
    int   index, live, type, band;

    unsigned char prop[COLS];         /* blockers on grass */
    float prop_h[COLS];
    unsigned char deco[COLS];         /* walk-over dressing */
    int   coin;                       /* column, or -99 */

    int   dir, lane_kind;
    float speed, span;
    Mover movers[MAX_MOVERS];
    int   mover_count;
    int   pads;                       /* water row of static lily pads */
    unsigned char pad[COLS];

    int   warn, train_on, train_cars, trains_run;
    float train_x, next_train, train_len;
    uint32_t train_col;
} Row;

typedef struct {
    float x, y, z, vx, vy, vz, s, life, max, floor_z;
    PA_Color col;
} Fx;

typedef struct {
    uint32_t run_seed;
    Row      rows[ROW_CAP];
    int      first, built, prev_type, streak, prev_water_dir;
    int      world, ch;

    float    px, py;
    float    draw_x, draw_y, hop_z, base_z;
    int      hopping;
    float    hop_t, hop_fx, hop_fy, hop_z0, hop_z1;
    int      hop_tx, hop_ty;
    int      facing;                  /* 0 +row, 1 +col, 2 -row, 3 -col */
    float    land_t, bump_t, press;

    int      on_log;
    float    log_offset;

    float    cam_y, cam_col;
    int      score, run_coins;
    float    idle;
    int      started, over, death, title;
    float    death_t, eagle_t, death_x, death_y, sink;
    float    shake;
    int      new_top;

    Fx       fx[MAX_FX];
    int      nfx;
    float    time, bell_t;

    int      select_open, select_idx;
    float    select_anim, select_scroll;
    int      reveal;                  /* character index being revealed + 1 */
    float    reveal_t;

    PA_Rng   bot_rng;
    float    bot_wait;
    int      bot_side;
    float    bot_t;
    int      bot_stage;
} Hopper;

static Hopper H;
static int    g_best, g_coins, g_unlocked = 1, g_char;
static int    g_loaded, g_launched;

/* -------------------------------------------------------------- view/layout */
typedef struct {
    float tile, rowlen, rise;
    float ax, ay, fx, fy;
    float cx, cy, cam_col, cam_row;
} View;
static View V;

static struct { int w, h; int ahead, behind, wide; float cy; } L;

/*
 * Measured from the plates: lanes run downhill to the right at about 16
 * degrees and a little over six tiles span the width of a portrait phone. The
 * whole world is rotated; boxes show a light top, a mid-tone south face and a
 * dark east end.
 */
#define LANE_ANGLE_DEG 16.0f

static void view_setup(View *v, float tile, float cx, float cy) {
    float th = LANE_ANGLE_DEG * PA_PI / 180.0f;
    v->tile = tile;
    v->rowlen = tile * 1.08f;
    v->rise = tile * 1.10f;
    v->ax = cosf(th) * tile;
    v->ay = sinf(th) * tile;
    v->fx = sinf(th) * v->rowlen;
    v->fy = -cosf(th) * v->rowlen;
    v->cx = cx; v->cy = cy;
    v->cam_col = 0.0f; v->cam_row = 0.0f;
}

static void compute_layout(int w, int h) {
    L.w = w; L.h = h;
    float tile = (float)w / 6.4f;
    if (w > h) tile = (float)h / 7.4f;
    L.cy = (float)h * (w > h ? 0.60f : 0.70f);
    view_setup(&V, tile, (float)w * 0.5f, L.cy);
    L.wide = (int)(((float)w * 0.5f) / V.ax) + 6;
    float lift = (float)L.wide * V.ay;
    L.ahead  = (int)ceilf((V.cy + lift + 3.0f * V.rise) / -V.fy) + 2;
    L.behind = (int)ceilf(((float)h - V.cy + lift + V.rise) / -V.fy) + 2;
    if (L.ahead > ROW_CAP - 14) L.ahead = ROW_CAP - 14;
    if (L.behind > 20) L.behind = 20;
}

static PA_Vec2 project(float col, float row, float z) {
    float dc = col - V.cam_col;
    float dr = row - V.cam_row;
    PA_Vec2 p;
    p.x = V.cx + dc * V.ax + dr * V.fx;
    p.y = V.cy + dc * V.ay + dr * V.fy - z * V.rise;
    return p;
}

/* Nearness to the camera of a ground point, in rows. Bigger draws later. */
static float depth_key(float col, float row) {
    return col * (V.ay / -V.fy) - row;
}

static float ui_scale(void) {
    float m = (float)(L.w < L.h ? L.w : L.h);
    return pa_clampf(m / 540.0f, 0.7f, 1.6f);
}

/* ------------------------------------------------------------- generation -- */
static uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static uint32_t row_seed(int index) {
    return hash32((uint32_t)index * 2654435761u + 0x9E3779B1u);
}

static Row *row_slot(int index) { return &H.rows[((index % ROW_CAP) + ROW_CAP) % ROW_CAP]; }

static Row *row_at(int index) {
    Row *r = row_slot(index);
    return (r->live && r->index == index) ? r : NULL;
}

static int max_streak(int type, PA_Rng *r, float d) {
    if (type == ROW_GRASS) return pa_rng_int(r, 1, 3);
    if (type == ROW_ROAD)  return pa_rng_int(r, 1, 3 + (int)(d * 2.0f));
    if (type == ROW_WATER) return pa_rng_int(r, 1, 3);
    return pa_rng_int(r, 1, 2);
}

static int roll_prop(PA_Rng *r) {
    float k = pa_rng_next(r);
    return k < 0.42f ? P_TREE : (k < 0.68f ? P_BUSH : (k < 0.86f ? P_ROCK : P_STUMP));
}

static void dress_grass(Row *row, PA_Rng *r, float deco_chance) {
    for (int c = 0; c < COLS; c++) {
        if (row->prop[c]) continue;
        if (!pa_rng_chance(r, deco_chance)) continue;
        float k = pa_rng_next(r);
        row->deco[c] = k < 0.55f ? D_TUFT : (k < 0.88f ? D_FLOWER : D_PEBBLE);
    }
}

static void build_row(int index) {
    Row *row = row_slot(index);
    PA_Rng r;
    pa_rng_seed(&r, row_seed(index) ^ H.run_seed);

    Row *prev = row_at(index - 1);
    memset(row, 0, sizeof(*row));
    row->index = index;
    row->live = 1;
    row->coin = -99;
    row->band = ((index % 2) + 2) % 2;

    if (index <= SAFE_ROWS) {
        row->type = ROW_GRASS;
        if (index < 0) {
            /* Behind the start line: a hedge of trees, then dense scenery to
               fill the bottom of the screen. */
            for (int c = 0; c < COLS; c++) {
                if (index == -1 || pa_rng_chance(&r, 0.6f)) {
                    row->prop[c] = (unsigned char)roll_prop(&r);
                    if (index == -1 && row->prop[c] == P_STUMP) row->prop[c] = P_BUSH;
                    /* Keep tall trees out from right in front of the start tile,
                       or the opening shot hides the hero behind a wall of leaves. */
                    if (index >= -2 && abs(c - HALF) <= 1 && row->prop[c] == P_TREE) row->prop[c] = P_BUSH;
                    row->prop_h[c] = pa_rng_next(&r);
                }
            }
        } else {
            int n = index == 0 ? 2 : pa_rng_int(&r, 1, 2);
            for (int i = 0; i < n; i++) {
                int c = pa_rng_int(&r, 0, COLS - 1);
                if (abs(c - HALF) < 2) continue;
                row->prop[c] = (unsigned char)roll_prop(&r);
                row->prop_h[c] = pa_rng_next(&r);
            }
        }
        dress_grass(row, &r, 0.30f);
        H.streak = (row->type == H.prev_type) ? H.streak + 1 : 1;
        H.prev_type = row->type;
        return;
    }

    float d = pa_clamp01((float)index / 200.0f);

    int type = H.prev_type;
    if (H.streak >= max_streak(H.prev_type, &r, d)) {
        float roll = pa_rng_next(&r);
        /* Each world leans on its own hazard: the arctic is mostly open water,
           the outback is crossed by long-distance rail. */
        static const float road_p[W_COUNT]  = { 0.55f, 0.50f, 0.40f, 0.56f };
        static const float water_p[W_COUNT] = { 0.27f, 0.22f, 0.46f, 0.30f };
        float rp = road_p[H.world], wp = water_p[H.world];
        if (H.prev_type == ROW_GRASS)
            type = roll < rp ? ROW_ROAD : (roll < rp + wp ? ROW_WATER : ROW_RAIL);
        else if (H.prev_type == ROW_ROAD)
            type = roll < 0.55f ? ROW_GRASS : (roll < 0.55f + wp * 0.9f ? ROW_WATER : ROW_RAIL);
        else if (H.prev_type == ROW_WATER)
            type = roll < 0.62f ? ROW_GRASS : ROW_ROAD;
        else
            type = roll < 0.55f ? ROW_GRASS : ROW_ROAD;
    }
    row->type = type;

    if (type == ROW_GRASS) {
        int count = pa_rng_int(&r, 1, 2 + (int)(d * 2.5f));
        if (count > 4) count = 4;
        for (int i = 0, placed = 0; i < count * 3 && placed < count; i++) {
            int c = pa_rng_int(&r, 0, COLS - 1);
            if (row->prop[c]) continue;
            row->prop[c] = (unsigned char)roll_prop(&r);
            row->prop_h[c] = pa_rng_next(&r);
            placed++;
        }
        /* Never seal a pocket: every open stretch of the grass row below must
           have at least one way forward. */
        if (prev && prev->type == ROW_GRASS) {
            int c = 0;
            while (c < COLS) {
                if (prev->prop[c]) { c++; continue; }
                int s = c, open = 0;
                while (c < COLS && !prev->prop[c]) { if (!row->prop[c]) open = 1; c++; }
                if (!open) row->prop[pa_rng_int(&r, s, c - 1)] = P_NONE;
            }
        }
        dress_grass(row, &r, 0.32f);
        if (pa_rng_chance(&r, 0.30f)) {
            for (int t = 0; t < 8; t++) {
                int cc = pa_rng_int(&r, 1, COLS - 2);
                if (!row->prop[cc]) { row->coin = cc - HALF; break; }
            }
        }
    } else if (type == ROW_ROAD) {
        row->dir = pa_rng_chance(&r, 0.5f) ? 1 : -1;
        float kroll = pa_rng_next(&r);
        row->lane_kind = kroll < 0.58f ? 0 : (kroll < 0.84f ? 1 : 2);
        float spd = pa_lerpf(1.9f, 5.4f, d) * pa_rng_range(&r, 0.82f, 1.20f);
        if (row->lane_kind == 1) spd *= 0.80f;
        if (row->lane_kind == 2) spd *= 0.88f;
        row->speed = spd;
        row->span = HALF * 2 + 10;
        float gap = pa_lerpf(5.6f, 3.0f, d) * pa_rng_range(&r, 0.9f, 1.25f);
        float x = pa_rng_range(&r, -row->span * 0.5f, -row->span * 0.5f + 3.0f);
        while (x < row->span * 0.5f && row->mover_count < MAX_MOVERS) {
            Mover *m = &row->movers[row->mover_count++];
            if (row->lane_kind == 0) {
                float k = pa_rng_next(&r);
                m->kind = k < 0.55f ? V_SEDAN : (k < 0.78f ? V_TAXI : V_VAN);
            } else {
                m->kind = row->lane_kind == 1 ? V_TRUCK : V_BUS;
            }
            m->len = m->kind == V_TRUCK ? 2.7f : m->kind == V_BUS ? 2.5f : m->kind == V_VAN ? 1.45f : 1.30f;
            m->col = H.world >= 0 ? WORLDS[H.world].car[pa_rng_int(&r, 0, 5)] : 0xE8414E;
            if (m->kind == V_TAXI) m->col = H.world == W_SPOOKY ? 0xD9B25A : 0xF7C531;
            m->x = x + m->len * 0.5f;
            x += m->len + gap * pa_rng_range(&r, 0.8f, 1.2f);
        }
        if (pa_rng_chance(&r, 0.16f)) row->coin = pa_rng_int(&r, -HALF + 1, HALF - 1);
    } else if (type == ROW_WATER) {
        row->dir = (prev && prev->type == ROW_WATER) ? -prev->dir : (pa_rng_chance(&r, 0.5f) ? 1 : -1);
        row->pads = !(prev && prev->type == ROW_WATER && prev->pads) && pa_rng_chance(&r, 0.26f);
        if (row->pads) {
            int n = pa_rng_int(&r, 3, 5);
            for (int i = 0; i < n * 3 && n > 0; i++) {
                int c = pa_rng_int(&r, 0, COLS - 1);
                if (row->pad[c]) continue;
                row->pad[c] = 1;
                n--;
            }
            /* A pad row behind a grass row must line up with an open tile. */
            if (prev && prev->type == ROW_GRASS) {
                int ok = 0;
                for (int c = 0; c < COLS; c++) if (row->pad[c] && !prev->prop[c]) ok = 1;
                if (!ok) for (int c = 0; c < COLS; c++) if (!prev->prop[c]) { row->pad[c] = 1; break; }
            }
            if (pa_rng_chance(&r, 0.35f)) {
                for (int c = 0; c < COLS; c++) if (row->pad[c] && pa_rng_chance(&r, 0.5f)) { row->coin = c - HALF; break; }
            }
        } else {
            row->speed = pa_lerpf(1.3f, 2.8f, d) * pa_rng_range(&r, 0.85f, 1.15f);
            row->span = HALF * 2 + 10;
            float gap = pa_lerpf(1.6f, 2.8f, d) * pa_rng_range(&r, 0.9f, 1.2f);
            float x = pa_rng_range(&r, -row->span * 0.5f, -row->span * 0.5f + 2.0f);
            while (x < row->span * 0.5f && row->mover_count < MAX_MOVERS) {
                Mover *m = &row->movers[row->mover_count++];
                m->len = (float)pa_rng_int(&r, 2, 4) - (H.world == W_ARCTIC ? 0.4f : 0.0f);
                m->x = x + m->len * 0.5f;
                m->kind = pa_rng_int(&r, 0, 1000);
                x += m->len + gap * pa_rng_range(&r, 0.8f, 1.2f);
            }
        }
    } else {
        row->dir = pa_rng_chance(&r, 0.5f) ? 1 : -1;
        row->speed = pa_lerpf(24.0f, 32.0f, d);
        row->span = HALF * 2 + 10;
        row->next_train = pa_rng_range(&r, 1.8f, 4.6f);
        row->train_cars = pa_rng_int(&r, 3, 5);
        row->train_len = (float)row->train_cars * 3.0f;
    }

    H.streak = (row->type == H.prev_type) ? H.streak + 1 : 1;
    H.prev_type = row->type;
}

static void ensure_rows(float ahead) {
    int target = (int)ceilf(ahead) + L.ahead + 4;
    int limit = H.first + ROW_CAP - 1;
    if (target > limit) target = limit;
    while (H.built < target) {
        H.built++;
        build_row(H.built);
    }
    int cut = (int)floorf(H.cam_y) - (L.behind + 4) - 2;
    while (H.first < cut) {
        Row *r = row_slot(H.first);
        if (r->index == H.first) r->live = 0;
        H.first++;
    }
}

static float ground_z(const Row *r) {
    if (!r) return Z_GRASS;
    if (r->type == ROW_GRASS) return Z_GRASS;
    if (r->type == ROW_WATER) return Z_WATER;
    return Z_ROAD;
}

/* ------------------------------------------------------------- particles -- */
static void fx_add(float x, float y, float z, float vx, float vy, float vz,
                   float s, float life, PA_Color col, float floor_z) {
    if (H.nfx >= MAX_FX) return;
    Fx *f = &H.fx[H.nfx++];
    f->x = x; f->y = y; f->z = z; f->vx = vx; f->vy = vy; f->vz = vz;
    f->s = s; f->life = life; f->max = life; f->col = col; f->floor_z = floor_z;
}

static void fx_update(float dt) {
    for (int i = 0; i < H.nfx; i++) {
        Fx *f = &H.fx[i];
        f->life -= dt;
        if (f->life <= 0.0f) { H.fx[i] = H.fx[--H.nfx]; i--; continue; }
        f->vz -= 12.0f * dt;
        f->x += f->vx * dt; f->y += f->vy * dt; f->z += f->vz * dt;
        if (f->z < f->floor_z) {
            f->z = f->floor_z;
            f->vz *= -0.25f;
            f->vx *= 0.5f; f->vy *= 0.5f;
        }
    }
}

static float frand(PA_Rng *r, float a, float b) { return pa_rng_range(r, a, b); }

static void burst(float x, float y, float z, int n, float spread, float up, float s,
                  PA_Color a, PA_Color b, float floor_z, float life) {
    PA_Rng r;
    pa_rng_seed(&r, (uint32_t)(H.time * 1000.0f) * 2654435761u + (uint32_t)n);
    for (int i = 0; i < n; i++) {
        float ang = frand(&r, 0.0f, PA_TAU);
        float sp = frand(&r, 0.3f, 1.0f) * spread;
        fx_add(x, y, z, cosf(ang) * sp, sinf(ang) * sp, up * frand(&r, 0.6f, 1.2f),
               s * frand(&r, 0.7f, 1.2f), life * frand(&r, 0.75f, 1.2f),
               (i & 1) ? a : b, floor_z);
    }
}

/* ------------------------------------------------------------------ input -- */
static int walkable(int col, int row_index) {
    if (col < -HALF || col > HALF) return 0;
    Row *r = row_at(row_index);
    if (r && r->type == ROW_GRASS && r->prop[col + HALF]) return 0;
    return 1;
}

static Mover *find_log(Row *row, float x) {
    for (int i = 0; i < row->mover_count; i++) {
        Mover *m = &row->movers[i];
        if (x > m->x - m->len * 0.5f - 0.28f && x < m->x + m->len * 0.5f + 0.28f) return m;
    }
    return NULL;
}

static float stand_z(const Row *r) {
    if (!r) return Z_GRASS;
    if (r->type == ROW_WATER) return r->pads ? Z_WATER + 0.04f : Z_WATER + 0.20f;
    if (r->type == ROW_RAIL) return 0.02f;
    return ground_z(r);
}

static void try_hop(int dx, int dy) {
    if (H.over || H.hopping) return;

    int face = dy > 0 ? 0 : dx > 0 ? 1 : dy < 0 ? 2 : 3;
    H.facing = face;

    float from_x = (H.on_log >= 0) ? H.draw_x : H.px;
    int tx = (int)floorf(from_x + 0.5f) + dx;
    int ty = (int)H.py + dy;
    if (ty < H.first) return;

    ensure_rows(H.cam_y);
    if (!walkable(tx, ty)) {
        if (tx >= -HALF && tx <= HALF) { H.bump_t = 0.12f; pa_tone(200, 120, 0.06f, 0, 0.07f); }
        return;
    }

    H.hop_fx = from_x;
    H.hop_fy = H.py;
    H.hop_tx = tx;
    H.hop_ty = ty;
    H.hop_z0 = H.base_z;
    H.hop_z1 = stand_z(row_at(ty));
    H.hopping = 1;
    H.hop_t = 0.0f;
    H.on_log = -1;
    if (!H.started) { H.started = 1; H.title = 0; }
    pa_tone(520.0f + (float)(H.score % 5) * 30.0f, 860.0f, 0.05f, 2, 0.05f);
}

/* ------------------------------------------------------------------- life -- */
static void save_progress(void) {
    if (pa_demo_mode()) return;
    pa_save_set("roadhopper.best", g_best);
    pa_save_set("roadhopper.coins", g_coins);
    pa_save_set("roadhopper.unlocked", g_unlocked);
    pa_save_set("roadhopper.char", g_char);
    pa_save_flush();
}

static void die(int kind) {
    if (H.over) return;
    H.over = 1;
    H.death = kind;
    H.death_t = 0.0f;
    H.death_x = H.draw_x;
    H.death_y = H.draw_y;
    H.hopping = 0;
    float gz = H.base_z;
    const CharDef *cd = &CHARS[H.ch];
    PA_Color body = pa_hex(cd->parts[cd->n > 4 ? 4 : 0].col);
    if (kind == DEATH_CAR) {
        H.shake = 0.15f;
        burst(H.draw_x, H.draw_y, gz + 0.35f, 16, 2.6f, 3.5f, 0.10f, PA_RGB(255, 255, 255), body, gz, 0.9f);
        pa_noise(0.12f, 0.20f);
        pa_tone(320, 70, 0.28f, 3, 0.12f);
    } else if (kind == DEATH_TRAIN) {
        H.shake = 0.22f;
        Row *r = row_at((int)H.py);
        float push = r ? (float)r->dir * 6.0f : 4.0f;
        PA_Rng rr;
        pa_rng_seed(&rr, 77u + (uint32_t)H.score);
        for (int i = 0; i < 20; i++)
            fx_add(H.draw_x, H.draw_y, gz + 0.4f, push * frand(&rr, 0.6f, 1.4f), frand(&rr, -1.5f, 1.5f),
                   frand(&rr, 2.0f, 5.0f), 0.11f, frand(&rr, 0.6f, 1.1f),
                   (i & 1) ? PA_RGB(255, 255, 255) : body, gz);
        pa_noise(0.30f, 0.24f);
        pa_tone(160, 48, 0.30f, 3, 0.14f);
    } else if (kind == DEATH_WATER) {
        PA_Rng rr;
        pa_rng_seed(&rr, 91u + (uint32_t)H.score);
        for (int i = 0; i < 26; i++) {
            float a = frand(&rr, 0.0f, PA_TAU), sp = frand(&rr, 0.3f, 1.4f);
            fx_add(H.draw_x + cosf(a) * 0.15f, H.draw_y + sinf(a) * 0.15f, Z_WATER, cosf(a) * sp, sinf(a) * sp,
                   frand(&rr, 3.5f, 7.5f), frand(&rr, 0.10f, 0.20f), frand(&rr, 0.8f, 1.2f),
                   (i % 3) ? PA_RGB(255, 255, 255) : pa_hex(WORLDS[H.world].water_hi), Z_WATER);
        }
        pa_noise(0.35f, 0.16f);
        pa_tone(700, 180, 0.30f, 0, 0.10f);
    } else {
        H.eagle_t = 0.0f;
        pa_tone(1900, 900, 0.45f, 3, 0.07f);
        pa_tone(1500, 700, 0.40f, 2, 0.05f);
    }
    if (H.score > g_best) {
        g_best = H.score;
        H.new_top = 1;
    }
    save_progress();
}

static void landed(void) {
    Row *r = row_at((int)H.py);
    H.land_t = 0.10f;
    if (!r) return;
    if (r->type == ROW_WATER) {
        if (r->pads) {
            if (!r->pad[(int)H.px + HALF]) { die(DEATH_WATER); return; }
            H.base_z = stand_z(r);
            burst(H.draw_x, H.draw_y, Z_WATER + 0.02f, 4, 0.8f, 0.6f, 0.07f,
                  pa_hex(WORLDS[H.world].water_hi), PA_RGB(255, 255, 255), Z_WATER, 0.30f);
            pa_tone(300, 200, 0.06f, 0, 0.07f);
            return;
        }
        Mover *m = find_log(r, H.draw_x);
        if (!m) { die(DEATH_WATER); return; }
        H.on_log = (int)(m - r->movers);
        H.log_offset = pa_clampf(H.draw_x - m->x, -m->len * 0.5f + 0.25f, m->len * 0.5f - 0.25f);
        H.draw_x = m->x + H.log_offset;
        H.base_z = stand_z(r);
        pa_tone(240, 160, 0.06f, 0, 0.08f);
        return;
    }
    H.base_z = stand_z(r);
    PA_Color dust = r->type == ROW_GRASS ? pa_shade(pa_hex(WORLDS[H.world].grass_a), 0.35f)
                                         : pa_shade(pa_hex(WORLDS[H.world].road), 0.45f);
    for (int k = 0; k < 4; k++) {
        float a = (float)k * PA_TAU / 4.0f + 0.4f;
        fx_add(H.draw_x + cosf(a) * 0.2f, H.draw_y + sinf(a) * 0.2f, H.base_z + 0.02f,
               cosf(a) * 1.1f, sinf(a) * 1.1f, 0.9f, 0.08f, 0.28f, dust, H.base_z);
    }
}

static void check_hazards(void) {
    Row *test = H.hopping ? row_at(H.hop_ty) : row_at((int)H.py);
    if (!test) return;
    float x = H.draw_x;

    if (test->type == ROW_ROAD) {
        for (int i = 0; i < test->mover_count; i++) {
            Mover *m = &test->movers[i];
            if (fabsf(m->x - x) < m->len * 0.5f + 0.30f) {
                if (!H.hopping || H.hop_t > 0.55f) { die(DEATH_CAR); return; }
            }
        }
    } else if (test->type == ROW_RAIL && test->train_on) {
        if (fabsf(test->train_x - x) < test->train_len * 0.5f + 0.3f) {
            if (!H.hopping || H.hop_t > 0.40f) { die(DEATH_TRAIN); return; }
        }
    }
}

/* ------------------------------------------------------------- the world -- */
static float mover_wrap(const Row *r, float x, float len) {
    float lim = r->span * 0.5f + 4.0f, cyc = r->span + 8.0f;
    if (r->dir > 0) { while (x - len * 0.5f > lim) x -= cyc; }
    else            { while (x + len * 0.5f < -lim) x += cyc; }
    return x;
}

static void world_tick(float dt) {
    for (int idx = H.first; idx <= H.built; idx++) {
        Row *r = row_at(idx);
        if (!r) continue;
        if (r->type == ROW_ROAD || (r->type == ROW_WATER && !r->pads)) {
            for (int i = 0; i < r->mover_count; i++) {
                Mover *m = &r->movers[i];
                m->x = mover_wrap(r, m->x + (float)r->dir * r->speed * dt, m->len);
            }
        } else if (r->type == ROW_RAIL) {
            if (!r->train_on) {
                r->next_train -= dt;
                r->warn = r->next_train < 1.3f;
                if (r->next_train <= 0.0f) {
                    r->train_on = 1;
                    float start = r->span * 0.5f + r->train_len * 0.5f + 1.0f;
                    r->train_x = r->dir > 0 ? -start : start;
                    if (fabsf((float)idx - H.py) < 7.0f) {
                        pa_tone(330, 320, 0.45f, 3, 0.07f);
                        pa_tone(415, 405, 0.45f, 3, 0.05f);
                    }
                }
            } else {
                r->train_x += (float)r->dir * r->speed * dt;
                if (fabsf(r->train_x) > r->span * 0.5f + r->train_len * 0.5f + 5.0f) {
                    r->train_on = 0;
                    r->warn = 0;
                    r->trains_run++;
                    uint32_t hsh = hash32(row_seed(idx) ^ (uint32_t)r->trains_run * 7919u ^ H.run_seed);
                    r->next_train = 2.6f + (float)(hsh % 1000u) / 1000.0f * 3.6f;
                }
            }
        }
    }
}

/* --------------------------------------------------------------- the bot -- */
/*
 * Review captures only (pa_demo_mode). The bot predicts every car, log and
 * train a short window ahead and hops the moment a move is safe, which gives
 * a captured run the rhythm of a confident player - near misses included -
 * and then, past a target score, deliberately meets the death that capture is
 * meant to show.
 */
static float mover_x_at(const Row *r, const Mover *m, float t) {
    return mover_wrap(r, m->x + (float)r->dir * r->speed * t, m->len);
}

static int road_hit(const Row *r, float x, float t0, float t1, float margin) {
    for (float t = t0; t <= t1 + 1e-4f; t += 0.02f)
        for (int i = 0; i < r->mover_count; i++) {
            const Mover *m = &r->movers[i];
            if (fabsf(mover_x_at(r, m, t) - x) < m->len * 0.5f + margin) return 1;
        }
    return 0;
}

static int rail_hit(const Row *r, float x, float t0, float t1) {
    if (r->train_on) {
        for (float t = t0; t <= t1 + 0.4f; t += 0.02f) {
            float tx = r->train_x + (float)r->dir * r->speed * t;
            if (fabsf(tx - x) < r->train_len * 0.5f + 1.2f) return 1;
        }
        return 0;
    }
    return r->next_train < t1 + 0.5f;
}

static int water_ok(const Row *r, int tx, float arrive, float stay) {
    if (r->pads) return r->pad[tx + HALF] != 0;
    for (int i = 0; i < r->mover_count; i++) {
        const Mover *m = &r->movers[i];
        float mx = mover_x_at(r, m, arrive);
        if (fabsf((float)tx - mx) < m->len * 0.5f - 0.15f) {
            float end = (float)tx + (float)r->dir * r->speed * stay;
            return fabsf(end) < EDGE - 0.4f;
        }
    }
    return 0;
}

static int cell_safe(int tx, int ty, float arrive, float stay) {
    if (!walkable(tx, ty)) return 0;
    Row *r = row_at(ty);
    if (!r) return 0;
    if (r->type == ROW_ROAD) return !road_hit(r, (float)tx, arrive * 0.5f, arrive + stay, 0.46f);
    if (r->type == ROW_RAIL) return !rail_hit(r, (float)tx, 0.0f, arrive + stay);
    if (r->type == ROW_WATER) return water_ok(r, tx, arrive, stay);
    return 1;
}

static int here_safe(float stay) {
    Row *r = row_at((int)H.py);
    if (!r) return 1;
    if (r->type == ROW_ROAD) return !road_hit(r, H.draw_x, 0.0f, stay, 0.40f);
    if (r->type == ROW_RAIL) return !rail_hit(r, H.draw_x, 0.0f, stay);
    if (r->type == ROW_WATER && !r->pads) {
        float end = H.draw_x + (float)r->dir * r->speed * stay;
        return fabsf(end) < EDGE - 0.2f;
    }
    return 1;
}

/* Walkable and, on a lily pad row, actually a pad. */
static int passable(int col, int row_index) {
    if (!walkable(col, row_index)) return 0;
    Row *r = row_at(row_index);
    return !(r && r->type == ROW_WATER && r->pads && !r->pad[col + HALF]);
}

static void bot_think(float dt) {
    int demo = pa_demo_mode();
    H.bot_t += dt;
    if (H.over || H.hopping || H.select_open || H.reveal) return;
    if (demo == 5) return;                       /* roster capture: no play */
    if (!H.started && H.bot_t < 1.2f) return;     /* let the title breathe */
    H.bot_wait -= dt;
    if (H.bot_wait > 0.0f) return;

    float fx = (H.on_log >= 0) ? H.draw_x : H.px;
    int cx = (int)floorf(fx + 0.5f);
    int cy = (int)H.py;
    float stay = 0.42f;
    Row *ahead = row_at(cy + 1);
    float pause = pa_rng_range(&H.bot_rng, 0.04f, 0.16f);

    /* Scripted endings, one per capture. */
    int target = demo == 1 ? 17 : demo == 2 ? 13 : demo == 3 ? 11 : demo == 4 ? 9 : 1 << 30;
    if (H.score >= target && ahead) {
        if (demo == 1 && ahead->type == ROW_ROAD && here_safe(0.2f) &&
            road_hit(ahead, (float)cx, HOP_TIME * 0.6f, HOP_TIME + 0.10f, 0.25f)) { try_hop(0, 1); return; }
        if (demo == 2) {
            Row *here = row_at(cy);
            if (here && here->type == ROW_RAIL && (here->warn || here->train_on)) return;   /* frozen */
            if (ahead->type == ROW_RAIL && ahead->warn && !ahead->train_on && ahead->next_train < 0.6f) {
                try_hop(0, 1); return;
            }
        }
        if (demo == 3 && ahead->type == ROW_WATER && !water_ok(ahead, cx, HOP_TIME, 0.1f) && here_safe(0.2f)) {
            try_hop(0, 1); return;
        }
        if (demo == 4) {
            Row *here = row_at(cy);
            if (here && here->type == ROW_GRASS) return;    /* idle until the eagle comes */
        }
    }

    int fwd = cell_safe(cx, cy + 1, HOP_TIME, stay);
    /* Take a coin one tile to the side when it is safe to. */
    Row *here = row_at(cy);
    if (here && here->coin != -99 && abs(here->coin - cx) == 1 && here->type == ROW_GRASS &&
        cell_safe(here->coin, cy, HOP_TIME, stay)) {
        try_hop(here->coin - cx, 0); H.bot_wait = pause; return;
    }
    if (fwd) { try_hop(0, 1); H.bot_wait = pause; return; }

    int safe_now = here_safe(stay * 0.8f);
    int blocked = !passable(cx, cy + 1);
    if (!safe_now || blocked) {
        /* Find the nearest column with an open way forward on this row. */
        int best = 0, best_d = 99;
        for (int s = -1; s <= 1; s += 2) {
            for (int k = 1; k <= COLS; k++) {
                int c = cx + s * k;
                if (!walkable(c, cy)) break;
                if (passable(c, cy + 1) && k < best_d) { best_d = k; best = s; }
            }
        }
        int order[2] = { best ? best : (cx > 0 ? -1 : 1), best ? -best : (cx > 0 ? 1 : -1) };
        for (int i = 0; i < 2; i++) {
            if (cell_safe(cx + order[i], cy, HOP_TIME, stay)) { try_hop(order[i], 0); H.bot_wait = pause; return; }
        }
        if (!safe_now && cell_safe(cx, cy - 1, HOP_TIME, stay)) { try_hop(0, -1); H.bot_wait = pause; return; }
    }
    /* Drift back toward the middle while waiting on safe ground. */
    if (here && here->type == ROW_GRASS && abs(cx) >= 3 &&
        cell_safe(cx > 0 ? cx - 1 : cx + 1, cy, HOP_TIME, stay) && pa_rng_chance(&H.bot_rng, 0.3f)) {
        try_hop(cx > 0 ? -1 : 1, 0); H.bot_wait = pause; return;
    }
    H.bot_wait = 0.03f;
}

/* ------------------------------------------------------------------ frame -- */
static void load_progress(void) {
    if (g_loaded) return;
    g_loaded = 1;
    if (pa_demo_mode()) {
        /* Fixed fixture so captures are repeatable and show progression. */
        g_best = 15; g_coins = 96; g_unlocked = 0x1FF & ~((1 << 2) | (1 << 6)); g_char = 0;
        int d = pa_demo_mode();
        g_char = d == 2 ? 3 : d == 3 ? 5 : d == 4 ? 7 : 0;
        return;
    }
    g_best = pa_save_get("roadhopper.best", 0);
    g_coins = pa_save_get("roadhopper.coins", 0);
    g_unlocked = pa_save_get("roadhopper.unlocked", 1) | 1;
    g_char = pa_save_get("roadhopper.char", 0);
    if (g_char < 0 || g_char >= CHAR_COUNT || !(g_unlocked & (1 << g_char))) g_char = 0;
}

static void new_run(void) {
    int keep_title = !g_launched;
    g_launched = 1;
    memset(&H, 0, sizeof(H));
    H.ch = g_char;
    H.world = CHARS[g_char].world;
    H.title = keep_title;
    uint32_t salt = pa_demo_mode() ? (uint32_t)pa_demo_mode() * 0x51ED27u
                                   : (uint32_t)(g_best * 2654435761u) ^ (uint32_t)(g_coins * 40503u);
    H.run_seed = 0x5EED1234u ^ salt ^ (uint32_t)H.world * 0x1000193u;
    pa_rng_seed(&H.bot_rng, 1234u + (uint32_t)pa_demo_mode());

    H.cam_y = -0.6f;
    H.on_log = -1;
    H.prev_type = ROW_GRASS;
    H.base_z = Z_GRASS;
    H.facing = 0;

    if (L.w == 0) compute_layout(540, 1170);
    H.first = -(L.behind + 4);
    H.built = H.first - 1;
    ensure_rows(0.0f);
    if (pa_demo_mode() == 5) { H.select_open = 1; H.select_idx = 0; H.select_scroll = 0.0f; }
}

static void hopper_start(void) {
    load_progress();
    g_launched = 0;
    new_run();
}

static void hopper_stop(void) { }

/* Shared UI geometry, so taps and drawing always agree. */
typedef struct { float x, y, w, h; } Box;
static int in_box(Box b, float x, float y) { return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h; }

typedef struct { Box card, play, chr, prize; } ResultsUI;
static ResultsUI results_ui(void) {
    ResultsUI u;
    float W = (float)L.w, Hh = (float)L.h, s = ui_scale();
    float cw = pa_clampf(W * 0.82f, 0.0f, 470.0f * s);
    u.card.w = cw; u.card.h = 316.0f * s;
    u.card.x = (W - cw) * 0.5f;
    u.card.y = Hh * 0.24f;
    if (L.w > L.h) u.card.y = Hh * 0.12f;
    float pw = 190.0f * s, ph = 118.0f * s, sw = 104.0f * s;
    float gap = 22.0f * s;
    float row_y = u.card.y + u.card.h + 48.0f * s;
    u.play.w = pw; u.play.h = ph; u.play.x = (W - pw) * 0.5f; u.play.y = row_y;
    u.chr.w = sw; u.chr.h = sw; u.chr.x = u.play.x - gap - sw; u.chr.y = row_y + (ph - sw) * 0.5f;
    u.prize.w = sw; u.prize.h = sw; u.prize.x = u.play.x + pw + gap; u.prize.y = u.chr.y;
    return u;
}

typedef struct { Box chr, prize; } TitleUI;
static TitleUI title_ui(void) {
    TitleUI u;
    float s = ui_scale(), sw = 96.0f * s;
    u.chr.w = u.chr.h = sw;
    u.chr.x = 24.0f * s; u.chr.y = (float)L.h - sw - 36.0f * s;
    u.prize.w = u.prize.h = sw;
    u.prize.x = (float)L.w - sw - 24.0f * s; u.prize.y = u.chr.y;
    return u;
}

typedef struct { Box play, left, right; } SelectUI;
static SelectUI select_ui(void) {
    SelectUI u;
    float s = ui_scale(), W = (float)L.w, Hh = (float)L.h;
    u.play.w = 230.0f * s; u.play.h = 96.0f * s;
    u.play.x = (W - u.play.w) * 0.5f; u.play.y = Hh * 0.76f;
    if (L.w > L.h) u.play.y = Hh * 0.78f;
    u.left.w = u.right.w = 80.0f * s; u.left.h = u.right.h = 110.0f * s;
    u.left.x = 10.0f * s; u.right.x = W - 90.0f * s;
    u.left.y = u.right.y = Hh * 0.44f;
    return u;
}

static int locked_count(void) {
    int n = 0;
    for (int i = 0; i < CHAR_COUNT; i++) if (!(g_unlocked & (1 << i))) n++;
    return n;
}

static void use_prize(void) {
    if (g_coins < PRIZE_COST || locked_count() == 0) { pa_sfx("bad"); return; }
    g_coins -= PRIZE_COST;
    int pick = hash32((uint32_t)(H.time * 977.0f) + (uint32_t)g_coins * 31u) % (uint32_t)locked_count();
    for (int i = 0; i < CHAR_COUNT; i++) {
        if (g_unlocked & (1 << i)) continue;
        if (pick-- == 0) { g_unlocked |= 1 << i; g_char = i; H.reveal = i + 1; H.reveal_t = 0.0f; break; }
    }
    pa_sfx("win");
    save_progress();
}

static void open_select(void) {
    H.select_open = 1;
    H.select_idx = g_char;
    H.select_scroll = (float)g_char;
    H.select_anim = 0.0f;
    pa_sfx("select");
}

static void select_update(float dt, const PA_Input *in) {
    H.select_anim = pa_clamp01(H.select_anim + dt * 4.0f);
    H.select_scroll = pa_approach(H.select_scroll, (float)H.select_idx, 14.0f, dt);
    SelectUI u = select_ui();
    int step = 0;
    if (in->swipe == PA_SWIPE_LEFT || in->key_pressed[PA_KEY_RIGHT]) step = 1;
    if (in->swipe == PA_SWIPE_RIGHT || in->key_pressed[PA_KEY_LEFT]) step = -1;
    if (in->tapped && in_box(u.left, in->x, in->y)) step = -1;
    if (in->tapped && in_box(u.right, in->x, in->y)) step = 1;
    if (pa_demo_mode() == 5) {
        /* Capture: walk the roster so every character is seen. */
        float t = H.time;
        int want = (int)(t / 0.9f);
        if (want > CHAR_COUNT - 1) want = CHAR_COUNT - 1;
        if (want != H.select_idx) step = want - H.select_idx;
    }
    if (step) {
        H.select_idx = (H.select_idx + step + CHAR_COUNT) % CHAR_COUNT;
        pa_tone(600, 760, 0.05f, 1, 0.07f);
    }
    int confirm = (in->tapped && in_box(u.play, in->x, in->y)) || in->key_pressed[PA_KEY_ENTER] ||
                  in->key_pressed[PA_KEY_SPACE];
    if (confirm) {
        if (g_unlocked & (1 << H.select_idx)) {
            g_char = H.select_idx;
            save_progress();
            pa_sfx("good");
            int was_title = H.title;
            g_launched = !was_title;
            new_run();
            H.title = was_title;
        } else {
            pa_sfx("bad");
        }
    }
}

static void hopper_update(float dt, const PA_Input *in) {
    H.time += dt;
    fx_update(dt);
    if (H.shake > 0.0f) H.shake -= dt;
    if (H.bump_t > 0.0f) H.bump_t -= dt;
    if (H.land_t > 0.0f) H.land_t -= dt;
    if (pa_demo_mode()) bot_think(dt);

    if (H.reveal) {
        H.reveal_t += dt;
        if (H.reveal_t > 0.8f && (in->tapped || in->key_pressed[PA_KEY_ENTER] || in->key_pressed[PA_KEY_SPACE])) {
            H.reveal = 0;
            g_launched = 1;
            new_run();
        }
        return;
    }
    if (H.select_open) { select_update(dt, in); return; }

    world_tick(dt);

    if (H.over) {
        H.death_t += dt;
        if (H.death == DEATH_EAGLE) {
            H.eagle_t += dt;
            /* Frame the snatch: the camera settles back over the victim. */
            H.cam_y = pa_approach(H.cam_y, H.death_y - 0.8f, 4.0f, dt);
        }
        if (H.death == DEATH_WATER) H.sink = pa_clamp01(H.death_t * 2.5f);
        if (H.death_t > RESULTS_AT + 0.3f) {
            ResultsUI u = results_ui();
            if (in->tapped && in_box(u.chr, in->x, in->y)) { open_select(); return; }
            if (in->tapped && in_box(u.prize, in->x, in->y)) { use_prize(); return; }
            if ((in->tapped && in_box(u.play, in->x, in->y)) || in->key_pressed[PA_KEY_ENTER] ||
                in->key_pressed[PA_KEY_SPACE] || in->key_pressed[PA_KEY_UP]) {
                pa_sfx("select");
                new_run();
            }
        }
        return;
    }

    /* Title buttons sit over the field; a tap anywhere else is the first hop. */
    int consumed = 0;
    if (H.title && in->tapped) {
        TitleUI u = title_ui();
        if (in_box(u.chr, in->x, in->y)) { open_select(); consumed = 1; }
        else if (in_box(u.prize, in->x, in->y)) { use_prize(); consumed = 1; }
    }
    if (H.select_open || H.reveal) return;

    H.press = pa_approach(H.press, (in->down && !H.hopping) ? 1.0f : 0.0f, 30.0f, dt);
    if (!consumed) {
        if (in->swipe == PA_SWIPE_UP || in->swipe == PA_SWIPE_TAP || in->key_pressed[PA_KEY_UP] ||
            in->key_pressed[PA_KEY_SPACE])
            try_hop(0, 1);
        else if (in->swipe == PA_SWIPE_DOWN || in->key_pressed[PA_KEY_DOWN]) try_hop(0, -1);
        else if (in->swipe == PA_SWIPE_LEFT || in->key_pressed[PA_KEY_LEFT]) try_hop(-1, 0);
        else if (in->swipe == PA_SWIPE_RIGHT || in->key_pressed[PA_KEY_RIGHT]) try_hop(1, 0);
    }

    if (H.hopping) {
        H.hop_t += dt / HOP_TIME;
        float t = pa_clamp01(H.hop_t);
        H.draw_x = pa_lerpf(H.hop_fx, (float)H.hop_tx, t);
        H.draw_y = pa_lerpf(H.hop_fy, (float)H.hop_ty, t);
        H.base_z = pa_lerpf(H.hop_z0, H.hop_z1, t);
        H.hop_z = sinf(t * PA_PI) * 0.45f;
        if (t >= 1.0f) {
            if (H.hop_ty > (int)H.py) {
                if (H.hop_ty > H.score) H.score = H.hop_ty;
                H.idle = 0.0f;
            }
            H.px = (float)H.hop_tx;
            H.py = (float)H.hop_ty;
            H.draw_x = H.px;
            H.draw_y = H.py;
            H.hop_z = 0.0f;
            H.hopping = 0;
            landed();
            if (H.over) return;
        }
    } else {
        H.hop_z = 0.0f;
        H.draw_y = H.py;
    }

    ensure_rows(H.cam_y > H.py ? H.cam_y : H.py);

    Row *here = row_at((int)H.py);
    if (!H.hopping && here && here->type == ROW_WATER && !here->pads) {
        if (H.on_log < 0) {
            Mover *m = find_log(here, H.draw_x);
            if (!m) { die(DEATH_WATER); return; }
            H.on_log = (int)(m - here->movers);
            H.log_offset = H.draw_x - m->x;
        }
        H.draw_x = here->movers[H.on_log].x + H.log_offset;
        H.px = floorf(H.draw_x + 0.5f);
        if (H.draw_x < -EDGE - 0.4f || H.draw_x > EDGE + 0.4f) { die(DEATH_WATER); return; }
    } else if (!H.hopping) {
        H.on_log = -1;
        H.draw_x = H.px;
    }

    /* Warning bell while a train is coming on a nearby track. */
    H.bell_t -= dt;
    for (int k = -1; k <= 3 && H.bell_t <= 0.0f; k++) {
        Row *rr = row_at((int)H.py + k);
        if (rr && rr->type == ROW_RAIL && rr->warn && !rr->train_on) {
            pa_tone(1320, 1300, 0.07f, 1, 0.05f);
            H.bell_t = 0.26f;
        }
    }

    float creep = pa_lerpf(0.50f, 1.25f, pa_clamp01((float)H.score / 200.0f));
    if (H.started) H.cam_y += creep * dt;
    float want = H.draw_y - 0.4f;
    if (want > H.cam_y) H.cam_y = pa_approach(H.cam_y, want, 7.0f, dt);
    H.cam_col = pa_approach(H.cam_col, H.draw_x * 0.55f, 5.0f, dt);

    if (H.draw_y < H.cam_y - 2.4f) { die(DEATH_EAGLE); return; }
    if (H.started) H.idle += dt;
    if (H.idle > IDLE_LIMIT) { die(DEATH_EAGLE); return; }

    check_hazards();
    if (H.over) return;

    Row *coin_row = row_at((int)H.py);
    if (coin_row && coin_row->coin != -99 && !H.hopping) {
        if (fabsf((float)coin_row->coin - H.draw_x) < 0.5f) {
            coin_row->coin = -99;
            g_coins++;
            H.run_coins++;
            burst(H.draw_x, H.draw_y, H.base_z + 0.6f, 8, 1.6f, 3.0f, 0.07f,
                  pa_hex(0xFFD21F), PA_RGB(255, 255, 255), H.base_z, 0.5f);
            pa_sfx("coin");
            if (!pa_demo_mode()) { pa_save_set("roadhopper.coins", g_coins); }
        }
    }
}

/* ================================================================ drawing == */
static float signed_area(const PA_Vec2 *q, int n) {
    float a = 0.0f;
    for (int i = 0; i < n; i++) {
        const PA_Vec2 *p0 = &q[i], *p1 = &q[(i + 1) % n];
        a += p0->x * p1->y - p1->x * p0->y;
    }
    return a;
}



/*
 * Box of footprint w (along the lane) by d (forward) and height h at base z.
 * Three flat tones - light top, mid south face, dark east end - like the
 * reference. Faces are culled by screen winding, so the box is right at any
 * camera angle. `bevel` adds a one-voxel lighter rim along the top's near edge.
 */
static void box3x(PA_Canvas *c, float col, float row, float z, float w, float d, float h,
                  PA_Color colour, float bevel) {
    float hw = w * 0.5f, hd = d * 0.5f;
    PA_Vec2 bot[4] = {
        project(col - hw, row - hd, z), project(col + hw, row - hd, z),
        project(col + hw, row + hd, z), project(col - hw, row + hd, z)
    };
    PA_Vec2 top[4] = {
        project(col - hw, row - hd, z + h), project(col + hw, row - hd, z + h),
        project(col + hw, row + hd, z + h), project(col - hw, row + hd, z + h)
    };
    float minx = top[0].x, maxx = top[0].x, miny = top[0].y, maxy = bot[0].y;
    for (int i = 0; i < 4; i++) {
        if (top[i].x < minx) minx = top[i].x;
        if (top[i].x > maxx) maxx = top[i].x;
        if (bot[i].x < minx) minx = bot[i].x;
        if (bot[i].x > maxx) maxx = bot[i].x;
        if (top[i].y < miny) miny = top[i].y;
        if (bot[i].y > maxy) maxy = bot[i].y;
    }
    if (maxx < (float)c->clip_x0 - 2.0f || minx > (float)c->clip_x1 + 2.0f ||
        maxy < (float)c->clip_y0 - 2.0f || miny > (float)c->clip_y1 + 2.0f)
        return;

    float top_sign = signed_area(top, 4);
    static const float tone[4] = { -0.17f, -0.36f, -0.17f, -0.36f };
    if (h > 0.001f) {
        for (int f = 0; f < 4; f++) {
            int i = f, j = (f + 1) % 4;
            PA_Vec2 face[4] = { bot[i], bot[j], top[j], top[i] };
            float s = signed_area(face, 4);
            if ((s > 0.0f) != (top_sign > 0.0f) || fabsf(s) < 0.5f) continue;
            pa_fill_poly(c, face, 4, pa_shade(colour, tone[f]));
        }
    }
    pa_fill_poly(c, top, 4, colour);
    if (bevel > 0.0f) {
        float b = d * bevel;
        PA_Vec2 rim[4] = {
            top[0], top[1], project(col + hw, row - hd + b, z + h), project(col - hw, row - hd + b, z + h)
        };
        pa_fill_poly(c, rim, 4, pa_shade(colour, 0.16f));
        float bw = w * bevel;
        PA_Vec2 rim2[4] = {
            top[0], project(col - hw + bw, row - hd + b, z + h), project(col - hw + bw, row + hd, z + h), top[3]
        };
        pa_fill_poly(c, rim2, 4, pa_shade(colour, 0.10f));
    }
}

static void box3(PA_Canvas *c, float col, float row, float z, float w, float d, float h, PA_Color colour) {
    box3x(c, col, row, z, w, d, h, colour, 0.0f);
}

/* Flat decals on the faces the camera sees: windows, lamps, faces. */
static void face_s(PA_Canvas *c, float c0, float c1, float row, float z0, float z1, PA_Color col) {
    PA_Vec2 q[4] = { project(c0, row, z0), project(c1, row, z0), project(c1, row, z1), project(c0, row, z1) };
    pa_fill_poly(c, q, 4, col);
}
static void face_e(PA_Canvas *c, float col, float r0, float r1, float z0, float z1, PA_Color cl) {
    PA_Vec2 q[4] = { project(col, r0, z0), project(col, r1, z0), project(col, r1, z1), project(col, r0, z1) };
    pa_fill_poly(c, q, 4, cl);
}
static void quad_top(PA_Canvas *c, float c0, float c1, float r0, float r1, float z, PA_Color col) {
    PA_Vec2 q[4] = { project(c0, r0, z), project(c1, r0, z), project(c1, r1, z), project(c0, r1, z) };
    pa_fill_poly(c, q, 4, col);
}

/* ------------------------------------------------------------ hard shadows */
/*
 * Shadows go into a coverage mask first and darken the frame once, so where
 * two shadows overlap the ground is not darkened twice - the reference's
 * shadows are one flat tone wherever they fall.
 */
static uint8_t *g_mask;
static float   *g_acc;
static int      g_mask_w, g_mask_h, g_mask_on;
static int      g_mx0, g_my0, g_mx1, g_my1;

static void mask_begin(PA_Canvas *c) {
    if (g_mask_w != c->w || g_mask_h != c->h) {
        free(g_mask); free(g_acc);
        g_mask = (uint8_t *)calloc((size_t)c->w * (size_t)c->h, 1);
        g_acc = (float *)calloc((size_t)c->w + 2, sizeof(float));
        g_mask_w = c->w; g_mask_h = c->h;
    }
    g_mask_on = g_mask && g_acc;
    g_mx0 = c->w; g_my0 = c->h; g_mx1 = -1; g_my1 = -1;
}

static void mask_poly(PA_Canvas *c, const PA_Vec2 *p, int n) {
    if (!g_mask_on) { pa_fill_poly(c, p, n, PA_RGBA(20, 18, 48, 84)); return; }
    float miny = p[0].y, maxy = p[0].y, minx = p[0].x, maxx = p[0].x;
    for (int i = 1; i < n; i++) {
        if (p[i].y < miny) miny = p[i].y;
        if (p[i].y > maxy) maxy = p[i].y;
        if (p[i].x < minx) minx = p[i].x;
        if (p[i].x > maxx) maxx = p[i].x;
    }
    int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx);
    if (y0 < c->clip_y0) y0 = c->clip_y0;
    if (y1 > c->clip_y1 - 1) y1 = c->clip_y1 - 1;
    if (x0 < c->clip_x0) x0 = c->clip_x0;
    if (x1 > c->clip_x1 - 1) x1 = c->clip_x1 - 1;
    if (y0 > y1 || x0 > x1) return;
    if (x0 < g_mx0) g_mx0 = x0;
    if (x1 > g_mx1) g_mx1 = x1;
    if (y0 < g_my0) g_my0 = y0;
    if (y1 > g_my1) g_my1 = y1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) g_acc[x] = 0.0f;
        for (int k = 0; k < 4; k++) {
            float sy = (float)y + ((float)k + 0.5f) * 0.25f;
            float xl = 1e9f, xr = -1e9f;
            for (int i = 0; i < n; i++) {
                PA_Vec2 a = p[i], b = p[(i + 1) % n];
                if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy)) {
                    float x = a.x + (sy - a.y) * (b.x - a.x) / (b.y - a.y);
                    if (x < xl) xl = x;
                    if (x > xr) xr = x;
                }
            }
            if (xr <= xl) continue;
            if (xl < (float)x0) xl = (float)x0;
            if (xr > (float)x1 + 1.0f) xr = (float)x1 + 1.0f;
            int a0 = (int)floorf(xl), a1 = (int)floorf(xr);
            for (int x = a0; x <= a1 && x <= x1; x++) {
                float lo = xl > (float)x ? xl : (float)x;
                float hi = xr < (float)(x + 1) ? xr : (float)(x + 1);
                if (hi > lo) g_acc[x] += (hi - lo) * 0.25f;
            }
        }
        uint8_t *m = g_mask + (size_t)y * (size_t)g_mask_w;
        for (int x = x0; x <= x1; x++) {
            int v = (int)(g_acc[x] * 255.0f + 0.5f);
            if (v > 255) v = 255;
            if (v > m[x]) m[x] = (uint8_t)v;
        }
    }
}

static void mask_apply(PA_Canvas *c, float strength) {
    if (!g_mask_on || g_mx1 < g_mx0) return;
    for (int y = g_my0; y <= g_my1; y++) {
        uint8_t *m = g_mask + (size_t)y * (size_t)g_mask_w;
        uint32_t *px = c->px + (size_t)y * (size_t)c->w;
        for (int x = g_mx0; x <= g_mx1; x++) {
            if (!m[x]) continue;
            float k = 1.0f - strength * (float)m[x] / 255.0f;
            uint32_t p = px[x];
            uint32_t r = (uint32_t)((float)((p >> 16) & 255) * k);
            uint32_t g = (uint32_t)((float)((p >> 8) & 255) * (k + (1.0f - k) * 0.04f));
            uint32_t b = (uint32_t)((float)(p & 255) * (k + (1.0f - k) * 0.10f));
            px[x] = (r << 16) | (g << 8) | b;
            m[x] = 0;
        }
    }
}

static int cross_z(PA_Vec2 o, PA_Vec2 a, PA_Vec2 b) {
    float v = (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    return v > 0.0f ? 1 : (v < 0.0f ? -1 : 0);
}

static int vec_cmp(const void *pa, const void *pb) {
    const PA_Vec2 *a = (const PA_Vec2 *)pa, *b = (const PA_Vec2 *)pb;
    if (a->x != b->x) return a->x < b->x ? -1 : 1;
    if (a->y != b->y) return a->y < b->y ? -1 : 1;
    return 0;
}

static int hull(PA_Vec2 *pts, int n, PA_Vec2 *out) {
    qsort(pts, (size_t)n, sizeof(PA_Vec2), vec_cmp);
    int k = 0;
    for (int i = 0; i < n; i++) {
        while (k >= 2 && cross_z(out[k - 2], out[k - 1], pts[i]) <= 0) k--;
        out[k++] = pts[i];
    }
    for (int i = n - 2, t = k + 1; i >= 0; i--) {
        while (k >= t && cross_z(out[k - 2], out[k - 1], pts[i]) <= 0) k--;
        out[k++] = pts[i];
    }
    return k - 1;
}

/** Hard shadow: the footprint swept down and to the right in proportion to
    the height of the thing casting it, on the ground at `gz`. */
static void shadow_box(PA_Canvas *c, float col, float row, float w, float d, float h, float gz) {
    float hw = w * 0.5f, hd = d * 0.5f;
    float ox = h * 0.58f, oy = -h * 0.26f;
    PA_Vec2 pts[8] = {
        project(col - hw, row - hd, gz), project(col + hw, row - hd, gz),
        project(col + hw, row + hd, gz), project(col - hw, row + hd, gz),
        project(col - hw + ox, row - hd + oy, gz), project(col + hw + ox, row - hd + oy, gz),
        project(col + hw + ox, row + hd + oy, gz), project(col - hw + ox, row + hd + oy, gz)
    };
    PA_Vec2 out[18];
    int n = hull(pts, 8, out);
    if (n >= 3) mask_poly(c, out, n);
}

/* ------------------------------------------------------------ ground rows -- */
static const World *WD(void) { return &WORLDS[H.world]; }

static PA_Color row_colour(const Row *r) {
    const World *w = WD();
    switch (r->type) {
        case ROW_ROAD:  return pa_hex(w->road);
        case ROW_WATER: return pa_hex(w->water);
        case ROW_RAIL:  return pa_hex(w->bed);
        default:        return pa_hex(r->band ? w->grass_b : w->grass_a);
    }
}

static int signal_blink(const Row *r) {
    return (r->warn || r->train_on) && ((int)(H.time * 5.0f) % 2) == 0;
}

static void draw_ground(PA_Canvas *c, Row *r) {
    const World *W = WD();
    float idx = (float)r->index;
    float lo = H.cam_col - (float)L.wide, hi = H.cam_col + (float)L.wide;
    float z = ground_z(r);
    PA_Color col = row_colour(r);
    PA_Color outside = pa_shade(col, r->type == ROW_GRASS ? -0.20f : -0.16f);

    quad_top(c, lo, -EDGE, idx - 0.5f, idx + 0.5f, z, outside);
    quad_top(c, EDGE, hi, idx - 0.5f, idx + 0.5f, z, outside);
    quad_top(c, -EDGE, EDGE, idx - 0.5f, idx + 0.5f, z, col);

    if (r->type == ROW_ROAD) {
        Row *nx = row_at(r->index + 1);
        if (nx && nx->type == ROW_ROAD) {
            for (float t = floorf(lo); t < hi; t += 1.0f) {
                float x0 = t + 0.25f;
                quad_top(c, x0, x0 + 0.50f, idx + 0.465f, idx + 0.535f, 0.0f,
                         pa_hex(W->dash));
            }
        }
    } else if (r->type == ROW_WATER) {
        /* The far bank throws a band of shade across the water under it. */
        quad_top(c, lo, hi, idx + 0.30f, idx + 0.5f, z, pa_shade(col, -0.10f));
        uint32_t bits = row_seed(r->index);
        float drift = r->pads ? 0.15f : -0.35f * (float)r->dir;
        for (int k = 0; k < 9; k++) {
            uint32_t hb = hash32(bits + (uint32_t)k * 977u);
            float span = (float)(L.wide * 2);
            float t = pa_wrapf((float)(hb & 1023) / 1023.0f * span + H.time * drift, span);
            float fx = lo + t;
            float fr = idx - 0.38f + (float)((hb >> 10) & 7) * 0.09f;
            float len = 0.10f + (float)((hb >> 14) & 3) * 0.06f;
            quad_top(c, fx, fx + len, fr, fr + 0.07f, z, pa_alpha(pa_hex(W->water_hi), 0.85f));
        }
    } else if (r->type == ROW_RAIL) {
        if (signal_blink(r)) quad_top(c, lo, hi, idx - 0.5f, idx + 0.5f, z, PA_RGBA(255, 40, 70, 46));
        for (float t = floorf(lo); t < hi; t += 0.62f)
            box3(c, t, idx, 0.0f, 0.16f, 0.82f, 0.03f, pa_hex(W->sleeper));
        for (int s2 = -1; s2 <= 1; s2 += 2) {
            float rr = idx + (float)s2 * 0.24f;
            box3(c, (lo + hi) * 0.5f, rr, 0.03f, hi - lo, 0.07f, 0.05f, pa_hex(W->rail));
        }
    }

    /* South lip down to the nearer row, wherever this row stands higher. */
    Row *n = row_at(r->index - 1);
    float zn = n ? ground_z(n) : z;
    if (z > zn + 0.001f) {
        PA_Color lip = r->type == ROW_GRASS
            ? (H.world == W_ARCTIC ? pa_hex(0xB9CCE0) : pa_shade(pa_hex(W->grass_a), -0.34f))
            : pa_shade(col, -0.30f);
        PA_Vec2 q[4] = {
            project(-EDGE, idx - 0.5f, z), project(EDGE, idx - 0.5f, z),
            project(EDGE, idx - 0.5f, zn), project(-EDGE, idx - 0.5f, zn)
        };
        pa_fill_poly(c, q, 4, lip);
        PA_Color lip_out = pa_shade(lip, -0.18f);
        PA_Vec2 a[4] = { project(lo, idx - 0.5f, z), project(-EDGE, idx - 0.5f, z),
                         project(-EDGE, idx - 0.5f, zn), project(lo, idx - 0.5f, zn) };
        PA_Vec2 b[4] = { project(EDGE, idx - 0.5f, z), project(hi, idx - 0.5f, z),
                         project(hi, idx - 0.5f, zn), project(EDGE, idx - 0.5f, zn) };
        pa_fill_poly(c, a, 4, lip_out);
        pa_fill_poly(c, b, 4, lip_out);
    }
}

/* ----------------------------------------------------------------- props -- */
/** Scenery outside the playable columns, from the row hash. */
static int prop_at(const Row *r, int col, int *kind, float *hv) {
    if (r->type != ROW_GRASS) return 0;
    if (col >= -HALF && col <= HALF) {
        int k = r->prop[col + HALF];
        if (!k) return 0;
        *kind = k; *hv = r->prop_h[col + HALF];
        return 1;
    }
    uint32_t h = hash32(row_seed(r->index) + (uint32_t)(col * 7919) + H.run_seed);
    if ((h & 15) > 8) return 0;
    float k = (float)((h >> 4) & 255) / 255.0f;
    *kind = k < 0.50f ? P_TREE : (k < 0.76f ? P_BUSH : (k < 0.90f ? P_ROCK : P_STUMP));
    *hv = (float)((h >> 12) & 255) / 255.0f;
    return 1;
}

static int deco_at(const Row *r, int col) {
    if (r->type != ROW_GRASS) return 0;
    if (col >= -HALF && col <= HALF) return r->deco[col + HALF];
    uint32_t h = hash32(row_seed(r->index) * 3u + (uint32_t)(col * 104729) + H.run_seed);
    if ((h & 7) > 2) return 0;
    return 1 + (int)((h >> 3) % 3u);
}

static void prop_shadow(PA_Canvas *c, int kind, float col, float row, float hv, float gz) {
    float h = 0.5f;
    float w = 0.7f;
    switch (H.world) {
        case W_CLASSIC: h = kind == P_TREE ? 0.9f + hv * 1.1f : kind == P_BUSH ? 0.55f : kind == P_ROCK ? 0.44f : 0.26f;
                        w = kind == P_STUMP ? 0.42f : 0.76f; break;
        case W_OUTBACK: h = kind == P_TREE ? 1.0f + hv * 0.9f : kind == P_BUSH ? 0.55f : kind == P_ROCK ? 0.42f : 0.95f;
                        w = kind == P_STUMP ? 0.24f : kind == P_TREE ? 0.62f : 0.72f; break;
        case W_ARCTIC:  h = kind == P_TREE ? 1.3f + hv * 0.6f : kind == P_BUSH ? 0.45f : kind == P_ROCK ? 0.55f : 0.72f;
                        w = kind == P_STUMP ? 0.46f : 0.74f; break;
        default:        h = kind == P_TREE ? 1.3f + hv * 0.5f : kind == P_BUSH ? 0.5f : kind == P_ROCK ? 0.62f : 0.42f;
                        w = kind == P_TREE ? 0.34f : kind == P_ROCK ? 0.5f : 0.6f; break;
    }
    shadow_box(c, col, row, w, w * (kind == P_ROCK && H.world == W_SPOOKY ? 0.36f : 1.0f), h, gz);
}

static void draw_prop(PA_Canvas *c, int kind, float col, float row, float hv, float z) {
    switch (H.world) {
    case W_CLASSIC:
        if (kind == P_TREE) {
            /* Classic tree: short trunk under one tall block of leaves. */
            float hh = 0.55f + hv * 1.0f;
            box3(c, col, row, z, 0.26f, 0.26f, 0.42f, pa_hex(0x7A5232));
            box3x(c, col, row, z + 0.40f, 0.78f, 0.78f, hh, pa_hex(hv > 0.5f ? 0x4FA83A : 0x5BB542), 0.10f);
        } else if (kind == P_BUSH) {
            box3x(c, col, row, z, 0.74f, 0.70f, 0.36f, pa_hex(0x58A83E), 0.10f);
            box3x(c, col - 0.06f, row + 0.04f, z + 0.36f, 0.44f, 0.40f, 0.18f, pa_hex(0x6BBA4A), 0.12f);
        } else if (kind == P_ROCK) {
            box3x(c, col, row, z, 0.72f, 0.62f, 0.30f, pa_hex(0xA8A8B8), 0.10f);
            box3x(c, col - 0.05f, row + 0.03f, z + 0.30f, 0.44f, 0.38f, 0.14f, pa_hex(0xBEBECC), 0.12f);
        } else {
            box3(c, col, row, z, 0.42f, 0.42f, 0.24f, pa_hex(0x8A5A36));
            quad_top(c, col - 0.15f, col + 0.15f, row - 0.15f, row + 0.15f, z + 0.241f, pa_hex(0xD3A574));
            quad_top(c, col - 0.07f, col + 0.07f, row - 0.07f, row + 0.07f, z + 0.242f, pa_hex(0xA87B4C));
        }
        break;
    case W_OUTBACK:
        if (kind == P_TREE) {
            float hh = 0.65f + hv * 0.9f;
            box3(c, col, row, z, 0.20f, 0.20f, 0.44f, pa_hex(0x6B4A30));
            box3x(c, col, row, z + 0.42f, 0.62f, 0.62f, hh, pa_hex(0x6E8A4E), 0.10f);
        } else if (kind == P_BUSH) {
            box3x(c, col, row, z, 0.72f, 0.68f, 0.34f, pa_hex(0x6A874C), 0.10f);
            box3x(c, col - 0.05f, row + 0.04f, z + 0.34f, 0.44f, 0.40f, 0.20f, pa_hex(0x7C9A5A), 0.12f);
        } else if (kind == P_ROCK) {
            box3x(c, col, row, z, 0.70f, 0.62f, 0.28f, pa_hex(0xBDB4DE), 0.10f);
            box3x(c, col - 0.06f, row + 0.02f, z + 0.28f, 0.42f, 0.36f, 0.14f, pa_hex(0xD2CBEC), 0.12f);
        } else {
            /* Cactus: column with two raised arms. */
            PA_Color g = pa_hex(0x5E9A48);
            box3(c, col, row, z, 0.24f, 0.24f, 0.92f, g);
            box3(c, col - 0.22f, row, z + 0.34f, 0.20f, 0.14f, 0.12f, g);
            box3(c, col - 0.28f, row, z + 0.46f, 0.12f, 0.14f, 0.26f, g);
            box3(c, col + 0.22f, row, z + 0.52f, 0.20f, 0.14f, 0.12f, g);
            box3(c, col + 0.28f, row, z + 0.64f, 0.12f, 0.14f, 0.22f, g);
        }
        break;
    case W_ARCTIC:
        if (kind == P_TREE) {
            float hh = 0.40f + hv * 0.25f;
            PA_Color g = pa_hex(0x2F6E52), snow = pa_hex(0xF6FAFE);
            box3(c, col, row, z, 0.20f, 0.20f, 0.30f, pa_hex(0x6B4E46));
            box3(c, col, row, z + 0.26f, 0.84f, 0.84f, hh, g);
            box3(c, col, row, z + 0.26f + hh, 0.84f, 0.84f, 0.06f, snow);
            box3(c, col, row, z + 0.32f + hh, 0.60f, 0.60f, hh, g);
            box3(c, col, row, z + 0.32f + hh * 2.0f, 0.60f, 0.60f, 0.06f, snow);
            box3(c, col, row, z + 0.38f + hh * 2.0f, 0.34f, 0.34f, hh * 0.8f, g);
            box3(c, col, row, z + 0.38f + hh * 2.8f, 0.34f, 0.34f, 0.07f, snow);
        } else if (kind == P_BUSH) {
            box3x(c, col, row, z, 0.76f, 0.70f, 0.28f, pa_hex(0xE8F0FA), 0.10f);
            box3x(c, col + 0.04f, row + 0.04f, z + 0.28f, 0.46f, 0.40f, 0.16f, pa_hex(0xF6FAFF), 0.12f);
        } else if (kind == P_ROCK) {
            box3x(c, col, row, z, 0.70f, 0.66f, 0.52f, pa_hex(0x9FD3F2), 0.12f);
            quad_top(c, col - 0.2f, col + 0.1f, row - 0.2f, row + 0.05f, z + 0.521f, pa_hex(0xD8F0FE));
        } else {
            /* Snowman. */
            PA_Color s = pa_hex(0xF4F8FC);
            box3(c, col, row, z, 0.48f, 0.48f, 0.34f, s);
            box3(c, col, row, z + 0.34f, 0.34f, 0.34f, 0.28f, s);
            box3(c, col, row, z + 0.62f, 0.26f, 0.26f, 0.06f, pa_hex(0x2A2E3C));
            box3(c, col, row, z + 0.68f, 0.18f, 0.18f, 0.14f, pa_hex(0x2A2E3C));
            box3(c, col, row - 0.22f, z + 0.44f, 0.06f, 0.12f, 0.06f, pa_hex(0xF7922B));
            face_s(c, col - 0.11f, col - 0.05f, row - 0.171f, z + 0.52f, z + 0.57f, pa_hex(0x1A1424));
            face_s(c, col + 0.05f, col + 0.11f, row - 0.171f, z + 0.52f, z + 0.57f, pa_hex(0x1A1424));
        }
        break;
    default:
        if (kind == P_TREE) {
            PA_Color b = pa_hex(0x3A2A48);
            float hh = 1.0f + hv * 0.5f;
            box3(c, col, row, z, 0.22f, 0.22f, hh, b);
            box3(c, col - 0.20f, row, z + hh * 0.55f, 0.24f, 0.12f, 0.10f, b);
            box3(c, col - 0.30f, row, z + hh * 0.55f, 0.10f, 0.12f, 0.30f, b);
            box3(c, col + 0.18f, row, z + hh * 0.72f, 0.20f, 0.12f, 0.10f, b);
            box3(c, col + 0.26f, row, z + hh * 0.72f, 0.10f, 0.12f, 0.24f, b);
        } else if (kind == P_BUSH) {
            box3x(c, col, row, z, 0.70f, 0.66f, 0.34f, pa_hex(0x4A3560), 0.10f);
            box3x(c, col - 0.05f, row + 0.04f, z + 0.34f, 0.42f, 0.38f, 0.18f, pa_hex(0x5A4274), 0.12f);
        } else if (kind == P_ROCK) {
            /* Gravestone on a little mound. */
            box3(c, col, row, z, 0.60f, 0.40f, 0.06f, pa_hex(0x9A5A2A));
            box3x(c, col, row + 0.04f, z + 0.06f, 0.46f, 0.16f, 0.52f, pa_hex(0x8A84A0), 0.12f);
            face_s(c, col - 0.12f, col + 0.12f, row - 0.041f, z + 0.38f, z + 0.42f, pa_hex(0x5E5874));
            face_s(c, col - 0.02f, col + 0.02f, row - 0.041f, z + 0.28f, z + 0.48f, pa_hex(0x5E5874));
        } else {
            /* Jack-o-lantern with a glowing face. */
            box3x(c, col, row, z, 0.50f, 0.48f, 0.40f, pa_hex(0xF07A1E), 0.10f);
            box3(c, col, row, z + 0.40f, 0.08f, 0.08f, 0.10f, pa_hex(0x4C8A30));
            PA_Color glow = pa_hex(0xFFD84A);
            face_s(c, col - 0.16f, col - 0.06f, row - 0.241f, z + 0.22f, z + 0.31f, glow);
            face_s(c, col + 0.06f, col + 0.16f, row - 0.241f, z + 0.22f, z + 0.31f, glow);
            face_s(c, col - 0.15f, col + 0.15f, row - 0.241f, z + 0.08f, z + 0.14f, glow);
        }
        break;
    }
}

static void draw_deco(PA_Canvas *c, int kind, float col, float row, float z, uint32_t seed) {
    const World *W = WD();
    float ox = ((float)(seed & 15) / 15.0f - 0.5f) * 0.36f;
    float oy = ((float)((seed >> 4) & 15) / 15.0f - 0.5f) * 0.36f;
    col += ox; row += oy;
    if (kind == D_TUFT) {
        PA_Color t = pa_hex(W->tuft);
        box3(c, col - 0.07f, row, z, 0.05f, 0.05f, 0.10f, t);
        box3(c, col + 0.02f, row + 0.04f, z, 0.05f, 0.05f, 0.14f, t);
        box3(c, col + 0.09f, row - 0.02f, z, 0.05f, 0.05f, 0.08f, t);
    } else if (kind == D_FLOWER) {
        PA_Color f = pa_hex((seed >> 9) & 1 ? W->flower_a : W->flower_b);
        box3(c, col, row, z, 0.04f, 0.04f, 0.10f, pa_hex(W->tuft));
        box3(c, col, row, z + 0.10f, 0.14f, 0.14f, 0.05f, f);
        box3(c, col, row, z + 0.15f, 0.05f, 0.05f, 0.02f, pa_hex(0xFFE04A));
    } else {
        box3(c, col, row, z, 0.16f, 0.12f, 0.05f, pa_hex(W->pebble));
    }
}

/* -------------------------------------------------------------- vehicles -- */
static void wheel(PA_Canvas *c, float x, float row) {
    box3(c, x, row, 0.0f, 0.26f, 0.76f, 0.20f, pa_hex(0x22202C));
    face_s(c, x - 0.05f, x + 0.05f, row - 0.381f, 0.06f, 0.14f, pa_hex(0xD8D8E2));
}

static void vehicle_shadow(PA_Canvas *c, const Row *r, const Mover *m) {
    float h = m->kind == V_TRUCK ? 1.1f : m->kind == V_BUS ? 0.95f : m->kind == V_VAN ? 0.72f : 0.62f;
    shadow_box(c, m->x, (float)r->index, m->len, 0.70f, h, 0.0f);
}

static void draw_vehicle(PA_Canvas *c, const Row *r, const Mover *m) {
    PA_Color body = pa_hex(m->col);
    PA_Color glass = pa_hex(0x2A3550), white = pa_hex(0xF4F6FA);
    float f = (float)r->dir;
    float row = (float)r->index;
    float L2 = m->len * 0.5f, x = m->x;
    float rs = row - 0.33f;     /* south face of a 0.66-deep body */

    if (m->kind == V_TRUCK) {
        float cab_x = x + f * (L2 - 0.36f);
        float tr_x = x - f * 0.38f, tr_len = m->len - 0.80f;
        wheel(c, cab_x, row);
        wheel(c, tr_x - f * tr_len * 0.32f, row);
        wheel(c, tr_x - f * tr_len * 0.10f, row);
        box3(c, tr_x, row, 0.10f, tr_len, 0.56f, 0.10f, pa_hex(0x2C2A36));
        box3x(c, tr_x, row, 0.18f, tr_len, 0.72f, 0.88f, pa_hex(H.world == W_SPOOKY ? 0xB8B0C8 : 0xE2E8F6), 0.03f);
        face_s(c, tr_x - tr_len * 0.5f + 0.06f, tr_x + tr_len * 0.5f - 0.06f, row - 0.361f, 0.30f, 0.36f,
               pa_hex(0xE24B4B));
        box3(c, cab_x, row, 0.12f, 0.70f, 0.68f, 0.58f, body);
        box3(c, cab_x - f * 0.05f, row, 0.70f, 0.40f, 0.60f, 0.10f, white);
        face_s(c, cab_x - 0.10f + f * 0.08f, cab_x + 0.10f + f * 0.08f, row - 0.341f, 0.42f, 0.62f, glass);
        if (f > 0) {
            face_e(c, cab_x + 0.351f, row - 0.26f, row + 0.26f, 0.42f, 0.62f, glass);
            face_e(c, cab_x + 0.351f, row - 0.30f, row - 0.18f, 0.18f, 0.27f, pa_hex(0xFFF2B0));
            face_e(c, cab_x + 0.351f, row + 0.18f, row + 0.30f, 0.18f, 0.27f, pa_hex(0xFFF2B0));
        } else {
            face_e(c, tr_x + tr_len * 0.5f + 0.001f, row - 0.30f, row + 0.30f, 0.22f, 0.30f, pa_hex(0xC03038));
        }
        return;
    }
    if (m->kind == V_BUS) {
        wheel(c, x - L2 + 0.45f, row);
        wheel(c, x + L2 - 0.45f, row);
        box3x(c, x, row, 0.12f, m->len, 0.70f, 0.78f, body, 0.03f);
        box3(c, x, row, 0.90f, m->len - 0.24f, 0.56f, 0.06f, white);
        float wz0 = 0.46f, wz1 = 0.72f;
        for (float t = -L2 + 0.18f; t < L2 - 0.30f; t += 0.42f)
            face_s(c, x + t, x + t + 0.32f, row - 0.351f, wz0, wz1, glass);
        face_s(c, x - L2 + 0.04f, x + L2 - 0.04f, row - 0.351f, 0.24f, 0.30f, white);
        if (f > 0) face_e(c, x + L2 + 0.001f, row - 0.28f, row + 0.28f, 0.42f, 0.74f, glass);
        else face_e(c, x + L2 + 0.001f, row - 0.30f, row + 0.30f, 0.50f, 0.74f, glass);
        face_e(c, x + L2 + 0.001f, row - 0.31f, row - 0.20f, 0.18f, 0.26f,
               f > 0 ? pa_hex(0xFFF2B0) : pa_hex(0xC03038));
        face_e(c, x + L2 + 0.001f, row + 0.20f, row + 0.31f, 0.18f, 0.26f,
               f > 0 ? pa_hex(0xFFF2B0) : pa_hex(0xC03038));
        return;
    }

    wheel(c, x - L2 + 0.30f, row);
    wheel(c, x + L2 - 0.30f, row);
    if (m->kind == V_VAN) {
        box3x(c, x, row, 0.12f, m->len, 0.68f, 0.56f, body, 0.03f);
        box3(c, x - f * 0.1f, row, 0.68f, m->len * 0.6f, 0.50f, 0.04f, pa_shade(body, -0.2f));
        float fx = x + f * (L2 - 0.36f);
        face_s(c, fx - 0.16f, fx + 0.16f, rs - 0.011f, 0.40f, 0.60f, glass);
        face_s(c, x - L2 + 0.06f, x + L2 - 0.06f, rs - 0.011f, 0.26f, 0.32f, white);
    } else {
        box3x(c, x, row, 0.12f, m->len, 0.66f, 0.26f, body, 0.04f);
        face_s(c, x - L2, x + L2, rs - 0.001f, 0.12f, 0.17f, pa_shade(body, -0.40f));
        float cx = x - f * m->len * 0.05f, cl = m->len * 0.56f;
        box3x(c, cx, row, 0.38f, cl, 0.60f, 0.24f, white, 0.05f);
        face_s(c, cx - cl * 0.5f + 0.07f, cx - 0.03f, row - 0.301f, 0.43f, 0.56f, glass);
        face_s(c, cx + 0.03f, cx + cl * 0.5f - 0.07f, row - 0.301f, 0.43f, 0.56f, glass);
        face_e(c, cx + cl * 0.5f + 0.001f, row - 0.22f, row + 0.22f, 0.43f, 0.56f, glass);
        if (m->kind == V_TAXI) {
            box3(c, cx, row, 0.62f, 0.26f, 0.22f, 0.10f, pa_hex(0xF4F4F4));
            face_s(c, cx - 0.10f, cx + 0.10f, row - 0.111f, 0.645f, 0.69f, pa_hex(0xE24B4B));
            for (float t = -L2 + 0.1f; t < L2 - 0.1f; t += 0.2f)
                face_s(c, x + t, x + t + 0.1f, rs - 0.002f, 0.22f, 0.28f, pa_hex(0x2A2A30));
        }
    }
    face_e(c, x + L2 + 0.001f, row - 0.28f, row - 0.17f, 0.20f, 0.30f,
           f > 0 ? pa_hex(0xFFF2B0) : pa_hex(0xC03038));
    face_e(c, x + L2 + 0.001f, row + 0.17f, row + 0.28f, 0.20f, 0.30f,
           f > 0 ? pa_hex(0xFFF2B0) : pa_hex(0xC03038));
    face_e(c, x + L2 + 0.002f, row - 0.33f, row + 0.33f, 0.12f, 0.16f, pa_hex(0xD8D8E2));
}

/* ------------------------------------------------------ river and rails -- */
static void draw_log(PA_Canvas *c, const Row *r, const Mover *m) {
    const World *W = WD();
    float row = (float)r->index, z = Z_WATER - 0.04f;
    float L2 = m->len * 0.5f;
    /* Foam where the log pushes the water. */
    float lead = m->x + (float)r->dir * (L2 + 0.05f);
    float wob = sinf(H.time * 9.0f + (float)m->kind) * 0.04f;
    quad_top(c, lead - 0.12f + wob, lead + 0.12f + wob, row - 0.38f, row + 0.30f, Z_WATER + 0.001f,
             PA_RGBA(255, 255, 255, 200));
    if (H.world == W_ARCTIC) {
        box3x(c, m->x, row, Z_WATER - 0.02f, m->len, 0.76f, 0.16f, pa_hex(W->log), 0.08f);
        box3(c, m->x - L2 * 0.4f, row + 0.30f, Z_WATER - 0.02f, m->len * 0.4f, 0.20f, 0.16f, pa_hex(W->log));
        quad_top(c, m->x - L2 + 0.1f, m->x - L2 * 0.2f, row - 0.2f, row + 0.1f, Z_WATER + 0.141f, pa_hex(0xFFFFFF));
        return;
    }
    box3x(c, m->x, row, z, m->len, 0.68f, 0.26f, pa_hex(W->log), 0.06f);
    uint32_t hs = hash32((uint32_t)m->kind * 31u + 7u);
    for (int k = 0; k < (int)(m->len * 3.0f); k++) {
        uint32_t hb = hash32(hs + (uint32_t)k);
        float t = -L2 + 0.12f + (float)(hb & 255) / 255.0f * (m->len - 0.5f);
        float rr = row - 0.26f + (float)((hb >> 8) & 7) * 0.07f;
        quad_top(c, m->x + t, m->x + t + 0.24f + (float)((hb >> 12) & 3) * 0.06f, rr, rr + 0.07f, z + 0.261f,
                 pa_hex(W->log_dark));
    }
    face_e(c, m->x + L2 + 0.001f, row - 0.25f, row + 0.25f, z + 0.04f, z + 0.22f, pa_shade(pa_hex(W->log), 0.25f));
    face_e(c, m->x + L2 + 0.002f, row - 0.10f, row + 0.10f, z + 0.10f, z + 0.16f, pa_hex(W->log_dark));
}

static void draw_pad(PA_Canvas *c, const Row *r, int col) {
    const World *W = WD();
    float row = (float)r->index, x = (float)col;
    float bob = sinf(H.time * 2.0f + (float)col) * 0.012f;
    float z = Z_WATER - 0.01f + bob;
    PA_Color p = pa_hex(W->pad);
    box3x(c, x, row, z, 0.70f, 0.70f, 0.05f, p, 0.06f);
    if (H.world != W_ARCTIC) {
        /* The notch every lily pad has. */
        PA_Vec2 q[3] = { project(x, row, z + 0.051f), project(x + 0.36f, row - 0.12f, z + 0.051f),
                         project(x + 0.36f, row + 0.06f, z + 0.051f) };
        pa_fill_poly(c, q, 3, pa_hex(W->water));
        quad_top(c, x - 0.25f, x - 0.05f, row + 0.05f, row + 0.25f, z + 0.052f, pa_shade(p, 0.15f));
    }
}

static void train_shadow(PA_Canvas *c, const Row *r) {
    shadow_box(c, r->train_x, (float)r->index, r->train_len, 0.86f, 1.05f, 0.0f);
}

static void draw_train(PA_Canvas *c, const Row *r) {
    const World *W = WD();
    float row = (float)r->index, f = (float)r->dir;
    PA_Color glass = pa_hex(0x2A3550), white = pa_hex(0xF2F2F6);
    for (int k = r->train_cars - 1; k >= 0; k--) {
        float cx = r->train_x + f * (r->train_len * 0.5f - 1.5f - (float)k * 3.0f);
        PA_Color body = pa_hex(k == 0 ? W->train_a : W->train_b);
        box3(c, cx, row, 0.06f, 2.6f, 0.62f, 0.10f, pa_hex(0x24222E));
        box3x(c, cx, row, 0.14f, 2.86f, 0.86f, 0.86f, body, 0.03f);
        box3(c, cx, row, 1.00f, 2.6f, 0.62f, 0.08f, pa_shade(body, -0.25f));
        face_s(c, cx - 1.43f, cx + 1.43f, row - 0.431f, 0.26f, 0.34f, white);
        int nwin = k == 0 ? 3 : 5;
        for (int wv = 0; wv < nwin; wv++) {
            float wx = cx - 1.15f + (float)wv * 0.52f + (k == 0 && f < 0 ? 1.04f : 0.0f);
            face_s(c, wx, wx + 0.34f, row - 0.431f, 0.52f, 0.82f, glass);
        }
        if (k == 0 && f > 0) {
            face_e(c, cx + 1.431f, row - 0.36f, row + 0.36f, 0.52f, 0.84f, glass);
            face_e(c, cx + 1.431f, row - 0.43f, row + 0.43f, 0.26f, 0.36f, pa_hex(0xFFD84A));
            face_e(c, cx + 1.432f, row - 0.10f, row + 0.10f, 0.40f, 0.48f, pa_hex(0xFFF6C0));
        } else if (k == r->train_cars - 1 || k == 0) {
            face_e(c, cx + 1.431f, row - 0.30f, row + 0.30f, 0.52f, 0.82f, glass);
        }
    }
}

#define SIGNAL_COL 2.8f

static void draw_signal(PA_Canvas *c, const Row *r, float side) {
    float row = (float)r->index + 0.42f;
    float col = side * SIGNAL_COL;
    for (int k = 0; k < 6; k++)
        box3(c, col, row, (float)k * 0.20f, 0.11f, 0.11f, 0.20f,
             (k & 1) ? pa_hex(0xF4F4F4) : pa_hex(0xD8363A));
    box3(c, col, row, 1.20f, 0.11f, 0.11f, 0.10f, pa_hex(0xF4F4F4));
    box3(c, col, row - 0.07f, 1.02f, 0.56f, 0.06f, 0.18f, pa_hex(0x1C1A24));
    int blink = signal_blink(r);
    for (int s = -1; s <= 1; s += 2) {
        int lit = (r->warn || r->train_on) && (((int)(H.time * 5.0f) + (s > 0)) % 2 == 0);
        PA_Vec2 lp = project(col + (float)s * 0.18f, row - 0.10f, 1.11f);
        if (lit) {
            float gr = V.tile * 0.9f;
            PA_Paint glow = pa_radial(lp.x, lp.y, 0.0f, gr);
            pa_stop(&glow, 0.0f, PA_RGBA(255, 60, 80, 170));
            pa_stop(&glow, 1.0f, PA_RGBA(255, 60, 80, 0));
            pa_fill_ellipse_paint(c, lp.x, lp.y, gr, gr * 0.75f, &glow);
        }
        pa_fill_rect(c, lp.x - V.tile * 0.045f, lp.y - V.tile * 0.045f, V.tile * 0.09f, V.tile * 0.09f,
                     lit ? pa_hex(0xFF3A48) : pa_hex(0x5A1C20));
    }
    (void)blink;
}

/* ------------------------------------------------------------------ coins -- */
static void draw_coin_at(PA_Canvas *c, float cx, float row, float z) {
    float ph = H.time * 3.0f + row * 0.7f;
    float spin = fabsf(cosf(ph));
    float w = 0.10f + 0.34f * spin;
    PA_Color gold = pa_hex(0xFFD21F), red = pa_hex(0xE8402E);
    box3(c, cx, row, z, w, 0.08f, 0.44f, gold);
    if (spin > 0.45f) {
        float s = w / 0.44f;
        float x0 = cx - 0.13f * s, x1 = cx + 0.13f * s, fr = row - 0.041f;
        face_s(c, x0, x0 + 0.07f * s, fr, z + 0.09f, z + 0.35f, red);
        face_s(c, x0, x1, fr, z + 0.28f, z + 0.35f, red);
        face_s(c, x0, x1, fr, z + 0.09f, z + 0.16f, red);
    }
}

/* ------------------------------------------------------------ characters -- */
typedef struct { float key; int i; } PartKey;

static void critter_part_world(const PartDef *p, int facing, float *dc, float *dr, float *w, float *d) {
    float fxv = 0, frv = 1;
    switch (facing) {
        case 1: fxv = 1; frv = 0; break;
        case 2: fxv = 0; frv = -1; break;
        case 3: fxv = -1; frv = 0; break;
        default: break;
    }
    float rxv = frv, rrv = -fxv;
    *dc = p->lx * rxv + p->lf * fxv;
    *dr = p->lx * rrv + p->lf * frv;
    if (fxv != 0.0f) { *w = p->d; *d = p->w; } else { *w = p->w; *d = p->d; }
}

/** mode 0 normal, 1 silhouette, 2 flattened. */
static void draw_critter(PA_Canvas *c, int idx, float col, float row, float z, int facing,
                         float sx, float sz, int mode) {
    const CharDef *cd = &CHARS[idx];
    PartKey keys[MAX_PARTS];
    int n = cd->n < MAX_PARTS ? cd->n : MAX_PARTS;
    float k = CHAR_SCALE;
    for (int i = 0; i < n; i++) {
        float dc, dr, w, d;
        critter_part_world(&cd->parts[i], facing, &dc, &dr, &w, &d);
        keys[i].key = depth_key(dc, dr) + cd->parts[i].z * 1.0f;
        keys[i].i = i;
    }
    for (int i = 1; i < n; i++) {
        PartKey t = keys[i];
        int j = i - 1;
        while (j >= 0 && keys[j].key > t.key) { keys[j + 1] = keys[j]; j--; }
        keys[j + 1] = t;
    }
    float hover = cd->hover + (cd->hover > 0.0f ? sinf(H.time * 3.0f) * 0.04f : 0.0f);
    for (int q = 0; q < n; q++) {
        const PartDef *p = &cd->parts[keys[q].i];
        float dc, dr, w, d;
        critter_part_world(p, facing, &dc, &dr, &w, &d);
        PA_Color colr = mode == 1 ? pa_hex(0x1C1830) : pa_hex(p->col);
        if (mode == 2) {
            box3(c, col + dc * k * 1.5f, row + dr * k * 1.5f, z + p->z * k * 0.08f,
                 w * k * 1.5f, d * k * 1.5f, p->h * k * 0.10f + 0.01f, colr);
        } else {
            box3(c, col + dc * k * sx, row + dr * k * sx, z + (p->z + hover) * k * sz,
                 w * k * sx, d * k * sx, p->h * k * sz, colr);
        }
    }
}

/* ------------------------------------------------------------------ eagle -- */
static void eagle_pos(float *x, float *row, float *z) {
    float t = H.eagle_t;
    float a = 0.55f;
    if (t < a) {
        float k = pa_smooth(t / a);
        *x = H.death_x; *row = H.death_y + (1.0f - k) * 9.0f; *z = pa_lerpf(3.4f, 0.95f, k);
    } else {
        float k = t - a;
        *x = H.death_x; *row = H.death_y - k * 7.0f; *z = 0.95f + k * 1.6f;
    }
}

static void draw_eagle(PA_Canvas *c, float x, float row, float z) {
    PA_Color brown = pa_hex(0x6B4528), dark = pa_hex(0x4E3220), white = pa_hex(0xF4F2EA), yel = pa_hex(0xF6C02E);
    float flap = sinf(H.time * 16.0f);
    for (int s = -1; s <= 1; s += 2) {
        box3(c, x + (float)s * 0.62f, row + 0.02f, z + 0.20f + flap * 0.10f, 0.74f, 0.64f, 0.08f, brown);
        box3(c, x + (float)s * 1.30f, row + 0.06f, z + 0.20f + flap * 0.32f, 0.66f, 0.54f, 0.07f, dark);
    }
    box3(c, x, row + 0.56f, z + 0.10f, 0.42f, 0.30f, 0.10f, white);
    box3(c, x, row, z, 0.52f, 0.90f, 0.36f, brown);
    box3(c, x - 0.14f, row - 0.12f, z - 0.16f, 0.08f, 0.08f, 0.18f, yel);
    box3(c, x + 0.14f, row - 0.12f, z - 0.16f, 0.08f, 0.08f, 0.18f, yel);
    box3(c, x, row - 0.58f, z + 0.10f, 0.38f, 0.34f, 0.32f, white);
    box3(c, x, row - 0.80f, z + 0.14f, 0.14f, 0.12f, 0.12f, yel);
    face_s(c, x - 0.15f, x - 0.08f, row - 0.751f, z + 0.30f, z + 0.37f, pa_hex(0x1A1424));
    face_s(c, x + 0.08f, x + 0.15f, row - 0.751f, z + 0.30f, z + 0.37f, pa_hex(0x1A1424));
}

/* ------------------------------------------------------------- drawables -- */
enum { DR_PROP, DR_DECO, DR_CAR, DR_LOG, DR_PAD, DR_TRAIN, DR_COIN, DR_SIGNAL, DR_PLAYER, DR_FX, DR_EAGLE };

typedef struct { float key; int kind; int row; int idx; float x; } Drawable;

#define MAX_DRAW 1400
static Drawable g_draw[MAX_DRAW];
static int      g_draw_n;

static void push(int kind, int row, int idx, float x, float y, float bias) {
    if (g_draw_n >= MAX_DRAW) return;
    Drawable *d = &g_draw[g_draw_n++];
    d->key = depth_key(x, y) + bias;
    d->kind = kind; d->row = row; d->idx = idx; d->x = x;
}

static int draw_cmp(const void *a, const void *b) {
    float d = ((const Drawable *)a)->key - ((const Drawable *)b)->key;
    return d < 0.0f ? -1 : (d > 0.0f ? 1 : 0);
}

static void player_pose(float *z, float *sx, float *sz) {
    float t = pa_clamp01(H.hop_t);
    *z = H.base_z + H.hop_z;
    *sx = 1.0f; *sz = 1.0f;
    if (H.hopping) {
        float st = sinf(t * PA_PI);
        *sz = 1.0f + 0.20f * st * (t < 0.5f ? 1.0f : 0.5f);
        *sx = 1.0f - 0.10f * st;
    } else if (H.land_t > 0.0f) {
        float k = H.land_t / 0.10f;
        *sz = 1.0f - 0.20f * k;
        *sx = 1.0f + 0.12f * k;
    } else {
        *sz = 1.0f - 0.18f * H.press + sinf(H.time * 5.0f) * 0.012f;
        *sx = 1.0f + 0.10f * H.press;
    }
    if (H.bump_t > 0.0f) *sz *= 1.0f - 0.10f * (H.bump_t / 0.12f);
}

static void draw_player(PA_Canvas *c) {
    float z, sx, sz;
    player_pose(&z, &sx, &sz);
    if (H.over) {
        if (H.death == DEATH_CAR) {
            draw_critter(c, H.ch, H.death_x, H.death_y, H.base_z + 0.005f, H.facing, 1.0f, 1.0f, 2);
            return;
        }
        if (H.death == DEATH_TRAIN) return;
        if (H.death == DEATH_WATER) {
            if (H.sink >= 1.0f) return;
            float s = 1.0f - H.sink;
            draw_critter(c, H.ch, H.death_x, H.death_y, Z_WATER - H.sink * 0.5f, H.facing, s, s, 0);
            return;
        }
        if (H.death == DEATH_EAGLE) {
            if (H.eagle_t < 0.55f) {
                draw_critter(c, H.ch, H.death_x, H.death_y, H.base_z, H.facing, 1.0f, 1.0f - 0.15f * pa_clamp01(H.eagle_t * 3.0f), 0);
            } else {
                float ex, er, ez;
                eagle_pos(&ex, &er, &ez);
                draw_critter(c, H.ch, ex, er, ez - 0.80f, 2, 1.0f, 1.0f, 0);
            }
            return;
        }
    }
    draw_critter(c, H.ch, H.draw_x, H.draw_y, z, H.facing, sx, sz, 0);
}

static void player_shadow(PA_Canvas *c) {
    if (H.over && H.death != DEATH_EAGLE) return;
    float gz = H.hopping ? pa_lerpf(H.hop_z0, H.hop_z1, pa_clamp01(H.hop_t)) : H.base_z;
    float x = H.draw_x, y = H.draw_y, h = 0.85f;
    if (H.over) {
        if (H.eagle_t >= 0.55f) return;
        x = H.death_x; y = H.death_y;
    } else {
        h = 0.85f - H.hop_z * 0.4f;
    }
    shadow_box(c, x, y, 0.52f, 0.48f, h, gz);
}

static void fx_draw(PA_Canvas *c, const Fx *f) {
    float k = pa_clamp01(f->life / f->max * 2.5f);
    float s = f->s * k;
    if (s < 0.01f) return;
    box3(c, f->x, f->y, f->z, s, s, s, f->col);
}

static void build_drawables(int far, int near) {
    g_draw_n = 0;
    for (int i = far; i >= near; i--) {
        Row *r = row_at(i);
        if (!r) continue;
        if (r->type == ROW_GRASS) {
            int lo = (int)floorf(H.cam_col) - L.wide, hi = (int)ceilf(H.cam_col) + L.wide;
            for (int t = lo; t <= hi; t++) {
                int kind; float hv;
                if (prop_at(r, t, &kind, &hv)) push(DR_PROP, i, t, (float)t, (float)i, 0.0f);
                else if (deco_at(r, t)) push(DR_DECO, i, t, (float)t, (float)i, -0.05f);
            }
        } else if (r->type == ROW_ROAD) {
            for (int m = 0; m < r->mover_count; m++) push(DR_CAR, i, m, r->movers[m].x, (float)i, 0.0f);
        } else if (r->type == ROW_WATER) {
            if (r->pads) {
                for (int cidx = 0; cidx < COLS; cidx++)
                    if (r->pad[cidx]) push(DR_PAD, i, cidx - HALF, (float)(cidx - HALF), (float)i, -0.6f);
            } else {
                for (int m = 0; m < r->mover_count; m++) push(DR_LOG, i, m, r->movers[m].x, (float)i, -0.6f);
            }
        } else if (r->type == ROW_RAIL) {
            int side = (hash32((uint32_t)r->index) & 1) ? 1 : -1;
            push(DR_SIGNAL, i, side, (float)side * SIGNAL_COL, (float)i + 0.42f, 0.0f);
            if (r->train_on) push(DR_TRAIN, i, 0, r->train_x, (float)i, 0.0f);
        }
        if (r->coin != -99) push(DR_COIN, i, 0, (float)r->coin, (float)i, 0.05f);
    }
    if (H.over && H.death == DEATH_EAGLE) {
        float ex, er, ez;
        eagle_pos(&ex, &er, &ez);
        push(DR_EAGLE, 0, 0, ex, er, 3.0f);
        if (H.eagle_t < 0.55f) push(DR_PLAYER, 0, 0, H.death_x, H.death_y, 0.3f);
        else push(DR_PLAYER, 0, 0, ex, er, 2.9f);
    } else if (H.over) {
        push(DR_PLAYER, 0, 0, H.death_x, H.death_y, H.death == DEATH_CAR ? -0.3f : 0.3f);
    } else {
        push(DR_PLAYER, 0, 0, H.draw_x, H.draw_y, 0.3f);
    }
    for (int k = 0; k < H.nfx; k++) push(DR_FX, 0, k, H.fx[k].x, H.fx[k].y, 0.4f);
    qsort(g_draw, (size_t)g_draw_n, sizeof(Drawable), draw_cmp);
}

static void draw_shadows(PA_Canvas *c) {
    mask_begin(c);
    for (int k = 0; k < g_draw_n; k++) {
        Drawable *d = &g_draw[k];
        Row *r = row_at(d->row);
        switch (d->kind) {
            case DR_PROP: {
                int kind; float hv;
                if (r && prop_at(r, d->idx, &kind, &hv)) prop_shadow(c, kind, d->x, (float)d->row, hv, Z_GRASS);
                break;
            }
            case DR_CAR:   if (r) vehicle_shadow(c, r, &r->movers[d->idx]); break;
            case DR_LOG:
                if (r) shadow_box(c, r->movers[d->idx].x, (float)d->row, r->movers[d->idx].len, 0.68f, 0.22f, Z_WATER);
                break;
            case DR_TRAIN: if (r) train_shadow(c, r); break;
            case DR_SIGNAL: if (r) shadow_box(c, d->x, (float)d->row + 0.42f, 0.12f, 0.12f, 1.3f, 0.0f); break;
            case DR_COIN:  if (r) shadow_box(c, d->x, (float)d->row, 0.30f, 0.10f, 0.7f, ground_z(r)); break;
            case DR_PLAYER: player_shadow(c); break;
            case DR_EAGLE: {
                float ex, er, ez;
                eagle_pos(&ex, &er, &ez);
                Row *gr = row_at((int)floorf(er + 0.5f));
                float gz = ground_z(gr);
                shadow_box(c, ex - ez * 0.4f, er + ez * 0.18f, 3.0f, 0.7f, 0.2f, gz);
                break;
            }
            default: break;
        }
    }
    mask_apply(c, 0.36f);
}

static void draw_objects(PA_Canvas *c) {
    for (int k = 0; k < g_draw_n; k++) {
        Drawable *d = &g_draw[k];
        Row *r = row_at(d->row);
        switch (d->kind) {
            case DR_PROP: {
                int kind; float hv;
                if (r && prop_at(r, d->idx, &kind, &hv)) draw_prop(c, kind, d->x, (float)d->row, hv, Z_GRASS);
                break;
            }
            case DR_DECO: {
                int kind = r ? deco_at(r, d->idx) : 0;
                if (kind) draw_deco(c, kind, d->x, (float)d->row, Z_GRASS,
                                    hash32(row_seed(d->row) + (uint32_t)d->idx * 13u));
                break;
            }
            case DR_CAR:    if (r) draw_vehicle(c, r, &r->movers[d->idx]); break;
            case DR_LOG:    if (r) draw_log(c, r, &r->movers[d->idx]); break;
            case DR_PAD:    if (r) draw_pad(c, r, d->idx); break;
            case DR_TRAIN:  if (r) draw_train(c, r); break;
            case DR_SIGNAL: if (r) draw_signal(c, r, (float)d->idx); break;
            case DR_COIN:
                if (r) draw_coin_at(c, (float)r->coin, (float)r->index,
                                    stand_z(r) + 0.22f + sinf(H.time * 4.0f + (float)r->index) * 0.05f);
                break;
            case DR_PLAYER: draw_player(c); break;
            case DR_FX:     if (d->idx < H.nfx) fx_draw(c, &H.fx[d->idx]); break;
            case DR_EAGLE: {
                float ex, er, ez;
                eagle_pos(&ex, &er, &ez);
                draw_eagle(c, ex, er, ez);
                break;
            }
        }
    }
}

/* -------------------------------------------------------------------- UI -- */
static void coin_glyph(PA_Canvas *c, float x, float y, float s) {
    pa_round_rect(c, x - s * 0.10f, y - s * 0.10f, s * 1.20f, s * 1.20f, s * 0.18f, PA_RGB(16, 14, 24));
    pa_round_rect(c, x, y, s, s, s * 0.12f, pa_hex(0xFFD21F));
    pa_text_bold(c, "C", x + s * 0.5f, y + s * 0.18f, s * 0.64f, pa_hex(0xE8402E), pa_hex(0xE8402E),
                 PA_ALIGN_CENTER, 0.0f, 0.4f);
}

static void ui_block(PA_Canvas *c, Box b, PA_Color face, float lift) {
    float r = b.h * 0.16f;
    pa_round_rect(c, b.x - 3.0f, b.y - 3.0f, b.w + 6.0f, b.h + lift + 6.0f, r + 3.0f, PA_RGB(20, 18, 30));
    pa_round_rect(c, b.x, b.y + lift, b.w, b.h, r, pa_shade(face, -0.35f));
    pa_round_rect(c, b.x, b.y, b.w, b.h, r, face);
    pa_round_rect(c, b.x + b.w * 0.06f, b.y + b.h * 0.07f, b.w * 0.88f, b.h * 0.22f, r * 0.6f,
                  pa_alpha(PA_RGB(255, 255, 255), 0.22f));
}

/* Character portrait inside a UI box, via a temporary camera. */
static void portrait(PA_Canvas *c, int idx, float cx, float cy, float tile, int facing, int silhouette) {
    View saved = V;
    view_setup(&V, tile, cx, cy);
    draw_critter(c, idx, 0.0f, 0.0f, 0.0f, facing, 1.0f, 1.0f, silhouette ? 1 : 0);
    V = saved;
}

static void gift_icon(PA_Canvas *c, float cx, float cy, float s) {
    pa_round_rect(c, cx - s * 0.5f, cy - s * 0.25f, s, s * 0.65f, s * 0.06f, pa_hex(0xE8402E));
    pa_round_rect(c, cx - s * 0.58f, cy - s * 0.45f, s * 1.16f, s * 0.26f, s * 0.06f, pa_hex(0xF25A4A));
    pa_fill_rect(c, cx - s * 0.08f, cy - s * 0.45f, s * 0.16f, s * 0.85f, pa_hex(0xFFD21F));
}

static void draw_prize_button(PA_Canvas *c, Box b) {
    int ready = g_coins >= PRIZE_COST && locked_count() > 0;
    float pulse = ready ? 1.0f + sinf(H.time * 6.0f) * 0.04f : 1.0f;
    Box bb = { b.x + b.w * (1.0f - pulse) * 0.5f, b.y + b.h * (1.0f - pulse) * 0.5f, b.w * pulse, b.h * pulse };
    ui_block(c, bb, ready ? pa_hex(0xFFB020) : pa_hex(0xE8E4F0), 6.0f);
    gift_icon(c, bb.x + bb.w * 0.5f, bb.y + bb.h * 0.42f, bb.w * 0.42f);
    char buf[24];
    if (locked_count() == 0) snprintf(buf, sizeof(buf), "ALL");
    else if (ready) snprintf(buf, sizeof(buf), "FREE!");
    else snprintf(buf, sizeof(buf), "%d/%d", g_coins, PRIZE_COST);
    float s = ui_scale();
    pa_text_bold(c, buf, bb.x + bb.w * 0.5f, bb.y + bb.h * 0.72f, 15.0f * s, PA_RGB(255, 255, 255),
                 PA_RGB(20, 18, 30), PA_ALIGN_CENTER, 1.0f, 1.4f);
}

static void draw_char_button(PA_Canvas *c, Box b) {
    ui_block(c, b, pa_hex(0xF4F6FA), 6.0f);
    portrait(c, g_char, b.x + b.w * 0.44f, b.y + b.h * 0.80f, b.w * 0.62f, 1, 0);
}

static void draw_hud(PA_Canvas *c) {
    float s = ui_scale(), W = (float)c->w;
    char buf[48];
    if (!H.title) {
        snprintf(buf, sizeof(buf), "%d", H.score);
        pa_text_bold(c, buf, 18.0f * s, 16.0f * s, 50.0f * s, PA_RGB(255, 255, 255), PA_RGB(16, 14, 24),
                     PA_ALIGN_LEFT, 4.0f * s, 2.4f);
        snprintf(buf, sizeof(buf), "TOP %d", g_best > H.score ? g_best : H.score);
        pa_text_bold(c, buf, 20.0f * s, 78.0f * s, 20.0f * s, PA_RGB(255, 255, 255), PA_RGB(16, 14, 24),
                     PA_ALIGN_LEFT, 2.0f * s, 1.6f);
    }
    float gs = 30.0f * s;
    float gx = W - 18.0f * s - gs, gy = 22.0f * s;
    coin_glyph(c, gx, gy, gs);
    snprintf(buf, sizeof(buf), "%d", g_coins);
    pa_text_bold(c, buf, gx - 12.0f * s, 18.0f * s, 38.0f * s, pa_hex(0xFFD400), PA_RGB(16, 14, 24),
                 PA_ALIGN_RIGHT, 3.0f * s, 2.3f);
    /* Pause under the coin counter, as in the reference. */
    pa_hub_pause_anchor(gx + gs * 0.5f - 6.0f * s, gy + gs + 40.0f * s, 22.0f * s);
}

static void draw_logo(PA_Canvas *c) {
    float s = ui_scale() * (c->w > c->h ? 0.62f : 1.0f), W = (float)c->w;
    float y = (float)c->h * (c->w > c->h ? 0.05f : 0.11f);
    float drop = sinf(pa_clamp01(H.time * 2.0f) * PA_PI * 0.5f);
    y -= (1.0f - drop) * 120.0f * s;
    struct { const char *t; float size; uint32_t fill, side; } lines[2] = {
        { "ROAD", 92.0f * s, 0xFFFFFF, 0xE8402E },
        { "HOPPER", 74.0f * s, 0xFFD400, 0xE07A10 }
    };
    for (int l = 0; l < 2; l++) {
        float ly = y + (l ? 104.0f * s : 0.0f);
        float sz = lines[l].size;
        float tr = sz * 0.10f;
        pa_text_bold(c, lines[l].t, W * 0.5f + 8.0f * 0.6f * s, ly + 8.0f * 1.6f * s, sz,
                     PA_RGB(16, 14, 24), PA_RGB(16, 14, 24), PA_ALIGN_CENTER, tr, 2.8f);
        for (int k = 7; k >= 1; k--)
            pa_text_bold(c, lines[l].t, W * 0.5f + (float)k * 0.6f * s, ly + (float)k * 1.6f * s, sz,
                         pa_hex(lines[l].side), pa_shade(pa_hex(lines[l].side), -0.25f), PA_ALIGN_CENTER, tr, 2.8f);
        pa_text_bold(c, lines[l].t, W * 0.5f, ly, sz, pa_hex(lines[l].fill), PA_RGB(16, 14, 24),
                     PA_ALIGN_CENTER, tr, 2.8f);
    }
}

static void draw_title(PA_Canvas *c) {
    float s = ui_scale();
    draw_logo(c);
    TitleUI u = title_ui();
    draw_char_button(c, u.chr);
    draw_prize_button(c, u.prize);
    float a = 0.65f + 0.35f * sinf(H.time * 4.0f);
    char buf[32];
    snprintf(buf, sizeof(buf), "TOP %d", g_best);
    float logo_s = c->w > c->h ? 0.62f : 1.0f;
    pa_text_bold(c, buf, (float)c->w * 0.5f, (float)c->h * (c->w > c->h ? 0.05f : 0.11f) + 214.0f * s * logo_s, 24.0f * s,
                 PA_RGB(255, 255, 255), PA_RGB(16, 14, 24), PA_ALIGN_CENTER, 2.0f * s, 1.8f);
    pa_text_bold(c, "TAP TO HOP", (float)c->w * 0.5f, u.chr.y + u.chr.h * 0.36f, 24.0f * s,
                 pa_alpha(PA_RGB(255, 255, 255), a), PA_RGBA(16, 14, 24, (int)(a * 255.0f)),
                 PA_ALIGN_CENTER, 3.0f * s, 1.8f);
}

static const char *death_line(void) {
    switch (H.death) {
        case DEATH_CAR:   return "SQUASHED!";
        case DEATH_TRAIN: return "DERAILED!";
        case DEATH_WATER: return "SPLASH!";
        default:          return "EAGLE SNACK!";
    }
}

static void draw_results(PA_Canvas *c) {
    float t = H.death_t - RESULTS_AT;
    if (t < 0.0f) return;
    float s = ui_scale();
    float k = pa_smooth(pa_clamp01(t / 0.35f));
    ResultsUI u = results_ui();
    float off = (1.0f - k) * (float)c->h * 0.6f;

    pa_fill_rect(c, 0, 0, (float)c->w, (float)c->h, PA_RGBA(12, 10, 30, (int)(k * 96.0f)));

    Box card = u.card;
    card.y += off;
    pa_round_rect(c, card.x - 4.0f, card.y - 4.0f, card.w + 8.0f, card.h + 16.0f, 30.0f * s, PA_RGB(20, 18, 30));
    pa_round_rect(c, card.x, card.y + 8.0f, card.w, card.h, 26.0f * s, pa_hex(0x1E2550));
    pa_round_rect(c, card.x, card.y, card.w, card.h, 26.0f * s, pa_hex(0x34407A));
    pa_round_rect(c, card.x + 10.0f * s, card.y + 10.0f * s, card.w - 20.0f * s, card.h * 0.52f, 20.0f * s,
                  pa_hex(0x3D4B8E));

    /* Ribbon across the top of the card. */
    float rw = card.w * 0.78f, rh = 54.0f * s;
    float rx = card.x + (card.w - rw) * 0.5f, ry = card.y - rh * 0.5f;
    pa_round_rect(c, rx - 3.0f, ry - 3.0f, rw + 6.0f, rh + 6.0f, 12.0f * s, PA_RGB(20, 18, 30));
    pa_round_rect(c, rx, ry, rw, rh, 10.0f * s, pa_hex(0xE8402E));
    pa_text_bold(c, death_line(), card.x + card.w * 0.5f, ry + rh * 0.20f, 30.0f * s, PA_RGB(255, 255, 255),
                 PA_RGB(20, 18, 30), PA_ALIGN_CENTER, 2.5f * s, 2.0f);

    pa_text_bold(c, "SCORE", card.x + card.w * 0.5f, card.y + 40.0f * s, 18.0f * s, pa_hex(0xB8C2EA),
                 pa_hex(0x34407A), PA_ALIGN_CENTER, 3.0f * s, 1.2f);
    float pop = 1.0f;
    float pt = t - 0.25f;
    if (pt < 0.0f) pop = 0.0f;
    else if (pt < 0.12f) pop = pa_lerpf(0.4f, 1.3f, pt / 0.12f);
    else if (pt < 0.30f) pop = pa_lerpf(1.3f, 1.0f, pa_smooth((pt - 0.12f) / 0.18f));
    char buf[48];
    if (pop > 0.0f) {
        float sz = 84.0f * s * pop;
        snprintf(buf, sizeof(buf), "%d", H.score);
        pa_text_bold(c, buf, card.x + card.w * 0.5f, card.y + 136.0f * s - sz * 0.5f, sz,
                     PA_RGB(255, 255, 255), PA_RGB(20, 18, 30), PA_ALIGN_CENTER, 5.0f * s * pop, 2.6f);
    }
    if (H.new_top) {
        float bw = 170.0f * s, bh = 40.0f * s;
        float wob = sinf(H.time * 8.0f) * 0.06f + 1.0f;
        pa_round_rect(c, card.x + (card.w - bw * wob) * 0.5f, card.y + 210.0f * s, bw * wob, bh, 10.0f * s,
                      pa_hex(0xFFD400));
        pa_text_bold(c, "NEW TOP!", card.x + card.w * 0.5f, card.y + 218.0f * s, 24.0f * s,
                     PA_RGB(255, 255, 255), pa_hex(0xE8402E), PA_ALIGN_CENTER, 2.0f * s, 1.8f);
    } else {
        snprintf(buf, sizeof(buf), "TOP %d", g_best);
        pa_text_bold(c, buf, card.x + card.w * 0.5f, card.y + 216.0f * s, 24.0f * s,
                     pa_hex(0xFFFFFF), PA_RGB(20, 18, 30), PA_ALIGN_CENTER, 2.0f * s, 1.6f);
    }
    /* Coins collected this run against the bank. */
    float cy = card.y + 266.0f * s;
    coin_glyph(c, card.x + card.w * 0.5f - 74.0f * s, cy, 24.0f * s);
    snprintf(buf, sizeof(buf), "+%d  TOTAL %d", H.run_coins, g_coins);
    pa_text_bold(c, buf, card.x + card.w * 0.5f - 40.0f * s, cy + 2.0f * s, 20.0f * s, pa_hex(0xFFD400),
                 PA_RGB(20, 18, 30), PA_ALIGN_LEFT, 1.5f * s, 1.6f);

    /* Buttons. */
    Box play = u.play, chr = u.chr, prize = u.prize;
    play.y += off * 1.2f; chr.y += off * 1.3f; prize.y += off * 1.3f;
    float breathe = 1.0f + sinf(H.time * 5.0f) * 0.025f;
    Box pb = { play.x - play.w * (breathe - 1.0f) * 0.5f, play.y, play.w * breathe, play.h };
    ui_block(c, pb, pa_hex(0x4CD964), 9.0f);
    float tx = pb.x + pb.w * 0.5f, ty = pb.y + pb.h * 0.5f, ts = pb.h * 0.28f;
    PA_Vec2 tri_o[3] = { { tx - ts * 0.8f - 4, ty - ts - 5 }, { tx + ts * 1.1f + 6, ty }, { tx - ts * 0.8f - 4, ty + ts + 5 } };
    PA_Vec2 tri[3] = { { tx - ts * 0.8f, ty - ts }, { tx + ts * 1.1f, ty }, { tx - ts * 0.8f, ty + ts } };
    pa_fill_poly(c, tri_o, 3, PA_RGB(20, 18, 30));
    pa_fill_poly(c, tri, 3, PA_RGB(255, 255, 255));
    draw_char_button(c, chr);
    draw_prize_button(c, prize);
}

static void draw_select(PA_Canvas *c) {
    float s = ui_scale(), W = (float)c->w, Hh = (float)c->h;
    float k = pa_smooth(H.select_anim);
    PA_Paint bg = pa_linear(0, 0, 0, Hh);
    pa_stop(&bg, 0.0f, PA_RGBA(40, 70, 140, (int)(k * 240.0f)));
    pa_stop(&bg, 1.0f, PA_RGBA(20, 24, 60, (int)(k * 250.0f)));
    pa_fill_rect_paint(c, 0, 0, W, Hh, &bg);
    /* Light rays behind the featured character. */
    float cx = W * 0.5f, cy = Hh * 0.44f;
    for (int i = 0; i < 12; i++) {
        float a0 = (float)i * PA_TAU / 12.0f + H.time * 0.25f, a1 = a0 + 0.13f;
        PA_Vec2 q[3] = { { cx, cy }, { cx + cosf(a0) * W, cy + sinf(a0) * W }, { cx + cosf(a1) * W, cy + sinf(a1) * W } };
        pa_fill_poly(c, q, 3, PA_RGBA(255, 255, 255, (int)(k * 16.0f)));
    }
    pa_text_bold(c, "CHARACTERS", W * 0.5f, Hh * 0.08f, 44.0f * s, PA_RGB(255, 255, 255), PA_RGB(16, 14, 24),
                 PA_ALIGN_CENTER, 3.0f * s, 2.4f);
    int have = 0;
    for (int i = 0; i < CHAR_COUNT; i++) if (g_unlocked & (1 << i)) have++;
    char buf[48];
    snprintf(buf, sizeof(buf), "%d / %d COLLECTED", have, CHAR_COUNT);
    pa_text_bold(c, buf, W * 0.5f, Hh * 0.08f + 58.0f * s, 20.0f * s, pa_hex(0xFFD400), PA_RGB(16, 14, 24),
                 PA_ALIGN_CENTER, 2.0f * s, 1.6f);

    /* Carousel: every character on its own world tile. */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < CHAR_COUNT; i++) {
            float d = (float)i - H.select_scroll;
            float ad = fabsf(d);
            if (ad > 3.2f) continue;
            int centre = ad < 0.5f;
            if ((pass == 1) != centre) continue;
            float scale = centre ? 1.0f : pa_lerpf(0.50f, 0.36f, pa_clamp01(ad - 1.0f));
            float tile = W * 0.40f * scale;
            float px = W * 0.5f + d * W * 0.36f;
            float py = cy + (centre ? 0.0f : -20.0f * s) + tile * 0.30f;
            int locked = !(g_unlocked & (1 << i));
            View saved = V;
            view_setup(&V, tile, px, py);
            const World *wd = &WORLDS[CHARS[i].world];
            box3x(c, 0.0f, 0.0f, -0.30f, 1.0f, 1.0f, 0.30f, pa_hex(wd->grass_a), 0.06f);
            int face = centre ? ((int)(H.time * 0.8f) % 4) : 2;
            if (centre) face = (face == 0) ? 1 : face;
            draw_critter(c, i, 0.0f, 0.0f, 0.0f, face, 1.0f, 1.0f + (centre ? sinf(H.time * 6.0f) * 0.03f : 0.0f),
                         locked ? 1 : 0);
            V = saved;
        }
    }
    int sel = H.select_idx;
    int locked = !(g_unlocked & (1 << sel));
    pa_text_bold(c, locked ? "? ? ?" : CHARS[sel].name, W * 0.5f, Hh * 0.635f, 40.0f * s,
                 PA_RGB(255, 255, 255), PA_RGB(16, 14, 24), PA_ALIGN_CENTER, 3.0f * s, 2.2f);
    snprintf(buf, sizeof(buf), "%s WORLD", WORLDS[CHARS[sel].world].name);
    pa_text_bold(c, buf, W * 0.5f, Hh * 0.635f + 52.0f * s, 20.0f * s, pa_hex(WORLDS[CHARS[sel].world].grass_a),
                 PA_RGB(16, 14, 24), PA_ALIGN_CENTER, 2.0f * s, 1.6f);
    SelectUI u = select_ui();
    ui_block(c, u.play, locked ? pa_hex(0x8A90A8) : pa_hex(0x4CD964), 8.0f);
    pa_text_bold(c, locked ? "LOCKED" : "PLAY", u.play.x + u.play.w * 0.5f, u.play.y + u.play.h * 0.26f,
                 40.0f * s, PA_RGB(255, 255, 255), PA_RGB(16, 14, 24), PA_ALIGN_CENTER, 3.0f * s, 2.2f);
    if (locked)
        pa_text_bold(c, "WIN IT FROM THE PRIZE BOX", W * 0.5f, u.play.y + u.play.h + 26.0f * s, 16.0f * s,
                     PA_RGB(255, 255, 255), PA_RGB(16, 14, 24), PA_ALIGN_CENTER, 1.5f * s, 1.4f);
    for (int side = -1; side <= 1; side += 2) {
        Box b = side < 0 ? u.left : u.right;
        float ax = b.x + b.w * 0.5f, ay = b.y + b.h * 0.5f, as = b.w * 0.30f;
        PA_Vec2 tri[3] = { { ax - side * as, ay - as * 1.3f }, { ax + side * as, ay }, { ax - side * as, ay + as * 1.3f } };
        PA_Vec2 tro[3] = { { ax - side * (as + 5), ay - as * 1.3f - 7 }, { ax + side * (as + 7), ay },
                           { ax - side * (as + 5), ay + as * 1.3f + 7 } };
        pa_fill_poly(c, tro, 3, PA_RGB(16, 14, 24));
        pa_fill_poly(c, tri, 3, PA_RGB(255, 255, 255));
    }
}

static void draw_reveal(PA_Canvas *c) {
    float s = ui_scale(), W = (float)c->w, Hh = (float)c->h;
    int idx = H.reveal - 1;
    float k = pa_smooth(pa_clamp01(H.reveal_t * 3.0f));
    pa_fill_rect(c, 0, 0, W, Hh, PA_RGBA(255, 196, 40, (int)(k * 235.0f)));
    float cx = W * 0.5f, cy = Hh * 0.46f;
    for (int i = 0; i < 16; i++) {
        float a0 = (float)i * PA_TAU / 16.0f + H.time * 0.6f, a1 = a0 + 0.12f;
        PA_Vec2 q[3] = { { cx, cy }, { cx + cosf(a0) * Hh, cy + sinf(a0) * Hh }, { cx + cosf(a1) * Hh, cy + sinf(a1) * Hh } };
        pa_fill_poly(c, q, 3, PA_RGBA(255, 255, 255, (int)(k * 60.0f)));
    }
    pa_text_bold(c, "NEW CHARACTER!", W * 0.5f, Hh * 0.14f, 44.0f * s, PA_RGB(255, 255, 255), PA_RGB(16, 14, 24),
                 PA_ALIGN_CENTER, 3.0f * s, 2.4f);
    float pop = pa_clamp01(H.reveal_t * 2.5f);
    float tile = W * 0.48f * (pop < 1.0f ? pa_lerpf(0.2f, 1.1f, pop) : 1.0f + sinf(H.time * 5.0f) * 0.02f);
    View saved = V;
    view_setup(&V, tile, cx, cy + tile * 0.25f);
    const World *wd = &WORLDS[CHARS[idx].world];
    box3x(c, 0.0f, 0.0f, -0.30f, 1.2f, 1.2f, 0.30f, pa_hex(wd->grass_a), 0.06f);
    draw_critter(c, idx, 0.0f, 0.0f, 0.0f, ((int)(H.time * 2.0f)) % 4, 1.0f, 1.0f, 0);
    V = saved;
    pa_text_bold(c, CHARS[idx].name, W * 0.5f, Hh * 0.66f, 48.0f * s, PA_RGB(255, 255, 255), PA_RGB(16, 14, 24),
                 PA_ALIGN_CENTER, 3.0f * s, 2.4f);
    char buf[40];
    snprintf(buf, sizeof(buf), "UNLOCKS THE %s", WORLDS[CHARS[idx].world].name);
    pa_text_bold(c, buf, W * 0.5f, Hh * 0.66f + 62.0f * s, 20.0f * s, PA_RGB(255, 255, 255), PA_RGB(16, 14, 24),
                 PA_ALIGN_CENTER, 2.0f * s, 1.6f);
    if (H.reveal_t > 0.8f)
        pa_text_bold(c, "TAP TO PLAY", W * 0.5f, Hh * 0.84f, 28.0f * s, PA_RGB(255, 255, 255), PA_RGB(16, 14, 24),
                     PA_ALIGN_CENTER, 3.0f * s, 2.0f);
}

static void hopper_render(PA_Canvas *c) {
    if (L.w != c->w || L.h != c->h) compute_layout(c->w, c->h);


    V.cam_col = H.cam_col;
    V.cam_row = H.cam_y;
    float base_cx = (float)c->w * 0.5f, base_cy = L.cy;
    V.cx = base_cx; V.cy = base_cy;
    if (H.shake > 0.0f) {
        float a = H.shake / 0.2f;
        V.cx += sinf(H.time * 91.0f) * 7.0f * a;
        V.cy += cosf(H.time * 77.0f) * 6.0f * a;
    }

    const World *W = WD();
    pa_clear(c, pa_shade(pa_hex(W->grass_a), -0.25f));

    int far = (int)ceilf(H.cam_y) + L.ahead;
    int near = (int)floorf(H.cam_y) - L.behind;
    for (int i = far; i >= near; i--) {
        Row *r = row_at(i);
        if (r) draw_ground(c, r);
    }
    build_drawables(far, near);
    draw_shadows(c);
    draw_objects(c);

    V.cx = base_cx; V.cy = base_cy;

    if (H.idle > IDLE_LIMIT - 2.5f && !H.over && H.started) {
        float warn = pa_clamp01((H.idle - (IDLE_LIMIT - 2.5f)) / 2.5f);
        pa_vignette(c, warn * 0.8f);
    }

    if (H.reveal) { pa_hub_hide_pause(); draw_reveal(c); return; }
    if (H.select_open) { pa_hub_hide_pause(); draw_select(c); return; }

    draw_hud(c);
    if (H.title) draw_title(c);
    if (H.over && H.death_t >= RESULTS_AT) { pa_hub_hide_pause(); draw_results(c); }
}

/* -------------------------------------------------------------- thumbnail -- */
static void hopper_thumb(PA_Canvas *c, float x, float y, float w, float h, float t) {
    int sx0 = c->clip_x0, sy0 = c->clip_y0, sx1 = c->clip_x1, sy1 = c->clip_y1;
    int cx0 = (int)x > sx0 ? (int)x : sx0, cy0 = (int)y > sy0 ? (int)y : sy0;
    int cx1 = (int)(x + w) < sx1 ? (int)(x + w) : sx1, cy1 = (int)(y + h) < sy1 ? (int)(y + h) : sy1;
    if (cx1 <= cx0 || cy1 <= cy0) return;
    c->clip_x0 = cx0; c->clip_y0 = cy0; c->clip_x1 = cx1; c->clip_y1 = cy1;

    View saved = V;
    int saved_world = H.world;
    float saved_time = H.time;
    H.world = W_CLASSIC;
    H.time = t;
    view_setup(&V, fminf(w / 5.0f, h / 3.0f), x + w * 0.5f, y + h * 0.62f);
    const World *Wd = &WORLDS[W_CLASSIC];
    pa_fill_rect(c, x, y, w, h, pa_hex(Wd->grass_a));
    int types[7] = { ROW_GRASS, ROW_GRASS, ROW_ROAD, ROW_ROAD, ROW_GRASS, ROW_WATER, ROW_GRASS };
    for (int i = 5; i >= -2; i--) {
        int ty = types[(i + 2) % 7];
        PA_Color col = ty == ROW_ROAD ? pa_hex(Wd->road) : ty == ROW_WATER ? pa_hex(Wd->water)
                     : pa_hex((i & 1) ? Wd->grass_b : Wd->grass_a);
        float z = ty == ROW_GRASS ? Z_GRASS : ty == ROW_WATER ? Z_WATER : 0.0f;
        quad_top(c, -6.0f, 6.0f, (float)i - 0.5f, (float)i + 0.5f, z, col);
        if (ty == ROW_GRASS) {
            PA_Vec2 q[4] = { project(-6, (float)i - 0.5f, z), project(6, (float)i - 0.5f, z),
                             project(6, (float)i - 0.5f, 0), project(-6, (float)i - 0.5f, 0) };
            pa_fill_poly(c, q, 4, pa_shade(pa_hex(Wd->grass_a), -0.34f));
        }
        if (ty == ROW_ROAD && types[(i + 3) % 7] == ROW_ROAD)
            for (float k = -6.0f; k < 6.0f; k += 1.0f)
                quad_top(c, k + 0.25f, k + 0.75f, (float)i + 0.465f, (float)i + 0.535f, 0.0f, pa_hex(Wd->dash));
    }
    g_mask_on = 0;
    Row fake;
    memset(&fake, 0, sizeof(fake));
    fake.index = 1; fake.dir = 1; fake.type = ROW_ROAD;
    Mover car = { pa_wrapf(t * 1.6f, 8.0f) - 4.0f, 1.3f, V_SEDAN, 0xEF5B3A };
    Mover truck = { 4.0f - pa_wrapf(t * 1.2f + 3.0f, 10.0f), 2.7f, V_TRUCK, 0x2FA4E7 };
    draw_prop(c, P_TREE, -1.6f, 4.0f, 0.8f, Z_GRASS);
    draw_prop(c, P_BUSH, 1.4f, 4.0f, 0.3f, Z_GRASS);
    Row fake2 = fake; fake2.index = 0; fake2.dir = -1;
    draw_vehicle(c, &fake2, &truck);
    draw_vehicle(c, &fake, &car);
    draw_prop(c, P_TREE, 1.7f, -1.0f, 0.5f, Z_GRASS);
    float hop = fabsf(sinf(t * 3.0f));
    draw_critter(c, 0, -0.2f, -1.0f, Z_GRASS + hop * 0.35f, 0, 1.0f, 1.0f + hop * 0.1f, 0);
    draw_prop(c, P_ROCK, -1.8f, -2.0f, 0.3f, Z_GRASS);

    V = saved;
    H.world = saved_world;
    H.time = saved_time;
    c->clip_x0 = sx0; c->clip_y0 = sy0; c->clip_x1 = sx1; c->clip_y1 = sy1;
}

const PA_Game PA_GAME_ROADHOPPER = {
    "roadhopper", "Road Hopper", "Endless Hopper",
    "Hop across traffic, log rivers and express rails. Collect coins, win new critters, dodge the eagle.",
    PA_RGB(123, 216, 79),
    hopper_start, hopper_stop, hopper_update, hopper_render, hopper_thumb
};
